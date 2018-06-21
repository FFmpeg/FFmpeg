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

#if HAVE_INLINE_ASM

#define DITHER1XBPP // only for MMX

/* hope these constant values are cache line aligned */
DECLARE_ASM_CONST(8, uint64_t, mmx_00ffw)   = 0x00ff00ff00ff00ffULL;
DECLARE_ASM_CONST(8, uint64_t, mmx_redmask) = 0xf8f8f8f8f8f8f8f8ULL;
DECLARE_ASM_CONST(8, uint64_t, mmx_grnmask) = 0xfcfcfcfcfcfcfcfcULL;
DECLARE_ASM_CONST(8, uint64_t, pb_e0) = 0xe0e0e0e0e0e0e0e0ULL;
DECLARE_ASM_CONST(8, uint64_t, pb_03) = 0x0303030303030303ULL;
DECLARE_ASM_CONST(8, uint64_t, pb_07) = 0x0707070707070707ULL;

//MMX versions
#if HAVE_MMX_INLINE && HAVE_6REGS
#undef RENAME
#undef COMPILE_TEMPLATE_MMXEXT
#define COMPILE_TEMPLATE_MMXEXT 0
#define RENAME(a) a ## _mmx
#include "yuv2rgb_template.c"
#endif /* HAVE_MMX_INLINE && HAVE_6REGS */

// MMXEXT versions
#if HAVE_MMXEXT_INLINE && HAVE_6REGS
#undef RENAME
#undef COMPILE_TEMPLATE_MMXEXT
#define COMPILE_TEMPLATE_MMXEXT 1
#define RENAME(a) a ## _mmxext
#include "yuv2rgb_template.c"
#endif /* HAVE_MMXEXT_INLINE && HAVE_6REGS */

#endif /* HAVE_INLINE_ASM */

av_cold SwsFunc ff_yuv2rgb_init_x86(SwsContext *c)
{
#if HAVE_MMX_INLINE && HAVE_6REGS
    int cpu_flags = av_get_cpu_flags();

#if HAVE_MMXEXT_INLINE
    if (INLINE_MMXEXT(cpu_flags)) {
        switch (c->dstFormat) {
        case AV_PIX_FMT_RGB24:
            return yuv420_rgb24_mmxext;
        case AV_PIX_FMT_BGR24:
            return yuv420_bgr24_mmxext;
        }
    }
#endif

    if (INLINE_MMX(cpu_flags)) {
        switch (c->dstFormat) {
            case AV_PIX_FMT_RGB32:
                if (c->srcFormat == AV_PIX_FMT_YUVA420P) {
#if HAVE_7REGS && CONFIG_SWSCALE_ALPHA
                    return yuva420_rgb32_mmx;
#endif
                    break;
                } else
                    return yuv420_rgb32_mmx;
            case AV_PIX_FMT_BGR32:
                if (c->srcFormat == AV_PIX_FMT_YUVA420P) {
#if HAVE_7REGS && CONFIG_SWSCALE_ALPHA
                    return yuva420_bgr32_mmx;
#endif
                    break;
                } else
                    return yuv420_bgr32_mmx;
            case AV_PIX_FMT_RGB24:
                return yuv420_rgb24_mmx;
            case AV_PIX_FMT_BGR24:
                return yuv420_bgr24_mmx;
            case AV_PIX_FMT_RGB565:
                return yuv420_rgb16_mmx;
            case AV_PIX_FMT_RGB555:
                return yuv420_rgb15_mmx;
        }
    }
#endif /* HAVE_MMX_INLINE  && HAVE_6REGS */

    return NULL;
}
