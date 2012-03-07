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
#include "libavutil/cpu.h"
#include "yuv2rgb_altivec.h"

#define vzero vec_splat_s32(0)

static inline void altivec_packIntArrayToCharArray(int *val, uint8_t *dest,
                                                   int dstW)
{
    register int i;
    vector unsigned int altivec_vectorShiftInt19 =
        vec_add(vec_splat_u32(10), vec_splat_u32(9));
    if ((uintptr_t)dest % 16) {
        /* badly aligned store, we force store alignment */
        /* and will handle load misalignment on val w/ vec_perm */
        vector unsigned char perm1;
        vector signed int v1;
        for (i = 0; (i < dstW) &&
             (((uintptr_t)dest + i) % 16); i++) {
            int t = val[i] >> 19;
            dest[i] = (t < 0) ? 0 : ((t > 255) ? 255 : t);
        }
        perm1 = vec_lvsl(i << 2, val);
        v1    = vec_ld(i << 2, val);
        for (; i < (dstW - 15); i += 16) {
            int offset = i << 2;
            vector signed int v2  = vec_ld(offset + 16, val);
            vector signed int v3  = vec_ld(offset + 32, val);
            vector signed int v4  = vec_ld(offset + 48, val);
            vector signed int v5  = vec_ld(offset + 64, val);
            vector signed int v12 = vec_perm(v1, v2, perm1);
            vector signed int v23 = vec_perm(v2, v3, perm1);
            vector signed int v34 = vec_perm(v3, v4, perm1);
            vector signed int v45 = vec_perm(v4, v5, perm1);

            vector signed int vA      = vec_sra(v12, altivec_vectorShiftInt19);
            vector signed int vB      = vec_sra(v23, altivec_vectorShiftInt19);
            vector signed int vC      = vec_sra(v34, altivec_vectorShiftInt19);
            vector signed int vD      = vec_sra(v45, altivec_vectorShiftInt19);
            vector unsigned short vs1 = vec_packsu(vA, vB);
            vector unsigned short vs2 = vec_packsu(vC, vD);
            vector unsigned char vf   = vec_packsu(vs1, vs2);
            vec_st(vf, i, dest);
            v1 = v5;
        }
    } else { // dest is properly aligned, great
        for (i = 0; i < (dstW - 15); i += 16) {
            int offset = i << 2;
            vector signed int v1      = vec_ld(offset, val);
            vector signed int v2      = vec_ld(offset + 16, val);
            vector signed int v3      = vec_ld(offset + 32, val);
            vector signed int v4      = vec_ld(offset + 48, val);
            vector signed int v5      = vec_sra(v1, altivec_vectorShiftInt19);
            vector signed int v6      = vec_sra(v2, altivec_vectorShiftInt19);
            vector signed int v7      = vec_sra(v3, altivec_vectorShiftInt19);
            vector signed int v8      = vec_sra(v4, altivec_vectorShiftInt19);
            vector unsigned short vs1 = vec_packsu(v5, v6);
            vector unsigned short vs2 = vec_packsu(v7, v8);
            vector unsigned char vf   = vec_packsu(vs1, vs2);
            vec_st(vf, i, dest);
        }
    }
    for (; i < dstW; i++) {
        int t = val[i] >> 19;
        dest[i] = (t < 0) ? 0 : ((t > 255) ? 255 : t);
    }
}

