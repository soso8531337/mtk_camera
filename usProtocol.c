/*
 * @brief U-Storage Project
 *
 * @note
 * Copyright(C) i4season, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 */

#include <string.h>
#include "usProtocol.h"
#include "usUsb.h"
#include "usSys.h"
#include "protocol.h"
#if defined(NXP_CHIP_18XX)
#include "board.h"
#include "USB.h"
#include <ctype.h>
#include <stdio.h>
#elif defined(LINUX)
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libusb-1.0/libusb.h>
#include <sys/ioctl.h>
#include <errno.h>
#elif defined(GP_CHIP)
#include "USB.h"
#include <ctype.h>
#include <stdio.h>
#endif

/*****************************************************************************
 * Private types/enumerations/variables
 ****************************************************************************/
struct accessory_t {
	uint32_t aoa_version;
	uint16_t vid;
	uint16_t pid;
	char device[256];
	char manufacturer[64];
	char model[64];
	char description[256];
	char version[32];
	char url[1024];
	char serial[128];
};

/*****************************************************************************
 * Public types/enumerations/variables
 ****************************************************************************/
/**********IOS Itunes***********/
#define VID_APPLE 0x5ac
#define PID_RANGE_LOW 0x1290
#define PID_RANGE_MAX 0x12af
/**********Android AOA***********/
/* Product IDs / Vendor IDs */
#define AOA_ACCESSORY_VID		0x18D1	/* Google */
#define AOA_ACCESSORY_PID		0x2D00	/* accessory */
#define AOA_ACCESSORY_ADB_PID		0x2D01	/* accessory + adb */
#define AOA_AUDIO_PID			0x2D02	/* audio */
#define AOA_AUDIO_ADB_PID		0x2D03	/* audio + adb */
#define AOA_ACCESSORY_AUDIO_PID		0x2D04	/* accessory + audio */
#define AOA_ACCESSORY_AUDIO_ADB_PID	0x2D05	/* accessory + audio + adb */
#define INTERFACE_CLASS_AOA 255 // Referrance http://www.usb.org/developers/defined_class/#BaseClassFFh
#define INTERFACE_SUBCLASS_AOA 255
/* Android Open Accessory protocol defines */
#define AOA_GET_PROTOCOL		51
#define AOA_SEND_IDENT			52
#define AOA_START_ACCESSORY		53
#define AOA_REGISTER_HID		54
#define AOA_UNREGISTER_HID		55
#define AOA_SET_HID_REPORT_DESC		56
#define AOA_SEND_HID_EVENT		57
#define AOA_AUDIO_SUPPORT		58
/* String IDs */
#define AOA_STRING_MAN_ID		0
#define AOA_STRING_MOD_ID		1
#define AOA_STRING_DSC_ID		2
#define AOA_STRING_VER_ID		3
#define AOA_STRING_URL_ID		4
#define AOA_STRING_SER_ID		5
/* Android Open Accessory protocol Filter Condition */
#define AOA_FTRANS_CLASS					0xff
#define AOA_FTRANS_SUBCLASS			0xff
#define AOA_FTRANS_PROTOCOL			0x00
/* IOS Itunes protocol Filter Condition */
#define IOS_FTRANS_CLASS					0xff
#define IOS_FTRANS_SUBCLASS			0xfe
#define IOS_FTRANS_PROTOCOL			0x02
/*TCP */
#define TH_FIN        0x01
#define TH_SYN        0x02
#define TH_RST        0x04
#define TH_PUSH       0x08
#define TH_ACK        0x10
#define TH_URG        0x20

/*IOS Default PORT*/
#define IOS_DEFAULT_PORT			5555
#define MPACKET_SIZE			(512)	/*Small packets size limited*/
#define IOS_PROHEADER(X)		(( ((X) < 2) ? 8 : sizeof(struct mux_header))+sizeof(struct tcphdr))
#define IOS_WIN_SIZE				131072 /*Must Not change this value*/
#if defined(NXP_CHIP_18XX)
#define USB_MTU				(32*1024)
uint8_t usProTmpBuffer[MPACKET_SIZE];
#elif defined(GP_CHIP)
#define USB_MTU				(32*1024)
#pragma default_variable_attributes = @ "NON_CACHE"
uint8_t usProTmpBuffer[MPACKET_SIZE];
#pragma default_variable_attributes =
#else
#define USB_MTU				(258*1024)
uint8_t usProTmpBuffer[MPACKET_SIZE];
#endif
//#define USB_MTU				(32*1024) //more than 32K may be error
// max transmission packet size
// libusb fragments these too, but doesn't send ZLPs so we're safe
// but we need to send a ZLP ourselves at the end (see usb-linux.c)
// we're using 3 * 16384 to optimize for the fragmentation
// this results in three URBs per full transfer, 32 USB packets each
// if there are ZLP issues this should make them show up easily too
#define USB_MTU_IOS			(3 * 16384)
#if defined(NXP_CHIP_18XX) || defined(GP_CHIP)	
#define USB_MTU_AOA		(1*16384)
#define IOS_MAX_PACKET		(32*1024)
#elif defined(LINUX)
#define USB_MTU_AOA		(256*16384)
#define IOS_MAX_PACKET		(32728) /*32KB-4-36*/
#endif

//#define IOS_MAGIC_LIMIT		4096	
#define IOS_MAGIC_LIMIT		0xFFFFFFFF	

#define IOS_MAGIC_SIZE			512

#ifndef IPPROTO_TCP
#define IPPROTO_TCP 		6
#endif

enum mux_conn_state {
	CONN_INIT,		//init
	CONN_REFUSED,		// RST received during SYN
	CONN_DYING,			// RST received
	CONN_DEAD,			// being freed; used to prevent infinite recursion between client<->device freeing
	CONN_CONNECTING,	// SYN
	CONN_CONNECTED, 	// SYN/SYNACK/ACK -> active
};

enum mux_protocol {
	MUX_PROTO_VERSION = 0,
	MUX_PROTO_CONTROL = 1,
	MUX_PROTO_SETUP = 2,
	MUX_PROTO_TCP = IPPROTO_TCP,
};


enum{
	PRO_IOS = 1,
	PRO_ANDROID
};

#if defined(DEBUG_ENABLE1)
#define PRODEBUG(...) do {printf("[PRO Mod]");printf(__VA_ARGS__);} while(0)
#else
#define PRODEBUG(...)
#endif

struct tcphdr{
	uint16_t th_sport;         /* source port */
	uint16_t th_dport;         /* destination port */
	uint32_t th_seq;             /* sequence number */
	uint32_t th_ack;             /* acknowledgement number */
	uint8_t th_x2:4;           /* (unused) */
	uint8_t th_off:4;          /* data offset */
	uint8_t th_flags;
	uint16_t th_win;           /* window */
	uint16_t th_sum;           /* checksum */
	uint16_t th_urp;           /* urgent pointer */
};

struct version_header
{
	uint32_t major;
	uint32_t minor;
	uint32_t padding;
};

struct mux_header
{
	uint32_t protocol;
	uint32_t length;
	uint32_t magic;
	uint16_t tx_seq;
	uint16_t rx_seq;
};

typedef struct{
	uint8_t version; /*Protocol version*/
	uint8_t ver_ok;	/*Display the version have get*/
	uint16_t rx_seq; /*itunes protocol rx seq*/
	uint16_t tx_seq;	/*itunes protocol tx seq*/
	uint32_t protlen;	/*itnues protocol total size include itself tcpheader*/
	uint32_t prohlen;	/*itnues protocol handle size*/
	/*tcp connection information*/
	struct{
		uint16_t sport, dport;
		uint32_t tx_seq, tx_ack, tx_acked, tx_win;
		uint32_t rx_seq, rx_recvd, rx_ack, rx_win;
		int flags;
	}tcpinfo;
	uint32_t max_payload;		
	usb_device usbdev;	/*usb interface*/
	/*Buffer used for receive/send data*/
	uint8_t *ib_buf;
	uint32_t ib_used;
	uint32_t ib_capacity;
}mux_itunes;

typedef struct {
	enum mux_conn_state State;		/*U-Storage Status*/
	uint8_t usType;	/*Android or IOS*/
	uint16_t VendorID; /**< Vendor ID for the USB product. */
	uint16_t ProductID; /**< Unique product ID for the USB product. */
	mux_itunes  itunes;
	void *priv;		/*Just used in ios, represent mux_itnues structure*/
}usStorage_info;

/*****************************************************************************
 * Global Var
 ****************************************************************************/
usStorage_info uSinfo;
static struct accessory_t acc_default = {
	.manufacturer = "i4season",
	.model = "U-Storage",
	.description = "U-Storage",
	.version = "1.0",
	.url = "https://www.simicloud.com/download/index.html",
	.serial = "0000000012345678",
};
static int32_t ios_port = IOS_DEFAULT_PORT;

/*This is the global buffer for send usb data or receive usb data
* It is very important
*/

#if defined(NXP_CHIP_18XX)
uint8_t usbBuffer[USB_MTU] __attribute__((section ("USB_RAM1"), zero_init));    // Memory dedicated for the USB Host Controlle;
#elif defined(GP_CHIP)
#pragma default_variable_attributes = @ "NON_CACHE"
uint8_t usbBuffer[USB_MTU];    // Memory dedicated for the USB Host Controlle;
#pragma default_variable_attributes =
#else
uint8_t usbBuffer[USB_MTU];
#endif



/*****************************************************************************
 * Private functions
 ****************************************************************************/
static void usProtocol_iosControlInput(unsigned char *payload, uint32_t payload_length);

static uint16_t find_sport(void)
{
	static uint16_t tcport =1;
	if(tcport == 0xFFFF){
		PRODEBUG("Port Reach To Max Reset it..\r\n");
		tcport = 0;
	}
	return (++tcport);
}
/*
*send_small_packet function not used the global buffer,
*because of it not used so much memory, so we just define internal var
*/
static int send_small_packet(mux_itunes *dev, enum mux_protocol proto, void *header, const void *data, int length)
{
	int hdrlen;
	int res;	
	uint32_t trueSend = 0;

	switch(proto) {
		case MUX_PROTO_VERSION:
			hdrlen = sizeof(struct version_header);
			break;
		case MUX_PROTO_SETUP:
			hdrlen = 0;
			break;
		case MUX_PROTO_TCP:
			hdrlen = sizeof(struct tcphdr);
			break;
		default:
			PRODEBUG("Invalid protocol %d for outgoing packet (hdr %p data %p len %d)\r\n", proto, header, data, length);	
			return -1;
	}
	PRODEBUG("send_packet(0x%x, %p, %p, %d)\r\n", proto, header, data, length);

	int mux_header_size = ((dev->version < 2) ? 8 : sizeof(struct mux_header));

	int total = mux_header_size + hdrlen + length;

	if(total > MPACKET_SIZE){
		PRODEBUG("Tried to send setup packet larger than 512Bytes (hdr %d data %d total %d) to device\r\n", 
							hdrlen, length, total);
		return -1;
	}
	struct mux_header *mhdr = (struct mux_header *)usProTmpBuffer;
	mhdr->protocol = htonl(proto);
	mhdr->length = htonl(total);
	if (dev->version >= 2) {
		mhdr->magic = htonl(0xfeedface);
		if (proto == MUX_PROTO_SETUP) {
			dev->tx_seq = 0;
			dev->rx_seq = 0xFFFF;
		}
		mhdr->tx_seq = htons(dev->tx_seq);
		mhdr->rx_seq = htons(dev->rx_seq);
		dev->tx_seq++;
	}
	memcpy(usProTmpBuffer + mux_header_size, header, hdrlen);
	if(data && length)
		memcpy(usProTmpBuffer + mux_header_size + hdrlen, data, length);
	
	if((res = usUsb_BlukPacketSend(&(dev->usbdev), usProTmpBuffer, 
						total, &trueSend)) != 0 || total != trueSend) {
		PRODEBUG("usb_send failed while sending packet (len %d-->%d) to device: %d\r\n", 
							total, trueSend, res);
		return -res;
	}
	PRODEBUG("sending packet ok(len %d) to device: successful\r\n", total);
	
	return total;
}

