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

/* TODO: drop this wrapper when all the is*() becomes functions */
#define DECLARE_WRAPPER(macro)                       \
static int macro##_func(enum AVPixelFormat pix_fmt)  \
{                                                    \
    return macro(pix_fmt);                           \
}

DECLARE_WRAPPER(is16BPS)
DECLARE_WRAPPER(isNBPS)
DECLARE_WRAPPER(isBE)
DECLARE_WRAPPER(isYUV)
DECLARE_WRAPPER(isPlanarYUV)
DECLARE_WRAPPER(isRGB)
DECLARE_WRAPPER(isGray)
DECLARE_WRAPPER(isRGBinInt)
DECLARE_WRAPPER(isBGRinInt)
DECLARE_WRAPPER(isBayer)
DECLARE_WRAPPER(isAnyRGB)
DECLARE_WRAPPER(isALPHA)
DECLARE_WRAPPER(isPacked)
DECLARE_WRAPPER(isPlanar)
DECLARE_WRAPPER(isPackedRGB)
DECLARE_WRAPPER(isPlanarRGB)
DECLARE_WRAPPER(usePal)

static const struct {
    const char *class;
    int (*cond)(enum AVPixelFormat pix_fmt);
} query_tab[] = {
    {"is16BPS",     is16BPS_func},
    {"isNBPS",      isNBPS_func},
    {"isBE",        isBE_func},
    {"isYUV",       isYUV_func},
    {"isPlanarYUV", isPlanarYUV_func},
    {"isRGB",       isRGB_func},
    {"Gray",        isGray_func},
    {"RGBinInt",    isRGBinInt_func},
    {"BGRinInt",    isBGRinInt_func},
    {"Bayer",       isBayer_func},
    {"AnyRGB",      isAnyRGB_func},
    {"ALPHA",       isALPHA_func},
    {"Packed",      isPacked_func},
    {"Planar",      isPlanar_func},
    {"PackedRGB",   isPackedRGB_func},
    {"PlanarRGB",   isPlanarRGB_func},
    {"usePal",      usePal_func},
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
