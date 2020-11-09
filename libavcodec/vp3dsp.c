/*
 * Copyright (C) 2004 The FFmpeg project
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
 */

/**
 * @file
 * Standard C DSP-oriented functions cribbed from the original VP3
 * source code.
 */

#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/avassert.h"

#include "avcodec.h"
#include "rnd_avg.h"
#include "vp3dsp.h"

#define IdctAdjustBeforeShift 8
#define xC1S7 64277
#define xC2S6 60547
#define xC3S5 54491
#define xC4S4 46341
#define xC5S3 36410
#define xC6S2 25080
#define xC7S1 12785

#define M(a, b) ((int)((SUINT)(a) * (b)) >> 16)

static av_always_inline void idct(uint8_t *dst, ptrdiff_t stride,
                                  int16_t *input, int type)
{
    int16_t *ip = input;

    int A, B, C, D, Ad, Bd, Cd, Dd, E, F, G, H;
    int Ed, Gd, Add, Bdd, Fd, Hd;

    int i;

    /* Inverse DCT on the rows now */
    for (i = 0; i < 8; i++) {
        /* Check for non-zero values */
        if (ip[0 * 8] | ip[1 * 8] | ip[2 * 8] | ip[3 * 8] |
            ip[4 * 8] | ip[5 * 8] | ip[6 * 8] | ip[7 * 8]) {
            A = M(xC1S7, ip[1 * 8]) + M(xC7S1, ip[7 * 8]);
            B = M(xC7S1, ip[1 * 8]) - M(xC1S7, ip[7 * 8]);
            C = M(xC3S5, ip[3 * 8]) + M(xC5S3, ip[5 * 8]);
            D = M(xC3S5, ip[5 * 8]) - M(xC5S3, ip[3 * 8]);

            Ad = M(xC4S4, (A - C));
            Bd = M(xC4S4, (B - D));

            Cd = A + C;
            Dd = B + D;

            E = M(xC4S4, (ip[0 * 8] + ip[4 * 8]));
            F = M(xC4S4, (ip[0 * 8] - ip[4 * 8]));

            G = M(xC2S6, ip[2 * 8]) + M(xC6S2, ip[6 * 8]);
            H = M(xC6S2, ip[2 * 8]) - M(xC2S6, ip[6 * 8]);

            Ed = E - G;
            Gd = E + G;

            Add = F + Ad;
            Bdd = Bd - H;

            Fd = F - Ad;
            Hd = Bd + H;

            /*  Final sequence of operations over-write original inputs. */
            ip[0 * 8] = Gd + Cd;
            ip[7 * 8] = Gd - Cd;

            ip[1 * 8] = Add + Hd;
            ip[2 * 8] = Add - Hd;

            ip[3 * 8] = Ed + Dd;
            ip[4 * 8] = Ed - Dd;

            ip[5 * 8] = Fd + Bdd;
            ip[6 * 8] = Fd - Bdd;
        }

        ip += 1;            /* next row */
    }

    ip = input;

    for (i = 0; i < 8; i++) {
        /* Check for non-zero values (bitwise or faster than ||) */
        if (ip[1] | ip[2] | ip[3] |
            ip[4] | ip[5] | ip[6] | ip[7]) {
            A = M(xC1S7, ip[1]) + M(xC7S1, ip[7]);
            B = M(xC7S1, ip[1]) - M(xC1S7, ip[7]);
            C = M(xC3S5, ip[3]) + M(xC5S3, ip[5]);
            D = M(xC3S5, ip[5]) - M(xC5S3, ip[3]);

            Ad = M(xC4S4, (A - C));
            Bd = M(xC4S4, (B - D));

            Cd = A + C;
            Dd = B + D;

            E = M(xC4S4, (ip[0] + ip[4])) + 8;
            F = M(xC4S4, (ip[0] - ip[4])) + 8;

            if (type == 1) { // HACK
                E += 16 * 128;
                F += 16 * 128;
            }

            G = M(xC2S6, ip[2]) + M(xC6S2, ip[6]);
            H = M(xC6S2, ip[2]) - M(xC2S6, ip[6]);

            Ed = E - G;
            Gd = E + G;

            Add = F + Ad;
            Bdd = Bd - H;

            Fd = F - Ad;
            Hd = Bd + H;

            /* Final sequence of operations over-write original inputs. */
            if (type == 1) {
                dst[0 * stride] = av_clip_uint8((Gd + Cd) >> 4);
                dst[7 * stride] = av_clip_uint8((Gd - Cd) >> 4);

                dst[1 * stride] = av_clip_uint8((Add + Hd) >> 4);
                dst[2 * stride] = av_clip_uint8((Add - Hd) >> 4);

                dst[3 * stride] = av_clip_uint8((Ed + Dd) >> 4);
                dst[4 * stride] = av_clip_uint8((Ed - Dd) >> 4);

                dst[5 * stride] = av_clip_uint8((Fd + Bdd) >> 4);
                dst[6 * stride] = av_clip_uint8((Fd - Bdd) >> 4);
            } else {
                dst[0 * stride] = av_clip_uint8(dst[0 * stride] + ((Gd + Cd) >> 4));
                dst[7 * stride] = av_clip_uint8(dst[7 * stride] + ((Gd - Cd) >> 4));

                dst[1 * stride] = av_clip_uint8(dst[1 * stride] + ((Add + Hd) >> 4));
                dst[2 * stride] = av_clip_uint8(dst[2 * stride] + ((Add - Hd) >> 4));

                dst[3 * stride] = av_clip_uint8(dst[3 * stride] + ((Ed + Dd) >> 4));
                dst[4 * stride] = av_clip_uint8(dst[4 * stride] + ((Ed - Dd) >> 4));

                dst[5 * stride] = av_clip_uint8(dst[5 * stride] + ((Fd + Bdd) >> 4));
                dst[6 * stride] = av_clip_uint8(dst[6 * stride] + ((Fd - Bdd) >> 4));
            }
        } else {
            if (type == 1) {
                dst[0*stride] =
                dst[1*stride] =
                dst[2*stride] =
                dst[3*stride] =
                dst[4*stride] =
                dst[5*stride] =
                dst[6*stride] =
                dst[7*stride] = av_clip_uint8(128 + ((xC4S4 * ip[0] + (IdctAdjustBeforeShift << 16)) >> 20));
            } else {
                if (ip[0]) {
                    int v = (xC4S4 * ip[0] + (IdctAdjustBeforeShift << 16)) >> 20;
                    dst[0 * stride] = av_clip_uint8(dst[0 * stride] + v);
                    dst[1 * stride] = av_clip_uint8(dst[1 * stride] + v);
                    dst[2 * stride] = av_clip_uint8(dst[2 * stride] + v);
                    dst[3 * stride] = av_clip_uint8(dst[3 * stride] + v);
                    dst[4 * stride] = av_clip_uint8(dst[4 * stride] + v);
                    dst[5 * stride] = av_clip_uint8(dst[5 * stride] + v);
                    dst[6 * stride] = av_clip_uint8(dst[6 * stride] + v);
                    dst[7 * stride] = av_clip_uint8(dst[7 * stride] + v);
                }
            }
        }

        ip += 8;            /* next column */
        dst++;
    }
}

