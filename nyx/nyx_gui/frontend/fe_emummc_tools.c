/*
 * Copyright (c) 2018 naehrwert
 * Copyright (c) 2018 Rajko Stojadinovic
 * Copyright (c) 2018-2026 CTCaer
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

//! fix the dram stuff and the pop ups

#include <fatfs_cfg.h>
#include <libs/lvgl/lv_objx/lv_label.h>
#include <mem/heap.h>
#include <storage/boot_storage.h>
#include <storage/emmc.h>
#include <storage/emummc_file_based.h>
#include <storage/mbr_gpt.h>
#include <storage/nx_emmc_bis.h>
#include <storage/sd.h>
#include <storage/sdmmc.h>
#include <string.h>
#include <stdlib.h>

#include <bdk.h>

#include "gui.h"
#include "fe_emummc_tools.h"
#include "../config.h"
#include "../hos/hos.h"
#include <libs/fatfs/diskio.h>
#include <libs/fatfs/ff.h>
#include <utils/sprintf.h>

#define OUT_FILENAME_SZ      128
#define NUM_SECTORS_PER_ITER 8192 // 4MB Cache.

extern volatile boot_cfg_t *b_cfg;

static int _emummc_resize_user(emmc_tool_gui_t *gui, u32 user_offset, u32 resized_cnt, sdmmc_storage_t *raw_based_storage, u32 raw_based_sector_offset, const char *file_based_path, u8 drive){
	bool file_based = file_based_path != NULL;

	if(drive == DRIVE_SD){
		sd_mount();
	}else{
		emmc_mount();
	}

	s_printf(gui->txt_buf, "\nFormatting USER... ");
	lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, gui->txt_buf);
	manual_system_maintenance(true);


	u32 user_sectors = resized_cnt - user_offset - 33;
	user_sectors = ALIGN_DOWN(user_sectors, 0x20);
	disk_set_info(DRIVE_EMU, SET_SECTOR_COUNT, &user_sectors);

	emmc_part_t user_part = {0};
	user_part.lba_start = user_offset;
	user_part.lba_end = user_offset + user_sectors - 1;
	strcpy(user_part.name, "USER");

	if(file_based){
		nx_emmc_bis_init_file_based(&user_part, true, file_based_path);
	}else{
		nx_emmc_bis_init(&user_part, true, raw_based_storage, raw_based_sector_offset);
	}

	u8 *buf = malloc(SZ_4M);
	int res = f_mkfs("emu:", FM_FAT32 | FM_SFD | FM_PRF2, 16384, buf, SZ_4M);

	nx_emmc_bis_end();
	hos_bis_keys_clear();

	if(res != FR_OK){
		s_printf(gui->txt_buf, "#FF0000 Failed (%d)!#\nPlease try again...\n", res);
		lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, gui->txt_buf);
		free(buf);
		return res;
	}

	s_printf(gui->txt_buf, "Done!\nWriting new GPT... ");
	lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, gui->txt_buf);
	manual_system_maintenance(true);

	mbr_t mbr = {0};
	mbr.boot_signature = 0xaa55;
	mbr.partitions[0].type = 0xee;
	mbr.partitions[0].start_sct = 1;
	mbr.partitions[0].start_sct_chs.sector = 0x02;
	mbr.partitions[0].end_sct_chs.sector = 0xff;
	mbr.partitions[0].end_sct_chs.cylinder = 0xff;
	mbr.partitions[0].end_sct_chs.head = 0xff;
	mbr.partitions[0].size_sct = 0xffffffff;

	gpt_t *gpt = zalloc(sizeof(*gpt));
	gpt_header_t gpt_hdr_backup = {0};

	if(file_based){
		emummc_storage_file_based_init(file_based_path);
	}

	res = 1;
	res &= sdmmc_storage_read(&emmc_storage, 1, sizeof(*gpt) / 0x200, gpt);
	res &= sdmmc_storage_read(&emmc_storage, gpt->header.alt_lba, 1, &gpt_hdr_backup);

	if(!res){
		s_printf(gui->txt_buf, "\n#FF0000 Failed to read original GPT...#\nPlease try again...\n");
		lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, gui->txt_buf);
		free(gpt);
		free(buf);
		emummc_storage_file_based_end();

		return FR_DISK_ERR;
	}

	u32 gpt_entry_idx = 0;
	for (gpt_entry_idx = 0; gpt_entry_idx < gpt->header.num_part_ents; gpt_entry_idx++)
		if (!memcmp(gpt->entries[gpt_entry_idx].name, (char[]) { 'U', 0, 'S', 0, 'E', 0, 'R', 0 }, 8))
			break;

	if (gpt_entry_idx >= gpt->header.num_part_ents)
	{
		s_printf(gui->txt_buf, "\n#FF0000 No USER partition...#\nPlease try again...\n");
		lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, gui->txt_buf);
		free(gpt);
		free(buf);
		emummc_storage_file_based_end();

		return FR_DISK_ERR;
	}

	// clear out all partition entries after user
	memset(&gpt->entries[gpt_entry_idx + 1], 0, sizeof(gpt->entries[0]) * (128 - (gpt_entry_idx + 1)));

	// Set new emuMMC size and USER size.
	mbr.partitions[0].size_sct = resized_cnt;
	gpt->entries[gpt_entry_idx].lba_end = user_part.lba_end;

	// Update Main GPT.
	gpt->header.num_part_ents = gpt_entry_idx + 1;
	gpt->header.alt_lba = resized_cnt - 1;
	gpt->header.last_use_lba = resized_cnt - 34;
	gpt->header.part_ents_crc32 = crc32_calc(0, (const u8 *)gpt->entries, sizeof(gpt_entry_t) * gpt->header.num_part_ents);
	gpt->header.crc32 = 0; // Set to 0 for calculation.
	gpt->header.crc32 = crc32_calc(0, (const u8 *)&gpt->header, gpt->header.size);

	// Update Backup GPT.
	gpt_hdr_backup.my_lba = resized_cnt - 1;
	gpt_hdr_backup.part_ent_lba = resized_cnt - 33;
	gpt_hdr_backup.part_ents_crc32 = gpt->header.part_ents_crc32;
	gpt_hdr_backup.last_use_lba = gpt->header.last_use_lba;
	gpt_hdr_backup.crc32 = 0; // Set to 0 for calculation.
	gpt_hdr_backup.crc32 = crc32_calc(0, (const u8 *)&gpt_hdr_backup, gpt_hdr_backup.size);

	res = 1;
	if(file_based){
		// Write main GPT
		res &= emummc_storage_file_based_write(gpt->header.my_lba, sizeof(gpt_t) >> 9, gpt);
		// Write backup GPT partition table.
		res &= emummc_storage_file_based_write(gpt_hdr_backup.part_ent_lba, ((sizeof(gpt_entry_t) * 128) >> 9), gpt->entries);
		// Write backup GPT header.
		res &= emummc_storage_file_based_write(gpt_hdr_backup.my_lba, 1, &gpt_hdr_backup);
		// Write MBR.
		res &= emummc_storage_file_based_write(0, 1, &mbr);
		// Clear nand patrol.
		memset(buf, 0, EMMC_BLOCKSIZE);
		res &= emummc_storage_file_based_write(NAND_PATROL_SECTOR, 1, buf);
	}else{
		// Write main GPT.
		res &= sdmmc_storage_write(raw_based_storage, raw_based_sector_offset + gpt->header.my_lba, sizeof(gpt_t) >> 9, gpt);
		// Write backup GPT partition table.
		res &= sdmmc_storage_write(raw_based_storage, raw_based_sector_offset + gpt_hdr_backup.part_ent_lba, ((sizeof(gpt_entry_t) * 128) >> 9), gpt->entries);
		// Write backup GPT header.
		res &= sdmmc_storage_write(raw_based_storage, raw_based_sector_offset + gpt_hdr_backup.my_lba, 1, &gpt_hdr_backup);
		// Write MBR.
		res &= sdmmc_storage_write(raw_based_storage, raw_based_sector_offset, 1, &mbr);
		// Clear nand patrol.
		memset(buf, 0, EMMC_BLOCKSIZE);
		res &= sdmmc_storage_write(raw_based_storage, raw_based_sector_offset + NAND_PATROL_SECTOR, 1, buf);
	}

	if(!res){
		s_printf(gui->txt_buf, "\n#FF0000 Failed to write GPT...#\nPlease try again...\n");
		lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, gui->txt_buf);
		free(gpt);
		free(buf);
		emummc_storage_file_based_end();

		return FR_DISK_ERR;
	}

	emummc_storage_file_based_end();

	free(gpt);
	free(buf);
	return FR_OK;
}

void load_emummc_cfg(emummc_cfg_t *emu_info)
{
	memset(emu_info, 0, sizeof(emummc_cfg_t));

	// Parse emuMMC configuration.
	LIST_INIT(ini_sections);
	if (ini_parse(&ini_sections, "emuMMC/emummc.ini", false))
		return;

	LIST_FOREACH_ENTRY(ini_sec_t, ini_sec, &ini_sections, link)
	{
		if (!strcmp(ini_sec->name, "emummc"))
		{
			LIST_FOREACH_ENTRY(ini_kv_t, kv, &ini_sec->kvs, link)
			{
				if (!strcmp("enabled",     kv->key))
					emu_info->enabled = atoi(kv->val);
				else if (!strcmp("sector", kv->key))
					emu_info->sector = strtol(kv->val, NULL, 16);
				else if (!strcmp("id",     kv->key))
					emu_info->id     = strtol(kv->val, NULL, 16);
				else if (!strcmp("path",   kv->key))
				{
					emu_info->path = (char *)malloc(strlen(kv->val) + 1);
					strcpy(emu_info->path, kv->val);
				}
				else if (!strcmp("nintendo_path", kv->key))
				{
					emu_info->nintendo_path = (char *)malloc(strlen(kv->val) + 1);
					strcpy(emu_info->nintendo_path, kv->val);
				}
			}

			break;
		}
	}

	ini_free(&ini_sections);
}

void save_emummc_cfg(u32 part_idx, u32 sector_start, const char *path, u8 drive)
{
	boot_storage_mount();

	char lbuf[16];
	FIL fp;

	int res = 0;

	res = f_mkdir("emuMMC");
	if(res != FR_OK && res != FR_EXIST){
		return;
	}

	res = f_open(&fp, "emuMMC/emummc.ini", FA_WRITE | FA_CREATE_ALWAYS);
	if (res != FR_OK){
		return;
	}

	// Add config entry.
	res = f_puts("[emummc]\nenabled=", &fp);
	gfx_printf("puts %d\n", res);
	if (part_idx && sector_start && drive == DRIVE_SD)
	{
		// 1: part. 1, 2: part. 2, 3: part. 3
		itoa(part_idx, lbuf, 10);
		f_puts(lbuf, &fp);
	}else if(drive == DRIVE_EMMC && sector_start && part_idx){
		// 4: emmc raw based
		f_puts("4", &fp);
	}
	else if (path && drive == DRIVE_SD)
		// 1: enabled, file based
		f_puts("1", &fp);
	else if(path && drive == DRIVE_EMMC){
		// 4: enabled, emmc file based
		f_puts("4", &fp);
	}else
		// 0: disable
		f_puts("0", &fp);

	if (!sector_start)
		f_puts("\nsector=0x0", &fp);
	else
	{
		f_puts("\nsector=0x", &fp);
		itoa(sector_start, lbuf, 16);
		f_puts(lbuf, &fp);
	}
	if (path)
	{
		f_puts("\npath=", &fp);
		f_puts(path, &fp);
	}

	// Get ID from path.
	u32 id_from_path = 0;
	if (path && strlen(path) >= 4){
		memcpy(&id_from_path, path + strlen(path) - 4, 4);
	}
	f_puts("\nid=0x", &fp);
	itoa(id_from_path, lbuf, 16);
	f_puts(lbuf, &fp);

	f_puts("\nnintendo_path=", &fp);
	if (path)
	{
		f_puts(path, &fp);
		f_puts("/Nintendo\n", &fp);
	}
	else
		f_puts("\n", &fp);

	f_close(&fp);
}

void update_emummc_base_folder(char *outFilename, u32 sdPathLen, u32 currPartIdx)
{
	if (currPartIdx < 10)
	{
		outFilename[sdPathLen] = '0';
		itoa(currPartIdx, &outFilename[sdPathLen + 1], 10);
	}
	else
		itoa(currPartIdx, &outFilename[sdPathLen], 10);
}

int emummc_raw_derive_bis_keys()
{
	u8 *cal0_buff = malloc(SZ_64K);

	// Generate BIS keys.
	if (hos_bis_keygen())
		goto error;

	// Read and decrypt CAL0 for validation of working BIS keys.
	emmc_set_partition(EMMC_GPP);
	LIST_INIT(gpt);
	emmc_gpt_parse(&gpt);
	// reads from emummc, if enabled
	emmc_part_t *cal0_part = emmc_part_find(&gpt, "PRODINFO"); // check if null
	nx_emmc_bis_init(cal0_part, false, NULL,  0);
	nx_emmc_bis_read(0, 0x40, cal0_buff);
	nx_emmc_bis_end();
	emmc_gpt_free(&gpt);

	nx_emmc_cal0_t *cal0 = (nx_emmc_cal0_t *)cal0_buff;

	// Check keys validity.
	if (memcmp(&cal0->magic, "CAL0", 4))
	{
error:
		// Clear EKS keys.
		hos_eks_clear(HOS_MKEY_VER_MAX);

		lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
		lv_obj_set_style(dark_bg, &mbox_darken);
		lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

		static const char * mbox_btn_map[] = { "\251", "\222Close", "\251", "" };
		lv_obj_t * mbox = lv_mbox_create(dark_bg, NULL);
		lv_mbox_set_recolor_text(mbox, true);
		lv_obj_set_width(mbox, LV_HOR_RES / 9 * 5);

		lv_mbox_set_text(mbox, "#C7EA46 BIS Keys Generation#");

		lv_obj_t * lb_desc = lv_label_create(mbox, NULL);
		lv_label_set_long_mode(lb_desc, LV_LABEL_LONG_BREAK);
		lv_label_set_recolor(lb_desc, true);
		lv_label_set_style(lb_desc, &monospace_text);
		lv_obj_set_width(lb_desc, LV_HOR_RES / 9 * 4);

		lv_label_set_text(lb_desc, "#FFDD00 BIS keys validation failed!#\n");
		lv_mbox_add_btns(mbox, mbox_btn_map, nyx_mbox_action);

		lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
		lv_obj_set_top(mbox, true);

		free(cal0_buff);
		return 0;
	}

	free(cal0_buff);

	return 1;
}

static int _dump_emummc_file_part(emmc_tool_gui_t *gui, char *sd_path, sdmmc_storage_t *storage, emmc_part_t *part, u32 resized_cnt, u8 drive)
{
	static const u32 FAT32_FILESIZE_LIMIT = 0xFFFFFFFF;
	static const u32 SECTORS_TO_MIB_COEFF = 11;

	u32 multipartSplitSize = 0xFE000000;
	u32 sectors_left = resized_cnt ? resized_cnt : part->lba_end - part->lba_start + 1;
	u32 total_sectors = sectors_left;
	u32 currPartIdx = 0;
	u32 numSplitParts = 0;
	int res = 0;
	char *outFilename = sd_path;
	u32 sdPathLen = strlen(sd_path);
	u32 free_space;

	FATFS *fs = drive == DRIVE_SD ? &sd_fs : &emmc_fs;
	if(drive == DRIVE_SD){
		free_space = (u32)((fs->free_clst * fs->csize) >> SECTORS_TO_MIB_COEFF);
		s_printf(gui->txt_buf, "#96FF00 SD Card free space:# %d MiB\n#96FF00 Total size:# %d MiB\n\n",
			free_space,
			sectors_left >> SECTORS_TO_MIB_COEFF);
	}else{
		free_space = (u32)((fs->free_clst * fs->csize) >> SECTORS_TO_MIB_COEFF);
		s_printf(gui->txt_buf, "#96FF00 eMMC free space:# %d MiB\n#96FF00 Total size:# %d MiB\n\n",
			free_space,
			sectors_left >> SECTORS_TO_MIB_COEFF);
	}

	lv_label_ins_text(gui->label_info, LV_LABEL_POS_LAST, gui->txt_buf);
	manual_system_maintenance(true);

	lv_bar_set_value(gui->bar, 0);
	lv_label_set_text(gui->label_pct, " "SYMBOL_DOT" 0%");
	manual_system_maintenance(true);

	// Check if the USER partition or the RAW eMMC fits the sd card free space.
	if (sectors_left > (fs->free_clst * fs->csize))
	{
		s_printf(gui->txt_buf, "\n#FFDD00 Not enough free space for file based emuMMC!#\n");
		lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, gui->txt_buf);
		manual_system_maintenance(true);

		return 0;
	}

	// Check if filesystem is FAT32 or the free space is smaller and dump in parts.
	if (sectors_left > (FAT32_FILESIZE_LIMIT / EMMC_BLOCKSIZE))
	{
		u32 multipartSplitSectors = multipartSplitSize / EMMC_BLOCKSIZE;
		numSplitParts = (sectors_left + multipartSplitSectors - 1) / multipartSplitSectors;

		// Get first part filename.
		update_emummc_base_folder(outFilename, sdPathLen, 0);
	}


	FIL fp;
	s_printf(gui->txt_buf, "#96FF00 Filepath:#\n%s\n#96FF00 Filename:# #FF8000 %s#",
		gui->base_path, outFilename + strlen(gui->base_path));
	lv_label_ins_text(gui->label_info, LV_LABEL_POS_LAST, gui->txt_buf);
	manual_system_maintenance(true);

	res = f_open(&fp, outFilename, FA_CREATE_ALWAYS | FA_WRITE);
	if (res)
	{
		s_printf(gui->txt_buf, "\n#FF0000 Error (%d) while creating#\n#FFDD00 %s#\n", res, outFilename);
		lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, gui->txt_buf);
		manual_system_maintenance(true);

		return 0;
	}

	u32 user_offset = 0;
	if(resized_cnt){
		gpt_t *gpt = zalloc(sizeof(*gpt));
		sdmmc_storage_read(&emmc_storage, 1, sizeof(*gpt) / 0x200, gpt);

		s32 gpt_idx = gpt_get_part_by_name(gpt, "USER", -1);

		if(gpt_idx == -1){
			s_printf(gui->txt_buf, "\n#FFDD00 USER partition not found!#\n");
			lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, gui->txt_buf);
			manual_system_maintenance(true);
			free(gpt);
			return 0;
		}
		user_offset = gpt->entries[gpt_idx].lba_start;
		part->lba_end = user_offset - 1;
		free(gpt);
	}

	u8 *buf = (u8 *)MIXD_BUF_ALIGNED;

	u32 lba_curr = part->lba_start;
	u32 bytesWritten = 0;
	u32 prevPct = 200;
	int retryCount = 0;
	DWORD *clmt = NULL;

	// total size to copy from emmc, excludes USER, if resize_cnt is set
	u32 copy_sectors = part->lba_end - part->lba_start + 1;
	// u64 copy_size    = (u64)copy_sectors << (u64)9;

	u64 total_size = (u64)sectors_left << (u64)9;

	// use total sectors (includes USER)
	if (total_size <= FAT32_FILESIZE_LIMIT)
		clmt = f_expand_cltbl(&fp, SZ_4M, total_size);
	else
		clmt = f_expand_cltbl(&fp, SZ_4M, MIN(total_size, multipartSplitSize));

	u32 num = 0;
	u32 pct = 0;


	lv_obj_set_opa_scale(gui->bar, LV_OPA_COVER);
	lv_obj_set_opa_scale(gui->label_pct, LV_OPA_COVER);
	while (sectors_left > 0)
	{
		if (numSplitParts != 0 && bytesWritten >= multipartSplitSize)
		{
			f_close(&fp);
			free(clmt);
			memset(&fp, 0, sizeof(fp));
			currPartIdx++;

			update_emummc_base_folder(outFilename, sdPathLen, currPartIdx);

			// Create next part.
			s_printf(gui->txt_buf, "%s#", outFilename + strlen(gui->base_path));
			lv_label_cut_text(gui->label_info, strlen(lv_label_get_text(gui->label_info)) - 3, 3);
			lv_label_ins_text(gui->label_info, LV_LABEL_POS_LAST, gui->txt_buf);
			manual_system_maintenance(true);

			res = f_open(&fp, outFilename, FA_CREATE_ALWAYS | FA_WRITE);
			if (res)
			{
				s_printf(gui->txt_buf, "\n#FF0000 Error (%d) while creating#\n#FFDD00 %s#\n", res, outFilename);
				lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, gui->txt_buf);
				manual_system_maintenance(true);

				return 0;
			}

			bytesWritten = 0;

			total_size = (u64)((u64)sectors_left << 9);
			clmt = f_expand_cltbl(&fp, SZ_4M, MIN(total_size, multipartSplitSize));
		}

		// Check for cancellation combo.
		if (btn_read_vol() == (BTN_VOL_UP | BTN_VOL_DOWN))
		{
			s_printf(gui->txt_buf, "\n#FFDD00 The emuMMC was cancelled!#\n");
			lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, gui->txt_buf);
			manual_system_maintenance(true);

			f_close(&fp);
			free(clmt);
			f_unlink(outFilename);

			msleep(1000);

			return 0;
		}

		if(copy_sectors){
			retryCount = 0;
			num = MIN(copy_sectors, NUM_SECTORS_PER_ITER);

			while (!sdmmc_storage_read(storage, lba_curr, num, buf))
			{
				s_printf(gui->txt_buf,
					"\n#FFDD00 Error reading %d blocks @ LBA %08X,#\n"
					"#FFDD00 from eMMC (try %d). #",
					num, lba_curr, ++retryCount);
				lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, gui->txt_buf);
				manual_system_maintenance(true);

				msleep(150);
				if (retryCount >= 3)
				{
					s_printf(gui->txt_buf, "#FF0000 Aborting...#\nPlease try again...\n");
					lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, gui->txt_buf);
					manual_system_maintenance(true);

					f_close(&fp);
					free(clmt);
					f_unlink(outFilename);

					return 0;
				}
				else
				{
					s_printf(gui->txt_buf, "#FFDD00 Retrying...#");
					lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, gui->txt_buf);
					manual_system_maintenance(true);
				}
			}

			manual_system_maintenance(false);

			res = f_write_fast(&fp, buf, EMMC_BLOCKSIZE * num);

			manual_system_maintenance(false);

			if (res)
			{
				s_printf(gui->txt_buf, "\n#FF0000 Fatal error (%d) when writing to %s#\nPlease try again...\n", res, drive == DRIVE_SD ? "SD Card" : "eMMC");
				lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, gui->txt_buf);
				manual_system_maintenance(true);

				f_close(&fp);
				free(clmt);
				f_unlink(outFilename);

				return 0;
			}

			copy_sectors -= num;
		}else{
			// no more data to copy, keep seeking
			u32 multipartSplitSizeSct = multipartSplitSize >> 9;
			num = multipartSplitSizeSct - (lba_curr % multipartSplitSizeSct);
			num = MIN(sectors_left, num);
		}

		pct = (u64)((u64)(lba_curr - part->lba_start) * 100u) / (u64)(total_sectors);
		if (pct != prevPct)
		{
			lv_bar_set_value(gui->bar, pct);
			s_printf(gui->txt_buf, " "SYMBOL_DOT" %d%%", pct);
			lv_label_set_text(gui->label_pct, gui->txt_buf);
			manual_system_maintenance(true);

			prevPct = pct;
		}

		lba_curr += num;
		sectors_left -= num;
		bytesWritten += num * EMMC_BLOCKSIZE;

		// Force a flush after a lot of data if not splitting.
		if (numSplitParts == 0 && bytesWritten >= multipartSplitSize)
		{
			f_sync(&fp);
			bytesWritten = 0;
		}

		manual_system_maintenance(false);
	}
	// Operation ended successfully.
	f_close(&fp);
	free(clmt);
	lv_bar_set_value(gui->bar, 100);
	lv_label_set_text(gui->label_pct, " "SYMBOL_DOT" 100%");
	manual_system_maintenance(true);

	if(resized_cnt){
		lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, "Done!");
		manual_system_maintenance(true);

		sd_path[sdPathLen] = '\0';
		if(_emummc_resize_user(gui, user_offset, resized_cnt, NULL, 0, sd_path, drive) != FR_OK){
			return 0;
		}
	}
	return 1;
}

void dump_emummc_file(emmc_tool_gui_t *gui, u32 resized_cnt, u8 drive)
{
	int res = 0;
	int base_len = 0;
	u32 timer = 0;

	char *txt_buf = (char *)malloc(SZ_16K);
	gui->base_path = (char *)malloc(OUT_FILENAME_SZ);
	gui->txt_buf = txt_buf;

	txt_buf[0] = 0;
	lv_label_set_text(gui->label_log, txt_buf);

	manual_system_maintenance(true);

	boot_storage_mount();

	if(drive == DRIVE_SD){
		if (!sd_mount())
		{
			lv_label_set_text(gui->label_info, "#FFDD00 Failed to init SD!#");
			goto out;
		}
	}else{
		if (!emmc_mount())
		{
			lv_label_set_text(gui->label_info, "#FFDD00 Failed to init eMMC!#");
			goto out;
		}
	}

	lv_label_set_text(gui->label_info, "Checking for available free space...");
	manual_system_maintenance(true);

	// Get SD Card free space for file based emuMMC.
	// already done right before
	// f_getfree("sd:", &sd_fs.free_clst, NULL);

	if (!emmc_initialize(false))
	{
		lv_label_set_text(gui->label_info, "#FFDD00 Failed to init eMMC!#");
		goto out;
	}

	if (!emummc_raw_derive_bis_keys())
	{
		s_printf(gui->txt_buf, "#FFDD00 For formatting USER partition,#\n#FFDD00 BIS keys are needed!#\n");
		lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, gui->txt_buf);
		emmc_end();
		goto out;
	}

	int i = 0;
	char sdPath[OUT_FILENAME_SZ];
	// Create Restore folders, if they do not exist.

	if(drive == DRIVE_SD){
		strcpy(sdPath, "sd:emuMMC/SD");
		f_mkdir("sd:emuMMC");
	}else{
		strcpy(sdPath, "emmc:emuMMC/EMMC");
		f_mkdir("emmc:emuMMC");
	}

	//also create on boot storage
	f_mkdir("emuMMC");

	base_len = strlen(sdPath);
	for (int j = 0; j < 100; j++)
	{
		update_emummc_base_folder(sdPath, base_len, j);
		if (f_stat(sdPath, NULL) == FR_NO_FILE)
			break;
	}
	f_mkdir(sdPath);
	f_mkdir(sdPath + (drive == DRIVE_SD ? 3 : 5));

	strcat(sdPath, "/eMMC");
	f_mkdir(sdPath);

	strcat(sdPath, "/");
	strcpy(gui->base_path, sdPath);

	timer = get_tmr_s();
	const u32 BOOT_PART_SECTORS = 0x2000; // Force 4 MiB.

	emmc_part_t bootPart;
	memset(&bootPart, 0, sizeof(bootPart));
	bootPart.lba_start = 0;
	bootPart.lba_end = BOOT_PART_SECTORS - 1;

	for (i = 0; i < 2; i++)
	{
		strcpy(bootPart.name, "BOOT");
		bootPart.name[4] = (u8)('0' + i);
		bootPart.name[5] = 0;

		s_printf(txt_buf, "#00DDFF %02d: %s#\n#00DDFF Range: 0x%08X - 0x%08X#\n\n",
			i, bootPart.name, bootPart.lba_start, bootPart.lba_end);
		lv_label_set_text(gui->label_info, txt_buf);
		s_printf(txt_buf, "%02d: %s... ", i, bootPart.name);
		lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, txt_buf);
		manual_system_maintenance(true);

		emmc_set_partition(i + 1);

		strcat(sdPath, bootPart.name);
		res = _dump_emummc_file_part(gui, sdPath, &emmc_storage, &bootPart, 0, drive);

		if (!res)
		{
			s_printf(txt_buf, "#FFDD00 Failed!#\n");
			goto out_failed;
		}
		else
			s_printf(txt_buf, "Done!\n");

		lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, txt_buf);
		manual_system_maintenance(true);

		strcpy(sdPath, gui->base_path);
	}

	// Get GP partition size dynamically.
	emmc_set_partition(EMMC_GPP);

	// Get GP partition size dynamically.
	u32 raw_num_sectors = emmc_storage.sec_cnt;

	emmc_part_t rawPart;
	memset(&rawPart, 0, sizeof(rawPart));
	rawPart.lba_start = 0;
	rawPart.lba_end = raw_num_sectors - 1;
	strcpy(rawPart.name, "GPP");

	s_printf(txt_buf, "#00DDFF %02d: %s#\n#00DDFF Range: 0x%08X - 0x%08X#\n\n",
		i, rawPart.name, resized_cnt ? resized_cnt : rawPart.lba_start, rawPart.lba_end);
	lv_label_set_text(gui->label_info, txt_buf);
	s_printf(txt_buf, "%02d: %s... ", i, rawPart.name);
	lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, txt_buf);
	manual_system_maintenance(true);

	res = _dump_emummc_file_part(gui, sdPath, &emmc_storage, &rawPart, resized_cnt, drive);

	if (!res)
		s_printf(txt_buf, "#FFDD00 Failed!#\n");
	else
		s_printf(txt_buf, "Done!\n");

	lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, txt_buf);
	manual_system_maintenance(true);

out_failed:
	timer = get_tmr_s() - timer;

	if (res)
	{
		s_printf(txt_buf, "Time taken: %dm %ds.\nFinished!", timer / 60, timer % 60);
		gui->base_path[strlen(gui->base_path) - 5] = '\0';
		strcpy(sdPath, gui->base_path);
		if(drive == DRIVE_SD){
			strcat(sdPath, "file_based");
		}else{
			strcat(sdPath, "file_emmc_based");
		}
		FIL fp;
		f_open(&fp, sdPath + (drive == DRIVE_SD ? 3 : 5) /* strip drive prefix*/, FA_CREATE_ALWAYS | FA_WRITE);
		f_close(&fp);

		gui->base_path[strlen(gui->base_path) - 1] = 0;
		save_emummc_cfg(0, 0, gui->base_path + (drive == DRIVE_SD ? 3 : 5), drive);
	}
	else
		s_printf(txt_buf, "Time taken: %dm %ds.", timer / 60, timer % 60);

	lv_label_set_text(gui->label_finish, txt_buf);

