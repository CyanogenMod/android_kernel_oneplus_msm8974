/*
 * Roccat Pyra driver for Linux
 *
 * Copyright (c) 2010 Stefan Achatz <erazor_de@users.sourceforge.net>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

/*
 * Roccat Pyra is a mobile gamer mouse which comes in wired and wireless
 * variant. Wireless variant is not tested.
 * Userland tools can be found at http://sourceforge.net/projects/roccat
 */

#include <linux/device.h>
#include <linux/input.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/hid-roccat.h>
#include "hid-ids.h"
#include "hid-roccat-common.h"
#include "hid-roccat-pyra.h"

static uint profile_numbers[5] = {0, 1, 2, 3, 4};

/* pyra_class is used for creating sysfs attributes via roccat char device */
static struct class *pyra_class;

static void profile_activated(struct pyra_device *pyra,
		unsigned int new_profile)
{
	if (new_profile >= ARRAY_SIZE(pyra->profile_settings))
		return;
	pyra->actual_profile = new_profile;
	pyra->actual_cpi = pyra->profile_settings[pyra->actual_profile].y_cpi;
}

static int pyra_send_control(struct usb_device *usb_dev, int value,
		enum pyra_control_requests request)
{
	struct pyra_control control;

	if ((request == PYRA_CONTROL_REQUEST_PROFILE_SETTINGS ||
			request == PYRA_CONTROL_REQUEST_PROFILE_BUTTONS) &&
			(value < 0 || value > 4))
		return -EINVAL;

	control.command = PYRA_COMMAND_CONTROL;
	control.value = value;
	control.request = request;

	return roccat_common_send(usb_dev, PYRA_COMMAND_CONTROL,
			&control, sizeof(struct pyra_control));
}

static int pyra_receive_control_status(struct usb_device *usb_dev)
{
	int retval;
	struct pyra_control control;

	do {
		msleep(10);
		retval = roccat_common_receive(usb_dev, PYRA_COMMAND_CONTROL,
				&control, sizeof(struct pyra_control));

		/* requested too early, try again */
	} while (retval == -EPROTO);

	if (!retval && control.command == PYRA_COMMAND_CONTROL &&
			control.request == PYRA_CONTROL_REQUEST_STATUS &&
			control.value == 1)
		return 0;
	else {
		hid_err(usb_dev, "receive control status: unknown response 0x%x 0x%x\n",
			control.request, control.value);
		return retval ? retval : -EINVAL;
	}
}

static int pyra_get_profile_settings(struct usb_device *usb_dev,
		struct pyra_profile_settings *buf, int number)
{
	int retval;
	retval = pyra_send_control(usb_dev, number,
			PYRA_CONTROL_REQUEST_PROFILE_SETTINGS);
	if (retval)
		return retval;
	return roccat_common_receive(usb_dev, PYRA_COMMAND_PROFILE_SETTINGS,
			buf, sizeof(struct pyra_profile_settings));
}

static int pyra_get_profile_buttons(struct usb_device *usb_dev,
		struct pyra_profile_buttons *buf, int number)
{
	int retval;
	retval = pyra_send_control(usb_dev, number,
			PYRA_CONTROL_REQUEST_PROFILE_BUTTONS);
	if (retval)
		return retval;
	return roccat_common_receive(usb_dev, PYRA_COMMAND_PROFILE_BUTTONS,
			buf, sizeof(struct pyra_profile_buttons));
}

static int pyra_get_settings(struct usb_device *usb_dev,
		struct pyra_settings *buf)
{
	return roccat_common_receive(usb_dev, PYRA_COMMAND_SETTINGS,
			buf, sizeof(struct pyra_settings));
}

static int pyra_get_info(struct usb_device *usb_dev, struct pyra_info *buf)
{
	return roccat_common_receive(usb_dev, PYRA_COMMAND_INFO,
			buf, sizeof(struct pyra_info));
}

