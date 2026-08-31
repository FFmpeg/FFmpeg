/*
 * Copyright (C) 2026 Loongson Technology Co. Ltd.
 * Contributed by Bo Jin(jinbo@loongson.cn)
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

#include "swscale_loongarch.h"
#include "libswscale/swscale_internal.h"
#include "libavutil/loongarch/cpu.h"

/* C reference implementations of the unscaled NV12/NV21 -> 32-bit RGB
 * conversions (interleaved chroma consumed from src[1]). They work as
 * the runtime fallback on LoongArch when LSX is unavailable, so they use
 * the lookup-table math (like yuv2rgb_c_32) rather than the fixed-point
 * coefficient form; the LSX implementations differ from them by at most
 * a couple of LSBs, which the checkasm yuv2rgb test tolerates. */
#define NVXXRGB32FUNC(func_name, uv_swap)                                   \
int func_name(SwsInternal *c, const uint8_t *const src[],                   \
              const int srcStride[], int srcSliceY, int srcSliceH,          \
              uint8_t *const dst[], const int dstStride[])                  \
{                                                                           \
    int y;                                                                  \
                                                                            \
    for (y = 0; y < srcSliceH; y++) {                                       \
        int yd = y + srcSliceY;                                             \
        uint32_t *dest = (uint32_t *)(dst[0] + yd * dstStride[0]);          \
        const uint8_t *py  = src[0] +      y      * srcStride[0];           \
        const uint8_t *puv = src[1] + (y >> 1) * srcStride[1];              \
        int i;                                                              \
                                                                            \
        for (i = 0; i < c->opts.dst_w; i++) {                               \
            int Y = py[i];                                                  \
            int U = puv[(i >> 1) * 2 + uv_swap];                            \
            int V = puv[(i >> 1) * 2 + (uv_swap ^ 1)];                      \
            uint32_t *r = (void *)c->table_rV[V+YUVRGB_TABLE_HEADROOM];     \
            uint32_t *g = (void *)(c->table_gU[U+YUVRGB_TABLE_HEADROOM]     \
                                 + c->table_gV[V+YUVRGB_TABLE_HEADROOM]);   \
            uint32_t *b = (void *)c->table_bU[U+YUVRGB_TABLE_HEADROOM];     \
            dest[i] = r[Y] + g[Y] + b[Y];                                   \
        }                                                                   \
    }                                                                       \
    return srcSliceH;                                                       \
}

NVXXRGB32FUNC(ff_nv12ToRgb32_c, 0)
NVXXRGB32FUNC(ff_nv21ToRgb32_c, 1)

/* Unscaled NV12/NV21 -> packed 32-bit RGB */
void ff_get_unscaled_swscale_loongarch(SwsInternal *c)
{
    int cpu_flags = av_get_cpu_flags();
    int use_lsx = have_lsx(cpu_flags);

    if ((c->opts.dst_w & 1) || (c->opts.dst_h & 1) ||
        (c->opts.flags & (SWS_ACCURATE_RND | SWS_BITEXACT | SWS_FULL_CHR_H_INT)))
        return;

    if (c->opts.src_format != AV_PIX_FMT_NV12 &&
        c->opts.src_format != AV_PIX_FMT_NV21)
        return;

    switch (c->opts.dst_format) {
    case AV_PIX_FMT_RGBA:
        c->convert_unscaled = use_lsx ?
            (c->opts.src_format == AV_PIX_FMT_NV12 ? yuv420_nv12_rgba32_lsx
                                                   : yuv420_nv21_rgba32_lsx) :
            (c->opts.src_format == AV_PIX_FMT_NV12 ? ff_nv12ToRgb32_c
                                                   : ff_nv21ToRgb32_c);
        break;
    case AV_PIX_FMT_ARGB:
        c->convert_unscaled = use_lsx ?
            (c->opts.src_format == AV_PIX_FMT_NV12 ? yuv420_nv12_argb32_lsx
                                                   : yuv420_nv21_argb32_lsx) :
            (c->opts.src_format == AV_PIX_FMT_NV12 ? ff_nv12ToRgb32_c
                                                   : ff_nv21ToRgb32_c);
        break;
    case AV_PIX_FMT_BGRA:
        c->convert_unscaled = use_lsx ?
            (c->opts.src_format == AV_PIX_FMT_NV12 ? yuv420_nv12_bgra32_lsx
                                                   : yuv420_nv21_bgra32_lsx) :
            (c->opts.src_format == AV_PIX_FMT_NV12 ? ff_nv12ToRgb32_c
                                                   : ff_nv21ToRgb32_c);
        break;
    case AV_PIX_FMT_ABGR:
        c->convert_unscaled = use_lsx ?
            (c->opts.src_format == AV_PIX_FMT_NV12 ? yuv420_nv12_abgr32_lsx
                                                   : yuv420_nv21_abgr32_lsx) :
            (c->opts.src_format == AV_PIX_FMT_NV12 ? ff_nv12ToRgb32_c
                                                   : ff_nv21ToRgb32_c);
        break;
    default:
        return;
    }

    c->dst_slice_align = 2;
}
