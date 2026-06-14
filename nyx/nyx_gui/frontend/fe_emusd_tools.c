#include "fe_emusd_tools.h"
#include "../storage/sfd.h"
#include <sec/se.h>
#include <storage/boot_storage.h>
#include <libs/fatfs/ff.h>
#include <storage/emmc.h>
#include <storage/mbr_gpt.h>
#include <storage/sd.h>
#include <storage/sdmmc.h>
#include <utils/list.h>
#include <utils/ini.h>
#include <string.h>
#include "gfx_utils.h"
#include <stdlib.h>
#include <utils/sprintf.h>

void load_emusd_cfg(emusd_cfg_t *emu_info){
	memset(emu_info, 0, sizeof(emusd_cfg_t));

	// Parse emuMMC configuration.
	LIST_INIT(ini_sections);
	if (ini_parse(&ini_sections, "emuSD/emusd.ini", false))
		return;

	LIST_FOREACH_ENTRY(ini_sec_t, ini_sec, &ini_sections, link)
	{
		if (!strcmp(ini_sec->name, "emusd"))
		{
			LIST_FOREACH_ENTRY(ini_kv_t, kv, &ini_sec->kvs, link)
			{
				if (!strcmp("enabled",     kv->key))
					emu_info->enabled = atoi(kv->val);
				else if(!strcmp("sector", kv->key))
					emu_info->sector = strtol(kv->val, NULL, 16);
				else if(!strcmp("path", kv->key)){
					emu_info->path =(char*)malloc(strlen(kv->val)+1);
					strcpy(emu_info->path, kv->val);
				}
			}

			break;
		}
	}

	ini_free(&ini_sections);
}

