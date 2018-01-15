#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <asm/types.h>
#include <linux/videodev2.h>

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <jpeglib.h>
#include <pthread.h>

#include "mtk_camera.h"
#include "protocol.h"

#ifdef USE_LIBV4L2
#include <libv4l2.h>
#define IOCTL_VIDEO(fd, req, value) v4l2_ioctl(fd, req, value)
#define OPEN_VIDEO(fd, flags) v4l2_open(fd, flags)
#define CLOSE_VIDEO(fd) v4l2_close(fd)
#else
#define IOCTL_VIDEO(fd, req, value) ioctl(fd, req, value)
#define OPEN_VIDEO(fd, flags) open(fd, flags)
#define CLOSE_VIDEO(fd) close(fd)
#endif
#define IOCTL_RETRY 4
#define NB_BUFFER 4

//#define THREAD_DECODE 1
//#define MTK_DECYUV 1
enum{
	CAP_STREAMING=1<<0,
	CAP_READWRITE=1<<1,
};

enum _streaming_state {
	STREAMING_OFF = 0,
	STREAMING_ON = 1,
	STREAMING_PAUSED = 2,
	STREAMING_QUIT = 3,
};

typedef struct _input_interval{
	uint32_t numerator; /*n/d*/
	uint32_t denominator;
}__attribute__((__packed__))intervalList;

typedef struct _input_frame{
    uint32_t width;
    uint32_t height;
	uint8_t intervalCount;
	intervalList *interlist;
}__attribute__((__packed__))frameList;

typedef struct _input_format{
    struct v4l2_fmtdesc format;
    uint8_t frameCount;
	frameList *framelist;
}__attribute__((__packed__))formatList;

typedef struct {
	uint8_t* start;
	size_t length;
} buffer_t;

typedef struct _input_buffer{
	uint32_t bufcnt;
	uint32_t memSize;
	buffer_t *buffers;
}v4l2Buffer;

typedef struct _input_frameBuffer{
	uint8_t haveInit;
	uint8_t frameType[32];
	uint32_t frameSize;/*frame size*/
	buffer_t frameMem; /*memory to store frame*/
	/* v4l2_buffer timestamp */
	struct timeval timestamp;
    /* signal fresh frames */
    pthread_mutex_t m_mutex;
    pthread_cond_t  m_cond;	
}frameBuffer;

#ifdef THREAD_DECODE
typedef struct _dBuf{
	uint8_t state;
	uint8_t* start;
	size_t length;
	size_t uselen;
	uint32_t prioty;	
	struct timeval timestamp;
}dbuffer;

typedef struct _decodeConfg{
	uint32_t bufcnt;
	dbuffer *buffers;
	uint32_t prioty;
    pthread_mutex_t m_mutex;	
    pthread_cond_t  m_cond;	
	pthread_t threadDecode;	
}decodeConfg;

enum{
	DCDBUF_IDLE = 0,
	DCDBUF_DATA0 = 1,
	DCDBUF_DECODING = 2,
	DCDBUF_DECODED = 3,
};

#endif
struct vdIn{
	uint8_t streamingState;
	pthread_t threadVideo;
	int fd;
	char videodevice[64];
	uint8_t cap_method;
    uint8_t formatCount;
	formatList *fmtlist;
	currentSet videoSet;
	v4l2Buffer videoBuf;
	frameBuffer frameBuf;
#ifdef THREAD_DECODE
	decodeConfg dcodeConf;
#endif	
};

enum{
	APP_FORMAT_MJPEG = 1,
	APP_FORMAT_YUV = 2,
	APP_FORMAT_NV12 = 3,
	APP_FORMAT_M420 = 4,
	APP_FORMAT_I420 = 5,
};
#define VS_FORMAT_UNCOMPRESSED	0x04
#define VS_FRAME_UNCOMPRESSED	0x05
#define VS_FORMAT_MJPEG			0x06
#define VS_FRAME_MJPEG 			0x07
#define CS_INTERFACE			0x24 


struct vdIn camContext;

/* ioctl with a number of retries in the case of failure
* args:
* fd - device descriptor
* IOCTL_X - ioctl reference
* arg - pointer to ioctl data
* returns - ioctl result
*/
static int xioctl(int fd, int IOCTL_X, void *arg)
{
	int ret = 0;
	int tries = IOCTL_RETRY;
	do {
	    ret = IOCTL_VIDEO(fd, IOCTL_X, arg);
	} while(ret && tries-- &&
	        ((errno == EINTR) || (errno == EAGAIN) || (errno == ETIMEDOUT)));

	if(ret && (tries <= 0)){
		DBG("ioctl (%i) retried %i times - giving up: %s)\n", IOCTL_X, IOCTL_RETRY, strerror(errno));
	}

	return (ret);
}


/**********************************Picture Function******************************************/
const static unsigned char dht_data[] = {
    0xff, 0xc4, 0x01, 0xa2, 0x00, 0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02,
    0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x01, 0x00, 0x03,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
    0x0a, 0x0b, 0x10, 0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03, 0x05,
    0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7d, 0x01, 0x02, 0x03, 0x00, 0x04,
    0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22,
    0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15,
    0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a,
    0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66,
    0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
    0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95,
    0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8,
    0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2,
    0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5,
    0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
    0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9,
    0xfa, 0x11, 0x00, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04, 0x07, 0x05,
    0x04, 0x04, 0x00, 0x01, 0x02, 0x77, 0x00, 0x01, 0x02, 0x03, 0x11, 0x04,
    0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22,
    0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33,
    0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25,
    0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a,
    0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66,
    0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
    0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94,
    0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
    0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
    0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
    0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa
};