/*
*send_big_packet function used the global buffer,
*because of it used so much memory
*/
static int send_big_packet(mux_itunes *dev, enum mux_protocol proto, void *header, const void *data, int length)
{
	uint8_t *buffer = dev->ib_buf;
	uint32_t trueSend = 0;
	int hdrlen;
	int res;

	switch(proto) {
		case MUX_PROTO_VERSION:
			hdrlen = sizeof(struct version_header);
			break;
		case MUX_PROTO_SETUP:
			hdrlen = 0;
			break;
		case MUX_PROTO_TCP:
			hdrlen = sizeof(struct tcphdr);
			break;
		default:
			PRODEBUG("Invalid protocol %d for outgoing packet (hdr %p data %p len %d)\r\n", proto, header, data, length);	
			return -1;
	}
	PRODEBUG("send_packet(0x%x, %p, %p, %d)\r\n", proto, header, data, length);

	int mux_header_size = ((dev->version < 2) ? 8 : sizeof(struct mux_header));

	int total = mux_header_size + hdrlen + length;

	if(total > dev->max_payload){
		PRODEBUG("Tried to send setup packet larger than %dBytes (hdr %d data %d total %d) to device\r\n", 
							dev->max_payload, hdrlen, length, total);
		return -1;
	}
	struct mux_header *mhdr = (struct mux_header *)buffer;
	mhdr->protocol = htonl(proto);
	mhdr->length = htonl(total);
	if (dev->version >= 2) {
		mhdr->magic = htonl(0xfeedface);
		if (proto == MUX_PROTO_SETUP) {
			dev->tx_seq = 0;
			dev->rx_seq = 0xFFFF;
		}
		mhdr->tx_seq = htons(dev->tx_seq);
		mhdr->rx_seq = htons(dev->rx_seq);
		dev->tx_seq++;
	}
	memcpy(buffer + mux_header_size, header, hdrlen);
	if(data == buffer + mux_header_size + hdrlen){
		PRODEBUG("Packet Have In memory(len %d)[%p]\r\n", total, data);
	}else if(data && length){
		memcpy(buffer + mux_header_size + hdrlen, data, length);
	}
	if((res = usUsb_BlukPacketSend(&(dev->usbdev), buffer, 
										total, &trueSend)) != 0 || total != trueSend) {
		PRODEBUG("usb_send failed while sending packet (len %d->%d) to device: %d\r\n", 
							total, trueSend, res);
		return -res;
	}
	PRODEBUG("sending packet ok(len %d) to device: successful\r\n", total);
	
	return total;
}


/*
*send_tcp  will auto select memroy type to send package,
*if package size less than MPACKET_SIZE, it will use send_small_packet
*/
static int send_tcp(mux_itunes *conn, uint8_t flags, const uint8_t *data, int length)
{
	struct tcphdr th;
	uint8_t mux_header_size = ((conn->version < 2) ? 8 : sizeof(struct mux_header));
	int total = mux_header_size + sizeof(struct tcphdr) + length;
	int res;

	if(!conn){
		return -1;
	}
	memset(&th, 0, sizeof(th));
	th.th_sport = htons(conn->tcpinfo.sport);
	th.th_dport = htons(conn->tcpinfo.dport);
	th.th_seq = htonl(conn->tcpinfo.tx_seq);
	th.th_ack = htonl(conn->tcpinfo.tx_ack);
	th.th_flags = flags;
	th.th_off = sizeof(th) / 4;
	th.th_win = htons(conn->tcpinfo.tx_win >> 8);

	PRODEBUG("[OUT]sport=%d dport=%d seq=%d ack=%d flags=0x%x window=%d[%d] len=%d\r\n",
				conn->tcpinfo.sport, conn->tcpinfo.dport, 
				conn->tcpinfo.tx_seq, conn->tcpinfo.tx_ack, flags, 
				conn->tcpinfo.tx_win, conn->tcpinfo.tx_win >> 8, length);

	if(total <= MPACKET_SIZE){
		PRODEBUG("[SmallDATA] Used STACK Memery Send Package[%d]\r\n", total);
		res = send_small_packet(conn, MUX_PROTO_TCP, &th, data, length);
	}else{
		PRODEBUG("[BigDATA] Used Global Memery Send Package[%d]\r\n", total);
		res = send_big_packet(conn, MUX_PROTO_TCP, &th, data, length);
	}
	if(res >= 0) {
		conn->tcpinfo.tx_acked = conn->tcpinfo.tx_ack;
	}
	return res;
}

static int send_tcp_ack(mux_itunes *conn)
{
	if(send_tcp(conn, TH_ACK, NULL, 0) < 0) {
		PRODEBUG("Error sending TCP ACK (%d->%d)", 
				conn->tcpinfo.sport, conn->tcpinfo.dport);
		return -1;
	}
	return 0;
}

static int receive_ack(mux_itunes *uSdev)
{
	uint32_t actual_length = 0;	
	uint8_t *payload = NULL;
	uint32_t payload_length;
	struct tcphdr *th;
	
	if(!uSdev){
		return  PROTOCOL_REPARA;
	}
	if(usUsb_BlukPacketReceiveTmout(&(uSdev->usbdev), usProTmpBuffer, 
								uSdev->usbdev.wMaxPacketSize, &actual_length, 500)){
		PRODEBUG("Receive ios Package ACK Error\r\n");
		return PROTOCOL_REGEN;
	}
	/*decode ack*/
	struct mux_header *mhdr =  (struct mux_header *)usProTmpBuffer;	
	int mux_header_size = ((uSdev->version < 2) ? 8 : sizeof(struct mux_header));
	
	if (uSdev->version >= 2) {
		uSdev->rx_seq = ntohs(mhdr->rx_seq);
	}		
	if(ntohl(mhdr->protocol) == MUX_PROTO_CONTROL){			
		payload = (unsigned char *)(mhdr+1);
		payload_length = actual_length - mux_header_size;
		usProtocol_iosControlInput(payload, payload_length);
		return PROTOCOL_REINVAILD;
	}else if(ntohl(mhdr->protocol) == MUX_PROTO_VERSION){
		PRODEBUG("Receive ios Package MUX_PROTO_VERSION[Error]\r\n");
		return PROTOCOL_REINVAILD;
	}
	if(actual_length != mux_header_size + sizeof(struct tcphdr)){
		PRODEBUG("Receive ios ACK Package Failed[%d/%d]\r\n", 
									actual_length, mux_header_size);
		return PROTOCOL_REGEN;
	}
	/*We need to decode tcp header*/			
	th = (struct tcphdr *)((char*)mhdr+mux_header_size);
	
	uSdev->tcpinfo.rx_seq = ntohl(th->th_seq);
	uSdev->tcpinfo.rx_ack = ntohl(th->th_ack);
	uSdev->tcpinfo.rx_win = ntohs(th->th_win);

	return PROTOCOL_REOK;
}

static void resetReceive(void)
{
	uint8_t rc;
	
	if(uSinfo.usType == PRO_IOS){
		mux_itunes *uSdev = &(uSinfo.itunes);
		int mux_header_size = ((uSdev->version < 2) ? 8 : sizeof(struct mux_header));
		
		while(uSdev->tcpinfo.rx_ack != uSdev->tcpinfo.tx_seq){
			uint32_t actual_length;			
			struct tcphdr *th;
			
			PRODEBUG("Reset Opeartion Need To receive RX_ACK[%u<--->%u]\r\n", 
							uSdev->tcpinfo.rx_ack, uSdev->tcpinfo.tx_seq);
			if((rc = usUsb_BlukPacketReceiveTmout(&(uSdev->usbdev), usProTmpBuffer, 
									uSdev->usbdev.wMaxPacketSize, &actual_length, 1000)) != 0){
				if(rc == USB_DISCNT){
					PRODEBUG("Device Disconncet\r\n");
					break;
				}else if(rc == USB_TMOUT){
					PRODEBUG("Reset Receive Package Finish[%d]\r\n", rc);
				}	
				break;
			}
			struct mux_header *mhdr =  (struct mux_header *)usProTmpBuffer;
			if (uSdev->version >= 2) {
				uSdev->rx_seq = ntohs(mhdr->rx_seq);
			}
			if(actual_length < mux_header_size + sizeof(struct tcphdr)){
				PRODEBUG("Receive ios Package is Too Small TCP Packet[%d/%d]\r\n", 
											actual_length, mux_header_size);
				break;
			}
			uSdev->protlen = ntohl(mhdr->length);
			if(actual_length >= uSdev->protlen){
				PRODEBUG("Fix IOS Receive Packet[%d--->%d]\r\n", 
											actual_length, uSdev->protlen); 		
				uSdev->prohlen = uSdev->protlen;
			}else{
				uSdev->prohlen= actual_length;
			}
			/*We need to decode tcp header*/			
			th = (struct tcphdr *)((char*)mhdr+mux_header_size);

			uSdev->tcpinfo.rx_seq = ntohl(th->th_seq);
			uSdev->tcpinfo.rx_ack = ntohl(th->th_ack);
			uSdev->tcpinfo.rx_win = ntohs(th->th_win);
			PRODEBUG("[IN][RESET]sport=%d dport=%d seq=%d ack=%d flags=0x%x window=%d[%d]len=%u\r\n",
						ntohs(th->th_sport), ntohs(th->th_dport),
						uSdev->tcpinfo.rx_seq, uSdev->tcpinfo.rx_ack, th->th_flags, 
						uSdev->tcpinfo.rx_win << 8, uSdev->tcpinfo.rx_win, uSdev->protlen);
		}
		memset(&(uSinfo.itunes.tcpinfo), 0, sizeof(uSinfo.itunes.tcpinfo));
		uSinfo.itunes.tcpinfo.sport = find_sport();
		uSinfo.itunes.tcpinfo.dport= ios_port;
		uSinfo.itunes.tcpinfo.tx_win = IOS_WIN_SIZE;
		uSinfo.State = CONN_CONNECTING;
		PRODEBUG("Reset Finish iPhone Device[v/p=%d:%d]\r\n", 
					uSinfo.VendorID, uSinfo.ProductID); 	
	}
	usProtocol_SetconnectPhoneStatus(CONN_CONNECTING);
}

