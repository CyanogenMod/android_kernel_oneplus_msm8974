/*
 * ALPS touchpad PS/2 mouse driver
 *
 * Copyright (c) 2003 Neil Brown <neilb@cse.unsw.edu.au>
 * Copyright (c) 2003-2005 Peter Osterlund <petero2@telia.com>
 * Copyright (c) 2004 Dmitry Torokhov <dtor@mail.ru>
 * Copyright (c) 2005 Vojtech Pavlik <vojtech@suse.cz>
 * Copyright (c) 2009 Sebastian Kapfer <sebastian_kapfer@gmx.net>
 *
 * ALPS detection, tap switching and status querying info is taken from
 * tpconfig utility (by C. Scott Ananian and Bruce Kall).
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/slab.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/serio.h>
#include <linux/libps2.h>

#include "psmouse.h"
#include "alps.h"

/*
 * Definitions for ALPS version 3 and 4 command mode protocol
 */
#define ALPS_V3_X_MAX	2000
#define ALPS_V3_Y_MAX	1400

#define ALPS_BITMAP_X_BITS	15
#define ALPS_BITMAP_Y_BITS	11

#define ALPS_CMD_NIBBLE_10	0x01f2

static const struct alps_nibble_commands alps_v3_nibble_commands[] = {
	{ PSMOUSE_CMD_SETPOLL,		0x00 }, /* 0 */
	{ PSMOUSE_CMD_RESET_DIS,	0x00 }, /* 1 */
	{ PSMOUSE_CMD_SETSCALE21,	0x00 }, /* 2 */
	{ PSMOUSE_CMD_SETRATE,		0x0a }, /* 3 */
	{ PSMOUSE_CMD_SETRATE,		0x14 }, /* 4 */
	{ PSMOUSE_CMD_SETRATE,		0x28 }, /* 5 */
	{ PSMOUSE_CMD_SETRATE,		0x3c }, /* 6 */
	{ PSMOUSE_CMD_SETRATE,		0x50 }, /* 7 */
	{ PSMOUSE_CMD_SETRATE,		0x64 }, /* 8 */
	{ PSMOUSE_CMD_SETRATE,		0xc8 }, /* 9 */
	{ ALPS_CMD_NIBBLE_10,		0x00 }, /* a */
	{ PSMOUSE_CMD_SETRES,		0x00 }, /* b */
	{ PSMOUSE_CMD_SETRES,		0x01 }, /* c */
	{ PSMOUSE_CMD_SETRES,		0x02 }, /* d */
	{ PSMOUSE_CMD_SETRES,		0x03 }, /* e */
	{ PSMOUSE_CMD_SETSCALE11,	0x00 }, /* f */
};

static const struct alps_nibble_commands alps_v4_nibble_commands[] = {
	{ PSMOUSE_CMD_ENABLE,		0x00 }, /* 0 */
	{ PSMOUSE_CMD_RESET_DIS,	0x00 }, /* 1 */
	{ PSMOUSE_CMD_SETSCALE21,	0x00 }, /* 2 */
	{ PSMOUSE_CMD_SETRATE,		0x0a }, /* 3 */
	{ PSMOUSE_CMD_SETRATE,		0x14 }, /* 4 */
	{ PSMOUSE_CMD_SETRATE,		0x28 }, /* 5 */
	{ PSMOUSE_CMD_SETRATE,		0x3c }, /* 6 */
	{ PSMOUSE_CMD_SETRATE,		0x50 }, /* 7 */
	{ PSMOUSE_CMD_SETRATE,		0x64 }, /* 8 */
	{ PSMOUSE_CMD_SETRATE,		0xc8 }, /* 9 */
	{ ALPS_CMD_NIBBLE_10,		0x00 }, /* a */
	{ PSMOUSE_CMD_SETRES,		0x00 }, /* b */
	{ PSMOUSE_CMD_SETRES,		0x01 }, /* c */
	{ PSMOUSE_CMD_SETRES,		0x02 }, /* d */
	{ PSMOUSE_CMD_SETRES,		0x03 }, /* e */
	{ PSMOUSE_CMD_SETSCALE11,	0x00 }, /* f */
};


#define ALPS_DUALPOINT		0x02	/* touchpad has trackstick */
#define ALPS_PASS		0x04	/* device has a pass-through port */

#define ALPS_WHEEL		0x08	/* hardware wheel present */
#define ALPS_FW_BK_1		0x10	/* front & back buttons present */
#define ALPS_FW_BK_2		0x20	/* front & back buttons present */
#define ALPS_FOUR_BUTTONS	0x40	/* 4 direction button present */
#define ALPS_PS2_INTERLEAVED	0x80	/* 3-byte PS/2 packet interleaved with
					   6-byte ALPS packet */

static const struct alps_model_info alps_model_data[] = {
	{ { 0x32, 0x02, 0x14 },	0x00, ALPS_PROTO_V2, 0xf8, 0xf8, ALPS_PASS | ALPS_DUALPOINT },	/* Toshiba Salellite Pro M10 */
	{ { 0x33, 0x02, 0x0a },	0x00, ALPS_PROTO_V1, 0x88, 0xf8, 0 },				/* UMAX-530T */
	{ { 0x53, 0x02, 0x0a },	0x00, ALPS_PROTO_V2, 0xf8, 0xf8, 0 },
	{ { 0x53, 0x02, 0x14 },	0x00, ALPS_PROTO_V2, 0xf8, 0xf8, 0 },
	{ { 0x60, 0x03, 0xc8 }, 0x00, ALPS_PROTO_V2, 0xf8, 0xf8, 0 },				/* HP ze1115 */
	{ { 0x63, 0x02, 0x0a },	0x00, ALPS_PROTO_V2, 0xf8, 0xf8, 0 },
	{ { 0x63, 0x02, 0x14 },	0x00, ALPS_PROTO_V2, 0xf8, 0xf8, 0 },
	{ { 0x63, 0x02, 0x28 },	0x00, ALPS_PROTO_V2, 0xf8, 0xf8, ALPS_FW_BK_2 },		/* Fujitsu Siemens S6010 */
	{ { 0x63, 0x02, 0x3c },	0x00, ALPS_PROTO_V2, 0x8f, 0x8f, ALPS_WHEEL },			/* Toshiba Satellite S2400-103 */
	{ { 0x63, 0x02, 0x50 },	0x00, ALPS_PROTO_V2, 0xef, 0xef, ALPS_FW_BK_1 },		/* NEC Versa L320 */
	{ { 0x63, 0x02, 0x64 },	0x00, ALPS_PROTO_V2, 0xf8, 0xf8, 0 },
	{ { 0x63, 0x03, 0xc8 }, 0x00, ALPS_PROTO_V2, 0xf8, 0xf8, ALPS_PASS | ALPS_DUALPOINT },	/* Dell Latitude D800 */
	{ { 0x73, 0x00, 0x0a },	0x00, ALPS_PROTO_V2, 0xf8, 0xf8, ALPS_DUALPOINT },		/* ThinkPad R61 8918-5QG */
	{ { 0x73, 0x02, 0x0a },	0x00, ALPS_PROTO_V2, 0xf8, 0xf8, 0 },
	{ { 0x73, 0x02, 0x14 },	0x00, ALPS_PROTO_V2, 0xf8, 0xf8, ALPS_FW_BK_2 },		/* Ahtec Laptop */
	{ { 0x20, 0x02, 0x0e },	0x00, ALPS_PROTO_V2, 0xf8, 0xf8, ALPS_PASS | ALPS_DUALPOINT },	/* XXX */
	{ { 0x22, 0x02, 0x0a },	0x00, ALPS_PROTO_V2, 0xf8, 0xf8, ALPS_PASS | ALPS_DUALPOINT },
	{ { 0x22, 0x02, 0x14 }, 0x00, ALPS_PROTO_V2, 0xff, 0xff, ALPS_PASS | ALPS_DUALPOINT },	/* Dell Latitude D600 */
	/* Dell Latitude E5500, E6400, E6500, Precision M4400 */
	{ { 0x62, 0x02, 0x14 }, 0x00, ALPS_PROTO_V2, 0xcf, 0xcf,
		ALPS_PASS | ALPS_DUALPOINT | ALPS_PS2_INTERLEAVED },
	{ { 0x73, 0x02, 0x50 }, 0x00, ALPS_PROTO_V2, 0xcf, 0xcf, ALPS_FOUR_BUTTONS },		/* Dell Vostro 1400 */
	{ { 0x52, 0x01, 0x14 }, 0x00, ALPS_PROTO_V2, 0xff, 0xff,
		ALPS_PASS | ALPS_DUALPOINT | ALPS_PS2_INTERLEAVED },				/* Toshiba Tecra A11-11L */
	{ { 0x73, 0x02, 0x64 },	0x9b, ALPS_PROTO_V3, 0x8f, 0x8f, ALPS_DUALPOINT },
	{ { 0x73, 0x02, 0x64 },	0x9d, ALPS_PROTO_V3, 0x8f, 0x8f, ALPS_DUALPOINT },
	{ { 0x73, 0x02, 0x64 },	0x8a, ALPS_PROTO_V4, 0x8f, 0x8f, 0 },
};