#ifdef MTK_DECYUV
uint32_t jpeg(uint8_t *ptrjpeg, uint32_t jpgsize, uint8_t* rgb, uint32_t width, uint32_t height, int quality)
{
	JSAMPARRAY image;
	char *jpg_file = "/tmp/jpg";
	int jpeg_size = 0;
	size_t i, j;
	image = calloc(height, sizeof (JSAMPROW));
	for (i = 0; i < height; i++) {
		image[i] = calloc(width * 3, sizeof (JSAMPLE));
		for (j = 0; j < width; j++) {
			image[i][j * 3 + 0] = rgb[(i * width + j) * 3 + 0];
			image[i][j * 3 + 1] = rgb[(i * width + j) * 3 + 1];
			image[i][j * 3 + 2] = rgb[(i * width + j) * 3 + 2];
		}
	}

	FILE *dest = fopen(jpg_file, "w");
	struct jpeg_compress_struct compress;
	struct jpeg_error_mgr error;
	compress.err = jpeg_std_error(&error);
	jpeg_create_compress(&compress);
	jpeg_stdio_dest(&compress, dest);

	compress.image_width = width;
	compress.image_height = height;
	compress.input_components = 3;
	compress.in_color_space = JCS_RGB;
	jpeg_set_defaults(&compress);
	jpeg_set_quality(&compress, quality, TRUE);
	jpeg_start_compress(&compress, TRUE);
	jpeg_write_scanlines(&compress, image, height);
	jpeg_finish_compress(&compress);
	jpeg_destroy_compress(&compress);

	for (i = 0; i < height; i++) {
		free(image[i]);
	}
	free(image);
	fclose(dest);
	dest = fopen(jpg_file, "rb");
	fseek(dest, 0, SEEK_END);
	jpeg_size = ftell(dest);
	fseek(dest, 0, SEEK_SET);
	fread(ptrjpeg, jpeg_size, 1, dest);
	fclose(dest);

	return jpeg_size;
}

static int minmax(int min, int v, int max)
{
	return (v < min) ? min : (max < v) ? max : v;
}

static uint8_t* yuyv2rgb(uint8_t* yuyv, uint32_t width, uint32_t height)
{
	uint8_t* rgb = calloc(width * height * 3, sizeof (uint8_t));
	size_t i, j;
	for (i = 0; i < height; i++) {
		for (j = 0; j < width; j += 2) {
			size_t index = i * width + j;
			int y0 = yuyv[index * 2 + 0] << 8;
			int u = yuyv[index * 2 + 1] - 128;
			int y1 = yuyv[index * 2 + 2] << 8;
			int v = yuyv[index * 2 + 3] - 128;
			rgb[index * 3 + 0] = minmax(0, (y0 + 359 * v) >> 8, 255);
			rgb[index * 3 + 1] = minmax(0, (y0 + 88 * v - 183 * u) >> 8, 255);
			rgb[index * 3 + 2] = minmax(0, (y0 + 454 * u) >> 8, 255);
			rgb[index * 3 + 3] = minmax(0, (y1 + 359 * v) >> 8, 255);
			rgb[index * 3 + 4] = minmax(0, (y1 + 88 * v - 183 * u) >> 8, 255);
			rgb[index * 3 + 5] = minmax(0, (y1 + 454 * u) >> 8, 255);
		}
	}
	return rgb;
}

static int convertFrameToJPEG(uint8_t* yuyv, uint32_t yuvsize, uint8_t *mjpeg, uint32_t jpgsize, 
					uint32_t width, uint32_t height, int quality)
{
	uint8_t *rgb = yuyv2rgb(yuyv, width, height);
	int jsize = 0;

	jsize = jpeg(mjpeg, jpgsize, rgb, width, height, quality);

	free(rgb);
	return jsize;
}

#endif
static int is_huffman(unsigned char *buf)
{
    unsigned char *ptbuf;
    int i = 0;
    ptbuf = buf;
    while(((ptbuf[0] << 8) | ptbuf[1]) != 0xffda) {
        if(i++ > 2048)
            return 0;
        if(((ptbuf[0] << 8) | ptbuf[1]) == 0xffc4)
            return 1;
        ptbuf++;
    }
    return 0;
}

static int memcpy_picture(unsigned char *out, unsigned char *buf, int size)
{
    unsigned char *ptdeb, *ptlimit, *ptcur = buf;
    int sizein, pos = 0;

    if(!is_huffman(buf)) {
        ptdeb = ptcur = buf;
        ptlimit = buf + size;
        while((((ptcur[0] << 8) | ptcur[1]) != 0xffc0) && (ptcur < ptlimit))
            ptcur++;
        if(ptcur >= ptlimit)
            return pos;
        sizein = ptcur - ptdeb;

        memcpy(out + pos, buf, sizein); pos += sizein;
        memcpy(out + pos, dht_data, sizeof(dht_data)); pos += sizeof(dht_data);
        memcpy(out + pos, ptcur, size - sizein); pos += size - sizein;
    } else {
        memcpy(out + pos, ptcur, size); pos += size;
    }
    return pos;
}


/*Camera function*/
static void uninitV4l2Memory(struct vdIn *vd)
{
	int i;
	
	if(!vd){
		return;
	}
	for(i = 0; i < vd->videoBuf.bufcnt; i++){
		if(vd->videoBuf.buffers[i].start){
			DBG("munmap->%d: Addr:%p Len:%d\n", 
					i, vd->videoBuf.buffers[i].start, vd->videoBuf.buffers[i].length);
			munmap(vd->videoBuf.buffers[i].start, vd->videoBuf.buffers[i].length);
		}
	}
	if(vd->videoBuf.buffers){
		free(vd->videoBuf.buffers);
		vd->videoBuf.buffers = NULL;
		vd->videoBuf.bufcnt = 0;
	}
}

