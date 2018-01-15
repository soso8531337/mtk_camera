/*
 * @brief U-Storage Project
 *
 * @note
 * Copyright(C) i4season, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 */
#include <stdint.h>
#include "usUsb.h"
#include "usSys.h"
#include "protocol.h"
#include "usFirmware.h"
#include "usProtocol.h"
#if defined(NXP_CHIP_18XX)
#include "board.h"
#include "USB.h"
#include "flash_map.h"
#include <ctype.h>
#include <stdio.h>
#include "i2c.h"
#elif defined(GP_CHIP)
//#include "USB.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#elif defined(LINUX)
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <memory.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif

#if defined(DEBUG_ENABLE1)
#define FRIMDEBUG(...) do {printf("[Firmware Mod]");printf(__VA_ARGS__);} while(0)
#else
#define FRIMDEBUG(...)
#endif

typedef struct
{
	unsigned int count[2];
	unsigned int state[4];
	unsigned char buffer[64];   
}MD5_CTX;

#define F(x,y,z) ((x & y) | (~x & z))
#define G(x,y,z) ((x & z) | (y & ~z))
#define H(x,y,z) (x^y^z)
#define I(x,y,z) (y ^ (x | ~z))
#define ROTATE_LEFT(x,n) ((x << n) | (x >> (32-n)))
#define FF(a,b,c,d,x,s,ac) \
{ \
	a += F(b,c,d) + x + ac; \
	a = ROTATE_LEFT(a,s); \
	a += b; \
}
#define GG(a,b,c,d,x,s,ac) \
{ \
	a += G(b,c,d) + x + ac; \
	a = ROTATE_LEFT(a,s); \
	a += b; \
}
#define HH(a,b,c,d,x,s,ac) \
{ \
	a += H(b,c,d) + x + ac; \
	a = ROTATE_LEFT(a,s); \
	a += b; \
}
#define II(a,b,c,d,x,s,ac) \
{ \
	a += I(b,c,d) + x + ac; \
	a = ROTATE_LEFT(a,s); \
	a += b; \
}

struct linfo{    
	char sn[32];
	char license[256];
};
struct allinfo{    
	struct linfo info;
	uint32_t crc;
};

/*********************************************************/
								/* CRC*/
/*********************************************************/
static const uint32_t crc32_table[256] = {
	0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL, 0x076dc419L,
	0x706af48fL, 0xe963a535L, 0x9e6495a3L, 0x0edb8832L, 0x79dcb8a4L,
	0xe0d5e91eL, 0x97d2d988L, 0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L,
	0x90bf1d91L, 0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
	0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L, 0x136c9856L,
	0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL, 0x14015c4fL, 0x63066cd9L,
	0xfa0f3d63L, 0x8d080df5L, 0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L,
	0xa2677172L, 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
	0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L, 0x32d86ce3L,
	0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L, 0x26d930acL, 0x51de003aL,
	0xc8d75180L, 0xbfd06116L, 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L,
	0xb8bda50fL, 0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
	0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL, 0x76dc4190L,
	0x01db7106L, 0x98d220bcL, 0xefd5102aL, 0x71b18589L, 0x06b6b51fL,
	0x9fbfe4a5L, 0xe8b8d433L, 0x7807c9a2L, 0x0f00f934L, 0x9609a88eL,
	0xe10e9818L, 0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
	0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL, 0x6c0695edL,
	0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L, 0x65b0d9c6L, 0x12b7e950L,
	0x8bbeb8eaL, 0xfcb9887cL, 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L,
	0xfbd44c65L, 0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
	0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL, 0x4369e96aL,
	0x346ed9fcL, 0xad678846L, 0xda60b8d0L, 0x44042d73L, 0x33031de5L,
	0xaa0a4c5fL, 0xdd0d7cc9L, 0x5005713cL, 0x270241aaL, 0xbe0b1010L,
	0xc90c2086L, 0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
	0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L, 0x59b33d17L,
	0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL, 0xedb88320L, 0x9abfb3b6L,
	0x03b6e20cL, 0x74b1d29aL, 0xead54739L, 0x9dd277afL, 0x04db2615L,
	0x73dc1683L, 0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
	0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L, 0xf00f9344L,
	0x8708a3d2L, 0x1e01f268L, 0x6906c2feL, 0xf762575dL, 0x806567cbL,
	0x196c3671L, 0x6e6b06e7L, 0xfed41b76L, 0x89d32be0L, 0x10da7a5aL,
	0x67dd4accL, 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
	0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L, 0xd1bb67f1L,
	0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL, 0xd80d2bdaL, 0xaf0a1b4cL,
	0x36034af6L, 0x41047a60L, 0xdf60efc3L, 0xa867df55L, 0x316e8eefL,
	0x4669be79L, 0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
	0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL, 0xc5ba3bbeL,
	0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L, 0xc2d7ffa7L, 0xb5d0cf31L,
	0x2cd99e8bL, 0x5bdeae1dL, 0x9b64c2b0L, 0xec63f226L, 0x756aa39cL,
	0x026d930aL, 0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
	0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L, 0x92d28e9bL,
	0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L, 0x86d3d2d4L, 0xf1d4e242L,
	0x68ddb3f8L, 0x1fda836eL, 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L,
	0x18b74777L, 0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
	0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L, 0xa00ae278L,
	0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L, 0xa7672661L, 0xd06016f7L,
	0x4969474dL, 0x3e6e77dbL, 0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L,
	0x37d83bf0L, 0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
	0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L, 0xbad03605L,
	0xcdd70693L, 0x54de5729L, 0x23d967bfL, 0xb3667a2eL, 0xc4614ab8L,
	0x5d681b02L, 0x2a6f2b94L, 0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL,
	0x2d02ef8dL
};

