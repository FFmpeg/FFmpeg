/*
 * AVPicture management routines
 * Copyright (c) 2001, 2002, 2003 Fabrice Bellard
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

/**
 * @file
 * AVPicture management routines
 */

#include "avcodec.h"
#include "internal.h"
#include "libavutil/common.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/colorspace.h"

int avpicture_fill(AVPicture *picture, uint8_t *ptr,
                   enum AVPixelFormat pix_fmt, int width, int height)
{
    int ret;

    if ((ret = av_image_check_size(width, height, 0, NULL)) < 0)
        return ret;

    if ((ret = av_image_fill_linesizes(picture->linesize, pix_fmt, width)) < 0)
        return ret;

    return av_image_fill_pointers(picture->data, pix_fmt,
                                  height, ptr, picture->linesize);
}

int avpicture_layout(const AVPicture* src, enum AVPixelFormat pix_fmt,
                     int width, int height,
                     unsigned char *dest, int dest_size)
{
    int i, j, nb_planes = 0, linesizes[4];
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    int size = avpicture_get_size(pix_fmt, width, height);

    if (size > dest_size || size < 0)
        return AVERROR(EINVAL);

    for (i = 0; i < desc->nb_components; i++)
        nb_planes = FFMAX(desc->comp[i].plane, nb_planes);

    nb_planes++;

    av_image_fill_linesizes(linesizes, pix_fmt, width);
    for (i = 0; i < nb_planes; i++) {
        int h, shift = (i == 1 || i == 2) ? desc->log2_chroma_h : 0;
        const unsigned char *s = src->data[i];
        h = (height + (1 << shift) - 1) >> shift;

        for (j = 0; j < h; j++) {
            memcpy(dest, s, linesizes[i]);
            dest += linesizes[i];
            s += src->linesize[i];
        }
    }

    if (desc->flags & AV_PIX_FMT_FLAG_PAL)
        memcpy((unsigned char *)(((size_t)dest + 3) & ~3),
               src->data[1], 256 * 4);

    return size;
}

int avpicture_get_size(enum AVPixelFormat pix_fmt, int width, int height)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    AVPicture dummy_pict;
    int ret;

    if (!desc)
        return AVERROR(EINVAL);
    if ((ret = av_image_check_size(width, height, 0, NULL)) < 0)
        return ret;
    if (desc->flags & AV_PIX_FMT_FLAG_PSEUDOPAL)
        // do not include palette for these pseudo-paletted formats
        return width * height;
    return avpicture_fill(&dummy_pict, NULL, pix_fmt, width, height);
}

int avpicture_alloc(AVPicture *picture,
                    enum AVPixelFormat pix_fmt, int width, int height)
{
    int ret = av_image_alloc(picture->data, picture->linesize,
                             width, height, pix_fmt, 1);
    if (ret < 0) {
        memset(picture, 0, sizeof(AVPicture));
        return ret;
    }

    return 0;
}

void avpicture_free(AVPicture *picture)
{
    av_free(picture->data[0]);
}

void av_picture_copy(AVPicture *dst, const AVPicture *src,
                     enum AVPixelFormat pix_fmt, int width, int height)
{
    av_image_copy(dst->data, dst->linesize, src->data,
                  src->linesize, pix_fmt, width, height);
}

