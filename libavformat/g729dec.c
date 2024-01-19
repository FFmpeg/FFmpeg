/*
 * G.729 raw format demuxer
 * Copyright (c) 2011 Vladimir Voroshilov
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

#include "libavutil/log.h"
#include "libavutil/opt.h"

#include "avformat.h"
#include "internal.h"

typedef struct G729DemuxerContext {
    AVClass *class;

    int bit_rate;
} G729DemuxerContext;

static int g729_read_header(AVFormatContext *s)
{
    AVStream* st;
    G729DemuxerContext *s1 = s->priv_data;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id    = AV_CODEC_ID_G729;
    st->codecpar->sample_rate = 8000;
    st->codecpar->ch_layout   = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;

    if (s1 && s1->bit_rate)
        s->bit_rate = s1->bit_rate;

    switch(s->bit_rate) {
    case 6400:
        st->codecpar->block_align = 8;
        break;
    case 8000:
        st->codecpar->block_align = 10;
        break;
    default:
        av_log(s, AV_LOG_ERROR, "Invalid bit_rate value %"PRId64". "
               "Only 6400 and 8000 b/s are supported.", s->bit_rate);
        return AVERROR(EINVAL);
    }

    avpriv_set_pts_info(st, 64, 80, st->codecpar->sample_rate);

    return 0;
}

static int g729_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVStream *st = s->streams[0];
    int ret = av_get_packet(s->pb, pkt, st->codecpar->block_align);
    if (ret < 0)
        return ret;

    pkt->stream_index = 0;
    pkt->dts = pkt->pts = pkt->pos / st->codecpar->block_align;
    pkt->duration = 1;

    return 0;
}

#define OFFSET(x) offsetof(G729DemuxerContext, x)
static const AVOption g729_options[] = {
    { "bit_rate", "", OFFSET(bit_rate), AV_OPT_TYPE_INT, { .i64 = 8000 }, 0, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

static const AVClass g729_demuxer_class = {
    .class_name = "g729 demuxer",
    .item_name  = av_default_item_name,
    .option     = g729_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const AVInputFormat ff_g729_demuxer = {
    .name           = "g729",
    .long_name      = NULL_IF_CONFIG_SMALL("G.729 raw format demuxer"),
    .priv_data_size = sizeof(G729DemuxerContext),
    .read_header    = g729_read_header,
    .read_packet    = g729_read_packet,
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "g729",
    .priv_class     = &g729_demuxer_class,
};
