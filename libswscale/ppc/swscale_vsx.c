/*
 * AltiVec-enhanced yuv2yuvX
 *
 * Copyright (C) 2004 Romain Dolbeau <romain@dolbeau.org>
 * based on the equivalent C code in swscale.c
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

#include <inttypes.h>

#include "config.h"
#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/mem_internal.h"
#include "yuv2rgb_altivec.h"
#include "libavutil/ppc/util_altivec.h"

#if HAVE_VSX
#define vzero vec_splat_s32(0)

#if !HAVE_BIGENDIAN
#define  GET_LS(a,b,c,s) {\
        ls  = a;\
        a = vec_vsx_ld(((b) << 1)  + 16, s);\
    }

#define yuv2planeX_8(d1, d2, l1, src, x, perm, filter) do {\
        vector signed short ls;\
        vector signed int   vf1, vf2, i1, i2;\
        GET_LS(l1, x, perm, src);\
        i1  = vec_mule(filter, ls);\
        i2  = vec_mulo(filter, ls);\
        vf1 = vec_mergeh(i1, i2);\
        vf2 = vec_mergel(i1, i2);\
        d1 = vec_add(d1, vf1);\
        d2 = vec_add(d2, vf2);\
    } while (0)

#define LOAD_FILTER(vf,f) {\
        vf = vec_vsx_ld(joffset, f);\
}
#define LOAD_L1(ll1,s,p){\
        ll1  = vec_vsx_ld(xoffset, s);\
}

// The 3 above is 2 (filterSize == 4) + 1 (sizeof(short) == 2).

// The neat trick: We only care for half the elements,
// high or low depending on (i<<3)%16 (it's 0 or 8 here),
// and we're going to use vec_mule, so we choose
// carefully how to "unpack" the elements into the even slots.
#define GET_VF4(a, vf, f) {\
    vf = (vector signed short)vec_vsx_ld(a << 3, f);\
    vf = vec_mergeh(vf, (vector signed short)vzero);\
}
#define FIRST_LOAD(sv, pos, s, per) {}
#define UPDATE_PTR(s0, d0, s1, d1) {}
#define LOAD_SRCV(pos, a, s, per, v0, v1, vf) {\
    vf = vec_vsx_ld(pos + a, s);\
}
#define LOAD_SRCV8(pos, a, s, per, v0, v1, vf) LOAD_SRCV(pos, a, s, per, v0, v1, vf)
#define GET_VFD(a, b, f, vf0, vf1, per, vf, off) {\
    vf  = vec_vsx_ld((a * 2 * filterSize) + (b * 2) + off, f);\
}

#define FUNC(name) name ## _vsx
#include "swscale_ppc_template.c"
#undef FUNC

#undef vzero

#endif /* !HAVE_BIGENDIAN */

static void yuv2plane1_8_u(const int16_t *src, uint8_t *dest, int dstW,
                           const uint8_t *dither, int offset, int start)
{
    int i;
    for (i = start; i < dstW; i++) {
        int val = (src[i] + dither[(i + offset) & 7]) >> 7;
        dest[i] = av_clip_uint8(val);
    }
}

static void yuv2plane1_8_vsx(const int16_t *src, uint8_t *dest, int dstW,
                           const uint8_t *dither, int offset)
{
    const int dst_u = -(uintptr_t)dest & 15;
    int i, j;
    LOCAL_ALIGNED(16, int16_t, val, [16]);
    const vec_u16 shifts = (vec_u16) {7, 7, 7, 7, 7, 7, 7, 7};
    vec_s16 vi, vileft, ditherleft, ditherright;
    vec_u8 vd;

    for (j = 0; j < 16; j++) {
        val[j] = dither[(dst_u + offset + j) & 7];
    }

    ditherleft = vec_ld(0, val);
    ditherright = vec_ld(0, &val[8]);

    yuv2plane1_8_u(src, dest, dst_u, dither, offset, 0);

    for (i = dst_u; i < dstW - 15; i += 16) {

        vi = vec_vsx_ld(0, &src[i]);
        vi = vec_adds(ditherleft, vi);
        vileft = vec_sra(vi, shifts);

        vi = vec_vsx_ld(0, &src[i + 8]);
        vi = vec_adds(ditherright, vi);
        vi = vec_sra(vi, shifts);

        vd = vec_packsu(vileft, vi);
        vec_st(vd, 0, &dest[i]);
    }

    yuv2plane1_8_u(src, dest, dstW, dither, offset, i);
}

#if !HAVE_BIGENDIAN

#define output_pixel(pos, val) \
    if (big_endian) { \
        AV_WB16(pos, av_clip_uintp2(val >> shift, output_bits)); \
    } else { \
        AV_WL16(pos, av_clip_uintp2(val >> shift, output_bits)); \
    }

static void yuv2plane1_nbps_u(const int16_t *src, uint16_t *dest, int dstW,
                              int big_endian, int output_bits, int start)
{
    int i;
    int shift = 15 - output_bits;

    for (i = start; i < dstW; i++) {
        int val = src[i] + (1 << (shift - 1));
        output_pixel(&dest[i], val);
    }
}

static av_always_inline void yuv2plane1_nbps_vsx(const int16_t *src,
                                                 uint16_t *dest, int dstW,
                                                 const int big_endian,
                                                 const int output_bits)
{
    const int dst_u = -(uintptr_t)dest & 7;
    const int shift = 15 - output_bits;
    const int add = (1 << (shift - 1));
    const int clip = (1 << output_bits) - 1;
    const vec_u16 vadd = (vec_u16) {add, add, add, add, add, add, add, add};
    const vec_u16 vswap = (vec_u16) vec_splat_u16(big_endian ? 8 : 0);
    const vec_u16 vshift = (vec_u16) vec_splat_u16(shift);
    const vec_u16 vlargest = (vec_u16) {clip, clip, clip, clip, clip, clip, clip, clip};
    vec_u16 v;
    int i;

    yuv2plane1_nbps_u(src, dest, dst_u, big_endian, output_bits, 0);

    for (i = dst_u; i < dstW - 7; i += 8) {
        v = vec_vsx_ld(0, (const uint16_t *) &src[i]);
        v = vec_add(v, vadd);
        v = vec_sr(v, vshift);
        v = vec_min(v, vlargest);
        v = vec_rl(v, vswap);
        vec_st(v, 0, &dest[i]);
    }

    yuv2plane1_nbps_u(src, dest, dstW, big_endian, output_bits, i);
}

static void yuv2planeX_nbps_u(const int16_t *filter, int filterSize,
                              const int16_t **src, uint16_t *dest, int dstW,
                              int big_endian, int output_bits, int start)
{
    int i;
    int shift = 11 + 16 - output_bits;

    for (i = start; i < dstW; i++) {
        int val = 1 << (shift - 1);
        int j;

        for (j = 0; j < filterSize; j++)
            val += src[j][i] * filter[j];

        output_pixel(&dest[i], val);
    }
}

static void yuv2planeX_nbps_vsx(const int16_t *filter, int filterSize,
                                const int16_t **src, uint16_t *dest, int dstW,
                                int big_endian, int output_bits)
{
    const int dst_u = -(uintptr_t)dest & 7;
    const int shift = 11 + 16 - output_bits;
    const int add = (1 << (shift - 1));
    const int clip = (1 << output_bits) - 1;
    const uint16_t swap = big_endian ? 8 : 0;
    const vec_u32 vadd = (vec_u32) {add, add, add, add};
    const vec_u32 vshift = (vec_u32) {shift, shift, shift, shift};
    const vec_u16 vswap = (vec_u16) {swap, swap, swap, swap, swap, swap, swap, swap};
    const vec_u16 vlargest = (vec_u16) {clip, clip, clip, clip, clip, clip, clip, clip};
    const vec_s16 vzero = vec_splat_s16(0);
    const vec_u8 vperm = (vec_u8) {0, 1, 8, 9, 2, 3, 10, 11, 4, 5, 12, 13, 6, 7, 14, 15};
    vec_s16 vfilter[MAX_FILTER_SIZE], vin;
    vec_u16 v;
    vec_u32 vleft, vright, vtmp;
    int i, j;

    for (i = 0; i < filterSize; i++) {
        vfilter[i] = (vec_s16) {filter[i], filter[i], filter[i], filter[i],
                                filter[i], filter[i], filter[i], filter[i]};
    }

    yuv2planeX_nbps_u(filter, filterSize, src, dest, dst_u, big_endian, output_bits, 0);

    for (i = dst_u; i < dstW - 7; i += 8) {
        vleft = vright = vadd;

        for (j = 0; j < filterSize; j++) {
            vin = vec_vsx_ld(0, &src[j][i]);
            vtmp = (vec_u32) vec_mule(vin, vfilter[j]);
            vleft = vec_add(vleft, vtmp);
            vtmp = (vec_u32) vec_mulo(vin, vfilter[j]);
            vright = vec_add(vright, vtmp);
        }

        vleft = vec_sra(vleft, vshift);
        vright = vec_sra(vright, vshift);
        v = vec_packsu(vleft, vright);
        v = (vec_u16) vec_max((vec_s16) v, vzero);
        v = vec_min(v, vlargest);
        v = vec_rl(v, vswap);
        v = vec_perm(v, v, vperm);
        vec_st(v, 0, &dest[i]);
    }

    yuv2planeX_nbps_u(filter, filterSize, src, dest, dstW, big_endian, output_bits, i);
}


#undef output_pixel

#define output_pixel(pos, val, bias, signedness) \
    if (big_endian) { \
        AV_WB16(pos, bias + av_clip_ ## signedness ## 16(val >> shift)); \
    } else { \
        AV_WL16(pos, bias + av_clip_ ## signedness ## 16(val >> shift)); \
    }

static void yuv2plane1_16_u(const int32_t *src, uint16_t *dest, int dstW,
                              int big_endian, int output_bits, int start)
{
    int i;
    const int shift = 3;

    for (i = start; i < dstW; i++) {
        int val = src[i] + (1 << (shift - 1));
        output_pixel(&dest[i], val, 0, uint);
    }
}

static av_always_inline void yuv2plane1_16_vsx(const int32_t *src,
                                               uint16_t *dest, int dstW,
                                               const int big_endian,
                                               int output_bits)
{
    const int dst_u = -(uintptr_t)dest & 7;
    const int shift = 3;
    const int add = (1 << (shift - 1));
    const vec_u32 vadd = (vec_u32) {add, add, add, add};
    const vec_u16 vswap = (vec_u16) vec_splat_u16(big_endian ? 8 : 0);
    const vec_u32 vshift = (vec_u32) vec_splat_u32(shift);
    vec_u32 v, v2;
    vec_u16 vd;
    int i;

    yuv2plane1_16_u(src, dest, dst_u, big_endian, output_bits, 0);

    for (i = dst_u; i < dstW - 7; i += 8) {
        v = vec_vsx_ld(0, (const uint32_t *) &src[i]);
        v = vec_add(v, vadd);
        v = vec_sr(v, vshift);

        v2 = vec_vsx_ld(0, (const uint32_t *) &src[i + 4]);
        v2 = vec_add(v2, vadd);
        v2 = vec_sr(v2, vshift);

        vd = vec_packsu(v, v2);
        vd = vec_rl(vd, vswap);

        vec_st(vd, 0, &dest[i]);
    }

    yuv2plane1_16_u(src, dest, dstW, big_endian, output_bits, i);
}

#if HAVE_POWER8

static void yuv2planeX_16_u(const int16_t *filter, int filterSize,
                            const int32_t **src, uint16_t *dest, int dstW,
                            int big_endian, int output_bits, int start)
{
    int i;
    int shift = 15;

    for (i = start; i < dstW; i++) {
        int val = 1 << (shift - 1);
        int j;

        /* range of val is [0,0x7FFFFFFF], so 31 bits, but with lanczos/spline
         * filters (or anything with negative coeffs, the range can be slightly
         * wider in both directions. To account for this overflow, we subtract
         * a constant so it always fits in the signed range (assuming a
         * reasonable filterSize), and re-add that at the end. */
        val -= 0x40000000;
        for (j = 0; j < filterSize; j++)
            val += src[j][i] * (unsigned)filter[j];

        output_pixel(&dest[i], val, 0x8000, int);
    }
}

static void yuv2planeX_16_vsx(const int16_t *filter, int filterSize,
                              const int32_t **src, uint16_t *dest, int dstW,
                              int big_endian, int output_bits)
{
    const int dst_u = -(uintptr_t)dest & 7;
    const int shift = 15;
    const int bias = 0x8000;
    const int add = (1 << (shift - 1)) - 0x40000000;
    const uint16_t swap = big_endian ? 8 : 0;
    const vec_u32 vadd = (vec_u32) {add, add, add, add};
    const vec_u32 vshift = (vec_u32) {shift, shift, shift, shift};
    const vec_u16 vswap = (vec_u16) {swap, swap, swap, swap, swap, swap, swap, swap};
    const vec_u16 vbias = (vec_u16) {bias, bias, bias, bias, bias, bias, bias, bias};
    vec_s32 vfilter[MAX_FILTER_SIZE];
    vec_u16 v;
    vec_u32 vleft, vright, vtmp;
    vec_s32 vin32l, vin32r;
    int i, j;

    for (i = 0; i < filterSize; i++) {
        vfilter[i] = (vec_s32) {filter[i], filter[i], filter[i], filter[i]};
    }

    yuv2planeX_16_u(filter, filterSize, src, dest, dst_u, big_endian, output_bits, 0);

    for (i = dst_u; i < dstW - 7; i += 8) {
        vleft = vright = vadd;

        for (j = 0; j < filterSize; j++) {
            vin32l = vec_vsx_ld(0, &src[j][i]);
            vin32r = vec_vsx_ld(0, &src[j][i + 4]);

            vtmp = (vec_u32) vec_mul(vin32l, vfilter[j]);
            vleft = vec_add(vleft, vtmp);
            vtmp = (vec_u32) vec_mul(vin32r, vfilter[j]);
            vright = vec_add(vright, vtmp);
        }

        vleft = vec_sra(vleft, vshift);
        vright = vec_sra(vright, vshift);
        v = (vec_u16) vec_packs((vec_s32) vleft, (vec_s32) vright);
        v = vec_add(v, vbias);
        v = vec_rl(v, vswap);
        vec_st(v, 0, &dest[i]);
    }

    yuv2planeX_16_u(filter, filterSize, src, dest, dstW, big_endian, output_bits, i);
}

#endif /* HAVE_POWER8 */

#define yuv2NBPS(bits, BE_LE, is_be, template_size, typeX_t) \
    yuv2NBPS1(bits, BE_LE, is_be, template_size, typeX_t) \
    yuv2NBPSX(bits, BE_LE, is_be, template_size, typeX_t)

#define yuv2NBPS1(bits, BE_LE, is_be, template_size, typeX_t) \
static void yuv2plane1_ ## bits ## BE_LE ## _vsx(const int16_t *src, \
                             uint8_t *dest, int dstW, \
                             const uint8_t *dither, int offset) \
{ \
    yuv2plane1_ ## template_size ## _vsx((const typeX_t *) src, \
                         (uint16_t *) dest, dstW, is_be, bits); \
}

