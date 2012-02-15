/*
 * Copyright (C) 2003 David S. Miller <davem@redhat.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/* The *no_round* functions have been added by James A. Morrison, 2003,2004.
   The vis code from libmpeg2 was adapted for libavcodec by James A. Morrison.
 */

#include "config.h"

#include <inttypes.h>

#include "libavcodec/dsputil.h"
#include "dsputil_vis.h"

#include "vis.h"

/* The trick used in some of this file is the formula from the MMX
 * motion comp code, which is:
 *
 * (x+y+1)>>1 == (x|y)-((x^y)>>1)
 *
 * This allows us to average 8 bytes at a time in a 64-bit FPU reg.
 * We avoid overflows by masking before we do the shift, and we
 * implement the shift by multiplying by 1/2 using mul8x16.  So in
 * VIS this is (assume 'x' is in f0, 'y' is in f2, a repeating mask
 * of '0xfe' is in f4, a repeating mask of '0x7f' is in f6, and
 * the value 0x80808080 is in f8):
 *
 *      fxor            f0,   f2, f10
 *      fand            f10,  f4, f10
 *      fmul8x16        f8,  f10, f10
 *      fand            f10,  f6, f10
 *      for             f0,   f2, f12
 *      fpsub16         f12, f10, f10
 */

#define DUP4(x) {x, x, x, x}
#define DUP8(x) {x, x, x, x, x, x, x, x}
DECLARE_ALIGNED(8, static const int16_t, constants1)[] = DUP4 (1);
DECLARE_ALIGNED(8, static const int16_t, constants2)[] = DUP4 (2);
DECLARE_ALIGNED(8, static const int16_t, constants3)[] = DUP4 (3);
DECLARE_ALIGNED(8, static const int16_t, constants6)[] = DUP4 (6);
DECLARE_ALIGNED(8, static const int8_t, constants_fe)[] = DUP8 (0xfe);
DECLARE_ALIGNED(8, static const int8_t, constants_7f)[] = DUP8 (0x7f);
DECLARE_ALIGNED(8, static const int8_t, constants128)[] = DUP8 (128);
DECLARE_ALIGNED(8, static const int16_t, constants256_512)[] =
        {256, 512, 256, 512};
DECLARE_ALIGNED(8, static const int16_t, constants256_1024)[] =
        {256, 1024, 256, 1024};

#define REF_0           0
#define REF_0_1         1
#define REF_2           2
#define REF_2_1         3
#define REF_4           4
#define REF_4_1         5
#define REF_6           6
#define REF_6_1         7
#define REF_S0          8
#define REF_S0_1        9
#define REF_S2          10
#define REF_S2_1        11
#define REF_S4          12
#define REF_S4_1        13
#define REF_S6          14
#define REF_S6_1        15
#define DST_0           16
#define DST_1           17
#define DST_2           18
#define DST_3           19
#define CONST_1         20
#define CONST_2         20
#define CONST_3         20
#define CONST_6         20
#define MASK_fe         20
#define CONST_128       22
#define CONST_256       22
#define CONST_512       22
#define CONST_1024      22
#define TMP0            24
#define TMP1            25
#define TMP2            26
#define TMP3            27
#define TMP4            28
#define TMP5            29
#define ZERO            30
#define MASK_7f         30

#define TMP6            32
#define TMP8            34
#define TMP10           36
#define TMP12           38
#define TMP14           40
#define TMP16           42
#define TMP18           44
#define TMP20           46
#define TMP22           48
#define TMP24           50
#define TMP26           52
#define TMP28           54
#define TMP30           56
#define TMP32           58

static void MC_put_o_16_vis (uint8_t * dest, const uint8_t * ref,
                             const int stride, int height)
{
        ref = vis_alignaddr(ref);
        do {    /* 5 cycles */
                vis_ld64(ref[0], TMP0);

                vis_ld64_2(ref, 8, TMP2);

                vis_ld64_2(ref, 16, TMP4);
                ref += stride;

                vis_faligndata(TMP0, TMP2, REF_0);
                vis_st64(REF_0, dest[0]);

                vis_faligndata(TMP2, TMP4, REF_2);
                vis_st64_2(REF_2, dest, 8);
                dest += stride;
        } while (--height);
}

static void MC_put_o_8_vis (uint8_t * dest, const uint8_t * ref,
                            const int stride, int height)
{
        ref = vis_alignaddr(ref);
        do {    /* 4 cycles */
                vis_ld64(ref[0], TMP0);

                vis_ld64(ref[8], TMP2);
                ref += stride;

                /* stall */

                vis_faligndata(TMP0, TMP2, REF_0);
                vis_st64(REF_0, dest[0]);
                dest += stride;
        } while (--height);
}


static void MC_avg_o_16_vis (uint8_t * dest, const uint8_t * ref,
                             const int stride, int height)
{
        int stride_8 = stride + 8;

        ref = vis_alignaddr(ref);

        vis_ld64(ref[0], TMP0);

        vis_ld64(ref[8], TMP2);

        vis_ld64(ref[16], TMP4);

        vis_ld64(dest[0], DST_0);

        vis_ld64(dest[8], DST_2);

        vis_ld64(constants_fe[0], MASK_fe);
        vis_faligndata(TMP0, TMP2, REF_0);

        vis_ld64(constants_7f[0], MASK_7f);
        vis_faligndata(TMP2, TMP4, REF_2);

        vis_ld64(constants128[0], CONST_128);

        ref += stride;
        height = (height >> 1) - 1;

        do {    /* 24 cycles */
                vis_ld64(ref[0], TMP0);
                vis_xor(DST_0, REF_0, TMP6);

                vis_ld64_2(ref, 8, TMP2);
                vis_and(TMP6, MASK_fe, TMP6);

                vis_ld64_2(ref, 16, TMP4);
                ref += stride;
                vis_mul8x16(CONST_128, TMP6, TMP6);
                vis_xor(DST_2, REF_2, TMP8);

                vis_and(TMP8, MASK_fe, TMP8);

                vis_or(DST_0, REF_0, TMP10);
                vis_ld64_2(dest, stride, DST_0);
                vis_mul8x16(CONST_128, TMP8, TMP8);

                vis_or(DST_2, REF_2, TMP12);
                vis_ld64_2(dest, stride_8, DST_2);

                vis_ld64(ref[0], TMP14);
                vis_and(TMP6, MASK_7f, TMP6);

                vis_and(TMP8, MASK_7f, TMP8);

                vis_psub16(TMP10, TMP6, TMP6);
                vis_st64(TMP6, dest[0]);

                vis_psub16(TMP12, TMP8, TMP8);
                vis_st64_2(TMP8, dest, 8);

                dest += stride;
                vis_ld64_2(ref, 8, TMP16);
                vis_faligndata(TMP0, TMP2, REF_0);

                vis_ld64_2(ref, 16, TMP18);
                vis_faligndata(TMP2, TMP4, REF_2);
                ref += stride;

                vis_xor(DST_0, REF_0, TMP20);

                vis_and(TMP20, MASK_fe, TMP20);

                vis_xor(DST_2, REF_2, TMP22);
                vis_mul8x16(CONST_128, TMP20, TMP20);

                vis_and(TMP22, MASK_fe, TMP22);

                vis_or(DST_0, REF_0, TMP24);
                vis_mul8x16(CONST_128, TMP22, TMP22);

                vis_or(DST_2, REF_2, TMP26);

                vis_ld64_2(dest, stride, DST_0);
                vis_faligndata(TMP14, TMP16, REF_0);

                vis_ld64_2(dest, stride_8, DST_2);
                vis_faligndata(TMP16, TMP18, REF_2);

                vis_and(TMP20, MASK_7f, TMP20);

                vis_and(TMP22, MASK_7f, TMP22);

                vis_psub16(TMP24, TMP20, TMP20);
                vis_st64(TMP20, dest[0]);

                vis_psub16(TMP26, TMP22, TMP22);
                vis_st64_2(TMP22, dest, 8);
                dest += stride;
        } while (--height);

        vis_ld64(ref[0], TMP0);
        vis_xor(DST_0, REF_0, TMP6);

        vis_ld64_2(ref, 8, TMP2);
        vis_and(TMP6, MASK_fe, TMP6);

        vis_ld64_2(ref, 16, TMP4);
        vis_mul8x16(CONST_128, TMP6, TMP6);
        vis_xor(DST_2, REF_2, TMP8);

        vis_and(TMP8, MASK_fe, TMP8);

        vis_or(DST_0, REF_0, TMP10);
        vis_ld64_2(dest, stride, DST_0);
        vis_mul8x16(CONST_128, TMP8, TMP8);

        vis_or(DST_2, REF_2, TMP12);
        vis_ld64_2(dest, stride_8, DST_2);

        vis_ld64(ref[0], TMP14);
        vis_and(TMP6, MASK_7f, TMP6);

        vis_and(TMP8, MASK_7f, TMP8);

        vis_psub16(TMP10, TMP6, TMP6);
        vis_st64(TMP6, dest[0]);

        vis_psub16(TMP12, TMP8, TMP8);
        vis_st64_2(TMP8, dest, 8);

        dest += stride;
        vis_faligndata(TMP0, TMP2, REF_0);

        vis_faligndata(TMP2, TMP4, REF_2);

        vis_xor(DST_0, REF_0, TMP20);

        vis_and(TMP20, MASK_fe, TMP20);

        vis_xor(DST_2, REF_2, TMP22);
        vis_mul8x16(CONST_128, TMP20, TMP20);

        vis_and(TMP22, MASK_fe, TMP22);

        vis_or(DST_0, REF_0, TMP24);
        vis_mul8x16(CONST_128, TMP22, TMP22);

        vis_or(DST_2, REF_2, TMP26);

        vis_and(TMP20, MASK_7f, TMP20);

        vis_and(TMP22, MASK_7f, TMP22);

        vis_psub16(TMP24, TMP20, TMP20);
        vis_st64(TMP20, dest[0]);

        vis_psub16(TMP26, TMP22, TMP22);
        vis_st64_2(TMP22, dest, 8);
}

static void MC_avg_o_8_vis (uint8_t * dest, const uint8_t * ref,
                            const int stride, int height)
{
        ref = vis_alignaddr(ref);

        vis_ld64(ref[0], TMP0);

        vis_ld64(ref[8], TMP2);

        vis_ld64(dest[0], DST_0);

        vis_ld64(constants_fe[0], MASK_fe);

        vis_ld64(constants_7f[0], MASK_7f);
        vis_faligndata(TMP0, TMP2, REF_0);

        vis_ld64(constants128[0], CONST_128);

        ref += stride;
        height = (height >> 1) - 1;

        do {    /* 12 cycles */
                vis_ld64(ref[0], TMP0);
                vis_xor(DST_0, REF_0, TMP4);

                vis_ld64(ref[8], TMP2);
                vis_and(TMP4, MASK_fe, TMP4);

                vis_or(DST_0, REF_0, TMP6);
                vis_ld64_2(dest, stride, DST_0);
                ref += stride;
                vis_mul8x16(CONST_128, TMP4, TMP4);

                vis_ld64(ref[0], TMP12);
                vis_faligndata(TMP0, TMP2, REF_0);

                vis_ld64(ref[8], TMP2);
                vis_xor(DST_0, REF_0, TMP0);
                ref += stride;

                vis_and(TMP0, MASK_fe, TMP0);

                vis_and(TMP4, MASK_7f, TMP4);

                vis_psub16(TMP6, TMP4, TMP4);
                vis_st64(TMP4, dest[0]);
                dest += stride;
                vis_mul8x16(CONST_128, TMP0, TMP0);

                vis_or(DST_0, REF_0, TMP6);
                vis_ld64_2(dest, stride, DST_0);

                vis_faligndata(TMP12, TMP2, REF_0);

                vis_and(TMP0, MASK_7f, TMP0);

                vis_psub16(TMP6, TMP0, TMP4);
                vis_st64(TMP4, dest[0]);
                dest += stride;
        } while (--height);

        vis_ld64(ref[0], TMP0);
        vis_xor(DST_0, REF_0, TMP4);

        vis_ld64(ref[8], TMP2);
        vis_and(TMP4, MASK_fe, TMP4);

        vis_or(DST_0, REF_0, TMP6);
        vis_ld64_2(dest, stride, DST_0);
        vis_mul8x16(CONST_128, TMP4, TMP4);

        vis_faligndata(TMP0, TMP2, REF_0);

        vis_xor(DST_0, REF_0, TMP0);

        vis_and(TMP0, MASK_fe, TMP0);

        vis_and(TMP4, MASK_7f, TMP4);

        vis_psub16(TMP6, TMP4, TMP4);
        vis_st64(TMP4, dest[0]);
        dest += stride;
        vis_mul8x16(CONST_128, TMP0, TMP0);

        vis_or(DST_0, REF_0, TMP6);

        vis_and(TMP0, MASK_7f, TMP0);

        vis_psub16(TMP6, TMP0, TMP4);
        vis_st64(TMP4, dest[0]);
}

static void MC_put_x_16_vis (uint8_t * dest, const uint8_t * ref,
                             const int stride, int height)
{
        unsigned long off = (unsigned long) ref & 0x7;
        unsigned long off_plus_1 = off + 1;

        ref = vis_alignaddr(ref);

        vis_ld64(ref[0],    TMP0);

        vis_ld64_2(ref, 8,  TMP2);

        vis_ld64_2(ref, 16, TMP4);

        vis_ld64(constants_fe[0], MASK_fe);

        vis_ld64(constants_7f[0], MASK_7f);
        vis_faligndata(TMP0, TMP2, REF_0);

        vis_ld64(constants128[0], CONST_128);
        vis_faligndata(TMP2, TMP4, REF_4);

        if (off != 0x7) {
                vis_alignaddr_g0((void *)off_plus_1);
                vis_faligndata(TMP0, TMP2, REF_2);
                vis_faligndata(TMP2, TMP4, REF_6);
        } else {
                vis_src1(TMP2, REF_2);
                vis_src1(TMP4, REF_6);
        }

        ref += stride;
        height = (height >> 1) - 1;

        do {    /* 34 cycles */
                vis_ld64(ref[0],    TMP0);
                vis_xor(REF_0, REF_2, TMP6);

                vis_ld64_2(ref, 8,  TMP2);
                vis_xor(REF_4, REF_6, TMP8);

                vis_ld64_2(ref, 16, TMP4);
                vis_and(TMP6, MASK_fe, TMP6);
                ref += stride;

                vis_ld64(ref[0],    TMP14);
                vis_mul8x16(CONST_128, TMP6, TMP6);
                vis_and(TMP8, MASK_fe, TMP8);

                vis_ld64_2(ref, 8,  TMP16);
                vis_mul8x16(CONST_128, TMP8, TMP8);
                vis_or(REF_0, REF_2, TMP10);

                vis_ld64_2(ref, 16, TMP18);
                ref += stride;
                vis_or(REF_4, REF_6, TMP12);

                vis_alignaddr_g0((void *)off);

                vis_faligndata(TMP0, TMP2, REF_0);

                vis_faligndata(TMP2, TMP4, REF_4);

                if (off != 0x7) {
                        vis_alignaddr_g0((void *)off_plus_1);
                        vis_faligndata(TMP0, TMP2, REF_2);
                        vis_faligndata(TMP2, TMP4, REF_6);
                } else {
                        vis_src1(TMP2, REF_2);
                        vis_src1(TMP4, REF_6);
                }

                vis_and(TMP6, MASK_7f, TMP6);

                vis_and(TMP8, MASK_7f, TMP8);

                vis_psub16(TMP10, TMP6, TMP6);
                vis_st64(TMP6, dest[0]);

                vis_psub16(TMP12, TMP8, TMP8);
                vis_st64_2(TMP8, dest, 8);
                dest += stride;

                vis_xor(REF_0, REF_2, TMP6);

                vis_xor(REF_4, REF_6, TMP8);

                vis_and(TMP6, MASK_fe, TMP6);

                vis_mul8x16(CONST_128, TMP6, TMP6);
                vis_and(TMP8, MASK_fe, TMP8);

                vis_mul8x16(CONST_128, TMP8, TMP8);
                vis_or(REF_0, REF_2, TMP10);

                vis_or(REF_4, REF_6, TMP12);

                vis_alignaddr_g0((void *)off);

                vis_faligndata(TMP14, TMP16, REF_0);

                vis_faligndata(TMP16, TMP18, REF_4);

                if (off != 0x7) {
                        vis_alignaddr_g0((void *)off_plus_1);
                        vis_faligndata(TMP14, TMP16, REF_2);
                        vis_faligndata(TMP16, TMP18, REF_6);
                } else {
                        vis_src1(TMP16, REF_2);
                        vis_src1(TMP18, REF_6);
                }

                vis_and(TMP6, MASK_7f, TMP6);

                vis_and(TMP8, MASK_7f, TMP8);

                vis_psub16(TMP10, TMP6, TMP6);
                vis_st64(TMP6, dest[0]);

                vis_psub16(TMP12, TMP8, TMP8);
                vis_st64_2(TMP8, dest, 8);
                dest += stride;
        } while (--height);

        vis_ld64(ref[0],    TMP0);
        vis_xor(REF_0, REF_2, TMP6);

        vis_ld64_2(ref, 8,  TMP2);
        vis_xor(REF_4, REF_6, TMP8);

        vis_ld64_2(ref, 16, TMP4);
        vis_and(TMP6, MASK_fe, TMP6);

        vis_mul8x16(CONST_128, TMP6, TMP6);
        vis_and(TMP8, MASK_fe, TMP8);

        vis_mul8x16(CONST_128, TMP8, TMP8);
        vis_or(REF_0, REF_2, TMP10);

        vis_or(REF_4, REF_6, TMP12);

        vis_alignaddr_g0((void *)off);

        vis_faligndata(TMP0, TMP2, REF_0);

        vis_faligndata(TMP2, TMP4, REF_4);

        if (off != 0x7) {
                vis_alignaddr_g0((void *)off_plus_1);
                vis_faligndata(TMP0, TMP2, REF_2);
                vis_faligndata(TMP2, TMP4, REF_6);
        } else {
                vis_src1(TMP2, REF_2);
                vis_src1(TMP4, REF_6);
        }

        vis_and(TMP6, MASK_7f, TMP6);

        vis_and(TMP8, MASK_7f, TMP8);

        vis_psub16(TMP10, TMP6, TMP6);
        vis_st64(TMP6, dest[0]);

        vis_psub16(TMP12, TMP8, TMP8);
        vis_st64_2(TMP8, dest, 8);
        dest += stride;

        vis_xor(REF_0, REF_2, TMP6);

        vis_xor(REF_4, REF_6, TMP8);

        vis_and(TMP6, MASK_fe, TMP6);

        vis_mul8x16(CONST_128, TMP6, TMP6);
        vis_and(TMP8, MASK_fe, TMP8);

        vis_mul8x16(CONST_128, TMP8, TMP8);
        vis_or(REF_0, REF_2, TMP10);

        vis_or(REF_4, REF_6, TMP12);

        vis_and(TMP6, MASK_7f, TMP6);

        vis_and(TMP8, MASK_7f, TMP8);

        vis_psub16(TMP10, TMP6, TMP6);
        vis_st64(TMP6, dest[0]);

        vis_psub16(TMP12, TMP8, TMP8);
        vis_st64_2(TMP8, dest, 8);
}

