/*
 * MMX optimized DSP utils
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
 */

#include "../dsputil.h"
#include "../simple_idct.h"

int mm_flags; /* multimedia extension flags */

int pix_abs16x16_mmx(UINT8 *blk1, UINT8 *blk2, int lx);
int pix_abs16x16_x2_mmx(UINT8 *blk1, UINT8 *blk2, int lx);
int pix_abs16x16_y2_mmx(UINT8 *blk1, UINT8 *blk2, int lx);
int pix_abs16x16_xy2_mmx(UINT8 *blk1, UINT8 *blk2, int lx);

int pix_abs16x16_mmx2(UINT8 *blk1, UINT8 *blk2, int lx);
int pix_abs16x16_x2_mmx2(UINT8 *blk1, UINT8 *blk2, int lx);
int pix_abs16x16_y2_mmx2(UINT8 *blk1, UINT8 *blk2, int lx);
int pix_abs16x16_xy2_mmx2(UINT8 *blk1, UINT8 *blk2, int lx);

int pix_abs8x8_mmx(UINT8 *blk1, UINT8 *blk2, int lx);
int pix_abs8x8_x2_mmx(UINT8 *blk1, UINT8 *blk2, int lx);
int pix_abs8x8_y2_mmx(UINT8 *blk1, UINT8 *blk2, int lx);
int pix_abs8x8_xy2_mmx(UINT8 *blk1, UINT8 *blk2, int lx);

int pix_abs8x8_mmx2(UINT8 *blk1, UINT8 *blk2, int lx);
int pix_abs8x8_x2_mmx2(UINT8 *blk1, UINT8 *blk2, int lx);
int pix_abs8x8_y2_mmx2(UINT8 *blk1, UINT8 *blk2, int lx);
int pix_abs8x8_xy2_mmx2(UINT8 *blk1, UINT8 *blk2, int lx);

/* external functions, from idct_mmx.c */
void ff_mmx_idct(DCTELEM *block);
void ff_mmxext_idct(DCTELEM *block);

/* pixel operations */
static const uint64_t mm_bone __attribute__ ((aligned(8))) = 0x0101010101010101ULL;
static const uint64_t mm_wone __attribute__ ((aligned(8))) = 0x0001000100010001ULL;
static const uint64_t mm_wtwo __attribute__ ((aligned(8))) = 0x0002000200020002ULL;

#define JUMPALIGN() __asm __volatile (".balign 8"::)
#define MOVQ_ZERO(regd)  __asm __volatile ("pxor %%" #regd ", %%" #regd ::)