static av_always_inline void idct10(uint8_t *dst, ptrdiff_t stride,
                                    int16_t *input, int type)
{
    int16_t *ip = input;

    int A, B, C, D, Ad, Bd, Cd, Dd, E, F, G, H;
    int Ed, Gd, Add, Bdd, Fd, Hd;

    int i;

    /* Inverse DCT on the rows now */
    for (i = 0; i < 4; i++) {
        /* Check for non-zero values */
        if (ip[0 * 8] | ip[1 * 8] | ip[2 * 8] | ip[3 * 8]) {
            A =  M(xC1S7, ip[1 * 8]);
            B =  M(xC7S1, ip[1 * 8]);
            C =  M(xC3S5, ip[3 * 8]);
            D = -M(xC5S3, ip[3 * 8]);

            Ad = M(xC4S4, (A - C));
            Bd = M(xC4S4, (B - D));

            Cd = A + C;
            Dd = B + D;

            E = M(xC4S4, ip[0 * 8]);
            F = E;

            G = M(xC2S6, ip[2 * 8]);
            H = M(xC6S2, ip[2 * 8]);

            Ed = E - G;
            Gd = E + G;

            Add = F + Ad;
            Bdd = Bd - H;

            Fd = F - Ad;
            Hd = Bd + H;

            /* Final sequence of operations over-write original inputs */
            ip[0 * 8] = Gd + Cd;
            ip[7 * 8] = Gd - Cd;

            ip[1 * 8] = Add + Hd;
            ip[2 * 8] = Add - Hd;

            ip[3 * 8] = Ed + Dd;
            ip[4 * 8] = Ed - Dd;

            ip[5 * 8] = Fd + Bdd;
            ip[6 * 8] = Fd - Bdd;

        }

        ip += 1;
    }

    ip = input;

    for (i = 0; i < 8; i++) {
        /* Check for non-zero values (bitwise or faster than ||) */
        if (ip[0] | ip[1] | ip[2] | ip[3]) {
            A =  M(xC1S7, ip[1]);
            B =  M(xC7S1, ip[1]);
            C =  M(xC3S5, ip[3]);
            D = -M(xC5S3, ip[3]);

            Ad = M(xC4S4, (A - C));
            Bd = M(xC4S4, (B - D));

            Cd = A + C;
            Dd = B + D;

            E = M(xC4S4, ip[0]);
            if (type == 1)
                E += 16 * 128;
            F = E;

            G = M(xC2S6, ip[2]);
            H = M(xC6S2, ip[2]);

            Ed = E - G;
            Gd = E + G;

            Add = F + Ad;
            Bdd = Bd - H;

            Fd = F - Ad;
            Hd = Bd + H;

            Gd += 8;
            Add += 8;
            Ed += 8;
            Fd += 8;

            /* Final sequence of operations over-write original inputs. */
            if (type == 1) {
                dst[0 * stride] = av_clip_uint8((Gd + Cd) >> 4);
                dst[7 * stride] = av_clip_uint8((Gd - Cd) >> 4);

                dst[1 * stride] = av_clip_uint8((Add + Hd) >> 4);
                dst[2 * stride] = av_clip_uint8((Add - Hd) >> 4);

                dst[3 * stride] = av_clip_uint8((Ed + Dd) >> 4);
                dst[4 * stride] = av_clip_uint8((Ed - Dd) >> 4);

                dst[5 * stride] = av_clip_uint8((Fd + Bdd) >> 4);
                dst[6 * stride] = av_clip_uint8((Fd - Bdd) >> 4);
            } else {
                dst[0 * stride] = av_clip_uint8(dst[0 * stride] + ((Gd + Cd) >> 4));
                dst[7 * stride] = av_clip_uint8(dst[7 * stride] + ((Gd - Cd) >> 4));

                dst[1 * stride] = av_clip_uint8(dst[1 * stride] + ((Add + Hd) >> 4));
                dst[2 * stride] = av_clip_uint8(dst[2 * stride] + ((Add - Hd) >> 4));

                dst[3 * stride] = av_clip_uint8(dst[3 * stride] + ((Ed + Dd) >> 4));
                dst[4 * stride] = av_clip_uint8(dst[4 * stride] + ((Ed - Dd) >> 4));

                dst[5 * stride] = av_clip_uint8(dst[5 * stride] + ((Fd + Bdd) >> 4));
                dst[6 * stride] = av_clip_uint8(dst[6 * stride] + ((Fd - Bdd) >> 4));
            }
        } else {
            if (type == 1) {
                dst[0*stride] =
                dst[1*stride] =
                dst[2*stride] =
                dst[3*stride] =
                dst[4*stride] =
                dst[5*stride] =
                dst[6*stride] =
                dst[7*stride] = 128;
            }
        }

        ip += 8;
        dst++;
    }
}