/*
 * XXX - this entry is suspicious. First byte has zero lower nibble,
 * which is what a normal mouse would report. Also, the value 0x0e
 * isn't valid per PS/2 spec.
 */

/* Packet formats are described in Documentation/input/alps.txt */

static bool alps_is_valid_first_byte(const struct alps_model_info *model,
				     unsigned char data)
{
	return (data & model->mask0) == model->byte0;
}

static void alps_report_buttons(struct psmouse *psmouse,
				struct input_dev *dev1, struct input_dev *dev2,
				int left, int right, int middle)
{
	struct input_dev *dev;

	/*
	 * If shared button has already been reported on the
	 * other device (dev2) then this event should be also
	 * sent through that device.
	 */
	dev = test_bit(BTN_LEFT, dev2->key) ? dev2 : dev1;
	input_report_key(dev, BTN_LEFT, left);

	dev = test_bit(BTN_RIGHT, dev2->key) ? dev2 : dev1;
	input_report_key(dev, BTN_RIGHT, right);

	dev = test_bit(BTN_MIDDLE, dev2->key) ? dev2 : dev1;
	input_report_key(dev, BTN_MIDDLE, middle);

	/*
	 * Sync the _other_ device now, we'll do the first
	 * device later once we report the rest of the events.
	 */
	input_sync(dev2);
}

static void alps_process_packet_v1_v2(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;
	const struct alps_model_info *model = priv->i;
	unsigned char *packet = psmouse->packet;
	struct input_dev *dev = psmouse->dev;
	struct input_dev *dev2 = priv->dev2;
	int x, y, z, ges, fin, left, right, middle;
	int back = 0, forward = 0;

	if (model->proto_version == ALPS_PROTO_V1) {
		left = packet[2] & 0x10;
		right = packet[2] & 0x08;
		middle = 0;
		x = packet[1] | ((packet[0] & 0x07) << 7);
		y = packet[4] | ((packet[3] & 0x07) << 7);
		z = packet[5];
	} else {
		left = packet[3] & 1;
		right = packet[3] & 2;
		middle = packet[3] & 4;
		x = packet[1] | ((packet[2] & 0x78) << (7 - 3));
		y = packet[4] | ((packet[3] & 0x70) << (7 - 4));
		z = packet[5];
	}

	if (model->flags & ALPS_FW_BK_1) {
		back = packet[0] & 0x10;
		forward = packet[2] & 4;
	}

	if (model->flags & ALPS_FW_BK_2) {
		back = packet[3] & 4;
		forward = packet[2] & 4;
		if ((middle = forward && back))
			forward = back = 0;
	}

	ges = packet[2] & 1;
	fin = packet[2] & 2;

	if ((model->flags & ALPS_DUALPOINT) && z == 127) {
		input_report_rel(dev2, REL_X,  (x > 383 ? (x - 768) : x));
		input_report_rel(dev2, REL_Y, -(y > 255 ? (y - 512) : y));

		alps_report_buttons(psmouse, dev2, dev, left, right, middle);

		input_sync(dev2);
		return;
	}

	alps_report_buttons(psmouse, dev, dev2, left, right, middle);

	/* Convert hardware tap to a reasonable Z value */
	if (ges && !fin)
		z = 40;

	/*
	 * A "tap and drag" operation is reported by the hardware as a transition
	 * from (!fin && ges) to (fin && ges). This should be translated to the
	 * sequence Z>0, Z==0, Z>0, so the Z==0 event has to be generated manually.
	 */
	if (ges && fin && !priv->prev_fin) {
		input_report_abs(dev, ABS_X, x);
		input_report_abs(dev, ABS_Y, y);
		input_report_abs(dev, ABS_PRESSURE, 0);
		input_report_key(dev, BTN_TOOL_FINGER, 0);
		input_sync(dev);
	}
	priv->prev_fin = fin;

	if (z > 30)
		input_report_key(dev, BTN_TOUCH, 1);
	if (z < 25)
		input_report_key(dev, BTN_TOUCH, 0);

	if (z > 0) {
		input_report_abs(dev, ABS_X, x);
		input_report_abs(dev, ABS_Y, y);
	}

	input_report_abs(dev, ABS_PRESSURE, z);
	input_report_key(dev, BTN_TOOL_FINGER, z > 0);

	if (model->flags & ALPS_WHEEL)
		input_report_rel(dev, REL_WHEEL, ((packet[2] << 1) & 0x08) - ((packet[0] >> 4) & 0x07));

	if (model->flags & (ALPS_FW_BK_1 | ALPS_FW_BK_2)) {
		input_report_key(dev, BTN_FORWARD, forward);
		input_report_key(dev, BTN_BACK, back);
	}

	if (model->flags & ALPS_FOUR_BUTTONS) {
		input_report_key(dev, BTN_0, packet[2] & 4);
		input_report_key(dev, BTN_1, packet[0] & 0x10);
		input_report_key(dev, BTN_2, packet[3] & 4);
		input_report_key(dev, BTN_3, packet[0] & 0x20);
	}

	input_sync(dev);
}

/*
 * Process bitmap data from v3 and v4 protocols. Returns the number of
 * fingers detected. A return value of 0 means at least one of the
 * bitmaps was empty.
 *
 * The bitmaps don't have enough data to track fingers, so this function
 * only generates points representing a bounding box of all contacts.
 * These points are returned in x1, y1, x2, and y2 when the return value
 * is greater than 0.
 */
