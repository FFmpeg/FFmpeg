/*
 * DSP utils : average functions are compiled twice for 3dnow/mmx2
 * Copyright (c) 2000, 2001 Gerard Lantau.
 * Copyright (c) 2002 Michael Niedermayer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * MMX optimization by Nick Kurshev <nickols_k@mail.ru>
 * mostly rewritten by Michael Niedermayer <michaelni@gmx.at>
 */
 
/* XXX: we use explicit registers to avoid a gcc 2.95.2 register asm
   clobber bug - now it will work with 2.95.2 and also with -fPIC
 */
static void DEF(put_pixels_x2)(UINT8 *block, const UINT8 *pixels, int line_size, int h)
{
    __asm __volatile(
        "xorl %%eax, %%eax		\n\t"
        ".balign 16			\n\t"
        "1:				\n\t"
	"movq (%1, %%eax), %%mm0	\n\t"
	"movq 1(%1, %%eax), %%mm1	\n\t"
	"movq (%2, %%eax), %%mm2	\n\t"
	"movq 1(%2, %%eax), %%mm3	\n\t"
	PAVGB" %%mm1, %%mm0		\n\t"
	PAVGB" %%mm3, %%mm2		\n\t"
	"movq %%mm0, (%3, %%eax)	\n\t"
	"movq %%mm2, (%4, %%eax)	\n\t"
        "addl %5, %%eax			\n\t"
	"movq (%1, %%eax), %%mm0	\n\t"
	"movq 1(%1, %%eax), %%mm1	\n\t"
	"movq (%2, %%eax), %%mm2	\n\t"
	"movq 1(%2, %%eax), %%mm3	\n\t"
	PAVGB" %%mm1, %%mm0		\n\t"
	PAVGB" %%mm3, %%mm2		\n\t"
	"movq %%mm0, (%3, %%eax)	\n\t"
	"movq %%mm2, (%4, %%eax)	\n\t"
        "addl %5, %%eax			\n\t"
        "subl $4, %0			\n\t"
        " jnz 1b			\n\t"
	:"+g"(h)
        :"D"(pixels), "S"(pixels+line_size), "r" (block), "r" (block+line_size),
        "g"(line_size<<1)
	:"%eax", "memory");
}
 
/* GL: this function does incorrect rounding if overflow */
static void DEF(put_no_rnd_pixels_x2)(UINT8 *block, const UINT8 *pixels, int line_size, int h)
{
    __asm __volatile(
        "xorl %%eax, %%eax		\n\t"
	MOVQ_BONE(%%mm7)
        ".balign 16			\n\t"
        "1:				\n\t"
	"movq (%1, %%eax), %%mm0	\n\t"
	"movq 1(%1, %%eax), %%mm1	\n\t"
	"movq (%2, %%eax), %%mm2	\n\t"
	"movq 1(%2, %%eax), %%mm3	\n\t"
        "psubusb %%mm7, %%mm0		\n\t"
        "psubusb %%mm7, %%mm2		\n\t"
	PAVGB" %%mm1, %%mm0		\n\t"
	PAVGB" %%mm3, %%mm2		\n\t"
	"movq %%mm0, (%3, %%eax)	\n\t"
	"movq %%mm2, (%4, %%eax)	\n\t"
        "addl %5, %%eax			\n\t"
	"movq (%1, %%eax), %%mm0	\n\t"
	"movq 1(%1, %%eax), %%mm1	\n\t"
	"movq (%2, %%eax), %%mm2	\n\t"
	"movq 1(%2, %%eax), %%mm3	\n\t"
        "psubusb %%mm7, %%mm0		\n\t"
        "psubusb %%mm7, %%mm2		\n\t"
	PAVGB" %%mm1, %%mm0		\n\t"
	PAVGB" %%mm3, %%mm2		\n\t"
	"movq %%mm0, (%3, %%eax)	\n\t"
	"movq %%mm2, (%4, %%eax)	\n\t"
        "addl %5, %%eax			\n\t"
        "subl $4, %0			\n\t"
        " jnz 1b			\n\t"
	:"+g"(h)
        :"D"(pixels), "S"(pixels+line_size), "r" (block), "r" (block+line_size),
        "r"(line_size<<1)
	:"%eax", "memory");
}

