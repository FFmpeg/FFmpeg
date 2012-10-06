/*
 * Adobe Filmstrip muxer
 * Copyright (c) 2010 Peter Ross
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
 * Adobe Filmstrip muxer
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"

#define RAND_TAG MKBETAG('R','a','n','d')

typedef struct {
    int nb_frames;
} FilmstripMuxContext;

static int write_header(AVFormatContext *s)
{
    if (s->streams[0]->codec->pix_fmt != AV_PIX_FMT_RGBA) {
        av_log(s, AV_LOG_ERROR, "only AV_PIX_FMT_RGBA is supported\n");
        return AVERROR_INVALIDDATA;
    }
    return 0;
}

static int write_packet(AVFormatContext *s, AVPacket *pkt)
{
    FilmstripMuxContext *film = s->priv_data;
    avio_write(s->pb, pkt->data, pkt->size);
    film->nb_frames++;
    return 0;
}

static int write_trailer(AVFormatContext *s)
{
    FilmstripMuxContext *film = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st = s->streams[0];
    int i;

    avio_wb32(pb, RAND_TAG);
    avio_wb32(pb, film->nb_frames);
    avio_wb16(pb, 0);  // packing method
    avio_wb16(pb, 0);  // reserved
    avio_wb16(pb, st->codec->width);
    avio_wb16(pb, st->codec->height);
    avio_wb16(pb, 0);  // leading
    avio_wb16(pb, 1/av_q2d(st->codec->time_base));
    for (i = 0; i < 16; i++)
        avio_w8(pb, 0x00);  // reserved

    return 0;
}

AVOutputFormat ff_filmstrip_muxer = {
    .name              = "filmstrip",
    .long_name         = NULL_IF_CONFIG_SMALL("Adobe Filmstrip"),
    .extensions        = "flm",
    .priv_data_size    = sizeof(FilmstripMuxContext),
    .audio_codec       = AV_CODEC_ID_NONE,
    .video_codec       = AV_CODEC_ID_RAWVIDEO,
    .write_header      = write_header,
    .write_packet      = write_packet,
    .write_trailer     = write_trailer,
};
