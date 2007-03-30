/*
 * MMX optimized DSP utils
 * Copyright (c) 2000, 2001 Fabrice Bellard.
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * MMX optimization by Nick Kurshev <nickols_k@mail.ru>
 */

#include "../dsputil.h"
#include "../simple_idct.h"
#include "../mpegvideo.h"
#include "x86_cpu.h"
#include "mmx.h"

//#undef NDEBUG
//#include <assert.h>

extern void ff_idct_xvid_mmx(short *block);
extern void ff_idct_xvid_mmx2(short *block);

int mm_flags; /* multimedia extension flags */

/* pixel operations */
static const uint64_t mm_bone attribute_used __attribute__ ((aligned(8))) = 0x0101010101010101ULL;
static const uint64_t mm_wone attribute_used __attribute__ ((aligned(8))) = 0x0001000100010001ULL;
static const uint64_t mm_wtwo attribute_used __attribute__ ((aligned(8))) = 0x0002000200020002ULL;

static const uint64_t ff_pdw_80000000[2] attribute_used __attribute__ ((aligned(16))) =
{0x8000000080000000ULL, 0x8000000080000000ULL};

static const uint64_t ff_pw_20 attribute_used __attribute__ ((aligned(8))) = 0x0014001400140014ULL;
static const uint64_t ff_pw_3  attribute_used __attribute__ ((aligned(8))) = 0x0003000300030003ULL;
static const uint64_t ff_pw_4  attribute_used __attribute__ ((aligned(8))) = 0x0004000400040004ULL;
static const uint64_t ff_pw_5  attribute_used __attribute__ ((aligned(8))) = 0x0005000500050005ULL;
static const uint64_t ff_pw_8  attribute_used __attribute__ ((aligned(8))) = 0x0008000800080008ULL;
static const uint64_t ff_pw_16 attribute_used __attribute__ ((aligned(8))) = 0x0010001000100010ULL;
static const uint64_t ff_pw_32 attribute_used __attribute__ ((aligned(8))) = 0x0020002000200020ULL;
static const uint64_t ff_pw_64 attribute_used __attribute__ ((aligned(8))) = 0x0040004000400040ULL;
static const uint64_t ff_pw_15 attribute_used __attribute__ ((aligned(8))) = 0x000F000F000F000FULL;

static const uint64_t ff_pb_1  attribute_used __attribute__ ((aligned(8))) = 0x0101010101010101ULL;
static const uint64_t ff_pb_3  attribute_used __attribute__ ((aligned(8))) = 0x0303030303030303ULL;
static const uint64_t ff_pb_7  attribute_used __attribute__ ((aligned(8))) = 0x0707070707070707ULL;
static const uint64_t ff_pb_3F attribute_used __attribute__ ((aligned(8))) = 0x3F3F3F3F3F3F3F3FULL;
static const uint64_t ff_pb_A1 attribute_used __attribute__ ((aligned(8))) = 0xA1A1A1A1A1A1A1A1ULL;
static const uint64_t ff_pb_5F attribute_used __attribute__ ((aligned(8))) = 0x5F5F5F5F5F5F5F5FULL;
static const uint64_t ff_pb_FC attribute_used __attribute__ ((aligned(8))) = 0xFCFCFCFCFCFCFCFCULL;

#define JUMPALIGN() __asm __volatile (ASMALIGN(3)::)
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
    "movq " #rega ", " #regr "  \n\t"\
    "pand " #regb ", " #regr "  \n\t"\
    "pxor " #rega ", " #regb "  \n\t"\
    "pand " #regfe "," #regb "  \n\t"\
    "psrlq $1, " #regb "        \n\t"\
    "paddb " #regb ", " #regr " \n\t"

#define PAVGB_MMX(rega, regb, regr, regfe) \
    "movq " #rega ", " #regr "  \n\t"\
    "por  " #regb ", " #regr "  \n\t"\
    "pxor " #rega ", " #regb "  \n\t"\
    "pand " #regfe "," #regb "  \n\t"\
    "psrlq $1, " #regb "        \n\t"\
    "psubb " #regb ", " #regr " \n\t"

// mm6 is supposed to contain 0xfefefefefefefefe
#define PAVGBP_MMX_NO_RND(rega, regb, regr,  regc, regd, regp) \
    "movq " #rega ", " #regr "  \n\t"\
    "movq " #regc ", " #regp "  \n\t"\
    "pand " #regb ", " #regr "  \n\t"\
    "pand " #regd ", " #regp "  \n\t"\
    "pxor " #rega ", " #regb "  \n\t"\
    "pxor " #regc ", " #regd "  \n\t"\
    "pand %%mm6, " #regb "      \n\t"\
    "pand %%mm6, " #regd "      \n\t"\
    "psrlq $1, " #regb "        \n\t"\
    "psrlq $1, " #regd "        \n\t"\
    "paddb " #regb ", " #regr " \n\t"\
    "paddb " #regd ", " #regp " \n\t"

#define PAVGBP_MMX(rega, regb, regr, regc, regd, regp) \
    "movq " #rega ", " #regr "  \n\t"\
    "movq " #regc ", " #regp "  \n\t"\
    "por  " #regb ", " #regr "  \n\t"\
    "por  " #regd ", " #regp "  \n\t"\
    "pxor " #rega ", " #regb "  \n\t"\
    "pxor " #regc ", " #regd "  \n\t"\
    "pand %%mm6, " #regb "      \n\t"\
    "pand %%mm6, " #regd "      \n\t"\
    "psrlq $1, " #regd "        \n\t"\
    "psrlq $1, " #regb "        \n\t"\
    "psubb " #regb ", " #regr " \n\t"\
    "psubb " #regd ", " #regp " \n\t"

/***********************************/
/* MMX no rounding */
#define DEF(x, y) x ## _no_rnd_ ## y ##_mmx
#define SET_RND  MOVQ_WONE
#define PAVGBP(a, b, c, d, e, f)        PAVGBP_MMX_NO_RND(a, b, c, d, e, f)
#define PAVGB(a, b, c, e)               PAVGB_MMX_NO_RND(a, b, c, e)

#include "dsputil_mmx_rnd.h"

#undef DEF
#undef SET_RND
#undef PAVGBP
#undef PAVGB
/***********************************/
/* MMX rounding */

#define DEF(x, y) x ## _ ## y ##_mmx
#define SET_RND  MOVQ_WTWO
#define PAVGBP(a, b, c, d, e, f)        PAVGBP_MMX(a, b, c, d, e, f)
#define PAVGB(a, b, c, e)               PAVGB_MMX(a, b, c, e)

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

#define SBUTTERFLY(a,b,t,n)\
    "movq " #a ", " #t "              \n\t" /* abcd */\
    "punpckl" #n " " #b ", " #a "     \n\t" /* aebf */\
    "punpckh" #n " " #b ", " #t "     \n\t" /* cgdh */\

/***********************************/
/* standard MMX */

#ifdef CONFIG_ENCODERS
static void get_pixels_mmx(DCTELEM *block, const uint8_t *pixels, int line_size)
{
    asm volatile(
        "mov $-128, %%"REG_a"           \n\t"
        "pxor %%mm7, %%mm7              \n\t"
        ASMALIGN(4)
        "1:                             \n\t"
        "movq (%0), %%mm0               \n\t"
        "movq (%0, %2), %%mm2           \n\t"
        "movq %%mm0, %%mm1              \n\t"
        "movq %%mm2, %%mm3              \n\t"
        "punpcklbw %%mm7, %%mm0         \n\t"
        "punpckhbw %%mm7, %%mm1         \n\t"
        "punpcklbw %%mm7, %%mm2         \n\t"
        "punpckhbw %%mm7, %%mm3         \n\t"
        "movq %%mm0, (%1, %%"REG_a")    \n\t"
        "movq %%mm1, 8(%1, %%"REG_a")   \n\t"
        "movq %%mm2, 16(%1, %%"REG_a")  \n\t"
        "movq %%mm3, 24(%1, %%"REG_a")  \n\t"
        "add %3, %0                     \n\t"
        "add $32, %%"REG_a"             \n\t"
        "js 1b                          \n\t"
        : "+r" (pixels)
        : "r" (block+64), "r" ((long)line_size), "r" ((long)line_size*2)
        : "%"REG_a
    );
}

static inline void diff_pixels_mmx(DCTELEM *block, const uint8_t *s1, const uint8_t *s2, int stride)
{
    asm volatile(
        "pxor %%mm7, %%mm7              \n\t"
        "mov $-128, %%"REG_a"           \n\t"
        ASMALIGN(4)
        "1:                             \n\t"
        "movq (%0), %%mm0               \n\t"
        "movq (%1), %%mm2               \n\t"
        "movq %%mm0, %%mm1              \n\t"
        "movq %%mm2, %%mm3              \n\t"
        "punpcklbw %%mm7, %%mm0         \n\t"
        "punpckhbw %%mm7, %%mm1         \n\t"
        "punpcklbw %%mm7, %%mm2         \n\t"
        "punpckhbw %%mm7, %%mm3         \n\t"
        "psubw %%mm2, %%mm0             \n\t"
        "psubw %%mm3, %%mm1             \n\t"
        "movq %%mm0, (%2, %%"REG_a")    \n\t"
        "movq %%mm1, 8(%2, %%"REG_a")   \n\t"
        "add %3, %0                     \n\t"
        "add %3, %1                     \n\t"
        "add $16, %%"REG_a"             \n\t"
        "jnz 1b                         \n\t"
        : "+r" (s1), "+r" (s2)
        : "r" (block+64), "r" ((long)stride)
        : "%"REG_a
    );
}
#endif //CONFIG_ENCODERS

void put_pixels_clamped_mmx(const DCTELEM *block, uint8_t *pixels, int line_size)
{
    const DCTELEM *p;
    uint8_t *pix;

    /* read the pixels */
    p = block;
    pix = pixels;
    /* unrolled loop */
        __asm __volatile(
                "movq   %3, %%mm0               \n\t"
                "movq   8%3, %%mm1              \n\t"
                "movq   16%3, %%mm2             \n\t"
                "movq   24%3, %%mm3             \n\t"
                "movq   32%3, %%mm4             \n\t"
                "movq   40%3, %%mm5             \n\t"
                "movq   48%3, %%mm6             \n\t"
                "movq   56%3, %%mm7             \n\t"
                "packuswb %%mm1, %%mm0          \n\t"
                "packuswb %%mm3, %%mm2          \n\t"
                "packuswb %%mm5, %%mm4          \n\t"
                "packuswb %%mm7, %%mm6          \n\t"
                "movq   %%mm0, (%0)             \n\t"
                "movq   %%mm2, (%0, %1)         \n\t"
                "movq   %%mm4, (%0, %1, 2)      \n\t"
                "movq   %%mm6, (%0, %2)         \n\t"
                ::"r" (pix), "r" ((long)line_size), "r" ((long)line_size*3), "m"(*p)
                :"memory");
        pix += line_size*4;
        p += 32;

    // if here would be an exact copy of the code above
    // compiler would generate some very strange code
    // thus using "r"
    __asm __volatile(
            "movq       (%3), %%mm0             \n\t"
            "movq       8(%3), %%mm1            \n\t"
            "movq       16(%3), %%mm2           \n\t"
            "movq       24(%3), %%mm3           \n\t"
            "movq       32(%3), %%mm4           \n\t"
            "movq       40(%3), %%mm5           \n\t"
            "movq       48(%3), %%mm6           \n\t"
            "movq       56(%3), %%mm7           \n\t"
            "packuswb %%mm1, %%mm0              \n\t"
            "packuswb %%mm3, %%mm2              \n\t"
            "packuswb %%mm5, %%mm4              \n\t"
            "packuswb %%mm7, %%mm6              \n\t"
            "movq       %%mm0, (%0)             \n\t"
            "movq       %%mm2, (%0, %1)         \n\t"
            "movq       %%mm4, (%0, %1, 2)      \n\t"
            "movq       %%mm6, (%0, %2)         \n\t"
            ::"r" (pix), "r" ((long)line_size), "r" ((long)line_size*3), "r"(p)
            :"memory");
}

static DECLARE_ALIGNED_8(const unsigned char, vector128[8]) =
  { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 };

void put_signed_pixels_clamped_mmx(const DCTELEM *block, uint8_t *pixels, int line_size)
{
    int i;

    movq_m2r(*vector128, mm1);
    for (i = 0; i < 8; i++) {
        movq_m2r(*(block), mm0);
        packsswb_m2r(*(block + 4), mm0);
        block += 8;
        paddb_r2r(mm1, mm0);
        movq_r2m(mm0, *pixels);
        pixels += line_size;
    }
}

void add_pixels_clamped_mmx(const DCTELEM *block, uint8_t *pixels, int line_size)
{
    const DCTELEM *p;
    uint8_t *pix;
    int i;

    /* read the pixels */
    p = block;
    pix = pixels;
    MOVQ_ZERO(mm7);
    i = 4;
    do {
        __asm __volatile(
                "movq   (%2), %%mm0     \n\t"
                "movq   8(%2), %%mm1    \n\t"
                "movq   16(%2), %%mm2   \n\t"
                "movq   24(%2), %%mm3   \n\t"
                "movq   %0, %%mm4       \n\t"
                "movq   %1, %%mm6       \n\t"
                "movq   %%mm4, %%mm5    \n\t"
                "punpcklbw %%mm7, %%mm4 \n\t"
                "punpckhbw %%mm7, %%mm5 \n\t"
                "paddsw %%mm4, %%mm0    \n\t"
                "paddsw %%mm5, %%mm1    \n\t"
                "movq   %%mm6, %%mm5    \n\t"
                "punpcklbw %%mm7, %%mm6 \n\t"
                "punpckhbw %%mm7, %%mm5 \n\t"
                "paddsw %%mm6, %%mm2    \n\t"
                "paddsw %%mm5, %%mm3    \n\t"
                "packuswb %%mm1, %%mm0  \n\t"
                "packuswb %%mm3, %%mm2  \n\t"
                "movq   %%mm0, %0       \n\t"
                "movq   %%mm2, %1       \n\t"
                :"+m"(*pix), "+m"(*(pix+line_size))
                :"r"(p)
                :"memory");
        pix += line_size*2;
        p += 16;
    } while (--i);
}

static void put_pixels4_mmx(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    __asm __volatile(
         "lea (%3, %3), %%"REG_a"       \n\t"
         ASMALIGN(3)
         "1:                            \n\t"
         "movd (%1), %%mm0              \n\t"
         "movd (%1, %3), %%mm1          \n\t"
         "movd %%mm0, (%2)              \n\t"
         "movd %%mm1, (%2, %3)          \n\t"
         "add %%"REG_a", %1             \n\t"
         "add %%"REG_a", %2             \n\t"
         "movd (%1), %%mm0              \n\t"
         "movd (%1, %3), %%mm1          \n\t"
         "movd %%mm0, (%2)              \n\t"
         "movd %%mm1, (%2, %3)          \n\t"
         "add %%"REG_a", %1             \n\t"
         "add %%"REG_a", %2             \n\t"
         "subl $4, %0                   \n\t"
         "jnz 1b                        \n\t"
         : "+g"(h), "+r" (pixels),  "+r" (block)
         : "r"((long)line_size)
         : "%"REG_a, "memory"
        );
}

static void put_pixels8_mmx(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    __asm __volatile(
         "lea (%3, %3), %%"REG_a"       \n\t"
         ASMALIGN(3)
         "1:                            \n\t"
         "movq (%1), %%mm0              \n\t"
         "movq (%1, %3), %%mm1          \n\t"
         "movq %%mm0, (%2)              \n\t"
         "movq %%mm1, (%2, %3)          \n\t"
         "add %%"REG_a", %1             \n\t"
         "add %%"REG_a", %2             \n\t"
         "movq (%1), %%mm0              \n\t"
         "movq (%1, %3), %%mm1          \n\t"
         "movq %%mm0, (%2)              \n\t"
         "movq %%mm1, (%2, %3)          \n\t"
         "add %%"REG_a", %1             \n\t"
         "add %%"REG_a", %2             \n\t"
         "subl $4, %0                   \n\t"
         "jnz 1b                        \n\t"
         : "+g"(h), "+r" (pixels),  "+r" (block)
         : "r"((long)line_size)
         : "%"REG_a, "memory"
        );
}

static void put_pixels16_mmx(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    __asm __volatile(
         "lea (%3, %3), %%"REG_a"       \n\t"
         ASMALIGN(3)
         "1:                            \n\t"
         "movq (%1), %%mm0              \n\t"
         "movq 8(%1), %%mm4             \n\t"
         "movq (%1, %3), %%mm1          \n\t"
         "movq 8(%1, %3), %%mm5         \n\t"
         "movq %%mm0, (%2)              \n\t"
         "movq %%mm4, 8(%2)             \n\t"
         "movq %%mm1, (%2, %3)          \n\t"
         "movq %%mm5, 8(%2, %3)         \n\t"
         "add %%"REG_a", %1             \n\t"
         "add %%"REG_a", %2             \n\t"
         "movq (%1), %%mm0              \n\t"
         "movq 8(%1), %%mm4             \n\t"
         "movq (%1, %3), %%mm1          \n\t"
         "movq 8(%1, %3), %%mm5         \n\t"
         "movq %%mm0, (%2)              \n\t"
         "movq %%mm4, 8(%2)             \n\t"
         "movq %%mm1, (%2, %3)          \n\t"
         "movq %%mm5, 8(%2, %3)         \n\t"
         "add %%"REG_a", %1             \n\t"
         "add %%"REG_a", %2             \n\t"
         "subl $4, %0                   \n\t"
         "jnz 1b                        \n\t"
         : "+g"(h), "+r" (pixels),  "+r" (block)
         : "r"((long)line_size)
         : "%"REG_a, "memory"
        );
}

static void clear_blocks_mmx(DCTELEM *blocks)
{
    __asm __volatile(
                "pxor %%mm7, %%mm7              \n\t"
                "mov $-128*6, %%"REG_a"         \n\t"
                "1:                             \n\t"
                "movq %%mm7, (%0, %%"REG_a")    \n\t"
                "movq %%mm7, 8(%0, %%"REG_a")   \n\t"
                "movq %%mm7, 16(%0, %%"REG_a")  \n\t"
                "movq %%mm7, 24(%0, %%"REG_a")  \n\t"
                "add $32, %%"REG_a"             \n\t"
                " js 1b                         \n\t"
                : : "r" (((uint8_t *)blocks)+128*6)
                : "%"REG_a
        );
}

#ifdef CONFIG_ENCODERS
static int pix_sum16_mmx(uint8_t * pix, int line_size){
    const int h=16;
    int sum;
    long index= -line_size*h;

    __asm __volatile(
                "pxor %%mm7, %%mm7              \n\t"
                "pxor %%mm6, %%mm6              \n\t"
                "1:                             \n\t"
                "movq (%2, %1), %%mm0           \n\t"
                "movq (%2, %1), %%mm1           \n\t"
                "movq 8(%2, %1), %%mm2          \n\t"
                "movq 8(%2, %1), %%mm3          \n\t"
                "punpcklbw %%mm7, %%mm0         \n\t"
                "punpckhbw %%mm7, %%mm1         \n\t"
                "punpcklbw %%mm7, %%mm2         \n\t"
                "punpckhbw %%mm7, %%mm3         \n\t"
                "paddw %%mm0, %%mm1             \n\t"
                "paddw %%mm2, %%mm3             \n\t"
                "paddw %%mm1, %%mm3             \n\t"
                "paddw %%mm3, %%mm6             \n\t"
                "add %3, %1                     \n\t"
                " js 1b                         \n\t"
                "movq %%mm6, %%mm5              \n\t"
                "psrlq $32, %%mm6               \n\t"
                "paddw %%mm5, %%mm6             \n\t"
                "movq %%mm6, %%mm5              \n\t"
                "psrlq $16, %%mm6               \n\t"
                "paddw %%mm5, %%mm6             \n\t"
                "movd %%mm6, %0                 \n\t"
                "andl $0xFFFF, %0               \n\t"
                : "=&r" (sum), "+r" (index)
                : "r" (pix - index), "r" ((long)line_size)
        );

        return sum;
}
#endif //CONFIG_ENCODERS

static void add_bytes_mmx(uint8_t *dst, uint8_t *src, int w){
    long i=0;
    asm volatile(
        "1:                             \n\t"
        "movq  (%1, %0), %%mm0          \n\t"
        "movq  (%2, %0), %%mm1          \n\t"
        "paddb %%mm0, %%mm1             \n\t"
        "movq %%mm1, (%2, %0)           \n\t"
        "movq 8(%1, %0), %%mm0          \n\t"
        "movq 8(%2, %0), %%mm1          \n\t"
        "paddb %%mm0, %%mm1             \n\t"
        "movq %%mm1, 8(%2, %0)          \n\t"
        "add $16, %0                    \n\t"
        "cmp %3, %0                     \n\t"
        " jb 1b                         \n\t"
        : "+r" (i)
        : "r"(src), "r"(dst), "r"((long)w-15)
    );
    for(; i<w; i++)
        dst[i+0] += src[i+0];
}