// FIXME remove the usage of scratch buffers.
static void yuv2planeX_altivec(const int16_t *filter, int filterSize,
                               const int16_t **src, uint8_t *dest, int dstW,
                               const uint8_t *dither, int offset)
{
    register int i, j;
    DECLARE_ALIGNED(16, int, val)[dstW];

    for (i = 0; i < dstW; i++)
        val[i] = dither[(i + offset) & 7] << 12;

    for (j = 0; j < filterSize; j++) {
        vector signed short l1, vLumFilter = vec_ld(j << 1, filter);
        vector unsigned char perm, perm0 = vec_lvsl(j << 1, filter);
        vLumFilter = vec_perm(vLumFilter, vLumFilter, perm0);
        vLumFilter = vec_splat(vLumFilter, 0); // lumFilter[j] is loaded 8 times in vLumFilter

        perm = vec_lvsl(0, src[j]);
        l1   = vec_ld(0, src[j]);

        for (i = 0; i < (dstW - 7); i += 8) {
            int offset = i << 2;
            vector signed short l2 = vec_ld((i << 1) + 16, src[j]);

            vector signed int v1 = vec_ld(offset, val);
            vector signed int v2 = vec_ld(offset + 16, val);

            vector signed short ls = vec_perm(l1, l2, perm); // lumSrc[j][i] ... lumSrc[j][i+7]

            vector signed int i1 = vec_mule(vLumFilter, ls);
            vector signed int i2 = vec_mulo(vLumFilter, ls);

            vector signed int vf1 = vec_mergeh(i1, i2);
            vector signed int vf2 = vec_mergel(i1, i2); // lumSrc[j][i] * lumFilter[j] ... lumSrc[j][i+7] * lumFilter[j]

            vector signed int vo1 = vec_add(v1, vf1);
            vector signed int vo2 = vec_add(v2, vf2);

            vec_st(vo1, offset, val);
            vec_st(vo2, offset + 16, val);

            l1 = l2;
        }
        for (; i < dstW; i++)
            val[i] += src[j][i] * filter[j];
    }
    altivec_packIntArrayToCharArray(val, dest, dstW);
}

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

                vector unsigned char src_v0 = vec_ld(srcPos, src);
                vector unsigned char src_v1, src_vF;
                vector signed short src_v, filter_v;
                vector signed int val_vEven, val_s;
                if ((((uintptr_t)src + srcPos) % 16) > 12) {
                    src_v1 = vec_ld(srcPos + 16, src);
                }
                src_vF = vec_perm(src_v0, src_v1, vec_lvsl(srcPos, src));

                src_v = // vec_unpackh sign-extends...
                        (vector signed short)(vec_mergeh((vector unsigned char)vzero, src_vF));
                // now put our elements in the even slots
                src_v = vec_mergeh(src_v, (vector signed short)vzero);

                filter_v = vec_ld(i << 3, filter);
                // The 3 above is 2 (filterSize == 4) + 1 (sizeof(short) == 2).

                // The neat trick: We only care for half the elements,
                // high or low depending on (i<<3)%16 (it's 0 or 8 here),
                // and we're going to use vec_mule, so we choose
                // carefully how to "unpack" the elements into the even slots.
                if ((i << 3) % 16)
                    filter_v = vec_mergel(filter_v, (vector signed short)vzero);
                else
                    filter_v = vec_mergeh(filter_v, (vector signed short)vzero);

                val_vEven = vec_mule(src_v, filter_v);
                val_s     = vec_sums(val_vEven, vzero);
                vec_st(val_s, 0, tempo);
                dst[i] = FFMIN(tempo[3] >> 7, (1 << 15) - 1);
            }
        break;

        case 8:
            for (i = 0; i < dstW; i++) {
                register int srcPos = filterPos[i];

                vector unsigned char src_v0 = vec_ld(srcPos, src);
                vector unsigned char src_v1, src_vF;
                vector signed short src_v, filter_v;
                vector signed int val_v, val_s;
                if ((((uintptr_t)src + srcPos) % 16) > 8) {
                    src_v1 = vec_ld(srcPos + 16, src);
                }
                src_vF = vec_perm(src_v0, src_v1, vec_lvsl(srcPos, src));

                src_v = // vec_unpackh sign-extends...
                        (vector signed short)(vec_mergeh((vector unsigned char)vzero, src_vF));
                filter_v = vec_ld(i << 4, filter);
                // the 4 above is 3 (filterSize == 8) + 1 (sizeof(short) == 2)

                val_v = vec_msums(src_v, filter_v, (vector signed int)vzero);
                val_s = vec_sums(val_v, vzero);
                vec_st(val_s, 0, tempo);
                dst[i] = FFMIN(tempo[3] >> 7, (1 << 15) - 1);
            }
        break;

        case 16:
            for (i = 0; i < dstW; i++) {
                register int srcPos = filterPos[i];

                vector unsigned char src_v0 = vec_ld(srcPos, src);
                vector unsigned char src_v1 = vec_ld(srcPos + 16, src);
                vector unsigned char src_vF = vec_perm(src_v0, src_v1, vec_lvsl(srcPos, src));

                vector signed short src_vA = // vec_unpackh sign-extends...
                                             (vector signed short)(vec_mergeh((vector unsigned char)vzero, src_vF));
                vector signed short src_vB = // vec_unpackh sign-extends...
                                             (vector signed short)(vec_mergel((vector unsigned char)vzero, src_vF));

                vector signed short filter_v0 = vec_ld(i << 5, filter);
                vector signed short filter_v1 = vec_ld((i << 5) + 16, filter);
                // the 5 above are 4 (filterSize == 16) + 1 (sizeof(short) == 2)

                vector signed int val_acc = vec_msums(src_vA, filter_v0, (vector signed int)vzero);
                vector signed int val_v   = vec_msums(src_vB, filter_v1, val_acc);

                vector signed int val_s = vec_sums(val_v, vzero);

                vec_st(val_s, 0, tempo);
                dst[i] = FFMIN(tempo[3] >> 7, (1 << 15) - 1);
            }
        break;

        default:
            for (i = 0; i < dstW; i++) {
                register int j;
                register int srcPos = filterPos[i];

                vector signed int val_s, val_v = (vector signed int)vzero;
                vector signed short filter_v0R = vec_ld(i * 2 * filterSize, filter);
                vector unsigned char permF     = vec_lvsl((i * 2 * filterSize), filter);

                vector unsigned char src_v0 = vec_ld(srcPos, src);
                vector unsigned char permS  = vec_lvsl(srcPos, src);

                for (j = 0; j < filterSize - 15; j += 16) {
                    vector unsigned char src_v1 = vec_ld(srcPos + j + 16, src);
                    vector unsigned char src_vF = vec_perm(src_v0, src_v1, permS);

                    vector signed short src_vA = // vec_unpackh sign-extends...
                                                 (vector signed short)(vec_mergeh((vector unsigned char)vzero, src_vF));
                    vector signed short src_vB = // vec_unpackh sign-extends...
                                                 (vector signed short)(vec_mergel((vector unsigned char)vzero, src_vF));

                    vector signed short filter_v1R = vec_ld((i * 2 * filterSize) + (j * 2) + 16, filter);
                    vector signed short filter_v2R = vec_ld((i * 2 * filterSize) + (j * 2) + 32, filter);
                    vector signed short filter_v0  = vec_perm(filter_v0R, filter_v1R, permF);
                    vector signed short filter_v1  = vec_perm(filter_v1R, filter_v2R, permF);

                    vector signed int val_acc = vec_msums(src_vA, filter_v0, val_v);
                    val_v = vec_msums(src_vB, filter_v1, val_acc);

                    filter_v0R = filter_v2R;
                    src_v0     = src_v1;
                }

                if (j < filterSize - 7) {
                    // loading src_v0 is useless, it's already done above
                    // vector unsigned char src_v0 = vec_ld(srcPos + j, src);
                    vector unsigned char src_v1, src_vF;
                    vector signed short src_v, filter_v1R, filter_v;
                    if ((((uintptr_t)src + srcPos) % 16) > 8) {
                        src_v1 = vec_ld(srcPos + j + 16, src);
                    }
                    src_vF = vec_perm(src_v0, src_v1, permS);

                    src_v = // vec_unpackh sign-extends...
                            (vector signed short)(vec_mergeh((vector unsigned char)vzero, src_vF));
                    // loading filter_v0R is useless, it's already done above
                    // vector signed short filter_v0R = vec_ld((i * 2 * filterSize) + j, filter);
                    filter_v1R = vec_ld((i * 2 * filterSize) + (j * 2) + 16, filter);
                    filter_v   = vec_perm(filter_v0R, filter_v1R, permF);

                    val_v = vec_msums(src_v, filter_v, val_v);
                }

                val_s = vec_sums(val_v, vzero);

                vec_st(val_s, 0, tempo);
                dst[i] = FFMIN(tempo[3] >> 7, (1 << 15) - 1);
            }
        }
}

