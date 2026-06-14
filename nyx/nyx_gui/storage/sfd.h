#ifndef _SFD_H
#define _SFD_H

#include <storage/sdmmc.h>
#include <utils/types.h>

int sfd_read(u32 sector, u32 count, void *buff);
int sfd_write(u32 sector, u32 count, void *buff);

bool sfd_file_based_init(const char *base_path);
bool sfd_init(sdmmc_storage_t *storage, u32 offset, u32 size);
void sfd_end();

sdmmc_storage_t *sfd_get_storage();

#endif