#ifndef _EMUMMC_FILE_BASED_H
#define _EMUMMC_FILE_BASED_H

#include <bdk.h>

int emummc_storage_file_base_set_partition(u32 partition);
int emummc_storage_file_based_init(const char *path);
void emummc_storage_file_based_end();
int emummc_storage_file_based_write(u32 sector, u32 num_sectors, void *buf);
int emummc_storage_file_based_read(u32 sector, u32 num_sectors, void *buf);
u32 emummc_storage_file_based_get_total_gpp_size(const char *path);
sdmmc_storage_t *emummc_get_storage();

#endif