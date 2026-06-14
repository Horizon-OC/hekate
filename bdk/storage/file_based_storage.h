#ifndef _FILE_BASED_STORAGE_H
#define _FILE_BASED_STORAGE_H

#include <bdk.h>


int file_based_storage_init(const char *base_path);
void file_based_storage_end();

int file_based_storage_read(u32 sector, u32 num_sectors, void *buf);
int file_based_storage_write(u32 sector, u32 num_sectors, void *buf);

u32 file_based_storage_get_total_size();

#endif