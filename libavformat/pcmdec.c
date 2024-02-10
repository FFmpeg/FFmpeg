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

#include "config_components.h"

#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "avformat.h"
#include "demux.h"
#include "internal.h"
#include "pcm.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/avassert.h"

typedef struct PCMAudioDemuxerContext {
    AVClass *class;
    int sample_rate;
    AVChannelLayout ch_layout;
} PCMAudioDemuxerContext;

static int pcm_read_header(AVFormatContext *s)
{
    PCMAudioDemuxerContext *s1 = s->priv_data;
    AVCodecParameters *par;
    AVStream *st;
    uint8_t *mime_type = NULL;
    int ret;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    par = st->codecpar;

    par->codec_type  = AVMEDIA_TYPE_AUDIO;
    par->codec_id    = ffifmt(s->iformat)->raw_codec_id;
    par->sample_rate = s1->sample_rate;
    ret = av_channel_layout_copy(&par->ch_layout, &s1->ch_layout);
    if (ret < 0)
        return ret;

    av_opt_get(s->pb, "mime_type", AV_OPT_SEARCH_CHILDREN, &mime_type);
    if (mime_type && s->iformat->mime_type) {
        int rate = 0, channels = 0, little_endian = 0;
        const char *options;
        if (av_stristart(mime_type, s->iformat->mime_type, &options)) { /* audio/L16 */
            while (options = strchr(options, ';')) {
                options++;
                if (!rate)
                    sscanf(options, " rate=%d",     &rate);
                if (!channels)
                    sscanf(options, " channels=%d", &channels);
                if (!little_endian) {
                     char val[sizeof("little-endian")];
                     if (sscanf(options, " endianness=%13s", val) == 1) {
                         little_endian = strcmp(val, "little-endian") == 0;
                     }
                }
            }
            if (rate <= 0) {
                av_log(s, AV_LOG_ERROR,
                       "Invalid sample_rate found in mime_type \"%s\"\n",
                       mime_type);
                av_freep(&mime_type);
                return AVERROR_INVALIDDATA;
            }
            par->sample_rate = rate;
            if (channels > 0) {
                av_channel_layout_uninit(&par->ch_layout);
                par->ch_layout.nb_channels = channels;
            }
            if (little_endian)
                par->codec_id = AV_CODEC_ID_PCM_S16LE;
        }
    }
    av_freep(&mime_type);

    par->bits_per_coded_sample = av_get_bits_per_sample(par->codec_id);

    av_assert0(par->bits_per_coded_sample > 0);

    par->block_align = par->bits_per_coded_sample * par->ch_layout.nb_channels / 8;

    avpriv_set_pts_info(st, 64, 1, par->sample_rate);
    return 0;
}

