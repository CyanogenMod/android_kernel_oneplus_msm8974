#include <linux/platform_device.h>
#include <linux/usb/g_hid.h>

/* HID descriptor for a mouse */
static struct hidg_func_descriptor ghid_device_android_mouse = {
	.subclass		= 1, /* Boot Interface Subclass */
	.protocol		= 2, /* Mouse */
	.report_length		= 4,
	.report_desc_length	= 52,
	.report_desc		= {
		0x05, 0x01,	/* USAGE_PAGE (Generic Desktop)           */
		0x09, 0x02,	/* USAGE (Mouse)                          */
		0xa1, 0x01,	/* COLLECTION (Application)               */
		0x09, 0x01,	/*   USAGE (Pointer)                      */
		0xa1, 0x00,	/*   COLLECTION (Physical)                */
		0x05, 0x09,	/*     USAGE_PAGE (Button)                */
		0x19, 0x01,	/*     USAGE_MINIMUM (1)                  */
		0x29, 0x05,	/*     USAGE_MAXIMUM (5)                  */
		0x15, 0x00,	/*     LOGICAL_MINIMUM (1)                */
		0x25, 0x01,	/*     LOGICAL_MAXIMUM (1)                */
		0x95, 0x05,	/*     REPORT_COUNT (5)                   */
		0x75, 0x01,	/*     REPORT_SIZE (1)                    */
		0x81, 0x02,	/*     INPUT (Data,Var,Abs,BitField)      */
		0x95, 0x01,	/*     REPORT_COUNT (1)                   */
		0x75, 0x03,	/*     REPORT_SIZE (3)                    */
		0x81, 0x01,	/*     Input(Cnst,Array,Abs,BitField)     */
		0x05, 0x01,	/*     USAGE_PAGE (Generic Desktop)       */
		0x09, 0x30,	/*     USAGE (x)                          */
		0x09, 0x31,	/*     USAGE (y)                          */
		0x09, 0x38,	/*     USAGE (Wheel)                      */
		0x15, 0x81,	/*     LOGICAL_MINIMUM (-127)             */
		0x25, 0x7F,	/*     LOGICAL_MAXIMUM (127)              */
		0x75, 0x08,	/*     REPORT_SIZE (8)                    */
		0x95, 0x03,	/*     REPORT_COUNT (3)                   */
		0x81, 0x06,	/*     INPUT (Data,Var,Rel,BitField)      */
		0xc0,		/*   END COLLECTION                       */
		0xc0		/* END COLLECTION                         */
	}
};

