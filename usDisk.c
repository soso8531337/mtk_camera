/*
 * @brief U-Storage Project
 *
 * @note
 * Copyright(C) i4season, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 */
#include <stdint.h>
#include "usDisk.h"
#include "usUsb.h"
#include "usSys.h"
#include "protocol.h"
#if defined(NXP_CHIP_18XX)
#include "board.h"
#include "USB.h"
#include <ctype.h>
#include <stdio.h>
#elif defined(GP_CHIP)
#include "USB.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#elif defined(LINUX)
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <getopt.h>
#include <pwd.h>
#include <grp.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/un.h>
#include <sys/select.h>
#include <linux/types.h>
#include <linux/netlink.h>
#include <sys/ioctl.h>
#include <scsi/sg.h>

#if !defined(BLKGETSIZE64)
#define BLKGETSIZE64           _IOR(0x12,114,size_t)
#endif

#endif

#define MSC_FTRANS_CLASS				0x08
#define MSC_FTRANS_SUBCLASS			0x06
#define MSC_FTRANS_PROTOCOL			0x50

#if defined(DEBUG_ENABLE1)
#define DSKDEBUG(...) do {printf("[DISK Mod]");printf(__VA_ARGS__);} while(0)
#else
#define DSKDEBUG(...)
#endif

#define STOR_DFT_PRO		"U-Storage"
#define STOR_DFT_VENDOR		"i4season"

enum{
	DISK_REOK=0,
	DISK_REPARA,
	DISK_REGEN,
	DISK_REINVAILD
};

enum{	
	STOR_CARD=1,
	STOR_HDD=0,
	STOR_MAX=2,
};
typedef struct {
	uint8_t disknum;
	uint32_t Blocks; /**< Number of blocks in the addressed LUN of the device. */
	uint32_t BlockSize; /**< Number of bytes in each block in the addressed LUN. */
	int64_t disk_cap;
	usb_device diskdev;
}usDisk_info;

usDisk_info uDinfo[STOR_MAX];

static usDisk_info * usDisk_FindLocation(uint8_t type);
#if defined(NXP_CHIP_18XX) || defined(GP_CHIP)
extern uint8_t DCOMP_MS_Host_NextMSInterfaceEndpoint(void* const CurrentDescriptor);
/*****************************************************************************
 * Private functions
 ****************************************************************************/

static uint8_t NXP_COMPFUNC_MSC_CLASS(void* const CurrentDescriptor)
{
	USB_Descriptor_Header_t* Header = DESCRIPTOR_PCAST(CurrentDescriptor, USB_Descriptor_Header_t);

	if (Header->Type == DTYPE_Interface){
		USB_Descriptor_Interface_t* Interface = DESCRIPTOR_PCAST(CurrentDescriptor, USB_Descriptor_Interface_t);
		if (Interface->Class  == MSC_FTRANS_CLASS&&
				(Interface->SubClass == MSC_FTRANS_SUBCLASS) &&
		   		(Interface->Protocol ==MSC_FTRANS_PROTOCOL)){
			return DESCRIPTOR_SEARCH_Found;
		}
	}

	return DESCRIPTOR_SEARCH_NotFound;
}

