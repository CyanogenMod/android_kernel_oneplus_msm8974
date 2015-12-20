/*
 *  Generic fillrect for frame buffers with packed pixels of any depth.
 *
 *      Copyright (C)  2000 James Simmons (jsimmons@linux-fbdev.org)
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 *
 * NOTES:
 *
 *  Also need to add code to deal with cards endians that are different than
 *  the native cpu endians. I also need to deal with MSB position in the word.
 *
 * Modified by Harm Hanemaaijer (fgenfb@yahoo.com 2013):
 *
 * The previous implementation was doing reads from the uncached framebuffer,
 * even for simple fills, which for performance reasons, on most platforms,
 * is ill-advised unless strictly necessary to conform to the raster operation.
 * In these circumstances, doing 64-bit access on 64-bit systems does not serve
 * much purpose except for probing for corner-case bugs and race conditions in
 * the hardware's framebuffer bus implementation. For 16bpp, it is better to
 * take advantage of write-combining features of the framebuffer and write a
 * half-word if required for the left and right edges.
 *
 * Additionally, on most platforms, integer divides are relatively slow so are
 * best avoided, especially in inner loops.
 */
#include <linux/module.h>
#include <linux/string.h>
#include <linux/fb.h>
#include <asm/types.h>
#include "fb_draw.h"

#if BITS_PER_LONG == 32
#  define FB_WRITEL fb_writel
#  define FB_READL  fb_readl
#else
#  define FB_WRITEL fb_writeq
#  define FB_READL  fb_readq
#endif

    /*
     *  Aligned pattern fill using 32/64-bit memory accesses
     */

static void
bitfill_aligned(struct fb_info *p, unsigned long __iomem *dst, int dst_idx,
		unsigned long pat, unsigned n, int bits, u32 bswapmask)
{
	unsigned long first, last;

	if (!n)
		return;

	first = fb_shifted_pixels_mask_long(p, dst_idx, bswapmask);
	last = ~fb_shifted_pixels_mask_long(p, (dst_idx+n) % bits, bswapmask);

	if (dst_idx+n <= bits) {
		// Single word
		if (last)
			first &= last;
		FB_WRITEL(comp(pat, FB_READL(dst), first), dst);
	} else {
		// Multiple destination words

		// Leading bits
		if (first!= ~0UL) {
			FB_WRITEL(comp(pat, FB_READL(dst), first), dst);
			dst++;
			n -= bits - dst_idx;
		}

		// Main chunk
		n /= bits;
		while (n >= 8) {
			FB_WRITEL(pat, dst++);
			FB_WRITEL(pat, dst++);
			FB_WRITEL(pat, dst++);
			FB_WRITEL(pat, dst++);
			FB_WRITEL(pat, dst++);
			FB_WRITEL(pat, dst++);
			FB_WRITEL(pat, dst++);
			FB_WRITEL(pat, dst++);
			n -= 8;
		}
		while (n--)
			FB_WRITEL(pat, dst++);

		// Trailing bits
		if (last)
			FB_WRITEL(comp(pat, FB_READL(dst), last), dst);
	}
}


    /*
     *  Unaligned generic pattern fill using 32/64-bit memory accesses
     *  The pattern must have been expanded to a full 32/64-bit value
     *  Left/right are the appropriate shifts to convert to the pattern to be
     *  used for the next 32/64-bit word
     */

static void
bitfill_unaligned(struct fb_info *p, unsigned long __iomem *dst, int dst_idx,
		  unsigned long pat, int left, int right, unsigned n, int bits)
{
	unsigned long first, last;

	if (!n)
		return;

	first = FB_SHIFT_HIGH(p, ~0UL, dst_idx);
	last = ~(FB_SHIFT_HIGH(p, ~0UL, (dst_idx+n) % bits));

	if (dst_idx+n <= bits) {
		// Single word
		if (last)
			first &= last;
		FB_WRITEL(comp(pat, FB_READL(dst), first), dst);
	} else {
		// Multiple destination words
		// Leading bits
		if (first) {
			FB_WRITEL(comp(pat, FB_READL(dst), first), dst);
			dst++;
			pat = pat << left | pat >> right;
			n -= bits - dst_idx;
		}

		// Main chunk
		n /= bits;
		while (n >= 4) {
			FB_WRITEL(pat, dst++);
			pat = pat << left | pat >> right;
			FB_WRITEL(pat, dst++);
			pat = pat << left | pat >> right;
			FB_WRITEL(pat, dst++);
			pat = pat << left | pat >> right;
			FB_WRITEL(pat, dst++);
			pat = pat << left | pat >> right;
			n -= 4;
		}
		while (n--) {
			FB_WRITEL(pat, dst++);
			pat = pat << left | pat >> right;
		}

		// Trailing bits
		if (last)
			FB_WRITEL(comp(pat, FB_READL(dst), last), dst);
	}
}

    /*
     *  Aligned pattern invert using 32/64-bit memory accesses
     */
