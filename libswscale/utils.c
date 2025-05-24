/*
 * Copyright (C) 2024 Niklas Haas
 * Copyright (C) 2001-2003 Michael Niedermayer <michaelni@gmx.at>
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

#define _DEFAULT_SOURCE
#define _SVID_SOURCE // needed for MAP_ANONYMOUS
#define _DARWIN_C_SOURCE // needed for MAP_ANON
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#if HAVE_MMAP
#include <sys/mman.h>
#if defined(MAP_ANON) && !defined(MAP_ANONYMOUS)
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif
#if HAVE_VIRTUALALLOC
#include <windows.h>
#endif

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/cpu.h"
#include "libavutil/csp.h"
#include "libavutil/emms.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/libm.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/slicethread.h"
#include "libavutil/thread.h"
#include "libavutil/aarch64/cpu.h"
#include "libavutil/ppc/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavutil/loongarch/cpu.h"

#include "rgb2rgb.h"
#include "swscale.h"
#include "swscale_internal.h"
#include "graph.h"

/**
 * Allocate and return an SwsContext without performing initialization.
 */
static SwsContext *alloc_set_opts(int srcW, int srcH, enum AVPixelFormat srcFormat,
                                  int dstW, int dstH, enum AVPixelFormat dstFormat,
                                  int flags, const double *param)
{
    SwsContext *sws = sws_alloc_context();
    if (!sws)
        return NULL;

    sws->flags = flags;
    sws->src_w = srcW;
    sws->src_h = srcH;
    sws->dst_w = dstW;
    sws->dst_h      = dstH;
    sws->src_format = srcFormat;
    sws->dst_format = dstFormat;

    if (param) {
        sws->scaler_params[0] = param[0];
        sws->scaler_params[1] = param[1];
    }

    return sws;
}

int ff_shuffle_filter_coefficients(SwsInternal *c, int *filterPos,
                                   int filterSize, int16_t *filter,
                                   int dstW)
{
#if ARCH_X86_64
    int i, j, k;
    int cpu_flags = av_get_cpu_flags();
    if (!filter)
        return 0;
    if (EXTERNAL_AVX2_FAST(cpu_flags) && !(cpu_flags & AV_CPU_FLAG_SLOW_GATHER)) {
        if ((c->srcBpc == 8) && (c->dstBpc <= 14)) {
           int16_t *filterCopy = NULL;
           if (filterSize > 4) {
               if (!FF_ALLOC_TYPED_ARRAY(filterCopy, dstW * filterSize))
                   return AVERROR(ENOMEM);
               memcpy(filterCopy, filter, dstW * filterSize * sizeof(int16_t));
           }
           // Do not swap filterPos for pixels which won't be processed by
           // the main loop.
           for (i = 0; i + 16 <= dstW; i += 16) {
               FFSWAP(int, filterPos[i + 2], filterPos[i + 4]);
               FFSWAP(int, filterPos[i + 3], filterPos[i + 5]);
               FFSWAP(int, filterPos[i + 10], filterPos[i + 12]);
               FFSWAP(int, filterPos[i + 11], filterPos[i + 13]);
           }
           if (filterSize > 4) {
               // 16 pixels are processed at a time.
               for (i = 0; i + 16 <= dstW; i += 16) {
                   // 4 filter coeffs are processed at a time.
                   for (k = 0; k + 4 <= filterSize; k += 4) {
                       for (j = 0; j < 16; ++j) {
                           int from = (i + j) * filterSize + k;
                           int to = i * filterSize + j * 4 + k * 16;
                           memcpy(&filter[to], &filterCopy[from], 4 * sizeof(int16_t));
                       }
                   }
               }
               // 4 pixels are processed at a time in the tail.
               for (; i < dstW; i += 4) {
                   // 4 filter coeffs are processed at a time.
                   int rem = dstW - i >= 4 ? 4 : dstW - i;
                   for (k = 0; k + 4 <= filterSize; k += 4) {
                       for (j = 0; j < rem; ++j) {
                           int from = (i + j) * filterSize + k;
                           int to = i * filterSize + j * 4 + k * 4;
                           memcpy(&filter[to], &filterCopy[from], 4 * sizeof(int16_t));
                       }
                   }
               }
           }
           av_free(filterCopy);
        }
    }
#endif
    return 0;
}

static double getSplineCoeff(double a, double b, double c, double d,
                             double dist)
{
    if (dist <= 1.0)
        return ((d * dist + c) * dist + b) * dist + a;
    else
        return getSplineCoeff(0.0,
                               b + 2.0 * c + 3.0 * d,
                               c + 3.0 * d,
                              -b - 3.0 * c - 6.0 * d,
                              dist - 1.0);
}

static av_cold int get_local_pos(SwsInternal *s, int chr_subsample, int pos, int dir)
{
    if (pos == -1 || pos <= -513) {
        pos = (128 << chr_subsample) - 128;
    }
    pos += 128; // relative to ideal left edge
    return pos >> chr_subsample;
}

typedef struct {
    int flag;                   ///< flag associated to the algorithm
    const char *description;    ///< human-readable description
    int size_factor;            ///< size factor used when initing the filters
} ScaleAlgorithm;

static const ScaleAlgorithm scale_algorithms[] = {
    { SWS_AREA,          "area averaging",                  1 /* downscale only, for upscale it is bilinear */ },
    { SWS_BICUBIC,       "bicubic",                         4 },
    { SWS_BICUBLIN,      "luma bicubic / chroma bilinear", -1 },
    { SWS_BILINEAR,      "bilinear",                        2 },
    { SWS_FAST_BILINEAR, "fast bilinear",                  -1 },
    { SWS_GAUSS,         "Gaussian",                        8 /* infinite ;) */ },
    { SWS_LANCZOS,       "Lanczos",                        -1 /* custom */ },
    { SWS_POINT,         "nearest neighbor / point",       -1 },
    { SWS_SINC,          "sinc",                           20 /* infinite ;) */ },
    { SWS_SPLINE,        "bicubic spline",                 20 /* infinite :)*/ },
    { SWS_X,             "experimental",                    8 },
};

