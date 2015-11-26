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
#include "libavutil/internal.h"
#include "libavutil/colorspace.h"

#if FF_API_AVPICTURE
FF_DISABLE_DEPRECATION_WARNINGS
int avpicture_fill(AVPicture *picture, uint8_t *ptr,
                   enum AVPixelFormat pix_fmt, int width, int height)
{
    return av_image_fill_arrays(picture->data, picture->linesize,
                                ptr, pix_fmt, width, height, 1);
}

int avpicture_layout(const AVPicture* src, enum AVPixelFormat pix_fmt,
                     int width, int height,
                     unsigned char *dest, int dest_size)
{
    return av_image_copy_to_buffer(dest, dest_size,
                                   src->data, src->linesize,
                                   pix_fmt, width, height, 1);
}

int avpicture_get_size(enum AVPixelFormat pix_fmt, int width, int height)
{
    return av_image_get_buffer_size(pix_fmt, width, height, 1);
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
FF_ENABLE_DEPRECATION_WARNINGS
#endif /* FF_API_AVPICTURE */
