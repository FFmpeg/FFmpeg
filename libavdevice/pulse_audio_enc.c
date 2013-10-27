/*
 * Copyright (c) 2013 Lukasz Marek <lukasz.m.luki@gmail.com>
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

#include <pulse/simple.h>
#include <pulse/error.h>
#include "libavformat/avformat.h"
#include "libavformat/internal.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavutil/log.h"
#include "pulse_audio_common.h"

typedef struct PulseData {
    AVClass *class;
    const char *server;
    const char *name;
    const char *stream_name;
    const char *device;
    pa_simple *pa;
    int64_t timestamp;
} PulseData;

static av_cold int pulse_write_header(AVFormatContext *h)
{
    PulseData *s = h->priv_data;
    AVStream *st = NULL;
    int ret;
    pa_sample_spec ss;
    pa_buffer_attr attr = { -1, -1, -1, -1, -1 };
    const char *stream_name = s->stream_name;

    if (h->nb_streams != 1 || h->streams[0]->codec->codec_type != AVMEDIA_TYPE_AUDIO) {
        av_log(s, AV_LOG_ERROR, "Only a single audio stream is supported.\n");
        return AVERROR(EINVAL);
    }
    st = h->streams[0];

    if (!stream_name) {
        if (h->filename[0])
            stream_name = h->filename;
        else
            stream_name = "Playback";
    }

    ss.format = codec_id_to_pulse_format(st->codec->codec_id);
    ss.rate = st->codec->sample_rate;
    ss.channels = st->codec->channels;

    s->pa = pa_simple_new(s->server,                 // Server
                          s->name,                   // Application name
                          PA_STREAM_PLAYBACK,
                          s->device,                 // Device
                          stream_name,               // Description of a stream
                          &ss,                       // Sample format
                          NULL,                      // Use default channel map
                          &attr,                     // Buffering attributes
                          &ret);                     // Result

    if (!s->pa) {
        av_log(s, AV_LOG_ERROR, "pa_simple_new failed: %s\n", pa_strerror(ret));
        return AVERROR(EIO);
    }

    avpriv_set_pts_info(st, 64, 1, 1000000);  /* 64 bits pts in us */

    return 0;
}

static av_cold int pulse_write_trailer(AVFormatContext *h)
{
    PulseData *s = h->priv_data;
    pa_simple_flush(s->pa, NULL);
    pa_simple_free(s->pa);
    s->pa = NULL;
    return 0;
}

static int pulse_write_packet(AVFormatContext *h, AVPacket *pkt)
{
    PulseData *s = h->priv_data;
    int error;

    if (!pkt) {
        if (pa_simple_flush(s->pa, &error) < 0) {
            av_log(s, AV_LOG_ERROR, "pa_simple_flush failed: %s\n", pa_strerror(error));
            return AVERROR(EIO);
        }
        return 0;
    }

    if (pkt->dts != AV_NOPTS_VALUE)
        s->timestamp = pkt->dts;

    if (pkt->duration) {
        s->timestamp += pkt->duration;
    } else {
        AVStream *st = h->streams[0];
        AVCodecContext *codec_ctx = st->codec;
        AVRational r = { 1, codec_ctx->sample_rate };
        int64_t samples = pkt->size / (av_get_bytes_per_sample(codec_ctx->sample_fmt) * codec_ctx->channels);
        s->timestamp += av_rescale_q(samples, r, st->time_base);
    }

    if (pa_simple_write(s->pa, pkt->data, pkt->size, &error) < 0) {
        av_log(s, AV_LOG_ERROR, "pa_simple_write failed: %s\n", pa_strerror(error));
        return AVERROR(EIO);
    }

    return 0;
}

static void pulse_get_output_timestamp(AVFormatContext *h, int stream, int64_t *dts, int64_t *wall)
{
    PulseData *s = h->priv_data;
    pa_usec_t latency = pa_simple_get_latency(s->pa, NULL);
    *wall = av_gettime();
    *dts = s->timestamp - latency;
}

#define OFFSET(a) offsetof(PulseData, a)
#define E AV_OPT_FLAG_ENCODING_PARAM

static const AVOption options[] = {
    { "server",        "set PulseAudio server",  OFFSET(server),      AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, E },
    { "name",          "set application name",   OFFSET(name),        AV_OPT_TYPE_STRING, {.str = LIBAVFORMAT_IDENT},  0, 0, E },
    { "stream_name",   "set stream description", OFFSET(stream_name), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, E },
    { "device",        "set device name",        OFFSET(device),      AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, E },
    { NULL }
};

static const AVClass pulse_muxer_class = {
    .class_name     = "Pulse muxer",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
};

AVOutputFormat ff_pulse_muxer = {
    .name           = "pulse",
    .long_name      = NULL_IF_CONFIG_SMALL("Pulse audio output"),
    .priv_data_size = sizeof(PulseData),
    .audio_codec    = AV_NE(AV_CODEC_ID_PCM_S16BE, AV_CODEC_ID_PCM_S16LE),
    .video_codec    = AV_CODEC_ID_NONE,
    .write_header   = pulse_write_header,
    .write_packet   = pulse_write_packet,
    .write_trailer  = pulse_write_trailer,
    .get_output_timestamp = pulse_get_output_timestamp,
    .flags          = AVFMT_NOFILE | AVFMT_ALLOW_FLUSH,
    .priv_class     = &pulse_muxer_class,
};
