/*
 * RAW PCM demuxers
 * Copyright (c) 2002 Fabrice Bellard
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

#include "avformat.h"
#include "internal.h"
#include "pcm.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/avassert.h"

typedef struct PCMAudioDemuxerContext {
    AVClass *class;
    int sample_rate;
    int channels;
} PCMAudioDemuxerContext;

static int pcm_read_header(AVFormatContext *s)
{
    PCMAudioDemuxerContext *s1 = s->priv_data;
    AVStream *st;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);


    st->codec->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id    = s->iformat->raw_codec_id;
    st->codec->sample_rate = s1->sample_rate;
    st->codec->channels    = s1->channels;

    st->codec->bits_per_coded_sample =
        av_get_bits_per_sample(st->codec->codec_id);

    av_assert0(st->codec->bits_per_coded_sample > 0);

    st->codec->block_align =
        st->codec->bits_per_coded_sample * st->codec->channels / 8;

    avpriv_set_pts_info(st, 64, 1, st->codec->sample_rate);
    return 0;
}

static const AVOption pcm_options[] = {
    { "sample_rate", "", offsetof(PCMAudioDemuxerContext, sample_rate), AV_OPT_TYPE_INT, {.i64 = 44100}, 0, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { "channels",    "", offsetof(PCMAudioDemuxerContext, channels),    AV_OPT_TYPE_INT, {.i64 = 1}, 0, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

#define PCMDEF(name_, long_name_, ext, codec)               \
static const AVClass name_ ## _demuxer_class = {            \
    .class_name = #name_ " demuxer",                        \
    .item_name  = av_default_item_name,                     \
    .option     = pcm_options,                              \
    .version    = LIBAVUTIL_VERSION_INT,                    \
};                                                          \
AVInputFormat ff_pcm_ ## name_ ## _demuxer = {              \
    .name           = #name_,                               \
    .long_name      = NULL_IF_CONFIG_SMALL(long_name_),     \
    .priv_data_size = sizeof(PCMAudioDemuxerContext),       \
    .read_header    = pcm_read_header,                      \
    .read_packet    = ff_pcm_read_packet,                   \
    .read_seek      = ff_pcm_read_seek,                     \
    .flags          = AVFMT_GENERIC_INDEX,                  \
    .extensions     = ext,                                  \
    .raw_codec_id   = codec,                                \
    .priv_class     = &name_ ## _demuxer_class,             \
};

PCMDEF(f64be, "PCM 64-bit floating-point big-endian",
       NULL, AV_CODEC_ID_PCM_F64BE)

PCMDEF(f64le, "PCM 64-bit floating-point little-endian",
       NULL, AV_CODEC_ID_PCM_F64LE)

PCMDEF(f32be, "PCM 32-bit floating-point big-endian",
       NULL, AV_CODEC_ID_PCM_F32BE)

PCMDEF(f32le, "PCM 32-bit floating-point little-endian",
       NULL, AV_CODEC_ID_PCM_F32LE)

PCMDEF(s32be, "PCM signed 32-bit big-endian",
       NULL, AV_CODEC_ID_PCM_S32BE)

PCMDEF(s32le, "PCM signed 32-bit little-endian",
       NULL, AV_CODEC_ID_PCM_S32LE)

PCMDEF(s24be, "PCM signed 24-bit big-endian",
       NULL, AV_CODEC_ID_PCM_S24BE)

PCMDEF(s24le, "PCM signed 24-bit little-endian",
       NULL, AV_CODEC_ID_PCM_S24LE)

PCMDEF(s16be, "PCM signed 16-bit big-endian",
       AV_NE("sw", NULL), AV_CODEC_ID_PCM_S16BE)

PCMDEF(s16le, "PCM signed 16-bit little-endian",
       AV_NE(NULL, "sw"), AV_CODEC_ID_PCM_S16LE)

PCMDEF(s8, "PCM signed 8-bit",
       "sb", AV_CODEC_ID_PCM_S8)

PCMDEF(u32be, "PCM unsigned 32-bit big-endian",
       NULL, AV_CODEC_ID_PCM_U32BE)

PCMDEF(u32le, "PCM unsigned 32-bit little-endian",
       NULL, AV_CODEC_ID_PCM_U32LE)

PCMDEF(u24be, "PCM unsigned 24-bit big-endian",
       NULL, AV_CODEC_ID_PCM_U24BE)

PCMDEF(u24le, "PCM unsigned 24-bit little-endian",
       NULL, AV_CODEC_ID_PCM_U24LE)

PCMDEF(u16be, "PCM unsigned 16-bit big-endian",
       AV_NE("uw", NULL), AV_CODEC_ID_PCM_U16BE)

PCMDEF(u16le, "PCM unsigned 16-bit little-endian",
       AV_NE(NULL, "uw"), AV_CODEC_ID_PCM_U16LE)

PCMDEF(u8, "PCM unsigned 8-bit",
       "ub", AV_CODEC_ID_PCM_U8)

PCMDEF(alaw, "PCM A-law",
       "al", AV_CODEC_ID_PCM_ALAW)

PCMDEF(mulaw, "PCM mu-law",
       "ul", AV_CODEC_ID_PCM_MULAW)

static const AVOption sln_options[] = {
    { "sample_rate", "", offsetof(PCMAudioDemuxerContext, sample_rate), AV_OPT_TYPE_INT, {.i64 = 8000}, 0, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { "channels",    "", offsetof(PCMAudioDemuxerContext, channels),    AV_OPT_TYPE_INT, {.i64 = 1}, 0, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

static const AVClass sln_demuxer_class = {
    .class_name = "sln demuxer",
    .item_name  = av_default_item_name,
    .option     = sln_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_sln_demuxer = {
    .name           = "sln",
    .long_name      = NULL_IF_CONFIG_SMALL("Asterisk raw pcm"),
    .priv_data_size = sizeof(PCMAudioDemuxerContext),
    .read_header    = pcm_read_header,
    .read_packet    = ff_pcm_read_packet,
    .read_seek      = ff_pcm_read_seek,
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "sln",
    .raw_codec_id   = AV_CODEC_ID_PCM_S16LE,
    .priv_class     = &sln_demuxer_class,
};