static int alps_process_bitmap(unsigned int x_map, unsigned int y_map,
			       int *x1, int *y1, int *x2, int *y2)
{
	struct alps_bitmap_point {
		int start_bit;
		int num_bits;
	};

	int fingers_x = 0, fingers_y = 0, fingers;
	int i, bit, prev_bit;
	struct alps_bitmap_point x_low = {0,}, x_high = {0,};
	struct alps_bitmap_point y_low = {0,}, y_high = {0,};
	struct alps_bitmap_point *point;

	if (!x_map || !y_map)
		return 0;

	*x1 = *y1 = *x2 = *y2 = 0;

	prev_bit = 0;
	point = &x_low;
	for (i = 0; x_map != 0; i++, x_map >>= 1) {
		bit = x_map & 1;
		if (bit) {
			if (!prev_bit) {
				point->start_bit = i;
				fingers_x++;
			}
			point->num_bits++;
		} else {
			if (prev_bit)
				point = &x_high;
			else
				point->num_bits = 0;
		}
		prev_bit = bit;
	}

	/*
	 * y bitmap is reversed for what we need (lower positions are in
	 * higher bits), so we process from the top end.
	 */
	y_map = y_map << (sizeof(y_map) * BITS_PER_BYTE - ALPS_BITMAP_Y_BITS);
	prev_bit = 0;
	point = &y_low;
	for (i = 0; y_map != 0; i++, y_map <<= 1) {
		bit = y_map & (1 << (sizeof(y_map) * BITS_PER_BYTE - 1));
		if (bit) {
			if (!prev_bit) {
				point->start_bit = i;
				fingers_y++;
			}
			point->num_bits++;
		} else {
			if (prev_bit)
				point = &y_high;
			else
				point->num_bits = 0;
		}
		prev_bit = bit;
	}

	/*
	 * Fingers can overlap, so we use the maximum count of fingers
	 * on either axis as the finger count.
	 */
	fingers = max(fingers_x, fingers_y);

	/*
	 * If total fingers is > 1 but either axis reports only a single
	 * contact, we have overlapping or adjacent fingers. For the
	 * purposes of creating a bounding box, divide the single contact
	 * (roughly) equally between the two points.
	 */
	if (fingers > 1) {
		if (fingers_x == 1) {
			i = x_low.num_bits / 2;
			x_low.num_bits = x_low.num_bits - i;
			x_high.start_bit = x_low.start_bit + i;
			x_high.num_bits = max(i, 1);
		} else if (fingers_y == 1) {
			i = y_low.num_bits / 2;
			y_low.num_bits = y_low.num_bits - i;
			y_high.start_bit = y_low.start_bit + i;
			y_high.num_bits = max(i, 1);
		}
	}

	*x1 = (ALPS_V3_X_MAX * (2 * x_low.start_bit + x_low.num_bits - 1)) /
	      (2 * (ALPS_BITMAP_X_BITS - 1));
	*y1 = (ALPS_V3_Y_MAX * (2 * y_low.start_bit + y_low.num_bits - 1)) /
	      (2 * (ALPS_BITMAP_Y_BITS - 1));

	if (fingers > 1) {
		*x2 = (ALPS_V3_X_MAX * (2 * x_high.start_bit + x_high.num_bits - 1)) /
		      (2 * (ALPS_BITMAP_X_BITS - 1));
		*y2 = (ALPS_V3_Y_MAX * (2 * y_high.start_bit + y_high.num_bits - 1)) /
		      (2 * (ALPS_BITMAP_Y_BITS - 1));
	}

	return fingers;
}

static void alps_set_slot(struct input_dev *dev, int slot, bool active,
			  int x, int y)
{
	input_mt_slot(dev, slot);
	input_mt_report_slot_state(dev, MT_TOOL_FINGER, active);
	if (active) {
		input_report_abs(dev, ABS_MT_POSITION_X, x);
		input_report_abs(dev, ABS_MT_POSITION_Y, y);
	}
}

static void alps_report_semi_mt_data(struct input_dev *dev, int num_fingers,
				     int x1, int y1, int x2, int y2)
{
	alps_set_slot(dev, 0, num_fingers != 0, x1, y1);
	alps_set_slot(dev, 1, num_fingers == 2, x2, y2);
}

static void alps_process_trackstick_packet_v3(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;
	unsigned char *packet = psmouse->packet;
	struct input_dev *dev = priv->dev2;
	int x, y, z, left, right, middle;

	/* Sanity check packet */
	if (!(packet[0] & 0x40)) {
		psmouse_dbg(psmouse, "Bad trackstick packet, discarding\n");
		return;
	}

	/*
	 * There's a special packet that seems to indicate the end
	 * of a stream of trackstick data. Filter these out.
	 */
	if (packet[1] == 0x7f && packet[2] == 0x7f && packet[4] == 0x7f)
		return;

	x = (s8)(((packet[0] & 0x20) << 2) | (packet[1] & 0x7f));
	y = (s8)(((packet[0] & 0x10) << 3) | (packet[2] & 0x7f));
	z = (packet[4] & 0x7c) >> 2;

	/*
	 * The x and y values tend to be quite large, and when used
	 * alone the trackstick is difficult to use. Scale them down
	 * to compensate.
	 */
	x /= 8;
	y /= 8;

	input_report_rel(dev, REL_X, x);
	input_report_rel(dev, REL_Y, -y);

	/*
	 * Most ALPS models report the trackstick buttons in the touchpad
	 * packets, but a few report them here. No reliable way has been
	 * found to differentiate between the models upfront, so we enable
	 * the quirk in response to seeing a button press in the trackstick
	 * packet.
	 */
	left = packet[3] & 0x01;
	right = packet[3] & 0x02;
	middle = packet[3] & 0x04;

	if (!(priv->quirks & ALPS_QUIRK_TRACKSTICK_BUTTONS) &&
	    (left || right || middle))
		priv->quirks |= ALPS_QUIRK_TRACKSTICK_BUTTONS;

	if (priv->quirks & ALPS_QUIRK_TRACKSTICK_BUTTONS) {
		input_report_key(dev, BTN_LEFT, left);
		input_report_key(dev, BTN_RIGHT, right);
		input_report_key(dev, BTN_MIDDLE, middle);
	}

	input_sync(dev);
	return;
}

