/*-----------------------------------------------------------------------*/
/* Low level disk I/O module skeleton for FatFs                          */
/* (C) ChaN, 2016                                                        */
/* (C) CTCaer, 2018-2025                                                 */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include <storage/emmc.h>
#include <string.h>

#include <bdk.h>

#include <libs/fatfs/diskio.h>	/* FatFs lower layer API */
#include <fatfs_cfg.h>
#include "../../storage/sfd.h"

static u32 sd_rsvd_sectors = 0;
static u32 ramdisk_sectors = 0;
static u32 emummc_sectors = 0;
static u32 bis_sectors = 0;
static u32 sfd_sectors = 0;

static bool bis_write_allowed = false;

static u32 cur_partition;

static void save_cur_partition(BYTE pdrv){
	bool save = false;
	switch(pdrv){
	case DRIVE_BOOT1:
	case DRIVE_BOOT1_1MB:
	case DRIVE_EMMC:
		save = true;
		break;
	case DRIVE_SD:
	case DRIVE_RAM:
		break;
	case DRIVE_BIS:
	case DRIVE_EMU:
		if(nx_emmc_bis_get_storage() == &emmc_storage){
			save = true;
		}
		break;
	case DRIVE_SFD:
		if(sfd_get_storage() == &emmc_storage){
			save = true;
		}
		break;
	default:
		break;
	}

	if(save){
		cur_partition = emmc_storage.partition;
	}
}

static void restore_cur_partition(BYTE pdrv){
	bool restore = false;
	switch(pdrv){
	case DRIVE_BOOT1:
	case DRIVE_BOOT1_1MB:
	case DRIVE_EMMC:
		restore = true;
		break;
	case DRIVE_SD:
	case DRIVE_RAM:
		break;
	case DRIVE_BIS:
	case DRIVE_EMU:
		if(nx_emmc_bis_get_storage() == &emmc_storage){
			restore = true;
		}
		break;
	case DRIVE_SFD:
		if(sfd_get_storage() == &emmc_storage){
			restore = true;
		}
		break;
	default:
		break;
	}

	if(restore){
		if(emmc_storage.partition != cur_partition){
			emmc_set_partition(cur_partition);
		}
	}
}

static bool ensure_partition(BYTE pdrv){
	u8 part;
	switch(pdrv){
	case DRIVE_BOOT1:
	case DRIVE_BOOT1_1MB:
		part = EMMC_BOOT1;
		break;
	case DRIVE_EMMC:
		part = EMMC_GPP;
		break;
	case DRIVE_SD:
	case DRIVE_RAM:
		return true;
	case DRIVE_BIS:
	case DRIVE_EMU:
	case DRIVE_SFD:
		return true;
	default:
		return false;
	}

	if(emmc_storage.partition != part){
		return emmc_set_partition(part);
	}

	return true;
}

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/
DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
	return 0;
}