static uint8_t usDisk_DeviceDetectHDD(uint8_t type, void *os_priv)
{	
	USB_StdDesDevice_t DeviceDescriptorData;
	uint8_t MaxLUNIndex;
	usDisk_info DiskInfo;

	memset(&DiskInfo, 0, sizeof(usDisk_info));
	usb_device *usbdev = &(DiskInfo.diskdev);
	usbdev->usb_type = type;
	/*set os_priv*/
	usUsb_Init(usbdev, os_priv);
	/*GEt device description*/
	memset(&DeviceDescriptorData, 0, sizeof(USB_StdDesDevice_t));	
	printf("Begin Get Deivec Descriptor...\r\n");
	if(usUsb_GetDeviceDescriptor(usbdev, &DeviceDescriptorData)){
		DSKDEBUG("usUusb_GetDeviceDescriptor Failed\r\n");
		return DISK_REGEN;
	}
	
	printf("Deivec Descriptor bNumConfigurations=%d...\r\n", DeviceDescriptorData.bNumConfigurations);
	/*Set callback*/	
	nxp_clminface nxpcall;	
	nxpcall.callbackInterface = NXP_COMPFUNC_MSC_CLASS;
	nxpcall.callbackEndpoint= DCOMP_MS_Host_NextMSInterfaceEndpoint;
	/*Claim Interface*/
	nxpcall.bNumConfigurations = DeviceDescriptorData.bNumConfigurations;
	if(usUsb_ClaimInterface(usbdev, &nxpcall)){
		DSKDEBUG("Attached Device Not a Valid DiskDevice.\r\n");		
		return DISK_REINVAILD;
	}

	if(usUsb_GetMaxLUN(usbdev, &MaxLUNIndex)){		
		printf("Get LUN Failed\r\n");		
		return DISK_REINVAILD;
	}
	printf(("Total LUNs: %d - Using first LUN in device.\r\n"), (MaxLUNIndex + 1));

	SCSI_Sense_Response_t SenseData;	
	printf("Get RequestSense\r\n");		
	if(usUsb_RequestSense(usbdev, MaxLUNIndex, &SenseData)){
		DSKDEBUG("RequestSense Failed\r\n");		
		return DISK_REINVAILD;
	}
	printf("Get InquiryData\r\n");
	SCSI_Inquiry_t InquiryData;
	if(usUsb_GetInquiryData(usbdev, MaxLUNIndex, &InquiryData)){
		printf("GetInquiryData Failed\r\n");		
		return DISK_REINVAILD;
	}
	printf("Get ReadDeviceCapacity\r\n");
	if(usUsb_ReadDeviceCapacity(usbdev, &(DiskInfo.Blocks), &(DiskInfo.BlockSize))){
		printf("ReadDeviceCapacity Failed\r\n");		
		return DISK_REINVAILD;
	}
	DiskInfo.disk_cap = (int64_t)DiskInfo.BlockSize *DiskInfo.Blocks;
	
	usDisk_info *pDiskInfo= usDisk_FindLocation(type);

	if(pDiskInfo == NULL){
		DSKDEBUG("No Found Location\r\n");
		return DISK_REGEN;
	}
	DiskInfo.disknum = pDiskInfo->disknum;
	memcpy(pDiskInfo, &DiskInfo, sizeof(usDisk_info));
	
	printf("Mass Storage Device Enumerated. [Num:%d Blocks:%d BlockSzie:%d Cap:%lld]\r\n",
			pDiskInfo->disknum, pDiskInfo->Blocks, pDiskInfo->BlockSize, pDiskInfo->disk_cap);
	return DISK_REOK;
}

static uint8_t usDisk_DeviceDetectCard(uint8_t type, void *os_priv)
{
#if defined(GP_CHIP)
	DSKDEBUG("GP Support SDCard\r\n");
	usDisk_info diskInfo, *pDiskInfo = NULL;

	memset(&diskInfo, 0, sizeof(usDisk_info));
    /* Read Card information */
	diskInfo.Blocks = *((uint32_t*)os_priv);
	diskInfo.BlockSize = 512;
	diskInfo.disk_cap = (int64_t)diskInfo.Blocks *diskInfo.BlockSize;

	pDiskInfo= usDisk_FindLocation(type);	
	if(pDiskInfo == NULL){
		DSKDEBUG("No Found Location\r\n");
		return DISK_REGEN;
	}
	pDiskInfo->Blocks = diskInfo.Blocks;
	pDiskInfo->BlockSize = diskInfo.BlockSize;
	pDiskInfo->disk_cap = diskInfo.disk_cap;
	usb_device *usbdev = &(pDiskInfo->diskdev);
	usbdev->usb_type = type;
	
	printf("SD Card Enumerated. [Num:%d Blocks:%d BlockSzie:%u Cap:%lld]\r\n",
			pDiskInfo->disknum, pDiskInfo->Blocks, pDiskInfo->BlockSize, pDiskInfo->disk_cap);
	
	return DISK_REOK;
#elif defined(NXP_CHIP_18XX)
	usDisk_info *pDiskInfo= usDisk_FindLocation(type);
	mci_card_struct *sdinfo = (mci_card_struct *)os_priv;
	
	if(pDiskInfo == NULL){
		DSKDEBUG("No Found Location\r\n");
		return DISK_REGEN;
	}
	usb_device *usbdev = &(pDiskInfo->diskdev);
	usbdev->usb_type = type;

    /* Read Card information */
	pDiskInfo->Blocks = sdinfo->card_info.blocknr;
	pDiskInfo->BlockSize = sdinfo->card_info.block_len;

	//pDiskInfo->disk_cap = (int64_t)sdinfo->card_info.device_size;
	pDiskInfo->disk_cap = (int64_t)pDiskInfo->BlockSize *pDiskInfo->Blocks;
	printf("SD Card Enumerated. [Num:%d Blocks:%d BlockSzie:%u Cap:%lld]\r\n",
			pDiskInfo->disknum, pDiskInfo->Blocks, pDiskInfo->BlockSize, pDiskInfo->disk_cap);

	return DISK_REOK;
#endif	
}