static av_cold int initFilter(int16_t **outFilter, int32_t **filterPos,
                              int *outFilterSize, int xInc, int srcW,
                              int dstW, int filterAlign, int one,
                              int flags, int cpu_flags,
                              SwsVector *srcFilter, SwsVector *dstFilter,
                              double param[2], int srcPos, int dstPos)
{
    int i;
    int filterSize;
    int filter2Size;
    int minFilterSize;
    int64_t *filter    = NULL;
    int64_t *filter2   = NULL;
    const int64_t fone = 1LL << (54 - FFMIN(av_log2(srcW/dstW), 8));
    int ret            = -1;

    emms_c(); // FIXME should not be required but IS (even for non-MMX versions)

    // NOTE: the +3 is for the MMX(+1) / SSE(+3) scaler which reads over the end
    if (!FF_ALLOC_TYPED_ARRAY(*filterPos, dstW + 3))
        goto nomem;

    if (FFABS(xInc - 0x10000) < 10 && srcPos == dstPos) { // unscaled
        int i;
        filterSize = 1;
        if (!FF_ALLOCZ_TYPED_ARRAY(filter, dstW * filterSize))
            goto nomem;

        for (i = 0; i < dstW; i++) {
            filter[i * filterSize] = fone;
            (*filterPos)[i]        = i;
        }
    } else if (flags & SWS_POINT) { // lame looking point sampling mode
        int i;
        int64_t xDstInSrc;
        filterSize = 1;
        if (!FF_ALLOC_TYPED_ARRAY(filter, dstW * filterSize))
            goto nomem;

        xDstInSrc = ((dstPos*(int64_t)xInc)>>8) - ((srcPos*0x8000LL)>>7);
        for (i = 0; i < dstW; i++) {
            int xx = (xDstInSrc - ((filterSize - 1) << 15) + (1 << 15)) >> 16;

            (*filterPos)[i] = xx;
            filter[i]       = fone;
            xDstInSrc      += xInc;
        }
    } else if ((xInc <= (1 << 16) && (flags & SWS_AREA)) ||
               (flags & SWS_FAST_BILINEAR)) { // bilinear upscale
        int i;
        int64_t xDstInSrc;
        filterSize = 2;
        if (!FF_ALLOC_TYPED_ARRAY(filter, dstW * filterSize))
            goto nomem;

        xDstInSrc = ((dstPos*(int64_t)xInc)>>8) - ((srcPos*0x8000LL)>>7);
        for (i = 0; i < dstW; i++) {
            int xx = (xDstInSrc - ((filterSize - 1) << 15) + (1 << 15)) >> 16;
            int j;

            (*filterPos)[i] = xx;
            // bilinear upscale / linear interpolate / area averaging
            for (j = 0; j < filterSize; j++) {
                int64_t coeff = fone - FFABS((int64_t)xx * (1 << 16) - xDstInSrc) * (fone >> 16);
                if (coeff < 0)
                    coeff = 0;
                filter[i * filterSize + j] = coeff;
                xx++;
            }
            xDstInSrc += xInc;
        }
    } else {
        int64_t xDstInSrc;
        int sizeFactor = -1;

        for (i = 0; i < FF_ARRAY_ELEMS(scale_algorithms); i++) {
            if (flags & scale_algorithms[i].flag && scale_algorithms[i].size_factor > 0) {
                sizeFactor = scale_algorithms[i].size_factor;
                break;
            }
        }
        if (flags & SWS_LANCZOS)
            sizeFactor = param[0] != SWS_PARAM_DEFAULT ? ceil(2 * param[0]) : 6;
        av_assert0(sizeFactor > 0);

        if (xInc <= 1 << 16)
            filterSize = 1 + sizeFactor;    // upscale
        else
            filterSize = 1 + (sizeFactor * srcW + dstW - 1) / dstW;

        filterSize = FFMIN(filterSize, srcW - 2);
        filterSize = FFMAX(filterSize, 1);

        if (!FF_ALLOC_TYPED_ARRAY(filter, dstW * filterSize))
            goto nomem;
        xDstInSrc = ((dstPos*(int64_t)xInc)>>7) - ((srcPos*0x10000LL)>>7);
        for (i = 0; i < dstW; i++) {
            int xx = (xDstInSrc - (filterSize - 2) * (1LL<<16)) / (1 << 17);
            int j;
            (*filterPos)[i] = xx;
            for (j = 0; j < filterSize; j++) {
                int64_t d = (FFABS(((int64_t)xx * (1 << 17)) - xDstInSrc)) << 13;
                double floatd;
                int64_t coeff;

                if (xInc > 1 << 16)
                    d = d * dstW / srcW;
                floatd = d * (1.0 / (1 << 30));

                if (flags & SWS_BICUBIC) {
                    int64_t B = (param[0] != SWS_PARAM_DEFAULT ? param[0] :   0) * (1 << 24);
                    int64_t C = (param[1] != SWS_PARAM_DEFAULT ? param[1] : 0.6) * (1 << 24);

                    if (d >= 1LL << 31) {
                        coeff = 0.0;
                    } else {
                        int64_t dd  = (d  * d) >> 30;
                        int64_t ddd = (dd * d) >> 30;

                        if (d < 1LL << 30)
                            coeff =  (12 * (1 << 24) -  9 * B - 6 * C) * ddd +
                                    (-18 * (1 << 24) + 12 * B + 6 * C) *  dd +
                                      (6 * (1 << 24) -  2 * B)         * (1 << 30);
                        else
                            coeff =      (-B -  6 * C) * ddd +
                                      (6 * B + 30 * C) * dd  +
                                    (-12 * B - 48 * C) * d   +
                                      (8 * B + 24 * C) * (1 << 30);
                    }
                    coeff /= (1LL<<54)/fone;
                } else if (flags & SWS_X) {
                    double A = param[0] != SWS_PARAM_DEFAULT ? param[0] : 1.0;
                    double c;

                    if (floatd < 1.0)
                        c = cos(floatd * M_PI);
                    else
                        c = -1.0;
                    if (c < 0.0)
                        c = -pow(-c, A);
                    else
                        c = pow(c, A);
                    coeff = (c * 0.5 + 0.5) * fone;
                } else if (flags & SWS_AREA) {
                    int64_t d2 = d - (1 << 29);
                    if (d2 * xInc < -(1LL << (29 + 16)))
                        coeff = 1.0 * (1LL << (30 + 16));
                    else if (d2 * xInc < (1LL << (29 + 16)))
                        coeff = -d2 * xInc + (1LL << (29 + 16));
                    else
                        coeff = 0.0;
                    coeff *= fone >> (30 + 16);
                } else if (flags & SWS_GAUSS) {
                    double p = param[0] != SWS_PARAM_DEFAULT ? param[0] : 3.0;
                    coeff = exp2(-p * floatd * floatd) * fone;
                } else if (flags & SWS_SINC) {
                    coeff = (d ? sin(floatd * M_PI) / (floatd * M_PI) : 1.0) * fone;
                } else if (flags & SWS_LANCZOS) {
                    double p = param[0] != SWS_PARAM_DEFAULT ? param[0] : 3.0;
                    coeff = (d ? sin(floatd * M_PI) * sin(floatd * M_PI / p) /
                             (floatd * floatd * M_PI * M_PI / p) : 1.0) * fone;
                    if (floatd > p)
                        coeff = 0;
                } else if (flags & SWS_BILINEAR) {
                    coeff = (1 << 30) - d;
                    if (coeff < 0)
                        coeff = 0;
                    coeff *= fone >> 30;
                } else if (flags & SWS_SPLINE) {
                    double p = -2.196152422706632;
                    coeff = getSplineCoeff(1.0, 0.0, p, -p - 1.0, floatd) * fone;
                } else {
                    av_assert0(0);
                }

                filter[i * filterSize + j] = coeff;
                xx++;
            }
            xDstInSrc += 2LL * xInc;
        }
    }

    /* apply src & dst Filter to filter -> filter2
     * av_free(filter);
     */
    av_assert0(filterSize > 0);
    filter2Size = filterSize;
    if (srcFilter)
        filter2Size += srcFilter->length - 1;
    if (dstFilter)
        filter2Size += dstFilter->length - 1;
    av_assert0(filter2Size > 0);
    if (!FF_ALLOCZ_TYPED_ARRAY(filter2, dstW * filter2Size))
        goto nomem;
    for (i = 0; i < dstW; i++) {
        int j, k;

        if (srcFilter) {
            for (k = 0; k < srcFilter->length; k++) {
                for (j = 0; j < filterSize; j++)
                    filter2[i * filter2Size + k + j] +=
                        srcFilter->coeff[k] * filter[i * filterSize + j];
            }
        } else {
            for (j = 0; j < filterSize; j++)
                filter2[i * filter2Size + j] = filter[i * filterSize + j];
        }
        // FIXME dstFilter

        (*filterPos)[i] += (filterSize - 1) / 2 - (filter2Size - 1) / 2;
    }
    av_freep(&filter);

    /* try to reduce the filter-size (step1 find size and shift left) */
    // Assume it is near normalized (*0.5 or *2.0 is OK but * 0.001 is not).
    minFilterSize = 0;
    for (i = dstW - 1; i >= 0; i--) {
        int min = filter2Size;
        int j;
        int64_t cutOff = 0.0;

        /* get rid of near zero elements on the left by shifting left */
        for (j = 0; j < filter2Size; j++) {
            int k;
            cutOff += FFABS(filter2[i * filter2Size]);

            if (cutOff > SWS_MAX_REDUCE_CUTOFF * fone)
                break;

            /* preserve monotonicity because the core can't handle the
             * filter otherwise */
            if (i < dstW - 1 && (*filterPos)[i] >= (*filterPos)[i + 1])
                break;

            // move filter coefficients left
            for (k = 1; k < filter2Size; k++)
                filter2[i * filter2Size + k - 1] = filter2[i * filter2Size + k];
            filter2[i * filter2Size + k - 1] = 0;
            (*filterPos)[i]++;
        }

        cutOff = 0;
        /* count near zeros on the right */
        for (j = filter2Size - 1; j > 0; j--) {
            cutOff += FFABS(filter2[i * filter2Size + j]);

            if (cutOff > SWS_MAX_REDUCE_CUTOFF * fone)
                break;
            min--;
        }

        if (min > minFilterSize)
            minFilterSize = min;
    }

    if (PPC_ALTIVEC(cpu_flags)) {
        // we can handle the special case 4, so we don't want to go the full 8
        if (minFilterSize < 5)
            filterAlign = 4;

        /* We really don't want to waste our time doing useless computation, so
         * fall back on the scalar C code for very small filters.
         * Vectorizing is worth it only if you have a decent-sized vector. */
        if (minFilterSize < 3)
            filterAlign = 1;
    }

    if (HAVE_MMX && cpu_flags & AV_CPU_FLAG_MMX || have_neon(cpu_flags)) {
        // special case for unscaled vertical filtering
        if (minFilterSize == 1 && filterAlign == 2)
            filterAlign = 1;
    }

    if (have_lasx(cpu_flags) || have_lsx(cpu_flags)) {
        int reNum = minFilterSize & (0x07);

        if (minFilterSize < 5)
            filterAlign = 4;
        if (reNum < 3)
            filterAlign = 1;
    }

    av_assert0(minFilterSize > 0);
    filterSize = (minFilterSize + (filterAlign - 1)) & (~(filterAlign - 1));
    av_assert0(filterSize > 0);
    filter = av_malloc_array(dstW, filterSize * sizeof(*filter));
    if (!filter)
        goto nomem;
    if (filterSize >= MAX_FILTER_SIZE * 16 /
                      ((flags & SWS_ACCURATE_RND) ? APCK_SIZE : 16)) {
        ret = RETCODE_USE_CASCADE;
        goto fail;
    }
    *outFilterSize = filterSize;

    if (flags & SWS_PRINT_INFO)
        av_log(NULL, AV_LOG_VERBOSE,
               "SwScaler: reducing / aligning filtersize %d -> %d\n",
               filter2Size, filterSize);
    /* try to reduce the filter-size (step2 reduce it) */
    for (i = 0; i < dstW; i++) {
        int j;

        for (j = 0; j < filterSize; j++) {
            if (j >= filter2Size)
                filter[i * filterSize + j] = 0;
            else
                filter[i * filterSize + j] = filter2[i * filter2Size + j];
            if ((flags & SWS_BITEXACT) && j >= minFilterSize)
                filter[i * filterSize + j] = 0;
        }
    }

    // FIXME try to align filterPos if possible

    // fix borders
    for (i = 0; i < dstW; i++) {
        int j;
        if ((*filterPos)[i] < 0) {
            // move filter coefficients left to compensate for filterPos
            for (j = 1; j < filterSize; j++) {
                int left = FFMAX(j + (*filterPos)[i], 0);
                filter[i * filterSize + left] += filter[i * filterSize + j];
                filter[i * filterSize + j]     = 0;
            }
            (*filterPos)[i]= 0;
        }

        if ((*filterPos)[i] + filterSize > srcW) {
            int shift = (*filterPos)[i] + FFMIN(filterSize - srcW, 0);
            int64_t acc = 0;

            for (j = filterSize - 1; j >= 0; j--) {
                if ((*filterPos)[i] + j >= srcW) {
                    acc += filter[i * filterSize + j];
                    filter[i * filterSize + j] = 0;
                }
            }
            for (j = filterSize - 1; j >= 0; j--) {
                if (j < shift) {
                    filter[i * filterSize + j] = 0;
                } else {
                    filter[i * filterSize + j] = filter[i * filterSize + j - shift];
                }
            }

            (*filterPos)[i]-= shift;
            filter[i * filterSize + srcW - 1 - (*filterPos)[i]] += acc;
        }
        av_assert0((*filterPos)[i] >= 0);
        av_assert0((*filterPos)[i] < srcW);
        if ((*filterPos)[i] + filterSize > srcW) {
            for (j = 0; j < filterSize; j++) {
                av_assert0((*filterPos)[i] + j < srcW || !filter[i * filterSize + j]);
            }
        }
    }

    // Note the +1 is for the MMX scaler which reads over the end
    /* align at 16 for AltiVec (needed by hScale_altivec_real) */
    if (!FF_ALLOCZ_TYPED_ARRAY(*outFilter, *outFilterSize * (dstW + 3)))
        goto nomem;

    /* normalize & store in outFilter */
    for (i = 0; i < dstW; i++) {
        int j;
        int64_t error = 0;
        int64_t sum   = 0;

        for (j = 0; j < filterSize; j++) {
            sum += filter[i * filterSize + j];
        }
        sum = (sum + one / 2) / one;
        if (!sum) {
            av_log(NULL, AV_LOG_WARNING, "SwScaler: zero vector in scaling\n");
            sum = 1;
        }
        for (j = 0; j < *outFilterSize; j++) {
            int64_t v = filter[i * filterSize + j] + error;
            int intV  = ROUNDED_DIV(v, sum);
            (*outFilter)[i * (*outFilterSize) + j] = intV;
            error                                  = v - intV * sum;
        }
    }

    (*filterPos)[dstW + 0] =
    (*filterPos)[dstW + 1] =
    (*filterPos)[dstW + 2] = (*filterPos)[dstW - 1]; /* the MMX/SSE scaler will
                                                      * read over the end */
    for (i = 0; i < *outFilterSize; i++) {
        int k = (dstW - 1) * (*outFilterSize) + i;
        (*outFilter)[k + 1 * (*outFilterSize)] =
        (*outFilter)[k + 2 * (*outFilterSize)] =
        (*outFilter)[k + 3 * (*outFilterSize)] = (*outFilter)[k];
    }

    ret = 0;
    goto done;
nomem:
    ret = AVERROR(ENOMEM);
fail:
    if(ret < 0)
        av_log(NULL, ret == RETCODE_USE_CASCADE ? AV_LOG_DEBUG : AV_LOG_ERROR, "sws: initFilter failed\n");
done:
    av_free(filter);
    av_free(filter2);
    return ret;
}

