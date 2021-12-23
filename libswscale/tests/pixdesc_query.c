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

#include <stdlib.h>

#include "libavutil/mem.h"
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
    {"isSemiPlanarYUV", isSemiPlanarYUV},
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
    {"DataInHighBits", isDataInHighBits},
    {"SwappedChroma", isSwappedChroma},
};

static int cmp_str(const void *a, const void *b)
{
    const char *s1 = *(const char **)a;
    const char *s2 = *(const char **)b;
    return strcmp(s1, s2);
}

int main(void)
{
    int i, j;

    for (i = 0; i < FF_ARRAY_ELEMS(query_tab); i++) {
        const char **pix_fmts = NULL;
        int nb_pix_fmts = 0;
        const AVPixFmtDescriptor *pix_desc = NULL;

        while ((pix_desc = av_pix_fmt_desc_next(pix_desc))) {
            enum AVPixelFormat pix_fmt = av_pix_fmt_desc_get_id(pix_desc);
            if (query_tab[i].cond(pix_fmt)) {
                const char *pix_name = pix_desc->name;
                if      (pix_fmt == AV_PIX_FMT_RGB32)   pix_name = "rgb32";
                else if (pix_fmt == AV_PIX_FMT_RGB32_1) pix_name = "rgb32_1";
                else if (pix_fmt == AV_PIX_FMT_BGR32)   pix_name = "bgr32";
                else if (pix_fmt == AV_PIX_FMT_BGR32_1) pix_name = "bgr32_1";

                av_dynarray_add(&pix_fmts, &nb_pix_fmts, (void *)pix_name);
            }
        }

        if (pix_fmts) {
            qsort(pix_fmts, nb_pix_fmts, sizeof(*pix_fmts), cmp_str);

            printf("%s:\n", query_tab[i].class);
            for (j = 0; j < nb_pix_fmts; j++)
                printf("  %s\n", pix_fmts[j]);
            printf("\n");

            av_free(pix_fmts);
        }
    }
    return 0;
}
