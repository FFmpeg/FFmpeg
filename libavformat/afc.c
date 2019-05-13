/*
 * AFC demuxer
 * Copyright (c) 2012 Paul B Mahol
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
#include "internal.h"

typedef struct AFCDemuxContext {
    int64_t    data_end;
} AFCDemuxContext;

static int afc_read_header(AVFormatContext *s)
{
    AFCDemuxContext *c = s->priv_data;
    AVStream *st;
    int ret;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id   = AV_CODEC_ID_ADPCM_AFC;
    st->codecpar->ch_layout  = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;

    if ((ret = ff_alloc_extradata(st->codecpar, 1)) < 0)
        return ret;
    st->codecpar->extradata[0] = 8 * st->codecpar->ch_layout.nb_channels;

    c->data_end = avio_rb32(s->pb) + 32LL;
    st->duration = avio_rb32(s->pb);
    st->codecpar->sample_rate = avio_rb16(s->pb);
    avio_skip(s->pb, 22);
    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

    return 0;
}

static int afc_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AFCDemuxContext *c = s->priv_data;
    int64_t size = c->data_end - avio_tell(s->pb);
    int ret;

    size = FFMIN(size, 18 * 128);
    if (size <= 0)
        return AVERROR_EOF;

    ret = av_get_packet(s->pb, pkt, size);
    pkt->stream_index = 0;
    return ret;
}

const AVInputFormat ff_afc_demuxer = {
    .name           = "afc",
    .long_name      = NULL_IF_CONFIG_SMALL("AFC"),
    .priv_data_size = sizeof(AFCDemuxContext),
    .read_header    = afc_read_header,
    .read_packet    = afc_read_packet,
    .extensions     = "afc",
    .flags          = AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK,
};
