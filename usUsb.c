/*
 * @note
 * Copyright(C) i4season U-Storage, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 *
 */

#include <ctype.h>
#include <stdio.h>
#include "usUsb.h"
#include "usSys.h"

#if defined(NXP_CHIP_18XX)
#include "board.h"
#include "USB.h"
#include <ctype.h>
#include <stdio.h>
#elif defined(LINUX)
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <libusb-1.0/libusb.h>
#elif defined(GP_CHIP)
#include "USB.h"
#include <ctype.h>
#include <stdio.h>
#include "drv_l2_sdc.h"
#include "drv_l1_sdc.h"
#include "drv_l1_ext_int.h"
#include "cp15.h"

#endif

#if defined(DEBUG_ENABLE1)
#define USBDEBUG(...) do {printf("[USB Mod]");printf(__VA_ARGS__);} while(0)
#else
#define USBDEBUG(...)
#endif

#define USB_TRUE		1
#define USB_FALSE		0

#define MAX_USB_PACKAGE   	(16*1024)
#define MAX_SCSI_SECTOR   	(32)

/***********************CHIP RELETIVE USB****************************/
/*****************************************************************************
 * Private functions
 ****************************************************************************/
#if defined(NXP_CHIP_18XX)
/*Special USB Command*/
static uint8_t NXP_SendControlRequest(const uint8_t corenum, 
			uint8_t bmRequestType, uint8_t bRequest, 
			uint16_t wValue, uint16_t wIndex, uint16_t wLength, void * const data)
{
	USB_ControlRequest = (USB_Request_Header_t)
		{
		.bmRequestType = (bmRequestType),
		.bRequest      = bRequest,
		.wValue        = wValue,
		.wIndex        = wIndex,
		.wLength       = wLength,
		};

	Pipe_SelectPipe(corenum, PIPE_CONTROLPIPE);

	return USB_Host_SendControlRequest(corenum,data);
}

static uint8_t NXP_BlukPacketReceiveStream(usb_device *usbdev, uint8_t *buffer, 
													uint32_t length, uint32_t *actual_length)
{
	uint8_t  ErrorCode = PIPE_RWSTREAM_NoError;
	uint32_t already = 0;
	const USB_ClassInfo_MS_Host_t *MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)usbdev->os_priv;

	if(!usbdev || !buffer || !actual_length){
		return USB_REPARA;
	}
	*actual_length = 0;
	while (already < length) {		
		uint32_t actual = 0;
		if(already == 0 && length > 512){
			/*we must receive 512*n, if it is not 512*n, divide twice*/
			ErrorCode = Pipe_Streaming2(MSInterfaceInfo->Config.PortNumber, MSInterfaceInfo->Config.DataINPipeNumber,
						buffer, length-length%512, &actual);
		}else{
			ErrorCode = Pipe_Streaming2(MSInterfaceInfo->Config.PortNumber, MSInterfaceInfo->Config.DataINPipeNumber,
						buffer+already, length-already, &actual);
		}
		if (ErrorCode) {
			USBDEBUG("USB Receive Error[%d]\r\n", ErrorCode);
			return ErrorCode;
		}
		already+= actual;
		*actual_length += actual;

		//USBDEBUG("USB Stream HcdDataTransfer Receive[%d]\r\n", Pipe_BytesInPipe(corenum));
	}

	USBDEBUG("USB Stream Receive [%d/%dBytes]\r\n", *actual_length, length);

	return USB_REOK;
}	

#ifdef NXP_ALLOW_WARING
static uint8_t NXP_BlukPacketReceiveStream2(usb_device *usbdev, uint8_t *buffer, 
													uint32_t length, uint32_t *actual_length)
{
	uint8_t  ErrorCode = PIPE_RWSTREAM_NoError;
	uint8_t ErrorCallback = 0;
	const USB_ClassInfo_MS_Host_t *MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)usbdev->os_priv;

	if(!usbdev || !buffer || !actual_length){
		return USB_REPARA;
	}
	Pipe_SelectPipe(MSInterfaceInfo->Config.PortNumber, 
			MSInterfaceInfo->Config.DataINPipeNumber);
	
	ErrorCode = Pipe_Streaming(MSInterfaceInfo->Config.PortNumber,buffer, length, 
				usbdev->wMaxPacketSize);
	if(ErrorCode == PIPE_RWSTREAM_IncompleteTransfer){
		USBDEBUG("PiPe Streaming Not Ready[%d]...\r\n", PIPE_RWSTREAM_IncompleteTransfer);			
	}else{
		*actual_length = PipeInfo[MSInterfaceInfo->Config.PortNumber][pipeselected[MSInterfaceInfo->Config.PortNumber]].ByteTransfered;
 		USBDEBUG("PiPe Streaming Read[%dBytes]...\r\n",  *actual_length);
	}
	while(((ErrorCallback = MSInterfaceInfo->State.IsActive) == USB_TRUE)
			&& !Pipe_IsStatusOK(MSInterfaceInfo->Config.PortNumber));
	
	Pipe_ClearIN(MSInterfaceInfo->Config.PortNumber);
	if(ErrorCallback != USB_TRUE){
		USBDEBUG("CallBack Excute Error Something Happen...\r\n");
		return USB_REGEN;
	}

	return USB_REOK;
}	
#endif

static uint8_t NXP_BlukPacketReceive(usb_device *usbdev, uint8_t *buffer, 
													uint32_t length, uint32_t *actual_length)
{
	uint8_t  ErrorCode = PIPE_RWSTREAM_NoError;
	
	const USB_ClassInfo_MS_Host_t *MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)usbdev->os_priv;

	if(!usbdev || !buffer || !actual_length){
		return USB_REPARA;
	}

	ErrorCode = Pipe_Streaming2(MSInterfaceInfo->Config.PortNumber, MSInterfaceInfo->Config.DataINPipeNumber,
					buffer, length, actual_length);
	if (ErrorCode) {
		USBDEBUG("USB Receive Error[%d]\r\n", ErrorCode);
		return ErrorCode;
	}
	USBDEBUG("USB Receive [%d/%dBytes]\r\n", *actual_length, length);

	return USB_REOK;
}	

static uint8_t NXP_BlukPacketSend(usb_device *usbdev, uint8_t *buffer, 
													uint32_t length, uint32_t *actual_length)
{
	uint8_t  ErrorCode = PIPE_RWSTREAM_NoError;
	const USB_ClassInfo_MS_Host_t *MSInterfaceInfo;
	uint32_t already = 0;
	
	if(!usbdev || !buffer || !actual_length){
		return USB_REPARA;
	}
	*actual_length= 0;
	MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)usbdev->os_priv;
	while(already < length){		
		uint32_t sndlen = 0;

		if(already == 0 && length % 512 == 0){
			ErrorCode = Pipe_Streaming2(MSInterfaceInfo->Config.PortNumber, MSInterfaceInfo->Config.DataOUTPipeNumber,
							buffer, length-1, &sndlen);
		}else{
			ErrorCode = Pipe_Streaming2(MSInterfaceInfo->Config.PortNumber, MSInterfaceInfo->Config.DataOUTPipeNumber,
							buffer+already, length-already, &sndlen);
		}

		if (ErrorCode) {
			USBDEBUG("USB Send Error[%d]\r\n", ErrorCode);
			return ErrorCode;
		}
		USBDEBUG("Send PiPe Streaming Write[%dBytes]...\r\n", sndlen);
		already += sndlen;		
		*actual_length += sndlen;
	}
	USBDEBUG("USB Send [%d/%dBytes]\r\n", *actual_length, length);
	return USB_REOK;
}

static uint8_t NXP_GetDeviceDescriptor(usb_device *usbdev, USB_StdDesDevice_t *DeviceDescriptorData)
{	
	if(!DeviceDescriptorData || !usbdev){
		return USB_REPARA;
	}
	if(USB_Host_GetDeviceDescriptor(usbdev->device_address, (USB_Descriptor_Device_t*)DeviceDescriptorData)){
		USBDEBUG("Error Getting Device Descriptor.\r\n");
		return USB_REGEN;
	}
	return USB_REOK;
}

static uint8_t NXP_GetDeviceConfigDescriptor(usb_device *usbdev, uint8_t index, uint16_t *cfgsize,
					uint8_t *ConfigDescriptorData, uint16_t ConfigDescriptorDataLen)
{	
	if(!ConfigDescriptorData || !usbdev || !cfgsize){
		return USB_REPARA;
	}
	if (USB_Host_GetDeviceConfigDescriptor(usbdev->device_address, index, cfgsize, 
				ConfigDescriptorData, ConfigDescriptorDataLen) != HOST_GETCONFIG_Successful) {
		USBDEBUG("Error Retrieving Configuration Descriptor.\r\n");
		return USB_REGEN;
	}

	return USB_REOK;
}

static uint8_t NXP_SetDeviceConfigDescriptor(usb_device *usbdev, uint8_t cfgindex)
{	
	if(!usbdev){
		return USB_REPARA;
	}
	if (USB_Host_SetDeviceConfiguration(usbdev->device_address, cfgindex) != 
				HOST_SENDCONTROL_Successful) {
		USBDEBUG("Error Setting Device Configuration.\r\n");
		return USB_REGEN;
	}
	
	return USB_REOK;
}

