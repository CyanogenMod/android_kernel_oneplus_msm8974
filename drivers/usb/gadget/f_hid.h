#ifndef _GADGET_F_HID_H
#define _GADGET_F_HID_H

#include <linux/hid.h>
#include <linux/usb/composite.h>
#include <linux/usb/gadget.h>
#include <linux/usb/g_hid.h>

int hidg_bind_config(struct usb_configuration *c,
			    struct hidg_func_descriptor *fdesc, int index);

int ghid_setup(struct usb_gadget *g, int count);

void ghid_cleanup(void);

#endif