static void alps_process_touchpad_packet_v3(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;
	unsigned char *packet = psmouse->packet;
	struct input_dev *dev = psmouse->dev;
	struct input_dev *dev2 = priv->dev2;
	int x, y, z;
	int left, right, middle;
	int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
	int fingers = 0, bmap_fingers;
	unsigned int x_bitmap, y_bitmap;

	/*
	 * There's no single feature of touchpad position and bitmap packets
	 * that can be used to distinguish between them. We rely on the fact
	 * that a bitmap packet should always follow a position packet with
	 * bit 6 of packet[4] set.
	 */
	if (priv->multi_packet) {
		/*
		 * Sometimes a position packet will indicate a multi-packet
		 * sequence, but then what follows is another position
		 * packet. Check for this, and when it happens process the
		 * position packet as usual.
		 */
		if (packet[0] & 0x40) {
			fingers = (packet[5] & 0x3) + 1;
			x_bitmap = ((packet[4] & 0x7e) << 8) |
				   ((packet[1] & 0x7f) << 2) |
				   ((packet[0] & 0x30) >> 4);
			y_bitmap = ((packet[3] & 0x70) << 4) |
				   ((packet[2] & 0x7f) << 1) |
				   (packet[4] & 0x01);

			bmap_fingers = alps_process_bitmap(x_bitmap, y_bitmap,
							   &x1, &y1, &x2, &y2);

			/*
			 * We shouldn't report more than one finger if
			 * we don't have two coordinates.
			 */
			if (fingers > 1 && bmap_fingers < 2)
				fingers = bmap_fingers;

			/* Now process position packet */
			packet = priv->multi_data;
		} else {
			priv->multi_packet = 0;
		}
	}

	/*
	 * Bit 6 of byte 0 is not usually set in position packets. The only
	 * times it seems to be set is in situations where the data is
	 * suspect anyway, e.g. a palm resting flat on the touchpad. Given
	 * this combined with the fact that this bit is useful for filtering
	 * out misidentified bitmap packets, we reject anything with this
	 * bit set.
	 */
	if (packet[0] & 0x40)
		return;

	if (!priv->multi_packet && (packet[4] & 0x40)) {
		priv->multi_packet = 1;
		memcpy(priv->multi_data, packet, sizeof(priv->multi_data));
		return;
	}

	priv->multi_packet = 0;

	left = packet[3] & 0x01;
	right = packet[3] & 0x02;
	middle = packet[3] & 0x04;

	x = ((packet[1] & 0x7f) << 4) | ((packet[4] & 0x30) >> 2) |
	    ((packet[0] & 0x30) >> 4);
	y = ((packet[2] & 0x7f) << 4) | (packet[4] & 0x0f);
	z = packet[5] & 0x7f;

	/*
	 * Sometimes the hardware sends a single packet with z = 0
	 * in the middle of a stream. Real releases generate packets
	 * with x, y, and z all zero, so these seem to be flukes.
	 * Ignore them.
	 */
	if (x && y && !z)
		return;

	/*
	 * If we don't have MT data or the bitmaps were empty, we have
	 * to rely on ST data.
	 */
	if (!fingers) {
		x1 = x;
		y1 = y;
		fingers = z > 0 ? 1 : 0;
	}

	if (z >= 64)
		input_report_key(dev, BTN_TOUCH, 1);
	else
		input_report_key(dev, BTN_TOUCH, 0);

	alps_report_semi_mt_data(dev, fingers, x1, y1, x2, y2);

	input_report_key(dev, BTN_TOOL_FINGER, fingers == 1);
	input_report_key(dev, BTN_TOOL_DOUBLETAP, fingers == 2);
	input_report_key(dev, BTN_TOOL_TRIPLETAP, fingers == 3);
	input_report_key(dev, BTN_TOOL_QUADTAP, fingers == 4);

	input_report_key(dev, BTN_LEFT, left);
	input_report_key(dev, BTN_RIGHT, right);
	input_report_key(dev, BTN_MIDDLE, middle);

	if (z > 0) {
		input_report_abs(dev, ABS_X, x);
		input_report_abs(dev, ABS_Y, y);
	}
	input_report_abs(dev, ABS_PRESSURE, z);

	input_sync(dev);

	if (!(priv->quirks & ALPS_QUIRK_TRACKSTICK_BUTTONS)) {
		left = packet[3] & 0x10;
		right = packet[3] & 0x20;
		middle = packet[3] & 0x40;

		input_report_key(dev2, BTN_LEFT, left);
		input_report_key(dev2, BTN_RIGHT, right);
		input_report_key(dev2, BTN_MIDDLE, middle);
		input_sync(dev2);
	}
}

static void alps_process_packet_v3(struct psmouse *psmouse)
{
	unsigned char *packet = psmouse->packet;

	/*
	 * v3 protocol packets come in three types, two representing
	 * touchpad data and one representing trackstick data.
	 * Trackstick packets seem to be distinguished by always
	 * having 0x3f in the last byte. This value has never been
	 * observed in the last byte of either of the other types
	 * of packets.
	 */
	if (packet[5] == 0x3f) {
		alps_process_trackstick_packet_v3(psmouse);
		return;
	}

	alps_process_touchpad_packet_v3(psmouse);
}

static void alps_process_packet_v4(struct psmouse *psmouse)
{
	unsigned char *packet = psmouse->packet;
	struct input_dev *dev = psmouse->dev;
	int x, y, z;
	int left, right;

	left = packet[4] & 0x01;
	right = packet[4] & 0x02;

	x = ((packet[1] & 0x7f) << 4) | ((packet[3] & 0x30) >> 2) |
	    ((packet[0] & 0x30) >> 4);
	y = ((packet[2] & 0x7f) << 4) | (packet[3] & 0x0f);
	z = packet[5] & 0x7f;

	if (z >= 64)
		input_report_key(dev, BTN_TOUCH, 1);
	else
		input_report_key(dev, BTN_TOUCH, 0);

	if (z > 0) {
		input_report_abs(dev, ABS_X, x);
		input_report_abs(dev, ABS_Y, y);
	}
	input_report_abs(dev, ABS_PRESSURE, z);

	input_report_key(dev, BTN_TOOL_FINGER, z > 0);
	input_report_key(dev, BTN_LEFT, left);
	input_report_key(dev, BTN_RIGHT, right);

	input_sync(dev);
}

static void alps_process_packet(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;
	const struct alps_model_info *model = priv->i;

	switch (model->proto_version) {
	case ALPS_PROTO_V1:
	case ALPS_PROTO_V2:
		alps_process_packet_v1_v2(psmouse);
		break;
	case ALPS_PROTO_V3:
		alps_process_packet_v3(psmouse);
		break;
	case ALPS_PROTO_V4:
		alps_process_packet_v4(psmouse);
		break;
	}
}

static void alps_report_bare_ps2_packet(struct psmouse *psmouse,
					unsigned char packet[],
					bool report_buttons)
{
	struct alps_data *priv = psmouse->private;
	struct input_dev *dev2 = priv->dev2;

	if (report_buttons)
		alps_report_buttons(psmouse, dev2, psmouse->dev,
				packet[0] & 1, packet[0] & 2, packet[0] & 4);

	input_report_rel(dev2, REL_X,
		packet[1] ? packet[1] - ((packet[0] << 4) & 0x100) : 0);
	input_report_rel(dev2, REL_Y,
		packet[2] ? ((packet[0] << 3) & 0x100) - packet[2] : 0);

	input_sync(dev2);
}

static psmouse_ret_t alps_handle_interleaved_ps2(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;

	if (psmouse->pktcnt < 6)
		return PSMOUSE_GOOD_DATA;

	if (psmouse->pktcnt == 6) {
		/*
		 * Start a timer to flush the packet if it ends up last
		 * 6-byte packet in the stream. Timer needs to fire
		 * psmouse core times out itself. 20 ms should be enough
		 * to decide if we are getting more data or not.
		 */
		mod_timer(&priv->timer, jiffies + msecs_to_jiffies(20));
		return PSMOUSE_GOOD_DATA;
	}

	del_timer(&priv->timer);

	if (psmouse->packet[6] & 0x80) {

		/*
		 * Highest bit is set - that means we either had
		 * complete ALPS packet and this is start of the
		 * next packet or we got garbage.
		 */

		if (((psmouse->packet[3] |
		      psmouse->packet[4] |
		      psmouse->packet[5]) & 0x80) ||
		    (!alps_is_valid_first_byte(priv->i, psmouse->packet[6]))) {
			psmouse_dbg(psmouse,
				    "refusing packet %x %x %x %x (suspected interleaved ps/2)\n",
				    psmouse->packet[3], psmouse->packet[4],
				    psmouse->packet[5], psmouse->packet[6]);
			return PSMOUSE_BAD_DATA;
		}

		alps_process_packet(psmouse);

		/* Continue with the next packet */
		psmouse->packet[0] = psmouse->packet[6];
		psmouse->pktcnt = 1;

	} else {

		/*
		 * High bit is 0 - that means that we indeed got a PS/2
		 * packet in the middle of ALPS packet.
		 *
		 * There is also possibility that we got 6-byte ALPS
		 * packet followed  by 3-byte packet from trackpoint. We
		 * can not distinguish between these 2 scenarios but
		 * because the latter is unlikely to happen in course of
		 * normal operation (user would need to press all
		 * buttons on the pad and start moving trackpoint
		 * without touching the pad surface) we assume former.
		 * Even if we are wrong the wost thing that would happen
		 * the cursor would jump but we should not get protocol
		 * de-synchronization.
		 */

		alps_report_bare_ps2_packet(psmouse, &psmouse->packet[3],
					    false);

		/*
		 * Continue with the standard ALPS protocol handling,
		 * but make sure we won't process it as an interleaved
		 * packet again, which may happen if all buttons are
		 * pressed. To avoid this let's reset the 4th bit which
		 * is normally 1.
		 */
		psmouse->packet[3] = psmouse->packet[6] & 0xf7;
		psmouse->pktcnt = 4;
	}

	return PSMOUSE_GOOD_DATA;
}