out:
	free(txt_buf);
	free(gui->base_path);
	sd_unmount();
	emmc_end();
}

static int _dump_emummc_raw_part(emmc_tool_gui_t *gui, int active_part, int part_idx, u32 sd_part_off, emmc_part_t *part, u32 resized_count, u8 drive)
{
	u32 num = 0;
	u32 pct = 0;
	u32 prevPct = 200;
	int retryCount = 0;
	u32 sd_sector_off = sd_part_off + (0x2000 * active_part);
	u32 lba_curr = part->lba_start;
	u8 *buf = (u8 *)MIXD_BUF_ALIGNED;

	u32 cur_emmc_part = emmc_storage.partition;

	sdmmc_storage_t *emu_storge = drive == DRIVE_SD ? &sd_storage : &emmc_storage;

	s_printf(gui->txt_buf, "\n\n\n");
	lv_label_ins_text(gui->label_info, LV_LABEL_POS_LAST, gui->txt_buf);
	manual_system_maintenance(true);

	lv_bar_set_value(gui->bar, 0);
	lv_label_set_text(gui->label_pct, " "SYMBOL_DOT" 0%");
	manual_system_maintenance(true);

	s_printf(gui->txt_buf, "#96FF00 Base folder:#\n%s\n#96FF00 Partition offset:# #FF8000 0x%08X#",
		gui->base_path, sd_part_off);
	lv_label_ins_text(gui->label_info, LV_LABEL_POS_LAST, gui->txt_buf);
	manual_system_maintenance(true);

	lv_obj_set_opa_scale(gui->bar, LV_OPA_COVER);
	lv_obj_set_opa_scale(gui->label_pct, LV_OPA_COVER);

	u32 user_offset = 0;

	if (resized_count)
	{
		// Get USER partition info.
		LIST_INIT(gpt_parsed);
		// NOTE: reads from emummc, if enabled
		emmc_gpt_parse(&gpt_parsed);
		emmc_part_t *user_part = emmc_part_find(&gpt_parsed, "USER");
		if (!user_part)
		{
			s_printf(gui->txt_buf, "\n#FFDD00 USER partition not found!#\n");
			lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, gui->txt_buf);
			manual_system_maintenance(true);

			return 0;
		}

		user_offset = user_part->lba_start;
		part->lba_end = user_offset - 1;
		emmc_gpt_free(&gpt_parsed);
	}

	u32 totalSectors = part->lba_end - part->lba_start + 1;
	while (totalSectors > 0)
	{
		// Check for cancellation combo.
		if (btn_read_vol() == (BTN_VOL_UP | BTN_VOL_DOWN))
		{
			s_printf(gui->txt_buf, "\n#FFDD00 The emuMMC was cancelled!#\n");
			lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, gui->txt_buf);
			manual_system_maintenance(true);

			msleep(1000);

			return 0;
		}

		retryCount = 0;
		num = MIN(totalSectors, NUM_SECTORS_PER_ITER);

		// Read data from eMMC.
		if(emmc_storage.partition != cur_emmc_part){
			emmc_set_partition(cur_emmc_part);
		}
		while (!sdmmc_storage_read(&emmc_storage, lba_curr, num, buf))
		{
			s_printf(gui->txt_buf,
				"\n#FFDD00 Error reading %d blocks @LBA %08X,#\n"
				"#FFDD00 from eMMC (try %d). #",
				num, lba_curr, ++retryCount);
			lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, gui->txt_buf);
			manual_system_maintenance(true);

			msleep(150);
			if (retryCount >= 3)
			{
				s_printf(gui->txt_buf, "#FF0000 Aborting...#\nPlease try again...\n");
				lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, gui->txt_buf);
				manual_system_maintenance(true);

				return 0;
			}
			else
			{
				s_printf(gui->txt_buf, "#FFDD00 Retrying...#\n");
				lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, gui->txt_buf);
				manual_system_maintenance(true);
			}
		}

		manual_system_maintenance(false);

		// Write data to SD card.
		retryCount = 0;
		if(drive == DRIVE_EMMC){
			if(emmc_storage.partition != EMMC_GPP){
				emmc_set_partition(EMMC_GPP);
			}
		}
		while (!sdmmc_storage_write(emu_storge, sd_sector_off + lba_curr, num, buf))
		{
			s_printf(gui->txt_buf,
				"\n#FFDD00 Error writing %d blocks @LBA %08X,#\n"
				"#FFDD00 to %s (try %d). #",
				num, lba_curr, drive == DRIVE_SD ? "SD" : "eMMC", ++retryCount);
			lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, gui->txt_buf);
			manual_system_maintenance(true);

			msleep(150);
			if (retryCount >= 3)
			{
				s_printf(gui->txt_buf, "#FF0000 Aborting...#\nPlease try again...\n");
				lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, gui->txt_buf);
				manual_system_maintenance(true);

				return 0;
			}
			else
			{
				s_printf(gui->txt_buf, "#FFDD00 Retrying...#\n");
				lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, gui->txt_buf);
				manual_system_maintenance(true);
			}
		}

		manual_system_maintenance(false);

		pct = (u64)((u64)(lba_curr - part->lba_start) * 100u) / (u64)(part->lba_end - part->lba_start);
		if (pct != prevPct)
		{
			lv_bar_set_value(gui->bar, pct);
			s_printf(gui->txt_buf, " "SYMBOL_DOT" %d%%", pct);
			lv_label_set_text(gui->label_pct, gui->txt_buf);
			manual_system_maintenance(true);

			prevPct = pct;
		}

		lba_curr += num;
		totalSectors -= num;
	}
	lv_bar_set_value(gui->bar, 100);
	lv_label_set_text(gui->label_pct, " "SYMBOL_DOT" 100%");
	manual_system_maintenance(true);

	// Set partition type to emuMMC (0xE0).
	if (active_part == 2 && drive == DRIVE_SD)
	{
		mbr_t mbr;
		sdmmc_storage_read(&sd_storage, 0, 1, &mbr);
		mbr.partitions[part_idx].type = 0xE0;
		sdmmc_storage_write(&sd_storage, 0, 1, &mbr);
	}

	if (resized_count)
	{
		lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, "Done!\n");

		if(_emummc_resize_user(gui, user_offset, resized_count, emu_storge, sd_sector_off, NULL, drive) != FR_OK){
			return 0;
		}
	}

	return 1;
}