static void fill_rgb2yuv_table(SwsInternal *c, const int table[4], int dstRange)
{
    int64_t W, V, Z, Cy, Cu, Cv;
    int64_t vr =  table[0];
    int64_t ub =  table[1];
    int64_t ug = -table[2];
    int64_t vg = -table[3];
    int64_t ONE = 65536;
    int64_t cy = ONE;
    uint8_t *p = (uint8_t*)c->input_rgb2yuv_table;
    int i;
    static const int8_t map[] = {
    BY_IDX, GY_IDX, -1    , BY_IDX, BY_IDX, GY_IDX, -1    , BY_IDX,
    RY_IDX, -1    , GY_IDX, RY_IDX, RY_IDX, -1    , GY_IDX, RY_IDX,
    RY_IDX, GY_IDX, -1    , RY_IDX, RY_IDX, GY_IDX, -1    , RY_IDX,
    BY_IDX, -1    , GY_IDX, BY_IDX, BY_IDX, -1    , GY_IDX, BY_IDX,
    BU_IDX, GU_IDX, -1    , BU_IDX, BU_IDX, GU_IDX, -1    , BU_IDX,
    RU_IDX, -1    , GU_IDX, RU_IDX, RU_IDX, -1    , GU_IDX, RU_IDX,
    RU_IDX, GU_IDX, -1    , RU_IDX, RU_IDX, GU_IDX, -1    , RU_IDX,
    BU_IDX, -1    , GU_IDX, BU_IDX, BU_IDX, -1    , GU_IDX, BU_IDX,
    BV_IDX, GV_IDX, -1    , BV_IDX, BV_IDX, GV_IDX, -1    , BV_IDX,
    RV_IDX, -1    , GV_IDX, RV_IDX, RV_IDX, -1    , GV_IDX, RV_IDX,
    RV_IDX, GV_IDX, -1    , RV_IDX, RV_IDX, GV_IDX, -1    , RV_IDX,
    BV_IDX, -1    , GV_IDX, BV_IDX, BV_IDX, -1    , GV_IDX, BV_IDX,
    RY_IDX, BY_IDX, RY_IDX, BY_IDX, RY_IDX, BY_IDX, RY_IDX, BY_IDX,
    BY_IDX, RY_IDX, BY_IDX, RY_IDX, BY_IDX, RY_IDX, BY_IDX, RY_IDX,
    GY_IDX, -1    , GY_IDX, -1    , GY_IDX, -1    , GY_IDX, -1    ,
    -1    , GY_IDX, -1    , GY_IDX, -1    , GY_IDX, -1    , GY_IDX,
    RU_IDX, BU_IDX, RU_IDX, BU_IDX, RU_IDX, BU_IDX, RU_IDX, BU_IDX,
    BU_IDX, RU_IDX, BU_IDX, RU_IDX, BU_IDX, RU_IDX, BU_IDX, RU_IDX,
    GU_IDX, -1    , GU_IDX, -1    , GU_IDX, -1    , GU_IDX, -1    ,
    -1    , GU_IDX, -1    , GU_IDX, -1    , GU_IDX, -1    , GU_IDX,
    RV_IDX, BV_IDX, RV_IDX, BV_IDX, RV_IDX, BV_IDX, RV_IDX, BV_IDX,
    BV_IDX, RV_IDX, BV_IDX, RV_IDX, BV_IDX, RV_IDX, BV_IDX, RV_IDX,
    GV_IDX, -1    , GV_IDX, -1    , GV_IDX, -1    , GV_IDX, -1    ,
    -1    , GV_IDX, -1    , GV_IDX, -1    , GV_IDX, -1    , GV_IDX, //23
    -1    , -1    , -1    , -1    , -1    , -1    , -1    , -1    , //24
    -1    , -1    , -1    , -1    , -1    , -1    , -1    , -1    , //25
    -1    , -1    , -1    , -1    , -1    , -1    , -1    , -1    , //26
    -1    , -1    , -1    , -1    , -1    , -1    , -1    , -1    , //27
    -1    , -1    , -1    , -1    , -1    , -1    , -1    , -1    , //28
    -1    , -1    , -1    , -1    , -1    , -1    , -1    , -1    , //29
    -1    , -1    , -1    , -1    , -1    , -1    , -1    , -1    , //30
    -1    , -1    , -1    , -1    , -1    , -1    , -1    , -1    , //31
    BY_IDX, GY_IDX, RY_IDX, -1    , -1    , -1    , -1    , -1    , //32
    BU_IDX, GU_IDX, RU_IDX, -1    , -1    , -1    , -1    , -1    , //33
    BV_IDX, GV_IDX, RV_IDX, -1    , -1    , -1    , -1    , -1    , //34
    };

    dstRange = 0; //FIXME range = 1 is handled elsewhere

    if (!dstRange) {
        cy = cy * 255 / 219;
    } else {
        vr = vr * 224 / 255;
        ub = ub * 224 / 255;
        ug = ug * 224 / 255;
        vg = vg * 224 / 255;
    }
    W = ROUNDED_DIV(ONE*ONE*ug, ub);
    V = ROUNDED_DIV(ONE*ONE*vg, vr);
    Z = ONE*ONE-W-V;

    Cy = ROUNDED_DIV(cy*Z, ONE);
    Cu = ROUNDED_DIV(ub*Z, ONE);
    Cv = ROUNDED_DIV(vr*Z, ONE);

    c->input_rgb2yuv_table[RY_IDX] = -ROUNDED_DIV((1 << RGB2YUV_SHIFT)*V        , Cy);
    c->input_rgb2yuv_table[GY_IDX] =  ROUNDED_DIV((1 << RGB2YUV_SHIFT)*ONE*ONE  , Cy);
    c->input_rgb2yuv_table[BY_IDX] = -ROUNDED_DIV((1 << RGB2YUV_SHIFT)*W        , Cy);

    c->input_rgb2yuv_table[RU_IDX] =  ROUNDED_DIV((1 << RGB2YUV_SHIFT)*V        , Cu);
    c->input_rgb2yuv_table[GU_IDX] = -ROUNDED_DIV((1 << RGB2YUV_SHIFT)*ONE*ONE  , Cu);
    c->input_rgb2yuv_table[BU_IDX] =  ROUNDED_DIV((1 << RGB2YUV_SHIFT)*(Z+W)    , Cu);

    c->input_rgb2yuv_table[RV_IDX] =  ROUNDED_DIV((1 << RGB2YUV_SHIFT)*(V+Z)    , Cv);
    c->input_rgb2yuv_table[GV_IDX] = -ROUNDED_DIV((1 << RGB2YUV_SHIFT)*ONE*ONE  , Cv);
    c->input_rgb2yuv_table[BV_IDX] =  ROUNDED_DIV((1 << RGB2YUV_SHIFT)*W        , Cv);

    if(/*!dstRange && */!memcmp(table, ff_yuv2rgb_coeffs[SWS_CS_DEFAULT], sizeof(ff_yuv2rgb_coeffs[SWS_CS_DEFAULT]))) {
        c->input_rgb2yuv_table[BY_IDX] =  ((int)(0.114 * 219 / 255 * (1 << RGB2YUV_SHIFT) + 0.5));
        c->input_rgb2yuv_table[BV_IDX] = (-(int)(0.081 * 224 / 255 * (1 << RGB2YUV_SHIFT) + 0.5));
        c->input_rgb2yuv_table[BU_IDX] =  ((int)(0.500 * 224 / 255 * (1 << RGB2YUV_SHIFT) + 0.5));
        c->input_rgb2yuv_table[GY_IDX] =  ((int)(0.587 * 219 / 255 * (1 << RGB2YUV_SHIFT) + 0.5));
        c->input_rgb2yuv_table[GV_IDX] = (-(int)(0.419 * 224 / 255 * (1 << RGB2YUV_SHIFT) + 0.5));
        c->input_rgb2yuv_table[GU_IDX] = (-(int)(0.331 * 224 / 255 * (1 << RGB2YUV_SHIFT) + 0.5));
        c->input_rgb2yuv_table[RY_IDX] =  ((int)(0.299 * 219 / 255 * (1 << RGB2YUV_SHIFT) + 0.5));
        c->input_rgb2yuv_table[RV_IDX] =  ((int)(0.500 * 224 / 255 * (1 << RGB2YUV_SHIFT) + 0.5));
        c->input_rgb2yuv_table[RU_IDX] = (-(int)(0.169 * 224 / 255 * (1 << RGB2YUV_SHIFT) + 0.5));
    }
    for(i=0; i<FF_ARRAY_ELEMS(map); i++)
        AV_WL16(p + 16*4 + 2*i, map[i] >= 0 ? c->input_rgb2yuv_table[map[i]] : 0);
}

#if CONFIG_SMALL
static void init_xyz_tables(uint16_t xyzgamma_tab[4096],  uint16_t xyzgammainv_tab[65536],
                            uint16_t rgbgamma_tab[65536], uint16_t rgbgammainv_tab[4096])
#else
static uint16_t xyzgamma_tab[4096],  rgbgammainv_tab[4096];
static uint16_t rgbgamma_tab[65536], xyzgammainv_tab[65536];
static av_cold void init_xyz_tables(void)
#endif
{
    double xyzgamma    = XYZ_GAMMA;
    double rgbgamma    = 1.0 / RGB_GAMMA;
    double xyzgammainv = 1.0 / XYZ_GAMMA;
    double rgbgammainv = RGB_GAMMA;

    /* set input gamma vectors */
    for (int i = 0; i < 4096; i++) {
        xyzgamma_tab[i]    = lrint(pow(i / 4095.0, xyzgamma)    * 65535.0);
        rgbgammainv_tab[i] = lrint(pow(i / 4095.0, rgbgammainv) * 65535.0);
    }

    /* set output gamma vectors */
    for (int i = 0; i < 65536; i++) {
        rgbgamma_tab[i]    = lrint(pow(i / 65535.0, rgbgamma)    * 4095.0);
        xyzgammainv_tab[i] = lrint(pow(i / 65535.0, xyzgammainv) * 4095.0);
    }
}

static int fill_xyztables(SwsInternal *c)
{
    static const int16_t xyz2rgb_matrix[3][4] = {
        {13270, -6295, -2041},
        {-3969,  7682,   170},
        {  228,  -835,  4329} };
    static const int16_t rgb2xyz_matrix[3][4] = {
        {1689, 1464,  739},
        { 871, 2929,  296},
        {  79,  488, 3891} };

    if (c->xyzgamma)
        return 0;

    memcpy(c->xyz2rgb_matrix, xyz2rgb_matrix, sizeof(c->xyz2rgb_matrix));
    memcpy(c->rgb2xyz_matrix, rgb2xyz_matrix, sizeof(c->rgb2xyz_matrix));

#if CONFIG_SMALL
    c->xyzgamma = av_malloc(sizeof(uint16_t) * 2 * (4096 + 65536));
    if (!c->xyzgamma)
        return AVERROR(ENOMEM);
    c->rgbgammainv = c->xyzgamma + 4096;
    c->rgbgamma = c->rgbgammainv + 4096;
    c->xyzgammainv = c->rgbgamma + 65536;
    init_xyz_tables(c->xyzgamma, c->xyzgammainv, c->rgbgamma, c->rgbgammainv);
#else
    c->xyzgamma = xyzgamma_tab;
    c->rgbgamma = rgbgamma_tab;
    c->xyzgammainv = xyzgammainv_tab;
    c->rgbgammainv = rgbgammainv_tab;

    static AVOnce xyz_init_static_once = AV_ONCE_INIT;
    ff_thread_once(&xyz_init_static_once, init_xyz_tables);
#endif
    return 0;
}

static int handle_jpeg(enum AVPixelFormat *format)
{
    switch (*format) {
    case AV_PIX_FMT_YUVJ420P:
        *format = AV_PIX_FMT_YUV420P;
        return 1;
    case AV_PIX_FMT_YUVJ411P:
        *format = AV_PIX_FMT_YUV411P;
        return 1;
    case AV_PIX_FMT_YUVJ422P:
        *format = AV_PIX_FMT_YUV422P;
        return 1;
    case AV_PIX_FMT_YUVJ444P:
        *format = AV_PIX_FMT_YUV444P;
        return 1;
    case AV_PIX_FMT_YUVJ440P:
        *format = AV_PIX_FMT_YUV440P;
        return 1;
    case AV_PIX_FMT_GRAY8:
    case AV_PIX_FMT_YA8:
    case AV_PIX_FMT_GRAY9LE:
    case AV_PIX_FMT_GRAY9BE:
    case AV_PIX_FMT_GRAY10LE:
    case AV_PIX_FMT_GRAY10BE:
    case AV_PIX_FMT_GRAY12LE:
    case AV_PIX_FMT_GRAY12BE:
    case AV_PIX_FMT_GRAY14LE:
    case AV_PIX_FMT_GRAY14BE:
    case AV_PIX_FMT_GRAY16LE:
    case AV_PIX_FMT_GRAY16BE:
    case AV_PIX_FMT_YA16BE:
    case AV_PIX_FMT_YA16LE:
        return 1;
    default:
        return 0;
    }
}

static int handle_0alpha(enum AVPixelFormat *format)
{
    switch (*format) {
    case AV_PIX_FMT_0BGR    : *format = AV_PIX_FMT_ABGR   ; return 1;
    case AV_PIX_FMT_BGR0    : *format = AV_PIX_FMT_BGRA   ; return 4;
    case AV_PIX_FMT_0RGB    : *format = AV_PIX_FMT_ARGB   ; return 1;
    case AV_PIX_FMT_RGB0    : *format = AV_PIX_FMT_RGBA   ; return 4;
    default:                                          return 0;
    }
}

static int handle_xyz(enum AVPixelFormat *format)
{
    switch (*format) {
    case AV_PIX_FMT_XYZ12BE : *format = AV_PIX_FMT_RGB48BE; return 1;
    case AV_PIX_FMT_XYZ12LE : *format = AV_PIX_FMT_RGB48LE; return 1;
    default:                                                return 0;
    }
}

static int handle_formats(SwsContext *sws)
{
    SwsInternal *c = sws_internal(sws);
    c->src0Alpha |= handle_0alpha(&sws->src_format);
    c->dst0Alpha |= handle_0alpha(&sws->dst_format);
    c->srcXYZ    |= handle_xyz(&sws->src_format);
    c->dstXYZ    |= handle_xyz(&sws->dst_format);
    if (c->srcXYZ || c->dstXYZ)
        return fill_xyztables(c);
    else
        return 0;
}

static int range_override_needed(enum AVPixelFormat format)
{
    return !isYUV(format) && !isGray(format);
}

