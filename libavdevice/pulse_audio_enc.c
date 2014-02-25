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
    int buffer_size;
    int buffer_duration;
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

    if (s->buffer_duration) {
        int64_t bytes = s->buffer_duration;
        bytes *= st->codec->channels * st->codec->sample_rate *
                 av_get_bytes_per_sample(st->codec->sample_fmt);
        bytes /= 1000;
        attr.tlength = FFMAX(s->buffer_size, av_clip64(bytes, 0, UINT32_MAX - 1));
        av_log(s, AV_LOG_DEBUG,
               "Buffer duration: %ums recalculated into %"PRId64" bytes buffer.\n",
               s->buffer_duration, bytes);
        av_log(s, AV_LOG_DEBUG, "Real buffer length is %u bytes\n", attr.tlength);
    } else if (s->buffer_size)
        attr.tlength = s->buffer_size;

    ss.format = ff_codec_id_to_pulse_format(st->codec->codec_id);
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
        return 1;
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
    pa_usec_t latency = pa_simple_get_latency(s->pa, NULL);
    *wall = av_gettime();
    *dts = s->timestamp - latency;
}

static int pulse_get_device_list(AVFormatContext *h, AVDeviceInfoList *device_list)
{
    PulseData *s = h->priv_data;
    return ff_pulse_audio_get_devices(device_list, s->server, 1);
}

#define OFFSET(a) offsetof(PulseData, a)
#define E AV_OPT_FLAG_ENCODING_PARAM

static const AVOption options[] = {
    { "server",        "set PulseAudio server",  OFFSET(server),      AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, E },
    { "name",          "set application name",   OFFSET(name),        AV_OPT_TYPE_STRING, {.str = LIBAVFORMAT_IDENT},  0, 0, E },
    { "stream_name",   "set stream description", OFFSET(stream_name), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, E },
    { "device",        "set device name",        OFFSET(device),      AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, E },
    { "buffer_size",   "set buffer size in bytes", OFFSET(buffer_size), AV_OPT_TYPE_INT,  {.i64 = 0}, 0, INT_MAX, E },
    { "buffer_duration", "set buffer duration in millisecs", OFFSET(buffer_duration), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, E },
    { NULL }
};

static const AVClass pulse_muxer_class = {
    .class_name     = "Pulse muxer",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
    .category       = AV_CLASS_CATEGORY_DEVICE_AUDIO_OUTPUT,
};

AVOutputFormat ff_pulse_muxer = {
    .name           = "pulse",
    .long_name      = NULL_IF_CONFIG_SMALL("Pulse audio output"),
    .priv_data_size = sizeof(PulseData),
    .audio_codec    = AV_NE(AV_CODEC_ID_PCM_S16BE, AV_CODEC_ID_PCM_S16LE),
    .video_codec    = AV_CODEC_ID_NONE,
    .write_header   = pulse_write_header,
    .write_packet   = pulse_write_packet,
    .write_uncoded_frame = pulse_write_frame,
    .write_trailer  = pulse_write_trailer,
    .get_output_timestamp = pulse_get_output_timestamp,
    .get_device_list = pulse_get_device_list,
    .flags          = AVFMT_NOFILE | AVFMT_ALLOW_FLUSH,
    .priv_class     = &pulse_muxer_class,
};
