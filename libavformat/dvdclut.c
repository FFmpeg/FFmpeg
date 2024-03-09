/*
 * DVD-Video subpicture CLUT (Color Lookup Table) utilities
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

#include "libavutil/bprint.h"
#include "libavutil/colorspace.h"
#include "libavutil/common.h"

#include "dvdclut.h"
#include "internal.h"

int ff_dvdclut_palette_extradata_cat(const uint32_t *clut,
                                     const size_t clut_size,
                                     AVCodecParameters *par)
{
    AVBPrint bp;

    if (clut_size != FF_DVDCLUT_CLUT_SIZE)
        return AVERROR(EINVAL);

    av_bprint_init(&bp, 0, FF_DVDCLUT_EXTRADATA_SIZE);

    av_bprintf(&bp, "palette: ");

    for (int i = 0; i < FF_DVDCLUT_CLUT_LEN; i++)
        av_bprintf(&bp, "%06"PRIx32"%s", clut[i],
                   i != (FF_DVDCLUT_CLUT_LEN - 1) ? ", " : "");

    av_bprintf(&bp, "\n");

    return ff_bprint_to_codecpar_extradata(par, &bp);
}

int ff_dvdclut_yuv_to_rgb(uint32_t *clut, const size_t clut_size)
{
    int y, cb, cr;
    uint8_t r, g, b;
    int r_add, g_add, b_add;

    if (clut_size != FF_DVDCLUT_CLUT_SIZE)
        return AVERROR(EINVAL);

    for (int i = 0; i < FF_DVDCLUT_CLUT_LEN; i++) {
        y  = (clut[i] >> 16) & 0xFF;
        cr = (clut[i] >> 8)  & 0xFF;
        cb = clut[i]         & 0xFF;

        YUV_TO_RGB1_CCIR(cb, cr);

        y = (y - 16) * FIX(255.0 / 219.0);
        r = av_clip_uint8((y + r_add - 1024) >> SCALEBITS);
        g = av_clip_uint8((y + g_add - 1024) >> SCALEBITS);
        b = av_clip_uint8((y + b_add - 1024) >> SCALEBITS);

        clut[i] = (r << 16) | (g << 8) | b;
    }

    return 0;
}