static inline uint32_t 
crc32(uint32_t val, const void *ss, int len)
{
	const unsigned char *s = ss;
        while (--len >= 0)
                val = crc32_table[(val ^ *s++) & 0xff] ^ (val >> 8);
        return val;
}

/*********************************************************/
								/* MD5*/
/*********************************************************/
static unsigned char PADDING[]={0x80,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

static void MD5Encode(unsigned char *output,unsigned int *input,unsigned int len)
{
	unsigned int i = 0,j = 0;
	while(j < len)
	{
		output[j] = input[i] & 0xFF;  
		output[j+1] = (input[i] >> 8) & 0xFF;
		output[j+2] = (input[i] >> 16) & 0xFF;
		output[j+3] = (input[i] >> 24) & 0xFF;
		i++;
		j+=4;
	}
}
static void MD5Decode(unsigned int *output,unsigned char *input,unsigned int len)
{
	unsigned int i = 0,j = 0;
	while(j < len)
	{
		output[i] = (input[j]) |
			(input[j+1] << 8) |
			(input[j+2] << 16) |
			(input[j+3] << 24);
		i++;
		j+=4; 
	}
}
static void MD5Transform(unsigned int state[4],unsigned char block[64])
{
	unsigned int a = state[0];
	unsigned int b = state[1];
	unsigned int c = state[2];
	unsigned int d = state[3];
	unsigned int x[64];
	MD5Decode(x,block,64);
	FF(a, b, c, d, x[ 0], 7, 0xd76aa478); /* 1 */
	FF(d, a, b, c, x[ 1], 12, 0xe8c7b756); /* 2 */
	FF(c, d, a, b, x[ 2], 17, 0x242070db); /* 3 */
	FF(b, c, d, a, x[ 3], 22, 0xc1bdceee); /* 4 */
	FF(a, b, c, d, x[ 4], 7, 0xf57c0faf); /* 5 */
	FF(d, a, b, c, x[ 5], 12, 0x4787c62a); /* 6 */
	FF(c, d, a, b, x[ 6], 17, 0xa8304613); /* 7 */
	FF(b, c, d, a, x[ 7], 22, 0xfd469501); /* 8 */
	FF(a, b, c, d, x[ 8], 7, 0x698098d8); /* 9 */
	FF(d, a, b, c, x[ 9], 12, 0x8b44f7af); /* 10 */
	FF(c, d, a, b, x[10], 17, 0xffff5bb1); /* 11 */
	FF(b, c, d, a, x[11], 22, 0x895cd7be); /* 12 */
	FF(a, b, c, d, x[12], 7, 0x6b901122); /* 13 */
	FF(d, a, b, c, x[13], 12, 0xfd987193); /* 14 */
	FF(c, d, a, b, x[14], 17, 0xa679438e); /* 15 */
	FF(b, c, d, a, x[15], 22, 0x49b40821); /* 16 */

	/* Round 2 */
	GG(a, b, c, d, x[ 1], 5, 0xf61e2562); /* 17 */
	GG(d, a, b, c, x[ 6], 9, 0xc040b340); /* 18 */
	GG(c, d, a, b, x[11], 14, 0x265e5a51); /* 19 */
	GG(b, c, d, a, x[ 0], 20, 0xe9b6c7aa); /* 20 */
	GG(a, b, c, d, x[ 5], 5, 0xd62f105d); /* 21 */
	GG(d, a, b, c, x[10], 9,  0x2441453); /* 22 */
	GG(c, d, a, b, x[15], 14, 0xd8a1e681); /* 23 */
	GG(b, c, d, a, x[ 4], 20, 0xe7d3fbc8); /* 24 */
	GG(a, b, c, d, x[ 9], 5, 0x21e1cde6); /* 25 */
	GG(d, a, b, c, x[14], 9, 0xc33707d6); /* 26 */
	GG(c, d, a, b, x[ 3], 14, 0xf4d50d87); /* 27 */
	GG(b, c, d, a, x[ 8], 20, 0x455a14ed); /* 28 */
	GG(a, b, c, d, x[13], 5, 0xa9e3e905); /* 29 */
	GG(d, a, b, c, x[ 2], 9, 0xfcefa3f8); /* 30 */
	GG(c, d, a, b, x[ 7], 14, 0x676f02d9); /* 31 */
	GG(b, c, d, a, x[12], 20, 0x8d2a4c8a); /* 32 */

	/* Round 3 */
	HH(a, b, c, d, x[ 5], 4, 0xfffa3942); /* 33 */
	HH(d, a, b, c, x[ 8], 11, 0x8771f681); /* 34 */
	HH(c, d, a, b, x[11], 16, 0x6d9d6122); /* 35 */
	HH(b, c, d, a, x[14], 23, 0xfde5380c); /* 36 */
	HH(a, b, c, d, x[ 1], 4, 0xa4beea44); /* 37 */
	HH(d, a, b, c, x[ 4], 11, 0x4bdecfa9); /* 38 */
	HH(c, d, a, b, x[ 7], 16, 0xf6bb4b60); /* 39 */
	HH(b, c, d, a, x[10], 23, 0xbebfbc70); /* 40 */
	HH(a, b, c, d, x[13], 4, 0x289b7ec6); /* 41 */
	HH(d, a, b, c, x[ 0], 11, 0xeaa127fa); /* 42 */
	HH(c, d, a, b, x[ 3], 16, 0xd4ef3085); /* 43 */
	HH(b, c, d, a, x[ 6], 23,  0x4881d05); /* 44 */
	HH(a, b, c, d, x[ 9], 4, 0xd9d4d039); /* 45 */
	HH(d, a, b, c, x[12], 11, 0xe6db99e5); /* 46 */
	HH(c, d, a, b, x[15], 16, 0x1fa27cf8); /* 47 */
	HH(b, c, d, a, x[ 2], 23, 0xc4ac5665); /* 48 */

	/* Round 4 */
	II(a, b, c, d, x[ 0], 6, 0xf4292244); /* 49 */
	II(d, a, b, c, x[ 7], 10, 0x432aff97); /* 50 */
	II(c, d, a, b, x[14], 15, 0xab9423a7); /* 51 */
	II(b, c, d, a, x[ 5], 21, 0xfc93a039); /* 52 */
	II(a, b, c, d, x[12], 6, 0x655b59c3); /* 53 */
	II(d, a, b, c, x[ 3], 10, 0x8f0ccc92); /* 54 */
	II(c, d, a, b, x[10], 15, 0xffeff47d); /* 55 */
	II(b, c, d, a, x[ 1], 21, 0x85845dd1); /* 56 */
	II(a, b, c, d, x[ 8], 6, 0x6fa87e4f); /* 57 */
	II(d, a, b, c, x[15], 10, 0xfe2ce6e0); /* 58 */
	II(c, d, a, b, x[ 6], 15, 0xa3014314); /* 59 */
	II(b, c, d, a, x[13], 21, 0x4e0811a1); /* 60 */
	II(a, b, c, d, x[ 4], 6, 0xf7537e82); /* 61 */
	II(d, a, b, c, x[11], 10, 0xbd3af235); /* 62 */
	II(c, d, a, b, x[ 2], 15, 0x2ad7d2bb); /* 63 */
	II(b, c, d, a, x[ 9], 21, 0xeb86d391); /* 64 */
	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
}

static void MD5Init(MD5_CTX *context)
{
	context->count[0] = 0;
	context->count[1] = 0;
	context->state[0] = 0x67452301;
	context->state[1] = 0xEFCDAB89;
	context->state[2] = 0x98BADCFE;
	context->state[3] = 0x10325476;
}
static void MD5Update(MD5_CTX *context,unsigned char *input,unsigned int inputlen)
{
	unsigned int i = 0,index = 0,partlen = 0;
	index = (context->count[0] >> 3) & 0x3F;
	partlen = 64 - index;
	context->count[0] += inputlen << 3;
	if(context->count[0] < (inputlen << 3))
		context->count[1]++;
	context->count[1] += inputlen >> 29;

	if(inputlen >= partlen)
	{
		memcpy(&context->buffer[index],input,partlen);
		MD5Transform(context->state,context->buffer);
		for(i = partlen;i+64 <= inputlen;i+=64)
			MD5Transform(context->state,&input[i]);
		index = 0;        
	}  
	else
	{
		i = 0;
	}
	memcpy(&context->buffer[index],&input[i],inputlen-i);
}
static void MD5Final(MD5_CTX *context,unsigned char digest[16])
{
	unsigned int index = 0,padlen = 0;
	unsigned char bits[8];
	index = (context->count[0] >> 3) & 0x3F;
	padlen = (index < 56)?(56-index):(120-index);
	MD5Encode(bits,context->count,8);
	MD5Update(context,PADDING,padlen);
	MD5Update(context,bits,8);
	MD5Encode(digest,context->state,16);
}


#if defined(NXP_CHIP_18XX)

static int wirteToFlash(uint8_t *payload, uint32_t paySize, int flag)
{
	uint32_t writeSize, curSize = 0;
	int tflag = 0;

#define WRITE_FLASH_SIZE		(16*1024)
	if(!payload){
		return -1;
	}
	while(curSize < paySize){
		if(paySize-curSize > WRITE_FLASH_SIZE){
			writeSize = WRITE_FLASH_SIZE;			
			tflag = 0;
		}else{
			tflag = flag;
			writeSize = paySize-curSize;
		}
		if(fw_upgrade(payload+curSize, writeSize, tflag) < 0){
			FRIMDEBUG("Flash Write Error[%d/%d]\r\n", curSize, paySize);
			return -1;
		}
		curSize += writeSize;
		FRIMDEBUG("Flash Write Successful[%d/%d]\r\n", curSize, paySize);
	}

	return 0;
}

static int upgradeFirmware(char *firmware)
{
	struct firmwareHeader firminfo;
	char md5str[33] = {0};
	MD5_CTX c;
	uint8_t *buffer = NULL;
	uint32_t size = 0, curPtr = 0, md5size = 0, totalSize = 0;
	unsigned char decrypt[16] = {0};
	int i;
	
	memset(&firminfo, 0, sizeof(struct firmwareHeader));
	if(spi_read(FLASH_UPGRADE_IMAGE, (unsigned char*)&firminfo, 
			sizeof(struct firmwareHeader)) != 0){
		FRIMDEBUG("Flash Read Error\r\n");
		return -1;
	}	
	FRIMDEBUG("Firmware in MD5: %s!!!!\r\n", firminfo.md5);

	curPtr = sizeof(struct firmwareHeader);
	memset(&c, 0, sizeof(c));
	MD5Init(&c);
	totalSize = firminfo.size+sizeof(struct firmwareHeader);
	while (curPtr < totalSize){
		if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
			FRIMDEBUG("usProtocol_GetAvaiableBuffer Failed\r\n");
			return -1;
		}
		memset(buffer, 0, size);
		if((totalSize-curPtr) > 16*1024){
			md5size = 16*1024;
		}else{
			md5size = totalSize-curPtr;
		}
		FRIMDEBUG("Firmware Read:0x%x buffer=%p  md5size=%d\r\n", 
					(FLASH_UPGRADE_IMAGE+curPtr), buffer, md5size);
		if(spi_read(FLASH_UPGRADE_IMAGE+curPtr, buffer, md5size) != 0){
			FRIMDEBUG("Flash Read Error\r\n");
			return -1;
		}

		MD5Update(&c,buffer, md5size);
		curPtr += md5size;
	}
	MD5Final(&c, decrypt);

	for(i=0;i<16;i++){		
		sprintf(&(md5str[i*2]),"%02x",decrypt[i]);
	}
	FRIMDEBUG("Firmware MD5:%s!!!!!!!!!!!!!!!!!!!!!!!!!\r\n", md5str);

	if(strncmp(md5str, firminfo.md5, sizeof(firminfo.md5)) == 0){
		FRIMDEBUG("Firmware Check MD5 Ok, upgrade successful\r\n");
		set_flage();
		return 0;
	}
	FRIMDEBUG("Firmware Check MD5 Not Sambe:OK:%s!!! Bad:%s!!!!\r\n", firminfo.md5, md5str);

	return -1;
}