int sws_setColorspaceDetails(SwsContext *sws, const int inv_table[4],
                             int srcRange, const int table[4], int dstRange,
                             int brightness, int contrast, int saturation)
{
    SwsInternal *c = sws_internal(sws);
    const AVPixFmtDescriptor *desc_dst;
    const AVPixFmtDescriptor *desc_src;
    int ret, need_reinit = 0;

    if (c->nb_slice_ctx) {
        int parent_ret = 0;
        for (int i = 0; i < c->nb_slice_ctx; i++) {
            int ret = sws_setColorspaceDetails(c->slice_ctx[i], inv_table,
                                               srcRange, table, dstRange,
                                               brightness, contrast, saturation);
            if (ret < 0)
                parent_ret = ret;
        }

        return parent_ret;
    }

    ret = handle_formats(sws);
    if (ret < 0)
        return ret;
    desc_dst = av_pix_fmt_desc_get(sws->dst_format);
    desc_src = av_pix_fmt_desc_get(sws->src_format);

    if(range_override_needed(sws->dst_format))
        dstRange = 0;
    if(range_override_needed(sws->src_format))
        srcRange = 0;

    if (sws->src_range != srcRange ||
        sws->dst_range != dstRange ||
        c->brightness != brightness ||
        c->contrast   != contrast ||
        c->saturation != saturation ||
        memcmp(c->srcColorspaceTable, inv_table, sizeof(int) * 4) ||
        memcmp(c->dstColorspaceTable,     table, sizeof(int) * 4)
    )
        need_reinit = 1;

    memmove(c->srcColorspaceTable, inv_table, sizeof(int) * 4);
    memmove(c->dstColorspaceTable, table, sizeof(int) * 4);



    c->brightness = brightness;
    c->contrast   = contrast;
    c->saturation = saturation;
    sws->src_range = srcRange;
    sws->dst_range = dstRange;

    if (need_reinit)
        ff_sws_init_range_convert(c);

    c->dstFormatBpp = av_get_bits_per_pixel(desc_dst);
    c->srcFormatBpp = av_get_bits_per_pixel(desc_src);

    if (c->cascaded_context[c->cascaded_mainindex])
        return sws_setColorspaceDetails(c->cascaded_context[c->cascaded_mainindex],inv_table, srcRange,table, dstRange, brightness,  contrast, saturation);

    if (!need_reinit)
        return 0;

    if ((isYUV(sws->dst_format) || isGray(sws->dst_format)) && (isYUV(sws->src_format) || isGray(sws->src_format))) {
        if (!c->cascaded_context[0] &&
            memcmp(c->dstColorspaceTable, c->srcColorspaceTable, sizeof(int) * 4) &&
            sws->src_w && sws->src_h && sws->dst_w && sws->dst_h) {
            enum AVPixelFormat tmp_format;
            int tmp_width, tmp_height;
            int srcW = sws->src_w;
            int srcH = sws->src_h;
            int dstW = sws->dst_w;
            int dstH = sws->dst_h;
            int ret;
            av_log(c, AV_LOG_VERBOSE, "YUV color matrix differs for YUV->YUV, using intermediate RGB to convert\n");

            if (isNBPS(sws->dst_format) || is16BPS(sws->dst_format)) {
                if (isALPHA(sws->src_format) && isALPHA(sws->dst_format)) {
                    tmp_format = AV_PIX_FMT_BGRA64;
                } else {
                    tmp_format = AV_PIX_FMT_BGR48;
                }
            } else {
                if (isALPHA(sws->src_format) && isALPHA(sws->dst_format)) {
                    tmp_format = AV_PIX_FMT_BGRA;
                } else {
                    tmp_format = AV_PIX_FMT_BGR24;
                }
            }

            if (srcW*srcH > dstW*dstH) {
                tmp_width  = dstW;
                tmp_height = dstH;
            } else {
                tmp_width  = srcW;
                tmp_height = srcH;
            }

            ret = av_image_alloc(c->cascaded_tmp[0], c->cascaded_tmpStride[0],
                                tmp_width, tmp_height, tmp_format, 64);
            if (ret < 0)
                return ret;

            c->cascaded_context[0] = alloc_set_opts(srcW, srcH, sws->src_format,
                                                    tmp_width, tmp_height, tmp_format,
                                                    sws->flags, sws->scaler_params);
            if (!c->cascaded_context[0])
                return -1;

            c->cascaded_context[0]->alpha_blend = sws->alpha_blend;
            ret = sws_init_context(c->cascaded_context[0], NULL , NULL);
            if (ret < 0)
                return ret;
            //we set both src and dst depending on that the RGB side will be ignored
            sws_setColorspaceDetails(c->cascaded_context[0], inv_table,
                                     srcRange, table, dstRange,
                                     brightness, contrast, saturation);

            c->cascaded_context[1] = alloc_set_opts(tmp_width, tmp_height, tmp_format,
                                                    dstW, dstH, sws->dst_format,
                                                    sws->flags, sws->scaler_params);
            if (!c->cascaded_context[1])
                return -1;
            c->cascaded_context[1]->src_range = srcRange;
            c->cascaded_context[1]->dst_range = dstRange;
            ret = sws_init_context(c->cascaded_context[1], NULL , NULL);
            if (ret < 0)
                return ret;
            sws_setColorspaceDetails(c->cascaded_context[1], inv_table,
                                     srcRange, table, dstRange,
                                     0, 1 << 16, 1 << 16);
            return 0;
        }
        //We do not support this combination currently, we need to cascade more contexts to compensate
        if (c->cascaded_context[0] && memcmp(c->dstColorspaceTable, c->srcColorspaceTable, sizeof(int) * 4))
            return -1; //AVERROR_PATCHWELCOME;
        return 0;
    }

    if (!isYUV(sws->dst_format) && !isGray(sws->dst_format)) {
        ff_yuv2rgb_c_init_tables(c, inv_table, srcRange, brightness,
                                 contrast, saturation);
        // FIXME factorize

#if ARCH_PPC
        ff_yuv2rgb_init_tables_ppc(c, inv_table, brightness,
                                   contrast, saturation);
#endif
    }

    fill_rgb2yuv_table(c, table, dstRange);

    return 0;
}

int sws_getColorspaceDetails(SwsContext *sws, int **inv_table,
                             int *srcRange, int **table, int *dstRange,
                             int *brightness, int *contrast, int *saturation)
{
    SwsInternal *c = sws_internal(sws);
    if (!c)
        return -1;

    if (c->nb_slice_ctx) {
        return sws_getColorspaceDetails(c->slice_ctx[0], inv_table, srcRange,
                                        table, dstRange, brightness, contrast,
                                        saturation);
    }

    *inv_table  = c->srcColorspaceTable;
    *table      = c->dstColorspaceTable;
    *srcRange   = range_override_needed(sws->src_format) ? 1 : sws->src_range;
    *dstRange   = range_override_needed(sws->dst_format) ? 1 : sws->dst_range;
    *brightness = c->brightness;
    *contrast   = c->contrast;
    *saturation = c->saturation;

    return 0;
}

SwsContext *sws_alloc_context(void)
{
    SwsInternal *c = (SwsInternal *) av_mallocz(sizeof(SwsInternal));
    if (!c)
        return NULL;

    c->opts.av_class = &ff_sws_context_class;
    av_opt_set_defaults(c);
    atomic_init(&c->stride_unaligned_warned, 0);
    atomic_init(&c->data_unaligned_warned,   0);

    return &c->opts;
}

static uint16_t * alloc_gamma_tbl(double e)
{
    int i = 0;
    uint16_t * tbl;
    tbl = (uint16_t*)av_malloc(sizeof(uint16_t) * 1 << 16);
    if (!tbl)
        return NULL;

    for (i = 0; i < 65536; ++i) {
        tbl[i] = pow(i / 65535.0, e) * 65535.0;
    }
    return tbl;
}

static enum AVPixelFormat alphaless_fmt(enum AVPixelFormat fmt)
{
    switch(fmt) {
    case AV_PIX_FMT_ARGB:       return AV_PIX_FMT_RGB24;
    case AV_PIX_FMT_RGBA:       return AV_PIX_FMT_RGB24;
    case AV_PIX_FMT_ABGR:       return AV_PIX_FMT_BGR24;
    case AV_PIX_FMT_BGRA:       return AV_PIX_FMT_BGR24;
    case AV_PIX_FMT_YA8:        return AV_PIX_FMT_GRAY8;

    case AV_PIX_FMT_YUVA420P:   return AV_PIX_FMT_YUV420P;
    case AV_PIX_FMT_YUVA422P:   return AV_PIX_FMT_YUV422P;
    case AV_PIX_FMT_YUVA444P:           return AV_PIX_FMT_YUV444P;

    case AV_PIX_FMT_GBRAP:              return AV_PIX_FMT_GBRP;

    case AV_PIX_FMT_GBRAP10LE:          return AV_PIX_FMT_GBRP10;
    case AV_PIX_FMT_GBRAP10BE:          return AV_PIX_FMT_GBRP10;

    case AV_PIX_FMT_GBRAP12LE:          return AV_PIX_FMT_GBRP12;
    case AV_PIX_FMT_GBRAP12BE:          return AV_PIX_FMT_GBRP12;

    case AV_PIX_FMT_GBRAP14LE:          return AV_PIX_FMT_GBRP14;
    case AV_PIX_FMT_GBRAP14BE:          return AV_PIX_FMT_GBRP14;

    case AV_PIX_FMT_GBRAP16LE:          return AV_PIX_FMT_GBRP16;
    case AV_PIX_FMT_GBRAP16BE:          return AV_PIX_FMT_GBRP16;

    case AV_PIX_FMT_RGBA64LE:   return AV_PIX_FMT_RGB48;
    case AV_PIX_FMT_RGBA64BE:   return AV_PIX_FMT_RGB48;
    case AV_PIX_FMT_BGRA64LE:   return AV_PIX_FMT_BGR48;
    case AV_PIX_FMT_BGRA64BE:   return AV_PIX_FMT_BGR48;

    case AV_PIX_FMT_YA16BE:             return AV_PIX_FMT_GRAY16;
    case AV_PIX_FMT_YA16LE:             return AV_PIX_FMT_GRAY16;

    case AV_PIX_FMT_YUVA420P9BE:        return AV_PIX_FMT_YUV420P9;
    case AV_PIX_FMT_YUVA422P9BE:        return AV_PIX_FMT_YUV422P9;
    case AV_PIX_FMT_YUVA444P9BE:        return AV_PIX_FMT_YUV444P9;
    case AV_PIX_FMT_YUVA420P9LE:        return AV_PIX_FMT_YUV420P9;
    case AV_PIX_FMT_YUVA422P9LE:        return AV_PIX_FMT_YUV422P9;
    case AV_PIX_FMT_YUVA444P9LE:        return AV_PIX_FMT_YUV444P9;
    case AV_PIX_FMT_YUVA420P10BE:       return AV_PIX_FMT_YUV420P10;
    case AV_PIX_FMT_YUVA422P10BE:       return AV_PIX_FMT_YUV422P10;
    case AV_PIX_FMT_YUVA444P10BE:       return AV_PIX_FMT_YUV444P10;
    case AV_PIX_FMT_YUVA420P10LE:       return AV_PIX_FMT_YUV420P10;
    case AV_PIX_FMT_YUVA422P10LE:       return AV_PIX_FMT_YUV422P10;
    case AV_PIX_FMT_YUVA444P10LE:       return AV_PIX_FMT_YUV444P10;
    case AV_PIX_FMT_YUVA420P16BE:       return AV_PIX_FMT_YUV420P16;
    case AV_PIX_FMT_YUVA422P16BE:       return AV_PIX_FMT_YUV422P16;
    case AV_PIX_FMT_YUVA444P16BE:       return AV_PIX_FMT_YUV444P16;
    case AV_PIX_FMT_YUVA420P16LE:       return AV_PIX_FMT_YUV420P16;
    case AV_PIX_FMT_YUVA422P16LE:       return AV_PIX_FMT_YUV422P16;
    case AV_PIX_FMT_YUVA444P16LE:       return AV_PIX_FMT_YUV444P16;

//     case AV_PIX_FMT_AYUV64LE:
//     case AV_PIX_FMT_AYUV64BE:
//     case AV_PIX_FMT_PAL8:
    default: return AV_PIX_FMT_NONE;
    }
}

