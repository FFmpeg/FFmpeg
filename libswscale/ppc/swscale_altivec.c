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
#define vzero vec_splat_s32(0)

#if HAVE_BIGENDIAN
#define  GET_LS(a,b,c,s) {\
        vector signed short l2  = vec_ld(((b) << 1) + 16, s);\
        ls  = vec_perm(a, l2, c);\
        a = l2;\
    }
#define  GET_VF(a, b, c,d) {\
        a = vec_mergeh(c, d);\
        b = vec_mergel(c, d);\
    }
#else
#define  GET_LS(a,b,c,s) {\
        ls  = a;\
        a = vec_vsx_ld(((b) << 1)  + 16, s);\
    }
#define  GET_VF(a, b, c, d) {\
        a = vec_mergel(d, c);\
        b = vec_mergeh(d, c);\
    }
#endif

#define yuv2planeX_8(d1, d2, l1, src, x, perm, filter) do {\
        vector signed short ls;\
        GET_LS(l1, x, perm, src);\
        vector signed int   i1  = vec_mule(filter, ls);\
        vector signed int   i2  = vec_mulo(filter, ls);\
        vector signed int   vf1, vf2;\
        GET_VF(vf1, vf2, i1, i2);\
        d1 = vec_add(d1, vf1);\
        d2 = vec_add(d2, vf2);\
    } while (0)

#if HAVE_BIGENDIAN
#define LOAD_FILTER(vf,f) {\
        vector unsigned char perm0 = vec_lvsl(joffset, f);\
        vf = vec_ld(joffset, f);\
        vf = vec_perm(vf, vf, perm0);\
}
#define LOAD_L1(ll1,s,p){\
        p = vec_lvsl(xoffset, s);\
        ll1   = vec_ld(xoffset, s);\
}
#else
#define LOAD_FILTER(vf,f) {\
        vf = vec_vsx_ld(joffset, f);\
}
#define LOAD_L1(ll1,s,p){\
        ll1  = vec_vsx_ld(xoffset, s);\
}
#endif

static void yuv2planeX_16_altivec(const int16_t *filter, int filterSize,
                                  const int16_t **src, uint8_t *dest,
                                  const uint8_t *dither, int offset, int x)
{
    register int i, j;
    DECLARE_ALIGNED(16, int, val)[16];
    vector signed int vo1, vo2, vo3, vo4;
    vector unsigned short vs1, vs2;
    vector unsigned char vf;
    vector unsigned int altivec_vectorShiftInt19 =
        vec_add(vec_splat_u32(10), vec_splat_u32(9));

    for (i = 0; i < 16; i++)
        val[i] = dither[(x + i + offset) & 7] << 12;

    vo1 = vec_ld(0,  val);
    vo2 = vec_ld(16, val);
    vo3 = vec_ld(32, val);
    vo4 = vec_ld(48, val);

    for (j = 0; j < filterSize; j++) {
        unsigned int joffset=j<<1;
        unsigned int xoffset=x<<1;
        vector unsigned char perm;
        vector signed short l1,vLumFilter;
        LOAD_FILTER(vLumFilter,filter);
        vLumFilter = vec_splat(vLumFilter, 0);
        LOAD_L1(l1,src[j],perm);
        yuv2planeX_8(vo1, vo2, l1, src[j], x,     perm, vLumFilter);
        yuv2planeX_8(vo3, vo4, l1, src[j], x + 8, perm, vLumFilter);
    }

    vo1 = vec_sra(vo1, altivec_vectorShiftInt19);
    vo2 = vec_sra(vo2, altivec_vectorShiftInt19);
    vo3 = vec_sra(vo3, altivec_vectorShiftInt19);
    vo4 = vec_sra(vo4, altivec_vectorShiftInt19);
    vs1 = vec_packsu(vo1, vo2);
    vs2 = vec_packsu(vo3, vo4);
    vf  = vec_packsu(vs1, vs2);
    VEC_ST(vf, 0, dest);
}