int usStorage_firmwareINFO(struct scsi_head *header)
{
	vs_acessory_parameter dinfo;	
	int flen = sizeof(vs_acessory_parameter);
	uint8_t *buffer = NULL, rc = 0;
	uint32_t size = 0, total = 0;
	struct firmwareHeader firminfo;
	typedef struct __image_info{
		unsigned char flag;
		unsigned int offset;
	}image_info;	
	struct allinfo license;

	if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
		FRIMDEBUG("usProtocol_GetAvaiableBuffer Failed\r\n");
		return 1;
	}	
	FRIMDEBUG("AvaiableBuffer 0x%p[%dBytes]\r\n", buffer, size);
	
	total = sizeof(struct scsi_head);
	memcpy(buffer, header, total);

	
	image_info image;
	memset(&image, 0, sizeof(image_info));
	if(spi_read(FLASH_SYS_DATA, (unsigned char*)&image, sizeof(image_info))){
		printf("Read FLASH_SYS_DATA Error\r\n");
		header->relag = 1;
		return usProtocol_SendPackage((void*)header, total);
	}
	memset(&dinfo, 0, sizeof(vs_acessory_parameter));
	memset(&firminfo, 0, sizeof(struct firmwareHeader));
	if(image.flag == 1){
		spi_read(FLASH_UPGRADE_IMAGE, (unsigned char*)&firminfo, sizeof(struct firmwareHeader));
	}else{
		spi_read(FLASH_DEF_IMAGE, (unsigned char*)&firminfo, sizeof(struct firmwareHeader));
	}
	memset(&license, 0, sizeof(struct allinfo));
	spi_read(FLASH_LICENCE_ADDR, (unsigned char*)&license, sizeof(struct allinfo));
	if(crc32(0,(void*)&(license.info),sizeof(license.info)) 
			 != license.crc){
		printf("ERROR: SN/License:\r\nSN:%s\r\nLicense:%s\r\n", 
								license.info.sn, license.info.license); 
		/*We not send error to phone, if we did will cause get firmware inforation error*/
	//	header->relag = 1;
	//	return usProtocol_SendPackage((void*)header, total);
		memset(dinfo.sn, 0, sizeof(dinfo.sn));		
		memset(dinfo.license, 0, sizeof(dinfo.license));
	}else{
		printf("SN/License:\r\nSN:%s\r\nLicense:%s\r\n", 
							license.info.sn, license.info.license);		
		strncpy(dinfo.sn, license.info.sn, sizeof(dinfo.sn)-1);
		strncpy(dinfo.license, license.info.license, sizeof(dinfo.license)-1);
	}
	strcpy(dinfo.fw_version, firminfo.version);
	strcpy(dinfo.hw_version, "1.0");
	strcpy(dinfo.manufacture, firminfo.vendor);
	strcpy(dinfo.model_name, firminfo.product);
	strcpy(dinfo.cardid, "1234567");
	memcpy(buffer+total, &dinfo, flen);
	total += flen;
	
	if((rc = usProtocol_SendPackage(buffer, total)) != 0){
		FRIMDEBUG("usProtocol_SendPackage Failed\r\n");
		return rc;
	}
	printf("usStorage_firmwareINFO Successful Firmware Info:\r\nVendor:%s\r\nProduct:%s\r\nVersion:%s\r\nSerical:%s\r\nLicense:%s\r\n", 
					dinfo.manufacture, dinfo.model_name, dinfo.fw_version, dinfo.sn, dinfo.license);
	
	return 0;
}

