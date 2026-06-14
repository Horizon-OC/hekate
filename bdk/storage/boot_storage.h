#ifndef _BOOT_STORAGE_H
#define _BOOT_STORAGE_H

#include <libs/fatfs/ff.h>
#include <utils/types.h>

// check if boot1 (1mb), boot1, gpp, sd (in that order) have fat32 partition,
// mount the partition and set the current drive to it
bool boot_storage_mount();

void boot_storage_unmount();
void boot_storage_end();

bool boot_storage_get_mounted();
bool boot_storage_get_initialized();

void *boot_storage_file_read(const char *path, u32 *fsize);
int boot_storage_save_to_file(const void *buf, u32 size, const char *filename);

FATFS *boot_storage_get_fs();

u8 boot_storage_get_drive();

#endif