void dump_emummc_raw(emmc_tool_gui_t *gui, int part_idx, u32 sector_start, u32 resized_count, u8 drive)
{
	int res = 0;
	u32 timer = 0;

	char *txt_buf = (char *)malloc(SZ_16K);
	gui->base_path = (char *)malloc(OUT_FILENAME_SZ);
	gui->txt_buf = txt_buf;

	txt_buf[0] = 0;
	lv_label_set_text(gui->label_log, txt_buf);

	manual_system_maintenance(true);

	sdmmc_storage_t *emu_storage = drive == DRIVE_SD ? &sd_storage : &emmc_storage;

	boot_storage_mount(); 

	if (drive == DRIVE_SD && !sd_initialize(false))
	{
		lv_label_set_text(gui->label_info, "#FFDD00 Failed to init SD!#");
		goto out;
	}

	if (!emmc_initialize(false))
	{
		lv_label_set_text(gui->label_info, "#FFDD00 Failed to init eMMC!#");
		goto out;
	}

	if (resized_count && !emummc_raw_derive_bis_keys())
	{
		s_printf(gui->txt_buf, "#FFDD00 For formatting USER partition,#\n#FFDD00 BIS keys are needed!#\n");
		lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, gui->txt_buf);
		emmc_end();
		goto out;
	}

	int i = 0;
	char sdPath[OUT_FILENAME_SZ];
	// Create folders, if they do not exist.
	f_mkdir("emuMMC");
	s_printf(sdPath, drive == DRIVE_SD ? "emuMMC/RAW%d" : "emuMMC/RAW_EMMC%d", part_idx);
	f_mkdir(sdPath);
	strcat(sdPath, "/");
	strcpy(gui->base_path, sdPath);

	timer = get_tmr_s();
	const u32 BOOT_PART_SECTORS = 0x2000; // Force 4 MiB.

	emmc_part_t bootPart;
	memset(&bootPart, 0, sizeof(bootPart));
	bootPart.lba_start = 0;
	bootPart.lba_end = BOOT_PART_SECTORS - 1;

	// Clear partition start.
	memset((u8 *)MIXD_BUF_ALIGNED, 0, SZ_16M);
	sdmmc_storage_write(emu_storage, sector_start - 0x8000, 0x8000, (u8 *)MIXD_BUF_ALIGNED);

	for (i = 0; i < 2; i++)
	{
		strcpy(bootPart.name, "BOOT");
		bootPart.name[4] = (u8)('0' + i);
		bootPart.name[5] = 0;

		s_printf(txt_buf, "#00DDFF %02d: %s#\n#00DDFF Range: 0x%08X - 0x%08X#\n\n",
			i, bootPart.name, bootPart.lba_start, bootPart.lba_end);
		lv_label_set_text(gui->label_info, txt_buf);
		s_printf(txt_buf, "%02d: %s... ", i, bootPart.name);
		lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, txt_buf);
		manual_system_maintenance(true);

		emmc_set_partition(i + 1);

		strcat(sdPath, bootPart.name);
		res = _dump_emummc_raw_part(gui, i, part_idx, sector_start, &bootPart, 0, drive);

		if (!res)
		{
			s_printf(txt_buf, "#FFDD00 Failed!#\n");
			goto out_failed;
		}
		else
			s_printf(txt_buf, "Done!\n");

		lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, txt_buf);
		manual_system_maintenance(true);

		strcpy(sdPath, gui->base_path);
	}

	emmc_set_partition(EMMC_GPP);

	// Get GP partition size dynamically.
	const u32 RAW_AREA_NUM_SECTORS = emmc_storage.sec_cnt;

	emmc_part_t rawPart;
	memset(&rawPart, 0, sizeof(rawPart));
	rawPart.lba_start = 0;
	rawPart.lba_end = RAW_AREA_NUM_SECTORS - 1;
	strcpy(rawPart.name, "GPP");
	{
		s_printf(txt_buf, "#00DDFF %02d: %s#\n#00DDFF Range: 0x%08X - 0x%08X#\n\n",
			i, rawPart.name, rawPart.lba_start, rawPart.lba_end);
		lv_label_set_text(gui->label_info, txt_buf);
		s_printf(txt_buf, "%02d: %s... ", i, rawPart.name);
		lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, txt_buf);
		manual_system_maintenance(true);

		res = _dump_emummc_raw_part(gui, 2, part_idx, sector_start, &rawPart, resized_count, drive);

		if (!res)
			s_printf(txt_buf, "#FFDD00 Failed!#\n");
		else
			s_printf(txt_buf, "Done!\n");

		lv_label_ins_text(gui->label_log, LV_LABEL_POS_LAST, txt_buf);
		manual_system_maintenance(true);
	}

out_failed:
	timer = get_tmr_s() - timer;

	if (res)
	{
		s_printf(txt_buf, "Time taken: %dm %ds.\nFinished!", timer / 60, timer % 60);
		strcpy(sdPath, gui->base_path);
		strcat(sdPath, drive == DRIVE_SD ? "raw_based" : "raw_emmc_based");
		FIL fp;
		f_open(&fp, sdPath, FA_CREATE_ALWAYS | FA_WRITE);
		f_write(&fp, &sector_start, 4, NULL);
		f_close(&fp);

		gui->base_path[strlen(gui->base_path) - 1] = 0;
		save_emummc_cfg(part_idx, sector_start, gui->base_path, drive);
	}
	else
		s_printf(txt_buf, "Time taken: %dm %ds.", timer / 60, timer % 60);

	lv_label_set_text(gui->label_finish, txt_buf);

out:
	free(txt_buf);
	free(gui->base_path);
	boot_storage_unmount();
	emmc_end();
}
