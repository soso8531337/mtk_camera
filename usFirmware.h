/*
 * @note
 * Copyright(C) i4season U-Storage, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 *
 */


#ifndef __USFIRMWARE_H_
#define __USFIRMWARE_H_

#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

struct firmwareHeader{
	char vendor[32]; /*vendor*/
	char product[32];/*model*/
	char version[32];	/*version*/
	char md5[32];	/*firmware md5*/
	int32_t size;	/*firmware size*/
};

int usStorage_firmwareINFO(struct scsi_head *header);
int usStorage_firmwareUP(uint8_t *buffer, uint32_t recvSize);
int usStorage_firmwareLicense(uint8_t *buffer, uint32_t recvSize);


#ifdef __cplusplus
}
#endif

#endif /* __USBDISK_H_ */