av_cold int ff_sws_init_single_context(SwsContext *sws, SwsFilter *srcFilter,
                                       SwsFilter *dstFilter)
{
    int i;
    int usesVFilter, usesHFilter;
    int unscaled;
    SwsInternal *c        = sws_internal(sws);
    SwsFilter dummyFilter = { NULL, NULL, NULL, NULL };
    int srcW              = sws->src_w;
    int srcH              = sws->src_h;
    int dstW              = sws->dst_w;
    int dstH              = sws->dst_h;
    int dst_stride        = FFALIGN(dstW * sizeof(int16_t) + 66, 16);
    int flags, cpu_flags;
    enum AVPixelFormat srcFormat, dstFormat;
    const AVPixFmtDescriptor *desc_src;
    const AVPixFmtDescriptor *desc_dst;
    int ret = 0;
    enum AVPixelFormat tmpFmt;
    static const float float_mult = 1.0f / 255.0f;

    cpu_flags = av_get_cpu_flags();
    flags     = sws->flags;
    emms_c();

    unscaled = (srcW == dstW && srcH == dstH);

    if (!c->contrast && !c->saturation && !c->dstFormatBpp)
        sws_setColorspaceDetails(sws, ff_yuv2rgb_coeffs[SWS_CS_DEFAULT], sws->src_range,
                                 ff_yuv2rgb_coeffs[SWS_CS_DEFAULT],
                                 sws->dst_range, 0, 1 << 16, 1 << 16);

    ret = handle_formats(sws);
    if (ret < 0)
        return ret;
    srcFormat = sws->src_format;
    dstFormat = sws->dst_format;
    desc_src = av_pix_fmt_desc_get(srcFormat);
    desc_dst = av_pix_fmt_desc_get(dstFormat);

    // If the source has no alpha then disable alpha blendaway
    if (c->src0Alpha)
        sws->alpha_blend = SWS_ALPHA_BLEND_NONE;

    if (!(unscaled && sws_isSupportedEndiannessConversion(srcFormat) &&
          av_pix_fmt_swap_endianness(srcFormat) == dstFormat)) {
        if (!sws_isSupportedInput(srcFormat)) {
            av_log(c, AV_LOG_ERROR, "%s is not supported as input pixel format\n",
                   av_get_pix_fmt_name(srcFormat));
            return AVERROR(EINVAL);
        }
        if (!sws_isSupportedOutput(dstFormat)) {
            av_log(c, AV_LOG_ERROR, "%s is not supported as output pixel format\n",
                   av_get_pix_fmt_name(dstFormat));
            return AVERROR(EINVAL);
        }
    }
    av_assert2(desc_src && desc_dst);

    i = flags & (SWS_POINT         |
                 SWS_AREA          |
                 SWS_BILINEAR      |
                 SWS_FAST_BILINEAR |
                 SWS_BICUBIC       |
                 SWS_X             |
                 SWS_GAUSS         |
                 SWS_LANCZOS       |
                 SWS_SINC          |
                 SWS_SPLINE        |
                 SWS_BICUBLIN);

    /* provide a default scaler if not set by caller */
    if (!i) {
        if (dstW < srcW && dstH < srcH)
            flags |= SWS_BICUBIC;
        else if (dstW > srcW && dstH > srcH)
            flags |= SWS_BICUBIC;
        else
            flags |= SWS_BICUBIC;
        sws->flags = flags;
    } else if (i & (i - 1)) {
        av_log(c, AV_LOG_ERROR,
               "Exactly one scaler algorithm must be chosen, got %X\n", i);
        return AVERROR(EINVAL);
    }
    /* sanity check */
    if (srcW < 1 || srcH < 1 || dstW < 1 || dstH < 1) {
        /* FIXME check if these are enough and try to lower them after
         * fixing the relevant parts of the code */
        av_log(c, AV_LOG_ERROR, "%dx%d -> %dx%d is invalid scaling dimension\n",
               srcW, srcH, dstW, dstH);
        return AVERROR(EINVAL);
    }
    if (flags & SWS_FAST_BILINEAR) {
        if (srcW < 8 || dstW < 8) {
            flags ^= SWS_FAST_BILINEAR | SWS_BILINEAR;
            sws->flags = flags;
        }
    }

    if (!dstFilter)
        dstFilter = &dummyFilter;
    if (!srcFilter)
        srcFilter = &dummyFilter;

    c->lumXInc      = (((int64_t)srcW << 16) + (dstW >> 1)) / dstW;
    c->lumYInc      = (((int64_t)srcH << 16) + (dstH >> 1)) / dstH;
    c->dstFormatBpp = av_get_bits_per_pixel(desc_dst);
    c->srcFormatBpp = av_get_bits_per_pixel(desc_src);
    c->vRounder     = 4 * 0x0001000100010001ULL;

    usesVFilter = (srcFilter->lumV && srcFilter->lumV->length > 1) ||
                  (srcFilter->chrV && srcFilter->chrV->length > 1) ||
                  (dstFilter->lumV && dstFilter->lumV->length > 1) ||
                  (dstFilter->chrV && dstFilter->chrV->length > 1);
    usesHFilter = (srcFilter->lumH && srcFilter->lumH->length > 1) ||
                  (srcFilter->chrH && srcFilter->chrH->length > 1) ||
                  (dstFilter->lumH && dstFilter->lumH->length > 1) ||
                  (dstFilter->chrH && dstFilter->chrH->length > 1);

    av_pix_fmt_get_chroma_sub_sample(srcFormat, &c->chrSrcHSubSample, &c->chrSrcVSubSample);
    av_pix_fmt_get_chroma_sub_sample(dstFormat, &c->chrDstHSubSample, &c->chrDstVSubSample);

    c->dst_slice_align = 1 << c->chrDstVSubSample;

    if (isAnyRGB(dstFormat) && !(flags&SWS_FULL_CHR_H_INT)) {
        if (dstW&1) {
            av_log(c, AV_LOG_DEBUG, "Forcing full internal H chroma due to odd output size\n");
            flags |= SWS_FULL_CHR_H_INT;
            sws->flags = flags;
        }

        if (   c->chrSrcHSubSample == 0
            && c->chrSrcVSubSample == 0
            && sws->dither != SWS_DITHER_BAYER //SWS_FULL_CHR_H_INT is currently not supported with SWS_DITHER_BAYER
            && !(sws->flags & SWS_FAST_BILINEAR)
        ) {
            av_log(c, AV_LOG_DEBUG, "Forcing full internal H chroma due to input having non subsampled chroma\n");
            flags |= SWS_FULL_CHR_H_INT;
            sws->flags = flags;
        }
    }

    if (sws->dither == SWS_DITHER_AUTO) {
        if (flags & SWS_ERROR_DIFFUSION)
            sws->dither = SWS_DITHER_ED;
    }

    if(dstFormat == AV_PIX_FMT_BGR4_BYTE ||
       dstFormat == AV_PIX_FMT_RGB4_BYTE ||
       dstFormat == AV_PIX_FMT_BGR8 ||
       dstFormat == AV_PIX_FMT_RGB8) {
        if (sws->dither == SWS_DITHER_AUTO)
            sws->dither = (flags & SWS_FULL_CHR_H_INT) ? SWS_DITHER_ED : SWS_DITHER_BAYER;
        if (!(flags & SWS_FULL_CHR_H_INT)) {
            if (sws->dither == SWS_DITHER_ED || sws->dither == SWS_DITHER_A_DITHER || sws->dither == SWS_DITHER_X_DITHER || sws->dither == SWS_DITHER_NONE) {
                av_log(c, AV_LOG_DEBUG,
                    "Desired dithering only supported in full chroma interpolation for destination format '%s'\n",
                    av_get_pix_fmt_name(dstFormat));
                flags   |= SWS_FULL_CHR_H_INT;
                sws->flags = flags;
            }
        }
        if (flags & SWS_FULL_CHR_H_INT) {
            if (sws->dither == SWS_DITHER_BAYER) {
                av_log(c, AV_LOG_DEBUG,
                    "Ordered dither is not supported in full chroma interpolation for destination format '%s'\n",
                    av_get_pix_fmt_name(dstFormat));
                sws->dither = SWS_DITHER_ED;
            }
        }
    }
    if (isPlanarRGB(dstFormat)) {
        if (!(flags & SWS_FULL_CHR_H_INT)) {
            av_log(c, AV_LOG_DEBUG,
                   "%s output is not supported with half chroma resolution, switching to full\n",
                   av_get_pix_fmt_name(dstFormat));
            flags   |= SWS_FULL_CHR_H_INT;
            sws->flags = flags;
        }
    }

    /* reuse chroma for 2 pixels RGB/BGR unless user wants full
     * chroma interpolation */
    if (flags & SWS_FULL_CHR_H_INT &&
        isAnyRGB(dstFormat)        &&
        !isPlanarRGB(dstFormat)    &&
        dstFormat != AV_PIX_FMT_RGBA64LE &&
        dstFormat != AV_PIX_FMT_RGBA64BE &&
        dstFormat != AV_PIX_FMT_BGRA64LE &&
        dstFormat != AV_PIX_FMT_BGRA64BE &&
        dstFormat != AV_PIX_FMT_RGB48LE &&
        dstFormat != AV_PIX_FMT_RGB48BE &&
        dstFormat != AV_PIX_FMT_BGR48LE &&
        dstFormat != AV_PIX_FMT_BGR48BE &&
        dstFormat != AV_PIX_FMT_RGBA  &&
        dstFormat != AV_PIX_FMT_ARGB  &&
        dstFormat != AV_PIX_FMT_BGRA  &&
        dstFormat != AV_PIX_FMT_ABGR  &&
        dstFormat != AV_PIX_FMT_RGB24 &&
        dstFormat != AV_PIX_FMT_BGR24 &&
        dstFormat != AV_PIX_FMT_BGR4_BYTE &&
        dstFormat != AV_PIX_FMT_RGB4_BYTE &&
        dstFormat != AV_PIX_FMT_BGR8 &&
        dstFormat != AV_PIX_FMT_RGB8 &&
        dstFormat != AV_PIX_FMT_X2RGB10LE &&
        dstFormat != AV_PIX_FMT_X2BGR10LE
    ) {
        av_log(c, AV_LOG_WARNING,
               "full chroma interpolation for destination format '%s' not yet implemented\n",
               av_get_pix_fmt_name(dstFormat));
        flags   &= ~SWS_FULL_CHR_H_INT;
        sws->flags = flags;
    }
    if (isAnyRGB(dstFormat) && !(flags & SWS_FULL_CHR_H_INT))
        c->chrDstHSubSample = 1;

    // drop some chroma lines if the user wants it
    c->vChrDrop          = (flags & SWS_SRC_V_CHR_DROP_MASK) >>
                           SWS_SRC_V_CHR_DROP_SHIFT;
    c->chrSrcVSubSample += c->vChrDrop;

    /* drop every other pixel for chroma calculation unless user
     * wants full chroma */
    if (isAnyRGB(srcFormat) && !(srcW & 1) && !(flags & SWS_FULL_CHR_H_INP) &&
        srcFormat != AV_PIX_FMT_RGB8 && srcFormat != AV_PIX_FMT_BGR8 &&
        srcFormat != AV_PIX_FMT_RGB4 && srcFormat != AV_PIX_FMT_BGR4 &&
        srcFormat != AV_PIX_FMT_RGB4_BYTE && srcFormat != AV_PIX_FMT_BGR4_BYTE &&
        srcFormat != AV_PIX_FMT_GBRP9BE   && srcFormat != AV_PIX_FMT_GBRP9LE  &&
        srcFormat != AV_PIX_FMT_GBRP10BE  && srcFormat != AV_PIX_FMT_GBRP10LE &&
        srcFormat != AV_PIX_FMT_GBRAP10BE && srcFormat != AV_PIX_FMT_GBRAP10LE &&
        srcFormat != AV_PIX_FMT_GBRP12BE  && srcFormat != AV_PIX_FMT_GBRP12LE &&
        srcFormat != AV_PIX_FMT_GBRAP12BE && srcFormat != AV_PIX_FMT_GBRAP12LE &&
        srcFormat != AV_PIX_FMT_GBRAP14BE && srcFormat != AV_PIX_FMT_GBRAP14LE &&
        srcFormat != AV_PIX_FMT_GBRP14BE  && srcFormat != AV_PIX_FMT_GBRP14LE &&
        srcFormat != AV_PIX_FMT_GBRP16BE  && srcFormat != AV_PIX_FMT_GBRP16LE &&
        srcFormat != AV_PIX_FMT_GBRAP16BE  && srcFormat != AV_PIX_FMT_GBRAP16LE &&
        srcFormat != AV_PIX_FMT_GBRPF32BE  && srcFormat != AV_PIX_FMT_GBRPF32LE &&
        srcFormat != AV_PIX_FMT_GBRAPF32BE && srcFormat != AV_PIX_FMT_GBRAPF32LE &&
        srcFormat != AV_PIX_FMT_GBRPF16BE  && srcFormat != AV_PIX_FMT_GBRPF16LE &&
        srcFormat != AV_PIX_FMT_GBRAPF16BE && srcFormat != AV_PIX_FMT_GBRAPF16LE &&
        ((dstW >> c->chrDstHSubSample) <= (srcW >> 1) ||
         (flags & SWS_FAST_BILINEAR)))
        c->chrSrcHSubSample = 1;

    // Note the AV_CEIL_RSHIFT is so that we always round toward +inf.
    c->chrSrcW = AV_CEIL_RSHIFT(srcW, c->chrSrcHSubSample);
    c->chrSrcH = AV_CEIL_RSHIFT(srcH, c->chrSrcVSubSample);
    c->chrDstW = AV_CEIL_RSHIFT(dstW, c->chrDstHSubSample);
    c->chrDstH = AV_CEIL_RSHIFT(dstH, c->chrDstVSubSample);

    if (!FF_ALLOCZ_TYPED_ARRAY(c->formatConvBuffer, FFALIGN(srcW * 2 + 78, 16) * 2))
        goto nomem;

    c->srcBpc = desc_src->comp[0].depth;
    if (c->srcBpc < 8)
        c->srcBpc = 8;
    c->dstBpc = desc_dst->comp[0].depth;
    if (c->dstBpc < 8)
        c->dstBpc = 8;
    if (isAnyRGB(srcFormat) || srcFormat == AV_PIX_FMT_PAL8)
        c->srcBpc = 16;
    if (c->dstBpc == 16)
        dst_stride <<= 1;

    if (INLINE_MMXEXT(cpu_flags) && c->srcBpc == 8 && c->dstBpc <= 14) {
        c->canMMXEXTBeUsed = dstW >= srcW && (dstW & 31) == 0 &&
                             c->chrDstW >= c->chrSrcW &&
                             (srcW & 15) == 0;
        if (!c->canMMXEXTBeUsed && dstW >= srcW && c->chrDstW >= c->chrSrcW && (srcW & 15) == 0

            && (flags & SWS_FAST_BILINEAR)) {
            if (flags & SWS_PRINT_INFO)
                av_log(c, AV_LOG_INFO,
                       "output width is not a multiple of 32 -> no MMXEXT scaler\n");
        }
        if (usesHFilter || isNBPS(sws->src_format) || is16BPS(sws->src_format) || isAnyRGB(sws->src_format))
            c->canMMXEXTBeUsed = 0;
    } else
        c->canMMXEXTBeUsed = 0;

    c->chrXInc = (((int64_t)c->chrSrcW << 16) + (c->chrDstW >> 1)) / c->chrDstW;
    c->chrYInc = (((int64_t)c->chrSrcH << 16) + (c->chrDstH >> 1)) / c->chrDstH;

    /* Match pixel 0 of the src to pixel 0 of dst and match pixel n-2 of src
     * to pixel n-2 of dst, but only for the FAST_BILINEAR mode otherwise do
     * correct scaling.
     * n-2 is the last chrominance sample available.
     * This is not perfect, but no one should notice the difference, the more
     * correct variant would be like the vertical one, but that would require
     * some special code for the first and last pixel */
    if (flags & SWS_FAST_BILINEAR) {
        if (c->canMMXEXTBeUsed) {
            c->lumXInc += 20;
            c->chrXInc += 20;
        }
        // we don't use the x86 asm scaler if MMX is available
        else if (INLINE_MMX(cpu_flags) && c->dstBpc <= 14) {
            c->lumXInc = ((int64_t)(srcW       - 2) << 16) / (dstW       - 2) - 20;
            c->chrXInc = ((int64_t)(c->chrSrcW - 2) << 16) / (c->chrDstW - 2) - 20;
        }
    }

    // hardcoded for now
    c->gamma_value = 2.2;
    tmpFmt = AV_PIX_FMT_RGBA64LE;

    if (!unscaled && sws->gamma_flag && (srcFormat != tmpFmt || dstFormat != tmpFmt)) {
        SwsInternal *c2;
        c->cascaded_context[0] = NULL;

        ret = av_image_alloc(c->cascaded_tmp[0], c->cascaded_tmpStride[0],
                            srcW, srcH, tmpFmt, 64);
        if (ret < 0)
            return ret;

        c->cascaded_context[0] = sws_getContext(srcW, srcH, srcFormat,
                                                srcW, srcH, tmpFmt,
                                                flags, NULL, NULL,
                                                sws->scaler_params);
        if (!c->cascaded_context[0]) {
            return AVERROR(ENOMEM);
        }

        c->cascaded_context[1] = sws_getContext(srcW, srcH, tmpFmt,
                                                dstW, dstH, tmpFmt,
                                                flags, srcFilter, dstFilter,
                                                sws->scaler_params);

        if (!c->cascaded_context[1])
            return AVERROR(ENOMEM);

        c2 = sws_internal(c->cascaded_context[1]);
        c2->is_internal_gamma = 1;
        c2->gamma     = alloc_gamma_tbl(    c->gamma_value);
        c2->inv_gamma = alloc_gamma_tbl(1.f/c->gamma_value);
        if (!c2->gamma || !c2->inv_gamma)
            return AVERROR(ENOMEM);

        // is_internal_flag is set after creating the context
        // to properly create the gamma convert FilterDescriptor
        // we have to re-initialize it
        ff_free_filters(c2);
        if ((ret = ff_init_filters(c2)) < 0) {
            sws_freeContext(c->cascaded_context[1]);
            c->cascaded_context[1] = NULL;
            return ret;
        }

        c->cascaded_context[2] = NULL;
        if (dstFormat != tmpFmt) {
            ret = av_image_alloc(c->cascaded_tmp[1], c->cascaded_tmpStride[1],
                                dstW, dstH, tmpFmt, 64);
            if (ret < 0)
                return ret;

            c->cascaded_context[2] = sws_getContext(dstW, dstH, tmpFmt,
                                                    dstW, dstH, dstFormat,
                                                    flags, NULL, NULL,
                                                    sws->scaler_params);
            if (!c->cascaded_context[2])
                return AVERROR(ENOMEM);
        }
        return 0;
    }

    if (isBayer(srcFormat)) {
        if (!unscaled ||
            (dstFormat != AV_PIX_FMT_RGB24 && dstFormat != AV_PIX_FMT_YUV420P &&
             dstFormat != AV_PIX_FMT_RGB48)) {
            enum AVPixelFormat tmpFormat = isBayer16BPS(srcFormat) ? AV_PIX_FMT_RGB48 : AV_PIX_FMT_RGB24;

            ret = av_image_alloc(c->cascaded_tmp[0], c->cascaded_tmpStride[0],
                                srcW, srcH, tmpFormat, 64);
            if (ret < 0)
                return ret;

            c->cascaded_context[0] = sws_getContext(srcW, srcH, srcFormat,
                                                    srcW, srcH, tmpFormat,
                                                    flags, srcFilter, NULL,
                                                    sws->scaler_params);
            if (!c->cascaded_context[0])
                return AVERROR(ENOMEM);

            c->cascaded_context[1] = sws_getContext(srcW, srcH, tmpFormat,
                                                    dstW, dstH, dstFormat,
                                                    flags, NULL, dstFilter,
                                                    sws->scaler_params);
            if (!c->cascaded_context[1])
                return AVERROR(ENOMEM);
            return 0;
        }
    }

    if (unscaled && c->srcBpc == 8 && dstFormat == AV_PIX_FMT_GRAYF32){
        for (i = 0; i < 256; ++i){
            c->uint2float_lut[i] = (float)i * float_mult;
        }
    }

    // float will be converted to uint16_t
    if ((srcFormat == AV_PIX_FMT_GRAYF32BE || srcFormat == AV_PIX_FMT_GRAYF32LE) &&
        (!unscaled || unscaled && dstFormat != srcFormat && (srcFormat != AV_PIX_FMT_GRAYF32 ||
        dstFormat != AV_PIX_FMT_GRAY8))){
        c->srcBpc = 16;
    }

    if (CONFIG_SWSCALE_ALPHA && isALPHA(srcFormat) && !isALPHA(dstFormat)) {
        enum AVPixelFormat tmpFormat = alphaless_fmt(srcFormat);

        if (tmpFormat != AV_PIX_FMT_NONE && sws->alpha_blend != SWS_ALPHA_BLEND_NONE) {
            if (!unscaled ||
                dstFormat != tmpFormat ||
                usesHFilter || usesVFilter ||
                sws->src_range != sws->dst_range
            ) {
                c->cascaded_mainindex = 1;
                ret = av_image_alloc(c->cascaded_tmp[0], c->cascaded_tmpStride[0],
                                     srcW, srcH, tmpFormat, 64);
                if (ret < 0)
                    return ret;

                c->cascaded_context[0] = alloc_set_opts(srcW, srcH, srcFormat,
                                                        srcW, srcH, tmpFormat,
                                                        flags, sws->scaler_params);
                if (!c->cascaded_context[0])
                    return AVERROR(EINVAL);
                c->cascaded_context[0]->alpha_blend = sws->alpha_blend;
                ret = sws_init_context(c->cascaded_context[0], NULL , NULL);
                if (ret < 0)
                    return ret;

                c->cascaded_context[1] = alloc_set_opts(srcW, srcH, tmpFormat,
                                                        dstW, dstH, dstFormat,
                                                        flags, sws->scaler_params);
                if (!c->cascaded_context[1])
                    return AVERROR(EINVAL);

                c->cascaded_context[1]->src_range = sws->src_range;
                c->cascaded_context[1]->dst_range = sws->dst_range;
                ret = sws_init_context(c->cascaded_context[1], srcFilter , dstFilter);
                if (ret < 0)
                    return ret;

                return 0;
            }
        }
    }

    /* alpha blend special case, note this has been split via cascaded contexts if its scaled */
    if (unscaled && !usesHFilter && !usesVFilter &&
        sws->alpha_blend != SWS_ALPHA_BLEND_NONE &&
        isALPHA(srcFormat) &&
        (sws->src_range == sws->dst_range || isAnyRGB(dstFormat)) &&
        alphaless_fmt(srcFormat) == dstFormat
    ) {
        c->convert_unscaled = ff_sws_alphablendaway;

        if (flags & SWS_PRINT_INFO)
            av_log(c, AV_LOG_INFO,
                    "using alpha blendaway %s -> %s special converter\n",
                    av_get_pix_fmt_name(srcFormat), av_get_pix_fmt_name(dstFormat));
        return 0;
    }

    /* unscaled special cases */
    if (unscaled && !usesHFilter && !usesVFilter &&
        (sws->src_range == sws->dst_range || isAnyRGB(dstFormat) ||
         isFloat(srcFormat) || isFloat(dstFormat) || isBayer(srcFormat))){

        ff_get_unscaled_swscale(c);

        if (c->convert_unscaled) {
            if (flags & SWS_PRINT_INFO)
                av_log(c, AV_LOG_INFO,
                       "using unscaled %s -> %s special converter\n",
                       av_get_pix_fmt_name(srcFormat), av_get_pix_fmt_name(dstFormat));
            return 0;
        }
    }

#if HAVE_MMAP && HAVE_MPROTECT && defined(MAP_ANONYMOUS)
#define USE_MMAP 1
#else
#define USE_MMAP 0
#endif

    /* precalculate horizontal scaler filter coefficients */
    {
#if HAVE_MMXEXT_INLINE
// can't downscale !!!
        if (c->canMMXEXTBeUsed && (flags & SWS_FAST_BILINEAR)) {
            c->lumMmxextFilterCodeSize = ff_init_hscaler_mmxext(dstW, c->lumXInc, NULL,
                                                             NULL, NULL, 8);
            c->chrMmxextFilterCodeSize = ff_init_hscaler_mmxext(c->chrDstW, c->chrXInc,
                                                             NULL, NULL, NULL, 4);

#if USE_MMAP
            c->lumMmxextFilterCode = mmap(NULL, c->lumMmxextFilterCodeSize,
                                          PROT_READ | PROT_WRITE,
                                          MAP_PRIVATE | MAP_ANONYMOUS,
                                          -1, 0);
            c->chrMmxextFilterCode = mmap(NULL, c->chrMmxextFilterCodeSize,
                                          PROT_READ | PROT_WRITE,
                                          MAP_PRIVATE | MAP_ANONYMOUS,
                                          -1, 0);
#elif HAVE_VIRTUALALLOC
            c->lumMmxextFilterCode = VirtualAlloc(NULL,
                                                  c->lumMmxextFilterCodeSize,
                                                  MEM_COMMIT,
                                                  PAGE_EXECUTE_READWRITE);
            c->chrMmxextFilterCode = VirtualAlloc(NULL,
                                                  c->chrMmxextFilterCodeSize,
                                                  MEM_COMMIT,
                                                  PAGE_EXECUTE_READWRITE);
#else
            c->lumMmxextFilterCode = av_malloc(c->lumMmxextFilterCodeSize);
            c->chrMmxextFilterCode = av_malloc(c->chrMmxextFilterCodeSize);
#endif

#ifdef MAP_ANONYMOUS
            if (c->lumMmxextFilterCode == MAP_FAILED || c->chrMmxextFilterCode == MAP_FAILED)
#else
            if (!c->lumMmxextFilterCode || !c->chrMmxextFilterCode)
#endif
            {
                av_log(c, AV_LOG_ERROR, "Failed to allocate MMX2FilterCode\n");
                return AVERROR(ENOMEM);
            }

            if (!FF_ALLOCZ_TYPED_ARRAY(c->hLumFilter,    dstW           / 8 + 8) ||
                !FF_ALLOCZ_TYPED_ARRAY(c->hChrFilter,    c->chrDstW     / 4 + 8) ||
                !FF_ALLOCZ_TYPED_ARRAY(c->hLumFilterPos, dstW       / 2 / 8 + 8) ||
                !FF_ALLOCZ_TYPED_ARRAY(c->hChrFilterPos, c->chrDstW / 2 / 4 + 8))
                goto nomem;

            ff_init_hscaler_mmxext(      dstW, c->lumXInc, c->lumMmxextFilterCode,
                                c->hLumFilter, (uint32_t*)c->hLumFilterPos, 8);
            ff_init_hscaler_mmxext(c->chrDstW, c->chrXInc, c->chrMmxextFilterCode,
                                c->hChrFilter, (uint32_t*)c->hChrFilterPos, 4);

#if USE_MMAP
            if (   mprotect(c->lumMmxextFilterCode, c->lumMmxextFilterCodeSize, PROT_EXEC | PROT_READ) == -1
                || mprotect(c->chrMmxextFilterCode, c->chrMmxextFilterCodeSize, PROT_EXEC | PROT_READ) == -1) {
                av_log(c, AV_LOG_ERROR, "mprotect failed, cannot use fast bilinear scaler\n");
                ret = AVERROR(EINVAL);
                goto fail;
            }
#endif
        } else
#endif /* HAVE_MMXEXT_INLINE */
        {
            const int filterAlign = X86_MMX(cpu_flags)     ? 4 :
                                    PPC_ALTIVEC(cpu_flags) ? 8 :
                                    have_neon(cpu_flags)   ? 4 :
                                    have_lsx(cpu_flags)    ? 8 :
                                    have_lasx(cpu_flags)   ? 8 : 1;

            if ((ret = initFilter(&c->hLumFilter, &c->hLumFilterPos,
                           &c->hLumFilterSize, c->lumXInc,
                           srcW, dstW, filterAlign, 1 << 14,
                           (flags & SWS_BICUBLIN) ? (flags | SWS_BICUBIC) : flags,
                           cpu_flags, srcFilter->lumH, dstFilter->lumH,
                           sws->scaler_params,
                           get_local_pos(c, 0, 0, 0),
                           get_local_pos(c, 0, 0, 0))) < 0)
                goto fail;
            if (ff_shuffle_filter_coefficients(c, c->hLumFilterPos, c->hLumFilterSize, c->hLumFilter, dstW) < 0)
                goto nomem;
            if ((ret = initFilter(&c->hChrFilter, &c->hChrFilterPos,
                           &c->hChrFilterSize, c->chrXInc,
                           c->chrSrcW, c->chrDstW, filterAlign, 1 << 14,
                           (flags & SWS_BICUBLIN) ? (flags | SWS_BILINEAR) : flags,
                           cpu_flags, srcFilter->chrH, dstFilter->chrH,
                           sws->scaler_params,
                           get_local_pos(c, c->chrSrcHSubSample, sws->src_h_chr_pos, 0),
                           get_local_pos(c, c->chrDstHSubSample, sws->dst_h_chr_pos, 0))) < 0)
                goto fail;
            if (ff_shuffle_filter_coefficients(c, c->hChrFilterPos, c->hChrFilterSize, c->hChrFilter, c->chrDstW) < 0)
                goto nomem;
        }
    } // initialize horizontal stuff

    /* precalculate vertical scaler filter coefficients */
    {
        const int filterAlign = X86_MMX(cpu_flags)     ? 2 :
                                PPC_ALTIVEC(cpu_flags) ? 8 :
                                have_neon(cpu_flags)   ? 2 : 1;

        if ((ret = initFilter(&c->vLumFilter, &c->vLumFilterPos, &c->vLumFilterSize,
                       c->lumYInc, srcH, dstH, filterAlign, (1 << 12),
                       (flags & SWS_BICUBLIN) ? (flags | SWS_BICUBIC) : flags,
                       cpu_flags, srcFilter->lumV, dstFilter->lumV,
                       sws->scaler_params,
                       get_local_pos(c, 0, 0, 1),
                       get_local_pos(c, 0, 0, 1))) < 0)
            goto fail;
        if ((ret = initFilter(&c->vChrFilter, &c->vChrFilterPos, &c->vChrFilterSize,
                       c->chrYInc, c->chrSrcH, c->chrDstH,
                       filterAlign, (1 << 12),
                       (flags & SWS_BICUBLIN) ? (flags | SWS_BILINEAR) : flags,
                       cpu_flags, srcFilter->chrV, dstFilter->chrV,
                       sws->scaler_params,
                       get_local_pos(c, c->chrSrcVSubSample, sws->src_v_chr_pos, 1),
                       get_local_pos(c, c->chrDstVSubSample, sws->dst_v_chr_pos, 1))) < 0)

            goto fail;

#if HAVE_ALTIVEC
        if (!FF_ALLOC_TYPED_ARRAY(c->vYCoeffsBank, c->vLumFilterSize * sws->dst_h) ||
            !FF_ALLOC_TYPED_ARRAY(c->vCCoeffsBank, c->vChrFilterSize * c->chrDstH))
            goto nomem;

        for (i = 0; i < c->vLumFilterSize * sws->dst_h; i++) {
            int j;
            short *p = (short *)&c->vYCoeffsBank[i];
            for (j = 0; j < 8; j++)
                p[j] = c->vLumFilter[i];
        }

        for (i = 0; i < c->vChrFilterSize * c->chrDstH; i++) {
            int j;
            short *p = (short *)&c->vCCoeffsBank[i];
            for (j = 0; j < 8; j++)
                p[j] = c->vChrFilter[i];
        }
#endif
    }

    for (i = 0; i < 4; i++)
        if (!FF_ALLOCZ_TYPED_ARRAY(c->dither_error[i], sws->dst_w + 3))
            goto nomem;

    c->needAlpha = (CONFIG_SWSCALE_ALPHA && isALPHA(sws->src_format) && isALPHA(sws->dst_format)) ? 1 : 0;

    // 64 / c->scalingBpp is the same as 16 / sizeof(scaling_intermediate)
    c->uv_off   = (dst_stride>>1) + 64 / (c->dstBpc &~ 7);
    c->uv_offx2 = dst_stride + 16;

    av_assert0(c->chrDstH <= dstH);

    if (flags & SWS_PRINT_INFO) {
        const char *scaler = NULL, *cpucaps;

        for (i = 0; i < FF_ARRAY_ELEMS(scale_algorithms); i++) {
            if (flags & scale_algorithms[i].flag) {
                scaler = scale_algorithms[i].description;
                break;
            }
        }
        if (!scaler)
            scaler =  "ehh flags invalid?!";
        av_log(c, AV_LOG_INFO, "%s scaler, from %s to %s%s ",
               scaler,
               av_get_pix_fmt_name(srcFormat),
               dstFormat == AV_PIX_FMT_BGR555   || dstFormat == AV_PIX_FMT_BGR565   ||
               dstFormat == AV_PIX_FMT_RGB444BE || dstFormat == AV_PIX_FMT_RGB444LE ||
               dstFormat == AV_PIX_FMT_BGR444BE || dstFormat == AV_PIX_FMT_BGR444LE ?
                                                             "dithered " : "",
               av_get_pix_fmt_name(dstFormat));

        if (INLINE_MMXEXT(cpu_flags))
            cpucaps = "MMXEXT";
        else if (INLINE_MMX(cpu_flags))
            cpucaps = "MMX";
        else if (PPC_ALTIVEC(cpu_flags))
            cpucaps = "AltiVec";
        else
            cpucaps = "C";

        av_log(c, AV_LOG_INFO, "using %s\n", cpucaps);

        av_log(c, AV_LOG_VERBOSE, "%dx%d -> %dx%d\n", srcW, srcH, dstW, dstH);
        av_log(c, AV_LOG_DEBUG,
               "lum srcW=%d srcH=%d dstW=%d dstH=%d xInc=%d yInc=%d\n",
               sws->src_w, sws->src_h, sws->dst_w, sws->dst_h, c->lumXInc, c->lumYInc);
        av_log(c, AV_LOG_DEBUG,
               "chr srcW=%d srcH=%d dstW=%d dstH=%d xInc=%d yInc=%d\n",
               c->chrSrcW, c->chrSrcH, c->chrDstW, c->chrDstH,
               c->chrXInc, c->chrYInc);
    }

    ff_sws_init_scale(c);

    return ff_init_filters(c);
nomem:
    ret = AVERROR(ENOMEM);
fail: // FIXME replace things by appropriate error codes
    if (ret == RETCODE_USE_CASCADE)  {
        int tmpW = sqrt(srcW * (int64_t)dstW);
        int tmpH = sqrt(srcH * (int64_t)dstH);
        enum AVPixelFormat tmpFormat = AV_PIX_FMT_YUV420P;

        if (isALPHA(srcFormat))
            tmpFormat = AV_PIX_FMT_YUVA420P;

        if (srcW*(int64_t)srcH <= 4LL*dstW*dstH)
            return AVERROR(EINVAL);

        ret = av_image_alloc(c->cascaded_tmp[0], c->cascaded_tmpStride[0],
                             tmpW, tmpH, tmpFormat, 64);
        if (ret < 0)
            return ret;

        c->cascaded_context[0] = sws_getContext(srcW, srcH, srcFormat,
                                                tmpW, tmpH, tmpFormat,
                                                flags, srcFilter, NULL,
                                                sws->scaler_params);
        if (!c->cascaded_context[0])
            return AVERROR(ENOMEM);

        c->cascaded_context[1] = sws_getContext(tmpW, tmpH, tmpFormat,
                                                dstW, dstH, dstFormat,
                                                flags, NULL, dstFilter,
                                                sws->scaler_params);
        if (!c->cascaded_context[1])
            return AVERROR(ENOMEM);
        return 0;
    }
    return ret;
}