static void DEF(put_pixels_y2)(UINT8 *block, const UINT8 *pixels, int line_size, int h)
{
#if 1
    // Michael - measure me
    __asm __volatile(
        "lea (%3, %3), %%eax            \n\t"
        "movq (%1), %%mm0		\n\t"
        "subl %3, %2			\n\t"
        ".balign 16			\n\t"
        "1:				\n\t"
	"movq (%1, %3), %%mm1		\n\t"
	"movq (%1, %3, 2), %%mm2	\n\t"
	PAVGB" %%mm1, %%mm0		\n\t"
	PAVGB" %%mm2, %%mm1		\n\t"
        "addl %%eax, %1			\n\t"
	"movq %%mm0, (%2, %3)		\n\t"
	"movq %%mm1, (%2, %3, 2)	\n\t"
	"movq (%1, %3), %%mm1		\n\t"
	"movq (%1, %3, 2), %%mm0	\n\t"
	PAVGB" %%mm1, %%mm2		\n\t"
	PAVGB" %%mm0, %%mm1		\n\t"
        "addl %%eax, %2			\n\t"
        "addl %%eax, %1			\n\t"
	"movq %%mm2, (%2, %3)		\n\t"
	"movq %%mm1, (%2, %3, 2)	\n\t"
        "addl %%eax, %2			\n\t"
        "subl $4, %0			\n\t"
        "jnz 1b				\n\t"
	:"+g"(h)
	:"D"(pixels), "S" (block), "c"(line_size)
	:"%eax", "memory");
#else
   // kabi measure me
    __asm __volatile(
	"movq (%2), %%mm0		\n\t"
        "addl %1, %2			\n\t"
        "xorl %%eax, %%eax		\n\t"
        "leal (%1, %2), %%edi		\n\t"
        "leal (%1, %3), %%esi		\n\t"
        "addl %1, %1			\n\t"
        ".balign 16			\n\t"
        "1:				\n\t"
	"movq (%2   , %%eax), %%mm1	\n\t"
	"movq (%%edi, %%eax), %%mm2	\n\t"
	PAVGB" %%mm1, %%mm0		\n\t"
	PAVGB" %%mm2, %%mm1		\n\t"
	"movq %%mm0, (%3   , %%eax)	\n\t"
	"movq %%mm1, (%%esi, %%eax)	\n\t"
        "addl %1, %%eax			\n\t"
	"movq (%2   , %%eax), %%mm1	\n\t"
	"movq (%%edi, %%eax), %%mm0	\n\t"
	PAVGB" %%mm1, %%mm2		\n\t"
	PAVGB" %%mm0, %%mm1		\n\t"
	"movq %%mm2, (%3   , %%eax)	\n\t"
	"movq %%mm1, (%%esi, %%eax)	\n\t"
        "addl %1, %%eax			\n\t"
        "subl $4, %0			\n\t"
        " jnz 1b			\n\t"
	:"+g"(h), "+r"(line_size), "+r"(pixels)
	: "r" (block)
	: "%eax", "%esi", "%edi", "memory");
#endif
}

/* GL: this function does incorrect rounding if overflow */
static void DEF(put_no_rnd_pixels_y2)(UINT8 *block, const UINT8 *pixels, int line_size, int h)
{
    __asm __volatile(
	MOVQ_BONE(%%mm7)
        "xorl %%eax, %%eax		\n\t"
	"movq (%1), %%mm0		\n\t"
        ".balign 16			\n\t"
        "1:				\n\t"
	"movq (%2, %%eax), %%mm1	\n\t"
	"movq (%3, %%eax), %%mm2	\n\t"
        "psubusb %%mm7, %%mm1		\n\t"
	PAVGB" %%mm1, %%mm0		\n\t"
	PAVGB" %%mm2, %%mm1		\n\t"
	"movq %%mm0, (%4, %%eax)	\n\t"
	"movq %%mm1, (%5, %%eax)	\n\t"
        "addl %6, %%eax			\n\t"
	"movq (%2, %%eax), %%mm1	\n\t"
	"movq (%3, %%eax), %%mm0	\n\t"
        "psubusb %%mm7, %%mm1		\n\t"
	PAVGB" %%mm1, %%mm2		\n\t"
	PAVGB" %%mm0, %%mm1		\n\t"
	"movq %%mm2, (%4, %%eax)	\n\t"
	"movq %%mm1, (%5, %%eax)	\n\t"
        "addl %6, %%eax			\n\t"
        "subl $4, %0			\n\t"
        " jnz 1b			\n\t"
	:"+g"(h)
	:"D"(pixels), "S"(pixels+line_size), "r"(pixels+line_size*2), "r" (block),
         "r" (block+line_size), "g"(line_size<<1)
	:"%eax",  "memory");
}