static uint8_t NXP_ClaimInterface(usb_device *usbdev, nxp_clminface*cPrivate)
{
	USB_Descriptor_Endpoint_t*  DataINEndpoint       = NULL;
	USB_Descriptor_Endpoint_t*  DataOUTEndpoint      = NULL;
	USB_Descriptor_Interface_t* MassStorageInterface = NULL;
	uint8_t portnum = 0, curConfigurations = 1;
	uint16_t ConfigDescriptorSize = 0;
	uint8_t  ConfigDescriptorData[512], *PtrConfigDescriptorData = NULL;
	

	if(!cPrivate){
		return USB_REPARA;
	}
	USB_ClassInfo_MS_Host_t *MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)(usbdev->os_priv);
	if( !MSInterfaceInfo||
			!cPrivate->callbackEndpoint || !cPrivate->callbackInterface){
		return USB_REPARA;
	}

	for(curConfigurations= 1; curConfigurations <= cPrivate->bNumConfigurations; curConfigurations++){
		if (USB_Host_GetDeviceConfigDescriptor(MSInterfaceInfo->Config.PortNumber, 
								curConfigurations, &ConfigDescriptorSize, ConfigDescriptorData,
								sizeof(ConfigDescriptorData)) != HOST_GETCONFIG_Successful) {
			USBDEBUG("Error Retrieving Configuration Descriptor.\r\n");
			continue;
		}
		if (DESCRIPTOR_TYPE(ConfigDescriptorData) != DTYPE_Configuration){
			continue;
		}		
		usUsb_Print(ConfigDescriptorData, ConfigDescriptorSize); 
		/*Set array name to point var, USB_GetNextDescriptorComp will change point*/
		PtrConfigDescriptorData = ConfigDescriptorData;		
		while (!(DataINEndpoint) || !(DataOUTEndpoint)){
			if (!(MassStorageInterface) ||
					USB_GetNextDescriptorComp(&ConfigDescriptorSize, (void **)&PtrConfigDescriptorData,
							cPrivate->callbackEndpoint) != DESCRIPTOR_SEARCH_COMP_Found){
				if (USB_GetNextDescriptorComp(&ConfigDescriptorSize, (void **)&PtrConfigDescriptorData,
							cPrivate->callbackInterface) != DESCRIPTOR_SEARCH_COMP_Found){
					DataINEndpoint	= NULL;
					DataOUTEndpoint = NULL;
					MassStorageInterface = NULL;					
					USBDEBUG("No Found Vaild Interface At Configuration[%d].\r\n", curConfigurations);
					break;
				}
				MassStorageInterface = DESCRIPTOR_PCAST(PtrConfigDescriptorData, USB_Descriptor_Interface_t);

				DataINEndpoint  = NULL;
				DataOUTEndpoint = NULL;

				continue;
			}

			USB_Descriptor_Endpoint_t* EndpointData = DESCRIPTOR_PCAST(PtrConfigDescriptorData, USB_Descriptor_Endpoint_t);

			if ((EndpointData->EndpointAddress & ENDPOINT_DIR_MASK) == ENDPOINT_DIR_IN){
				DataINEndpoint  = EndpointData;				
				USBDEBUG("Found Vaild INInterface At Configuration[%d].\r\n", curConfigurations);
			}else{
				DataOUTEndpoint = EndpointData;				
				USBDEBUG("Found Vaild OUTInterface At Configuration[%d].\r\n", curConfigurations);
			}			
		}
		if(DataINEndpoint && DataOUTEndpoint){
			break;
		}
	}
	if(!(DataINEndpoint) || !(DataOUTEndpoint) ||
			(curConfigurations > cPrivate->bNumConfigurations)){
		USBDEBUG("No Found Suitable Interface.\r\n");
		return USB_REGEN;
	}
	memset(&MSInterfaceInfo->State, 0x00, sizeof(MSInterfaceInfo->State));	
	portnum = MSInterfaceInfo->Config.PortNumber;
	for (uint8_t PipeNum = 1; PipeNum < PIPE_TOTAL_PIPES; PipeNum++){
		uint16_t Size;
		uint8_t  Type;
		uint8_t  Token;
		uint8_t  EndpointAddress;
		bool     DoubleBanked;

		if (PipeNum == MSInterfaceInfo->Config.DataINPipeNumber){
			Size            = le16_to_cpu(DataINEndpoint->EndpointSize);
			EndpointAddress = DataINEndpoint->EndpointAddress;
			Token           = PIPE_TOKEN_IN;
			Type            = EP_TYPE_BULK;
			DoubleBanked    = MSInterfaceInfo->Config.DataINPipeDoubleBank;

			MSInterfaceInfo->State.DataINPipeSize = DataINEndpoint->EndpointSize;
			
		}else if (PipeNum == MSInterfaceInfo->Config.DataOUTPipeNumber){
			Size            = le16_to_cpu(DataOUTEndpoint->EndpointSize);
			EndpointAddress = DataOUTEndpoint->EndpointAddress;
			Token           = PIPE_TOKEN_OUT;
			Type            = EP_TYPE_BULK;
			DoubleBanked    = MSInterfaceInfo->Config.DataOUTPipeDoubleBank;
			
			MSInterfaceInfo->State.DataOUTPipeSize = DataOUTEndpoint->EndpointSize;
		}else{
			continue;
		}

		if (!(Pipe_ConfigurePipe(portnum,PipeNum, Type, Token, EndpointAddress, Size,
		                         DoubleBanked ? PIPE_BANK_DOUBLE : PIPE_BANK_SINGLE))){
			return USB_REGEN;
		}
	}

	/*Set Configuration*/
	if(NXP_SetDeviceConfigDescriptor(usbdev, curConfigurations)){
		USBDEBUG("Error Setting Device Configuration.\r\n");
		return USB_REGEN;
	}

	MSInterfaceInfo->State.InterfaceNumber = MassStorageInterface->InterfaceNumber;
	MSInterfaceInfo->State.IsActive = true;
	/*Set the usbdev struct*/
	usbdev->device_address= MSInterfaceInfo->Config.PortNumber;
	usbdev->wMaxPacketSize = MSInterfaceInfo->State.DataOUTPipeSize;
	usbdev->ep_in = MSInterfaceInfo->Config.DataINPipeNumber;
	usbdev->ep_out = MSInterfaceInfo->Config.DataOUTPipeNumber;
	return USB_REOK;
}

#ifdef NXP_ALLOW_WARING
static void NXP_SdcardWriteCheck(void *buff, uint32_t secStart, uint32_t numSec)
{
	uint8_t secBuf[512];
	uint32_t startCur;
	uint32_t diffCur;
	
	for(startCur = 0; startCur < numSec; startCur++){
		int32_t act_read;
		act_read =  Chip_SDMMC_ReadBlocks(LPC_SDMMC, secBuf, secStart+startCur, 1);
		if(!act_read){
			printf("Error reading SDCard\r\n");			
			return ;
		}
		uint8_t *diffbuf = (uint8_t *)buff + startCur*512;
		for(diffCur = 0; diffCur < 512; diffCur++){
			if(diffbuf[diffCur] != secBuf[diffCur]){
				printf("Data Not Same===>ErrorSec:%d SecStart:%d NumSec:%d>>Blow is detail Sector:\r\n", startCur, secStart, numSec);
				usUsb_Print(diffbuf, 512);
				printf("=================================================");
				usUsb_Print(secBuf, 512);
				printf("\r\n\r\n");
				break;
			}
		}
	}
	printf("Data Check Finish SecStart:%d NumSec:%d\r\n", secStart, numSec);
}
#endif
static uint8_t NXP_Init(usb_device *usbdev, void *os_priv)
{
	USB_ClassInfo_MS_Host_t *MSInterfaceInfo;
	/*os_priv is USB_ClassInfo_MS_Host_t *MSInterfaceInfo*/
	if(!usbdev || !os_priv){
		return USB_REPARA;
	}
	usbdev->os_priv = os_priv;

	if(usbdev->usb_type != USB_CARD){
		MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)(os_priv);
		/*Just set device_address == PortNumber*/
		usbdev->device_address  = MSInterfaceInfo->Config.PortNumber;
	}

	return USB_REOK;
}

