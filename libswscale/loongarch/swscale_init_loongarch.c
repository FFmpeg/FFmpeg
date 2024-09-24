/*
 * Copyright (C) 2022 Loongson Technology Corporation Limited
 * Contributed by Hao Chen(chenhao@loongson.cn)
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
#include "libswscale/rgb2rgb.h"
#include "libavutil/loongarch/cpu.h"

av_cold void ff_sws_init_range_convert_loongarch(SwsInternal *c)
{
    /* This code is currently disabled because of changes in the base
     * implementation of these functions. This code should be enabled
     * again once those changes are ported to this architecture. */
#if 0
    int cpu_flags = av_get_cpu_flags();

    if (have_lsx(cpu_flags)) {
        if (c->dstBpc <= 14) {
            if (c->opts.src_range) {
                c->lumConvertRange = lumRangeFromJpeg_lsx;
                c->chrConvertRange = chrRangeFromJpeg_lsx;
            } else {
                c->lumConvertRange = lumRangeToJpeg_lsx;
                c->chrConvertRange = chrRangeToJpeg_lsx;
            }
        }
    }
#if HAVE_LASX
    if (have_lasx(cpu_flags)) {
        if (c->dstBpc <= 14) {
            if (c->opts.src_range) {
                c->lumConvertRange = lumRangeFromJpeg_lasx;
                c->chrConvertRange = chrRangeFromJpeg_lasx;
            } else {
                c->lumConvertRange = lumRangeToJpeg_lasx;
                c->chrConvertRange = chrRangeToJpeg_lasx;
            }
        }
    }
#endif // #if HAVE_LASX
#endif
}

av_cold void ff_sws_init_swscale_loongarch(SwsInternal *c)
{
    int cpu_flags = av_get_cpu_flags();
    if (have_lsx(cpu_flags)) {
        ff_sws_init_output_lsx(c, &c->yuv2plane1, &c->yuv2planeX,
                               &c->yuv2nv12cX, &c->yuv2packed1,
                               &c->yuv2packed2, &c->yuv2packedX, &c->yuv2anyX);
        ff_sws_init_input_lsx(c);
        if (c->srcBpc == 8) {
            if (c->dstBpc <= 14) {
                c->hyScale = c->hcScale = ff_hscale_8_to_15_lsx;
            } else {
                c->hyScale = c->hcScale = ff_hscale_8_to_19_lsx;
            }
        } else {
            c->hyScale = c->hcScale = c->dstBpc > 14 ? ff_hscale_16_to_19_lsx
                                                     : ff_hscale_16_to_15_lsx;
        }
    }
#if HAVE_LASX
    if (have_lasx(cpu_flags)) {
        ff_sws_init_output_lasx(c, &c->yuv2plane1, &c->yuv2planeX,
                                &c->yuv2nv12cX, &c->yuv2packed1,
                                &c->yuv2packed2, &c->yuv2packedX, &c->yuv2anyX);
        ff_sws_init_input_lasx(c);
        if (c->srcBpc == 8) {
            if (c->dstBpc <= 14) {
                c->hyScale = c->hcScale = ff_hscale_8_to_15_lasx;
            } else {
                c->hyScale = c->hcScale = ff_hscale_8_to_19_lasx;
            }
        } else {
            c->hyScale = c->hcScale = c->dstBpc > 14 ? ff_hscale_16_to_19_lasx
                                                     : ff_hscale_16_to_15_lasx;
        }
    }
#endif // #if HAVE_LASX
}

av_cold void rgb2rgb_init_loongarch(void)
{
#if HAVE_LASX
    int cpu_flags = av_get_cpu_flags();
    if (have_lasx(cpu_flags))
        interleaveBytes = ff_interleave_bytes_lasx;
#endif // #if HAVE_LASX
}

av_cold SwsFunc ff_yuv2rgb_init_loongarch(SwsInternal *c)
{
    int cpu_flags = av_get_cpu_flags();
#if HAVE_LASX
    if (have_lasx(cpu_flags)) {
        if (c->opts.src_format == AV_PIX_FMT_YUV420P) {
            switch (c->opts.dst_format) {
                case AV_PIX_FMT_RGB24:
                    return yuv420_rgb24_lasx;
                case AV_PIX_FMT_BGR24:
                    return yuv420_bgr24_lasx;
                case AV_PIX_FMT_RGBA:
                    if (CONFIG_SWSCALE_ALPHA && isALPHA(c->opts.src_format)) {
                        break;
                    } else
                        return yuv420_rgba32_lasx;
                case AV_PIX_FMT_ARGB:
                    if (CONFIG_SWSCALE_ALPHA && isALPHA(c->opts.src_format)) {
                        break;
                    } else
                        return yuv420_argb32_lasx;
                case AV_PIX_FMT_BGRA:
                    if (CONFIG_SWSCALE_ALPHA && isALPHA(c->opts.src_format)) {
                        break;
                    } else
                        return yuv420_bgra32_lasx;
                case AV_PIX_FMT_ABGR:
                    if (CONFIG_SWSCALE_ALPHA && isALPHA(c->opts.src_format)) {
                        break;
                    } else
                        return yuv420_abgr32_lasx;
            }
        }
    }
#endif // #if HAVE_LASX
    if (have_lsx(cpu_flags)) {
        if (c->opts.src_format == AV_PIX_FMT_YUV420P) {
            switch (c->opts.dst_format) {
                case AV_PIX_FMT_RGB24:
                    return yuv420_rgb24_lsx;
                case AV_PIX_FMT_BGR24:
                    return yuv420_bgr24_lsx;
                case AV_PIX_FMT_RGBA:
                    if (CONFIG_SWSCALE_ALPHA && isALPHA(c->opts.src_format)) {
                        break;
                    } else
                        return yuv420_rgba32_lsx;
                case AV_PIX_FMT_ARGB:
                    if (CONFIG_SWSCALE_ALPHA && isALPHA(c->opts.src_format)) {
                        break;
                    } else
                        return yuv420_argb32_lsx;
                case AV_PIX_FMT_BGRA:
                    if (CONFIG_SWSCALE_ALPHA && isALPHA(c->opts.src_format)) {
                        break;
                    } else
                        return yuv420_bgra32_lsx;
                case AV_PIX_FMT_ABGR:
                    if (CONFIG_SWSCALE_ALPHA && isALPHA(c->opts.src_format)) {
                        break;
                    } else
                        return yuv420_abgr32_lsx;
            }
        }
    }
    return NULL;
}