#define H263_LOOP_FILTER \
        "pxor %%mm7, %%mm7              \n\t"\
        "movq  %0, %%mm0                \n\t"\
        "movq  %0, %%mm1                \n\t"\
        "movq  %3, %%mm2                \n\t"\
        "movq  %3, %%mm3                \n\t"\
        "punpcklbw %%mm7, %%mm0         \n\t"\
        "punpckhbw %%mm7, %%mm1         \n\t"\
        "punpcklbw %%mm7, %%mm2         \n\t"\
        "punpckhbw %%mm7, %%mm3         \n\t"\
        "psubw %%mm2, %%mm0             \n\t"\
        "psubw %%mm3, %%mm1             \n\t"\
        "movq  %1, %%mm2                \n\t"\
        "movq  %1, %%mm3                \n\t"\
        "movq  %2, %%mm4                \n\t"\
        "movq  %2, %%mm5                \n\t"\
        "punpcklbw %%mm7, %%mm2         \n\t"\
        "punpckhbw %%mm7, %%mm3         \n\t"\
        "punpcklbw %%mm7, %%mm4         \n\t"\
        "punpckhbw %%mm7, %%mm5         \n\t"\
        "psubw %%mm2, %%mm4             \n\t"\
        "psubw %%mm3, %%mm5             \n\t"\
        "psllw $2, %%mm4                \n\t"\
        "psllw $2, %%mm5                \n\t"\
        "paddw %%mm0, %%mm4             \n\t"\
        "paddw %%mm1, %%mm5             \n\t"\
        "pxor %%mm6, %%mm6              \n\t"\
        "pcmpgtw %%mm4, %%mm6           \n\t"\
        "pcmpgtw %%mm5, %%mm7           \n\t"\
        "pxor %%mm6, %%mm4              \n\t"\
        "pxor %%mm7, %%mm5              \n\t"\
        "psubw %%mm6, %%mm4             \n\t"\
        "psubw %%mm7, %%mm5             \n\t"\
        "psrlw $3, %%mm4                \n\t"\
        "psrlw $3, %%mm5                \n\t"\
        "packuswb %%mm5, %%mm4          \n\t"\
        "packsswb %%mm7, %%mm6          \n\t"\
        "pxor %%mm7, %%mm7              \n\t"\
        "movd %4, %%mm2                 \n\t"\
        "punpcklbw %%mm2, %%mm2         \n\t"\
        "punpcklbw %%mm2, %%mm2         \n\t"\
        "punpcklbw %%mm2, %%mm2         \n\t"\
        "psubusb %%mm4, %%mm2           \n\t"\
        "movq %%mm2, %%mm3              \n\t"\
        "psubusb %%mm4, %%mm3           \n\t"\
        "psubb %%mm3, %%mm2             \n\t"\
        "movq %1, %%mm3                 \n\t"\
        "movq %2, %%mm4                 \n\t"\
        "pxor %%mm6, %%mm3              \n\t"\
        "pxor %%mm6, %%mm4              \n\t"\
        "paddusb %%mm2, %%mm3           \n\t"\
        "psubusb %%mm2, %%mm4           \n\t"\
        "pxor %%mm6, %%mm3              \n\t"\
        "pxor %%mm6, %%mm4              \n\t"\
        "paddusb %%mm2, %%mm2           \n\t"\
        "packsswb %%mm1, %%mm0          \n\t"\
        "pcmpgtb %%mm0, %%mm7           \n\t"\
        "pxor %%mm7, %%mm0              \n\t"\
        "psubb %%mm7, %%mm0             \n\t"\
        "movq %%mm0, %%mm1              \n\t"\
        "psubusb %%mm2, %%mm0           \n\t"\
        "psubb %%mm0, %%mm1             \n\t"\
        "pand %5, %%mm1                 \n\t"\
        "psrlw $2, %%mm1                \n\t"\
        "pxor %%mm7, %%mm1              \n\t"\
        "psubb %%mm7, %%mm1             \n\t"\
        "movq %0, %%mm5                 \n\t"\
        "movq %3, %%mm6                 \n\t"\
        "psubb %%mm1, %%mm5             \n\t"\
        "paddb %%mm1, %%mm6             \n\t"

static void h263_v_loop_filter_mmx(uint8_t *src, int stride, int qscale){
    const int strength= ff_h263_loop_filter_strength[qscale];

    asm volatile(

        H263_LOOP_FILTER

        "movq %%mm3, %1                 \n\t"
        "movq %%mm4, %2                 \n\t"
        "movq %%mm5, %0                 \n\t"
        "movq %%mm6, %3                 \n\t"
        : "+m" (*(uint64_t*)(src - 2*stride)),
          "+m" (*(uint64_t*)(src - 1*stride)),
          "+m" (*(uint64_t*)(src + 0*stride)),
          "+m" (*(uint64_t*)(src + 1*stride))
        : "g" (2*strength), "m"(ff_pb_FC)
    );
}

static inline void transpose4x4(uint8_t *dst, uint8_t *src, int dst_stride, int src_stride){
    asm volatile( //FIXME could save 1 instruction if done as 8x4 ...
        "movd  %4, %%mm0                \n\t"
        "movd  %5, %%mm1                \n\t"
        "movd  %6, %%mm2                \n\t"
        "movd  %7, %%mm3                \n\t"
        "punpcklbw %%mm1, %%mm0         \n\t"
        "punpcklbw %%mm3, %%mm2         \n\t"
        "movq %%mm0, %%mm1              \n\t"
        "punpcklwd %%mm2, %%mm0         \n\t"
        "punpckhwd %%mm2, %%mm1         \n\t"
        "movd  %%mm0, %0                \n\t"
        "punpckhdq %%mm0, %%mm0         \n\t"
        "movd  %%mm0, %1                \n\t"
        "movd  %%mm1, %2                \n\t"
        "punpckhdq %%mm1, %%mm1         \n\t"
        "movd  %%mm1, %3                \n\t"

        : "=m" (*(uint32_t*)(dst + 0*dst_stride)),
          "=m" (*(uint32_t*)(dst + 1*dst_stride)),
          "=m" (*(uint32_t*)(dst + 2*dst_stride)),
          "=m" (*(uint32_t*)(dst + 3*dst_stride))
        :  "m" (*(uint32_t*)(src + 0*src_stride)),
           "m" (*(uint32_t*)(src + 1*src_stride)),
           "m" (*(uint32_t*)(src + 2*src_stride)),
           "m" (*(uint32_t*)(src + 3*src_stride))
    );
}

static void h263_h_loop_filter_mmx(uint8_t *src, int stride, int qscale){
    const int strength= ff_h263_loop_filter_strength[qscale];
    uint64_t temp[4] __attribute__ ((aligned(8)));
    uint8_t *btemp= (uint8_t*)temp;

    src -= 2;

    transpose4x4(btemp  , src           , 8, stride);
    transpose4x4(btemp+4, src + 4*stride, 8, stride);
    asm volatile(
        H263_LOOP_FILTER // 5 3 4 6

        : "+m" (temp[0]),
          "+m" (temp[1]),
          "+m" (temp[2]),
          "+m" (temp[3])
        : "g" (2*strength), "m"(ff_pb_FC)
    );

    asm volatile(
        "movq %%mm5, %%mm1              \n\t"
        "movq %%mm4, %%mm0              \n\t"
        "punpcklbw %%mm3, %%mm5         \n\t"
        "punpcklbw %%mm6, %%mm4         \n\t"
        "punpckhbw %%mm3, %%mm1         \n\t"
        "punpckhbw %%mm6, %%mm0         \n\t"
        "movq %%mm5, %%mm3              \n\t"
        "movq %%mm1, %%mm6              \n\t"
        "punpcklwd %%mm4, %%mm5         \n\t"
        "punpcklwd %%mm0, %%mm1         \n\t"
        "punpckhwd %%mm4, %%mm3         \n\t"
        "punpckhwd %%mm0, %%mm6         \n\t"
        "movd %%mm5, (%0)               \n\t"
        "punpckhdq %%mm5, %%mm5         \n\t"
        "movd %%mm5, (%0,%2)            \n\t"
        "movd %%mm3, (%0,%2,2)          \n\t"
        "punpckhdq %%mm3, %%mm3         \n\t"
        "movd %%mm3, (%0,%3)            \n\t"
        "movd %%mm1, (%1)               \n\t"
        "punpckhdq %%mm1, %%mm1         \n\t"
        "movd %%mm1, (%1,%2)            \n\t"
        "movd %%mm6, (%1,%2,2)          \n\t"
        "punpckhdq %%mm6, %%mm6         \n\t"
        "movd %%mm6, (%1,%3)            \n\t"
        :: "r" (src),
           "r" (src + 4*stride),
           "r" ((long)   stride ),
           "r" ((long)(3*stride))
    );
}

#ifdef CONFIG_ENCODERS
static int pix_norm1_mmx(uint8_t *pix, int line_size) {
    int tmp;
  asm volatile (
      "movl $16,%%ecx\n"
      "pxor %%mm0,%%mm0\n"
      "pxor %%mm7,%%mm7\n"
      "1:\n"
      "movq (%0),%%mm2\n"       /* mm2 = pix[0-7] */
      "movq 8(%0),%%mm3\n"      /* mm3 = pix[8-15] */

      "movq %%mm2,%%mm1\n"      /* mm1 = mm2 = pix[0-7] */

      "punpckhbw %%mm0,%%mm1\n" /* mm1 = [pix4-7] */
      "punpcklbw %%mm0,%%mm2\n" /* mm2 = [pix0-3] */

      "movq %%mm3,%%mm4\n"      /* mm4 = mm3 = pix[8-15] */
      "punpckhbw %%mm0,%%mm3\n" /* mm3 = [pix12-15] */
      "punpcklbw %%mm0,%%mm4\n" /* mm4 = [pix8-11] */

      "pmaddwd %%mm1,%%mm1\n"   /* mm1 = (pix0^2+pix1^2,pix2^2+pix3^2) */
      "pmaddwd %%mm2,%%mm2\n"   /* mm2 = (pix4^2+pix5^2,pix6^2+pix7^2) */

      "pmaddwd %%mm3,%%mm3\n"
      "pmaddwd %%mm4,%%mm4\n"

      "paddd %%mm1,%%mm2\n"     /* mm2 = (pix0^2+pix1^2+pix4^2+pix5^2,
                                          pix2^2+pix3^2+pix6^2+pix7^2) */
      "paddd %%mm3,%%mm4\n"
      "paddd %%mm2,%%mm7\n"

      "add %2, %0\n"
      "paddd %%mm4,%%mm7\n"
      "dec %%ecx\n"
      "jnz 1b\n"

      "movq %%mm7,%%mm1\n"
      "psrlq $32, %%mm7\n"      /* shift hi dword to lo */
      "paddd %%mm7,%%mm1\n"
      "movd %%mm1,%1\n"
      : "+r" (pix), "=r"(tmp) : "r" ((long)line_size) : "%ecx" );
    return tmp;
}

static int sse8_mmx(void *v, uint8_t * pix1, uint8_t * pix2, int line_size, int h) {
    int tmp;
  asm volatile (
      "movl %4,%%ecx\n"
      "shr $1,%%ecx\n"
      "pxor %%mm0,%%mm0\n"      /* mm0 = 0 */
      "pxor %%mm7,%%mm7\n"      /* mm7 holds the sum */
      "1:\n"
      "movq (%0),%%mm1\n"       /* mm1 = pix1[0][0-7] */
      "movq (%1),%%mm2\n"       /* mm2 = pix2[0][0-7] */
      "movq (%0,%3),%%mm3\n"    /* mm3 = pix1[1][0-7] */
      "movq (%1,%3),%%mm4\n"    /* mm4 = pix2[1][0-7] */

      /* todo: mm1-mm2, mm3-mm4 */
      /* algo: substract mm1 from mm2 with saturation and vice versa */
      /*       OR the results to get absolute difference */
      "movq %%mm1,%%mm5\n"
      "movq %%mm3,%%mm6\n"
      "psubusb %%mm2,%%mm1\n"
      "psubusb %%mm4,%%mm3\n"
      "psubusb %%mm5,%%mm2\n"
      "psubusb %%mm6,%%mm4\n"

      "por %%mm1,%%mm2\n"
      "por %%mm3,%%mm4\n"

      /* now convert to 16-bit vectors so we can square them */
      "movq %%mm2,%%mm1\n"
      "movq %%mm4,%%mm3\n"

      "punpckhbw %%mm0,%%mm2\n"
      "punpckhbw %%mm0,%%mm4\n"
      "punpcklbw %%mm0,%%mm1\n" /* mm1 now spread over (mm1,mm2) */
      "punpcklbw %%mm0,%%mm3\n" /* mm4 now spread over (mm3,mm4) */

      "pmaddwd %%mm2,%%mm2\n"
      "pmaddwd %%mm4,%%mm4\n"
      "pmaddwd %%mm1,%%mm1\n"
      "pmaddwd %%mm3,%%mm3\n"

      "lea (%0,%3,2), %0\n"     /* pix1 += 2*line_size */
      "lea (%1,%3,2), %1\n"     /* pix2 += 2*line_size */

      "paddd %%mm2,%%mm1\n"
      "paddd %%mm4,%%mm3\n"
      "paddd %%mm1,%%mm7\n"
      "paddd %%mm3,%%mm7\n"

      "decl %%ecx\n"
      "jnz 1b\n"

      "movq %%mm7,%%mm1\n"
      "psrlq $32, %%mm7\n"      /* shift hi dword to lo */
      "paddd %%mm7,%%mm1\n"
      "movd %%mm1,%2\n"
      : "+r" (pix1), "+r" (pix2), "=r"(tmp)
      : "r" ((long)line_size) , "m" (h)
      : "%ecx");
    return tmp;
}

static int sse16_mmx(void *v, uint8_t * pix1, uint8_t * pix2, int line_size, int h) {
    int tmp;
  asm volatile (
      "movl %4,%%ecx\n"
      "pxor %%mm0,%%mm0\n"      /* mm0 = 0 */
      "pxor %%mm7,%%mm7\n"      /* mm7 holds the sum */
      "1:\n"
      "movq (%0),%%mm1\n"       /* mm1 = pix1[0-7] */
      "movq (%1),%%mm2\n"       /* mm2 = pix2[0-7] */
      "movq 8(%0),%%mm3\n"      /* mm3 = pix1[8-15] */
      "movq 8(%1),%%mm4\n"      /* mm4 = pix2[8-15] */

      /* todo: mm1-mm2, mm3-mm4 */
      /* algo: substract mm1 from mm2 with saturation and vice versa */
      /*       OR the results to get absolute difference */
      "movq %%mm1,%%mm5\n"
      "movq %%mm3,%%mm6\n"
      "psubusb %%mm2,%%mm1\n"
      "psubusb %%mm4,%%mm3\n"
      "psubusb %%mm5,%%mm2\n"
      "psubusb %%mm6,%%mm4\n"

      "por %%mm1,%%mm2\n"
      "por %%mm3,%%mm4\n"

      /* now convert to 16-bit vectors so we can square them */
      "movq %%mm2,%%mm1\n"
      "movq %%mm4,%%mm3\n"

      "punpckhbw %%mm0,%%mm2\n"
      "punpckhbw %%mm0,%%mm4\n"
      "punpcklbw %%mm0,%%mm1\n" /* mm1 now spread over (mm1,mm2) */
      "punpcklbw %%mm0,%%mm3\n" /* mm4 now spread over (mm3,mm4) */

      "pmaddwd %%mm2,%%mm2\n"
      "pmaddwd %%mm4,%%mm4\n"
      "pmaddwd %%mm1,%%mm1\n"
      "pmaddwd %%mm3,%%mm3\n"

      "add %3,%0\n"
      "add %3,%1\n"

      "paddd %%mm2,%%mm1\n"
      "paddd %%mm4,%%mm3\n"
      "paddd %%mm1,%%mm7\n"
      "paddd %%mm3,%%mm7\n"

      "decl %%ecx\n"
      "jnz 1b\n"

      "movq %%mm7,%%mm1\n"
      "psrlq $32, %%mm7\n"      /* shift hi dword to lo */
      "paddd %%mm7,%%mm1\n"
      "movd %%mm1,%2\n"
      : "+r" (pix1), "+r" (pix2), "=r"(tmp)
      : "r" ((long)line_size) , "m" (h)
      : "%ecx");
    return tmp;
}

static int sse16_sse2(void *v, uint8_t * pix1, uint8_t * pix2, int line_size, int h) {
    int tmp;
  asm volatile (
      "shr $1,%2\n"
      "pxor %%xmm0,%%xmm0\n"    /* mm0 = 0 */
      "pxor %%xmm7,%%xmm7\n"    /* mm7 holds the sum */
      "1:\n"
      "movdqu (%0),%%xmm1\n"    /* mm1 = pix1[0][0-15] */
      "movdqu (%1),%%xmm2\n"    /* mm2 = pix2[0][0-15] */
      "movdqu (%0,%4),%%xmm3\n" /* mm3 = pix1[1][0-15] */
      "movdqu (%1,%4),%%xmm4\n" /* mm4 = pix2[1][0-15] */

      /* todo: mm1-mm2, mm3-mm4 */
      /* algo: substract mm1 from mm2 with saturation and vice versa */
      /*       OR the results to get absolute difference */
      "movdqa %%xmm1,%%xmm5\n"
      "movdqa %%xmm3,%%xmm6\n"
      "psubusb %%xmm2,%%xmm1\n"
      "psubusb %%xmm4,%%xmm3\n"
      "psubusb %%xmm5,%%xmm2\n"
      "psubusb %%xmm6,%%xmm4\n"

      "por %%xmm1,%%xmm2\n"
      "por %%xmm3,%%xmm4\n"

      /* now convert to 16-bit vectors so we can square them */
      "movdqa %%xmm2,%%xmm1\n"
      "movdqa %%xmm4,%%xmm3\n"

      "punpckhbw %%xmm0,%%xmm2\n"
      "punpckhbw %%xmm0,%%xmm4\n"
      "punpcklbw %%xmm0,%%xmm1\n"  /* mm1 now spread over (mm1,mm2) */
      "punpcklbw %%xmm0,%%xmm3\n"  /* mm4 now spread over (mm3,mm4) */

      "pmaddwd %%xmm2,%%xmm2\n"
      "pmaddwd %%xmm4,%%xmm4\n"
      "pmaddwd %%xmm1,%%xmm1\n"
      "pmaddwd %%xmm3,%%xmm3\n"

      "lea (%0,%4,2), %0\n"        /* pix1 += 2*line_size */
      "lea (%1,%4,2), %1\n"        /* pix2 += 2*line_size */

      "paddd %%xmm2,%%xmm1\n"
      "paddd %%xmm4,%%xmm3\n"
      "paddd %%xmm1,%%xmm7\n"
      "paddd %%xmm3,%%xmm7\n"

      "decl %2\n"
      "jnz 1b\n"

      "movdqa %%xmm7,%%xmm1\n"
      "psrldq $8, %%xmm7\n"        /* shift hi qword to lo */
      "paddd %%xmm1,%%xmm7\n"
      "movdqa %%xmm7,%%xmm1\n"
      "psrldq $4, %%xmm7\n"        /* shift hi dword to lo */
      "paddd %%xmm1,%%xmm7\n"
      "movd %%xmm7,%3\n"
      : "+r" (pix1), "+r" (pix2), "+r"(h), "=r"(tmp)
      : "r" ((long)line_size));
    return tmp;
}

static int hf_noise8_mmx(uint8_t * pix1, int line_size, int h) {
    int tmp;
  asm volatile (
      "movl %3,%%ecx\n"
      "pxor %%mm7,%%mm7\n"
      "pxor %%mm6,%%mm6\n"

      "movq (%0),%%mm0\n"
      "movq %%mm0, %%mm1\n"
      "psllq $8, %%mm0\n"
      "psrlq $8, %%mm1\n"
      "psrlq $8, %%mm0\n"
      "movq %%mm0, %%mm2\n"
      "movq %%mm1, %%mm3\n"
      "punpcklbw %%mm7,%%mm0\n"
      "punpcklbw %%mm7,%%mm1\n"
      "punpckhbw %%mm7,%%mm2\n"
      "punpckhbw %%mm7,%%mm3\n"
      "psubw %%mm1, %%mm0\n"
      "psubw %%mm3, %%mm2\n"

      "add %2,%0\n"

      "movq (%0),%%mm4\n"
      "movq %%mm4, %%mm1\n"
      "psllq $8, %%mm4\n"
      "psrlq $8, %%mm1\n"
      "psrlq $8, %%mm4\n"
      "movq %%mm4, %%mm5\n"
      "movq %%mm1, %%mm3\n"
      "punpcklbw %%mm7,%%mm4\n"
      "punpcklbw %%mm7,%%mm1\n"
      "punpckhbw %%mm7,%%mm5\n"
      "punpckhbw %%mm7,%%mm3\n"
      "psubw %%mm1, %%mm4\n"
      "psubw %%mm3, %%mm5\n"
      "psubw %%mm4, %%mm0\n"
      "psubw %%mm5, %%mm2\n"
      "pxor %%mm3, %%mm3\n"
      "pxor %%mm1, %%mm1\n"
      "pcmpgtw %%mm0, %%mm3\n\t"
      "pcmpgtw %%mm2, %%mm1\n\t"
      "pxor %%mm3, %%mm0\n"
      "pxor %%mm1, %%mm2\n"
      "psubw %%mm3, %%mm0\n"
      "psubw %%mm1, %%mm2\n"
      "paddw %%mm0, %%mm2\n"
      "paddw %%mm2, %%mm6\n"

      "add %2,%0\n"
      "1:\n"

      "movq (%0),%%mm0\n"
      "movq %%mm0, %%mm1\n"
      "psllq $8, %%mm0\n"
      "psrlq $8, %%mm1\n"
      "psrlq $8, %%mm0\n"
      "movq %%mm0, %%mm2\n"
      "movq %%mm1, %%mm3\n"
      "punpcklbw %%mm7,%%mm0\n"
      "punpcklbw %%mm7,%%mm1\n"
      "punpckhbw %%mm7,%%mm2\n"
      "punpckhbw %%mm7,%%mm3\n"
      "psubw %%mm1, %%mm0\n"
      "psubw %%mm3, %%mm2\n"
      "psubw %%mm0, %%mm4\n"
      "psubw %%mm2, %%mm5\n"
      "pxor %%mm3, %%mm3\n"
      "pxor %%mm1, %%mm1\n"
      "pcmpgtw %%mm4, %%mm3\n\t"
      "pcmpgtw %%mm5, %%mm1\n\t"
      "pxor %%mm3, %%mm4\n"
      "pxor %%mm1, %%mm5\n"
      "psubw %%mm3, %%mm4\n"
      "psubw %%mm1, %%mm5\n"
      "paddw %%mm4, %%mm5\n"
      "paddw %%mm5, %%mm6\n"

      "add %2,%0\n"

      "movq (%0),%%mm4\n"
      "movq %%mm4, %%mm1\n"
      "psllq $8, %%mm4\n"
      "psrlq $8, %%mm1\n"
      "psrlq $8, %%mm4\n"
      "movq %%mm4, %%mm5\n"
      "movq %%mm1, %%mm3\n"
      "punpcklbw %%mm7,%%mm4\n"
      "punpcklbw %%mm7,%%mm1\n"
      "punpckhbw %%mm7,%%mm5\n"
      "punpckhbw %%mm7,%%mm3\n"
      "psubw %%mm1, %%mm4\n"
      "psubw %%mm3, %%mm5\n"
      "psubw %%mm4, %%mm0\n"
      "psubw %%mm5, %%mm2\n"
      "pxor %%mm3, %%mm3\n"
      "pxor %%mm1, %%mm1\n"
      "pcmpgtw %%mm0, %%mm3\n\t"
      "pcmpgtw %%mm2, %%mm1\n\t"
      "pxor %%mm3, %%mm0\n"
      "pxor %%mm1, %%mm2\n"
      "psubw %%mm3, %%mm0\n"
      "psubw %%mm1, %%mm2\n"
      "paddw %%mm0, %%mm2\n"
      "paddw %%mm2, %%mm6\n"

      "add %2,%0\n"
      "subl $2, %%ecx\n"
      " jnz 1b\n"

      "movq %%mm6, %%mm0\n"
      "punpcklwd %%mm7,%%mm0\n"
      "punpckhwd %%mm7,%%mm6\n"
      "paddd %%mm0, %%mm6\n"

      "movq %%mm6,%%mm0\n"
      "psrlq $32, %%mm6\n"
      "paddd %%mm6,%%mm0\n"
      "movd %%mm0,%1\n"
      : "+r" (pix1), "=r"(tmp)
      : "r" ((long)line_size) , "g" (h-2)
      : "%ecx");
      return tmp;
}