#define yuv2NBPSX(bits, BE_LE, is_be, template_size, typeX_t) \
static void yuv2planeX_ ## bits ## BE_LE ## _vsx(const int16_t *filter, int filterSize, \
                              const int16_t **src, uint8_t *dest, int dstW, \
                              const uint8_t *dither, int offset)\
{ \
    yuv2planeX_## template_size ## _vsx(filter, \
                         filterSize, (const typeX_t **) src, \
                         (uint16_t *) dest, dstW, is_be, bits); \
}

yuv2NBPS( 9, BE, 1, nbps, int16_t)
yuv2NBPS( 9, LE, 0, nbps, int16_t)
yuv2NBPS(10, BE, 1, nbps, int16_t)
yuv2NBPS(10, LE, 0, nbps, int16_t)
yuv2NBPS(12, BE, 1, nbps, int16_t)
yuv2NBPS(12, LE, 0, nbps, int16_t)
yuv2NBPS(14, BE, 1, nbps, int16_t)
yuv2NBPS(14, LE, 0, nbps, int16_t)

yuv2NBPS1(16, BE, 1, 16, int32_t)
yuv2NBPS1(16, LE, 0, 16, int32_t)
#if HAVE_POWER8
yuv2NBPSX(16, BE, 1, 16, int32_t)
yuv2NBPSX(16, LE, 0, 16, int32_t)
#endif

#define WRITERGB \
        R_l = vec_max(R_l, zero32); \
        R_r = vec_max(R_r, zero32); \
        G_l = vec_max(G_l, zero32); \
        G_r = vec_max(G_r, zero32); \
        B_l = vec_max(B_l, zero32); \
        B_r = vec_max(B_r, zero32); \
\
        R_l = vec_min(R_l, rgbclip); \
        R_r = vec_min(R_r, rgbclip); \
        G_l = vec_min(G_l, rgbclip); \
        G_r = vec_min(G_r, rgbclip); \
        B_l = vec_min(B_l, rgbclip); \
        B_r = vec_min(B_r, rgbclip); \
\
        R_l = vec_sr(R_l, shift22); \
        R_r = vec_sr(R_r, shift22); \
        G_l = vec_sr(G_l, shift22); \
        G_r = vec_sr(G_r, shift22); \
        B_l = vec_sr(B_l, shift22); \
        B_r = vec_sr(B_r, shift22); \
\
        rd16 = vec_packsu(R_l, R_r); \
        gd16 = vec_packsu(G_l, G_r); \
        bd16 = vec_packsu(B_l, B_r); \
        rd = vec_packsu(rd16, zero16); \
        gd = vec_packsu(gd16, zero16); \
        bd = vec_packsu(bd16, zero16); \
\
        switch(target) { \
        case AV_PIX_FMT_RGB24: \
            out0 = vec_perm(rd, gd, perm3rg0); \
            out0 = vec_perm(out0, bd, perm3tb0); \
            out1 = vec_perm(rd, gd, perm3rg1); \
            out1 = vec_perm(out1, bd, perm3tb1); \
\
            vec_vsx_st(out0, 0, dest); \
            vec_vsx_st(out1, 16, dest); \
\
            dest += 24; \
        break; \
        case AV_PIX_FMT_BGR24: \
            out0 = vec_perm(bd, gd, perm3rg0); \
            out0 = vec_perm(out0, rd, perm3tb0); \
            out1 = vec_perm(bd, gd, perm3rg1); \
            out1 = vec_perm(out1, rd, perm3tb1); \
\
            vec_vsx_st(out0, 0, dest); \
            vec_vsx_st(out1, 16, dest); \
\
            dest += 24; \
        break; \
        case AV_PIX_FMT_BGRA: \
            out0 = vec_mergeh(bd, gd); \
            out1 = vec_mergeh(rd, ad); \
\
            tmp8 = (vec_u8) vec_mergeh((vec_u16) out0, (vec_u16) out1); \
            vec_vsx_st(tmp8, 0, dest); \
            tmp8 = (vec_u8) vec_mergel((vec_u16) out0, (vec_u16) out1); \
            vec_vsx_st(tmp8, 16, dest); \
\
            dest += 32; \
        break; \
        case AV_PIX_FMT_RGBA: \
            out0 = vec_mergeh(rd, gd); \
            out1 = vec_mergeh(bd, ad); \
\
            tmp8 = (vec_u8) vec_mergeh((vec_u16) out0, (vec_u16) out1); \
            vec_vsx_st(tmp8, 0, dest); \
            tmp8 = (vec_u8) vec_mergel((vec_u16) out0, (vec_u16) out1); \
            vec_vsx_st(tmp8, 16, dest); \
\
            dest += 32; \
        break; \
        case AV_PIX_FMT_ARGB: \
            out0 = vec_mergeh(ad, rd); \
            out1 = vec_mergeh(gd, bd); \
\
            tmp8 = (vec_u8) vec_mergeh((vec_u16) out0, (vec_u16) out1); \
            vec_vsx_st(tmp8, 0, dest); \
            tmp8 = (vec_u8) vec_mergel((vec_u16) out0, (vec_u16) out1); \
            vec_vsx_st(tmp8, 16, dest); \
\
            dest += 32; \
        break; \
        case AV_PIX_FMT_ABGR: \
            out0 = vec_mergeh(ad, bd); \
            out1 = vec_mergeh(gd, rd); \
\
            tmp8 = (vec_u8) vec_mergeh((vec_u16) out0, (vec_u16) out1); \
            vec_vsx_st(tmp8, 0, dest); \
            tmp8 = (vec_u8) vec_mergel((vec_u16) out0, (vec_u16) out1); \
            vec_vsx_st(tmp8, 16, dest); \
\
            dest += 32; \
        break; \
        }

static av_always_inline void
yuv2rgb_full_X_vsx_template(SwsContext *c, const int16_t *lumFilter,
                          const int16_t **lumSrc, int lumFilterSize,
                          const int16_t *chrFilter, const int16_t **chrUSrc,
                          const int16_t **chrVSrc, int chrFilterSize,
                          const int16_t **alpSrc, uint8_t *dest,
                          int dstW, int y, enum AVPixelFormat target, int hasAlpha)
{
    vec_s16 vv;
    vec_s32 vy32_l, vy32_r, vu32_l, vu32_r, vv32_l, vv32_r, tmp32;
    vec_s32 R_l, R_r, G_l, G_r, B_l, B_r;
    vec_s32 tmp, tmp2, tmp3, tmp4;
    vec_u16 rd16, gd16, bd16;
    vec_u8 rd, bd, gd, ad, out0, out1, tmp8;
    vec_s16 vlumFilter[MAX_FILTER_SIZE], vchrFilter[MAX_FILTER_SIZE];
    const vec_s32 ystart = vec_splats(1 << 9);
    const vec_s32 uvstart = vec_splats((1 << 9) - (128 << 19));
    const vec_u16 zero16 = vec_splat_u16(0);
    const vec_s32 y_offset = vec_splats(c->yuv2rgb_y_offset);
    const vec_s32 y_coeff = vec_splats(c->yuv2rgb_y_coeff);
    const vec_s32 y_add = vec_splats(1 << 21);
    const vec_s32 v2r_coeff = vec_splats(c->yuv2rgb_v2r_coeff);
    const vec_s32 v2g_coeff = vec_splats(c->yuv2rgb_v2g_coeff);
    const vec_s32 u2g_coeff = vec_splats(c->yuv2rgb_u2g_coeff);
    const vec_s32 u2b_coeff = vec_splats(c->yuv2rgb_u2b_coeff);
    const vec_s32 rgbclip = vec_splats(1 << 30);
    const vec_s32 zero32 = vec_splat_s32(0);
    const vec_u32 shift22 = vec_splats(22U);
    const vec_u32 shift10 = vec_splat_u32(10);
    int i, j;

    // Various permutations
    const vec_u8 perm3rg0 = (vec_u8) {0x0, 0x10, 0,
                                      0x1, 0x11, 0,
                                      0x2, 0x12, 0,
                                      0x3, 0x13, 0,
                                      0x4, 0x14, 0,
                                      0x5 };
    const vec_u8 perm3rg1 = (vec_u8) {     0x15, 0,
                                      0x6, 0x16, 0,
                                      0x7, 0x17, 0 };
    const vec_u8 perm3tb0 = (vec_u8) {0x0, 0x1, 0x10,
                                      0x3, 0x4, 0x11,
                                      0x6, 0x7, 0x12,
                                      0x9, 0xa, 0x13,
                                      0xc, 0xd, 0x14,
                                      0xf };
    const vec_u8 perm3tb1 = (vec_u8) {     0x0, 0x15,
                                      0x2, 0x3, 0x16,
                                      0x5, 0x6, 0x17 };

    ad = vec_splats((uint8_t) 255);

    for (i = 0; i < lumFilterSize; i++)
        vlumFilter[i] = vec_splats(lumFilter[i]);
    for (i = 0; i < chrFilterSize; i++)
        vchrFilter[i] = vec_splats(chrFilter[i]);

    for (i = 0; i < dstW; i += 8) {
        vy32_l =
        vy32_r = ystart;
        vu32_l =
        vu32_r =
        vv32_l =
        vv32_r = uvstart;

        for (j = 0; j < lumFilterSize; j++) {
            vv = vec_ld(0, &lumSrc[j][i]);
            tmp = vec_mule(vv, vlumFilter[j]);
            tmp2 = vec_mulo(vv, vlumFilter[j]);
            tmp3 = vec_mergeh(tmp, tmp2);
            tmp4 = vec_mergel(tmp, tmp2);

            vy32_l = vec_adds(vy32_l, tmp3);
            vy32_r = vec_adds(vy32_r, tmp4);
        }

        for (j = 0; j < chrFilterSize; j++) {
            vv = vec_ld(0, &chrUSrc[j][i]);
            tmp = vec_mule(vv, vchrFilter[j]);
            tmp2 = vec_mulo(vv, vchrFilter[j]);
            tmp3 = vec_mergeh(tmp, tmp2);
            tmp4 = vec_mergel(tmp, tmp2);

            vu32_l = vec_adds(vu32_l, tmp3);
            vu32_r = vec_adds(vu32_r, tmp4);

            vv = vec_ld(0, &chrVSrc[j][i]);
            tmp = vec_mule(vv, vchrFilter[j]);
            tmp2 = vec_mulo(vv, vchrFilter[j]);
            tmp3 = vec_mergeh(tmp, tmp2);
            tmp4 = vec_mergel(tmp, tmp2);

            vv32_l = vec_adds(vv32_l, tmp3);
            vv32_r = vec_adds(vv32_r, tmp4);
        }

        vy32_l = vec_sra(vy32_l, shift10);
        vy32_r = vec_sra(vy32_r, shift10);
        vu32_l = vec_sra(vu32_l, shift10);
        vu32_r = vec_sra(vu32_r, shift10);
        vv32_l = vec_sra(vv32_l, shift10);
        vv32_r = vec_sra(vv32_r, shift10);

        vy32_l = vec_sub(vy32_l, y_offset);
        vy32_r = vec_sub(vy32_r, y_offset);
        vy32_l = vec_mul(vy32_l, y_coeff);
        vy32_r = vec_mul(vy32_r, y_coeff);
        vy32_l = vec_add(vy32_l, y_add);
        vy32_r = vec_add(vy32_r, y_add);

        R_l = vec_mul(vv32_l, v2r_coeff);
        R_l = vec_add(R_l, vy32_l);
        R_r = vec_mul(vv32_r, v2r_coeff);
        R_r = vec_add(R_r, vy32_r);
        G_l = vec_mul(vv32_l, v2g_coeff);
        tmp32 = vec_mul(vu32_l, u2g_coeff);
        G_l = vec_add(G_l, vy32_l);
        G_l = vec_add(G_l, tmp32);
        G_r = vec_mul(vv32_r, v2g_coeff);
        tmp32 = vec_mul(vu32_r, u2g_coeff);
        G_r = vec_add(G_r, vy32_r);
        G_r = vec_add(G_r, tmp32);

        B_l = vec_mul(vu32_l, u2b_coeff);
        B_l = vec_add(B_l, vy32_l);
        B_r = vec_mul(vu32_r, u2b_coeff);
        B_r = vec_add(B_r, vy32_r);

        WRITERGB
    }
}

