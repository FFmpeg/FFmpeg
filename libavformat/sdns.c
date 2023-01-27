/*
 * SDNS demuxer
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
#include "demux.h"
#include "internal.h"

static int sdns_probe(const AVProbeData *p)
{
    if (AV_RL32(p->buf) != MKTAG('S','D','N','S'))
        return 0;
    if (AV_RB32(p->buf + 8) <= 0)
        return 0;
    if (AV_RB32(p->buf + 12) <= 0 ||
        AV_RB32(p->buf + 12) > 128)
        return 0;
    return AVPROBE_SCORE_MAX / 3;
}

static int sdns_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    AVCodecParameters *par;
    int channels, ret;
    AVStream *st;

    avio_skip(pb, 8);

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    par              = st->codecpar;
    par->codec_type  = AVMEDIA_TYPE_AUDIO;
    par->codec_id    = AV_CODEC_ID_XMA1;
    par->sample_rate = avio_rb32(pb);
    channels         = avio_rb32(pb);
    if (channels <= 0 || channels > 128)
        return AVERROR_INVALIDDATA;
    av_channel_layout_default(&par->ch_layout, channels);
    if (par->sample_rate <= 0)
        return AVERROR_INVALIDDATA;
    par->block_align = 2048;
    if ((ret = ff_alloc_extradata(par, 8 + 20 * ((channels + 1) / 2))) < 0)
        return ret;
    memset(par->extradata, 0, 28);
    par->extradata[4] = (channels + 1) / 2;
    for (int i = 0; i < par->extradata[4]; i++)
        par->extradata[8 + 20 * i + 17] = FFMIN(2, channels - i * 2);
    avpriv_set_pts_info(st, 64, 1, par->sample_rate);
    avio_seek(pb, 0x1000, SEEK_SET);

    return 0;
}

static int sdns_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret;

    if (avio_feof(s->pb))
        return AVERROR_EOF;
    ret = av_get_packet(s->pb, pkt, 2048);
    pkt->stream_index = 0;

    return ret;
}

const AVInputFormat ff_sdns_demuxer = {
    .name           = "sdns",
    .long_name      = NULL_IF_CONFIG_SMALL("Xbox SDNS"),
    .read_probe     = sdns_probe,
    .read_header    = sdns_read_header,
    .read_packet    = sdns_read_packet,
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "sdns",
};
