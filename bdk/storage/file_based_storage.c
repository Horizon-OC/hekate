#include "file_based_storage.h"
#include <libs/fatfs/ff.h>
#include <stdlib.h>
#include <string.h>

typedef struct{
	FIL active_file;
	s32 active_file_idx;
	char base_path[0x80];
	u32 part_sz_sct;
	u32 base_path_len;
}file_based_storage_ctxt_t;

static file_based_storage_ctxt_t ctx;

int file_based_storage_init(const char *base_path) {
	ctx.active_file_idx = -0xff;
	ctx.base_path_len = strlen(base_path);

	strcpy(ctx.base_path, base_path);
	strcat(ctx.base_path, "00");

	FILINFO fi;
	if(f_stat(ctx.base_path, &fi) != FR_OK) {
		return 0;
	}

	if(fi.fsize) {
		ctx.part_sz_sct = fi.fsize >> 9;
	}else{
		return 0;
	}
	ctx.part_sz_sct = fi.fsize;
	return 1;
}

void file_based_storage_end() {
	if(ctx.active_file_idx != -0xff) {
		f_close(&ctx.active_file);
	}
	ctx.active_file_idx = -0xff;
	ctx.part_sz_sct = 0;
}

static int file_based_storage_change_file(const char *name, s32 idx) {
	int res;

	if(ctx.active_file_idx == idx){
		return FR_OK;
	}

	if(ctx.active_file_idx != -0xff){
		f_close(&ctx.active_file);
		ctx.active_file_idx = -0xff;
	}

	strcpy(ctx.base_path + ctx.base_path_len, name);

	#if FF_FS_READONLY == 1
	res = f_open(&ctx.active_file, ctx.base_path, FA_READ);
	#else
	res = f_open(&ctx.active_file, ctx.base_path, FA_READ | FA_WRITE);
	#endif

	if(res != FR_OK){
		return res;
	}

	ctx.active_file_idx = idx;

	return FR_OK;
}

static int file_based_storage_readwrite_single(u32 sector, u32 num_sectors, void *buf, bool is_write){
	#if FF_FS_READONLY == 1
	if(is_write){
		return FR_WRITE_PROTECTED;
	}
	#endif

	int res = f_lseek(&ctx.active_file, (u64)sector << 9);
	if(res != FR_OK){
		return res;
	}

	if(is_write){
		res = f_write(&ctx.active_file, buf, (u64)num_sectors << 9, NULL);
	}else{
		res = f_read(&ctx.active_file, buf, (u64)num_sectors << 9, NULL);
	}

	if(res != FR_OK){
		return res;
	}

	return FR_OK;
}

int file_based_storage_readwrite(u32 sector, u32 num_sectors, void *buf, bool is_write) {
	#if FF_FS_READONLY == 1
	if(is_write){
		return 0;
	}
	#endif

	if(ctx.part_sz_sct == 0){
		return 0;
	}

	int res;

	u32 scts_left = num_sectors;
	u32 cur_sct = sector;

	while(scts_left){
		u32 offset = cur_sct % ctx.part_sz_sct;
		u32 sct_cnt = ctx.part_sz_sct - offset;
		sct_cnt = MIN(scts_left, sct_cnt);

		u32 file_idx = cur_sct / ctx.part_sz_sct;

		if((s32) file_idx != ctx.active_file_idx) {
			char name[3];
			if(file_idx < 10){
				strcpy(name, "0");
			}
			itoa(file_idx, name + strlen(name), 10);

			res = file_based_storage_change_file(name, file_idx);
			if(res != FR_OK){
				return 0;
			}
		}

		res = file_based_storage_readwrite_single(offset, sct_cnt, buf + ((u64)(num_sectors - scts_left) << 9), is_write);
		if(res != FR_OK){
			return 0;
		}

		cur_sct += sct_cnt;
		scts_left -= sct_cnt;
	}

	f_sync(&ctx.active_file);
	return 1;
}

int file_based_storage_read(u32 sector, u32 num_sectors, void *buf) {
	return file_based_storage_readwrite(sector, num_sectors, buf, false);
}

int file_based_storage_write(u32 sector, u32 num_sectors, void *buf) {
	return file_based_storage_readwrite(sector, num_sectors, buf, true);
}

u32 file_based_storage_get_total_size() {
	u32 total_size_sct = 0;
	char file_path[0x80];
	u32 cur_idx = 0;
	int res;

	strcpy(file_path, ctx.base_path);
	strcpy(file_path + ctx.base_path_len, "00");

	FILINFO fi;
	res = f_stat(file_path, &fi);

	while(res == FR_OK){
		cur_idx++;
		total_size_sct += fi.fsize >> 9;

		char name[3] = "0";
		if(cur_idx >= 10){
			name[0] = '\0';
		}
		itoa(cur_idx, name + strlen(name), 10);

		strcpy(file_path + ctx.base_path_len, name);

		res = f_stat(file_path, &fi);
	}

	return total_size_sct;
}