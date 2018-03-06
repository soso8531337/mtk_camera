/*
 * @brief U-Storage Project
 *
 * @note
 * Copyright(C) i4season, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 *
 * @par
 * Software that is described herein is for illustrative purposes only
 * which provides customers with programming information regarding the
 * LPC products.  This software is supplied "AS IS" without any warranties of
 * any kind, and NXP Semiconductors and its licensor disclaim any and
 * all warranties, express or implied, including all implied warranties of
 * merchantability, fitness for a particular purpose and non-infringement of
 * intellectual property rights.  NXP Semiconductors assumes no responsibility
 * or liability for the use of the software, conveys no license or rights under any
 * patent, copyright, mask work right, or any other intellectual property rights in
 * or to any products. NXP Semiconductors reserves the right to make changes
 * in the software without notification. NXP Semiconductors also makes no
 * representation or warranty that such application will be suitable for the
 * specified use without further testing or modification.
 *
 * @par
 * Permission to use, copy, modify, and distribute this software and its
 * documentation is hereby granted, under NXP Semiconductors' and its
 * licensor's relevant copyrights in the software, without fee, provided that it
 * is used in conjunction with NXP Semiconductors microcontrollers.  This
 * copyright, permission, and disclaimer notice must appear in all copies of
 * this code.
 */

#include "usUsb.h"
#include "usDisk.h"
#include "usProtocol.h"
#include "usSys.h"
#include "usFirmware.h"
#include "usCamera.h"
#if defined(NXP_CHIP_18XX)
#include <string.h>
#include "board.h"
#include "USB.h"
#include <ctype.h>
#include "chip.h"
#include "FreeRTOS.h"
#include "task.h"
#include "i2c.h"
#elif defined(GP_CHIP)
#include <string.h>
#include "USB.h"
#include <ctype.h>
#include "FreeRTOS.h"
#include "task.h"
#include "drv_l2_sdc.h"
#include "drv_l1_sdc.h"
#include "drv_l1_ext_int.h"
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
#include <linux/netlink.h>
#include <sys/socket.h>
#include <poll.h>
#include <pthread.h>

#endif

/*****************************************************************************
 * Private types/enumerations/variables
 ****************************************************************************/
#if defined(DEBUG_ENABLE1)
#define SDEBUGOUT(...) do {printf("[Storage Mod]");printf(__VA_ARGS__);} while(0)
#else
#define SDEBUGOUT(...)
#endif

#define USDISK_SECTOR		DEF_SECTOR
#define OP_DIV(x)			((x)/USDISK_SECTOR)
#define OP_MOD(x)			((x)%USDISK_SECTOR)

#if defined(LINUX)
#define USB_WRTE			(256*1024)
#define USB_WRTESEC	512     /*USB_WRTE/512*/
#else
#define USB_WRTE			(32*1024)
#define USB_WRTESEC	64    /*USB_WRTE/512*/
#endif
#ifdef ENOUGH_MEMORY
#if defined(NXP_CHIP_18XX)
uint8_t usbWriteBuffer[USB_WRTE] __attribute__((section ("USB_RAM1"), zero_init));
#elif defined(GP_CHIP)
#pragma default_variable_attributes = @ "NON_CACHE"
uint8_t usbWriteBuffer[USB_WRTE];
#pragma default_variable_attributes =

#else
uint8_t usbWriteBuffer[USB_WRTE];
#endif
#endif

#if defined(NXP_CHIP_18XX)

#define NXP_USB_PHONE 	0
#define NXP_USB_DISK	1

/*Plug flag*/
volatile uint8_t notifyPhone = 0;
/** LPCUSBlib Mass Storage Class driver interface configuration and state information. This structure is
 *  passed to all Mass Storage Class driver functions, so that multiple instances of the same class
 *  within a device can be differentiated from one another.
 */
 /** Use USB0 for Phone
 *    Use USB1 for Mass Storage
*/
static USB_ClassInfo_MS_Host_t UStorage_Interface[]	= {
	{
		.Config = {
			.DataINPipeNumber       = 1,
			.DataINPipeDoubleBank   = false,

			.DataOUTPipeNumber      = 2,
			.DataOUTPipeDoubleBank  = false,
			.PortNumber = 0,
		},
	},
	{
		.Config = {
			.DataINPipeNumber       = 1,
			.DataINPipeDoubleBank   = false,

			.DataOUTPipeNumber      = 2,
			.DataOUTPipeDoubleBank  = false,
			.PortNumber = 1,
		},
	},
	
};

void scu_pinmux(unsigned port, unsigned pin, unsigned mode, unsigned func)
{
	volatile unsigned int * const scu_base=(unsigned int*)(LPC_SCU_BASE);
	scu_base[(PORT_OFFSET*port+PIN_OFFSET*pin)/4]=mode+func;
} /* scu_pinmux */

#define SCU_SFSPx(port, pin) (*((volatile uint32_t *) ((LPC_SCU_BASE + PORT_OFFSET * port + PIN_OFFSET * pin))))
/**
  \fn          int32_t SCU_PinConfiguare (uint8_t port, uint8_t pin, uint32_t pin_cfg)
  \brief       Set pin function and electrical characteristics
  \param[in]   port       Port number (0..15)
  \param[in]   pin        Pin number (0..31)
  \param[in]   pin_cfg    pin_cfg configuration bit mask
   - \b  0: function succeeded
   - \b -1: function failed
*/
int32_t SCU_PinConfigure (uint8_t port, uint8_t pin, uint32_t pin_cfg) 
{
	if ((port > 15) || (pin > 31)) return -1;
	SCU_SFSPx(port, pin) = pin_cfg;
	return 0;
}

int32_t SCU_USB1_PinConfigure (uint32_t USB1_pin_cfg) 
{
	LPC_SCU->SFSUSB = USB1_pin_cfg;
	return 0;
}

#define SCU_SFSUSB_AIM                (1    <<  0)
#define SCU_SFSUSB_ESEA               (1    <<  1)
#define SCU_SFSUSB_EPD                (1    <<  2)
#define SCU_SFSUSB_EPWR               (1    <<  4)
#define SCU_SFSUSB_VBUS               (1    <<  5)

#define SCU_USB1_PIN_CFG_AIM                  (SCU_SFSUSB_AIM )
#define SCU_USB1_PIN_CFG_ESEA                 (SCU_SFSUSB_ESEA)
#define SCU_USB1_PIN_CFG_EPD                  (SCU_SFSUSB_EPD )
#define SCU_USB1_PIN_CFG_EPWR                 (SCU_SFSUSB_EPWR)
#define SCU_USB1_PIN_CFG_VBUS                 (SCU_SFSUSB_VBUS)

#define USB_PORTSC1_H_PTS_POS                  (          30U)
#define USB_PORTSC1_H_PTS_MSK                  (3UL    << USB_PORTSC1_H_PTS_POS)
#define USB_PORTSC1_H_PTS(n)                   (((n)   << USB_PORTSC1_H_PTS_POS) & USB_PORTSC1_H_PTS_MSK)
static void SetUsb1ClockPinmux( void )
{
	volatile int32_t tmo = 1000;
	uint32_t  portsc;
		
#if (RTE_USB_USB1_HS_PHY_EN)// modified by Roger
	SCU_PinConfigure(RTE_USB1_ULPI_CLK_PORT, RTE_USB1_ULPI_CLK_BIT,  RTE_USB1_ULPI_CLK_FUNC | SCU_SFS_EPUN | SCU_SFS_EHS | SCU_SFS_EZI | SCU_SFS_ZIF);
	SCU_PinConfigure(RTE_USB1_ULPI_DIR_PORT, RTE_USB1_ULPI_DIR_BIT,  RTE_USB1_ULPI_DIR_FUNC | SCU_SFS_EPUN | SCU_SFS_EHS | SCU_SFS_EZI | SCU_SFS_ZIF);
	SCU_PinConfigure(RTE_USB1_ULPI_STP_PORT, RTE_USB1_ULPI_STP_BIT,  RTE_USB1_ULPI_STP_FUNC | SCU_SFS_EPUN | SCU_SFS_EHS | SCU_SFS_EZI | SCU_SFS_ZIF);
	SCU_PinConfigure(RTE_USB1_ULPI_NXT_PORT, RTE_USB1_ULPI_NXT_BIT,  RTE_USB1_ULPI_NXT_FUNC | SCU_SFS_EPUN | SCU_SFS_EHS | SCU_SFS_EZI | SCU_SFS_ZIF);
	SCU_PinConfigure(RTE_USB1_ULPI_D0_PORT,  RTE_USB1_ULPI_D0_BIT,   RTE_USB1_ULPI_D0_FUNC | SCU_SFS_EPUN | SCU_SFS_EHS | SCU_SFS_EZI | SCU_SFS_ZIF );
	SCU_PinConfigure(RTE_USB1_ULPI_D1_PORT,  RTE_USB1_ULPI_D1_BIT,   RTE_USB1_ULPI_D1_FUNC | SCU_SFS_EPUN | SCU_SFS_EHS | SCU_SFS_EZI | SCU_SFS_ZIF );
	SCU_PinConfigure(RTE_USB1_ULPI_D2_PORT,  RTE_USB1_ULPI_D2_BIT,   RTE_USB1_ULPI_D2_FUNC | SCU_SFS_EPUN | SCU_SFS_EHS | SCU_SFS_EZI | SCU_SFS_ZIF );
	SCU_PinConfigure(RTE_USB1_ULPI_D3_PORT,  RTE_USB1_ULPI_D3_BIT,   RTE_USB1_ULPI_D3_FUNC | SCU_SFS_EPUN | SCU_SFS_EHS | SCU_SFS_EZI | SCU_SFS_ZIF );
	SCU_PinConfigure(RTE_USB1_ULPI_D4_PORT,  RTE_USB1_ULPI_D4_BIT,   RTE_USB1_ULPI_D4_FUNC | SCU_SFS_EPUN | SCU_SFS_EHS | SCU_SFS_EZI | SCU_SFS_ZIF );
	SCU_PinConfigure(RTE_USB1_ULPI_D5_PORT,  RTE_USB1_ULPI_D5_BIT,   RTE_USB1_ULPI_D5_FUNC | SCU_SFS_EPUN | SCU_SFS_EHS | SCU_SFS_EZI | SCU_SFS_ZIF );
	SCU_PinConfigure(RTE_USB1_ULPI_D6_PORT,  RTE_USB1_ULPI_D6_BIT,   RTE_USB1_ULPI_D6_FUNC | SCU_SFS_EPUN | SCU_SFS_EHS | SCU_SFS_EZI | SCU_SFS_ZIF );
	SCU_PinConfigure(RTE_USB1_ULPI_D7_PORT,  RTE_USB1_ULPI_D7_BIT,   RTE_USB1_ULPI_D7_FUNC | SCU_SFS_EPUN | SCU_SFS_EHS | SCU_SFS_EZI | SCU_SFS_ZIF );
#endif

	/* switch to ulpi phy and turn on the power to phy*/
	printf("PORTSC1_H = %x\r\n",LPC_USB1->PORTSC1_H);
	portsc = LPC_USB1->PORTSC1_H & 0x00FFFFFF;
	portsc |= 0x80000000;
	LPC_USB1->PORTSC1_H = portsc;
	/* reset the controller */
	printf("bask %x \r\n", &(LPC_CCU1->CLKCCU[CLK_MX_USB1].CFG));
	LPC_CGU->BASE_CLK[CLK_BASE_USB1]     = (0x01U << 11) | (0x0CU << 24) ; 

	/* disable USB1_CLOCK */
	LPC_CCU1->CLKCCU[CLK_USB1].CFG = 0;

	/* reset the controller */
	LPC_CCU1->CLKCCU[CLK_MX_USB1].CFG |= 1U;
	while (!(LPC_CCU1->CLKCCU[CLK_MX_USB1].STAT & 1U));
	LPC_USB1->PORTSC1_H |=   USB_PORTSC1_H_PTS(2U);
	SCU_USB1_PinConfigure (SCU_USB1_PIN_CFG_AIM  |
					SCU_USB1_PIN_CFG_ESEA |
					SCU_USB1_PIN_CFG_EPD  |
					SCU_USB1_PIN_CFG_EPWR);
}