#ifdef NXP_DISK_LIMITSIZE /*limit read write disk size*/
uint8_t NXP_DiskReadSectors(usb_device *usbdev, 
				void *buff, uint32_t secStart, uint32_t numSec, uint16_t BlockSize)
{
	int ret;
	uint8_t *curBuf = NULL;
	uint32_t already = 0, curSec;
	uint8_t numSecPer;

	curBuf = (uint8_t*)buff;
	curSec = secStart;
	while(already < numSec){		
		if(numSec-already > MAX_SCSI_SECTOR){
			numSecPer = MAX_SCSI_SECTOR;
		}else{
			numSecPer = numSec-already;
		}
		if(usbdev->usb_type == USB_CARD){
			int32_t act_read;
			act_read =  Chip_SDMMC_ReadBlocks(LPC_SDMMC, 
					(void *)(curBuf+already*DEF_SECTOR), curSec, numSecPer);
			if(!act_read){
				USBDEBUG("Error write SDCard\r\n"); 		
				return USB_REGEN;
			}
		}else{	
			ret = MS_Host_ReadDeviceBlocks((USB_ClassInfo_MS_Host_t*)usbdev->os_priv, 0, 
								curSec, numSecPer, BlockSize, (void *)(curBuf+already*BlockSize));
			if(ret) {
				printf("Error reading device block. ret = %d\r\n", ret);				
				/*Read Error not Reset Device Configuration modify by zhangwei 20170118*/
				//USB_Host_SetDeviceConfiguration(usbdev->device_address, 0);
				return USB_REGEN;
			}
		}
		already += numSecPer;
		curSec += numSecPer;
		USBDEBUG("SCSI Read %dSectors [%d/%d/%d]\r\n", numSecPer, curSec, already, numSec);		
	}

	return USB_REOK;
}

uint8_t NXP_DiskWriteSectors(usb_device *usbdev, 
					void *buff, uint32_t secStart, uint32_t numSec, uint16_t BlockSize)
{
	int ret;
	uint8_t *curBuf = NULL;
	uint32_t already = 0, curSec;
	uint8_t numSecPer;

	curBuf = (uint8_t*)buff;
	curSec = secStart;
	while(already < numSec){		
		if(numSec-already > MAX_SCSI_SECTOR){
			numSecPer = MAX_SCSI_SECTOR;
		}else{
			numSecPer = numSec-already;
		}
		if(usbdev->usb_type == USB_CARD){
			int32_t act_written;	
			act_written =  Chip_SDMMC_WriteBlocks(LPC_SDMMC, 
					(void *)(curBuf+already*DEF_SECTOR), curSec, numSecPer);
			if(!act_written){
				USBDEBUG("Error write SDCard\r\n"); 		
				return USB_REGEN;
			}
		}else{
			ret = MS_Host_WriteDeviceBlocks((USB_ClassInfo_MS_Host_t*)usbdev->os_priv, 0, 
								curSec, numSecPer, BlockSize, (void *)(curBuf+already*BlockSize));
			if(ret) {
				printf("Error writing device block. ret = %d\r\n", ret);				
				/*Write Error not Reset Device Configuration modify by zhangwei 20170118*/
				//USB_Host_SetDeviceConfiguration(usbdev->device_address, 0);
				return USB_REGEN;
			}
		}
		already += numSecPer;
		curSec += numSecPer;
		USBDEBUG("SCSI Write %dSectors [%d/%d/%d]\r\n", numSecPer, curSec, already, numSec);		
	}

	return USB_REOK;
}

#else
uint8_t NXP_DiskReadSectors(usb_device *usbdev, 
				void *buff, uint32_t secStart, uint32_t numSec, uint16_t BlockSize)
{
	int ret;

	if(!usbdev || !buff){
		printf("Parameter Error\r\n");
		return USB_REPARA;
	}
	if(usbdev->usb_type == USB_CARD){
		int32_t act_read;

		act_read =  Chip_SDMMC_ReadBlocks(LPC_SDMMC, buff, secStart, numSec);
		if(!act_read){
			USBDEBUG("Error reading SDCard\r\n");			
			return USB_REGEN;
		}
		USBDEBUG("SDCard Read %dSectors [StarSector:%d][%dBytes]\r\n", 
				numSec, secStart, act_read);	
		return USB_REOK;		
	}else{
		/*USB Storage Read*/
		ret = MS_Host_ReadDeviceBlocks((USB_ClassInfo_MS_Host_t*)usbdev->os_priv, 0, 
				secStart, numSec, BlockSize, buff);
		if(ret) {
			printf("Error reading device block. ret = %d\r\n", ret);
			/*Read Error not Reset Device Configuration modify by zhangwei 20170118*/
			//USB_Host_SetDeviceConfiguration(usbdev->device_address, 0);
			return USB_REGEN;
		}	
		USBDEBUG("SCSI Read %dSectors [StarSector:%d]\r\n", numSec, secStart); 	
	}
	return USB_REOK;
}

uint8_t NXP_DiskWriteSectors(usb_device *usbdev, 
					void *buff, uint32_t secStart, uint32_t numSec, uint16_t BlockSize)
{
	int ret;

	if(!usbdev || !buff){
		printf("Parameter Error\r\n");
		return USB_REPARA;
	}
	if(usbdev->usb_type == USB_CARD){
		int32_t act_written;
		
		act_written =  Chip_SDMMC_WriteBlocks(LPC_SDMMC, buff, secStart, numSec);
		if(!act_written){
			USBDEBUG("Error write SDCard\r\n");			
			return USB_REGEN;
		}
		USBDEBUG("SDCard Write %dSectors [StarSector:%d][%dBytes]\r\n", 
				numSec, secStart, act_written);	
		return USB_REOK;		
	}else{
		/*USB Storage Write*/
		ret = MS_Host_WriteDeviceBlocks((USB_ClassInfo_MS_Host_t*)usbdev->os_priv, 0, 
						secStart, numSec, BlockSize, buff);
		if(ret) {
			printf("Error writing device block. ret = %d\r\n", ret);			
			/*Write Error not Reset Device Configuration modify by zhangwei 20170118*/
			//USB_Host_SetDeviceConfiguration(usbdev->device_address, 0);
			return USB_REGEN;
		}
		USBDEBUG("SCSI Write %dSectors [StarSector:%d]\r\n", numSec, secStart); 	
	}
	return USB_REOK;
}
#endif

uint8_t NXP_GetMaxLUN(usb_device *usbdev, uint8_t *LunIndex)
{
	USB_ClassInfo_MS_Host_t *MSInterfaceInfo;
	/*os_priv is USB_ClassInfo_MS_Host_t *MSInterfaceInfo*/
	if(!usbdev || !LunIndex){
		return USB_REPARA;
	}
	MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)(usbdev->os_priv);

	if (MS_Host_GetMaxLUN(MSInterfaceInfo, LunIndex)) {
		USB_Host_SetDeviceConfiguration(usbdev->device_address, 0);
		return USB_REGEN;
	}

	return USB_REOK;
}

uint8_t NXP_ResetMSInterface(usb_device *usbdev)
{
	USB_ClassInfo_MS_Host_t *MSInterfaceInfo;
	/*os_priv is USB_ClassInfo_MS_Host_t *MSInterfaceInfo*/
	if(!usbdev){
		return USB_REPARA;
	}
	
	MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)(usbdev->os_priv);

	if (MS_Host_ResetMSInterface(MSInterfaceInfo)) {
		USBDEBUG("Error resetting Mass Storage interface.\r\n");
		USB_Host_SetDeviceConfiguration(usbdev->device_address, 0);
		return USB_REGEN;
	}
	
	return USB_REOK;	
}

uint8_t NXP_RequestSense(usb_device *usbdev, 
				uint8_t index, SCSI_Sense_Response_t *SenseData)
{
	USB_ClassInfo_MS_Host_t *MSInterfaceInfo;
	/*os_priv is USB_ClassInfo_MS_Host_t *MSInterfaceInfo*/
	if(!usbdev || !SenseData){
		return USB_REPARA;
	}
	
	MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)(usbdev->os_priv);

	if (MS_Host_RequestSense(MSInterfaceInfo, index, 
					(SCSI_Request_Sense_Response_t*)SenseData) != 0) {
		USBDEBUG("Error retrieving device sense.\r\n");
		USB_Host_SetDeviceConfiguration(usbdev->device_address, 0);
		return USB_REGEN;
	}
	
	return USB_REOK;	
}

uint8_t NXP_GetInquiryData(usb_device *usbdev,
						uint8_t index, SCSI_Inquiry_t *InquiryData)
{
	USB_ClassInfo_MS_Host_t *MSInterfaceInfo;
	/*os_priv is USB_ClassInfo_MS_Host_t *MSInterfaceInfo*/
	if(!usbdev || !InquiryData){
		return USB_REPARA;
	}
	
	MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)(usbdev->os_priv);

	if (MS_Host_GetInquiryData(MSInterfaceInfo, index, (SCSI_Inquiry_Response_t*)InquiryData) != 0) {
		USBDEBUG("Error retrieving device Inquiry.\r\n");
		USB_Host_SetDeviceConfiguration(usbdev->device_address, 0);
		return USB_REGEN;
	}
	
	return USB_REOK;
}