static void
bitfill_aligned_rev(struct fb_info *p, unsigned long __iomem *dst,
		    int dst_idx, unsigned long pat, unsigned n, int bits,
		    u32 bswapmask)
{
	unsigned long val = pat, dat;
	unsigned long first, last;

	if (!n)
		return;

	first = fb_shifted_pixels_mask_long(p, dst_idx, bswapmask);
	last = ~fb_shifted_pixels_mask_long(p, (dst_idx+n) % bits, bswapmask);

	if (dst_idx+n <= bits) {
		// Single word
		if (last)
			first &= last;
		dat = FB_READL(dst);
		FB_WRITEL(comp(dat ^ val, dat, first), dst);
	} else {
		// Multiple destination words
		// Leading bits
		if (first!=0UL) {
			dat = FB_READL(dst);
			FB_WRITEL(comp(dat ^ val, dat, first), dst);
			dst++;
			n -= bits - dst_idx;
		}

		// Main chunk
		n /= bits;
		while (n >= 8) {
			FB_WRITEL(FB_READL(dst) ^ val, dst);
			dst++;
			FB_WRITEL(FB_READL(dst) ^ val, dst);
			dst++;
			FB_WRITEL(FB_READL(dst) ^ val, dst);
			dst++;
			FB_WRITEL(FB_READL(dst) ^ val, dst);
			dst++;
			FB_WRITEL(FB_READL(dst) ^ val, dst);
			dst++;
			FB_WRITEL(FB_READL(dst) ^ val, dst);
			dst++;
			FB_WRITEL(FB_READL(dst) ^ val, dst);
			dst++;
			FB_WRITEL(FB_READL(dst) ^ val, dst);
			dst++;
			n -= 8;
		}
		while (n--) {
			FB_WRITEL(FB_READL(dst) ^ val, dst);
			dst++;
		}
		// Trailing bits
		if (last) {
			dat = FB_READL(dst);
			FB_WRITEL(comp(dat ^ val, dat, last), dst);
		}
	}
}


    /*
     *  Unaligned generic pattern invert using 32/64-bit memory accesses
     *  The pattern must have been expanded to a full 32/64-bit value
     *  Left/right are the appropriate shifts to convert to the pattern to be
     *  used for the next 32/64-bit word
     */

static void
bitfill_unaligned_rev(struct fb_info *p, unsigned long __iomem *dst,
		      int dst_idx, unsigned long pat, int left, int right,
		      unsigned n, int bits)
{
	unsigned long first, last, dat;

	if (!n)
		return;

	first = FB_SHIFT_HIGH(p, ~0UL, dst_idx);
	last = ~(FB_SHIFT_HIGH(p, ~0UL, (dst_idx+n) % bits));

	if (dst_idx+n <= bits) {
		// Single word
		if (last)
			first &= last;
		dat = FB_READL(dst);
		FB_WRITEL(comp(dat ^ pat, dat, first), dst);
	} else {
		// Multiple destination words

		// Leading bits
		if (first != 0UL) {
			dat = FB_READL(dst);
			FB_WRITEL(comp(dat ^ pat, dat, first), dst);
			dst++;
			pat = pat << left | pat >> right;
			n -= bits - dst_idx;
		}

		// Main chunk
		n /= bits;
		while (n >= 4) {
			FB_WRITEL(FB_READL(dst) ^ pat, dst);
			dst++;
			pat = pat << left | pat >> right;
			FB_WRITEL(FB_READL(dst) ^ pat, dst);
			dst++;
			pat = pat << left | pat >> right;
			FB_WRITEL(FB_READL(dst) ^ pat, dst);
			dst++;
			pat = pat << left | pat >> right;
			FB_WRITEL(FB_READL(dst) ^ pat, dst);
			dst++;
			pat = pat << left | pat >> right;
			n -= 4;
		}
		while (n--) {
			FB_WRITEL(FB_READL(dst) ^ pat, dst);
			dst++;
			pat = pat << left | pat >> right;
		}

		// Trailing bits
		if (last) {
			dat = FB_READL(dst);
			FB_WRITEL(comp(dat ^ pat, dat, last), dst);
		}
	}
}