void usDisk_DeviceInit(void *os_priv)
{
	return ;
}

uint8_t usDisk_DeviceDetect(uint8_t type, void *os_priv)
{
	if(type == USB_CARD){
		/*SDCard*/
		return usDisk_DeviceDetectCard(type, os_priv);
	}else if(type == USB_DISK){
		return usDisk_DeviceDetectHDD(type, os_priv);
	}

	return DISK_REINVAILD;
}

uint8_t usDisk_DeviceDisConnect(uint8_t type, void *os_priv)
{
	if(type == USB_DISK){
		memset(&uDinfo[STOR_HDD], 0, sizeof(usDisk_info));
	}else if(type == USB_CARD){
		memset(&uDinfo[STOR_CARD], 0, sizeof(usDisk_info));
	}
	return DISK_REOK;
}
uint8_t usDisk_cacheSYNC(int16_t wlun)
{
	return DISK_REOK;
}

uint8_t usDisk_DiskStartStop(uint8_t state)
{
	if(uDinfo[STOR_HDD].disknum){
		DSKDEBUG("%s Disk\r\n", (state == 1)?"Start":"Stop");
		return usUsb_DiskStartStop(&(uDinfo[STOR_HDD].diskdev), 0, state);
	}
	return DISK_REOK;
}

#elif defined(LINUX)

#ifndef BLKROSET
#define BLKROSET   _IO(0x12,93)
#define BLKROGET   _IO(0x12,94)
#define BLKRRPART  _IO(0x12,95)
#define BLKGETSIZE _IO(0x12,96)
#define BLKFLSBUF  _IO(0x12,97)
#define BLKRASET   _IO(0x12,98)
#define BLKRAGET   _IO(0x12,99)
#define BLKSSZGET  _IO(0x12,104)
#define BLKBSZGET  _IOR(0x12,112,size_t)
#define BLKBSZSET  _IOW(0x12,113,size_t)
#define BLKGETSIZE64 _IOR(0x12,114,size_t)
#endif

#define sys_offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

/* Purpose: container_of - cast a member of a structure out to the containing structure
 * In     : ptr   : the pointer to the member.
 *          type  : the type of the container struct this is embedded in.
 *          member: the name of the member within the struct.
 */
#define container_of(ptr, type, member) ({ \
        const typeof( ((type *)0)->member ) *__mptr = (ptr); \
        (type *)( (char *)__mptr - sys_offsetof(type,member) );})

#define BLKRASIZE			(1024)
#define SYS_CLA_BLK 	"/sys/class/block"
#define SYS_BLK		"/sys/block"
#define SYS_DROP_CACHE		"/proc/sys/vm/drop_caches"

typedef struct  _diskLinux{
	int diskFD;
	char dev[256];
}diskLinux;
extern int special_set_sdcard(char *devpath);

