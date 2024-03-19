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
#include "avio_internal.h"
#include "mux.h"
#include "rawenc.h"

#define RAND_TAG MKBETAG('R','a','n','d')

static av_cold int init(AVFormatContext *s)
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

    avio_wb32(pb, RAND_TAG);
    avio_wb32(pb, st->nb_frames);
    avio_wb16(pb, 0);  // packing method
    avio_wb16(pb, 0);  // reserved
    avio_wb16(pb, st->codecpar->width);
    avio_wb16(pb, st->codecpar->height);
    avio_wb16(pb, 0);  // leading
    // TODO: should be avg_frame_rate
    avio_wb16(pb, st->time_base.den / st->time_base.num);
    ffio_fill(pb, 0x00, 16);  // reserved

    return 0;
}

const FFOutputFormat ff_filmstrip_muxer = {
    .p.name            = "filmstrip",
    .p.long_name       = NULL_IF_CONFIG_SMALL("Adobe Filmstrip"),
    .p.extensions      = "flm",
    .p.audio_codec     = AV_CODEC_ID_NONE,
    .p.video_codec     = AV_CODEC_ID_RAWVIDEO,
    .p.subtitle_codec  = AV_CODEC_ID_NONE,
    .flags_internal    = FF_OFMT_FLAG_MAX_ONE_OF_EACH |
                         FF_OFMT_FLAG_ONLY_DEFAULT_CODECS,
    .init              = init,
    .write_packet      = ff_raw_write_packet,
    .write_trailer     = write_trailer,
};
