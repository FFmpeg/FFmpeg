/*
 * software YUV to RGB converter
 *
 * Copyright (C) 2009 Konstantin Shishkov
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

#define DITHER1XBPP // only for MMX

//MMX versions
#if HAVE_MMX
#undef RENAME
#define COMPILE_TEMPLATE_MMX
#define RENAME(a) a ## _mmx
#include "yuv2rgb_template.c"
#undef COMPILE_TEMPLATE_MMX
#endif /* HAVE_MMX */

// MMXEXT versions
#undef RENAME
#define COMPILE_TEMPLATE_MMXEXT
#define RENAME(a) a ## _mmxext
#include "yuv2rgb_template.c"
#undef COMPILE_TEMPLATE_MMXEXT

//SSSE3 versions
#undef RENAME
#define COMPILE_TEMPLATE_SSSE3
#define RENAME(a) a ## _ssse3
#include "yuv2rgb_template.c"

#endif /* HAVE_X86ASM */

av_cold SwsFunc ff_yuv2rgb_init_x86(SwsContext *c)
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
        }
    }

    if (EXTERNAL_MMXEXT(cpu_flags)) {
        switch (c->dstFormat) {
        case AV_PIX_FMT_RGB24:
            return yuv420_rgb24_mmxext;
        case AV_PIX_FMT_BGR24:
            return yuv420_bgr24_mmxext;
        }
    }

    if (EXTERNAL_MMX(cpu_flags)) {
        switch (c->dstFormat) {
            case AV_PIX_FMT_RGB32:
                if (c->srcFormat == AV_PIX_FMT_YUVA420P) {
#if CONFIG_SWSCALE_ALPHA
                    return yuva420_rgb32_mmx;
#endif
                    break;
                } else
                    return yuv420_rgb32_mmx;
            case AV_PIX_FMT_BGR32:
                if (c->srcFormat == AV_PIX_FMT_YUVA420P) {
#if CONFIG_SWSCALE_ALPHA
                    return yuva420_bgr32_mmx;
#endif
                    break;
                } else
                    return yuv420_bgr32_mmx;
            case AV_PIX_FMT_RGB565:
                return yuv420_rgb16_mmx;
            case AV_PIX_FMT_RGB555:
                return yuv420_rgb15_mmx;
        }
    }

#endif /* HAVE_X86ASM */
    return NULL;
}
