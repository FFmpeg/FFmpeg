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
 * ALSA input and output: output
 * @author Luca Abeni ( lucabe72 email it )
 * @author Benoit Fouet ( benoit fouet free fr )
 *
 * This avdevice encoder allows to play audio to an ALSA (Advanced Linux
 * Sound Architecture) device.
 *
 * The filename parameter is the name of an ALSA PCM device capable of
 * capture, for example "default" or "plughw:1"; see the ALSA documentation
 * for naming conventions. The empty string is equivalent to "default".
 *
 * The playback period is set to the lower value available for the device,
 * which gives a low latency suitable for real-time playback.
 */

#include <alsa/asoundlib.h>

#include "libavutil/internal.h"

#include "libavformat/avformat.h"

#include "alsa.h"

static av_cold int audio_write_header(AVFormatContext *s1)
{
    AlsaData *s = s1->priv_data;
    AVStream *st;
    unsigned int sample_rate;
    enum AVCodecID codec_id;
    int res;

    st = s1->streams[0];
    sample_rate = st->codecpar->sample_rate;
    codec_id    = st->codecpar->codec_id;
    res = ff_alsa_open(s1, SND_PCM_STREAM_PLAYBACK, &sample_rate,
        st->codecpar->channels, &codec_id);
    if (sample_rate != st->codecpar->sample_rate) {
        av_log(s1, AV_LOG_ERROR,
               "sample rate %d not available, nearest is %d\n",
               st->codecpar->sample_rate, sample_rate);
        goto fail;
    }

    return res;

fail:
    snd_pcm_close(s->h);
    return AVERROR(EIO);
}

static int audio_write_packet(AVFormatContext *s1, AVPacket *pkt)
{
    AlsaData *s = s1->priv_data;
    int res;
    int size     = pkt->size;
    uint8_t *buf = pkt->data;

    size /= s->frame_size;
    if (s->reorder_func) {
        if (size > s->reorder_buf_size)
            if (ff_alsa_extend_reorder_buf(s, size))
                return AVERROR(ENOMEM);
        s->reorder_func(buf, s->reorder_buf, size);
        buf = s->reorder_buf;
    }
    while ((res = snd_pcm_writei(s->h, buf, size)) < 0) {
        if (res == -EAGAIN) {

            return AVERROR(EAGAIN);
        }

        if (ff_alsa_xrun_recover(s1, res) < 0) {
            av_log(s1, AV_LOG_ERROR, "ALSA write error: %s\n",
                   snd_strerror(res));

            return AVERROR(EIO);
        }
    }

    return 0;
}

AVOutputFormat ff_alsa_muxer = {
    .name           = "alsa",
    .long_name      = NULL_IF_CONFIG_SMALL("ALSA audio output"),
    .priv_data_size = sizeof(AlsaData),
    .audio_codec    = DEFAULT_CODEC_ID,
    .video_codec    = AV_CODEC_ID_NONE,
    .write_header   = audio_write_header,
    .write_packet   = audio_write_packet,
    .write_trailer  = ff_alsa_close,
    .flags          = AVFMT_NOFILE,
};