#define SETUP(x, buf0, alpha1, buf1, alpha) { \
    x = vec_ld(0, buf0); \
    tmp = vec_mule(x, alpha1); \
    tmp2 = vec_mulo(x, alpha1); \
    tmp3 = vec_mergeh(tmp, tmp2); \
    tmp4 = vec_mergel(tmp, tmp2); \
\
    x = vec_ld(0, buf1); \
    tmp = vec_mule(x, alpha); \
    tmp2 = vec_mulo(x, alpha); \
    tmp5 = vec_mergeh(tmp, tmp2); \
    tmp6 = vec_mergel(tmp, tmp2); \
\
    tmp3 = vec_add(tmp3, tmp5); \
    tmp4 = vec_add(tmp4, tmp6); \
}


static av_always_inline void
yuv2rgb_full_2_vsx_template(SwsContext *c, const int16_t *buf[2],
                     const int16_t *ubuf[2], const int16_t *vbuf[2],
                     const int16_t *abuf[2], uint8_t *dest, int dstW,
                     int yalpha, int uvalpha, int y,
                     enum AVPixelFormat target, int hasAlpha)
{
    const int16_t *buf0  = buf[0],  *buf1  = buf[1],
                  *ubuf0 = ubuf[0], *ubuf1 = ubuf[1],
                  *vbuf0 = vbuf[0], *vbuf1 = vbuf[1],
                  *abuf0 = hasAlpha ? abuf[0] : NULL,
                  *abuf1 = hasAlpha ? abuf[1] : NULL;
    const int16_t  yalpha1 = 4096 - yalpha;
    const int16_t uvalpha1 = 4096 - uvalpha;
    vec_s16 vy, vu, vv, A = vec_splat_s16(0);
    vec_s32 vy32_l, vy32_r, vu32_l, vu32_r, vv32_l, vv32_r, tmp32;
    vec_s32 R_l, R_r, G_l, G_r, B_l, B_r;
    vec_s32 tmp, tmp2, tmp3, tmp4, tmp5, tmp6;
    vec_u16 rd16, gd16, bd16;
    vec_u8 rd, bd, gd, ad, out0, out1, tmp8;
    const vec_s16 vyalpha1 = vec_splats(yalpha1);
    const vec_s16 vuvalpha1 = vec_splats(uvalpha1);
    const vec_s16 vyalpha = vec_splats((int16_t) yalpha);
    const vec_s16 vuvalpha = vec_splats((int16_t) uvalpha);
    const vec_u16 zero16 = vec_splat_u16(0);
    const vec_s32 y_offset = vec_splats(c->yuv2rgb_y_offset);
    const vec_s32 y_coeff = vec_splats(c->yuv2rgb_y_coeff);
    const vec_s32 y_add = vec_splats(1 << 21);
    const vec_s32 v2r_coeff = vec_splats(c->yuv2rgb_v2r_coeff);
    const vec_s32 v2g_coeff = vec_splats(c->yuv2rgb_v2g_coeff);
    const vec_s32 u2g_coeff = vec_splats(c->yuv2rgb_u2g_coeff);
    const vec_s32 u2b_coeff = vec_splats(c->yuv2rgb_u2b_coeff);
    const vec_s32 rgbclip = vec_splats(1 << 30);
    const vec_s32 zero32 = vec_splat_s32(0);
    const vec_u32 shift19 = vec_splats(19U);
    const vec_u32 shift22 = vec_splats(22U);
    const vec_u32 shift10 = vec_splat_u32(10);
    const vec_s32 dec128 = vec_splats(128 << 19);
    const vec_s32 add18 = vec_splats(1 << 18);
    int i;

    // Various permutations
    const vec_u8 perm3rg0 = (vec_u8) {0x0, 0x10, 0,
                                      0x1, 0x11, 0,
                                      0x2, 0x12, 0,
                                      0x3, 0x13, 0,
                                      0x4, 0x14, 0,
                                      0x5 };
    const vec_u8 perm3rg1 = (vec_u8) {     0x15, 0,
                                      0x6, 0x16, 0,
                                      0x7, 0x17, 0 };
    const vec_u8 perm3tb0 = (vec_u8) {0x0, 0x1, 0x10,
                                      0x3, 0x4, 0x11,
                                      0x6, 0x7, 0x12,
                                      0x9, 0xa, 0x13,
                                      0xc, 0xd, 0x14,
                                      0xf };
    const vec_u8 perm3tb1 = (vec_u8) {     0x0, 0x15,
                                      0x2, 0x3, 0x16,
                                      0x5, 0x6, 0x17 };

    av_assert2(yalpha  <= 4096U);
    av_assert2(uvalpha <= 4096U);

    for (i = 0; i < dstW; i += 8) {
        SETUP(vy, &buf0[i], vyalpha1, &buf1[i], vyalpha);
        vy32_l = vec_sra(tmp3, shift10);
        vy32_r = vec_sra(tmp4, shift10);

        SETUP(vu, &ubuf0[i], vuvalpha1, &ubuf1[i], vuvalpha);
        tmp3 = vec_sub(tmp3, dec128);
        tmp4 = vec_sub(tmp4, dec128);
        vu32_l = vec_sra(tmp3, shift10);
        vu32_r = vec_sra(tmp4, shift10);

        SETUP(vv, &vbuf0[i], vuvalpha1, &vbuf1[i], vuvalpha);
        tmp3 = vec_sub(tmp3, dec128);
        tmp4 = vec_sub(tmp4, dec128);
        vv32_l = vec_sra(tmp3, shift10);
        vv32_r = vec_sra(tmp4, shift10);

        if (hasAlpha) {
            SETUP(A, &abuf0[i], vyalpha1, &abuf1[i], vyalpha);
            tmp3 = vec_add(tmp3, add18);
            tmp4 = vec_add(tmp4, add18);
            tmp3 = vec_sra(tmp3, shift19);
            tmp4 = vec_sra(tmp4, shift19);
            A = vec_packs(tmp3, tmp4);
            ad = vec_packsu(A, (vec_s16) zero16);
        } else {
            ad = vec_splats((uint8_t) 255);
        }

        vy32_l = vec_sub(vy32_l, y_offset);
        vy32_r = vec_sub(vy32_r, y_offset);
        vy32_l = vec_mul(vy32_l, y_coeff);
        vy32_r = vec_mul(vy32_r, y_coeff);
        vy32_l = vec_add(vy32_l, y_add);
        vy32_r = vec_add(vy32_r, y_add);

        R_l = vec_mul(vv32_l, v2r_coeff);
        R_l = vec_add(R_l, vy32_l);
        R_r = vec_mul(vv32_r, v2r_coeff);
        R_r = vec_add(R_r, vy32_r);
        G_l = vec_mul(vv32_l, v2g_coeff);
        tmp32 = vec_mul(vu32_l, u2g_coeff);
        G_l = vec_add(G_l, vy32_l);
        G_l = vec_add(G_l, tmp32);
        G_r = vec_mul(vv32_r, v2g_coeff);
        tmp32 = vec_mul(vu32_r, u2g_coeff);
        G_r = vec_add(G_r, vy32_r);
        G_r = vec_add(G_r, tmp32);

        B_l = vec_mul(vu32_l, u2b_coeff);
        B_l = vec_add(B_l, vy32_l);
        B_r = vec_mul(vu32_r, u2b_coeff);
        B_r = vec_add(B_r, vy32_r);

        WRITERGB
    }
}

static av_always_inline void
yuv2rgb_2_vsx_template(SwsContext *c, const int16_t *buf[2],
                     const int16_t *ubuf[2], const int16_t *vbuf[2],
                     const int16_t *abuf[2], uint8_t *dest, int dstW,
                     int yalpha, int uvalpha, int y,
                     enum AVPixelFormat target, int hasAlpha)
{
    const int16_t *buf0  = buf[0],  *buf1  = buf[1],
                  *ubuf0 = ubuf[0], *ubuf1 = ubuf[1],
                  *vbuf0 = vbuf[0], *vbuf1 = vbuf[1],
                  *abuf0 = hasAlpha ? abuf[0] : NULL,
                  *abuf1 = hasAlpha ? abuf[1] : NULL;
    const int16_t  yalpha1 = 4096 - yalpha;
    const int16_t uvalpha1 = 4096 - uvalpha;
    vec_s16 vy, vu, vv, A = vec_splat_s16(0);
    vec_s32 vy32_l, vy32_r, vu32_l, vu32_r, vv32_l, vv32_r, tmp32;
    vec_s32 R_l, R_r, G_l, G_r, B_l, B_r, vud32_l, vud32_r, vvd32_l, vvd32_r;
    vec_s32 tmp, tmp2, tmp3, tmp4, tmp5, tmp6;
    vec_u16 rd16, gd16, bd16;
    vec_u8 rd, bd, gd, ad, out0, out1, tmp8;
    const vec_s16 vyalpha1 = vec_splats(yalpha1);
    const vec_s16 vuvalpha1 = vec_splats(uvalpha1);
    const vec_s16 vyalpha = vec_splats((int16_t) yalpha);
    const vec_s16 vuvalpha = vec_splats((int16_t) uvalpha);
    const vec_u16 zero16 = vec_splat_u16(0);
    const vec_s32 y_offset = vec_splats(c->yuv2rgb_y_offset);
    const vec_s32 y_coeff = vec_splats(c->yuv2rgb_y_coeff);
    const vec_s32 y_add = vec_splats(1 << 21);
    const vec_s32 v2r_coeff = vec_splats(c->yuv2rgb_v2r_coeff);
    const vec_s32 v2g_coeff = vec_splats(c->yuv2rgb_v2g_coeff);
    const vec_s32 u2g_coeff = vec_splats(c->yuv2rgb_u2g_coeff);
    const vec_s32 u2b_coeff = vec_splats(c->yuv2rgb_u2b_coeff);
    const vec_s32 rgbclip = vec_splats(1 << 30);
    const vec_s32 zero32 = vec_splat_s32(0);
    const vec_u32 shift19 = vec_splats(19U);
    const vec_u32 shift22 = vec_splats(22U);
    const vec_u32 shift10 = vec_splat_u32(10);
    const vec_s32 dec128 = vec_splats(128 << 19);
    const vec_s32 add18 = vec_splats(1 << 18);
    int i;

    // Various permutations
    const vec_u8 doubleleft = (vec_u8) {0, 1, 2, 3,
                                        0, 1, 2, 3,
                                        4, 5, 6, 7,
                                        4, 5, 6, 7 };
    const vec_u8 doubleright = (vec_u8) {8, 9, 10, 11,
                                         8, 9, 10, 11,
                                         12, 13, 14, 15,
                                         12, 13, 14, 15 };
    const vec_u8 perm3rg0 = (vec_u8) {0x0, 0x10, 0,
                                      0x1, 0x11, 0,
                                      0x2, 0x12, 0,
                                      0x3, 0x13, 0,
                                      0x4, 0x14, 0,
                                      0x5 };
    const vec_u8 perm3rg1 = (vec_u8) {     0x15, 0,
                                      0x6, 0x16, 0,
                                      0x7, 0x17, 0 };
    const vec_u8 perm3tb0 = (vec_u8) {0x0, 0x1, 0x10,
                                      0x3, 0x4, 0x11,
                                      0x6, 0x7, 0x12,
                                      0x9, 0xa, 0x13,
                                      0xc, 0xd, 0x14,
                                      0xf };
    const vec_u8 perm3tb1 = (vec_u8) {     0x0, 0x15,
                                      0x2, 0x3, 0x16,
                                      0x5, 0x6, 0x17 };

    av_assert2(yalpha  <= 4096U);
    av_assert2(uvalpha <= 4096U);