uint8_t NXP_ReadDeviceCapacity(usb_device *usbdev, uint32_t *Blocks, uint32_t *BlockSize)
{
	SCSI_Capacity_t DiskCapacity;
	USB_ClassInfo_MS_Host_t *MSInterfaceInfo;
	uint8_t loopCount = 0;
	/*os_priv is USB_ClassInfo_MS_Host_t *MSInterfaceInfo*/
	if(!usbdev || !Blocks|| !BlockSize){
		return USB_REPARA;
	}
	
	MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)(usbdev->os_priv);
	for (loopCount=0; loopCount< 0x80; loopCount++) {
		uint8_t ErrorCode = MS_Host_TestUnitReady(MSInterfaceInfo, 0);
		if (!(ErrorCode)) {
			break;
		}
		/* Check if an error other than a logical command error (device busy) received */
		if (ErrorCode != MS_ERROR_LOGICAL_CMD_FAILED) {
			USBDEBUG("Failed\r\n");
			USB_Host_SetDeviceConfiguration(usbdev->device_address, 0);
			return USB_REGEN;
		}
	}
	if(loopCount == 0x80){
		USBDEBUG("MS_Host_TestUnitReady Not Ready, May Be no HDD or HDD Bad\r\n");
		return USB_REGEN;
	}
	if (MS_Host_ReadDeviceCapacity(MSInterfaceInfo, 0, &DiskCapacity)) {
		USBDEBUG("Error retrieving device capacity.\r\n");
		USB_Host_SetDeviceConfiguration(usbdev->device_address, 0);
		return USB_REGEN;
	}
	*Blocks = DiskCapacity.Blocks;
	*BlockSize = DiskCapacity.BlockSize;
	
	return USB_REOK;
}

uint8_t NXP_DiskStartStop(usb_device *usbdev,
						uint8_t index, uint8_t state)
{
#if 0
	USB_ClassInfo_MS_Host_t *MSInterfaceInfo;

	if(!usbdev){
		return USB_REPARA;
	}

	MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)(usbdev->os_priv);
	if(MS_Host_StartStopDisk(MSInterfaceInfo, index, state)){
		printf("%s Lun %d Error..\r\n", 
				((state == 1)?"Start":"Stop"), index);		
		return USB_REGEN;
	}
#endif
	printf("%s Lun %d Successful..\r\n", 
			((state == 1)?"Start":"Stop"), index);
	return USB_REOK;
}

#elif defined(LINUX)
static uint8_t LINUX_SendControlRequest(void* dev_handle, 
			uint8_t bmRequestType, uint8_t bRequest, 
			uint16_t wValue, uint16_t wIndex, uint16_t wLength, void * const data)
{
	int8_t rc;
	
	rc =  libusb_control_transfer((libusb_device_handle *)dev_handle, bmRequestType, 
					bRequest, wValue, wIndex, data, wLength, 0);
	
	return (rc < 0)?USB_REGEN:USB_REOK;
}

static uint8_t LINUX_BlukPacketSend(usb_device *usbdev, uint8_t *buffer, 
														uint32_t length, uint32_t *actual_length)
{
	int8_t rc = 0;
	int transferred = 0;
	uint32_t already = 0, sndlen;
	
	if(!usbdev || !buffer || !actual_length){
		return USB_REGEN;
	}

#if defined(NXP_CHIP_18XX)	
#define MAX_USB_PACKAGE2   	(32*1024)
#elif defined(LINUX)
#define MAX_USB_PACKAGE2   	(512*1024)
#endif
	while(already < length){	
		if(length-already > MAX_USB_PACKAGE2){
			sndlen = MAX_USB_PACKAGE2-1;
		}else{
			sndlen= length -already;
		}
		
		rc = libusb_bulk_transfer((struct libusb_device_handle *)(usbdev->os_priv),
								usbdev->ep_out,
								buffer+already,
								sndlen,
								&transferred,
								0);
		if(rc < 0){
				if (rc  == LIBUSB_ERROR_TIMEOUT){
					USBDEBUG("LIBUSB Send Timeout\n");
				}else if(rc == LIBUSB_ERROR_NO_DEVICE){
					USBDEBUG("Device OffLine....\n");
					return USB_DISCNT;
				}else{
					USBDEBUG("LIBUSB bulk transfer error %d\n", rc);
				}
				return USB_RETRANS;
		}
		if(sndlen % usbdev->wMaxPacketSize == 0){
			uint8_t buf[1]; 	
			int transfer = 0;
			USBDEBUG("LIBUSB Send ZLP.\r\n");
			rc = libusb_bulk_transfer((struct libusb_device_handle *)(usbdev->os_priv),
									usbdev->ep_out,
									buf,
									0,
									&transfer,
									0);
			if(rc < 0){
					if (rc	== LIBUSB_ERROR_TIMEOUT){
						USBDEBUG("LIBUSB Send ZLP Timeout\n");
					}else if(rc == LIBUSB_ERROR_NO_DEVICE){
						USBDEBUG("Device OffLine....\n");
					}else{
						USBDEBUG("LIBUSB bulk transfer	ZLP error %d\n", rc);
					}
					return USB_RETRANS;
			}	
		}
		already += transferred;
	}

	*actual_length = already;
	USBDEBUG("LIBUSB Send Successful:%u/%d.\r\n", *actual_length, length);

	return rc?USB_REGEN:USB_REOK;
}

static uint8_t LINUX_BlukPacketReceive(usb_device *usbdev, uint8_t *buffer, 
															uint32_t length, uint32_t *actual_length)
{
	int8_t rc;
	int transferred = 0;

	if(!usbdev || !buffer || !actual_length){
		return USB_REGEN;
	}
	rc = libusb_bulk_transfer((struct libusb_device_handle *)(usbdev->os_priv),
							usbdev->ep_in,
							buffer,
							length,
							&transferred,
							0);
	if(rc < 0){
			if (rc  == LIBUSB_ERROR_TIMEOUT){
				USBDEBUG("LIBUSB Receive Timeout\n");
			}else if(rc == LIBUSB_ERROR_NO_DEVICE){
				USBDEBUG("Device OffLine....\n");				
				return USB_DISCNT;
			}else if(rc == LIBUSB_ERROR_OVERFLOW){
				USBDEBUG("LIBUSB OverFlow[%p/%d/%dBytes]....\n", 
							buffer, length, transferred);
			}else{
				USBDEBUG("LIBUSB bulk transfer error %d\n", rc);
			}
			return USB_RETRANS;
	}
	
	*actual_length = transferred;
	USBDEBUG("LIBUSB Receive %u/%d.\r\n", *actual_length, length);

	return rc?USB_REGEN:USB_REOK;
}

static uint8_t LINUX_BlukPacketReceiveTmout(usb_device *usbdev, uint8_t *buffer, 
															uint32_t length, uint32_t *actual_length, int timeout)
{
	int8_t rc;
	int transferred = 0;

	if(!usbdev || !buffer || !actual_length){
		return USB_REGEN;
	}
	rc = libusb_bulk_transfer((struct libusb_device_handle *)(usbdev->os_priv),
							usbdev->ep_in,
							buffer,
							length,
							&transferred,
							timeout);
	if(rc < 0){
			if (rc  == LIBUSB_ERROR_TIMEOUT){
				USBDEBUG("LIBUSB Receive Timeout\n");
				return USB_TMOUT;
			}else if(rc == LIBUSB_ERROR_NO_DEVICE){
				USBDEBUG("Device OffLine....\n");				
				return USB_DISCNT;
			}else if(rc == LIBUSB_ERROR_OVERFLOW){
				USBDEBUG("LIBUSB OverFlow[%p/%d/%dBytes]....\n", 
							buffer, length, transferred);
			}else{
				USBDEBUG("LIBUSB bulk transfer error %d\n", rc);
			}
			return USB_RETRANS;
	}
	
	*actual_length = transferred;
	USBDEBUG("LIBUSB Receive %u/%d.\r\n", *actual_length, length);

	return rc?USB_REGEN:USB_REOK;
}


static uint8_t LINUX_GetDeviceDescriptor(usb_device *usbdev, USB_StdDesDevice_t *DeviceDescriptorData)
{
	libusb_device_handle *dev_handle;

	if(!DeviceDescriptorData || !usbdev){
		return USB_REPARA;
	}
	dev_handle = (libusb_device_handle*)(usbdev->os_priv);
	if(libusb_get_device_descriptor(libusb_get_device(dev_handle), 
					(struct libusb_device_descriptor*)DeviceDescriptorData)){
		USBDEBUG("Error Getting Device Descriptor.\r\n");
		return USB_REGEN;
	}
	return USB_REOK;
}

static uint8_t LINUX_SetDeviceConfigDescriptor(usb_device *usbdev, uint8_t cfgindex)
{	
	if(!usbdev){
		return USB_REPARA;
	}
	if (libusb_set_configuration((struct libusb_device_handle *)(usbdev->os_priv), cfgindex) != 0) {
		USBDEBUG("Error Setting Device Configuration.\r\n");
		return USB_REGEN;
	}
	
	return USB_REOK;
}

static uint8_t LINUX_Init(usb_device *usbdev, void *os_priv)
{
	static uint8_t usbinit = 0;
	int res;

	if(usbinit == 1){
		return USB_REOK;
	}
	res = libusb_init(NULL);
	//libusb_set_debug(NULL, 3);
	if(res != 0) {
		USBDEBUG("libusb_init failed: %d", res);
		return USB_REGEN;
	}
	return USB_REOK;
}