/****************SD Card************************/
/* SDIO wait flag */
static volatile int32_t sdio_wait_exit = 0;
static volatile int32_t sd_plugin = 0;

/* SDMMC card info structure */
mci_card_struct SDCardInfo;

/* Delay callback for timed SDIF/SDMMC functions */
static void sdmmc_waitms(uint32_t time)
{
	/* In an RTOS, the thread would sleep allowing other threads to run.
	   For standalone operation, we just spin on RI timer */
	vTaskDelay(time);
	return;
}

static void sdmmc_waitms2(uint32_t time)
{
	/* In an RTOS, the thread would sleep allowing other threads to run.
	   For standalone operation, we just spin on RI timer */
	int32_t curr = (int32_t) Chip_RIT_GetCounter(LPC_RITIMER);
	int32_t final = curr + ((SystemCoreClock / 1000) * time);

	if (final == curr) return;

	if ((final < 0) && (curr > 0)) {
		while (Chip_RIT_GetCounter(LPC_RITIMER) < (uint32_t) final) {}
	}
	else {
		while ((int32_t) Chip_RIT_GetCounter(LPC_RITIMER) < final) {}
	}

	return;
}

/**
 * @brief	Sets up the SD event driven wakeup
 * @param	bits : Status bits to poll for command completion
 * @return	Nothing
 */
static void sdmmc_setup_wakeup(void *bits)
{
	uint32_t bit_mask = *((uint32_t *)bits);
	/* Wait for IRQ - for an RTOS, you would pend on an event here with a IRQ based wakeup. */
	NVIC_ClearPendingIRQ(SDIO_IRQn);
	sdio_wait_exit = 0;
	Chip_SDIF_SetIntMask(LPC_SDMMC, bit_mask);
	NVIC_EnableIRQ(SDIO_IRQn);
}

/**
 * @brief	A better wait callback for SDMMC driven by the IRQ flag
 * @return	0 on success, or failure condition (-1)
 */
static uint32_t sdmmc_irq_driven_wait(void)
{
	uint32_t status;

	/* Wait for event, would be nice to have a timeout, but keep it  simple */
	while (sdio_wait_exit == 0) {}

	/* Get status and clear interrupts */
	status = Chip_SDIF_GetIntStatus(LPC_SDMMC);
	Chip_SDIF_ClrIntStatus(LPC_SDMMC, status);
	Chip_SDIF_SetIntMask(LPC_SDMMC, 0);

	return status;
}
static void App_SDMMC_Init(void)
{
	memset(&SDCardInfo, 0, sizeof(SDCardInfo));
	SDCardInfo.card_info.evsetup_cb = sdmmc_setup_wakeup;
	SDCardInfo.card_info.waitfunc_cb = sdmmc_irq_driven_wait;
	SDCardInfo.card_info.msdelay_func = sdmmc_waitms;
}

void SDIO_IRQHandler(void)
{
	/* All SD based register handling is done in the callback
	   function. The SDIO interrupt is not enabled as part of this
	   driver and needs to be enabled/disabled in the callbacks or
	   application as needed. This is to allow flexibility with IRQ
	   handling for applicaitons and RTOSes. */
	/* Set wait exit flag to tell wait function we are ready. In an RTOS,
	   this would trigger wakeup of a thread waiting for the IRQ. */
	NVIC_DisableIRQ(SDIO_IRQn);
	sdio_wait_exit = 1;
}

static void P7_Board_SDMMC_Init(void)
{
	Chip_SCU_PinMuxSet(0x1, 9, (SCU_PINIO_FAST | SCU_MODE_FUNC7));  /* P1.9 connected to SDIO_D0 */
	Chip_SCU_PinMuxSet(0x1, 10, (SCU_PINIO_FAST | SCU_MODE_FUNC7));  /* P1.10 connected to SDIO_D1 */
	Chip_SCU_PinMuxSet(0x1, 11, (SCU_PINIO_FAST | SCU_MODE_FUNC7));  /* P1.11 connected to SDIO_D2 */
	Chip_SCU_PinMuxSet(0x1, 12, (SCU_PINIO_FAST | SCU_MODE_FUNC7));  /* P1.12 connected to SDIO_D3 */

	Chip_SCU_PinMuxSet(0x1, 13, (SCU_MODE_INACT | SCU_MODE_INBUFF_EN | SCU_MODE_FUNC7));  /* P1.13 connected to SDIO_CD */
	Chip_SCU_PinMuxSet(0x1, 6, (SCU_PINIO_FAST | SCU_MODE_FUNC7));  /* P1.6 connected to SDIO_CMD */
	Chip_SCU_PinMuxSet(0x1, 5, (SCU_PINIO_FAST | SCU_MODE_FUNC7));  /* P1.7 connected to SDIO_POW */
	Chip_SCU_PinMuxSet(0xD, 15, (SCU_PINIO_FAST | SCU_MODE_FUNC5));  /* PD.15 connected to SDIO_WP */
	Chip_SCU_ClockPinMuxSet(2, (SCU_MODE_INACT | SCU_MODE_HIGHSPEEDSLEW_EN | SCU_MODE_FUNC4));/* CLK2 connected to SDIO_CLK */
}


static void SDMMC_Init(void)
{
    /* Disable SD/MMC interrupt */
	NVIC_DisableIRQ(SDIO_IRQn);
	App_SDMMC_Init();
	/*  SD/MMC initialization */
	P7_Board_SDMMC_Init();
	/* The SDIO driver needs to know the SDIO clock rate */
	Chip_SDIF_Init(LPC_SDMMC);	
	/* Enable SD/MMC Interrupt */
	NVIC_EnableIRQ(SDIO_IRQn);
	printf("SD/MMC Init Finish...\r\n");
}

/*****************************************************************************
 * Public types/enumerations/variables
 ****************************************************************************/
static void usSys_init(int port_num)
{
	if(port_num >= 2){
		die(1);
		return;
	}
#if (defined(CHIP_LPC43XX) || defined(CHIP_LPC18XX))
	if (port_num== 0){
		Chip_USB0_Init();
	} else {
		SetUsb1ClockPinmux();
		//Chip_USB1_Init();
	}
#endif
	USB_Init(UStorage_Interface[port_num].Config.PortNumber, USB_MODE_Host);
}

/** Configures the board hardware and chip peripherals for the demo's functionality. */
static void SetupHardware(void)
{
#if (defined(CHIP_LPC43XX_NOSYSTEM))
	SystemCoreClockUpdate();
	Board_Init();
#endif
	usSys_init(0);
	usSys_init(1);
	/*Init SD Card*/
	SDMMC_Init();
#if (defined(CHIP_LPC43XX_NOSYSTEM))
	/* Hardware Initialization */
	Board_Debug_Init();
#endif
}

#endif //#if defined(NXP_CHIP_18XX)
/*****************************************************************************
 * Private functions
 ****************************************************************************/
static int usStorage_sendHEAD(struct scsi_head *header)
{
	uint8_t buffer[PRO_HDR] = {0};

	if(!header){
		return 1;
	}

	memcpy(buffer, header, PRO_HDR);
	/*Send To Phone*/
	if(usProtocol_SendPackage(buffer, PRO_HDR)){
		SDEBUGOUT("Send To Phone[Just Header Error]\r\n");
		return 1;
	}

	return 0;
}

#ifdef NXP_ALLOW_WARING
static int usStorage_sendHEADBUF(struct scsi_head *header)
{
	uint8_t *buffer = NULL;
	uint32_t size = 0;

	if(!header){
		return 1;
	}

	if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
		SDEBUGOUT("usProtocol_GetAvaiableBuffer Failed\r\n");
		return 1;
	}
	memcpy(buffer, header, PRO_HDR);
	/*Send To Phone*/
	if(usProtocol_SendPackage(buffer, PRO_HDR)){
		SDEBUGOUT("Send To Phone[Just Header Error]\r\n");
		return 1;
	}

	return 0;
}

/*Just For Test*/
static int usStorage_Handle2(void)
{
	uint8_t *buffer = NULL;
	uint32_t size = 0;
	uint32_t secCount = 1, addr = 0;
	
#define TYPE_SDCARD		0
#define TYPE_HDD			1	
	printf("Now Test USB<----->SDMMC!!\r\n");
	
	while(1){
		if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
			printf("usProtocol_GetAvaiableBuffer Failed\r\n");
			die(1);
		}
		if(secCount == 128){
			printf("Reset SecCount to 1\r\n");
			secCount = 1;
		}
		if(usDisk_DiskReadSectors(buffer, TYPE_HDD, addr, secCount)){
			printf("Read Sector Error[addr:%d  SectorCount:%d]\r\n",
						addr, secCount);
			die(1);
		}
		/*Write to SDCard*/
		if(usDisk_DiskWriteSectors(buffer, TYPE_SDCARD, addr, secCount) != 0){
			printf("Write Sector Error[addr:%d  SectorCount:%d]\r\n",
						addr, secCount);
			die(1);
		}
		printf("Test %uSector Addr: %u Finish....\r\n", secCount, addr);
		addr += secCount;
		secCount++;
	}
}

#endif

/*
*Read Multi TIme
*/
static int usStorage_diskMULREAD(struct scsi_head *header)
{
	uint8_t *buffer = NULL, rc;
	uint32_t size = 0, rsize = 0, avsize = 0;
	int32_t addr = 0;

	if(!header){
		SDEBUGOUT("usStorage_diskREAD Parameter Failed\r\n");
		return 1;
	}
	addr = header->addr;
	
	if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
		SDEBUGOUT("usProtocol_GetAvaiableBuffer Failed\r\n");
		return 1;
	}
	memcpy(buffer, header, PRO_HDR);
	/*Send To Phone*/
	if((rc = usProtocol_SendPackage(buffer, PRO_HDR)) != 0){
		SDEBUGOUT("Send To Phone[Just Header Error]\r\n");
		return rc;
	}
	if(!header->len){		
		SDEBUGOUT("Read Request Len is 0[MayBeError]\r\n");
		return 0;
	}
	while(rsize < header->len){
		uint32_t secCount = 0;
		if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
			SDEBUGOUT("usProtocol_GetAvaiableBuffer Failed\r\n");
			return 1;
		}

		avsize = min(USDISK_SECTOR*OP_DIV(size), header->len-rsize); /*We leave a sector for safe*/		
		secCount = OP_DIV(avsize);
		if(usDisk_DiskReadSectors(buffer, header->wlun, addr, secCount)){			
			header->relag = 1;
			SDEBUGOUT("Read Sector Error[SndTotal:%d addr:%d  SectorCount:%d relag=%d] Send To APP the Result[SHIT]\r\n",
					rsize, addr, secCount, header->relag);
			usProtocol_SendPackage(header, PRO_HDR);
			return 1;
		}
		/*Send To Phone*/
		if((rc = usProtocol_SendPackage(buffer, avsize)) != 0){
			SDEBUGOUT("Send To Phone[SndTotal:%d addr:%d  SectorCount:%d]\r\n",
					rsize, addr, secCount);
			return rc;
		}
		SDEBUGOUT("READ INFO:%p[SZ:%dBytes][DS:%d(%d-->%d) [TS:%dBytes]\r\n", 
						buffer, avsize, header->addr, addr, addr +secCount, header->len);
		addr += secCount;
		rsize += avsize;
	}

	SDEBUGOUT("REQUEST READ OK:\r\nwtag=%d\r\nctrid=%d\r\naddr=%d\r\nlen=%d\r\nwlun=%d\r\n", 
			header->wtag, header->ctrid, header->addr, header->len, header->wlun);
	
	return 0;
}