static void DEF(avg_pixels)(UINT8 *block, const UINT8 *pixels, int line_size, int h)
{
    __asm __volatile(
        "xorl %%eax, %%eax		\n\t"
        ".balign 16			\n\t"
        "1:				\n\t"
	"movq (%1, %%eax), %%mm0	\n\t"
	"movq (%2, %%eax), %%mm2	\n\t"
	"movq (%3, %%eax), %%mm3	\n\t"
	"movq (%4, %%eax), %%mm4	\n\t"
	PAVGB" %%mm3, %%mm0		\n\t"
	PAVGB" %%mm4, %%mm2		\n\t"
	"movq %%mm0, (%3, %%eax)	\n\t"
	"movq %%mm2, (%4, %%eax)	\n\t"
        "addl %5, %%eax			\n\t"
	"movq (%1, %%eax), %%mm0	\n\t"
	"movq (%2, %%eax), %%mm2	\n\t"
	"movq (%3, %%eax), %%mm3	\n\t"
	"movq (%4, %%eax), %%mm4	\n\t"
	PAVGB" %%mm3, %%mm0		\n\t"
	PAVGB" %%mm4, %%mm2		\n\t"
	"movq %%mm0, (%3, %%eax)	\n\t"
	"movq %%mm2, (%4, %%eax)	\n\t"
        "addl %5, %%eax			\n\t"
        "subl $4, %0			\n\t"
        " jnz 1b			\n\t"
	:"+g"(h)
        :"D"(pixels), "S"(pixels+line_size), "r" (block), "r" (block+line_size),
         "g"(line_size<<1)
	:"%eax", "memory");
}

static void DEF(avg_pixels_x2)(UINT8 *block, const UINT8 *pixels, int line_size, int h)
{
    __asm __volatile(
        "xorl %%eax, %%eax		\n\t"
        ".balign 16			\n\t"
        "1:				\n\t"
	"movq (%1, %%eax), %%mm0	\n\t"
	"movq 1(%1, %%eax), %%mm1	\n\t"
	"movq (%2, %%eax), %%mm2	\n\t"
	"movq 1(%2, %%eax), %%mm3	\n\t"
	PAVGB" %%mm1, %%mm0		\n\t"
	PAVGB" %%mm3, %%mm2		\n\t"
	"movq (%3, %%eax), %%mm3	\n\t"
	"movq (%4, %%eax), %%mm4	\n\t"
	PAVGB" %%mm3, %%mm0		\n\t"
	PAVGB" %%mm4, %%mm2		\n\t"
	"movq %%mm0, (%3, %%eax)	\n\t"
	"movq %%mm2, (%4, %%eax)	\n\t"
        "addl %5, %%eax			\n\t"
	"movq (%1, %%eax), %%mm0	\n\t"
	"movq 1(%1, %%eax), %%mm1	\n\t"
	"movq (%2, %%eax), %%mm2	\n\t"
	"movq 1(%2, %%eax), %%mm3	\n\t"
	PAVGB" %%mm1, %%mm0		\n\t"
	PAVGB" %%mm3, %%mm2		\n\t"
	"movq (%3, %%eax), %%mm3	\n\t"
	"movq (%4, %%eax), %%mm4	\n\t"
	PAVGB" %%mm3, %%mm0		\n\t"
	PAVGB" %%mm4, %%mm2		\n\t"
	"movq %%mm0, (%3, %%eax)	\n\t"
	"movq %%mm2, (%4, %%eax)	\n\t"
        "addl %5, %%eax			\n\t"
        "subl $4, %0			\n\t"
        " jnz 1b			\n\t"
	:"+g"(h)
        :"D"(pixels), "S"(pixels+line_size), "r" (block), "r" (block+line_size),
         "g"(line_size<<1)
	:"%eax", "memory");
}