static int hf_noise16_mmx(uint8_t * pix1, int line_size, int h) {
    int tmp;
    uint8_t * pix= pix1;
  asm volatile (
      "movl %3,%%ecx\n"
      "pxor %%mm7,%%mm7\n"
      "pxor %%mm6,%%mm6\n"

      "movq (%0),%%mm0\n"
      "movq 1(%0),%%mm1\n"
      "movq %%mm0, %%mm2\n"
      "movq %%mm1, %%mm3\n"
      "punpcklbw %%mm7,%%mm0\n"
      "punpcklbw %%mm7,%%mm1\n"
      "punpckhbw %%mm7,%%mm2\n"
      "punpckhbw %%mm7,%%mm3\n"
      "psubw %%mm1, %%mm0\n"
      "psubw %%mm3, %%mm2\n"

      "add %2,%0\n"

      "movq (%0),%%mm4\n"
      "movq 1(%0),%%mm1\n"
      "movq %%mm4, %%mm5\n"
      "movq %%mm1, %%mm3\n"
      "punpcklbw %%mm7,%%mm4\n"
      "punpcklbw %%mm7,%%mm1\n"
      "punpckhbw %%mm7,%%mm5\n"
      "punpckhbw %%mm7,%%mm3\n"
      "psubw %%mm1, %%mm4\n"
      "psubw %%mm3, %%mm5\n"
      "psubw %%mm4, %%mm0\n"
      "psubw %%mm5, %%mm2\n"
      "pxor %%mm3, %%mm3\n"
      "pxor %%mm1, %%mm1\n"
      "pcmpgtw %%mm0, %%mm3\n\t"
      "pcmpgtw %%mm2, %%mm1\n\t"
      "pxor %%mm3, %%mm0\n"
      "pxor %%mm1, %%mm2\n"
      "psubw %%mm3, %%mm0\n"
      "psubw %%mm1, %%mm2\n"
      "paddw %%mm0, %%mm2\n"
      "paddw %%mm2, %%mm6\n"

      "add %2,%0\n"
      "1:\n"

      "movq (%0),%%mm0\n"
      "movq 1(%0),%%mm1\n"
      "movq %%mm0, %%mm2\n"
      "movq %%mm1, %%mm3\n"
      "punpcklbw %%mm7,%%mm0\n"
      "punpcklbw %%mm7,%%mm1\n"
      "punpckhbw %%mm7,%%mm2\n"
      "punpckhbw %%mm7,%%mm3\n"
      "psubw %%mm1, %%mm0\n"
      "psubw %%mm3, %%mm2\n"
      "psubw %%mm0, %%mm4\n"
      "psubw %%mm2, %%mm5\n"
      "pxor %%mm3, %%mm3\n"
      "pxor %%mm1, %%mm1\n"
      "pcmpgtw %%mm4, %%mm3\n\t"
      "pcmpgtw %%mm5, %%mm1\n\t"
      "pxor %%mm3, %%mm4\n"
      "pxor %%mm1, %%mm5\n"
      "psubw %%mm3, %%mm4\n"
      "psubw %%mm1, %%mm5\n"
      "paddw %%mm4, %%mm5\n"
      "paddw %%mm5, %%mm6\n"

      "add %2,%0\n"

      "movq (%0),%%mm4\n"
      "movq 1(%0),%%mm1\n"
      "movq %%mm4, %%mm5\n"
      "movq %%mm1, %%mm3\n"
      "punpcklbw %%mm7,%%mm4\n"
      "punpcklbw %%mm7,%%mm1\n"
      "punpckhbw %%mm7,%%mm5\n"
      "punpckhbw %%mm7,%%mm3\n"
      "psubw %%mm1, %%mm4\n"
      "psubw %%mm3, %%mm5\n"
      "psubw %%mm4, %%mm0\n"
      "psubw %%mm5, %%mm2\n"
      "pxor %%mm3, %%mm3\n"
      "pxor %%mm1, %%mm1\n"
      "pcmpgtw %%mm0, %%mm3\n\t"
      "pcmpgtw %%mm2, %%mm1\n\t"
      "pxor %%mm3, %%mm0\n"
      "pxor %%mm1, %%mm2\n"
      "psubw %%mm3, %%mm0\n"
      "psubw %%mm1, %%mm2\n"
      "paddw %%mm0, %%mm2\n"
      "paddw %%mm2, %%mm6\n"

      "add %2,%0\n"
      "subl $2, %%ecx\n"
      " jnz 1b\n"

      "movq %%mm6, %%mm0\n"
      "punpcklwd %%mm7,%%mm0\n"
      "punpckhwd %%mm7,%%mm6\n"
      "paddd %%mm0, %%mm6\n"

      "movq %%mm6,%%mm0\n"
      "psrlq $32, %%mm6\n"
      "paddd %%mm6,%%mm0\n"
      "movd %%mm0,%1\n"
      : "+r" (pix1), "=r"(tmp)
      : "r" ((long)line_size) , "g" (h-2)
      : "%ecx");
      return tmp + hf_noise8_mmx(pix+8, line_size, h);
}

static int nsse16_mmx(void *p, uint8_t * pix1, uint8_t * pix2, int line_size, int h) {
    MpegEncContext *c = p;
    int score1, score2;

    if(c) score1 = c->dsp.sse[0](c, pix1, pix2, line_size, h);
    else  score1 = sse16_mmx(c, pix1, pix2, line_size, h);
    score2= hf_noise16_mmx(pix1, line_size, h) - hf_noise16_mmx(pix2, line_size, h);

    if(c) return score1 + FFABS(score2)*c->avctx->nsse_weight;
    else  return score1 + FFABS(score2)*8;
}

static int nsse8_mmx(void *p, uint8_t * pix1, uint8_t * pix2, int line_size, int h) {
    MpegEncContext *c = p;
    int score1= sse8_mmx(c, pix1, pix2, line_size, h);
    int score2= hf_noise8_mmx(pix1, line_size, h) - hf_noise8_mmx(pix2, line_size, h);

    if(c) return score1 + FFABS(score2)*c->avctx->nsse_weight;
    else  return score1 + FFABS(score2)*8;
}

static int vsad_intra16_mmx(void *v, uint8_t * pix, uint8_t * dummy, int line_size, int h) {
    int tmp;

    assert( (((int)pix) & 7) == 0);
    assert((line_size &7) ==0);

#define SUM(in0, in1, out0, out1) \
      "movq (%0), %%mm2\n"\
      "movq 8(%0), %%mm3\n"\
      "add %2,%0\n"\
      "movq %%mm2, " #out0 "\n"\
      "movq %%mm3, " #out1 "\n"\
      "psubusb " #in0 ", %%mm2\n"\
      "psubusb " #in1 ", %%mm3\n"\
      "psubusb " #out0 ", " #in0 "\n"\
      "psubusb " #out1 ", " #in1 "\n"\
      "por %%mm2, " #in0 "\n"\
      "por %%mm3, " #in1 "\n"\
      "movq " #in0 ", %%mm2\n"\
      "movq " #in1 ", %%mm3\n"\
      "punpcklbw %%mm7, " #in0 "\n"\
      "punpcklbw %%mm7, " #in1 "\n"\
      "punpckhbw %%mm7, %%mm2\n"\
      "punpckhbw %%mm7, %%mm3\n"\
      "paddw " #in1 ", " #in0 "\n"\
      "paddw %%mm3, %%mm2\n"\
      "paddw %%mm2, " #in0 "\n"\
      "paddw " #in0 ", %%mm6\n"


  asm volatile (
      "movl %3,%%ecx\n"
      "pxor %%mm6,%%mm6\n"
      "pxor %%mm7,%%mm7\n"
      "movq (%0),%%mm0\n"
      "movq 8(%0),%%mm1\n"
      "add %2,%0\n"
      "subl $2, %%ecx\n"
      SUM(%%mm0, %%mm1, %%mm4, %%mm5)
      "1:\n"

      SUM(%%mm4, %%mm5, %%mm0, %%mm1)

      SUM(%%mm0, %%mm1, %%mm4, %%mm5)

      "subl $2, %%ecx\n"
      "jnz 1b\n"

      "movq %%mm6,%%mm0\n"
      "psrlq $32, %%mm6\n"
      "paddw %%mm6,%%mm0\n"
      "movq %%mm0,%%mm6\n"
      "psrlq $16, %%mm0\n"
      "paddw %%mm6,%%mm0\n"
      "movd %%mm0,%1\n"
      : "+r" (pix), "=r"(tmp)
      : "r" ((long)line_size) , "m" (h)
      : "%ecx");
    return tmp & 0xFFFF;
}
#undef SUM

static int vsad_intra16_mmx2(void *v, uint8_t * pix, uint8_t * dummy, int line_size, int h) {
    int tmp;

    assert( (((int)pix) & 7) == 0);
    assert((line_size &7) ==0);

#define SUM(in0, in1, out0, out1) \
      "movq (%0), " #out0 "\n"\
      "movq 8(%0), " #out1 "\n"\
      "add %2,%0\n"\
      "psadbw " #out0 ", " #in0 "\n"\
      "psadbw " #out1 ", " #in1 "\n"\
      "paddw " #in1 ", " #in0 "\n"\
      "paddw " #in0 ", %%mm6\n"

  asm volatile (
      "movl %3,%%ecx\n"
      "pxor %%mm6,%%mm6\n"
      "pxor %%mm7,%%mm7\n"
      "movq (%0),%%mm0\n"
      "movq 8(%0),%%mm1\n"
      "add %2,%0\n"
      "subl $2, %%ecx\n"
      SUM(%%mm0, %%mm1, %%mm4, %%mm5)
      "1:\n"

      SUM(%%mm4, %%mm5, %%mm0, %%mm1)

      SUM(%%mm0, %%mm1, %%mm4, %%mm5)

      "subl $2, %%ecx\n"
      "jnz 1b\n"

      "movd %%mm6,%1\n"
      : "+r" (pix), "=r"(tmp)
      : "r" ((long)line_size) , "m" (h)
      : "%ecx");
    return tmp;
}
#undef SUM

static int vsad16_mmx(void *v, uint8_t * pix1, uint8_t * pix2, int line_size, int h) {
    int tmp;

    assert( (((int)pix1) & 7) == 0);
    assert( (((int)pix2) & 7) == 0);
    assert((line_size &7) ==0);

#define SUM(in0, in1, out0, out1) \
      "movq (%0),%%mm2\n"\
      "movq (%1)," #out0 "\n"\
      "movq 8(%0),%%mm3\n"\
      "movq 8(%1)," #out1 "\n"\
      "add %3,%0\n"\
      "add %3,%1\n"\
      "psubb " #out0 ", %%mm2\n"\
      "psubb " #out1 ", %%mm3\n"\
      "pxor %%mm7, %%mm2\n"\
      "pxor %%mm7, %%mm3\n"\
      "movq %%mm2, " #out0 "\n"\
      "movq %%mm3, " #out1 "\n"\
      "psubusb " #in0 ", %%mm2\n"\
      "psubusb " #in1 ", %%mm3\n"\
      "psubusb " #out0 ", " #in0 "\n"\
      "psubusb " #out1 ", " #in1 "\n"\
      "por %%mm2, " #in0 "\n"\
      "por %%mm3, " #in1 "\n"\
      "movq " #in0 ", %%mm2\n"\
      "movq " #in1 ", %%mm3\n"\
      "punpcklbw %%mm7, " #in0 "\n"\
      "punpcklbw %%mm7, " #in1 "\n"\
      "punpckhbw %%mm7, %%mm2\n"\
      "punpckhbw %%mm7, %%mm3\n"\
      "paddw " #in1 ", " #in0 "\n"\
      "paddw %%mm3, %%mm2\n"\
      "paddw %%mm2, " #in0 "\n"\
      "paddw " #in0 ", %%mm6\n"


  asm volatile (
      "movl %4,%%ecx\n"
      "pxor %%mm6,%%mm6\n"
      "pcmpeqw %%mm7,%%mm7\n"
      "psllw $15, %%mm7\n"
      "packsswb %%mm7, %%mm7\n"
      "movq (%0),%%mm0\n"
      "movq (%1),%%mm2\n"
      "movq 8(%0),%%mm1\n"
      "movq 8(%1),%%mm3\n"
      "add %3,%0\n"
      "add %3,%1\n"
      "subl $2, %%ecx\n"
      "psubb %%mm2, %%mm0\n"
      "psubb %%mm3, %%mm1\n"
      "pxor %%mm7, %%mm0\n"
      "pxor %%mm7, %%mm1\n"
      SUM(%%mm0, %%mm1, %%mm4, %%mm5)
      "1:\n"

      SUM(%%mm4, %%mm5, %%mm0, %%mm1)

      SUM(%%mm0, %%mm1, %%mm4, %%mm5)

      "subl $2, %%ecx\n"
      "jnz 1b\n"

      "movq %%mm6,%%mm0\n"
      "psrlq $32, %%mm6\n"
      "paddw %%mm6,%%mm0\n"
      "movq %%mm0,%%mm6\n"
      "psrlq $16, %%mm0\n"
      "paddw %%mm6,%%mm0\n"
      "movd %%mm0,%2\n"
      : "+r" (pix1), "+r" (pix2), "=r"(tmp)
      : "r" ((long)line_size) , "m" (h)
      : "%ecx");
    return tmp & 0x7FFF;
}
#undef SUM

static int vsad16_mmx2(void *v, uint8_t * pix1, uint8_t * pix2, int line_size, int h) {
    int tmp;

    assert( (((int)pix1) & 7) == 0);
    assert( (((int)pix2) & 7) == 0);
    assert((line_size &7) ==0);

#define SUM(in0, in1, out0, out1) \
      "movq (%0)," #out0 "\n"\
      "movq (%1),%%mm2\n"\
      "movq 8(%0)," #out1 "\n"\
      "movq 8(%1),%%mm3\n"\
      "add %3,%0\n"\
      "add %3,%1\n"\
      "psubb %%mm2, " #out0 "\n"\
      "psubb %%mm3, " #out1 "\n"\
      "pxor %%mm7, " #out0 "\n"\
      "pxor %%mm7, " #out1 "\n"\
      "psadbw " #out0 ", " #in0 "\n"\
      "psadbw " #out1 ", " #in1 "\n"\
      "paddw " #in1 ", " #in0 "\n"\
      "paddw " #in0 ", %%mm6\n"

  asm volatile (
      "movl %4,%%ecx\n"
      "pxor %%mm6,%%mm6\n"
      "pcmpeqw %%mm7,%%mm7\n"
      "psllw $15, %%mm7\n"
      "packsswb %%mm7, %%mm7\n"
      "movq (%0),%%mm0\n"
      "movq (%1),%%mm2\n"
      "movq 8(%0),%%mm1\n"
      "movq 8(%1),%%mm3\n"
      "add %3,%0\n"
      "add %3,%1\n"
      "subl $2, %%ecx\n"
      "psubb %%mm2, %%mm0\n"
      "psubb %%mm3, %%mm1\n"
      "pxor %%mm7, %%mm0\n"
      "pxor %%mm7, %%mm1\n"
      SUM(%%mm0, %%mm1, %%mm4, %%mm5)
      "1:\n"

      SUM(%%mm4, %%mm5, %%mm0, %%mm1)

      SUM(%%mm0, %%mm1, %%mm4, %%mm5)

      "subl $2, %%ecx\n"
      "jnz 1b\n"

      "movd %%mm6,%2\n"
      : "+r" (pix1), "+r" (pix2), "=r"(tmp)
      : "r" ((long)line_size) , "m" (h)
      : "%ecx");
    return tmp;
}
#undef SUM

static void diff_bytes_mmx(uint8_t *dst, uint8_t *src1, uint8_t *src2, int w){
    long i=0;
    asm volatile(
        "1:                             \n\t"
        "movq  (%2, %0), %%mm0          \n\t"
        "movq  (%1, %0), %%mm1          \n\t"
        "psubb %%mm0, %%mm1             \n\t"
        "movq %%mm1, (%3, %0)           \n\t"
        "movq 8(%2, %0), %%mm0          \n\t"
        "movq 8(%1, %0), %%mm1          \n\t"
        "psubb %%mm0, %%mm1             \n\t"
        "movq %%mm1, 8(%3, %0)          \n\t"
        "add $16, %0                    \n\t"
        "cmp %4, %0                     \n\t"
        " jb 1b                         \n\t"
        : "+r" (i)
        : "r"(src1), "r"(src2), "r"(dst), "r"((long)w-15)
    );
    for(; i<w; i++)
        dst[i+0] = src1[i+0]-src2[i+0];
}

static void sub_hfyu_median_prediction_mmx2(uint8_t *dst, uint8_t *src1, uint8_t *src2, int w, int *left, int *left_top){
    long i=0;
    uint8_t l, lt;

    asm volatile(
        "1:                             \n\t"
        "movq  -1(%1, %0), %%mm0        \n\t" // LT
        "movq  (%1, %0), %%mm1          \n\t" // T
        "movq  -1(%2, %0), %%mm2        \n\t" // L
        "movq  (%2, %0), %%mm3          \n\t" // X
        "movq %%mm2, %%mm4              \n\t" // L
        "psubb %%mm0, %%mm2             \n\t"
        "paddb %%mm1, %%mm2             \n\t" // L + T - LT
        "movq %%mm4, %%mm5              \n\t" // L
        "pmaxub %%mm1, %%mm4            \n\t" // max(T, L)
        "pminub %%mm5, %%mm1            \n\t" // min(T, L)
        "pminub %%mm2, %%mm4            \n\t"
        "pmaxub %%mm1, %%mm4            \n\t"
        "psubb %%mm4, %%mm3             \n\t" // dst - pred
        "movq %%mm3, (%3, %0)           \n\t"
        "add $8, %0                     \n\t"
        "cmp %4, %0                     \n\t"
        " jb 1b                         \n\t"
        : "+r" (i)
        : "r"(src1), "r"(src2), "r"(dst), "r"((long)w)
    );

    l= *left;
    lt= *left_top;

    dst[0]= src2[0] - mid_pred(l, src1[0], (l + src1[0] - lt)&0xFF);

    *left_top= src1[w-1];
    *left    = src2[w-1];
}

#define LBUTTERFLY2(a1,b1,a2,b2)\
    "paddw " #b1 ", " #a1 "           \n\t"\
    "paddw " #b2 ", " #a2 "           \n\t"\
    "paddw " #b1 ", " #b1 "           \n\t"\
    "paddw " #b2 ", " #b2 "           \n\t"\
    "psubw " #a1 ", " #b1 "           \n\t"\
    "psubw " #a2 ", " #b2 "           \n\t"

#define HADAMARD48\
        LBUTTERFLY2(%%mm0, %%mm1, %%mm2, %%mm3)\
        LBUTTERFLY2(%%mm4, %%mm5, %%mm6, %%mm7)\
        LBUTTERFLY2(%%mm0, %%mm2, %%mm1, %%mm3)\
        LBUTTERFLY2(%%mm4, %%mm6, %%mm5, %%mm7)\
        LBUTTERFLY2(%%mm0, %%mm4, %%mm1, %%mm5)\
        LBUTTERFLY2(%%mm2, %%mm6, %%mm3, %%mm7)\

#define MMABS(a,z)\
    "pxor " #z ", " #z "              \n\t"\
    "pcmpgtw " #a ", " #z "           \n\t"\
    "pxor " #z ", " #a "              \n\t"\
    "psubw " #z ", " #a "             \n\t"

#define MMABS_SUM(a,z, sum)\
    "pxor " #z ", " #z "              \n\t"\
    "pcmpgtw " #a ", " #z "           \n\t"\
    "pxor " #z ", " #a "              \n\t"\
    "psubw " #z ", " #a "             \n\t"\
    "paddusw " #a ", " #sum "         \n\t"

#define MMABS_MMX2(a,z)\
    "pxor " #z ", " #z "              \n\t"\
    "psubw " #a ", " #z "             \n\t"\
    "pmaxsw " #z ", " #a "            \n\t"

#define MMABS_SUM_MMX2(a,z, sum)\
    "pxor " #z ", " #z "              \n\t"\
    "psubw " #a ", " #z "             \n\t"\
    "pmaxsw " #z ", " #a "            \n\t"\
    "paddusw " #a ", " #sum "         \n\t"

#define TRANSPOSE4(a,b,c,d,t)\
    SBUTTERFLY(a,b,t,wd) /* a=aebf t=cgdh */\
    SBUTTERFLY(c,d,b,wd) /* c=imjn b=kolp */\
    SBUTTERFLY(a,c,d,dq) /* a=aeim d=bfjn */\
    SBUTTERFLY(t,b,c,dq) /* t=cgko c=dhlp */

#define LOAD4(o, a, b, c, d)\
        "movq "#o"(%1), " #a "        \n\t"\
        "movq "#o"+16(%1), " #b "     \n\t"\
        "movq "#o"+32(%1), " #c "     \n\t"\
        "movq "#o"+48(%1), " #d "     \n\t"

#define STORE4(o, a, b, c, d)\
        "movq "#a", "#o"(%1)          \n\t"\
        "movq "#b", "#o"+16(%1)       \n\t"\
        "movq "#c", "#o"+32(%1)       \n\t"\
        "movq "#d", "#o"+48(%1)       \n\t"\

