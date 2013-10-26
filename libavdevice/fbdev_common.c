/*
 * Copyright (c) 2011 Stefano Sabatini
 * Copyright (c) 2009 Giliard B. de Freitas <giliarde@gmail.com>
 * Copyright (C) 2002 Gunnar Monell <gmo@linux.nu>
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

#include "fbdev_common.h"
#include "libavutil/common.h"

struct rgb_pixfmt_map_entry {
    int bits_per_pixel;
    int red_offset, green_offset, blue_offset, alpha_offset;
    enum AVPixelFormat pixfmt;
};

static const struct rgb_pixfmt_map_entry rgb_pixfmt_map[] = {
    // bpp, red_offset,  green_offset, blue_offset, alpha_offset, pixfmt
    {  32,       0,           8,          16,           24,   AV_PIX_FMT_RGBA  },
    {  32,      16,           8,           0,           24,   AV_PIX_FMT_BGRA  },
    {  32,       8,          16,          24,            0,   AV_PIX_FMT_ARGB  },
    {  32,       3,           2,           8,            0,   AV_PIX_FMT_ABGR  },
    {  24,       0,           8,          16,            0,   AV_PIX_FMT_RGB24 },
    {  24,      16,           8,           0,            0,   AV_PIX_FMT_BGR24 },
    {  16,      11,           5,           0,           16,   AV_PIX_FMT_RGB565 },
};

enum AVPixelFormat ff_get_pixfmt_from_fb_varinfo(struct fb_var_screeninfo *varinfo)
{
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(rgb_pixfmt_map); i++) {
        const struct rgb_pixfmt_map_entry *entry = &rgb_pixfmt_map[i];
        if (entry->bits_per_pixel == varinfo->bits_per_pixel &&
            entry->red_offset     == varinfo->red.offset     &&
            entry->green_offset   == varinfo->green.offset   &&
            entry->blue_offset    == varinfo->blue.offset)
            return entry->pixfmt;
    }

    return AV_PIX_FMT_NONE;
}
