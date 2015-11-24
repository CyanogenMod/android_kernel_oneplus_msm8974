/*
 * HID driver for FiiO usb DAC
 *
 * Copyright (c) 2015 Cyanogen, Inc.
 *
 * hid-elo.c used as skeleton driver.
 *
 * This driver is licensed under the terms of GPLv2.
 */

#include <linux/hid.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/usb.h>

#include "hid-ids.h"

struct fiio_priv {
	u32 seen_keys[8];
	u32 used_keys[8];
};

static int fiio_event(struct hid_device *hdev, struct hid_field *field,
		      struct hid_usage *usage, __s32 value)
{
	struct hid_input *hidinput;
	struct fiio_priv *priv;

	int index = usage->code / 32;
	int offset = usage->code % 32;

	if (!(hdev->claimed & HID_CLAIMED_INPUT) || list_empty(&hdev->inputs))
		return 0;

	hidinput = list_first_entry(&hdev->inputs, struct hid_input, list);
	priv = hid_get_drvdata(hdev);

	/* We only care about keypress events */
	if (usage->type != EV_KEY)
		return 0;

	/* Key 0 is "unassigned", not KEY_UNKNOWN */
	if ((usage->type == EV_KEY) && (usage->code == 0))
		return 0;

	/*
	 * The key has generated both press and release events, so let it be
	 * handled normally
	 *
	 * Prev/Next song keys seem to be reversed:
	 *
	 *   |<< >>| >||
	 *            ^ Returns Play/Pause
	 *        ^ Returns Previous Song
	 *    ^ Returns Next Song
	 *
	 * Windows does NOT remap these.
	 */
	if ((priv->used_keys[index] & 1 << offset) &&
	    (priv->seen_keys[index] & 1 << offset)) {
		switch (usage->code) {
		case KEY_NEXTSONG:
			input_event(hidinput->input, EV_KEY, KEY_PREVIOUSSONG,
				    value);
			input_sync(hidinput->input);
			return 1;
			break;
		case KEY_PREVIOUSSONG:
			input_event(hidinput->input, EV_KEY, KEY_NEXTSONG,
				    value);
			input_sync(hidinput->input);
			return 1;
			break;
		default:
			return 0;
			break;
		}
	}

	switch (usage->code) {
	case KEY_VOLUMEUP:
	case KEY_VOLUMEDOWN:
	case KEY_MUTE:
	case KEY_PLAYPAUSE:
	case KEY_NEXTSONG:
	case KEY_PREVIOUSSONG:
	case KEY_STOPCD:
		break;
	default:
		hid_info(hdev, "FiiO: Unknown Key: 0x%0x, 0x%0x\n", usage->code,
			 value);
		break;
	}


	if (value)
		priv->seen_keys[index] |= 1 << offset;
	else
		priv->used_keys[index] |= 1 << offset;

	return 1;
}

static int fiio_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct fiio_priv *priv;
	int ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	hid_set_drvdata(hdev, priv);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		goto err_free;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		goto err_free;
	}

	return 0;
err_free:
	kfree(priv);
	return ret;
}

static void fiio_remove(struct hid_device *hdev)
{
	struct fiio_priv *priv = hid_get_drvdata(hdev);

	hid_hw_stop(hdev);
	kfree(priv);
}

static const struct hid_device_id fiio_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_GYROCOM, USB_DEVICE_ID_GYROCOM_E18), },
	{ }
};
MODULE_DEVICE_TABLE(hid, fiio_devices);

static struct hid_driver fiio_driver = {
	.name = "fiio",
	.id_table = fiio_devices,
	.probe = fiio_probe,
	.remove = fiio_remove,
	.event = fiio_event,
};

static int __init fiio_driver_init(void)
{
	int ret;

	ret = hid_register_driver(&fiio_driver);

	return ret;
}
module_init(fiio_driver_init);

static void __exit fiio_driver_exit(void)
{
	hid_unregister_driver(&fiio_driver);
}
module_exit(fiio_driver_exit);

MODULE_AUTHOR("Pat Erley <pat@cyngn.com>");
MODULE_LICENSE("GPL");
