/*
 *  Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "exfat_config.h"
#include "exfat_global.h"

INT32 __wstrchr(UINT16 *str, UINT16 wchar)
{
	while (*str) {
		if (*(str++) == wchar) return(1);
	}
	return(0);
}

INT32 __wstrlen(UINT16 *str)
{
	INT32 length = 0;

	while (*(str++)) length++;
	return(length);
}

#define BITMAP_LOC(v)           ((v) >> 3)
#define BITMAP_SHIFT(v)         ((v) & 0x07)

void Bitmap_set_all(UINT8 *bitmap, INT32 mapsize)
{
	MEMSET(bitmap, 0xFF, mapsize);
}

void Bitmap_clear_all(UINT8 *bitmap, INT32 mapsize)
{
	MEMSET(bitmap, 0x0, mapsize);
}

INT32 Bitmap_test(UINT8 *bitmap, INT32 i)
{
	UINT8 data;

	data = bitmap[BITMAP_LOC(i)];
	if ((data >> BITMAP_SHIFT(i)) & 0x01) return(1);
	return(0);
}

void Bitmap_set(UINT8 *bitmap, INT32 i)
{
	bitmap[BITMAP_LOC(i)] |= (0x01 << BITMAP_SHIFT(i));
}

void Bitmap_clear(UINT8 *bitmap, INT32 i)
{
	bitmap[BITMAP_LOC(i)] &= ~(0x01 << BITMAP_SHIFT(i));
}

void Bitmap_nbits_set(UINT8 *bitmap, INT32 offset, INT32 nbits)
{
	INT32   i;

	for (i = 0; i < nbits; i++) {
		Bitmap_set(bitmap, offset+i);
	}
}

void Bitmap_nbits_clear(UINT8 *bitmap, INT32 offset, INT32 nbits)
{
	INT32   i;

	for (i = 0; i < nbits; i++) {
		Bitmap_clear(bitmap, offset+i);
	}
}

void my_itoa(INT8 *buf, INT32 v)
{
	INT32 mod[10];
	INT32 i;

	for (i = 0; i < 10; i++) {
		mod[i] = (v % 10);
		v = v / 10;
		if (v == 0) break;
	}

	if (i == 10)
		i--;

	for (; i >= 0; i--) {
		*buf = (UINT8) ('0' + mod[i]);
		buf++;
	}
	*buf = '\0';
}

INT32 my_log2(UINT32 v)
{
	UINT32 bits = 0;

	while (v > 1) {
		if (v & 0x01) return(-1);
		v >>= 1;
		bits++;
	}
	return(bits);
}