static int pyra_send(struct usb_device *usb_dev, uint command,
		void const *buf, uint size)
{
	int retval;
	retval = roccat_common_send(usb_dev, command, buf, size);
	if (retval)
		return retval;
	return pyra_receive_control_status(usb_dev);
}

static int pyra_set_profile_settings(struct usb_device *usb_dev,
		struct pyra_profile_settings const *settings)
{
	return pyra_send(usb_dev, PYRA_COMMAND_PROFILE_SETTINGS, settings,
			sizeof(struct pyra_profile_settings));
}

static int pyra_set_profile_buttons(struct usb_device *usb_dev,
		struct pyra_profile_buttons const *buttons)
{
	return pyra_send(usb_dev, PYRA_COMMAND_PROFILE_BUTTONS, buttons,
			sizeof(struct pyra_profile_buttons));
}

static int pyra_set_settings(struct usb_device *usb_dev,
		struct pyra_settings const *settings)
{
	return pyra_send(usb_dev, PYRA_COMMAND_SETTINGS, settings,
			sizeof(struct pyra_settings));
}

static ssize_t pyra_sysfs_read_profilex_settings(struct file *fp,
		struct kobject *kobj, struct bin_attribute *attr, char *buf,
		loff_t off, size_t count)
{
	struct device *dev =
			container_of(kobj, struct device, kobj)->parent->parent;
	struct pyra_device *pyra = hid_get_drvdata(dev_get_drvdata(dev));

	if (off >= sizeof(struct pyra_profile_settings))
		return 0;

	if (off + count > sizeof(struct pyra_profile_settings))
		count = sizeof(struct pyra_profile_settings) - off;

	mutex_lock(&pyra->pyra_lock);
	memcpy(buf, ((char const *)&pyra->profile_settings[*(uint *)(attr->private)]) + off,
			count);
	mutex_unlock(&pyra->pyra_lock);

	return count;
}

static ssize_t pyra_sysfs_read_profilex_buttons(struct file *fp,
		struct kobject *kobj, struct bin_attribute *attr, char *buf,
		loff_t off, size_t count)
{
	struct device *dev =
			container_of(kobj, struct device, kobj)->parent->parent;
	struct pyra_device *pyra = hid_get_drvdata(dev_get_drvdata(dev));

	if (off >= sizeof(struct pyra_profile_buttons))
		return 0;

	if (off + count > sizeof(struct pyra_profile_buttons))
		count = sizeof(struct pyra_profile_buttons) - off;

	mutex_lock(&pyra->pyra_lock);
	memcpy(buf, ((char const *)&pyra->profile_buttons[*(uint *)(attr->private)]) + off,
			count);
	mutex_unlock(&pyra->pyra_lock);

	return count;
}

static ssize_t pyra_sysfs_write_profile_settings(struct file *fp,
		struct kobject *kobj, struct bin_attribute *attr, char *buf,
		loff_t off, size_t count)
{
	struct device *dev =
			container_of(kobj, struct device, kobj)->parent->parent;
	struct pyra_device *pyra = hid_get_drvdata(dev_get_drvdata(dev));
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	int retval = 0;
	int difference;
	int profile_number;
	struct pyra_profile_settings *profile_settings;

	if (off != 0 || count != sizeof(struct pyra_profile_settings))
		return -EINVAL;

	profile_number = ((struct pyra_profile_settings const *)buf)->number;
	profile_settings = &pyra->profile_settings[profile_number];

	mutex_lock(&pyra->pyra_lock);
	difference = memcmp(buf, profile_settings,
			sizeof(struct pyra_profile_settings));
	if (difference) {
		retval = pyra_set_profile_settings(usb_dev,
				(struct pyra_profile_settings const *)buf);
		if (!retval)
			memcpy(profile_settings, buf,
					sizeof(struct pyra_profile_settings));
	}
	mutex_unlock(&pyra->pyra_lock);

	if (retval)
		return retval;

	return sizeof(struct pyra_profile_settings);
}

