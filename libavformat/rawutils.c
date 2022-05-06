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

#include "libavutil/intreadwrite.h"
#include "libavcodec/packet.h"
#include "avformat.h"
#include "rawutils.h"

int ff_reshuffle_raw_rgb(AVFormatContext *s, AVPacket **ppkt, AVCodecParameters *par, int expected_stride)
{
    int ret;
    AVPacket *pkt = *ppkt;
    int64_t bpc = par->bits_per_coded_sample != 15 ? par->bits_per_coded_sample : 16;
    int min_stride = (par->width * bpc + 7) >> 3;
    int with_pal_size = min_stride * par->height + 1024;
    int contains_pal = bpc == 8 && pkt->size == with_pal_size;
    int size = contains_pal ? min_stride * par->height : pkt->size;
    int stride = size / par->height;
    int padding = expected_stride - FFMIN(expected_stride, stride);
    int y;
    AVPacket *new_pkt;

    if (pkt->size == expected_stride * par->height)
        return 0;
    if (size != stride * par->height)
        return 0;

    new_pkt = av_packet_alloc();
    if (!new_pkt)
        return AVERROR(ENOMEM);

    ret = av_new_packet(new_pkt, expected_stride * par->height);
    if (ret < 0)
        goto fail;

    ret = av_packet_copy_props(new_pkt, pkt);
    if (ret < 0)
        goto fail;

    for (y = 0; y<par->height; y++) {
        memcpy(new_pkt->data + y*expected_stride, pkt->data + y*stride, FFMIN(expected_stride, stride));
        memset(new_pkt->data + y*expected_stride + expected_stride - padding, 0, padding);
    }

    *ppkt = new_pkt;
    return 1 + contains_pal;
fail:
    av_packet_free(&new_pkt);

    return ret;
}

int ff_get_packet_palette(AVFormatContext *s, AVPacket *pkt, int ret, uint32_t *palette)
{
    uint8_t *side_data;
    size_t size;

    side_data = av_packet_get_side_data(pkt, AV_PKT_DATA_PALETTE, &size);
    if (side_data) {
        if (size != AVPALETTE_SIZE) {
            av_log(s, AV_LOG_ERROR, "Invalid palette side data\n");
            return AVERROR_INVALIDDATA;
        }
        memcpy(palette, side_data, AVPALETTE_SIZE);
        return 1;
    }

    if (ret == CONTAINS_PAL) {
        for (int i = 0; i < AVPALETTE_COUNT; i++)
            palette[i] = AV_RL32(pkt->data + pkt->size - AVPALETTE_SIZE + i*4);
        return 1;
    }

    return 0;
}
