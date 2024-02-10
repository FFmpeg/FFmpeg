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

#include "libavutil/attributes.h"
#include "libavutil/channel_layout.h"
#include "avformat.h"
#include "demux.h"
#include "internal.h"

static const uint8_t frame_size[4] = { 24, 20, 4, 1 };

static av_cold int g723_1_init(AVFormatContext *s)
{
    AVStream *st;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type     = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id       = AV_CODEC_ID_G723_1;
    st->codecpar->ch_layout      = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
    st->codecpar->sample_rate    = 8000;

    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
    st->start_time = 0;

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
        return ret < 0 ? ret : AVERROR_EOF;
    }

    return pkt->size;
}

const FFInputFormat ff_g723_1_demuxer = {
    .p.name         = "g723_1",
    .p.long_name    = NULL_IF_CONFIG_SMALL("G.723.1"),
    .p.extensions   = "tco,rco,g723_1",
    .p.flags        = AVFMT_GENERIC_INDEX,
    .read_header = g723_1_init,
    .read_packet = g723_1_read_packet,
};