static uint8_t usProtocol_iosSendPackage(mux_itunes *uSdev, void *buffer, uint32_t size)
{
	uint8_t *payBuf = (uint8_t *)buffer;
	uint32_t curSnd = 0, havSnd = 0, winCnt = 0, ackTimeOut=0, nakCnt = 0;	
	uint32_t nakSeqnum = 0, offSetHdr = IOS_PROHEADER(2);

	while(havSnd < size){	
		if(size-havSnd >= IOS_MAX_PACKET){
			curSnd = IOS_MAX_PACKET;
		}else{
			curSnd = size-havSnd;
			if(((curSnd+offSetHdr) & 0x1FF) == 0){
				curSnd = curSnd-offSetHdr;				
				PRODEBUG("[IOS] Spite Two Piece[%u]\r\n", curSnd);
			}			
		}
		winCnt = (curSnd&0xFF)?((curSnd>>8)+1):(curSnd>>8);
		nakSeqnum = uSdev->tcpinfo.tx_seq-uSdev->tcpinfo.rx_ack; /*nak sequence number*/
		nakCnt = (nakSeqnum&0xFF)?((nakSeqnum>>8)+1):(nakSeqnum>>8);

		if(nakCnt >= uSdev->tcpinfo.rx_win || 
				winCnt > (uSdev->tcpinfo.rx_win-nakCnt)){
			PRODEBUG("Peer Windows is Full Receive ACK\r\n");		
			if(receive_ack(uSdev) != 0 && ackTimeOut++ > 2){/*wait 125*3ms~250*3ms*/
				PRODEBUG("[IOS] Wait ACK Waste too much Time..\r\n");
				return PROTOCOL_RETRY;
			}
			continue;
		}	
		if(send_tcp(uSdev, TH_ACK, payBuf+havSnd, curSnd) < 0){
			PRODEBUG("usProtocol_iosSendPackage Error:%p Size:%d\r\n", payBuf+havSnd, curSnd);
			return PROTOCOL_REGEN;
		}

		uSdev->tcpinfo.tx_seq += curSnd;		
		havSnd += curSnd;
		PRODEBUG("[IOS]usProtocol_iosSendPackage IOS Package-->%d[WinSize=%d]\r\n", curSnd, uSdev->tcpinfo.rx_win);		
	}

	PRODEBUG("[IOS]usProtocol_iosSendPackage IOS Package Finish[paySize=%d]\r\n", size); 	
	return PROTOCOL_REOK;
}

#if 0
static uint8_t usProtocol_iosSendPackage(mux_itunes *uSdev, void *buffer, uint32_t size)
{	
	uint8_t *tbuffer = (uint8_t *)buffer;
	uint32_t  sndSize = 0, curSize = 0, rx_win = 0;
	
	if(!buffer || !size || !uSdev){
		return PROTOCOL_REPARA;
	}

	/*may be send header to peer, so we decrease PRO_HDR*/
	rx_win = uSdev->tcpinfo.rx_win - PRO_HDR;
	while(curSize < size){
		if(!rx_win){
			PRODEBUG("Peer Windows is full  wait ACK[win:%u]\r\n", uSdev->tcpinfo.rx_win);
			if(receive_ack(uSdev) != 0){
				PRODEBUG("Wait ACK Error[%d]\r\n", rx_win);
			}
			rx_win = uSdev->tcpinfo.rx_win;
			PRODEBUG("Wait ACK Successful[win:%u]\r\n", uSdev->tcpinfo.rx_win);
		}
		if(size-curSize >= IOS_MAX_PACKET){
			sndSize = IOS_MAX_PACKET-IOS_MAGIC_SIZE;
		}else{
			sndSize = size-curSize;
		}
		sndSize = min(sndSize, rx_win);
		rx_win -= sndSize;
		/*Send ios package*/
	#if defined(NXP_CHIP_18XX)
		if(sndSize % IOS_MAGIC_LIMIT == 0){
			PRODEBUG("Need To Divide Package Because Package size is %d\r\n", sndSize);
			if(send_tcp(uSdev, TH_ACK, tbuffer+curSize, sndSize-IOS_MAGIC_SIZE) < 0){
				PRODEBUG("usProtocol_iosSendPackage Error:%p Size:%d\r\n", buffer, sndSize);
				return PROTOCOL_REGEN;
			}		
			uSdev->tcpinfo.tx_seq += (sndSize-IOS_MAGIC_SIZE);
			curSize += (sndSize-IOS_MAGIC_SIZE);
			sndSize = IOS_MAGIC_SIZE;
		}
	#endif	
		if(send_tcp(uSdev, TH_ACK, tbuffer+curSize, sndSize) < 0){
			PRODEBUG("usProtocol_iosSendPackage Error:%p Size:%d\r\n", buffer, sndSize);
			return PROTOCOL_REGEN;
		}
		uSdev->tcpinfo.tx_seq += sndSize;		
		curSize += sndSize;
		PRODEBUG("usProtocol_iosSendPackage Successful:%p Size:%d curSize:%d Win:%d\r\n", 
				buffer, sndSize, curSize, rx_win);		
	}

	return PROTOCOL_REOK;
}
#endif

static uint8_t usProtocol_aoaSendPackage(mux_itunes *uSdev, void *buffer, uint32_t size)
{
	uint32_t actual_length = 0;
	uint32_t already = 0;
	uint8_t *curBuf = NULL;
	uint8_t rc;

	if(!buffer || !size){
		PRODEBUG("usUsb_BlukPacketSend Error Parameter:%p Size:%d\r\n", 
							buffer, size);		
		return PROTOCOL_REGEN;
	}
	/*aoa package send must send 16KB*n*/
	curBuf = (uint8_t *)buffer;
	while(already < size){
		uint32_t sndSize = 0, freeSize = 0;
		freeSize = size-already;
	#if defined(NXP_CHIP_18XX)|| defined(GP_CHIP)	
		if(freeSize >= USB_MTU_AOA){
			sndSize = USB_MTU_AOA-1;
		}else{
			sndSize = freeSize;
		}
	#elif defined(LINUX)
		if(freeSize % 512 == 0){
			sndSize = freeSize-1;
		}else{
			sndSize = freeSize;
		}
	#endif	
		if((rc = usUsb_BlukPacketSend(&(uSdev->usbdev), curBuf+already, 
						sndSize, &actual_length)) != 0){
			if(rc >= USB_DISCNT){
				PRODEBUG("Device Disconncet\r\n");
				return PROTOCOL_DISCONNECT;
			}						
			PRODEBUG("usUsb_BlukPacketSend Error:%p sndSize:%d already:%d\r\n", 
						buffer, sndSize, already);		
			return PROTOCOL_REGEN;
		}
		already+= actual_length;
		PRODEBUG("usUsb_BlukPacketSend Successful:%p sndSize:%d already:%d\r\n", 
					buffer, sndSize, already);		
	}

	return PROTOCOL_REOK;
}

static uint8_t usProtocol_aoaRecvPackage(mux_itunes *uSdev, void **buffer, 
											uint32_t tsize, uint32_t *rsize)
{
	uint8_t *tbuffer = uSdev->ib_buf;
	uint8_t rc;
	uint32_t Recvsize = 0, trueRecv;
	if(!uSdev || !buffer){
		return PROTOCOL_REGEN;
	}
	*rsize = 0;
	if(tsize == 0){
		struct scsi_head *hdr = NULL;
		uint32_t actual_length = 0;
		PRODEBUG("First Receive aoa Package\r\n");
		uSdev->protlen = uSdev->prohlen = 0;
		/*Receive Header*/
		if((rc = usUsb_BlukPacketReceive(&(uSdev->usbdev), tbuffer, 
						uSdev->usbdev.wMaxPacketSize, &actual_length)) != 0){
			if(rc >= USB_DISCNT){
				PRODEBUG("Device Disconncet\r\n");
				return PROTOCOL_DISCONNECT;
			}else if(rc == USB_TMOUT){
				PRODEBUG("Protocol Receive Timeout\r\n");
				return PROTOCOL_RTIMOUT;
			}						
			PRODEBUG("Receive aoa Package First Error\r\n");
			return PROTOCOL_REGEN;
		}
		if(actual_length < PRO_HDR){
			PRODEBUG("Receive aoa Package is Too Small[%d]\r\n", actual_length);
			return PROTOCOL_REGEN;
		}
		hdr = (struct scsi_head *)tbuffer;
		PRODEBUG("Receive First aoa Package Finish-->wtag=%d\r\nctrid=%d\r\naddr=%d\r\nlen=%d\r\nwlun=%d\r\n", 
						hdr->wtag, hdr->ctrid, hdr->addr, hdr->len, hdr->wlun);		
		/*Check Protocol header*/
		if(hdr->head != SCSI_PHONE_MAGIC){
			PRODEBUG("AOA Package Header Error:0x%x\r\n", hdr->head);
			return PROTOCOL_REGEN;
		}
		if(hdr->ctrid & SCSI_WFLAG){
			uSdev->protlen = hdr->len+PRO_HDR;
		}else{
			uSdev->protlen = PRO_HDR;
		}
		if(actual_length >= uSdev->protlen){
			PRODEBUG("Fix AOA Receive Packet[%d--->%d]\r\n", 
										actual_length, uSdev->protlen);			
			uSdev->prohlen = uSdev->protlen;
		}else{
			uSdev->prohlen= actual_length;
		}		
		//uSdev->prohlen = actual_length;		
		tbuffer += actual_length;

		if(uSdev->protlen<= uSdev->max_payload){
			PRODEBUG("We Can Receive it Finish one time[total=%d header.len=%d]..\r\n",
					uSdev->protlen, hdr->len);
			if((hdr->ctrid & SCSI_WFLAG) &&hdr->len){
				while(uSdev->protlen-uSdev->prohlen){
					if((rc = usUsb_BlukPacketReceive(&(uSdev->usbdev), tbuffer, 
							uSdev->protlen-uSdev->prohlen, &actual_length)) != 0){
						if(rc >= USB_DISCNT){
							PRODEBUG("Device Disconncet\r\n");
							return PROTOCOL_DISCONNECT;
						}else if(rc == USB_TMOUT){
							PRODEBUG("Protocol Receive Timeout\r\n");
							return PROTOCOL_RTIMOUT;
						}							
						PRODEBUG("Receive aoa Package Header2 Error\r\n");
						return PROTOCOL_REGEN;
					}
					uSdev->prohlen += actual_length;					
					tbuffer += actual_length;
					PRODEBUG("Part Receive aoa Package[Total:%dBytes Handle:%dByte Now:%dBytes]\r\n",
								uSdev->protlen, uSdev->prohlen, actual_length);
				}
			}else{				
				PRODEBUG("Receive NON Write aoa Package[Total:%dBytes Handle:%dByte Now:%dBytes]\r\n",
							uSdev->protlen, uSdev->prohlen, actual_length);
				uSdev->prohlen = uSdev->protlen;
			}
			*buffer = uSdev->ib_buf;
			*rsize = uSdev->protlen;
			PRODEBUG("Receive aoa Package[once]-->buffer:%p Recvsize:%d Total:%d Handle:%d\r\n",
				uSdev->ib_buf, *rsize, uSdev->protlen, uSdev->prohlen);					
			return PROTOCOL_REOK;
		}
		*rsize = uSdev->prohlen;
	}
	if(!(*rsize) && uSdev->prohlen != tsize){
		PRODEBUG("Mismatch Handle Size %d/%d\r\n", tsize, uSdev->prohlen);
		return PROTOCOL_REGEN;
	}
	/*we just receive 512*number */
	Recvsize = uSdev->max_payload-(tbuffer-uSdev->ib_buf);
	Recvsize = Recvsize-Recvsize%DEF_SECTOR;
	Recvsize = min(Recvsize, (uSdev->protlen-uSdev->prohlen));
	
	PRODEBUG("Prepare Receive aoa Package %d[%d/%d]\r\n", 
					Recvsize, uSdev->protlen, uSdev->prohlen);
	if((rc= usUsb_BlukPacketReceiveStream(&(uSdev->usbdev), tbuffer, 
									Recvsize, &trueRecv)) != 0){
		if(rc >= USB_DISCNT){
			PRODEBUG("Device Disconncet\r\n");
			return PROTOCOL_DISCONNECT;
		}else if(rc == USB_TMOUT){
			PRODEBUG("Protocol Receive Timeout\r\n");
			return PROTOCOL_RTIMOUT;
		}								
		PRODEBUG("Receive aoa Package Stream Error\r\n");
		return PROTOCOL_REGEN;
	}

	*buffer = uSdev->ib_buf;
	*rsize += trueRecv;
	uSdev->prohlen += trueRecv;
	PRODEBUG("Receive aoa Package Finish-->buffer:%p Recvsize:%d Total:%d Handle:%d\r\n",
			uSdev->ib_buf, *rsize, uSdev->protlen, uSdev->prohlen);
	
	return PROTOCOL_REOK;
}

