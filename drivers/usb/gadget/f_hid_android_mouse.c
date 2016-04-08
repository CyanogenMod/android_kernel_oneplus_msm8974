#include <linux/platform_device.h>
#include <linux/usb/g_hid.h>

/* HID descriptor for a mouse */
static struct hidg_func_descriptor ghid_device_android_mouse = {
	.subclass      = 0, /* No subclass */
	.protocol      = 2, /* Mouse */
	.report_length = 4,
	.report_desc_length	= 52,
	.report_desc = {
		0x05, 0x01,  //Usage Page(Generic Desktop Controls)
		0x09, 0x02,  //Usage (Mouse)
		0xa1, 0x01,  //Collection (Application)
		0x09, 0x01,  //Usage (pointer)
		0xa1, 0x00,  //Collection (Physical)
		0x05, 0x09,  //Usage Page (Button)
		0x19, 0x01,  //Usage Minimum(1)
		0x29, 0x05,  //Usage Maximum(5)
		0x15, 0x00,  //Logical Minimum(1)
		0x25, 0x01,  //Logical Maximum(1)
		0x95, 0x05,  //Report Count(5)
		0x75, 0x01,  //Report Size(1)
		0x81, 0x02,  //Input(Data,Variable,Absolute,BitField)
		0x95, 0x01,  //Report Count(1)
		0x75, 0x03,  //Report Size(3)
		0x81, 0x01,  //Input(Constant,Array,Absolute,BitField)
		0x05, 0x01,  //Usage Page(Generic Desktop Controls)
		0x09, 0x30,  //Usage(x)
		0x09, 0x31,  //Usage(y)
		0x09, 0x38,  //Usage(Wheel)
		0x15, 0x81,  //Logical Minimum(-127)
		0x25, 0x7F,  //Logical Maximum(127)
		0x75, 0x08,  //Report Size(8)
		0x95, 0x03,  //Report Count(3)
		0x81, 0x06,  //Input(Data,Variable,Relative,BitField)
		0xc0,  //End Collection
		0xc0  //End Collection
	}
};