void save_emusd_cfg(u32 part_idx, u32 sector_start, const char *path, u8 drive){
	boot_storage_mount();

	char lbuf[16];
	FIL fp;

	f_mkdir("emuSD");

	if (f_open(&fp, "emuSD/emusd.ini", FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
		return;

	// Add config entry.
	f_puts("[emusd]\nenabled=", &fp);
	if((sector_start || path) && drive == DRIVE_SD) {
		f_puts("1", &fp);
	} else if((sector_start || path) && drive == DRIVE_EMMC){
		f_puts("4", &fp);
	}else{
		f_puts("0", &fp);
	}

	if(!sector_start){
		f_puts("\nsector=0x0", &fp);
	}else{
		f_puts("\nsector=0x", &fp);
		itoa(sector_start, lbuf, 16);
		f_puts(lbuf, &fp);
	}

	if(path){
		f_puts("\npath=", &fp);
		f_puts(path, &fp);
	}

	f_puts("\n", &fp);
	f_close(&fp);
}

static void update_base_path(char *path, int idx, int path_len) {
	if(idx < 10){
		path[path_len] = '0';
		itoa(idx, path + path_len + 1, 10);
	}else{
		itoa(idx, path + path_len, 10);
	}
}

int create_emusd_file(u32 size_sct, u8 drive) {
	static const u32 file_max_scts = 0xfe000000 / 0x200;

	char path[0x80];
	if (drive == DRIVE_SD){
		strcpy(path, "sd:emuSD/SD");
		f_mkdir("sd:emuSD");
	}else {
		strcpy(path, "emmc:emuSD/EMMC");
		f_mkdir("emmc:emuSD");
	}
	int path_len = strlen(path);

	f_mkdir("emuSD");

	for(int j = 0; j < 100; j++){
		update_base_path(path, j, path_len);
		if(f_stat(path, NULL) == FR_NO_FILE){
			break;
		}
	}

	f_mkdir(path);
	f_mkdir(path + (drive == DRIVE_SD ? 3 : 5));

	u32 base_path_len = strlen(path);

	strcat(path, "/SD");
	f_mkdir(path);

	strcat(path, "/");
	path_len = strlen(path);

	// allocate files for emusd
	u32 sct_left = size_sct;
	u32 idx = 0;

	gfx_printf("sct left 0x%x\n", sct_left);
	while(sct_left) {
		update_base_path(path, idx, path_len);

		u32 scts = MIN(sct_left, file_max_scts);

		FIL f;
		int res = f_open(&f,  path, FA_CREATE_ALWAYS | FA_WRITE);
		if(res != FR_OK) {
			gfx_printf("open fail\n");
			return 0;
		}

		FSIZE_t seek_bytes = (FSIZE_t)scts << 9;
		res = f_lseek(&f, seek_bytes);
		f_close(&f);

		if(res != FR_OK){
			f_unlink(path);
			return 0;
		}

		sct_left -= scts;
		idx++;
	}


	path[path_len] = '\0';

	sfd_file_based_init(path);

	u8 *buf = malloc(SZ_4M);
	u32 cluster_size = 65536;

	int res = f_mkfs("sfd:", FM_FAT32, cluster_size, buf, SZ_4M);

	while (res != FR_OK && cluster_size > 4096){
		cluster_size /= 2;
		res = f_mkfs("sfd:", FM_FAT32, cluster_size, buf, SZ_4M);
	}

	if(res == FR_OK){
		FATFS fs;
		res = f_mount(&fs, "sfd:", 1);

		if(res == FR_OK){
			gfx_printf("mount fail %d\n", res);
			char label[0x30];
			strcpy(label, "sfd:emusd");
			res = f_setlabel(label);
		}

		f_mount(NULL, "sfd:", 0);
	}

	sfd_end();

	free(buf);

	if(res != FR_OK) {
		return 0;
	}

	path[base_path_len] = '\0';
	s_printf(path + base_path_len, drive == DRIVE_SD ? "/file_based" : "/file_emmc_based");
	FIL f;
	res = f_open(&f, path + (drive == DRIVE_SD ? 3 : 5), FA_WRITE | FA_CREATE_ALWAYS);
	f_close(&f);
	gfx_printf("%s %d\n", path, res);

	path[base_path_len] = '\0';
	save_emusd_cfg(0, 0, path + (drive == DRIVE_SD ? 3 : 5), drive);

	return 1;
}

int create_emusd(int part_idx, u32 sector_mbr, u32 sector_start, u32 sector_size, const char *name, u8 drive){
	sdmmc_storage_t *storage = drive == DRIVE_SD ? &sd_storage : &emmc_storage;

	sfd_init(storage, sector_start, sector_size);

	u8 *buf = malloc(SZ_4M);
	FIL f;
	u32 cluster_size = 65536;

	u32 mkfs_error = f_mkfs("sfd:", FM_FAT32 | FM_SFD, cluster_size, buf, SZ_4M);

	while(mkfs_error != FR_OK && cluster_size > 4096){
		cluster_size /= 2;
		mkfs_error = f_mkfs("sfd:", FM_FAT32 | FM_SFD, cluster_size, buf, SZ_4M);
	}

	if(mkfs_error == FR_OK){
		FATFS fs;
		mkfs_error = f_mount(&fs, "sfd:", 1);

		if(mkfs_error == FR_OK){
			mkfs_error = f_open(&f, "sfd:.no_boot_storage", FA_CREATE_ALWAYS | FA_WRITE);
			f_close(&f);
			
			if(mkfs_error == FR_OK){
				char label[0x30];
				strcpy(label, "sfd:");
				strcat(label, name);
				mkfs_error = f_setlabel(label);

				if(mkfs_error == FR_OK){
					u8 random_number[16];
					mbr_t mbr = {0};
					se_rng_pseudo(random_number, SE_RNG_BLOCK_SIZE);
					memcpy(&mbr.signature, random_number, 4);
					mbr.boot_signature = 0xaa55;
					mbr.partitions[0].start_sct = sector_start - sector_mbr;
					mbr.partitions[0].size_sct = sector_size;
					mbr.partitions[0].type = 0x0c;
				
					mkfs_error = sdmmc_storage_write(storage, sector_mbr, 1, &mbr) ? FR_OK : FR_DISK_ERR;
				}
			}
		}

		f_mount(NULL, "sfd:", 0);
	}

	sfd_end();

	if(mkfs_error != FR_OK){
		return mkfs_error;
	}

	char path[0x80];
	strcpy(path, "emuSD");
	f_mkdir(path);
	s_printf(path + strlen(path), "/RAW_%s%d", drive == DRIVE_SD ? "SD" : "EMMC", part_idx);
	f_mkdir(path);
	strcat(path, drive == DRIVE_SD ? "/raw_based" : "/raw_emmc_based");

	f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE);
	f_write(&f, &sector_mbr, 4, NULL);
	f_close(&f);

	save_emusd_cfg(part_idx, sector_mbr, NULL, drive); 

	return FR_OK;
}