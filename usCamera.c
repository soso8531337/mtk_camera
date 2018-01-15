#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>


#include "mtk_camera.h"
#include "mtk_http.h"

#define LOCKMODE (S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)    


static uint8_t *framePtr = NULL;
static uint32_t memSize = 0;

int lockfile(int fd)
{
	struct flock f1;

	f1.l_type = F_WRLCK;
	f1.l_start = 0;
	f1.l_whence = SEEK_SET;
	f1.l_len = 0;

	return (fcntl(fd, F_SETLK, &f1));

}

int already_running(char *pidfile)
{
	int fd;
	char buf[16];

	fd = open(pidfile, O_RDWR|O_CREAT, LOCKMODE);
	if(fd < 0){
		DBG("Can not open %s:%s\n", pidfile, strerror(errno));
		return -1;

	}

	if(lockfile(fd) < 0){
		if(errno == EACCES || errno == EAGAIN){
			close(fd);
			return 1;

		}
		DBG("Can not lock %s:%s\n", pidfile, strerror(errno));
		return -1;

	}
	ftruncate(fd, 0);
	sprintf(buf, "%ld", (long)getpid());
	write(fd, buf, strlen(buf)+1);
	return 0;
}

int usCamera_init(char *video)
{
	if(!video){
		return -1;
	}
	if(mtkVideoInit(video, NULL) < 0){
		mtkVideoUninit();
		return -1;
	}
#ifdef CAM_HTTP	
	if(mktHttpInit(0) < 0){
		mktHttpUnInit();
		mtkVideoUninit();
		return -1;
	}
#endif
	return 0;
}


int usCamera_Streaming(uint8_t **tfrPtr, uint32_t *tfrSize)
{
	struct timeval tm;
	char *tmpFrame = NULL;
	uint32_t frameSize = 0;
	char frameType[32] = {0};
	
	mtkGetVideoLock();
	mtkGetVideoFrame(&tmpFrame, &frameSize, frameType, &tm);
	if(frameSize > memSize){
		framePtr = realloc(framePtr, frameSize+512);
		if(framePtr == NULL){
			DBG("Http Calloc Memory Failed\n");
			mtkGetVideoUnLock();
			return -1;
		}
		memSize = frameSize+512;
	}
	if(frameSize){
		memcpy(framePtr, tmpFrame, frameSize);
	}
	mtkGetVideoUnLock();

	*tfrPtr = framePtr;
	*tfrSize = frameSize;
	return 0;
}

int usCamera_GetConfig(uint8_t *confPtr, uint32_t confSize, uint32_t *useSize)
{
	return mtkV4l2Config(confPtr, confSize, useSize);
}

int usCamera_GetCurConfig(uint8_t *confPtr, uint32_t confSize, uint32_t *useSize)
{
	return mtkV4l2CurConfig(confPtr, confSize, useSize);
}

