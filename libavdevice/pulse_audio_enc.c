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

#include <pulse/pulseaudio.h>
#include <pulse/error.h>
#include "libavformat/avformat.h"
#include "libavformat/internal.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavutil/log.h"
#include "libavutil/attributes.h"
#include "pulse_audio_common.h"

typedef struct PulseData {
    AVClass *class;
    const char *server;
    const char *name;
    const char *stream_name;
    const char *device;
    int64_t timestamp;
    int buffer_size;               /**< Buffer size in bytes */
    int buffer_duration;           /**< Buffer size in ms, recalculated to buffer_size */
    int last_result;
    pa_threaded_mainloop *mainloop;
    pa_context *ctx;
    pa_stream *stream;
    int nonblocking;
} PulseData;

static void pulse_stream_writable(pa_stream *stream, size_t nbytes, void *userdata)
{
    AVFormatContext *h = userdata;
    PulseData *s = h->priv_data;
    int64_t val = nbytes;

    if (stream != s->stream)
        return;

    avdevice_dev_to_app_control_message(h, AV_DEV_TO_APP_BUFFER_WRITABLE, &val, sizeof(val));
    pa_threaded_mainloop_signal(s->mainloop, 0);
}

static void pulse_overflow(pa_stream *stream, void *userdata)
{
    AVFormatContext *h = userdata;
    avdevice_dev_to_app_control_message(h, AV_DEV_TO_APP_BUFFER_OVERFLOW, NULL, 0);
}

static void pulse_underflow(pa_stream *stream, void *userdata)
{
    AVFormatContext *h = userdata;
    avdevice_dev_to_app_control_message(h, AV_DEV_TO_APP_BUFFER_UNDERFLOW, NULL, 0);
}

static void pulse_stream_state(pa_stream *stream, void *userdata)
{
    PulseData *s = userdata;

    if (stream != s->stream)
        return;

    switch (pa_stream_get_state(s->stream)) {
        case PA_STREAM_READY:
        case PA_STREAM_FAILED:
        case PA_STREAM_TERMINATED:
            pa_threaded_mainloop_signal(s->mainloop, 0);
        default:
            break;
    }
}

static int pulse_stream_wait(PulseData *s)
{
    pa_stream_state_t state;

    while ((state = pa_stream_get_state(s->stream)) != PA_STREAM_READY) {
        if (state == PA_STREAM_FAILED || state == PA_STREAM_TERMINATED)
            return AVERROR_EXTERNAL;
        pa_threaded_mainloop_wait(s->mainloop);
    }
    return 0;
}

static void pulse_context_state(pa_context *ctx, void *userdata)
{
    PulseData *s = userdata;

    if (s->ctx != ctx)
        return;

    switch (pa_context_get_state(ctx)) {
        case PA_CONTEXT_READY:
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            pa_threaded_mainloop_signal(s->mainloop, 0);
        default:
            break;
    }
}

static int pulse_context_wait(PulseData *s)
{
    pa_context_state_t state;

    while ((state = pa_context_get_state(s->ctx)) != PA_CONTEXT_READY) {
        if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED)
            return AVERROR_EXTERNAL;
        pa_threaded_mainloop_wait(s->mainloop);
    }
    return 0;
}

static void pulse_stream_result(pa_stream *stream, int success, void *userdata)
{
    PulseData *s = userdata;

    if (stream != s->stream)
        return;

    s->last_result = success ? 0 : AVERROR_EXTERNAL;
    pa_threaded_mainloop_signal(s->mainloop, 0);
}

static int pulse_finish_stream_operation(PulseData *s, pa_operation *op, const char *name)
{
    if (!op) {
        pa_threaded_mainloop_unlock(s->mainloop);
        av_log(s, AV_LOG_ERROR, "%s failed.\n", name);
        return AVERROR_EXTERNAL;
    }
    s->last_result = 2;
    while (s->last_result == 2)
        pa_threaded_mainloop_wait(s->mainloop);
    pa_operation_unref(op);
    pa_threaded_mainloop_unlock(s->mainloop);
    if (s->last_result != 0)
        av_log(s, AV_LOG_ERROR, "%s failed.\n", name);
    return s->last_result;
}

