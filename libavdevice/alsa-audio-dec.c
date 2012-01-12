/*
 * ALSA input and output
 * Copyright (c) 2007 Luca Abeni ( lucabe72 email it )
 * Copyright (c) 2007 Benoit Fouet ( benoit fouet free fr )
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * ALSA input and output: input
 * @author Luca Abeni ( lucabe72 email it )
 * @author Benoit Fouet ( benoit fouet free fr )
 * @author Nicolas George ( nicolas george normalesup org )
 *
 * This avdevice decoder allows to capture audio from an ALSA (Advanced
 * Linux Sound Architecture) device.
 *
 * The filename parameter is the name of an ALSA PCM device capable of
 * capture, for example "default" or "plughw:1"; see the ALSA documentation
 * for naming conventions. The empty string is equivalent to "default".
 *
 * The capture period is set to the lower value available for the device,
 * which gives a low latency suitable for real-time capture.
 *
 * The PTS are an Unix time in microsecond.
 *
 * Due to a bug in the ALSA library
 * (https://bugtrack.alsa-project.org/alsa-bug/view.php?id=4308), this
 * decoder does not work with certain ALSA plugins, especially the dsnoop
 * plugin.
 */

#include <alsa/asoundlib.h>
#include "libavformat/avformat.h"
#include "libavformat/internal.h"
#include "libavutil/opt.h"

#include "alsa-audio.h"

static av_cold int audio_read_header(AVFormatContext *s1)
{
    AlsaData *s = s1->priv_data;
    AVStream *st;
    int ret;
    enum CodecID codec_id;
    snd_pcm_sw_params_t *sw_params;

    st = avformat_new_stream(s1, NULL);
    if (!st) {
        av_log(s1, AV_LOG_ERROR, "Cannot add stream\n");

        return AVERROR(ENOMEM);
    }
    codec_id    = s1->audio_codec_id;

    ret = ff_alsa_open(s1, SND_PCM_STREAM_CAPTURE, &s->sample_rate, s->channels,
        &codec_id);
    if (ret < 0) {
        return AVERROR(EIO);
    }

    if (snd_pcm_type(s->h) != SND_PCM_TYPE_HW)
        av_log(s1, AV_LOG_WARNING,
               "capture with some ALSA plugins, especially dsnoop, "
               "may hang.\n");

    ret = snd_pcm_sw_params_malloc(&sw_params);
    if (ret < 0) {
        av_log(s1, AV_LOG_ERROR, "cannot allocate software parameters structure (%s)\n",
               snd_strerror(ret));
        goto fail;
    }

    snd_pcm_sw_params_current(s->h, sw_params);
    snd_pcm_sw_params_set_tstamp_mode(s->h, sw_params, SND_PCM_TSTAMP_ENABLE);

    ret = snd_pcm_sw_params(s->h, sw_params);
    snd_pcm_sw_params_free(sw_params);
    if (ret < 0) {
        av_log(s1, AV_LOG_ERROR, "cannot install ALSA software parameters (%s)\n",
               snd_strerror(ret));
        goto fail;
    }

    /* take real parameters */
    st->codec->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id    = codec_id;
    st->codec->sample_rate = s->sample_rate;
    st->codec->channels    = s->channels;
    avpriv_set_pts_info(st, 64, 1, 1000000);  /* 64 bits pts in us */

    return 0;

fail:
    snd_pcm_close(s->h);
    return AVERROR(EIO);
}

static int audio_read_packet(AVFormatContext *s1, AVPacket *pkt)
{
    AlsaData *s  = s1->priv_data;
    AVStream *st = s1->streams[0];
    int res;
    snd_htimestamp_t timestamp;
    snd_pcm_uframes_t ts_delay;

    if (av_new_packet(pkt, s->period_size) < 0) {
        return AVERROR(EIO);
    }

    while ((res = snd_pcm_readi(s->h, pkt->data, pkt->size / s->frame_size)) < 0) {
        if (res == -EAGAIN) {
            av_free_packet(pkt);

            return AVERROR(EAGAIN);
        }
        if (ff_alsa_xrun_recover(s1, res) < 0) {
            av_log(s1, AV_LOG_ERROR, "ALSA read error: %s\n",
                   snd_strerror(res));
            av_free_packet(pkt);

            return AVERROR(EIO);
        }
    }

    snd_pcm_htimestamp(s->h, &ts_delay, &timestamp);
    ts_delay += res;
    pkt->pts = timestamp.tv_sec * 1000000LL
               + (timestamp.tv_nsec * st->codec->sample_rate
                  - ts_delay * 1000000000LL + st->codec->sample_rate * 500LL)
               / (st->codec->sample_rate * 1000LL);

    pkt->size = res * s->frame_size;

    return 0;
}

static const AVOption options[] = {
    { "sample_rate", "", offsetof(AlsaData, sample_rate), AV_OPT_TYPE_INT, {.dbl = 48000}, 1, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { "channels",    "", offsetof(AlsaData, channels),    AV_OPT_TYPE_INT, {.dbl = 2},     1, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

static const AVClass alsa_demuxer_class = {
    .class_name     = "ALSA demuxer",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_alsa_demuxer = {
    .name           = "alsa",
    .long_name      = NULL_IF_CONFIG_SMALL("ALSA audio input"),
    .priv_data_size = sizeof(AlsaData),
    .read_header    = audio_read_header,
    .read_packet    = audio_read_packet,
    .read_close     = ff_alsa_close,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &alsa_demuxer_class,
};