static void alps_flush_packet(unsigned long data)
{
	struct psmouse *psmouse = (struct psmouse *)data;

	serio_pause_rx(psmouse->ps2dev.serio);

	if (psmouse->pktcnt == psmouse->pktsize) {

		/*
		 * We did not any more data in reasonable amount of time.
		 * Validate the last 3 bytes and process as a standard
		 * ALPS packet.
		 */
		if ((psmouse->packet[3] |
		     psmouse->packet[4] |
		     psmouse->packet[5]) & 0x80) {
			psmouse_dbg(psmouse,
				    "refusing packet %x %x %x (suspected interleaved ps/2)\n",
				    psmouse->packet[3], psmouse->packet[4],
				    psmouse->packet[5]);
		} else {
			alps_process_packet(psmouse);
		}
		psmouse->pktcnt = 0;
	}

	serio_continue_rx(psmouse->ps2dev.serio);
}

static psmouse_ret_t alps_process_byte(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;
	const struct alps_model_info *model = priv->i;

	/*
	 * Check if we are dealing with a bare PS/2 packet, presumably from
	 * a device connected to the external PS/2 port. Because bare PS/2
	 * protocol does not have enough constant bits to self-synchronize
	 * properly we only do this if the device is fully synchronized.
	 */
	if (!psmouse->out_of_sync_cnt && (psmouse->packet[0] & 0xc8) == 0x08) {
		if (psmouse->pktcnt == 3) {
			alps_report_bare_ps2_packet(psmouse, psmouse->packet,
						    true);
			return PSMOUSE_FULL_PACKET;
		}
		return PSMOUSE_GOOD_DATA;
	}

	/* Check for PS/2 packet stuffed in the middle of ALPS packet. */

	if ((model->flags & ALPS_PS2_INTERLEAVED) &&
	    psmouse->pktcnt >= 4 && (psmouse->packet[3] & 0x0f) == 0x0f) {
		return alps_handle_interleaved_ps2(psmouse);
	}

	if (!alps_is_valid_first_byte(model, psmouse->packet[0])) {
		psmouse_dbg(psmouse,
			    "refusing packet[0] = %x (mask0 = %x, byte0 = %x)\n",
			    psmouse->packet[0], model->mask0, model->byte0);
		return PSMOUSE_BAD_DATA;
	}

	/* Bytes 2 - pktsize should have 0 in the highest bit */
	if (psmouse->pktcnt >= 2 && psmouse->pktcnt <= psmouse->pktsize &&
	    (psmouse->packet[psmouse->pktcnt - 1] & 0x80)) {
		psmouse_dbg(psmouse, "refusing packet[%i] = %x\n",
			    psmouse->pktcnt - 1,
			    psmouse->packet[psmouse->pktcnt - 1]);
		return PSMOUSE_BAD_DATA;
	}

	if (psmouse->pktcnt == psmouse->pktsize) {
		alps_process_packet(psmouse);
		return PSMOUSE_FULL_PACKET;
	}

	return PSMOUSE_GOOD_DATA;
}

static int alps_command_mode_send_nibble(struct psmouse *psmouse, int nibble)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	struct alps_data *priv = psmouse->private;
	int command;
	unsigned char *param;
	unsigned char dummy[4];

	BUG_ON(nibble > 0xf);

	command = priv->nibble_commands[nibble].command;
	param = (command & 0x0f00) ?
		dummy : (unsigned char *)&priv->nibble_commands[nibble].data;

	if (ps2_command(ps2dev, param, command))
		return -1;

	return 0;
}

static int alps_command_mode_set_addr(struct psmouse *psmouse, int addr)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	struct alps_data *priv = psmouse->private;
	int i, nibble;

	if (ps2_command(ps2dev, NULL, priv->addr_command))
		return -1;

	for (i = 12; i >= 0; i -= 4) {
		nibble = (addr >> i) & 0xf;
		if (alps_command_mode_send_nibble(psmouse, nibble))
			return -1;
	}

	return 0;
}

static int __alps_command_mode_read_reg(struct psmouse *psmouse, int addr)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char param[4];

	if (ps2_command(ps2dev, param, PSMOUSE_CMD_GETINFO))
		return -1;

	/*
	 * The address being read is returned in the first two bytes
	 * of the result. Check that this address matches the expected
	 * address.
	 */
	if (addr != ((param[0] << 8) | param[1]))
		return -1;

	return param[2];
}

static int alps_command_mode_read_reg(struct psmouse *psmouse, int addr)
{
	if (alps_command_mode_set_addr(psmouse, addr))
		return -1;
	return __alps_command_mode_read_reg(psmouse, addr);
}

static int __alps_command_mode_write_reg(struct psmouse *psmouse, u8 value)
{
	if (alps_command_mode_send_nibble(psmouse, (value >> 4) & 0xf))
		return -1;
	if (alps_command_mode_send_nibble(psmouse, value & 0xf))
		return -1;
	return 0;
}

static int alps_command_mode_write_reg(struct psmouse *psmouse, int addr,
				       u8 value)
{
	if (alps_command_mode_set_addr(psmouse, addr))
		return -1;
	return __alps_command_mode_write_reg(psmouse, value);
}

static int alps_enter_command_mode(struct psmouse *psmouse,
				   unsigned char *resp)
{
	unsigned char param[4];
	struct ps2dev *ps2dev = &psmouse->ps2dev;

	if (ps2_command(ps2dev, NULL, PSMOUSE_CMD_RESET_WRAP) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_RESET_WRAP) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_RESET_WRAP) ||
	    ps2_command(ps2dev, param, PSMOUSE_CMD_GETINFO)) {
		psmouse_err(psmouse, "failed to enter command mode\n");
		return -1;
	}

	if (param[0] != 0x88 && param[1] != 0x07) {
		psmouse_dbg(psmouse,
			    "unknown response while entering command mode: %2.2x %2.2x %2.2x\n",
			    param[0], param[1], param[2]);
		return -1;
	}

	if (resp)
		*resp = param[2];
	return 0;
}

static inline int alps_exit_command_mode(struct psmouse *psmouse)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	if (ps2_command(ps2dev, NULL, PSMOUSE_CMD_SETSTREAM))
		return -1;
	return 0;
}