static ssize_t pyra_sysfs_write_profile_buttons(struct file *fp,
		struct kobject *kobj, struct bin_attribute *attr, char *buf,
		loff_t off, size_t count)
{
	struct device *dev =
			container_of(kobj, struct device, kobj)->parent->parent;
	struct pyra_device *pyra = hid_get_drvdata(dev_get_drvdata(dev));
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	int retval = 0;
	int difference;
	int profile_number;
	struct pyra_profile_buttons *profile_buttons;

	if (off != 0 || count != sizeof(struct pyra_profile_buttons))
		return -EINVAL;

	profile_number = ((struct pyra_profile_buttons const *)buf)->number;
	profile_buttons = &pyra->profile_buttons[profile_number];

	mutex_lock(&pyra->pyra_lock);
	difference = memcmp(buf, profile_buttons,
			sizeof(struct pyra_profile_buttons));
	if (difference) {
		retval = pyra_set_profile_buttons(usb_dev,
				(struct pyra_profile_buttons const *)buf);
		if (!retval)
			memcpy(profile_buttons, buf,
					sizeof(struct pyra_profile_buttons));
	}
	mutex_unlock(&pyra->pyra_lock);

	if (retval)
		return retval;

	return sizeof(struct pyra_profile_buttons);
}

static ssize_t pyra_sysfs_read_settings(struct file *fp,
		struct kobject *kobj, struct bin_attribute *attr, char *buf,
		loff_t off, size_t count)
{
	struct device *dev =
			container_of(kobj, struct device, kobj)->parent->parent;
	struct pyra_device *pyra = hid_get_drvdata(dev_get_drvdata(dev));

	if (off >= sizeof(struct pyra_settings))
		return 0;

	if (off + count > sizeof(struct pyra_settings))
		count = sizeof(struct pyra_settings) - off;

	mutex_lock(&pyra->pyra_lock);
	memcpy(buf, ((char const *)&pyra->settings) + off, count);
	mutex_unlock(&pyra->pyra_lock);

	return count;
}

static ssize_t pyra_sysfs_write_settings(struct file *fp,
		struct kobject *kobj, struct bin_attribute *attr, char *buf,
		loff_t off, size_t count)
{
	struct device *dev =
			container_of(kobj, struct device, kobj)->parent->parent;
	struct pyra_device *pyra = hid_get_drvdata(dev_get_drvdata(dev));
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	int retval = 0;
	int difference;
	struct pyra_roccat_report roccat_report;
	struct pyra_settings const *settings;

	if (off != 0 || count != sizeof(struct pyra_settings))
		return -EINVAL;

	settings = (struct pyra_settings const *)buf;
	if (settings->startup_profile >= ARRAY_SIZE(pyra->profile_settings))
		return -EINVAL;

	mutex_lock(&pyra->pyra_lock);
	difference = memcmp(buf, &pyra->settings, sizeof(struct pyra_settings));
	if (difference) {
		retval = pyra_set_settings(usb_dev,
				(struct pyra_settings const *)buf);
		if (retval) {
			mutex_unlock(&pyra->pyra_lock);
			return retval;
		}

		memcpy(&pyra->settings, buf,
				sizeof(struct pyra_settings));

		profile_activated(pyra, pyra->settings.startup_profile);

		roccat_report.type = PYRA_MOUSE_EVENT_BUTTON_TYPE_PROFILE_2;
		roccat_report.value = pyra->settings.startup_profile + 1;
		roccat_report.key = 0;
		roccat_report_event(pyra->chrdev_minor,
				(uint8_t const *)&roccat_report);
	}
	mutex_unlock(&pyra->pyra_lock);
	return sizeof(struct pyra_settings);
}


static ssize_t pyra_sysfs_show_actual_cpi(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct pyra_device *pyra =
			hid_get_drvdata(dev_get_drvdata(dev->parent->parent));
	return snprintf(buf, PAGE_SIZE, "%d\n", pyra->actual_cpi);
}

