/*
 * Pulseaudio input
 * Copyright (c) 2011 Luca Barbato <lu_zero@gentoo.org>
 * Copyright 2004-2006 Lennart Poettering
 * Copyright (c) 2014 Michael Niedermayer <michaelni@gmx.at>
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

#include <pulse/rtclock.h>
#include <pulse/error.h>

#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"

#include "libavformat/avformat.h"
#include "libavformat/internal.h"
#include "pulse_audio_common.h"
#include "timefilter.h"

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

    pa_threaded_mainloop *mainloop;
    pa_context *context;
    pa_stream *stream;

    TimeFilter *timefilter;
    int last_period;
    int wallclock;
} PulseData;


#define CHECK_SUCCESS_GOTO(rerror, expression, label)        \
    do {                                                        \
        if (!(expression)) {                                    \
            rerror = AVERROR_EXTERNAL;                          \
            goto label;                                         \
        }                                                       \
    } while (0)

#define CHECK_DEAD_GOTO(p, rerror, label)                               \
    do {                                                                \
        if (!(p)->context || !PA_CONTEXT_IS_GOOD(pa_context_get_state((p)->context)) || \
            !(p)->stream || !PA_STREAM_IS_GOOD(pa_stream_get_state((p)->stream))) { \
            rerror = AVERROR_EXTERNAL;                                  \
            goto label;                                                 \
        }                                                               \
    } while (0)

static void context_state_cb(pa_context *c, void *userdata) {
    PulseData *p = userdata;

    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_READY:
        case PA_CONTEXT_TERMINATED:
        case PA_CONTEXT_FAILED:
            pa_threaded_mainloop_signal(p->mainloop, 0);
            break;
    }
}

static void stream_state_cb(pa_stream *s, void * userdata) {
    PulseData *p = userdata;

    switch (pa_stream_get_state(s)) {
        case PA_STREAM_READY:
        case PA_STREAM_FAILED:
        case PA_STREAM_TERMINATED:
            pa_threaded_mainloop_signal(p->mainloop, 0);
            break;
    }
}

static void stream_request_cb(pa_stream *s, size_t length, void *userdata) {
    PulseData *p = userdata;

    pa_threaded_mainloop_signal(p->mainloop, 0);
}

static void stream_latency_update_cb(pa_stream *s, void *userdata) {
    PulseData *p = userdata;

    pa_threaded_mainloop_signal(p->mainloop, 0);
}

static av_cold int pulse_close(AVFormatContext *s)
{
    PulseData *pd = s->priv_data;

    if (pd->mainloop)
        pa_threaded_mainloop_stop(pd->mainloop);

    if (pd->stream)
        pa_stream_unref(pd->stream);
    pd->stream = NULL;

    if (pd->context) {
        pa_context_disconnect(pd->context);
        pa_context_unref(pd->context);
    }
    pd->context = NULL;

    if (pd->mainloop)
        pa_threaded_mainloop_free(pd->mainloop);
    pd->mainloop = NULL;

    ff_timefilter_destroy(pd->timefilter);
    pd->timefilter = NULL;

    return 0;
}

static av_cold int pulse_read_header(AVFormatContext *s)
{
    PulseData *pd = s->priv_data;
    AVStream *st;
    char *device = NULL;
    int ret;
    enum AVCodecID codec_id =
        s->audio_codec_id == AV_CODEC_ID_NONE ? DEFAULT_CODEC_ID : s->audio_codec_id;
    const pa_sample_spec ss = { ff_codec_id_to_pulse_format(codec_id),
                                pd->sample_rate,
                                pd->channels };

    pa_buffer_attr attr = { -1 };

    st = avformat_new_stream(s, NULL);

    if (!st) {
        av_log(s, AV_LOG_ERROR, "Cannot add stream\n");
        return AVERROR(ENOMEM);
    }

    attr.fragsize = pd->fragment_size;

    if (s->filename[0] != '\0' && strcmp(s->filename, "default"))
        device = s->filename;

    if (!(pd->mainloop = pa_threaded_mainloop_new())) {
        pulse_close(s);
        return AVERROR_EXTERNAL;
    }

    if (!(pd->context = pa_context_new(pa_threaded_mainloop_get_api(pd->mainloop), pd->name))) {
        pulse_close(s);
        return AVERROR_EXTERNAL;
    }

    pa_context_set_state_callback(pd->context, context_state_cb, pd);

    if (pa_context_connect(pd->context, pd->server, 0, NULL) < 0) {
        pulse_close(s);
        return AVERROR(pa_context_errno(pd->context));
    }

    pa_threaded_mainloop_lock(pd->mainloop);

    if (pa_threaded_mainloop_start(pd->mainloop) < 0) {
        ret = -1;
        goto unlock_and_fail;
    }

    for (;;) {
        pa_context_state_t state;

        state = pa_context_get_state(pd->context);

        if (state == PA_CONTEXT_READY)
            break;

        if (!PA_CONTEXT_IS_GOOD(state)) {
            ret = AVERROR(pa_context_errno(pd->context));
            goto unlock_and_fail;
        }

        /* Wait until the context is ready */
        pa_threaded_mainloop_wait(pd->mainloop);
    }

    if (!(pd->stream = pa_stream_new(pd->context, pd->stream_name, &ss, NULL))) {
        ret = AVERROR(pa_context_errno(pd->context));
        goto unlock_and_fail;
    }

    pa_stream_set_state_callback(pd->stream, stream_state_cb, pd);
    pa_stream_set_read_callback(pd->stream, stream_request_cb, pd);
    pa_stream_set_write_callback(pd->stream, stream_request_cb, pd);
    pa_stream_set_latency_update_callback(pd->stream, stream_latency_update_cb, pd);

    ret = pa_stream_connect_record(pd->stream, device, &attr,
                                    PA_STREAM_INTERPOLATE_TIMING
                                    |PA_STREAM_ADJUST_LATENCY
                                    |PA_STREAM_AUTO_TIMING_UPDATE);

    if (ret < 0) {
        ret = AVERROR(pa_context_errno(pd->context));
        goto unlock_and_fail;
    }

    for (;;) {
        pa_stream_state_t state;

        state = pa_stream_get_state(pd->stream);

        if (state == PA_STREAM_READY)
            break;

        if (!PA_STREAM_IS_GOOD(state)) {
            ret = AVERROR(pa_context_errno(pd->context));
            goto unlock_and_fail;
        }

        /* Wait until the stream is ready */
        pa_threaded_mainloop_wait(pd->mainloop);
    }

    pa_threaded_mainloop_unlock(pd->mainloop);

    /* take real parameters */
    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id    = codec_id;
    st->codecpar->sample_rate = pd->sample_rate;
    st->codecpar->channels    = pd->channels;
    avpriv_set_pts_info(st, 64, 1, 1000000);  /* 64 bits pts in us */

    pd->timefilter = ff_timefilter_new(1000000.0 / pd->sample_rate,
                                       1000, 1.5E-6);

    if (!pd->timefilter) {
        pulse_close(s);
        return AVERROR(ENOMEM);
    }

    return 0;