static void MC_put_x_8_vis (uint8_t * dest, const uint8_t * ref,
                            const int stride, int height)
{
        unsigned long off = (unsigned long) ref & 0x7;
        unsigned long off_plus_1 = off + 1;

        ref = vis_alignaddr(ref);

        vis_ld64(ref[0], TMP0);

        vis_ld64(ref[8], TMP2);

        vis_ld64(constants_fe[0], MASK_fe);

        vis_ld64(constants_7f[0], MASK_7f);

        vis_ld64(constants128[0], CONST_128);
        vis_faligndata(TMP0, TMP2, REF_0);

        if (off != 0x7) {
                vis_alignaddr_g0((void *)off_plus_1);
                vis_faligndata(TMP0, TMP2, REF_2);
        } else {
                vis_src1(TMP2, REF_2);
        }

        ref += stride;
        height = (height >> 1) - 1;

        do {    /* 20 cycles */
                vis_ld64(ref[0], TMP0);
                vis_xor(REF_0, REF_2, TMP4);

                vis_ld64_2(ref, 8, TMP2);
                vis_and(TMP4, MASK_fe, TMP4);
                ref += stride;

                vis_ld64(ref[0], TMP8);
                vis_or(REF_0, REF_2, TMP6);
                vis_mul8x16(CONST_128, TMP4, TMP4);

                vis_alignaddr_g0((void *)off);

                vis_ld64_2(ref, 8, TMP10);
                ref += stride;
                vis_faligndata(TMP0, TMP2, REF_0);

                if (off != 0x7) {
                        vis_alignaddr_g0((void *)off_plus_1);
                        vis_faligndata(TMP0, TMP2, REF_2);
                } else {
                        vis_src1(TMP2, REF_2);
                }

                vis_and(TMP4, MASK_7f, TMP4);

                vis_psub16(TMP6, TMP4, DST_0);
                vis_st64(DST_0, dest[0]);
                dest += stride;

                vis_xor(REF_0, REF_2, TMP12);

                vis_and(TMP12, MASK_fe, TMP12);

                vis_or(REF_0, REF_2, TMP14);
                vis_mul8x16(CONST_128, TMP12, TMP12);

                vis_alignaddr_g0((void *)off);
                vis_faligndata(TMP8, TMP10, REF_0);
                if (off != 0x7) {
                        vis_alignaddr_g0((void *)off_plus_1);
                        vis_faligndata(TMP8, TMP10, REF_2);
                } else {
                        vis_src1(TMP10, REF_2);
                }

                vis_and(TMP12, MASK_7f, TMP12);

                vis_psub16(TMP14, TMP12, DST_0);
                vis_st64(DST_0, dest[0]);
                dest += stride;
        } while (--height);

        vis_ld64(ref[0], TMP0);
        vis_xor(REF_0, REF_2, TMP4);

        vis_ld64_2(ref, 8, TMP2);
        vis_and(TMP4, MASK_fe, TMP4);

        vis_or(REF_0, REF_2, TMP6);
        vis_mul8x16(CONST_128, TMP4, TMP4);

        vis_alignaddr_g0((void *)off);

        vis_faligndata(TMP0, TMP2, REF_0);

        if (off != 0x7) {
                vis_alignaddr_g0((void *)off_plus_1);
                vis_faligndata(TMP0, TMP2, REF_2);
        } else {
                vis_src1(TMP2, REF_2);
        }

        vis_and(TMP4, MASK_7f, TMP4);

        vis_psub16(TMP6, TMP4, DST_0);
        vis_st64(DST_0, dest[0]);
        dest += stride;

        vis_xor(REF_0, REF_2, TMP12);

        vis_and(TMP12, MASK_fe, TMP12);

        vis_or(REF_0, REF_2, TMP14);
        vis_mul8x16(CONST_128, TMP12, TMP12);

        vis_and(TMP12, MASK_7f, TMP12);

        vis_psub16(TMP14, TMP12, DST_0);
        vis_st64(DST_0, dest[0]);
        dest += stride;
}

static void MC_avg_x_16_vis (uint8_t * dest, const uint8_t * ref,
                             const int stride, int height)
{
        unsigned long off = (unsigned long) ref & 0x7;
        unsigned long off_plus_1 = off + 1;

        vis_set_gsr(5 << VIS_GSR_SCALEFACT_SHIFT);

        vis_ld64(constants3[0], CONST_3);
        vis_fzero(ZERO);
        vis_ld64(constants256_512[0], CONST_256);

        ref = vis_alignaddr(ref);
        do {    /* 26 cycles */
                vis_ld64(ref[0], TMP0);

                vis_ld64(ref[8], TMP2);

                vis_alignaddr_g0((void *)off);

                vis_ld64(ref[16], TMP4);

                vis_ld64(dest[0], DST_0);
                vis_faligndata(TMP0, TMP2, REF_0);

                vis_ld64(dest[8], DST_2);
                vis_faligndata(TMP2, TMP4, REF_4);

                if (off != 0x7) {
                        vis_alignaddr_g0((void *)off_plus_1);
                        vis_faligndata(TMP0, TMP2, REF_2);
                        vis_faligndata(TMP2, TMP4, REF_6);
                } else {
                        vis_src1(TMP2, REF_2);
                        vis_src1(TMP4, REF_6);
                }

                vis_mul8x16au(REF_0,   CONST_256, TMP0);

                vis_pmerge(ZERO,     REF_2,     TMP4);
                vis_mul8x16au(REF_0_1, CONST_256, TMP2);

                vis_pmerge(ZERO, REF_2_1, TMP6);

                vis_padd16(TMP0, TMP4, TMP0);

                vis_mul8x16al(DST_0,   CONST_512, TMP4);
                vis_padd16(TMP2, TMP6, TMP2);

                vis_mul8x16al(DST_1,   CONST_512, TMP6);

                vis_mul8x16au(REF_6,   CONST_256, TMP12);

                vis_padd16(TMP0, TMP4, TMP0);
                vis_mul8x16au(REF_6_1, CONST_256, TMP14);

                vis_padd16(TMP2, TMP6, TMP2);
                vis_mul8x16au(REF_4,   CONST_256, TMP16);

                vis_padd16(TMP0, CONST_3, TMP8);
                vis_mul8x16au(REF_4_1, CONST_256, TMP18);

                vis_padd16(TMP2, CONST_3, TMP10);
                vis_pack16(TMP8, DST_0);

                vis_pack16(TMP10, DST_1);
                vis_padd16(TMP16, TMP12, TMP0);

                vis_st64(DST_0, dest[0]);
                vis_mul8x16al(DST_2,   CONST_512, TMP4);
                vis_padd16(TMP18, TMP14, TMP2);

                vis_mul8x16al(DST_3,   CONST_512, TMP6);
                vis_padd16(TMP0, CONST_3, TMP0);

                vis_padd16(TMP2, CONST_3, TMP2);

                vis_padd16(TMP0, TMP4, TMP0);

                vis_padd16(TMP2, TMP6, TMP2);
                vis_pack16(TMP0, DST_2);

                vis_pack16(TMP2, DST_3);
                vis_st64(DST_2, dest[8]);

                ref += stride;
                dest += stride;
        } while (--height);
}

static void MC_avg_x_8_vis (uint8_t * dest, const uint8_t * ref,
                            const int stride, int height)
{
        unsigned long off = (unsigned long) ref & 0x7;
        unsigned long off_plus_1 = off + 1;
        int stride_times_2 = stride << 1;

        vis_set_gsr(5 << VIS_GSR_SCALEFACT_SHIFT);

        vis_ld64(constants3[0], CONST_3);
        vis_fzero(ZERO);
        vis_ld64(constants256_512[0], CONST_256);

        ref = vis_alignaddr(ref);
        height >>= 2;
        do {    /* 47 cycles */
                vis_ld64(ref[0],   TMP0);

                vis_ld64_2(ref, 8, TMP2);
                ref += stride;

                vis_alignaddr_g0((void *)off);

                vis_ld64(ref[0],   TMP4);
                vis_faligndata(TMP0, TMP2, REF_0);

                vis_ld64_2(ref, 8, TMP6);
                ref += stride;

                vis_ld64(ref[0],   TMP8);

                vis_ld64_2(ref, 8, TMP10);
                ref += stride;
                vis_faligndata(TMP4, TMP6, REF_4);

                vis_ld64(ref[0],   TMP12);

                vis_ld64_2(ref, 8, TMP14);
                ref += stride;
                vis_faligndata(TMP8, TMP10, REF_S0);

                vis_faligndata(TMP12, TMP14, REF_S4);

                if (off != 0x7) {
                        vis_alignaddr_g0((void *)off_plus_1);

                        vis_ld64(dest[0], DST_0);
                        vis_faligndata(TMP0, TMP2, REF_2);

                        vis_ld64_2(dest, stride, DST_2);
                        vis_faligndata(TMP4, TMP6, REF_6);

                        vis_faligndata(TMP8, TMP10, REF_S2);

                        vis_faligndata(TMP12, TMP14, REF_S6);
                } else {
                        vis_ld64(dest[0], DST_0);
                        vis_src1(TMP2, REF_2);

                        vis_ld64_2(dest, stride, DST_2);
                        vis_src1(TMP6, REF_6);

                        vis_src1(TMP10, REF_S2);

                        vis_src1(TMP14, REF_S6);
                }

                vis_pmerge(ZERO,     REF_0,     TMP0);
                vis_mul8x16au(REF_0_1, CONST_256, TMP2);

                vis_pmerge(ZERO,     REF_2,     TMP4);
                vis_mul8x16au(REF_2_1, CONST_256, TMP6);

                vis_padd16(TMP0, CONST_3, TMP0);
                vis_mul8x16al(DST_0,   CONST_512, TMP16);

                vis_padd16(TMP2, CONST_3, TMP2);
                vis_mul8x16al(DST_1,   CONST_512, TMP18);

                vis_padd16(TMP0, TMP4, TMP0);
                vis_mul8x16au(REF_4, CONST_256, TMP8);

                vis_padd16(TMP2, TMP6, TMP2);
                vis_mul8x16au(REF_4_1, CONST_256, TMP10);

                vis_padd16(TMP0, TMP16, TMP0);
                vis_mul8x16au(REF_6, CONST_256, TMP12);

                vis_padd16(TMP2, TMP18, TMP2);
                vis_mul8x16au(REF_6_1, CONST_256, TMP14);

                vis_padd16(TMP8, CONST_3, TMP8);
                vis_mul8x16al(DST_2, CONST_512, TMP16);

                vis_padd16(TMP8, TMP12, TMP8);
                vis_mul8x16al(DST_3, CONST_512, TMP18);

                vis_padd16(TMP10, TMP14, TMP10);
                vis_pack16(TMP0, DST_0);

                vis_pack16(TMP2, DST_1);
                vis_st64(DST_0, dest[0]);
                dest += stride;
                vis_padd16(TMP10, CONST_3, TMP10);

                vis_ld64_2(dest, stride, DST_0);
                vis_padd16(TMP8, TMP16, TMP8);

                vis_ld64_2(dest, stride_times_2, TMP4/*DST_2*/);
                vis_padd16(TMP10, TMP18, TMP10);
                vis_pack16(TMP8, DST_2);

                vis_pack16(TMP10, DST_3);
                vis_st64(DST_2, dest[0]);
                dest += stride;

                vis_mul8x16au(REF_S0_1, CONST_256, TMP2);
                vis_pmerge(ZERO,     REF_S0,     TMP0);

                vis_pmerge(ZERO,     REF_S2,     TMP24);
                vis_mul8x16au(REF_S2_1, CONST_256, TMP6);

                vis_padd16(TMP0, CONST_3, TMP0);
                vis_mul8x16au(REF_S4, CONST_256, TMP8);

                vis_padd16(TMP2, CONST_3, TMP2);
                vis_mul8x16au(REF_S4_1, CONST_256, TMP10);

                vis_padd16(TMP0, TMP24, TMP0);
                vis_mul8x16au(REF_S6, CONST_256, TMP12);

                vis_padd16(TMP2, TMP6, TMP2);
                vis_mul8x16au(REF_S6_1, CONST_256, TMP14);

                vis_padd16(TMP8, CONST_3, TMP8);
                vis_mul8x16al(DST_0,   CONST_512, TMP16);

                vis_padd16(TMP10, CONST_3, TMP10);
                vis_mul8x16al(DST_1,   CONST_512, TMP18);

                vis_padd16(TMP8, TMP12, TMP8);
                vis_mul8x16al(TMP4/*DST_2*/, CONST_512, TMP20);

                vis_mul8x16al(TMP5/*DST_3*/, CONST_512, TMP22);
                vis_padd16(TMP0, TMP16, TMP0);

                vis_padd16(TMP2, TMP18, TMP2);
                vis_pack16(TMP0, DST_0);

                vis_padd16(TMP10, TMP14, TMP10);
                vis_pack16(TMP2, DST_1);
                vis_st64(DST_0, dest[0]);
                dest += stride;

                vis_padd16(TMP8, TMP20, TMP8);

                vis_padd16(TMP10, TMP22, TMP10);
                vis_pack16(TMP8, DST_2);

                vis_pack16(TMP10, DST_3);
                vis_st64(DST_2, dest[0]);
                dest += stride;
        } while (--height);
}

static void MC_put_y_16_vis (uint8_t * dest, const uint8_t * ref,
                             const int stride, int height)
{
        ref = vis_alignaddr(ref);
        vis_ld64(ref[0], TMP0);

        vis_ld64_2(ref, 8, TMP2);

        vis_ld64_2(ref, 16, TMP4);
        ref += stride;

        vis_ld64(ref[0], TMP6);
        vis_faligndata(TMP0, TMP2, REF_0);

        vis_ld64_2(ref, 8, TMP8);
        vis_faligndata(TMP2, TMP4, REF_4);

        vis_ld64_2(ref, 16, TMP10);
        ref += stride;

        vis_ld64(constants_fe[0], MASK_fe);
        vis_faligndata(TMP6, TMP8, REF_2);

        vis_ld64(constants_7f[0], MASK_7f);
        vis_faligndata(TMP8, TMP10, REF_6);

        vis_ld64(constants128[0], CONST_128);
        height = (height >> 1) - 1;
        do {    /* 24 cycles */
                vis_ld64(ref[0], TMP0);
                vis_xor(REF_0, REF_2, TMP12);

                vis_ld64_2(ref, 8, TMP2);
                vis_xor(REF_4, REF_6, TMP16);

                vis_ld64_2(ref, 16, TMP4);
                ref += stride;
                vis_or(REF_0, REF_2, TMP14);

                vis_ld64(ref[0], TMP6);
                vis_or(REF_4, REF_6, TMP18);

                vis_ld64_2(ref, 8, TMP8);
                vis_faligndata(TMP0, TMP2, REF_0);

                vis_ld64_2(ref, 16, TMP10);
                ref += stride;
                vis_faligndata(TMP2, TMP4, REF_4);

                vis_and(TMP12, MASK_fe, TMP12);

                vis_and(TMP16, MASK_fe, TMP16);
                vis_mul8x16(CONST_128, TMP12, TMP12);

                vis_mul8x16(CONST_128, TMP16, TMP16);
                vis_xor(REF_0, REF_2, TMP0);

                vis_xor(REF_4, REF_6, TMP2);

                vis_or(REF_0, REF_2, TMP20);

                vis_and(TMP12, MASK_7f, TMP12);

                vis_and(TMP16, MASK_7f, TMP16);

                vis_psub16(TMP14, TMP12, TMP12);
                vis_st64(TMP12, dest[0]);

                vis_psub16(TMP18, TMP16, TMP16);
                vis_st64_2(TMP16, dest, 8);
                dest += stride;

                vis_or(REF_4, REF_6, TMP18);

                vis_and(TMP0, MASK_fe, TMP0);

                vis_and(TMP2, MASK_fe, TMP2);
                vis_mul8x16(CONST_128, TMP0, TMP0);

                vis_faligndata(TMP6, TMP8, REF_2);
                vis_mul8x16(CONST_128, TMP2, TMP2);

                vis_faligndata(TMP8, TMP10, REF_6);

                vis_and(TMP0, MASK_7f, TMP0);

                vis_and(TMP2, MASK_7f, TMP2);

                vis_psub16(TMP20, TMP0, TMP0);
                vis_st64(TMP0, dest[0]);

                vis_psub16(TMP18, TMP2, TMP2);
                vis_st64_2(TMP2, dest, 8);
                dest += stride;
        } while (--height);

        vis_ld64(ref[0], TMP0);
        vis_xor(REF_0, REF_2, TMP12);

        vis_ld64_2(ref, 8, TMP2);
        vis_xor(REF_4, REF_6, TMP16);

        vis_ld64_2(ref, 16, TMP4);
        vis_or(REF_0, REF_2, TMP14);

        vis_or(REF_4, REF_6, TMP18);

        vis_faligndata(TMP0, TMP2, REF_0);

        vis_faligndata(TMP2, TMP4, REF_4);

        vis_and(TMP12, MASK_fe, TMP12);

        vis_and(TMP16, MASK_fe, TMP16);
        vis_mul8x16(CONST_128, TMP12, TMP12);

        vis_mul8x16(CONST_128, TMP16, TMP16);
        vis_xor(REF_0, REF_2, TMP0);

        vis_xor(REF_4, REF_6, TMP2);

        vis_or(REF_0, REF_2, TMP20);

        vis_and(TMP12, MASK_7f, TMP12);

        vis_and(TMP16, MASK_7f, TMP16);

        vis_psub16(TMP14, TMP12, TMP12);
        vis_st64(TMP12, dest[0]);

        vis_psub16(TMP18, TMP16, TMP16);
        vis_st64_2(TMP16, dest, 8);
        dest += stride;

        vis_or(REF_4, REF_6, TMP18);

        vis_and(TMP0, MASK_fe, TMP0);

        vis_and(TMP2, MASK_fe, TMP2);
        vis_mul8x16(CONST_128, TMP0, TMP0);

        vis_mul8x16(CONST_128, TMP2, TMP2);

        vis_and(TMP0, MASK_7f, TMP0);

        vis_and(TMP2, MASK_7f, TMP2);

        vis_psub16(TMP20, TMP0, TMP0);
        vis_st64(TMP0, dest[0]);

        vis_psub16(TMP18, TMP2, TMP2);
        vis_st64_2(TMP2, dest, 8);
}