static int pulse_flash_stream(PulseData *s)
{
    pa_operation *op;
    pa_threaded_mainloop_lock(s->mainloop);
    op = pa_stream_flush(s->stream, pulse_stream_result, s);
    return pulse_finish_stream_operation(s, op, "pa_stream_flush");
}

static void pulse_map_channels_to_pulse(int64_t channel_layout, pa_channel_map *channel_map)
{
    channel_map->channels = 0;
    if (channel_layout & AV_CH_FRONT_LEFT)
        channel_map->map[channel_map->channels++] = PA_CHANNEL_POSITION_FRONT_LEFT;
    if (channel_layout & AV_CH_FRONT_RIGHT)
        channel_map->map[channel_map->channels++] = PA_CHANNEL_POSITION_FRONT_RIGHT;
    if (channel_layout & AV_CH_FRONT_CENTER)
        channel_map->map[channel_map->channels++] = PA_CHANNEL_POSITION_FRONT_CENTER;
    if (channel_layout & AV_CH_LOW_FREQUENCY)
        channel_map->map[channel_map->channels++] = PA_CHANNEL_POSITION_LFE;
    if (channel_layout & AV_CH_BACK_LEFT)
        channel_map->map[channel_map->channels++] = PA_CHANNEL_POSITION_REAR_LEFT;
    if (channel_layout & AV_CH_BACK_RIGHT)
        channel_map->map[channel_map->channels++] = PA_CHANNEL_POSITION_REAR_RIGHT;
    if (channel_layout & AV_CH_FRONT_LEFT_OF_CENTER)
        channel_map->map[channel_map->channels++] = PA_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER;
    if (channel_layout & AV_CH_FRONT_RIGHT_OF_CENTER)
        channel_map->map[channel_map->channels++] = PA_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER;
    if (channel_layout & AV_CH_BACK_CENTER)
        channel_map->map[channel_map->channels++] = PA_CHANNEL_POSITION_REAR_CENTER;
    if (channel_layout & AV_CH_SIDE_LEFT)
        channel_map->map[channel_map->channels++] = PA_CHANNEL_POSITION_SIDE_LEFT;
    if (channel_layout & AV_CH_SIDE_RIGHT)
        channel_map->map[channel_map->channels++] = PA_CHANNEL_POSITION_SIDE_RIGHT;
    if (channel_layout & AV_CH_TOP_CENTER)
        channel_map->map[channel_map->channels++] = PA_CHANNEL_POSITION_TOP_CENTER;
    if (channel_layout & AV_CH_TOP_FRONT_LEFT)
        channel_map->map[channel_map->channels++] = PA_CHANNEL_POSITION_TOP_FRONT_LEFT;
    if (channel_layout & AV_CH_TOP_FRONT_CENTER)
        channel_map->map[channel_map->channels++] = PA_CHANNEL_POSITION_TOP_FRONT_CENTER;
    if (channel_layout & AV_CH_TOP_FRONT_RIGHT)
        channel_map->map[channel_map->channels++] = PA_CHANNEL_POSITION_TOP_FRONT_RIGHT;
    if (channel_layout & AV_CH_TOP_BACK_LEFT)
        channel_map->map[channel_map->channels++] = PA_CHANNEL_POSITION_TOP_REAR_LEFT;
    if (channel_layout & AV_CH_TOP_BACK_CENTER)
        channel_map->map[channel_map->channels++] = PA_CHANNEL_POSITION_TOP_REAR_CENTER;
    if (channel_layout & AV_CH_TOP_BACK_RIGHT)
        channel_map->map[channel_map->channels++] = PA_CHANNEL_POSITION_TOP_REAR_RIGHT;
    if (channel_layout & AV_CH_STEREO_LEFT)
        channel_map->map[channel_map->channels++] = PA_CHANNEL_POSITION_FRONT_LEFT;
    if (channel_layout & AV_CH_STEREO_RIGHT)
        channel_map->map[channel_map->channels++] = PA_CHANNEL_POSITION_FRONT_RIGHT;
    if (channel_layout & AV_CH_WIDE_LEFT)
        channel_map->map[channel_map->channels++] = PA_CHANNEL_POSITION_AUX0;
    if (channel_layout & AV_CH_WIDE_RIGHT)
        channel_map->map[channel_map->channels++] = PA_CHANNEL_POSITION_AUX1;
    if (channel_layout & AV_CH_SURROUND_DIRECT_LEFT)
        channel_map->map[channel_map->channels++] = PA_CHANNEL_POSITION_AUX2;
    if (channel_layout & AV_CH_SURROUND_DIRECT_RIGHT)
        channel_map->map[channel_map->channels++] = PA_CHANNEL_POSITION_AUX3;
    if (channel_layout & AV_CH_LOW_FREQUENCY_2)
        channel_map->map[channel_map->channels++] = PA_CHANNEL_POSITION_LFE;
}