int usStorage_firmwareUP(uint8_t *buffer, uint32_t recvSize)
{
	struct scsi_head scsi;	
	uint8_t headbuf[PRO_HDR] = {0}; 
	uint32_t hSize = recvSize;
	uint8_t res;

	if(recvSize < PRO_HDR){
		FRIMDEBUG("Frimware Request Error\r\n");
		return -1;
	}
	/*We must save the header in var*/
	memcpy(&scsi, buffer, PRO_HDR);
	if(scsi.ctrid == SCSI_UPDATE_START){
		FRIMDEBUG("Prepare To update Firmware\r\n");		
	}else if(scsi.ctrid == SCSI_UPDATE_DATA){
		uint8_t *payload = buffer+SCSI_HEAD_SIZE;
		uint32_t paySize, curSize = 0, firmSize = 0;
		int32_t flag = 0;		
		struct firmwareHeader *firminfo = NULL;		
	
		paySize= recvSize-PRO_HDR;
		if(paySize){
			FRIMDEBUG("Write %uBytes to Firmware\r\n", paySize);
			firminfo = (struct firmwareHeader *)payload;
			firmSize = firminfo->size+sizeof(struct firmwareHeader);
			FRIMDEBUG("Firmware Size %d\r\n", firmSize);
			if(paySize == firmSize &&paySize == scsi.len){
				flag = 1;
			}
			if(wirteToFlash(payload, paySize, flag) < 0){				
				scsi.relag = 1;
				goto sndRes;
			}
			
			curSize += paySize;
		}
		while(curSize < scsi.len){
			uint8_t *pbuffer = NULL;
			if((res = usProtocol_RecvPackage((void **)&pbuffer, hSize, &paySize)) != 0){
				if(res == PROTOCOL_RTIMOUT){
					FRIMDEBUG("Firmware Receive Timeout\r\n");
					return -1;	
				}else{
					FRIMDEBUG("usProtocol_RecvPackage Failed IN Firmware\r\n");
					/*Write to Phone*/
					scsi.relag = 1;		
					goto sndRes;
				}
			}
			hSize+= paySize;
			if(firminfo == NULL){
				firminfo = (struct firmwareHeader *)pbuffer;				
				firmSize = firminfo->size+sizeof(struct firmwareHeader);			
				FRIMDEBUG("Second Receive Firmware Size %d\r\n", firmSize);
			}
			if(curSize+paySize == scsi.len){
				FRIMDEBUG("Last Part Firmware [PartT:%uBytes/%dBytes]\r\n", paySize, scsi.len);
				flag = 1;
			}else{
				flag = 0;
			}
			
			if(wirteToFlash(pbuffer, paySize, flag)){
				FRIMDEBUG("fw_upgrade wirte flash failed\r\n");				
				scsi.relag = 1;
				goto sndRes;
			}

			FRIMDEBUG("Write Firmware: %uBytes [PartT:%uBytes/%dBytes]\r\n", 
								paySize, curSize, scsi.len);			
			curSize += paySize;
		}
		
		if(scsi.len < firmSize){
			FRIMDEBUG("Firmware Send Multi Time Not Support:%d/%d\r\n", scsi.len, firmSize);
			scsi.relag = 1;
			goto sndRes;
		}

		FRIMDEBUG("Write Firmware %d Successful:%d\r\n", 
						firmSize, curSize);
	}else if(scsi.ctrid == SCSI_UPDATE_END){
		FRIMDEBUG("Check Firmware MD5!!\r\n");
		if(upgradeFirmware(NULL) < 0){
			FRIMDEBUG("upgradeFirmware  Error\r\n"); 		
			scsi.relag = 1;
		}else{
			/*notify i2c to restart nxp*/
			FRIMDEBUG("UPdate Firmware Successful...Restart NXP!!\r\n");
			i2c_ioctl(IOCTL_POWER_RESET_I2C, NULL);
		}
	}else{
		FRIMDEBUG("unknown Comannd ID:%d\r\n", scsi.ctrid);
		scsi.relag = 1;
	}

sndRes:

	memcpy(headbuf, &scsi, PRO_HDR);
	/*Send To Phone*/
	if(usProtocol_SendPackage(headbuf, PRO_HDR)){
		FRIMDEBUG("Send To Phone[Just Header Error]\r\n");
		return 1;
	}

	return 0;
}