/*Read Once*/
static int usStorage_diskSIGREAD(struct scsi_head *header)
{
	uint8_t *buffer = NULL;
	uint32_t size = 0;
	uint8_t rc;

	if(!header){
		SDEBUGOUT("usStorage_diskREAD Parameter Failed\r\n");
		return 1;
	}
	
	if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
		SDEBUGOUT("usProtocol_GetAvaiableBuffer Failed\r\n");
		return 1;
	}
	if(size < header->len){
		SDEBUGOUT("usStorage_diskSIGREAD Space Not Enough\r\n");
		return 1;
	}

	if(usDisk_DiskReadSectors(buffer, header->wlun, header->addr, OP_DIV(header->len))){
		SDEBUGOUT("Read Sector Error[SndTotal:%d addr:%d  SectorCount:%d]\r\n",
				header->len+PRO_HDR, header->addr, OP_DIV(header->len));
		/*Write to Phone*/
		header->relag = 1;
		usStorage_sendHEAD(header);	
		return 1;
	}
	/*Send To Phone*/
	usStorage_sendHEAD(header); 
	if((rc = usProtocol_SendPackage(buffer, header->len)) != 0){
		SDEBUGOUT("Send To Phone[SndTotal:%d addr:%d  SectorCount:%d]\r\n",
				header->len+PRO_HDR, header->addr, OP_DIV(header->len));
		return rc;
	}
	
	SDEBUGOUT("REQUEST READ OK:\r\nwtag=%d\r\nctrid=%d\r\naddr=%d\r\nlen=%d\r\nwlun=%d\r\n", 
			header->wtag, header->ctrid, header->addr, header->len, header->wlun);
	
	return 0;
}

static int usStorage_diskREAD(struct scsi_head *header)
{
	uint32_t size = 0;
	uint8_t *buffer = NULL;

	if(!header){
		SDEBUGOUT("usStorage_diskREAD Parameter Error\r\n");
		return 1;
	}

	if(!header->len){
		SDEBUGOUT("usStorage_diskREAD 0Bytes\r\n");
		/*Write to Phone*/
		header->relag = 1;
		usStorage_sendHEAD(header);	
		return 1;
	}
	
	if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
		SDEBUGOUT("usStorage_diskREAD Failed\r\n");
		return 1;
	}
	if(size < header->len+PRO_HDR){
		SDEBUGOUT("Use usStorage_diskMULREAD To Send[%d/%d]\r\n",
					header->len+PRO_HDR, size);
		return usStorage_diskMULREAD(header);
	}

	return usStorage_diskSIGREAD(header);
}

#ifdef ENOUGH_MEMORY
/*reduce write disk counts, but increase memcpy system call and increase cpu usage*/
static int usStorage_diskWRITE(uint8_t *buffer, uint32_t recvSize, struct scsi_head *header)
{
	uint32_t hSize = recvSize;
	uint32_t paySize, curSize = 0, addr;
	uint8_t *writePtr = NULL, rc = 0;
	uint32_t usbWriteBufferLen = 0;

	if(!buffer || !header){
		SDEBUGOUT("usStorage_diskWRITE Parameter Error\r\n");
		return 1;
	}
	if(!header->len){
		SDEBUGOUT("usStorage_diskWRITE Length is 0\r\n");
		return 0;
	}
	addr = header->addr;
	/*Write the first payload*/
	paySize= recvSize-PRO_HDR;

	writePtr = usbWriteBuffer;
	usbWriteBufferLen = USB_WRTE;
	if(paySize){
		memcpy(writePtr, buffer+PRO_HDR, paySize);
		usbWriteBufferLen -= paySize;
		writePtr += paySize;
	}
	curSize = paySize;
	SDEBUGOUT("REQUEST WRITE: PART INFO: addr:%d curSize:%dBytes paySize:%dBytes WriteBuffer:%d\r\n", 
							addr, curSize, paySize, usbWriteBufferLen);	
	while(curSize < header->len){
		uint8_t *pbuffer = NULL;
		paySize = 0;
		if((rc = usProtocol_RecvPackage((void **)&pbuffer, hSize, &paySize)) != 0){
			if(rc == PROTOCOL_RTIMOUT){
				SDEBUGOUT("usStorage_diskWRITE Receive Timeout\r\n");
			}else{
				SDEBUGOUT("usProtocol_RecvPackage Failed\r\n");
				/*Write to Phone*/
				header->relag = 1;				
				//usStorage_sendHEAD(header);			
			}
			return rc;
		}		
		hSize+= paySize;

		while(usbWriteBufferLen < paySize){
			SDEBUGOUT("Usb Write Buffer Overflow, Write To Disk\r\n");
			if(usbWriteBufferLen > 0){
				memcpy(writePtr, pbuffer, usbWriteBufferLen);
			}
			if(usDisk_DiskWriteSectors(usbWriteBuffer, header->wlun, addr, USB_WRTESEC)){
				SDEBUGOUT("REQUEST WRITE Error[addr:%d	SectorCount:%d]\r\n",
								addr, USB_WRTESEC);
				/*Write to Phone*/
				header->relag = 1;
				rc = usStorage_sendHEAD(header);
				return rc?rc:1;
			}
			addr += USB_WRTESEC;
			curSize += usbWriteBufferLen;
			/*Reset Var*/
			writePtr = usbWriteBuffer;
			pbuffer += usbWriteBufferLen;
			paySize = paySize-usbWriteBufferLen;
			usbWriteBufferLen = USB_WRTE;
		}
		memcpy(writePtr, pbuffer, paySize);
		writePtr += paySize;
		usbWriteBufferLen -= paySize;		
		curSize += paySize;		
		SDEBUGOUT("Usb Write Buffer Save Data:\r\nwritePtr:%p usbWriteBufferLen=%u"
					" curSize:%u paySize:%u\r\n", writePtr, usbWriteBufferLen, 
						curSize, paySize);
	}

	if(usbWriteBufferLen != USB_WRTE &&
			usDisk_DiskWriteSectors(usbWriteBuffer, header->wlun, addr,
				OP_DIV(USB_WRTE-usbWriteBufferLen))){
		SDEBUGOUT("REQUEST WRITE Error[addr:%d	SectorCount:%d]\r\n",
						addr, OP_DIV(USB_WRTE-usbWriteBufferLen));
		header->relag = 1;
	}

	/*Write to Phone*/
	if((rc = usStorage_sendHEAD(header)) != 0){
		SDEBUGOUT("Error Send Header\r\n");
		return rc;
	}

	SDEBUGOUT("REQUEST WRITE FINISH:\r\nwtag=%d\r\nctrid=%d\r\naddr=%d\r\nlen=%d\r\nwlun=%d\r\n", 
			header->wtag, header->ctrid, header->addr, header->len, header->wlun);
	
	return 0;
}

#else
static int usStorage_diskWRITE(uint8_t *buffer, uint32_t recvSize, struct scsi_head *header)
{
	uint32_t hSize = recvSize;
	uint32_t paySize, curSize = 0, secSize = 0, sdivSize = 0;
	uint32_t addr;	
	uint8_t sector[USDISK_SECTOR] = {0};
	uint8_t res;

	if(!buffer || !header){
		SDEBUGOUT("usStorage_diskWRITE Parameter Error\r\n");
		return 1;
	}
	if(!header->len){
		SDEBUGOUT("usStorage_diskWRITE Length is 0\r\n");
		return 0;
	}
	header->relag = 0;
	addr = header->addr;
	/*Write the first payload*/
	paySize= recvSize-PRO_HDR;
	if((secSize = OP_MOD(paySize)) > 0){
		memcpy(sector, buffer+PRO_HDR+OP_DIV(paySize)*USDISK_SECTOR, 
					secSize);
		SDEBUGOUT("REQUEST WRITE: InComplete Sector Detect[BEGIN]:%d/%dBytes\r\n", 
								secSize, USDISK_SECTOR);
	}
	if((sdivSize = OP_DIV(paySize)) > 0&& 
			usDisk_DiskWriteSectors(buffer+PRO_HDR, header->wlun, addr, sdivSize)){
		SDEBUGOUT("REQUEST WRITE Error[addr:%d  SectorCount:%d]\r\n",
						addr, sdivSize);
		/*Write to Phone*/
		header->relag = 1;
#ifdef IMMEDIATE_SNDHDR  	
		usStorage_sendHEAD(header);	
		return 1;
#else
		SDEBUGOUT("Write DISK Error, We Need To Receive complete the package...[FUCK]\r\n");
#endif
		
	}
	addr += sdivSize;
	curSize = paySize-secSize;
	SDEBUGOUT("REQUEST WRITE: PART INFO: addr:%d curSize:%dBytes %d/%dBytes\r\n", 
							addr, curSize, secSize, paySize);	
	while(curSize < header->len){
		uint32_t secCount = 0;
		uint8_t *ptr = NULL, *pbuffer = NULL;
		if((res = usProtocol_RecvPackage((void **)&pbuffer, hSize, &paySize)) != 0){
			if(res == PROTOCOL_RTIMOUT){
				SDEBUGOUT("usStorage_diskWRITE Timeout\r\n");
			}else{
				SDEBUGOUT("usProtocol_RecvPackage Failed\r\n");
				/*Write to Phone*/
				header->relag = 1;
				usStorage_sendHEAD(header);
			}
			return res;
		}
		/*add handle size*/		
		hSize+= paySize;
		ptr = pbuffer;
		if(secSize){
			SDEBUGOUT("REQUEST WIRTE: Handle InComplete Sector[%dBytes Payload:%dBytes]\r\n",
					secSize, paySize);
			if(paySize < USDISK_SECTOR-secSize){
				memcpy(sector+secSize, ptr, paySize);
				secSize += paySize;
				SDEBUGOUT("REQUEST WIRTE: PayLoad Not Enough Fill Sector[update to %dBytes CurSize:%dByte Payload:%dBytes]\r\n",
						secSize, curSize, paySize);
				continue;
			}
			memcpy(sector+secSize, ptr, USDISK_SECTOR-secSize);
			ptr += (USDISK_SECTOR-secSize);
			/*Write to disk*/
			if(header->relag == 0 && usDisk_DiskWriteSectors(sector, header->wlun, addr, 1)){
				SDEBUGOUT("REQUEST WRITE Last Sector Error[addr:%d SectorCount:%d]\r\n",
							addr, secCount);
				/*Write to Phone*/				
				header->relag = 1;
			#ifdef IMMEDIATE_SNDHDR  	
				usStorage_sendHEAD(header); 
				return 1;
			#else
				SDEBUGOUT("Write DISK Error Again, We Need To Continue Receive the package...[FUCK2]\r\n");
			#endif		
			}
			/*add var*/
			addr++;
			curSize += USDISK_SECTOR;
			paySize -= (USDISK_SECTOR-secSize);
			secSize = 0;
			SDEBUGOUT("REQUEST WIRTE: Handle InComplete Sector OK[Payload:%dBytes]\r\n",
								paySize);			
		}
		
		secCount = OP_DIV(paySize);
		if(!secSize && (secSize = OP_MOD(paySize)) > 0){
			SDEBUGOUT("REQUEST WRITE: InComplete Sector Detect [LAST]:%d/%dBytes\r\n", 
									secSize, USDISK_SECTOR);
			memcpy(sector, ptr+secCount*USDISK_SECTOR, secSize);
		}
		/*Write to disk*/
		if(header->relag == 0 &&
				secCount && usDisk_DiskWriteSectors(ptr, header->wlun, addr, secCount)){
			SDEBUGOUT("REQUEST WRITE Error[addr:%d	SectorCount:%d]\r\n",
							addr, sdivSize);
			/*Write to Phone*/
			header->relag = 1;
		#ifdef IMMEDIATE_SNDHDR	
			usStorage_sendHEAD(header); 
			return 1;
		#else
			SDEBUGOUT("Write DISK Error Again, We Need To Continue Receive the package...[FUCK3]\r\n");
		#endif
		}
		/*Add var*/
		addr += secCount;
		curSize += secCount*USDISK_SECTOR;
	}

	/*Write to Phone*/
	if(usStorage_sendHEAD(header)){
		SDEBUGOUT("Error Send Header\r\n");
		return 1;
	}

	SDEBUGOUT("REQUEST WRITE FINISH:\r\nresult=%d\r\nwtag=%d\r\nctrid=%d\r\naddr=%d\r\nlen=%d\r\nwlun=%d\r\n", 
					header->relag,header->wtag, header->ctrid, header->addr, header->len, header->wlun);
	return 0;
}
#endif