uint8_t LINUX_DiskReadSectors(usb_device *usbdev, 
				void *buff, uint32_t secStart, uint32_t numSec, uint16_t BlockSize)
{
	int fd;
	char *devname = (char*)usbdev->os_priv;
	off_t offset;
	int already = 0, res, total = 0;
	
	fd = open(devname, O_RDONLY);
	if(fd < 0){
		USBDEBUG("Open %s Error:%s\n", devname, strerror(errno));
		return USB_REGEN;
	}	
	offset = (off_t)secStart *BlockSize;	
	if(lseek(fd, offset, SEEK_SET) < 0){
		USBDEBUG("Lseek %s Error:%s\n", devname, strerror(errno));
		close(fd);
		return USB_REGEN;
	}
	
	total = numSec*BlockSize;
	already = 0;
	do {
		res  = read(fd, buff + already, total - already);
		if (res < 0) {
			if(errno ==  EINTR ||
					errno ==  EAGAIN){
				continue;
			}
			USBDEBUG("Read %s Error:%s\r\n", devname, strerror(errno));
			close(fd);
			return USB_REGEN;		
		}else if(res == 0){
			USBDEBUG("Read End OF File: %s Error:%s[already=%d total=%d]\r\n", 
					devname, strerror(errno), already, total);
			close(fd);
			return USB_REGEN;		
		}
		already += res;
	} while (already < total);
	close(fd);
	
	return USB_REOK;
}

uint8_t LINUX_DiskWriteSectors(usb_device *usbdev, 
				void *buff, uint32_t secStart, uint32_t numSec, uint16_t BlockSize)
{
	int fd;
	char *devname = (char*)usbdev->os_priv;
	off_t offset;
	int already = 0, res, total = 0;
	
	fd = open(devname, O_RDWR);
	if(fd < 0){
		USBDEBUG("Open %s Error:%s\n", devname, strerror(errno));
		return USB_REGEN;
	}
	offset = (off_t)secStart *BlockSize;	
	if(lseek(fd, offset, SEEK_SET) < 0){
		USBDEBUG("Lseek %s Error:%s\n", devname, strerror(errno));
		close(fd);
		return USB_REGEN;
	}

	total = numSec*BlockSize;
	already = 0;
	do {
		res  = write(fd, buff + already, total - already);
		if (res < 0) {
			if(errno ==  EINTR ||
					errno ==  EAGAIN){
				continue;
			}
			USBDEBUG("Write %s Error:%s\r\n", devname, strerror(errno));
			close(fd);
			return USB_REGEN;		
		}
		already += res;
	} while (already < total);
	close(fd);
	
	return USB_REOK;
}
#elif defined(GP_CHIP)
/*Special USB Command*/
//#define GP_USB_TIMEOUT		2000
#define GP_USB_TIMEOUT		0
#ifndef GP_SD_IDX
#define GP_SD_IDX		1
#endif
static uint8_t GP_SendControlRequest(const uint8_t corenum, 
			uint8_t bmRequestType, uint8_t bRequest, 
			uint16_t wValue, uint16_t wIndex, uint16_t wLength, void * const data)
{
	USB_ControlRequest = (USB_Request_Header_t)
		{
		.bmRequestType = (bmRequestType),
		.bRequest      = bRequest,
		.wValue        = wValue,
		.wIndex        = wIndex,
		.wLength       = wLength,
		};

	return LIBUSB_Host_SendControlRequest(corenum,data);
}

static uint8_t GP_BlukPacketReceive(usb_device *usbdev, uint8_t *buffer, 
			uint32_t length, uint32_t *actual_length)
{
	uint8_t  ErrorCode = PIPE_RWSTREAM_NoError;
	
	const USB_ClassInfo_MS_Host_t *MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)usbdev->os_priv;

	if(!usbdev || !buffer || !actual_length){
		return USB_REPARA;
	}
	Pipe_SelectPipe(MSInterfaceInfo->Config.PortNumber, 
			MSInterfaceInfo->Config.DataINPipeNumber);

	ErrorCode = Pipe_Streaming(MSInterfaceInfo->Config.PortNumber, buffer, 
					length, actual_length, GP_USB_TIMEOUT);
	if (ErrorCode) {
		USBDEBUG("USB Receive Error[%d]\r\n", ErrorCode);
		return ErrorCode;
	}
	USBDEBUG("USB Receive [%d/%dBytes]\r\n", *actual_length, length);

	return USB_REOK;
}	

static uint8_t GP_BlukPacketReceiveStream(usb_device *usbdev, uint8_t *buffer, 
				uint32_t length, uint32_t *actual_length)
{
	uint8_t  ErrorCode = PIPE_RWSTREAM_NoError;

	ErrorCode = GP_BlukPacketReceive(usbdev, buffer, length, actual_length);
	if(ErrorCode){
		USBDEBUG("USB Stream Receive Error[%d]\r\n", ErrorCode);
		return ErrorCode;
	}
	USBDEBUG("USB Stream Receive [%d/%dBytes]\r\n", *actual_length, length);

	return USB_REOK;
}

static uint8_t GP_BlukPacketSend(usb_device *usbdev, uint8_t *buffer, 
                                  uint32_t length, uint32_t *actual_length)
{
	uint8_t  ErrorCode = PIPE_RWSTREAM_NoError;
	const USB_ClassInfo_MS_Host_t *MSInterfaceInfo;
	
	if(!usbdev || !buffer || !actual_length){
		return USB_REPARA;
	}
	
	MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)usbdev->os_priv;
	Pipe_SelectPipe(MSInterfaceInfo->Config.PortNumber, 
			MSInterfaceInfo->Config.DataOUTPipeNumber);
	
	USBDEBUG("Prepare USB Send ===================>[%dBytes]\r\n", length);
	ErrorCode = Pipe_Streaming(MSInterfaceInfo->Config.PortNumber, buffer, 
					length, actual_length, GP_USB_TIMEOUT);

	if (ErrorCode) {
		USBDEBUG("USB Send Error[%d]\r\n", ErrorCode);
		return ErrorCode;
	}
	USBDEBUG("USB Send [%d/%dBytes]\r\n", *actual_length, length);
	
	return USB_REOK;
}

static uint8_t GP_GetDeviceDescriptor(usb_device *usbdev, USB_StdDesDevice_t *DeviceDescriptorData)
{	
	if(!DeviceDescriptorData || !usbdev){
		return USB_REPARA;
	}	
	printf("GP_GetDeviceDescriptor--->portNum=%d\r\n", usbdev->device_address);
	if(LIBUSB_Host_GetDeviceDescriptor(usbdev->device_address, (USB_Descriptor_Device_t*)DeviceDescriptorData)){
		USBDEBUG("Error Getting Device Descriptor.\r\n");
		return USB_REGEN;
	}
	return USB_REOK;
}

static uint8_t GP_GetDeviceConfigDescriptor(usb_device *usbdev, uint8_t index, uint16_t *cfgsize,
					uint8_t *ConfigDescriptorData, uint16_t ConfigDescriptorDataLen)
{	
	if(!ConfigDescriptorData || !usbdev || !cfgsize){
		return USB_REPARA;
	}
	if (LIBUSB_Host_GetDeviceConfigDescriptor(usbdev->device_address, index, cfgsize, 
				ConfigDescriptorData, ConfigDescriptorDataLen) != HOST_GETCONFIG_Successful) {
		USBDEBUG("Error Retrieving Configuration Descriptor.\r\n");
		return USB_REGEN;
	}

	return USB_REOK;
}

static uint8_t GP_SetDeviceConfigDescriptor(usb_device *usbdev, uint8_t cfgindex)
{	
	if(!usbdev){
		return USB_REPARA;
	}
	if (LIBUSB_Host_SetDeviceConfiguration(usbdev->device_address, cfgindex) != 
				HOST_SENDCONTROL_Successful) {
		USBDEBUG("Error Setting Device Configuration.\r\n");
		return USB_REGEN;
	}
	
	return USB_REOK;
}