static void usProtocol_iosControlInput(unsigned char *payload, uint32_t payload_length)
{
	if (payload_length > 0) {
		switch (payload[0]) {
		case 3:
			if (payload_length > 1){
				PRODEBUG("usProtocol_iosControlInput ERROR 3:");
				usUsb_PrintStr((uint8_t*)(payload+1), payload_length-1);
			}else{
				PRODEBUG("%s: Error occured, but empty error message", __func__);
			}
			break;
		case 7:
			if (payload_length > 1){
				PRODEBUG("usProtocol_iosControlInput ERROR 7:");
				usUsb_PrintStr((uint8_t*)(payload+1), payload_length-1);
			}
			break;
		default:			
			PRODEBUG("usProtocol_iosControlInput ERROR %d:", payload[0]);			
			usUsb_PrintStr((uint8_t*)(payload+1), payload_length-1);
			break;
		}
	} else {
		PRODEBUG("%s: got a type 1 packet without payload", __func__);
	}
}

static uint8_t usProtocol_iosRecvPackage(mux_itunes *uSdev, void **buffer, 
											uint32_t tsize, uint32_t *rsize)
{
	uint8_t *tbuffer = uSdev->ib_buf;	
	uint8_t *payload = NULL;
	uint32_t sizeSend = 0;	
	uint32_t trueSend = 0;
	uint8_t rc;
	int mux_header_size = ((uSdev->version < 2) ? 8 : sizeof(struct mux_header));
	
	if(!uSdev || !buffer){
		return PROTOCOL_REGEN;
	}
	/*We need to read ios protocol header*/
	*rsize = 0;
	if(tsize == 0 || 
			(!uSdev->protlen && !uSdev->prohlen)){
		uint32_t actual_length = 0;
		struct tcphdr *th;
		uint32_t payload_length, read_length;
		
		PRODEBUG("First Receive ios Package\r\n");
		/*Receive Header*/
		if((rc = usUsb_BlukPacketReceive(&(uSdev->usbdev), tbuffer, 
								uSdev->usbdev.wMaxPacketSize, &actual_length)) != 0){
			if(rc >= USB_DISCNT){
				PRODEBUG("Device Disconncet\r\n");
				return PROTOCOL_DISCONNECT;
			}else if(rc == USB_TMOUT){
				PRODEBUG("Protocol Receive Timeout\r\n");
				return PROTOCOL_RTIMOUT;
			}
			PRODEBUG("First Receive ios Package Error[%d]\r\n", rc);
			return PROTOCOL_REGEN;
		}
		struct mux_header *mhdr =  (struct mux_header *)tbuffer;
		if (uSdev->version >= 2) {
			uSdev->rx_seq = ntohs(mhdr->rx_seq);
		}		
		if(ntohl(mhdr->protocol) == MUX_PROTO_CONTROL){			
			payload = (unsigned char *)(mhdr+1);
			payload_length = actual_length - mux_header_size;
			usProtocol_iosControlInput(payload, payload_length);
			return PROTOCOL_REINVAILD;
		}else if(ntohl(mhdr->protocol) == MUX_PROTO_VERSION){
			PRODEBUG("Receive ios Package MUX_PROTO_VERSION[Error]\r\n");
			return PROTOCOL_REINVAILD;
		}
		if(actual_length < mux_header_size + sizeof(struct tcphdr)){
			PRODEBUG("Receive ios Package is Too Small TCP Packet[%d/%d]\r\n", 
										actual_length, mux_header_size);
			return PROTOCOL_REGEN;
		}
		
		uSdev->protlen  = ntohl(mhdr->length);
		if(actual_length >= uSdev->protlen){
			PRODEBUG("Fix IOS Receive Packet[%d--->%d]\r\n", 
										actual_length, uSdev->protlen);			
			uSdev->prohlen = uSdev->protlen;
		}else{
			uSdev->prohlen= actual_length;
		}
		tbuffer += uSdev->prohlen;
		if(uSdev->protlen<= uSdev->max_payload &&
				(read_length = uSdev->protlen-uSdev->prohlen) > 0){
			if((rc = usUsb_BlukPacketReceive(&(uSdev->usbdev), tbuffer, 
							read_length, &actual_length)) != 0){
				if(rc >= USB_DISCNT){
					PRODEBUG("Device Disconncet\r\n");
					return PROTOCOL_DISCONNECT;
				}else if(rc == USB_TMOUT){
					PRODEBUG("Protocol Receive Timeout\r\n");
					return PROTOCOL_RTIMOUT;
				}							
				PRODEBUG("Receive ios Package Header2 Error\r\n");
				return PROTOCOL_REGEN;
			}
		}else{
			read_length = 0;			
			actual_length=0;
		}
		/*We need to decode tcp header*/			
		th = (struct tcphdr *)((char*)mhdr+mux_header_size);

		uSdev->tcpinfo.rx_seq = ntohl(th->th_seq);
		uSdev->tcpinfo.rx_ack = ntohl(th->th_ack);
		uSdev->tcpinfo.rx_win = ntohs(th->th_win);
		payload = (unsigned char *)(th+1);
		payload_length = uSdev->protlen - mux_header_size- sizeof(struct tcphdr);
		
		PRODEBUG("[IN]sport=%d dport=%d seq=%d ack=%d flags=0x%x window=%d[%d]len=%u\r\n",
					ntohs(th->th_sport), ntohs(th->th_dport),
					uSdev->tcpinfo.rx_seq, uSdev->tcpinfo.rx_ack, th->th_flags, 
					uSdev->tcpinfo.rx_win << 8, uSdev->tcpinfo.rx_win, uSdev->protlen);
		if(th->th_flags & TH_RST ||
				th->th_flags != TH_ACK) {
			/*Connection Reset*/
			PRODEBUG("Connection Reset:\r\n");
			usUsb_PrintStr(payload, payload_length);
			resetReceive();
			return PROTOCOL_REGEN;
		}
		uSdev->prohlen += actual_length;		
		tbuffer += actual_length;
		if(uSdev->prohlen == uSdev->protlen && 
				uSdev->protlen<= uSdev->max_payload){
			*buffer = payload;
			*rsize = payload_length;
			if(uSdev->protlen == (mux_header_size+ sizeof(struct tcphdr))){
				PRODEBUG("We Receive TCP ACK Not Send ACK To Peer..\r\n");
			}else{
				/*Send ACK*/	
				send_tcp_ack(uSdev);
			}
			/*update tx_ack*/
			uSdev->tcpinfo.tx_ack += payload_length;
			PRODEBUG("We Receive it Finish one time..\r\n");
			/*reset protlen and prohlen*/
			uSdev->protlen = uSdev->prohlen = 0;
			return PROTOCOL_REOK;
		}
		*rsize = uSdev->prohlen - mux_header_size- sizeof(struct tcphdr);
	}

	/*we just receive 512*number */
	sizeSend = uSdev->max_payload-(tbuffer-uSdev->ib_buf);
	sizeSend = sizeSend-sizeSend%DEF_SECTOR;
	sizeSend = min(sizeSend, (uSdev->protlen-uSdev->prohlen));
	PRODEBUG("Prepare Receive ios Package %d\r\n", sizeSend);
	if((rc = usUsb_BlukPacketReceiveStream(&(uSdev->usbdev), tbuffer, 
									sizeSend, &trueSend)) != 0){
		if(rc >= USB_DISCNT){
			PRODEBUG("Device Disconncet\r\n");
			return PROTOCOL_DISCONNECT;
		}else if(rc == USB_TMOUT){
			PRODEBUG("Protocol Receive Timeout\r\n");
			return PROTOCOL_RTIMOUT;
		}									
		PRODEBUG("Receive ios Package Stream Error\r\n");
		return PROTOCOL_REGEN;
	}
	if(payload != NULL){
		*buffer = payload;		
		PRODEBUG("We Need To Used payload Point[%p]-->[%p]\r\n", payload, tbuffer);
	}else{
		*buffer = tbuffer;
		PRODEBUG("We Need To Used tbuffer Point[%p]\r\n",tbuffer);		
	}
	*rsize += trueSend;
	uSdev->prohlen += trueSend;
	if(uSdev->prohlen == uSdev->protlen){
		PRODEBUG("We Need To Send ACK[Package Finish ack:%u]\r\n", uSdev->tcpinfo.tx_ack);
		send_tcp_ack(uSdev);
		uSdev->tcpinfo.tx_ack += (uSdev->protlen-sizeof(struct tcphdr) - mux_header_size);		
		/*reset protlen and prohlen*/
		uSdev->protlen = uSdev->prohlen = 0;
	}
	PRODEBUG("Receive IOS Package Finish-->buffer:%p Recvsize:%d Total:%d Handle:%d\r\n",
			uSdev->ib_buf, *rsize, uSdev->protlen, uSdev->prohlen);
	
	return PROTOCOL_REOK;
}


/*****************************************************************************
 * Private functions[Chip]
 ****************************************************************************/
#if defined(NXP_CHIP_18XX) || defined(GP_CHIP)
static uint8_t NXP_FILTERFUNC_AOA_CLASS(void* const CurrentDescriptor)
{
	USB_Descriptor_Header_t* Header = DESCRIPTOR_PCAST(CurrentDescriptor, USB_Descriptor_Header_t);

	if (Header->Type == DTYPE_Interface){
		USB_Descriptor_Interface_t* Interface = DESCRIPTOR_PCAST(CurrentDescriptor, USB_Descriptor_Interface_t);

		if (Interface->Class  == AOA_FTRANS_CLASS){
			return DESCRIPTOR_SEARCH_Found;
		}
	}

	return DESCRIPTOR_SEARCH_NotFound;
}

static uint8_t  NXP_COMPFUNC_AOA_CLASS(void* const CurrentDescriptor)
{
	USB_Descriptor_Header_t* Header = DESCRIPTOR_PCAST(CurrentDescriptor, USB_Descriptor_Header_t);

	if (Header->Type == DTYPE_Interface){
		USB_Descriptor_Interface_t* Interface = DESCRIPTOR_PCAST(CurrentDescriptor, USB_Descriptor_Interface_t);
		if (Interface->Class  == AOA_FTRANS_CLASS&&
				(Interface->SubClass == AOA_FTRANS_SUBCLASS) &&
		   		(Interface->Protocol ==AOA_FTRANS_PROTOCOL)){
			return DESCRIPTOR_SEARCH_Found;
		}
	}

	return DESCRIPTOR_SEARCH_NotFound;
}

