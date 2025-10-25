#include <stdlib.h>
#include <string.h>

#include "fmrb_hal.h"

#include "../../lib/ff14b/source/ff.h"
#include "../../lib/ff14b/source/diskio.h"

int FLASH_disk_erase(void) {
  return RES_OK;
}

int FLASH_disk_initialize(void) {
  return RES_OK;
}

int FLASH_disk_status(void) {
  /* Flash ROM is always ready */
  return RES_OK;
}

int FLASH_disk_read(BYTE *buff, LBA_t sector, UINT count) {
  return RES_OK;
}

int FLASH_disk_write(const BYTE *buff, LBA_t sector, UINT count) {
  return RES_OK;
}

DRESULT FLASH_disk_ioctl(BYTE cmd, void *buff) {
  return RES_OK;
}

static LBA_t clst2sect (	/* !=0:Sector number, 0:Failed (invalid cluster#) */
	FATFS* fs,		/* Filesystem object */
	DWORD clst		/* Cluster# to be converted */
) {
  return 0;
}

void FILE_physical_address(FIL *fp, uint8_t **addr) {
}

int FILE_sector_size(void) {
  return 0;
}

