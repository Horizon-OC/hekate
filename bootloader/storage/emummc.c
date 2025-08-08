/*
 * Copyright (c) 2019-2022 CTCaer
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

#include <storage/emmc.h>
#include <storage/sd.h>
#include <storage/sdmmc.h>
#include <string.h>
#include <stdlib.h>

#include <bdk.h>

#include "emummc.h"
#include "../config.h"
#include <libs/fatfs/ff.h>
#include <storage/emummc_file_based.h>

emummc_cfg_t emu_cfg = { 0 };

void emummc_load_cfg()
{
	emu_cfg.enabled = 0;
	emu_cfg.path = NULL;
	emu_cfg.sector = 0;
	emu_cfg.id = 0;
	emu_cfg.file_based_part_size = 0;
	emu_cfg.active_part = 0;
	emu_cfg.fs_ver = 0;
	if (!emu_cfg.nintendo_path)
		emu_cfg.nintendo_path = (char *)malloc(0x200);
	if (!emu_cfg.emummc_file_based_path)
		emu_cfg.emummc_file_based_path = (char *)malloc(0x200);

	emu_cfg.nintendo_path[0] = 0;
	emu_cfg.emummc_file_based_path[0] = 0;

	LIST_INIT(ini_sections);
	if (ini_parse(&ini_sections, "emuMMC/emummc.ini", false))
	{
		LIST_FOREACH_ENTRY(ini_sec_t, ini_sec, &ini_sections, link)
		{
			if (ini_sec->type == INI_CHOICE)
			{
				if (strcmp(ini_sec->name, "emummc"))
					continue;

				LIST_FOREACH_ENTRY(ini_kv_t, kv, &ini_sec->kvs, link)
				{
					if (!strcmp("enabled",            kv->key))
						emu_cfg.enabled = atoi(kv->val);
					else if (!strcmp("sector",        kv->key))
						emu_cfg.sector  = strtol(kv->val, NULL, 16);
					else if (!strcmp("id",            kv->key))
						emu_cfg.id      = strtol(kv->val, NULL, 16);
					else if (!strcmp("path",          kv->key))
						emu_cfg.path   = kv->val;
					else if (!strcmp("nintendo_path", kv->key))
						strcpy(emu_cfg.nintendo_path, kv->val);
				}
				break;
			}
		}
	}
}

bool emummc_set_path(char *path)
{
	FIL fp;
	bool found = false;

	// strcpy(emu_cfg.emummc_file_based_path, "sd:");
	strcpy(emu_cfg.emummc_file_based_path, "");
	strcat(emu_cfg.emummc_file_based_path, path);
	strcat(emu_cfg.emummc_file_based_path, "/raw_based");

	if (!f_open(&fp, emu_cfg.emummc_file_based_path, FA_READ))
	{
		if (!f_read(&fp, &emu_cfg.sector, 4, NULL)){
			if (emu_cfg.sector){
				found = true;
				emu_cfg.enabled = 1;
				goto out;
			}
		}
	}

	strcpy(emu_cfg.emummc_file_based_path, "");
	strcat(emu_cfg.emummc_file_based_path, path);
	strcat(emu_cfg.emummc_file_based_path, "/raw_emmc_based");
	if (!f_open(&fp, emu_cfg.emummc_file_based_path, FA_READ))
	{
		if (!f_read(&fp, &emu_cfg.sector, 4, NULL)){
			if (emu_cfg.sector){
				found = true;
				emu_cfg.enabled = 4;
				goto out;
			}
		}
	}

	// strcpy(emu_cfg.emummc_file_based_path, "sd:");
	strcpy(emu_cfg.emummc_file_based_path, "");
	strcat(emu_cfg.emummc_file_based_path, path);
	strcat(emu_cfg.emummc_file_based_path, "/file_based");
	if (!f_stat(emu_cfg.emummc_file_based_path, NULL))
	{
		emu_cfg.sector = 0;
		emu_cfg.path = path;
		emu_cfg.enabled = 1;

		found = true;
		goto out;
	}

	// strcpy(emu_cfg.emummc_file_based_path, "sd:");
	strcpy(emu_cfg.emummc_file_based_path, "");
	strcat(emu_cfg.emummc_file_based_path, path);
	strcat(emu_cfg.emummc_file_based_path, "/file_emmc_based");
	if (!f_stat(emu_cfg.emummc_file_based_path, NULL))
	{
		emu_cfg.sector = 0;
		emu_cfg.path = path;
		emu_cfg.enabled = 4;

		found = true;
		goto out;
	}

out:

	if (found)
	{
		// Get ID from path.
		u32 id_from_path = 0;
		u32 path_size = strlen(path);
		if (path_size >= 4)
			memcpy(&id_from_path, path + path_size - 4, 4);
		emu_cfg.id = id_from_path;

		strcpy(emu_cfg.nintendo_path, path);
		strcat(emu_cfg.nintendo_path, "/Nintendo");
	}

	return found;
}

static int emummc_raw_get_part_off(int part_idx)
{
	switch (part_idx)
	{
	case 0:
		return 2;
	case 1:
		return 0;
	case 2:
		return 1;
	}
	return 2;
}

int emummc_storage_init_mmc()
{
	// FILINFO fno;
	emu_cfg.active_part = 0;

	// Always init eMMC even when in emuMMC. eMMC is needed from the emuMMC driver anyway.
	if (!emmc_initialize(false))
		return 2;

	if (!emu_cfg.enabled || h_cfg.emummc_force_disable)
		return 0;

	bool file_based = false;

	if(emu_cfg.enabled == 4){
		// emmc based
		if(!emu_cfg.sector){
			// file based
			if(!emmc_mount()){
				gfx_printf("emmc mount fail\n");
				return 1;
			}
			strcpy(emu_cfg.emummc_file_based_path, "emmc:");
			strcat(emu_cfg.emummc_file_based_path, emu_cfg.path);
			strcat(emu_cfg.emummc_file_based_path, "/eMMC/");
			file_based = true;
		}else{
			// raw based
			// emmc already initialized
		}
	}else{
		// sd based
		if(!emu_cfg.sector){
			// file based
			if(!sd_mount()){
				return 1;
			}
			strcpy(emu_cfg.emummc_file_based_path, "sd:");
			strcat(emu_cfg.emummc_file_based_path, emu_cfg.path);
			strcat(emu_cfg.emummc_file_based_path, "/eMMC/");
			file_based = true;
		}else{
			// raw based
			if(!sd_initialize(false)){
				return 1;
			}
		}
	}

	if(file_based){
		gfx_printf("file based\n");
		return emummc_storage_file_based_init(emu_cfg.emummc_file_based_path) == 0;
	}

	return 0;
}

int emummc_storage_end()
{
	if(!h_cfg.emummc_force_disable && emu_cfg.enabled && !emu_cfg.sector){
		emummc_storage_file_based_end();
	}
	if(!emu_cfg.enabled || h_cfg.emummc_force_disable || emu_cfg.enabled == 4){
		emmc_end();
	}else{
		sd_end();
	}

	return 1;
}

int emummc_storage_read(u32 sector, u32 num_sectors, void *buf)
{
	sdmmc_storage_t *storage = emu_cfg.enabled == 4 ? &emmc_storage : &sd_storage;
	// FIL fp;
	if (!emu_cfg.enabled || h_cfg.emummc_force_disable)
		return sdmmc_storage_read(&emmc_storage, sector, num_sectors, buf);
	else if (emu_cfg.sector)
	{
		sector += emu_cfg.sector;
		sector += emummc_raw_get_part_off(emu_cfg.active_part) * 0x2000;
		return sdmmc_storage_read(storage, sector, num_sectors, buf);
	}
	else
	{
		return emummc_storage_file_based_read(sector, num_sectors, buf);
	}

	return 1;
}

int emummc_storage_write(u32 sector, u32 num_sectors, void *buf)
{
	sdmmc_storage_t *storage = emu_cfg.enabled == 4 ? &emmc_storage : &sd_storage;
	// FIL fp;
	if (!emu_cfg.enabled || h_cfg.emummc_force_disable)
		return sdmmc_storage_write(&emmc_storage, sector, num_sectors, buf);
	else if (emu_cfg.sector)
	{
		sector += emu_cfg.sector;
		sector += emummc_raw_get_part_off(emu_cfg.active_part) * 0x2000;
		return sdmmc_storage_write(storage, sector, num_sectors, buf);
	}
	else
	{
		return emummc_storage_file_based_write(sector, num_sectors, buf);
	}
}

int emummc_storage_set_mmc_partition(u32 partition)
{
	emu_cfg.active_part = partition;

	if(h_cfg.emummc_force_disable || !emu_cfg.enabled){
		emmc_set_partition(partition);
		return 1;
	}

	if(!emu_cfg.sector){
		emummc_storage_file_base_set_partition(partition);
		return 1;
	}

	if(emu_cfg.enabled != 4){
		emmc_set_partition(partition);
	}else{
		emmc_set_partition(EMMC_GPP);
	}
	return 1;
}

sdmmc_storage_t *emummc_get_storage(){
	return emu_cfg.enabled == 4 ? &emmc_storage : &sd_storage;
}