void ff_vp3dsp_idct10_put(uint8_t *dest, ptrdiff_t stride, int16_t *block)
{
    idct10(dest, stride, block, 1);
    memset(block, 0, sizeof(*block) * 64);
}

void ff_vp3dsp_idct10_add(uint8_t *dest, ptrdiff_t stride, int16_t *block)
{
    idct10(dest, stride, block, 2);
    memset(block, 0, sizeof(*block) * 64);
}

static void vp3_idct_put_c(uint8_t *dest /* align 8 */, ptrdiff_t stride,
                           int16_t *block /* align 16 */)
{
    idct(dest, stride, block, 1);
    memset(block, 0, sizeof(*block) * 64);
}

static void vp3_idct_add_c(uint8_t *dest /* align 8 */, ptrdiff_t stride,
                           int16_t *block /* align 16 */)
{
    idct(dest, stride, block, 2);
    memset(block, 0, sizeof(*block) * 64);
}

static void vp3_idct_dc_add_c(uint8_t *dest /* align 8 */, ptrdiff_t stride,
                              int16_t *block /* align 16 */)
{
    int i, dc = (block[0] + 15) >> 5;

    for (i = 0; i < 8; i++) {
        dest[0] = av_clip_uint8(dest[0] + dc);
        dest[1] = av_clip_uint8(dest[1] + dc);
        dest[2] = av_clip_uint8(dest[2] + dc);
        dest[3] = av_clip_uint8(dest[3] + dc);
        dest[4] = av_clip_uint8(dest[4] + dc);
        dest[5] = av_clip_uint8(dest[5] + dc);
        dest[6] = av_clip_uint8(dest[6] + dc);
        dest[7] = av_clip_uint8(dest[7] + dc);
        dest   += stride;
    }
    block[0] = 0;
}

