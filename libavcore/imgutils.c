/*
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

/**
 * @file
 * misc image utilities
 */

#include "imgutils.h"
#include "libavutil/pixdesc.h"

int av_fill_image_linesizes(int linesizes[4], enum PixelFormat pix_fmt, int width)
{
    int i;
    const AVPixFmtDescriptor *desc = &av_pix_fmt_descriptors[pix_fmt];
    int max_step     [4];       /* max pixel step for each plane */
    int max_step_comp[4];       /* the component for each plane which has the max pixel step */

    memset(linesizes, 0, 4*sizeof(linesizes[0]));

    if (desc->flags & PIX_FMT_HWACCEL)
        return AVERROR(EINVAL);

    if (desc->flags & PIX_FMT_BITSTREAM) {
        linesizes[0] = (width * (desc->comp[0].step_minus1+1) + 7) >> 3;
        return 0;
    }

    memset(max_step     , 0, sizeof(max_step     ));
    memset(max_step_comp, 0, sizeof(max_step_comp));
    for (i = 0; i < 4; i++) {
        const AVComponentDescriptor *comp = &(desc->comp[i]);
        if ((comp->step_minus1+1) > max_step[comp->plane]) {
            max_step     [comp->plane] = comp->step_minus1+1;
            max_step_comp[comp->plane] = i;
        }
    }

    for (i = 0; i < 4; i++) {
        int s = (max_step_comp[i] == 1 || max_step_comp[i] == 2) ? desc->log2_chroma_w : 0;
        linesizes[i] = max_step[i] * (((width + (1 << s) - 1)) >> s);
    }

    return 0;
}

int av_fill_image_pointers(uint8_t *data[4], enum PixelFormat pix_fmt, int height,
                           uint8_t *ptr, const int linesizes[4])
{
    int i, total_size, size[4], has_plane[4];

    const AVPixFmtDescriptor *desc = &av_pix_fmt_descriptors[pix_fmt];
    memset(data     , 0, sizeof(data[0])*4);
    memset(size     , 0, sizeof(size));
    memset(has_plane, 0, sizeof(has_plane));

    if (desc->flags & PIX_FMT_HWACCEL)
        return AVERROR(EINVAL);

    data[0] = ptr;
    size[0] = linesizes[0] * height;

    if (desc->flags & PIX_FMT_PAL) {
        size[0] = (size[0] + 3) & ~3;
        data[1] = ptr + size[0]; /* palette is stored here as 256 32 bits words */
        return size[0] + 256 * 4;
    }

    for (i = 0; i < 4; i++)
        has_plane[desc->comp[i].plane] = 1;

    total_size = size[0];
    for (i = 1; has_plane[i] && i < 4; i++) {
        int h, s = (i == 1 || i == 2) ? desc->log2_chroma_h : 0;
        data[i] = data[i-1] + size[i-1];
        h = (height + (1 << s) - 1) >> s;
        size[i] = h * linesizes[i];
        total_size += size[i];
    }

    return total_size;
}