static int context_init_threaded(SwsContext *sws,
                                 SwsFilter *src_filter, SwsFilter *dst_filter)
{
    SwsInternal *c = sws_internal(sws);
    int ret;

    ret = avpriv_slicethread_create(&c->slicethread, (void*) sws,
                                    ff_sws_slice_worker, NULL, sws->threads);
    if (ret == AVERROR(ENOSYS)) {
        sws->threads = 1;
        return 0;
    } else if (ret < 0)
        return ret;

    sws->threads = ret;

    c->slice_ctx = av_calloc(sws->threads, sizeof(*c->slice_ctx));
    c->slice_err = av_calloc(sws->threads, sizeof(*c->slice_err));
    if (!c->slice_ctx || !c->slice_err)
        return AVERROR(ENOMEM);

    for (int i = 0; i < sws->threads; i++) {
        SwsContext *slice;
        slice = c->slice_ctx[i] = sws_alloc_context();
        if (!slice)
            return AVERROR(ENOMEM);
        sws_internal(slice)->parent = sws;
        c->nb_slice_ctx++;

        ret = av_opt_copy(slice, sws);
        if (ret < 0)
            return ret;
        slice->threads = 1;

        ret = ff_sws_init_single_context(slice, src_filter, dst_filter);
        if (ret < 0)
            return ret;

        if (slice->dither == SWS_DITHER_ED) {
            av_log(c, AV_LOG_VERBOSE,
                   "Error-diffusion dither is in use, scaling will be single-threaded.");
            break;
        }
    }

    return 0;
}

