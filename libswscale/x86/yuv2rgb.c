/*
 * software YUV to RGB converter
 *
 * Copyright (C) 2001-2007 Michael Niedermayer
 * Copyright (C) 2009-2010 Konstantin Shishkov
 *
 * MMX/MMXEXT template stuff (needed for fast movntq support),
 * 1,4,8bpp support and context / deglobalize stuff
 * by Michael Niedermayer (michaelni@gmx.at)
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

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "config.h"
#include "libswscale/rgb2rgb.h"
#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"
#include "libavutil/attributes.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavutil/cpu.h"

#if HAVE_X86ASM

#define YUV2RGB_LOOP(depth)                                          \
    h_size = (c->dstW + 7) & ~7;                                     \
    if (h_size * depth > FFABS(dstStride[0]))                        \
        h_size -= 8;                                                 \
                                                                     \
    vshift = c->srcFormat != AV_PIX_FMT_YUV422P;                     \
                                                                     \
    for (y = 0; y < srcSliceH; y++) {                                \
        uint8_t *image    = dst[0] + (y + srcSliceY) * dstStride[0]; \
        const uint8_t *py = src[0] +               y * srcStride[0]; \
        const uint8_t *pu = src[1] +   (y >> vshift) * srcStride[1]; \
        const uint8_t *pv = src[2] +   (y >> vshift) * srcStride[2]; \
        x86_reg index = -h_size / 2;                                 \

extern void ff_yuv_420_rgb24_ssse3(x86_reg index, uint8_t *image, const uint8_t *pu_index,
                                   const uint8_t *pv_index, const uint64_t *pointer_c_dither,
                                   const uint8_t *py_2index);
extern void ff_yuv_420_bgr24_ssse3(x86_reg index, uint8_t *image, const uint8_t *pu_index,
                                   const uint8_t *pv_index, const uint64_t *pointer_c_dither,
                                   const uint8_t *py_2index);

extern void ff_yuv_420_rgb15_ssse3(x86_reg index, uint8_t *image, const uint8_t *pu_index,
                                   const uint8_t *pv_index, const uint64_t *pointer_c_dither,
                                   const uint8_t *py_2index);
extern void ff_yuv_420_rgb16_ssse3(x86_reg index, uint8_t *image, const uint8_t *pu_index,
                                   const uint8_t *pv_index, const uint64_t *pointer_c_dither,
                                   const uint8_t *py_2index);
extern void ff_yuv_420_rgb32_ssse3(x86_reg index, uint8_t *image, const uint8_t *pu_index,
                                   const uint8_t *pv_index, const uint64_t *pointer_c_dither,
                                   const uint8_t *py_2index);
extern void ff_yuv_420_bgr32_ssse3(x86_reg index, uint8_t *image, const uint8_t *pu_index,
                                   const uint8_t *pv_index, const uint64_t *pointer_c_dither,
                                   const uint8_t *py_2index);
extern void ff_yuva_420_rgb32_ssse3(x86_reg index, uint8_t *image, const uint8_t *pu_index,
                                    const uint8_t *pv_index, const uint64_t *pointer_c_dither,
                                    const uint8_t *py_2index, const uint8_t *pa_2index);
extern void ff_yuva_420_bgr32_ssse3(x86_reg index, uint8_t *image, const uint8_t *pu_index,
                                    const uint8_t *pv_index, const uint64_t *pointer_c_dither,
                                    const uint8_t *py_2index, const uint8_t *pa_2index);
#if ARCH_X86_64
extern void ff_yuv_420_gbrp24_ssse3(x86_reg index, uint8_t *image, uint8_t *dst_b, uint8_t *dst_r,
                                    const uint8_t *pu_index, const uint8_t *pv_index,
                                    const uint64_t *pointer_c_dither,
                                    const uint8_t *py_2index);
#endif

static inline int yuv420_rgb15_ssse3(SwsInternal *c, const uint8_t *const src[],
                                     const int srcStride[],
                                     int srcSliceY, int srcSliceH,
                                     uint8_t *const dst[], const int dstStride[])
{
    int y, h_size, vshift;

    YUV2RGB_LOOP(2)

        c->blueDither  = ff_dither8[y       & 1];
        c->greenDither = ff_dither8[y       & 1];
        c->redDither   = ff_dither8[(y + 1) & 1];

        ff_yuv_420_rgb15_ssse3(index, image, pu - index, pv - index, &(c->redDither), py - 2 * index);
    }
    return srcSliceH;
}

static inline int yuv420_rgb16_ssse3(SwsInternal *c, const uint8_t *const src[],
                                     const int srcStride[],
                                     int srcSliceY, int srcSliceH,
                                     uint8_t *const dst[], const int dstStride[])
{
    int y, h_size, vshift;

    YUV2RGB_LOOP(2)

        c->blueDither  = ff_dither8[y       & 1];
        c->greenDither = ff_dither4[y       & 1];
        c->redDither   = ff_dither8[(y + 1) & 1];

        ff_yuv_420_rgb16_ssse3(index, image, pu - index, pv - index, &(c->redDither), py - 2 * index);
    }
    return srcSliceH;
}

static inline int yuv420_rgb32_ssse3(SwsInternal *c, const uint8_t *const src[],
                                     const int srcStride[],
                                     int srcSliceY, int srcSliceH,
                                     uint8_t *const dst[], const int dstStride[])
{
    int y, h_size, vshift;

    YUV2RGB_LOOP(4)

        ff_yuv_420_rgb32_ssse3(index, image, pu - index, pv - index, &(c->redDither), py - 2 * index);
    }
    return srcSliceH;
}

static inline int yuv420_bgr32_ssse3(SwsInternal *c, const uint8_t *const src[],
                                     const int srcStride[],
                                     int srcSliceY, int srcSliceH,
                                     uint8_t *const dst[], const int dstStride[])
{
    int y, h_size, vshift;

    YUV2RGB_LOOP(4)

        ff_yuv_420_bgr32_ssse3(index, image, pu - index, pv - index, &(c->redDither), py - 2 * index);
    }
    return srcSliceH;
}

static inline int yuva420_rgb32_ssse3(SwsInternal *c, const uint8_t *const src[],
                                      const int srcStride[],
                                      int srcSliceY, int srcSliceH,
                                      uint8_t *const dst[], const int dstStride[])
{
    int y, h_size, vshift;
    YUV2RGB_LOOP(4)

        const uint8_t *pa = src[3] + y * srcStride[3];
        ff_yuva_420_rgb32_ssse3(index, image, pu - index, pv - index, &(c->redDither), py - 2 * index, pa - 2 * index);
    }
    return srcSliceH;
}

static inline int yuva420_bgr32_ssse3(SwsInternal *c, const uint8_t *const src[],
                                      const int srcStride[],
                                      int srcSliceY, int srcSliceH,
                                      uint8_t *const dst[], const int dstStride[])
{
    int y, h_size, vshift;

    YUV2RGB_LOOP(4)

        const uint8_t *pa = src[3] + y * srcStride[3];
        ff_yuva_420_bgr32_ssse3(index, image, pu - index, pv - index, &(c->redDither), py - 2 * index, pa - 2 * index);
    }
    return srcSliceH;
}

static inline int yuv420_rgb24_ssse3(SwsInternal *c, const uint8_t *const src[],
                                     const int srcStride[],
                                     int srcSliceY, int srcSliceH,
                                     uint8_t *const dst[], const int dstStride[])
{
    int y, h_size, vshift;

    YUV2RGB_LOOP(3)

        ff_yuv_420_rgb24_ssse3(index, image, pu - index, pv - index, &(c->redDither), py - 2 * index);
    }
    return srcSliceH;
}

static inline int yuv420_bgr24_ssse3(SwsInternal *c, const uint8_t *const src[],
                                     const int srcStride[],
                                     int srcSliceY, int srcSliceH,
                                     uint8_t *const dst[], const int dstStride[])
{
    int y, h_size, vshift;

    YUV2RGB_LOOP(3)

        ff_yuv_420_bgr24_ssse3(index, image, pu - index, pv - index, &(c->redDither), py - 2 * index);
    }
    return srcSliceH;
}

#if ARCH_X86_64
static inline int yuv420_gbrp_ssse3(SwsInternal *c, const uint8_t *const src[],
                                    const int srcStride[],
                                    int srcSliceY, int srcSliceH,
                                    uint8_t *const dst[], const int dstStride[])
{
    int y, h_size, vshift;

    h_size = (c->dstW + 7) & ~7;
    if (h_size * 3 > FFABS(dstStride[0]))
        h_size -= 8;

    vshift = c->srcFormat != AV_PIX_FMT_YUV422P;

    for (y = 0; y < srcSliceH; y++) {
        uint8_t *dst_g    = dst[0] + (y + srcSliceY) * dstStride[0];
        uint8_t *dst_b    = dst[1] + (y + srcSliceY) * dstStride[1];
        uint8_t *dst_r    = dst[2] + (y + srcSliceY) * dstStride[2];
        const uint8_t *py = src[0] +               y * srcStride[0];
        const uint8_t *pu = src[1] +   (y >> vshift) * srcStride[1];
        const uint8_t *pv = src[2] +   (y >> vshift) * srcStride[2];
        x86_reg index = -h_size / 2;

        ff_yuv_420_gbrp24_ssse3(index, dst_g, dst_b, dst_r, pu - index, pv - index, &(c->redDither), py - 2 * index);
    }
    return srcSliceH;
}
#endif

#endif /* HAVE_X86ASM */

