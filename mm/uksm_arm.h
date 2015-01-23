#ifndef _UKSM_ARM_H
#define _UKSM_ARM_H

#include <asm/page.h>

#undef memcmp
#define memcmp uksm_memcmp

static inline int uksm_memcmp(const void *s1, const void *s2, size_t n)
{
	register int r1, r2, r3;
	__asm__(
	"	ldr	%4, [%1], #4\n"
	"	ldr	%3, [%0], #4\n"
	"	b	2f\n"

	"1:	pld	[%0, #28]\n"
	"	pld	[%1, #28]\n"
	"	ldr	%5, [%1], #4\n"
	"	teq	%3, %4\n"
	"	ldr	%3, [%0], #4\n"
	"	bne	3f\n"
	"	ldr	%4, [%1], #4\n"
	"	teq	%3, %5\n"
	"	ldr	%3, [%0], #4\n"
	"	bne	3f\n"
	"	ldr	%5, [%1], #4\n"
	"	teq	%3, %4\n"
	"	ldr	%3, [%0], #4\n"
	"	bne	3f\n"
	"	ldr	%4, [%1], #4\n"
	"	teq	%3, %5\n"
	"	ldr	%3, [%0], #4\n"
	"	bne	3f\n"
	"2:	ldr	%5, [%1], #4\n"
	"	teq	%3, %4\n"
	"	ldr	%3, [%0], #4\n"
	"	bne	3f\n"
	"	ldr	%4, [%1], #4\n"
	"	teq	%3, %5\n"
	"	ldr	%3, [%0], #4\n"
	"	bne	3f\n"
	"	ldr	%5, [%1], #4\n"
	"	teq	%3, %4\n"
	"	ldr	%3, [%0], #4\n"
	"	bne	3f\n"
	"	teq	%3, %5\n"
	"	bne	3f\n"

	"	subs	%2, %2, #32\n"
	"	ldrpl	%4, [%1], #4\n"
	"	ldrpl	%3, [%0], #4\n"
	"	bhi	1b\n"
	"	beq	2b\n"
	"3:	add	%2, %2, #32\n"
	: "+Qr" (s1), "+Qr" (s2), "+r" (n),
	  "=&r" (r1), "=&r" (r2), "=&r" (r3)
	: : "cc");

	return n;
}

static inline int is_full_zero(void *s1, size_t n)
{
	register int r1, r2, r3;
	__asm__(
	"	ldr	%4, [%0], #4\n"
	"	b	2f\n"

	"1:	pld	[%0, #28]\n"
	"	ldr	%2, [%0], #4\n"
	"	ldr	%3, [%0], #4\n"
	"	orrs	%4, %4, %2\n"
	"	bne	3f\n"
	"	ldr	%2, [%0], #4\n"
	"	ldr	%4, [%0], #4\n"
	"	orrs	%3, %3, %2\n"
	"	bne	3f\n"
	"2:	ldr	%2, [%0], #4\n"
	"	ldr	%3, [%0], #4\n"
	"	orrs	%4, %4, %2\n"
	"	ldr	%2, [%0], #4\n"
	"	bne	3f\n"
	"	orrs	%3, %3, %2\n"
	"	bne	3f\n"

	"	subs	%1, %1, #32\n"
	"	ldrpl	%4, [%0], #4\n"
	"	bhi	1b\n"
	"	beq	2b\n"
	"3:	add	%1, %1, #32\n"
	: "+Qr" (s1), "+r" (n),
	  "=&r" (r1), "=&r" (r2), "=&r" (r3)
	: : "cc");

	return !n;
}

#endif