void ff_sws_init_swScale_altivec(SwsContext *c)
{
    enum PixelFormat dstFormat = c->dstFormat;

    if (!(av_get_cpu_flags() & AV_CPU_FLAG_ALTIVEC))
        return;

    if (c->srcBpc == 8 && c->dstBpc <= 10) {
        c->hyScale = c->hcScale = hScale_altivec_real;
    }
    if (!is16BPS(dstFormat) && !is9_OR_10BPS(dstFormat) &&
        dstFormat != PIX_FMT_NV12 && dstFormat != PIX_FMT_NV21 &&
        !c->alpPixBuf) {
        c->yuv2planeX = yuv2planeX_altivec;
    }

    /* The following list of supported dstFormat values should
     * match what's found in the body of ff_yuv2packedX_altivec() */
    if (!(c->flags & (SWS_BITEXACT | SWS_FULL_CHR_H_INT)) && !c->alpPixBuf) {
        switch (c->dstFormat) {
        case PIX_FMT_ABGR:
            c->yuv2packedX = ff_yuv2abgr_X_altivec;
            break;
        case PIX_FMT_BGRA:
            c->yuv2packedX = ff_yuv2bgra_X_altivec;
            break;
        case PIX_FMT_ARGB:
            c->yuv2packedX = ff_yuv2argb_X_altivec;
            break;
        case PIX_FMT_RGBA:
            c->yuv2packedX = ff_yuv2rgba_X_altivec;
            break;
        case PIX_FMT_BGR24:
            c->yuv2packedX = ff_yuv2bgr24_X_altivec;
            break;
        case PIX_FMT_RGB24:
            c->yuv2packedX = ff_yuv2rgb24_X_altivec;
            break;
        }
    }
}