static int hadamard8_diff_mmx(void *s, uint8_t *src1, uint8_t *src2, int stride, int h){
    DECLARE_ALIGNED_8(uint64_t, temp[16]);
    int sum=0;

    assert(h==8);

    diff_pixels_mmx((DCTELEM*)temp, src1, src2, stride);

    asm volatile(
        LOAD4(0 , %%mm0, %%mm1, %%mm2, %%mm3)
        LOAD4(64, %%mm4, %%mm5, %%mm6, %%mm7)

        HADAMARD48

        "movq %%mm7, 112(%1)            \n\t"

        TRANSPOSE4(%%mm0, %%mm1, %%mm2, %%mm3, %%mm7)
        STORE4(0 , %%mm0, %%mm3, %%mm7, %%mm2)

        "movq 112(%1), %%mm7            \n\t"
        TRANSPOSE4(%%mm4, %%mm5, %%mm6, %%mm7, %%mm0)
        STORE4(64, %%mm4, %%mm7, %%mm0, %%mm6)

        LOAD4(8 , %%mm0, %%mm1, %%mm2, %%mm3)
        LOAD4(72, %%mm4, %%mm5, %%mm6, %%mm7)

        HADAMARD48

        "movq %%mm7, 120(%1)            \n\t"

        TRANSPOSE4(%%mm0, %%mm1, %%mm2, %%mm3, %%mm7)
        STORE4(8 , %%mm0, %%mm3, %%mm7, %%mm2)

        "movq 120(%1), %%mm7            \n\t"
        TRANSPOSE4(%%mm4, %%mm5, %%mm6, %%mm7, %%mm0)
        "movq %%mm7, %%mm5              \n\t"//FIXME remove
        "movq %%mm6, %%mm7              \n\t"
        "movq %%mm0, %%mm6              \n\t"
//        STORE4(72, %%mm4, %%mm7, %%mm0, %%mm6) //FIXME remove

        LOAD4(64, %%mm0, %%mm1, %%mm2, %%mm3)
//        LOAD4(72, %%mm4, %%mm5, %%mm6, %%mm7)

        HADAMARD48
        "movq %%mm7, 64(%1)             \n\t"
        MMABS(%%mm0, %%mm7)
        MMABS_SUM(%%mm1, %%mm7, %%mm0)
        MMABS_SUM(%%mm2, %%mm7, %%mm0)
        MMABS_SUM(%%mm3, %%mm7, %%mm0)
        MMABS_SUM(%%mm4, %%mm7, %%mm0)
        MMABS_SUM(%%mm5, %%mm7, %%mm0)
        MMABS_SUM(%%mm6, %%mm7, %%mm0)
        "movq 64(%1), %%mm1             \n\t"
        MMABS_SUM(%%mm1, %%mm7, %%mm0)
        "movq %%mm0, 64(%1)             \n\t"

        LOAD4(0 , %%mm0, %%mm1, %%mm2, %%mm3)
        LOAD4(8 , %%mm4, %%mm5, %%mm6, %%mm7)

        HADAMARD48
        "movq %%mm7, (%1)               \n\t"
        MMABS(%%mm0, %%mm7)
        MMABS_SUM(%%mm1, %%mm7, %%mm0)
        MMABS_SUM(%%mm2, %%mm7, %%mm0)
        MMABS_SUM(%%mm3, %%mm7, %%mm0)
        MMABS_SUM(%%mm4, %%mm7, %%mm0)
        MMABS_SUM(%%mm5, %%mm7, %%mm0)
        MMABS_SUM(%%mm6, %%mm7, %%mm0)
        "movq (%1), %%mm1               \n\t"
        MMABS_SUM(%%mm1, %%mm7, %%mm0)
        "movq 64(%1), %%mm1             \n\t"
        MMABS_SUM(%%mm1, %%mm7, %%mm0)

        "movq %%mm0, %%mm1              \n\t"
        "psrlq $32, %%mm0               \n\t"
        "paddusw %%mm1, %%mm0           \n\t"
        "movq %%mm0, %%mm1              \n\t"
        "psrlq $16, %%mm0               \n\t"
        "paddusw %%mm1, %%mm0           \n\t"
        "movd %%mm0, %0                 \n\t"

        : "=r" (sum)
        : "r"(temp)
    );
    return sum&0xFFFF;
}

static int hadamard8_diff_mmx2(void *s, uint8_t *src1, uint8_t *src2, int stride, int h){
    DECLARE_ALIGNED_8(uint64_t, temp[16]);
    int sum=0;

    assert(h==8);

    diff_pixels_mmx((DCTELEM*)temp, src1, src2, stride);

    asm volatile(
        LOAD4(0 , %%mm0, %%mm1, %%mm2, %%mm3)
        LOAD4(64, %%mm4, %%mm5, %%mm6, %%mm7)

        HADAMARD48

        "movq %%mm7, 112(%1)            \n\t"

        TRANSPOSE4(%%mm0, %%mm1, %%mm2, %%mm3, %%mm7)
        STORE4(0 , %%mm0, %%mm3, %%mm7, %%mm2)

        "movq 112(%1), %%mm7            \n\t"
        TRANSPOSE4(%%mm4, %%mm5, %%mm6, %%mm7, %%mm0)
        STORE4(64, %%mm4, %%mm7, %%mm0, %%mm6)

        LOAD4(8 , %%mm0, %%mm1, %%mm2, %%mm3)
        LOAD4(72, %%mm4, %%mm5, %%mm6, %%mm7)

        HADAMARD48

        "movq %%mm7, 120(%1)            \n\t"

        TRANSPOSE4(%%mm0, %%mm1, %%mm2, %%mm3, %%mm7)
        STORE4(8 , %%mm0, %%mm3, %%mm7, %%mm2)

        "movq 120(%1), %%mm7            \n\t"
        TRANSPOSE4(%%mm4, %%mm5, %%mm6, %%mm7, %%mm0)
        "movq %%mm7, %%mm5              \n\t"//FIXME remove
        "movq %%mm6, %%mm7              \n\t"
        "movq %%mm0, %%mm6              \n\t"
//        STORE4(72, %%mm4, %%mm7, %%mm0, %%mm6) //FIXME remove

        LOAD4(64, %%mm0, %%mm1, %%mm2, %%mm3)
//        LOAD4(72, %%mm4, %%mm5, %%mm6, %%mm7)

        HADAMARD48
        "movq %%mm7, 64(%1)             \n\t"
        MMABS_MMX2(%%mm0, %%mm7)
        MMABS_SUM_MMX2(%%mm1, %%mm7, %%mm0)
        MMABS_SUM_MMX2(%%mm2, %%mm7, %%mm0)
        MMABS_SUM_MMX2(%%mm3, %%mm7, %%mm0)
        MMABS_SUM_MMX2(%%mm4, %%mm7, %%mm0)
        MMABS_SUM_MMX2(%%mm5, %%mm7, %%mm0)
        MMABS_SUM_MMX2(%%mm6, %%mm7, %%mm0)
        "movq 64(%1), %%mm1             \n\t"
        MMABS_SUM_MMX2(%%mm1, %%mm7, %%mm0)
        "movq %%mm0, 64(%1)             \n\t"

        LOAD4(0 , %%mm0, %%mm1, %%mm2, %%mm3)
        LOAD4(8 , %%mm4, %%mm5, %%mm6, %%mm7)

        HADAMARD48
        "movq %%mm7, (%1)               \n\t"
        MMABS_MMX2(%%mm0, %%mm7)
        MMABS_SUM_MMX2(%%mm1, %%mm7, %%mm0)
        MMABS_SUM_MMX2(%%mm2, %%mm7, %%mm0)
        MMABS_SUM_MMX2(%%mm3, %%mm7, %%mm0)
        MMABS_SUM_MMX2(%%mm4, %%mm7, %%mm0)
        MMABS_SUM_MMX2(%%mm5, %%mm7, %%mm0)
        MMABS_SUM_MMX2(%%mm6, %%mm7, %%mm0)
        "movq (%1), %%mm1               \n\t"
        MMABS_SUM_MMX2(%%mm1, %%mm7, %%mm0)
        "movq 64(%1), %%mm1             \n\t"
        MMABS_SUM_MMX2(%%mm1, %%mm7, %%mm0)

        "pshufw $0x0E, %%mm0, %%mm1     \n\t"
        "paddusw %%mm1, %%mm0           \n\t"
        "pshufw $0x01, %%mm0, %%mm1     \n\t"
        "paddusw %%mm1, %%mm0           \n\t"
        "movd %%mm0, %0                 \n\t"

        : "=r" (sum)
        : "r"(temp)
    );
    return sum&0xFFFF;
}


WARPER8_16_SQ(hadamard8_diff_mmx, hadamard8_diff16_mmx)
WARPER8_16_SQ(hadamard8_diff_mmx2, hadamard8_diff16_mmx2)

static int ssd_int8_vs_int16_mmx(int8_t *pix1, int16_t *pix2, int size){
    int sum;
    long i=size;
    asm volatile(
        "pxor %%mm4, %%mm4 \n"
        "1: \n"
        "sub $8, %0 \n"
        "movq (%2,%0), %%mm2 \n"
        "movq (%3,%0,2), %%mm0 \n"
        "movq 8(%3,%0,2), %%mm1 \n"
        "punpckhbw %%mm2, %%mm3 \n"
        "punpcklbw %%mm2, %%mm2 \n"
        "psraw $8, %%mm3 \n"
        "psraw $8, %%mm2 \n"
        "psubw %%mm3, %%mm1 \n"
        "psubw %%mm2, %%mm0 \n"
        "pmaddwd %%mm1, %%mm1 \n"
        "pmaddwd %%mm0, %%mm0 \n"
        "paddd %%mm1, %%mm4 \n"
        "paddd %%mm0, %%mm4 \n"
        "jg 1b \n"
        "movq %%mm4, %%mm3 \n"
        "psrlq $32, %%mm3 \n"
        "paddd %%mm3, %%mm4 \n"
        "movd %%mm4, %1 \n"
        :"+r"(i), "=r"(sum)
        :"r"(pix1), "r"(pix2)
    );
    return sum;
}

#endif //CONFIG_ENCODERS

#define put_no_rnd_pixels8_mmx(a,b,c,d) put_pixels8_mmx(a,b,c,d)
#define put_no_rnd_pixels16_mmx(a,b,c,d) put_pixels16_mmx(a,b,c,d)

#define QPEL_V_LOW(m3,m4,m5,m6, pw_20, pw_3, rnd, in0, in1, in2, in7, out, OP)\
        "paddw " #m4 ", " #m3 "           \n\t" /* x1 */\
        "movq "MANGLE(ff_pw_20)", %%mm4   \n\t" /* 20 */\
        "pmullw " #m3 ", %%mm4            \n\t" /* 20x1 */\
        "movq "#in7", " #m3 "             \n\t" /* d */\
        "movq "#in0", %%mm5               \n\t" /* D */\
        "paddw " #m3 ", %%mm5             \n\t" /* x4 */\
        "psubw %%mm5, %%mm4               \n\t" /* 20x1 - x4 */\
        "movq "#in1", %%mm5               \n\t" /* C */\
        "movq "#in2", %%mm6               \n\t" /* B */\
        "paddw " #m6 ", %%mm5             \n\t" /* x3 */\
        "paddw " #m5 ", %%mm6             \n\t" /* x2 */\
        "paddw %%mm6, %%mm6               \n\t" /* 2x2 */\
        "psubw %%mm6, %%mm5               \n\t" /* -2x2 + x3 */\
        "pmullw "MANGLE(ff_pw_3)", %%mm5  \n\t" /* -6x2 + 3x3 */\
        "paddw " #rnd ", %%mm4            \n\t" /* x2 */\
        "paddw %%mm4, %%mm5               \n\t" /* 20x1 - 6x2 + 3x3 - x4 */\
        "psraw $5, %%mm5                  \n\t"\
        "packuswb %%mm5, %%mm5            \n\t"\
        OP(%%mm5, out, %%mm7, d)

#define QPEL_BASE(OPNAME, ROUNDER, RND, OP_MMX2, OP_3DNOW)\
static void OPNAME ## mpeg4_qpel16_h_lowpass_mmx2(uint8_t *dst, uint8_t *src, int dstStride, int srcStride, int h){\
    uint64_t temp;\
\
    asm volatile(\
        "pxor %%mm7, %%mm7                \n\t"\
        "1:                               \n\t"\
        "movq  (%0), %%mm0                \n\t" /* ABCDEFGH */\
        "movq %%mm0, %%mm1                \n\t" /* ABCDEFGH */\
        "movq %%mm0, %%mm2                \n\t" /* ABCDEFGH */\
        "punpcklbw %%mm7, %%mm0           \n\t" /* 0A0B0C0D */\
        "punpckhbw %%mm7, %%mm1           \n\t" /* 0E0F0G0H */\
        "pshufw $0x90, %%mm0, %%mm5       \n\t" /* 0A0A0B0C */\
        "pshufw $0x41, %%mm0, %%mm6       \n\t" /* 0B0A0A0B */\
        "movq %%mm2, %%mm3                \n\t" /* ABCDEFGH */\
        "movq %%mm2, %%mm4                \n\t" /* ABCDEFGH */\
        "psllq $8, %%mm2                  \n\t" /* 0ABCDEFG */\
        "psllq $16, %%mm3                 \n\t" /* 00ABCDEF */\
        "psllq $24, %%mm4                 \n\t" /* 000ABCDE */\
        "punpckhbw %%mm7, %%mm2           \n\t" /* 0D0E0F0G */\
        "punpckhbw %%mm7, %%mm3           \n\t" /* 0C0D0E0F */\
        "punpckhbw %%mm7, %%mm4           \n\t" /* 0B0C0D0E */\
        "paddw %%mm3, %%mm5               \n\t" /* b */\
        "paddw %%mm2, %%mm6               \n\t" /* c */\
        "paddw %%mm5, %%mm5               \n\t" /* 2b */\
        "psubw %%mm5, %%mm6               \n\t" /* c - 2b */\
        "pshufw $0x06, %%mm0, %%mm5       \n\t" /* 0C0B0A0A */\
        "pmullw "MANGLE(ff_pw_3)", %%mm6  \n\t" /* 3c - 6b */\
        "paddw %%mm4, %%mm0               \n\t" /* a */\
        "paddw %%mm1, %%mm5               \n\t" /* d */\
        "pmullw "MANGLE(ff_pw_20)", %%mm0 \n\t" /* 20a */\
        "psubw %%mm5, %%mm0               \n\t" /* 20a - d */\
        "paddw %6, %%mm6                  \n\t"\
        "paddw %%mm6, %%mm0               \n\t" /* 20a - 6b + 3c - d */\
        "psraw $5, %%mm0                  \n\t"\
        "movq %%mm0, %5                   \n\t"\
        /* mm1=EFGH, mm2=DEFG, mm3=CDEF, mm4=BCDE, mm7=0 */\
        \
        "movq 5(%0), %%mm0                \n\t" /* FGHIJKLM */\
        "movq %%mm0, %%mm5                \n\t" /* FGHIJKLM */\
        "movq %%mm0, %%mm6                \n\t" /* FGHIJKLM */\
        "psrlq $8, %%mm0                  \n\t" /* GHIJKLM0 */\
        "psrlq $16, %%mm5                 \n\t" /* HIJKLM00 */\
        "punpcklbw %%mm7, %%mm0           \n\t" /* 0G0H0I0J */\
        "punpcklbw %%mm7, %%mm5           \n\t" /* 0H0I0J0K */\
        "paddw %%mm0, %%mm2               \n\t" /* b */\
        "paddw %%mm5, %%mm3               \n\t" /* c */\
        "paddw %%mm2, %%mm2               \n\t" /* 2b */\
        "psubw %%mm2, %%mm3               \n\t" /* c - 2b */\
        "movq %%mm6, %%mm2                \n\t" /* FGHIJKLM */\
        "psrlq $24, %%mm6                 \n\t" /* IJKLM000 */\
        "punpcklbw %%mm7, %%mm2           \n\t" /* 0F0G0H0I */\
        "punpcklbw %%mm7, %%mm6           \n\t" /* 0I0J0K0L */\
        "pmullw "MANGLE(ff_pw_3)", %%mm3  \n\t" /* 3c - 6b */\
        "paddw %%mm2, %%mm1               \n\t" /* a */\
        "paddw %%mm6, %%mm4               \n\t" /* d */\
        "pmullw "MANGLE(ff_pw_20)", %%mm1 \n\t" /* 20a */\
        "psubw %%mm4, %%mm3               \n\t" /* - 6b +3c - d */\
        "paddw %6, %%mm1                  \n\t"\
        "paddw %%mm1, %%mm3               \n\t" /* 20a - 6b +3c - d */\
        "psraw $5, %%mm3                  \n\t"\
        "movq %5, %%mm1                   \n\t"\
        "packuswb %%mm3, %%mm1            \n\t"\
        OP_MMX2(%%mm1, (%1),%%mm4, q)\
        /* mm0= GHIJ, mm2=FGHI, mm5=HIJK, mm6=IJKL, mm7=0 */\
        \
        "movq 9(%0), %%mm1                \n\t" /* JKLMNOPQ */\
        "movq %%mm1, %%mm4                \n\t" /* JKLMNOPQ */\
        "movq %%mm1, %%mm3                \n\t" /* JKLMNOPQ */\
        "psrlq $8, %%mm1                  \n\t" /* KLMNOPQ0 */\
        "psrlq $16, %%mm4                 \n\t" /* LMNOPQ00 */\
        "punpcklbw %%mm7, %%mm1           \n\t" /* 0K0L0M0N */\
        "punpcklbw %%mm7, %%mm4           \n\t" /* 0L0M0N0O */\
        "paddw %%mm1, %%mm5               \n\t" /* b */\
        "paddw %%mm4, %%mm0               \n\t" /* c */\
        "paddw %%mm5, %%mm5               \n\t" /* 2b */\
        "psubw %%mm5, %%mm0               \n\t" /* c - 2b */\
        "movq %%mm3, %%mm5                \n\t" /* JKLMNOPQ */\
        "psrlq $24, %%mm3                 \n\t" /* MNOPQ000 */\
        "pmullw "MANGLE(ff_pw_3)", %%mm0  \n\t" /* 3c - 6b */\
        "punpcklbw %%mm7, %%mm3           \n\t" /* 0M0N0O0P */\
        "paddw %%mm3, %%mm2               \n\t" /* d */\
        "psubw %%mm2, %%mm0               \n\t" /* -6b + 3c - d */\
        "movq %%mm5, %%mm2                \n\t" /* JKLMNOPQ */\
        "punpcklbw %%mm7, %%mm2           \n\t" /* 0J0K0L0M */\
        "punpckhbw %%mm7, %%mm5           \n\t" /* 0N0O0P0Q */\
        "paddw %%mm2, %%mm6               \n\t" /* a */\
        "pmullw "MANGLE(ff_pw_20)", %%mm6 \n\t" /* 20a */\
        "paddw %6, %%mm0                  \n\t"\
        "paddw %%mm6, %%mm0               \n\t" /* 20a - 6b + 3c - d */\
        "psraw $5, %%mm0                  \n\t"\
        /* mm1=KLMN, mm2=JKLM, mm3=MNOP, mm4=LMNO, mm5=NOPQ mm7=0 */\
        \
        "paddw %%mm5, %%mm3               \n\t" /* a */\
        "pshufw $0xF9, %%mm5, %%mm6       \n\t" /* 0O0P0Q0Q */\
        "paddw %%mm4, %%mm6               \n\t" /* b */\
        "pshufw $0xBE, %%mm5, %%mm4       \n\t" /* 0P0Q0Q0P */\
        "pshufw $0x6F, %%mm5, %%mm5       \n\t" /* 0Q0Q0P0O */\
        "paddw %%mm1, %%mm4               \n\t" /* c */\
        "paddw %%mm2, %%mm5               \n\t" /* d */\
        "paddw %%mm6, %%mm6               \n\t" /* 2b */\
        "psubw %%mm6, %%mm4               \n\t" /* c - 2b */\
        "pmullw "MANGLE(ff_pw_20)", %%mm3 \n\t" /* 20a */\
        "pmullw "MANGLE(ff_pw_3)", %%mm4  \n\t" /* 3c - 6b */\
        "psubw %%mm5, %%mm3               \n\t" /* -6b + 3c - d */\
        "paddw %6, %%mm4                  \n\t"\
        "paddw %%mm3, %%mm4               \n\t" /* 20a - 6b + 3c - d */\
        "psraw $5, %%mm4                  \n\t"\
        "packuswb %%mm4, %%mm0            \n\t"\
        OP_MMX2(%%mm0, 8(%1), %%mm4, q)\
        \
        "add %3, %0                       \n\t"\
        "add %4, %1                       \n\t"\
        "decl %2                          \n\t"\
        " jnz 1b                          \n\t"\
        : "+a"(src), "+c"(dst), "+m"(h)\
        : "d"((long)srcStride), "S"((long)dstStride), /*"m"(ff_pw_20), "m"(ff_pw_3),*/ "m"(temp), "m"(ROUNDER)\
        : "memory"\
    );\
}\
\
static void OPNAME ## mpeg4_qpel16_h_lowpass_3dnow(uint8_t *dst, uint8_t *src, int dstStride, int srcStride, int h){\
    int i;\
    int16_t temp[16];\
    /* quick HACK, XXX FIXME MUST be optimized */\
    for(i=0; i<h; i++)\
    {\
        temp[ 0]= (src[ 0]+src[ 1])*20 - (src[ 0]+src[ 2])*6 + (src[ 1]+src[ 3])*3 - (src[ 2]+src[ 4]);\
        temp[ 1]= (src[ 1]+src[ 2])*20 - (src[ 0]+src[ 3])*6 + (src[ 0]+src[ 4])*3 - (src[ 1]+src[ 5]);\
        temp[ 2]= (src[ 2]+src[ 3])*20 - (src[ 1]+src[ 4])*6 + (src[ 0]+src[ 5])*3 - (src[ 0]+src[ 6]);\
        temp[ 3]= (src[ 3]+src[ 4])*20 - (src[ 2]+src[ 5])*6 + (src[ 1]+src[ 6])*3 - (src[ 0]+src[ 7]);\
        temp[ 4]= (src[ 4]+src[ 5])*20 - (src[ 3]+src[ 6])*6 + (src[ 2]+src[ 7])*3 - (src[ 1]+src[ 8]);\
        temp[ 5]= (src[ 5]+src[ 6])*20 - (src[ 4]+src[ 7])*6 + (src[ 3]+src[ 8])*3 - (src[ 2]+src[ 9]);\
        temp[ 6]= (src[ 6]+src[ 7])*20 - (src[ 5]+src[ 8])*6 + (src[ 4]+src[ 9])*3 - (src[ 3]+src[10]);\
        temp[ 7]= (src[ 7]+src[ 8])*20 - (src[ 6]+src[ 9])*6 + (src[ 5]+src[10])*3 - (src[ 4]+src[11]);\
        temp[ 8]= (src[ 8]+src[ 9])*20 - (src[ 7]+src[10])*6 + (src[ 6]+src[11])*3 - (src[ 5]+src[12]);\
        temp[ 9]= (src[ 9]+src[10])*20 - (src[ 8]+src[11])*6 + (src[ 7]+src[12])*3 - (src[ 6]+src[13]);\
        temp[10]= (src[10]+src[11])*20 - (src[ 9]+src[12])*6 + (src[ 8]+src[13])*3 - (src[ 7]+src[14]);\
        temp[11]= (src[11]+src[12])*20 - (src[10]+src[13])*6 + (src[ 9]+src[14])*3 - (src[ 8]+src[15]);\
        temp[12]= (src[12]+src[13])*20 - (src[11]+src[14])*6 + (src[10]+src[15])*3 - (src[ 9]+src[16]);\
        temp[13]= (src[13]+src[14])*20 - (src[12]+src[15])*6 + (src[11]+src[16])*3 - (src[10]+src[16]);\
        temp[14]= (src[14]+src[15])*20 - (src[13]+src[16])*6 + (src[12]+src[16])*3 - (src[11]+src[15]);\
        temp[15]= (src[15]+src[16])*20 - (src[14]+src[16])*6 + (src[13]+src[15])*3 - (src[12]+src[14]);\
        asm volatile(\
            "movq (%0), %%mm0               \n\t"\
            "movq 8(%0), %%mm1              \n\t"\
            "paddw %2, %%mm0                \n\t"\
            "paddw %2, %%mm1                \n\t"\
            "psraw $5, %%mm0                \n\t"\
            "psraw $5, %%mm1                \n\t"\
            "packuswb %%mm1, %%mm0          \n\t"\
            OP_3DNOW(%%mm0, (%1), %%mm1, q)\
            "movq 16(%0), %%mm0             \n\t"\
            "movq 24(%0), %%mm1             \n\t"\
            "paddw %2, %%mm0                \n\t"\
            "paddw %2, %%mm1                \n\t"\
            "psraw $5, %%mm0                \n\t"\
            "psraw $5, %%mm1                \n\t"\
            "packuswb %%mm1, %%mm0          \n\t"\
            OP_3DNOW(%%mm0, 8(%1), %%mm1, q)\
            :: "r"(temp), "r"(dst), "m"(ROUNDER)\
            : "memory"\
        );\
        dst+=dstStride;\
        src+=srcStride;\
    }\
}\
\
static void OPNAME ## mpeg4_qpel8_h_lowpass_mmx2(uint8_t *dst, uint8_t *src, int dstStride, int srcStride, int h){\
    uint64_t temp;\