static av_always_inline void vp3_v_loop_filter_c(uint8_t *first_pixel, ptrdiff_t stride,
                                                 int *bounding_values, int count)
{
    unsigned char *end;
    int filter_value;
    const ptrdiff_t nstride = -stride;

    for (end = first_pixel + count; first_pixel < end; first_pixel++) {
        filter_value = (first_pixel[2 * nstride] - first_pixel[stride]) +
                       (first_pixel[0] - first_pixel[nstride]) * 3;
        filter_value = bounding_values[(filter_value + 4) >> 3];

        first_pixel[nstride] = av_clip_uint8(first_pixel[nstride] + filter_value);
        first_pixel[0]       = av_clip_uint8(first_pixel[0] - filter_value);
    }
}

static av_always_inline void vp3_h_loop_filter_c(uint8_t *first_pixel, ptrdiff_t stride,
                                                 int *bounding_values, int count)
{
    unsigned char *end;
    int filter_value;

    for (end = first_pixel + count * stride; first_pixel != end; first_pixel += stride) {
        filter_value = (first_pixel[-2] - first_pixel[1]) +
                       (first_pixel[ 0] - first_pixel[-1]) * 3;
        filter_value = bounding_values[(filter_value + 4) >> 3];

        first_pixel[-1] = av_clip_uint8(first_pixel[-1] + filter_value);
        first_pixel[ 0] = av_clip_uint8(first_pixel[ 0] - filter_value);
    }
}

#define LOOP_FILTER(prefix, suffix, dim, count) \
void prefix##_##dim##_loop_filter_##count##suffix(uint8_t *first_pixel, ptrdiff_t stride, \
                                int *bounding_values) \
{ \
    vp3_##dim##_loop_filter_c(first_pixel, stride, bounding_values, count); \
}

static LOOP_FILTER(vp3,_c, v, 8)
static LOOP_FILTER(vp3,_c, h, 8)
LOOP_FILTER(ff_vp3dsp, , v, 12)
LOOP_FILTER(ff_vp3dsp, , h, 12)

static void put_no_rnd_pixels_l2(uint8_t *dst, const uint8_t *src1,
                                 const uint8_t *src2, ptrdiff_t stride, int h)
{
    int i;

    for (i = 0; i < h; i++) {
        uint32_t a, b;

        a = AV_RN32(&src1[i * stride]);
        b = AV_RN32(&src2[i * stride]);
        AV_WN32A(&dst[i * stride], no_rnd_avg32(a, b));
        a = AV_RN32(&src1[i * stride + 4]);
        b = AV_RN32(&src2[i * stride + 4]);
        AV_WN32A(&dst[i * stride + 4], no_rnd_avg32(a, b));
    }
}

av_cold void ff_vp3dsp_init(VP3DSPContext *c, int flags)
{
    c->put_no_rnd_pixels_l2 = put_no_rnd_pixels_l2;

    c->idct_put      = vp3_idct_put_c;
    c->idct_add      = vp3_idct_add_c;
    c->idct_dc_add   = vp3_idct_dc_add_c;
    c->v_loop_filter = c->v_loop_filter_unaligned = vp3_v_loop_filter_8_c;
    c->h_loop_filter = c->h_loop_filter_unaligned = vp3_h_loop_filter_8_c;

    if (ARCH_ARM)
        ff_vp3dsp_init_arm(c, flags);
    if (ARCH_PPC)
        ff_vp3dsp_init_ppc(c, flags);
    if (ARCH_X86)
        ff_vp3dsp_init_x86(c, flags);
    if (ARCH_MIPS)
        ff_vp3dsp_init_mips(c, flags);
}

/*
 * This function initializes the loop filter boundary limits if the frame's
 * quality index is different from the previous frame's.
 *
 * where sizeof(bounding_values_array) is 256 * sizeof(int)
 *
 * The filter_limit_values may not be larger than 127.
 */
void ff_vp3dsp_set_bounding_values(int * bounding_values_array, int filter_limit)
{
    int *bounding_values = bounding_values_array + 127;
    int x;
    int value;

    av_assert0(filter_limit < 128U);

    /* set up the bounding values */
    memset(bounding_values_array, 0, 256 * sizeof(int));
    for (x = 0; x < filter_limit; x++) {
        bounding_values[-x] = -x;
        bounding_values[x] = x;
    }
    for (x = value = filter_limit; x < 128 && value; x++, value--) {
        bounding_values[ x] =  value;
        bounding_values[-x] = -value;
    }
    if (value)
        bounding_values[128] = value;
    bounding_values[129] = bounding_values[130] = filter_limit * 0x02020202U;
}
