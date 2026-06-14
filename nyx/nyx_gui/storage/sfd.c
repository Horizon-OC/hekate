#include "sfd.h"
#include <storage/emmc.h>
#include <storage/sdmmc.h>
#include <storage/file_based_storage.h>
#include <libs/fatfs/diskio.h>
#include <string.h>


static bool file_based;
static sdmmc_storage_t *_storage;
static u32 _offset;
static u32 _size;

static void ensure_partition(){
	if(_storage == &emmc_storage){
		emmc_set_partition(EMMC_GPP);
	}
}

sdmmc_storage_t *sfd_get_storage(){
	if(file_based){
		return NULL;
	}else{
		return _storage;
	}
}

int sfd_read(u32 sector, u32 count, void *buff){
	int res;
	if(sector + count > _size){
		return 0;
	}

	if(file_based){
		res = file_based_storage_read(sector, count, buff);
	}else{
		ensure_partition();
		res = sdmmc_storage_read(_storage, sector + _offset, count, buff);
	}
	return res;
}

int sfd_write(u32 sector, u32 count, void *buff){
	int res;
	if(sector + count > _size){
		return 0;
	}

	if(file_based){
		res = file_based_storage_write(sector, count, buff);
	}else{
		ensure_partition();
		res = sdmmc_storage_write(_storage, sector + _offset, count, buff);
	}

	return res;
}

bool sfd_init(sdmmc_storage_t *storage, u32 offset, u32 size){
	_storage = storage;
	_offset = offset;
	_size = size;
	disk_set_info(DRIVE_SFD, SET_SECTOR_COUNT, &size);
	return true;
}

bool sfd_file_based_init(const char *base_path) {
	file_based = true;
	if(!file_based_storage_init(base_path)){
		gfx_printf("file based init fail\n");
		return 0;
	}
	_size = file_based_storage_get_total_size();
	disk_set_info(DRIVE_SFD, SET_SECTOR_COUNT, &_size);

	return 1;
}

void sfd_end(){
	_storage = NULL;
	_offset = 0;
	_size = 0;
	u32 size = 0;
	file_based = false;
	disk_set_info(DRIVE_SFD, SET_SECTOR_COUNT, &size);
}