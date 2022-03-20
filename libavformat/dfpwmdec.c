/*
 * RAW PCM demuxers
 * Copyright (c) 2002 Fabrice Bellard
 * Copyright (c) 2022 Jack Bruienne
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

#include "libavutil/avstring.h"
#include "avformat.h"
#include "internal.h"
#include "pcm.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/avassert.h"

typedef struct DFPWMAudioDemuxerContext {
    AVClass *class;
    int sample_rate;
#if FF_API_OLD_CHANNEL_LAYOUT
    int channels;
#endif
    AVChannelLayout ch_layout;
} DFPWMAudioDemuxerContext;

static int dfpwm_read_header(AVFormatContext *s)
{
    DFPWMAudioDemuxerContext *s1 = s->priv_data;
    AVCodecParameters *par;
    AVStream *st;
    int ret;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    par = st->codecpar;

    par->codec_type  = AVMEDIA_TYPE_AUDIO;
    par->codec_id    = s->iformat->raw_codec_id;
    par->sample_rate = s1->sample_rate;
#if FF_API_OLD_CHANNEL_LAYOUT
    if (s1->ch_layout.nb_channels) {
#endif
    ret = av_channel_layout_copy(&par->ch_layout, &s1->ch_layout);
    if (ret < 0)
        return ret;
#if FF_API_OLD_CHANNEL_LAYOUT
    } else
        par->ch_layout.nb_channels = s1->channels;
#endif
    par->bits_per_coded_sample = 1;
    par->block_align = 1;

    avpriv_set_pts_info(st, 64, 1, par->sample_rate);
    return 0;
}

static const AVOption dfpwm_options[] = {
    { "sample_rate", "", offsetof(DFPWMAudioDemuxerContext, sample_rate), AV_OPT_TYPE_INT, {.i64 = 48000}, 0, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
#if FF_API_OLD_CHANNEL_LAYOUT
    { "channels",    "", offsetof(DFPWMAudioDemuxerContext, channels),    AV_OPT_TYPE_INT, {.i64 = 1}, 0, INT_MAX, AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_DEPRECATED },
    { "ch_layout",   "", offsetof(DFPWMAudioDemuxerContext, ch_layout),   AV_OPT_TYPE_CHLAYOUT, {.str = NULL}, 0, 0, AV_OPT_FLAG_DECODING_PARAM },
#else
    { "ch_layout",   "", offsetof(DFPWMAudioDemuxerContext, ch_layout),   AV_OPT_TYPE_CHLAYOUT, {.str = "mono"}, 0, 0, AV_OPT_FLAG_DECODING_PARAM },
#endif
    { NULL },
};
static const AVClass dfpwm_demuxer_class = {
    .class_name = "dfpwm demuxer",
    .item_name  = av_default_item_name,
    .option     = dfpwm_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const AVInputFormat ff_dfpwm_demuxer = {
    .name           = "dfpwm",
    .long_name      = NULL_IF_CONFIG_SMALL("raw DFPWM1a"),
    .priv_data_size = sizeof(DFPWMAudioDemuxerContext),
    .read_header    = dfpwm_read_header,
    .read_packet    = ff_pcm_read_packet,
    .read_seek      = ff_pcm_read_seek,
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "dfpwm",
    .raw_codec_id   = AV_CODEC_ID_DFPWM,
    .priv_class     = &dfpwm_demuxer_class,
};