static inline void yuv2planeX_u(const int16_t *filter, int filterSize,
                                const int16_t **src, uint8_t *dest, int dstW,
                                const uint8_t *dither, int offset, int x)
{
    int i, j;

    for (i = x; i < dstW; i++) {
        int t = dither[(i + offset) & 7] << 12;
        for (j = 0; j < filterSize; j++)
            t += src[j][i] * filter[j];
        dest[i] = av_clip_uint8(t >> 19);
    }
}

static void yuv2planeX_altivec(const int16_t *filter, int filterSize,
                               const int16_t **src, uint8_t *dest, int dstW,
                               const uint8_t *dither, int offset)
{
    int dst_u = -(uintptr_t)dest & 15;
    int i;

    yuv2planeX_u(filter, filterSize, src, dest, dst_u, dither, offset, 0);

    for (i = dst_u; i < dstW - 15; i += 16)
        yuv2planeX_16_altivec(filter, filterSize, src, dest + i, dither,
                              offset, i);

    yuv2planeX_u(filter, filterSize, src, dest, dstW, dither, offset, i);
}

#if HAVE_BIGENDIAN
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
#else /* else of #if HAVE_BIGENDIAN */
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
#endif /* end of #if HAVE_BIGENDIAN */

static void hScale_altivec_real(SwsContext *c, int16_t *dst, int dstW,
                                const uint8_t *src, const int16_t *filter,
                                const int32_t *filterPos, int filterSize)
{
    register int i;
    DECLARE_ALIGNED(16, int, tempo)[4];

    if (filterSize % 4) {
        for (i = 0; i < dstW; i++) {
            register int j;
            register int srcPos = filterPos[i];
            register int val    = 0;
            for (j = 0; j < filterSize; j++)
                val += ((int)src[srcPos + j]) * filter[filterSize * i + j];
            dst[i] = FFMIN(val >> 7, (1 << 15) - 1);
        }
    } else
        switch (filterSize) {
        case 4:
            for (i = 0; i < dstW; i++) {
                register int srcPos = filterPos[i];

                vector unsigned char src_vF = unaligned_load(srcPos, src);
                vector signed short src_v, filter_v;
                vector signed int val_vEven, val_s;
                src_v = // vec_unpackh sign-extends...
                        (vector signed short)(VEC_MERGEH((vector unsigned char)vzero, src_vF));
                // now put our elements in the even slots
                src_v = vec_mergeh(src_v, (vector signed short)vzero);
                GET_VF4(i, filter_v, filter);
                val_vEven = vec_mule(src_v, filter_v);
                val_s     = vec_sums(val_vEven, vzero);
                vec_st(val_s, 0, tempo);
                dst[i] = FFMIN(tempo[3] >> 7, (1 << 15) - 1);
            }
        break;
        case 8:
            for (i = 0; i < dstW; i++) {
                register int srcPos = filterPos[i];
                vector unsigned char src_vF, src_v0, src_v1;
                vector unsigned char permS;
                vector signed short src_v, filter_v;
                vector signed int val_v, val_s;
                FIRST_LOAD(src_v0, srcPos, src, permS);
                LOAD_SRCV8(srcPos, 0, src, permS, src_v0, src_v1, src_vF);
                src_v = // vec_unpackh sign-extends...
                        (vector signed short)(VEC_MERGEH((vector unsigned char)vzero, src_vF));
                filter_v = vec_ld(i << 4, filter);
                val_v = vec_msums(src_v, filter_v, (vector signed int)vzero);
                val_s = vec_sums(val_v, vzero);
                vec_st(val_s, 0, tempo);
                dst[i] = FFMIN(tempo[3] >> 7, (1 << 15) - 1);
            }
        break;

        case 16:
            for (i = 0; i < dstW; i++) {
                register int srcPos = filterPos[i];

                vector unsigned char src_vF = unaligned_load(srcPos, src);
                vector signed short src_vA = // vec_unpackh sign-extends...
                                             (vector signed short)(VEC_MERGEH((vector unsigned char)vzero, src_vF));
                vector signed short src_vB = // vec_unpackh sign-extends...
                                             (vector signed short)(VEC_MERGEL((vector unsigned char)vzero, src_vF));
                vector signed short filter_v0 = vec_ld(i << 5, filter);
                vector signed short filter_v1 = vec_ld((i << 5) + 16, filter);

                vector signed int val_acc = vec_msums(src_vA, filter_v0, (vector signed int)vzero);
                vector signed int val_v   = vec_msums(src_vB, filter_v1, val_acc);

                vector signed int val_s = vec_sums(val_v, vzero);

                VEC_ST(val_s, 0, tempo);
                dst[i] = FFMIN(tempo[3] >> 7, (1 << 15) - 1);
            }
        break;

        default:
            for (i = 0; i < dstW; i++) {
                register int j, offset = i * 2 * filterSize;
                register int srcPos = filterPos[i];

                vector signed int val_s, val_v = (vector signed int)vzero;
                vector signed short filter_v0R;
                vector unsigned char permF, src_v0, permS;
                FIRST_LOAD(filter_v0R, offset, filter, permF);
                FIRST_LOAD(src_v0, srcPos, src, permS);

                for (j = 0; j < filterSize - 15; j += 16) {
                    vector unsigned char src_v1, src_vF;
                    vector signed short filter_v1R, filter_v2R, filter_v0, filter_v1;
                    LOAD_SRCV(srcPos, j, src, permS, src_v0, src_v1, src_vF);
                    vector signed short src_vA = // vec_unpackh sign-extends...
                                                 (vector signed short)(VEC_MERGEH((vector unsigned char)vzero, src_vF));
                    vector signed short src_vB = // vec_unpackh sign-extends...
                                                 (vector signed short)(VEC_MERGEL((vector unsigned char)vzero, src_vF));
                    GET_VFD(i, j, filter, filter_v0R, filter_v1R, permF, filter_v0, 0);
                    GET_VFD(i, j, filter, filter_v1R, filter_v2R, permF, filter_v1, 16);

                    vector signed int val_acc = vec_msums(src_vA, filter_v0, val_v);
                    val_v = vec_msums(src_vB, filter_v1, val_acc);
                    UPDATE_PTR(filter_v2R, filter_v0R, src_v1, src_v0);
                }

                if (j < filterSize - 7) {
                    // loading src_v0 is useless, it's already done above
                    vector unsigned char src_v1, src_vF;
                    vector signed short src_v, filter_v1R, filter_v;
                    LOAD_SRCV8(srcPos, j, src, permS, src_v0, src_v1, src_vF);
                    src_v = // vec_unpackh sign-extends...
                            (vector signed short)(VEC_MERGEH((vector unsigned char)vzero, src_vF));
                    GET_VFD(i, j, filter, filter_v0R, filter_v1R, permF, filter_v, 0);
                    val_v = vec_msums(src_v, filter_v, val_v);
                }
                val_s = vec_sums(val_v, vzero);

                VEC_ST(val_s, 0, tempo);
                dst[i] = FFMIN(tempo[3] >> 7, (1 << 15) - 1);
            }
        }
}
#endif /* HAVE_ALTIVEC */

av_cold void ff_sws_init_swscale_ppc(SwsContext *c)
{
#if HAVE_ALTIVEC
    enum AVPixelFormat dstFormat = c->dstFormat;

    if (!(av_get_cpu_flags() & AV_CPU_FLAG_ALTIVEC))
        return;

    if (c->srcBpc == 8 && c->dstBpc <= 14) {
        c->hyScale = c->hcScale = hScale_altivec_real;
    }
    if (!is16BPS(dstFormat) && !is9_OR_10BPS(dstFormat) &&
        dstFormat != AV_PIX_FMT_NV12 && dstFormat != AV_PIX_FMT_NV21 &&
        !c->alpPixBuf) {
        c->yuv2planeX = yuv2planeX_altivec;
    }

    /* The following list of supported dstFormat values should
     * match what's found in the body of ff_yuv2packedX_altivec() */
    if (!(c->flags & (SWS_BITEXACT | SWS_FULL_CHR_H_INT)) && !c->alpPixBuf) {
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
}