int usStorage_firmwareLicense(uint8_t *buffer, uint32_t recvSize)
{
	struct scsi_head scsi;	
	uint8_t headbuf[PRO_HDR] = {0}; 
	uint32_t hSize = recvSize;
	uint8_t *payload = buffer+SCSI_HEAD_SIZE;
	uint32_t paySize, curSize = 0, totalSize = sizeof(struct allinfo);
	struct allinfo license;
	uint8_t *pbuffer = NULL, res, *plince = (uint8_t *)&license;	
	
	if(recvSize < PRO_HDR){
		FRIMDEBUG("Frimware License Request Error\r\n");
		return -1;
	}
	/*We must save the header in var*/
	memset(plince, 0, sizeof(struct allinfo));
	memcpy(&scsi, buffer, PRO_HDR);
		
	paySize= recvSize-PRO_HDR;
	if(paySize != totalSize){
		FRIMDEBUG("Receive InComplete Licence Information[%d/%d]\r\n", paySize, totalSize);
		if(paySize){
			memcpy(plince, payload, paySize);
			curSize = paySize;
		}
		while(curSize < scsi.len){		
			if((res = usProtocol_RecvPackage((void **)&pbuffer, hSize, &paySize)) != 0){
				printf("usProtocol_RecvPackage Failed IN firmwareLicense [%d]\r\n", res);
				/*Write to Phone*/
				scsi.relag = 1;
				goto sndRes;
			}
			hSize+= paySize;
			memcpy(plince+curSize, pbuffer, paySize);
			curSize += paySize;
			FRIMDEBUG("Receive License Information[PartT:%uBytes/%dBytes/%dBytes]\r\n", 
							 paySize, curSize, scsi.len);			
		}
		
	}else{
		memcpy(plince, payload, totalSize);
	}
	FRIMDEBUG("License Information:\r\n\tSN:%s\r\n\tLicense:%s\r\n\tCRC:%u\r\n", 
					 license.info.sn, license.info.license, license.crc);			

	if(crc32(0,(void*)&(license.info),sizeof(license.info)) 
			 != license.crc){
		FRIMDEBUG("ERROR: SN/License:\r\nSN:%s\r\nLicense:%s\r\n", 
								license.info.sn, license.info.license); 
		/*Write to Phone*/
		scsi.relag = 1;
		goto sndRes;
	}
	/*Write to device*/
	if(vs_write_info(FLASH_LICENCE_ADDR, plince, totalSize) != 0){
		FRIMDEBUG("Write License Failed\r\n");
		/*Write to Phone*/
		scsi.relag = 1;
		goto sndRes;		
	}

	FRIMDEBUG("Wirte License Successful\r\n");
	
sndRes:

	memcpy(headbuf, &scsi, PRO_HDR);
	/*Send To Phone*/
	if(usProtocol_SendPackage(headbuf, PRO_HDR)){
		FRIMDEBUG("Send To Phone[Just Header Error]\r\n");
		return 1;
	}

	return 0;
}