static int usStorage_diskINQUIRY(struct scsi_head *header)
{
	uint8_t *buffer = NULL;
	uint32_t size = 0, total = 0;
	struct scsi_inquiry_info dinfo;
	uint8_t rc = 0;
	
	if(!header){
		SDEBUGOUT("usStorage_diskINQUIRY Parameter Failed\r\n");
		return 1;
	}
	if(header->len != sizeof(struct scsi_inquiry_info)){
		SDEBUGOUT("usStorage_diskINQUIRY Parameter Error:[%d/%d]\r\n",
					header->len, sizeof(struct scsi_inquiry_info));
		return 1;
	}
	if(usDisk_DiskInquiry(header->wlun, &dinfo)){
		SDEBUGOUT("usDisk_DiskInquiry  Error\r\n");
	}
	if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
		SDEBUGOUT("usProtocol_GetAvaiableBuffer Failed\r\n");
		return 1;
	}
	memcpy(buffer, header, PRO_HDR);
	memcpy(buffer+PRO_HDR, &dinfo,  sizeof(struct scsi_inquiry_info));
	total = PRO_HDR+sizeof(struct scsi_inquiry_info);
	
	if((rc  = usProtocol_SendPackage(buffer, total)) != 0){
		SDEBUGOUT("usStorage_diskINQUIRY Failed\r\n");
		return rc;
	}
	
	SDEBUGOUT("usStorage_diskINQUIRY Successful\r\nDisk INQUIRY\r\nSize:%lld\r\nVendor:%s\r\nProduct:%s\r\nVersion:%s\r\nSerical:%s\r\n", 
				dinfo.size, dinfo.vendor, dinfo.product, dinfo.version, dinfo.serial);
	
	return 0;
}

static int usStorage_diskLUN(struct scsi_head *header)
{
	//uint8_t num = usDisk_DiskNum();
	uint8_t num = 1;
	uint8_t *buffer = NULL, rc = 0;
	uint32_t size = 0, total = 0;

	printf("Disk Number-->%d\r\n", num);
	if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
		SDEBUGOUT("usProtocol_GetAvaiableBuffer Failed\r\n");
		return 1;
	}	
	SDEBUGOUT("AvaiableBuffer 0x%p[%dBytes][Disk:%d]\r\n", 
				buffer, size, num);
	
	total = sizeof(struct scsi_head);
	memcpy(buffer, header, total);
	memcpy(buffer+total, &num, 1);
	total += 1;
	
	if((rc = usProtocol_SendPackage(buffer, total)) != 0){
		SDEBUGOUT("usProtocol_SendPackage Failed[ret=%d]\r\n", rc);
		return rc;
	}
	SDEBUGOUT("usStorage_diskLUN Successful[DiskNumber %d]\r\n", num);
	return 0;
}

static int usStorage_cacheSYNC(struct scsi_head *header)
{
	if(!header){
		return 1;
	}
	usDisk_cacheSYNC(header->wlun);
	usStorage_sendHEAD(header);

	return 0;
}

static int usStorage_cameraStream(struct scsi_head *header)
{
	uint8_t *framePtr = NULL, *curBuf = NULL;
	uint32_t frameSize = 0, curSend = 0, haveSend = 0;	
	char frameType[32] = {0};
	uint8_t *buffer = NULL;
	uint32_t size = 0;
	uint8_t rc = 0;
	
	if(usCamera_Streaming(&framePtr, &frameSize) < 0){
		header->relag = 1;
	}

	if(!frameSize){
		header->len = 0;
		usStorage_sendHEAD(header);
		return 0;
	}
#define CAMERA_MAX_SEND		(204800)/*200KB*/
	while(haveSend < frameSize){
		if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
			SDEBUGOUT("usStorage_diskREAD Failed\r\n");
			return 1;
		}
		
		if(frameSize-haveSend >= CAMERA_MAX_SEND){
			curSend = CAMERA_MAX_SEND;
		}else{
			curSend = frameSize-haveSend;
		}
		if(size < curSend){
			curSend = size;
		}
		if(curSend + haveSend == frameSize){			
			header->addr = 2;
			SDEBUGOUT("Send Last Frame Part[%d]\n",  curSend);
		}		
		header->len = curSend;
		memcpy(buffer, header, PRO_HDR);
		memcpy(buffer+PRO_HDR, framePtr+haveSend, curSend);
		if((rc = usProtocol_SendPackage(buffer, header->len+PRO_HDR)) != 0){
			SDEBUGOUT("Send To Phone[SndTotal:%d] Failed\n",header->len+PRO_HDR);
			return rc;
		}

		haveSend+= curSend;
	}

	return 0;
}

static int usStorage_cameraConf(struct scsi_head *header)
{
	uint8_t *buffer = NULL;
	uint32_t size = 0, usedSize = 0;

	if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
		SDEBUGOUT("usProtocol_GetAvaiableBuffer Failed\r\n");
		return 1;
	}
	usCamera_GetConfig(buffer+PRO_HDR, size-PRO_HDR, &usedSize);
	header->len = usedSize;
	memcpy(buffer, header, PRO_HDR);

	
	return usProtocol_SendPackage(buffer, header->len+PRO_HDR);
}

static int usStorage_cameraCurConf(struct scsi_head *header)
{
	uint8_t *buffer = NULL;
	uint32_t size = 0, usedSize = 0;

	if(usProtocol_GetAvaiableBuffer((void **)&buffer, &size)){
		SDEBUGOUT("usProtocol_GetAvaiableBuffer Failed\r\n");
		return 1;
	}
	
	usCamera_GetCurConfig(buffer+PRO_HDR, size-PRO_HDR, &usedSize);
	header->len = usedSize;
	memcpy(buffer, header, PRO_HDR);

	
	return usProtocol_SendPackage(buffer, header->len+PRO_HDR);
}

static int usStorage_Handle(void)
{	
	uint8_t *buffer;
	uint32_t size=0;
	uint8_t rc;
	struct scsi_head header;

	if((rc = usProtocol_RecvPackage((void **)&buffer, 0, &size)) != 0){
		if(rc == PROTOCOL_DISCONNECT){
			return PROTOCOL_DISCONNECT;
		}else if(rc == PROTOCOL_RTIMOUT){
			return PROTOCOL_RTIMOUT;
		}
		SDEBUGOUT("usProtocol_RecvPackage Failed\r\n");
		return rc;
	}
	if(size < PRO_HDR){
		SDEBUGOUT("usProtocol_RecvPackage Too Small [%d]Bytes\r\n", size);
		return 1;
	}
	/*Must save the header, it will be erase*/
	memcpy(&header, buffer, PRO_HDR);
	if(header.head != SCSI_PHONE_MAGIC){
		SDEBUGOUT("Package Header Error:0x%x\r\n", header.head);
		return PROTOCOL_REGEN;
	}
	SDEBUGOUT("usProtocol_RecvPackage [%d/%d]Bytes\r\n", 
				header.len, size);
	/*Handle Package*/
	SDEBUGOUT("RQUEST:\r\nwtag=%d\r\nctrid=%d\r\naddr=%d\r\nlen=%d\r\nwlun=%d\r\n", 
			header.wtag, header.ctrid, header.addr, header.len, header.wlun);

	switch(header.ctrid){
		case SCSI_READ:
			return usStorage_diskREAD(&header);
		case SCSI_WRITE:		
			return usStorage_diskWRITE(buffer, size, &header);
		case SCSI_INQUIRY:
			return usStorage_diskINQUIRY(&header);
		case SCSI_GET_LUN:
			return usStorage_diskLUN(&header);
		case SCSI_UPDATE_START:
		case SCSI_UPDATE_DATA:
		case SCSI_UPDATE_END:
			return usStorage_firmwareUP(buffer, size);
		case SCSI_FIRMWARE_INFO:
			return usStorage_firmwareINFO(&header);
		case SCSI_SYNC_INFO:
			return usStorage_cacheSYNC(&header);
		case SCSI_WRITE_LIC:
			return usStorage_firmwareLicense(buffer, size);
		case SCSI_CAMERA_STREAM:
			return usStorage_cameraStream(&header);
		case SCSI_CAMERA_GETCONF:
			return usStorage_cameraConf(&header);
		case SCSI_CAMERA_GETCURCONF:
			return usStorage_cameraCurConf(&header);
		default:
			SDEBUGOUT("Unhandle Command\r\nheader:%x\r\nwtag:%d\r\n"
						"ctrid:%d\r\naddr:%u\r\nlen:%d\r\nwlun:%d\r\n", header.head,
						header.wtag, header.ctrid, header.addr, header.len, header.wlun);
			header.relag = 1;
			usStorage_sendHEAD(&header);
	}

	return 0;
}

/*****************************************************************************
 * Public functions
 ****************************************************************************/

#if defined(NXP_CHIP_18XX)
/** Main program entry point. This routine configures the hardware required by the application, then
 *  calls the filesystem function to read files from USB Disk
 *Ustorage Project by Szitman 20161022
 */
static uint8_t NXP_setDiskNotifyTag(void)
{
	if(USB_HostState[NXP_USB_PHONE] != HOST_STATE_Configured){
		printf("No Phone Detected...\r\n");
		return 0;
	}
	if(notifyPhone++ == 127){
		notifyPhone = 1;
	}
	return notifyPhone;
}

static uint8_t NXP_getDiskNotifyTag(void)
{
	return notifyPhone;
}

static void NXP_resetDiskNotifyTag(void)
{
	notifyPhone = 0;
}

static uint8_t	NXP_notifyDiskChange(void)
{
	struct scsi_head header;
	static uint32_t wtag = 0;

	header.head = SCSI_DEVICE_MAGIC;
	header.ctrid = SCSI_GET_LUN;

	header.wtag = wtag++;
	header.len = 1;

	header.relag = 0;
	header.wlun = 0;
	header.addr = 0;
	if(usStorage_diskLUN(&header) != 0){
		printf("Get Disk Lun Failed\r\n");
	}
	printf("Notify Disk Change to Phone Successful....\r\n");

	return 0;
}

