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
#include "yuv2rgb_altivec.h"
#include "libavutil/ppc/util_altivec.h"

#if HAVE_ALTIVEC
#if HAVE_BIGENDIAN
#define vzero vec_splat_s32(0)

#define  GET_LS(a,b,c,s) {\
        vector signed short l2  = vec_ld(((b) << 1) + 16, s);\
        ls  = vec_perm(a, l2, c);\
        a = l2;\
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
        vector unsigned char perm0 = vec_lvsl(joffset, f);\
        vf = vec_ld(joffset, f);\
        vf = vec_perm(vf, vf, perm0);\
}
#define LOAD_L1(ll1,s,p){\
        p = vec_lvsl(xoffset, s);\
        ll1   = vec_ld(xoffset, s);\
}

// The 3 above is 2 (filterSize == 4) + 1 (sizeof(short) == 2).

// The neat trick: We only care for half the elements,
// high or low depending on (i<<3)%16 (it's 0 or 8 here),
// and we're going to use vec_mule, so we choose
// carefully how to "unpack" the elements into the even slots.
#define GET_VF4(a, vf, f) {\
    vf = vec_ld(a<< 3, f);\
    if ((a << 3) % 16)\
        vf = vec_mergel(vf, (vector signed short)vzero);\
    else\
        vf = vec_mergeh(vf, (vector signed short)vzero);\
}
#define FIRST_LOAD(sv, pos, s, per) {\
    sv = vec_ld(pos, s);\
    per = vec_lvsl(pos, s);\
}
#define UPDATE_PTR(s0, d0, s1, d1) {\
    d0 = s0;\
    d1 = s1;\
}
#define LOAD_SRCV(pos, a, s, per, v0, v1, vf) {\
    v1 = vec_ld(pos + a + 16, s);\
    vf = vec_perm(v0, v1, per);\
}
#define LOAD_SRCV8(pos, a, s, per, v0, v1, vf) {\
    if ((((uintptr_t)s + pos) % 16) > 8) {\
        v1 = vec_ld(pos + a + 16, s);\
    }\
    vf = vec_perm(v0, src_v1, per);\
}
#define GET_VFD(a, b, f, vf0, vf1, per, vf, off) {\
    vf1 = vec_ld((a * 2 * filterSize) + (b * 2) + 16 + off, f);\
    vf  = vec_perm(vf0, vf1, per);\
}

#define FUNC(name) name ## _altivec
#include "swscale_ppc_template.c"
#undef FUNC

#undef vzero

#endif /* HAVE_BIGENDIAN */

