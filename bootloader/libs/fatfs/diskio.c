/*-----------------------------------------------------------------------*/
/* Low level disk I/O module skeleton for FatFs     (C)ChaN, 2016        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "../../storage/emusd.h"
#include <storage/sd.h>
#include <storage/sdmmc.h>

#include <bdk.h>

#include <libs/fatfs/diskio.h>	/* FatFs lower layer API */
#include <fatfs_cfg.h>

static bool ensure_partition(BYTE pdrv){
	u8 part;
	switch(pdrv){
	case DRIVE_SD:
		return true;
	case DRIVE_BOOT1:
	case DRIVE_BOOT1_1MB:
		part = EMMC_BOOT1;
		break;
	case DRIVE_EMMC:
		part = EMMC_GPP;
		break;
	case DRIVE_EMUSD:
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
	if(!ensure_partition(pdrv)){
		return RES_ERROR;
	}
	
	sdmmc_storage_t *storage = &sd_storage;
	u32 actual_sector = sector;
	switch(pdrv){
		case DRIVE_SD:
			break;
		case DRIVE_BOOT1:
		case DRIVE_EMMC:
			storage = &emmc_storage;
			break;
		case DRIVE_BOOT1_1MB:
			storage = &emmc_storage;
			actual_sector = sector + (0x100000 / 512);
			break;
		case DRIVE_EMUSD:
			return emusd_storage_read(sector, count, buff) ? RES_OK : RES_ERROR;
			break;
		default:
			return RES_ERROR;

	}


	return sdmmc_storage_read(storage, actual_sector, count, buff) ? RES_OK : RES_ERROR;
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
	if(!ensure_partition(pdrv)){
		return RES_ERROR;
	}

	sdmmc_storage_t *storage = &sd_storage;
	u32 actual_sector = sector;
	switch(pdrv){
		case DRIVE_SD:
			break;
		case DRIVE_BOOT1:
		case DRIVE_EMMC:
			storage = &emmc_storage;
			break;
		case DRIVE_BOOT1_1MB:
			storage = &emmc_storage;
			actual_sector = sector + (0x100000 / 512);
			break;
		case DRIVE_EMUSD:
			return emusd_storage_write(sector, count, (void*)buff) ? RES_OK : RES_ERROR;
		default:
			return RES_ERROR;

	}


	return sdmmc_storage_write(storage, actual_sector, count, (void*)buff) ? RES_OK : RES_ERROR;
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
	return RES_OK;
}