    for (i = 0; i < (dstW + 1) >> 1; i += 8) {
        SETUP(vy, &buf0[i * 2], vyalpha1, &buf1[i * 2], vyalpha);
        vy32_l = vec_sra(tmp3, shift10);
        vy32_r = vec_sra(tmp4, shift10);

        SETUP(vu, &ubuf0[i], vuvalpha1, &ubuf1[i], vuvalpha);
        tmp3 = vec_sub(tmp3, dec128);
        tmp4 = vec_sub(tmp4, dec128);
        vu32_l = vec_sra(tmp3, shift10);
        vu32_r = vec_sra(tmp4, shift10);

        SETUP(vv, &vbuf0[i], vuvalpha1, &vbuf1[i], vuvalpha);
        tmp3 = vec_sub(tmp3, dec128);
        tmp4 = vec_sub(tmp4, dec128);
        vv32_l = vec_sra(tmp3, shift10);
        vv32_r = vec_sra(tmp4, shift10);

        if (hasAlpha) {
            SETUP(A, &abuf0[i], vyalpha1, &abuf1[i], vyalpha);
            tmp3 = vec_add(tmp3, add18);
            tmp4 = vec_add(tmp4, add18);
            tmp3 = vec_sra(tmp3, shift19);
            tmp4 = vec_sra(tmp4, shift19);
            A = vec_packs(tmp3, tmp4);
            ad = vec_packsu(A, (vec_s16) zero16);
        } else {
            ad = vec_splats((uint8_t) 255);
        }

        vy32_l = vec_sub(vy32_l, y_offset);
        vy32_r = vec_sub(vy32_r, y_offset);
        vy32_l = vec_mul(vy32_l, y_coeff);
        vy32_r = vec_mul(vy32_r, y_coeff);
        vy32_l = vec_add(vy32_l, y_add);
        vy32_r = vec_add(vy32_r, y_add);

        // Use the first UV half
        vud32_l = vec_perm(vu32_l, vu32_l, doubleleft);
        vud32_r = vec_perm(vu32_l, vu32_l, doubleright);
        vvd32_l = vec_perm(vv32_l, vv32_l, doubleleft);
        vvd32_r = vec_perm(vv32_l, vv32_l, doubleright);

        R_l = vec_mul(vvd32_l, v2r_coeff);
        R_l = vec_add(R_l, vy32_l);
        R_r = vec_mul(vvd32_r, v2r_coeff);
        R_r = vec_add(R_r, vy32_r);
        G_l = vec_mul(vvd32_l, v2g_coeff);
        tmp32 = vec_mul(vud32_l, u2g_coeff);
        G_l = vec_add(G_l, vy32_l);
        G_l = vec_add(G_l, tmp32);
        G_r = vec_mul(vvd32_r, v2g_coeff);
        tmp32 = vec_mul(vud32_r, u2g_coeff);
        G_r = vec_add(G_r, vy32_r);
        G_r = vec_add(G_r, tmp32);

        B_l = vec_mul(vud32_l, u2b_coeff);
        B_l = vec_add(B_l, vy32_l);
        B_r = vec_mul(vud32_r, u2b_coeff);
        B_r = vec_add(B_r, vy32_r);

        WRITERGB

        // New Y for the second half
        SETUP(vy, &buf0[i * 2 + 8], vyalpha1, &buf1[i * 2 + 8], vyalpha);
        vy32_l = vec_sra(tmp3, shift10);
        vy32_r = vec_sra(tmp4, shift10);

        vy32_l = vec_sub(vy32_l, y_offset);
        vy32_r = vec_sub(vy32_r, y_offset);
        vy32_l = vec_mul(vy32_l, y_coeff);
        vy32_r = vec_mul(vy32_r, y_coeff);
        vy32_l = vec_add(vy32_l, y_add);
        vy32_r = vec_add(vy32_r, y_add);

        // Second UV half
        vud32_l = vec_perm(vu32_r, vu32_r, doubleleft);
        vud32_r = vec_perm(vu32_r, vu32_r, doubleright);
        vvd32_l = vec_perm(vv32_r, vv32_r, doubleleft);
        vvd32_r = vec_perm(vv32_r, vv32_r, doubleright);

        R_l = vec_mul(vvd32_l, v2r_coeff);
        R_l = vec_add(R_l, vy32_l);
        R_r = vec_mul(vvd32_r, v2r_coeff);
        R_r = vec_add(R_r, vy32_r);
        G_l = vec_mul(vvd32_l, v2g_coeff);
        tmp32 = vec_mul(vud32_l, u2g_coeff);
        G_l = vec_add(G_l, vy32_l);
        G_l = vec_add(G_l, tmp32);
        G_r = vec_mul(vvd32_r, v2g_coeff);
        tmp32 = vec_mul(vud32_r, u2g_coeff);
        G_r = vec_add(G_r, vy32_r);
        G_r = vec_add(G_r, tmp32);

        B_l = vec_mul(vud32_l, u2b_coeff);
        B_l = vec_add(B_l, vy32_l);
        B_r = vec_mul(vud32_r, u2b_coeff);
        B_r = vec_add(B_r, vy32_r);

        WRITERGB
    }
}

#undef SETUP

static av_always_inline void
yuv2rgb_full_1_vsx_template(SwsContext *c, const int16_t *buf0,
                     const int16_t *ubuf[2], const int16_t *vbuf[2],
                     const int16_t *abuf0, uint8_t *dest, int dstW,
                     int uvalpha, int y, enum AVPixelFormat target,
                     int hasAlpha)
{
    const int16_t *ubuf0 = ubuf[0], *vbuf0 = vbuf[0];
    const int16_t *ubuf1 = ubuf[1], *vbuf1 = vbuf[1];
    vec_s16 vy, vu, vv, A = vec_splat_s16(0), tmp16;
    vec_s32 vy32_l, vy32_r, vu32_l, vu32_r, vv32_l, vv32_r, tmp32, tmp32_2;
    vec_s32 R_l, R_r, G_l, G_r, B_l, B_r;
    vec_u16 rd16, gd16, bd16;
    vec_u8 rd, bd, gd, ad, out0, out1, tmp8;
    const vec_u16 zero16 = vec_splat_u16(0);
    const vec_s32 y_offset = vec_splats(c->yuv2rgb_y_offset);
    const vec_s32 y_coeff = vec_splats(c->yuv2rgb_y_coeff);
    const vec_s32 y_add = vec_splats(1 << 21);
    const vec_s32 v2r_coeff = vec_splats(c->yuv2rgb_v2r_coeff);
    const vec_s32 v2g_coeff = vec_splats(c->yuv2rgb_v2g_coeff);
    const vec_s32 u2g_coeff = vec_splats(c->yuv2rgb_u2g_coeff);
    const vec_s32 u2b_coeff = vec_splats(c->yuv2rgb_u2b_coeff);
    const vec_s32 rgbclip = vec_splats(1 << 30);
    const vec_s32 zero32 = vec_splat_s32(0);
    const vec_u32 shift2 = vec_splat_u32(2);
    const vec_u32 shift22 = vec_splats(22U);
    const vec_u16 sub7 = vec_splats((uint16_t) (128 << 7));
    const vec_u16 sub8 = vec_splats((uint16_t) (128 << 8));
    const vec_s16 mul4 = vec_splat_s16(4);
    const vec_s16 mul8 = vec_splat_s16(8);
    const vec_s16 add64 = vec_splat_s16(64);
    const vec_u16 shift7 = vec_splat_u16(7);
    const vec_s16 max255 = vec_splat_s16(255);
    int i;

    // Various permutations
    const vec_u8 perm3rg0 = (vec_u8) {0x0, 0x10, 0,
                                      0x1, 0x11, 0,
                                      0x2, 0x12, 0,
                                      0x3, 0x13, 0,
                                      0x4, 0x14, 0,
                                      0x5 };
    const vec_u8 perm3rg1 = (vec_u8) {     0x15, 0,
                                      0x6, 0x16, 0,
                                      0x7, 0x17, 0 };
    const vec_u8 perm3tb0 = (vec_u8) {0x0, 0x1, 0x10,
                                      0x3, 0x4, 0x11,
                                      0x6, 0x7, 0x12,
                                      0x9, 0xa, 0x13,
                                      0xc, 0xd, 0x14,
                                      0xf };
    const vec_u8 perm3tb1 = (vec_u8) {     0x0, 0x15,
                                      0x2, 0x3, 0x16,
                                      0x5, 0x6, 0x17 };

    for (i = 0; i < dstW; i += 8) { // The x86 asm also overwrites padding bytes.
        vy = vec_ld(0, &buf0[i]);
        vy32_l = vec_unpackh(vy);
        vy32_r = vec_unpackl(vy);
        vy32_l = vec_sl(vy32_l, shift2);
        vy32_r = vec_sl(vy32_r, shift2);

        vu = vec_ld(0, &ubuf0[i]);
        vv = vec_ld(0, &vbuf0[i]);
        if (uvalpha < 2048) {
            vu = (vec_s16) vec_sub((vec_u16) vu, sub7);
            vv = (vec_s16) vec_sub((vec_u16) vv, sub7);

            tmp32 = vec_mule(vu, mul4);
            tmp32_2 = vec_mulo(vu, mul4);
            vu32_l = vec_mergeh(tmp32, tmp32_2);
            vu32_r = vec_mergel(tmp32, tmp32_2);
            tmp32 = vec_mule(vv, mul4);
            tmp32_2 = vec_mulo(vv, mul4);
            vv32_l = vec_mergeh(tmp32, tmp32_2);
            vv32_r = vec_mergel(tmp32, tmp32_2);
        } else {
            tmp16 = vec_ld(0, &ubuf1[i]);
            vu = vec_add(vu, tmp16);
            vu = (vec_s16) vec_sub((vec_u16) vu, sub8);
            tmp16 = vec_ld(0, &vbuf1[i]);
            vv = vec_add(vv, tmp16);
            vv = (vec_s16) vec_sub((vec_u16) vv, sub8);

            vu32_l = vec_mule(vu, mul8);
            vu32_r = vec_mulo(vu, mul8);
            vv32_l = vec_mule(vv, mul8);
            vv32_r = vec_mulo(vv, mul8);
        }

        if (hasAlpha) {
            A = vec_ld(0, &abuf0[i]);
            A = vec_add(A, add64);
            A = vec_sr(A, shift7);
            A = vec_max(A, max255);
            ad = vec_packsu(A, (vec_s16) zero16);
        } else {
            ad = vec_splats((uint8_t) 255);
        }

        vy32_l = vec_sub(vy32_l, y_offset);
        vy32_r = vec_sub(vy32_r, y_offset);
        vy32_l = vec_mul(vy32_l, y_coeff);
        vy32_r = vec_mul(vy32_r, y_coeff);
        vy32_l = vec_add(vy32_l, y_add);
        vy32_r = vec_add(vy32_r, y_add);

        R_l = vec_mul(vv32_l, v2r_coeff);
        R_l = vec_add(R_l, vy32_l);
        R_r = vec_mul(vv32_r, v2r_coeff);
        R_r = vec_add(R_r, vy32_r);
        G_l = vec_mul(vv32_l, v2g_coeff);
        tmp32 = vec_mul(vu32_l, u2g_coeff);
        G_l = vec_add(G_l, vy32_l);
        G_l = vec_add(G_l, tmp32);
        G_r = vec_mul(vv32_r, v2g_coeff);
        tmp32 = vec_mul(vu32_r, u2g_coeff);
        G_r = vec_add(G_r, vy32_r);
        G_r = vec_add(G_r, tmp32);

        B_l = vec_mul(vu32_l, u2b_coeff);
        B_l = vec_add(B_l, vy32_l);
        B_r = vec_mul(vu32_r, u2b_coeff);
        B_r = vec_add(B_r, vy32_r);

        WRITERGB
    }
}