static void free_usbmemory(uint8_t corenum)
{
	int i;

	Pipe_ClosePipe(corenum, PIPE_CONTROLPIPE);	// FIXME close only relevant pipes , take long time in ISR	
	for (i = PIPE_CONTROLPIPE + 1; i < PIPE_TOTAL_PIPES; i++)
		if (PipeInfo[corenum][i].PipeHandle != 0) {
			Pipe_ClosePipe(corenum, i);
		}
}
 /*
**return value:
*0: no usb disk
*1: usb disk ok
*/
 static void wait_usbdisk(void)
{
	if(USB_HostState[NXP_USB_DISK] < HOST_STATE_Powered){
		//printf("Disk status=%d\r\n", USB_HostState[NXP_USB_DISK]);
		return ;
	}
	while (USB_HostState[NXP_USB_DISK] != HOST_STATE_Configured) {
		USB_USBTask(NXP_USB_DISK, USB_MODE_Host);
		continue;
	}
}

 static void wait_sdcard(void)
{
	 uint32_t rc;

	int32_t SD_status = Chip_SDIF_CardNDetect(LPC_SDMMC);
	if(SD_status == 0 &&sd_plugin == 0){
		/*Found SD Card*/
		/* Enable slot power */		
		printf("SD/MMC Card Plug In! ..\r\n");
		Chip_SDIF_PowerOn(LPC_SDMMC);
		/* Enumerate the SDMMC card once detected.
		 * Note this function may block for a little while. */		 
		printf("SD/MMC PowerON! ..\r\n");
		rc = Chip_SDMMC_Acquire(LPC_SDMMC, &SDCardInfo);
		if (!rc) {
			printf("SD/MMC Card enumeration failed! ..\r\n");
			Chip_SDIF_PowerOff(LPC_SDMMC);
			/*ReInit SD Card, if not driver will down*/
			SDMMC_Init();			
			return;
		}
	#ifdef LIMIT_RDONLY	
		/* Check if Write Protected */
		if (Chip_SDIF_CardWpOn(LPC_SDMMC)){
			printf("SDMMC Card is write protected!, We Think It Failed..\r\n");
			return ;
		}
	#endif
		printf("SD/MMC Acquire Finish! ..\r\n");
		if(usDisk_DeviceDetect(USB_CARD, (void*)&SDCardInfo)){
			printf("SD/MMC Device Detect Failed\r\n");
			return;
		}
		sd_plugin = 1;		
		NXP_setDiskNotifyTag();
	}else if(SD_status && sd_plugin == 1){
		/*SD Card Out*/		
		printf("SD/MMC Card Plug Out! ..\r\n");
		Chip_SDIF_DeInit(LPC_SDMMC);
		Chip_SDIF_PowerOff(LPC_SDMMC);
		sd_plugin = 0;
		usDisk_DeviceDisConnect(USB_CARD, NULL);		
		App_SDMMC_Init();		
		NXP_setDiskNotifyTag();
		/*We Need to Reinit SD Driver for safe*/
		SDMMC_Init();
	#if 0	/*if we start it , phone will not detect*/	
		/*notify i2c to restart nxp*/		
		usDisk_DiskStartStop(0);
		i2c_ioctl(IOCTL_POWER_RESET_I2C, NULL);
	#endif	
	}
}

void vs_main_disk(void *pvParameters)
{
	while(1){
		wait_sdcard();
		wait_usbdisk();
		vTaskDelay(100);
	}
}

void vs_main(void *pvParameters)
{
	SetupHardware();

	SDEBUGOUT("U-Storage Running.\r\n");
	
	usProtocol_init();
	xTaskCreate(vs_main_disk, "vTaskDisk", 1024,
			NULL, (tskIDLE_PRIORITY + 2UL), (TaskHandle_t *) NULL);

	while(1){
		if(USB_HostState[NXP_USB_PHONE] < HOST_STATE_Powered){
			//printf("Phone status=%d\r\n", USB_HostState[NXP_USB_PHONE]);
			vTaskDelay(100);
			continue;
		}
		while (USB_HostState[NXP_USB_PHONE] != HOST_STATE_Configured) {
			if(USB_HostState[NXP_USB_PHONE] == HOST_STATE_Unattached){
				printf("Phone Unttached...\r\n");
				break;
			}
			USB_USBTask(NXP_USB_PHONE, USB_MODE_Host);
			continue;
		}
		/*Connect Phone Device*/
		if(usProtocol_ConnectPhone()){
			/*Connect to Phone Failed*/
			vTaskDelay(200);
			continue;
		}
		usStorage_Handle();
		if(USB_HostState[NXP_USB_PHONE] == HOST_STATE_Configured &&
					NXP_getDiskNotifyTag()){
			printf("We Need To Notify Phone Disk Changed.....\r\n");
			NXP_notifyDiskChange();			
			NXP_resetDiskNotifyTag();
		}
	}
}


/** Event handler for the USB_DeviceAttached event. This indicates that a device has been attached to the host, and
 *  starts the library USB task to begin the enumeration and USB management process.
 */
void EVENT_USB_Host_DeviceAttached(const uint8_t corenum)
{
	if(corenum == NXP_USB_DISK){
		printf(("Disk Attached on port %d\r\n"), corenum);
	}else{
		printf(("Phone Attached on port %d\r\n"), corenum);	
	}
}

/** Event handler for the USB_DeviceUnattached event. This indicates that a device has been removed from the host, and
 *  stops the library USB task management process.
 */
void EVENT_USB_Host_DeviceUnattached(const uint8_t corenum)
{
	uint8_t res;
	printf(("\r\nDevice Unattached on port %d\r\n"), corenum);
	//Chip_CREG_DisableUSB0Phy();
	memset(&(UStorage_Interface[corenum].State), 0x00, sizeof(UStorage_Interface[corenum].State));
	if(corenum == NXP_USB_DISK){
		usDisk_DeviceDisConnect(USB_DISK, NULL);
		NXP_setDiskNotifyTag();	
	#if defined(HALT_RESTART)	
		/*notify i2c to restart nxp*/
		i2c_ioctl(IOCTL_POWER_RESET_I2C, NULL);
	#endif
	}else{
		res = usProtocol_DeviceDisConnect();
		if(res == 0){
			printf("No Phone Connceted, so Not Restart NXP\r\n");
			return;
		}else{
			printf("Phone [%d] Connceted Restart NXP[Try Stop Disk]\r\n", res);
			/*notify i2c to restart nxp*/
			usDisk_DiskStartStop(0);
		#if defined(HALT_RESTART)				
			i2c_ioctl(IOCTL_POWER_RESET_I2C, NULL);
		#endif
		}
	}	
	free_usbmemory(corenum);
	USB_Disable(corenum, USB_MODE_Host);
	/*We Need to init usb driver again, if not usb driver will broken*/
	sdmmc_waitms2(50);
	usSys_init(corenum);
}

/** Event handler for the USB_DeviceEnumerationComplete event. This indicates that a device has been successfully
 *  enumerated by the host and is now ready to be used by the application.
 */
void EVENT_USB_Host_DeviceEnumerationComplete(const uint8_t corenum)
{
	if(corenum == NXP_USB_DISK){
		printf("Disk Enumeration on port %d\r\n", corenum);
		usDisk_DeviceDetect(USB_DISK, &UStorage_Interface[corenum]);		
		NXP_setDiskNotifyTag();
	}else if(corenum == NXP_USB_PHONE){
		printf("Phone Enumeration on port %d\r\n", corenum);
		usProtocol_DeviceDetect(&UStorage_Interface[corenum]);	
	}else{
		SDEBUGOUT("Unknown USB Port %d.\r\n", corenum);
	}
}
/** Event handler for the USB_HostError event. This indicates that a hardware error occurred while in host mode. */
void EVENT_USB_Host_HostError(const uint8_t corenum, const uint8_t ErrorCode)
{
	//Chip_CREG_DisableUSB0Phy();	
	free_usbmemory(corenum);
	USB_Disable(corenum, USB_MODE_Host);
	
	printf(("Host Mode Error\r\n"
			  " -- Error port %d\r\n"
			  " -- Error Code %d\r\n" ), corenum, ErrorCode);
	sdmmc_waitms2(50);
	usSys_init(corenum);	
	/*notify i2c to restart nxp*/	
	usDisk_DiskStartStop(0);
#if defined(HALT_RESTART)					
	i2c_ioctl(IOCTL_POWER_RESET_I2C, NULL);
#endif
}

/** Event handler for the USB_DeviceEnumerationFailed event. This indicates that a problem occurred while
 *  enumerating an attached USB device.
 */
void EVENT_USB_Host_DeviceEnumerationFailed(const uint8_t corenum,
											const uint8_t ErrorCode,
											const uint8_t SubErrorCode)
{
	//Chip_CREG_DisableUSB0Phy();	
	free_usbmemory(corenum);
	USB_Disable(corenum, USB_MODE_Host);
	printf(("Dev Enum Error\r\n"
			  " -- Error port %d\r\n"
			  " -- Error Code %d\r\n"
			  " -- Sub Error Code %d\r\n"
			  " -- In State %d\r\n" ),
			 corenum, ErrorCode, SubErrorCode, USB_HostState[corenum]);
	printf("Reinit Core:%d\r\n", corenum);
	sdmmc_waitms2(50);
	usSys_init(corenum);	
	/*notify i2c to restart nxp*/	
	usDisk_DiskStartStop(0);
#if defined(HALT_RESTART)	
	i2c_ioctl(IOCTL_POWER_RESET_I2C, NULL);
#endif
}
#elif defined(GP_CHIP)
#define GP_USB_PHONE 	0
#define GP_USB_DISK	1
/*Plug flag*/
volatile uint8_t notifyPhone = 0;
QueueHandle_t   sd_task_que;
static INT8U card_detect_status;
static INT32S gp_sd_idx = GP_SD_IDX;

/** LPCUSBlib Mass Storage Class driver interface configuration and state information. This structure is
 *  passed to all Mass Storage Class driver functions, so that multiple instances of the same class
 *  within a device can be differentiated from one another.
 */
 /** Use USB0 for Phone
 *    Use USB1 for Mass Storage
*/
static USB_ClassInfo_MS_Host_t UStorage_Interface[]	= {
	{
		.Config = {
			.DataINPipeNumber       = 1,
			.DataINPipeDoubleBank   = false,

			.DataOUTPipeNumber      = 2,
			.DataOUTPipeDoubleBank  = false,
			.PortNumber = 0,
		},
	},
	{
		.Config = {
			.DataINPipeNumber       = 1,
			.DataINPipeDoubleBank   = false,

			.DataOUTPipeNumber      = 2,
			.DataOUTPipeDoubleBank  = false,
			.PortNumber = 1,
		},
	},
	
};

typedef enum {
    SD_INIT = 0x85900000,
    SD_TEST,
    SD_PLUG_OUT
} SD_TASK_ENUM;

void gp_sd_card_detect_isr(void)
{
	INT32U msg_id;
	//card insert detect
	if(card_detect_status == 0)
	{
		msg_id = SD_INIT;
		card_detect_status = 1;
		//send notify queue to initial SD       
		xQueueSendFromISR(sd_task_que, (void *)&msg_id, 0);

	}else if(card_detect_status == 2){
		msg_id = SD_PLUG_OUT;
		card_detect_status = 3;

		xQueueSendFromISR(sd_task_que, (void *)&msg_id, 0);
	}
}

