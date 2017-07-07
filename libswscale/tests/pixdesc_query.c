/*
 * Copyright (c) 2017 Clément Bœsch <u pkh me>
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

#include "libswscale/swscale_internal.h"

static const struct {
    const char *class;
    int (*cond)(enum AVPixelFormat pix_fmt);
} query_tab[] = {
    {"is16BPS",     is16BPS},
    {"isNBPS",      isNBPS},
    {"isBE",        isBE},
    {"isYUV",       isYUV},
    {"isPlanarYUV", isPlanarYUV},
    {"isRGB",       isRGB},
    {"Gray",        isGray},
    {"RGBinInt",    isRGBinInt},
    {"BGRinInt",    isBGRinInt},
    {"Bayer",       isBayer},
    {"AnyRGB",      isAnyRGB},
    {"ALPHA",       isALPHA},
    {"Packed",      isPacked},
    {"Planar",      isPlanar},
    {"PackedRGB",   isPackedRGB},
    {"PlanarRGB",   isPlanarRGB},
    {"usePal",      usePal},
};

int main(void)
{
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(query_tab); i++) {
        const AVPixFmtDescriptor *pix_desc = NULL;
        printf("%s:\n", query_tab[i].class);
        while ((pix_desc = av_pix_fmt_desc_next(pix_desc))) {
            enum AVPixelFormat pix_fmt = av_pix_fmt_desc_get_id(pix_desc);
            if (query_tab[i].cond(pix_fmt))
                printf("  %s\n", pix_desc->name);
        }
        printf("\n");
    }
    return 0;
}
