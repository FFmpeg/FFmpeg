/*
 * D-Cinema audio demuxer
 * Copyright (c) 2005 Reimar Döffinger
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

#include "libavutil/channel_layout.h"
#include "avformat.h"

static int daud_header(AVFormatContext *s) {
    AVStream *st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id = AV_CODEC_ID_PCM_S24DAUD;
    st->codecpar->codec_tag = MKTAG('d', 'a', 'u', 'd');
    st->codecpar->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_5POINT1;
    st->codecpar->sample_rate = 96000;
    st->codecpar->bit_rate = 3 * 6 * 96000 * 8;
    st->codecpar->block_align = 3 * 6;
    st->codecpar->bits_per_coded_sample = 24;
    return 0;
}

static int daud_packet(AVFormatContext *s, AVPacket *pkt) {
    AVIOContext *pb = s->pb;
    int ret, size;
    if (avio_feof(pb))
        return AVERROR(EIO);
    size = avio_rb16(pb);
    avio_rb16(pb); // unknown
    ret = av_get_packet(pb, pkt, size);
    pkt->stream_index = 0;
    return ret;
}

const AVInputFormat ff_daud_demuxer = {
    .name           = "daud",
    .long_name      = NULL_IF_CONFIG_SMALL("D-Cinema audio"),
    .read_header    = daud_header,
    .read_packet    = daud_packet,
    .extensions     = "302,daud",
};