static av_always_inline void
yuv2rgb_1_vsx_template(SwsContext *c, const int16_t *buf0,
                     const int16_t *ubuf[2], const int16_t *vbuf[2],
                     const int16_t *abuf0, uint8_t *dest, int dstW,
                     int uvalpha, int y, enum AVPixelFormat target,
                     int hasAlpha)
{
    const int16_t *ubuf0 = ubuf[0], *vbuf0 = vbuf[0];
    const int16_t *ubuf1 = ubuf[1], *vbuf1 = vbuf[1];
    vec_s16 vy, vu, vv, A = vec_splat_s16(0), tmp16;
    vec_s32 vy32_l, vy32_r, vu32_l, vu32_r, vv32_l, vv32_r, tmp32, tmp32_2;
    vec_s32 vud32_l, vud32_r, vvd32_l, vvd32_r;
    vec_s32 R_l, R_r, G_l, G_r, B_l, B_r;
    vec_u16 rd16, gd16, bd16;
    vec_u8 rd, bd, gd, ad, out0, out1, tmp8;
    const vec_u16 zero16 = vec_splat_u16(0);
    const vec_s32 y_offset = vec_splats(c->yuv2rgb_y_offset);
    const vec_s32 y_coeff = vec_splats(c->yuv2rgb_y_coeff);
    const vec_s32 y_add = vec_splats(1 << 21);
    const vec_s32 v2r_coeff = vec_splats(c->yuv2rgb_v2r_coeff);
    const vec_s32 v2g_coeff = vec_splats(c->yuv2rgb_v2g_coeff);
    const vec_s32 u2g_coeff = vec_splats(c->yuv2rgb_u2g_coeff);
    const vec_s32 u2b_coeff = vec_splats(c->yuv2rgb_u2b_coeff);
    const vec_s32 rgbclip = vec_splats(1 << 30);
    const vec_s32 zero32 = vec_splat_s32(0);
    const vec_u32 shift2 = vec_splat_u32(2);
    const vec_u32 shift22 = vec_splats(22U);
    const vec_u16 sub7 = vec_splats((uint16_t) (128 << 7));
    const vec_u16 sub8 = vec_splats((uint16_t) (128 << 8));
    const vec_s16 mul4 = vec_splat_s16(4);
    const vec_s16 mul8 = vec_splat_s16(8);
    const vec_s16 add64 = vec_splat_s16(64);
    const vec_u16 shift7 = vec_splat_u16(7);
    const vec_s16 max255 = vec_splat_s16(255);
    int i;

    // Various permutations
    const vec_u8 doubleleft = (vec_u8) {0, 1, 2, 3,
                                        0, 1, 2, 3,
                                        4, 5, 6, 7,
                                        4, 5, 6, 7 };
    const vec_u8 doubleright = (vec_u8) {8, 9, 10, 11,
                                         8, 9, 10, 11,
                                         12, 13, 14, 15,
                                         12, 13, 14, 15 };
    const vec_u8 perm3rg0 = (vec_u8) {0x0, 0x10, 0,
                                      0x1, 0x11, 0,
                                      0x2, 0x12, 0,
                                      0x3, 0x13, 0,
                                      0x4, 0x14, 0,
                                      0x5 };
    const vec_u8 perm3rg1 = (vec_u8) {     0x15, 0,
                                      0x6, 0x16, 0,
                                      0x7, 0x17, 0 };
    const vec_u8 perm3tb0 = (vec_u8) {0x0, 0x1, 0x10,
                                      0x3, 0x4, 0x11,
                                      0x6, 0x7, 0x12,
                                      0x9, 0xa, 0x13,
                                      0xc, 0xd, 0x14,
                                      0xf };
    const vec_u8 perm3tb1 = (vec_u8) {     0x0, 0x15,
                                      0x2, 0x3, 0x16,
                                      0x5, 0x6, 0x17 };

    for (i = 0; i < (dstW + 1) >> 1; i += 8) { // The x86 asm also overwrites padding bytes.
        vy = vec_ld(0, &buf0[i * 2]);
        vy32_l = vec_unpackh(vy);
        vy32_r = vec_unpackl(vy);
        vy32_l = vec_sl(vy32_l, shift2);
        vy32_r = vec_sl(vy32_r, shift2);

        vu = vec_ld(0, &ubuf0[i]);
        vv = vec_ld(0, &vbuf0[i]);
        if (uvalpha < 2048) {
            vu = (vec_s16) vec_sub((vec_u16) vu, sub7);
            vv = (vec_s16) vec_sub((vec_u16) vv, sub7);

            tmp32 = vec_mule(vu, mul4);
            tmp32_2 = vec_mulo(vu, mul4);
            vu32_l = vec_mergeh(tmp32, tmp32_2);
            vu32_r = vec_mergel(tmp32, tmp32_2);
            tmp32 = vec_mule(vv, mul4);
            tmp32_2 = vec_mulo(vv, mul4);
            vv32_l = vec_mergeh(tmp32, tmp32_2);
            vv32_r = vec_mergel(tmp32, tmp32_2);
        } else {
            tmp16 = vec_ld(0, &ubuf1[i]);
            vu = vec_add(vu, tmp16);
            vu = (vec_s16) vec_sub((vec_u16) vu, sub8);
            tmp16 = vec_ld(0, &vbuf1[i]);
            vv = vec_add(vv, tmp16);
            vv = (vec_s16) vec_sub((vec_u16) vv, sub8);

            vu32_l = vec_mule(vu, mul8);
            vu32_r = vec_mulo(vu, mul8);
            vv32_l = vec_mule(vv, mul8);
            vv32_r = vec_mulo(vv, mul8);
        }

        if (hasAlpha) {
            A = vec_ld(0, &abuf0[i]);
            A = vec_add(A, add64);
            A = vec_sr(A, shift7);
            A = vec_max(A, max255);
            ad = vec_packsu(A, (vec_s16) zero16);
        } else {
            ad = vec_splats((uint8_t) 255);
        }

        vy32_l = vec_sub(vy32_l, y_offset);
        vy32_r = vec_sub(vy32_r, y_offset);
        vy32_l = vec_mul(vy32_l, y_coeff);
        vy32_r = vec_mul(vy32_r, y_coeff);
        vy32_l = vec_add(vy32_l, y_add);
        vy32_r = vec_add(vy32_r, y_add);

        // Use the first UV half
        vud32_l = vec_perm(vu32_l, vu32_l, doubleleft);
        vud32_r = vec_perm(vu32_l, vu32_l, doubleright);
        vvd32_l = vec_perm(vv32_l, vv32_l, doubleleft);
        vvd32_r = vec_perm(vv32_l, vv32_l, doubleright);

        R_l = vec_mul(vvd32_l, v2r_coeff);
        R_l = vec_add(R_l, vy32_l);
        R_r = vec_mul(vvd32_r, v2r_coeff);
        R_r = vec_add(R_r, vy32_r);
        G_l = vec_mul(vvd32_l, v2g_coeff);
        tmp32 = vec_mul(vud32_l, u2g_coeff);
        G_l = vec_add(G_l, vy32_l);
        G_l = vec_add(G_l, tmp32);
        G_r = vec_mul(vvd32_r, v2g_coeff);
        tmp32 = vec_mul(vud32_r, u2g_coeff);
        G_r = vec_add(G_r, vy32_r);
        G_r = vec_add(G_r, tmp32);

        B_l = vec_mul(vud32_l, u2b_coeff);
        B_l = vec_add(B_l, vy32_l);
        B_r = vec_mul(vud32_r, u2b_coeff);
        B_r = vec_add(B_r, vy32_r);

        WRITERGB

        // New Y for the second half
        vy = vec_ld(16, &buf0[i * 2]);
        vy32_l = vec_unpackh(vy);
        vy32_r = vec_unpackl(vy);
        vy32_l = vec_sl(vy32_l, shift2);
        vy32_r = vec_sl(vy32_r, shift2);

        vy32_l = vec_sub(vy32_l, y_offset);
        vy32_r = vec_sub(vy32_r, y_offset);
        vy32_l = vec_mul(vy32_l, y_coeff);
        vy32_r = vec_mul(vy32_r, y_coeff);
        vy32_l = vec_add(vy32_l, y_add);
        vy32_r = vec_add(vy32_r, y_add);

        // Second UV half
        vud32_l = vec_perm(vu32_r, vu32_r, doubleleft);
        vud32_r = vec_perm(vu32_r, vu32_r, doubleright);
        vvd32_l = vec_perm(vv32_r, vv32_r, doubleleft);
        vvd32_r = vec_perm(vv32_r, vv32_r, doubleright);

        R_l = vec_mul(vvd32_l, v2r_coeff);
        R_l = vec_add(R_l, vy32_l);
        R_r = vec_mul(vvd32_r, v2r_coeff);
        R_r = vec_add(R_r, vy32_r);
        G_l = vec_mul(vvd32_l, v2g_coeff);
        tmp32 = vec_mul(vud32_l, u2g_coeff);
        G_l = vec_add(G_l, vy32_l);
        G_l = vec_add(G_l, tmp32);
        G_r = vec_mul(vvd32_r, v2g_coeff);
        tmp32 = vec_mul(vud32_r, u2g_coeff);
        G_r = vec_add(G_r, vy32_r);
        G_r = vec_add(G_r, tmp32);

        B_l = vec_mul(vud32_l, u2b_coeff);
        B_l = vec_add(B_l, vy32_l);
        B_r = vec_mul(vud32_r, u2b_coeff);
        B_r = vec_add(B_r, vy32_r);

        WRITERGB
    }
}

#undef WRITERGB

#define YUV2RGBWRAPPERX(name, base, ext, fmt, hasAlpha) \
static void name ## ext ## _X_vsx(SwsContext *c, const int16_t *lumFilter, \
                                const int16_t **lumSrc, int lumFilterSize, \
                                const int16_t *chrFilter, const int16_t **chrUSrc, \
                                const int16_t **chrVSrc, int chrFilterSize, \
                                const int16_t **alpSrc, uint8_t *dest, int dstW, \
                                int y) \
{ \
    name ## base ## _X_vsx_template(c, lumFilter, lumSrc, lumFilterSize, \
                                  chrFilter, chrUSrc, chrVSrc, chrFilterSize, \
                                  alpSrc, dest, dstW, y, fmt, hasAlpha); \
}

#define YUV2RGBWRAPPERX2(name, base, ext, fmt, hasAlpha) \
static void name ## ext ## _2_vsx(SwsContext *c, const int16_t *buf[2], \
                                const int16_t *ubuf[2], const int16_t *vbuf[2], \
                                const int16_t *abuf[2], uint8_t *dest, int dstW, \
                                int yalpha, int uvalpha, int y) \
{ \
    name ## base ## _2_vsx_template(c, buf, ubuf, vbuf, abuf, \
                                  dest, dstW, yalpha, uvalpha, y, fmt, hasAlpha); \
}

#define YUV2RGBWRAPPER(name, base, ext, fmt, hasAlpha) \
static void name ## ext ## _1_vsx(SwsContext *c, const int16_t *buf0, \
                                const int16_t *ubuf[2], const int16_t *vbuf[2], \
                                const int16_t *abuf0, uint8_t *dest, int dstW, \
                                int uvalpha, int y) \
{ \
    name ## base ## _1_vsx_template(c, buf0, ubuf, vbuf, abuf0, dest, \
                                  dstW, uvalpha, y, fmt, hasAlpha); \
}

YUV2RGBWRAPPER(yuv2, rgb, bgrx32, AV_PIX_FMT_BGRA,  0)
YUV2RGBWRAPPER(yuv2, rgb, rgbx32, AV_PIX_FMT_RGBA,  0)
YUV2RGBWRAPPER(yuv2, rgb, xrgb32, AV_PIX_FMT_ARGB,  0)
YUV2RGBWRAPPER(yuv2, rgb, xbgr32, AV_PIX_FMT_ABGR,  0)

YUV2RGBWRAPPER(yuv2, rgb, rgb24, AV_PIX_FMT_RGB24,   0)
YUV2RGBWRAPPER(yuv2, rgb, bgr24, AV_PIX_FMT_BGR24,   0)

YUV2RGBWRAPPERX2(yuv2, rgb, bgrx32, AV_PIX_FMT_BGRA,  0)
YUV2RGBWRAPPERX2(yuv2, rgb, rgbx32, AV_PIX_FMT_RGBA,  0)
YUV2RGBWRAPPERX2(yuv2, rgb, xrgb32, AV_PIX_FMT_ARGB,  0)
YUV2RGBWRAPPERX2(yuv2, rgb, xbgr32, AV_PIX_FMT_ABGR,  0)

YUV2RGBWRAPPERX2(yuv2, rgb, rgb24, AV_PIX_FMT_RGB24,   0)
YUV2RGBWRAPPERX2(yuv2, rgb, bgr24, AV_PIX_FMT_BGR24,   0)

YUV2RGBWRAPPER(yuv2, rgb_full, bgrx32_full, AV_PIX_FMT_BGRA,  0)
YUV2RGBWRAPPER(yuv2, rgb_full, rgbx32_full, AV_PIX_FMT_RGBA,  0)
YUV2RGBWRAPPER(yuv2, rgb_full, xrgb32_full, AV_PIX_FMT_ARGB,  0)
YUV2RGBWRAPPER(yuv2, rgb_full, xbgr32_full, AV_PIX_FMT_ABGR,  0)

YUV2RGBWRAPPER(yuv2, rgb_full, rgb24_full,  AV_PIX_FMT_RGB24, 0)
YUV2RGBWRAPPER(yuv2, rgb_full, bgr24_full,  AV_PIX_FMT_BGR24, 0)

YUV2RGBWRAPPERX2(yuv2, rgb_full, bgrx32_full, AV_PIX_FMT_BGRA,  0)
YUV2RGBWRAPPERX2(yuv2, rgb_full, rgbx32_full, AV_PIX_FMT_RGBA,  0)
YUV2RGBWRAPPERX2(yuv2, rgb_full, xrgb32_full, AV_PIX_FMT_ARGB,  0)
YUV2RGBWRAPPERX2(yuv2, rgb_full, xbgr32_full, AV_PIX_FMT_ABGR,  0)

YUV2RGBWRAPPERX2(yuv2, rgb_full, rgb24_full,  AV_PIX_FMT_RGB24, 0)
YUV2RGBWRAPPERX2(yuv2, rgb_full, bgr24_full,  AV_PIX_FMT_BGR24, 0)

YUV2RGBWRAPPERX(yuv2, rgb_full, bgrx32_full, AV_PIX_FMT_BGRA,  0)
YUV2RGBWRAPPERX(yuv2, rgb_full, rgbx32_full, AV_PIX_FMT_RGBA,  0)
YUV2RGBWRAPPERX(yuv2, rgb_full, xrgb32_full, AV_PIX_FMT_ARGB,  0)
YUV2RGBWRAPPERX(yuv2, rgb_full, xbgr32_full, AV_PIX_FMT_ABGR,  0)

YUV2RGBWRAPPERX(yuv2, rgb_full, rgb24_full,  AV_PIX_FMT_RGB24, 0)
YUV2RGBWRAPPERX(yuv2, rgb_full, bgr24_full,  AV_PIX_FMT_BGR24, 0)