static ssize_t pyra_sysfs_show_actual_profile(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct pyra_device *pyra =
			hid_get_drvdata(dev_get_drvdata(dev->parent->parent));
	return snprintf(buf, PAGE_SIZE, "%d\n", pyra->actual_profile);
}

static ssize_t pyra_sysfs_show_firmware_version(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct pyra_device *pyra =
			hid_get_drvdata(dev_get_drvdata(dev->parent->parent));
	return snprintf(buf, PAGE_SIZE, "%d\n", pyra->firmware_version);
}

static ssize_t pyra_sysfs_show_startup_profile(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct pyra_device *pyra =
			hid_get_drvdata(dev_get_drvdata(dev->parent->parent));
	return snprintf(buf, PAGE_SIZE, "%d\n", pyra->settings.startup_profile);
}

static struct device_attribute pyra_attributes[] = {
	__ATTR(actual_cpi, 0440, pyra_sysfs_show_actual_cpi, NULL),
	__ATTR(actual_profile, 0440, pyra_sysfs_show_actual_profile, NULL),
	__ATTR(firmware_version, 0440,
			pyra_sysfs_show_firmware_version, NULL),
	__ATTR(startup_profile, 0440,
			pyra_sysfs_show_startup_profile, NULL),
	__ATTR_NULL
};

static struct bin_attribute pyra_bin_attributes[] = {
	{
		.attr = { .name = "profile_settings", .mode = 0220 },
		.size = sizeof(struct pyra_profile_settings),
		.write = pyra_sysfs_write_profile_settings
	},
	{
		.attr = { .name = "profile1_settings", .mode = 0440 },
		.size = sizeof(struct pyra_profile_settings),
		.read = pyra_sysfs_read_profilex_settings,
		.private = &profile_numbers[0]
	},
	{
		.attr = { .name = "profile2_settings", .mode = 0440 },
		.size = sizeof(struct pyra_profile_settings),
		.read = pyra_sysfs_read_profilex_settings,
		.private = &profile_numbers[1]
	},
	{
		.attr = { .name = "profile3_settings", .mode = 0440 },
		.size = sizeof(struct pyra_profile_settings),
		.read = pyra_sysfs_read_profilex_settings,
		.private = &profile_numbers[2]
	},
	{
		.attr = { .name = "profile4_settings", .mode = 0440 },
		.size = sizeof(struct pyra_profile_settings),
		.read = pyra_sysfs_read_profilex_settings,
		.private = &profile_numbers[3]
	},
	{
		.attr = { .name = "profile5_settings", .mode = 0440 },
		.size = sizeof(struct pyra_profile_settings),
		.read = pyra_sysfs_read_profilex_settings,
		.private = &profile_numbers[4]
	},
	{
		.attr = { .name = "profile_buttons", .mode = 0220 },
		.size = sizeof(struct pyra_profile_buttons),
		.write = pyra_sysfs_write_profile_buttons
	},
	{
		.attr = { .name = "profile1_buttons", .mode = 0440 },
		.size = sizeof(struct pyra_profile_buttons),
		.read = pyra_sysfs_read_profilex_buttons,
		.private = &profile_numbers[0]
	},
	{
		.attr = { .name = "profile2_buttons", .mode = 0440 },
		.size = sizeof(struct pyra_profile_buttons),
		.read = pyra_sysfs_read_profilex_buttons,
		.private = &profile_numbers[1]
	},
	{
		.attr = { .name = "profile3_buttons", .mode = 0440 },
		.size = sizeof(struct pyra_profile_buttons),
		.read = pyra_sysfs_read_profilex_buttons,
		.private = &profile_numbers[2]
	},
	{
		.attr = { .name = "profile4_buttons", .mode = 0440 },
		.size = sizeof(struct pyra_profile_buttons),
		.read = pyra_sysfs_read_profilex_buttons,
		.private = &profile_numbers[3]
	},
	{
		.attr = { .name = "profile5_buttons", .mode = 0440 },
		.size = sizeof(struct pyra_profile_buttons),
		.read = pyra_sysfs_read_profilex_buttons,
		.private = &profile_numbers[4]
	},
	{
		.attr = { .name = "settings", .mode = 0660 },
		.size = sizeof(struct pyra_settings),
		.read = pyra_sysfs_read_settings,
		.write = pyra_sysfs_write_settings
	},
	__ATTR_NULL
};