static uint8_t GP_ClaimInterface(usb_device *usbdev, nxp_clminface*cPrivate)
{
	USB_Descriptor_Endpoint_t*  DataINEndpoint       = NULL;
	USB_Descriptor_Endpoint_t*  DataOUTEndpoint      = NULL;
	USB_Descriptor_Interface_t* MassStorageInterface = NULL;
	uint8_t portnum = 0, curConfigurations = 1;
	uint16_t ConfigDescriptorSize = 0;
	uint8_t  ConfigDescriptorData[512], *PtrConfigDescriptorData = NULL;
	

	if(!cPrivate){
		return USB_REPARA;
	}
	USB_ClassInfo_MS_Host_t *MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)(usbdev->os_priv);
	if( !MSInterfaceInfo||
			!cPrivate->callbackEndpoint || !cPrivate->callbackInterface){
		return USB_REPARA;
	}

	for(curConfigurations= 1; curConfigurations <= cPrivate->bNumConfigurations; curConfigurations++){
		if (LIBUSB_Host_GetDeviceConfigDescriptor(MSInterfaceInfo->Config.PortNumber, 
								curConfigurations, &ConfigDescriptorSize, ConfigDescriptorData,
								sizeof(ConfigDescriptorData)) != HOST_GETCONFIG_Successful) {
			USBDEBUG("Error Retrieving Configuration Descriptor.\r\n");
			continue;
		}
		if (DESCRIPTOR_TYPE(ConfigDescriptorData) != DTYPE_Configuration){
			continue;
		}		
		usUsb_Print(ConfigDescriptorData, ConfigDescriptorSize); 
		/*Set array name to point var, USB_GetNextDescriptorComp will change point*/
		PtrConfigDescriptorData = ConfigDescriptorData;		
		while (!(DataINEndpoint) || !(DataOUTEndpoint)){
			if (!(MassStorageInterface) ||
					USB_GetNextDescriptorComp(&ConfigDescriptorSize, (void **)&PtrConfigDescriptorData,
							cPrivate->callbackEndpoint) != DESCRIPTOR_SEARCH_COMP_Found){
				if (USB_GetNextDescriptorComp(&ConfigDescriptorSize, (void **)&PtrConfigDescriptorData,
							cPrivate->callbackInterface) != DESCRIPTOR_SEARCH_COMP_Found){
					DataINEndpoint	= NULL;
					DataOUTEndpoint = NULL;
					MassStorageInterface = NULL;					
					USBDEBUG("No Found Vaild Interface At Configuration[%d].\r\n", curConfigurations);
					break;
				}
				MassStorageInterface = DESCRIPTOR_PCAST(PtrConfigDescriptorData, USB_Descriptor_Interface_t);

				DataINEndpoint  = NULL;
				DataOUTEndpoint = NULL;

				continue;
			}

			USB_Descriptor_Endpoint_t* EndpointData = DESCRIPTOR_PCAST(PtrConfigDescriptorData, USB_Descriptor_Endpoint_t);

			if ((EndpointData->EndpointAddress & ENDPOINT_DIR_MASK) == ENDPOINT_DIR_IN){
				DataINEndpoint  = EndpointData;				
				USBDEBUG("Found Vaild INInterface At Configuration[%d].\r\n", curConfigurations);
			}else{
				DataOUTEndpoint = EndpointData;				
				USBDEBUG("Found Vaild OUTInterface At Configuration[%d].\r\n", curConfigurations);
			}			
		}
		if(DataINEndpoint && DataOUTEndpoint){
			break;
		}
	}
	if(!(DataINEndpoint) || !(DataOUTEndpoint) ||
			(curConfigurations > cPrivate->bNumConfigurations)){
		USBDEBUG("No Found Suitable Interface.\r\n");
		return USB_REGEN;
	}
	memset(&MSInterfaceInfo->State, 0x00, sizeof(MSInterfaceInfo->State));	
	portnum = MSInterfaceInfo->Config.PortNumber;
	for (uint8_t PipeNum = 1; PipeNum < PIPE_TOTAL_PIPES; PipeNum++){
		uint16_t Size;
		uint8_t  Type;
		uint8_t  Token;
		uint8_t  EndpointAddress;
		bool     DoubleBanked;

		if (PipeNum == MSInterfaceInfo->Config.DataINPipeNumber){
			Size            = le16_to_cpu(DataINEndpoint->EndpointSize);
			EndpointAddress = DataINEndpoint->EndpointAddress;
			Token           = PIPE_TOKEN_IN;
			Type            = EP_TYPE_BULK;
			DoubleBanked    = MSInterfaceInfo->Config.DataINPipeDoubleBank;

			MSInterfaceInfo->State.DataINPipeSize = DataINEndpoint->EndpointSize;
			
		}else if (PipeNum == MSInterfaceInfo->Config.DataOUTPipeNumber){
			Size            = le16_to_cpu(DataOUTEndpoint->EndpointSize);
			EndpointAddress = DataOUTEndpoint->EndpointAddress;
			Token           = PIPE_TOKEN_OUT;
			Type            = EP_TYPE_BULK;
			DoubleBanked    = MSInterfaceInfo->Config.DataOUTPipeDoubleBank;
			
			MSInterfaceInfo->State.DataOUTPipeSize = DataOUTEndpoint->EndpointSize;
		}else{
			continue;
		}

		if (!(Pipe_ConfigurePipe(portnum,PipeNum, Type, Token, EndpointAddress, Size,
		                         DoubleBanked ? PIPE_BANK_DOUBLE : PIPE_BANK_SINGLE))){
			return USB_REGEN;
		}
	}

	/*Set Configuration*/
	if(GP_SetDeviceConfigDescriptor(usbdev, curConfigurations)){
		USBDEBUG("Error Setting Device Configuration.\r\n");
		return USB_REGEN;
	}

	MSInterfaceInfo->State.InterfaceNumber = MassStorageInterface->InterfaceNumber;
	MSInterfaceInfo->State.IsActive = true;
	/*Set the usbdev struct*/
	usbdev->device_address= MSInterfaceInfo->Config.PortNumber;
	usbdev->wMaxPacketSize = MSInterfaceInfo->State.DataOUTPipeSize;
	usbdev->ep_in = MSInterfaceInfo->Config.DataINPipeNumber;
	usbdev->ep_out = MSInterfaceInfo->Config.DataOUTPipeNumber;
	return USB_REOK;
}

static uint8_t GP_Init(usb_device *usbdev, void *os_priv)
{
	USB_ClassInfo_MS_Host_t *MSInterfaceInfo;
	/*os_priv is USB_ClassInfo_MS_Host_t *MSInterfaceInfo*/
	if(!usbdev || !os_priv){
		return USB_REPARA;
	}
	usbdev->os_priv = os_priv;

	if(usbdev->usb_type != USB_CARD){
		MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)(os_priv);
		/*Just set device_address == PortNumber*/
		usbdev->device_address  = MSInterfaceInfo->Config.PortNumber;
	}

	return USB_REOK;
}

#ifdef NXP_DISK_LIMITSIZE /*limit read write disk size*/
uint8_t GP_DiskReadSectors(usb_device *usbdev, 
				void *buff, uint32_t secStart, uint32_t numSec, uint16_t BlockSize)
{
	int ret;
	uint8_t *curBuf = NULL;
	uint32_t already = 0, curSec;
	uint8_t numSecPer;

	curBuf = (uint8_t*)buff;
	curSec = secStart;
	while(already < numSec){		
		if(numSec-already > MAX_SCSI_SECTOR){
			numSecPer = MAX_SCSI_SECTOR;
		}else{
			numSecPer = numSec-already;
		}
		if(usbdev->usb_type == USB_CARD){
			USBDEBUG("GP SDCard Read\r\n");			
			D_cache_invalid_range((unsigned int)buff, numSec*512);
			ret = drvl2_sd_read(GP_SD_IDX, secStart, (INT32U *)buff, numSec);
			if(ret){
				printf("GP Read SD Card Error. ret=%d\r\n", ret);
				return USB_REGEN;
			}
		}else{	
			ret = MassStorage_ReadDeviceBlocks((USB_ClassInfo_MS_Host_t*)usbdev->os_priv, 0, 
								curSec, numSecPer, BlockSize, (void *)(curBuf+already*BlockSize));
			if(ret) {
				printf("Error reading device block. ret = %d\r\n", ret);				
				/*Read Error not Reset Device Configuration modify by zhangwei 20170118*/
				//USB_Host_SetDeviceConfiguration(usbdev->device_address, 0);
				return USB_REGEN;
			}
		}
		already += numSecPer;
		curSec += numSecPer;
		USBDEBUG("SCSI Read %dSectors [%d/%d/%d]\r\n", numSecPer, curSec, already, numSec);		
	}

	return USB_REOK;
}

uint8_t GP_DiskWriteSectors(usb_device *usbdev, 
					void *buff, uint32_t secStart, uint32_t numSec, uint16_t BlockSize)
{
	int ret;
	uint8_t *curBuf = NULL;
	uint32_t already = 0, curSec;
	uint8_t numSecPer;

	curBuf = (uint8_t*)buff;
	curSec = secStart;
	while(already < numSec){		
		if(numSec-already > MAX_SCSI_SECTOR){
			numSecPer = MAX_SCSI_SECTOR;
		}else{
			numSecPer = numSec-already;
		}
		if(usbdev->usb_type == USB_CARD){
			USBDEBUG("GP SDCard Write\r\n");
			ret = drvl2_sd_write(GP_SD_IDX, secStart, (INT32U *)buff, numSec);
			if(ret){
				printf("GP Write SD Card Error. ret=%d\r\n", ret);
				return USB_REGEN;
			}
		}else{
			ret = MassStorage_WriteDeviceBlocks((USB_ClassInfo_MS_Host_t*)usbdev->os_priv, 0, 
								curSec, numSecPer, BlockSize, (void *)(curBuf+already*BlockSize));
			if(ret) {
				printf("Error writing device block. ret = %d\r\n", ret);				
				/*Write Error not Reset Device Configuration modify by zhangwei 20170118*/
				//USB_Host_SetDeviceConfiguration(usbdev->device_address, 0);
				return USB_REGEN;
			}
		}
		already += numSecPer;
		curSec += numSecPer;
		USBDEBUG("SCSI Write %dSectors [%d/%d/%d]\r\n", numSecPer, curSec, already, numSec);		
	}

	return USB_REOK;
}