static uint8_t NXP_COMPFUNC_IOS_CLASS(void* const CurrentDescriptor)
{
	USB_Descriptor_Header_t* Header = DESCRIPTOR_PCAST(CurrentDescriptor, USB_Descriptor_Header_t);

	if (Header->Type == DTYPE_Interface){
		USB_Descriptor_Interface_t* Interface = DESCRIPTOR_PCAST(CurrentDescriptor, USB_Descriptor_Interface_t);

		if (Interface->Class  == IOS_FTRANS_CLASS&&
				(Interface->SubClass == IOS_FTRANS_SUBCLASS) &&
		   		(Interface->Protocol ==IOS_FTRANS_PROTOCOL)){
			return DESCRIPTOR_SEARCH_Found;
		}
	}

	return DESCRIPTOR_SEARCH_NotFound;
}


uint8_t DCOMP_MS_Host_NextMSInterfaceEndpoint(void* const CurrentDescriptor)
{
	USB_Descriptor_Header_t* Header = DESCRIPTOR_PCAST(CurrentDescriptor, USB_Descriptor_Header_t);

	if (Header->Type == DTYPE_Endpoint){
		USB_Descriptor_Endpoint_t* Endpoint = DESCRIPTOR_PCAST(CurrentDescriptor, USB_Descriptor_Endpoint_t);

		uint8_t EndpointType = (Endpoint->Attributes & EP_TYPE_MASK);

		if ((EndpointType == EP_TYPE_BULK) && (!(Pipe_IsEndpointBound(Endpoint->EndpointAddress)))){
			return DESCRIPTOR_SEARCH_Found;
		}
	}else if (Header->Type == DTYPE_Interface){
		return DESCRIPTOR_SEARCH_Fail;
	}

	return DESCRIPTOR_SEARCH_NotFound;
}

uint8_t usProtocol_SwitchAOAMode(usb_device *usbdev)
{
	USB_ClassInfo_MS_Host_t *MSInterfaceInfo = (USB_ClassInfo_MS_Host_t *)(usbdev->os_priv);
	uint8_t version[2] = {0};
	uint16_t ConfigDescriptorSize = 0;
	uint8_t  *ConfigDescriptorData = usProTmpBuffer, *PtrConfigDescriptorData = NULL;
	
	if(!usbdev){
		PRODEBUG("Parameter Empty..\r\n");
		return PROTOCOL_REPARA;
	}
	memset(&MSInterfaceInfo->State, 0x00, sizeof(MSInterfaceInfo->State));

	if(usUsb_GetDeviceConfigDescriptor(usbdev, 1, &ConfigDescriptorSize, 
						ConfigDescriptorData, MPACKET_SIZE)){
		PRODEBUG("Get Device ConfigDescriptor Error..\r\n");
		return PROTOCOL_REGEN;
	}

	if (DESCRIPTOR_TYPE(ConfigDescriptorData) != DTYPE_Configuration){
		return PROTOCOL_REGEN;
	}
	/*Found Interface Class */
	usUsb_Print(ConfigDescriptorData, ConfigDescriptorSize); 
	/*Must Set to var, the funciton will change the point*/
	PtrConfigDescriptorData = ConfigDescriptorData;
	if(((USB_StdDescriptor_Configuration_Header_t*)ConfigDescriptorData)->bNumInterfaces == 1){
		printf("Attached Device Only Have One Interface, Try it.\r\n");
	}else if (USB_GetNextDescriptorComp(&ConfigDescriptorSize, (void **)&PtrConfigDescriptorData,
				NXP_FILTERFUNC_AOA_CLASS) != DESCRIPTOR_SEARCH_COMP_Found){		
		printf("Attached Device Not a Valid AOA Device[NO 0xff Interface].\r\n");
		return PROTOCOL_REGEN;
	}
	/*Set AOA*/
	if(usUsb_SendControlRequest(usbdev, REQDIR_DEVICETOHOST|REQTYPE_VENDOR, 
					AOA_GET_PROTOCOL, 0, 0, 2, version)){
		PRODEBUG("Get AOA Protocol Version Failed.\r\n");
		return PROTOCOL_REGEN;
	}
	acc_default.aoa_version = ((version[1] << 8) | version[0]);
	PRODEBUG("Found Device supports AOA %d.0!\r\n", acc_default.aoa_version);
	/* In case of a no_app accessory, the version must be >= 2 */
	if((acc_default.aoa_version < 2) && !strlen(acc_default.manufacturer)) {
		PRODEBUG("Connecting without an Android App only for AOA 2.0.\r\n");
		return PROTOCOL_REGEN;
	}
	if(strlen(acc_default.manufacturer)) {
		PRODEBUG("sending manufacturer: %s\r\n", acc_default.manufacturer);
		if(usUsb_SendControlRequest(usbdev, REQDIR_HOSTTODEVICE|REQTYPE_VENDOR,
						AOA_SEND_IDENT, 0, AOA_STRING_MAN_ID, 
					strlen(acc_default.manufacturer) + 1, (uint8_t *)acc_default.manufacturer)){
			PRODEBUG("Get AOA Protocol Version Failed.\r\n");
			return PROTOCOL_REGEN;
		}
	}
	if(strlen(acc_default.model)) {
		PRODEBUG("sending model: %s\r\n", acc_default.model);
		if(usUsb_SendControlRequest(usbdev, REQDIR_HOSTTODEVICE|REQTYPE_VENDOR,
								AOA_SEND_IDENT, 0, AOA_STRING_MOD_ID, 
							strlen(acc_default.model) + 1, (uint8_t *)acc_default.model)){
			PRODEBUG("Could not Set AOA model.\r\n");
			return PROTOCOL_REGEN;
		}
	}

	if(strlen(acc_default.description)){
		PRODEBUG("sending description: %s\r\n", acc_default.description);
		if(usUsb_SendControlRequest(usbdev, REQDIR_HOSTTODEVICE|REQTYPE_VENDOR,
							AOA_SEND_IDENT, 0, AOA_STRING_DSC_ID, 
							strlen(acc_default.description) + 1, (uint8_t *)acc_default.description)){
			PRODEBUG("Could not Set AOA description.\r\n");
			return PROTOCOL_REGEN;
		}
	}
	
	if(strlen(acc_default.version)){
		PRODEBUG("sending version string: %s\r\n", acc_default.version);
		if(usUsb_SendControlRequest(usbdev, REQDIR_HOSTTODEVICE|REQTYPE_VENDOR,
							AOA_SEND_IDENT, 0, AOA_STRING_VER_ID, 
							strlen(acc_default.version) + 1, (uint8_t *)acc_default.version)){
			PRODEBUG("Could not Set AOA version.\r\n");
			return PROTOCOL_REGEN;
		}
	}
	
	if(strlen(acc_default.url)){
		PRODEBUG("sending url string: %s\r\n", acc_default.url);
		if(usUsb_SendControlRequest(usbdev, REQDIR_HOSTTODEVICE|REQTYPE_VENDOR,
							AOA_SEND_IDENT, 0, AOA_STRING_URL_ID, 
							strlen(acc_default.url) + 1, (uint8_t *)acc_default.url)){
			PRODEBUG("Could not Set AOA url.\r\n");
			return PROTOCOL_REGEN;
		}
	}
	if(strlen(acc_default.serial)){
		PRODEBUG("sending serial number: %s\r\n", acc_default.serial);
		if(usUsb_SendControlRequest(usbdev, REQDIR_HOSTTODEVICE|REQTYPE_VENDOR,
							AOA_SEND_IDENT, 0, AOA_STRING_SER_ID, 
							strlen(acc_default.serial) + 1, (uint8_t *)acc_default.serial)){
			PRODEBUG("Could not Set AOA serial.\r\n");
			return PROTOCOL_REGEN;
		}
	}
	if(usUsb_SendControlRequest(usbdev, REQDIR_HOSTTODEVICE|REQTYPE_VENDOR,
						AOA_START_ACCESSORY, 0, 0, 0, NULL)){
		PRODEBUG("Could not Start AOA.\r\n");
		return PROTOCOL_REGEN;
	}

	PRODEBUG("Start AOA Successful  Android Will Reconnect\r\n");
	return PROTOCOL_REOK;
}
#elif defined(LINUX)
typedef enum{
	OCTL_DIS_USB_SW = 0X01,
	OCTL_ENABLE_USB_SW,
} USBPowerValue;
#define USB_POWER_CONFIG		"/dev/vs_control"
static uint8_t LINUX_USBPowerControl(USBPowerValue value)
{
	int fd;

	if(access(USB_POWER_CONFIG, F_OK)){
		PRODEBUG("No Need To Setup USB Power[%s Not Exist]\r\n", USB_POWER_CONFIG);
		return PROTOCOL_REOK;
	}

	fd= open(USB_POWER_CONFIG, O_RDWR | O_NONBLOCK);
	if (fd < 0 && errno == EROFS)
		fd = open(USB_POWER_CONFIG, O_RDONLY | O_NONBLOCK);
	if (fd<0){
		PRODEBUG("Open %s Failed:%s", USB_POWER_CONFIG, strerror(errno));
		return PROTOCOL_REGEN; 
	}
	if(ioctl(fd, value)){
		PRODEBUG("IOCTL Failed:%s", strerror(errno));
	}
	close(fd);
	PRODEBUG("IOCTL Successful:%d", value);
	return PROTOCOL_REOK;
}

