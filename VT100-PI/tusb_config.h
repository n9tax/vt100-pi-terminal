// TinyUSB configuration: HOST only (PIO-USB) for the keyboard on GP6/GP7.
// The native USB port is unused (no device stack), which keeps the host+device
// coexistence problems out of the picture. Debug goes to the LCD instead.
#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#define CFG_TUSB_OS               OPT_OS_PICO

#define CFG_TUD_ENABLED           0
#define CFG_TUH_ENABLED           1
#define CFG_TUH_RPI_PIO_USB       1        // host runs on PIO, not native USB
#define CFG_TUH_MAX_SPEED         OPT_MODE_FULL_SPEED
#ifndef PIO_USB_DP_PIN_DEFAULT
#define PIO_USB_DP_PIN_DEFAULT    6        // GP6 (D-) on GP7; matches config.h
#endif

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN        __attribute__ ((aligned(4)))
#endif

#define CFG_TUH_ENUMERATION_BUFSIZE 256
#define CFG_TUH_HUB                 1
#define CFG_TUH_DEVICE_MAX          (CFG_TUH_HUB ? 4 : 1)
#define CFG_TUH_HID                 4
#define CFG_TUH_HID_EPIN_BUFSIZE    64
#define CFG_TUH_HID_EPOUT_BUFSIZE   64

#endif // _TUSB_CONFIG_H_