static int pyra_init_pyra_device_struct(struct usb_device *usb_dev,
		struct pyra_device *pyra)
{
	struct pyra_info info;
	int retval, i;

	mutex_init(&pyra->pyra_lock);

	retval = pyra_get_info(usb_dev, &info);
	if (retval)
		return retval;

	pyra->firmware_version = info.firmware_version;

	retval = pyra_get_settings(usb_dev, &pyra->settings);
	if (retval)
		return retval;

	for (i = 0; i < 5; ++i) {
		retval = pyra_get_profile_settings(usb_dev,
				&pyra->profile_settings[i], i);
		if (retval)
			return retval;

		retval = pyra_get_profile_buttons(usb_dev,
				&pyra->profile_buttons[i], i);
		if (retval)
			return retval;
	}

	profile_activated(pyra, pyra->settings.startup_profile);

	return 0;
}

static int pyra_init_specials(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct pyra_device *pyra;
	int retval;

	if (intf->cur_altsetting->desc.bInterfaceProtocol
			== USB_INTERFACE_PROTOCOL_MOUSE) {

		pyra = kzalloc(sizeof(*pyra), GFP_KERNEL);
		if (!pyra) {
			hid_err(hdev, "can't alloc device descriptor\n");
			return -ENOMEM;
		}
		hid_set_drvdata(hdev, pyra);

		retval = pyra_init_pyra_device_struct(usb_dev, pyra);
		if (retval) {
			hid_err(hdev, "couldn't init struct pyra_device\n");
			goto exit_free;
		}

		retval = roccat_connect(pyra_class, hdev,
				sizeof(struct pyra_roccat_report));
		if (retval < 0) {
			hid_err(hdev, "couldn't init char dev\n");
		} else {
			pyra->chrdev_minor = retval;
			pyra->roccat_claimed = 1;
		}
	} else {
		hid_set_drvdata(hdev, NULL);
	}

	return 0;
exit_free:
	kfree(pyra);
	return retval;
}

static void pyra_remove_specials(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct pyra_device *pyra;

	if (intf->cur_altsetting->desc.bInterfaceProtocol
			== USB_INTERFACE_PROTOCOL_MOUSE) {
		pyra = hid_get_drvdata(hdev);
		if (pyra->roccat_claimed)
			roccat_disconnect(pyra->chrdev_minor);
		kfree(hid_get_drvdata(hdev));
	}
}

static int pyra_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int retval;

	retval = hid_parse(hdev);
	if (retval) {
		hid_err(hdev, "parse failed\n");
		goto exit;
	}

	retval = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (retval) {
		hid_err(hdev, "hw start failed\n");
		goto exit;
	}

	retval = pyra_init_specials(hdev);
	if (retval) {
		hid_err(hdev, "couldn't install mouse\n");
		goto exit_stop;
	}
	return 0;

exit_stop:
	hid_hw_stop(hdev);
exit:
	return retval;
}

static void pyra_remove(struct hid_device *hdev)
{
	pyra_remove_specials(hdev);
	hid_hw_stop(hdev);
}