static int disk_chk_proc(char *dev)
{
	FILE *procpt = NULL;
	int ma, mi, sz;
	char line[128], ptname[64], devname[256] = {0};

	if ((procpt = fopen("/proc/partitions", "r")) == NULL) {
		DSKDEBUG("Fail to fopen(proc/partitions)\r\n");
		return 0;		
	}
	while (fgets(line, sizeof(line), procpt) != NULL) {
		memset(ptname, 0, sizeof(ptname));
		if (sscanf(line, " %d %d %d %[^\n ]",
				&ma, &mi, &sz, ptname) != 4)
				continue;
		if(!strcmp(ptname, dev)){
			DSKDEBUG("Partition File Found %s\r\n", dev);
			sprintf(devname, "/dev/%s", dev);
			if(access(devname, F_OK)){
				mknod(devname, S_IFBLK|0644, makedev(ma, mi));
			}	
			fclose(procpt);
			return 1;
		}
	}

	fclose(procpt);
	return 0;
}

static int blockdev_readahead(char *devname, int readahead)
{
	int fd;
	long larg;
	
	if(!devname){
		return -1;
	}
	fd = open(devname, O_RDWR);
	if (fd < 0) {
		return -1;
	}
	if(ioctl(fd, BLKRAGET, &larg) == -1){	
		DSKDEBUG("Get ReadAhead Error[%s]...\r\n", devname);
		close(fd);
		return -1;
	}
	if(ioctl(fd, BLKRASET, readahead) == -1){
		DSKDEBUG("Set ReadAhead Error[%s]...\r\n", devname);
		close(fd);
		return -1;
	}
	DSKDEBUG("Set %s ReadAhead From %ld To %d...\r\n", 
				devname, larg, readahead);
	close(fd);

	return 0;
}