#elif defined(GP_CHIP)
int usStorage_firmwareINFO(struct scsi_head *header)
{
	vs_acessory_parameter dinfo;	
	int flen = sizeof(vs_acessory_parameter);
	uint8_t *buffer = NULL, rc = 0;
	uint32_t size = 0, total = 0;
	if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
		FRIMDEBUG("usProtocol_GetAvaiableBuffer Failed\r\n");
		return 1;
	}	
	FRIMDEBUG("AvaiableBuffer 0x%p[%dBytes]\r\n", buffer, size);

	total = sizeof(struct scsi_head);
	memcpy(buffer, header, total);


	memset(&dinfo, 0, sizeof(vs_acessory_parameter));
	strcpy(dinfo.fw_version, "2.000.002");
	strcpy(dinfo.hw_version, "1.0");
	strcpy(dinfo.manufacture, "i4season");
	strcpy(dinfo.model_name, "uStorage");
	strcpy(dinfo.sn, "1234567890");
	strcpy(dinfo.cardid, "1234567");
	strcpy(dinfo.license, "1234567890");
	memcpy(buffer+total, &dinfo, flen);
	total += flen;

	if((rc = usProtocol_SendPackage(buffer, total)) != 0){
		FRIMDEBUG("usProtocol_SendPackage Failed\r\n");
		return rc;
	}
	printf("usStorage_firmwareINFO Successful Firmware Info:\r\nVendor:%s\r\nProduct:%s\r\nVersion:%s\r\nSerical:%s\r\nLicense:%s\r\n", 
					dinfo.manufacture, dinfo.model_name, dinfo.fw_version, dinfo.sn, dinfo.license);

	return 0;
}
int usStorage_firmwareUP(uint8_t *buffer, uint32_t recvSize)
{
	return 0;
}

int usStorage_firmwareLicense(uint8_t *buffer, uint32_t recvSize)
{
	printf("Dose Not Support Wirte License\r\n");
	return usProtocol_SendPackage(buffer, PRO_HDR);
}

#elif defined(LINUX)

#define FILENAME "/dev/mtdblock2"
#define LICENSE_OFFSET 0xf000
#define SYS_FIRMCONF		"/etc/firmware"
#define USTORAGE_UPPATH			"/tmp/u-storage.firmware"
#define MD5_FLAG			"MD5="
#define SYS_FIRM_SKIP		(64*1024) //skip 64KB
#define SYS_FIRM_UPSCRIP		"/sbin/sysupgrade"
#define SYS_UPGRADE_LED_BEGIN			"pioctl fan 2"
#define SYS_UPGRADE_LED_END			"pioctl fan 1"

static int compute_md5(char *filename, off_t offset, char *md5buf)
{
	MD5_CTX c;
	int fd, i;
	char tmp[33] = {0};
	unsigned char decrypt[16];	
	unsigned char buf[1024*16];

	if(!filename){
		return -1;
	}	
	fd = open(filename, O_RDONLY);
	if(fd < 0){
		FRIMDEBUG("ERROR: couldn't open %s\r\n", filename);
		return -1;
	}
	if(offset &&
			lseek(fd, offset, SEEK_SET) < 0){
		FRIMDEBUG("ERROR: Lseek %s Error:%s\r\n", 
				filename, strerror(errno));
		close(fd);
		return -1;
	}
	
	MD5Init(&c);
	for (;;){
		i=read(fd,buf,1024*16);
		if (i < 0) {
			FRIMDEBUG("Read error.errmsg: %s\r\n", strerror(errno));
			close(fd);
			return -1; 
		}else if ( i == 0 ){
			break;
		}
		MD5Update(&c,buf,(unsigned long)i);
	}
	MD5Final(&c, decrypt);
	close(fd);
	FRIMDEBUG("%s MD5:", filename);
	for(i=0;i<16;i++){		
		printf("%02x",decrypt[i]);
		sprintf(&(tmp[i*2]),"%02x",decrypt[i]);
	}
	printf("\n");
	memcpy(md5buf, tmp, strlen(tmp));
	
	return 0;
}

static int showSnLicense(struct allinfo *info)
{
	int fd;

	if(!info){
		return -1;
	}
	fd = open(FILENAME,O_RDONLY);
	if(fd < 0){
		return -1;
	}
	lseek(fd, LICENSE_OFFSET, SEEK_SET);
	if(read(fd, info, sizeof(struct allinfo)) < 0){
		FRIMDEBUG("Read %s Failed:%s\r\n", 
						FILENAME, strerror(errno));
		close(fd);
		return -1;
	}
	close(fd);
	if(crc32(0,(void*)&info->info,sizeof(info->info)) 
			 != info->crc){
		FRIMDEBUG("ERROR: SN/License:\r\nSN:%s\r\nLicense:%s\r\n", 
								info->info.sn, info->info.license); 
		return -1;
	}
	FRIMDEBUG("OK: SN/License:\r\nSN:%s\r\nLicense:%s\r\n", 
							info->info.sn, info->info.license); 
	return 0;
}

static int writeFirmware(int fd, uint8_t *payload, uint32_t size)
{
	uint32_t already = 0;
	int res;
	
	if(!payload || !size){
		return -1;
	}
	do {
		res  = write(fd, payload + already, size - already);
		if (res < 0) {
			if(errno ==  EINTR ||
					errno ==  EAGAIN){
				continue;
			}
			FRIMDEBUG("Write Firmware %s Error:%s\r\n", 
					USTORAGE_UPPATH, strerror(errno));
			close(fd);
			return -1;
		}
		already += res;
	} while (already < size);

	return 0;
}

