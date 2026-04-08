#include "emusd.h"
#include <bdk.h>
#include <storage/boot_storage.h>
#include <storage/emmc.h>
#include <storage/file_based_storage.h>
#include <storage/sd.h>
#include <storage/sdmmc.h>
#include <string.h>
#include <utils/ini.h>
#include <libs/fatfs/ff.h>
#include "../config.h"
#include "gfx_utils.h"


FATFS emusd_fs;
bool emusd_mounted = false;
bool emusd_initialized = false;
emusd_cfg_t emu_sd_cfg = {0};
extern hekate_config h_cfg;

static bool emusd_get_mounted() {
	return emusd_mounted;
}

void emusd_load_cfg()
{
	emu_sd_cfg.enabled = 0;
	emu_sd_cfg.fs_ver  = 0;
	emu_sd_cfg.id      = 0;
	emu_sd_cfg.sector  = 0;
	if(!emu_sd_cfg.path) {
		emu_sd_cfg.path = (char*)malloc(0x200);
	}
	if(!emu_sd_cfg.emummc_file_based_path){
		emu_sd_cfg.emummc_file_based_path = (char*)malloc(0x200);
	}

	LIST_INIT(ini_sections);
	if(!ini_parse(&ini_sections, "emuSD/emusd.ini", false))
	{
		LIST_FOREACH_ENTRY(ini_sec_t, ini_sec, &ini_sections, link)
		{
			if(ini_sec->type == INI_CHOICE)
			{
				if(strcmp(ini_sec->name, "emusd")){
					continue;
				}

				LIST_FOREACH_ENTRY(ini_kv_t, kv, &ini_sec->kvs, link)
				{
					if(!strcmp(kv->key, "enabled")){
						emu_sd_cfg.enabled = atoi(kv->val);
					} else if(!strcmp(kv->key, "sector")){
						emu_sd_cfg.sector = strtol(kv->val, NULL, 16);
					} else if(!strcmp(kv->key, "path")){
						emu_sd_cfg.path = kv->val;
					}
				}
				break;
			}
		}
	}
}

bool emusd_mount() {
	if (emusd_mounted) {
		return true;
	}

	if (emusd_storage_init_mmc()){
		return false;
	}

	int res = f_mount(&emusd_fs, "emusd:", 1);

	if(res) {
		EPRINTFARGS("emusd mount fail %d", res);
		return false;
	}
	emusd_mounted = true;
	return true;
}

bool emusd_unmount() {
	f_mount(NULL, "emusd:", 1);
	emusd_mounted = false;
	return true;
}

bool emusd_set_path(char *path) {
	FIL fp;
	bool found = false;
	strcpy(emu_sd_cfg.emummc_file_based_path, "");
	strcpy(emu_sd_cfg.emummc_file_based_path, path);
	strcat(emu_sd_cfg.emummc_file_based_path, "/raw_based");
	if(!f_open(&fp, emu_sd_cfg.emummc_file_based_path, FA_READ))
	{
		if(!f_read(&fp, &emu_sd_cfg.sector, 4, NULL)){
			if(emu_sd_cfg.sector){
				found = true;
				emu_sd_cfg.enabled = 1;
				goto out;
			}
		}
	}

	strcpy(emu_sd_cfg.emummc_file_based_path, "");
	strcat(emu_sd_cfg.emummc_file_based_path, path);
	strcat(emu_sd_cfg.emummc_file_based_path, "/raw_emmc_based");
	if (!f_open(&fp, emu_sd_cfg.emummc_file_based_path, FA_READ))
	{
		if (!f_read(&fp, &emu_sd_cfg.sector, 4, NULL)){
			if (emu_sd_cfg.sector){
				found = true;
				emu_sd_cfg.enabled = 4;
				goto out;
			}
		}
	}

	// strcpy(emu_sd_cfg.emummc_file_based_path, "sd:");
	strcpy(emu_sd_cfg.emummc_file_based_path, "");
	strcat(emu_sd_cfg.emummc_file_based_path, path);
	strcat(emu_sd_cfg.emummc_file_based_path, "/file_based");
	if (!f_stat(emu_sd_cfg.emummc_file_based_path, NULL))
	{
		emu_sd_cfg.sector = 0;
		emu_sd_cfg.path = path;
		emu_sd_cfg.enabled = 1;

		found = true;
		goto out;
	}

	// strcpy(emu_sd_cfg.emummc_file_based_path, "sd:");
	strcpy(emu_sd_cfg.emummc_file_based_path, "");
	strcat(emu_sd_cfg.emummc_file_based_path, path);
	strcat(emu_sd_cfg.emummc_file_based_path, "/file_emmc_based");
	if (!f_stat(emu_sd_cfg.emummc_file_based_path, NULL))
	{
		emu_sd_cfg.sector = 0;
		emu_sd_cfg.path = path;
		emu_sd_cfg.enabled = 4;

		found = true;
		goto out;
	}

out:
	return found;
}