static uint8_t LINUX_SwitchAOAMode(libusb_device* dev)
{
	int res=-1, j;
	libusb_device_handle *handle;
	struct libusb_config_descriptor *config;
	uint8_t version[2];
	uint8_t bus = libusb_get_bus_number(dev);
	uint8_t address = libusb_get_device_address(dev);

	// potentially blocking operations follow; they will only run when new devices are detected, which is acceptable
	if((res = libusb_open(dev, &handle)) != 0) {
		PRODEBUG("Could not open device %d-%d: %d\n", bus, address, res);
		return PROTOCOL_REGEN;
	}
	if((res = libusb_get_active_config_descriptor(dev, &config)) != 0) {
		PRODEBUG("Could not get configuration descriptor for device %d-%d: %d\n", bus, address, res);
		libusb_close(handle);
		return PROTOCOL_REGEN;
	}
	
	for(j=0; j<config->bNumInterfaces; j++) {
		const struct libusb_interface_descriptor *intf = &config->interface[j].altsetting[0];
		/*We Just limit InterfaceClass, limit InterfaceSubClass may be lost sanxing huawei device*/
		if(config->bNumInterfaces > 1 &&
					intf->bInterfaceClass != INTERFACE_CLASS_AOA){
			continue;
		}
		if(intf->bInterfaceClass == LIBUSB_CLASS_HUB){
			continue;
		}		
		/*Before switch AOA Mode, we need to notify kernel*/
		LINUX_USBPowerControl(OCTL_DIS_USB_SW);
		/* Now asking if device supports Android Open Accessory protocol */
		res = libusb_control_transfer(handle,
					      LIBUSB_ENDPOINT_IN |
					      LIBUSB_REQUEST_TYPE_VENDOR,
					      AOA_GET_PROTOCOL, 0, 0, version,
					      sizeof(version), 0);
		if (res < 0) {
			PRODEBUG("Could not getting AOA protocol %d-%d: %d\n", bus, address, res);
			libusb_free_config_descriptor(config);
			libusb_close(handle);
			return PROTOCOL_REGEN;
		}else{
			acc_default.aoa_version = ((version[1] << 8) | version[0]);
			PRODEBUG("Device[%d-%d] supports AOA %d.0!\n", bus, address, acc_default.aoa_version);
		}
		/* In case of a no_app accessory, the version must be >= 2 */
		if((acc_default.aoa_version < 2) && !strlen(acc_default.manufacturer)) {
			PRODEBUG("Connecting without an Android App only for AOA 2.0[%d-%d]\n", bus,address);
			libusb_free_config_descriptor(config);
			libusb_close(handle);
			return PROTOCOL_REGEN;
		}
		if(strlen(acc_default.manufacturer)) {
			PRODEBUG("sending manufacturer: %s\n", acc_default.manufacturer);
			res = libusb_control_transfer(handle,
						  LIBUSB_ENDPOINT_OUT
						  | LIBUSB_REQUEST_TYPE_VENDOR,
						  AOA_SEND_IDENT, 0,
						  AOA_STRING_MAN_ID,
						  (uint8_t *)acc_default.manufacturer,
						  strlen(acc_default.manufacturer) + 1, 0);
			if(res < 0){
				PRODEBUG("Could not Set AOA manufacturer %d-%d: %d\n", bus, address, res);
				libusb_free_config_descriptor(config);
				libusb_close(handle);
				return PROTOCOL_REGEN;
			}
		}
		if(strlen(acc_default.model)) {
			PRODEBUG("sending model: %s\n", acc_default.model);
			res = libusb_control_transfer(handle,
						  LIBUSB_ENDPOINT_OUT
						  | LIBUSB_REQUEST_TYPE_VENDOR,
						  AOA_SEND_IDENT, 0,
						  AOA_STRING_MOD_ID,
						  (uint8_t *)acc_default.model,
						  strlen(acc_default.model) + 1, 0);
			if(res < 0){
				PRODEBUG("Could not Set AOA model %d-%d: %d\n", bus, address, res);
				libusb_free_config_descriptor(config);
				libusb_close(handle);
				return PROTOCOL_REGEN;
			}
		}
		
		if(strlen(acc_default.description)){
			PRODEBUG("sending description: %s\n", acc_default.description);
			res = libusb_control_transfer(handle,
						  LIBUSB_ENDPOINT_OUT
						  | LIBUSB_REQUEST_TYPE_VENDOR,
						  AOA_SEND_IDENT, 0,
						  AOA_STRING_DSC_ID,
						  (uint8_t *)acc_default.description,
						  strlen(acc_default.description) + 1, 0);
			if(res < 0){
				PRODEBUG("Could not Set AOA description %d-%d: %d\n", bus, address, res);
				libusb_free_config_descriptor(config);
				libusb_close(handle);
				return PROTOCOL_REGEN;
			}
		}
		
		if(strlen(acc_default.version)){
			PRODEBUG("sending version string: %s\n", acc_default.version);
			res = libusb_control_transfer(handle,
						  LIBUSB_ENDPOINT_OUT
						  | LIBUSB_REQUEST_TYPE_VENDOR,
						  AOA_SEND_IDENT, 0,
						  AOA_STRING_VER_ID,
						  (uint8_t *)acc_default.version,
						  strlen(acc_default.version) + 1, 0);
			if(res < 0){
				PRODEBUG("Could not Set AOA version %d-%d: %d\n", bus, address, res);
				libusb_free_config_descriptor(config);
				libusb_close(handle);
				return PROTOCOL_REGEN;
			}
		}
		
		if(strlen(acc_default.url)){
			PRODEBUG("sending url string: %s\n", acc_default.url);
			res = libusb_control_transfer(handle,
						  LIBUSB_ENDPOINT_OUT
						  | LIBUSB_REQUEST_TYPE_VENDOR,
						  AOA_SEND_IDENT, 0,
						  AOA_STRING_URL_ID,
						  (uint8_t *)acc_default.url,
						  strlen(acc_default.url) + 1, 0);
			if(res < 0){
				PRODEBUG("Could not Set AOA url %d-%d: %d\n", bus, address, res);
				libusb_free_config_descriptor(config);
				libusb_close(handle);
				return PROTOCOL_REGEN;
			}
		}

		if(strlen(acc_default.serial)){
			PRODEBUG("sending serial number: %s\n", acc_default.serial);
			res = libusb_control_transfer(handle,
						  LIBUSB_ENDPOINT_OUT
						  | LIBUSB_REQUEST_TYPE_VENDOR,
						  AOA_SEND_IDENT, 0,
						  AOA_STRING_SER_ID,
						  (uint8_t *)acc_default.serial,
						  strlen(acc_default.serial) + 1, 0);
			if(res < 0){
				PRODEBUG("Could not Set AOA serial %d-%d: %d\n", bus, address, res);
				libusb_free_config_descriptor(config);
				libusb_close(handle);
				return PROTOCOL_REGEN;
			}
		}
		res = libusb_control_transfer(handle,
					  LIBUSB_ENDPOINT_OUT |
					  LIBUSB_REQUEST_TYPE_VENDOR,
					  AOA_START_ACCESSORY, 0, 0, NULL, 0, 0);
		if(res < 0){
			PRODEBUG("Could not Start AOA %d-%d: %d\n", bus, address, res);
			libusb_free_config_descriptor(config);
			libusb_close(handle);
			return PROTOCOL_REGEN;
		}
		PRODEBUG("Turning the device %d-%d in Accessory mode Successful\n", bus, address);
		libusb_free_config_descriptor(config);
		libusb_close(handle);
		return PROTOCOL_REOK;
	}	
	
	libusb_free_config_descriptor(config);
	libusb_close(handle);
	PRODEBUG("No Found Android Device in %d-%d\n", bus, address);

	return PROTOCOL_REGEN;
}
#endif
/*****************************************************************************
 * Public functions
 ****************************************************************************/
uint8_t usProtocol_GetIOSVersion(mux_itunes *uSdev)
{
	struct version_header rvh, *vh;	
	uint8_t mux_header_size; 
	uint32_t trueRecv = 0;
	
	if(!uSdev){
		return PROTOCOL_REPARA;
	}
	if(uSdev->ver_ok == 1){
		PRODEBUG("IOS Version Have Get[ver.%d]\r\n", uSdev->version);
		return PROTOCOL_REOK;
	}
	uSdev->version = 0;
	uSdev->tx_seq = uSdev->rx_seq = uSdev->protlen = 0;
	/*confirm source dest port*/
	memset(&(uSdev->tcpinfo), 0, sizeof(uSdev->tcpinfo));
	uSdev->tcpinfo.sport = find_sport();
	uSdev->tcpinfo.dport= ios_port;
	uSdev->tcpinfo.tx_win = IOS_WIN_SIZE;	
	/*Begin to conncet to iPhone*/
	/*1.request PROTOCOL_VERSION*/
	rvh.major = htonl(2);
	rvh.minor = htonl(0);
	rvh.padding = 0;
	if(send_small_packet(uSdev, MUX_PROTO_VERSION, &rvh, NULL, 0) < 0) {
		PRODEBUG("Error sending version request packet to device\r\n");
		return PROTOCOL_REINVAILD;
	}
	/*Send Successful receive response*/
	mux_header_size = ((uSdev->version < 2) ? 8 : sizeof(struct mux_header));
	if(usUsb_BlukPacketReceive(&(uSdev->usbdev), usProTmpBuffer,  sizeof(usProTmpBuffer), &trueRecv)){
		PRODEBUG("Error receive version request packet from phone\r\n");
		return PROTOCOL_REINVAILD;
	}
	vh = (struct version_header *)(usProTmpBuffer+mux_header_size);
	vh->major = ntohl(vh->major);
	vh->minor = ntohl(vh->minor);
	if(vh->major != 2 && vh->major != 1) {
		PRODEBUG("Device has unknown version %d.%d\r\n", vh->major, vh->minor);
		return PROTOCOL_REINVAILD;
	}
	uSdev->version = vh->major;

	if (uSdev->version >= 2 &&
		(send_small_packet(uSdev, MUX_PROTO_SETUP, NULL, "\x07", 1)) < 0) {
		PRODEBUG("iPhone Send SetUP Package Failed\r\n");
		return PROTOCOL_REINVAILD;
	}
	
	uSdev->ver_ok = 1;
	PRODEBUG("IOS Version Get Successful[ver.%d]\r\n", uSdev->version);
	
	return PROTOCOL_REOK;
}

uint8_t usProtocol_ConnectIOSPhone(mux_itunes *uSdev)
{
	uint8_t mux_header_size; 
	uint32_t trueRecv = 0;
	struct tcphdr *th;
	uint32_t protocol;

	if(!uSdev){
		return PROTOCOL_REPARA;
	}

	if(usProtocol_GetIOSVersion(uSdev)){
		return PROTOCOL_REINVAILD;
	}

	PRODEBUG("Connected to v%d device\r\n", uSdev->version);
	/*Send TH_SYNC*/
	if(send_tcp(uSdev, TH_SYN, NULL, 0) < 0){
		PRODEBUG("Error sending TCP SYN to device (%d->%d)\r\n", 
				uSdev->tcpinfo.sport, uSdev->tcpinfo.dport);
		return PROTOCOL_REINVAILD; //bleh
	}
	/*Wait TH_ACK*/
	if(usUsb_BlukPacketReceive(&(uSdev->usbdev), usProTmpBuffer, sizeof(usProTmpBuffer), &trueRecv)){
		PRODEBUG("Error receive tcp ack response packet from phone\r\n");
		return PROTOCOL_REINVAILD;
	}
	PRODEBUG("ACK Step1: Receive (%dBytes)\r\n", trueRecv);	
	struct mux_header *mhdr = (struct mux_header *)usProTmpBuffer;
	if(ntohl(mhdr->length) > (sizeof(usProTmpBuffer))){
		PRODEBUG("Setup Package is More than %u/%dByte\r\n", 
				ntohl(mhdr->length), sizeof(usProTmpBuffer));
		return PROTOCOL_REINVAILD;
	}
	/*Decode Package*/
	protocol = ntohl(mhdr->protocol);
	switch(protocol) {
		case MUX_PROTO_VERSION:
			PRODEBUG("MUX_PROTO_VERSION\r\n");
			return PROTOCOL_REINVAILD;
		case MUX_PROTO_CONTROL:
			PRODEBUG("Receive MUX_PROTO_CONTROL[SameThing Happen] Continue Read TCP Packet...\r\n");		
			if(usUsb_BlukPacketReceive(&(uSdev->usbdev), usProTmpBuffer, sizeof(usProTmpBuffer), &trueRecv)){
				PRODEBUG("Error receive tcp ack response packet from phone\r\n");
				return PROTOCOL_REINVAILD;
			}
			mhdr = (struct mux_header *)usProTmpBuffer;
			/*Not Break continue to decode*/
		case MUX_PROTO_TCP:			
			mux_header_size = ((uSdev->version < 2) ? 8 : sizeof(struct mux_header));
			th = (struct tcphdr *)((char*)mhdr+mux_header_size);
			uSdev->tcpinfo.rx_seq = ntohl(th->th_seq);
			uSdev->tcpinfo.rx_ack = ntohl(th->th_ack);
			uSdev->tcpinfo.rx_win = ntohs(th->th_win);
			PRODEBUG("[IN]sport=%d dport=%d seq=%d ack=%d flags=0x%x window=%d[%d] len=%u\r\n",
						ntohs(th->th_sport), ntohs(th->th_dport), 
						uSdev->tcpinfo.rx_seq, uSdev->tcpinfo.rx_ack, th->th_flags, 
						uSdev->tcpinfo.rx_win << 8, uSdev->tcpinfo.rx_win, trueRecv);

			
			if(th->th_flags != (TH_SYN|TH_ACK)) {
				if(th->th_flags & TH_RST){
					PRODEBUG("Connection refused by device(%d->%d)\r\n", 
								uSdev->tcpinfo.sport , uSdev->tcpinfo.dport);
				}		
				return PROTOCOL_REGEN;
			} else {			
				uSdev->tcpinfo.tx_seq++;
				uSdev->tcpinfo.tx_ack++;
				uSdev->tcpinfo.rx_recvd = uSdev->tcpinfo.rx_seq;
				if(send_tcp(uSdev, TH_ACK, NULL, 0) < 0) {
					PRODEBUG("Error sending TCP ACK to device(%d->%d)\r\n", 
						uSdev->tcpinfo.sport , uSdev->tcpinfo.dport);
					return PROTOCOL_REGEN;
				}
			}	
			break;
		default:
			PRODEBUG("Incoming packet has unknown protocol 0x%x)", ntohl(mhdr->protocol));
			return PROTOCOL_REGEN;
	}

	PRODEBUG("Successful Connect To iPhone(%d->%d)\r\n", 
		uSdev->tcpinfo.sport , uSdev->tcpinfo.dport);
	
	return PROTOCOL_REOK;
}