unlock_and_fail:
    pa_threaded_mainloop_unlock(pd->mainloop);

    pulse_close(s);
    return ret;
}

static int pulse_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    PulseData *pd  = s->priv_data;
    int ret;
    size_t read_length;
    const void *read_data = NULL;
    int64_t dts;
    pa_usec_t latency;
    int negative;

    pa_threaded_mainloop_lock(pd->mainloop);

    CHECK_DEAD_GOTO(pd, ret, unlock_and_fail);

    while (!read_data) {
        int r;

        r = pa_stream_peek(pd->stream, &read_data, &read_length);
        CHECK_SUCCESS_GOTO(ret, r == 0, unlock_and_fail);

        if (read_length <= 0) {
            pa_threaded_mainloop_wait(pd->mainloop);
            CHECK_DEAD_GOTO(pd, ret, unlock_and_fail);
        } else if (!read_data) {
            /* There's a hole in the stream, skip it. We could generate
                * silence, but that wouldn't work for compressed streams. */
            r = pa_stream_drop(pd->stream);
            CHECK_SUCCESS_GOTO(ret, r == 0, unlock_and_fail);
        }
    }

    if (av_new_packet(pkt, read_length) < 0) {
        ret = AVERROR(ENOMEM);
        goto unlock_and_fail;
    }

    dts = av_gettime();
    pa_operation_unref(pa_stream_update_timing_info(pd->stream, NULL, NULL));

    if (pa_stream_get_latency(pd->stream, &latency, &negative) >= 0) {
        enum AVCodecID codec_id =
            s->audio_codec_id == AV_CODEC_ID_NONE ? DEFAULT_CODEC_ID : s->audio_codec_id;
        int frame_size = ((av_get_bits_per_sample(codec_id) >> 3) * pd->channels);
        int frame_duration = read_length / frame_size;


        if (negative) {
            dts += latency;
        } else
            dts -= latency;
        if (pd->wallclock)
            pkt->pts = ff_timefilter_update(pd->timefilter, dts, pd->last_period);

        pd->last_period = frame_duration;
    } else {
        av_log(s, AV_LOG_WARNING, "pa_stream_get_latency() failed\n");
    }

    memcpy(pkt->data, read_data, read_length);
    pa_stream_drop(pd->stream);

    pa_threaded_mainloop_unlock(pd->mainloop);
    return 0;

