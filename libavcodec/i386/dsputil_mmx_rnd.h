/*
 * DSP utils mmx functions are compiled twice for rnd/no_rnd
 * Copyright (c) 2000, 2001 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * MMX optimization by Nick Kurshev <nickols_k@mail.ru>
 * mostly rewritten by Michael Niedermayer <michaelni@gmx.at>
 * and improved by Zdenek Kabelac <kabi@users.sf.net>
 */

// put_pixels
static void DEF(put, pixels_x2)(UINT8 *block, const UINT8 *pixels, int line_size, int h)
{
    __asm __volatile(
	MOVQ_BFE(%%mm7)
	"lea	(%3, %3), %%eax		\n\t"
	".balign 8			\n\t"
	"1:				\n\t"
	"movq	(%1), %%mm0		\n\t"
	"movq	1(%1), %%mm1		\n\t"
	"movq	(%1, %3), %%mm2		\n\t"
	"movq	1(%1, %3), %%mm3	\n\t"
	PAVGBP(%%mm0, %%mm1, %%mm5,   %%mm2, %%mm3, %%mm6)
	"movq	%%mm5, (%2)		\n\t"
	"movq	%%mm6, (%2, %3)		\n\t"
	"addl	%%eax, %1		\n\t"
	"addl	%%eax, %2		\n\t"
	"movq	(%1), %%mm0		\n\t"
	"movq	1(%1), %%mm1		\n\t"
	"movq	(%1, %3), %%mm2		\n\t"
	"movq	1(%1, %3), %%mm3	\n\t"
	PAVGBP(%%mm0, %%mm1, %%mm5,   %%mm2, %%mm3, %%mm6)
	"movq	%%mm5, (%2)		\n\t"
	"movq	%%mm6, (%2, %3)		\n\t"
	"addl	%%eax, %1		\n\t"
	"addl	%%eax, %2		\n\t"
	"subl	$4, %0			\n\t"
	"jnz	1b			\n\t"
	:"+g"(h), "+S"(pixels), "+D"(block)
	:"r"(line_size)
	:"eax", "memory");
}

static void DEF(put, pixels_y2)(UINT8 *block, const UINT8 *pixels, int line_size, int h)
{
  __asm __volatile(
	MOVQ_BFE(%%mm7)
	"lea (%3, %3), %%eax		\n\t"
	"movq (%1), %%mm0		\n\t"
	".balign 8			\n\t"
	"1:				\n\t"
	"movq	(%1, %3), %%mm1		\n\t"
	"movq	(%1, %%eax),%%mm2	\n\t"
	PAVGBP(%%mm1, %%mm0, %%mm5,   %%mm2, %%mm1, %%mm6)
        "movq	%%mm5, (%2)		\n\t"
        "movq	%%mm6, (%2, %3)		\n\t"
	"addl	%%eax, %1		\n\t"
	"addl	%%eax, %2		\n\t"
	"movq	(%1, %3), %%mm1		\n\t"
	"movq	(%1, %%eax),%%mm0	\n\t"
	PAVGBP(%%mm1, %%mm2, %%mm5,   %%mm0, %%mm1, %%mm6)
	"movq	%%mm5, (%2)		\n\t"
	"movq	%%mm6, (%2, %3)		\n\t"
	"addl	%%eax, %1		\n\t"
	"addl	%%eax, %2		\n\t"
	"subl	$4, %0			\n\t"
	"jnz	1b			\n\t"
	:"+g"(h), "+S"(pixels), "+D"(block)
	:"r"(line_size)
	:"eax", "memory");
}

// ((a + b)/2 + (c + d)/2)/2
// not sure if this is properly replacing original code
// - ok it's really unsable at this moment -> disabled
static void DEF(put, disabled_pixels_xy2)(UINT8 *block, const UINT8 *pixels, int line_size, int h)
{
    __asm __volatile(
	MOVQ_BFE(%%mm7)
	"lea (%3, %3), %%eax		\n\t"
	"movq (%1), %%mm0		\n\t"
	"movq	(%1), %%mm0		\n\t"
	"movq	1(%1), %%mm1		\n\t"
	".balign 8			\n\t"
	"1:				\n\t"
	"movq	(%1, %3), %%mm2		\n\t"
	"movq	1(%1, %3), %%mm3	\n\t"
	PAVGBP(%%mm2, %%mm0, %%mm4,   %%mm3, %%mm1, %%mm5)
	//PAVGBR(%%mm2, %%mm0, %%mm4)
	//PAVGBR(%%mm3, %%mm1, %%mm5)
	PAVGB(%%mm4, %%mm5)
	"movq	%%mm6, (%2)		\n\t"

	"movq	(%1, %%eax), %%mm0	\n\t"
	"movq	1(%1, %%eax), %%mm1	\n\t"
	PAVGBP(%%mm0, %%mm2, %%mm4,   %%mm1, %%mm3, %%mm5)
	//PAVGBR(%%mm0, %%mm2, %%mm4)
	//PAVGBR(%%mm1, %%mm3, %%mm5)
	PAVGB(%%mm4, %%mm5)
	"movq	%%mm6, (%2, %3)		\n\t"
	"addl	%%eax, %1		\n\t"
	"addl	%%eax, %2		\n\t"

	"subl	$2, %0			\n\t"

	"jnz	1b			\n\t"
	:"+g"(h), "+S"(pixels), "+D"(block)
	:"r"(line_size)
	:"eax", "memory");
}

// avg_pixels