static av_always_inline void
write422(const vec_s16 vy1, const vec_s16 vy2,
         const vec_s16 vu, const vec_s16 vv,
         uint8_t *dest, const enum AVPixelFormat target)
{
    vec_u8 vd1, vd2, tmp;
    const vec_u8 yuyv1 = (vec_u8) {
                         0x0, 0x10, 0x1, 0x18,
                         0x2, 0x11, 0x3, 0x19,
                         0x4, 0x12, 0x5, 0x1a,
                         0x6, 0x13, 0x7, 0x1b };
    const vec_u8 yuyv2 = (vec_u8) {
                         0x8, 0x14, 0x9, 0x1c,
                         0xa, 0x15, 0xb, 0x1d,
                         0xc, 0x16, 0xd, 0x1e,
                         0xe, 0x17, 0xf, 0x1f };
    const vec_u8 yvyu1 = (vec_u8) {
                         0x0, 0x18, 0x1, 0x10,
                         0x2, 0x19, 0x3, 0x11,
                         0x4, 0x1a, 0x5, 0x12,
                         0x6, 0x1b, 0x7, 0x13 };
    const vec_u8 yvyu2 = (vec_u8) {
                         0x8, 0x1c, 0x9, 0x14,
                         0xa, 0x1d, 0xb, 0x15,
                         0xc, 0x1e, 0xd, 0x16,
                         0xe, 0x1f, 0xf, 0x17 };
    const vec_u8 uyvy1 = (vec_u8) {
                         0x10, 0x0, 0x18, 0x1,
                         0x11, 0x2, 0x19, 0x3,
                         0x12, 0x4, 0x1a, 0x5,
                         0x13, 0x6, 0x1b, 0x7 };
    const vec_u8 uyvy2 = (vec_u8) {
                         0x14, 0x8, 0x1c, 0x9,
                         0x15, 0xa, 0x1d, 0xb,
                         0x16, 0xc, 0x1e, 0xd,
                         0x17, 0xe, 0x1f, 0xf };

    vd1 = vec_packsu(vy1, vy2);
    vd2 = vec_packsu(vu, vv);

    switch (target) {
    case AV_PIX_FMT_YUYV422:
        tmp = vec_perm(vd1, vd2, yuyv1);
        vec_st(tmp, 0, dest);
        tmp = vec_perm(vd1, vd2, yuyv2);
        vec_st(tmp, 16, dest);
    break;
    case AV_PIX_FMT_YVYU422:
        tmp = vec_perm(vd1, vd2, yvyu1);
        vec_st(tmp, 0, dest);
        tmp = vec_perm(vd1, vd2, yvyu2);
        vec_st(tmp, 16, dest);
    break;
    case AV_PIX_FMT_UYVY422:
        tmp = vec_perm(vd1, vd2, uyvy1);
        vec_st(tmp, 0, dest);
        tmp = vec_perm(vd1, vd2, uyvy2);
        vec_st(tmp, 16, dest);
    break;
    }
}

static av_always_inline void
yuv2422_X_vsx_template(SwsContext *c, const int16_t *lumFilter,
                     const int16_t **lumSrc, int lumFilterSize,
                     const int16_t *chrFilter, const int16_t **chrUSrc,
                     const int16_t **chrVSrc, int chrFilterSize,
                     const int16_t **alpSrc, uint8_t *dest, int dstW,
                     int y, enum AVPixelFormat target)
{
    int i, j;
    vec_s16 vy1, vy2, vu, vv;
    vec_s32 vy32[4], vu32[2], vv32[2], tmp, tmp2, tmp3, tmp4;
    vec_s16 vlumFilter[MAX_FILTER_SIZE], vchrFilter[MAX_FILTER_SIZE];
    const vec_s32 start = vec_splats(1 << 18);
    const vec_u32 shift19 = vec_splats(19U);

    for (i = 0; i < lumFilterSize; i++)
        vlumFilter[i] = vec_splats(lumFilter[i]);
    for (i = 0; i < chrFilterSize; i++)
        vchrFilter[i] = vec_splats(chrFilter[i]);

    for (i = 0; i < ((dstW + 1) >> 1); i += 8) {
        vy32[0] =
        vy32[1] =
        vy32[2] =
        vy32[3] =
        vu32[0] =
        vu32[1] =
        vv32[0] =
        vv32[1] = start;

        for (j = 0; j < lumFilterSize; j++) {
            vv = vec_ld(0, &lumSrc[j][i * 2]);
            tmp = vec_mule(vv, vlumFilter[j]);
            tmp2 = vec_mulo(vv, vlumFilter[j]);
            tmp3 = vec_mergeh(tmp, tmp2);
            tmp4 = vec_mergel(tmp, tmp2);

            vy32[0] = vec_adds(vy32[0], tmp3);
            vy32[1] = vec_adds(vy32[1], tmp4);

            vv = vec_ld(0, &lumSrc[j][(i + 4) * 2]);
            tmp = vec_mule(vv, vlumFilter[j]);
            tmp2 = vec_mulo(vv, vlumFilter[j]);
            tmp3 = vec_mergeh(tmp, tmp2);
            tmp4 = vec_mergel(tmp, tmp2);

            vy32[2] = vec_adds(vy32[2], tmp3);
            vy32[3] = vec_adds(vy32[3], tmp4);
        }

        for (j = 0; j < chrFilterSize; j++) {
            vv = vec_ld(0, &chrUSrc[j][i]);
            tmp = vec_mule(vv, vchrFilter[j]);
            tmp2 = vec_mulo(vv, vchrFilter[j]);
            tmp3 = vec_mergeh(tmp, tmp2);
            tmp4 = vec_mergel(tmp, tmp2);

            vu32[0] = vec_adds(vu32[0], tmp3);
            vu32[1] = vec_adds(vu32[1], tmp4);

            vv = vec_ld(0, &chrVSrc[j][i]);
            tmp = vec_mule(vv, vchrFilter[j]);
            tmp2 = vec_mulo(vv, vchrFilter[j]);
            tmp3 = vec_mergeh(tmp, tmp2);
            tmp4 = vec_mergel(tmp, tmp2);

            vv32[0] = vec_adds(vv32[0], tmp3);
            vv32[1] = vec_adds(vv32[1], tmp4);
        }

        for (j = 0; j < 4; j++) {
            vy32[j] = vec_sra(vy32[j], shift19);
        }
        for (j = 0; j < 2; j++) {
            vu32[j] = vec_sra(vu32[j], shift19);
            vv32[j] = vec_sra(vv32[j], shift19);
        }

        vy1 = vec_packs(vy32[0], vy32[1]);
        vy2 = vec_packs(vy32[2], vy32[3]);
        vu = vec_packs(vu32[0], vu32[1]);
        vv = vec_packs(vv32[0], vv32[1]);

        write422(vy1, vy2, vu, vv, &dest[i * 4], target);
    }
}

#define SETUP(x, buf0, buf1, alpha) { \
    x = vec_ld(0, buf0); \
    tmp = vec_mule(x, alpha); \
    tmp2 = vec_mulo(x, alpha); \
    tmp3 = vec_mergeh(tmp, tmp2); \
    tmp4 = vec_mergel(tmp, tmp2); \
\
    x = vec_ld(0, buf1); \
    tmp = vec_mule(x, alpha); \
    tmp2 = vec_mulo(x, alpha); \
    tmp5 = vec_mergeh(tmp, tmp2); \
    tmp6 = vec_mergel(tmp, tmp2); \
\
    tmp3 = vec_add(tmp3, tmp5); \
    tmp4 = vec_add(tmp4, tmp6); \
\
    tmp3 = vec_sra(tmp3, shift19); \
    tmp4 = vec_sra(tmp4, shift19); \
    x = vec_packs(tmp3, tmp4); \
}

static av_always_inline void
yuv2422_2_vsx_template(SwsContext *c, const int16_t *buf[2],
                     const int16_t *ubuf[2], const int16_t *vbuf[2],
                     const int16_t *abuf[2], uint8_t *dest, int dstW,
                     int yalpha, int uvalpha, int y,
                     enum AVPixelFormat target)
{
    const int16_t *buf0  = buf[0],  *buf1  = buf[1],
                  *ubuf0 = ubuf[0], *ubuf1 = ubuf[1],
                  *vbuf0 = vbuf[0], *vbuf1 = vbuf[1];
    const int16_t  yalpha1 = 4096 - yalpha;
    const int16_t uvalpha1 = 4096 - uvalpha;
    vec_s16 vy1, vy2, vu, vv;
    vec_s32 tmp, tmp2, tmp3, tmp4, tmp5, tmp6;
    const vec_s16 vyalpha1 = vec_splats(yalpha1);
    const vec_s16 vuvalpha1 = vec_splats(uvalpha1);
    const vec_u32 shift19 = vec_splats(19U);
    int i;
    av_assert2(yalpha  <= 4096U);
    av_assert2(uvalpha <= 4096U);

    for (i = 0; i < ((dstW + 1) >> 1); i += 8) {

        SETUP(vy1, &buf0[i * 2], &buf1[i * 2], vyalpha1)
        SETUP(vy2, &buf0[(i + 4) * 2], &buf1[(i + 4) * 2], vyalpha1)
        SETUP(vu, &ubuf0[i], &ubuf1[i], vuvalpha1)
        SETUP(vv, &vbuf0[i], &vbuf1[i], vuvalpha1)

        write422(vy1, vy2, vu, vv, &dest[i * 4], target);
    }
}

#undef SETUP

static av_always_inline void
yuv2422_1_vsx_template(SwsContext *c, const int16_t *buf0,
                     const int16_t *ubuf[2], const int16_t *vbuf[2],
                     const int16_t *abuf0, uint8_t *dest, int dstW,
                     int uvalpha, int y, enum AVPixelFormat target)
{
    const int16_t *ubuf0 = ubuf[0], *vbuf0 = vbuf[0];
    vec_s16 vy1, vy2, vu, vv, tmp;
    const vec_s16 add64 = vec_splats((int16_t) 64);
    const vec_s16 add128 = vec_splats((int16_t) 128);
    const vec_u16 shift7 = vec_splat_u16(7);
    const vec_u16 shift8 = vec_splat_u16(8);
    int i;

    if (uvalpha < 2048) {
        for (i = 0; i < ((dstW + 1) >> 1); i += 8) {
            vy1 = vec_ld(0, &buf0[i * 2]);
            vy2 = vec_ld(0, &buf0[(i + 4) * 2]);
            vu = vec_ld(0, &ubuf0[i]);
            vv = vec_ld(0, &vbuf0[i]);

            vy1 = vec_add(vy1, add64);
            vy2 = vec_add(vy2, add64);
            vu = vec_add(vu, add64);
            vv = vec_add(vv, add64);

            vy1 = vec_sra(vy1, shift7);
            vy2 = vec_sra(vy2, shift7);
            vu = vec_sra(vu, shift7);
            vv = vec_sra(vv, shift7);

            write422(vy1, vy2, vu, vv, &dest[i * 4], target);
        }
    } else {
        const int16_t *ubuf1 = ubuf[1], *vbuf1 = vbuf[1];
        for (i = 0; i < ((dstW + 1) >> 1); i += 8) {
            vy1 = vec_ld(0, &buf0[i * 2]);
            vy2 = vec_ld(0, &buf0[(i + 4) * 2]);
            vu = vec_ld(0, &ubuf0[i]);
            tmp = vec_ld(0, &ubuf1[i]);
            vu = vec_adds(vu, tmp);
            vv = vec_ld(0, &vbuf0[i]);
            tmp = vec_ld(0, &vbuf1[i]);
            vv = vec_adds(vv, tmp);

            vy1 = vec_add(vy1, add64);
            vy2 = vec_add(vy2, add64);
            vu = vec_adds(vu, add128);
            vv = vec_adds(vv, add128);

            vy1 = vec_sra(vy1, shift7);
            vy2 = vec_sra(vy2, shift7);
            vu = vec_sra(vu, shift8);
            vv = vec_sra(vv, shift8);

            write422(vy1, vy2, vu, vv, &dest[i * 4], target);
        }
    }
}

#define YUV2PACKEDWRAPPERX(name, base, ext, fmt) \
static void name ## ext ## _X_vsx(SwsContext *c, const int16_t *lumFilter, \
                                const int16_t **lumSrc, int lumFilterSize, \
                                const int16_t *chrFilter, const int16_t **chrUSrc, \
                                const int16_t **chrVSrc, int chrFilterSize, \
                                const int16_t **alpSrc, uint8_t *dest, int dstW, \
                                int y) \
{ \
    name ## base ## _X_vsx_template(c, lumFilter, lumSrc, lumFilterSize, \
                                  chrFilter, chrUSrc, chrVSrc, chrFilterSize, \
                                  alpSrc, dest, dstW, y, fmt); \
}

#define YUV2PACKEDWRAPPER2(name, base, ext, fmt) \
YUV2PACKEDWRAPPERX(name, base, ext, fmt) \
static void name ## ext ## _2_vsx(SwsContext *c, const int16_t *buf[2], \
                                const int16_t *ubuf[2], const int16_t *vbuf[2], \
                                const int16_t *abuf[2], uint8_t *dest, int dstW, \
                                int yalpha, int uvalpha, int y) \
{ \
    name ## base ## _2_vsx_template(c, buf, ubuf, vbuf, abuf, \
                                  dest, dstW, yalpha, uvalpha, y, fmt); \
}

