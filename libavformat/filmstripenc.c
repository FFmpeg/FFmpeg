/*
 * Adobe Filmstrip muxer
 * Copyright (c) 2010 Peter Ross
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

/**
 * @file
 * Adobe Filmstrip muxer
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "rawenc.h"

#define RAND_TAG MKBETAG('R','a','n','d')

static int write_header(AVFormatContext *s)
{
    if (s->streams[0]->codecpar->format != AV_PIX_FMT_RGBA) {
        av_log(s, AV_LOG_ERROR, "only AV_PIX_FMT_RGBA is supported\n");
        return AVERROR_INVALIDDATA;
    }
    return 0;
}

static int write_trailer(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    AVStream *st = s->streams[0];
    int i;

    avio_wb32(pb, RAND_TAG);
    avio_wb32(pb, st->nb_frames);
    avio_wb16(pb, 0);  // packing method
    avio_wb16(pb, 0);  // reserved
    avio_wb16(pb, st->codecpar->width);
    avio_wb16(pb, st->codecpar->height);
    avio_wb16(pb, 0);  // leading
    // TODO: should be avg_frame_rate
    avio_wb16(pb, st->time_base.den / st->time_base.num);
    for (i = 0; i < 16; i++)
        avio_w8(pb, 0x00);  // reserved

    return 0;
}

AVOutputFormat ff_filmstrip_muxer = {
    .name              = "filmstrip",
    .long_name         = NULL_IF_CONFIG_SMALL("Adobe Filmstrip"),
    .extensions        = "flm",
    .audio_codec       = AV_CODEC_ID_NONE,
    .video_codec       = AV_CODEC_ID_RAWVIDEO,
    .write_header      = write_header,
    .write_packet      = ff_raw_write_packet,
    .write_trailer     = write_trailer,
};
