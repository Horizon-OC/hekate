#include "boot_storage.h"
#include <libs/fatfs/ff.h>
#include <fatfs_cfg.h>
#include <storage/sd.h>
#include <storage/emmc.h>
#include <utils/types.h>
#include <gfx_utils.h>
#include <stdlib.h>

#define DEV_INVALID 0xff

static FATFS boot_storage_fs;
static BYTE drive_cur = -1;
static BYTE drive = -1;

static const char* drive_base_paths[] = {
	[DRIVE_SD]         = "sd:",
	[DRIVE_BOOT1]      = "boot1_:",
	[DRIVE_BOOT1_1MB]  = "boot1_1mb:",
	[DRIVE_EMMC]       = "emmc:",
};

static bool _is_eligible(){
	if(f_stat(".no_boot_storage", NULL) == FR_OK){
		return false;
	}
	return true;
} 

bool boot_storage_get_mounted(){
	switch(drive_cur){
	case DRIVE_SD:
		return sd_get_card_mounted();
	case DRIVE_EMMC:
		return emmc_get_mounted();
	case DRIVE_BOOT1:
	case DRIVE_BOOT1_1MB:
		return drive_cur != DEV_INVALID;
	}
	return false;
}

bool boot_storage_get_initialized(){
	switch(drive_cur){
	case DRIVE_BOOT1:
	case DRIVE_EMMC:
	case DRIVE_BOOT1_1MB:
		return emmc_get_initialized();
	case DRIVE_SD:
		return sd_get_card_initialized();
	}
	return false;
}

static bool _boot_storage_initialize(){
	switch(drive_cur){
	case DRIVE_BOOT1:
	case DRIVE_EMMC:
	case DRIVE_BOOT1_1MB:
		return emmc_initialize(false);
	case DRIVE_SD:
		return sd_initialize(false);
	}

	return false;
}

static void _boot_storage_end(bool deinit){
	BYTE prev_drive = drive_cur;

	if(boot_storage_get_mounted()){
		switch(drive_cur){
		case DRIVE_SD:
			sd_unmount();
			break;
		case DRIVE_EMMC:
			emmc_unmount();
			break;
		case DRIVE_BOOT1:
		case DRIVE_BOOT1_1MB:
			f_mount(NULL, drive_base_paths[drive_cur], 0);
		}
		drive_cur = DEV_INVALID;
	}

	if(deinit){
		switch(prev_drive){
		case DRIVE_SD:
			sd_end();
			break;
		case DRIVE_EMMC:
		case DRIVE_BOOT1:
		case DRIVE_BOOT1_1MB:
			emmc_end();
			break;
		}
	}
}

void boot_storage_unmount(){
	_boot_storage_end(false);
}

void boot_storage_end(){
	_boot_storage_end(true);
}

u8 boot_storage_get_drive(){
	return drive;
}

static bool _boot_storage_mount(){
	// may want to check sd card first and prioritize it

	FRESULT res;

	bool prev_emmc_initialized = emmc_get_initialized();
	bool prev_sd_initialized = sd_get_card_initialized();

	if(!prev_emmc_initialized && !emmc_initialize(false)){
		goto emmc_init_fail;
	}

	static const BYTE emmc_drives[] = {DRIVE_BOOT1_1MB, DRIVE_BOOT1}; 

	for(BYTE i = 0; i < ARRAY_SIZE(emmc_drives); i++){
		res = f_mount(&boot_storage_fs, drive_base_paths[emmc_drives[i]], true);
		if(res == FR_OK){
			res = f_chdrive(drive_base_paths[emmc_drives[i]]);
			if(res == FR_OK && _is_eligible()){
				drive_cur = emmc_drives[i];
				drive = drive_cur;
				break;
			}else{
				f_mount(NULL, drive_base_paths[emmc_drives[i]],false);
				res = FR_INVALID_DRIVE;
			}
		}
	}

	if(res != FR_OK){
		if(!prev_emmc_initialized) {
			emmc_end();
		}
	}

	if(res == FR_OK){
		return true;
	}

emmc_init_fail:
	if(!emmc_initialize(false)){
		goto emmc_init_fail2;
	}

	if(!emmc_mount()){
		if(!prev_emmc_initialized) {
			emmc_end();
		}
		goto emmc_init_fail2;
	}

	res = f_chdrive(drive_base_paths[DRIVE_EMMC]);

	if(res == FR_OK && _is_eligible()){
		drive_cur = DRIVE_EMMC;
		drive = drive_cur;
		return true;
	}

emmc_init_fail2:
	if(!sd_initialize(false)){
		goto out;
	}

	if(!sd_mount()){
		if(!prev_sd_initialized) {
			sd_end();
		}
		goto out;
	}

	res = f_chdrive(drive_base_paths[DRIVE_SD]);

	if(res == FR_OK && _is_eligible()){
		drive_cur = DRIVE_SD;
		drive = drive_cur;
		return true;
	}

	if(!prev_sd_initialized) {
		sd_end();
	}

out:
	return false;
}

bool boot_storage_mount(){
	bool mounted = boot_storage_get_mounted();
	bool initialized = boot_storage_get_initialized();
	bool res = mounted && initialized;
	if(!mounted){
		// not mounted. mounting will also initialize.
		res = _boot_storage_mount();
	}else if(!initialized){
		res = _boot_storage_initialize();
	}

	if(res){
		res = f_chdrive(drive_base_paths[drive_cur]) == FR_OK;
	}

	return res;
}

void *boot_storage_file_read(const char *path, u32 *fsize)
{
	FIL fp;
	if (!boot_storage_get_mounted())
		return NULL;

	if (f_open(&fp, path, FA_READ) != FR_OK)
		return NULL;

	u32 size = f_size(&fp);
	if (fsize)
		*fsize = size;

	void *buf = malloc(size);

	if (f_read(&fp, buf, size, NULL) != FR_OK)
	{
		free(buf);
		f_close(&fp);

		return NULL;
	}

	f_close(&fp);

	return buf;
}

int boot_storage_save_to_file(const void *buf, u32 size, const char *filename)
{
	FIL fp;
	u32 res = 0;
	if (!boot_storage_get_mounted())
		return FR_DISK_ERR;

	res = f_open(&fp, filename, FA_CREATE_ALWAYS | FA_WRITE);
	if (res)
	{
		EPRINTFARGS("Error (%d) creating file\n%s.\n", res, filename);
		return res;
	}

	f_write(&fp, buf, size, NULL);
	f_close(&fp);

	return 0;
}

FATFS *boot_storage_get_fs() {
	switch(drive_cur){
	case DRIVE_BOOT1:
		return &boot_storage_fs;
	case DRIVE_EMMC:
		return &emmc_fs;
	case DRIVE_BOOT1_1MB:
		return &boot_storage_fs;
	case DRIVE_SD:
		return &sd_fs;
	}
	return NULL;
}