\
    asm volatile(\
        "pxor %%mm7, %%mm7                \n\t"\
        "1:                               \n\t"\
        "movq  (%0), %%mm0                \n\t" /* ABCDEFGH */\
        "movq %%mm0, %%mm1                \n\t" /* ABCDEFGH */\
        "movq %%mm0, %%mm2                \n\t" /* ABCDEFGH */\
        "punpcklbw %%mm7, %%mm0           \n\t" /* 0A0B0C0D */\
        "punpckhbw %%mm7, %%mm1           \n\t" /* 0E0F0G0H */\
        "pshufw $0x90, %%mm0, %%mm5       \n\t" /* 0A0A0B0C */\
        "pshufw $0x41, %%mm0, %%mm6       \n\t" /* 0B0A0A0B */\
        "movq %%mm2, %%mm3                \n\t" /* ABCDEFGH */\
        "movq %%mm2, %%mm4                \n\t" /* ABCDEFGH */\
        "psllq $8, %%mm2                  \n\t" /* 0ABCDEFG */\
        "psllq $16, %%mm3                 \n\t" /* 00ABCDEF */\
        "psllq $24, %%mm4                 \n\t" /* 000ABCDE */\
        "punpckhbw %%mm7, %%mm2           \n\t" /* 0D0E0F0G */\
        "punpckhbw %%mm7, %%mm3           \n\t" /* 0C0D0E0F */\
        "punpckhbw %%mm7, %%mm4           \n\t" /* 0B0C0D0E */\
        "paddw %%mm3, %%mm5               \n\t" /* b */\
        "paddw %%mm2, %%mm6               \n\t" /* c */\
        "paddw %%mm5, %%mm5               \n\t" /* 2b */\
        "psubw %%mm5, %%mm6               \n\t" /* c - 2b */\
        "pshufw $0x06, %%mm0, %%mm5       \n\t" /* 0C0B0A0A */\
        "pmullw "MANGLE(ff_pw_3)", %%mm6  \n\t" /* 3c - 6b */\
        "paddw %%mm4, %%mm0               \n\t" /* a */\
        "paddw %%mm1, %%mm5               \n\t" /* d */\
        "pmullw "MANGLE(ff_pw_20)", %%mm0 \n\t" /* 20a */\
        "psubw %%mm5, %%mm0               \n\t" /* 20a - d */\
        "paddw %6, %%mm6                  \n\t"\
        "paddw %%mm6, %%mm0               \n\t" /* 20a - 6b + 3c - d */\
        "psraw $5, %%mm0                  \n\t"\
        /* mm1=EFGH, mm2=DEFG, mm3=CDEF, mm4=BCDE, mm7=0 */\
        \
        "movd 5(%0), %%mm5                \n\t" /* FGHI */\
        "punpcklbw %%mm7, %%mm5           \n\t" /* 0F0G0H0I */\
        "pshufw $0xF9, %%mm5, %%mm6       \n\t" /* 0G0H0I0I */\
        "paddw %%mm5, %%mm1               \n\t" /* a */\
        "paddw %%mm6, %%mm2               \n\t" /* b */\
        "pshufw $0xBE, %%mm5, %%mm6       \n\t" /* 0H0I0I0H */\
        "pshufw $0x6F, %%mm5, %%mm5       \n\t" /* 0I0I0H0G */\
        "paddw %%mm6, %%mm3               \n\t" /* c */\
        "paddw %%mm5, %%mm4               \n\t" /* d */\
        "paddw %%mm2, %%mm2               \n\t" /* 2b */\
        "psubw %%mm2, %%mm3               \n\t" /* c - 2b */\
        "pmullw "MANGLE(ff_pw_20)", %%mm1 \n\t" /* 20a */\
        "pmullw "MANGLE(ff_pw_3)", %%mm3  \n\t" /* 3c - 6b */\
        "psubw %%mm4, %%mm3               \n\t" /* -6b + 3c - d */\
        "paddw %6, %%mm1                  \n\t"\
        "paddw %%mm1, %%mm3               \n\t" /* 20a - 6b + 3c - d */\
        "psraw $5, %%mm3                  \n\t"\
        "packuswb %%mm3, %%mm0            \n\t"\
        OP_MMX2(%%mm0, (%1), %%mm4, q)\
        \
        "add %3, %0                       \n\t"\
        "add %4, %1                       \n\t"\
        "decl %2                          \n\t"\
        " jnz 1b                          \n\t"\
        : "+a"(src), "+c"(dst), "+m"(h)\
        : "S"((long)srcStride), "D"((long)dstStride), /*"m"(ff_pw_20), "m"(ff_pw_3),*/ "m"(temp), "m"(ROUNDER)\
        : "memory"\
    );\
}\
\
static void OPNAME ## mpeg4_qpel8_h_lowpass_3dnow(uint8_t *dst, uint8_t *src, int dstStride, int srcStride, int h){\
    int i;\
    int16_t temp[8];\
    /* quick HACK, XXX FIXME MUST be optimized */\
    for(i=0; i<h; i++)\
    {\
        temp[ 0]= (src[ 0]+src[ 1])*20 - (src[ 0]+src[ 2])*6 + (src[ 1]+src[ 3])*3 - (src[ 2]+src[ 4]);\
        temp[ 1]= (src[ 1]+src[ 2])*20 - (src[ 0]+src[ 3])*6 + (src[ 0]+src[ 4])*3 - (src[ 1]+src[ 5]);\
        temp[ 2]= (src[ 2]+src[ 3])*20 - (src[ 1]+src[ 4])*6 + (src[ 0]+src[ 5])*3 - (src[ 0]+src[ 6]);\
        temp[ 3]= (src[ 3]+src[ 4])*20 - (src[ 2]+src[ 5])*6 + (src[ 1]+src[ 6])*3 - (src[ 0]+src[ 7]);\
        temp[ 4]= (src[ 4]+src[ 5])*20 - (src[ 3]+src[ 6])*6 + (src[ 2]+src[ 7])*3 - (src[ 1]+src[ 8]);\
        temp[ 5]= (src[ 5]+src[ 6])*20 - (src[ 4]+src[ 7])*6 + (src[ 3]+src[ 8])*3 - (src[ 2]+src[ 8]);\
        temp[ 6]= (src[ 6]+src[ 7])*20 - (src[ 5]+src[ 8])*6 + (src[ 4]+src[ 8])*3 - (src[ 3]+src[ 7]);\
        temp[ 7]= (src[ 7]+src[ 8])*20 - (src[ 6]+src[ 8])*6 + (src[ 5]+src[ 7])*3 - (src[ 4]+src[ 6]);\
        asm volatile(\
            "movq (%0), %%mm0           \n\t"\
            "movq 8(%0), %%mm1          \n\t"\
            "paddw %2, %%mm0            \n\t"\
            "paddw %2, %%mm1            \n\t"\
            "psraw $5, %%mm0            \n\t"\
            "psraw $5, %%mm1            \n\t"\
            "packuswb %%mm1, %%mm0      \n\t"\
            OP_3DNOW(%%mm0, (%1), %%mm1, q)\
            :: "r"(temp), "r"(dst), "m"(ROUNDER)\
            :"memory"\
        );\
        dst+=dstStride;\
        src+=srcStride;\
    }\
}

#define QPEL_OP(OPNAME, ROUNDER, RND, OP, MMX)\
\
static void OPNAME ## mpeg4_qpel16_v_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    uint64_t temp[17*4];\
    uint64_t *temp_ptr= temp;\
    int count= 17;\
\
    /*FIXME unroll */\
    asm volatile(\
        "pxor %%mm7, %%mm7              \n\t"\
        "1:                             \n\t"\
        "movq (%0), %%mm0               \n\t"\
        "movq (%0), %%mm1               \n\t"\
        "movq 8(%0), %%mm2              \n\t"\
        "movq 8(%0), %%mm3              \n\t"\
        "punpcklbw %%mm7, %%mm0         \n\t"\
        "punpckhbw %%mm7, %%mm1         \n\t"\
        "punpcklbw %%mm7, %%mm2         \n\t"\
        "punpckhbw %%mm7, %%mm3         \n\t"\
        "movq %%mm0, (%1)               \n\t"\
        "movq %%mm1, 17*8(%1)           \n\t"\
        "movq %%mm2, 2*17*8(%1)         \n\t"\
        "movq %%mm3, 3*17*8(%1)         \n\t"\
        "add $8, %1                     \n\t"\
        "add %3, %0                     \n\t"\
        "decl %2                        \n\t"\
        " jnz 1b                        \n\t"\
        : "+r" (src), "+r" (temp_ptr), "+r"(count)\
        : "r" ((long)srcStride)\
        : "memory"\
    );\
    \
    temp_ptr= temp;\
    count=4;\
    \
/*FIXME reorder for speed */\
    asm volatile(\
        /*"pxor %%mm7, %%mm7              \n\t"*/\
        "1:                             \n\t"\
        "movq (%0), %%mm0               \n\t"\
        "movq 8(%0), %%mm1              \n\t"\
        "movq 16(%0), %%mm2             \n\t"\
        "movq 24(%0), %%mm3             \n\t"\
        QPEL_V_LOW(%%mm0, %%mm1, %%mm2, %%mm3, %5, %6, %5, 16(%0),  8(%0),   (%0), 32(%0), (%1), OP)\
        QPEL_V_LOW(%%mm1, %%mm2, %%mm3, %%mm0, %5, %6, %5,  8(%0),   (%0),   (%0), 40(%0), (%1, %3), OP)\
        "add %4, %1                     \n\t"\
        QPEL_V_LOW(%%mm2, %%mm3, %%mm0, %%mm1, %5, %6, %5,   (%0),   (%0),  8(%0), 48(%0), (%1), OP)\
        \
        QPEL_V_LOW(%%mm3, %%mm0, %%mm1, %%mm2, %5, %6, %5,   (%0),  8(%0), 16(%0), 56(%0), (%1, %3), OP)\
        "add %4, %1                     \n\t"\
        QPEL_V_LOW(%%mm0, %%mm1, %%mm2, %%mm3, %5, %6, %5,  8(%0), 16(%0), 24(%0), 64(%0), (%1), OP)\
        QPEL_V_LOW(%%mm1, %%mm2, %%mm3, %%mm0, %5, %6, %5, 16(%0), 24(%0), 32(%0), 72(%0), (%1, %3), OP)\
        "add %4, %1                     \n\t"\
        QPEL_V_LOW(%%mm2, %%mm3, %%mm0, %%mm1, %5, %6, %5, 24(%0), 32(%0), 40(%0), 80(%0), (%1), OP)\
        QPEL_V_LOW(%%mm3, %%mm0, %%mm1, %%mm2, %5, %6, %5, 32(%0), 40(%0), 48(%0), 88(%0), (%1, %3), OP)\
        "add %4, %1                     \n\t"\
        QPEL_V_LOW(%%mm0, %%mm1, %%mm2, %%mm3, %5, %6, %5, 40(%0), 48(%0), 56(%0), 96(%0), (%1), OP)\
        QPEL_V_LOW(%%mm1, %%mm2, %%mm3, %%mm0, %5, %6, %5, 48(%0), 56(%0), 64(%0),104(%0), (%1, %3), OP)\
        "add %4, %1                     \n\t"\
        QPEL_V_LOW(%%mm2, %%mm3, %%mm0, %%mm1, %5, %6, %5, 56(%0), 64(%0), 72(%0),112(%0), (%1), OP)\
        QPEL_V_LOW(%%mm3, %%mm0, %%mm1, %%mm2, %5, %6, %5, 64(%0), 72(%0), 80(%0),120(%0), (%1, %3), OP)\
        "add %4, %1                     \n\t"\
        QPEL_V_LOW(%%mm0, %%mm1, %%mm2, %%mm3, %5, %6, %5, 72(%0), 80(%0), 88(%0),128(%0), (%1), OP)\
        \
        QPEL_V_LOW(%%mm1, %%mm2, %%mm3, %%mm0, %5, %6, %5, 80(%0), 88(%0), 96(%0),128(%0), (%1, %3), OP)\
        "add %4, %1                     \n\t"  \
        QPEL_V_LOW(%%mm2, %%mm3, %%mm0, %%mm1, %5, %6, %5, 88(%0), 96(%0),104(%0),120(%0), (%1), OP)\
        QPEL_V_LOW(%%mm3, %%mm0, %%mm1, %%mm2, %5, %6, %5, 96(%0),104(%0),112(%0),112(%0), (%1, %3), OP)\
        \
        "add $136, %0                   \n\t"\
        "add %6, %1                     \n\t"\
        "decl %2                        \n\t"\
        " jnz 1b                        \n\t"\
        \
        : "+r"(temp_ptr), "+r"(dst), "+g"(count)\
        : "r"((long)dstStride), "r"(2*(long)dstStride), /*"m"(ff_pw_20), "m"(ff_pw_3),*/ "m"(ROUNDER), "g"(4-14*(long)dstStride)\
        :"memory"\
    );\
}\
\
static void OPNAME ## mpeg4_qpel8_v_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    uint64_t temp[9*2];\
    uint64_t *temp_ptr= temp;\
    int count= 9;\
\
    /*FIXME unroll */\
    asm volatile(\
        "pxor %%mm7, %%mm7              \n\t"\
        "1:                             \n\t"\
        "movq (%0), %%mm0               \n\t"\
        "movq (%0), %%mm1               \n\t"\
        "punpcklbw %%mm7, %%mm0         \n\t"\
        "punpckhbw %%mm7, %%mm1         \n\t"\
        "movq %%mm0, (%1)               \n\t"\
        "movq %%mm1, 9*8(%1)            \n\t"\
        "add $8, %1                     \n\t"\
        "add %3, %0                     \n\t"\
        "decl %2                        \n\t"\
        " jnz 1b                        \n\t"\
        : "+r" (src), "+r" (temp_ptr), "+r"(count)\
        : "r" ((long)srcStride)\
        : "memory"\
    );\
    \
    temp_ptr= temp;\
    count=2;\
    \