static void dropCache(void)
{
	int dev_fd;

	if((dev_fd = open(SYS_DROP_CACHE, O_WRONLY)) < 0 ||
			write(dev_fd, "3", 1) <= 0){
		DSKDEBUG("Drop Cache Failed[%s]...\r\n", strerror(errno));
	}
	close(dev_fd);
}
void usDisk_DeviceInit(void *os_priv)
{
	struct dirent *dent;
	DIR *dir;
	struct stat statbuf;
	char sys_dir[1024] = {0}, linkdir[2048] = {0}, linkcnt[2048] = {0};

	/*Get Block Device*/
	if(stat(SYS_CLA_BLK, &statbuf) == 0){
		strcpy(sys_dir, SYS_CLA_BLK);
	}else{
		if(stat(SYS_BLK, &statbuf) == 0){
			strcpy(sys_dir, SYS_BLK);
		}else{
			DSKDEBUG("SYS_CLASS can not find block\r\n");
			memset(sys_dir, 0, sizeof(sys_dir));
			return ;
		}
	}
		
	dir = opendir(sys_dir);
	if(dir == NULL){
		DSKDEBUG("Opendir Failed\r\n");
		return ;
	}	
	while((dent = readdir(dir)) != NULL){
		char devpath[512], linkbuf[1024] = {0};
		int len;
		char devbuf[128] = {0};
		int fd = -1;
				
		if(strstr(dent->d_name, "sd") == NULL || strlen(dent->d_name) != 3){
			if(strstr(dent->d_name, "mmcblk") == NULL || 
				strlen(dent->d_name) != 7){
				continue;
			}
		}		
		if(disk_chk_proc(dent->d_name) == 0){
			DSKDEBUG("Partition Not Exist %s\r\n", dent->d_name);
			continue;
		}
		len = strlen(sys_dir) + strlen(dent->d_name) + 1;
		sprintf(devpath, "%s/%s", sys_dir, dent->d_name);
		devpath[len] = '\0';
		if(readlink(devpath, linkbuf, sizeof(linkbuf)-1) < 0){
			DSKDEBUG("ReadLink %s Error:%s\r\n", linkbuf, strerror(errno));
			continue;
		}

		sprintf(devbuf, "/dev/%s", dent->d_name);
		/*Check dev can open*/
		if((fd = open(devbuf, O_RDONLY)) < 0){
			DSKDEBUG("Open [%s] Failed:%s\r\n", 
					devbuf, strerror(errno));
			continue;
		}else{
			close(fd);
		}
		/*preread*/
		snprintf(linkdir, sizeof(linkdir)-1, "%s/%s", sys_dir, dent->d_name);
		readlink(linkdir, linkcnt, sizeof(linkcnt)-1);
		if(strstr(dent->d_name, "mmcblk") ||
				special_set_sdcard(linkcnt) == 1){
			usDisk_DeviceDetect(USB_CARD, (void*)devbuf);
		}else{
			usDisk_DeviceDetect(USB_DISK, (void*)devbuf);
		}
		DSKDEBUG("ADD Device [%s] To Storage List\r\n", dent->d_name);
		//break;
	}

	closedir(dir);
}
uint8_t usDisk_DeviceDetect(uint8_t type, void *os_priv)
{
	unsigned char sense_b[32] = {0};
	unsigned char rcap_buff[8] = {0};
	unsigned char cmd[] = {0x25, 0, 0, 0 , 0, 0};
	struct sg_io_hdr io_hdr;
	unsigned int lastblock, blocksize;
	int dev_fd;
	int64_t disk_cap = 0;
	diskLinux *linxDiskinfo = NULL;
	char *dev = (char *)os_priv;

	if(os_priv == NULL){
		return DISK_REGEN;
	}
	usDisk_info *pDiskInfo= usDisk_FindLocation(type);
	if(pDiskInfo == NULL){
		DSKDEBUG("No Found Location\r\n");
		return DISK_REGEN;
	}

	linxDiskinfo = calloc(1, sizeof(diskLinux));
	if(!linxDiskinfo){
		DSKDEBUG("Calloc Memory Failed\r\n");
		return DISK_REGEN;
	}
	strcpy(linxDiskinfo->dev, dev);
	pDiskInfo->diskdev.usb_type = type;
	pDiskInfo->diskdev.os_priv = (void*)(linxDiskinfo->dev);

	/*Set readahead parameter*/
	if(blockdev_readahead(dev, BLKRASIZE) <  0){
		DSKDEBUG("SetReadAhead %s Failed\r\n", dev);
	}
	linxDiskinfo->diskFD = open(dev, O_RDWR);
	if(linxDiskinfo->diskFD < 0){
			DSKDEBUG("Open diskFD %s Failed\r\n", dev);
	}
	DSKDEBUG("Open diskFD %s %d Successful\r\n", dev, linxDiskinfo->diskFD);

	dev_fd= open(dev, O_RDWR | O_NONBLOCK);
	if (dev_fd < 0 && errno == EROFS)
		dev_fd = open(dev, O_RDONLY | O_NONBLOCK);
	if (dev_fd<0){
		DSKDEBUG("Open %s Failed:%s", dev, strerror(errno));
		if(linxDiskinfo){
			close(linxDiskinfo->diskFD);
			free(linxDiskinfo);
		}
		return DISK_REGEN; 
	}

	memset(&io_hdr, 0, sizeof(struct sg_io_hdr));
	io_hdr.interface_id = 'S';
	io_hdr.cmd_len = sizeof(cmd);
	io_hdr.dxferp = rcap_buff;
	io_hdr.dxfer_len = 8;
	io_hdr.mx_sb_len = sizeof(sense_b);
	io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
	io_hdr.cmdp = cmd;
	io_hdr.sbp = sense_b;
	io_hdr.timeout = 20000;

	if(ioctl(dev_fd, SG_IO, &io_hdr)<0){
		DSKDEBUG("IOCTRL error:%s[Used BLKGETSIZE64]!", strerror(errno));
		if (ioctl(dev_fd, BLKGETSIZE64, &disk_cap) != 0) {			
			DSKDEBUG("Get Disk Capatiy Failed");
		}		
		DSKDEBUG("Disk Capacity = %lld Bytes", disk_cap);
		close(dev_fd);
		pDiskInfo->disk_cap = disk_cap;
		pDiskInfo->BlockSize = DEF_SECTOR;
		pDiskInfo->Blocks = disk_cap/pDiskInfo->BlockSize;
		/*Drop cache when the new sdcard insert*/
		DSKDEBUG("Drop Cache Becasuse OF New SDCard Insert[%s]..\r\n", dev);
		dropCache();
		return DISK_REOK;
	}

	/* Address of last disk block */
	lastblock =  ((rcap_buff[0]<<24)|(rcap_buff[1]<<16)|
	(rcap_buff[2]<<8)|(rcap_buff[3]));

	/* Block size */
	blocksize =  ((rcap_buff[4]<<24)|(rcap_buff[5]<<16)|
	(rcap_buff[6]<<8)|(rcap_buff[7]));

	/* Calculate disk capacity */
	pDiskInfo->Blocks= (lastblock+1);
	pDiskInfo->BlockSize= blocksize;	
	pDiskInfo->disk_cap  = (lastblock+1);
	pDiskInfo->disk_cap *= blocksize;
	DSKDEBUG("Disk Blocks = %u BlockSize = %u Disk Capacity=%lld\r\n", 
			pDiskInfo->Blocks, pDiskInfo->BlockSize, pDiskInfo->disk_cap);
	close(dev_fd);
	/*Drop cache when the new card insert*/
	DSKDEBUG("Drop Cache Becasuse OF New Disk Insert[%s]..\r\n", dev);
	dropCache();

	return DISK_REOK;
}