av_cold int sws_init_context(SwsContext *sws, SwsFilter *srcFilter,
                             SwsFilter *dstFilter)
{
    SwsInternal *c = sws_internal(sws);
    static AVOnce rgb2rgb_once = AV_ONCE_INIT;
    enum AVPixelFormat src_format, dst_format;
    int ret;

    c->frame_src = av_frame_alloc();
    c->frame_dst = av_frame_alloc();
    if (!c->frame_src || !c->frame_dst)
        return AVERROR(ENOMEM);

    if (ff_thread_once(&rgb2rgb_once, ff_sws_rgb2rgb_init) != 0)
        return AVERROR_UNKNOWN;

    src_format = sws->src_format;
    dst_format = sws->dst_format;
    sws->src_range |= handle_jpeg(&sws->src_format);
    sws->dst_range |= handle_jpeg(&sws->dst_format);

    if (src_format != sws->src_format || dst_format != sws->dst_format)
        av_log(c, AV_LOG_WARNING, "deprecated pixel format used, make sure you did set range correctly\n");

    if (sws->threads != 1) {
        ret = context_init_threaded(sws, srcFilter, dstFilter);
        if (ret < 0 || sws->threads > 1)
            return ret;
        // threading disabled in this build, init as single-threaded
    }

    return ff_sws_init_single_context(sws, srcFilter, dstFilter);
}

SwsContext *sws_getContext(int srcW, int srcH, enum AVPixelFormat srcFormat,
                           int dstW, int dstH, enum AVPixelFormat dstFormat,
                           int flags, SwsFilter *srcFilter,
                           SwsFilter *dstFilter, const double *param)
{
    SwsContext *sws;

    sws = alloc_set_opts(srcW, srcH, srcFormat,
                         dstW, dstH, dstFormat,
                         flags, param);
    if (!sws)
        return NULL;

    if (sws_init_context(sws, srcFilter, dstFilter) < 0) {
        sws_freeContext(sws);
        return NULL;
    }

    return sws;
}

static int isnan_vec(SwsVector *a)
{
    int i;
    for (i=0; i<a->length; i++)
        if (isnan(a->coeff[i]))
            return 1;
    return 0;
}

static void makenan_vec(SwsVector *a)
{
    int i;
    for (i=0; i<a->length; i++)
        a->coeff[i] = NAN;
}

SwsVector *sws_allocVec(int length)
{
    SwsVector *vec;

    if(length <= 0 || length > INT_MAX/ sizeof(double))
        return NULL;

    vec = av_malloc(sizeof(SwsVector));
    if (!vec)
        return NULL;
    vec->length = length;
    vec->coeff  = av_malloc(sizeof(double) * length);
    if (!vec->coeff)
        av_freep(&vec);
    return vec;
}

SwsVector *sws_getGaussianVec(double variance, double quality)
{
    const int length = (int)(variance * quality + 0.5) | 1;
    int i;
    double middle  = (length - 1) * 0.5;
    SwsVector *vec;

    if(variance < 0 || quality < 0)
        return NULL;

    vec = sws_allocVec(length);

    if (!vec)
        return NULL;

    for (i = 0; i < length; i++) {
        double dist = i - middle;
        vec->coeff[i] = exp(-dist * dist / (2 * variance * variance)) /
                        sqrt(2 * variance * M_PI);
    }

    sws_normalizeVec(vec, 1.0);

    return vec;
}

/**
 * Allocate and return a vector with length coefficients, all
 * with the same value c.
 */
static
SwsVector *sws_getConstVec(double c, int length)
{
    int i;
    SwsVector *vec = sws_allocVec(length);

    if (!vec)
        return NULL;

    for (i = 0; i < length; i++)
        vec->coeff[i] = c;

    return vec;
}

/**
 * Allocate and return a vector with just one coefficient, with
 * value 1.0.
 */
static
SwsVector *sws_getIdentityVec(void)
{
    return sws_getConstVec(1.0, 1);
}

static double sws_dcVec(SwsVector *a)
{
    int i;
    double sum = 0;

    for (i = 0; i < a->length; i++)
        sum += a->coeff[i];

    return sum;
}

