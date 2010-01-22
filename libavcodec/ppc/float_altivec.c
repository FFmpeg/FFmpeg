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

#include "libavcodec/dsputil.h"

#include "dsputil_altivec.h"
#include "util_altivec.h"

static void vector_fmul_altivec(float *dst, const float *src, int len)
{
    int i;
    vector float d0, d1, s, zero = (vector float)vec_splat_u32(0);
    for(i=0; i<len-7; i+=8) {
        d0 = vec_ld(0, dst+i);
        s = vec_ld(0, src+i);
        d1 = vec_ld(16, dst+i);
        d0 = vec_madd(d0, s, zero);
        d1 = vec_madd(d1, vec_ld(16,src+i), zero);
        vec_st(d0, 0, dst+i);
        vec_st(d1, 16, dst+i);
    }
}

static void vector_fmul_reverse_altivec(float *dst, const float *src0,
                                        const float *src1, int len)
{
    int i;
    vector float d, s0, s1, h0, l0,
                 s2, s3, zero = (vector float)vec_splat_u32(0);
    src1 += len-4;
    for(i=0; i<len-7; i+=8) {
        s1 = vec_ld(0, src1-i);              // [a,b,c,d]
        s0 = vec_ld(0, src0+i);
        l0 = vec_mergel(s1, s1);             // [c,c,d,d]
        s3 = vec_ld(-16, src1-i);
        h0 = vec_mergeh(s1, s1);             // [a,a,b,b]
        s2 = vec_ld(16, src0+i);
        s1 = vec_mergeh(vec_mergel(l0,h0),   // [d,b,d,b]
                        vec_mergeh(l0,h0));  // [c,a,c,a]
                                             // [d,c,b,a]
        l0 = vec_mergel(s3, s3);
        d = vec_madd(s0, s1, zero);
        h0 = vec_mergeh(s3, s3);
        vec_st(d, 0, dst+i);
        s3 = vec_mergeh(vec_mergel(l0,h0),
                        vec_mergeh(l0,h0));
        d = vec_madd(s2, s3, zero);
        vec_st(d, 16, dst+i);
    }
}

static void vector_fmul_add_altivec(float *dst, const float *src0,
                                    const float *src1, const float *src2,
                                    int len)
{
    int i;
    vector float d, s0, s1, s2, t0, t1, edges;
    vector unsigned char align = vec_lvsr(0,dst),
                         mask = vec_lvsl(0, dst);

    for (i=0; i<len-3; i+=4) {
        t0 = vec_ld(0, dst+i);
        t1 = vec_ld(15, dst+i);
        s0 = vec_ld(0, src0+i);
        s1 = vec_ld(0, src1+i);
        s2 = vec_ld(0, src2+i);
        edges = vec_perm(t1 ,t0, mask);
        d = vec_madd(s0,s1,s2);
        t1 = vec_perm(d, edges, align);
        t0 = vec_perm(edges, d, align);
        vec_st(t1, 15, dst+i);
        vec_st(t0, 0, dst+i);
    }
}

static void vector_fmul_window_altivec(float *dst, const float *src0, const float *src1, const float *win, float add_bias, int len)
{
    union {
        vector float v;
        float s[4];
    } vadd;
    vector float vadd_bias, zero, t0, t1, s0, s1, wi, wj;
    const vector unsigned char reverse = vcprm(3,2,1,0);
    int i,j;

    dst += len;
    win += len;
    src0+= len;

    vadd.s[0] = add_bias;
    vadd_bias = vec_splat(vadd.v, 0);
    zero = (vector float)vec_splat_u32(0);

    for(i=-len*4, j=len*4-16; i<0; i+=16, j-=16) {
        s0 = vec_ld(i, src0);
        s1 = vec_ld(j, src1);
        wi = vec_ld(i, win);
        wj = vec_ld(j, win);

        s1 = vec_perm(s1, s1, reverse);
        wj = vec_perm(wj, wj, reverse);

        t0 = vec_madd(s0, wj, vadd_bias);
        t0 = vec_nmsub(s1, wi, t0);
        t1 = vec_madd(s0, wi, vadd_bias);
        t1 = vec_madd(s1, wj, t1);
        t1 = vec_perm(t1, t1, reverse);

        vec_st(t0, i, dst);
        vec_st(t1, j, dst);
    }
}

static void int32_to_float_fmul_scalar_altivec(float *dst, const int *src, float mul, int len)
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

    for(i=0; i<len; i+=8) {
        src1 = vec_ctf(vec_ld(0,  src+i), 0);
        src2 = vec_ctf(vec_ld(16, src+i), 0);
        dst1 = vec_madd(src1, mul_v, zero);
        dst2 = vec_madd(src2, mul_v, zero);
        vec_st(dst1,  0, dst+i);
        vec_st(dst2, 16, dst+i);
    }
}


static vector signed short
float_to_int16_one_altivec(const float *src)
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
    if(((long)dst)&15) //FIXME
    for(i=0; i<len-7; i+=8) {
        d0 = vec_ld(0, dst+i);
        d = float_to_int16_one_altivec(src+i);
        d1 = vec_ld(15, dst+i);
        d1 = vec_perm(d1, d0, vec_lvsl(0,dst+i));
        align = vec_lvsr(0, dst+i);
        d0 = vec_perm(d1, d, align);
        d1 = vec_perm(d, d1, align);
        vec_st(d0, 0, dst+i);
        vec_st(d1,15, dst+i);
    }
    else
    for(i=0; i<len-7; i+=8) {
        d = float_to_int16_one_altivec(src+i);
        vec_st(d, 0, dst+i);
    }
}

static void
float_to_int16_interleave_altivec(int16_t *dst, const float **src,
                                  long len, int channels)
{
    int i;
    vector signed short d0, d1, d2, c0, c1, t0, t1;
    vector unsigned char align;
    if(channels == 1)
        float_to_int16_altivec(dst, src[0], len);
    else
        if (channels == 2) {
        if(((long)dst)&15)
        for(i=0; i<len-7; i+=8) {
            d0 = vec_ld(0, dst + i);
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
            dst+=8;
        }
        else
        for(i=0; i<len-7; i+=8) {
            t0 = float_to_int16_one_altivec(src[0] + i);
            t1 = float_to_int16_one_altivec(src[1] + i);
            d0 = vec_mergeh(t0, t1);
            d1 = vec_mergel(t0, t1);
            vec_st(d0,  0, dst + i);
            vec_st(d1, 16, dst + i);
            dst+=8;
        }
    } else {
        DECLARE_ALIGNED(16, int16_t, tmp)[len];
        int c, j;
        for (c = 0; c < channels; c++) {
            float_to_int16_altivec(tmp, src[c], len);
            for (i = 0, j = c; i < len; i++, j+=channels) {
                dst[j] = tmp[i];
            }
        }
   }
}

void float_init_altivec(DSPContext* c, AVCodecContext *avctx)
{
    c->vector_fmul = vector_fmul_altivec;
    c->vector_fmul_reverse = vector_fmul_reverse_altivec;
    c->vector_fmul_add = vector_fmul_add_altivec;
    c->int32_to_float_fmul_scalar = int32_to_float_fmul_scalar_altivec;
    if(!(avctx->flags & CODEC_FLAG_BITEXACT)) {
        c->vector_fmul_window = vector_fmul_window_altivec;
        c->float_to_int16 = float_to_int16_altivec;
        c->float_to_int16_interleave = float_to_int16_interleave_altivec;
    }
}