static const struct alps_model_info *alps_get_model(struct psmouse *psmouse, int *version)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	static const unsigned char rates[] = { 0, 10, 20, 40, 60, 80, 100, 200 };
	unsigned char param[4];
	const struct alps_model_info *model = NULL;
	int i;

	/*
	 * First try "E6 report".
	 * ALPS should return 0,0,10 or 0,0,100 if no buttons are pressed.
	 * The bits 0-2 of the first byte will be 1s if some buttons are
	 * pressed.
	 */
	param[0] = 0;
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES) ||
	    ps2_command(ps2dev,  NULL, PSMOUSE_CMD_SETSCALE11) ||
	    ps2_command(ps2dev,  NULL, PSMOUSE_CMD_SETSCALE11) ||
	    ps2_command(ps2dev,  NULL, PSMOUSE_CMD_SETSCALE11))
		return NULL;

	param[0] = param[1] = param[2] = 0xff;
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_GETINFO))
		return NULL;

	psmouse_dbg(psmouse, "E6 report: %2.2x %2.2x %2.2x",
		    param[0], param[1], param[2]);

	if ((param[0] & 0xf8) != 0 || param[1] != 0 ||
	    (param[2] != 10 && param[2] != 100))
		return NULL;

	/*
	 * Now try "E7 report". Allowed responses are in
	 * alps_model_data[].signature
	 */
	param[0] = 0;
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES) ||
	    ps2_command(ps2dev,  NULL, PSMOUSE_CMD_SETSCALE21) ||
	    ps2_command(ps2dev,  NULL, PSMOUSE_CMD_SETSCALE21) ||
	    ps2_command(ps2dev,  NULL, PSMOUSE_CMD_SETSCALE21))
		return NULL;

	param[0] = param[1] = param[2] = 0xff;
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_GETINFO))
		return NULL;

	psmouse_dbg(psmouse, "E7 report: %2.2x %2.2x %2.2x",
		    param[0], param[1], param[2]);

	if (version) {
		for (i = 0; i < ARRAY_SIZE(rates) && param[2] != rates[i]; i++)
			/* empty */;
		*version = (param[0] << 8) | (param[1] << 4) | i;
	}

	for (i = 0; i < ARRAY_SIZE(alps_model_data); i++) {
		if (!memcmp(param, alps_model_data[i].signature,
			    sizeof(alps_model_data[i].signature))) {
			model = alps_model_data + i;
			break;
		}
	}

	if (model && model->proto_version > ALPS_PROTO_V2) {
		/*
		 * Need to check command mode response to identify
		 * model
		 */
		model = NULL;
		if (alps_enter_command_mode(psmouse, param)) {
			psmouse_warn(psmouse,
				     "touchpad failed to enter command mode\n");
		} else {
			for (i = 0; i < ARRAY_SIZE(alps_model_data); i++) {
				if (alps_model_data[i].proto_version > ALPS_PROTO_V2 &&
				    alps_model_data[i].command_mode_resp == param[0]) {
					model = alps_model_data + i;
					break;
				}
			}
			alps_exit_command_mode(psmouse);

			if (!model)
				psmouse_dbg(psmouse,
					    "Unknown command mode response %2.2x\n",
					    param[0]);
		}
	}

	return model;
}

/*
 * For DualPoint devices select the device that should respond to
 * subsequent commands. It looks like glidepad is behind stickpointer,
 * I'd thought it would be other way around...
 */
static int alps_passthrough_mode_v2(struct psmouse *psmouse, bool enable)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	int cmd = enable ? PSMOUSE_CMD_SETSCALE21 : PSMOUSE_CMD_SETSCALE11;

	if (ps2_command(ps2dev, NULL, cmd) ||
	    ps2_command(ps2dev, NULL, cmd) ||
	    ps2_command(ps2dev, NULL, cmd) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE))
		return -1;

	/* we may get 3 more bytes, just ignore them */
	ps2_drain(ps2dev, 3, 100);

	return 0;
}

static int alps_absolute_mode_v1_v2(struct psmouse *psmouse)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;

	/* Try ALPS magic knock - 4 disable before enable */
	if (ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_ENABLE))
		return -1;

	/*
	 * Switch mouse to poll (remote) mode so motion data will not
	 * get in our way
	 */
	return ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_SETPOLL);
}

static int alps_get_status(struct psmouse *psmouse, char *param)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;

	/* Get status: 0xF5 0xF5 0xF5 0xE9 */
	if (ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE) ||
	    ps2_command(ps2dev, param, PSMOUSE_CMD_GETINFO))
		return -1;

	psmouse_dbg(psmouse, "Status: %2.2x %2.2x %2.2x",
		    param[0], param[1], param[2]);

	return 0;
}

/*
 * Turn touchpad tapping on or off. The sequences are:
 * 0xE9 0xF5 0xF5 0xF3 0x0A to enable,
 * 0xE9 0xF5 0xF5 0xE8 0x00 to disable.
 * My guess that 0xE9 (GetInfo) is here as a sync point.
 * For models that also have stickpointer (DualPoints) its tapping
 * is controlled separately (0xE6 0xE6 0xE6 0xF3 0x14|0x0A) but
 * we don't fiddle with it.
 */
static int alps_tap_mode(struct psmouse *psmouse, int enable)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	int cmd = enable ? PSMOUSE_CMD_SETRATE : PSMOUSE_CMD_SETRES;
	unsigned char tap_arg = enable ? 0x0A : 0x00;
	unsigned char param[4];

	if (ps2_command(ps2dev, param, PSMOUSE_CMD_GETINFO) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE) ||
	    ps2_command(ps2dev, &tap_arg, cmd))
		return -1;

	if (alps_get_status(psmouse, param))
		return -1;

	return 0;
}

/*
 * alps_poll() - poll the touchpad for current motion packet.
 * Used in resync.
 */
static int alps_poll(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;
	unsigned char buf[sizeof(psmouse->packet)];
	bool poll_failed;

	if (priv->i->flags & ALPS_PASS)
		alps_passthrough_mode_v2(psmouse, true);

	poll_failed = ps2_command(&psmouse->ps2dev, buf,
				  PSMOUSE_CMD_POLL | (psmouse->pktsize << 8)) < 0;

	if (priv->i->flags & ALPS_PASS)
		alps_passthrough_mode_v2(psmouse, false);

	if (poll_failed || (buf[0] & priv->i->mask0) != priv->i->byte0)
		return -1;

	if ((psmouse->badbyte & 0xc8) == 0x08) {
/*
 * Poll the track stick ...
 */
		if (ps2_command(&psmouse->ps2dev, buf, PSMOUSE_CMD_POLL | (3 << 8)))
			return -1;
	}

	memcpy(psmouse->packet, buf, sizeof(buf));
	return 0;
}

static int alps_hw_init_v1_v2(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;
	const struct alps_model_info *model = priv->i;

	if ((model->flags & ALPS_PASS) &&
	    alps_passthrough_mode_v2(psmouse, true)) {
		return -1;
	}

	if (alps_tap_mode(psmouse, true)) {
		psmouse_warn(psmouse, "Failed to enable hardware tapping\n");
		return -1;
	}

	if (alps_absolute_mode_v1_v2(psmouse)) {
		psmouse_err(psmouse, "Failed to enable absolute mode\n");
		return -1;
	}

	if ((model->flags & ALPS_PASS) &&
	    alps_passthrough_mode_v2(psmouse, false)) {
		return -1;
	}

	/* ALPS needs stream mode, otherwise it won't report any data */
	if (ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_SETSTREAM)) {
		psmouse_err(psmouse, "Failed to enable stream mode\n");
		return -1;
	}

	return 0;
}

/*
 * Enable or disable passthrough mode to the trackstick. Must be in
 * command mode when calling this function.
 */
static int alps_passthrough_mode_v3(struct psmouse *psmouse, bool enable)
{
	int reg_val;

	reg_val = alps_command_mode_read_reg(psmouse, 0x0008);
	if (reg_val == -1)
		return -1;

	if (enable)
		reg_val |= 0x01;
	else
		reg_val &= ~0x01;

	if (__alps_command_mode_write_reg(psmouse, reg_val))
		return -1;

	return 0;
}

