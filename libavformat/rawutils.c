/*
 * Raw video utils
 * Copyright (c) 2016 Michael Niedermayer
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

#include "avformat.h"
#include "internal.h"

int ff_reshuffle_raw_rgb(AVFormatContext *s, AVPacket **ppkt, AVCodecContext *enc, int expected_stride)
{
    int ret;
    AVPacket *pkt = *ppkt;
    int64_t bpc = enc->bits_per_coded_sample != 15 ? enc->bits_per_coded_sample : 16;
    int min_stride = (enc->width * bpc + 7) >> 3;
    int with_pal_size = min_stride * enc->height + 1024;
    int size = bpc == 8 && pkt->size == with_pal_size ? min_stride * enc->height : pkt->size;
    int stride = size / enc->height;
    int padding = expected_stride - FFMIN(expected_stride, stride);
    int y;
    AVPacket *new_pkt;

    if (pkt->size == expected_stride * enc->height)
        return 0;
    if (size != stride * enc->height)
        return 0;

    new_pkt = av_packet_alloc();
    if (!new_pkt)
        return AVERROR(ENOMEM);

    ret = av_new_packet(new_pkt, expected_stride * enc->height);
    if (ret < 0)
        goto fail;

    ret = av_packet_copy_props(new_pkt, pkt);
    if (ret < 0)
        goto fail;

    for (y = 0; y<enc->height; y++) {
        memcpy(new_pkt->data + y*expected_stride, pkt->data + y*stride, FFMIN(expected_stride, stride));
        memset(new_pkt->data + y*expected_stride + expected_stride - padding, 0, padding);
    }

    *ppkt = new_pkt;
    return 1;
fail:
    av_packet_free(&new_pkt);

    return ret;
}