static void pyra_keep_values_up_to_date(struct pyra_device *pyra,
		u8 const *data)
{
	struct pyra_mouse_event_button const *button_event;

	switch (data[0]) {
	case PYRA_MOUSE_REPORT_NUMBER_BUTTON:
		button_event = (struct pyra_mouse_event_button const *)data;
		switch (button_event->type) {
		case PYRA_MOUSE_EVENT_BUTTON_TYPE_PROFILE_2:
			profile_activated(pyra, button_event->data1 - 1);
			break;
		case PYRA_MOUSE_EVENT_BUTTON_TYPE_CPI:
			pyra->actual_cpi = button_event->data1;
			break;
		}
		break;
	}
}

static void pyra_report_to_chrdev(struct pyra_device const *pyra,
		u8 const *data)
{
	struct pyra_roccat_report roccat_report;
	struct pyra_mouse_event_button const *button_event;

	if (data[0] != PYRA_MOUSE_REPORT_NUMBER_BUTTON)
		return;

	button_event = (struct pyra_mouse_event_button const *)data;

	switch (button_event->type) {
	case PYRA_MOUSE_EVENT_BUTTON_TYPE_PROFILE_2:
	case PYRA_MOUSE_EVENT_BUTTON_TYPE_CPI:
		roccat_report.type = button_event->type;
		roccat_report.value = button_event->data1;
		roccat_report.key = 0;
		roccat_report_event(pyra->chrdev_minor,
				(uint8_t const *)&roccat_report);
		break;
	case PYRA_MOUSE_EVENT_BUTTON_TYPE_MACRO:
	case PYRA_MOUSE_EVENT_BUTTON_TYPE_SHORTCUT:
	case PYRA_MOUSE_EVENT_BUTTON_TYPE_QUICKLAUNCH:
		if (button_event->data2 == PYRA_MOUSE_EVENT_BUTTON_PRESS) {
			roccat_report.type = button_event->type;
			roccat_report.key = button_event->data1;
			/*
			 * pyra reports profile numbers with range 1-5.
			 * Keeping this behaviour.
			 */
			roccat_report.value = pyra->actual_profile + 1;
			roccat_report_event(pyra->chrdev_minor,
					(uint8_t const *)&roccat_report);
		}
		break;
	}
}

static int pyra_raw_event(struct hid_device *hdev, struct hid_report *report,
		u8 *data, int size)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct pyra_device *pyra = hid_get_drvdata(hdev);

	if (intf->cur_altsetting->desc.bInterfaceProtocol
			!= USB_INTERFACE_PROTOCOL_MOUSE)
		return 0;

	if (pyra == NULL)
		return 0;

	pyra_keep_values_up_to_date(pyra, data);

	if (pyra->roccat_claimed)
		pyra_report_to_chrdev(pyra, data);

	return 0;
}

static const struct hid_device_id pyra_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_ROCCAT,
			USB_DEVICE_ID_ROCCAT_PYRA_WIRED) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ROCCAT,
			USB_DEVICE_ID_ROCCAT_PYRA_WIRELESS) },
	{ }
};

MODULE_DEVICE_TABLE(hid, pyra_devices);

static struct hid_driver pyra_driver = {
		.name = "pyra",
		.id_table = pyra_devices,
		.probe = pyra_probe,
		.remove = pyra_remove,
		.raw_event = pyra_raw_event
};

static int __init pyra_init(void)
{
	int retval;

	/* class name has to be same as driver name */
	pyra_class = class_create(THIS_MODULE, "pyra");
	if (IS_ERR(pyra_class))
		return PTR_ERR(pyra_class);
	pyra_class->dev_attrs = pyra_attributes;
	pyra_class->dev_bin_attrs = pyra_bin_attributes;

	retval = hid_register_driver(&pyra_driver);
	if (retval)
		class_destroy(pyra_class);
	return retval;
}

static void __exit pyra_exit(void)
{
	hid_unregister_driver(&pyra_driver);
	class_destroy(pyra_class);
}

module_init(pyra_init);
module_exit(pyra_exit);

MODULE_AUTHOR("Stefan Achatz");
MODULE_DESCRIPTION("USB Roccat Pyra driver");
MODULE_LICENSE("GPL v2");
