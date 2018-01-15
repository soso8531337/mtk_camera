#ifndef _MTK_CAMERA_H
#define _MTK_CAMERA_H

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <linux/videodev2.h>

typedef struct _currentSet{
	uint32_t format;
    uint32_t width;
    uint32_t height;
	uint32_t denominator;	
}currentSet;


#define DEBUG 	1
#ifdef DEBUG
#define DBG(...) fprintf(stderr, "DBG(%s, %s(), %d): ", __FILE__, __FUNCTION__, __LINE__); fprintf(stderr, __VA_ARGS__)
#else
#define DBG(...)
#endif

int mtkVideoInit(char *videoName, currentSet *videoSet);
int mtkVideoUninit(void);
void mtkGetVideoLock(void);
void mtkGetVideoUnLock(void);
void mtkBroadcastVideoLock(void);
int mtkGetVideoFrame(char **framePtr, uint32_t *frameSize, char *frameType, struct timeval *tm);
int mtkV4l2Config(uint8_t *confPtr, uint32_t confSize, uint32_t *useSize);
int mtkV4l2CurConfig(uint8_t *confPtr, uint32_t confSize, uint32_t *useSize);

#endif