uint8_t usProtocol_SetconnectPhoneStatus(uint8_t status)
{
	uSinfo.State = (enum mux_conn_state)status;
	return PROTOCOL_REOK;
}
uint8_t usProtocol_ConnectPhone(void)
{
	if(uSinfo.State  == CONN_INIT){
		PRODEBUG("Error Connect State[INIT]\r\n");
		return PROTOCOL_REINVAILD;
	}else if(uSinfo.State  == CONN_CONNECTED){
		PRODEBUG("Connect State[CONN_CONNECTED]\r\n");
		return PROTOCOL_REOK;
	}
	if(uSinfo.usType == PRO_ANDROID && 
			uSinfo.State == CONN_CONNECTING){
		uSinfo.State = CONN_CONNECTED;
		PRODEBUG("Androd Device[v/p=%d:%d] Connected\r\n", 
					uSinfo.VendorID, uSinfo.ProductID);
		return PROTOCOL_REOK;
	}
	/*Connect iPhone, we need to the large buffer to send packet*/
	if(usProtocol_ConnectIOSPhone(&(uSinfo.itunes))){
		PRODEBUG("iPhone Device[v/p=%d:%d] Connect Failed\r\n", 
					uSinfo.VendorID, uSinfo.ProductID);
		return PROTOCOL_REINVAILD;
	}
	uSinfo.itunes.max_payload = uSinfo.itunes.ib_capacity -  sizeof(struct mux_header) - sizeof(struct tcphdr);
	uSinfo.State = CONN_CONNECTED;
	PRODEBUG("iPhone Device[v/p=%d:%d] Connected\r\n", 
				uSinfo.VendorID, uSinfo.ProductID);
	
	return PROTOCOL_REOK;	
}
#if defined(NXP_CHIP_18XX) || defined(GP_CHIP)
uint8_t usProtocol_DeviceDetect(void *os_priv)
{
	USB_StdDesDevice_t DeviceDescriptorData;
	int8_t PhoneType = -1;
	nxp_clminface nxpcall;

	if(uSinfo.State== CONN_CONNECTING){
		return PROTOCOL_REOK;
	}
	usb_device *usbdev = &(uSinfo.itunes.usbdev);
	/*We need to memset*/
	memset(&uSinfo, 0, sizeof(uSinfo));
	/*set os_priv*/
	usbdev->usb_type =  USB_PHONE;
	usUsb_Init(usbdev, os_priv);
	/*GEt device description*/
	memset(&DeviceDescriptorData, 0, sizeof(USB_StdDesDevice_t));
	if(usUsb_GetDeviceDescriptor(usbdev, &DeviceDescriptorData)){
		PRODEBUG("usUusb_GetDeviceDescriptor Failed\r\n");
		return PROTOCOL_REGEN;
	}
	if(DeviceDescriptorData.idVendor == VID_APPLE &&
		(DeviceDescriptorData.idProduct>= PID_RANGE_LOW 
			&& DeviceDescriptorData.idProduct <= PID_RANGE_MAX)){
		/*iPhone Device*/
		PRODEBUG("Found iPhone Device[v/p=%d:%d].\r\n", 
				DeviceDescriptorData.idVendor, DeviceDescriptorData.idProduct);
		/*Set Type*/
		PhoneType = PRO_IOS;
		/*Set callback*/
		nxpcall.callbackInterface = NXP_COMPFUNC_IOS_CLASS;
		nxpcall.callbackEndpoint= DCOMP_MS_Host_NextMSInterfaceEndpoint;
		
	}else if(DeviceDescriptorData.idVendor == AOA_ACCESSORY_VID &&
		(DeviceDescriptorData.idProduct >= AOA_ACCESSORY_PID 
			&& DeviceDescriptorData.idProduct <= AOA_ACCESSORY_AUDIO_ADB_PID)){
		/*Android Device*/
		PRODEBUG("Found Android Device[v/p=%d:%d].\r\n", 
				DeviceDescriptorData.idVendor, DeviceDescriptorData.idProduct);
		/*Set Type*/
		PhoneType = PRO_ANDROID;		
		/*Set callback*/
		nxpcall.callbackInterface = NXP_COMPFUNC_AOA_CLASS;
		nxpcall.callbackEndpoint= DCOMP_MS_Host_NextMSInterfaceEndpoint;
	}else{
		/*Switch to AOA Mode*/
		return usProtocol_SwitchAOAMode(usbdev);
	}
	/*Claim Interface*/
	nxpcall.bNumConfigurations = DeviceDescriptorData.bNumConfigurations;
	if(usUsb_ClaimInterface(usbdev, &nxpcall)){
		PRODEBUG("Attached Device Not a Valid PhoneDevice.\r\n");
		return PROTOCOL_REINVAILD;
	}
	
	/*Set Global var*/
	uSinfo.usType = PhoneType;
	uSinfo.VendorID = DeviceDescriptorData.idVendor;
	uSinfo.ProductID = DeviceDescriptorData.idProduct;
	uSinfo.State = CONN_CONNECTING;

	/*We need to set global buffer to uSinfo*/
	uSinfo.itunes.ib_buf = usbBuffer;
	uSinfo.itunes.ib_capacity = sizeof(usbBuffer);
	if(uSinfo.usType == PRO_IOS){
		uSinfo.itunes.max_payload = uSinfo.itunes.ib_capacity - IOS_PROHEADER(uSinfo.itunes.version);
	}else{
		uSinfo.itunes.max_payload = uSinfo.itunes.ib_capacity;
	}
	PRODEBUG("Phone Change to CONNCETING State.\r\n");
	
	return PROTOCOL_REOK;
}

uint8_t usProtocol_DeviceDisConnect(void)
{
	uint8_t typePhone = uSinfo.usType;
		
	//memset(&uSinfo, 0, sizeof(uSinfo));
	uSinfo.State = CONN_INIT;
	printf("Just Set the Connect State, if not, the core may be crash.[%d]\r\n", uSinfo.State);
	return typePhone;
}

uint8_t usProtocol_init(void)
{

#if defined(USTOR_ZEBAO)
	strcpy(acc_default.manufacturer, "Ravpower");
	strcpy(acc_default.model, "RP-UM001");
	strcpy(acc_default.description, "RP-UM001");
	strcpy(acc_default.version, "2.000.000");
#endif

	/*Print Special configuration*/
	printf("Configuration:\r\n\tVendor:%s\r\n\tMode:%s\r\n\tDescription:%s\r\n"
				"\tVersion:%s\r\n\tURL:%s\r\n\tSerical:%s\r\n\tiosPort:%d\r\n", acc_default.manufacturer,
			acc_default.model, acc_default.description, acc_default.version, acc_default.url,
			acc_default.serial, ios_port);

	memset(&uSinfo, 0, sizeof(uSinfo));

	return PROTOCOL_REOK;
}

#elif defined(LINUX)

/*Global Var*/
usb_device usb_phone;

uint8_t usProtocol_DeviceDisConnect(void)
{
	if(usb_phone.os_priv){		
		libusb_device_handle *dev = (libusb_device_handle *)usb_phone.os_priv;
		PRODEBUG("Destory Usb Resource\r\n");

		libusb_release_interface(dev, usb_phone.interface);
		libusb_close(dev);
		usb_phone.os_priv = NULL;	
	}
	memset(&usb_phone, 0, sizeof(usb_device));
	memset(&uSinfo, 0, sizeof(uSinfo));
	
	return PROTOCOL_REOK;
}