int emusd_storage_init_mmc() {
	if(!emusd_initialized) {
		if (!emu_sd_cfg.enabled || h_cfg.emummc_force_disable) {
			if(!sd_initialize(false)){
				return 1;
			}
			emusd_initialized = true;
			return 0;
		}

		bool file_based = false;
		if(emu_sd_cfg.enabled == 4){
			if(!emu_sd_cfg.sector){
				//file based
				if(!emmc_mount()){
					return 1;
				}
				strcpy(emu_sd_cfg.emummc_file_based_path, "emmc:");
				strcat(emu_sd_cfg.emummc_file_based_path, emu_sd_cfg.path);
				strcat(emu_sd_cfg.emummc_file_based_path, "/SD/");
				file_based = true;
			}else{
				if(!emmc_initialize(false)){
					return 1;
				}
			}
		}else{
			if(!emu_sd_cfg.sector){
				//file based
				if(!sd_mount()){
					return 1;
				}
				strcpy(emu_sd_cfg.emummc_file_based_path, "sd:");
				strcat(emu_sd_cfg.emummc_file_based_path, emu_sd_cfg.path);
				strcat(emu_sd_cfg.emummc_file_based_path, "/SD/");
				file_based = true;
			}else{
				if(!sd_initialize(false)){
					return 1;
				}
			}
		}

		if(file_based){
			return file_based_storage_init(emu_sd_cfg.emummc_file_based_path) == 0;
		}

		emusd_initialized = true;
	}
	return 0;
}

int emusd_storage_end() {
	if(!h_cfg.emummc_force_disable && emu_sd_cfg.enabled && !emu_sd_cfg.sector){
		file_based_storage_end();
	}
	if(!emu_sd_cfg.enabled || h_cfg.emummc_force_disable || emu_sd_cfg.enabled == 1){
		sd_unmount();
	}else{
		emmc_unmount();
	}
	emusd_initialized = false;
	return 0;
}

int emusd_storage_write(u32 sector, u32 num_sectors, void *buf) {
	if(!emu_sd_cfg.enabled || h_cfg.emummc_force_disable){
		return sdmmc_storage_write(&sd_storage, sector, num_sectors, buf);
	}else if(emu_sd_cfg.sector){
		sdmmc_storage_t *storage = emu_sd_cfg.enabled == 1 ? &sd_storage : &emmc_storage;
		sector += emu_sd_cfg.sector;
		return sdmmc_storage_write(storage, sector, num_sectors, buf);
	}else{
		return file_based_storage_write(sector, num_sectors, buf);
	}
}

int emusd_storage_read(u32 sector, u32 num_sectors, void *buf) {
	if(!emu_sd_cfg.enabled || h_cfg.emummc_force_disable){
		return sdmmc_storage_read(&sd_storage, sector, num_sectors, buf);
	}else if(emu_sd_cfg.sector){
		sdmmc_storage_t *storage = emu_sd_cfg.enabled == 1 ? &sd_storage : &emmc_storage;
		sector += emu_sd_cfg.sector;
		return sdmmc_storage_read(storage, sector, num_sectors, buf);
	}else{
		return file_based_storage_read(sector, num_sectors, buf);
	}
}

sdmmc_storage_t *emusd_get_storage() {
	return emu_sd_cfg.enabled == 4 ? &emmc_storage : &sd_storage;
}


bool emusd_is_gpt() {
	if(emu_sd_cfg.enabled) {
		return emusd_fs.part_type;
	} else {
		return sd_fs.part_type;
	}
}

int emusd_get_fs_type() {
	if(emu_sd_cfg.enabled) {
		return emusd_fs.fs_type;
	} else {
		return sd_fs.fs_type;
	}
}

void *emusd_file_read(const char *path, u32 *fsize) {
	if(emu_sd_cfg.enabled) {
		char *path1 = malloc(0x100);
		strcpy(path1, "emusd:");
		strcat(path1, path);
		FIL fp;
		if (!emusd_get_mounted()) {
			free(path1);
			return NULL;
		}

		if (f_open(&fp, path1, FA_READ) != FR_OK) {
			free(path1);
			return NULL;
		}

		u32 size = f_size(&fp);
		if (fsize)
			*fsize = size;

		void *buf = malloc(size);

		if (f_read(&fp, buf, size, NULL) != FR_OK)
		{
			free(buf);
			f_close(&fp);
			free(path1);
			return NULL;
		}

		f_close(&fp);
		free(path1);

		return buf;
	} else {
		return boot_storage_file_read(path, fsize);
	}
}