static void MC_put_y_8_vis (uint8_t * dest, const uint8_t * ref,
                            const int stride, int height)
{
        ref = vis_alignaddr(ref);
        vis_ld64(ref[0], TMP0);

        vis_ld64_2(ref, 8, TMP2);
        ref += stride;

        vis_ld64(ref[0], TMP4);

        vis_ld64_2(ref, 8, TMP6);
        ref += stride;

        vis_ld64(constants_fe[0], MASK_fe);
        vis_faligndata(TMP0, TMP2, REF_0);

        vis_ld64(constants_7f[0], MASK_7f);
        vis_faligndata(TMP4, TMP6, REF_2);

        vis_ld64(constants128[0], CONST_128);
        height = (height >> 1) - 1;
        do {    /* 12 cycles */
                vis_ld64(ref[0], TMP0);
                vis_xor(REF_0, REF_2, TMP4);

                vis_ld64_2(ref, 8, TMP2);
                ref += stride;
                vis_and(TMP4, MASK_fe, TMP4);

                vis_or(REF_0, REF_2, TMP6);
                vis_mul8x16(CONST_128, TMP4, TMP4);

                vis_faligndata(TMP0, TMP2, REF_0);
                vis_ld64(ref[0], TMP0);

                vis_ld64_2(ref, 8, TMP2);
                ref += stride;
                vis_xor(REF_0, REF_2, TMP12);

                vis_and(TMP4, MASK_7f, TMP4);

                vis_and(TMP12, MASK_fe, TMP12);

                vis_mul8x16(CONST_128, TMP12, TMP12);
                vis_or(REF_0, REF_2, TMP14);

                vis_psub16(TMP6, TMP4, DST_0);
                vis_st64(DST_0, dest[0]);
                dest += stride;

                vis_faligndata(TMP0, TMP2, REF_2);

                vis_and(TMP12, MASK_7f, TMP12);

                vis_psub16(TMP14, TMP12, DST_0);
                vis_st64(DST_0, dest[0]);
                dest += stride;
        } while (--height);

        vis_ld64(ref[0], TMP0);
        vis_xor(REF_0, REF_2, TMP4);

        vis_ld64_2(ref, 8, TMP2);
        vis_and(TMP4, MASK_fe, TMP4);

        vis_or(REF_0, REF_2, TMP6);
        vis_mul8x16(CONST_128, TMP4, TMP4);

        vis_faligndata(TMP0, TMP2, REF_0);

        vis_xor(REF_0, REF_2, TMP12);

        vis_and(TMP4, MASK_7f, TMP4);

        vis_and(TMP12, MASK_fe, TMP12);

        vis_mul8x16(CONST_128, TMP12, TMP12);
        vis_or(REF_0, REF_2, TMP14);

        vis_psub16(TMP6, TMP4, DST_0);
        vis_st64(DST_0, dest[0]);
        dest += stride;

        vis_and(TMP12, MASK_7f, TMP12);

        vis_psub16(TMP14, TMP12, DST_0);
        vis_st64(DST_0, dest[0]);
}

static void MC_avg_y_16_vis (uint8_t * dest, const uint8_t * ref,
                             const int stride, int height)
{
        int stride_8 = stride + 8;
        int stride_16 = stride + 16;

        vis_set_gsr(5 << VIS_GSR_SCALEFACT_SHIFT);

        ref = vis_alignaddr(ref);

        vis_ld64(ref[ 0], TMP0);
        vis_fzero(ZERO);

        vis_ld64(ref[ 8], TMP2);

        vis_ld64(ref[16], TMP4);

        vis_ld64(constants3[0], CONST_3);
        vis_faligndata(TMP0, TMP2, REF_2);

        vis_ld64(constants256_512[0], CONST_256);
        vis_faligndata(TMP2, TMP4, REF_6);
        height >>= 1;

        do {    /* 31 cycles */
                vis_ld64_2(ref, stride, TMP0);
                vis_pmerge(ZERO,       REF_2,     TMP12);
                vis_mul8x16au(REF_2_1, CONST_256, TMP14);

                vis_ld64_2(ref, stride_8, TMP2);
                vis_pmerge(ZERO,       REF_6,     TMP16);
                vis_mul8x16au(REF_6_1, CONST_256, TMP18);

                vis_ld64_2(ref, stride_16, TMP4);
                ref += stride;

                vis_ld64(dest[0], DST_0);
                vis_faligndata(TMP0, TMP2, REF_0);

                vis_ld64_2(dest, 8, DST_2);
                vis_faligndata(TMP2, TMP4, REF_4);

                vis_ld64_2(ref, stride, TMP6);
                vis_pmerge(ZERO,     REF_0,     TMP0);
                vis_mul8x16au(REF_0_1, CONST_256, TMP2);

                vis_ld64_2(ref, stride_8, TMP8);
                vis_pmerge(ZERO,     REF_4,     TMP4);

                vis_ld64_2(ref, stride_16, TMP10);
                ref += stride;

                vis_ld64_2(dest, stride, REF_S0/*DST_4*/);
                vis_faligndata(TMP6, TMP8, REF_2);
                vis_mul8x16au(REF_4_1, CONST_256, TMP6);

                vis_ld64_2(dest, stride_8, REF_S2/*DST_6*/);
                vis_faligndata(TMP8, TMP10, REF_6);
                vis_mul8x16al(DST_0,   CONST_512, TMP20);

                vis_padd16(TMP0, CONST_3, TMP0);
                vis_mul8x16al(DST_1,   CONST_512, TMP22);

                vis_padd16(TMP2, CONST_3, TMP2);
                vis_mul8x16al(DST_2,   CONST_512, TMP24);

                vis_padd16(TMP4, CONST_3, TMP4);
                vis_mul8x16al(DST_3,   CONST_512, TMP26);

                vis_padd16(TMP6, CONST_3, TMP6);

                vis_padd16(TMP12, TMP20, TMP12);
                vis_mul8x16al(REF_S0,   CONST_512, TMP20);

                vis_padd16(TMP14, TMP22, TMP14);
                vis_mul8x16al(REF_S0_1, CONST_512, TMP22);

                vis_padd16(TMP16, TMP24, TMP16);
                vis_mul8x16al(REF_S2,   CONST_512, TMP24);

                vis_padd16(TMP18, TMP26, TMP18);
                vis_mul8x16al(REF_S2_1, CONST_512, TMP26);

                vis_padd16(TMP12, TMP0, TMP12);
                vis_mul8x16au(REF_2,   CONST_256, TMP28);

                vis_padd16(TMP14, TMP2, TMP14);
                vis_mul8x16au(REF_2_1, CONST_256, TMP30);

                vis_padd16(TMP16, TMP4, TMP16);
                vis_mul8x16au(REF_6,   CONST_256, REF_S4);

                vis_padd16(TMP18, TMP6, TMP18);
                vis_mul8x16au(REF_6_1, CONST_256, REF_S6);

                vis_pack16(TMP12, DST_0);
                vis_padd16(TMP28, TMP0, TMP12);

                vis_pack16(TMP14, DST_1);
                vis_st64(DST_0, dest[0]);
                vis_padd16(TMP30, TMP2, TMP14);

                vis_pack16(TMP16, DST_2);
                vis_padd16(REF_S4, TMP4, TMP16);

                vis_pack16(TMP18, DST_3);
                vis_st64_2(DST_2, dest, 8);
                dest += stride;
                vis_padd16(REF_S6, TMP6, TMP18);

                vis_padd16(TMP12, TMP20, TMP12);

                vis_padd16(TMP14, TMP22, TMP14);
                vis_pack16(TMP12, DST_0);

                vis_padd16(TMP16, TMP24, TMP16);
                vis_pack16(TMP14, DST_1);
                vis_st64(DST_0, dest[0]);

                vis_padd16(TMP18, TMP26, TMP18);
                vis_pack16(TMP16, DST_2);

                vis_pack16(TMP18, DST_3);
                vis_st64_2(DST_2, dest, 8);
                dest += stride;
        } while (--height);
}

static void MC_avg_y_8_vis (uint8_t * dest, const uint8_t * ref,
                            const int stride, int height)
{
        int stride_8 = stride + 8;

        vis_set_gsr(5 << VIS_GSR_SCALEFACT_SHIFT);

        ref = vis_alignaddr(ref);

        vis_ld64(ref[ 0], TMP0);
        vis_fzero(ZERO);

        vis_ld64(ref[ 8], TMP2);

        vis_ld64(constants3[0], CONST_3);
        vis_faligndata(TMP0, TMP2, REF_2);

        vis_ld64(constants256_512[0], CONST_256);

        height >>= 1;
        do {    /* 20 cycles */
                vis_ld64_2(ref, stride, TMP0);
                vis_pmerge(ZERO,       REF_2,     TMP8);
                vis_mul8x16au(REF_2_1, CONST_256, TMP10);

                vis_ld64_2(ref, stride_8, TMP2);
                ref += stride;

                vis_ld64(dest[0], DST_0);

                vis_ld64_2(dest, stride, DST_2);
                vis_faligndata(TMP0, TMP2, REF_0);

                vis_ld64_2(ref, stride, TMP4);
                vis_mul8x16al(DST_0,   CONST_512, TMP16);
                vis_pmerge(ZERO,       REF_0,     TMP12);

                vis_ld64_2(ref, stride_8, TMP6);
                ref += stride;
                vis_mul8x16al(DST_1,   CONST_512, TMP18);
                vis_pmerge(ZERO,       REF_0_1,   TMP14);

                vis_padd16(TMP12, CONST_3, TMP12);
                vis_mul8x16al(DST_2,   CONST_512, TMP24);

                vis_padd16(TMP14, CONST_3, TMP14);
                vis_mul8x16al(DST_3,   CONST_512, TMP26);

                vis_faligndata(TMP4, TMP6, REF_2);

                vis_padd16(TMP8, TMP12, TMP8);

                vis_padd16(TMP10, TMP14, TMP10);
                vis_mul8x16au(REF_2,   CONST_256, TMP20);

                vis_padd16(TMP8, TMP16, TMP0);
                vis_mul8x16au(REF_2_1, CONST_256, TMP22);

                vis_padd16(TMP10, TMP18, TMP2);
                vis_pack16(TMP0, DST_0);

                vis_pack16(TMP2, DST_1);
                vis_st64(DST_0, dest[0]);
                dest += stride;
                vis_padd16(TMP12, TMP20, TMP12);

                vis_padd16(TMP14, TMP22, TMP14);

                vis_padd16(TMP12, TMP24, TMP0);

                vis_padd16(TMP14, TMP26, TMP2);
                vis_pack16(TMP0, DST_2);

                vis_pack16(TMP2, DST_3);
                vis_st64(DST_2, dest[0]);
                dest += stride;
        } while (--height);
}

static void MC_put_xy_16_vis (uint8_t * dest, const uint8_t * ref,
                              const int stride, int height)
{
        unsigned long off = (unsigned long) ref & 0x7;
        unsigned long off_plus_1 = off + 1;
        int stride_8 = stride + 8;
        int stride_16 = stride + 16;

        vis_set_gsr(5 << VIS_GSR_SCALEFACT_SHIFT);

        ref = vis_alignaddr(ref);

        vis_ld64(ref[ 0], TMP0);
        vis_fzero(ZERO);

        vis_ld64(ref[ 8], TMP2);

        vis_ld64(ref[16], TMP4);

        vis_ld64(constants2[0], CONST_2);
        vis_faligndata(TMP0, TMP2, REF_S0);

        vis_ld64(constants256_512[0], CONST_256);
        vis_faligndata(TMP2, TMP4, REF_S4);

        if (off != 0x7) {
                vis_alignaddr_g0((void *)off_plus_1);
                vis_faligndata(TMP0, TMP2, REF_S2);
                vis_faligndata(TMP2, TMP4, REF_S6);
        } else {
                vis_src1(TMP2, REF_S2);
                vis_src1(TMP4, REF_S6);
        }

        height >>= 1;
        do {
                vis_ld64_2(ref, stride, TMP0);
                vis_mul8x16au(REF_S0, CONST_256, TMP12);
                vis_pmerge(ZERO,      REF_S0_1,  TMP14);

                vis_alignaddr_g0((void *)off);

                vis_ld64_2(ref, stride_8, TMP2);
                vis_mul8x16au(REF_S2, CONST_256, TMP16);
                vis_pmerge(ZERO,      REF_S2_1,  TMP18);

                vis_ld64_2(ref, stride_16, TMP4);
                ref += stride;
                vis_mul8x16au(REF_S4, CONST_256, TMP20);
                vis_pmerge(ZERO,      REF_S4_1,  TMP22);

                vis_ld64_2(ref, stride, TMP6);
                vis_mul8x16au(REF_S6, CONST_256, TMP24);
                vis_pmerge(ZERO,      REF_S6_1,  TMP26);

                vis_ld64_2(ref, stride_8, TMP8);
                vis_faligndata(TMP0, TMP2, REF_0);

                vis_ld64_2(ref, stride_16, TMP10);
                ref += stride;
                vis_faligndata(TMP2, TMP4, REF_4);

                vis_faligndata(TMP6, TMP8, REF_S0);

                vis_faligndata(TMP8, TMP10, REF_S4);

                if (off != 0x7) {
                        vis_alignaddr_g0((void *)off_plus_1);
                        vis_faligndata(TMP0, TMP2, REF_2);
                        vis_faligndata(TMP2, TMP4, REF_6);
                        vis_faligndata(TMP6, TMP8, REF_S2);
                        vis_faligndata(TMP8, TMP10, REF_S6);
                } else {
                        vis_src1(TMP2, REF_2);
                        vis_src1(TMP4, REF_6);
                        vis_src1(TMP8, REF_S2);
                        vis_src1(TMP10, REF_S6);
                }

                vis_mul8x16au(REF_0, CONST_256, TMP0);
                vis_pmerge(ZERO,      REF_0_1,  TMP2);

                vis_mul8x16au(REF_2, CONST_256, TMP4);
                vis_pmerge(ZERO,      REF_2_1,  TMP6);

                vis_padd16(TMP0, CONST_2, TMP8);
                vis_mul8x16au(REF_4, CONST_256, TMP0);

                vis_padd16(TMP2, CONST_2, TMP10);
                vis_mul8x16au(REF_4_1, CONST_256, TMP2);

                vis_padd16(TMP8, TMP4, TMP8);
                vis_mul8x16au(REF_6, CONST_256, TMP4);

                vis_padd16(TMP10, TMP6, TMP10);
                vis_mul8x16au(REF_6_1, CONST_256, TMP6);

                vis_padd16(TMP12, TMP8, TMP12);

                vis_padd16(TMP14, TMP10, TMP14);

                vis_padd16(TMP12, TMP16, TMP12);

                vis_padd16(TMP14, TMP18, TMP14);
                vis_pack16(TMP12, DST_0);

                vis_pack16(TMP14, DST_1);
                vis_st64(DST_0, dest[0]);
                vis_padd16(TMP0, CONST_2, TMP12);

                vis_mul8x16au(REF_S0, CONST_256, TMP0);
                vis_padd16(TMP2, CONST_2, TMP14);

                vis_mul8x16au(REF_S0_1, CONST_256, TMP2);
                vis_padd16(TMP12, TMP4, TMP12);

                vis_mul8x16au(REF_S2, CONST_256, TMP4);
                vis_padd16(TMP14, TMP6, TMP14);

                vis_mul8x16au(REF_S2_1, CONST_256, TMP6);
                vis_padd16(TMP20, TMP12, TMP20);

                vis_padd16(TMP22, TMP14, TMP22);

                vis_padd16(TMP20, TMP24, TMP20);

                vis_padd16(TMP22, TMP26, TMP22);
                vis_pack16(TMP20, DST_2);

                vis_pack16(TMP22, DST_3);
                vis_st64_2(DST_2, dest, 8);
                dest += stride;
                vis_padd16(TMP0, TMP4, TMP24);

                vis_mul8x16au(REF_S4, CONST_256, TMP0);
                vis_padd16(TMP2, TMP6, TMP26);

                vis_mul8x16au(REF_S4_1, CONST_256, TMP2);
                vis_padd16(TMP24, TMP8, TMP24);

                vis_padd16(TMP26, TMP10, TMP26);
                vis_pack16(TMP24, DST_0);

                vis_pack16(TMP26, DST_1);
                vis_st64(DST_0, dest[0]);
                vis_pmerge(ZERO, REF_S6, TMP4);

                vis_pmerge(ZERO,      REF_S6_1,  TMP6);

                vis_padd16(TMP0, TMP4, TMP0);

                vis_padd16(TMP2, TMP6, TMP2);

                vis_padd16(TMP0, TMP12, TMP0);

                vis_padd16(TMP2, TMP14, TMP2);
                vis_pack16(TMP0, DST_2);

                vis_pack16(TMP2, DST_3);
                vis_st64_2(DST_2, dest, 8);
                dest += stride;
        } while (--height);
}