/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/
DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{
	return 0;
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/
DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	DWORD sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
	DRESULT res = RES_OK;

	save_cur_partition(pdrv);

	if(!ensure_partition(pdrv)){
		res =  RES_ERROR;
	}

	if(res == RES_OK){
		switch (pdrv)
		{
		case DRIVE_SD:
			res = sdmmc_storage_read(&sd_storage, sector, count, (void *)buff) ? RES_OK : RES_ERROR;
			break;
		case DRIVE_RAM:
			res = ram_disk_read(sector, count, (void *)buff);
			break;
		case DRIVE_EMMC:
			res = sdmmc_storage_read(&emmc_storage, sector, count, (void *)buff) ? RES_OK : RES_ERROR;
			break;
		case DRIVE_BIS:
		case DRIVE_EMU:
			res = nx_emmc_bis_read(sector, count, (void *)buff) ? RES_OK : RES_ERROR;
			break;
		case DRIVE_BOOT1_1MB:
			res = sdmmc_storage_read(&emmc_storage, sector + (0x100000 / 512), count, buff) ? RES_OK : RES_ERROR;
			break;
		case DRIVE_BOOT1:
			res = sdmmc_storage_read(&emmc_storage, sector, count, buff) ? RES_OK : RES_ERROR;
			break;
		case DRIVE_SFD:
			res = sfd_read(sector, count, buff) ? RES_OK : RES_ERROR;
			break;
		}
	}

	restore_cur_partition(pdrv);

	return res;
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/
DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	DWORD sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
	DRESULT res = RES_OK;

	save_cur_partition(pdrv);

	if(!ensure_partition(pdrv)){
		res = RES_ERROR;
	}

	if(res == RES_OK){
		switch (pdrv)
		{
		case DRIVE_SD:
			res =  sdmmc_storage_write(&sd_storage, sector, count, (void *)buff) ? RES_OK : RES_ERROR;
			break;
		case DRIVE_RAM:
			res =  ram_disk_write(sector, count, (void *)buff);
			break;
		case DRIVE_EMMC:
			res =  sdmmc_storage_write(&emmc_storage, sector, count, (void*)buff) ? RES_OK : RES_ERROR;
			break;
		case DRIVE_BIS:
			if (!bis_write_allowed){
				res = RES_WRPRT;
				break;
			}
			res = nx_emmc_bis_write(sector, count, (void *)buff) ? RES_OK : RES_ERROR;
			break;
		case DRIVE_EMU:
			res =  nx_emmc_bis_write(sector, count, (void *)buff) ? RES_OK : RES_ERROR;
			break;
		case DRIVE_BOOT1_1MB:
			res =  sdmmc_storage_write(&emmc_storage, sector + (0x100000 / 512), count, (void*)buff) ? RES_OK : RES_ERROR;
			break;
		case DRIVE_BOOT1:
			res =  sdmmc_storage_write(&emmc_storage, sector, count, (void*)buff) ? RES_OK : RES_ERROR;
			break;
		case DRIVE_SFD:
			res =  sfd_write(sector, count, (void*)buff) ? RES_OK : RES_ERROR;
			break;
		}
	}

	restore_cur_partition(pdrv);

	return res;
}

/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/
DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
	DWORD *buf = (DWORD *)buff;

	switch (pdrv)
	{
	case DRIVE_SD:
		switch (cmd)
		{
		case GET_SECTOR_COUNT:
			*buf = sd_storage.sec_cnt - sd_rsvd_sectors;
			break;
		case GET_BLOCK_SIZE:
			*buf = 32768; // Align to 16MB.
			break;
		}
		break;

	case DRIVE_RAM:
		switch (cmd)
		{
		case GET_SECTOR_COUNT:
			*buf = ramdisk_sectors;
			break;
		case GET_BLOCK_SIZE:
			*buf = 2048; // Align to 1MB.
			break;
		}
		break;

	case DRIVE_BIS:
		switch (cmd)
		{
		case GET_SECTOR_COUNT:
			*buf = bis_sectors;
			break;
		case GET_BLOCK_SIZE:
			*buf = 32768; // Align to 16MB.
			break;
		}
		break;

	case DRIVE_EMU:
		switch (cmd)
		{
		case GET_SECTOR_COUNT:
			*buf = emummc_sectors;
			break;
		case GET_BLOCK_SIZE:
			*buf = 16384; // Align to 8MB (With BOOT0/1 data will be at 16MB BU).
			break;
		}
		break;

	case DRIVE_SFD:
		switch(cmd){
		case GET_SECTOR_COUNT:
			*buf = sfd_sectors;
			break;
		case GET_BLOCK_SIZE:
			*buf = 32768;
			break;
		}
		break;

	default: // Catch all for unknown devices.
		switch (cmd)
		{
		case CTRL_SYNC:
			break;
		case GET_SECTOR_COUNT:
		case GET_BLOCK_SIZE:
			*buf = 0; // Zero value to force default or abort.
			break;
		}
		break;
	}

	return RES_OK;
}

DRESULT disk_set_info (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
	DWORD *buf = (DWORD *)buff;

	if (cmd == SET_SECTOR_COUNT)
	{
		switch (pdrv)
		{
		case DRIVE_SD:
			sd_rsvd_sectors = *buf;
			break;

		case DRIVE_RAM:
			ramdisk_sectors = *buf;
			break;

		case DRIVE_BIS:
			bis_sectors = *buf;
			break;

		case DRIVE_EMU:
			emummc_sectors = *buf;
			break;
		case DRIVE_SFD:
			sfd_sectors = *buf;
			break;
		}
	}
	else if (cmd == SET_WRITE_PROTECT && pdrv == DRIVE_BIS)
		bis_write_allowed = *(bool *)buff;

	return RES_OK;
}