static int initV4l2Memory(struct vdIn *vd)
{
	int i;
	struct v4l2_requestbuffers req;
	struct v4l2_buffer buf;
	
	if(!vd){
		return -1;
	}
	
	/*request buffers*/
	memset(&req, 0, sizeof(struct v4l2_requestbuffers));
	req.count = NB_BUFFER;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	
	if (xioctl(vd->fd, VIDIOC_REQBUFS, &req) == -1){
		DBG("Requst Buffer Failed\n");
		return -1;
	}
	vd->videoBuf.memSize = 0;
	vd->videoBuf.bufcnt = req.count;	
	vd->videoBuf.buffers = calloc(req.count, sizeof (buffer_t));
	if(!vd->videoBuf.buffers){
		DBG("Calloc Buffer Memory Faild:%s\n", strerror(errno));
		return -1;
	}

	/*map buffers*/
	for (i = 0; i < vd->videoBuf.bufcnt; i++){
		memset(&buf, 0, sizeof(struct v4l2_buffer));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		if (xioctl(vd->fd, VIDIOC_QUERYBUF, &buf) == -1){
			DBG("VIDIOC_QUERYBUF Failed\n");
			uninitV4l2Memory(vd);
			return -1;
		}

		vd->videoBuf.buffers[i].length = buf.length;
		vd->videoBuf.buffers[i].start = 
			mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, 
					vd->fd, buf.m.offset);
		if (vd->videoBuf.buffers[i].start == MAP_FAILED){
			DBG("Map Memory Failed\n");
			uninitV4l2Memory(vd);
			return -1;
		}
		if(!vd->videoBuf.memSize){
			vd->videoBuf.memSize = buf.length;
		}
		DBG("MAP[%d] Address:%p Len:%u offset:%u\n", i, 
				vd->videoBuf.buffers[i].start, vd->videoBuf.buffers[i].length, buf.m.offset);
	}
	
	/*Queue the buffers*/
	for(i = 0; i < vd->videoBuf.bufcnt; ++i) {
		memset(&buf, 0, sizeof(struct v4l2_buffer));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		if(xioctl(vd->fd, VIDIOC_QBUF, &buf) == -1){
			DBG("VIDIOC_QBUF Failed\n");
			uninitV4l2Memory(vd);
			return -1;
		}
	}
	DBG("Init Video Buffer Successful..\n");

	return 0;
}

static void showV4l2Config(struct vdIn *vd)
{
    int i, j, k;
		
	if(!vd || !vd->fmtlist){
		return;
	}

	printf("Current Setting:%c%c%c%c-%ux%u-%u\n", vd->videoSet.format&0xFF, 
             	(vd->videoSet.format>>8)&0xFF, (vd->videoSet.format>>16)&0xFF,
             		(vd->videoSet.format>>24)&0xFF, vd->videoSet.width, vd->videoSet.height,
             			vd->videoSet.denominator);
	for(i=0; i < vd->formatCount; i++){		
		printf("Format[%d]:%s Frame:%d\n", i, vd->fmtlist[i].format.description, 
				vd->fmtlist[i].frameCount);
		for(j=0; j < vd->fmtlist[i].frameCount; j++){
			printf("\tFrame[%d]:%dx%d=>", j, vd->fmtlist[i].framelist[j].width, vd->fmtlist[i].framelist[j].height);
			for(k = 0; k < vd->fmtlist[i].framelist[j].intervalCount; k++){
				printf("%u/%u ", vd->fmtlist[i].framelist[j].interlist[k].numerator, vd->fmtlist[i].framelist[j].interlist[k].denominator);
			}
			printf("\n");
		}	
	}	
}

static void uninitV4l2Config(struct vdIn *vd)
{
    int i, j;
	
	if(!vd || !vd->fmtlist){
		return;
	}
	/*close video fd*/
	CLOSE_VIDEO(vd->fd);
	for(i=0; i < vd->formatCount; i++){
		for(j=0; j < vd->fmtlist[i].frameCount; j++){
			if(vd->fmtlist[i].framelist[j].intervalCount){
				DBG("Free IntervalList Memory..\n");
				free(vd->fmtlist[i].framelist[j].interlist);
			}
		}
		/*free frame memory*/
		DBG("Free FrameList Memory..\n");
		free(vd->fmtlist[i].framelist);
	}
	/*free format memory*/
	DBG("Free FormatList Memory..\n");
	if(vd->fmtlist){
		free(vd->fmtlist);		
		vd->fmtlist = NULL;
	}

	vd->formatCount = 0;
	DBG("Uninit V4l2Config Successful..\n");
}

