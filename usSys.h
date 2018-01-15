#ifndef __USSYS_H_
#define __USSYS_H_


#ifdef __cplusplus
extern "C" {
#endif

//#define NXP_CHIP_18XX 1
//#define GP_CHIP		1
#define LINUX   1
#ifndef DEBUG_ENABLE
#define DEBUG_ENABLE1    1
#endif

#if defined(GP_CHIP)
#define GP_SD_IDX		1
#endif

#if defined(LINUX)
#define ENOUGH_MEMORY	1
#endif
/** @defgroup Mass_Storage_Host_Definition Main definitions
 * @ingroup Mass_Storage_Host_18xx43xx Mass_Storage_Host_17xx40xx
 * @{
 */

/** LED mask for the library LED driver, to indicate that the USB interface is not ready. */
#define LEDMASK_USB_NOTREADY      LEDS_LED1

/** LED mask for the library LED driver, to indicate that the USB interface is enumerating. */
#define LEDMASK_USB_ENUMERATING  (LEDS_LED2 | LEDS_LED3)

/** LED mask for the library LED driver, to indicate that the USB interface is ready. */
#define LEDMASK_USB_READY        (LEDS_LED2 | LEDS_LED4)

/** LED mask for the library LED driver, to indicate that an error has occurred in the USB interface. */
#define LEDMASK_USB_ERROR        (LEDS_LED1 | LEDS_LED3)

/** LED mask for the library LED driver, to indicate that the USB interface is busy. */
#define LEDMASK_USB_BUSY          LEDS_LED2

void die(uint8_t rc);

#ifdef __cplusplus
}
#endif
#endif

