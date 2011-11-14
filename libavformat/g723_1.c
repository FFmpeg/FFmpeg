/*
 * G.723.1 demuxer
 * Copyright (c) 2010 Mohamed Naufal Basheer
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
 * G.723.1 demuxer
 */

#include "avformat.h"

static const uint8_t frame_size[4] = {24, 20, 4, 1};

static int g723_1_init(AVFormatContext *s, AVFormatParameters *ap)
{
    AVStream *st;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codec->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id    = CODEC_ID_G723_1;
    st->codec->channels    = 1;
    st->codec->sample_rate = 8000;

    av_set_pts_info(st, 64, 1, st->codec->sample_rate);

    return 0;
}

static int g723_1_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int size, byte, ret;

    pkt->pos = avio_tell(s->pb);
    byte     = avio_r8(s->pb);
    size     = frame_size[byte & 3];

    ret = av_new_packet(pkt, size);
    if (ret < 0)
        return ret;

    pkt->data[0]      = byte;
    pkt->duration     = 240;
    pkt->stream_index = 0;

    ret = avio_read(s->pb, pkt->data + 1, size - 1);
    if (ret < size - 1) {
        av_free_packet(pkt);
        return ret < 0 ? ret : AVERROR_EOF;
    }

    return pkt->size;
}

AVInputFormat ff_g723_1_demuxer = {
    "g723_1",
    NULL_IF_CONFIG_SMALL("G.723.1 format"),
    0,
    NULL,
    g723_1_init,
    g723_1_read_packet,
    .extensions = "tco,rco",
    .flags = AVFMT_GENERIC_INDEX
};