static int initV4l2Config(struct vdIn *vd, currentSet *camSetPara)
{
    int ret = 0;	
	struct v4l2_capability cap;
	struct v4l2_fmtdesc fmtdesc;
	struct v4l2_frmsizeenum fsenum;
	struct v4l2_frmivalenum fival;
	struct v4l2_format currentFormat;
	struct v4l2_streamparm fps;
	
	if((vd->fd = OPEN_VIDEO(vd->videodevice, O_RDWR)) == -1) {
		DBG("Open %s Failed: %s\n", vd->videodevice, strerror(errno));
		return -1;
	}

    memset(&cap, 0, sizeof(struct v4l2_capability));
    ret = xioctl(vd->fd, VIDIOC_QUERYCAP, &cap);
    if(ret < 0) {
        DBG("Error opening device %s: unable to query device.\n", vd->videodevice);
        goto fatal;
    }

    if((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0) {
        DBG("Error opening device %s: video capture not supported.\n",
                vd->videodevice);
        goto fatal;
    }

    if((cap.capabilities & V4L2_CAP_STREAMING)) {
		DBG("Camera Support STREAMING Method..\n");
		vd->cap_method |= CAP_STREAMING;
    }else{
		DBG("Camera Not Support STREAMING\n");
		goto fatal;
	}

	if(camSetPara){
		/*Set camera Config*/
		memset(&currentFormat, 0, sizeof(struct v4l2_format));
		currentFormat.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		currentFormat.fmt.pix.width = camSetPara->width;
		currentFormat.fmt.pix.height = camSetPara->height;
		currentFormat.fmt.pix.pixelformat = camSetPara->format;
		currentFormat.fmt.pix.field = V4L2_FIELD_ANY;
		if (xioctl(vd->fd, VIDIOC_S_FMT, &currentFormat) != 0){
			DBG("Setting Camera Failed\n");
			goto fatal;
		}
		/*Set intervals*/
		if(camSetPara->denominator){
			memset(&fps, 0, sizeof(struct v4l2_streamparm));		
			fps.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			fps.parm.capture.timeperframe.denominator = camSetPara->denominator;
			fps.parm.capture.timeperframe.numerator = 1;
			xioctl(vd->fd, VIDIOC_S_PARM, &fps);
		}
	}else{
		memset(&currentFormat, 0, sizeof(struct v4l2_format));
		currentFormat.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		xioctl(vd->fd, VIDIOC_G_FMT, &currentFormat);
		currentFormat.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		xioctl(vd->fd, VIDIOC_S_FMT, &currentFormat);
	}
	/*get current intervals*/
	memset(&fps, 0, sizeof(struct v4l2_streamparm));
	fps.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if(xioctl(vd->fd, VIDIOC_G_PARM, &fps) == 0){
		if(fps.parm.capture.capability & V4L2_CAP_TIMEPERFRAME) {
			vd->videoSet.denominator = fps.parm.capture.timeperframe.denominator;
		}else{
			vd->videoSet.denominator = 0;
		}
	}
	/*init current setting*/
    memset(&currentFormat, 0, sizeof(struct v4l2_format));
    currentFormat.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(vd->fd, VIDIOC_G_FMT, &currentFormat) == 0) {
        DBG("Current Setting: %dx%d FPS:%d F:%c%c%c%c\n",
             currentFormat.fmt.pix.width,
             currentFormat.fmt.pix.height, vd->videoSet.denominator, currentFormat.fmt.pix.pixelformat&0xFF, 
             	(currentFormat.fmt.pix.pixelformat>>8)&0xFF, (currentFormat.fmt.pix.pixelformat>>16)&0xFF,
             		(currentFormat.fmt.pix.pixelformat>>24)&0xFF);
		vd->videoSet.format = currentFormat.fmt.pix.pixelformat;
		vd->videoSet.width= currentFormat.fmt.pix.width;
		vd->videoSet.height= currentFormat.fmt.pix.height;
    }
	
	/*init format list*/
	for(vd->formatCount = 0; 1; vd->formatCount++) {
		/*Get Format Descriptor*/
		memset(&fmtdesc, 0, sizeof(struct v4l2_fmtdesc));
	    fmtdesc.index = vd->formatCount;
	    fmtdesc.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if(xioctl(vd->fd, VIDIOC_ENUM_FMT, &fmtdesc) < 0) {
			break;
		}
	    if (vd->fmtlist== NULL) {
	        vd->fmtlist = (formatList*)calloc(1, sizeof(formatList));
	    } else {
	        vd->fmtlist = (formatList*)realloc(vd->fmtlist, (vd->formatCount + 1) * sizeof(formatList));
	    }
		if(vd->fmtlist == NULL){
			DBG("Calloc/realloc FormatList Failed:%s\n", strerror(errno));
			goto fatal;
		}
		memcpy(&vd->fmtlist[vd->formatCount], &fmtdesc, sizeof(struct v4l2_fmtdesc));
	    DBG("Supported format: %s\n", fmtdesc.description);
		memset(&fsenum, 0, sizeof(struct v4l2_frmsizeenum));
		fsenum.pixel_format = fmtdesc.pixelformat;
        int j = 0;		
		vd->fmtlist[vd->formatCount].frameCount = 0;
		vd->fmtlist[vd->formatCount].framelist = NULL;
		while(1){
			/*Get Frame Descriptor*/
			fsenum.index = j;
			j++;
			if(xioctl(vd->fd, VIDIOC_ENUM_FRAMESIZES, &fsenum) == 0){
				vd->fmtlist[vd->formatCount].frameCount++;
				if (vd->fmtlist[vd->formatCount].framelist== NULL) {
					vd->fmtlist[vd->formatCount].framelist = (frameList*)
					calloc(1, sizeof(frameList));
				} else {
					vd->fmtlist[vd->formatCount].framelist = (frameList*)
					realloc(vd->fmtlist[vd->formatCount].framelist, j * sizeof(frameList));
				}

				if (vd->fmtlist[vd->formatCount].framelist == NULL) {
					DBG("Calloc/realloc FrameList Failed:%s\n", strerror(errno));
					goto fatal;
				}

				vd->fmtlist[vd->formatCount].framelist[j-1].width = fsenum.discrete.width;
				vd->fmtlist[vd->formatCount].framelist[j-1].height = fsenum.discrete.height;
				DBG("\tSupported size: %dx%d\n", fsenum.discrete.width, fsenum.discrete.height);
				/*Get Frame Intervals*/
				vd->fmtlist[vd->formatCount].framelist[j-1].intervalCount = 0;
				vd->fmtlist[vd->formatCount].framelist[j-1].interlist = NULL;
				memset(&fival, 0, sizeof(fival));				
				fival.index = 0;
				fival.pixel_format = fsenum.pixel_format;
				fival.width = fsenum.discrete.width;
				fival.height = fsenum.discrete.height;				
				int k = 0;
				while(1){					
					fival.index = k;
					k++;
					if(xioctl(vd->fd, VIDIOC_ENUM_FRAMEINTERVALS, &fival) == 0){					
						if (fival.type != V4L2_FRMIVAL_TYPE_DISCRETE){							
							break;
						}
						vd->fmtlist[vd->formatCount].framelist[j-1].intervalCount++;
						if (vd->fmtlist[vd->formatCount].framelist[j-1].interlist == NULL) {
							vd->fmtlist[vd->formatCount].framelist[j-1].interlist = (intervalList*)
							calloc(1, sizeof(intervalList));
						} else {
							vd->fmtlist[vd->formatCount].framelist[j-1].interlist = (intervalList*)
							realloc(vd->fmtlist[vd->formatCount].framelist[j-1].interlist, k * sizeof(intervalList));
						}

						if (vd->fmtlist[vd->formatCount].framelist[j-1].interlist == NULL) {
							DBG("Calloc/realloc IntervalList Failed:%s\n", strerror(errno));
							goto fatal;
						}
						
						vd->fmtlist[vd->formatCount].framelist[j-1].interlist[k-1].numerator = fival.discrete.numerator;
						vd->fmtlist[vd->formatCount].framelist[j-1].interlist[k-1].denominator = fival.discrete.denominator;
						DBG("\tSupported interval:%u/%u\n",
								fival.discrete.numerator, fival.discrete.denominator);				
					}else{
						break;
					}
				}		
			} else {
				break;
			}
		}
		
	}
	DBG("Init V4l2Config Successful..\n");
	return 0;
fatal:
	uninitV4l2Config(vd);	
	return -1;
}