#define output_pixel(pos, val, bias, signedness) \
    if (big_endian) { \
        AV_WB16(pos, bias + av_clip_ ## signedness ## 16(val >> shift)); \
    } else { \
        AV_WL16(pos, bias + av_clip_ ## signedness ## 16(val >> shift)); \
    }

static void
yuv2plane1_float_u(const int32_t *src, float *dest, int dstW, int start)
{
    static const int big_endian = HAVE_BIGENDIAN;
    static const int shift = 3;
    static const float float_mult = 1.0f / 65535.0f;
    int i, val;
    uint16_t val_uint;

    for (i = start; i < dstW; ++i){
        val = src[i] + (1 << (shift - 1));
        output_pixel(&val_uint, val, 0, uint);
        dest[i] = float_mult * (float)val_uint;
    }
}

static void
yuv2plane1_float_bswap_u(const int32_t *src, uint32_t *dest, int dstW, int start)
{
    static const int big_endian = HAVE_BIGENDIAN;
    static const int shift = 3;
    static const float float_mult = 1.0f / 65535.0f;
    int i, val;
    uint16_t val_uint;

    for (i = start; i < dstW; ++i){
        val = src[i] + (1 << (shift - 1));
        output_pixel(&val_uint, val, 0, uint);
        dest[i] = av_bswap32(av_float2int(float_mult * (float)val_uint));
    }
}

static void yuv2plane1_float_altivec(const int32_t *src, float *dest, int dstW)
{
    const int dst_u = -(uintptr_t)dest & 3;
    const int shift = 3;
    const int add = (1 << (shift - 1));
    const int clip = (1 << 16) - 1;
    const float fmult = 1.0f / 65535.0f;
    const vector uint32_t vadd = (vector uint32_t) {add, add, add, add};
    const vector uint32_t vshift = (vector uint32_t) vec_splat_u32(shift);
    const vector uint32_t vlargest = (vector uint32_t) {clip, clip, clip, clip};
    const vector float vmul = (vector float) {fmult, fmult, fmult, fmult};
    const vector float vzero = (vector float) {0, 0, 0, 0};
    vector uint32_t v;
    vector float vd;
    int i;

    yuv2plane1_float_u(src, dest, dst_u, 0);

    for (i = dst_u; i < dstW - 3; i += 4) {
        v = vec_ld(0, (const uint32_t *) &src[i]);
        v = vec_add(v, vadd);
        v = vec_sr(v, vshift);
        v = vec_min(v, vlargest);

        vd = vec_ctf(v, 0);
        vd = vec_madd(vd, vmul, vzero);

        vec_st(vd, 0, &dest[i]);
    }

    yuv2plane1_float_u(src, dest, dstW, i);
}

static void yuv2plane1_float_bswap_altivec(const int32_t *src, uint32_t *dest, int dstW)
{
    const int dst_u = -(uintptr_t)dest & 3;
    const int shift = 3;
    const int add = (1 << (shift - 1));
    const int clip = (1 << 16) - 1;
    const float fmult = 1.0f / 65535.0f;
    const vector uint32_t vadd = (vector uint32_t) {add, add, add, add};
    const vector uint32_t vshift = (vector uint32_t) vec_splat_u32(shift);
    const vector uint32_t vlargest = (vector uint32_t) {clip, clip, clip, clip};
    const vector float vmul = (vector float) {fmult, fmult, fmult, fmult};
    const vector float vzero = (vector float) {0, 0, 0, 0};
    const vector uint32_t vswapbig = (vector uint32_t) {16, 16, 16, 16};
    const vector uint16_t vswapsmall = vec_splat_u16(8);
    vector uint32_t v;
    vector float vd;
    int i;

    yuv2plane1_float_bswap_u(src, dest, dst_u, 0);

    for (i = dst_u; i < dstW - 3; i += 4) {
        v = vec_ld(0, (const uint32_t *) &src[i]);
        v = vec_add(v, vadd);
        v = vec_sr(v, vshift);
        v = vec_min(v, vlargest);

        vd = vec_ctf(v, 0);
        vd = vec_madd(vd, vmul, vzero);

        vd = (vector float) vec_rl((vector uint32_t) vd, vswapbig);
        vd = (vector float) vec_rl((vector uint16_t) vd, vswapsmall);

        vec_st(vd, 0, (float *) &dest[i]);
    }

    yuv2plane1_float_bswap_u(src, dest, dstW, i);
}

#define yuv2plane1_float(template, dest_type, BE_LE) \
static void yuv2plane1_float ## BE_LE ## _altivec(const int16_t *src, uint8_t *dest, \
                                                  int dstW, \
                                                  const uint8_t *dither, int offset) \
{ \
    template((const int32_t *)src, (dest_type *)dest, dstW); \
}

#if HAVE_BIGENDIAN
yuv2plane1_float(yuv2plane1_float_altivec,       float,    BE)
yuv2plane1_float(yuv2plane1_float_bswap_altivec, uint32_t, LE)
#else
yuv2plane1_float(yuv2plane1_float_altivec,       float,    LE)
yuv2plane1_float(yuv2plane1_float_bswap_altivec, uint32_t, BE)
#endif

#endif /* HAVE_ALTIVEC */

av_cold void ff_sws_init_swscale_ppc(SwsContext *c)
{
#if HAVE_ALTIVEC
    enum AVPixelFormat dstFormat = c->dstFormat;

    if (!(av_get_cpu_flags() & AV_CPU_FLAG_ALTIVEC))
        return;

#if HAVE_BIGENDIAN
    if (c->srcBpc == 8 && c->dstBpc <= 14) {
        c->hyScale = c->hcScale = hScale_real_altivec;
    }
    if (!is16BPS(dstFormat) && !isNBPS(dstFormat) && !isSemiPlanarYUV(dstFormat) &&
        dstFormat != AV_PIX_FMT_GRAYF32BE && dstFormat != AV_PIX_FMT_GRAYF32LE &&
        !c->needAlpha) {
        c->yuv2planeX = yuv2planeX_altivec;
    }
#endif

    if (dstFormat == AV_PIX_FMT_GRAYF32BE) {
        c->yuv2plane1 = yuv2plane1_floatBE_altivec;
    } else if (dstFormat == AV_PIX_FMT_GRAYF32LE) {
        c->yuv2plane1 = yuv2plane1_floatLE_altivec;
    }

    /* The following list of supported dstFormat values should
     * match what's found in the body of ff_yuv2packedX_altivec() */
    if (!(c->flags & (SWS_BITEXACT | SWS_FULL_CHR_H_INT)) && !c->needAlpha) {
        switch (c->dstFormat) {
        case AV_PIX_FMT_ABGR:
            c->yuv2packedX = ff_yuv2abgr_X_altivec;
            break;
        case AV_PIX_FMT_BGRA:
            c->yuv2packedX = ff_yuv2bgra_X_altivec;
            break;
        case AV_PIX_FMT_ARGB:
            c->yuv2packedX = ff_yuv2argb_X_altivec;
            break;
        case AV_PIX_FMT_RGBA:
            c->yuv2packedX = ff_yuv2rgba_X_altivec;
            break;
        case AV_PIX_FMT_BGR24:
            c->yuv2packedX = ff_yuv2bgr24_X_altivec;
            break;
        case AV_PIX_FMT_RGB24:
            c->yuv2packedX = ff_yuv2rgb24_X_altivec;
            break;
        }
    }
#endif /* HAVE_ALTIVEC */

    ff_sws_init_swscale_vsx(c);
}