static void MC_put_xy_8_vis (uint8_t * dest, const uint8_t * ref,
                             const int stride, int height)
{
        unsigned long off = (unsigned long) ref & 0x7;
        unsigned long off_plus_1 = off + 1;
        int stride_8 = stride + 8;

        vis_set_gsr(5 << VIS_GSR_SCALEFACT_SHIFT);

        ref = vis_alignaddr(ref);

        vis_ld64(ref[ 0], TMP0);
        vis_fzero(ZERO);

        vis_ld64(ref[ 8], TMP2);

        vis_ld64(constants2[0], CONST_2);

        vis_ld64(constants256_512[0], CONST_256);
        vis_faligndata(TMP0, TMP2, REF_S0);

        if (off != 0x7) {
                vis_alignaddr_g0((void *)off_plus_1);
                vis_faligndata(TMP0, TMP2, REF_S2);
        } else {
                vis_src1(TMP2, REF_S2);
        }

        height >>= 1;
        do {    /* 26 cycles */
                vis_ld64_2(ref, stride, TMP0);
                vis_mul8x16au(REF_S0,   CONST_256, TMP8);
                vis_pmerge(ZERO,        REF_S2,    TMP12);

                vis_alignaddr_g0((void *)off);

                vis_ld64_2(ref, stride_8, TMP2);
                ref += stride;
                vis_mul8x16au(REF_S0_1, CONST_256, TMP10);
                vis_pmerge(ZERO,        REF_S2_1,  TMP14);

                vis_ld64_2(ref, stride, TMP4);

                vis_ld64_2(ref, stride_8, TMP6);
                ref += stride;
                vis_faligndata(TMP0, TMP2, REF_S4);

                vis_pmerge(ZERO, REF_S4, TMP18);

                vis_pmerge(ZERO, REF_S4_1, TMP20);

                vis_faligndata(TMP4, TMP6, REF_S0);

                if (off != 0x7) {
                        vis_alignaddr_g0((void *)off_plus_1);
                        vis_faligndata(TMP0, TMP2, REF_S6);
                        vis_faligndata(TMP4, TMP6, REF_S2);
                } else {
                        vis_src1(TMP2, REF_S6);
                        vis_src1(TMP6, REF_S2);
                }

                vis_padd16(TMP18, CONST_2, TMP18);
                vis_mul8x16au(REF_S6,   CONST_256, TMP22);

                vis_padd16(TMP20, CONST_2, TMP20);
                vis_mul8x16au(REF_S6_1, CONST_256, TMP24);

                vis_mul8x16au(REF_S0,   CONST_256, TMP26);
                vis_pmerge(ZERO, REF_S0_1, TMP28);

                vis_mul8x16au(REF_S2,   CONST_256, TMP30);
                vis_padd16(TMP18, TMP22, TMP18);

                vis_mul8x16au(REF_S2_1, CONST_256, TMP32);
                vis_padd16(TMP20, TMP24, TMP20);

                vis_padd16(TMP8,  TMP18, TMP8);

                vis_padd16(TMP10, TMP20, TMP10);

                vis_padd16(TMP8,  TMP12, TMP8);

                vis_padd16(TMP10, TMP14, TMP10);
                vis_pack16(TMP8,  DST_0);

                vis_pack16(TMP10, DST_1);
                vis_st64(DST_0, dest[0]);
                dest += stride;
                vis_padd16(TMP18, TMP26, TMP18);

                vis_padd16(TMP20, TMP28, TMP20);

                vis_padd16(TMP18, TMP30, TMP18);

                vis_padd16(TMP20, TMP32, TMP20);
                vis_pack16(TMP18, DST_2);

                vis_pack16(TMP20, DST_3);
                vis_st64(DST_2, dest[0]);
                dest += stride;
        } while (--height);
}

static void MC_avg_xy_16_vis (uint8_t * dest, const uint8_t * ref,
                              const int stride, int height)
{
        unsigned long off = (unsigned long) ref & 0x7;
        unsigned long off_plus_1 = off + 1;
        int stride_8 = stride + 8;
        int stride_16 = stride + 16;

        vis_set_gsr(4 << VIS_GSR_SCALEFACT_SHIFT);

        ref = vis_alignaddr(ref);

        vis_ld64(ref[ 0], TMP0);
        vis_fzero(ZERO);

        vis_ld64(ref[ 8], TMP2);

        vis_ld64(ref[16], TMP4);

        vis_ld64(constants6[0], CONST_6);
        vis_faligndata(TMP0, TMP2, REF_S0);

        vis_ld64(constants256_1024[0], CONST_256);
        vis_faligndata(TMP2, TMP4, REF_S4);

        if (off != 0x7) {
                vis_alignaddr_g0((void *)off_plus_1);
                vis_faligndata(TMP0, TMP2, REF_S2);
                vis_faligndata(TMP2, TMP4, REF_S6);
        } else {
                vis_src1(TMP2, REF_S2);
                vis_src1(TMP4, REF_S6);
        }

        height >>= 1;
        do {    /* 55 cycles */
                vis_ld64_2(ref, stride, TMP0);
                vis_mul8x16au(REF_S0, CONST_256, TMP12);
                vis_pmerge(ZERO,      REF_S0_1,  TMP14);

                vis_alignaddr_g0((void *)off);

                vis_ld64_2(ref, stride_8, TMP2);
                vis_mul8x16au(REF_S2, CONST_256, TMP16);
                vis_pmerge(ZERO,      REF_S2_1,  TMP18);

                vis_ld64_2(ref, stride_16, TMP4);
                ref += stride;
                vis_mul8x16au(REF_S4, CONST_256, TMP20);
                vis_pmerge(ZERO,      REF_S4_1,  TMP22);

                vis_ld64_2(ref, stride, TMP6);
                vis_mul8x16au(REF_S6, CONST_256, TMP24);
                vis_pmerge(ZERO,      REF_S6_1,  TMP26);

                vis_ld64_2(ref, stride_8, TMP8);
                vis_faligndata(TMP0, TMP2, REF_0);

                vis_ld64_2(ref, stride_16, TMP10);
                ref += stride;
                vis_faligndata(TMP2, TMP4, REF_4);

                vis_ld64(dest[0], DST_0);
                vis_faligndata(TMP6, TMP8, REF_S0);

                vis_ld64_2(dest, 8, DST_2);
                vis_faligndata(TMP8, TMP10, REF_S4);

                if (off != 0x7) {
                        vis_alignaddr_g0((void *)off_plus_1);
                        vis_faligndata(TMP0, TMP2, REF_2);
                        vis_faligndata(TMP2, TMP4, REF_6);
                        vis_faligndata(TMP6, TMP8, REF_S2);
                        vis_faligndata(TMP8, TMP10, REF_S6);
                } else {
                        vis_src1(TMP2, REF_2);
                        vis_src1(TMP4, REF_6);
                        vis_src1(TMP8, REF_S2);
                        vis_src1(TMP10, REF_S6);
                }

                vis_mul8x16al(DST_0,   CONST_1024, TMP30);
                vis_pmerge(ZERO, REF_0, TMP0);

                vis_mul8x16al(DST_1,   CONST_1024, TMP32);
                vis_pmerge(ZERO,      REF_0_1,  TMP2);

                vis_mul8x16au(REF_2, CONST_256, TMP4);
                vis_pmerge(ZERO,      REF_2_1,  TMP6);

                vis_mul8x16al(DST_2,   CONST_1024, REF_0);
                vis_padd16(TMP0, CONST_6, TMP0);

                vis_mul8x16al(DST_3,   CONST_1024, REF_2);
                vis_padd16(TMP2, CONST_6, TMP2);

                vis_padd16(TMP0, TMP4, TMP0);
                vis_mul8x16au(REF_4, CONST_256, TMP4);

                vis_padd16(TMP2, TMP6, TMP2);
                vis_mul8x16au(REF_4_1, CONST_256, TMP6);

                vis_padd16(TMP12, TMP0, TMP12);
                vis_mul8x16au(REF_6, CONST_256, TMP8);

                vis_padd16(TMP14, TMP2, TMP14);
                vis_mul8x16au(REF_6_1, CONST_256, TMP10);

                vis_padd16(TMP12, TMP16, TMP12);
                vis_mul8x16au(REF_S0, CONST_256, REF_4);

                vis_padd16(TMP14, TMP18, TMP14);
                vis_mul8x16au(REF_S0_1, CONST_256, REF_6);

                vis_padd16(TMP12, TMP30, TMP12);

                vis_padd16(TMP14, TMP32, TMP14);
                vis_pack16(TMP12, DST_0);

                vis_pack16(TMP14, DST_1);
                vis_st64(DST_0, dest[0]);
                vis_padd16(TMP4, CONST_6, TMP4);

                vis_ld64_2(dest, stride, DST_0);
                vis_padd16(TMP6, CONST_6, TMP6);
                vis_mul8x16au(REF_S2, CONST_256, TMP12);

                vis_padd16(TMP4, TMP8, TMP4);
                vis_mul8x16au(REF_S2_1, CONST_256,  TMP14);

                vis_padd16(TMP6, TMP10, TMP6);

                vis_padd16(TMP20, TMP4, TMP20);

                vis_padd16(TMP22, TMP6, TMP22);

                vis_padd16(TMP20, TMP24, TMP20);

                vis_padd16(TMP22, TMP26, TMP22);

                vis_padd16(TMP20, REF_0, TMP20);
                vis_mul8x16au(REF_S4, CONST_256, REF_0);

                vis_padd16(TMP22, REF_2, TMP22);
                vis_pack16(TMP20, DST_2);

                vis_pack16(TMP22, DST_3);
                vis_st64_2(DST_2, dest, 8);
                dest += stride;

                vis_ld64_2(dest, 8, DST_2);
                vis_mul8x16al(DST_0,   CONST_1024, TMP30);
                vis_pmerge(ZERO,      REF_S4_1,  REF_2);

                vis_mul8x16al(DST_1,   CONST_1024, TMP32);
                vis_padd16(REF_4, TMP0, TMP8);

                vis_mul8x16au(REF_S6, CONST_256, REF_4);
                vis_padd16(REF_6, TMP2, TMP10);

                vis_mul8x16au(REF_S6_1, CONST_256, REF_6);
                vis_padd16(TMP8, TMP12, TMP8);

                vis_padd16(TMP10, TMP14, TMP10);

                vis_padd16(TMP8, TMP30, TMP8);

                vis_padd16(TMP10, TMP32, TMP10);
                vis_pack16(TMP8, DST_0);

                vis_pack16(TMP10, DST_1);
                vis_st64(DST_0, dest[0]);

                vis_padd16(REF_0, TMP4, REF_0);

                vis_mul8x16al(DST_2,   CONST_1024, TMP30);
                vis_padd16(REF_2, TMP6, REF_2);

                vis_mul8x16al(DST_3,   CONST_1024, TMP32);
                vis_padd16(REF_0, REF_4, REF_0);

                vis_padd16(REF_2, REF_6, REF_2);

                vis_padd16(REF_0, TMP30, REF_0);

                /* stall */

                vis_padd16(REF_2, TMP32, REF_2);
                vis_pack16(REF_0, DST_2);

                vis_pack16(REF_2, DST_3);
                vis_st64_2(DST_2, dest, 8);
                dest += stride;
        } while (--height);
}

static void MC_avg_xy_8_vis (uint8_t * dest, const uint8_t * ref,
                             const int stride, int height)
{
        unsigned long off = (unsigned long) ref & 0x7;
        unsigned long off_plus_1 = off + 1;
        int stride_8 = stride + 8;

        vis_set_gsr(4 << VIS_GSR_SCALEFACT_SHIFT);

        ref = vis_alignaddr(ref);

        vis_ld64(ref[0], TMP0);
        vis_fzero(ZERO);

        vis_ld64_2(ref, 8, TMP2);

        vis_ld64(constants6[0], CONST_6);

        vis_ld64(constants256_1024[0], CONST_256);
        vis_faligndata(TMP0, TMP2, REF_S0);

        if (off != 0x7) {
                vis_alignaddr_g0((void *)off_plus_1);
                vis_faligndata(TMP0, TMP2, REF_S2);
        } else {
                vis_src1(TMP2, REF_S2);
        }

        height >>= 1;
        do {    /* 31 cycles */
                vis_ld64_2(ref, stride, TMP0);
                vis_mul8x16au(REF_S0, CONST_256, TMP8);
                vis_pmerge(ZERO,      REF_S0_1,  TMP10);

                vis_ld64_2(ref, stride_8, TMP2);
                ref += stride;
                vis_mul8x16au(REF_S2, CONST_256, TMP12);
                vis_pmerge(ZERO,      REF_S2_1,  TMP14);

                vis_alignaddr_g0((void *)off);

                vis_ld64_2(ref, stride, TMP4);
                vis_faligndata(TMP0, TMP2, REF_S4);

                vis_ld64_2(ref, stride_8, TMP6);
                ref += stride;

                vis_ld64(dest[0], DST_0);
                vis_faligndata(TMP4, TMP6, REF_S0);

                vis_ld64_2(dest, stride, DST_2);

                if (off != 0x7) {
                        vis_alignaddr_g0((void *)off_plus_1);
                        vis_faligndata(TMP0, TMP2, REF_S6);
                        vis_faligndata(TMP4, TMP6, REF_S2);
                } else {
                        vis_src1(TMP2, REF_S6);
                        vis_src1(TMP6, REF_S2);
                }

                vis_mul8x16al(DST_0,   CONST_1024, TMP30);
                vis_pmerge(ZERO, REF_S4, TMP22);

                vis_mul8x16al(DST_1,   CONST_1024, TMP32);
                vis_pmerge(ZERO,      REF_S4_1,  TMP24);

                vis_mul8x16au(REF_S6, CONST_256, TMP26);
                vis_pmerge(ZERO,      REF_S6_1,  TMP28);

                vis_mul8x16au(REF_S0, CONST_256, REF_S4);
                vis_padd16(TMP22, CONST_6, TMP22);

                vis_mul8x16au(REF_S0_1, CONST_256, REF_S6);
                vis_padd16(TMP24, CONST_6, TMP24);

                vis_mul8x16al(DST_2,   CONST_1024, REF_0);
                vis_padd16(TMP22, TMP26, TMP22);

                vis_mul8x16al(DST_3,   CONST_1024, REF_2);
                vis_padd16(TMP24, TMP28, TMP24);

                vis_mul8x16au(REF_S2, CONST_256, TMP26);
                vis_padd16(TMP8, TMP22, TMP8);

                vis_mul8x16au(REF_S2_1, CONST_256, TMP28);
                vis_padd16(TMP10, TMP24, TMP10);

                vis_padd16(TMP8, TMP12, TMP8);

                vis_padd16(TMP10, TMP14, TMP10);

                vis_padd16(TMP8, TMP30, TMP8);

                vis_padd16(TMP10, TMP32, TMP10);
                vis_pack16(TMP8, DST_0);

                vis_pack16(TMP10, DST_1);
                vis_st64(DST_0, dest[0]);
                dest += stride;

                vis_padd16(REF_S4, TMP22, TMP12);

                vis_padd16(REF_S6, TMP24, TMP14);

                vis_padd16(TMP12, TMP26, TMP12);

                vis_padd16(TMP14, TMP28, TMP14);

                vis_padd16(TMP12, REF_0, TMP12);

                vis_padd16(TMP14, REF_2, TMP14);
                vis_pack16(TMP12, DST_2);

                vis_pack16(TMP14, DST_3);
                vis_st64(DST_2, dest[0]);
                dest += stride;
        } while (--height);
}

/* End of rounding code */

/* Start of no rounding code */
/* The trick used in some of this file is the formula from the MMX
 * motion comp code, which is:
 *
 * (x+y)>>1 == (x&y)+((x^y)>>1)
 *
 * This allows us to average 8 bytes at a time in a 64-bit FPU reg.
 * We avoid overflows by masking before we do the shift, and we
 * implement the shift by multiplying by 1/2 using mul8x16.  So in
 * VIS this is (assume 'x' is in f0, 'y' is in f2, a repeating mask
 * of '0xfe' is in f4, a repeating mask of '0x7f' is in f6, and
 * the value 0x80808080 is in f8):
 *
 *      fxor            f0,   f2, f10
 *      fand            f10,  f4, f10
 *      fmul8x16        f8,  f10, f10
 *      fand            f10,  f6, f10
 *      fand            f0,   f2, f12
 *      fpadd16         f12, f10, f10
 */