#else
uint8_t GP_DiskReadSectors(usb_device *usbdev, 
				void *buff, uint32_t secStart, uint32_t numSec, uint16_t BlockSize)
{
	int ret;

	if(!usbdev || !buff){
		printf("Parameter Error\r\n");
		return USB_REPARA;
	}
	if(usbdev->usb_type == USB_CARD){
		USBDEBUG("GP SDCard Read\r\n");		
		D_cache_invalid_range((unsigned int)buff, numSec*512);
		ret = drvl2_sd_read(GP_SD_IDX, secStart, (INT32U *)buff, numSec);
		if(ret){
			printf("GP Read SD Card Error. ret=%d\r\n", ret);
			return USB_REGEN;
		}
		USBDEBUG("SD Read %dSectors [StarSector:%d]\r\n", numSec, secStart); 	
		return USB_REOK;
	}else{
		/*USB Storage Read*/
		ret = MassStorage_ReadDeviceBlocks((USB_ClassInfo_MS_Host_t*)usbdev->os_priv, 0, 
				secStart, numSec, BlockSize, buff);
		if(ret) {
			printf("Error reading device block. ret = %d\r\n", ret);
			/*Read Error not Reset Device Configuration modify by zhangwei 20170118*/
			//USB_Host_SetDeviceConfiguration(usbdev->device_address, 0);
			return USB_REGEN;
		}	
		USBDEBUG("SCSI Read %dSectors [StarSector:%d]\r\n", numSec, secStart); 	
	}
	return USB_REOK;
}

uint8_t GP_DiskWriteSectors(usb_device *usbdev, 
					void *buff, uint32_t secStart, uint32_t numSec, uint16_t BlockSize)
{
	int ret;

	if(!usbdev || !buff){
		printf("Parameter Error\r\n");
		return USB_REPARA;
	}
	if(usbdev->usb_type == USB_CARD){
		USBDEBUG("GP SDCard Write\r\n");
		ret = drvl2_sd_write(GP_SD_IDX, secStart, (INT32U *)buff, numSec);
		if(ret){
			printf("GP Write SD Card Error. ret=%d\r\n", ret);
			return USB_REGEN;
		}
		USBDEBUG("SD Write %dSectors [StarSector:%d]\r\n", numSec, secStart);		
		return USB_REOK;
	}else{
		/*USB Storage Write*/
		ret = MassStorage_WriteDeviceBlocks((USB_ClassInfo_MS_Host_t*)usbdev->os_priv, 0, 
						secStart, numSec, BlockSize, buff);
		if(ret) {
			printf("Error writing device block. ret = %d\r\n", ret);			
			/*Write Error not Reset Device Configuration modify by zhangwei 20170118*/
			//USB_Host_SetDeviceConfiguration(usbdev->device_address, 0);
			return USB_REGEN;
		}
		USBDEBUG("SCSI Write %dSectors [StarSector:%d]\r\n", numSec, secStart); 	
	}
	return USB_REOK;
}
#endif

uint8_t GP_GetMaxLUN(usb_device *usbdev, uint8_t *LunIndex)
{
	USB_ClassInfo_MS_Host_t *MSInterfaceInfo;
	/*os_priv is USB_ClassInfo_MS_Host_t *MSInterfaceInfo*/
	if(!usbdev || !LunIndex){
		return USB_REPARA;
	}
	MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)(usbdev->os_priv);

	if (MassStorage_GetMaxLUN(MSInterfaceInfo, LunIndex)) {
		LIBUSB_Host_SetDeviceConfiguration(usbdev->device_address, 0);
		return USB_REGEN;
	}

	return USB_REOK;
}

uint8_t GP_ResetMSInterface(usb_device *usbdev)
{
	USB_ClassInfo_MS_Host_t *MSInterfaceInfo;
	/*os_priv is USB_ClassInfo_MS_Host_t *MSInterfaceInfo*/
	if(!usbdev){
		return USB_REPARA;
	}
	
	MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)(usbdev->os_priv);

	if (MassStorage_Host_ResetMSInterface(MSInterfaceInfo)) {
		USBDEBUG("Error resetting Mass Storage interface.\r\n");
		LIBUSB_Host_SetDeviceConfiguration(usbdev->device_address, 0);
		return USB_REGEN;
	}
	
	return USB_REOK;	
}

uint8_t GP_RequestSense(usb_device *usbdev, 
				uint8_t index, SCSI_Sense_Response_t *SenseData)
{
	USB_ClassInfo_MS_Host_t *MSInterfaceInfo;
	/*os_priv is USB_ClassInfo_MS_Host_t *MSInterfaceInfo*/
	if(!usbdev || !SenseData){
		return USB_REPARA;
	}
	
	MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)(usbdev->os_priv);

	if (MassStorage_RequestSense(MSInterfaceInfo, index, 
					(SCSI_Request_Sense_Response_t*)SenseData) != 0) {
		USBDEBUG("Error retrieving device sense.\r\n");
		LIBUSB_Host_SetDeviceConfiguration(usbdev->device_address, 0);
		return USB_REGEN;
	}
	
	return USB_REOK;	
}

uint8_t GP_GetInquiryData(usb_device *usbdev,
						uint8_t index, SCSI_Inquiry_t *InquiryData)
{
	USB_ClassInfo_MS_Host_t *MSInterfaceInfo;
	/*os_priv is USB_ClassInfo_MS_Host_t *MSInterfaceInfo*/
	if(!usbdev || !InquiryData){
		return USB_REPARA;
	}
	
	MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)(usbdev->os_priv);

	if (MassStorage_GetInquiryData(MSInterfaceInfo, index, (SCSI_Inquiry_Response_t*)InquiryData) != 0) {
		USBDEBUG("Error retrieving device Inquiry.\r\n");
		LIBUSB_Host_SetDeviceConfiguration(usbdev->device_address, 0);
		return USB_REGEN;
	}
	
	return USB_REOK;
}

uint8_t GP_ReadDeviceCapacity(usb_device *usbdev, uint32_t *Blocks, uint32_t *BlockSize)
{
	SCSI_Capacity_t DiskCapacity;
	USB_ClassInfo_MS_Host_t *MSInterfaceInfo;
	uint8_t loopCount = 0;
	/*os_priv is USB_ClassInfo_MS_Host_t *MSInterfaceInfo*/
	if(!usbdev || !Blocks|| !BlockSize){
		return USB_REPARA;
	}
	
	MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)(usbdev->os_priv);
	for (loopCount=0; loopCount< 0x80; loopCount++) {
		uint8_t ErrorCode = MassStorage_TestUnitReady(MSInterfaceInfo, 0);
		if (!(ErrorCode)) {
			break;
		}
		/* Check if an error other than a logical command error (device busy) received */
		if (ErrorCode != MS_ERROR_LOGICAL_CMD_FAILED) {
			USBDEBUG("Failed\r\n");
			LIBUSB_Host_SetDeviceConfiguration(usbdev->device_address, 0);
			return USB_REGEN;
		}
	}
	if(loopCount == 0x80){
		USBDEBUG("MS_Host_TestUnitReady Not Ready, May Be no HDD or HDD Bad\r\n");
		return USB_REGEN;
	}
	if (MassStorage_ReadDeviceCapacity(MSInterfaceInfo, 0, &DiskCapacity)) {
		USBDEBUG("Error retrieving device capacity.\r\n");
		LIBUSB_Host_SetDeviceConfiguration(usbdev->device_address, 0);
		return USB_REGEN;
	}
	*Blocks = DiskCapacity.Blocks;
	*BlockSize = DiskCapacity.BlockSize;
	
	return USB_REOK;
}

uint8_t GP_DiskStartStop(usb_device *usbdev,
						uint8_t index, uint8_t state)
{
	printf("%s Lun %d Successful..\r\n", 
			((state == 1)?"Start":"Stop"), index);
	return USB_REOK;
}

#endif

/*****************************************************************************
 * Public functions
 ****************************************************************************/

/************************PUBLIC USB INTERFACE***************************/
/*Special USB Command*/
/*
*cPrivate is about chip vendor, is specially
*return Value:
*0: Successful
*>0: Failed
*/
uint8_t usUsb_SendControlRequest(usb_device *usbdev, 
			uint8_t bmRequestType, uint8_t bRequest, 
			uint16_t wValue, uint16_t wIndex, uint16_t wLength,  void * data)
{
#if defined(NXP_CHIP_18XX)
	if(!usbdev){
		return USB_REPARA;
	}
	return NXP_SendControlRequest(usbdev->device_address, bmRequestType,
				bRequest, wValue, wIndex, wLength, data);
#elif defined(LINUX)
	if(!usbdev){
		return USB_REPARA;
	}
	return LINUX_SendControlRequest(usbdev->os_priv, bmRequestType,
				bRequest, wValue, wIndex, wLength, data);
#elif defined(GP_CHIP)
	if(!usbdev){
		return USB_REPARA;
	}
	return GP_SendControlRequest(usbdev->device_address, bmRequestType,
				bRequest, wValue, wIndex, wLength, data);
#endif
}

