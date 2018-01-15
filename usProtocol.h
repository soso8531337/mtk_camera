/*
 * @note
 * Copyright(C) i4season U-Storage, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 *
 */

#ifndef __USPROTOCOL_H_
#define __USPROTOCOL_H_

#include <ctype.h>
#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
#define min(x, y)				(((x) < (y)) ? (x) : (y))

#define htons(A)        ((((uint16_t)(A) & 0xff00) >> 8) | \
                                                   (((uint16_t)(A) & 0x00ff) << 8))

#define htonl(A)        ((((uint32_t)(A) & 0xff000000) >> 24) | \
                                                   (((uint32_t)(A) & 0x00ff0000) >> 8) | \
                                                   (((uint32_t)(A) & 0x0000ff00) << 8) | \
                                                   (((uint32_t)(A) & 0x000000ff) << 24))
#define ntohs(A)		htons(A)
#define ntohl(A)		htonl(A)

enum{
	PROTOCOL_REOK=0,
	PROTOCOL_REPARA,
	PROTOCOL_REGEN,
	PROTOCOL_RTIMOUT,
	PROTOCOL_DISCONNECT,
	PROTOCOL_REINVAILD,
	PROTOCOL_RETRY,
};
uint8_t usProtocol_init(void);
uint8_t usProtocol_DeviceDetect(void *os_priv);
uint8_t usProtocol_ConnectPhone(void);
uint8_t usProtocol_DeviceDisConnect(void);
uint8_t usProtocol_SendPackage(void *buffer, uint32_t size);
uint8_t usProtocol_RecvPackage(void **buffer, uint32_t tsize, uint32_t *rsize);
uint8_t usProtocol_GetAvaiableBuffer(void **buffer, uint32_t *size);
uint8_t usProtocol_SetconnectPhoneStatus(uint8_t status);

#ifdef __cplusplus
}
#endif

#endif /* __USPROTOCOL_H_ */