uint8_t usProtocol_DeviceDetect(void *os_priv)
{
	int cnt, i, res, j;
	libusb_device **devs;	
	int8_t PhoneType = -1;

	usUsb_Init(NULL, os_priv);

	if(uSinfo.State >= CONN_CONNECTING){
		return PROTOCOL_REOK;
	}
	if(usb_phone.os_priv){		
		libusb_device_handle *dev = (libusb_device_handle *)usb_phone.os_priv;
		PRODEBUG("We Need To Free Privious Usb Resource\r\n");

		libusb_release_interface(dev, usb_phone.interface);
		libusb_close(dev);
		usb_phone.os_priv = NULL;	
	}
	memset(&usb_phone, 0, sizeof(usb_device));
	
	cnt = libusb_get_device_list(NULL, &devs);
	if(cnt < 0){
		PRODEBUG("Get Device List Failed.\r\n");
		return PROTOCOL_REINVAILD;
	}
	for(i=0; i<cnt; i++) {
		libusb_device *dev = devs[i];		
		struct libusb_device_descriptor devdesc;
		uint8_t bus = libusb_get_bus_number(dev);
		uint8_t address = libusb_get_device_address(dev);
		if((res = libusb_get_device_descriptor(dev, &devdesc)) != 0) {
			PRODEBUG("Could not get device descriptor for device %d-%d: %d\n", bus, address, res);
			continue;
		}
		if(devdesc.idVendor == VID_APPLE &&
			(devdesc.idProduct >= PID_RANGE_LOW && devdesc.idProduct <= PID_RANGE_MAX)){
			PRODEBUG("Found IOS device  v/p %04x:%04x at %d-%d\n", 
					devdesc.idVendor, devdesc.idProduct, bus, address);
			PhoneType = PRO_IOS;
		}else if(devdesc.idVendor == AOA_ACCESSORY_VID &&
			(devdesc.idProduct >= AOA_ACCESSORY_PID && devdesc.idProduct <= AOA_ACCESSORY_AUDIO_ADB_PID)){
			PRODEBUG("Found Android AOA device  v/p %04x:%04x at %d-%d\n", 
					devdesc.idVendor, devdesc.idProduct, bus, address);
			PhoneType = PRO_ANDROID;
		}else{
			PRODEBUG("Try To Switch Android AOA Mode  v/p %04x:%04x at %d-%d\n", 
						devdesc.idVendor, devdesc.idProduct, bus, address);
			LINUX_SwitchAOAMode(dev);
			continue;
		}
		libusb_device_handle *handle;
		PRODEBUG("Found new device with v/p %04x:%04x at %d-%d\n", devdesc.idVendor, devdesc.idProduct, bus, address);
		// potentially blocking operations follow; they will only run when new devices are detected, which is acceptable
		if((res = libusb_open(dev, &handle)) != 0) {
			PRODEBUG("Could not open device %d-%d: %d\n", bus, address, res);
			continue;
		}
		
		int current_config = 0;
		if((res = libusb_get_configuration(handle, &current_config)) != 0) {
			PRODEBUG("Could not get configuration for device %d-%d: %d\n", bus, address, res);
			libusb_close(handle);
			continue;
		}
		if (current_config != devdesc.bNumConfigurations) {
			struct libusb_config_descriptor *config;
			if((res = libusb_get_active_config_descriptor(dev, &config)) != 0) {
				PRODEBUG("Could not get old configuration descriptor for device %d-%d: %d\n", bus, address, res);
			} else {
				for(j=0; j<config->bNumInterfaces; j++) {
					const struct libusb_interface_descriptor *intf = &config->interface[j].altsetting[0];
					if((res = libusb_kernel_driver_active(handle, intf->bInterfaceNumber)) < 0) {
						PRODEBUG("Could not check kernel ownership of interface %d for device %d-%d: %d\n", intf->bInterfaceNumber, bus, address, res);
						continue;
					}
					if(res == 1) {
						PRODEBUG("Detaching kernel driver for device %d-%d, interface %d\n", bus, address, intf->bInterfaceNumber);
						if((res = libusb_detach_kernel_driver(handle, intf->bInterfaceNumber)) < 0) {
							PRODEBUG("Could not detach kernel driver (%d), configuration change will probably fail!\n", res);
							continue;
						}
					}
				}
				libusb_free_config_descriptor(config);
			}
		
			PRODEBUG("Setting configuration for device %d-%d, from %d to %d\n", bus, address, current_config, devdesc.bNumConfigurations);
			if((res = libusb_set_configuration(handle, devdesc.bNumConfigurations)) != 0) {
				PRODEBUG("Could not set configuration %d for device %d-%d: %d\n", devdesc.bNumConfigurations, bus, address, res);
				libusb_close(handle);
				continue;
			}
		}
		
		struct libusb_config_descriptor *config;
		if((res = libusb_get_active_config_descriptor(dev, &config)) != 0) {
			PRODEBUG("Could not get configuration descriptor for device %d-%d: %d\n", bus, address, res);
			libusb_close(handle);
			continue;
		}
		
		for(j=0; j<config->bNumInterfaces; j++) {
			const struct libusb_interface_descriptor *intf = &config->interface[j].altsetting[0];
			if(PhoneType == PRO_IOS &&
				   (intf->bInterfaceClass != IOS_FTRANS_CLASS ||
				   intf->bInterfaceSubClass != IOS_FTRANS_SUBCLASS ||
				   intf->bInterfaceProtocol != IOS_FTRANS_PROTOCOL)){
				continue;
			}else if(PhoneType == PRO_ANDROID&&
				   intf->bInterfaceClass != INTERFACE_CLASS_AOA){
				continue;
			}
			if(intf->bNumEndpoints != 2) {
				PRODEBUG("Endpoint count mismatch for interface %d of device %d-%d\n", intf->bInterfaceNumber, bus, address);
				continue;
			}
			if((intf->endpoint[0].bEndpointAddress & 0x80) == LIBUSB_ENDPOINT_OUT &&
			   (intf->endpoint[1].bEndpointAddress & 0x80) == LIBUSB_ENDPOINT_IN) {
				usb_phone.interface =  intf->bInterfaceNumber;
				usb_phone.ep_out = intf->endpoint[0].bEndpointAddress;
				usb_phone.ep_in = intf->endpoint[1].bEndpointAddress;
				PRODEBUG("Found interface %d with endpoints %02x/%02x for device %d-%d\n", usb_phone.interface, usb_phone.ep_out, usb_phone.ep_in, bus, address);
				break;
			} else if((intf->endpoint[1].bEndpointAddress & 0x80) == LIBUSB_ENDPOINT_OUT &&
					  (intf->endpoint[0].bEndpointAddress & 0x80) == LIBUSB_ENDPOINT_IN) {
				usb_phone.interface =  intf->bInterfaceNumber;
				usb_phone.ep_out = intf->endpoint[1].bEndpointAddress;
				usb_phone.ep_in = intf->endpoint[0].bEndpointAddress;				
				PRODEBUG("Found interface %d with swapped endpoints %02x/%02x for device %d-%d\n", usb_phone.interface, usb_phone.ep_out, usb_phone.ep_in, bus, address);
				break;
			} else {
				PRODEBUG("Endpoint type mismatch for interface %d of device %d-%d\n", intf->bInterfaceNumber, bus, address);
			}
		}
		
		if(j == config->bNumInterfaces){
			PRODEBUG("Could not find a suitable USB interface for device %d-%d\n", bus, address);
			libusb_free_config_descriptor(config);
			libusb_close(handle);
			continue;
		}	
		libusb_free_config_descriptor(config);
		
		if((res = libusb_claim_interface(handle, usb_phone.interface)) != 0) {
			PRODEBUG("Could not claim interface %d for device %d-%d: %d\n", usb_phone.interface, bus, address, res);
			libusb_close(handle);
			continue;
		}

		usb_phone.os_priv = (void*)handle;
		usb_phone.bus_number= bus;
		usb_phone.device_address= address;
		usb_phone.wMaxPacketSize = libusb_get_max_packet_size(dev, usb_phone.ep_out);
		if (usb_phone.wMaxPacketSize <= 0) {
			PRODEBUG("Could not determine wMaxPacketSize for device %d-%d, setting to 64\n", bus, address);
			usb_phone.wMaxPacketSize = 64;
		} else {
			PRODEBUG("Using wMaxPacketSize=%d for device %d-%d\n", usb_phone.wMaxPacketSize, bus, address);
		}
		/*Set Global var*/
		uSinfo.usType = PhoneType;
		uSinfo.VendorID = devdesc.idVendor;
		uSinfo.ProductID = devdesc.idProduct;
		uSinfo.State = CONN_CONNECTING;
		
		/*We need to set global buffer to uSinfo*/
		uSinfo.itunes.ib_buf = usbBuffer;
		uSinfo.itunes.ib_capacity = sizeof(usbBuffer);
		if(uSinfo.usType == PRO_IOS){
			uSinfo.itunes.max_payload = uSinfo.itunes.ib_capacity - IOS_PROHEADER(uSinfo.itunes.version);
		}else{
			uSinfo.itunes.max_payload = uSinfo.itunes.ib_capacity;
		}
		/*set usbdev*/
		memcpy(&uSinfo.itunes.usbdev, &usb_phone, sizeof(usb_device));
		libusb_free_device_list(devs, 1);

		/*we need to notify kernel found aoa device*/
		if(PhoneType == PRO_ANDROID){
			LINUX_USBPowerControl(OCTL_ENABLE_USB_SW);
		}
		PRODEBUG("Phone Change to CONNCETING State.\r\n");
		return PROTOCOL_REOK;		
	}

	libusb_free_device_list(devs, 1);
	
	return PROTOCOL_REGEN;
}

uint8_t usProtocol_init(void)
{
	FILE *fp;
	char line[256] = {0}, key[128], value[128];	
	char vendor[64] = {0};
	struct accessory_t ustorConf;

	memset(&ustorConf, 0, sizeof(struct accessory_t));
	/*Get Firmware Info*/
	fp = fopen("/etc/firmware", "r");
	if(fp == NULL){
		PRODEBUG("Open /etc/firmware Failed:%s\r\n", strerror(errno));
		return PROTOCOL_REGEN;
	}
	while (fgets(line, sizeof(line), fp)) {
		memset(key, 0, sizeof(key));
		memset(value, 0, sizeof(value));		
		if (sscanf(line, "%[^=]=%[^\n ]",
					key, value) != 2)
			continue;
		if(!strcasecmp(key, "ustor-VENDOR")){			
			strncpy(ustorConf.manufacturer, value, sizeof(ustorConf.manufacturer)-1);			
		}else if(!strcasecmp(key, "ustor-Model")){
			strncpy(ustorConf.model, value, sizeof(ustorConf.model)-1);			
		}else if(!strcasecmp(key, "ustor-Description")){
			strncpy(ustorConf.description, value, sizeof(ustorConf.description)-1);			
		}else if(!strcasecmp(key, "ustor-Version")){
			strncpy(ustorConf.version, value, sizeof(ustorConf.version)-1);			
		}else if(!strcasecmp(key, "ustor-URL")){
			strncpy(ustorConf.url, value, sizeof(ustorConf.url)-1);
		}else if(!strcasecmp(key, "ustor-Serical")){
			strncpy(ustorConf.serial, value, sizeof(ustorConf.serial)-1);
		}else if(!strcasecmp(key, "ustor-Port")){
			ios_port = atoi(value);
		}
	}
	fclose(fp);
	/*Use Default cardid*/
	if(!strlen(ustorConf.manufacturer) || !strlen(ustorConf.model)
			|| !strlen(ustorConf.version)){
		PRODEBUG("Use Default Configuration\r\n");
	}else{
		PRODEBUG("Use Special Configuration\r\n");
		memcpy(&acc_default, &ustorConf, sizeof(struct accessory_t));
	}
	/*Print Special configuration*/
	PRODEBUG("Configuration:\r\n\tVendor:%s\n\tMode:%s\n\tDescription:%s\n"
				"\tVersion:%s\n\tURL:%s\n\tSerical:%s\n\tiosPort:%d\n", acc_default.manufacturer,
			acc_default.model, acc_default.description, acc_default.version, acc_default.url,
			acc_default.serial, ios_port);

	return PROTOCOL_REOK;
}

#endif

uint8_t usProtocol_GetAvaiableBuffer(void **buffer, uint32_t *size)
{
	if(!buffer || !size){
		return PROTOCOL_REPARA;
	}
	
	if(uSinfo.usType == PRO_IOS){
		*buffer = uSinfo.itunes.ib_buf + IOS_PROHEADER(uSinfo.itunes.version);
		*size = uSinfo.itunes.max_payload;
	}else{
		*buffer = uSinfo.itunes.ib_buf;
		*size = uSinfo.itunes.max_payload;
	}
	if(*size == 0){
		printf("No Avaiable Buffer:%p Size:%d\r\n", *buffer, *size);
		return PROTOCOL_DISCONNECT;
	}
	PRODEBUG("Avaiable Buffer:%p Size:%d\r\n", *buffer, *size);

	return PROTOCOL_REOK;
}

uint8_t usProtocol_SendPackage(void *buffer, uint32_t size)
{
	if(uSinfo.State != CONN_CONNECTED){
		PRODEBUG("Phone Not Connected\r\n");		
		return PROTOCOL_REOK;
	}
	if(uSinfo.usType == PRO_IOS){
		return usProtocol_iosSendPackage(&uSinfo.itunes, buffer, size);
	}else if(uSinfo.usType == PRO_ANDROID){
		return usProtocol_aoaSendPackage(&uSinfo.itunes, buffer, size);
	}

	return PROTOCOL_REOK;
}

uint8_t usProtocol_RecvPackage(void **buffer, uint32_t tsize, uint32_t *rsize)
{
	if(uSinfo.usType == PRO_IOS){
		return usProtocol_iosRecvPackage(&uSinfo.itunes, buffer, tsize, rsize);
	}else{
		return usProtocol_aoaRecvPackage(&uSinfo.itunes, buffer, tsize, rsize);
	}
}