static void MC_put_no_round_o_16_vis (uint8_t * dest, const uint8_t * ref,
                                      const int stride, int height)
{
        ref = vis_alignaddr(ref);
        do {    /* 5 cycles */
                vis_ld64(ref[0], TMP0);

                vis_ld64_2(ref, 8, TMP2);

                vis_ld64_2(ref, 16, TMP4);
                ref += stride;

                vis_faligndata(TMP0, TMP2, REF_0);
                vis_st64(REF_0, dest[0]);

                vis_faligndata(TMP2, TMP4, REF_2);
                vis_st64_2(REF_2, dest, 8);
                dest += stride;
        } while (--height);
}

static void MC_put_no_round_o_8_vis (uint8_t * dest, const uint8_t * ref,
                            const int stride, int height)
{
        ref = vis_alignaddr(ref);
        do {    /* 4 cycles */
                vis_ld64(ref[0], TMP0);

                vis_ld64(ref[8], TMP2);
                ref += stride;

                /* stall */

                vis_faligndata(TMP0, TMP2, REF_0);
                vis_st64(REF_0, dest[0]);
                dest += stride;
        } while (--height);
}


static void MC_avg_no_round_o_16_vis (uint8_t * dest, const uint8_t * ref,
                             const int stride, int height)
{
        int stride_8 = stride + 8;

        ref = vis_alignaddr(ref);

        vis_ld64(ref[0], TMP0);

        vis_ld64(ref[8], TMP2);

        vis_ld64(ref[16], TMP4);

        vis_ld64(dest[0], DST_0);

        vis_ld64(dest[8], DST_2);

        vis_ld64(constants_fe[0], MASK_fe);
        vis_faligndata(TMP0, TMP2, REF_0);

        vis_ld64(constants_7f[0], MASK_7f);
        vis_faligndata(TMP2, TMP4, REF_2);

        vis_ld64(constants128[0], CONST_128);

        ref += stride;
        height = (height >> 1) - 1;

        do {    /* 24 cycles */
                vis_ld64(ref[0], TMP0);
                vis_xor(DST_0, REF_0, TMP6);

                vis_ld64_2(ref, 8, TMP2);
                vis_and(TMP6, MASK_fe, TMP6);

                vis_ld64_2(ref, 16, TMP4);
                ref += stride;
                vis_mul8x16(CONST_128, TMP6, TMP6);
                vis_xor(DST_2, REF_2, TMP8);

                vis_and(TMP8, MASK_fe, TMP8);

                vis_and(DST_0, REF_0, TMP10);
                vis_ld64_2(dest, stride, DST_0);
                vis_mul8x16(CONST_128, TMP8, TMP8);

                vis_and(DST_2, REF_2, TMP12);
                vis_ld64_2(dest, stride_8, DST_2);

                vis_ld64(ref[0], TMP14);
                vis_and(TMP6, MASK_7f, TMP6);

                vis_and(TMP8, MASK_7f, TMP8);

                vis_padd16(TMP10, TMP6, TMP6);
                vis_st64(TMP6, dest[0]);

                vis_padd16(TMP12, TMP8, TMP8);
                vis_st64_2(TMP8, dest, 8);

                dest += stride;
                vis_ld64_2(ref, 8, TMP16);
                vis_faligndata(TMP0, TMP2, REF_0);

                vis_ld64_2(ref, 16, TMP18);
                vis_faligndata(TMP2, TMP4, REF_2);
                ref += stride;

                vis_xor(DST_0, REF_0, TMP20);

                vis_and(TMP20, MASK_fe, TMP20);

                vis_xor(DST_2, REF_2, TMP22);
                vis_mul8x16(CONST_128, TMP20, TMP20);

                vis_and(TMP22, MASK_fe, TMP22);

                vis_and(DST_0, REF_0, TMP24);
                vis_mul8x16(CONST_128, TMP22, TMP22);

                vis_and(DST_2, REF_2, TMP26);

                vis_ld64_2(dest, stride, DST_0);
                vis_faligndata(TMP14, TMP16, REF_0);

                vis_ld64_2(dest, stride_8, DST_2);
                vis_faligndata(TMP16, TMP18, REF_2);

                vis_and(TMP20, MASK_7f, TMP20);

                vis_and(TMP22, MASK_7f, TMP22);

                vis_padd16(TMP24, TMP20, TMP20);
                vis_st64(TMP20, dest[0]);

                vis_padd16(TMP26, TMP22, TMP22);
                vis_st64_2(TMP22, dest, 8);
                dest += stride;
        } while (--height);

        vis_ld64(ref[0], TMP0);
        vis_xor(DST_0, REF_0, TMP6);

        vis_ld64_2(ref, 8, TMP2);
        vis_and(TMP6, MASK_fe, TMP6);

        vis_ld64_2(ref, 16, TMP4);
        vis_mul8x16(CONST_128, TMP6, TMP6);
        vis_xor(DST_2, REF_2, TMP8);

        vis_and(TMP8, MASK_fe, TMP8);

        vis_and(DST_0, REF_0, TMP10);
        vis_ld64_2(dest, stride, DST_0);
        vis_mul8x16(CONST_128, TMP8, TMP8);

        vis_and(DST_2, REF_2, TMP12);
        vis_ld64_2(dest, stride_8, DST_2);

        vis_ld64(ref[0], TMP14);
        vis_and(TMP6, MASK_7f, TMP6);

        vis_and(TMP8, MASK_7f, TMP8);

        vis_padd16(TMP10, TMP6, TMP6);
        vis_st64(TMP6, dest[0]);

        vis_padd16(TMP12, TMP8, TMP8);
        vis_st64_2(TMP8, dest, 8);

        dest += stride;
        vis_faligndata(TMP0, TMP2, REF_0);

        vis_faligndata(TMP2, TMP4, REF_2);

        vis_xor(DST_0, REF_0, TMP20);

        vis_and(TMP20, MASK_fe, TMP20);

        vis_xor(DST_2, REF_2, TMP22);
        vis_mul8x16(CONST_128, TMP20, TMP20);

        vis_and(TMP22, MASK_fe, TMP22);

        vis_and(DST_0, REF_0, TMP24);
        vis_mul8x16(CONST_128, TMP22, TMP22);

        vis_and(DST_2, REF_2, TMP26);

        vis_and(TMP20, MASK_7f, TMP20);

        vis_and(TMP22, MASK_7f, TMP22);

        vis_padd16(TMP24, TMP20, TMP20);
        vis_st64(TMP20, dest[0]);

        vis_padd16(TMP26, TMP22, TMP22);
        vis_st64_2(TMP22, dest, 8);
}

static void MC_avg_no_round_o_8_vis (uint8_t * dest, const uint8_t * ref,
                            const int stride, int height)
{
        ref = vis_alignaddr(ref);

        vis_ld64(ref[0], TMP0);

        vis_ld64(ref[8], TMP2);

        vis_ld64(dest[0], DST_0);

        vis_ld64(constants_fe[0], MASK_fe);

        vis_ld64(constants_7f[0], MASK_7f);
        vis_faligndata(TMP0, TMP2, REF_0);

        vis_ld64(constants128[0], CONST_128);

        ref += stride;
        height = (height >> 1) - 1;

        do {    /* 12 cycles */
                vis_ld64(ref[0], TMP0);
                vis_xor(DST_0, REF_0, TMP4);

                vis_ld64(ref[8], TMP2);
                vis_and(TMP4, MASK_fe, TMP4);

                vis_and(DST_0, REF_0, TMP6);
                vis_ld64_2(dest, stride, DST_0);
                ref += stride;
                vis_mul8x16(CONST_128, TMP4, TMP4);

                vis_ld64(ref[0], TMP12);
                vis_faligndata(TMP0, TMP2, REF_0);

                vis_ld64(ref[8], TMP2);
                vis_xor(DST_0, REF_0, TMP0);
                ref += stride;

                vis_and(TMP0, MASK_fe, TMP0);

                vis_and(TMP4, MASK_7f, TMP4);

                vis_padd16(TMP6, TMP4, TMP4);
                vis_st64(TMP4, dest[0]);
                dest += stride;
                vis_mul8x16(CONST_128, TMP0, TMP0);

                vis_and(DST_0, REF_0, TMP6);
                vis_ld64_2(dest, stride, DST_0);

                vis_faligndata(TMP12, TMP2, REF_0);

                vis_and(TMP0, MASK_7f, TMP0);

                vis_padd16(TMP6, TMP0, TMP4);
                vis_st64(TMP4, dest[0]);
                dest += stride;
        } while (--height);

        vis_ld64(ref[0], TMP0);
        vis_xor(DST_0, REF_0, TMP4);

        vis_ld64(ref[8], TMP2);
        vis_and(TMP4, MASK_fe, TMP4);

        vis_and(DST_0, REF_0, TMP6);
        vis_ld64_2(dest, stride, DST_0);
        vis_mul8x16(CONST_128, TMP4, TMP4);

        vis_faligndata(TMP0, TMP2, REF_0);

        vis_xor(DST_0, REF_0, TMP0);

        vis_and(TMP0, MASK_fe, TMP0);

        vis_and(TMP4, MASK_7f, TMP4);

        vis_padd16(TMP6, TMP4, TMP4);
        vis_st64(TMP4, dest[0]);
        dest += stride;
        vis_mul8x16(CONST_128, TMP0, TMP0);

        vis_and(DST_0, REF_0, TMP6);

        vis_and(TMP0, MASK_7f, TMP0);

        vis_padd16(TMP6, TMP0, TMP4);
        vis_st64(TMP4, dest[0]);
}

static void MC_put_no_round_x_16_vis (uint8_t * dest, const uint8_t * ref,
                             const int stride, int height)
{
        unsigned long off = (unsigned long) ref & 0x7;
        unsigned long off_plus_1 = off + 1;

        ref = vis_alignaddr(ref);

        vis_ld64(ref[0],    TMP0);

        vis_ld64_2(ref, 8,  TMP2);

        vis_ld64_2(ref, 16, TMP4);

        vis_ld64(constants_fe[0], MASK_fe);

        vis_ld64(constants_7f[0], MASK_7f);
        vis_faligndata(TMP0, TMP2, REF_0);

        vis_ld64(constants128[0], CONST_128);
        vis_faligndata(TMP2, TMP4, REF_4);

        if (off != 0x7) {
                vis_alignaddr_g0((void *)off_plus_1);
                vis_faligndata(TMP0, TMP2, REF_2);
                vis_faligndata(TMP2, TMP4, REF_6);
        } else {
                vis_src1(TMP2, REF_2);
                vis_src1(TMP4, REF_6);
        }

        ref += stride;
        height = (height >> 1) - 1;

        do {    /* 34 cycles */
                vis_ld64(ref[0],    TMP0);
                vis_xor(REF_0, REF_2, TMP6);

                vis_ld64_2(ref, 8,  TMP2);
                vis_xor(REF_4, REF_6, TMP8);

                vis_ld64_2(ref, 16, TMP4);
                vis_and(TMP6, MASK_fe, TMP6);
                ref += stride;

                vis_ld64(ref[0],    TMP14);
                vis_mul8x16(CONST_128, TMP6, TMP6);
                vis_and(TMP8, MASK_fe, TMP8);

                vis_ld64_2(ref, 8,  TMP16);
                vis_mul8x16(CONST_128, TMP8, TMP8);
                vis_and(REF_0, REF_2, TMP10);

                vis_ld64_2(ref, 16, TMP18);
                ref += stride;
                vis_and(REF_4, REF_6, TMP12);

                vis_alignaddr_g0((void *)off);

                vis_faligndata(TMP0, TMP2, REF_0);

                vis_faligndata(TMP2, TMP4, REF_4);

                if (off != 0x7) {
                        vis_alignaddr_g0((void *)off_plus_1);
                        vis_faligndata(TMP0, TMP2, REF_2);
                        vis_faligndata(TMP2, TMP4, REF_6);
                } else {
                        vis_src1(TMP2, REF_2);
                        vis_src1(TMP4, REF_6);
                }

                vis_and(TMP6, MASK_7f, TMP6);

                vis_and(TMP8, MASK_7f, TMP8);

                vis_padd16(TMP10, TMP6, TMP6);
                vis_st64(TMP6, dest[0]);

                vis_padd16(TMP12, TMP8, TMP8);
                vis_st64_2(TMP8, dest, 8);
                dest += stride;

                vis_xor(REF_0, REF_2, TMP6);

                vis_xor(REF_4, REF_6, TMP8);

                vis_and(TMP6, MASK_fe, TMP6);

                vis_mul8x16(CONST_128, TMP6, TMP6);
                vis_and(TMP8, MASK_fe, TMP8);

                vis_mul8x16(CONST_128, TMP8, TMP8);
                vis_and(REF_0, REF_2, TMP10);

                vis_and(REF_4, REF_6, TMP12);

                vis_alignaddr_g0((void *)off);

                vis_faligndata(TMP14, TMP16, REF_0);

                vis_faligndata(TMP16, TMP18, REF_4);

                if (off != 0x7) {
                        vis_alignaddr_g0((void *)off_plus_1);
                        vis_faligndata(TMP14, TMP16, REF_2);
                        vis_faligndata(TMP16, TMP18, REF_6);
                } else {
                        vis_src1(TMP16, REF_2);
                        vis_src1(TMP18, REF_6);
                }

                vis_and(TMP6, MASK_7f, TMP6);

                vis_and(TMP8, MASK_7f, TMP8);

                vis_padd16(TMP10, TMP6, TMP6);
                vis_st64(TMP6, dest[0]);

                vis_padd16(TMP12, TMP8, TMP8);
                vis_st64_2(TMP8, dest, 8);
                dest += stride;
        } while (--height);

        vis_ld64(ref[0],    TMP0);
        vis_xor(REF_0, REF_2, TMP6);

        vis_ld64_2(ref, 8,  TMP2);
        vis_xor(REF_4, REF_6, TMP8);

        vis_ld64_2(ref, 16, TMP4);
        vis_and(TMP6, MASK_fe, TMP6);

        vis_mul8x16(CONST_128, TMP6, TMP6);
        vis_and(TMP8, MASK_fe, TMP8);

        vis_mul8x16(CONST_128, TMP8, TMP8);
        vis_and(REF_0, REF_2, TMP10);

        vis_and(REF_4, REF_6, TMP12);

        vis_alignaddr_g0((void *)off);

        vis_faligndata(TMP0, TMP2, REF_0);

        vis_faligndata(TMP2, TMP4, REF_4);

        if (off != 0x7) {
                vis_alignaddr_g0((void *)off_plus_1);
                vis_faligndata(TMP0, TMP2, REF_2);
                vis_faligndata(TMP2, TMP4, REF_6);
        } else {
                vis_src1(TMP2, REF_2);
                vis_src1(TMP4, REF_6);
        }

        vis_and(TMP6, MASK_7f, TMP6);

        vis_and(TMP8, MASK_7f, TMP8);

        vis_padd16(TMP10, TMP6, TMP6);
        vis_st64(TMP6, dest[0]);

        vis_padd16(TMP12, TMP8, TMP8);
        vis_st64_2(TMP8, dest, 8);
        dest += stride;

        vis_xor(REF_0, REF_2, TMP6);

        vis_xor(REF_4, REF_6, TMP8);

        vis_and(TMP6, MASK_fe, TMP6);

        vis_mul8x16(CONST_128, TMP6, TMP6);
        vis_and(TMP8, MASK_fe, TMP8);

        vis_mul8x16(CONST_128, TMP8, TMP8);
        vis_and(REF_0, REF_2, TMP10);

        vis_and(REF_4, REF_6, TMP12);

        vis_and(TMP6, MASK_7f, TMP6);

        vis_and(TMP8, MASK_7f, TMP8);

        vis_padd16(TMP10, TMP6, TMP6);
        vis_st64(TMP6, dest[0]);

        vis_padd16(TMP12, TMP8, TMP8);
        vis_st64_2(TMP8, dest, 8);
}