#define YUV2PACKEDWRAPPER(name, base, ext, fmt) \
YUV2PACKEDWRAPPER2(name, base, ext, fmt) \
static void name ## ext ## _1_vsx(SwsContext *c, const int16_t *buf0, \
                                const int16_t *ubuf[2], const int16_t *vbuf[2], \
                                const int16_t *abuf0, uint8_t *dest, int dstW, \
                                int uvalpha, int y) \
{ \
    name ## base ## _1_vsx_template(c, buf0, ubuf, vbuf, \
                                  abuf0, dest, dstW, uvalpha, \
                                  y, fmt); \
}

YUV2PACKEDWRAPPER(yuv2, 422, yuyv422, AV_PIX_FMT_YUYV422)
YUV2PACKEDWRAPPER(yuv2, 422, yvyu422, AV_PIX_FMT_YVYU422)
YUV2PACKEDWRAPPER(yuv2, 422, uyvy422, AV_PIX_FMT_UYVY422)

static void hyscale_fast_vsx(SwsContext *c, int16_t *dst, int dstWidth,
                           const uint8_t *src, int srcW, int xInc)
{
    int i;
    unsigned int xpos = 0, xx;
    vec_u8 vin, vin2, vperm;
    vec_s8 vmul, valpha;
    vec_s16 vtmp, vtmp2, vtmp3, vtmp4;
    vec_u16 vd_l, vd_r, vcoord16[2];
    vec_u32 vcoord[4];
    const vec_u32 vadd = (vec_u32) {
        0,
        xInc * 1,
        xInc * 2,
        xInc * 3,
    };
    const vec_u16 vadd16 = (vec_u16) { // Modulo math
        0,
        xInc * 1,
        xInc * 2,
        xInc * 3,
        xInc * 4,
        xInc * 5,
        xInc * 6,
        xInc * 7,
    };
    const vec_u32 vshift16 = vec_splats((uint32_t) 16);
    const vec_u16 vshift9 = vec_splat_u16(9);
    const vec_u8 vzero = vec_splat_u8(0);
    const vec_u16 vshift = vec_splat_u16(7);

    for (i = 0; i < dstWidth; i += 16) {
        vcoord16[0] = vec_splats((uint16_t) xpos);
        vcoord16[1] = vec_splats((uint16_t) (xpos + xInc * 8));

        vcoord16[0] = vec_add(vcoord16[0], vadd16);
        vcoord16[1] = vec_add(vcoord16[1], vadd16);

        vcoord16[0] = vec_sr(vcoord16[0], vshift9);
        vcoord16[1] = vec_sr(vcoord16[1], vshift9);
        valpha = (vec_s8) vec_pack(vcoord16[0], vcoord16[1]);

        xx = xpos >> 16;
        vin = vec_vsx_ld(0, &src[xx]);

        vcoord[0] = vec_splats(xpos & 0xffff);
        vcoord[1] = vec_splats((xpos & 0xffff) + xInc * 4);
        vcoord[2] = vec_splats((xpos & 0xffff) + xInc * 8);
        vcoord[3] = vec_splats((xpos & 0xffff) + xInc * 12);

        vcoord[0] = vec_add(vcoord[0], vadd);
        vcoord[1] = vec_add(vcoord[1], vadd);
        vcoord[2] = vec_add(vcoord[2], vadd);
        vcoord[3] = vec_add(vcoord[3], vadd);

        vcoord[0] = vec_sr(vcoord[0], vshift16);
        vcoord[1] = vec_sr(vcoord[1], vshift16);
        vcoord[2] = vec_sr(vcoord[2], vshift16);
        vcoord[3] = vec_sr(vcoord[3], vshift16);

        vcoord16[0] = vec_pack(vcoord[0], vcoord[1]);
        vcoord16[1] = vec_pack(vcoord[2], vcoord[3]);
        vperm = vec_pack(vcoord16[0], vcoord16[1]);

        vin = vec_perm(vin, vin, vperm);

        vin2 = vec_vsx_ld(1, &src[xx]);
        vin2 = vec_perm(vin2, vin2, vperm);

        vmul = (vec_s8) vec_sub(vin2, vin);
        vtmp = vec_mule(vmul, valpha);
        vtmp2 = vec_mulo(vmul, valpha);
        vtmp3 = vec_mergeh(vtmp, vtmp2);
        vtmp4 = vec_mergel(vtmp, vtmp2);

        vd_l = (vec_u16) vec_mergeh(vin, vzero);
        vd_r = (vec_u16) vec_mergel(vin, vzero);
        vd_l = vec_sl(vd_l, vshift);
        vd_r = vec_sl(vd_r, vshift);

        vd_l = vec_add(vd_l, (vec_u16) vtmp3);
        vd_r = vec_add(vd_r, (vec_u16) vtmp4);

        vec_st((vec_s16) vd_l, 0, &dst[i]);
        vec_st((vec_s16) vd_r, 0, &dst[i + 8]);

        xpos += xInc * 16;
    }
    for (i=dstWidth-1; (i*xInc)>>16 >=srcW-1; i--)
        dst[i] = src[srcW-1]*128;
}

#define HCSCALE(in, out) \
        vin = vec_vsx_ld(0, &in[xx]); \
        vin = vec_perm(vin, vin, vperm); \
\
        vin2 = vec_vsx_ld(1, &in[xx]); \
        vin2 = vec_perm(vin2, vin2, vperm); \
\
        vtmp = vec_mule(vin, valphaxor); \
        vtmp2 = vec_mulo(vin, valphaxor); \
        vtmp3 = vec_mergeh(vtmp, vtmp2); \
        vtmp4 = vec_mergel(vtmp, vtmp2); \
\
        vtmp = vec_mule(vin2, valpha); \
        vtmp2 = vec_mulo(vin2, valpha); \
        vd_l = vec_mergeh(vtmp, vtmp2); \
        vd_r = vec_mergel(vtmp, vtmp2); \
\
        vd_l = vec_add(vd_l, vtmp3); \
        vd_r = vec_add(vd_r, vtmp4); \
\
        vec_st((vec_s16) vd_l, 0, &out[i]); \
        vec_st((vec_s16) vd_r, 0, &out[i + 8])

static void hcscale_fast_vsx(SwsContext *c, int16_t *dst1, int16_t *dst2,
                           int dstWidth, const uint8_t *src1,
                           const uint8_t *src2, int srcW, int xInc)
{
    int i;
    unsigned int xpos = 0, xx;
    vec_u8 vin, vin2, vperm;
    vec_u8 valpha, valphaxor;
    vec_u16 vtmp, vtmp2, vtmp3, vtmp4;
    vec_u16 vd_l, vd_r, vcoord16[2];
    vec_u32 vcoord[4];
    const vec_u8 vxor = vec_splats((uint8_t) 127);
    const vec_u32 vadd = (vec_u32) {
        0,
        xInc * 1,
        xInc * 2,
        xInc * 3,
    };
    const vec_u16 vadd16 = (vec_u16) { // Modulo math
        0,
        xInc * 1,
        xInc * 2,
        xInc * 3,
        xInc * 4,
        xInc * 5,
        xInc * 6,
        xInc * 7,
    };
    const vec_u32 vshift16 = vec_splats((uint32_t) 16);
    const vec_u16 vshift9 = vec_splat_u16(9);

    for (i = 0; i < dstWidth; i += 16) {
        vcoord16[0] = vec_splats((uint16_t) xpos);
        vcoord16[1] = vec_splats((uint16_t) (xpos + xInc * 8));

        vcoord16[0] = vec_add(vcoord16[0], vadd16);
        vcoord16[1] = vec_add(vcoord16[1], vadd16);

        vcoord16[0] = vec_sr(vcoord16[0], vshift9);
        vcoord16[1] = vec_sr(vcoord16[1], vshift9);
        valpha = vec_pack(vcoord16[0], vcoord16[1]);
        valphaxor = vec_xor(valpha, vxor);

        xx = xpos >> 16;

        vcoord[0] = vec_splats(xpos & 0xffff);
        vcoord[1] = vec_splats((xpos & 0xffff) + xInc * 4);
        vcoord[2] = vec_splats((xpos & 0xffff) + xInc * 8);
        vcoord[3] = vec_splats((xpos & 0xffff) + xInc * 12);

        vcoord[0] = vec_add(vcoord[0], vadd);
        vcoord[1] = vec_add(vcoord[1], vadd);
        vcoord[2] = vec_add(vcoord[2], vadd);
        vcoord[3] = vec_add(vcoord[3], vadd);

        vcoord[0] = vec_sr(vcoord[0], vshift16);
        vcoord[1] = vec_sr(vcoord[1], vshift16);
        vcoord[2] = vec_sr(vcoord[2], vshift16);
        vcoord[3] = vec_sr(vcoord[3], vshift16);

        vcoord16[0] = vec_pack(vcoord[0], vcoord[1]);
        vcoord16[1] = vec_pack(vcoord[2], vcoord[3]);
        vperm = vec_pack(vcoord16[0], vcoord16[1]);

        HCSCALE(src1, dst1);
        HCSCALE(src2, dst2);

        xpos += xInc * 16;
    }
    for (i=dstWidth-1; (i*xInc)>>16 >=srcW-1; i--) {
        dst1[i] = src1[srcW-1]*128;
        dst2[i] = src2[srcW-1]*128;
    }
}

#undef HCSCALE

static void hScale8To19_vsx(SwsContext *c, int16_t *_dst, int dstW,
                            const uint8_t *src, const int16_t *filter,
                            const int32_t *filterPos, int filterSize)
{
    int i, j;
    int32_t *dst = (int32_t *) _dst;
    vec_s16 vfilter, vin;
    vec_u8 vin8;
    vec_s32 vout;
    const vec_u8 vzero = vec_splat_u8(0);
    const vec_u8 vunusedtab[8] = {
        (vec_u8) {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
                  0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf},
        (vec_u8) {0x0, 0x1, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
                  0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10},
        (vec_u8) {0x0, 0x1, 0x2, 0x3, 0x10, 0x10, 0x10, 0x10,
                  0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10},
        (vec_u8) {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x10, 0x10,
                  0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10},
        (vec_u8) {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
                  0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10},
        (vec_u8) {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
                  0x8, 0x9, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10},
        (vec_u8) {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
                  0x8, 0x9, 0xa, 0xb, 0x10, 0x10, 0x10, 0x10},
        (vec_u8) {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
                  0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0x10, 0x10},
    };
    const vec_u8 vunused = vunusedtab[filterSize % 8];

    if (filterSize == 1) {
        for (i = 0; i < dstW; i++) {
            int srcPos = filterPos[i];
            int val    = 0;
            for (j = 0; j < filterSize; j++) {
                val += ((int)src[srcPos + j]) * filter[filterSize * i + j];
            }
            dst[i] = FFMIN(val >> 3, (1 << 19) - 1); // the cubic equation does overflow ...
        }
    } else {
        for (i = 0; i < dstW; i++) {
            const int srcPos = filterPos[i];
            vout = vec_splat_s32(0);
            for (j = 0; j < filterSize; j += 8) {
                vin8 = vec_vsx_ld(0, &src[srcPos + j]);
                vin = (vec_s16) vec_mergeh(vin8, vzero);
                if (j + 8 > filterSize) // Remove the unused elements on the last round
                    vin = vec_perm(vin, (vec_s16) vzero, vunused);

                vfilter = vec_vsx_ld(0, &filter[filterSize * i + j]);
                vout = vec_msums(vin, vfilter, vout);
            }
            vout = vec_sums(vout, (vec_s32) vzero);
            dst[i] = FFMIN(vout[3] >> 3, (1 << 19) - 1);
        }
    }
}

static void hScale16To19_vsx(SwsContext *c, int16_t *_dst, int dstW,
                             const uint8_t *_src, const int16_t *filter,
                             const int32_t *filterPos, int filterSize)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(c->srcFormat);
    int i, j;
    int32_t *dst        = (int32_t *) _dst;
    const uint16_t *src = (const uint16_t *) _src;
    int bits            = desc->comp[0].depth - 1;
    int sh              = bits - 4;
    vec_s16 vfilter, vin;
    vec_s32 vout, vtmp, vtmp2, vfilter32_l, vfilter32_r;
    const vec_u8 vzero = vec_splat_u8(0);
    const vec_u8 vunusedtab[8] = {
        (vec_u8) {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
                  0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf},
        (vec_u8) {0x0, 0x1, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
                  0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10},
        (vec_u8) {0x0, 0x1, 0x2, 0x3, 0x10, 0x10, 0x10, 0x10,
                  0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10},
        (vec_u8) {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x10, 0x10,
                  0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10},
        (vec_u8) {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
                  0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10},
        (vec_u8) {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
                  0x8, 0x9, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10},
        (vec_u8) {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
                  0x8, 0x9, 0xa, 0xb, 0x10, 0x10, 0x10, 0x10},
        (vec_u8) {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
                  0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0x10, 0x10},
    };
    const vec_u8 vunused = vunusedtab[filterSize % 8];

    if ((isAnyRGB(c->srcFormat) || c->srcFormat==AV_PIX_FMT_PAL8) && desc->comp[0].depth<16) {
        sh = 9;
    } else if (desc->flags & AV_PIX_FMT_FLAG_FLOAT) { /* float input are process like uint 16bpc */
        sh = 16 - 1 - 4;
    }

    if (filterSize == 1) {
        for (i = 0; i < dstW; i++) {
            int srcPos = filterPos[i];
            int val    = 0;

            for (j = 0; j < filterSize; j++) {
                val += src[srcPos + j] * filter[filterSize * i + j];
            }
            // filter=14 bit, input=16 bit, output=30 bit, >> 11 makes 19 bit
            dst[i] = FFMIN(val >> sh, (1 << 19) - 1);
        }
    } else {
        for (i = 0; i < dstW; i++) {
            const int srcPos = filterPos[i];
            vout = vec_splat_s32(0);
            for (j = 0; j < filterSize; j += 8) {
                vin = (vec_s16) vec_vsx_ld(0, &src[srcPos + j]);
                if (j + 8 > filterSize) // Remove the unused elements on the last round
                    vin = vec_perm(vin, (vec_s16) vzero, vunused);

                vfilter = vec_vsx_ld(0, &filter[filterSize * i + j]);
                vfilter32_l = vec_unpackh(vfilter);
                vfilter32_r = vec_unpackl(vfilter);

                vtmp = (vec_s32) vec_mergeh(vin, (vec_s16) vzero);
                vtmp2 = (vec_s32) vec_mergel(vin, (vec_s16) vzero);

                vtmp = vec_mul(vtmp, vfilter32_l);
                vtmp2 = vec_mul(vtmp2, vfilter32_r);

                vout = vec_adds(vout, vtmp);
                vout = vec_adds(vout, vtmp2);
            }
            vout = vec_sums(vout, (vec_s32) vzero);
            dst[i] = FFMIN(vout[3] >> sh, (1 << 19) - 1);
        }
    }
}