/*FIXME reorder for speed */\
    asm volatile(\
        /*"pxor %%mm7, %%mm7              \n\t"*/\
        "1:                             \n\t"\
        "movq (%0), %%mm0               \n\t"\
        "movq 8(%0), %%mm1              \n\t"\
        "movq 16(%0), %%mm2             \n\t"\
        "movq 24(%0), %%mm3             \n\t"\
        QPEL_V_LOW(%%mm0, %%mm1, %%mm2, %%mm3, %5, %6, %5, 16(%0),  8(%0),   (%0), 32(%0), (%1), OP)\
        QPEL_V_LOW(%%mm1, %%mm2, %%mm3, %%mm0, %5, %6, %5,  8(%0),   (%0),   (%0), 40(%0), (%1, %3), OP)\
        "add %4, %1                     \n\t"\
        QPEL_V_LOW(%%mm2, %%mm3, %%mm0, %%mm1, %5, %6, %5,   (%0),   (%0),  8(%0), 48(%0), (%1), OP)\
        \
        QPEL_V_LOW(%%mm3, %%mm0, %%mm1, %%mm2, %5, %6, %5,   (%0),  8(%0), 16(%0), 56(%0), (%1, %3), OP)\
        "add %4, %1                     \n\t"\
        QPEL_V_LOW(%%mm0, %%mm1, %%mm2, %%mm3, %5, %6, %5,  8(%0), 16(%0), 24(%0), 64(%0), (%1), OP)\
        \
        QPEL_V_LOW(%%mm1, %%mm2, %%mm3, %%mm0, %5, %6, %5, 16(%0), 24(%0), 32(%0), 64(%0), (%1, %3), OP)\
        "add %4, %1                     \n\t"\
        QPEL_V_LOW(%%mm2, %%mm3, %%mm0, %%mm1, %5, %6, %5, 24(%0), 32(%0), 40(%0), 56(%0), (%1), OP)\
        QPEL_V_LOW(%%mm3, %%mm0, %%mm1, %%mm2, %5, %6, %5, 32(%0), 40(%0), 48(%0), 48(%0), (%1, %3), OP)\
                \
        "add $72, %0                    \n\t"\
        "add %6, %1                     \n\t"\
        "decl %2                        \n\t"\
        " jnz 1b                        \n\t"\
         \
        : "+r"(temp_ptr), "+r"(dst), "+g"(count)\
        : "r"((long)dstStride), "r"(2*(long)dstStride), /*"m"(ff_pw_20), "m"(ff_pw_3),*/ "m"(ROUNDER), "g"(4-6*(long)dstStride)\
        : "memory"\
   );\
}\
\
static void OPNAME ## qpel8_mc00_ ## MMX (uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## pixels8_mmx(dst, src, stride, 8);\
}\
\
static void OPNAME ## qpel8_mc10_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[8];\
    uint8_t * const half= (uint8_t*)temp;\
    put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(half, src, 8, stride, 8);\
    OPNAME ## pixels8_l2_ ## MMX(dst, src, half, stride, stride, 8);\
}\
\
static void OPNAME ## qpel8_mc20_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## mpeg4_qpel8_h_lowpass_ ## MMX(dst, src, stride, stride, 8);\
}\
\
static void OPNAME ## qpel8_mc30_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[8];\
    uint8_t * const half= (uint8_t*)temp;\
    put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(half, src, 8, stride, 8);\
    OPNAME ## pixels8_l2_ ## MMX(dst, src+1, half, stride, stride, 8);\
}\
\
static void OPNAME ## qpel8_mc01_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[8];\
    uint8_t * const half= (uint8_t*)temp;\
    put ## RND ## mpeg4_qpel8_v_lowpass_ ## MMX(half, src, 8, stride);\
    OPNAME ## pixels8_l2_ ## MMX(dst, src, half, stride, stride, 8);\
}\
\
static void OPNAME ## qpel8_mc02_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## mpeg4_qpel8_v_lowpass_ ## MMX(dst, src, stride, stride);\
}\
\
static void OPNAME ## qpel8_mc03_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[8];\
    uint8_t * const half= (uint8_t*)temp;\
    put ## RND ## mpeg4_qpel8_v_lowpass_ ## MMX(half, src, 8, stride);\
    OPNAME ## pixels8_l2_ ## MMX(dst, src+stride, half, stride, stride, 8);\
}\
static void OPNAME ## qpel8_mc11_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t half[8 + 9];\
    uint8_t * const halfH= ((uint8_t*)half) + 64;\
    uint8_t * const halfHV= ((uint8_t*)half);\
    put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(halfH, src, 8, stride, 9);\
    put ## RND ## pixels8_l2_ ## MMX(halfH, src, halfH, 8, stride, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass_ ## MMX(halfHV, halfH, 8, 8);\
    OPNAME ## pixels8_l2_ ## MMX(dst, halfH, halfHV, stride, 8, 8);\
}\
static void OPNAME ## qpel8_mc31_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t half[8 + 9];\
    uint8_t * const halfH= ((uint8_t*)half) + 64;\
    uint8_t * const halfHV= ((uint8_t*)half);\
    put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(halfH, src, 8, stride, 9);\
    put ## RND ## pixels8_l2_ ## MMX(halfH, src+1, halfH, 8, stride, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass_ ## MMX(halfHV, halfH, 8, 8);\
    OPNAME ## pixels8_l2_ ## MMX(dst, halfH, halfHV, stride, 8, 8);\
}\
static void OPNAME ## qpel8_mc13_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t half[8 + 9];\
    uint8_t * const halfH= ((uint8_t*)half) + 64;\
    uint8_t * const halfHV= ((uint8_t*)half);\
    put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(halfH, src, 8, stride, 9);\
    put ## RND ## pixels8_l2_ ## MMX(halfH, src, halfH, 8, stride, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass_ ## MMX(halfHV, halfH, 8, 8);\
    OPNAME ## pixels8_l2_ ## MMX(dst, halfH+8, halfHV, stride, 8, 8);\
}\
static void OPNAME ## qpel8_mc33_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t half[8 + 9];\
    uint8_t * const halfH= ((uint8_t*)half) + 64;\
    uint8_t * const halfHV= ((uint8_t*)half);\
    put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(halfH, src, 8, stride, 9);\
    put ## RND ## pixels8_l2_ ## MMX(halfH, src+1, halfH, 8, stride, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass_ ## MMX(halfHV, halfH, 8, 8);\
    OPNAME ## pixels8_l2_ ## MMX(dst, halfH+8, halfHV, stride, 8, 8);\
}\
static void OPNAME ## qpel8_mc21_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t half[8 + 9];\
    uint8_t * const halfH= ((uint8_t*)half) + 64;\
    uint8_t * const halfHV= ((uint8_t*)half);\
    put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(halfH, src, 8, stride, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass_ ## MMX(halfHV, halfH, 8, 8);\
    OPNAME ## pixels8_l2_ ## MMX(dst, halfH, halfHV, stride, 8, 8);\
}\
static void OPNAME ## qpel8_mc23_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t half[8 + 9];\
    uint8_t * const halfH= ((uint8_t*)half) + 64;\
    uint8_t * const halfHV= ((uint8_t*)half);\
    put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(halfH, src, 8, stride, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass_ ## MMX(halfHV, halfH, 8, 8);\
    OPNAME ## pixels8_l2_ ## MMX(dst, halfH+8, halfHV, stride, 8, 8);\
}\
static void OPNAME ## qpel8_mc12_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t half[8 + 9];\
    uint8_t * const halfH= ((uint8_t*)half);\
    put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(halfH, src, 8, stride, 9);\
    put ## RND ## pixels8_l2_ ## MMX(halfH, src, halfH, 8, stride, 9);\
    OPNAME ## mpeg4_qpel8_v_lowpass_ ## MMX(dst, halfH, stride, 8);\
}\
static void OPNAME ## qpel8_mc32_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t half[8 + 9];\
    uint8_t * const halfH= ((uint8_t*)half);\
    put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(halfH, src, 8, stride, 9);\
    put ## RND ## pixels8_l2_ ## MMX(halfH, src+1, halfH, 8, stride, 9);\
    OPNAME ## mpeg4_qpel8_v_lowpass_ ## MMX(dst, halfH, stride, 8);\
}\
static void OPNAME ## qpel8_mc22_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t half[9];\
    uint8_t * const halfH= ((uint8_t*)half);\
    put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(halfH, src, 8, stride, 9);\
    OPNAME ## mpeg4_qpel8_v_lowpass_ ## MMX(dst, halfH, stride, 8);\
}\
static void OPNAME ## qpel16_mc00_ ## MMX (uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## pixels16_mmx(dst, src, stride, 16);\
}\
\
static void OPNAME ## qpel16_mc10_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[32];\
    uint8_t * const half= (uint8_t*)temp;\
    put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(half, src, 16, stride, 16);\
    OPNAME ## pixels16_l2_ ## MMX(dst, src, half, stride, stride, 16);\
}\
\
static void OPNAME ## qpel16_mc20_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## mpeg4_qpel16_h_lowpass_ ## MMX(dst, src, stride, stride, 16);\
}\
\
static void OPNAME ## qpel16_mc30_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[32];\
    uint8_t * const half= (uint8_t*)temp;\
    put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(half, src, 16, stride, 16);\
    OPNAME ## pixels16_l2_ ## MMX(dst, src+1, half, stride, stride, 16);\
}\
\
static void OPNAME ## qpel16_mc01_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[32];\
    uint8_t * const half= (uint8_t*)temp;\
    put ## RND ## mpeg4_qpel16_v_lowpass_ ## MMX(half, src, 16, stride);\
    OPNAME ## pixels16_l2_ ## MMX(dst, src, half, stride, stride, 16);\
}\
\
static void OPNAME ## qpel16_mc02_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## mpeg4_qpel16_v_lowpass_ ## MMX(dst, src, stride, stride);\
}\
\
static void OPNAME ## qpel16_mc03_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[32];\
    uint8_t * const half= (uint8_t*)temp;\
    put ## RND ## mpeg4_qpel16_v_lowpass_ ## MMX(half, src, 16, stride);\
    OPNAME ## pixels16_l2_ ## MMX(dst, src+stride, half, stride, stride, 16);\
}\
static void OPNAME ## qpel16_mc11_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t half[16*2 + 17*2];\
    uint8_t * const halfH= ((uint8_t*)half) + 256;\
    uint8_t * const halfHV= ((uint8_t*)half);\
    put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(halfH, src, 16, stride, 17);\
    put ## RND ## pixels16_l2_ ## MMX(halfH, src, halfH, 16, stride, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass_ ## MMX(halfHV, halfH, 16, 16);\
    OPNAME ## pixels16_l2_ ## MMX(dst, halfH, halfHV, stride, 16, 16);\
}\
static void OPNAME ## qpel16_mc31_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t half[16*2 + 17*2];\
    uint8_t * const halfH= ((uint8_t*)half) + 256;\
    uint8_t * const halfHV= ((uint8_t*)half);\
    put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(halfH, src, 16, stride, 17);\
    put ## RND ## pixels16_l2_ ## MMX(halfH, src+1, halfH, 16, stride, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass_ ## MMX(halfHV, halfH, 16, 16);\
    OPNAME ## pixels16_l2_ ## MMX(dst, halfH, halfHV, stride, 16, 16);\
}\
static void OPNAME ## qpel16_mc13_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t half[16*2 + 17*2];\
    uint8_t * const halfH= ((uint8_t*)half) + 256;\
    uint8_t * const halfHV= ((uint8_t*)half);\
    put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(halfH, src, 16, stride, 17);\
    put ## RND ## pixels16_l2_ ## MMX(halfH, src, halfH, 16, stride, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass_ ## MMX(halfHV, halfH, 16, 16);\
    OPNAME ## pixels16_l2_ ## MMX(dst, halfH+16, halfHV, stride, 16, 16);\
}\
static void OPNAME ## qpel16_mc33_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t half[16*2 + 17*2];\
    uint8_t * const halfH= ((uint8_t*)half) + 256;\
    uint8_t * const halfHV= ((uint8_t*)half);\
    put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(halfH, src, 16, stride, 17);\
    put ## RND ## pixels16_l2_ ## MMX(halfH, src+1, halfH, 16, stride, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass_ ## MMX(halfHV, halfH, 16, 16);\
    OPNAME ## pixels16_l2_ ## MMX(dst, halfH+16, halfHV, stride, 16, 16);\
}\
static void OPNAME ## qpel16_mc21_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t half[16*2 + 17*2];\
    uint8_t * const halfH= ((uint8_t*)half) + 256;\
    uint8_t * const halfHV= ((uint8_t*)half);\
    put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(halfH, src, 16, stride, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass_ ## MMX(halfHV, halfH, 16, 16);\
    OPNAME ## pixels16_l2_ ## MMX(dst, halfH, halfHV, stride, 16, 16);\
}\
static void OPNAME ## qpel16_mc23_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t half[16*2 + 17*2];\
    uint8_t * const halfH= ((uint8_t*)half) + 256;\
    uint8_t * const halfHV= ((uint8_t*)half);\
    put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(halfH, src, 16, stride, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass_ ## MMX(halfHV, halfH, 16, 16);\
    OPNAME ## pixels16_l2_ ## MMX(dst, halfH+16, halfHV, stride, 16, 16);\
}\
static void OPNAME ## qpel16_mc12_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t half[17*2];\
    uint8_t * const halfH= ((uint8_t*)half);\
    put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(halfH, src, 16, stride, 17);\
    put ## RND ## pixels16_l2_ ## MMX(halfH, src, halfH, 16, stride, 17);\
    OPNAME ## mpeg4_qpel16_v_lowpass_ ## MMX(dst, halfH, stride, 16);\
}\
static void OPNAME ## qpel16_mc32_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t half[17*2];\
    uint8_t * const halfH= ((uint8_t*)half);\
    put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(halfH, src, 16, stride, 17);\
    put ## RND ## pixels16_l2_ ## MMX(halfH, src+1, halfH, 16, stride, 17);\
    OPNAME ## mpeg4_qpel16_v_lowpass_ ## MMX(dst, halfH, stride, 16);\
}\
static void OPNAME ## qpel16_mc22_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t half[17*2];\
    uint8_t * const halfH= ((uint8_t*)half);\
    put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(halfH, src, 16, stride, 17);\
    OPNAME ## mpeg4_qpel16_v_lowpass_ ## MMX(dst, halfH, stride, 16);\
}

#define PUT_OP(a,b,temp, size) "mov" #size " " #a ", " #b "        \n\t"
#define AVG_3DNOW_OP(a,b,temp, size) \
"mov" #size " " #b ", " #temp "   \n\t"\
"pavgusb " #temp ", " #a "        \n\t"\
"mov" #size " " #a ", " #b "      \n\t"
#define AVG_MMX2_OP(a,b,temp, size) \
"mov" #size " " #b ", " #temp "   \n\t"\
"pavgb " #temp ", " #a "          \n\t"\
"mov" #size " " #a ", " #b "      \n\t"

QPEL_BASE(put_       , ff_pw_16, _       , PUT_OP, PUT_OP)
QPEL_BASE(avg_       , ff_pw_16, _       , AVG_MMX2_OP, AVG_3DNOW_OP)
QPEL_BASE(put_no_rnd_, ff_pw_15, _no_rnd_, PUT_OP, PUT_OP)
QPEL_OP(put_       , ff_pw_16, _       , PUT_OP, 3dnow)
QPEL_OP(avg_       , ff_pw_16, _       , AVG_3DNOW_OP, 3dnow)
QPEL_OP(put_no_rnd_, ff_pw_15, _no_rnd_, PUT_OP, 3dnow)
QPEL_OP(put_       , ff_pw_16, _       , PUT_OP, mmx2)
QPEL_OP(avg_       , ff_pw_16, _       , AVG_MMX2_OP, mmx2)
QPEL_OP(put_no_rnd_, ff_pw_15, _no_rnd_, PUT_OP, mmx2)

/***********************************/
/* bilinear qpel: not compliant to any spec, only for -lavdopts fast */

#define QPEL_2TAP_XY(OPNAME, SIZE, MMX, XY, HPEL)\
static void OPNAME ## 2tap_qpel ## SIZE ## _mc ## XY ## _ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## pixels ## SIZE ## HPEL(dst, src, stride, SIZE);\
}
#define QPEL_2TAP_L3(OPNAME, SIZE, MMX, XY, S0, S1, S2)\
static void OPNAME ## 2tap_qpel ## SIZE ## _mc ## XY ## _ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## 2tap_qpel ## SIZE ## _l3_ ## MMX(dst, src+S0, stride, SIZE, S1, S2);\
}

#define QPEL_2TAP(OPNAME, SIZE, MMX)\
QPEL_2TAP_XY(OPNAME, SIZE, MMX, 20, _x2_ ## MMX)\
QPEL_2TAP_XY(OPNAME, SIZE, MMX, 02, _y2_ ## MMX)\
QPEL_2TAP_XY(OPNAME, SIZE, MMX, 22, _xy2_mmx)\
static const qpel_mc_func OPNAME ## 2tap_qpel ## SIZE ## _mc00_ ## MMX =\
                          OPNAME ## qpel ## SIZE ## _mc00_ ## MMX;\
static const qpel_mc_func OPNAME ## 2tap_qpel ## SIZE ## _mc21_ ## MMX =\
                          OPNAME ## 2tap_qpel ## SIZE ## _mc20_ ## MMX;\
static const qpel_mc_func OPNAME ## 2tap_qpel ## SIZE ## _mc12_ ## MMX =\
                          OPNAME ## 2tap_qpel ## SIZE ## _mc02_ ## MMX;\
static void OPNAME ## 2tap_qpel ## SIZE ## _mc32_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## pixels ## SIZE ## _y2_ ## MMX(dst, src+1, stride, SIZE);\
}\
static void OPNAME ## 2tap_qpel ## SIZE ## _mc23_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## pixels ## SIZE ## _x2_ ## MMX(dst, src+stride, stride, SIZE);\
}\
QPEL_2TAP_L3(OPNAME, SIZE, MMX, 10, 0,         1,       0)\
QPEL_2TAP_L3(OPNAME, SIZE, MMX, 30, 1,        -1,       0)\
QPEL_2TAP_L3(OPNAME, SIZE, MMX, 01, 0,         stride,  0)\
QPEL_2TAP_L3(OPNAME, SIZE, MMX, 03, stride,   -stride,  0)\
QPEL_2TAP_L3(OPNAME, SIZE, MMX, 11, 0,         stride,  1)\
QPEL_2TAP_L3(OPNAME, SIZE, MMX, 31, 1,         stride, -1)\
QPEL_2TAP_L3(OPNAME, SIZE, MMX, 13, stride,   -stride,  1)\
QPEL_2TAP_L3(OPNAME, SIZE, MMX, 33, stride+1, -stride, -1)\

QPEL_2TAP(put_, 16, mmx2)
QPEL_2TAP(avg_, 16, mmx2)
QPEL_2TAP(put_,  8, mmx2)
QPEL_2TAP(avg_,  8, mmx2)
QPEL_2TAP(put_, 16, 3dnow)
QPEL_2TAP(avg_, 16, 3dnow)
QPEL_2TAP(put_,  8, 3dnow)
QPEL_2TAP(avg_,  8, 3dnow)


#if 0
static void just_return() { return; }
#endif

#define SET_QPEL_FUNC(postfix1, postfix2) \
    c->put_ ## postfix1 = put_ ## postfix2;\
    c->put_no_rnd_ ## postfix1 = put_no_rnd_ ## postfix2;\
    c->avg_ ## postfix1 = avg_ ## postfix2;

static void gmc_mmx(uint8_t *dst, uint8_t *src, int stride, int h, int ox, int oy,
                    int dxx, int dxy, int dyx, int dyy, int shift, int r, int width, int height){
    const int w = 8;
    const int ix = ox>>(16+shift);
    const int iy = oy>>(16+shift);
    const int oxs = ox>>4;
    const int oys = oy>>4;
    const int dxxs = dxx>>4;
    const int dxys = dxy>>4;
    const int dyxs = dyx>>4;
    const int dyys = dyy>>4;
    const uint16_t r4[4] = {r,r,r,r};
    const uint16_t dxy4[4] = {dxys,dxys,dxys,dxys};
    const uint16_t dyy4[4] = {dyys,dyys,dyys,dyys};
    const uint64_t shift2 = 2*shift;
    uint8_t edge_buf[(h+1)*stride];
    int x, y;

    const int dxw = (dxx-(1<<(16+shift)))*(w-1);
    const int dyh = (dyy-(1<<(16+shift)))*(h-1);
    const int dxh = dxy*(h-1);
    const int dyw = dyx*(w-1);
    if( // non-constant fullpel offset (3% of blocks)
        (ox^(ox+dxw) | ox^(ox+dxh) | ox^(ox+dxw+dxh) |
         oy^(oy+dyw) | oy^(oy+dyh) | oy^(oy+dyw+dyh)) >> (16+shift)
        // uses more than 16 bits of subpel mv (only at huge resolution)
        || (dxx|dxy|dyx|dyy)&15 )
    {
        //FIXME could still use mmx for some of the rows
        ff_gmc_c(dst, src, stride, h, ox, oy, dxx, dxy, dyx, dyy, shift, r, width, height);
        return;
    }

    src += ix + iy*stride;
    if( (unsigned)ix >= width-w ||
        (unsigned)iy >= height-h )
    {
        ff_emulated_edge_mc(edge_buf, src, stride, w+1, h+1, ix, iy, width, height);
        src = edge_buf;
    }

    asm volatile(
        "movd         %0, %%mm6 \n\t"
        "pxor      %%mm7, %%mm7 \n\t"
        "punpcklwd %%mm6, %%mm6 \n\t"
        "punpcklwd %%mm6, %%mm6 \n\t"
        :: "r"(1<<shift)
    );

    for(x=0; x<w; x+=4){
        uint16_t dx4[4] = { oxs - dxys + dxxs*(x+0),
                            oxs - dxys + dxxs*(x+1),
                            oxs - dxys + dxxs*(x+2),
                            oxs - dxys + dxxs*(x+3) };
        uint16_t dy4[4] = { oys - dyys + dyxs*(x+0),
                            oys - dyys + dyxs*(x+1),
                            oys - dyys + dyxs*(x+2),
                            oys - dyys + dyxs*(x+3) };

        for(y=0; y<h; y++){
            asm volatile(
                "movq   %0,  %%mm4 \n\t"
                "movq   %1,  %%mm5 \n\t"
                "paddw  %2,  %%mm4 \n\t"
                "paddw  %3,  %%mm5 \n\t"
                "movq   %%mm4, %0  \n\t"
                "movq   %%mm5, %1  \n\t"
                "psrlw  $12, %%mm4 \n\t"
                "psrlw  $12, %%mm5 \n\t"
                : "+m"(*dx4), "+m"(*dy4)
                : "m"(*dxy4), "m"(*dyy4)
            );

            asm volatile(
                "movq   %%mm6, %%mm2 \n\t"
                "movq   %%mm6, %%mm1 \n\t"
                "psubw  %%mm4, %%mm2 \n\t"
                "psubw  %%mm5, %%mm1 \n\t"
                "movq   %%mm2, %%mm0 \n\t"
                "movq   %%mm4, %%mm3 \n\t"
                "pmullw %%mm1, %%mm0 \n\t" // (s-dx)*(s-dy)
                "pmullw %%mm5, %%mm3 \n\t" // dx*dy
                "pmullw %%mm5, %%mm2 \n\t" // (s-dx)*dy
                "pmullw %%mm4, %%mm1 \n\t" // dx*(s-dy)

                "movd   %4,    %%mm5 \n\t"
                "movd   %3,    %%mm4 \n\t"
                "punpcklbw %%mm7, %%mm5 \n\t"
                "punpcklbw %%mm7, %%mm4 \n\t"
                "pmullw %%mm5, %%mm3 \n\t" // src[1,1] * dx*dy
                "pmullw %%mm4, %%mm2 \n\t" // src[0,1] * (s-dx)*dy

                "movd   %2,    %%mm5 \n\t"
                "movd   %1,    %%mm4 \n\t"
                "punpcklbw %%mm7, %%mm5 \n\t"
                "punpcklbw %%mm7, %%mm4 \n\t"
                "pmullw %%mm5, %%mm1 \n\t" // src[1,0] * dx*(s-dy)
                "pmullw %%mm4, %%mm0 \n\t" // src[0,0] * (s-dx)*(s-dy)
                "paddw  %5,    %%mm1 \n\t"
                "paddw  %%mm3, %%mm2 \n\t"
                "paddw  %%mm1, %%mm0 \n\t"
                "paddw  %%mm2, %%mm0 \n\t"

                "psrlw    %6,    %%mm0 \n\t"
                "packuswb %%mm0, %%mm0 \n\t"
                "movd     %%mm0, %0    \n\t"

                : "=m"(dst[x+y*stride])
                : "m"(src[0]), "m"(src[1]),
                  "m"(src[stride]), "m"(src[stride+1]),
                  "m"(*r4), "m"(shift2)
            );
            src += stride;
        }
        src += 4-h*stride;
    }
}

#ifdef CONFIG_ENCODERS
static int try_8x8basis_mmx(int16_t rem[64], int16_t weight[64], int16_t basis[64], int scale){
    long i=0;

    assert(FFABS(scale) < 256);
    scale<<= 16 + 1 - BASIS_SHIFT + RECON_SHIFT;

    asm volatile(
        "pcmpeqw %%mm6, %%mm6           \n\t" // -1w
        "psrlw $15, %%mm6               \n\t" //  1w
        "pxor %%mm7, %%mm7              \n\t"
        "movd  %4, %%mm5                \n\t"
        "punpcklwd %%mm5, %%mm5         \n\t"
        "punpcklwd %%mm5, %%mm5         \n\t"
        "1:                             \n\t"
        "movq  (%1, %0), %%mm0          \n\t"
        "movq  8(%1, %0), %%mm1         \n\t"
        "pmulhw %%mm5, %%mm0            \n\t"
        "pmulhw %%mm5, %%mm1            \n\t"
        "paddw %%mm6, %%mm0             \n\t"
        "paddw %%mm6, %%mm1             \n\t"
        "psraw $1, %%mm0                \n\t"
        "psraw $1, %%mm1                \n\t"
        "paddw (%2, %0), %%mm0          \n\t"
        "paddw 8(%2, %0), %%mm1         \n\t"
        "psraw $6, %%mm0                \n\t"
        "psraw $6, %%mm1                \n\t"
        "pmullw (%3, %0), %%mm0         \n\t"
        "pmullw 8(%3, %0), %%mm1        \n\t"
        "pmaddwd %%mm0, %%mm0           \n\t"
        "pmaddwd %%mm1, %%mm1           \n\t"
        "paddd %%mm1, %%mm0             \n\t"
        "psrld $4, %%mm0                \n\t"
        "paddd %%mm0, %%mm7             \n\t"
        "add $16, %0                    \n\t"
        "cmp $128, %0                   \n\t" //FIXME optimize & bench
        " jb 1b                         \n\t"
        "movq %%mm7, %%mm6              \n\t"
        "psrlq $32, %%mm7               \n\t"
        "paddd %%mm6, %%mm7             \n\t"
        "psrld $2, %%mm7                \n\t"
        "movd %%mm7, %0                 \n\t"

        : "+r" (i)
        : "r"(basis), "r"(rem), "r"(weight), "g"(scale)
    );
    return i;
}