static int startVideoCatpure(struct vdIn *vd)
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if(!vd){
		return -1;
	}	
	if(xioctl(vd->fd, VIDIOC_STREAMON, &type) == -1){
		DBG("VIDIOC_STREAMON Failed\n");
		return -1;
	}

	DBG("Start Camera Successful..\n");

	return 0;
}

static int stopVideoCatpure(struct vdIn *vd)
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	
	if(!vd){
		return -1;
	}
	if(xioctl(vd->fd, VIDIOC_STREAMOFF, &type) == -1){
		DBG("VIDIOC_STREAMOFF Failed\n");
		return -1;
	}

	DBG("Stop Camera Successful..\n");

	return 0;
}

static int initframeMemory(frameBuffer *framePtr, uint32_t memSize)
{
	if(!framePtr){
		return -1;
	}
	memset(framePtr, 0, sizeof(frameBuffer));	
	framePtr->frameMem.start = calloc(1, memSize);
	if(!framePtr->frameMem.start){
		DBG("Calloc Frame Memory Failed\n");
		return -1;
	}
	framePtr->frameMem.length = memSize;
	pthread_cond_init(&framePtr->m_cond, NULL);
	pthread_mutex_init(&framePtr->m_mutex, NULL);
	framePtr->haveInit = 1;
	return 0;
}

static void uninitframeMemory(frameBuffer *framePtr)
{
	if(!framePtr){
		return ;
	}

	framePtr->haveInit = 0;
	pthread_cond_destroy(&framePtr->m_cond);
	pthread_mutex_destroy(&framePtr->m_mutex);
	if(framePtr->frameMem.length){
		free(framePtr->frameMem.start);
		framePtr->frameMem.start = NULL;
		framePtr->frameMem.length = 0;
	}
}

#ifdef THREAD_DECODE
void *loopDecodeFrames(void *arg)
{
	struct vdIn *vd = (struct vdIn *)arg;
	int i, minPrioty = 0, curIter;
	uint8_t *frameBuf = NULL;
	size_t framLen;
	while(1){
		pthread_mutex_lock(&vd->dcodeConf.m_mutex);
		pthread_cond_wait(&vd->dcodeConf.m_cond, &vd->dcodeConf.m_mutex);
		minPrioty = 0;
		curIter = 0;
		for(i = 0; i < vd->dcodeConf.bufcnt; i++){
			if(vd->dcodeConf.buffers[i].state == DCDBUF_DATA0 && 
					(!minPrioty || minPrioty > vd->dcodeConf.buffers[i].prioty)){
				minPrioty = vd->dcodeConf.buffers[i].prioty;
				curIter = i;
			}
		}
		if(curIter == vd->dcodeConf.bufcnt){
			pthread_mutex_unlock(&vd->dcodeConf.m_mutex);
			continue;
		}
		frameBuf = vd->dcodeConf.buffers[curIter].start;
		framLen = vd->dcodeConf.buffers[curIter].uselen; 
		vd->dcodeConf.buffers[i].state = DCDBUF_DECODING;
		/*unlock*/
		pthread_mutex_unlock(&vd->dcodeConf.m_mutex);
		
		/*Decode YUV*/
		/*calloc memeory*/
		pthread_mutex_lock(&vd->frameBuf.m_mutex);		
		if ((vd->videoSet.format == V4L2_PIX_FMT_YUYV) ||
			(vd->videoSet.format == V4L2_PIX_FMT_UYVY) ||
			(vd->videoSet.format == V4L2_PIX_FMT_RGB565) ) {	
				/*Need To Convert to jpeg*/
			vd->frameBuf.frameSize = convertFrameToJPEG(frameBuf, framLen,
					vd->frameBuf.frameMem.start, vd->frameBuf.frameMem.length, vd->videoSet.width, vd->videoSet.height, 80);	
		}else{
			vd->frameBuf.frameSize = memcpy_picture(vd->frameBuf.frameMem.start, frameBuf, framLen);
		}		
		vd->frameBuf.timestamp = vd->dcodeConf.buffers[curIter].timestamp;
		DBG("[%ld] [%d]FrameSize=%d\n", time(NULL), curIter, vd->frameBuf.frameSize);
		vd->dcodeConf.buffers[curIter].state = DCDBUF_IDLE;
		pthread_cond_broadcast(&vd->frameBuf.m_cond);
		pthread_mutex_unlock(&vd->frameBuf.m_mutex);
	}

	return NULL;
}