#define MOVQ_WONE(regd) \
    __asm __volatile ( \
    "pcmpeqd %%" #regd ", %%" #regd " \n\t" \
    "psrlw $15, %%" #regd ::)

#define MOVQ_BFE(regd) \
    __asm __volatile ( \
    "pcmpeqd %%" #regd ", %%" #regd " \n\t"\
    "paddb %%" #regd ", %%" #regd " \n\t" ::)

#ifndef PIC
#define MOVQ_BONE(regd)  __asm __volatile ("movq %0, %%" #regd " \n\t" ::"m"(mm_bone))
#define MOVQ_WTWO(regd)  __asm __volatile ("movq %0, %%" #regd " \n\t" ::"m"(mm_wtwo))
#else
// for shared library it's better to use this way for accessing constants
// pcmpeqd -> -1
#define MOVQ_BONE(regd) \
    __asm __volatile ( \
    "pcmpeqd %%" #regd ", %%" #regd " \n\t" \
    "psrlw $15, %%" #regd " \n\t" \
    "packuswb %%" #regd ", %%" #regd " \n\t" ::)

#define MOVQ_WTWO(regd) \
    __asm __volatile ( \
    "pcmpeqd %%" #regd ", %%" #regd " \n\t" \
    "psrlw $15, %%" #regd " \n\t" \
    "psllw $1, %%" #regd " \n\t"::)

#endif

// using regr as temporary and for the output result
// first argument is unmodifed and second is trashed
// regfe is supposed to contain 0xfefefefefefefefe
#define PAVGB_MMX_NO_RND(rega, regb, regr, regfe) \
    "movq " #rega ", " #regr "	\n\t"\
    "pand " #regb ", " #regr "	\n\t"\
    "pxor " #rega ", " #regb "	\n\t"\
    "pand " #regfe "," #regb "	\n\t"\
    "psrlq $1, " #regb " 	\n\t"\
    "paddb " #regb ", " #regr "	\n\t"

#define PAVGB_MMX(rega, regb, regr, regfe) \
    "movq " #rega ", " #regr "	\n\t"\
    "por  " #regb ", " #regr "	\n\t"\
    "pxor " #rega ", " #regb "	\n\t"\
    "pand " #regfe "," #regb "	\n\t"\
    "psrlq $1, " #regb "	\n\t"\
    "psubb " #regb ", " #regr "	\n\t"

// mm6 is supposed to contain 0xfefefefefefefefe
#define PAVGBP_MMX_NO_RND(rega, regb, regr,  regc, regd, regp) \
    "movq " #rega ", " #regr "	\n\t"\
    "movq " #regc ", " #regp "	\n\t"\
    "pand " #regb ", " #regr "	\n\t"\
    "pand " #regd ", " #regp "	\n\t"\
    "pxor " #rega ", " #regb "	\n\t"\
    "pxor " #regc ", " #regd "	\n\t"\
    "pand %%mm6, " #regb "	\n\t"\
    "pand %%mm6, " #regd "	\n\t"\
    "psrlq $1, " #regb " 	\n\t"\
    "psrlq $1, " #regd " 	\n\t"\
    "paddb " #regb ", " #regr "	\n\t"\
    "paddb " #regd ", " #regp "	\n\t"

#define PAVGBP_MMX(rega, regb, regr, regc, regd, regp) \
    "movq " #rega ", " #regr "	\n\t"\
    "movq " #regc ", " #regp "	\n\t"\
    "por  " #regb ", " #regr "	\n\t"\
    "por  " #regd ", " #regp "	\n\t"\
    "pxor " #rega ", " #regb "	\n\t"\
    "pxor " #regc ", " #regd "	\n\t"\
    "pand %%mm6, " #regb "     	\n\t"\
    "pand %%mm6, " #regd "     	\n\t"\
    "psrlq $1, " #regd "	\n\t"\
    "psrlq $1, " #regb "	\n\t"\
    "psubb " #regb ", " #regr "	\n\t"\
    "psubb " #regd ", " #regp "	\n\t"

/***********************************/
/* MMX no rounding */
#define DEF(x, y) x ## _no_rnd_ ## y ##_mmx
#define SET_RND  MOVQ_WONE
#define PAVGBP(a, b, c, d, e, f)	PAVGBP_MMX_NO_RND(a, b, c, d, e, f)
#define PAVGB(a, b, c, e)		PAVGB_MMX_NO_RND(a, b, c, e)

#include "dsputil_mmx_rnd.h"

#undef DEF
#undef SET_RND
#undef PAVGBP
#undef PAVGB
/***********************************/
/* MMX rounding */

#define DEF(x, y) x ## _ ## y ##_mmx
#define SET_RND  MOVQ_WTWO
#define PAVGBP(a, b, c, d, e, f)	PAVGBP_MMX(a, b, c, d, e, f)
#define PAVGB(a, b, c, e)		PAVGB_MMX(a, b, c, e)

#include "dsputil_mmx_rnd.h"

#undef DEF
#undef SET_RND
#undef PAVGBP
#undef PAVGB

/***********************************/
/* 3Dnow specific */

#define DEF(x) x ## _3dnow
/* for Athlons PAVGUSB is prefered */
#define PAVGB "pavgusb"

#include "dsputil_mmx_avg.h"

#undef DEF
#undef PAVGB

/***********************************/
/* MMX2 specific */

#define DEF(x) x ## _mmx2

/* Introduced only in MMX2 set */
#define PAVGB "pavgb"

#include "dsputil_mmx_avg.h"

#undef DEF
#undef PAVGB

/***********************************/
/* standard MMX */

static void get_pixels_mmx(DCTELEM *block, const UINT8 *pixels, int line_size)
{
    asm volatile(
        "movl $-128, %%eax	\n\t"
        "pxor %%mm7, %%mm7	\n\t"
        ".balign 16		\n\t"
        "1:			\n\t"
        "movq (%0), %%mm0	\n\t"
        "movq (%0, %2), %%mm2	\n\t"
        "movq %%mm0, %%mm1	\n\t"
        "movq %%mm2, %%mm3	\n\t"
        "punpcklbw %%mm7, %%mm0	\n\t"
        "punpckhbw %%mm7, %%mm1	\n\t"
        "punpcklbw %%mm7, %%mm2	\n\t"
        "punpckhbw %%mm7, %%mm3	\n\t"
        "movq %%mm0, (%1, %%eax)\n\t"
        "movq %%mm1, 8(%1, %%eax)\n\t"
        "movq %%mm2, 16(%1, %%eax)\n\t"
        "movq %%mm3, 24(%1, %%eax)\n\t"
        "addl %3, %0		\n\t"
        "addl $32, %%eax	\n\t"
        "js 1b			\n\t"
        : "+r" (pixels)
        : "r" (block+64), "r" (line_size), "r" (line_size*2)
        : "%eax"
    );
}

static void diff_pixels_mmx(DCTELEM *block, const UINT8 *s1, const UINT8 *s2, int stride)
{
    asm volatile(
        "pxor %%mm7, %%mm7	\n\t"
        "movl $-128, %%eax	\n\t"
        ".balign 16		\n\t"
        "1:			\n\t"
        "movq (%0), %%mm0	\n\t"
        "movq (%1), %%mm2	\n\t"
        "movq %%mm0, %%mm1	\n\t"
        "movq %%mm2, %%mm3	\n\t"
        "punpcklbw %%mm7, %%mm0	\n\t"
        "punpckhbw %%mm7, %%mm1	\n\t"
        "punpcklbw %%mm7, %%mm2	\n\t"
        "punpckhbw %%mm7, %%mm3	\n\t"
        "psubw %%mm2, %%mm0	\n\t"
        "psubw %%mm3, %%mm1	\n\t"
        "movq %%mm0, (%2, %%eax)\n\t"
        "movq %%mm1, 8(%2, %%eax)\n\t"
        "addl %3, %0		\n\t"
        "addl %3, %1		\n\t"
        "addl $16, %%eax	\n\t"
        "jnz 1b			\n\t"
        : "+r" (s1), "+r" (s2)
        : "r" (block+64), "r" (stride)
        : "%eax"
    );
}

static void put_pixels_clamped_mmx(const DCTELEM *block, UINT8 *pixels, int line_size)
{
    const DCTELEM *p;
    UINT8 *pix;

    /* read the pixels */
    p = block;
    pix = pixels;
    /* unrolled loop */
	__asm __volatile(
		"movq	%3, %%mm0\n\t"
		"movq	8%3, %%mm1\n\t"
		"movq	16%3, %%mm2\n\t"
		"movq	24%3, %%mm3\n\t"
		"movq	32%3, %%mm4\n\t"
		"movq	40%3, %%mm5\n\t"
		"movq	48%3, %%mm6\n\t"
		"movq	56%3, %%mm7\n\t"
		"packuswb %%mm1, %%mm0\n\t"
		"packuswb %%mm3, %%mm2\n\t"
		"packuswb %%mm5, %%mm4\n\t"
		"packuswb %%mm7, %%mm6\n\t"
		"movq	%%mm0, (%0)\n\t"
		"movq	%%mm2, (%0, %1)\n\t"
		"movq	%%mm4, (%0, %1, 2)\n\t"
		"movq	%%mm6, (%0, %2)\n\t"
		::"r" (pix), "r" (line_size), "r" (line_size*3), "m"(*p)
		:"memory");
        pix += line_size*4;
        p += 32;

    // if here would be an exact copy of the code above
    // compiler would generate some very strange code
    // thus using "r"
    __asm __volatile(
	    "movq	(%3), %%mm0\n\t"
	    "movq	8(%3), %%mm1\n\t"
	    "movq	16(%3), %%mm2\n\t"
	    "movq	24(%3), %%mm3\n\t"
	    "movq	32(%3), %%mm4\n\t"
	    "movq	40(%3), %%mm5\n\t"
	    "movq	48(%3), %%mm6\n\t"
	    "movq	56(%3), %%mm7\n\t"
	    "packuswb %%mm1, %%mm0\n\t"
	    "packuswb %%mm3, %%mm2\n\t"
	    "packuswb %%mm5, %%mm4\n\t"
	    "packuswb %%mm7, %%mm6\n\t"
	    "movq	%%mm0, (%0)\n\t"
	    "movq	%%mm2, (%0, %1)\n\t"
	    "movq	%%mm4, (%0, %1, 2)\n\t"
	    "movq	%%mm6, (%0, %2)\n\t"
	    ::"r" (pix), "r" (line_size), "r" (line_size*3), "r"(p)
	    :"memory");
}

static void add_pixels_clamped_mmx(const DCTELEM *block, UINT8 *pixels, int line_size)
{
    const DCTELEM *p;
    UINT8 *pix;
    int i;

    /* read the pixels */
    p = block;
    pix = pixels;
    MOVQ_ZERO(mm7);
    i = 4;
    do {
	__asm __volatile(
		"movq	(%2), %%mm0\n\t"
		"movq	8(%2), %%mm1\n\t"
		"movq	16(%2), %%mm2\n\t"
		"movq	24(%2), %%mm3\n\t"
		"movq	%0, %%mm4\n\t"
		"movq	%1, %%mm6\n\t"
		"movq	%%mm4, %%mm5\n\t"
		"punpcklbw %%mm7, %%mm4\n\t"
		"punpckhbw %%mm7, %%mm5\n\t"
		"paddsw	%%mm4, %%mm0\n\t"
		"paddsw	%%mm5, %%mm1\n\t"
		"movq	%%mm6, %%mm5\n\t"
		"punpcklbw %%mm7, %%mm6\n\t"
		"punpckhbw %%mm7, %%mm5\n\t"
		"paddsw	%%mm6, %%mm2\n\t"
		"paddsw	%%mm5, %%mm3\n\t"
		"packuswb %%mm1, %%mm0\n\t"
		"packuswb %%mm3, %%mm2\n\t"
		"movq	%%mm0, %0\n\t"
		"movq	%%mm2, %1\n\t"
		:"+m"(*pix), "+m"(*(pix+line_size))
		:"r"(p)
		:"memory");
        pix += line_size*2;
        p += 16;
    } while (--i);
}

static void put_pixels8_mmx(UINT8 *block, const UINT8 *pixels, int line_size, int h)
{
    __asm __volatile(
	 "lea (%3, %3), %%eax		\n\t"
	 ".balign 8			\n\t"
	 "1:				\n\t"
	 "movq (%1), %%mm0		\n\t"
	 "movq (%1, %3), %%mm1		\n\t"
     	 "movq %%mm0, (%2)		\n\t"
	 "movq %%mm1, (%2, %3)		\n\t"
	 "addl %%eax, %1		\n\t"
         "addl %%eax, %2       		\n\t"
	 "movq (%1), %%mm0		\n\t"
	 "movq (%1, %3), %%mm1		\n\t"
	 "movq %%mm0, (%2)		\n\t"
	 "movq %%mm1, (%2, %3)		\n\t"
	 "addl %%eax, %1		\n\t"
	 "addl %%eax, %2       		\n\t"
	 "subl $4, %0			\n\t"
	 "jnz 1b			\n\t"
	 : "+g"(h), "+r" (pixels),  "+r" (block)
	 : "r"(line_size)
	 : "%eax", "memory"
	);
}

static void put_pixels16_mmx(UINT8 *block, const UINT8 *pixels, int line_size, int h)
{
    __asm __volatile(
	 "lea (%3, %3), %%eax		\n\t"
	 ".balign 8			\n\t"
	 "1:				\n\t"
	 "movq (%1), %%mm0		\n\t"
	 "movq 8(%1), %%mm4		\n\t"
	 "movq (%1, %3), %%mm1		\n\t"
	 "movq 8(%1, %3), %%mm5		\n\t"
     	 "movq %%mm0, (%2)		\n\t"
     	 "movq %%mm4, 8(%2)		\n\t"
	 "movq %%mm1, (%2, %3)		\n\t"
	 "movq %%mm5, 8(%2, %3)		\n\t"
	 "addl %%eax, %1		\n\t"
         "addl %%eax, %2       		\n\t"
	 "movq (%1), %%mm0		\n\t"
	 "movq 8(%1), %%mm4		\n\t"
	 "movq (%1, %3), %%mm1		\n\t"
	 "movq 8(%1, %3), %%mm5		\n\t"
	 "movq %%mm0, (%2)		\n\t"
	 "movq %%mm4, 8(%2)		\n\t"
	 "movq %%mm1, (%2, %3)		\n\t"
	 "movq %%mm5, 8(%2, %3)		\n\t"
	 "addl %%eax, %1		\n\t"
	 "addl %%eax, %2       		\n\t"
	 "subl $4, %0			\n\t"
	 "jnz 1b			\n\t"
	 : "+g"(h), "+r" (pixels),  "+r" (block)
	 : "r"(line_size)
	 : "%eax", "memory"
	);
}

static void clear_blocks_mmx(DCTELEM *blocks)
{
    __asm __volatile(
                "pxor %%mm7, %%mm7		\n\t"
                "movl $-128*6, %%eax		\n\t"
                "1:				\n\t"
                "movq %%mm7, (%0, %%eax)	\n\t"
                "movq %%mm7, 8(%0, %%eax)	\n\t"
                "movq %%mm7, 16(%0, %%eax)	\n\t"
                "movq %%mm7, 24(%0, %%eax)	\n\t"
                "addl $32, %%eax		\n\t"
                " js 1b				\n\t"
                : : "r" (((int)blocks)+128*6)
                : "%eax"
        );
}

#if 0
static void just_return() { return; }
#endif

void dsputil_init_mmx(void)
{
    mm_flags = mm_support();
#if 0
    fprintf(stderr, "libavcodec: CPU flags:");
    if (mm_flags & MM_MMX)
        fprintf(stderr, " mmx");
    if (mm_flags & MM_MMXEXT)
        fprintf(stderr, " mmxext");
    if (mm_flags & MM_3DNOW)
        fprintf(stderr, " 3dnow");
    if (mm_flags & MM_SSE)
        fprintf(stderr, " sse");
    if (mm_flags & MM_SSE2)
        fprintf(stderr, " sse2");
    fprintf(stderr, "\n");
#endif

    if (mm_flags & MM_MMX) {
        get_pixels = get_pixels_mmx;
        diff_pixels = diff_pixels_mmx;
        put_pixels_clamped = put_pixels_clamped_mmx;
        add_pixels_clamped = add_pixels_clamped_mmx;
        clear_blocks= clear_blocks_mmx;

        pix_abs16x16     = pix_abs16x16_mmx;
        pix_abs16x16_x2  = pix_abs16x16_x2_mmx;
        pix_abs16x16_y2  = pix_abs16x16_y2_mmx;
        pix_abs16x16_xy2 = pix_abs16x16_xy2_mmx;
        pix_abs8x8    = pix_abs8x8_mmx;
        pix_abs8x8_x2 = pix_abs8x8_x2_mmx;
        pix_abs8x8_y2 = pix_abs8x8_y2_mmx;
        pix_abs8x8_xy2= pix_abs8x8_xy2_mmx;

        put_pixels_tab[0][0] = put_pixels16_mmx;
        put_pixels_tab[0][1] = put_pixels16_x2_mmx;
        put_pixels_tab[0][2] = put_pixels16_y2_mmx;
        put_pixels_tab[0][3] = put_pixels16_xy2_mmx;

        put_no_rnd_pixels_tab[0][0] = put_pixels16_mmx;
        put_no_rnd_pixels_tab[0][1] = put_no_rnd_pixels16_x2_mmx;
        put_no_rnd_pixels_tab[0][2] = put_no_rnd_pixels16_y2_mmx;
        put_no_rnd_pixels_tab[0][3] = put_no_rnd_pixels16_xy2_mmx;

        avg_pixels_tab[0][0] = avg_pixels16_mmx;
        avg_pixels_tab[0][1] = avg_pixels16_x2_mmx;
        avg_pixels_tab[0][2] = avg_pixels16_y2_mmx;
        avg_pixels_tab[0][3] = avg_pixels16_xy2_mmx;

        avg_no_rnd_pixels_tab[0][0] = avg_no_rnd_pixels16_mmx;
        avg_no_rnd_pixels_tab[0][1] = avg_no_rnd_pixels16_x2_mmx;
        avg_no_rnd_pixels_tab[0][2] = avg_no_rnd_pixels16_y2_mmx;
        avg_no_rnd_pixels_tab[0][3] = avg_no_rnd_pixels16_xy2_mmx;
        
        put_pixels_tab[1][0] = put_pixels8_mmx;
        put_pixels_tab[1][1] = put_pixels8_x2_mmx;
        put_pixels_tab[1][2] = put_pixels8_y2_mmx;
        put_pixels_tab[1][3] = put_pixels8_xy2_mmx;

        put_no_rnd_pixels_tab[1][0] = put_pixels8_mmx;
        put_no_rnd_pixels_tab[1][1] = put_no_rnd_pixels8_x2_mmx;
        put_no_rnd_pixels_tab[1][2] = put_no_rnd_pixels8_y2_mmx;
        put_no_rnd_pixels_tab[1][3] = put_no_rnd_pixels8_xy2_mmx;

        avg_pixels_tab[1][0] = avg_pixels8_mmx;
        avg_pixels_tab[1][1] = avg_pixels8_x2_mmx;
        avg_pixels_tab[1][2] = avg_pixels8_y2_mmx;
        avg_pixels_tab[1][3] = avg_pixels8_xy2_mmx;

        avg_no_rnd_pixels_tab[1][0] = avg_no_rnd_pixels8_mmx;
        avg_no_rnd_pixels_tab[1][1] = avg_no_rnd_pixels8_x2_mmx;
        avg_no_rnd_pixels_tab[1][2] = avg_no_rnd_pixels8_y2_mmx;
        avg_no_rnd_pixels_tab[1][3] = avg_no_rnd_pixels8_xy2_mmx;

        if (mm_flags & MM_MMXEXT) {
            pix_abs16x16    = pix_abs16x16_mmx2;
            pix_abs16x16_x2 = pix_abs16x16_x2_mmx2;
            pix_abs16x16_y2 = pix_abs16x16_y2_mmx2;
            pix_abs16x16_xy2= pix_abs16x16_xy2_mmx2;

            pix_abs8x8    = pix_abs8x8_mmx2;
            pix_abs8x8_x2 = pix_abs8x8_x2_mmx2;
            pix_abs8x8_y2 = pix_abs8x8_y2_mmx2;
            pix_abs8x8_xy2= pix_abs8x8_xy2_mmx2;

            put_pixels_tab[0][1] = put_pixels16_x2_mmx2;
            put_pixels_tab[0][2] = put_pixels16_y2_mmx2;
            put_no_rnd_pixels_tab[0][1] = put_no_rnd_pixels16_x2_mmx2;
            put_no_rnd_pixels_tab[0][2] = put_no_rnd_pixels16_y2_mmx2;

            avg_pixels_tab[0][0] = avg_pixels16_mmx2;
            avg_pixels_tab[0][1] = avg_pixels16_x2_mmx2;
            avg_pixels_tab[0][2] = avg_pixels16_y2_mmx2;
            avg_pixels_tab[0][3] = avg_pixels16_xy2_mmx2;

            put_pixels_tab[1][1] = put_pixels8_x2_mmx2;
            put_pixels_tab[1][2] = put_pixels8_y2_mmx2;
            put_no_rnd_pixels_tab[1][1] = put_no_rnd_pixels8_x2_mmx2;
            put_no_rnd_pixels_tab[1][2] = put_no_rnd_pixels8_y2_mmx2;

            avg_pixels_tab[1][0] = avg_pixels8_mmx2;
            avg_pixels_tab[1][1] = avg_pixels8_x2_mmx2;
            avg_pixels_tab[1][2] = avg_pixels8_y2_mmx2;
            avg_pixels_tab[1][3] = avg_pixels8_xy2_mmx2;
        } else if (mm_flags & MM_3DNOW) {
            put_pixels_tab[0][1] = put_pixels16_x2_3dnow;
            put_pixels_tab[0][2] = put_pixels16_y2_3dnow;
            put_no_rnd_pixels_tab[0][1] = put_no_rnd_pixels16_x2_3dnow;
            put_no_rnd_pixels_tab[0][2] = put_no_rnd_pixels16_y2_3dnow;

            avg_pixels_tab[0][0] = avg_pixels16_3dnow;
            avg_pixels_tab[0][1] = avg_pixels16_x2_3dnow;
            avg_pixels_tab[0][2] = avg_pixels16_y2_3dnow;
            avg_pixels_tab[0][3] = avg_pixels16_xy2_3dnow;
            
            put_pixels_tab[1][1] = put_pixels8_x2_3dnow;
            put_pixels_tab[1][2] = put_pixels8_y2_3dnow;
            put_no_rnd_pixels_tab[1][1] = put_no_rnd_pixels8_x2_3dnow;
            put_no_rnd_pixels_tab[1][2] = put_no_rnd_pixels8_y2_3dnow;

            avg_pixels_tab[1][0] = avg_pixels8_3dnow;
            avg_pixels_tab[1][1] = avg_pixels8_x2_3dnow;
            avg_pixels_tab[1][2] = avg_pixels8_y2_3dnow;
            avg_pixels_tab[1][3] = avg_pixels8_xy2_3dnow;
        }

        /* idct */
        if (mm_flags & MM_MMXEXT) {
            ff_idct = ff_mmxext_idct;
        } else {
            ff_idct = ff_mmx_idct;
        }
#ifdef SIMPLE_IDCT
//	ff_idct = simple_idct;
	ff_idct = simple_idct_mmx;
#endif
    }

#if 0
    // for speed testing
    get_pixels = just_return;
    put_pixels_clamped = just_return;
    add_pixels_clamped = just_return;

    pix_abs16x16 = just_return;
    pix_abs16x16_x2 = just_return;
    pix_abs16x16_y2 = just_return;
    pix_abs16x16_xy2 = just_return;

    put_pixels_tab[0] = just_return;
    put_pixels_tab[1] = just_return;
    put_pixels_tab[2] = just_return;
    put_pixels_tab[3] = just_return;

    put_no_rnd_pixels_tab[0] = just_return;
    put_no_rnd_pixels_tab[1] = just_return;
    put_no_rnd_pixels_tab[2] = just_return;
    put_no_rnd_pixels_tab[3] = just_return;

    avg_pixels_tab[0] = just_return;
    avg_pixels_tab[1] = just_return;
    avg_pixels_tab[2] = just_return;
    avg_pixels_tab[3] = just_return;

    avg_no_rnd_pixels_tab[0] = just_return;
    avg_no_rnd_pixels_tab[1] = just_return;
    avg_no_rnd_pixels_tab[2] = just_return;
    avg_no_rnd_pixels_tab[3] = just_return;

    //av_fdct = just_return;
    //ff_idct = just_return;
#endif
}

void gen_idct_put(UINT8 *dest, int line_size, DCTELEM *block);

/**
 * this will send coeff matrixes which would have different results for the 16383 type MMX vs C IDCTs to the C IDCT
 */ 
void bit_exact_idct_put(UINT8 *dest, int line_size, INT16 *block){
    if(   block[0]>1022 && block[1]==0 && block[4 ]==0 && block[5 ]==0
       && block[8]==0   && block[9]==0 && block[12]==0 && block[13]==0){
        int16_t tmp[64];
        int i;

        for(i=0; i<64; i++)
            tmp[i]= block[i];
        for(i=0; i<64; i++)
            block[i]= tmp[block_permute_op(i)];
        
        simple_idct_put(dest, line_size, block);
    }
    else
        gen_idct_put(dest, line_size, block);
}

/* remove any non bit exact operation (testing purpose). NOTE that
   this function should be kept as small as possible because it is
   always difficult to test automatically non bit exact cases. */
void dsputil_set_bit_exact_mmx(void)
{
    if (mm_flags & MM_MMX) {
    
        /* MMX2 & 3DNOW */
        put_no_rnd_pixels_tab[0][1] = put_no_rnd_pixels16_x2_mmx;
        put_no_rnd_pixels_tab[0][2] = put_no_rnd_pixels16_y2_mmx;
        avg_pixels_tab[0][3] = avg_pixels16_xy2_mmx;
        put_no_rnd_pixels_tab[1][1] = put_no_rnd_pixels8_x2_mmx;
        put_no_rnd_pixels_tab[1][2] = put_no_rnd_pixels8_y2_mmx;
        avg_pixels_tab[1][3] = avg_pixels8_xy2_mmx;

        if (mm_flags & MM_MMXEXT) {
            pix_abs16x16_x2  = pix_abs16x16_x2_mmx;
            pix_abs16x16_y2  = pix_abs16x16_y2_mmx;
            pix_abs16x16_xy2 = pix_abs16x16_xy2_mmx;
            pix_abs8x8_x2 = pix_abs8x8_x2_mmx;
            pix_abs8x8_y2 = pix_abs8x8_y2_mmx;
            pix_abs8x8_xy2= pix_abs8x8_xy2_mmx;
        }
#ifdef SIMPLE_IDCT
        if(ff_idct_put==gen_idct_put && ff_idct == simple_idct_mmx)
            ff_idct_put= bit_exact_idct_put;
#endif
    }
}