av_cold SwsFunc ff_yuv2rgb_init_x86(SwsInternal *c)
{
#if HAVE_X86ASM
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSSE3(cpu_flags)) {
        switch (c->dstFormat) {
        case AV_PIX_FMT_RGB32:
            if (c->srcFormat == AV_PIX_FMT_YUVA420P) {
#if CONFIG_SWSCALE_ALPHA
                return yuva420_rgb32_ssse3;
#endif
                break;
            } else
                return yuv420_rgb32_ssse3;
        case AV_PIX_FMT_BGR32:
            if (c->srcFormat == AV_PIX_FMT_YUVA420P) {
#if CONFIG_SWSCALE_ALPHA
                return yuva420_bgr32_ssse3;
#endif
                break;
            } else
                return yuv420_bgr32_ssse3;
        case AV_PIX_FMT_RGB24:
            return yuv420_rgb24_ssse3;
        case AV_PIX_FMT_BGR24:
            return yuv420_bgr24_ssse3;
        case AV_PIX_FMT_RGB565:
            return yuv420_rgb16_ssse3;
        case AV_PIX_FMT_RGB555:
            return yuv420_rgb15_ssse3;
#if ARCH_X86_64
        case AV_PIX_FMT_GBRP:
            return yuv420_gbrp_ssse3;
#endif
        }
    }

#endif /* HAVE_X86ASM */
    return NULL;
}