uint8_t usDisk_DeviceDisConnect(uint8_t type, void *os_priv)
{
	uint8_t curdisk = 0;	
	diskLinux *linxDiskinfo = NULL;

	if(!os_priv){
		return DISK_REPARA;
	}
	for(curdisk = 0; curdisk < STOR_MAX; curdisk++){
		/*We need to use dev[0] represent first element address, if use dev, 
			it represent arrary*/
		if(!uDinfo[curdisk].diskdev.os_priv){
			continue;
		}
		linxDiskinfo = container_of((char*)(uDinfo[curdisk].diskdev.os_priv),
										diskLinux, dev[0]);
		if(linxDiskinfo &&
				!strcmp(linxDiskinfo->dev, os_priv)){
			DSKDEBUG("Disk DiskConncect [%s]\r\n",  os_priv);
			close(linxDiskinfo->diskFD);
			free(linxDiskinfo);
			memset(&uDinfo[curdisk], 0, sizeof(usDisk_info));			
			return DISK_REOK;
		}
	}
	
	DSKDEBUG("Disk Not Found [%s]\r\n", os_priv);
	return DISK_REGEN;
}

uint8_t usDisk_cacheSYNC(int16_t wlun)
{
	diskLinux *linxDiskinfo = NULL;

	if(wlun >= STOR_MAX){
		DSKDEBUG("Cache Disk Error:%d\r\n", wlun);
		return DISK_REPARA;
	}
	if(!uDinfo[wlun].diskdev.os_priv){
		DSKDEBUG("Cache Disk May Be PlugOut:%d\r\n", wlun);
		return DISK_REGEN;
	}
	linxDiskinfo = container_of((char*)(uDinfo[wlun].diskdev.os_priv),
									diskLinux, dev[0]);
	fsync(linxDiskinfo->diskFD);

	DSKDEBUG("ReWrite Cache To DiskFD:%d Successful[wlun:%d]...\r\n", 
					linxDiskinfo->diskFD, wlun);

	return DISK_REOK;
}

uint8_t usDisk_DiskStartStop(uint8_t state)
{
	return DISK_REOK;
}
#endif
static usDisk_info * usDisk_FindLocation(uint8_t type)
{
	if(type == USB_CARD && uDinfo[STOR_CARD].disknum == 0){
		DSKDEBUG("Find SDCard Location\r\n");		
		memset(&uDinfo[STOR_CARD], 0, sizeof(usDisk_info));
		uDinfo[STOR_CARD].disknum = 1<< STOR_CARD;
		return &uDinfo[STOR_CARD];
	}else if(type == USB_DISK && uDinfo[STOR_HDD].disknum == 0){
		DSKDEBUG("Find HDD Location\r\n");
		memset(&uDinfo[STOR_HDD], 0, sizeof(usDisk_info));
		uDinfo[STOR_HDD].disknum = 1<< STOR_HDD;
		return &uDinfo[STOR_HDD];
	}

	return NULL;
}

