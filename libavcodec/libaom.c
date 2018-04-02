/*
 * Copyright (c) 2013 Guillaume Martres <smarter@ubuntu.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <aom/aom_image.h>

#include "libaom.h"

#define HIGH_DEPTH(fmt)                      \
case AOM_IMG_FMT_I ## fmt ## 16:             \
    switch (depth) {                         \
    case  8:                                 \
        return AV_PIX_FMT_YUV ## fmt ## P;   \
    case 10:                                 \
        return AV_PIX_FMT_YUV ## fmt ## P10; \
    case 12:                                 \
        return AV_PIX_FMT_YUV ## fmt ## P12; \
    default:                                 \
        return AV_PIX_FMT_NONE;              \
    }

enum AVPixelFormat ff_aom_imgfmt_to_pixfmt(aom_img_fmt_t img, int depth)
{
    switch (img) {
    case AOM_IMG_FMT_RGB24:
        return AV_PIX_FMT_RGB24;
    case AOM_IMG_FMT_RGB565:
        return AV_PIX_FMT_RGB565BE;
    case AOM_IMG_FMT_RGB555:
        return AV_PIX_FMT_RGB555BE;
    case AOM_IMG_FMT_UYVY:
        return AV_PIX_FMT_UYVY422;
    case AOM_IMG_FMT_YUY2:
        return AV_PIX_FMT_YUYV422;
    case AOM_IMG_FMT_YVYU:
        return AV_PIX_FMT_YVYU422;
    case AOM_IMG_FMT_BGR24:
        return AV_PIX_FMT_BGR24;
    case AOM_IMG_FMT_ARGB:
        return AV_PIX_FMT_ARGB;
    case AOM_IMG_FMT_ARGB_LE:
        return AV_PIX_FMT_BGRA;
    case AOM_IMG_FMT_RGB565_LE:
        return AV_PIX_FMT_RGB565LE;
    case AOM_IMG_FMT_RGB555_LE:
        return AV_PIX_FMT_RGB555LE;
    case AOM_IMG_FMT_I420:
        return AV_PIX_FMT_YUV420P;
    case AOM_IMG_FMT_I422:
        return AV_PIX_FMT_YUV422P;
    case AOM_IMG_FMT_I444:
        return AV_PIX_FMT_YUV444P;
    case AOM_IMG_FMT_444A:
        return AV_PIX_FMT_YUVA444P;
    HIGH_DEPTH(420)
    HIGH_DEPTH(422)
    HIGH_DEPTH(444)
    default:
        return AV_PIX_FMT_NONE;
    }
}

#undef HIGH_DEPTH

aom_img_fmt_t ff_aom_pixfmt_to_imgfmt(enum AVPixelFormat pix)
{
    switch (pix) {
    case AV_PIX_FMT_RGB24:
        return AOM_IMG_FMT_RGB24;
    case AV_PIX_FMT_RGB565BE:
        return AOM_IMG_FMT_RGB565;
    case AV_PIX_FMT_RGB555BE:
        return AOM_IMG_FMT_RGB555;
    case AV_PIX_FMT_UYVY422:
        return AOM_IMG_FMT_UYVY;
    case AV_PIX_FMT_YUYV422:
        return AOM_IMG_FMT_YUY2;
    case AV_PIX_FMT_YVYU422:
        return AOM_IMG_FMT_YVYU;
    case AV_PIX_FMT_BGR24:
        return AOM_IMG_FMT_BGR24;
    case AV_PIX_FMT_ARGB:
        return AOM_IMG_FMT_ARGB;
    case AV_PIX_FMT_BGRA:
        return AOM_IMG_FMT_ARGB_LE;
    case AV_PIX_FMT_RGB565LE:
        return AOM_IMG_FMT_RGB565_LE;
    case AV_PIX_FMT_RGB555LE:
        return AOM_IMG_FMT_RGB555_LE;
    case AV_PIX_FMT_YUV420P:
        return AOM_IMG_FMT_I420;
    case AV_PIX_FMT_YUV422P:
        return AOM_IMG_FMT_I422;
    case AV_PIX_FMT_YUV444P:
        return AOM_IMG_FMT_I444;
    case AV_PIX_FMT_YUVA444P:
        return AOM_IMG_FMT_444A;
    case AV_PIX_FMT_YUV420P10:
        return AOM_IMG_FMT_I42016;
    case AV_PIX_FMT_YUV422P10:
        return AOM_IMG_FMT_I42216;
    case AV_PIX_FMT_YUV444P10:
        return AOM_IMG_FMT_I44416;
    case AV_PIX_FMT_YUV420P12:
        return AOM_IMG_FMT_I42016;
    case AV_PIX_FMT_YUV422P12:
        return AOM_IMG_FMT_I42216;
    case AV_PIX_FMT_YUV444P12:
        return AOM_IMG_FMT_I44416;
    default:
        return AOM_IMG_FMT_NONE;
    }
}
