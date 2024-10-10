/*
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 * Contributed by Shiyou Yin<yinshiyou-hf@loongson.cn>
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

av_cold void ff_sws_init_input_lsx(SwsInternal *c)
{
    enum AVPixelFormat srcFormat = c->opts.src_format;

    switch (srcFormat) {
    case AV_PIX_FMT_YUYV422:
        c->chrToYV12 = yuy2ToUV_lsx;
        break;
    case AV_PIX_FMT_YVYU422:
        c->chrToYV12 = yvy2ToUV_lsx;
        break;
    case AV_PIX_FMT_UYVY422:
        c->chrToYV12 = uyvyToUV_lsx;
        break;
    case AV_PIX_FMT_NV12:
    case AV_PIX_FMT_NV16:
    case AV_PIX_FMT_NV24:
        c->chrToYV12 = nv12ToUV_lsx;
        break;
    case AV_PIX_FMT_NV21:
    case AV_PIX_FMT_NV42:
        c->chrToYV12 = nv21ToUV_lsx;
        break;
    case AV_PIX_FMT_GBRAP:
    case AV_PIX_FMT_GBRP:
        c->readChrPlanar = planar_rgb_to_uv_lsx;
        break;
    }

    if (c->needAlpha) {
        switch (srcFormat) {
        case AV_PIX_FMT_BGRA:
        case AV_PIX_FMT_RGBA:
            c->alpToYV12 = rgbaToA_lsx;
            break;
        case AV_PIX_FMT_ABGR:
        case AV_PIX_FMT_ARGB:
            c->alpToYV12 = abgrToA_lsx;
            break;
        }
    }
}
