/*
 * @note
 * Copyright(C) i4season U-Storage, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 *
 */


#ifndef __USDISK_H_
#define __USDISK_H_

#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

void usDisk_DeviceInit(void *os_priv);
uint8_t usDisk_DeviceDetect(uint8_t type, void *os_priv);
uint8_t usDisk_DeviceDisConnect(uint8_t type, void *os_priv);
uint8_t usDisk_DiskReadSectors(void *buff, int16_t wlun, uint32_t secStart, uint32_t numSec);
uint8_t usDisk_DiskWriteSectors(void *buff, int16_t wlun, uint32_t secStart, uint32_t numSec);
uint8_t usDisk_DiskNum(void);
uint8_t usDisk_DiskInquiry(int16_t wlun, struct scsi_inquiry_info *inquiry);
uint8_t usDisk_cacheSYNC(int16_t wlun);
uint8_t usDisk_DiskStartStop(uint8_t state);

#ifdef __cplusplus
}
#endif

#endif /* __USBDISK_H_ */

