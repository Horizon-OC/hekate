/*
 * Copyright (c) 2019-2021 CTCaer
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

#ifndef _EMUSD_H
#define _EMUSD_H

#include <bdk.h>

typedef struct _emusd_cfg_t
{
	int   enabled;
	u64   sector;
	u32   id;
	char *path;
	// Internal.
	int fs_ver;
	char *emummc_file_based_path;

} emusd_cfg_t;

extern emusd_cfg_t emu_sd_cfg;

void emusd_load_cfg();
bool emusd_set_path(char *path);
int  emusd_storage_init_mmc();
int  emusd_storage_end();
int  emusd_storage_read(u32 sector, u32 num_sectors, void *buf);
int  emusd_storage_write(u32 sector, u32 num_sectors, void *buf);
sdmmc_storage_t *emusd_get_storage();
bool emusd_is_gpt();
int emusd_get_fs_type();
bool emusd_mount();
bool emusd_unmount();
void *emusd_file_read(const char *path, u32 *fsize);

#endif