static int upgradeFirmware(char *firmware)
{
	char md5str[33] = {0}, buffer[512] = {0}, md5cmp[33] = {0};
	char syscmd[1024] = {0};
	char *ptr = NULL;
	int count=10, i;
	FILE *fp;

	if(!firmware){
		return -1;
	}
	if(compute_md5(firmware, SYS_FIRM_SKIP, md5str) < 0){
		FRIMDEBUG("Compute MD5:%s Failed\r\n", firmware);
		return -1;
	}
	/*Check md5 value in firmware*/
	fp = fopen(firmware, "r");
	if(!fp){
		FRIMDEBUG("FOpen:%s Failed\r\n", firmware);
		return -1;
	}
	for(i=0; i < count; i++){
		memset(buffer, 0, sizeof(buffer));
		if(fgets(buffer, sizeof(buffer)-1, fp) <= 0){
			FRIMDEBUG("Read:%s Failed\r\n", firmware);
			fclose(fp);
			return -1;
		}
		if(strncmp(buffer, MD5_FLAG, strlen(MD5_FLAG)) == 0){
			FRIMDEBUG("Found MD5 String:%s\r\n", buffer);
			break;
		}
	}
	fclose(fp);
	
	if(i == count){
		FRIMDEBUG("No Found MD5 String:%s\r\n", firmware);
		return -1;
	}
	i = 0;
	ptr = buffer + strlen(MD5_FLAG);
	while(*ptr != '\n' && i < sizeof(md5cmp)){
		md5cmp[i] = *ptr;
		ptr++;
		i++;
	}
	if(strcasecmp(md5str, md5cmp)){
		FRIMDEBUG("MD5 String Not Same:%s<-->%s\r\n", md5str, md5cmp);
		return -1;
	}
	FRIMDEBUG("Firmware OK MD5 String Same:%s<-->%s\r\n", md5str, md5cmp);
	snprintf(syscmd, sizeof(syscmd)-1, "%s %s &", SYS_FIRM_UPSCRIP, firmware);
	system(syscmd);
	
	return 0;
}

int usStorage_firmwareINFO(struct scsi_head *header)
{
	vs_acessory_parameter dinfo;
	int flen = sizeof(vs_acessory_parameter);
	struct allinfo sinfo;
	FILE *fp;
	char line[256] = {0}, key[128], value[128];
	uint8_t *buffer = NULL, rc = 0;
	uint32_t size = 0, total = 0;	

	if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
		FRIMDEBUG("usProtocol_GetAvaiableBuffer Failed\r\n");
		return 1;
	}	
	FRIMDEBUG("AvaiableBuffer 0x%p[%dBytes]\r\n", buffer, size);
	total = sizeof(struct scsi_head);
	memcpy(buffer, header, total);
	
	memset(&dinfo, 0, flen);
	/*Get Firmware Info*/
	fp = fopen(SYS_FIRMCONF, "r");
	if(fp == NULL){
		FRIMDEBUG("Open %s Failed:%s\r\n", 
						SYS_FIRMCONF, strerror(errno));
		header->relag = 1;
		memcpy(buffer, header, sizeof(struct scsi_head));
		goto sndRes;
	}
	while (fgets(line, sizeof(line), fp)) {
		memset(key, 0, sizeof(key));
		memset(value, 0, sizeof(value));		
		if (sscanf(line, "%[^=]=%[^\n ]",
					key, value) != 2)
			continue;
		if(!strcasecmp(key, "VENDOR")){
			strcpy(dinfo.manufacture, value);
		}else if(!strcasecmp(key, "CURFILE")){
			strcpy(dinfo.model_name, value);
		}else if(!strcasecmp(key, "CURVER")){
			strcpy(dinfo.fw_version, value);
			FRIMDEBUG("Version is %s\r\n", dinfo.fw_version);
		}
	}
	fclose(fp);
	/*Use Default cardid*/
	strcpy(dinfo.cardid, "1234567");
	/*Get SN/license*/
	memset(&sinfo, 0, sizeof(struct allinfo));
	if(showSnLicense(&sinfo) == 0){
		/*OK*/
		strncpy(dinfo.sn, sinfo.info.sn, sizeof(dinfo.sn)-1);
		strncpy(dinfo.license, sinfo.info.license, sizeof(dinfo.license)-1);		
	}else{
		/*Bad*/
		memset(dinfo.sn, 0, sizeof(dinfo.sn));		
		memset(dinfo.license, 0, sizeof(dinfo.license));
	}
	memcpy(buffer+total, &dinfo, flen);
	total += flen;

sndRes:
	if((rc = usProtocol_SendPackage(buffer, total))){
		FRIMDEBUG("usProtocol_SendPackage Failed\r\n");
		return rc;
	}
	FRIMDEBUG("usStorage_firmwareINFO Successful Firmware Info:\r\nVendor:%s\r\nProduct:%s\r\nVersion:%s\r\nSerical:%s\r\nLicense:%s\r\n", 
					dinfo.manufacture, dinfo.model_name, dinfo.fw_version, dinfo.sn, dinfo.license);
	
	return 0;
}