static void MC_put_no_round_x_8_vis (uint8_t * dest, const uint8_t * ref,
                            const int stride, int height)
{
        unsigned long off = (unsigned long) ref & 0x7;
        unsigned long off_plus_1 = off + 1;

        ref = vis_alignaddr(ref);

        vis_ld64(ref[0], TMP0);

        vis_ld64(ref[8], TMP2);

        vis_ld64(constants_fe[0], MASK_fe);

        vis_ld64(constants_7f[0], MASK_7f);

        vis_ld64(constants128[0], CONST_128);
        vis_faligndata(TMP0, TMP2, REF_0);

        if (off != 0x7) {
                vis_alignaddr_g0((void *)off_plus_1);
                vis_faligndata(TMP0, TMP2, REF_2);
        } else {
                vis_src1(TMP2, REF_2);
        }

        ref += stride;
        height = (height >> 1) - 1;

        do {    /* 20 cycles */
                vis_ld64(ref[0], TMP0);
                vis_xor(REF_0, REF_2, TMP4);

                vis_ld64_2(ref, 8, TMP2);
                vis_and(TMP4, MASK_fe, TMP4);
                ref += stride;

                vis_ld64(ref[0], TMP8);
                vis_and(REF_0, REF_2, TMP6);
                vis_mul8x16(CONST_128, TMP4, TMP4);

                vis_alignaddr_g0((void *)off);

                vis_ld64_2(ref, 8, TMP10);
                ref += stride;
                vis_faligndata(TMP0, TMP2, REF_0);

                if (off != 0x7) {
                        vis_alignaddr_g0((void *)off_plus_1);
                        vis_faligndata(TMP0, TMP2, REF_2);
                } else {
                        vis_src1(TMP2, REF_2);
                }

                vis_and(TMP4, MASK_7f, TMP4);

                vis_padd16(TMP6, TMP4, DST_0);
                vis_st64(DST_0, dest[0]);
                dest += stride;

                vis_xor(REF_0, REF_2, TMP12);

                vis_and(TMP12, MASK_fe, TMP12);

                vis_and(REF_0, REF_2, TMP14);
                vis_mul8x16(CONST_128, TMP12, TMP12);

                vis_alignaddr_g0((void *)off);
                vis_faligndata(TMP8, TMP10, REF_0);
                if (off != 0x7) {
                        vis_alignaddr_g0((void *)off_plus_1);
                        vis_faligndata(TMP8, TMP10, REF_2);
                } else {
                        vis_src1(TMP10, REF_2);
                }

                vis_and(TMP12, MASK_7f, TMP12);

                vis_padd16(TMP14, TMP12, DST_0);
                vis_st64(DST_0, dest[0]);
                dest += stride;
        } while (--height);

        vis_ld64(ref[0], TMP0);
        vis_xor(REF_0, REF_2, TMP4);

        vis_ld64_2(ref, 8, TMP2);
        vis_and(TMP4, MASK_fe, TMP4);

        vis_and(REF_0, REF_2, TMP6);
        vis_mul8x16(CONST_128, TMP4, TMP4);

        vis_alignaddr_g0((void *)off);

        vis_faligndata(TMP0, TMP2, REF_0);

        if (off != 0x7) {
                vis_alignaddr_g0((void *)off_plus_1);
                vis_faligndata(TMP0, TMP2, REF_2);
        } else {
                vis_src1(TMP2, REF_2);
        }

        vis_and(TMP4, MASK_7f, TMP4);

        vis_padd16(TMP6, TMP4, DST_0);
        vis_st64(DST_0, dest[0]);
        dest += stride;

        vis_xor(REF_0, REF_2, TMP12);

        vis_and(TMP12, MASK_fe, TMP12);

        vis_and(REF_0, REF_2, TMP14);
        vis_mul8x16(CONST_128, TMP12, TMP12);

        vis_and(TMP12, MASK_7f, TMP12);

        vis_padd16(TMP14, TMP12, DST_0);
        vis_st64(DST_0, dest[0]);
        dest += stride;
}

static void MC_avg_no_round_x_16_vis (uint8_t * dest, const uint8_t * ref,
                             const int stride, int height)
{
        unsigned long off = (unsigned long) ref & 0x7;
        unsigned long off_plus_1 = off + 1;

        vis_set_gsr(5 << VIS_GSR_SCALEFACT_SHIFT);

        vis_ld64(constants3[0], CONST_3);
        vis_fzero(ZERO);
        vis_ld64(constants256_512[0], CONST_256);

        ref = vis_alignaddr(ref);
        do {    /* 26 cycles */
                vis_ld64(ref[0], TMP0);

                vis_ld64(ref[8], TMP2);

                vis_alignaddr_g0((void *)off);

                vis_ld64(ref[16], TMP4);

                vis_ld64(dest[0], DST_0);
                vis_faligndata(TMP0, TMP2, REF_0);

                vis_ld64(dest[8], DST_2);
                vis_faligndata(TMP2, TMP4, REF_4);

                if (off != 0x7) {
                        vis_alignaddr_g0((void *)off_plus_1);
                        vis_faligndata(TMP0, TMP2, REF_2);
                        vis_faligndata(TMP2, TMP4, REF_6);
                } else {
                        vis_src1(TMP2, REF_2);
                        vis_src1(TMP4, REF_6);
                }

                vis_mul8x16au(REF_0,   CONST_256, TMP0);

                vis_pmerge(ZERO,     REF_2,     TMP4);
                vis_mul8x16au(REF_0_1, CONST_256, TMP2);

                vis_pmerge(ZERO, REF_2_1, TMP6);

                vis_padd16(TMP0, TMP4, TMP0);

                vis_mul8x16al(DST_0,   CONST_512, TMP4);
                vis_padd16(TMP2, TMP6, TMP2);

                vis_mul8x16al(DST_1,   CONST_512, TMP6);

                vis_mul8x16au(REF_6,   CONST_256, TMP12);

                vis_padd16(TMP0, TMP4, TMP0);
                vis_mul8x16au(REF_6_1, CONST_256, TMP14);

                vis_padd16(TMP2, TMP6, TMP2);
                vis_mul8x16au(REF_4,   CONST_256, TMP16);

                vis_padd16(TMP0, CONST_3, TMP8);
                vis_mul8x16au(REF_4_1, CONST_256, TMP18);

                vis_padd16(TMP2, CONST_3, TMP10);
                vis_pack16(TMP8, DST_0);

                vis_pack16(TMP10, DST_1);
                vis_padd16(TMP16, TMP12, TMP0);

                vis_st64(DST_0, dest[0]);
                vis_mul8x16al(DST_2,   CONST_512, TMP4);
                vis_padd16(TMP18, TMP14, TMP2);

                vis_mul8x16al(DST_3,   CONST_512, TMP6);
                vis_padd16(TMP0, CONST_3, TMP0);

                vis_padd16(TMP2, CONST_3, TMP2);

                vis_padd16(TMP0, TMP4, TMP0);

                vis_padd16(TMP2, TMP6, TMP2);
                vis_pack16(TMP0, DST_2);

                vis_pack16(TMP2, DST_3);
                vis_st64(DST_2, dest[8]);

                ref += stride;
                dest += stride;
        } while (--height);
}

static void MC_avg_no_round_x_8_vis (uint8_t * dest, const uint8_t * ref,
                            const int stride, int height)
{
        unsigned long off = (unsigned long) ref & 0x7;
        unsigned long off_plus_1 = off + 1;
        int stride_times_2 = stride << 1;

        vis_set_gsr(5 << VIS_GSR_SCALEFACT_SHIFT);

        vis_ld64(constants3[0], CONST_3);
        vis_fzero(ZERO);
        vis_ld64(constants256_512[0], CONST_256);

        ref = vis_alignaddr(ref);
        height >>= 2;
        do {    /* 47 cycles */
                vis_ld64(ref[0],   TMP0);

                vis_ld64_2(ref, 8, TMP2);
                ref += stride;

                vis_alignaddr_g0((void *)off);

                vis_ld64(ref[0],   TMP4);
                vis_faligndata(TMP0, TMP2, REF_0);

                vis_ld64_2(ref, 8, TMP6);
                ref += stride;

                vis_ld64(ref[0],   TMP8);

                vis_ld64_2(ref, 8, TMP10);
                ref += stride;
                vis_faligndata(TMP4, TMP6, REF_4);

                vis_ld64(ref[0],   TMP12);

                vis_ld64_2(ref, 8, TMP14);
                ref += stride;
                vis_faligndata(TMP8, TMP10, REF_S0);

                vis_faligndata(TMP12, TMP14, REF_S4);

                if (off != 0x7) {
                        vis_alignaddr_g0((void *)off_plus_1);

                        vis_ld64(dest[0], DST_0);
                        vis_faligndata(TMP0, TMP2, REF_2);

                        vis_ld64_2(dest, stride, DST_2);
                        vis_faligndata(TMP4, TMP6, REF_6);

                        vis_faligndata(TMP8, TMP10, REF_S2);

                        vis_faligndata(TMP12, TMP14, REF_S6);
                } else {
                        vis_ld64(dest[0], DST_0);
                        vis_src1(TMP2, REF_2);

                        vis_ld64_2(dest, stride, DST_2);
                        vis_src1(TMP6, REF_6);

                        vis_src1(TMP10, REF_S2);

                        vis_src1(TMP14, REF_S6);
                }

                vis_pmerge(ZERO,     REF_0,     TMP0);
                vis_mul8x16au(REF_0_1, CONST_256, TMP2);

                vis_pmerge(ZERO,     REF_2,     TMP4);
                vis_mul8x16au(REF_2_1, CONST_256, TMP6);

                vis_padd16(TMP0, CONST_3, TMP0);
                vis_mul8x16al(DST_0,   CONST_512, TMP16);

                vis_padd16(TMP2, CONST_3, TMP2);
                vis_mul8x16al(DST_1,   CONST_512, TMP18);

                vis_padd16(TMP0, TMP4, TMP0);
                vis_mul8x16au(REF_4, CONST_256, TMP8);

                vis_padd16(TMP2, TMP6, TMP2);
                vis_mul8x16au(REF_4_1, CONST_256, TMP10);

                vis_padd16(TMP0, TMP16, TMP0);
                vis_mul8x16au(REF_6, CONST_256, TMP12);

                vis_padd16(TMP2, TMP18, TMP2);
                vis_mul8x16au(REF_6_1, CONST_256, TMP14);

                vis_padd16(TMP8, CONST_3, TMP8);
                vis_mul8x16al(DST_2, CONST_512, TMP16);

                vis_padd16(TMP8, TMP12, TMP8);
                vis_mul8x16al(DST_3, CONST_512, TMP18);

                vis_padd16(TMP10, TMP14, TMP10);
                vis_pack16(TMP0, DST_0);

                vis_pack16(TMP2, DST_1);
                vis_st64(DST_0, dest[0]);
                dest += stride;
                vis_padd16(TMP10, CONST_3, TMP10);

                vis_ld64_2(dest, stride, DST_0);
                vis_padd16(TMP8, TMP16, TMP8);

                vis_ld64_2(dest, stride_times_2, TMP4/*DST_2*/);
                vis_padd16(TMP10, TMP18, TMP10);
                vis_pack16(TMP8, DST_2);

                vis_pack16(TMP10, DST_3);
                vis_st64(DST_2, dest[0]);
                dest += stride;

                vis_mul8x16au(REF_S0_1, CONST_256, TMP2);
                vis_pmerge(ZERO,     REF_S0,     TMP0);

                vis_pmerge(ZERO,     REF_S2,     TMP24);
                vis_mul8x16au(REF_S2_1, CONST_256, TMP6);

                vis_padd16(TMP0, CONST_3, TMP0);
                vis_mul8x16au(REF_S4, CONST_256, TMP8);

                vis_padd16(TMP2, CONST_3, TMP2);
                vis_mul8x16au(REF_S4_1, CONST_256, TMP10);

                vis_padd16(TMP0, TMP24, TMP0);
                vis_mul8x16au(REF_S6, CONST_256, TMP12);

                vis_padd16(TMP2, TMP6, TMP2);
                vis_mul8x16au(REF_S6_1, CONST_256, TMP14);

                vis_padd16(TMP8, CONST_3, TMP8);
                vis_mul8x16al(DST_0,   CONST_512, TMP16);

                vis_padd16(TMP10, CONST_3, TMP10);
                vis_mul8x16al(DST_1,   CONST_512, TMP18);

                vis_padd16(TMP8, TMP12, TMP8);
                vis_mul8x16al(TMP4/*DST_2*/, CONST_512, TMP20);

                vis_mul8x16al(TMP5/*DST_3*/, CONST_512, TMP22);
                vis_padd16(TMP0, TMP16, TMP0);

                vis_padd16(TMP2, TMP18, TMP2);
                vis_pack16(TMP0, DST_0);

                vis_padd16(TMP10, TMP14, TMP10);
                vis_pack16(TMP2, DST_1);
                vis_st64(DST_0, dest[0]);
                dest += stride;

                vis_padd16(TMP8, TMP20, TMP8);

                vis_padd16(TMP10, TMP22, TMP10);
                vis_pack16(TMP8, DST_2);

                vis_pack16(TMP10, DST_3);
                vis_st64(DST_2, dest[0]);
                dest += stride;
        } while (--height);
}

static void MC_put_no_round_y_16_vis (uint8_t * dest, const uint8_t * ref,
                             const int stride, int height)
{
        ref = vis_alignaddr(ref);
        vis_ld64(ref[0], TMP0);

        vis_ld64_2(ref, 8, TMP2);

        vis_ld64_2(ref, 16, TMP4);
        ref += stride;

        vis_ld64(ref[0], TMP6);
        vis_faligndata(TMP0, TMP2, REF_0);

        vis_ld64_2(ref, 8, TMP8);
        vis_faligndata(TMP2, TMP4, REF_4);

        vis_ld64_2(ref, 16, TMP10);
        ref += stride;

        vis_ld64(constants_fe[0], MASK_fe);
        vis_faligndata(TMP6, TMP8, REF_2);

        vis_ld64(constants_7f[0], MASK_7f);
        vis_faligndata(TMP8, TMP10, REF_6);

        vis_ld64(constants128[0], CONST_128);
        height = (height >> 1) - 1;
        do {    /* 24 cycles */
                vis_ld64(ref[0], TMP0);
                vis_xor(REF_0, REF_2, TMP12);

                vis_ld64_2(ref, 8, TMP2);
                vis_xor(REF_4, REF_6, TMP16);

                vis_ld64_2(ref, 16, TMP4);
                ref += stride;
                vis_and(REF_0, REF_2, TMP14);

                vis_ld64(ref[0], TMP6);
                vis_and(REF_4, REF_6, TMP18);

                vis_ld64_2(ref, 8, TMP8);
                vis_faligndata(TMP0, TMP2, REF_0);

                vis_ld64_2(ref, 16, TMP10);
                ref += stride;
                vis_faligndata(TMP2, TMP4, REF_4);

                vis_and(TMP12, MASK_fe, TMP12);

                vis_and(TMP16, MASK_fe, TMP16);
                vis_mul8x16(CONST_128, TMP12, TMP12);

                vis_mul8x16(CONST_128, TMP16, TMP16);
                vis_xor(REF_0, REF_2, TMP0);

                vis_xor(REF_4, REF_6, TMP2);

                vis_and(REF_0, REF_2, TMP20);

                vis_and(TMP12, MASK_7f, TMP12);

                vis_and(TMP16, MASK_7f, TMP16);

                vis_padd16(TMP14, TMP12, TMP12);
                vis_st64(TMP12, dest[0]);

                vis_padd16(TMP18, TMP16, TMP16);
                vis_st64_2(TMP16, dest, 8);
                dest += stride;

                vis_and(REF_4, REF_6, TMP18);

                vis_and(TMP0, MASK_fe, TMP0);

                vis_and(TMP2, MASK_fe, TMP2);
                vis_mul8x16(CONST_128, TMP0, TMP0);

                vis_faligndata(TMP6, TMP8, REF_2);
                vis_mul8x16(CONST_128, TMP2, TMP2);

                vis_faligndata(TMP8, TMP10, REF_6);

                vis_and(TMP0, MASK_7f, TMP0);

                vis_and(TMP2, MASK_7f, TMP2);

                vis_padd16(TMP20, TMP0, TMP0);
                vis_st64(TMP0, dest[0]);

                vis_padd16(TMP18, TMP2, TMP2);
                vis_st64_2(TMP2, dest, 8);
                dest += stride;
        } while (--height);

        vis_ld64(ref[0], TMP0);
        vis_xor(REF_0, REF_2, TMP12);

        vis_ld64_2(ref, 8, TMP2);
        vis_xor(REF_4, REF_6, TMP16);

        vis_ld64_2(ref, 16, TMP4);
        vis_and(REF_0, REF_2, TMP14);

        vis_and(REF_4, REF_6, TMP18);

        vis_faligndata(TMP0, TMP2, REF_0);

        vis_faligndata(TMP2, TMP4, REF_4);

        vis_and(TMP12, MASK_fe, TMP12);

        vis_and(TMP16, MASK_fe, TMP16);
        vis_mul8x16(CONST_128, TMP12, TMP12);

        vis_mul8x16(CONST_128, TMP16, TMP16);
        vis_xor(REF_0, REF_2, TMP0);

        vis_xor(REF_4, REF_6, TMP2);

        vis_and(REF_0, REF_2, TMP20);

        vis_and(TMP12, MASK_7f, TMP12);

        vis_and(TMP16, MASK_7f, TMP16);

        vis_padd16(TMP14, TMP12, TMP12);
        vis_st64(TMP12, dest[0]);

        vis_padd16(TMP18, TMP16, TMP16);
        vis_st64_2(TMP16, dest, 8);
        dest += stride;

        vis_and(REF_4, REF_6, TMP18);

        vis_and(TMP0, MASK_fe, TMP0);

        vis_and(TMP2, MASK_fe, TMP2);
        vis_mul8x16(CONST_128, TMP0, TMP0);

        vis_mul8x16(CONST_128, TMP2, TMP2);

        vis_and(TMP0, MASK_7f, TMP0);

        vis_and(TMP2, MASK_7f, TMP2);

        vis_padd16(TMP20, TMP0, TMP0);
        vis_st64(TMP0, dest[0]);

        vis_padd16(TMP18, TMP2, TMP2);
        vis_st64_2(TMP2, dest, 8);
}

