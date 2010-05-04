/*
 * software YUV to RGB converter
 *
 * Copyright (C) 2009 Konstantin Shishkov
 *
 * MMX/MMX2 template stuff (needed for fast movntq support),
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
#include <assert.h>

#include "config.h"
#include "libswscale/rgb2rgb.h"
#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"
#include "libavutil/x86_cpu.h"

#define DITHER1XBPP // only for MMX

/* hope these constant values are cache line aligned */
DECLARE_ASM_CONST(8, uint64_t, mmx_00ffw)   = 0x00ff00ff00ff00ffULL;
DECLARE_ASM_CONST(8, uint64_t, mmx_redmask) = 0xf8f8f8f8f8f8f8f8ULL;
DECLARE_ASM_CONST(8, uint64_t, mmx_grnmask) = 0xfcfcfcfcfcfcfcfcULL;

//MMX versions
#undef RENAME
#undef HAVE_MMX2
#undef HAVE_AMD3DNOW
#define HAVE_MMX2 0
#define HAVE_AMD3DNOW 0
#define RENAME(a) a ## _MMX
#if CONFIG_GPL
#include "yuv2rgb_template.c"
#else
#include "yuv2rgb_template2.c"
#endif

//MMX2 versions
#undef RENAME
#undef HAVE_MMX2
#define HAVE_MMX2 1
#define RENAME(a) a ## _MMX2
#if CONFIG_GPL
#include "yuv2rgb_template.c"
#else
#include "yuv2rgb_template2.c"
#endif

SwsFunc ff_yuv2rgb_init_mmx(SwsContext *c)
{
    if (c->flags & SWS_CPU_CAPS_MMX2) {
        switch (c->dstFormat) {
        case PIX_FMT_RGB32:
            if (CONFIG_SWSCALE_ALPHA && c->srcFormat == PIX_FMT_YUVA420P) {
                if (HAVE_7REGS) return yuva420_rgb32_MMX2;
                break;
            } else return yuv420_rgb32_MMX2;
        case PIX_FMT_BGR32:
            if (CONFIG_SWSCALE_ALPHA && c->srcFormat == PIX_FMT_YUVA420P) {
                if (HAVE_7REGS) return yuva420_bgr32_MMX2;
                break;
            } else return yuv420_bgr32_MMX2;
        case PIX_FMT_RGB24:  return yuv420_rgb24_MMX2;
        case PIX_FMT_BGR24:  return yuv420_bgr24_MMX2;
        case PIX_FMT_RGB565: return yuv420_rgb16_MMX2;
        case PIX_FMT_RGB555: return yuv420_rgb15_MMX2;
        }
    }
    if (c->flags & SWS_CPU_CAPS_MMX) {
        switch (c->dstFormat) {
        case PIX_FMT_RGB32:
            if (CONFIG_SWSCALE_ALPHA && c->srcFormat == PIX_FMT_YUVA420P) {
                if (HAVE_7REGS) return yuva420_rgb32_MMX;
                break;
            } else return yuv420_rgb32_MMX;
        case PIX_FMT_BGR32:
            if (CONFIG_SWSCALE_ALPHA && c->srcFormat == PIX_FMT_YUVA420P) {
                if (HAVE_7REGS) return yuva420_bgr32_MMX;
                break;
            } else return yuv420_bgr32_MMX;
        case PIX_FMT_RGB24:  return yuv420_rgb24_MMX;
        case PIX_FMT_BGR24:  return yuv420_bgr24_MMX;
        case PIX_FMT_RGB565: return yuv420_rgb16_MMX;
        case PIX_FMT_RGB555: return yuv420_rgb15_MMX;
        }
    }

    return NULL;
}