static void add_8x8basis_mmx(int16_t rem[64], int16_t basis[64], int scale){
    long i=0;

    if(FFABS(scale) < 256){
        scale<<= 16 + 1 - BASIS_SHIFT + RECON_SHIFT;
        asm volatile(
                "pcmpeqw %%mm6, %%mm6   \n\t" // -1w
                "psrlw $15, %%mm6       \n\t" //  1w
                "movd  %3, %%mm5        \n\t"
                "punpcklwd %%mm5, %%mm5 \n\t"
                "punpcklwd %%mm5, %%mm5 \n\t"
                "1:                     \n\t"
                "movq  (%1, %0), %%mm0  \n\t"
                "movq  8(%1, %0), %%mm1 \n\t"
                "pmulhw %%mm5, %%mm0    \n\t"
                "pmulhw %%mm5, %%mm1    \n\t"
                "paddw %%mm6, %%mm0     \n\t"
                "paddw %%mm6, %%mm1     \n\t"
                "psraw $1, %%mm0        \n\t"
                "psraw $1, %%mm1        \n\t"
                "paddw (%2, %0), %%mm0  \n\t"
                "paddw 8(%2, %0), %%mm1 \n\t"
                "movq %%mm0, (%2, %0)   \n\t"
                "movq %%mm1, 8(%2, %0)  \n\t"
                "add $16, %0            \n\t"
                "cmp $128, %0           \n\t" //FIXME optimize & bench
                " jb 1b                 \n\t"

                : "+r" (i)
                : "r"(basis), "r"(rem), "g"(scale)
        );
    }else{
        for(i=0; i<8*8; i++){
            rem[i] += (basis[i]*scale + (1<<(BASIS_SHIFT - RECON_SHIFT-1)))>>(BASIS_SHIFT - RECON_SHIFT);
        }
    }
}
#endif /* CONFIG_ENCODERS */