unlock_and_fail:
    pa_threaded_mainloop_unlock(pd->mainloop);
    return ret;
}

static int pulse_get_device_list(AVFormatContext *h, AVDeviceInfoList *device_list)
{
    PulseData *s = h->priv_data;
    return ff_pulse_audio_get_devices(device_list, s->server, 0);
}

#define OFFSET(a) offsetof(PulseData, a)
#define D AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    { "server",        "set PulseAudio server",                             OFFSET(server),        AV_OPT_TYPE_STRING, {.str = NULL},     0, 0, D },
    { "name",          "set application name",                              OFFSET(name),          AV_OPT_TYPE_STRING, {.str = LIBAVFORMAT_IDENT},  0, 0, D },
    { "stream_name",   "set stream description",                            OFFSET(stream_name),   AV_OPT_TYPE_STRING, {.str = "record"}, 0, 0, D },
    { "sample_rate",   "set sample rate in Hz",                             OFFSET(sample_rate),   AV_OPT_TYPE_INT,    {.i64 = 48000},    1, INT_MAX, D },
    { "channels",      "set number of audio channels",                      OFFSET(channels),      AV_OPT_TYPE_INT,    {.i64 = 2},        1, INT_MAX, D },
    { "frame_size",    "set number of bytes per frame",                     OFFSET(frame_size),    AV_OPT_TYPE_INT,    {.i64 = 1024},     1, INT_MAX, D },
    { "fragment_size", "set buffering size, affects latency and cpu usage", OFFSET(fragment_size), AV_OPT_TYPE_INT,    {.i64 = -1},      -1, INT_MAX, D },
    { "wallclock",     "set the initial pts using the current time",     OFFSET(wallclock),     AV_OPT_TYPE_INT,    {.i64 = 1},       -1, 1, D },
    { NULL },
};

static const AVClass pulse_demuxer_class = {
    .class_name     = "Pulse demuxer",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
    .category       = AV_CLASS_CATEGORY_DEVICE_AUDIO_INPUT,
};

AVInputFormat ff_pulse_demuxer = {
    .name           = "pulse",
    .long_name      = NULL_IF_CONFIG_SMALL("Pulse audio input"),
    .priv_data_size = sizeof(PulseData),
    .read_header    = pulse_read_header,
    .read_packet    = pulse_read_packet,
    .read_close     = pulse_close,
    .get_device_list = pulse_get_device_list,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &pulse_demuxer_class,
};