static int8_t  usDisk_FindStorage(int16_t wlun)
{
	int8_t curdisk;
	if(wlun >= STOR_MAX){
		DSKDEBUG("DiskNum:%d To Large.\r\n", wlun);
		return -1;
	}

	for(curdisk=0; curdisk< STOR_MAX; curdisk++){
		if(uDinfo[curdisk].disknum == (1 << wlun)){
			DSKDEBUG("Found Storage:%d\r\n", wlun);
			return curdisk;
		}
	}

	DSKDEBUG("No Found Storage:%d\r\n", wlun);
	return -1;
}

uint8_t usDisk_DiskReadSectors(void *buff, int16_t wlun, uint32_t secStart, uint32_t numSec)
{
	int8_t cwlun = -1;

	if(!buff || wlun > STOR_MAX){
		DSKDEBUG("DiskReadSectors Failed[DiskNum:%d].\r\n", wlun);
		return DISK_REPARA;
	}
	if((cwlun = usDisk_FindStorage(wlun)) < 0){
		return DISK_REGEN;
	}
	if(usUsb_DiskReadSectors(&(uDinfo[cwlun].diskdev), 
			buff, secStart,numSec, uDinfo[cwlun].BlockSize)){
		DSKDEBUG("DiskReadSectors Failed[DiskNum:%d secStart:%d numSec:%d].\r\n", 
						uDinfo[cwlun].disknum, secStart, numSec);
		return DISK_REGEN;
	}
	
	DSKDEBUG("DiskReadSectors Successful[DiskNum:%d secStart:%d numSec:%d].\r\n", 
					uDinfo[cwlun].disknum, secStart, numSec);
	return DISK_REOK;
}

uint8_t usDisk_DiskWriteSectors(void *buff, int16_t wlun, uint32_t secStart, uint32_t numSec)
{
	int8_t cwlun = -1;

	if(!buff || wlun > STOR_MAX){
		DSKDEBUG("DiskWriteSectors Failed[DiskNum:%d].\r\n", wlun);
		return DISK_REPARA;
	}
	if((cwlun = usDisk_FindStorage(wlun)) < 0){
		return DISK_REGEN;
	}	
	if(usUsb_DiskWriteSectors(&(uDinfo[cwlun].diskdev), 
				buff, secStart,numSec, uDinfo[cwlun].BlockSize)){
		DSKDEBUG("DiskWriteSectors Failed[DiskNum:%d secStart:%d numSec:%d].\r\n", 
						uDinfo[cwlun].disknum, secStart, numSec);
		return DISK_REGEN;
	}
	
	DSKDEBUG("DiskWriteSectors Successful[DiskNum:%d secStart:%d numSec:%d].\r\n", 
					uDinfo[cwlun].disknum, secStart, numSec);
	return DISK_REOK;	
}

uint8_t usDisk_DiskNum(void)
{
	uint8_t curdisk = 0, totalDisk = 0;

	for(curdisk = 0; curdisk < STOR_MAX; curdisk++){
		totalDisk += uDinfo[curdisk].disknum;
	}
	DSKDEBUG("Total Disk %d\r\n", totalDisk);
	return totalDisk;
}

uint8_t usDisk_DiskInquiry(int16_t wlun, struct scsi_inquiry_info *inquiry)
{
	int8_t cwlun;
	if(!inquiry){
		DSKDEBUG("usDisk_DiskInquiry Parameter Error\r\n");
		return DISK_REPARA;
	}	
	memset(inquiry, 0, sizeof(struct scsi_inquiry_info));
	if((cwlun = usDisk_FindStorage(wlun)) < 0){
		return DISK_REPARA;
	}
	memset(inquiry, 0, sizeof(struct scsi_inquiry_info));
	/*Init Other Info*/
	inquiry->size = uDinfo[cwlun].disk_cap;
	strcpy(inquiry->product, STOR_DFT_PRO);
	strcpy(inquiry->vendor, STOR_DFT_VENDOR);
	strcpy(inquiry->serial, "1234567890abcdef");
	strcpy(inquiry->version, "1.0");

	return DISK_REOK;	
}