static void MC_put_no_round_y_8_vis (uint8_t * dest, const uint8_t * ref,
                            const int stride, int height)
{
        ref = vis_alignaddr(ref);
        vis_ld64(ref[0], TMP0);

        vis_ld64_2(ref, 8, TMP2);
        ref += stride;

        vis_ld64(ref[0], TMP4);

        vis_ld64_2(ref, 8, TMP6);
        ref += stride;

        vis_ld64(constants_fe[0], MASK_fe);
        vis_faligndata(TMP0, TMP2, REF_0);

        vis_ld64(constants_7f[0], MASK_7f);
        vis_faligndata(TMP4, TMP6, REF_2);

        vis_ld64(constants128[0], CONST_128);
        height = (height >> 1) - 1;
        do {    /* 12 cycles */
                vis_ld64(ref[0], TMP0);
                vis_xor(REF_0, REF_2, TMP4);

                vis_ld64_2(ref, 8, TMP2);
                ref += stride;
                vis_and(TMP4, MASK_fe, TMP4);

                vis_and(REF_0, REF_2, TMP6);
                vis_mul8x16(CONST_128, TMP4, TMP4);

                vis_faligndata(TMP0, TMP2, REF_0);
                vis_ld64(ref[0], TMP0);

                vis_ld64_2(ref, 8, TMP2);
                ref += stride;
                vis_xor(REF_0, REF_2, TMP12);

                vis_and(TMP4, MASK_7f, TMP4);

                vis_and(TMP12, MASK_fe, TMP12);

                vis_mul8x16(CONST_128, TMP12, TMP12);
                vis_and(REF_0, REF_2, TMP14);

                vis_padd16(TMP6, TMP4, DST_0);
                vis_st64(DST_0, dest[0]);
                dest += stride;

                vis_faligndata(TMP0, TMP2, REF_2);

                vis_and(TMP12, MASK_7f, TMP12);

                vis_padd16(TMP14, TMP12, DST_0);
                vis_st64(DST_0, dest[0]);
                dest += stride;
        } while (--height);

        vis_ld64(ref[0], TMP0);
        vis_xor(REF_0, REF_2, TMP4);

        vis_ld64_2(ref, 8, TMP2);
        vis_and(TMP4, MASK_fe, TMP4);

        vis_and(REF_0, REF_2, TMP6);
        vis_mul8x16(CONST_128, TMP4, TMP4);

        vis_faligndata(TMP0, TMP2, REF_0);

        vis_xor(REF_0, REF_2, TMP12);

        vis_and(TMP4, MASK_7f, TMP4);

        vis_and(TMP12, MASK_fe, TMP12);

        vis_mul8x16(CONST_128, TMP12, TMP12);
        vis_and(REF_0, REF_2, TMP14);

        vis_padd16(TMP6, TMP4, DST_0);
        vis_st64(DST_0, dest[0]);
        dest += stride;

        vis_and(TMP12, MASK_7f, TMP12);

        vis_padd16(TMP14, TMP12, DST_0);
        vis_st64(DST_0, dest[0]);
}

static void MC_avg_no_round_y_16_vis (uint8_t * dest, const uint8_t * ref,
                             const int stride, int height)
{
        int stride_8 = stride + 8;
        int stride_16 = stride + 16;

        vis_set_gsr(5 << VIS_GSR_SCALEFACT_SHIFT);

        ref = vis_alignaddr(ref);

        vis_ld64(ref[ 0], TMP0);
        vis_fzero(ZERO);

        vis_ld64(ref[ 8], TMP2);

        vis_ld64(ref[16], TMP4);

        vis_ld64(constants3[0], CONST_3);
        vis_faligndata(TMP0, TMP2, REF_2);

        vis_ld64(constants256_512[0], CONST_256);
        vis_faligndata(TMP2, TMP4, REF_6);
        height >>= 1;

        do {    /* 31 cycles */
                vis_ld64_2(ref, stride, TMP0);
                vis_pmerge(ZERO,       REF_2,     TMP12);
                vis_mul8x16au(REF_2_1, CONST_256, TMP14);

                vis_ld64_2(ref, stride_8, TMP2);
                vis_pmerge(ZERO,       REF_6,     TMP16);
                vis_mul8x16au(REF_6_1, CONST_256, TMP18);

                vis_ld64_2(ref, stride_16, TMP4);
                ref += stride;

                vis_ld64(dest[0], DST_0);
                vis_faligndata(TMP0, TMP2, REF_0);

                vis_ld64_2(dest, 8, DST_2);
                vis_faligndata(TMP2, TMP4, REF_4);

                vis_ld64_2(ref, stride, TMP6);
                vis_pmerge(ZERO,     REF_0,     TMP0);
                vis_mul8x16au(REF_0_1, CONST_256, TMP2);

                vis_ld64_2(ref, stride_8, TMP8);
                vis_pmerge(ZERO,     REF_4,     TMP4);

                vis_ld64_2(ref, stride_16, TMP10);
                ref += stride;

                vis_ld64_2(dest, stride, REF_S0/*DST_4*/);
                vis_faligndata(TMP6, TMP8, REF_2);
                vis_mul8x16au(REF_4_1, CONST_256, TMP6);

                vis_ld64_2(dest, stride_8, REF_S2/*DST_6*/);
                vis_faligndata(TMP8, TMP10, REF_6);
                vis_mul8x16al(DST_0,   CONST_512, TMP20);

                vis_padd16(TMP0, CONST_3, TMP0);
                vis_mul8x16al(DST_1,   CONST_512, TMP22);

                vis_padd16(TMP2, CONST_3, TMP2);
                vis_mul8x16al(DST_2,   CONST_512, TMP24);

                vis_padd16(TMP4, CONST_3, TMP4);
                vis_mul8x16al(DST_3,   CONST_512, TMP26);

                vis_padd16(TMP6, CONST_3, TMP6);

                vis_padd16(TMP12, TMP20, TMP12);
                vis_mul8x16al(REF_S0,   CONST_512, TMP20);

                vis_padd16(TMP14, TMP22, TMP14);
                vis_mul8x16al(REF_S0_1, CONST_512, TMP22);

                vis_padd16(TMP16, TMP24, TMP16);
                vis_mul8x16al(REF_S2,   CONST_512, TMP24);

                vis_padd16(TMP18, TMP26, TMP18);
                vis_mul8x16al(REF_S2_1, CONST_512, TMP26);

                vis_padd16(TMP12, TMP0, TMP12);
                vis_mul8x16au(REF_2,   CONST_256, TMP28);

                vis_padd16(TMP14, TMP2, TMP14);
                vis_mul8x16au(REF_2_1, CONST_256, TMP30);

                vis_padd16(TMP16, TMP4, TMP16);
                vis_mul8x16au(REF_6,   CONST_256, REF_S4);

                vis_padd16(TMP18, TMP6, TMP18);
                vis_mul8x16au(REF_6_1, CONST_256, REF_S6);

                vis_pack16(TMP12, DST_0);
                vis_padd16(TMP28, TMP0, TMP12);

                vis_pack16(TMP14, DST_1);
                vis_st64(DST_0, dest[0]);
                vis_padd16(TMP30, TMP2, TMP14);

                vis_pack16(TMP16, DST_2);
                vis_padd16(REF_S4, TMP4, TMP16);

                vis_pack16(TMP18, DST_3);
                vis_st64_2(DST_2, dest, 8);
                dest += stride;
                vis_padd16(REF_S6, TMP6, TMP18);

                vis_padd16(TMP12, TMP20, TMP12);

                vis_padd16(TMP14, TMP22, TMP14);
                vis_pack16(TMP12, DST_0);

                vis_padd16(TMP16, TMP24, TMP16);
                vis_pack16(TMP14, DST_1);
                vis_st64(DST_0, dest[0]);

                vis_padd16(TMP18, TMP26, TMP18);
                vis_pack16(TMP16, DST_2);

                vis_pack16(TMP18, DST_3);
                vis_st64_2(DST_2, dest, 8);
                dest += stride;
        } while (--height);
}

static void MC_avg_no_round_y_8_vis (uint8_t * dest, const uint8_t * ref,
                            const int stride, int height)
{
        int stride_8 = stride + 8;

        vis_set_gsr(5 << VIS_GSR_SCALEFACT_SHIFT);

        ref = vis_alignaddr(ref);

        vis_ld64(ref[ 0], TMP0);
        vis_fzero(ZERO);

        vis_ld64(ref[ 8], TMP2);

        vis_ld64(constants3[0], CONST_3);
        vis_faligndata(TMP0, TMP2, REF_2);

        vis_ld64(constants256_512[0], CONST_256);

        height >>= 1;
        do {    /* 20 cycles */
                vis_ld64_2(ref, stride, TMP0);
                vis_pmerge(ZERO,       REF_2,     TMP8);
                vis_mul8x16au(REF_2_1, CONST_256, TMP10);

                vis_ld64_2(ref, stride_8, TMP2);
                ref += stride;

                vis_ld64(dest[0], DST_0);

                vis_ld64_2(dest, stride, DST_2);
                vis_faligndata(TMP0, TMP2, REF_0);

                vis_ld64_2(ref, stride, TMP4);
                vis_mul8x16al(DST_0,   CONST_512, TMP16);
                vis_pmerge(ZERO,       REF_0,     TMP12);

                vis_ld64_2(ref, stride_8, TMP6);
                ref += stride;
                vis_mul8x16al(DST_1,   CONST_512, TMP18);
                vis_pmerge(ZERO,       REF_0_1,   TMP14);

                vis_padd16(TMP12, CONST_3, TMP12);
                vis_mul8x16al(DST_2,   CONST_512, TMP24);

                vis_padd16(TMP14, CONST_3, TMP14);
                vis_mul8x16al(DST_3,   CONST_512, TMP26);

                vis_faligndata(TMP4, TMP6, REF_2);

                vis_padd16(TMP8, TMP12, TMP8);

                vis_padd16(TMP10, TMP14, TMP10);
                vis_mul8x16au(REF_2,   CONST_256, TMP20);

                vis_padd16(TMP8, TMP16, TMP0);
                vis_mul8x16au(REF_2_1, CONST_256, TMP22);

                vis_padd16(TMP10, TMP18, TMP2);
                vis_pack16(TMP0, DST_0);

                vis_pack16(TMP2, DST_1);
                vis_st64(DST_0, dest[0]);
                dest += stride;
                vis_padd16(TMP12, TMP20, TMP12);

                vis_padd16(TMP14, TMP22, TMP14);

                vis_padd16(TMP12, TMP24, TMP0);

                vis_padd16(TMP14, TMP26, TMP2);
                vis_pack16(TMP0, DST_2);

                vis_pack16(TMP2, DST_3);
                vis_st64(DST_2, dest[0]);
                dest += stride;
        } while (--height);
}

static void MC_put_no_round_xy_16_vis (uint8_t * dest, const uint8_t * ref,
                                       const int stride, int height)
{
        unsigned long off = (unsigned long) ref & 0x7;
        unsigned long off_plus_1 = off + 1;
        int stride_8 = stride + 8;
        int stride_16 = stride + 16;

        vis_set_gsr(5 << VIS_GSR_SCALEFACT_SHIFT);

        ref = vis_alignaddr(ref);

        vis_ld64(ref[ 0], TMP0);
        vis_fzero(ZERO);

        vis_ld64(ref[ 8], TMP2);

        vis_ld64(ref[16], TMP4);

        vis_ld64(constants1[0], CONST_1);
        vis_faligndata(TMP0, TMP2, REF_S0);

        vis_ld64(constants256_512[0], CONST_256);
        vis_faligndata(TMP2, TMP4, REF_S4);

        if (off != 0x7) {
                vis_alignaddr_g0((void *)off_plus_1);
                vis_faligndata(TMP0, TMP2, REF_S2);
                vis_faligndata(TMP2, TMP4, REF_S6);
        } else {
                vis_src1(TMP2, REF_S2);
                vis_src1(TMP4, REF_S6);
        }

        height >>= 1;
        do {
                vis_ld64_2(ref, stride, TMP0);
                vis_mul8x16au(REF_S0, CONST_256, TMP12);
                vis_pmerge(ZERO,      REF_S0_1,  TMP14);

                vis_alignaddr_g0((void *)off);

                vis_ld64_2(ref, stride_8, TMP2);
                vis_mul8x16au(REF_S2, CONST_256, TMP16);
                vis_pmerge(ZERO,      REF_S2_1,  TMP18);

                vis_ld64_2(ref, stride_16, TMP4);
                ref += stride;
                vis_mul8x16au(REF_S4, CONST_256, TMP20);
                vis_pmerge(ZERO,      REF_S4_1,  TMP22);

                vis_ld64_2(ref, stride, TMP6);
                vis_mul8x16au(REF_S6, CONST_256, TMP24);
                vis_pmerge(ZERO,      REF_S6_1,  TMP26);

                vis_ld64_2(ref, stride_8, TMP8);
                vis_faligndata(TMP0, TMP2, REF_0);

                vis_ld64_2(ref, stride_16, TMP10);
                ref += stride;
                vis_faligndata(TMP2, TMP4, REF_4);

                vis_faligndata(TMP6, TMP8, REF_S0);

                vis_faligndata(TMP8, TMP10, REF_S4);

                if (off != 0x7) {
                        vis_alignaddr_g0((void *)off_plus_1);
                        vis_faligndata(TMP0, TMP2, REF_2);
                        vis_faligndata(TMP2, TMP4, REF_6);
                        vis_faligndata(TMP6, TMP8, REF_S2);
                        vis_faligndata(TMP8, TMP10, REF_S6);
                } else {
                        vis_src1(TMP2, REF_2);
                        vis_src1(TMP4, REF_6);
                        vis_src1(TMP8, REF_S2);
                        vis_src1(TMP10, REF_S6);
                }

                vis_mul8x16au(REF_0, CONST_256, TMP0);
                vis_pmerge(ZERO,      REF_0_1,  TMP2);

                vis_mul8x16au(REF_2, CONST_256, TMP4);
                vis_pmerge(ZERO,      REF_2_1,  TMP6);

                vis_padd16(TMP0, CONST_2, TMP8);
                vis_mul8x16au(REF_4, CONST_256, TMP0);

                vis_padd16(TMP2, CONST_1, TMP10);
                vis_mul8x16au(REF_4_1, CONST_256, TMP2);

                vis_padd16(TMP8, TMP4, TMP8);
                vis_mul8x16au(REF_6, CONST_256, TMP4);

                vis_padd16(TMP10, TMP6, TMP10);
                vis_mul8x16au(REF_6_1, CONST_256, TMP6);

                vis_padd16(TMP12, TMP8, TMP12);

                vis_padd16(TMP14, TMP10, TMP14);

                vis_padd16(TMP12, TMP16, TMP12);

                vis_padd16(TMP14, TMP18, TMP14);
                vis_pack16(TMP12, DST_0);

                vis_pack16(TMP14, DST_1);
                vis_st64(DST_0, dest[0]);
                vis_padd16(TMP0, CONST_1, TMP12);

                vis_mul8x16au(REF_S0, CONST_256, TMP0);
                vis_padd16(TMP2, CONST_1, TMP14);

                vis_mul8x16au(REF_S0_1, CONST_256, TMP2);
                vis_padd16(TMP12, TMP4, TMP12);

                vis_mul8x16au(REF_S2, CONST_256, TMP4);
                vis_padd16(TMP14, TMP6, TMP14);

                vis_mul8x16au(REF_S2_1, CONST_256, TMP6);
                vis_padd16(TMP20, TMP12, TMP20);

                vis_padd16(TMP22, TMP14, TMP22);

                vis_padd16(TMP20, TMP24, TMP20);

                vis_padd16(TMP22, TMP26, TMP22);
                vis_pack16(TMP20, DST_2);

                vis_pack16(TMP22, DST_3);
                vis_st64_2(DST_2, dest, 8);
                dest += stride;
                vis_padd16(TMP0, TMP4, TMP24);

                vis_mul8x16au(REF_S4, CONST_256, TMP0);
                vis_padd16(TMP2, TMP6, TMP26);

                vis_mul8x16au(REF_S4_1, CONST_256, TMP2);
                vis_padd16(TMP24, TMP8, TMP24);

                vis_padd16(TMP26, TMP10, TMP26);
                vis_pack16(TMP24, DST_0);

                vis_pack16(TMP26, DST_1);
                vis_st64(DST_0, dest[0]);
                vis_pmerge(ZERO, REF_S6, TMP4);

                vis_pmerge(ZERO,      REF_S6_1,  TMP6);

                vis_padd16(TMP0, TMP4, TMP0);

                vis_padd16(TMP2, TMP6, TMP2);

                vis_padd16(TMP0, TMP12, TMP0);

                vis_padd16(TMP2, TMP14, TMP2);
                vis_pack16(TMP0, DST_2);

                vis_pack16(TMP2, DST_3);
                vis_st64_2(DST_2, dest, 8);
                dest += stride;
        } while (--height);
}