#define PREFETCH(name, op) \
static void name(void *mem, int stride, int h){\
    const uint8_t *p= mem;\
    do{\
        asm volatile(#op" %0" :: "m"(*p));\
        p+= stride;\
    }while(--h);\
}
PREFETCH(prefetch_mmx2,  prefetcht0)
PREFETCH(prefetch_3dnow, prefetch)
#undef PREFETCH

#include "h264dsp_mmx.c"

/* AVS specific */
void ff_cavsdsp_init_mmx2(DSPContext* c, AVCodecContext *avctx);

void ff_put_cavs_qpel8_mc00_mmx2(uint8_t *dst, uint8_t *src, int stride) {
    put_pixels8_mmx(dst, src, stride, 8);
}
void ff_avg_cavs_qpel8_mc00_mmx2(uint8_t *dst, uint8_t *src, int stride) {
    avg_pixels8_mmx(dst, src, stride, 8);
}
void ff_put_cavs_qpel16_mc00_mmx2(uint8_t *dst, uint8_t *src, int stride) {
    put_pixels16_mmx(dst, src, stride, 16);
}
void ff_avg_cavs_qpel16_mc00_mmx2(uint8_t *dst, uint8_t *src, int stride) {
    avg_pixels16_mmx(dst, src, stride, 16);
}

/* external functions, from idct_mmx.c */
void ff_mmx_idct(DCTELEM *block);
void ff_mmxext_idct(DCTELEM *block);

void ff_vp3_idct_sse2(int16_t *input_data);
void ff_vp3_idct_mmx(int16_t *data);
void ff_vp3_dsp_init_mmx(void);

/* XXX: those functions should be suppressed ASAP when all IDCTs are
   converted */
#ifdef CONFIG_GPL
static void ff_libmpeg2mmx_idct_put(uint8_t *dest, int line_size, DCTELEM *block)
{
    ff_mmx_idct (block);
    put_pixels_clamped_mmx(block, dest, line_size);
}
static void ff_libmpeg2mmx_idct_add(uint8_t *dest, int line_size, DCTELEM *block)
{
    ff_mmx_idct (block);
    add_pixels_clamped_mmx(block, dest, line_size);
}
static void ff_libmpeg2mmx2_idct_put(uint8_t *dest, int line_size, DCTELEM *block)
{
    ff_mmxext_idct (block);
    put_pixels_clamped_mmx(block, dest, line_size);
}
static void ff_libmpeg2mmx2_idct_add(uint8_t *dest, int line_size, DCTELEM *block)
{
    ff_mmxext_idct (block);
    add_pixels_clamped_mmx(block, dest, line_size);
}
#endif
static void ff_vp3_idct_put_sse2(uint8_t *dest, int line_size, DCTELEM *block)
{
    ff_vp3_idct_sse2(block);
    put_signed_pixels_clamped_mmx(block, dest, line_size);
}
static void ff_vp3_idct_add_sse2(uint8_t *dest, int line_size, DCTELEM *block)
{
    ff_vp3_idct_sse2(block);
    add_pixels_clamped_mmx(block, dest, line_size);
}
static void ff_vp3_idct_put_mmx(uint8_t *dest, int line_size, DCTELEM *block)
{
    ff_vp3_idct_mmx(block);
    put_signed_pixels_clamped_mmx(block, dest, line_size);
}
static void ff_vp3_idct_add_mmx(uint8_t *dest, int line_size, DCTELEM *block)
{
    ff_vp3_idct_mmx(block);
    add_pixels_clamped_mmx(block, dest, line_size);
}
static void ff_idct_xvid_mmx_put(uint8_t *dest, int line_size, DCTELEM *block)
{
    ff_idct_xvid_mmx (block);
    put_pixels_clamped_mmx(block, dest, line_size);
}
static void ff_idct_xvid_mmx_add(uint8_t *dest, int line_size, DCTELEM *block)
{
    ff_idct_xvid_mmx (block);
    add_pixels_clamped_mmx(block, dest, line_size);
}
static void ff_idct_xvid_mmx2_put(uint8_t *dest, int line_size, DCTELEM *block)
{
    ff_idct_xvid_mmx2 (block);
    put_pixels_clamped_mmx(block, dest, line_size);
}
static void ff_idct_xvid_mmx2_add(uint8_t *dest, int line_size, DCTELEM *block)
{
    ff_idct_xvid_mmx2 (block);
    add_pixels_clamped_mmx(block, dest, line_size);
}

static void vorbis_inverse_coupling_3dnow(float *mag, float *ang, int blocksize)
{
    int i;
    asm volatile("pxor %%mm7, %%mm7":);
    for(i=0; i<blocksize; i+=2) {
        asm volatile(
            "movq    %0,    %%mm0 \n\t"
            "movq    %1,    %%mm1 \n\t"
            "movq    %%mm0, %%mm2 \n\t"
            "movq    %%mm1, %%mm3 \n\t"
            "pfcmpge %%mm7, %%mm2 \n\t" // m <= 0.0
            "pfcmpge %%mm7, %%mm3 \n\t" // a <= 0.0
            "pslld   $31,   %%mm2 \n\t" // keep only the sign bit
            "pxor    %%mm2, %%mm1 \n\t"
            "movq    %%mm3, %%mm4 \n\t"
            "pand    %%mm1, %%mm3 \n\t"
            "pandn   %%mm1, %%mm4 \n\t"
            "pfadd   %%mm0, %%mm3 \n\t" // a = m + ((a<0) & (a ^ sign(m)))
            "pfsub   %%mm4, %%mm0 \n\t" // m = m + ((a>0) & (a ^ sign(m)))
            "movq    %%mm3, %1    \n\t"
            "movq    %%mm0, %0    \n\t"
            :"+m"(mag[i]), "+m"(ang[i])
            ::"memory"
        );
    }
    asm volatile("femms");
}
static void vorbis_inverse_coupling_sse(float *mag, float *ang, int blocksize)
{
    int i;

    asm volatile(
            "movaps  %0,     %%xmm5 \n\t"
        ::"m"(ff_pdw_80000000[0])
    );
    for(i=0; i<blocksize; i+=4) {
        asm volatile(
            "movaps  %0,     %%xmm0 \n\t"
            "movaps  %1,     %%xmm1 \n\t"
            "xorps   %%xmm2, %%xmm2 \n\t"
            "xorps   %%xmm3, %%xmm3 \n\t"
            "cmpleps %%xmm0, %%xmm2 \n\t" // m <= 0.0
            "cmpleps %%xmm1, %%xmm3 \n\t" // a <= 0.0
            "andps   %%xmm5, %%xmm2 \n\t" // keep only the sign bit
            "xorps   %%xmm2, %%xmm1 \n\t"
            "movaps  %%xmm3, %%xmm4 \n\t"
            "andps   %%xmm1, %%xmm3 \n\t"
            "andnps  %%xmm1, %%xmm4 \n\t"
            "addps   %%xmm0, %%xmm3 \n\t" // a = m + ((a<0) & (a ^ sign(m)))
            "subps   %%xmm4, %%xmm0 \n\t" // m = m + ((a>0) & (a ^ sign(m)))
            "movaps  %%xmm3, %1     \n\t"
            "movaps  %%xmm0, %0     \n\t"
            :"+m"(mag[i]), "+m"(ang[i])
            ::"memory"
        );
    }
}

static void vector_fmul_3dnow(float *dst, const float *src, int len){
    long i = (len-4)*4;
    asm volatile(
        "1: \n\t"
        "movq    (%1,%0), %%mm0 \n\t"
        "movq   8(%1,%0), %%mm1 \n\t"
        "pfmul   (%2,%0), %%mm0 \n\t"
        "pfmul  8(%2,%0), %%mm1 \n\t"
        "movq   %%mm0,  (%1,%0) \n\t"
        "movq   %%mm1, 8(%1,%0) \n\t"
        "sub  $16, %0 \n\t"
        "jge 1b \n\t"
        "femms  \n\t"
        :"+r"(i)
        :"r"(dst), "r"(src)
        :"memory"
    );
}
static void vector_fmul_sse(float *dst, const float *src, int len){
    long i = (len-8)*4;
    asm volatile(
        "1: \n\t"
        "movaps    (%1,%0), %%xmm0 \n\t"
        "movaps  16(%1,%0), %%xmm1 \n\t"
        "mulps     (%2,%0), %%xmm0 \n\t"
        "mulps   16(%2,%0), %%xmm1 \n\t"
        "movaps  %%xmm0,   (%1,%0) \n\t"
        "movaps  %%xmm1, 16(%1,%0) \n\t"
        "sub  $32, %0 \n\t"
        "jge 1b \n\t"
        :"+r"(i)
        :"r"(dst), "r"(src)
        :"memory"
    );
}

static void vector_fmul_reverse_3dnow2(float *dst, const float *src0, const float *src1, int len){
    long i = len*4-16;
    asm volatile(
        "1: \n\t"
        "pswapd   8(%1), %%mm0 \n\t"
        "pswapd    (%1), %%mm1 \n\t"
        "pfmul  (%3,%0), %%mm0 \n\t"
        "pfmul 8(%3,%0), %%mm1 \n\t"
        "movq  %%mm0,  (%2,%0) \n\t"
        "movq  %%mm1, 8(%2,%0) \n\t"
        "add   $16, %1 \n\t"
        "sub   $16, %0 \n\t"
        "jge   1b \n\t"
        :"+r"(i), "+r"(src1)
        :"r"(dst), "r"(src0)
    );
    asm volatile("femms");
}
static void vector_fmul_reverse_sse(float *dst, const float *src0, const float *src1, int len){
    long i = len*4-32;
    asm volatile(
        "1: \n\t"
        "movaps        16(%1), %%xmm0 \n\t"
        "movaps          (%1), %%xmm1 \n\t"
        "shufps $0x1b, %%xmm0, %%xmm0 \n\t"
        "shufps $0x1b, %%xmm1, %%xmm1 \n\t"
        "mulps        (%3,%0), %%xmm0 \n\t"
        "mulps      16(%3,%0), %%xmm1 \n\t"
        "movaps     %%xmm0,   (%2,%0) \n\t"
        "movaps     %%xmm1, 16(%2,%0) \n\t"
        "add    $32, %1 \n\t"
        "sub    $32, %0 \n\t"
        "jge    1b \n\t"
        :"+r"(i), "+r"(src1)
        :"r"(dst), "r"(src0)
    );
}

static void vector_fmul_add_add_3dnow(float *dst, const float *src0, const float *src1,
                                      const float *src2, int src3, int len, int step){
    long i = (len-4)*4;
    if(step == 2 && src3 == 0){
        dst += (len-4)*2;
        asm volatile(
            "1: \n\t"
            "movq   (%2,%0),  %%mm0 \n\t"
            "movq  8(%2,%0),  %%mm1 \n\t"
            "pfmul  (%3,%0),  %%mm0 \n\t"
            "pfmul 8(%3,%0),  %%mm1 \n\t"
            "pfadd  (%4,%0),  %%mm0 \n\t"
            "pfadd 8(%4,%0),  %%mm1 \n\t"
            "movd     %%mm0,   (%1) \n\t"
            "movd     %%mm1, 16(%1) \n\t"
            "psrlq      $32,  %%mm0 \n\t"
            "psrlq      $32,  %%mm1 \n\t"
            "movd     %%mm0,  8(%1) \n\t"
            "movd     %%mm1, 24(%1) \n\t"
            "sub  $32, %1 \n\t"
            "sub  $16, %0 \n\t"
            "jge  1b \n\t"
            :"+r"(i), "+r"(dst)
            :"r"(src0), "r"(src1), "r"(src2)
            :"memory"
        );
    }
    else if(step == 1 && src3 == 0){
        asm volatile(
            "1: \n\t"
            "movq    (%2,%0), %%mm0 \n\t"
            "movq   8(%2,%0), %%mm1 \n\t"
            "pfmul   (%3,%0), %%mm0 \n\t"
            "pfmul  8(%3,%0), %%mm1 \n\t"
            "pfadd   (%4,%0), %%mm0 \n\t"
            "pfadd  8(%4,%0), %%mm1 \n\t"
            "movq  %%mm0,   (%1,%0) \n\t"
            "movq  %%mm1,  8(%1,%0) \n\t"
            "sub  $16, %0 \n\t"
            "jge  1b \n\t"
            :"+r"(i)
            :"r"(dst), "r"(src0), "r"(src1), "r"(src2)
            :"memory"
        );
    }
    else
        ff_vector_fmul_add_add_c(dst, src0, src1, src2, src3, len, step);
    asm volatile("femms");
}
static void vector_fmul_add_add_sse(float *dst, const float *src0, const float *src1,
                                    const float *src2, int src3, int len, int step){
    long i = (len-8)*4;
    if(step == 2 && src3 == 0){
        dst += (len-8)*2;
        asm volatile(
            "1: \n\t"
            "movaps   (%2,%0), %%xmm0 \n\t"
            "movaps 16(%2,%0), %%xmm1 \n\t"
            "mulps    (%3,%0), %%xmm0 \n\t"
            "mulps  16(%3,%0), %%xmm1 \n\t"
            "addps    (%4,%0), %%xmm0 \n\t"
            "addps  16(%4,%0), %%xmm1 \n\t"
            "movss     %%xmm0,   (%1) \n\t"
            "movss     %%xmm1, 32(%1) \n\t"
            "movhlps   %%xmm0, %%xmm2 \n\t"
            "movhlps   %%xmm1, %%xmm3 \n\t"
            "movss     %%xmm2, 16(%1) \n\t"
            "movss     %%xmm3, 48(%1) \n\t"
            "shufps $0xb1, %%xmm0, %%xmm0 \n\t"
            "shufps $0xb1, %%xmm1, %%xmm1 \n\t"
            "movss     %%xmm0,  8(%1) \n\t"
            "movss     %%xmm1, 40(%1) \n\t"
            "movhlps   %%xmm0, %%xmm2 \n\t"
            "movhlps   %%xmm1, %%xmm3 \n\t"
            "movss     %%xmm2, 24(%1) \n\t"
            "movss     %%xmm3, 56(%1) \n\t"
            "sub  $64, %1 \n\t"
            "sub  $32, %0 \n\t"
            "jge  1b \n\t"
            :"+r"(i), "+r"(dst)
            :"r"(src0), "r"(src1), "r"(src2)
            :"memory"
        );
    }
    else if(step == 1 && src3 == 0){
        asm volatile(
            "1: \n\t"
            "movaps   (%2,%0), %%xmm0 \n\t"
            "movaps 16(%2,%0), %%xmm1 \n\t"
            "mulps    (%3,%0), %%xmm0 \n\t"
            "mulps  16(%3,%0), %%xmm1 \n\t"
            "addps    (%4,%0), %%xmm0 \n\t"
            "addps  16(%4,%0), %%xmm1 \n\t"
            "movaps %%xmm0,   (%1,%0) \n\t"
            "movaps %%xmm1, 16(%1,%0) \n\t"
            "sub  $32, %0 \n\t"
            "jge  1b \n\t"
            :"+r"(i)
            :"r"(dst), "r"(src0), "r"(src1), "r"(src2)
            :"memory"
        );
    }
    else
        ff_vector_fmul_add_add_c(dst, src0, src1, src2, src3, len, step);
}

static void float_to_int16_3dnow(int16_t *dst, const float *src, int len){
    // not bit-exact: pf2id uses different rounding than C and SSE
    int i;
    for(i=0; i<len; i+=4) {
        asm volatile(
            "pf2id       %1, %%mm0 \n\t"
            "pf2id       %2, %%mm1 \n\t"
            "packssdw %%mm1, %%mm0 \n\t"
            "movq     %%mm0, %0    \n\t"
            :"=m"(dst[i])
            :"m"(src[i]), "m"(src[i+2])
        );
    }
    asm volatile("femms");
}
static void float_to_int16_sse(int16_t *dst, const float *src, int len){
    int i;
    for(i=0; i<len; i+=4) {
        asm volatile(
            "cvtps2pi    %1, %%mm0 \n\t"
            "cvtps2pi    %2, %%mm1 \n\t"
            "packssdw %%mm1, %%mm0 \n\t"
            "movq     %%mm0, %0    \n\t"
            :"=m"(dst[i])
            :"m"(src[i]), "m"(src[i+2])
        );
    }
    asm volatile("emms");
}

#ifdef CONFIG_SNOW_DECODER
extern void ff_snow_horizontal_compose97i_sse2(DWTELEM *b, int width);
extern void ff_snow_horizontal_compose97i_mmx(DWTELEM *b, int width);
extern void ff_snow_vertical_compose97i_sse2(DWTELEM *b0, DWTELEM *b1, DWTELEM *b2, DWTELEM *b3, DWTELEM *b4, DWTELEM *b5, int width);
extern void ff_snow_vertical_compose97i_mmx(DWTELEM *b0, DWTELEM *b1, DWTELEM *b2, DWTELEM *b3, DWTELEM *b4, DWTELEM *b5, int width);
extern void ff_snow_inner_add_yblock_sse2(const uint8_t *obmc, const int obmc_stride, uint8_t * * block, int b_w, int b_h,
                           int src_x, int src_y, int src_stride, slice_buffer * sb, int add, uint8_t * dst8);
extern void ff_snow_inner_add_yblock_mmx(const uint8_t *obmc, const int obmc_stride, uint8_t * * block, int b_w, int b_h,
                          int src_x, int src_y, int src_stride, slice_buffer * sb, int add, uint8_t * dst8);
#endif

void dsputil_init_mmx(DSPContext* c, AVCodecContext *avctx)
{
    mm_flags = mm_support();

    if (avctx->dsp_mask) {
        if (avctx->dsp_mask & FF_MM_FORCE)
            mm_flags |= (avctx->dsp_mask & 0xffff);
        else
            mm_flags &= ~(avctx->dsp_mask & 0xffff);
    }

#if 0
    av_log(avctx, AV_LOG_INFO, "libavcodec: CPU flags:");
    if (mm_flags & MM_MMX)
        av_log(avctx, AV_LOG_INFO, " mmx");
    if (mm_flags & MM_MMXEXT)
        av_log(avctx, AV_LOG_INFO, " mmxext");
    if (mm_flags & MM_3DNOW)
        av_log(avctx, AV_LOG_INFO, " 3dnow");
    if (mm_flags & MM_SSE)
        av_log(avctx, AV_LOG_INFO, " sse");
    if (mm_flags & MM_SSE2)
        av_log(avctx, AV_LOG_INFO, " sse2");
    av_log(avctx, AV_LOG_INFO, "\n");
#endif

    if (mm_flags & MM_MMX) {
        const int idct_algo= avctx->idct_algo;

#ifdef CONFIG_ENCODERS
        const int dct_algo = avctx->dct_algo;
        if(dct_algo==FF_DCT_AUTO || dct_algo==FF_DCT_MMX){
            if(mm_flags & MM_SSE2){
                c->fdct = ff_fdct_sse2;
            }else if(mm_flags & MM_MMXEXT){
                c->fdct = ff_fdct_mmx2;
            }else{
                c->fdct = ff_fdct_mmx;
            }
        }
#endif //CONFIG_ENCODERS
        if(avctx->lowres==0){
            if(idct_algo==FF_IDCT_AUTO || idct_algo==FF_IDCT_SIMPLEMMX){
                c->idct_put= ff_simple_idct_put_mmx;
                c->idct_add= ff_simple_idct_add_mmx;
                c->idct    = ff_simple_idct_mmx;
                c->idct_permutation_type= FF_SIMPLE_IDCT_PERM;
#ifdef CONFIG_GPL
            }else if(idct_algo==FF_IDCT_LIBMPEG2MMX){
                if(mm_flags & MM_MMXEXT){
                    c->idct_put= ff_libmpeg2mmx2_idct_put;
                    c->idct_add= ff_libmpeg2mmx2_idct_add;
                    c->idct    = ff_mmxext_idct;
                }else{
                    c->idct_put= ff_libmpeg2mmx_idct_put;
                    c->idct_add= ff_libmpeg2mmx_idct_add;
                    c->idct    = ff_mmx_idct;
                }
                c->idct_permutation_type= FF_LIBMPEG2_IDCT_PERM;
#endif
            }else if(idct_algo==FF_IDCT_VP3 &&
                     avctx->codec->id!=CODEC_ID_THEORA &&
                     !(avctx->flags & CODEC_FLAG_BITEXACT)){
                if(mm_flags & MM_SSE2){
                    c->idct_put= ff_vp3_idct_put_sse2;
                    c->idct_add= ff_vp3_idct_add_sse2;
                    c->idct    = ff_vp3_idct_sse2;
                    c->idct_permutation_type= FF_TRANSPOSE_IDCT_PERM;
                }else{
                    ff_vp3_dsp_init_mmx();
                    c->idct_put= ff_vp3_idct_put_mmx;
                    c->idct_add= ff_vp3_idct_add_mmx;
                    c->idct    = ff_vp3_idct_mmx;
                    c->idct_permutation_type= FF_PARTTRANS_IDCT_PERM;
                }
            }else if(idct_algo==FF_IDCT_CAVS){
                    c->idct_permutation_type= FF_TRANSPOSE_IDCT_PERM;
            }else if(idct_algo==FF_IDCT_XVIDMMX){
                if(mm_flags & MM_MMXEXT){
                    c->idct_put= ff_idct_xvid_mmx2_put;
                    c->idct_add= ff_idct_xvid_mmx2_add;
                    c->idct    = ff_idct_xvid_mmx2;
                }else{
                    c->idct_put= ff_idct_xvid_mmx_put;
                    c->idct_add= ff_idct_xvid_mmx_add;
                    c->idct    = ff_idct_xvid_mmx;
                }
            }
        }

#ifdef CONFIG_ENCODERS
        c->get_pixels = get_pixels_mmx;
        c->diff_pixels = diff_pixels_mmx;
#endif //CONFIG_ENCODERS
        c->put_pixels_clamped = put_pixels_clamped_mmx;
        c->put_signed_pixels_clamped = put_signed_pixels_clamped_mmx;
        c->add_pixels_clamped = add_pixels_clamped_mmx;
        c->clear_blocks = clear_blocks_mmx;
#ifdef CONFIG_ENCODERS
        c->pix_sum = pix_sum16_mmx;
#endif //CONFIG_ENCODERS

        c->put_pixels_tab[0][0] = put_pixels16_mmx;
        c->put_pixels_tab[0][1] = put_pixels16_x2_mmx;
        c->put_pixels_tab[0][2] = put_pixels16_y2_mmx;
        c->put_pixels_tab[0][3] = put_pixels16_xy2_mmx;

        c->put_no_rnd_pixels_tab[0][0] = put_pixels16_mmx;
        c->put_no_rnd_pixels_tab[0][1] = put_no_rnd_pixels16_x2_mmx;
        c->put_no_rnd_pixels_tab[0][2] = put_no_rnd_pixels16_y2_mmx;
        c->put_no_rnd_pixels_tab[0][3] = put_no_rnd_pixels16_xy2_mmx;

        c->avg_pixels_tab[0][0] = avg_pixels16_mmx;
        c->avg_pixels_tab[0][1] = avg_pixels16_x2_mmx;
        c->avg_pixels_tab[0][2] = avg_pixels16_y2_mmx;
        c->avg_pixels_tab[0][3] = avg_pixels16_xy2_mmx;

        c->avg_no_rnd_pixels_tab[0][0] = avg_no_rnd_pixels16_mmx;
        c->avg_no_rnd_pixels_tab[0][1] = avg_no_rnd_pixels16_x2_mmx;
        c->avg_no_rnd_pixels_tab[0][2] = avg_no_rnd_pixels16_y2_mmx;
        c->avg_no_rnd_pixels_tab[0][3] = avg_no_rnd_pixels16_xy2_mmx;

        c->put_pixels_tab[1][0] = put_pixels8_mmx;
        c->put_pixels_tab[1][1] = put_pixels8_x2_mmx;
        c->put_pixels_tab[1][2] = put_pixels8_y2_mmx;
        c->put_pixels_tab[1][3] = put_pixels8_xy2_mmx;

        c->put_no_rnd_pixels_tab[1][0] = put_pixels8_mmx;
        c->put_no_rnd_pixels_tab[1][1] = put_no_rnd_pixels8_x2_mmx;
        c->put_no_rnd_pixels_tab[1][2] = put_no_rnd_pixels8_y2_mmx;
        c->put_no_rnd_pixels_tab[1][3] = put_no_rnd_pixels8_xy2_mmx;

        c->avg_pixels_tab[1][0] = avg_pixels8_mmx;
        c->avg_pixels_tab[1][1] = avg_pixels8_x2_mmx;
        c->avg_pixels_tab[1][2] = avg_pixels8_y2_mmx;
        c->avg_pixels_tab[1][3] = avg_pixels8_xy2_mmx;

        c->avg_no_rnd_pixels_tab[1][0] = avg_no_rnd_pixels8_mmx;
        c->avg_no_rnd_pixels_tab[1][1] = avg_no_rnd_pixels8_x2_mmx;
        c->avg_no_rnd_pixels_tab[1][2] = avg_no_rnd_pixels8_y2_mmx;
        c->avg_no_rnd_pixels_tab[1][3] = avg_no_rnd_pixels8_xy2_mmx;

        c->gmc= gmc_mmx;

        c->add_bytes= add_bytes_mmx;
#ifdef CONFIG_ENCODERS
        c->diff_bytes= diff_bytes_mmx;

        c->hadamard8_diff[0]= hadamard8_diff16_mmx;
        c->hadamard8_diff[1]= hadamard8_diff_mmx;

        c->pix_norm1 = pix_norm1_mmx;
        c->sse[0] = (mm_flags & MM_SSE2) ? sse16_sse2 : sse16_mmx;
          c->sse[1] = sse8_mmx;
        c->vsad[4]= vsad_intra16_mmx;

        c->nsse[0] = nsse16_mmx;
        c->nsse[1] = nsse8_mmx;
        if(!(avctx->flags & CODEC_FLAG_BITEXACT)){
            c->vsad[0] = vsad16_mmx;
        }

        if(!(avctx->flags & CODEC_FLAG_BITEXACT)){
            c->try_8x8basis= try_8x8basis_mmx;
        }
        c->add_8x8basis= add_8x8basis_mmx;

        c->ssd_int8_vs_int16 = ssd_int8_vs_int16_mmx;

#endif //CONFIG_ENCODERS

        c->h263_v_loop_filter= h263_v_loop_filter_mmx;
        c->h263_h_loop_filter= h263_h_loop_filter_mmx;
        c->put_h264_chroma_pixels_tab[0]= put_h264_chroma_mc8_mmx;
        c->put_h264_chroma_pixels_tab[1]= put_h264_chroma_mc4_mmx;

        c->h264_idct_dc_add=
        c->h264_idct_add= ff_h264_idct_add_mmx;
        c->h264_idct8_dc_add=
        c->h264_idct8_add= ff_h264_idct8_add_mmx;

        if (mm_flags & MM_MMXEXT) {
            c->prefetch = prefetch_mmx2;

            c->put_pixels_tab[0][1] = put_pixels16_x2_mmx2;
            c->put_pixels_tab[0][2] = put_pixels16_y2_mmx2;

            c->avg_pixels_tab[0][0] = avg_pixels16_mmx2;
            c->avg_pixels_tab[0][1] = avg_pixels16_x2_mmx2;
            c->avg_pixels_tab[0][2] = avg_pixels16_y2_mmx2;

            c->put_pixels_tab[1][1] = put_pixels8_x2_mmx2;
            c->put_pixels_tab[1][2] = put_pixels8_y2_mmx2;

            c->avg_pixels_tab[1][0] = avg_pixels8_mmx2;
            c->avg_pixels_tab[1][1] = avg_pixels8_x2_mmx2;
            c->avg_pixels_tab[1][2] = avg_pixels8_y2_mmx2;

#ifdef CONFIG_ENCODERS
            c->hadamard8_diff[0]= hadamard8_diff16_mmx2;
            c->hadamard8_diff[1]= hadamard8_diff_mmx2;
            c->vsad[4]= vsad_intra16_mmx2;
#endif //CONFIG_ENCODERS

            c->h264_idct_dc_add= ff_h264_idct_dc_add_mmx2;
            c->h264_idct8_dc_add= ff_h264_idct8_dc_add_mmx2;

            if(!(avctx->flags & CODEC_FLAG_BITEXACT)){
                c->put_no_rnd_pixels_tab[0][1] = put_no_rnd_pixels16_x2_mmx2;
                c->put_no_rnd_pixels_tab[0][2] = put_no_rnd_pixels16_y2_mmx2;
                c->put_no_rnd_pixels_tab[1][1] = put_no_rnd_pixels8_x2_mmx2;
                c->put_no_rnd_pixels_tab[1][2] = put_no_rnd_pixels8_y2_mmx2;
                c->avg_pixels_tab[0][3] = avg_pixels16_xy2_mmx2;
                c->avg_pixels_tab[1][3] = avg_pixels8_xy2_mmx2;
#ifdef CONFIG_ENCODERS
                c->vsad[0] = vsad16_mmx2;
#endif //CONFIG_ENCODERS
            }

#if 1
            SET_QPEL_FUNC(qpel_pixels_tab[0][ 0], qpel16_mc00_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[0][ 1], qpel16_mc10_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[0][ 2], qpel16_mc20_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[0][ 3], qpel16_mc30_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[0][ 4], qpel16_mc01_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[0][ 5], qpel16_mc11_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[0][ 6], qpel16_mc21_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[0][ 7], qpel16_mc31_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[0][ 8], qpel16_mc02_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[0][ 9], qpel16_mc12_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[0][10], qpel16_mc22_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[0][11], qpel16_mc32_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[0][12], qpel16_mc03_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[0][13], qpel16_mc13_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[0][14], qpel16_mc23_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[0][15], qpel16_mc33_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[1][ 0], qpel8_mc00_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[1][ 1], qpel8_mc10_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[1][ 2], qpel8_mc20_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[1][ 3], qpel8_mc30_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[1][ 4], qpel8_mc01_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[1][ 5], qpel8_mc11_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[1][ 6], qpel8_mc21_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[1][ 7], qpel8_mc31_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[1][ 8], qpel8_mc02_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[1][ 9], qpel8_mc12_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[1][10], qpel8_mc22_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[1][11], qpel8_mc32_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[1][12], qpel8_mc03_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[1][13], qpel8_mc13_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[1][14], qpel8_mc23_mmx2)
            SET_QPEL_FUNC(qpel_pixels_tab[1][15], qpel8_mc33_mmx2)
#endif

//FIXME 3dnow too
#define dspfunc(PFX, IDX, NUM) \
    c->PFX ## _pixels_tab[IDX][ 0] = PFX ## NUM ## _mc00_mmx2; \
    c->PFX ## _pixels_tab[IDX][ 1] = PFX ## NUM ## _mc10_mmx2; \
    c->PFX ## _pixels_tab[IDX][ 2] = PFX ## NUM ## _mc20_mmx2; \
    c->PFX ## _pixels_tab[IDX][ 3] = PFX ## NUM ## _mc30_mmx2; \
    c->PFX ## _pixels_tab[IDX][ 4] = PFX ## NUM ## _mc01_mmx2; \
    c->PFX ## _pixels_tab[IDX][ 5] = PFX ## NUM ## _mc11_mmx2; \
    c->PFX ## _pixels_tab[IDX][ 6] = PFX ## NUM ## _mc21_mmx2; \
    c->PFX ## _pixels_tab[IDX][ 7] = PFX ## NUM ## _mc31_mmx2; \
    c->PFX ## _pixels_tab[IDX][ 8] = PFX ## NUM ## _mc02_mmx2; \
    c->PFX ## _pixels_tab[IDX][ 9] = PFX ## NUM ## _mc12_mmx2; \
    c->PFX ## _pixels_tab[IDX][10] = PFX ## NUM ## _mc22_mmx2; \
    c->PFX ## _pixels_tab[IDX][11] = PFX ## NUM ## _mc32_mmx2; \
    c->PFX ## _pixels_tab[IDX][12] = PFX ## NUM ## _mc03_mmx2; \
    c->PFX ## _pixels_tab[IDX][13] = PFX ## NUM ## _mc13_mmx2; \
    c->PFX ## _pixels_tab[IDX][14] = PFX ## NUM ## _mc23_mmx2; \
    c->PFX ## _pixels_tab[IDX][15] = PFX ## NUM ## _mc33_mmx2

            dspfunc(put_h264_qpel, 0, 16);
            dspfunc(put_h264_qpel, 1, 8);
            dspfunc(put_h264_qpel, 2, 4);
            dspfunc(avg_h264_qpel, 0, 16);
            dspfunc(avg_h264_qpel, 1, 8);
            dspfunc(avg_h264_qpel, 2, 4);

            dspfunc(put_2tap_qpel, 0, 16);
            dspfunc(put_2tap_qpel, 1, 8);
            dspfunc(avg_2tap_qpel, 0, 16);
            dspfunc(avg_2tap_qpel, 1, 8);
#undef dspfunc

            c->avg_h264_chroma_pixels_tab[0]= avg_h264_chroma_mc8_mmx2;
            c->avg_h264_chroma_pixels_tab[1]= avg_h264_chroma_mc4_mmx2;
            c->avg_h264_chroma_pixels_tab[2]= avg_h264_chroma_mc2_mmx2;
            c->put_h264_chroma_pixels_tab[2]= put_h264_chroma_mc2_mmx2;
            c->h264_v_loop_filter_luma= h264_v_loop_filter_luma_mmx2;
            c->h264_h_loop_filter_luma= h264_h_loop_filter_luma_mmx2;
            c->h264_v_loop_filter_chroma= h264_v_loop_filter_chroma_mmx2;
            c->h264_h_loop_filter_chroma= h264_h_loop_filter_chroma_mmx2;
            c->h264_v_loop_filter_chroma_intra= h264_v_loop_filter_chroma_intra_mmx2;
            c->h264_h_loop_filter_chroma_intra= h264_h_loop_filter_chroma_intra_mmx2;
            c->h264_loop_filter_strength= h264_loop_filter_strength_mmx2;

            c->weight_h264_pixels_tab[0]= ff_h264_weight_16x16_mmx2;
            c->weight_h264_pixels_tab[1]= ff_h264_weight_16x8_mmx2;
            c->weight_h264_pixels_tab[2]= ff_h264_weight_8x16_mmx2;
            c->weight_h264_pixels_tab[3]= ff_h264_weight_8x8_mmx2;
            c->weight_h264_pixels_tab[4]= ff_h264_weight_8x4_mmx2;
            c->weight_h264_pixels_tab[5]= ff_h264_weight_4x8_mmx2;
            c->weight_h264_pixels_tab[6]= ff_h264_weight_4x4_mmx2;
            c->weight_h264_pixels_tab[7]= ff_h264_weight_4x2_mmx2;

            c->biweight_h264_pixels_tab[0]= ff_h264_biweight_16x16_mmx2;
            c->biweight_h264_pixels_tab[1]= ff_h264_biweight_16x8_mmx2;
            c->biweight_h264_pixels_tab[2]= ff_h264_biweight_8x16_mmx2;
            c->biweight_h264_pixels_tab[3]= ff_h264_biweight_8x8_mmx2;
            c->biweight_h264_pixels_tab[4]= ff_h264_biweight_8x4_mmx2;
            c->biweight_h264_pixels_tab[5]= ff_h264_biweight_4x8_mmx2;
            c->biweight_h264_pixels_tab[6]= ff_h264_biweight_4x4_mmx2;
            c->biweight_h264_pixels_tab[7]= ff_h264_biweight_4x2_mmx2;

#ifdef CONFIG_CAVS_DECODER
            ff_cavsdsp_init_mmx2(c, avctx);
#endif

#ifdef CONFIG_ENCODERS
            c->sub_hfyu_median_prediction= sub_hfyu_median_prediction_mmx2;
#endif //CONFIG_ENCODERS
        } else if (mm_flags & MM_3DNOW) {
            c->prefetch = prefetch_3dnow;

            c->put_pixels_tab[0][1] = put_pixels16_x2_3dnow;
            c->put_pixels_tab[0][2] = put_pixels16_y2_3dnow;

            c->avg_pixels_tab[0][0] = avg_pixels16_3dnow;
            c->avg_pixels_tab[0][1] = avg_pixels16_x2_3dnow;
            c->avg_pixels_tab[0][2] = avg_pixels16_y2_3dnow;

            c->put_pixels_tab[1][1] = put_pixels8_x2_3dnow;
            c->put_pixels_tab[1][2] = put_pixels8_y2_3dnow;

            c->avg_pixels_tab[1][0] = avg_pixels8_3dnow;
            c->avg_pixels_tab[1][1] = avg_pixels8_x2_3dnow;
            c->avg_pixels_tab[1][2] = avg_pixels8_y2_3dnow;

            if(!(avctx->flags & CODEC_FLAG_BITEXACT)){
                c->put_no_rnd_pixels_tab[0][1] = put_no_rnd_pixels16_x2_3dnow;
                c->put_no_rnd_pixels_tab[0][2] = put_no_rnd_pixels16_y2_3dnow;
                c->put_no_rnd_pixels_tab[1][1] = put_no_rnd_pixels8_x2_3dnow;
                c->put_no_rnd_pixels_tab[1][2] = put_no_rnd_pixels8_y2_3dnow;
                c->avg_pixels_tab[0][3] = avg_pixels16_xy2_3dnow;
                c->avg_pixels_tab[1][3] = avg_pixels8_xy2_3dnow;
            }

            SET_QPEL_FUNC(qpel_pixels_tab[0][ 0], qpel16_mc00_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[0][ 1], qpel16_mc10_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[0][ 2], qpel16_mc20_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[0][ 3], qpel16_mc30_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[0][ 4], qpel16_mc01_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[0][ 5], qpel16_mc11_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[0][ 6], qpel16_mc21_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[0][ 7], qpel16_mc31_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[0][ 8], qpel16_mc02_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[0][ 9], qpel16_mc12_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[0][10], qpel16_mc22_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[0][11], qpel16_mc32_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[0][12], qpel16_mc03_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[0][13], qpel16_mc13_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[0][14], qpel16_mc23_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[0][15], qpel16_mc33_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[1][ 0], qpel8_mc00_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[1][ 1], qpel8_mc10_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[1][ 2], qpel8_mc20_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[1][ 3], qpel8_mc30_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[1][ 4], qpel8_mc01_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[1][ 5], qpel8_mc11_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[1][ 6], qpel8_mc21_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[1][ 7], qpel8_mc31_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[1][ 8], qpel8_mc02_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[1][ 9], qpel8_mc12_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[1][10], qpel8_mc22_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[1][11], qpel8_mc32_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[1][12], qpel8_mc03_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[1][13], qpel8_mc13_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[1][14], qpel8_mc23_3dnow)
            SET_QPEL_FUNC(qpel_pixels_tab[1][15], qpel8_mc33_3dnow)

#define dspfunc(PFX, IDX, NUM) \
    c->PFX ## _pixels_tab[IDX][ 0] = PFX ## NUM ## _mc00_3dnow; \
    c->PFX ## _pixels_tab[IDX][ 1] = PFX ## NUM ## _mc10_3dnow; \
    c->PFX ## _pixels_tab[IDX][ 2] = PFX ## NUM ## _mc20_3dnow; \
    c->PFX ## _pixels_tab[IDX][ 3] = PFX ## NUM ## _mc30_3dnow; \
    c->PFX ## _pixels_tab[IDX][ 4] = PFX ## NUM ## _mc01_3dnow; \
    c->PFX ## _pixels_tab[IDX][ 5] = PFX ## NUM ## _mc11_3dnow; \
    c->PFX ## _pixels_tab[IDX][ 6] = PFX ## NUM ## _mc21_3dnow; \
    c->PFX ## _pixels_tab[IDX][ 7] = PFX ## NUM ## _mc31_3dnow; \
    c->PFX ## _pixels_tab[IDX][ 8] = PFX ## NUM ## _mc02_3dnow; \
    c->PFX ## _pixels_tab[IDX][ 9] = PFX ## NUM ## _mc12_3dnow; \
    c->PFX ## _pixels_tab[IDX][10] = PFX ## NUM ## _mc22_3dnow; \
    c->PFX ## _pixels_tab[IDX][11] = PFX ## NUM ## _mc32_3dnow; \
    c->PFX ## _pixels_tab[IDX][12] = PFX ## NUM ## _mc03_3dnow; \
    c->PFX ## _pixels_tab[IDX][13] = PFX ## NUM ## _mc13_3dnow; \
    c->PFX ## _pixels_tab[IDX][14] = PFX ## NUM ## _mc23_3dnow; \
    c->PFX ## _pixels_tab[IDX][15] = PFX ## NUM ## _mc33_3dnow

            dspfunc(put_h264_qpel, 0, 16);
            dspfunc(put_h264_qpel, 1, 8);
            dspfunc(put_h264_qpel, 2, 4);
            dspfunc(avg_h264_qpel, 0, 16);
            dspfunc(avg_h264_qpel, 1, 8);
            dspfunc(avg_h264_qpel, 2, 4);

            dspfunc(put_2tap_qpel, 0, 16);
            dspfunc(put_2tap_qpel, 1, 8);
            dspfunc(avg_2tap_qpel, 0, 16);
            dspfunc(avg_2tap_qpel, 1, 8);

            c->avg_h264_chroma_pixels_tab[0]= avg_h264_chroma_mc8_3dnow;
            c->avg_h264_chroma_pixels_tab[1]= avg_h264_chroma_mc4_3dnow;
        }

#ifdef CONFIG_SNOW_DECODER
        if(mm_flags & MM_SSE2){
            c->horizontal_compose97i = ff_snow_horizontal_compose97i_sse2;
            c->vertical_compose97i = ff_snow_vertical_compose97i_sse2;
            c->inner_add_yblock = ff_snow_inner_add_yblock_sse2;
        }
        else{
            c->horizontal_compose97i = ff_snow_horizontal_compose97i_mmx;
            c->vertical_compose97i = ff_snow_vertical_compose97i_mmx;
            c->inner_add_yblock = ff_snow_inner_add_yblock_mmx;
        }
#endif

        if(mm_flags & MM_3DNOW){
            c->vorbis_inverse_coupling = vorbis_inverse_coupling_3dnow;
            c->vector_fmul = vector_fmul_3dnow;
            if(!(avctx->flags & CODEC_FLAG_BITEXACT))
                c->float_to_int16 = float_to_int16_3dnow;
        }
        if(mm_flags & MM_3DNOWEXT)
            c->vector_fmul_reverse = vector_fmul_reverse_3dnow2;
        if(mm_flags & MM_SSE){
            c->vorbis_inverse_coupling = vorbis_inverse_coupling_sse;
            c->vector_fmul = vector_fmul_sse;
            c->float_to_int16 = float_to_int16_sse;
            c->vector_fmul_reverse = vector_fmul_reverse_sse;
            c->vector_fmul_add_add = vector_fmul_add_add_sse;
        }
        if(mm_flags & MM_3DNOW)
            c->vector_fmul_add_add = vector_fmul_add_add_3dnow; // faster than sse
    }

#ifdef CONFIG_ENCODERS
    dsputil_init_pix_mmx(c, avctx);
#endif //CONFIG_ENCODERS
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