/* Must be in command mode when calling this function */
static int alps_absolute_mode_v3(struct psmouse *psmouse)
{
	int reg_val;

	reg_val = alps_command_mode_read_reg(psmouse, 0x0004);
	if (reg_val == -1)
		return -1;

	reg_val |= 0x06;
	if (__alps_command_mode_write_reg(psmouse, reg_val))
		return -1;

	return 0;
}

static int alps_hw_init_v3(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	int reg_val;
	unsigned char param[4];

	priv->nibble_commands = alps_v3_nibble_commands;
	priv->addr_command = PSMOUSE_CMD_RESET_WRAP;

	if (alps_enter_command_mode(psmouse, NULL))
		goto error;

	/* Check for trackstick */
	reg_val = alps_command_mode_read_reg(psmouse, 0x0008);
	if (reg_val == -1)
		goto error;
	if (reg_val & 0x80) {
		if (alps_passthrough_mode_v3(psmouse, true))
			goto error;
		if (alps_exit_command_mode(psmouse))
			goto error;

		/*
		 * E7 report for the trackstick
		 *
		 * There have been reports of failures to seem to trace back
		 * to the above trackstick check failing. When these occur
		 * this E7 report fails, so when that happens we continue
		 * with the assumption that there isn't a trackstick after
		 * all.
		 */
		param[0] = 0x64;
		if (ps2_command(ps2dev, NULL, PSMOUSE_CMD_SETSCALE21) ||
		    ps2_command(ps2dev, NULL, PSMOUSE_CMD_SETSCALE21) ||
		    ps2_command(ps2dev, NULL, PSMOUSE_CMD_SETSCALE21) ||
		    ps2_command(ps2dev, param, PSMOUSE_CMD_GETINFO)) {
			psmouse_warn(psmouse, "trackstick E7 report failed\n");
		} else {
			psmouse_dbg(psmouse,
				    "trackstick E7 report: %2.2x %2.2x %2.2x\n",
				    param[0], param[1], param[2]);

			/*
			 * Not sure what this does, but it is absolutely
			 * essential. Without it, the touchpad does not
			 * work at all and the trackstick just emits normal
			 * PS/2 packets.
			 */
			if (ps2_command(ps2dev, NULL, PSMOUSE_CMD_SETSCALE11) ||
			    ps2_command(ps2dev, NULL, PSMOUSE_CMD_SETSCALE11) ||
			    ps2_command(ps2dev, NULL, PSMOUSE_CMD_SETSCALE11) ||
			    alps_command_mode_send_nibble(psmouse, 0x9) ||
			    alps_command_mode_send_nibble(psmouse, 0x4)) {
				psmouse_err(psmouse,
					    "Error sending magic E6 sequence\n");
				goto error_passthrough;
			}
		}

		if (alps_enter_command_mode(psmouse, NULL))
			goto error_passthrough;
		if (alps_passthrough_mode_v3(psmouse, false))
			goto error;
	}

	if (alps_absolute_mode_v3(psmouse)) {
		psmouse_err(psmouse, "Failed to enter absolute mode\n");
		goto error;
	}

	reg_val = alps_command_mode_read_reg(psmouse, 0x0006);
	if (reg_val == -1)
		goto error;
	if (__alps_command_mode_write_reg(psmouse, reg_val | 0x01))
		goto error;

	reg_val = alps_command_mode_read_reg(psmouse, 0x0007);
	if (reg_val == -1)
		goto error;
	if (__alps_command_mode_write_reg(psmouse, reg_val | 0x01))
		goto error;

	if (alps_command_mode_read_reg(psmouse, 0x0144) == -1)
		goto error;
	if (__alps_command_mode_write_reg(psmouse, 0x04))
		goto error;

	if (alps_command_mode_read_reg(psmouse, 0x0159) == -1)
		goto error;
	if (__alps_command_mode_write_reg(psmouse, 0x03))
		goto error;

	if (alps_command_mode_read_reg(psmouse, 0x0163) == -1)
		goto error;
	if (alps_command_mode_write_reg(psmouse, 0x0163, 0x03))
		goto error;

	if (alps_command_mode_read_reg(psmouse, 0x0162) == -1)
		goto error;
	if (alps_command_mode_write_reg(psmouse, 0x0162, 0x04))
		goto error;

	/*
	 * This ensures the trackstick packets are in the format
	 * supported by this driver. If bit 1 isn't set the packet
	 * format is different.
	 */
	if (alps_command_mode_write_reg(psmouse, 0x0008, 0x82))
		goto error;

	alps_exit_command_mode(psmouse);

	/* Set rate and enable data reporting */
	param[0] = 0x64;
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_SETRATE) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_ENABLE)) {
		psmouse_err(psmouse, "Failed to enable data reporting\n");
		return -1;
	}

	return 0;

error_passthrough:
	/* Something failed while in passthrough mode, so try to get out */
	if (!alps_enter_command_mode(psmouse, NULL))
		alps_passthrough_mode_v3(psmouse, false);
error:
	/*
	 * Leaving the touchpad in command mode will essentially render
	 * it unusable until the machine reboots, so exit it here just
	 * to be safe
	 */
	alps_exit_command_mode(psmouse);
	return -1;
}

/* Must be in command mode when calling this function */
static int alps_absolute_mode_v4(struct psmouse *psmouse)
{
	int reg_val;

	reg_val = alps_command_mode_read_reg(psmouse, 0x0004);
	if (reg_val == -1)
		return -1;

	reg_val |= 0x02;
	if (__alps_command_mode_write_reg(psmouse, reg_val))
		return -1;

	return 0;
}

static int alps_hw_init_v4(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char param[4];

	priv->nibble_commands = alps_v4_nibble_commands;
	priv->addr_command = PSMOUSE_CMD_DISABLE;

	if (alps_enter_command_mode(psmouse, NULL))
		goto error;

	if (alps_absolute_mode_v4(psmouse)) {
		psmouse_err(psmouse, "Failed to enter absolute mode\n");
		goto error;
	}

	if (alps_command_mode_write_reg(psmouse, 0x0007, 0x8c))
		goto error;

	if (alps_command_mode_write_reg(psmouse, 0x0149, 0x03))
		goto error;

	if (alps_command_mode_write_reg(psmouse, 0x0160, 0x03))
		goto error;

	if (alps_command_mode_write_reg(psmouse, 0x017f, 0x15))
		goto error;

	if (alps_command_mode_write_reg(psmouse, 0x0151, 0x01))
		goto error;

	if (alps_command_mode_write_reg(psmouse, 0x0168, 0x03))
		goto error;

	if (alps_command_mode_write_reg(psmouse, 0x014a, 0x03))
		goto error;

	if (alps_command_mode_write_reg(psmouse, 0x0161, 0x03))
		goto error;

	alps_exit_command_mode(psmouse);

	/*
	 * This sequence changes the output from a 9-byte to an
	 * 8-byte format. All the same data seems to be present,
	 * just in a more compact format.
	 */
	param[0] = 0xc8;
	param[1] = 0x64;
	param[2] = 0x50;
	if (ps2_command(ps2dev, &param[0], PSMOUSE_CMD_SETRATE) ||
	    ps2_command(ps2dev, &param[1], PSMOUSE_CMD_SETRATE) ||
	    ps2_command(ps2dev, &param[2], PSMOUSE_CMD_SETRATE) ||
	    ps2_command(ps2dev, param, PSMOUSE_CMD_GETID))
		return -1;

	/* Set rate and enable data reporting */
	param[0] = 0x64;
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_SETRATE) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_ENABLE)) {
		psmouse_err(psmouse, "Failed to enable data reporting\n");
		return -1;
	}

	return 0;