static int initDecodeConf(struct vdIn *vd)
{
	int i;
	memset(&(vd->dcodeConf), 0, sizeof(decodeConfg));
	pthread_mutex_init(&vd->dcodeConf.m_mutex, NULL);
	pthread_cond_init(&vd->dcodeConf.m_cond, NULL);
	/*init memeory*/
	vd->dcodeConf.bufcnt = vd->videoBuf.bufcnt;	
	vd->dcodeConf.buffers = calloc(vd->dcodeConf.bufcnt, sizeof (dbuffer));
	for(i = 0; i< vd->dcodeConf.bufcnt; i++){
		vd->dcodeConf.buffers[i].start = calloc(1, vd->videoBuf.memSize);
		vd->dcodeConf.buffers[i].length = vd->videoBuf.memSize;
		vd->dcodeConf.buffers[i].state = DCDBUF_IDLE;
		vd->dcodeConf.buffers[i].prioty = 0;
	}
	vd->dcodeConf.prioty = 0;
    pthread_create(&(vd->dcodeConf.threadDecode), NULL, loopDecodeFrames, (void*)vd);

	return 0;
}

static int getVideoFrame(struct vdIn *vd)
{
	struct v4l2_buffer buf;
	memset(&buf, 0, sizeof buf);
	int i, minPrioty = 0, minIter = 0;
	
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	if (xioctl(vd->fd, VIDIOC_DQBUF, &buf) == -1){
		DBG("VIDIOC_DQBUF Failed\n");
		return -1;
	}	
	/* copy JPG picture to global buffer */
	pthread_mutex_lock(&vd->dcodeConf.m_mutex);	
	for(i = 0; i < vd->dcodeConf.bufcnt; i++){
		if(vd->dcodeConf.buffers[i].state== DCDBUF_IDLE){
			memcpy(vd->dcodeConf.buffers[i].start, vd->videoBuf.buffers[buf.index].start, buf.bytesused);
			vd->dcodeConf.buffers[i].uselen= buf.bytesused;
			vd->dcodeConf.buffers[i].prioty = vd->dcodeConf.prioty++;
			break;
		}else if(vd->dcodeConf.buffers[i].state== DCDBUF_DATA0){
			if(!minPrioty || minPrioty >  vd->dcodeConf.buffers[i].prioty){
				minPrioty = vd->dcodeConf.buffers[i].prioty;
				minIter = i;
			}
		}
	}

	if(i == vd->dcodeConf.bufcnt){
		DBG("No Idle Buffer Used:%d..\n", minIter);
		memcpy(vd->dcodeConf.buffers[minIter].start, vd->videoBuf.buffers[buf.index].start, buf.bytesused);
		vd->dcodeConf.buffers[minIter].uselen= buf.bytesused;
		vd->dcodeConf.buffers[minIter].prioty = vd->dcodeConf.prioty++;
		vd->dcodeConf.buffers[minIter].state = DCDBUF_DATA0;
	}else{
		vd->dcodeConf.buffers[i].state = DCDBUF_DATA0;
	}
	
	//DBG("[%ld] Kernel FrameSize:%d\n", time(NULL), buf.bytesused);
	pthread_cond_broadcast(&vd->dcodeConf.m_cond);
	pthread_mutex_unlock(&vd->dcodeConf.m_mutex);	
	if (xioctl(vd->fd, VIDIOC_QBUF, &buf) == -1){
		DBG("VIDIOC_QBUF Failed\n");
		return -1;
	}
	return 0;
}
#else
static int getVideoFrame(struct vdIn *vd)
{
	struct v4l2_buffer buf;
	memset(&buf, 0, sizeof buf);
	
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	if (xioctl(vd->fd, VIDIOC_DQBUF, &buf) == -1){
		DBG("VIDIOC_DQBUF Failed\n");
		//return -1;
		_exit(1);		
	}	
	/* copy JPG picture to global buffer */
	pthread_mutex_lock(&vd->frameBuf.m_mutex);	
	/*calloc memeory*/
	if(vd->frameBuf.frameMem.length < buf.bytesused){
		if(vd->frameBuf.frameMem.length){
			free(vd->frameBuf.frameMem.start);
		}
		vd->frameBuf.frameMem.start = calloc(1, buf.bytesused);
		vd->frameBuf.frameMem.length = buf.bytesused;
	}
	if ((vd->videoSet.format == V4L2_PIX_FMT_YUYV) ||
		(vd->videoSet.format == V4L2_PIX_FMT_UYVY) ||
		(vd->videoSet.format == V4L2_PIX_FMT_RGB565) ) {	
	#ifdef MTK_DECYUV
			/*Need To Convert to jpeg*/
		vd->frameBuf.frameSize = convertFrameToJPEG( vd->videoBuf.buffers[buf.index].start, buf.bytesused,
				vd->frameBuf.frameMem.start, vd->frameBuf.frameMem.length, vd->videoSet.width, vd->videoSet.height, 80);	
	#else
		memcpy(vd->frameBuf.frameMem.start, vd->videoBuf.buffers[buf.index].start, buf.bytesused);
		vd->frameBuf.frameSize = buf.bytesused;
	#endif
	}else{
		vd->frameBuf.frameSize = memcpy_picture(vd->frameBuf.frameMem.start,  vd->videoBuf.buffers[buf.index].start, buf.bytesused);
	}
	vd->frameBuf.timestamp = buf.timestamp;
	pthread_cond_broadcast(&vd->frameBuf.m_cond);
	pthread_mutex_unlock(&vd->frameBuf.m_mutex);	
	if (xioctl(vd->fd, VIDIOC_QBUF, &buf) == -1){
		DBG("VIDIOC_QBUF Failed\n");
		//return -1;
		_exit(1);
	}
		
	return 0;
}
#endif

void *loopCatpureFrames(void *arg)
{
	struct vdIn *vd = (struct vdIn *)arg;
	fd_set fds;
	struct timeval tv;
	int r;
	
	if(!vd){
		return NULL;
	}

	while(vd->streamingState != STREAMING_QUIT){
		if(vd->streamingState != STREAMING_ON){
			usleep(100);
			continue;
		}
		FD_ZERO(&fds);
		FD_SET(vd->fd, &fds);
		/* Timeout. */
		tv.tv_sec = 2;
		tv.tv_usec = 0;

		r = select(vd->fd + 1, &fds, NULL, NULL, &tv);
		if(r == -1){
			if(errno == EINTR){
				continue;
			}
			DBG("Select Error:%s\n", strerror(errno));
			return NULL;
		}else if(r == 0){
			DBG("Timeout..\n");
			continue;
		}
		if(getVideoFrame(vd) != 0){
			DBG("Get Video Frame Error\n");
			return NULL;
		}
	}

	DBG("loopCatpureFrames Quit..\n");	
	return NULL;
}