uint8_t usUsb_BlukPacketSend(usb_device *usbdev, uint8_t *buffer, 
												const uint32_t length, uint32_t *actual_length)
{
#if defined(NXP_CHIP_18XX)
	return NXP_BlukPacketSend(usbdev, buffer, length, actual_length);
#elif defined(LINUX)
	return LINUX_BlukPacketSend(usbdev, buffer, length, actual_length);
#elif defined(GP_CHIP)
	return GP_BlukPacketSend(usbdev, buffer, length, actual_length);
#endif
}

uint8_t usUsb_BlukPacketReceive(usb_device *usbdev, uint8_t *buffer, 
															uint32_t length, uint32_t *actual_length)
{
#if defined(NXP_CHIP_18XX)
	return NXP_BlukPacketReceive(usbdev, buffer, length, actual_length);
#elif defined(LINUX)
	return LINUX_BlukPacketReceiveTmout(usbdev, buffer, length, actual_length, 2000);
#elif defined(GP_CHIP)
	return GP_BlukPacketReceive(usbdev, buffer, length, actual_length);
#endif

}

uint8_t usUsb_BlukPacketReceiveTmout(usb_device *usbdev, uint8_t *buffer, 
															uint32_t length, uint32_t *actual_length, int timeout)
{
#if defined(NXP_CHIP_18XX)
	return NXP_BlukPacketReceive(usbdev, buffer, length, actual_length);
#elif defined(LINUX)
	return LINUX_BlukPacketReceiveTmout(usbdev, buffer, length, actual_length, timeout);
#elif defined(GP_CHIP)
	return GP_BlukPacketReceive(usbdev, buffer, length, actual_length);
#endif

}

uint8_t usUsb_BlukPacketReceiveStream(usb_device *usbdev, uint8_t *buffer, 
															uint32_t length, uint32_t *actual_length)
{
#if defined(NXP_CHIP_18XX)
	return NXP_BlukPacketReceiveStream(usbdev, buffer, length, actual_length);
#elif defined(LINUX)
	return LINUX_BlukPacketReceiveTmout(usbdev, buffer, length, actual_length, 2000);
#elif defined(GP_CHIP)
	return GP_BlukPacketReceiveStream(usbdev, buffer, length, actual_length);
#endif

}

uint8_t usUsb_GetDeviceDescriptor(usb_device *usbdev, USB_StdDesDevice_t *DeviceDescriptorData)
{
#if defined(NXP_CHIP_18XX)
	return NXP_GetDeviceDescriptor(usbdev, DeviceDescriptorData);
#elif defined(LINUX)
	return LINUX_GetDeviceDescriptor(usbdev, DeviceDescriptorData);
#elif defined(GP_CHIP)
	return GP_GetDeviceDescriptor(usbdev, DeviceDescriptorData);
#endif
}

uint8_t usUsb_GetDeviceConfigDescriptor(usb_device *usbdev, uint8_t index, uint16_t *cfgsize,
					uint8_t *ConfigDescriptorData, uint16_t ConfigDescriptorDataLen)
{
#if defined(NXP_CHIP_18XX)
	return NXP_GetDeviceConfigDescriptor(usbdev, index, cfgsize, 
						ConfigDescriptorData, ConfigDescriptorDataLen);
#elif defined(LINUX)
	return USB_REOK;
#elif defined(GP_CHIP)
	return GP_GetDeviceConfigDescriptor(usbdev, index, cfgsize, 
					ConfigDescriptorData, ConfigDescriptorDataLen);
#endif
}

uint8_t usUsb_SetDeviceConfigDescriptor(usb_device *usbdev, uint8_t cfgindex)
{
#if defined(NXP_CHIP_18XX)
	return NXP_SetDeviceConfigDescriptor(usbdev, cfgindex);
#elif defined(LINUX)
	return LINUX_SetDeviceConfigDescriptor(usbdev, cfgindex);
#elif defined(GP_CHIP)
	return GP_SetDeviceConfigDescriptor(usbdev, cfgindex);
#endif
}

uint8_t usUsb_ClaimInterface(usb_device *usbdev, void *cPrivate)
{
#if defined(NXP_CHIP_18XX)
	return NXP_ClaimInterface(usbdev, (nxp_clminface*)cPrivate);
#elif defined(LINUX)
	return USB_REOK;
#elif defined(GP_CHIP)
	return GP_ClaimInterface(usbdev, (nxp_clminface*)cPrivate);
#endif
}

uint8_t usUsb_GetMaxLUN(usb_device *usbdev, uint8_t *LunIndex)
{
#if defined(NXP_CHIP_18XX)
	return NXP_GetMaxLUN(usbdev, LunIndex);
#elif defined(LINUX)
	return USB_REOK;
#elif defined(GP_CHIP)
	return GP_GetMaxLUN(usbdev, LunIndex);
#endif
}

uint8_t usUsb_ResetMSInterface(usb_device *usbdev)
{
#if defined(NXP_CHIP_18XX)
	return NXP_ResetMSInterface(usbdev);
#elif defined(LINUX)
	return USB_REOK;
#elif defined(GP_CHIP)
	return GP_ResetMSInterface(usbdev);
#endif
}

uint8_t usUsb_RequestSense(usb_device *usbdev,
						uint8_t index, SCSI_Sense_Response_t *SenseData)
{
#if defined(NXP_CHIP_18XX)
	return NXP_RequestSense(usbdev, index, SenseData);
#elif defined(LINUX)
	return USB_REOK;
#elif defined(GP_CHIP)
	return GP_RequestSense(usbdev, index, SenseData);
#endif
}

uint8_t usUsb_GetInquiryData(usb_device *usbdev,
						uint8_t index, SCSI_Inquiry_t *InquiryData)
{
#if defined(NXP_CHIP_18XX)
	return NXP_GetInquiryData(usbdev, index, InquiryData);
#elif defined(LINUX)
	return USB_REOK;
#elif defined(GP_CHIP)
	return GP_GetInquiryData(usbdev, index, InquiryData);
#endif
}

uint8_t usUsb_ReadDeviceCapacity(usb_device *usbdev, uint32_t *Blocks, uint32_t *BlockSize)
{
#if defined(NXP_CHIP_18XX)
	return NXP_ReadDeviceCapacity(usbdev, Blocks, BlockSize);
#elif defined(LINUX)
	*BlockSize = DEF_SECTOR;
	return USB_REOK;	
#elif defined(GP_CHIP)
	return GP_ReadDeviceCapacity(usbdev, Blocks, BlockSize);
#endif
}

uint8_t usUsb_Init(usb_device *usbdev, void *os_priv)
{
#if defined(NXP_CHIP_18XX)
	return NXP_Init(usbdev, os_priv);
#elif defined(LINUX)
	return LINUX_Init(usbdev, os_priv);
#elif defined(GP_CHIP)
	return GP_Init(usbdev, os_priv);
#endif
}

uint8_t usUsb_DiskReadSectors(usb_device *usbdev, 
				void *buff, uint32_t secStart, uint32_t numSec, uint16_t BlockSize)
{
#if defined(NXP_CHIP_18XX)
	return NXP_DiskReadSectors(usbdev, buff, secStart, numSec, BlockSize);
#elif defined(LINUX)
	return LINUX_DiskReadSectors(usbdev, buff, secStart, numSec, BlockSize);
#elif defined(GP_CHIP)
	return GP_DiskReadSectors(usbdev, buff, secStart, numSec, BlockSize);
#endif

}

uint8_t usUsb_DiskWriteSectors(usb_device *usbdev, 
				void *buff, uint32_t secStart, uint32_t numSec, uint16_t BlockSize)
{
#if defined(NXP_CHIP_18XX)
	return NXP_DiskWriteSectors(usbdev, buff, secStart, numSec, BlockSize);
#elif defined(LINUX)
	return LINUX_DiskWriteSectors(usbdev, buff, secStart, numSec, BlockSize);
#elif defined(GP_CHIP)
	return GP_DiskWriteSectors(usbdev, buff, secStart, numSec, BlockSize);
#endif
}

/*
* state = 1: start disk
* state = 0: stop disk
*/
uint8_t usUsb_DiskStartStop(usb_device *usbdev,
						uint8_t index, uint8_t state)
{
#if defined(NXP_CHIP_18XX)
	return NXP_DiskStartStop(usbdev, index, state);
#elif defined(LINUX)
	return USB_REOK;
#elif defined(GP_CHIP)
	return GP_DiskStartStop(usbdev, index, state);
#endif
}

void usUsb_Print(uint8_t *buffer, int length)
{
	int cur = 0;
	
	if(!buffer){
		return;
	}

	for(; cur< length; cur++){
		if(cur % 16 == 0){
			printf("\r\n");
		}
		printf("0x%02x ", buffer[cur]);

	}
	
	printf("\r\n");
}
void usUsb_PrintStr(uint8_t *buffer, int length)
{
	int cur = 0;
	
	if(!buffer){
		return;
	}

	for(; cur< length; cur++){
		printf("%c", buffer[cur]);
	}
	
	printf("\r\n");

}