static void SDMMC_Init(void)
{
	drvl1_sdc_pwr_sel(1);
	drvl1_sdc_pwr_enable(1);//PMOS 1:off
	vTaskDelay(200);//turn off power awhile
	drvl1_sdc_pwr_enable(0);
	drvl1_sdc_pwr_sel(1);
	drvl1_sdc_levelshifter_enable(1);
	vTaskDelay(200);//turn on power awhile
	//queue create
	sd_task_que = xQueueCreate(5,sizeof(INT32U));
	//external interrupt init
	card_detect_status = 0;
	drv_l1_ext_int_init();
	drv_l1_ext_edge_set(EXTA,FALLING);
	drv_l1_ext_user_isr_set(EXTA, gp_sd_card_detect_isr);
	drv_l1_ext_enable_set(EXTA,ENABLE);
	//boot and inserted card, send queue to initial
	if((R_IOA_I_DATA&0x0400) == 0){
		INT32U msg_send = SD_INIT;
		xQueueSend(sd_task_que, &msg_send, portMAX_DELAY);
	}
}

static uint8_t GP_setDiskNotifyTag(void)
{
	if(USB_HostState[GP_USB_PHONE] != HOST_STATE_Configured){
		printf("No Phone Detected...\r\n");
		return 0;
	}
	if(notifyPhone++ == 127){
		notifyPhone = 1;
	}
	return notifyPhone;
}

static uint8_t GP_getDiskNotifyTag(void)
{
	return notifyPhone;
}

static void GP_resetDiskNotifyTag(void)
{
	notifyPhone = 0;
}

static uint8_t	GP_notifyDiskChange(void)
{
	struct scsi_head header;
	static uint32_t wtag = 0;

	header.head = SCSI_DEVICE_MAGIC;
	header.ctrid = SCSI_GET_LUN;

	header.wtag = wtag++;
	header.len = 1;

	header.relag = 0;
	header.wlun = 0;
	header.addr = 0;
	if(usStorage_diskLUN(&header) != 0){
		printf("Get Disk Lun Failed\r\n");
	}
	printf("Notify Disk Change to Phone Successful....\r\n");

	return 0;
}
 static void SetupHardware(void)
 {
	USB_Init(UStorage_Interface[0].Config.PortNumber, USB_MODE_Host);	
	USB_Init(UStorage_Interface[1].Config.PortNumber, USB_MODE_Host);
	/*Init SD*/
	SDMMC_Init();
 }
 
static void wait_sdcard(INT32S SD_IDX)
{
	INT32U msg, result;
	INT32S ret;
	INT32U total_sec = 0;
	
	while(1){
		result = xQueueReceive(sd_task_que, &msg, portMAX_DELAY);
		if(result != pdPASS){
			vTaskDelay(30);
			continue;
		}
		switch(msg){
			case SD_INIT:
				//debouce awhile
				vTaskDelay(40);
				if((R_IOA_I_DATA&0x0400) != 0){
					//unstable return  
					card_detect_status = 0;	
					break;
				}
				drvl2_sd_set_bit_mode(SD_IDX, 4);
				ret = drvl2_sd_init(SD_IDX); // SD_IDX
				if(ret != 0){
					if((R_IOA_I_DATA&0x0400) != 0){
						printf("card remove during initial\r\n");
					}else{
						printf("init not done\r\n");
					}
					//card_detect_status = 0;  
					drvl2_sd_card_remove(1);
					drvl1_sdc_pwr_enable(0);
					drvl1_sdc_pwr_sel(1);
					drvl1_sdc_levelshifter_enable(1);
					card_detect_status = 0;  
					drv_l1_ext_edge_set(EXTA,FALLING);
					break;
				}
				
				printf("SD[%d]:drvl2_sd_init exit\r\n", SD_IDX);
				total_sec = drvl2_sd_sector_number_get(SD_IDX);
				printf("SD[%d]:SD total sector num = %d\r\n", SD_IDX, total_sec);
				card_detect_status = 2;  
				drv_l1_ext_edge_set(EXTA,RISING);
				if(usDisk_DeviceDetect(USB_CARD, (void*)&total_sec)){
					printf("SD/MMC Device Detect Failed\r\n");
					break;
				}
				GP_setDiskNotifyTag();
				break;
			case SD_PLUG_OUT:
				 vTaskDelay(40);
				 if((R_IOA_I_DATA&0x0400) == 0){
					 //unstable return	 
					 card_detect_status = 2;  
					 break;
				 }			
				//debouce awhile
				drvl2_sd_card_remove(1);
				drvl1_sdc_pwr_enable(0);
				drvl1_sdc_pwr_sel(1);
				drvl1_sdc_levelshifter_enable(1);
				card_detect_status = 0;  
				drv_l1_ext_edge_set(EXTA,FALLING);
				usDisk_DeviceDisConnect(USB_CARD, NULL);		
				GP_setDiskNotifyTag();
				printf("SD plug out\r\n");
				break;
			default:
				break;
		}
	}
}
 /*
**return value:
*0: no usb disk
*1: usb disk ok
*/
 static void wait_usbdisk(void)
{
	if(USB_HostState[GP_USB_DISK] < HOST_STATE_Powered){
		//printf("Disk status=%d\r\n", USB_HostState[NXP_USB_DISK]);
		return ;
	}
	while (USB_HostState[GP_USB_DISK] != HOST_STATE_Configured) {
		MS_Host_USBTask(&UStorage_Interface[GP_USB_DISK]);
		USB_USBTask(GP_USB_DISK, USB_MODE_Host);
		continue;
	}
}

void vs_main_disk(void *pvParameters)
{
	while(1){
		wait_usbdisk();
		vTaskDelay(100);
	}
}

void vs_sd_disk(void *pvParameters)
{
	INT32S SD_IDX = (*((INT32S *)pvParameters));
	wait_sdcard(SD_IDX);
}

void vs_main(void *pvParameters)
{
	SetupHardware();

	SDEBUGOUT("U-Storage Running.\r\n");	
	usProtocol_init();
	xTaskCreate(vs_main_disk, "vTaskDisk", 1024,
			NULL, (tskIDLE_PRIORITY + 2UL), (TaskHandle_t *) NULL);
	xTaskCreate(vs_sd_disk, "vTaskSDDisk", 1024,
			(void*)(&gp_sd_idx), (tskIDLE_PRIORITY + 2UL), (TaskHandle_t *) NULL);

	while(1){
		if(USB_HostState[GP_USB_PHONE] < HOST_STATE_Powered){
			//printf("Phone status=%d\r\n", USB_HostState[NXP_USB_PHONE]);
			vTaskDelay(100);
			continue;
		}
		while (USB_HostState[GP_USB_PHONE] != HOST_STATE_Configured) {
			if(USB_HostState[GP_USB_PHONE] == HOST_STATE_Unattached){
				printf("Phone Unttached...\r\n");
				break;
			}
			MS_Host_USBTask(&UStorage_Interface[GP_USB_PHONE]);
			USB_USBTask(GP_USB_PHONE, USB_MODE_Host);
			continue;
		}
		/*Connect Phone Device*/
		if(usProtocol_ConnectPhone()){
			/*Connect to Phone Failed*/
			vTaskDelay(200);
			continue;
		}
		usStorage_Handle();
		if(USB_HostState[GP_USB_PHONE] == HOST_STATE_Configured &&
					GP_getDiskNotifyTag()){
			printf("We Need To Notify Phone Disk Changed.....\r\n");
			GP_notifyDiskChange();			
			GP_resetDiskNotifyTag();
		}
	}
}


/** Event handler for the USB_DeviceAttached event. This indicates that a device has been attached to the host, and
 *  starts the library USB task to begin the enumeration and USB management process.
 */
void EVENT_USB_Host_DeviceAttached(const uint8_t corenum)
{
	if(corenum == GP_USB_DISK){
		printf(("Disk Attached on port %d\r\n"), corenum);
	}else{
		printf(("Phone Attached on port %d\r\n"), corenum);	
	}
}

/** Event handler for the USB_DeviceUnattached event. This indicates that a device has been removed from the host, and
 *  stops the library USB task management process.
 */
void EVENT_USB_Host_DeviceUnattached(const uint8_t corenum)
{
	uint8_t res;
	printf(("\r\nDevice Unattached on port %d\r\n"), corenum);
	memset(&(UStorage_Interface[corenum].State), 0x00, sizeof(UStorage_Interface[corenum].State));
	if(corenum == GP_USB_DISK){
		usDisk_DeviceDisConnect(USB_DISK, NULL);
		GP_setDiskNotifyTag();		
	}else{
		res = usProtocol_DeviceDisConnect();
		if(res == 0){
			printf("No Phone Connceted, so Not Restart NXP\r\n");
		}else{
			printf("Phone [%d] Connceted Restart NXP[Try Stop Disk]\r\n", res);
			/*notify i2c to restart nxp*/
			usDisk_DiskStartStop(0);
		}
	}
}

/** Event handler for the USB_DeviceEnumerationComplete event. This indicates that a device has been successfully
 *  enumerated by the host and is now ready to be used by the application.
 */
void EVENT_USB_Host_DeviceEnumerationComplete(const uint8_t corenum)
{
	if(corenum == GP_USB_DISK){
		printf("Disk Enumeration on port %d\r\n", corenum);
		usDisk_DeviceDetect(USB_DISK, &UStorage_Interface[corenum]);		
		GP_setDiskNotifyTag();
	}else if(corenum == GP_USB_PHONE){
		printf("Phone Enumeration on port %d\r\n", corenum);
		usProtocol_DeviceDetect(&UStorage_Interface[corenum]);	
	}else{
		SDEBUGOUT("Unknown USB Port %d.\r\n", corenum);
	}
}
/** Event handler for the USB_HostError event. This indicates that a hardware error occurred while in host mode. */
void EVENT_USB_Host_HostError(const uint8_t corenum, const uint8_t ErrorCode)
{
	printf(("Host Mode Error\r\n"
			  " -- Error port %d\r\n"
			  " -- Error Code %d\r\n" ), corenum, ErrorCode);
	
	/*notify i2c to restart nxp*/	
	usDisk_DiskStartStop(0);
}

/** Event handler for the USB_DeviceEnumerationFailed event. This indicates that a problem occurred while
 *  enumerating an attached USB device.
 */
void EVENT_USB_Host_DeviceEnumerationFailed(const uint8_t corenum,
											const uint8_t ErrorCode,
											const uint8_t SubErrorCode)
{
	printf(("Dev Enum Error\r\n"
			  " -- Error port %d\r\n"
			  " -- Error Code %d\r\n"
			  " -- Sub Error Code %d\r\n"
			  " -- In State %d\r\n" ),
			 corenum, ErrorCode, SubErrorCode, USB_HostState[corenum]);
	printf("Reinit Core:%d\r\n", corenum);
	/*notify i2c to restart nxp*/	
	usDisk_DiskStartStop(0);
}

#elif defined(LINUX)

#define UEVENT_BUFFER_SIZE		2048
#define UEVENT_NUM_ENVP			32
#ifndef NETLINK_KOBJECT_UEVENT
#define NETLINK_KOBJECT_UEVENT	15
#endif
#define STOR_SUBSYS				"block"
#define STOR_DEVTYPE			"disk"
#define STOR_STR_ADD			"add"
#define STOR_STR_REM			"remove"
#define STOR_STR_CHANGE		"change"
#define PHONE_SUBSYS				"usb"
#define PHONE_DEVTYPE			"usb_device"
#define HUB_CONFIG			"/tmp/us.hub"
#define PHONE_BUS_LOC           "usb1/1-1/1-1.1/1-1.1.2"
#define DISK_BUS_LOC            "usb1/1-1/1-1.4"

