/*
 * Pulseaudio input
 * Copyright (c) 2011 Luca Barbato <lu_zero@gentoo.org>
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

/**
 * @file
 * PulseAudio input using the simple API.
 * @author Luca Barbato <lu_zero@gentoo.org>
 */

#include <pulse/simple.h>
#include <pulse/rtclock.h>
#include <pulse/error.h>

#include "libavformat/avformat.h"
#include "libavformat/internal.h"
#include "libavutil/opt.h"

#define DEFAULT_CODEC_ID AV_NE(AV_CODEC_ID_PCM_S16BE, AV_CODEC_ID_PCM_S16LE)

typedef struct PulseData {
    AVClass *class;
    char *server;
    char *name;
    char *stream_name;
    int  sample_rate;
    int  channels;
    int  frame_size;
    int  fragment_size;
    pa_simple *s;
    int64_t pts;
    int64_t frame_duration;
} PulseData;

static pa_sample_format_t codec_id_to_pulse_format(int codec_id) {
    switch (codec_id) {
    case AV_CODEC_ID_PCM_U8:    return PA_SAMPLE_U8;
    case AV_CODEC_ID_PCM_ALAW:  return PA_SAMPLE_ALAW;
    case AV_CODEC_ID_PCM_MULAW: return PA_SAMPLE_ULAW;
    case AV_CODEC_ID_PCM_S16LE: return PA_SAMPLE_S16LE;
    case AV_CODEC_ID_PCM_S16BE: return PA_SAMPLE_S16BE;
    case AV_CODEC_ID_PCM_F32LE: return PA_SAMPLE_FLOAT32LE;
    case AV_CODEC_ID_PCM_F32BE: return PA_SAMPLE_FLOAT32BE;
    case AV_CODEC_ID_PCM_S32LE: return PA_SAMPLE_S32LE;
    case AV_CODEC_ID_PCM_S32BE: return PA_SAMPLE_S32BE;
    case AV_CODEC_ID_PCM_S24LE: return PA_SAMPLE_S24LE;
    case AV_CODEC_ID_PCM_S24BE: return PA_SAMPLE_S24BE;
    default:                 return PA_SAMPLE_INVALID;
    }
}

static av_cold int pulse_read_header(AVFormatContext *s)
{
    PulseData *pd = s->priv_data;
    AVStream *st;
    char *device = NULL;
    int ret;
    enum AVCodecID codec_id =
        s->audio_codec_id == AV_CODEC_ID_NONE ? DEFAULT_CODEC_ID : s->audio_codec_id;
    const pa_sample_spec ss = { codec_id_to_pulse_format(codec_id),
                                pd->sample_rate,
                                pd->channels };

    pa_buffer_attr attr = { -1 };

    st = avformat_new_stream(s, NULL);

    if (!st) {
        av_log(s, AV_LOG_ERROR, "Cannot add stream\n");
        return AVERROR(ENOMEM);
    }

    attr.fragsize = pd->fragment_size;

    if (strcmp(s->filename, "default"))
        device = s->filename;

    pd->s = pa_simple_new(pd->server, pd->name,
                          PA_STREAM_RECORD,
                          device, pd->stream_name, &ss,
                          NULL, &attr, &ret);

    if (!pd->s) {
        av_log(s, AV_LOG_ERROR, "pa_simple_new failed: %s\n",
               pa_strerror(ret));
        return AVERROR(EIO);
    }
    /* take real parameters */
    st->codec->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id    = codec_id;
    st->codec->sample_rate = pd->sample_rate;
    st->codec->channels    = pd->channels;
    avpriv_set_pts_info(st, 64, 1, 1000000);  /* 64 bits pts in us */

    pd->pts = AV_NOPTS_VALUE;
    pd->frame_duration = (pd->frame_size * 1000000LL * 8) /
        (pd->sample_rate * pd->channels * av_get_bits_per_sample(codec_id));

    return 0;
}

static int pulse_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    PulseData *pd  = s->priv_data;
    int res;
    pa_usec_t latency;

    if (av_new_packet(pkt, pd->frame_size) < 0) {
        return AVERROR(ENOMEM);
    }

    if ((pa_simple_read(pd->s, pkt->data, pkt->size, &res)) < 0) {
        av_log(s, AV_LOG_ERROR, "pa_simple_read failed: %s\n",
               pa_strerror(res));
        av_free_packet(pkt);
        return AVERROR(EIO);
    }

    if ((latency = pa_simple_get_latency(pd->s, &res)) == (pa_usec_t) -1) {
        av_log(s, AV_LOG_ERROR, "pa_simple_get_latency() failed: %s\n",
               pa_strerror(res));
        return AVERROR(EIO);
    }

    if (pd->pts == AV_NOPTS_VALUE) {
        pd->pts = -latency;
    }

    pkt->pts = pd->pts;

    pd->pts += pd->frame_duration;

    return 0;
}

static av_cold int pulse_close(AVFormatContext *s)
{
    PulseData *pd = s->priv_data;
    pa_simple_free(pd->s);
    return 0;
}

#define OFFSET(a) offsetof(PulseData, a)
#define D AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    { "server",        "pulse server name",                              OFFSET(server),        AV_OPT_TYPE_STRING, {.str = NULL},     0, 0, D },
    { "name",          "application name",                               OFFSET(name),          AV_OPT_TYPE_STRING, {.str = LIBAVFORMAT_IDENT},  0, 0, D },
    { "stream_name",   "stream description",                             OFFSET(stream_name),   AV_OPT_TYPE_STRING, {.str = "record"}, 0, 0, D },
    { "sample_rate",   "sample rate in Hz",                              OFFSET(sample_rate),   AV_OPT_TYPE_INT,    {.i64 = 48000},    1, INT_MAX, D },
    { "channels",      "number of audio channels",                       OFFSET(channels),      AV_OPT_TYPE_INT,    {.i64 = 2},        1, INT_MAX, D },
    { "frame_size",    "number of bytes per frame",                      OFFSET(frame_size),    AV_OPT_TYPE_INT,    {.i64 = 1024},     1, INT_MAX, D },
    { "fragment_size", "buffering size, affects latency and cpu usage",  OFFSET(fragment_size), AV_OPT_TYPE_INT,    {.i64 = -1},      -1, INT_MAX, D },
    { NULL },
};

static const AVClass pulse_demuxer_class = {
    .class_name     = "Pulse demuxer",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_pulse_demuxer = {
    .name           = "pulse",
    .long_name      = NULL_IF_CONFIG_SMALL("Pulse audio input"),
    .priv_data_size = sizeof(PulseData),
    .read_header    = pulse_read_header,
    .read_packet    = pulse_read_packet,
    .read_close     = pulse_close,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &pulse_demuxer_class,
};