int usStorage_firmwareUP(uint8_t *buffer, uint32_t recvSize)
{
	static int fd = -1;
	struct scsi_head scsi;	
	uint8_t headbuf[PRO_HDR] = {0};	
	uint32_t hSize = recvSize;

	if(recvSize < PRO_HDR){
		FRIMDEBUG("Frimware Request Error\r\n");
		return -1;
	}
	/*We must save the header in var*/
	memcpy(&scsi, buffer, PRO_HDR);
	if(scsi.ctrid == SCSI_UPDATE_START){
		FRIMDEBUG("Prepare To update Firmware\r\n");
		system(SYS_UPGRADE_LED_BEGIN);
		close(fd);
		fd = open(USTORAGE_UPPATH, 
				O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR|S_IWUSR);
		if(fd < 0){
			FRIMDEBUG("Open %s Failed:%s\r\n", 
					USTORAGE_UPPATH, strerror(errno));
			scsi.relag = 1;
		}else{
			FRIMDEBUG("Open %s Successful:%d", 
				USTORAGE_UPPATH, fd);
		}
	}else if(scsi.ctrid == SCSI_UPDATE_DATA){
		uint8_t *payload = buffer+SCSI_HEAD_SIZE;
		uint32_t paySize, curSize = 0;
		
		if(fd < 0){
			FRIMDEBUG("%s Not Open\r\n", USTORAGE_UPPATH);
			scsi.relag = 1;
			goto sndRes;
		}
		paySize= recvSize-PRO_HDR;
		if(paySize){
			FRIMDEBUG("Write %uBytes to Firmware\r\n", paySize);
			if(writeFirmware(fd, payload, paySize) < 0){
				goto sndRes;
			}
			curSize += paySize;
		}
		while(curSize < scsi.len){
			uint8_t *pbuffer = NULL, res;
			if((res = usProtocol_RecvPackage((void **)&pbuffer, hSize, &paySize)) != 0){
				if(res == PROTOCOL_RTIMOUT){
					FRIMDEBUG("Firmware Receive Timeout\r\n");
					return -1;	
				}else{
					FRIMDEBUG("usProtocol_RecvPackage Failed IN Firmware\r\n");
					/*Write to Phone*/
					scsi.relag = 1;
					goto sndRes;
				}
			}
			hSize+= paySize;
			if(writeFirmware(fd, pbuffer, paySize) < 0){
				goto sndRes;
			}
			FRIMDEBUG("Write Firmware %s: %uBytes [PartT:%uBytes/%dBytes]\r\n", 
							USTORAGE_UPPATH, paySize, curSize, scsi.len);			
			curSize += paySize;
		}
		FRIMDEBUG("Write Firmware %s Successful:%d\r\n", 
						USTORAGE_UPPATH, curSize);
	}else if(scsi.ctrid == SCSI_UPDATE_END){
		close(fd);
		fd = -1;
		FRIMDEBUG("Check %s MD5\r\n", USTORAGE_UPPATH);
		if(upgradeFirmware(USTORAGE_UPPATH) < 0){
			FRIMDEBUG("upgradeFirmware %s Error\r\n", USTORAGE_UPPATH);			
			scsi.relag = 1;
		}
	}else{
		FRIMDEBUG("unknown Comannd ID:%d\r\n", scsi.ctrid);
		scsi.relag = 1;
	}

sndRes:

	memcpy(headbuf, &scsi, PRO_HDR);
	/*Send To Phone*/
	if(usProtocol_SendPackage(headbuf, PRO_HDR)){
		FRIMDEBUG("Send To Phone[Just Header Error]\r\n");
		return 1;
	}

	return 0;
}

int usStorage_firmwareLicense(uint8_t *buffer, uint32_t recvSize)
{
	struct scsi_head scsi;	
	uint8_t headbuf[PRO_HDR] = {0}; 
	uint32_t hSize = recvSize;
	uint8_t *payload = buffer+SCSI_HEAD_SIZE;
	uint32_t paySize, curSize = 0, totalSize = sizeof(struct allinfo);
	struct allinfo license;
	uint8_t *pbuffer = NULL, res, *plince = (uint8_t *)&license;	
	int fd;
	
	if(recvSize < PRO_HDR){
		FRIMDEBUG("Frimware License Request Error\r\n");
		return -1;
	}
	/*We must save the header in var*/
	memset(plince, 0, sizeof(struct allinfo));
	memcpy(&scsi, buffer, PRO_HDR);
		
	paySize= recvSize-PRO_HDR;
	if(paySize != totalSize){
		FRIMDEBUG("Receive InComplete Licence Information[%d/%d]\r\n", paySize, totalSize);
		if(paySize){
			memcpy(plince, payload, paySize);
			curSize = paySize;
		}
		while(curSize < scsi.len){		
			if((res = usProtocol_RecvPackage((void **)&pbuffer, hSize, &paySize)) != 0){
				FRIMDEBUG("usProtocol_RecvPackage Failed IN firmwareLicense [%d]\r\n", res);
				/*Write to Phone*/
				scsi.relag = 1;
				goto sndRes;
			}
			hSize+= paySize;
			memcpy(plince+curSize, pbuffer, paySize);
			curSize += paySize;
			FRIMDEBUG("Receive License Information[PartT:%uBytes/%dBytes/%dBytes]\r\n", 
							 paySize, curSize, scsi.len);			
		}
		
	}else{
		memcpy(plince, payload, totalSize);
	}
	FRIMDEBUG("License Information:\n\tSN:%s\n\tLicense:%s\n\tCRC:%u\n", 
					 license.info.sn, license.info.license, license.crc);			

	if(crc32(0,(void*)&(license.info),sizeof(license.info)) 
			 != license.crc){
		FRIMDEBUG("ERROR: SN/License:\r\nSN:%s\r\nLicense:%s\r\n", 
								license.info.sn, license.info.license); 
		/*Write to Phone*/
		scsi.relag = 1;
		goto sndRes;
	}
	/*Write to device*/
	fd = open(FILENAME,O_WRONLY);
	if(fd < 0){
		FRIMDEBUG("Open %s Failed:%s\n", FILENAME, strerror(errno));
		scsi.relag = 1;
		goto sndRes;
	}
	lseek(fd, LICENSE_OFFSET, SEEK_SET);
	if(write(fd, &license, sizeof(struct allinfo)) < 0){
		FRIMDEBUG("Write %s Failed:%s\r\n", 
						FILENAME, strerror(errno));
		close(fd);
		scsi.relag = 1;
		goto sndRes;
	}
	fsync(fd);
	close(fd);
	
	sync();
	FRIMDEBUG("Wirte License Successful\n");
	
sndRes:

	memcpy(headbuf, &scsi, PRO_HDR);
	/*Send To Phone*/
	if(usProtocol_SendPackage(headbuf, PRO_HDR)){
		FRIMDEBUG("Send To Phone[Just Header Error]\r\n");
		return 1;
	}

	return 0;
}

#endif