static void DEF(avg_pixels_y2)(UINT8 *block, const UINT8 *pixels, int line_size, int h)
{
    __asm __volatile(
        "xorl %%eax, %%eax		\n\t"
	"movq (%1), %%mm0		\n\t"
        ".balign 16			\n\t"
        "1:				\n\t"
	"movq (%2, %%eax), %%mm1	\n\t"
	"movq (%3, %%eax), %%mm2	\n\t"
	PAVGB" %%mm1, %%mm0		\n\t"
	PAVGB" %%mm2, %%mm1		\n\t"
	"movq (%4, %%eax), %%mm3	\n\t"
	"movq (%5, %%eax), %%mm4	\n\t"
	PAVGB" %%mm3, %%mm0		\n\t"
	PAVGB" %%mm4, %%mm1		\n\t"
	"movq %%mm0, (%4, %%eax)	\n\t"
	"movq %%mm1, (%5, %%eax)	\n\t"
        "addl %6, %%eax			\n\t"
	"movq (%2, %%eax), %%mm1	\n\t"
	"movq (%3, %%eax), %%mm0	\n\t"
	PAVGB" %%mm1, %%mm2		\n\t"
	PAVGB" %%mm0, %%mm1		\n\t"
	"movq (%4, %%eax), %%mm3	\n\t"
	"movq (%5, %%eax), %%mm4	\n\t"
	PAVGB" %%mm3, %%mm2		\n\t"
	PAVGB" %%mm4, %%mm1		\n\t"
	"movq %%mm2, (%4, %%eax)	\n\t"
	"movq %%mm1, (%5, %%eax)	\n\t"
        "addl %6, %%eax			\n\t"
        "subl $4, %0			\n\t"
        " jnz 1b			\n\t"
	:"+g"(h)
	:"D"(pixels), "S"(pixels+line_size), "r"(pixels+line_size*2), "r" (block),
         "r" (block+line_size), "g"(line_size<<1)
	:"%eax",  "memory");
}

// Note this is not correctly rounded, but this function is only used for b frames so it doesnt matter 
static void DEF(avg_pixels_xy2)(UINT8 *block, const UINT8 *pixels, int line_size, int h)
{
    __asm __volatile(
	MOVQ_BONE(%%mm7)
        "xorl %%eax, %%eax		\n\t"
	"movq (%1), %%mm0		\n\t"
	"movq 1(%1), %%mm1		\n\t"
	PAVGB" %%mm1, %%mm0		\n\t"
        ".balign 16			\n\t"
        "1:				\n\t"
	"movq (%2, %%eax), %%mm1	\n\t"
	"movq (%3, %%eax), %%mm2	\n\t"
	"movq 1(%2, %%eax), %%mm3	\n\t"
	"movq 1(%3, %%eax), %%mm4	\n\t"
        "psubusb %%mm7, %%mm2		\n\t"
	PAVGB" %%mm3, %%mm1		\n\t"
	PAVGB" %%mm4, %%mm2		\n\t"
	PAVGB" %%mm1, %%mm0		\n\t"
	PAVGB" %%mm2, %%mm1		\n\t"
	"movq (%4, %%eax), %%mm3	\n\t"
	"movq (%5, %%eax), %%mm4	\n\t"
	PAVGB" %%mm3, %%mm0		\n\t"
	PAVGB" %%mm4, %%mm1		\n\t"
	"movq %%mm0, (%4, %%eax)	\n\t"
	"movq %%mm1, (%5, %%eax)	\n\t"
        "addl %6, %%eax			\n\t"
	"movq (%2, %%eax), %%mm1	\n\t"
	"movq (%3, %%eax), %%mm0	\n\t"
	"movq 1(%2, %%eax), %%mm3	\n\t"
	"movq 1(%3, %%eax), %%mm4	\n\t"
	PAVGB" %%mm3, %%mm1		\n\t"
	PAVGB" %%mm4, %%mm0		\n\t"
	PAVGB" %%mm1, %%mm2		\n\t"
	PAVGB" %%mm0, %%mm1		\n\t"
	"movq (%4, %%eax), %%mm3	\n\t"
	"movq (%5, %%eax), %%mm4	\n\t"
	PAVGB" %%mm3, %%mm2		\n\t"
	PAVGB" %%mm4, %%mm1		\n\t"
	"movq %%mm2, (%4, %%eax)	\n\t"
	"movq %%mm1, (%5, %%eax)	\n\t"
        "addl %6, %%eax			\n\t"
        "subl $4, %0			\n\t"
        " jnz 1b			\n\t"
	:"+g"(h)
	:"D"(pixels), "S"(pixels+line_size), "r"(pixels+line_size*2), "r" (block),
         "r" (block+line_size), "g"(line_size<<1)
	:"%eax",  "memory");
}