struct udevd_uevent_msg {
	unsigned char id;
	char *action;
	char *devpath;
	char *subsystem;	
	char *devname;
	char *devtype;
	dev_t devt;
	unsigned long long seqnum;
	unsigned int timeout;
	char *envp[UEVENT_NUM_ENVP+1];
	char envbuf[];
};

typedef struct usbStatus{
	volatile uint8_t usbPhoneStatus; /*0: phone not plug 1:phone plug*/
	volatile uint32_t usbSendCount; /*0:Usb sending ok  1:Usb sending pending*/
	volatile uint8_t usbDiskStatus;
}usbStatus;

typedef enum{
	notifyNONE,
	notifyADD,
	notifyREM
}plugStatus;

usbStatus usbLinux;

void notify_set_action(uint8_t action)
{
	usbLinux.usbDiskStatus = action;
}

uint8_t notify_get_action(void)
{
	return usbLinux.usbDiskStatus;
}

void notify_plug(uint8_t action)
{
	struct scsi_head header;
	static uint32_t wtag = 0;

	if(action == notifyNONE){
		return;
	}
	memset(&header, 0, PRO_HDR);
	header.head = SCSI_DEVICE_MAGIC;
	if(action == notifyADD){
		header.ctrid = SCSI_INPUT;
	}else{
		header.ctrid = SCSI_OUTPUT;
	}

	header.wtag = wtag++;
	header.len = 0;

	usStorage_sendHEAD(&header);
}

int special_set_sdcard(char *devpath)
{
	FILE *fp;
	char line[256] = {0}, key[128], value[128];
	
#define SYS_FIRMCONF1		"/etc/firmware"
	
	if(!devpath){
		return -1;
	}
	/*Get Firmware Info*/
	fp = fopen(SYS_FIRMCONF1, "r");
	if(fp == NULL){
		SDEBUGOUT("Open %s Failed:%s\r\n", 
						SYS_FIRMCONF1, strerror(errno));
		return -1;
	}
	while (fgets(line, sizeof(line), fp)) {
		memset(key, 0, sizeof(key));
		memset(value, 0, sizeof(value));		
		if (sscanf(line, "%[^=]=%[^\n ]",
					key, value) != 2)
			continue;
		if(!strcasecmp(key, "SDLOC")){
			fclose(fp);//close fp first
			if(strstr(devpath, value)){
				SDEBUGOUT("Found Special SD Card Location:%s\r\n", value);
				return 1;
			}
			return 0;
		}
	}
	fclose(fp);

	return 0;
}

static int storage_init_netlink_sock(void)
{
	struct sockaddr_nl snl;
	int retval, sockfd = -1;

	memset(&snl, 0x00, sizeof(struct sockaddr_nl));
	snl.nl_family = AF_NETLINK;
	snl.nl_pid = getpid();
	snl.nl_groups = 1;

	sockfd = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
	if (sockfd == -1) {
		SDEBUGOUT("error getting socket: %s\r\n", strerror(errno));
		return -1;
	}
	retval = bind(sockfd, (struct sockaddr *) &snl,
		      sizeof(struct sockaddr_nl));
	if (retval < 0) {
		SDEBUGOUT("bind failed: %s\r\n", strerror(errno));
		close(sockfd);
		return -1;
	}
	/*Triger phone*/
	if(access("/sys/bus/usb/devices/usb1/1-1/1-1.2/uevent", F_OK) == 0){
		system("echo add > /sys/bus/usb/devices/usb1/1-1/1-1.2/uevent &");
	}else if(access("/sys/bus/usb/devices/usb1/1-1/uevent", F_OK) == 0){
		system("echo add > /sys/bus/usb/devices/usb1/1-1/uevent &");
	}
	return sockfd;
}

static struct udevd_uevent_msg *get_msg_from_envbuf(const char *buf, int buf_size)
{
	int bufpos;
	int i;
	struct udevd_uevent_msg *msg;
	int maj = 0;
	int min = 0;

	msg = malloc(sizeof(struct udevd_uevent_msg) + buf_size);
	if (msg == NULL)
		return NULL;
	memset(msg, 0x00, sizeof(struct udevd_uevent_msg) + buf_size);

	/* copy environment buffer and reconstruct envp */
	memcpy(msg->envbuf, buf, buf_size);
	bufpos = 0;
	for (i = 0; (bufpos < buf_size) && (i < UEVENT_NUM_ENVP-2); i++) {
		int keylen;
		char *key;

		key = &msg->envbuf[bufpos];
		keylen = strlen(key);
		msg->envp[i] = key;
		bufpos += keylen + 1;
		SDEBUGOUT( "add '%s' to msg.envp[%i]\r\n", msg->envp[i], i);

		/* remember some keys for further processing */
		if (strncmp(key, "ACTION=", 7) == 0)
			msg->action = &key[7];
		else if (strncmp(key, "DEVPATH=", 8) == 0)
			msg->devpath = &key[8];
		else if (strncmp(key, "SUBSYSTEM=", 10) == 0)
			msg->subsystem = &key[10];		
		else if (strncmp(key, "DEVNAME=", 8) == 0)
			msg->devname = &key[8];
		else if (strncmp(key, "DEVTYPE=", 8) == 0)
			msg->devtype = &key[8];		
		else if (strncmp(key, "SEQNUM=", 7) == 0)
			msg->seqnum = strtoull(&key[7], NULL, 10);
		else if (strncmp(key, "MAJOR=", 6) == 0)
			maj = strtoull(&key[6], NULL, 10);
		else if (strncmp(key, "MINOR=", 6) == 0)
			min = strtoull(&key[6], NULL, 10);
		else if (strncmp(key, "TIMEOUT=", 8) == 0)
			msg->timeout = strtoull(&key[8], NULL, 10);
	}
	msg->devt = makedev(maj, min);
	msg->envp[i++] = "UDEVD_EVENT=1";
	msg->envp[i] = NULL;

	if (msg->devpath == NULL || msg->action == NULL) {
		SDEBUGOUT("DEVPATH or ACTION missing, ignore message\r\n");
		free(msg);
		return NULL;
	}
	return msg;
}

static int storage_handle_diskplug(struct udevd_uevent_msg *msg)
{
	int countTime = 0;
	uint32_t sendCount=0;
	uint8_t diskPlug;
	uint8_t rc;
	
	if(!msg){
		return -1;
	}
#define COUNT_TIME		10	

	if(strncmp(msg->devname, "sd", 2) &&
			strncmp(msg->devname, "mmcblk", 6)){
		SDEBUGOUT("Unknown Disk [%s]\r\n", msg->devname);
		return -1;
	}
	/*handle event*/
	if(!strcasecmp(msg->action, STOR_STR_ADD)){
		char devbuf[128] = {0};
		int fd = -1;

		sprintf(devbuf, "/dev/%s", msg->devname);
		if(access(devbuf, F_OK)){
			mknod(devbuf, S_IFBLK|0644, msg->devt);
		}
		/*Check dev can open*/
		if((fd = open(devbuf, O_RDONLY)) < 0){
			SDEBUGOUT("Open [%s] Failed:%s\r\n", 
					devbuf, strerror(errno));
			return 0;
		}else{
			close(fd);
		}
		/*preread*/
		SDEBUGOUT("ADD Device %d [%s/%s] To Storage List\r\n", 
				msg->id, msg->devname,  msg->devpath);
		notify_set_action(notifyADD);
		if(!strncmp(msg->devname, "mmcblk", 6) ||
					special_set_sdcard(msg->devpath) == 1){
			usDisk_DeviceDetect(USB_CARD, (void*)devbuf);
		}else{
			usDisk_DeviceDetect(USB_DISK, (void*)devbuf);
		}
	}else if(!strcasecmp(msg->action, STOR_STR_REM)){
		SDEBUGOUT("Remove Device [%s/%s] From Storage List\r\n", 
					 msg->devname,  msg->devpath);		
		char devbuf[128] = {0};
		sprintf(devbuf, "/dev/%s", msg->devname);

		if(!strncmp(msg->devname, "mmcblk", 6) ||
					special_set_sdcard(msg->devpath) == 1){
			rc = usDisk_DeviceDisConnect(USB_CARD, devbuf);
		}else{
			rc = usDisk_DeviceDisConnect(USB_DISK, (void*)devbuf);
		}	
		if(rc){			
			return 0;
		}		
		notify_set_action(notifyREM);			
	}else if(!strcasecmp(msg->action, STOR_STR_CHANGE)){		
		char devbuf[128] = {0};		
		int fd = -1;		
		
		SDEBUGOUT("Try To Handle Device %s [%s/%s] Event\r\n", 
					msg->action, msg->devname,  msg->devpath);
		sprintf(devbuf, "/dev/%s", msg->devname);
		if(access(devbuf, F_OK)){
			mknod(devbuf, S_IFBLK|0644, msg->devt);
		}
		/*Check dev can open*/
		if((fd = open(devbuf, O_RDONLY)) < 0){
			/*Remove ID*/
			SDEBUGOUT("We Think it may be Remove action[%s]\r\n", msg->devname);
			if(!strncmp(msg->devname, "mmcblk", 6) ||
						special_set_sdcard(msg->devpath) == 1){
				rc = usDisk_DeviceDisConnect(USB_CARD, devbuf);
			}else{
				rc = usDisk_DeviceDisConnect(USB_DISK, (void*)devbuf);
			}
			if(rc){
				return 0;
			}			
			notify_set_action(notifyREM);			
		}else{
			close(fd);			
			SDEBUGOUT("We Think it may be Add action[%s]\r\n", msg->devname);
			notify_set_action(notifyADD);			
			if(!strncmp(msg->devname, "mmcblk", 6) ||
						special_set_sdcard(msg->devpath) == 1){
				usDisk_DeviceDetect(USB_CARD, (void*)devbuf);
			}else{
				usDisk_DeviceDetect(USB_DISK, (void*)devbuf);
			}
		}		
	}else{
		SDEBUGOUT("Unhandle Device %s [%s/%s] Event\r\n",
				msg->action, msg->devname,	msg->devpath);
		return 0;
	}
#if 0 /*Let main thread to notify app, if we do it, we may be block*/	
	/*confirm try to send to peer wait 500ms*/
	sendCount = usbLinux.usbSendCount;
	while(countTime++ < COUNT_TIME){
		if(usbLinux.usbSendCount != sendCount &&
				notify_get_action() == notifyNONE){
			SDEBUGOUT("Main Thread Notify Disk %s [%s/%s] Event\r\n",
					msg->action, msg->devname, msg->devpath);
			return 0;
		}
		usleep(50000);
	}
	/*May be block receive, we need to notify to peer*/
	diskPlug = usbLinux.usbDiskStatus;
	usbLinux.usbDiskStatus = notifyNONE;
	notify_plug(diskPlug);
#endif	
	SDEBUGOUT("Plug Thread Notify Disk %s [%s/%s] Event\r\n",
			msg->action, msg->devname, msg->devpath);

	return 0;
}

static int storage_handle_phoneplug(struct udevd_uevent_msg *msg)
{
	if(!msg){
		return -1;
	}
	
	if(!strcasecmp(msg->action, STOR_STR_ADD)){
		/*Add phone*/
		/*We just set phone plug in, notify main thread to detect phone*/
		usbLinux.usbPhoneStatus = 1;
		SDEBUGOUT("Phone PlugIN %s [%s/%s] Event\r\n",
				msg->action, msg->devname, msg->devpath);		
	}else if(!strcasecmp(msg->action, STOR_STR_REM)){
		usbLinux.usbPhoneStatus = 0;/*We just set phone plug out status*/
		SDEBUGOUT("Phone PlugOUT %s [%s/%s] Event\r\n",
				msg->action, msg->devname, msg->devpath);		
	}else{
		SDEBUGOUT("Phone Not Handle %s [%s] Event\r\n",
				msg->action,  msg->devpath);		
	}

	return 0;
}

