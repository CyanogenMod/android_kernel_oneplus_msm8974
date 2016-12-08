/*
 *  linux/fs/ext4/bitmap.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 */

#include <linux/buffer_head.h>
#include <linux/jbd2.h>
#include "ext4.h"

static const int nibblemap[] = {4, 3, 3, 2, 3, 2, 2, 1, 3, 2, 2, 1, 2, 1, 1, 0};

unsigned int ext4_count_free(char *bitmap, unsigned int numchars)
{
	unsigned int i, sum = 0;

	for (i = 0; i < numchars; i++)
		sum += nibblemap[bitmap[i] & 0xf] +
			nibblemap[(bitmap[i] >> 4) & 0xf];
	return sum;
}