static void MC_put_no_round_xy_8_vis (uint8_t * dest, const uint8_t * ref,
                                      const int stride, int height)
{
        unsigned long off = (unsigned long) ref & 0x7;
        unsigned long off_plus_1 = off + 1;
        int stride_8 = stride + 8;

        vis_set_gsr(5 << VIS_GSR_SCALEFACT_SHIFT);

        ref = vis_alignaddr(ref);

        vis_ld64(ref[ 0], TMP0);
        vis_fzero(ZERO);

        vis_ld64(ref[ 8], TMP2);

        vis_ld64(constants1[0], CONST_1);

        vis_ld64(constants256_512[0], CONST_256);
        vis_faligndata(TMP0, TMP2, REF_S0);

        if (off != 0x7) {
                vis_alignaddr_g0((void *)off_plus_1);
                vis_faligndata(TMP0, TMP2, REF_S2);
        } else {
                vis_src1(TMP2, REF_S2);
        }

        height >>= 1;
        do {    /* 26 cycles */
                vis_ld64_2(ref, stride, TMP0);
                vis_mul8x16au(REF_S0,   CONST_256, TMP8);
                vis_pmerge(ZERO,        REF_S2,    TMP12);

                vis_alignaddr_g0((void *)off);

                vis_ld64_2(ref, stride_8, TMP2);
                ref += stride;
                vis_mul8x16au(REF_S0_1, CONST_256, TMP10);
                vis_pmerge(ZERO,        REF_S2_1,  TMP14);

                vis_ld64_2(ref, stride, TMP4);

                vis_ld64_2(ref, stride_8, TMP6);
                ref += stride;
                vis_faligndata(TMP0, TMP2, REF_S4);

                vis_pmerge(ZERO, REF_S4, TMP18);

                vis_pmerge(ZERO, REF_S4_1, TMP20);

                vis_faligndata(TMP4, TMP6, REF_S0);

                if (off != 0x7) {
                        vis_alignaddr_g0((void *)off_plus_1);
                        vis_faligndata(TMP0, TMP2, REF_S6);
                        vis_faligndata(TMP4, TMP6, REF_S2);
                } else {
                        vis_src1(TMP2, REF_S6);
                        vis_src1(TMP6, REF_S2);
                }

                vis_padd16(TMP18, CONST_1, TMP18);
                vis_mul8x16au(REF_S6,   CONST_256, TMP22);

                vis_padd16(TMP20, CONST_1, TMP20);
                vis_mul8x16au(REF_S6_1, CONST_256, TMP24);

                vis_mul8x16au(REF_S0,   CONST_256, TMP26);
                vis_pmerge(ZERO, REF_S0_1, TMP28);

                vis_mul8x16au(REF_S2,   CONST_256, TMP30);
                vis_padd16(TMP18, TMP22, TMP18);

                vis_mul8x16au(REF_S2_1, CONST_256, TMP32);
                vis_padd16(TMP20, TMP24, TMP20);

                vis_padd16(TMP8,  TMP18, TMP8);

                vis_padd16(TMP10, TMP20, TMP10);

                vis_padd16(TMP8,  TMP12, TMP8);

                vis_padd16(TMP10, TMP14, TMP10);
                vis_pack16(TMP8,  DST_0);

                vis_pack16(TMP10, DST_1);
                vis_st64(DST_0, dest[0]);
                dest += stride;
                vis_padd16(TMP18, TMP26, TMP18);

                vis_padd16(TMP20, TMP28, TMP20);

                vis_padd16(TMP18, TMP30, TMP18);

                vis_padd16(TMP20, TMP32, TMP20);
                vis_pack16(TMP18, DST_2);

                vis_pack16(TMP20, DST_3);
                vis_st64(DST_2, dest[0]);
                dest += stride;
        } while (--height);
}

static void MC_avg_no_round_xy_16_vis (uint8_t * dest, const uint8_t * ref,
                                       const int stride, int height)
{
        unsigned long off = (unsigned long) ref & 0x7;
        unsigned long off_plus_1 = off + 1;
        int stride_8 = stride + 8;
        int stride_16 = stride + 16;

        vis_set_gsr(4 << VIS_GSR_SCALEFACT_SHIFT);

        ref = vis_alignaddr(ref);

        vis_ld64(ref[ 0], TMP0);
        vis_fzero(ZERO);

        vis_ld64(ref[ 8], TMP2);

        vis_ld64(ref[16], TMP4);

        vis_ld64(constants6[0], CONST_6);
        vis_faligndata(TMP0, TMP2, REF_S0);

        vis_ld64(constants256_1024[0], CONST_256);
        vis_faligndata(TMP2, TMP4, REF_S4);

        if (off != 0x7) {
                vis_alignaddr_g0((void *)off_plus_1);
                vis_faligndata(TMP0, TMP2, REF_S2);
                vis_faligndata(TMP2, TMP4, REF_S6);
        } else {
                vis_src1(TMP2, REF_S2);
                vis_src1(TMP4, REF_S6);
        }

        height >>= 1;
        do {    /* 55 cycles */
                vis_ld64_2(ref, stride, TMP0);
                vis_mul8x16au(REF_S0, CONST_256, TMP12);
                vis_pmerge(ZERO,      REF_S0_1,  TMP14);

                vis_alignaddr_g0((void *)off);

                vis_ld64_2(ref, stride_8, TMP2);
                vis_mul8x16au(REF_S2, CONST_256, TMP16);
                vis_pmerge(ZERO,      REF_S2_1,  TMP18);

                vis_ld64_2(ref, stride_16, TMP4);
                ref += stride;
                vis_mul8x16au(REF_S4, CONST_256, TMP20);
                vis_pmerge(ZERO,      REF_S4_1,  TMP22);

                vis_ld64_2(ref, stride, TMP6);
                vis_mul8x16au(REF_S6, CONST_256, TMP24);
                vis_pmerge(ZERO,      REF_S6_1,  TMP26);

                vis_ld64_2(ref, stride_8, TMP8);
                vis_faligndata(TMP0, TMP2, REF_0);

                vis_ld64_2(ref, stride_16, TMP10);
                ref += stride;
                vis_faligndata(TMP2, TMP4, REF_4);

                vis_ld64(dest[0], DST_0);
                vis_faligndata(TMP6, TMP8, REF_S0);

                vis_ld64_2(dest, 8, DST_2);
                vis_faligndata(TMP8, TMP10, REF_S4);

                if (off != 0x7) {
                        vis_alignaddr_g0((void *)off_plus_1);
                        vis_faligndata(TMP0, TMP2, REF_2);
                        vis_faligndata(TMP2, TMP4, REF_6);
                        vis_faligndata(TMP6, TMP8, REF_S2);
                        vis_faligndata(TMP8, TMP10, REF_S6);
                } else {
                        vis_src1(TMP2, REF_2);
                        vis_src1(TMP4, REF_6);
                        vis_src1(TMP8, REF_S2);
                        vis_src1(TMP10, REF_S6);
                }

                vis_mul8x16al(DST_0,   CONST_1024, TMP30);
                vis_pmerge(ZERO, REF_0, TMP0);

                vis_mul8x16al(DST_1,   CONST_1024, TMP32);
                vis_pmerge(ZERO,      REF_0_1,  TMP2);

                vis_mul8x16au(REF_2, CONST_256, TMP4);
                vis_pmerge(ZERO,      REF_2_1,  TMP6);

                vis_mul8x16al(DST_2,   CONST_1024, REF_0);
                vis_padd16(TMP0, CONST_6, TMP0);

                vis_mul8x16al(DST_3,   CONST_1024, REF_2);
                vis_padd16(TMP2, CONST_6, TMP2);

                vis_padd16(TMP0, TMP4, TMP0);
                vis_mul8x16au(REF_4, CONST_256, TMP4);

                vis_padd16(TMP2, TMP6, TMP2);
                vis_mul8x16au(REF_4_1, CONST_256, TMP6);

                vis_padd16(TMP12, TMP0, TMP12);
                vis_mul8x16au(REF_6, CONST_256, TMP8);

                vis_padd16(TMP14, TMP2, TMP14);
                vis_mul8x16au(REF_6_1, CONST_256, TMP10);

                vis_padd16(TMP12, TMP16, TMP12);
                vis_mul8x16au(REF_S0, CONST_256, REF_4);

                vis_padd16(TMP14, TMP18, TMP14);
                vis_mul8x16au(REF_S0_1, CONST_256, REF_6);

                vis_padd16(TMP12, TMP30, TMP12);

                vis_padd16(TMP14, TMP32, TMP14);
                vis_pack16(TMP12, DST_0);

                vis_pack16(TMP14, DST_1);
                vis_st64(DST_0, dest[0]);
                vis_padd16(TMP4, CONST_6, TMP4);

                vis_ld64_2(dest, stride, DST_0);
                vis_padd16(TMP6, CONST_6, TMP6);
                vis_mul8x16au(REF_S2, CONST_256, TMP12);

                vis_padd16(TMP4, TMP8, TMP4);
                vis_mul8x16au(REF_S2_1, CONST_256,  TMP14);

                vis_padd16(TMP6, TMP10, TMP6);

                vis_padd16(TMP20, TMP4, TMP20);

                vis_padd16(TMP22, TMP6, TMP22);

                vis_padd16(TMP20, TMP24, TMP20);

                vis_padd16(TMP22, TMP26, TMP22);

                vis_padd16(TMP20, REF_0, TMP20);
                vis_mul8x16au(REF_S4, CONST_256, REF_0);

                vis_padd16(TMP22, REF_2, TMP22);
                vis_pack16(TMP20, DST_2);

                vis_pack16(TMP22, DST_3);
                vis_st64_2(DST_2, dest, 8);
                dest += stride;

                vis_ld64_2(dest, 8, DST_2);
                vis_mul8x16al(DST_0,   CONST_1024, TMP30);
                vis_pmerge(ZERO,      REF_S4_1,  REF_2);

                vis_mul8x16al(DST_1,   CONST_1024, TMP32);
                vis_padd16(REF_4, TMP0, TMP8);

                vis_mul8x16au(REF_S6, CONST_256, REF_4);
                vis_padd16(REF_6, TMP2, TMP10);

                vis_mul8x16au(REF_S6_1, CONST_256, REF_6);
                vis_padd16(TMP8, TMP12, TMP8);

                vis_padd16(TMP10, TMP14, TMP10);

                vis_padd16(TMP8, TMP30, TMP8);

                vis_padd16(TMP10, TMP32, TMP10);
                vis_pack16(TMP8, DST_0);

                vis_pack16(TMP10, DST_1);
                vis_st64(DST_0, dest[0]);

                vis_padd16(REF_0, TMP4, REF_0);

                vis_mul8x16al(DST_2,   CONST_1024, TMP30);
                vis_padd16(REF_2, TMP6, REF_2);

                vis_mul8x16al(DST_3,   CONST_1024, TMP32);
                vis_padd16(REF_0, REF_4, REF_0);

                vis_padd16(REF_2, REF_6, REF_2);

                vis_padd16(REF_0, TMP30, REF_0);

                /* stall */

                vis_padd16(REF_2, TMP32, REF_2);
                vis_pack16(REF_0, DST_2);

                vis_pack16(REF_2, DST_3);
                vis_st64_2(DST_2, dest, 8);
                dest += stride;
        } while (--height);
}

static void MC_avg_no_round_xy_8_vis (uint8_t * dest, const uint8_t * ref,
                                      const int stride, int height)
{
        unsigned long off = (unsigned long) ref & 0x7;
        unsigned long off_plus_1 = off + 1;
        int stride_8 = stride + 8;

        vis_set_gsr(4 << VIS_GSR_SCALEFACT_SHIFT);

        ref = vis_alignaddr(ref);

        vis_ld64(ref[0], TMP0);
        vis_fzero(ZERO);

        vis_ld64_2(ref, 8, TMP2);

        vis_ld64(constants6[0], CONST_6);

        vis_ld64(constants256_1024[0], CONST_256);
        vis_faligndata(TMP0, TMP2, REF_S0);

        if (off != 0x7) {
                vis_alignaddr_g0((void *)off_plus_1);
                vis_faligndata(TMP0, TMP2, REF_S2);
        } else {
                vis_src1(TMP2, REF_S2);
        }

        height >>= 1;
        do {    /* 31 cycles */
                vis_ld64_2(ref, stride, TMP0);
                vis_mul8x16au(REF_S0, CONST_256, TMP8);
                vis_pmerge(ZERO,      REF_S0_1,  TMP10);

                vis_ld64_2(ref, stride_8, TMP2);
                ref += stride;
                vis_mul8x16au(REF_S2, CONST_256, TMP12);
                vis_pmerge(ZERO,      REF_S2_1,  TMP14);

                vis_alignaddr_g0((void *)off);

                vis_ld64_2(ref, stride, TMP4);
                vis_faligndata(TMP0, TMP2, REF_S4);

                vis_ld64_2(ref, stride_8, TMP6);
                ref += stride;

                vis_ld64(dest[0], DST_0);
                vis_faligndata(TMP4, TMP6, REF_S0);

                vis_ld64_2(dest, stride, DST_2);

                if (off != 0x7) {
                        vis_alignaddr_g0((void *)off_plus_1);
                        vis_faligndata(TMP0, TMP2, REF_S6);
                        vis_faligndata(TMP4, TMP6, REF_S2);
                } else {
                        vis_src1(TMP2, REF_S6);
                        vis_src1(TMP6, REF_S2);
                }

                vis_mul8x16al(DST_0,   CONST_1024, TMP30);
                vis_pmerge(ZERO, REF_S4, TMP22);

                vis_mul8x16al(DST_1,   CONST_1024, TMP32);
                vis_pmerge(ZERO,      REF_S4_1,  TMP24);

                vis_mul8x16au(REF_S6, CONST_256, TMP26);
                vis_pmerge(ZERO,      REF_S6_1,  TMP28);

                vis_mul8x16au(REF_S0, CONST_256, REF_S4);
                vis_padd16(TMP22, CONST_6, TMP22);

                vis_mul8x16au(REF_S0_1, CONST_256, REF_S6);
                vis_padd16(TMP24, CONST_6, TMP24);

                vis_mul8x16al(DST_2,   CONST_1024, REF_0);
                vis_padd16(TMP22, TMP26, TMP22);

                vis_mul8x16al(DST_3,   CONST_1024, REF_2);
                vis_padd16(TMP24, TMP28, TMP24);

                vis_mul8x16au(REF_S2, CONST_256, TMP26);
                vis_padd16(TMP8, TMP22, TMP8);

                vis_mul8x16au(REF_S2_1, CONST_256, TMP28);
                vis_padd16(TMP10, TMP24, TMP10);

                vis_padd16(TMP8, TMP12, TMP8);

                vis_padd16(TMP10, TMP14, TMP10);

                vis_padd16(TMP8, TMP30, TMP8);

                vis_padd16(TMP10, TMP32, TMP10);
                vis_pack16(TMP8, DST_0);

                vis_pack16(TMP10, DST_1);
                vis_st64(DST_0, dest[0]);
                dest += stride;

                vis_padd16(REF_S4, TMP22, TMP12);

                vis_padd16(REF_S6, TMP24, TMP14);

                vis_padd16(TMP12, TMP26, TMP12);

                vis_padd16(TMP14, TMP28, TMP14);

                vis_padd16(TMP12, REF_0, TMP12);

                vis_padd16(TMP14, REF_2, TMP14);
                vis_pack16(TMP12, DST_2);

                vis_pack16(TMP14, DST_3);
                vis_st64(DST_2, dest[0]);
                dest += stride;
        } while (--height);
}

/* End of no rounding code */

#define ACCEL_SPARC_VIS 1
#define ACCEL_SPARC_VIS2 2

static int vis_level(void)
{
    int accel = 0;
    accel |= ACCEL_SPARC_VIS;
    accel |= ACCEL_SPARC_VIS2;
    return accel;
}

/* libavcodec initialization code */
void ff_dsputil_init_vis(DSPContext* c, AVCodecContext *avctx)
{
  /* VIS-specific optimizations */
  int accel = vis_level ();
  const int high_bit_depth = avctx->bits_per_raw_sample > 8;

  if (accel & ACCEL_SPARC_VIS) {
      if (avctx->bits_per_raw_sample <= 8 &&
          avctx->idct_algo == FF_IDCT_SIMPLEVIS) {
          c->idct_put = ff_simple_idct_put_vis;
          c->idct_add = ff_simple_idct_add_vis;
          c->idct     = ff_simple_idct_vis;
          c->idct_permutation_type = FF_TRANSPOSE_IDCT_PERM;
      }

      if (!high_bit_depth) {
      c->put_pixels_tab[0][0] = MC_put_o_16_vis;
      c->put_pixels_tab[0][1] = MC_put_x_16_vis;
      c->put_pixels_tab[0][2] = MC_put_y_16_vis;
      c->put_pixels_tab[0][3] = MC_put_xy_16_vis;

      c->put_pixels_tab[1][0] = MC_put_o_8_vis;
      c->put_pixels_tab[1][1] = MC_put_x_8_vis;
      c->put_pixels_tab[1][2] = MC_put_y_8_vis;
      c->put_pixels_tab[1][3] = MC_put_xy_8_vis;

      c->avg_pixels_tab[0][0] = MC_avg_o_16_vis;
      c->avg_pixels_tab[0][1] = MC_avg_x_16_vis;
      c->avg_pixels_tab[0][2] = MC_avg_y_16_vis;
      c->avg_pixels_tab[0][3] = MC_avg_xy_16_vis;

      c->avg_pixels_tab[1][0] = MC_avg_o_8_vis;
      c->avg_pixels_tab[1][1] = MC_avg_x_8_vis;
      c->avg_pixels_tab[1][2] = MC_avg_y_8_vis;
      c->avg_pixels_tab[1][3] = MC_avg_xy_8_vis;

      c->put_no_rnd_pixels_tab[0][0] = MC_put_no_round_o_16_vis;
      c->put_no_rnd_pixels_tab[0][1] = MC_put_no_round_x_16_vis;
      c->put_no_rnd_pixels_tab[0][2] = MC_put_no_round_y_16_vis;
      c->put_no_rnd_pixels_tab[0][3] = MC_put_no_round_xy_16_vis;

      c->put_no_rnd_pixels_tab[1][0] = MC_put_no_round_o_8_vis;
      c->put_no_rnd_pixels_tab[1][1] = MC_put_no_round_x_8_vis;
      c->put_no_rnd_pixels_tab[1][2] = MC_put_no_round_y_8_vis;
      c->put_no_rnd_pixels_tab[1][3] = MC_put_no_round_xy_8_vis;

      c->avg_no_rnd_pixels_tab[0][0] = MC_avg_no_round_o_16_vis;
      c->avg_no_rnd_pixels_tab[0][1] = MC_avg_no_round_x_16_vis;
      c->avg_no_rnd_pixels_tab[0][2] = MC_avg_no_round_y_16_vis;
      c->avg_no_rnd_pixels_tab[0][3] = MC_avg_no_round_xy_16_vis;

      c->avg_no_rnd_pixels_tab[1][0] = MC_avg_no_round_o_8_vis;
      c->avg_no_rnd_pixels_tab[1][1] = MC_avg_no_round_x_8_vis;
      c->avg_no_rnd_pixels_tab[1][2] = MC_avg_no_round_y_8_vis;
      c->avg_no_rnd_pixels_tab[1][3] = MC_avg_no_round_xy_8_vis;
      }
  }
}
