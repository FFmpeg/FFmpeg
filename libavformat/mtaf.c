/*
 * MTAF demuxer
 * Copyright (c) 2016 Paul B Mahol
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
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "demux.h"
#include "internal.h"

static int mtaf_probe(const AVProbeData *p)
{
    if (p->buf_size < 0x44)
        return 0;

    if (AV_RL32(p->buf) != MKTAG('M','T','A','F') ||
        AV_RL32(p->buf + 0x40) != MKTAG('H','E','A','D'))
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int mtaf_read_header(AVFormatContext *s)
{
    int stream_count;
    AVStream *st;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    avio_skip(s->pb, 0x5c);
    st->duration = avio_rl32(s->pb);
    avio_skip(s->pb, 1);
    stream_count = avio_r8(s->pb);
    if (!stream_count)
        return AVERROR_INVALIDDATA;

    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id    = AV_CODEC_ID_ADPCM_MTAF;
    st->codecpar->ch_layout.nb_channels = 2 * stream_count;
    st->codecpar->sample_rate = 48000;
    st->codecpar->block_align = 0x110 * st->codecpar->ch_layout.nb_channels / 2;
    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

    avio_seek(s->pb, 0x800, SEEK_SET);

    return 0;
}

static int mtaf_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVCodecParameters *par = s->streams[0]->codecpar;

    return av_get_packet(s->pb, pkt, par->block_align);
}

const FFInputFormat ff_mtaf_demuxer = {
    .p.name         = "mtaf",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Konami PS2 MTAF"),
    .p.extensions   = "mtaf",
    .read_probe     = mtaf_probe,
    .read_header    = mtaf_read_header,
    .read_packet    = mtaf_read_packet,
};
