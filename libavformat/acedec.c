/*
 * ACE demuxer
 * Copyright (c) 2020 Paul B Mahol
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

static int ace_probe(const AVProbeData *p)
{
    uint32_t asc;

    if (AV_RB32(p->buf) != MKBETAG('A','A','C',' '))
        return 0;
    if (p->buf_size < 0x44)
        return 0;
    asc = AV_RB32(p->buf + 0x40);
    if (asc < 0x44 || asc > p->buf_size - 4)
        return 0;
    if (AV_RB32(p->buf + asc) != MKBETAG('A','S','C',' '))
        return 0;

    return AVPROBE_SCORE_MAX / 2 + 1;
}

static int ace_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    AVCodecParameters *par;
    int ret, codec, rate, nb_channels;
    uint32_t asc_pos, size;
    AVStream *st;

    avio_skip(pb, 0x40);
    asc_pos = avio_rb32(pb);
    if (asc_pos < 0x44)
        return AVERROR_INVALIDDATA;
    avio_skip(pb, asc_pos - 0x44);
    if (avio_rb32(pb) != MKBETAG('A','S','C',' '))
        return AVERROR_INVALIDDATA;
    avio_skip(pb, 0xec);
    codec = avio_rb32(pb);
    nb_channels = avio_rb32(pb);
    if (nb_channels <= 0 || nb_channels > 8)
        return AVERROR_INVALIDDATA;
    size = avio_rb32(pb);
    if (size == 0)
        return AVERROR_INVALIDDATA;
    rate = avio_rb32(pb);
    if (rate <= 0)
        return AVERROR_INVALIDDATA;
    avio_skip(pb, 16);

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->start_time = 0;
    par = st->codecpar;
    par->codec_type  = AVMEDIA_TYPE_AUDIO;
    par->channels    = nb_channels;
    par->sample_rate = rate;
    par->block_align = (codec == 4 ? 0x60 : codec == 5 ? 0x98 : 0xC0) * nb_channels;
    st->duration     = (size / par->block_align) * 1024LL;
    par->codec_id    = AV_CODEC_ID_ATRAC3;

    ret = ff_alloc_extradata(par, 14);
    if (ret < 0)
        return ret;

    AV_WL16(st->codecpar->extradata, 1);
    AV_WL16(st->codecpar->extradata+2, 2048 * par->channels);
    AV_WL16(st->codecpar->extradata+4, 0);
    AV_WL16(st->codecpar->extradata+6, codec == 4 ? 1 : 0);
    AV_WL16(st->codecpar->extradata+8, codec == 4 ? 1 : 0);
    AV_WL16(st->codecpar->extradata+10, 1);
    AV_WL16(st->codecpar->extradata+12, 0);

    avpriv_set_pts_info(st, 64, 1, par->sample_rate);

    return 0;
}

static int ace_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVCodecParameters *par = s->streams[0]->codecpar;

    return av_get_packet(s->pb, pkt, par->block_align);
}

AVInputFormat ff_ace_demuxer = {
    .name           = "ace",
    .long_name      = NULL_IF_CONFIG_SMALL("tri-Ace Audio Container"),
    .read_probe     = ace_probe,
    .read_header    = ace_read_header,
    .read_packet    = ace_read_packet,
    .flags          = AVFMT_GENERIC_INDEX,
};
