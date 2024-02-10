/*
 * OSQ demuxer
 * Copyright (c) 2023 Paul B Mahol
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
#include "avio_internal.h"
#include "avformat.h"
#include "demux.h"
#include "internal.h"
#include "rawdec.h"

static int osq_probe(const AVProbeData *p)
{
    if (AV_RL32(p->buf) != MKTAG('O','S','Q',' '))
        return 0;
    if (AV_RL32(p->buf + 4) != 48)
        return 0;
    if (AV_RL16(p->buf + 8) != 1)
        return 0;
    if (p->buf[10] == 0)
        return 0;
    if (p->buf[11] == 0)
        return 0;
    if (AV_RL32(p->buf + 12) == 0)
        return 0;
    if (AV_RL16(p->buf + 16) == 0)
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int osq_read_header(AVFormatContext *s)
{
    uint32_t t, size;
    AVStream *st;
    int ret;

    t = avio_rl32(s->pb);
    if (t != MKTAG('O','S','Q',' '))
        return AVERROR_INVALIDDATA;

    size = avio_rl32(s->pb);
    if (size != 48)
        return AVERROR_INVALIDDATA;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    if ((ret = ff_get_extradata(s, st->codecpar, s->pb, size)) < 0)
        return ret;

    t = avio_rl32(s->pb);
    if (t != MKTAG('R','I','F','F'))
        return AVERROR_INVALIDDATA;
    avio_skip(s->pb, 8);

    t = avio_rl32(s->pb);
    if (t != MKTAG('f','m','t',' '))
        return AVERROR_INVALIDDATA;
    size = avio_rl32(s->pb);
    avio_skip(s->pb, size);

    t = avio_rl32(s->pb);
    size = avio_rl32(s->pb);
    while (t != MKTAG('d','a','t','a')) {
        avio_skip(s->pb, size);

        t = avio_rl32(s->pb);
        size = avio_rl32(s->pb);
        if (avio_feof(s->pb))
            return AVERROR_INVALIDDATA;
    }

    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id    = AV_CODEC_ID_OSQ;
    st->codecpar->sample_rate = AV_RL32(st->codecpar->extradata + 4);
    if (st->codecpar->sample_rate <= 0)
        return AVERROR_INVALIDDATA;
    st->codecpar->ch_layout.order = AV_CHANNEL_ORDER_UNSPEC;
    st->codecpar->ch_layout.nb_channels = st->codecpar->extradata[3];
    if (st->codecpar->ch_layout.nb_channels == 0)
        return AVERROR_INVALIDDATA;
    st->start_time = 0;
    st->duration = AV_RL32(st->codecpar->extradata + 16);
    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

    return 0;
}

const FFInputFormat ff_osq_demuxer = {
    .p.name         = "osq",
    .p.long_name    = NULL_IF_CONFIG_SMALL("raw OSQ"),
    .p.extensions   = "osq",
    .p.flags        = AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK | AVFMT_NOTIMESTAMPS,
    .p.priv_class   = &ff_raw_demuxer_class,
    .read_probe     = osq_probe,
    .read_header    = osq_read_header,
    .read_packet    = ff_raw_read_partial_packet,
    .raw_codec_id   = AV_CODEC_ID_OSQ,
    .priv_data_size = sizeof(FFRawDemuxerContext),
};
