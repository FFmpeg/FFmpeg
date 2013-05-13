/*
 * Copyright (c) 2006 Luca Barbato <lu_zero@gentoo.org>
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

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/mem.h"
#include "libavutil/ppc/util_altivec.h"
#include "libavcodec/fmtconvert.h"
#include "dsputil_altivec.h"

#if HAVE_ALTIVEC

static void int32_to_float_fmul_scalar_altivec(float *dst, const int *src,
                                               float mul, int len)
{
    union {
        vector float v;
        float s[4];
    } mul_u;
    int i;
    vector float src1, src2, dst1, dst2, mul_v, zero;

    zero = (vector float)vec_splat_u32(0);
    mul_u.s[0] = mul;
    mul_v = vec_splat(mul_u.v, 0);

    for (i = 0; i < len; i += 8) {
        src1 = vec_ctf(vec_ld(0,  src+i), 0);
        src2 = vec_ctf(vec_ld(16, src+i), 0);
        dst1 = vec_madd(src1, mul_v, zero);
        dst2 = vec_madd(src2, mul_v, zero);
        vec_st(dst1,  0, dst+i);
        vec_st(dst2, 16, dst+i);
    }
}


static vector signed short float_to_int16_one_altivec(const float *src)
{
    vector float s0 = vec_ld(0, src);
    vector float s1 = vec_ld(16, src);
    vector signed int t0 = vec_cts(s0, 0);
    vector signed int t1 = vec_cts(s1, 0);
    return vec_packs(t0,t1);
}

static void float_to_int16_altivec(int16_t *dst, const float *src, long len)
{
    int i;
    vector signed short d0, d1, d;
    vector unsigned char align;
    if (((long)dst) & 15) { //FIXME
        for (i = 0; i < len - 7; i += 8) {
            d0 = vec_ld(0, dst+i);
            d  = float_to_int16_one_altivec(src + i);
            d1 = vec_ld(15, dst+i);
            d1 = vec_perm(d1, d0, vec_lvsl(0, dst + i));
            align = vec_lvsr(0, dst + i);
            d0 = vec_perm(d1, d, align);
            d1 = vec_perm(d, d1, align);
            vec_st(d0,  0, dst + i);
            vec_st(d1, 15, dst + i);
        }
    } else {
        for (i = 0; i < len - 7; i += 8) {
            d = float_to_int16_one_altivec(src + i);
            vec_st(d, 0, dst + i);
        }
    }
}

#define VSTE_INC(dst, v, elem, inc) do {                \
        vector signed short s = vec_splat(v, elem);     \
        vec_ste(s, 0, dst);                             \
        dst += inc;                                     \
    } while (0)

static void float_to_int16_stride_altivec(int16_t *dst, const float *src,
                                          long len, int stride)
{
    int i;
    vector signed short d;

    for (i = 0; i < len - 7; i += 8) {
        d = float_to_int16_one_altivec(src + i);
        VSTE_INC(dst, d, 0, stride);
        VSTE_INC(dst, d, 1, stride);
        VSTE_INC(dst, d, 2, stride);
        VSTE_INC(dst, d, 3, stride);
        VSTE_INC(dst, d, 4, stride);
        VSTE_INC(dst, d, 5, stride);
        VSTE_INC(dst, d, 6, stride);
        VSTE_INC(dst, d, 7, stride);
    }
}

static void float_to_int16_interleave_altivec(int16_t *dst, const float **src,
                                              long len, int channels)
{
    int i;
    vector signed short d0, d1, d2, c0, c1, t0, t1;
    vector unsigned char align;

    if (channels == 1)
        float_to_int16_altivec(dst, src[0], len);
    else {
        if (channels == 2) {
            if (((long)dst) & 15) {
                for (i = 0; i < len - 7; i += 8) {
                    d0 = vec_ld(0,  dst + i);
                    t0 = float_to_int16_one_altivec(src[0] + i);
                    d1 = vec_ld(31, dst + i);
                    t1 = float_to_int16_one_altivec(src[1] + i);
                    c0 = vec_mergeh(t0, t1);
                    c1 = vec_mergel(t0, t1);
                    d2 = vec_perm(d1, d0, vec_lvsl(0, dst + i));
                    align = vec_lvsr(0, dst + i);
                    d0 = vec_perm(d2, c0, align);
                    d1 = vec_perm(c0, c1, align);
                    vec_st(d0,  0, dst + i);
                    d0 = vec_perm(c1, d2, align);
                    vec_st(d1, 15, dst + i);
                    vec_st(d0, 31, dst + i);
                    dst += 8;
                }
            } else {
                for (i = 0; i < len - 7; i += 8) {
                    t0 = float_to_int16_one_altivec(src[0] + i);
                    t1 = float_to_int16_one_altivec(src[1] + i);
                    d0 = vec_mergeh(t0, t1);
                    d1 = vec_mergel(t0, t1);
                    vec_st(d0,  0, dst + i);
                    vec_st(d1, 16, dst + i);
                    dst += 8;
                }
            }
        } else {
            for (i = 0; i < channels; i++)
                float_to_int16_stride_altivec(dst + i, src[i], len, channels);
        }
    }
}

#endif /* HAVE_ALTIVEC */

av_cold void ff_fmt_convert_init_ppc(FmtConvertContext *c,
                                     AVCodecContext *avctx)
{
#if HAVE_ALTIVEC
    c->int32_to_float_fmul_scalar = int32_to_float_fmul_scalar_altivec;
    if (!(avctx->flags & CODEC_FLAG_BITEXACT)) {
        c->float_to_int16 = float_to_int16_altivec;
        c->float_to_int16_interleave = float_to_int16_interleave_altivec;
    }
#endif /* HAVE_ALTIVEC */
}