static void hScale16To15_vsx(SwsContext *c, int16_t *dst, int dstW,
                             const uint8_t *_src, const int16_t *filter,
                             const int32_t *filterPos, int filterSize)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(c->srcFormat);
    int i, j;
    const uint16_t *src = (const uint16_t *) _src;
    int sh              = desc->comp[0].depth - 1;
    vec_s16 vfilter, vin;
    vec_s32 vout, vtmp, vtmp2, vfilter32_l, vfilter32_r;
    const vec_u8 vzero = vec_splat_u8(0);
    const vec_u8 vunusedtab[8] = {
        (vec_u8) {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
                  0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf},
        (vec_u8) {0x0, 0x1, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
                  0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10},
        (vec_u8) {0x0, 0x1, 0x2, 0x3, 0x10, 0x10, 0x10, 0x10,
                  0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10},
        (vec_u8) {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x10, 0x10,
                  0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10},
        (vec_u8) {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
                  0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10},
        (vec_u8) {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
                  0x8, 0x9, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10},
        (vec_u8) {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
                  0x8, 0x9, 0xa, 0xb, 0x10, 0x10, 0x10, 0x10},
        (vec_u8) {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
                  0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0x10, 0x10},
    };
    const vec_u8 vunused = vunusedtab[filterSize % 8];

    if (sh<15) {
        sh = isAnyRGB(c->srcFormat) || c->srcFormat==AV_PIX_FMT_PAL8 ? 13 : (desc->comp[0].depth - 1);
    } else if (desc->flags & AV_PIX_FMT_FLAG_FLOAT) { /* float input are process like uint 16bpc */
        sh = 16 - 1;
    }

    if (filterSize == 1) {
        for (i = 0; i < dstW; i++) {
            int srcPos = filterPos[i];
            int val    = 0;

            for (j = 0; j < filterSize; j++) {
                val += src[srcPos + j] * filter[filterSize * i + j];
            }
            // filter=14 bit, input=16 bit, output=30 bit, >> 15 makes 15 bit
            dst[i] = FFMIN(val >> sh, (1 << 15) - 1);
        }
    } else {
        for (i = 0; i < dstW; i++) {
            const int srcPos = filterPos[i];
            vout = vec_splat_s32(0);
            for (j = 0; j < filterSize; j += 8) {
                vin = (vec_s16) vec_vsx_ld(0, &src[srcPos + j]);
                if (j + 8 > filterSize) // Remove the unused elements on the last round
                    vin = vec_perm(vin, (vec_s16) vzero, vunused);

                vfilter = vec_vsx_ld(0, &filter[filterSize * i + j]);
                vfilter32_l = vec_unpackh(vfilter);
                vfilter32_r = vec_unpackl(vfilter);

                vtmp = (vec_s32) vec_mergeh(vin, (vec_s16) vzero);
                vtmp2 = (vec_s32) vec_mergel(vin, (vec_s16) vzero);

                vtmp = vec_mul(vtmp, vfilter32_l);
                vtmp2 = vec_mul(vtmp2, vfilter32_r);

                vout = vec_adds(vout, vtmp);
                vout = vec_adds(vout, vtmp2);
            }
            vout = vec_sums(vout, (vec_s32) vzero);
            dst[i] = FFMIN(vout[3] >> sh, (1 << 15) - 1);
        }
    }
}

#endif /* !HAVE_BIGENDIAN */

#endif /* HAVE_VSX */

av_cold void ff_sws_init_swscale_vsx(SwsContext *c)
{
#if HAVE_VSX
    enum AVPixelFormat dstFormat = c->dstFormat;
    const int cpu_flags = av_get_cpu_flags();
    const unsigned char power8 = HAVE_POWER8 && cpu_flags & AV_CPU_FLAG_POWER8;

    if (!(cpu_flags & AV_CPU_FLAG_VSX))
        return;

#if !HAVE_BIGENDIAN
    if (c->srcBpc == 8) {
        if (c->dstBpc <= 14) {
            c->hyScale = c->hcScale = hScale_real_vsx;
            if (c->flags & SWS_FAST_BILINEAR && c->dstW >= c->srcW && c->chrDstW >= c->chrSrcW) {
                c->hyscale_fast = hyscale_fast_vsx;
                c->hcscale_fast = hcscale_fast_vsx;
            }
        } else {
            c->hyScale = c->hcScale = hScale8To19_vsx;
        }
    } else {
        if (power8) {
            c->hyScale = c->hcScale = c->dstBpc > 14 ? hScale16To19_vsx
                                                     : hScale16To15_vsx;
        }
    }
    if (!is16BPS(dstFormat) && !isNBPS(dstFormat) && !isSemiPlanarYUV(dstFormat) &&
        dstFormat != AV_PIX_FMT_GRAYF32BE && dstFormat != AV_PIX_FMT_GRAYF32LE &&
        !c->needAlpha) {
        c->yuv2planeX = yuv2planeX_vsx;
    }
#endif

    if (!(c->flags & (SWS_BITEXACT | SWS_FULL_CHR_H_INT)) && !c->needAlpha) {
        switch (c->dstBpc) {
        case 8:
            c->yuv2plane1 = yuv2plane1_8_vsx;
            break;
#if !HAVE_BIGENDIAN
        case 9:
            c->yuv2plane1 = isBE(dstFormat) ? yuv2plane1_9BE_vsx  : yuv2plane1_9LE_vsx;
            c->yuv2planeX = isBE(dstFormat) ? yuv2planeX_9BE_vsx  : yuv2planeX_9LE_vsx;
            break;
        case 10:
            c->yuv2plane1 = isBE(dstFormat) ? yuv2plane1_10BE_vsx  : yuv2plane1_10LE_vsx;
            c->yuv2planeX = isBE(dstFormat) ? yuv2planeX_10BE_vsx  : yuv2planeX_10LE_vsx;
            break;
        case 12:
            c->yuv2plane1 = isBE(dstFormat) ? yuv2plane1_12BE_vsx  : yuv2plane1_12LE_vsx;
            c->yuv2planeX = isBE(dstFormat) ? yuv2planeX_12BE_vsx  : yuv2planeX_12LE_vsx;
            break;
        case 14:
            c->yuv2plane1 = isBE(dstFormat) ? yuv2plane1_14BE_vsx  : yuv2plane1_14LE_vsx;
            c->yuv2planeX = isBE(dstFormat) ? yuv2planeX_14BE_vsx  : yuv2planeX_14LE_vsx;
            break;
        case 16:
            c->yuv2plane1 = isBE(dstFormat) ? yuv2plane1_16BE_vsx  : yuv2plane1_16LE_vsx;
#if HAVE_POWER8
            if (cpu_flags & AV_CPU_FLAG_POWER8) {
                c->yuv2planeX = isBE(dstFormat) ? yuv2planeX_16BE_vsx  : yuv2planeX_16LE_vsx;
            }
#endif /* HAVE_POWER8 */
            break;
#endif /* !HAVE_BIGENDIAN */
        }
    }

    if (c->flags & SWS_BITEXACT)
        return;

#if !HAVE_BIGENDIAN
    if (c->flags & SWS_FULL_CHR_H_INT) {
        switch (dstFormat) {
            case AV_PIX_FMT_RGB24:
                if (power8) {
                    c->yuv2packed1 = yuv2rgb24_full_1_vsx;
                    c->yuv2packed2 = yuv2rgb24_full_2_vsx;
                    c->yuv2packedX = yuv2rgb24_full_X_vsx;
                }
            break;
            case AV_PIX_FMT_BGR24:
                if (power8) {
                    c->yuv2packed1 = yuv2bgr24_full_1_vsx;
                    c->yuv2packed2 = yuv2bgr24_full_2_vsx;
                    c->yuv2packedX = yuv2bgr24_full_X_vsx;
                }
            break;
            case AV_PIX_FMT_BGRA:
                if (power8) {
                    if (!c->needAlpha) {
                        c->yuv2packed1 = yuv2bgrx32_full_1_vsx;
                        c->yuv2packed2 = yuv2bgrx32_full_2_vsx;
                        c->yuv2packedX = yuv2bgrx32_full_X_vsx;
                    }
                }
            break;
            case AV_PIX_FMT_RGBA:
                if (power8) {
                    if (!c->needAlpha) {
                        c->yuv2packed1 = yuv2rgbx32_full_1_vsx;
                        c->yuv2packed2 = yuv2rgbx32_full_2_vsx;
                        c->yuv2packedX = yuv2rgbx32_full_X_vsx;
                    }
                }
            break;
            case AV_PIX_FMT_ARGB:
                if (power8) {
                    if (!c->needAlpha) {
                        c->yuv2packed1 = yuv2xrgb32_full_1_vsx;
                        c->yuv2packed2 = yuv2xrgb32_full_2_vsx;
                        c->yuv2packedX = yuv2xrgb32_full_X_vsx;
                    }
                }
            break;
            case AV_PIX_FMT_ABGR:
                if (power8) {
                    if (!c->needAlpha) {
                        c->yuv2packed1 = yuv2xbgr32_full_1_vsx;
                        c->yuv2packed2 = yuv2xbgr32_full_2_vsx;
                        c->yuv2packedX = yuv2xbgr32_full_X_vsx;
                    }
                }
            break;
        }
    } else { /* !SWS_FULL_CHR_H_INT */
        switch (dstFormat) {
            case AV_PIX_FMT_YUYV422:
                c->yuv2packed1 = yuv2yuyv422_1_vsx;
                c->yuv2packed2 = yuv2yuyv422_2_vsx;
                c->yuv2packedX = yuv2yuyv422_X_vsx;
            break;
            case AV_PIX_FMT_YVYU422:
                c->yuv2packed1 = yuv2yvyu422_1_vsx;
                c->yuv2packed2 = yuv2yvyu422_2_vsx;
                c->yuv2packedX = yuv2yvyu422_X_vsx;
            break;
            case AV_PIX_FMT_UYVY422:
                c->yuv2packed1 = yuv2uyvy422_1_vsx;
                c->yuv2packed2 = yuv2uyvy422_2_vsx;
                c->yuv2packedX = yuv2uyvy422_X_vsx;
            break;
            case AV_PIX_FMT_BGRA:
                if (power8) {
                    if (!c->needAlpha) {
                        c->yuv2packed1 = yuv2bgrx32_1_vsx;
                        c->yuv2packed2 = yuv2bgrx32_2_vsx;
                    }
                }
            break;
            case AV_PIX_FMT_RGBA:
                if (power8) {
                    if (!c->needAlpha) {
                        c->yuv2packed1 = yuv2rgbx32_1_vsx;
                        c->yuv2packed2 = yuv2rgbx32_2_vsx;
                    }
                }
            break;
            case AV_PIX_FMT_ARGB:
                if (power8) {
                    if (!c->needAlpha) {
                        c->yuv2packed1 = yuv2xrgb32_1_vsx;
                        c->yuv2packed2 = yuv2xrgb32_2_vsx;
                    }
                }
            break;
            case AV_PIX_FMT_ABGR:
                if (power8) {
                    if (!c->needAlpha) {
                        c->yuv2packed1 = yuv2xbgr32_1_vsx;
                        c->yuv2packed2 = yuv2xbgr32_2_vsx;
                    }
                }
            break;
            case AV_PIX_FMT_RGB24:
                if (power8) {
                    c->yuv2packed1 = yuv2rgb24_1_vsx;
                    c->yuv2packed2 = yuv2rgb24_2_vsx;
                }
            break;
            case AV_PIX_FMT_BGR24:
                if (power8) {
                    c->yuv2packed1 = yuv2bgr24_1_vsx;
                    c->yuv2packed2 = yuv2bgr24_2_vsx;
                }
            break;
        }
    }
#endif /* !HAVE_BIGENDIAN */

#endif /* HAVE_VSX */
}
