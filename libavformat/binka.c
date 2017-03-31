/*
 * Copyright (c) 2021 Paul B Mahol
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
#include "avformat.h"
#include "internal.h"

static int binka_probe(const AVProbeData *p)
{
    if (AV_RB32(p->buf) == MKBETAG('1', 'F', 'C', 'B') &&
        (p->buf[4] == 1 || p->buf[4] == 2))
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int binka_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    AVStream *st;
    int entries, offset;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    avio_skip(pb, 5);

    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id = AV_CODEC_ID_BINKAUDIO_DCT;
    st->codecpar->ch_layout.nb_channels = avio_r8(pb);
    st->codecpar->sample_rate = avio_rl16(pb);
    st->duration = avio_rl32(pb);

    avio_skip(pb, 8);
    entries = avio_rl16(pb);

    offset = entries * 2 + 2;
    avio_skip(pb, offset);

    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

    return 0;
}

static int binka_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *pb = s->pb;
    AVStream *st = s->streams[0];
    int64_t pos;
    int pkt_size;
    int ret;

    if (avio_feof(pb))
        return AVERROR_EOF;

    pos = avio_tell(pb);
    avio_skip(pb, 2);
    pkt_size = avio_rl16(pb) + 4;
    if (pkt_size <= 4)
        return AVERROR(EIO);
    ret = av_new_packet(pkt, pkt_size);
    if (ret < 0)
        return ret;

    avio_read(pb, pkt->data + 4, pkt_size - 4);
    AV_WL32(pkt->data, pkt_size);

    pkt->pos = pos;
    pkt->stream_index = 0;
    pkt->duration = av_get_audio_frame_duration2(st->codecpar, 0);

    return 0;
}

const AVInputFormat ff_binka_demuxer = {
    .name           = "binka",
    .long_name      = NULL_IF_CONFIG_SMALL("Bink Audio"),
    .read_probe     = binka_probe,
    .read_header    = binka_read_header,
    .read_packet    = binka_read_packet,
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "binka",
};