error:
	/*
	 * Leaving the touchpad in command mode will essentially render
	 * it unusable until the machine reboots, so exit it here just
	 * to be safe
	 */
	alps_exit_command_mode(psmouse);
	return -1;
}

static int alps_hw_init(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;
	const struct alps_model_info *model = priv->i;
	int ret = -1;

	switch (model->proto_version) {
	case ALPS_PROTO_V1:
	case ALPS_PROTO_V2:
		ret = alps_hw_init_v1_v2(psmouse);
		break;
	case ALPS_PROTO_V3:
		ret = alps_hw_init_v3(psmouse);
		break;
	case ALPS_PROTO_V4:
		ret = alps_hw_init_v4(psmouse);
		break;
	}

	return ret;
}

static int alps_reconnect(struct psmouse *psmouse)
{
	const struct alps_model_info *model;

	psmouse_reset(psmouse);

	model = alps_get_model(psmouse, NULL);
	if (!model)
		return -1;

	return alps_hw_init(psmouse);
}

static void alps_disconnect(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;

	psmouse_reset(psmouse);
	del_timer_sync(&priv->timer);
	input_unregister_device(priv->dev2);
	kfree(priv);
}

int alps_init(struct psmouse *psmouse)
{
	struct alps_data *priv;
	const struct alps_model_info *model;
	struct input_dev *dev1 = psmouse->dev, *dev2;
	int version;

	priv = kzalloc(sizeof(struct alps_data), GFP_KERNEL);
	dev2 = input_allocate_device();
	if (!priv || !dev2)
		goto init_fail;

	priv->dev2 = dev2;
	setup_timer(&priv->timer, alps_flush_packet, (unsigned long)psmouse);

	psmouse->private = priv;

	psmouse_reset(psmouse);

	model = alps_get_model(psmouse, &version);
	if (!model)
		goto init_fail;

	priv->i = model;

	if (alps_hw_init(psmouse))
		goto init_fail;

	/*
	 * Undo part of setup done for us by psmouse core since touchpad
	 * is not a relative device.
	 */
	__clear_bit(EV_REL, dev1->evbit);
	__clear_bit(REL_X, dev1->relbit);
	__clear_bit(REL_Y, dev1->relbit);

	/*
	 * Now set up our capabilities.
	 */
	dev1->evbit[BIT_WORD(EV_KEY)] |= BIT_MASK(EV_KEY);
	dev1->keybit[BIT_WORD(BTN_TOUCH)] |= BIT_MASK(BTN_TOUCH);
	dev1->keybit[BIT_WORD(BTN_TOOL_FINGER)] |= BIT_MASK(BTN_TOOL_FINGER);
	dev1->keybit[BIT_WORD(BTN_LEFT)] |=
		BIT_MASK(BTN_LEFT) | BIT_MASK(BTN_RIGHT);

	dev1->evbit[BIT_WORD(EV_ABS)] |= BIT_MASK(EV_ABS);

	switch (model->proto_version) {
	case ALPS_PROTO_V1:
	case ALPS_PROTO_V2:
		input_set_abs_params(dev1, ABS_X, 0, 1023, 0, 0);
		input_set_abs_params(dev1, ABS_Y, 0, 767, 0, 0);
		break;
	case ALPS_PROTO_V3:
		set_bit(INPUT_PROP_SEMI_MT, dev1->propbit);
		input_mt_init_slots(dev1, 2);
		input_set_abs_params(dev1, ABS_MT_POSITION_X, 0, ALPS_V3_X_MAX, 0, 0);
		input_set_abs_params(dev1, ABS_MT_POSITION_Y, 0, ALPS_V3_Y_MAX, 0, 0);

		set_bit(BTN_TOOL_DOUBLETAP, dev1->keybit);
		set_bit(BTN_TOOL_TRIPLETAP, dev1->keybit);
		set_bit(BTN_TOOL_QUADTAP, dev1->keybit);
		/* fall through */
	case ALPS_PROTO_V4:
		input_set_abs_params(dev1, ABS_X, 0, ALPS_V3_X_MAX, 0, 0);
		input_set_abs_params(dev1, ABS_Y, 0, ALPS_V3_Y_MAX, 0, 0);
		break;
	}

	input_set_abs_params(dev1, ABS_PRESSURE, 0, 127, 0, 0);

	if (model->flags & ALPS_WHEEL) {
		dev1->evbit[BIT_WORD(EV_REL)] |= BIT_MASK(EV_REL);
		dev1->relbit[BIT_WORD(REL_WHEEL)] |= BIT_MASK(REL_WHEEL);
	}

	if (model->flags & (ALPS_FW_BK_1 | ALPS_FW_BK_2)) {
		dev1->keybit[BIT_WORD(BTN_FORWARD)] |= BIT_MASK(BTN_FORWARD);
		dev1->keybit[BIT_WORD(BTN_BACK)] |= BIT_MASK(BTN_BACK);
	}

	if (model->flags & ALPS_FOUR_BUTTONS) {
		dev1->keybit[BIT_WORD(BTN_0)] |= BIT_MASK(BTN_0);
		dev1->keybit[BIT_WORD(BTN_1)] |= BIT_MASK(BTN_1);
		dev1->keybit[BIT_WORD(BTN_2)] |= BIT_MASK(BTN_2);
		dev1->keybit[BIT_WORD(BTN_3)] |= BIT_MASK(BTN_3);
	} else {
		dev1->keybit[BIT_WORD(BTN_MIDDLE)] |= BIT_MASK(BTN_MIDDLE);
	}

	snprintf(priv->phys, sizeof(priv->phys), "%s/input1", psmouse->ps2dev.serio->phys);
	dev2->phys = priv->phys;
	dev2->name = (model->flags & ALPS_DUALPOINT) ? "DualPoint Stick" : "PS/2 Mouse";
	dev2->id.bustype = BUS_I8042;
	dev2->id.vendor  = 0x0002;
	dev2->id.product = PSMOUSE_ALPS;
	dev2->id.version = 0x0000;
	dev2->dev.parent = &psmouse->ps2dev.serio->dev;

	dev2->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REL);
	dev2->relbit[BIT_WORD(REL_X)] = BIT_MASK(REL_X) | BIT_MASK(REL_Y);
	dev2->keybit[BIT_WORD(BTN_LEFT)] =
		BIT_MASK(BTN_LEFT) | BIT_MASK(BTN_MIDDLE) | BIT_MASK(BTN_RIGHT);

	if (input_register_device(priv->dev2))
		goto init_fail;

	psmouse->protocol_handler = alps_process_byte;
	psmouse->poll = alps_poll;
	psmouse->disconnect = alps_disconnect;
	psmouse->reconnect = alps_reconnect;
	psmouse->pktsize = model->proto_version == ALPS_PROTO_V4 ? 8 : 6;

	/* We are having trouble resyncing ALPS touchpads so disable it for now */
	psmouse->resync_time = 0;

	/* Allow 2 invalid packets without resetting device */
	psmouse->resetafter = psmouse->pktsize * 2;

	return 0;

init_fail:
	psmouse_reset(psmouse);
	input_free_device(dev2);
	kfree(priv);
	psmouse->private = NULL;
	return -1;
}

int alps_detect(struct psmouse *psmouse, bool set_properties)
{
	int version;
	const struct alps_model_info *model;

	model = alps_get_model(psmouse, &version);
	if (!model)
		return -1;

	if (set_properties) {
		psmouse->vendor = "ALPS";
		psmouse->name = model->flags & ALPS_DUALPOINT ?
				"DualPoint TouchPad" : "GlidePoint";
		psmouse->model = version;
	}
	return 0;
}