int mtkVideoInit(char *videoName, currentSet *videoSet)
{
	if(!videoName){
		return -1;
	}
	memset(&camContext, 0, sizeof(struct vdIn));
	strcpy(camContext.videodevice, videoName);
	
	if(initV4l2Config(&camContext, videoSet) != 0){
		DBG("initV4l2Config Failed\n");
		return -1;
	}
	
	if(initV4l2Memory(&camContext) != 0){
		DBG("initV4l2Memory Failed\n");
		return -1;
	}

	if(initframeMemory(&camContext.frameBuf, camContext.videoBuf.memSize) != 0){
		DBG("initframeMemory Failed\n");
		return -1;
	}
#ifdef THREAD_DECODE
	initDecodeConf(&camContext);
#endif	
    pthread_create(&(camContext.threadVideo), NULL, loopCatpureFrames, (void*)&camContext);

    //pthread_detach(camContext.threadVideo);
	/*Start Cature*/
	startVideoCatpure(&camContext);
	/*Set Stream ON*/
	camContext.streamingState = STREAMING_ON;
	return 0;
}

int mtkVideoUninit(void)
{
	camContext.streamingState = STREAMING_QUIT;
	if(camContext.threadVideo){
		pthread_join(camContext.threadVideo, NULL);
	}
	stopVideoCatpure(&camContext);
	uninitframeMemory(&camContext.frameBuf);
	uninitV4l2Memory(&camContext);
	uninitV4l2Config(&camContext);
	
	return 0;
}
/*blocking*/
void mtkGetVideoLock(void)
{
	pthread_mutex_lock(&camContext.frameBuf.m_mutex);
	pthread_cond_wait(&camContext.frameBuf.m_cond, &camContext.frameBuf.m_mutex);
}

void mtkGetVideoUnLock(void)
{
	pthread_mutex_unlock(&camContext.frameBuf.m_mutex);
}

void mtkBroadcastVideoLock(void)
{
	if(!camContext.frameBuf.haveInit){
		return;
	}
	pthread_mutex_lock(&camContext.frameBuf.m_mutex);
	pthread_cond_broadcast(&camContext.frameBuf.m_cond);
	pthread_mutex_unlock(&camContext.frameBuf.m_mutex);
}
int mtkGetVideoFrame(char **framePtr, uint32_t *frameSize, char *frameType, struct timeval *tm)
{
	if(!framePtr || !frameSize 
			|| !tm || !frameType){
		return -1;
	}

	*framePtr = (char *)camContext.frameBuf.frameMem.start;
	*frameSize = camContext.frameBuf.frameSize;
	memcpy(tm, &camContext.frameBuf.timestamp, sizeof(struct timeval));
	if(camContext.videoSet.format == V4L2_PIX_FMT_YUYV){
		strcpy(frameType, "yuyv");
	}else if(camContext.videoSet.format == V4L2_PIX_FMT_UYVY){
		strcpy(frameType, "uyvy");
	}else if(camContext.videoSet.format == V4L2_PIX_FMT_RGB565){
		strcpy(frameType, "rgb565");
	}else{
		strcpy(frameType, "jpeg");
	}

#ifdef MTK_DECYUV
	strcpy(frameType, "jpeg");
#endif
	return 0;
}

static void usCamera_show(uint8_t *vsFormatPtr)
{
	uint8_t index =0, index2, index3;
	cameraFrameDes *camFrame;
	cameraDescriptor *camDes;
	uint16_t vsFormatLen = 0, vsFrameLen = 0;
	uint8_t *vsFramePtr = NULL;

	for(index = 0; index < camContext.formatCount; index++){
		printf("Format Number:%d  vsFormatlen:%d\n", index, vsFormatLen);
		camDes = (cameraDescriptor *)(vsFormatPtr+vsFormatLen);
		printf("FormatType:%d==>", camDes->bFormatType);
		if(camDes->bSubFormatType == APP_FORMAT_MJPEG){
			printf("[%d]: MJPEG-%d-%d\n", index+1, 
				camDes->bFormatIndex, camDes->bNumFrameDescriptors);
		}else if(camDes->bSubFormatType == APP_FORMAT_YUV){
			printf("[%d]: YUV-%d-%d\n", index+1, 
				camDes->bFormatIndex, camDes->bNumFrameDescriptors);
		}else if(camDes->bSubFormatType == APP_FORMAT_NV12){
			printf("[%d]: YUV-%d-%d\n", index+1, 
				camDes->bFormatIndex, camDes->bNumFrameDescriptors);
		}else if(camDes->bSubFormatType == APP_FORMAT_M420){
			printf("[%d]: YUV-%d-%d\n", index+1, 
				camDes->bFormatIndex, camDes->bNumFrameDescriptors);
		}else if(camDes->bSubFormatType == APP_FORMAT_I420){
			printf("[%d]: YUV-%d-%d\n", index+1, 
				camDes->bFormatIndex, camDes->bNumFrameDescriptors);
		}else{
			printf("[%d]: UNK-%d-%d\n", index+1, 
				camDes->bFormatIndex, camDes->bNumFrameDescriptors);
			continue;
		}
		vsFormatLen += sizeof(cameraDescriptor);
		vsFrameLen = 0;
		vsFramePtr = (uint8_t *)(camDes->bFrames);
		for(index2 = 0; index2 < camDes->bNumFrameDescriptors; index2++){
			camFrame = (cameraFrameDes *)(vsFramePtr+vsFrameLen);
			printf("Frame[%d]:Index:%d-%dx%d-%d/", index2+1, camFrame->bFrameIndex, 
					camFrame->wWidth, camFrame->wHeight, camFrame->bFrameIntervalType);
			for(index3=0; index3< camFrame->bFrameIntervalType; index3++){
				printf("%u/", camFrame->dwFrameIntervalArr[index3]);
			}
			printf("/\n");
			vsFrameLen += (sizeof(cameraFrameDes) + sizeof(uint32_t)*camFrame->bFrameIntervalType);
		}		
		vsFormatLen += vsFrameLen;
	}
	printf("vsFormatLen=%d\n", vsFormatLen);
	printf("========================END========================\n");
}