static av_cold int pulse_write_trailer(AVFormatContext *h)
{
    PulseData *s = h->priv_data;

    if (s->mainloop) {
        pa_threaded_mainloop_lock(s->mainloop);
        if (s->stream) {
            pa_stream_disconnect(s->stream);
            pa_stream_set_state_callback(s->stream, NULL, NULL);
            pa_stream_set_write_callback(s->stream, NULL, NULL);
            pa_stream_set_overflow_callback(s->stream, NULL, NULL);
            pa_stream_set_underflow_callback(s->stream, NULL, NULL);
            pa_stream_unref(s->stream);
            s->stream = NULL;
        }
        if (s->ctx) {
            pa_context_disconnect(s->ctx);
            pa_context_set_state_callback(s->ctx, NULL, NULL);
            pa_context_unref(s->ctx);
            s->ctx = NULL;
        }
        pa_threaded_mainloop_unlock(s->mainloop);
        pa_threaded_mainloop_stop(s->mainloop);
        pa_threaded_mainloop_free(s->mainloop);
        s->mainloop = NULL;
    }

    return 0;
}

static av_cold int pulse_write_header(AVFormatContext *h)
{
    PulseData *s = h->priv_data;
    AVStream *st = NULL;
    int ret;
    pa_sample_spec sample_spec;
    pa_buffer_attr buffer_attributes = { -1, -1, -1, -1, -1 };
    pa_channel_map channel_map;
    pa_mainloop_api *mainloop_api;
    const char *stream_name = s->stream_name;
    static const pa_stream_flags_t stream_flags = PA_STREAM_INTERPOLATE_TIMING |
                                                  PA_STREAM_AUTO_TIMING_UPDATE |
                                                  PA_STREAM_NOT_MONOTONIC;

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
    s->nonblocking = (h->flags & AVFMT_FLAG_NONBLOCK);

    if (s->buffer_duration) {
        int64_t bytes = s->buffer_duration;
        bytes *= st->codec->channels * st->codec->sample_rate *
                 av_get_bytes_per_sample(st->codec->sample_fmt);
        bytes /= 1000;
        buffer_attributes.tlength = FFMAX(s->buffer_size, av_clip64(bytes, 0, UINT32_MAX - 1));
        av_log(s, AV_LOG_DEBUG,
               "Buffer duration: %ums recalculated into %"PRId64" bytes buffer.\n",
               s->buffer_duration, bytes);
        av_log(s, AV_LOG_DEBUG, "Real buffer length is %u bytes\n", buffer_attributes.tlength);
    } else if (s->buffer_size)
        buffer_attributes.tlength = s->buffer_size;

    sample_spec.format = ff_codec_id_to_pulse_format(st->codec->codec_id);
    sample_spec.rate = st->codec->sample_rate;
    sample_spec.channels = st->codec->channels;
    if (!pa_sample_spec_valid(&sample_spec)) {
        av_log(s, AV_LOG_ERROR, "Invalid sample spec.\n");
        return AVERROR(EINVAL);
    }

    if (sample_spec.channels == 1) {
        channel_map.channels = 1;
        channel_map.map[0] = PA_CHANNEL_POSITION_MONO;
    } else if (st->codec->channel_layout) {
        if (av_get_channel_layout_nb_channels(st->codec->channel_layout) != st->codec->channels)
            return AVERROR(EINVAL);
        pulse_map_channels_to_pulse(st->codec->channel_layout, &channel_map);
        /* Unknown channel is present in channel_layout, let PulseAudio use its default. */
        if (channel_map.channels != sample_spec.channels) {
            av_log(s, AV_LOG_WARNING, "Unknown channel. Using defaul channel map.\n");
            channel_map.channels = 0;
        }
    } else
        channel_map.channels = 0;

    if (!channel_map.channels)
        av_log(s, AV_LOG_WARNING, "Using PulseAudio's default channel map.\n");
    else if (!pa_channel_map_valid(&channel_map)) {
        av_log(s, AV_LOG_ERROR, "Invalid channel map.\n");
        return AVERROR(EINVAL);
    }

    /* start main loop */
    s->mainloop = pa_threaded_mainloop_new();
    if (!s->mainloop) {
        av_log(s, AV_LOG_ERROR, "Cannot create threaded mainloop.\n");
        return AVERROR(ENOMEM);
    }
    if ((ret = pa_threaded_mainloop_start(s->mainloop)) < 0) {
        av_log(s, AV_LOG_ERROR, "Cannot start threaded mainloop: %s.\n", pa_strerror(ret));
        pa_threaded_mainloop_free(s->mainloop);
        s->mainloop = NULL;
        return AVERROR_EXTERNAL;
    }

    pa_threaded_mainloop_lock(s->mainloop);

    mainloop_api = pa_threaded_mainloop_get_api(s->mainloop);
    if (!mainloop_api) {
        av_log(s, AV_LOG_ERROR, "Cannot get mainloop API.\n");
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    s->ctx = pa_context_new(mainloop_api, s->name);
    if (!s->ctx) {
        av_log(s, AV_LOG_ERROR, "Cannot create context.\n");
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    pa_context_set_state_callback(s->ctx, pulse_context_state, s);

    if ((ret = pa_context_connect(s->ctx, s->server, 0, NULL)) < 0) {
        av_log(s, AV_LOG_ERROR, "Cannot connect context: %s.\n", pa_strerror(ret));
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    if ((ret = pulse_context_wait(s)) < 0) {
        av_log(s, AV_LOG_ERROR, "Context failed.\n");
        goto fail;
    }

    s->stream = pa_stream_new(s->ctx, stream_name, &sample_spec,
                              channel_map.channels ? &channel_map : NULL);
    if (!s->stream) {
        av_log(s, AV_LOG_ERROR, "Cannot create stream.\n");
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    pa_stream_set_state_callback(s->stream, pulse_stream_state, s);
    pa_stream_set_write_callback(s->stream, pulse_stream_writable, h);
    pa_stream_set_overflow_callback(s->stream, pulse_overflow, h);
    pa_stream_set_underflow_callback(s->stream, pulse_underflow, h);

    if ((ret = pa_stream_connect_playback(s->stream, s->device, &buffer_attributes,
                                          stream_flags, NULL, NULL)) < 0) {
        av_log(s, AV_LOG_ERROR, "pa_stream_connect_playback failed: %s.\n", pa_strerror(ret));
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    if ((ret = pulse_stream_wait(s)) < 0) {
        av_log(s, AV_LOG_ERROR, "Stream failed.\n");
        goto fail;
    }

    pa_threaded_mainloop_unlock(s->mainloop);

    avpriv_set_pts_info(st, 64, 1, 1000000);  /* 64 bits pts in us */

    return 0;
  fail:
    pa_threaded_mainloop_unlock(s->mainloop);
    pulse_write_trailer(h);
    return ret;
}

static int pulse_write_packet(AVFormatContext *h, AVPacket *pkt)
{
    PulseData *s = h->priv_data;
    int ret;

    if (!pkt)
        return pulse_flash_stream(s);

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

    pa_threaded_mainloop_lock(s->mainloop);
    if (!PA_STREAM_IS_GOOD(pa_stream_get_state(s->stream))) {
        av_log(s, AV_LOG_ERROR, "PulseAudio stream is in invalid state.\n");
        goto fail;
    }
    while (!pa_stream_writable_size(s->stream)) {
        if (s->nonblocking) {
            pa_threaded_mainloop_unlock(s->mainloop);
            return AVERROR(EAGAIN);
        } else
            pa_threaded_mainloop_wait(s->mainloop);
    }

    if ((ret = pa_stream_write(s->stream, pkt->data, pkt->size, NULL, 0, PA_SEEK_RELATIVE)) < 0) {
        av_log(s, AV_LOG_ERROR, "pa_stream_write failed: %s\n", pa_strerror(ret));
        goto fail;
    }
    pa_threaded_mainloop_unlock(s->mainloop);

    return 0;
  fail:
    pa_threaded_mainloop_unlock(s->mainloop);
    return AVERROR_EXTERNAL;
}

static int pulse_write_frame(AVFormatContext *h, int stream_index,
                             AVFrame **frame, unsigned flags)
{
    AVPacket pkt;

    /* Planar formats are not supported yet. */
    if (flags & AV_WRITE_UNCODED_FRAME_QUERY)
        return av_sample_fmt_is_planar(h->streams[stream_index]->codec->sample_fmt) ?
               AVERROR(EINVAL) : 0;

    pkt.data     = (*frame)->data[0];
    pkt.size     = (*frame)->nb_samples * av_get_bytes_per_sample((*frame)->format) * (*frame)->channels;
    pkt.dts      = (*frame)->pkt_dts;
    pkt.duration = av_frame_get_pkt_duration(*frame);
    return pulse_write_packet(h, &pkt);
}


static void pulse_get_output_timestamp(AVFormatContext *h, int stream, int64_t *dts, int64_t *wall)
{
    PulseData *s = h->priv_data;
    pa_usec_t latency;
    int neg;
    pa_threaded_mainloop_lock(s->mainloop);
    pa_stream_get_latency(s->stream, &latency, &neg);
    pa_threaded_mainloop_unlock(s->mainloop);
    *wall = av_gettime();
    *dts = s->timestamp - (neg ? -latency : latency);
}

static int pulse_get_device_list(AVFormatContext *h, AVDeviceInfoList *device_list)
{
    PulseData *s = h->priv_data;
    return ff_pulse_audio_get_devices(device_list, s->server, 1);
}

#define OFFSET(a) offsetof(PulseData, a)
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "server",          "set PulseAudio server",            OFFSET(server),          AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, E },
    { "name",            "set application name",             OFFSET(name),            AV_OPT_TYPE_STRING, {.str = LIBAVFORMAT_IDENT},  0, 0, E },
    { "stream_name",     "set stream description",           OFFSET(stream_name),     AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, E },
    { "device",          "set device name",                  OFFSET(device),          AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, E },
    { "buffer_size",     "set buffer size in bytes",         OFFSET(buffer_size),     AV_OPT_TYPE_INT,    {.i64 = 0}, 0, INT_MAX, E },
    { "buffer_duration", "set buffer duration in millisecs", OFFSET(buffer_duration), AV_OPT_TYPE_INT,    {.i64 = 0}, 0, INT_MAX, E },
    { NULL }
};

static const AVClass pulse_muxer_class = {
    .class_name     = "PulseAudio muxer",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
    .category       = AV_CLASS_CATEGORY_DEVICE_AUDIO_OUTPUT,
};

AVOutputFormat ff_pulse_muxer = {
    .name                 = "pulse",
    .long_name            = NULL_IF_CONFIG_SMALL("Pulse audio output"),
    .priv_data_size       = sizeof(PulseData),
    .audio_codec          = AV_NE(AV_CODEC_ID_PCM_S16BE, AV_CODEC_ID_PCM_S16LE),
    .video_codec          = AV_CODEC_ID_NONE,
    .write_header         = pulse_write_header,
    .write_packet         = pulse_write_packet,
    .write_uncoded_frame  = pulse_write_frame,
    .write_trailer        = pulse_write_trailer,
    .get_output_timestamp = pulse_get_output_timestamp,
    .get_device_list      = pulse_get_device_list,
    .flags                = AVFMT_NOFILE | AVFMT_ALLOW_FLUSH,
    .priv_class           = &pulse_muxer_class,
};