void sws_scaleVec(SwsVector *a, double scalar)
{
    int i;

    for (i = 0; i < a->length; i++)
        a->coeff[i] *= scalar;
}

void sws_normalizeVec(SwsVector *a, double height)
{
    sws_scaleVec(a, height / sws_dcVec(a));
}

static SwsVector *sws_sumVec(SwsVector *a, SwsVector *b)
{
    int length = FFMAX(a->length, b->length);
    int i;
    SwsVector *vec = sws_getConstVec(0.0, length);

    if (!vec)
        return NULL;

    for (i = 0; i < a->length; i++)
        vec->coeff[i + (length - 1) / 2 - (a->length - 1) / 2] += a->coeff[i];
    for (i = 0; i < b->length; i++)
        vec->coeff[i + (length - 1) / 2 - (b->length - 1) / 2] += b->coeff[i];

    return vec;
}

/* shift left / or right if "shift" is negative */
static SwsVector *sws_getShiftedVec(SwsVector *a, int shift)
{
    int length = a->length + FFABS(shift) * 2;
    int i;
    SwsVector *vec = sws_getConstVec(0.0, length);

    if (!vec)
        return NULL;

    for (i = 0; i < a->length; i++) {
        vec->coeff[i + (length    - 1) / 2 -
                       (a->length - 1) / 2 - shift] = a->coeff[i];
    }

    return vec;
}

static
void sws_shiftVec(SwsVector *a, int shift)
{
    SwsVector *shifted = sws_getShiftedVec(a, shift);
    if (!shifted) {
        makenan_vec(a);
        return;
    }
    av_free(a->coeff);
    a->coeff  = shifted->coeff;
    a->length = shifted->length;
    av_free(shifted);
}

static
void sws_addVec(SwsVector *a, SwsVector *b)
{
    SwsVector *sum = sws_sumVec(a, b);
    if (!sum) {
        makenan_vec(a);
        return;
    }
    av_free(a->coeff);
    a->coeff  = sum->coeff;
    a->length = sum->length;
    av_free(sum);
}

/**
 * Print with av_log() a textual representation of the vector a
 * if log_level <= av_log_level.
 */
static
void sws_printVec2(SwsVector *a, AVClass *log_ctx, int log_level)
{
    int i;
    double max = 0;
    double min = 0;
    double range;

    for (i = 0; i < a->length; i++)
        if (a->coeff[i] > max)
            max = a->coeff[i];

    for (i = 0; i < a->length; i++)
        if (a->coeff[i] < min)
            min = a->coeff[i];

    range = max - min;

    for (i = 0; i < a->length; i++) {
        int x = (int)((a->coeff[i] - min) * 60.0 / range + 0.5);
        av_log(log_ctx, log_level, "%1.3f ", a->coeff[i]);
        for (; x > 0; x--)
            av_log(log_ctx, log_level, " ");
        av_log(log_ctx, log_level, "|\n");
    }
}

void sws_freeVec(SwsVector *a)
{
    if (!a)
        return;
    av_freep(&a->coeff);
    a->length = 0;
    av_free(a);
}

void sws_freeFilter(SwsFilter *filter)
{
    if (!filter)
        return;

    sws_freeVec(filter->lumH);
    sws_freeVec(filter->lumV);
    sws_freeVec(filter->chrH);
    sws_freeVec(filter->chrV);
    av_free(filter);
}

SwsFilter *sws_getDefaultFilter(float lumaGBlur, float chromaGBlur,
                                float lumaSharpen, float chromaSharpen,
                                float chromaHShift, float chromaVShift,
                                int verbose)
{
    SwsFilter *filter = av_malloc(sizeof(SwsFilter));
    if (!filter)
        return NULL;

    if (lumaGBlur != 0.0) {
        filter->lumH = sws_getGaussianVec(lumaGBlur, 3.0);
        filter->lumV = sws_getGaussianVec(lumaGBlur, 3.0);
    } else {
        filter->lumH = sws_getIdentityVec();
        filter->lumV = sws_getIdentityVec();
    }

    if (chromaGBlur != 0.0) {
        filter->chrH = sws_getGaussianVec(chromaGBlur, 3.0);
        filter->chrV = sws_getGaussianVec(chromaGBlur, 3.0);
    } else {
        filter->chrH = sws_getIdentityVec();
        filter->chrV = sws_getIdentityVec();
    }

    if (!filter->lumH || !filter->lumV || !filter->chrH || !filter->chrV)
        goto fail;

    if (chromaSharpen != 0.0) {
        SwsVector *id = sws_getIdentityVec();
        if (!id)
            goto fail;
        sws_scaleVec(filter->chrH, -chromaSharpen);
        sws_scaleVec(filter->chrV, -chromaSharpen);
        sws_addVec(filter->chrH, id);
        sws_addVec(filter->chrV, id);
        sws_freeVec(id);
    }

    if (lumaSharpen != 0.0) {
        SwsVector *id = sws_getIdentityVec();
        if (!id)
            goto fail;
        sws_scaleVec(filter->lumH, -lumaSharpen);
        sws_scaleVec(filter->lumV, -lumaSharpen);
        sws_addVec(filter->lumH, id);
        sws_addVec(filter->lumV, id);
        sws_freeVec(id);
    }

    if (chromaHShift != 0.0)
        sws_shiftVec(filter->chrH, (int)(chromaHShift + 0.5));

    if (chromaVShift != 0.0)
        sws_shiftVec(filter->chrV, (int)(chromaVShift + 0.5));

    sws_normalizeVec(filter->chrH, 1.0);
    sws_normalizeVec(filter->chrV, 1.0);
    sws_normalizeVec(filter->lumH, 1.0);
    sws_normalizeVec(filter->lumV, 1.0);

    if (isnan_vec(filter->chrH) ||
        isnan_vec(filter->chrV) ||
        isnan_vec(filter->lumH) ||
        isnan_vec(filter->lumV))
        goto fail;

    if (verbose)
        sws_printVec2(filter->chrH, NULL, AV_LOG_DEBUG);
    if (verbose)
        sws_printVec2(filter->lumH, NULL, AV_LOG_DEBUG);

    return filter;

fail:
    sws_freeVec(filter->lumH);
    sws_freeVec(filter->lumV);
    sws_freeVec(filter->chrH);
    sws_freeVec(filter->chrV);
    av_freep(&filter);
    return NULL;
}

void sws_freeContext(SwsContext *sws)
{
    SwsInternal *c = sws_internal(sws);
    int i;
    if (!c)
        return;

    for (i = 0; i < FF_ARRAY_ELEMS(c->graph); i++)
        ff_sws_graph_free(&c->graph[i]);

    for (i = 0; i < c->nb_slice_ctx; i++)
        sws_freeContext(c->slice_ctx[i]);
    av_freep(&c->slice_ctx);
    av_freep(&c->slice_err);

    avpriv_slicethread_free(&c->slicethread);

    for (i = 0; i < 4; i++)
        av_freep(&c->dither_error[i]);

    av_frame_free(&c->frame_src);
    av_frame_free(&c->frame_dst);

    av_freep(&c->src_ranges.ranges);

    av_freep(&c->vLumFilter);
    av_freep(&c->vChrFilter);
    av_freep(&c->hLumFilter);
    av_freep(&c->hChrFilter);
#if HAVE_ALTIVEC
    av_freep(&c->vYCoeffsBank);
    av_freep(&c->vCCoeffsBank);
#endif

    av_freep(&c->vLumFilterPos);
    av_freep(&c->vChrFilterPos);
    av_freep(&c->hLumFilterPos);
    av_freep(&c->hChrFilterPos);

#if HAVE_MMX_INLINE
#if USE_MMAP
    if (c->lumMmxextFilterCode)
        munmap(c->lumMmxextFilterCode, c->lumMmxextFilterCodeSize);
    if (c->chrMmxextFilterCode)
        munmap(c->chrMmxextFilterCode, c->chrMmxextFilterCodeSize);
#elif HAVE_VIRTUALALLOC
    if (c->lumMmxextFilterCode)
        VirtualFree(c->lumMmxextFilterCode, 0, MEM_RELEASE);
    if (c->chrMmxextFilterCode)
        VirtualFree(c->chrMmxextFilterCode, 0, MEM_RELEASE);
#else
    av_free(c->lumMmxextFilterCode);
    av_free(c->chrMmxextFilterCode);
#endif
    c->lumMmxextFilterCode = NULL;
    c->chrMmxextFilterCode = NULL;
#endif /* HAVE_MMX_INLINE */

    av_freep(&c->yuvTable);
    av_freep(&c->formatConvBuffer);

    sws_freeContext(c->cascaded_context[0]);
    sws_freeContext(c->cascaded_context[1]);
    sws_freeContext(c->cascaded_context[2]);
    memset(c->cascaded_context, 0, sizeof(c->cascaded_context));
    av_freep(&c->cascaded_tmp[0][0]);
    av_freep(&c->cascaded_tmp[1][0]);

    av_freep(&c->gamma);
    av_freep(&c->inv_gamma);
#if CONFIG_SMALL
    av_freep(&c->xyzgamma);
#endif

    av_freep(&c->rgb0_scratch);
    av_freep(&c->xyz_scratch);

    ff_free_filters(c);

    av_free(c);
}

void sws_free_context(SwsContext **pctx)
{
    SwsContext *ctx = *pctx;
    if (!ctx)
        return;

    sws_freeContext(ctx);
    *pctx = NULL;
}

SwsContext *sws_getCachedContext(SwsContext *prev, int srcW,
                                 int srcH, enum AVPixelFormat srcFormat,
                                 int dstW, int dstH,
                                 enum AVPixelFormat dstFormat, int flags,
                                 SwsFilter *srcFilter,
                                 SwsFilter *dstFilter,
                                 const double *param)
{
    SwsContext *sws;
    static const double default_param[2] = { SWS_PARAM_DEFAULT,
                                             SWS_PARAM_DEFAULT };

    if (!param)
        param = default_param;

    if (prev && (prev->src_w            == srcW      &&
                 prev->src_h            == srcH      &&
                 prev->src_format       == srcFormat &&
                 prev->dst_w            == dstW      &&
                 prev->dst_h            == dstH      &&
                 prev->dst_format       == dstFormat &&
                 prev->flags            == flags     &&
                 prev->scaler_params[0] == param[0]  &&
                 prev->scaler_params[1] == param[1])) {
        return prev;
    }

    if (!(sws = sws_alloc_context())) {
        sws_free_context(&prev);
        return NULL;
    }

    if (prev) {
        av_opt_copy(sws, prev);
        sws_free_context(&prev);
    }

    sws->src_w            = srcW;
    sws->src_h            = srcH;
    sws->src_format       = srcFormat;
    sws->dst_w            = dstW;
    sws->dst_h            = dstH;
    sws->dst_format       = dstFormat;
    sws->flags            = flags;
    sws->scaler_params[0] = param[0];
    sws->scaler_params[1] = param[1];

    if (sws_init_context(sws, srcFilter, dstFilter) < 0)
        sws_free_context(&sws);

    return sws;
}

int ff_range_add(RangeList *rl, unsigned int start, unsigned int len)
{
    Range *tmp;
    unsigned int idx;

    /* find the first existing range after the new one */
    for (idx = 0; idx < rl->nb_ranges; idx++)
        if (rl->ranges[idx].start > start)
            break;

    /* check for overlap */
    if (idx > 0) {
        Range *prev = &rl->ranges[idx - 1];
        if (prev->start + prev->len > start)
            return AVERROR(EINVAL);
    }
    if (idx < rl->nb_ranges) {
        Range *next = &rl->ranges[idx];
        if (start + len > next->start)
            return AVERROR(EINVAL);
    }

    tmp = av_fast_realloc(rl->ranges, &rl->ranges_allocated,
                          (rl->nb_ranges + 1) * sizeof(*rl->ranges));
    if (!tmp)
        return AVERROR(ENOMEM);
    rl->ranges = tmp;

    memmove(rl->ranges + idx + 1, rl->ranges + idx,
            sizeof(*rl->ranges) * (rl->nb_ranges - idx));
    rl->ranges[idx].start = start;
    rl->ranges[idx].len   = len;
    rl->nb_ranges++;

    /* merge ranges */
    if (idx > 0) {
        Range *prev = &rl->ranges[idx - 1];
        Range *cur  = &rl->ranges[idx];
        if (prev->start + prev->len == cur->start) {
            prev->len += cur->len;
            memmove(rl->ranges + idx - 1, rl->ranges + idx,
                    sizeof(*rl->ranges) * (rl->nb_ranges - idx));
            rl->nb_ranges--;
            idx--;
        }
    }
    if (idx < rl->nb_ranges - 1) {
        Range *cur  = &rl->ranges[idx];
        Range *next = &rl->ranges[idx + 1];
        if (cur->start + cur->len == next->start) {
            cur->len += next->len;
            memmove(rl->ranges + idx, rl->ranges + idx + 1,
                    sizeof(*rl->ranges) * (rl->nb_ranges - idx - 1));
            rl->nb_ranges--;
        }
    }

    return 0;
}