int mtkV4l2Config(uint8_t *confPtr, uint32_t confSize, uint32_t *useSize)
{
    int i, j, k;
		
	if(!confPtr|| !confSize){
		return -1;
	}
	
	cameraFrameDes *camFrame;
	cameraDescriptor *camDes;

	struct vdIn *vd = &camContext;
	uint8_t *tConfPtr = confPtr;
	uint16_t vsConfLen = 0;

	*useSize = 0;
	printf("Current Setting:%c%c%c%c-%ux%u-%u\n", vd->videoSet.format&0xFF, 
             	(vd->videoSet.format>>8)&0xFF, (vd->videoSet.format>>16)&0xFF,
             		(vd->videoSet.format>>24)&0xFF, vd->videoSet.width, vd->videoSet.height,
             			vd->videoSet.denominator);
	for(i=0; i < vd->formatCount; i++){		
		printf("Format[%d]:%s Frame:%d\n", i, vd->fmtlist[i].format.description, 
				vd->fmtlist[i].frameCount);
		camDes = (cameraDescriptor*)(tConfPtr+vsConfLen);
		//camDes->bFormatIndex = i;
		camDes->bFormatIndex = vd->fmtlist[i].frameCount; /*just for test, sdk bug*/
		camDes->bNumFrameDescriptors = vd->fmtlist[i].frameCount;
		if(vd->fmtlist[i].format.pixelformat == V4L2_PIX_FMT_YUYV){
			camDes->bFormatType = VS_FORMAT_UNCOMPRESSED;
			camDes->bSubFormatType = APP_FORMAT_YUV;
		}else if(vd->fmtlist[i].format.pixelformat == V4L2_PIX_FMT_MJPEG){
			camDes->bFormatType = VS_FORMAT_MJPEG;
			camDes->bSubFormatType = APP_FORMAT_MJPEG;
		}
		camDes->bDefaultFrameIndex = 0;
		vsConfLen += sizeof(cameraDescriptor);

		for(j=0; j < vd->fmtlist[i].frameCount; j++){			
			printf("\tFrame[%d]:%dx%d=>", j, vd->fmtlist[i].framelist[j].width, vd->fmtlist[i].framelist[j].height);
			camFrame = (cameraFrameDes*)(tConfPtr+vsConfLen);
			camFrame->bFrameIndex = j;
			camFrame->bmCapabilities = 0;
			camFrame->wWidth = vd->fmtlist[i].framelist[j].width;
			camFrame->wHeight = vd->fmtlist[i].framelist[j].height;
			camFrame->dwDefaultFrameInterval = (uint32_t)(1000000000/vd->fmtlist[i].framelist[j].interlist[0].denominator);
			camFrame->bFrameIntervalType = vd->fmtlist[i].framelist[j].intervalCount;
			for(k = 0; k < vd->fmtlist[i].framelist[j].intervalCount; k++){
				printf("%u/%u ", vd->fmtlist[i].framelist[j].interlist[k].numerator, vd->fmtlist[i].framelist[j].interlist[k].denominator);
				camFrame->dwFrameIntervalArr[k] = (uint32_t)(1000000000/vd->fmtlist[i].framelist[j].interlist[k].denominator);
			}
			vsConfLen += (sizeof(cameraFrameDes)+ vd->fmtlist[i].framelist[j].intervalCount*sizeof(uint32_t));
			printf("\n");
		}	
	}	

	*useSize = vsConfLen;
	printf("Config Len is %d\n", vsConfLen);
	usCamera_show(confPtr);

	return 0;
}

int mtkV4l2CurConfig(uint8_t *confPtr, uint32_t confSize, uint32_t *useSize)
{
    int i, j, k;
		
	if(!confPtr|| !confSize){
		return -1;
	}
	camAppConfig *appConf = NULL;
	struct vdIn *vd = &camContext;

	appConf = (camAppConfig *)confPtr;
	memset(appConf, 0, sizeof(camAppConfig));
	for(i=0; i < vd->formatCount; i++){		
		printf("Format[%d]:%s Frame:%d\n", i, vd->fmtlist[i].format.description, 
				vd->fmtlist[i].frameCount);
		if(vd->videoSet.format != vd->fmtlist[i].format.pixelformat){
	
			continue;
		}
		appConf->bFromatIndex = i;
		if(vd->fmtlist[i].format.pixelformat == V4L2_PIX_FMT_YUYV){
			appConf->bSubFormatType = APP_FORMAT_YUV;
		}else if(vd->fmtlist[i].format.pixelformat == V4L2_PIX_FMT_MJPEG){
			appConf->bSubFormatType = APP_FORMAT_MJPEG;
		}
		
		for(j=0; j < vd->fmtlist[i].frameCount; j++){			
			printf("\tFrame[%d]:%dx%d\n", j, vd->fmtlist[i].framelist[j].width, vd->fmtlist[i].framelist[j].height);
			if(vd->videoSet.width == vd->fmtlist[i].framelist[j].width &&
					vd->videoSet.height == vd->fmtlist[i].framelist[j].height){
				appConf->bFrameIndex = j;
				break;
			}
		}
		break;
	}	
	appConf->dwFrameInterval = (uint32_t)(1000000000/vd->videoSet.denominator);
	*useSize = sizeof(camAppConfig);


	return 0;
}