static const AVOption pcm_options[] = {
    { "sample_rate", "", offsetof(PCMAudioDemuxerContext, sample_rate), AV_OPT_TYPE_INT, {.i64 = 44100}, 0, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { "ch_layout",   "", offsetof(PCMAudioDemuxerContext, ch_layout),   AV_OPT_TYPE_CHLAYOUT, {.str = "mono"}, 0, 0, AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};
static const AVClass pcm_demuxer_class = {
    .class_name = "pcm demuxer",
    .item_name  = av_default_item_name,
    .option     = pcm_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

#define PCMDEF_0(name_, long_name_, ext, codec, ...)
#define PCMDEF_1(name_, long_name_, ext, codec, ...)        \
const FFInputFormat ff_pcm_ ## name_ ## _demuxer = {        \
    .p.name         = #name_,                               \
    .p.long_name    = NULL_IF_CONFIG_SMALL(long_name_),     \
    .p.flags        = AVFMT_GENERIC_INDEX,                  \
    .p.extensions   = ext,                                  \
    .p.priv_class   = &pcm_demuxer_class,                   \
    .priv_data_size = sizeof(PCMAudioDemuxerContext),       \
    .read_header    = pcm_read_header,                      \
    .read_packet    = ff_pcm_read_packet,                   \
    .read_seek      = ff_pcm_read_seek,                     \
    .raw_codec_id   = codec,                                \
    __VA_ARGS__                                             \
};
#define PCMDEF_2(name, long_name, ext, codec, enabled, ...) \
    PCMDEF_ ## enabled(name, long_name, ext, codec, __VA_ARGS__)
#define PCMDEF_3(name, long_name, ext, codec, config, ...)  \
    PCMDEF_2(name, long_name, ext, codec, config,   __VA_ARGS__)
#define PCMDEF_EXT(name, long_name, ext, uppercase, ...)    \
    PCMDEF_3(name, long_name, ext, AV_CODEC_ID_PCM_ ## uppercase, \
             CONFIG_PCM_ ## uppercase ## _DEMUXER,  __VA_ARGS__)
#define PCMDEF(name, long_name, ext, uppercase)             \
    PCMDEF_EXT(name, long_name, ext, uppercase, )

PCMDEF(f64be, "PCM 64-bit floating-point big-endian",           NULL, F64BE)
PCMDEF(f64le, "PCM 64-bit floating-point little-endian",        NULL, F64LE)
PCMDEF(f32be, "PCM 32-bit floating-point big-endian",           NULL, F32BE)
PCMDEF(f32le, "PCM 32-bit floating-point little-endian",        NULL, F32LE)
PCMDEF(s32be, "PCM signed 32-bit big-endian",                   NULL, S32BE)
PCMDEF(s32le, "PCM signed 32-bit little-endian",                NULL, S32LE)
PCMDEF(s24be, "PCM signed 24-bit big-endian",                   NULL, S24BE)
PCMDEF(s24le, "PCM signed 24-bit little-endian",                NULL, S24LE)
PCMDEF_EXT(s16be, "PCM signed 16-bit big-endian",
           AV_NE("sw", NULL), S16BE, .p.mime_type = "audio/L16")
PCMDEF(s16le, "PCM signed 16-bit little-endian",   AV_NE(NULL, "sw"), S16LE)
PCMDEF(s8,    "PCM signed 8-bit",                               "sb",    S8)
PCMDEF(u32be, "PCM unsigned 32-bit big-endian",                 NULL, U32BE)
PCMDEF(u32le, "PCM unsigned 32-bit little-endian",              NULL, U32LE)
PCMDEF(u24be, "PCM unsigned 24-bit big-endian",                 NULL, U24BE)
PCMDEF(u24le, "PCM unsigned 24-bit little-endian",              NULL, U24LE)
PCMDEF(u16be, "PCM unsigned 16-bit big-endian",    AV_NE("uw", NULL), U16BE)
PCMDEF(u16le, "PCM unsigned 16-bit little-endian", AV_NE(NULL, "uw"), U16LE)
PCMDEF(u8,    "PCM unsigned 8-bit",                             "ub",    U8)
PCMDEF(alaw,  "PCM A-law",                                      "al",  ALAW)
PCMDEF(mulaw, "PCM mu-law",                                     "ul", MULAW)
PCMDEF(vidc,  "PCM Archimedes VIDC",                            NULL,  VIDC)

#if CONFIG_SLN_DEMUXER
static const AVOption sln_options[] = {
    { "sample_rate", "", offsetof(PCMAudioDemuxerContext, sample_rate), AV_OPT_TYPE_INT, {.i64 = 8000}, 0, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { "ch_layout",   "", offsetof(PCMAudioDemuxerContext, ch_layout),   AV_OPT_TYPE_CHLAYOUT, {.str = "mono"}, 0, 0, AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

static const AVClass sln_demuxer_class = {
    .class_name = "sln demuxer",
    .item_name  = av_default_item_name,
    .option     = sln_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFInputFormat ff_sln_demuxer = {
    .p.name         = "sln",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Asterisk raw pcm"),
    .p.flags        = AVFMT_GENERIC_INDEX,
    .p.extensions   = "sln",
    .p.priv_class   = &sln_demuxer_class,
    .priv_data_size = sizeof(PCMAudioDemuxerContext),
    .read_header    = pcm_read_header,
    .read_packet    = ff_pcm_read_packet,
    .read_seek      = ff_pcm_read_seek,
    .raw_codec_id   = AV_CODEC_ID_PCM_S16LE,
};
#endif