static int storage_action_handle(int sockfd)
{
	char buffer[UEVENT_BUFFER_SIZE*2] = {0};
	struct udevd_uevent_msg *msg;
	int bufpos; 
	ssize_t size;
	char *pos = NULL;

	size = recv(sockfd, &buffer, sizeof(buffer), 0);
	if (size <= 0) {
		SDEBUGOUT("error receiving uevent message: %s\r\n", strerror(errno));
		return -1;
	}
	if ((size_t)size > sizeof(buffer)-1)
		size = sizeof(buffer)-1;
	buffer[size] = '\0';
	/* start of event payload */
	bufpos = strlen(buffer)+1;
	msg = get_msg_from_envbuf(&buffer[bufpos], size-bufpos);
	if (msg == NULL)
		return -1;

	/* validate message */
	pos = strchr(buffer, '@');
	if (pos == NULL) {
		SDEBUGOUT("Invalid uevent '%s'\r\n", buffer);
		free(msg);
		return -1;
	}
	pos[0] = '\0';
	if (msg->action == NULL) {
		SDEBUGOUT("no ACTION in payload found, skip event '%s'\r\n", buffer);
		free(msg);
		return -1;
	}

	if (strcmp(msg->action, buffer) != 0) {
		SDEBUGOUT("ACTION in payload does not match uevent, skip event '%s'\r\n", buffer);
		free(msg);
		return -1;
	}
	if(!msg->subsystem || !msg->devtype){
		SDEBUGOUT("Subsystem/Devtype mismatch [%s/%s]\r\n", 
				msg->subsystem, msg->devtype);
		free(msg);
		return 0;
	}
	if(!strcasecmp(msg->subsystem, STOR_SUBSYS) &&
			!strcasecmp(msg->devtype, STOR_DEVTYPE)){
		storage_handle_diskplug(msg);
	}else if(!strcasecmp(msg->subsystem, PHONE_SUBSYS) &&
				!strcasecmp(msg->devtype, PHONE_DEVTYPE)){
		/*we use HUB_CONFIG to known if it has hub, base on /etc/firmware*/
		if(access(HUB_CONFIG, F_OK) == 0 &&
				strstr(msg->devpath, PHONE_BUS_LOC)){
			SDEBUGOUT("Phone Detect[Have HUB] [%s/%s]\r\n", 
						msg->devname,  msg->devpath);
			storage_handle_phoneplug(msg);
		}else if(access(HUB_CONFIG, F_OK)){
			SDEBUGOUT("Phone Detect[No HUB] [%s/%s]\r\n", 
						msg->devname,  msg->devpath);
			storage_handle_phoneplug(msg);
		}else{
			SDEBUGOUT("No Complete Match DevicePlug %s [%s/%s] Event\r\n",
					msg->action, msg->devname,	msg->devpath);
		}
	}else{
		SDEBUGOUT("Unhandle DevicePlug %s [%s/%s] Event\r\n",
				msg->action, msg->devname,	msg->devpath);
	}

	free(msg);
	return 0;
}

static int handler_sig(void)
{
	struct sigaction act;
	memset(&act, 0, sizeof(struct sigaction));
	act.sa_handler = SIG_IGN;
	sigfillset(&act.sa_mask);
	if ((sigaction(SIGCHLD, &act, NULL) == -1) ||
		(sigaction(SIGTERM, &act, NULL) == -1) ||
		(sigaction(SIGSEGV, &act, NULL) == -1)) {
		SDEBUGOUT("Fail to sigaction[Errmsg:%s]\r\n", strerror(errno));	
	}

	act.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &act, NULL) == -1) {		
		SDEBUGOUT("Fail to signal(SIGPIPE)[Errmsg:%s]\r\n", strerror(errno));	
	}
	return 0;
}

static int daemonize(void)
{
	pid_t pid;
	pid_t sid;

	// already a daemon
	if (getppid() == 1)
		return 0;

	pid = fork();
	if (pid < 0) {
		SDEBUGOUT("fork() failed.\r\n");
		return pid;
	}

	if (pid > 0) {
		// exit parent process
		exit(0);
	}
	
	// Create a new SID for the child process
	sid = setsid();
	if (sid < 0) {
		SDEBUGOUT("setsid() failed.\r\n");
		return -1;
	}

	pid = fork();
	if (pid < 0) {
		SDEBUGOUT("fork() failed (second).\r\n");
		return pid;
	}

	if (pid > 0) {
		// exit parent process
		exit(0);
	}

	// Change the current working directory.
	if ((chdir("/")) < 0) {
		SDEBUGOUT("chdir() failed\r\n");
		return -2;
	}
	// Redirect standard files to /dev/null
	if (!freopen("/dev/null", "r", stdin)) {
		SDEBUGOUT("Redirection of stdin failed.\r\n");
		return -3;
	}
	if (!freopen("/dev/null", "w", stdout)) {
		SDEBUGOUT("Redirection of stdout failed.\r\n");
		return -3;
	}

	return 0;
}

void* vs_main_disk(void *pvParameters)
{
	int plugSocket = 0, cnt;
	struct pollfd fds;
	
	usDisk_DeviceInit(NULL);
	while(1){
		if(plugSocket <= 0 && 
				(plugSocket = storage_init_netlink_sock()) < 0){
			usleep(200000);
			continue;
		}
		fds.fd = plugSocket;
		fds.events = POLLIN;
		cnt = poll(&fds, 1, -1);
		if(cnt < 0){
			if(cnt == -1 && errno == EINTR){
				continue;
			}else{
				SDEBUGOUT("POLL Error:%s.\r\n", strerror(errno));
				usleep(200000);
				continue;
			}
		}else if(cnt == 0){
			/*timeout*/
			continue;
		}
		if(fds.revents & POLLIN){
			/*receive plug information*/
			storage_action_handle(plugSocket);
		}
	}

	return NULL;
}

enum{
  USTORAGE_PHOTO  = 0x01,
};

typedef struct _eventCont{
	uint32_t capEvt;
}eventCont;

eventCont evtMain;
void* vs_event_func(void *pvParameters)
{
	int fd = 0, cnt;
	struct pollfd fds;
	uint32_t event = 0;

#define US_EVENT_DEVNAME	"/dev/vs_ustorage"

	if(access(US_EVENT_DEVNAME, F_OK)){
		SDEBUGOUT("Not Exist %s\n", US_EVENT_DEVNAME);
		return NULL;
	}
	fd = open(US_EVENT_DEVNAME, O_RDONLY);
	if(fd < 0){
		SDEBUGOUT("Open %s failed:%s\n", US_EVENT_DEVNAME, strerror(errno));
		return NULL;
	}
	memset(&evtMain, 0, sizeof(evtMain));
	
	fds.fd = fd;
	fds.events = POLLIN;
	
	while(1){
		cnt = poll(&fds, 1, -1);
		if(cnt < 0){
			if(cnt == -1 && errno == EINTR){
				continue;
			}else{
				SDEBUGOUT("POLL Error:%s.\r\n", strerror(errno));
				usleep(200000);
				continue;
			}
		}
		if(fds.revents & POLLIN){
			/*receive event information*/
			if(read(fd, &event, sizeof(event)) <= 0){
				SDEBUGOUT("Read Error:%s.\n", strerror(errno));
				continue;
			}
			if(event == USTORAGE_PHOTO){
				if(usProtocol_PhoneStatus() != CONN_CONNECTED){
					SDEBUGOUT("Phone Not OK Event IGNORE[%u]\n", evtMain.capEvt);
					continue;
				}
				SDEBUGOUT("Capture Event[%u]\n", evtMain.capEvt);
				evtMain.capEvt++;
			}
		}
	}

	return NULL;
}

void vs_capture_notify(uint8_t force)
{
	static uint32_t preCapevt = 0;
	struct scsi_head header;

	if(preCapevt != evtMain.capEvt || force){
		header.addr = 0;
		header.addr64 = 0;
		header.len = 0;
		header.ctrid = SCSI_CAMERA_CAPTURE;
		header.head = SCSI_DEVICE_MAGIC;				
		header.relag= 0;
		preCapevt = evtMain.capEvt;
		usStorage_sendHEAD(&header);
		SDEBUGOUT("Capture Event[%u] OK\n", preCapevt);
	}
}

int main(int argc, char **argv)
{
	uint8_t phoneStatus, diskStatus;
	int ErrorCode, keepAlive = 0;
	struct scsi_head header;

	if(argc < 2){
		printf("Usage:%s /dev/videox\n", argv[0]);
		return 0;
	}

	if (already_running(PID_FILE)){
		printf("Another \"camera\" is running! Exit!\n");
		return -1;
	}	
	SDEBUGOUT("U-Storage Running.\r\n");
	

	if(argc <= 2){
		daemonize();
		handler_sig();
	}
	if(usCamera_init(argv[1]) != 0){
		return 0;
	}	
	usProtocol_init();
	memset(&usbLinux, 0, sizeof(usbStatus));

#if 1
	pthread_t diskThread;

	if (pthread_create(&diskThread, NULL, vs_main_disk, NULL) != 0) {
		SDEBUGOUT("ERROR: Could not start disk thread!\r\n");
		return -1;
	}
	pthread_t eventThread;
	
	if (pthread_create(&eventThread, NULL, vs_event_func, NULL) != 0) {
		SDEBUGOUT("ERROR: Could not start event thread!\r\n");
		return -1;
	}
#endif
	
	while(1){
		phoneStatus = usbLinux.usbPhoneStatus;
		if (phoneStatus && usProtocol_DeviceDetect(NULL)) {
			SDEBUGOUT("Detect Phone Failed.\r\n");
			usleep(500000);
			continue;
		}else if(!phoneStatus){			
			SDEBUGOUT("Wait Phone Plug IN.\r\n");			
			usProtocol_DeviceDisConnect();
			usleep(500000);
			continue;			
		}
		/*Connect Phone Device*/
		if(usProtocol_ConnectPhone()){
			/*Connect to Phone Failed*/
			usleep(500000);
			continue;
		}
		ErrorCode = usStorage_Handle();	
		if(ErrorCode == PROTOCOL_DISCONNECT){			
			SDEBUGOUT("Destory USB Resource.\r\n");
			usProtocol_DeviceDisConnect();
			keepAlive = 0;
			continue;
		}else if(ErrorCode == PROTOCOL_RTIMOUT){
			if(keepAlive < 2){ //just send 2 timeout package
				memset(&header, 0, sizeof(header));
				header.head = SCSI_DEVICE_MAGIC;
				header.ctrid = SCSI_TIMEOUT;
				//usStorage_sendHEAD(&header);		
				SDEBUGOUT("Send Timeout Package.\r\n");
			}
			keepAlive++;
		}else{
			keepAlive = 0;
		}
		/*catpure picture*/
		vs_capture_notify(0);
		diskStatus = usbLinux.usbDiskStatus;
		if(diskStatus != notifyNONE){
			notify_plug(diskStatus);
			usbLinux.usbDiskStatus = notifyNONE;
		}

		if(usbLinux.usbSendCount++ == 0xFFFFFFFF){
			usbLinux.usbSendCount = 0;
		}
	}
}
#endif //#if defined(NXP_CHIP_18XX)