static void
fast_fill16(struct fb_info *p, unsigned long __iomem *dst, int dst_idx,
	     unsigned long pat, u32 width_in_bits, u32 height)
{
	for (; height--; dst_idx += p->fix.line_length * 8) {
		u32 n;
		unsigned long __iomem *dstp;
		u32 last_bits;
		dst += dst_idx >> (ffs(BITS_PER_LONG) - 1);
		dst_idx &= (BITS_PER_LONG - 1);
		n = width_in_bits;
		dstp = dst;
#if BITS_PER_LONG == 32
		if (dst_idx) {
			fb_writew(pat, (u16 *)dstp + 1);
			dstp++;
			n -= 16;
			if (n == 0)
				continue;
		}
		else if (n == 16) {
			fb_writew(pat, (u16 *)dstp);
			continue;
		}	
#else /* BITS_PER_LONG == 64 */
		if (dst_idx) {
			if (dst_idx == 16) {
				fb_writew(pat, (u16 *)dstp + 1);
				if (n == 32) {
					fb_writew(pat, (u16 *)dstp + 2);
					continue;
				}
				fb_writel(pat, (u32 *)dstp + 1);
			}
			else if (dst_idx == 32) {
				if (n == 16) {
					fb_writew(pat, (u16 *)dstp  + 2);
					continue;
				}
				fb_writel(pat, (u32 *)dstp + 1);
			}
			else if (dst_idx == 48) {
				fb_writew(pat, (u16 *)dstp + 3);
			dstp++;
			n -= 64 - dist_idx;
			if (n == 0)
				continue;
		}
#endif
		n /= BITS_PER_LONG;
		while (n >= 8) {
			FB_WRITEL(pat, dstp++);
			FB_WRITEL(pat, dstp++);
			FB_WRITEL(pat, dstp++);
			FB_WRITEL(pat, dstp++);
			FB_WRITEL(pat, dstp++);
			FB_WRITEL(pat, dstp++);
			FB_WRITEL(pat, dstp++);
			FB_WRITEL(pat, dstp++);
			n -= 8;
		}
		while (n--)
			FB_WRITEL(pat, dstp++);
		last_bits = (dst_idx + width_in_bits) % BITS_PER_LONG;
#if BITS_PER_LONG == 32
		if (last_bits)
			fb_writew(pat, dstp);
#else /* BITS_PER_LONG == 64 */
		if (last_bits & 32) {
			fb_writel(pat, dstp);
			if (last_bits & 16)
				fb_writew(pat, (u16 *)dstp + 2);
		}
		else if (last_bits & 16)
			fb_writew(pat, dstp);
#endif
	}
}

static void
fast_fill32(struct fb_info *p, unsigned long __iomem *dst, int dst_idx,
	     unsigned long pat, u32 width_in_bits, u32 height)
{
	for (; height--; dst_idx += p->fix.line_length * 8) {
		u32 n;
		unsigned long __iomem *dstp;
		dst += dst_idx >> (ffs(BITS_PER_LONG) - 1);
		dst_idx &= (BITS_PER_LONG - 1);
		n = width_in_bits;
		dstp = dst;
#if BITS_PER_LONG == 64
		if (dst_idx) {
			fb_writel(pat, (u32 *)dstp + 1);
			dstp++;
			n -= 32;
			if (n == 0)
				continue;
		}
		else if (n == 32) {
			fb_writel(pat, dstp);
			continue;
		}	
#endif
		n /= BITS_PER_LONG;
		while (n >= 8) {
			FB_WRITEL(pat, dstp++);
			FB_WRITEL(pat, dstp++);
			FB_WRITEL(pat, dstp++);
			FB_WRITEL(pat, dstp++);
			FB_WRITEL(pat, dstp++);
			FB_WRITEL(pat, dstp++);
			FB_WRITEL(pat, dstp++);
			FB_WRITEL(pat, dstp++);
			n -= 8;
		}
		while (n--)
			FB_WRITEL(pat, dstp++);
#if BITS_PER_LONG == 64
		if ((dst_idx + width_in_bits) % 64)
			fb_writel(pat, dstp);
#endif
	}
}

void cfb_fillrect(struct fb_info *p, const struct fb_fillrect *rect)
{
	unsigned long pat, pat2, fg;
	unsigned long width = rect->width, height = rect->height;
	int bits = BITS_PER_LONG, bytes = bits >> 3;
	u32 bpp = p->var.bits_per_pixel;
	unsigned long __iomem *dst;
	int dst_idx, left;

	if (p->state != FBINFO_STATE_RUNNING)
		return;

	if (p->fix.visual == FB_VISUAL_TRUECOLOR ||
	    p->fix.visual == FB_VISUAL_DIRECTCOLOR )
		fg = ((u32 *) (p->pseudo_palette))[rect->color];
	else
		fg = rect->color;

	pat = pixel_to_pat(bpp, fg);

	dst = (unsigned long __iomem *)((unsigned long)p->screen_base & ~(bytes-1));
	dst_idx = ((unsigned long)p->screen_base & (bytes - 1))*8;
	dst_idx += rect->dy*p->fix.line_length*8+rect->dx*bpp;
	/* FIXME For now we support 1-32 bpp only */
	if (p->fbops->fb_sync)
		p->fbops->fb_sync(p);
	/*
         * Note: fb_be_math(p) could be used to check fb endianness, but
         * __LITTLE_ENDIAN is used later in the function, so also use it here.
	 */
#if !defined(CONFIG_FB_CFB_REV_PIXELS_IN_BYTE) && defined(__LITTLE_ENDIAN)
	if (rect->rop == ROP_COPY) {
		if (bpp == 16) {
			fast_fill16(p, dst, dst_idx, pat, width * 16, height);
			return;
		}
		else if (bpp == 32) {
			fast_fill32(p, dst, dst_idx, pat, width * 32, height);
			return;
		}
	}
#endif
	left = bits % bpp;
	if (!left) {
		u32 bswapmask = fb_compute_bswapmask(p);
		void (*fill_op32)(struct fb_info *p,
				  unsigned long __iomem *dst, int dst_idx,
		                  unsigned long pat, unsigned n, int bits,
				  u32 bswapmask) = NULL;

		switch (rect->rop) {
		case ROP_XOR:
			fill_op32 = bitfill_aligned_rev;
			break;
		case ROP_COPY:
			fill_op32 = bitfill_aligned;
			break;
		default:
			printk( KERN_ERR "cfb_fillrect(): unknown rop, defaulting to ROP_COPY\n");
			fill_op32 = bitfill_aligned;
			break;
		}
		while (height--) {
			dst += dst_idx >> (ffs(bits) - 1);
			dst_idx &= (bits - 1);
			fill_op32(p, dst, dst_idx, pat, width*bpp, bits,
				  bswapmask);
			dst_idx += p->fix.line_length*8;
		}
	} else {
		int right, r;
		void (*fill_op)(struct fb_info *p, unsigned long __iomem *dst,
				int dst_idx, unsigned long pat, int left,
				int right, unsigned n, int bits) = NULL;
#ifdef __LITTLE_ENDIAN
		right = left;
		left = bpp - right;
#else
		right = bpp - left;
#endif
		switch (rect->rop) {
		case ROP_XOR:
			fill_op = bitfill_unaligned_rev;
			break;
		case ROP_COPY:
			fill_op = bitfill_unaligned;
			break;
		default:
			printk(KERN_ERR "cfb_fillrect(): unknown rop, defaulting to ROP_COPY\n");
			fill_op = bitfill_unaligned;
			break;
		}
		while (height--) {
			dst += dst_idx / bits;
			dst_idx &= (bits - 1);
			r = dst_idx % bpp;
			/* rotate pattern to the correct start position */
			pat2 = le_long_to_cpu(rolx(cpu_to_le_long(pat), r, bpp));
			fill_op(p, dst, dst_idx, pat2, left, right,
				width*bpp, bits);
			dst_idx += p->fix.line_length*8;
		}
	}
}

EXPORT_SYMBOL(cfb_fillrect);

MODULE_AUTHOR("James Simmons <jsimmons@users.sf.net>");
MODULE_DESCRIPTION("Generic software accelerated fill rectangle");
MODULE_LICENSE("GPL");
