/*
 * ALSA input and output
 * Copyright (c) 2007 Luca Abeni ( lucabe72 email it )
 * Copyright (c) 2007 Benoit Fouet ( benoit fouet free fr )
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
#include "libavformat/internal.h"
#include "libavutil/opt.h"
#include "libavutil/mathematics.h"

#include "avdevice.h"
#include "alsa-audio.h"

static av_cold int audio_read_header(AVFormatContext *s1,
                                     AVFormatParameters *ap)
{
    AlsaData *s = s1->priv_data;
    AVStream *st;
    int ret;
    enum CodecID codec_id;
    double o;

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

    /* take real parameters */
    st->codec->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id    = codec_id;
    st->codec->sample_rate = s->sample_rate;
    st->codec->channels    = s->channels;
    avpriv_set_pts_info(st, 64, 1, 1000000);  /* 64 bits pts in us */
    o = 2 * M_PI * s->period_size / s->sample_rate * 1.5; // bandwidth: 1.5Hz
    s->timefilter = ff_timefilter_new(1000000.0 / s->sample_rate,
                                      sqrt(2 * o), o * o);
    if (!s->timefilter)
        goto fail;

    return 0;

fail:
    snd_pcm_close(s->h);
    return AVERROR(EIO);
}

static int audio_read_packet(AVFormatContext *s1, AVPacket *pkt)
{
    AlsaData *s  = s1->priv_data;
    int res;
    int64_t dts;
    snd_pcm_sframes_t delay = 0;

    if (av_new_packet(pkt, s->period_size * s->frame_size) < 0) {
        return AVERROR(EIO);
    }

    while ((res = snd_pcm_readi(s->h, pkt->data, s->period_size)) < 0) {
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
        ff_timefilter_reset(s->timefilter);
    }

    dts = av_gettime();
    snd_pcm_delay(s->h, &delay);
    dts -= av_rescale(delay + res, 1000000, s->sample_rate);
    pkt->pts = ff_timefilter_update(s->timefilter, dts, res);

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
