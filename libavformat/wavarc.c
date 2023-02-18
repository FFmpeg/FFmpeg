/*
 * WavArc demuxer
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

#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "avio_internal.h"
#include "demux.h"
#include "internal.h"

typedef struct WavArcContext {
    int64_t data_end;
} WavArcContext;

static int wavarc_probe(const AVProbeData *p)
{
    int len = p->buf[0];
    uint32_t id;

    if (len == 0 || len + 6 >= p->buf_size)
        return 0;

    if (p->buf[len + 1] != 0)
        return 0;

    id = AV_RL32(p->buf + len + 2);
    if (id != MKTAG('0','C','P','Y') &&
        id != MKTAG('1','D','I','F') &&
        id != MKTAG('2','S','L','P') &&
        id != MKTAG('3','N','L','P') &&
        id != MKTAG('4','A','L','P') &&
        id != MKTAG('5','E','L','P'))
        return 0;

    return AVPROBE_SCORE_MAX / 3 * 2;
}

static int wavarc_read_header(AVFormatContext *s)
{
    WavArcContext *w = s->priv_data;
    AVIOContext *pb = s->pb;
    AVCodecParameters *par;
    int filename_len, fmt_len, ret;
    uint8_t data[36];
    AVStream *st;
    uint32_t id;

    filename_len = avio_r8(pb);
    if (filename_len == 0)
        return AVERROR_INVALIDDATA;
    avio_skip(pb, filename_len);
    if (avio_r8(pb))
        return AVERROR_INVALIDDATA;
    id = avio_rl32(pb);
    if (avio_read(pb, data, sizeof(data)) != sizeof(data))
        return AVERROR(EIO);
    fmt_len = AV_RL32(data + 32);
    if (fmt_len < 12)
        return AVERROR_INVALIDDATA;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    par = st->codecpar;

    ret = ff_alloc_extradata(par, fmt_len + sizeof(data));
    if (ret < 0)
        return ret;
    memcpy(par->extradata, data, sizeof(data));
    ret = ffio_read_size(pb, par->extradata + sizeof(data), fmt_len);
    if (ret < 0)
        return ret;

    par->codec_type = AVMEDIA_TYPE_AUDIO;
    par->codec_id   = AV_CODEC_ID_WAVARC;
    par->codec_tag  = id;

    do {
        id = avio_rl32(pb);
        if (id != MKTAG('d','a','t','a'))
            avio_skip(pb, avio_rl32(pb));
    } while (id != MKTAG('d','a','t','a') && !avio_feof(pb));
    w->data_end = avio_rl32(pb);
    w->data_end += avio_tell(pb);

    if (AV_RL32(par->extradata + 16) != MKTAG('R','I','F','F'))
        return AVERROR_INVALIDDATA;
    if (AV_RL32(par->extradata + 24) != MKTAG('W','A','V','E'))
        return AVERROR_INVALIDDATA;
    if (AV_RL32(par->extradata + 28) != MKTAG('f','m','t',' '))
        return AVERROR_INVALIDDATA;

    av_channel_layout_default(&par->ch_layout, AV_RL16(par->extradata + 38));
    par->sample_rate = AV_RL32(par->extradata + 40);
    avpriv_set_pts_info(st, 64, 1, par->sample_rate);
    st->start_time = 0;

    return 0;
}

static int wavarc_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    WavArcContext *w = s->priv_data;
    AVIOContext *pb = s->pb;
    int64_t size, left = w->data_end - avio_tell(pb);
    int ret;

    size = FFMIN(left, 1024);
    if (size <= 0)
        return AVERROR_EOF;

    ret = av_get_packet(pb, pkt, size);
    pkt->stream_index = 0;
    return ret;
}

const AVInputFormat ff_wavarc_demuxer = {
    .name           = "wavarc",
    .long_name      = NULL_IF_CONFIG_SMALL("Waveform Archiver"),
    .priv_data_size = sizeof(WavArcContext),
    .read_probe     = wavarc_probe,
    .read_packet    = wavarc_read_packet,
    .flags          = AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK | AVFMT_NOTIMESTAMPS,
    .read_header    = wavarc_read_header,
    .extensions     = "wa",
    .raw_codec_id   = AV_CODEC_ID_WAVARC,
};
