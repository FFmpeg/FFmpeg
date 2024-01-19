/*
 * Linux audio play interface
 * Copyright (c) 2000, 2001 Fabrice Bellard
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

#include "config.h"

#include <stdint.h>

#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>

#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"

#include "avdevice.h"
#include "libavformat/internal.h"

#include "oss.h"

static int audio_read_header(AVFormatContext *s1)
{
    OSSAudioData *s = s1->priv_data;
    AVStream *st;
    int ret;

    st = avformat_new_stream(s1, NULL);
    if (!st) {
        return AVERROR(ENOMEM);
    }

    ret = ff_oss_audio_open(s1, 0, s1->url);
    if (ret < 0) {
        return AVERROR(EIO);
    }

    /* take real parameters */
    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id = s->codec_id;
    st->codecpar->sample_rate = s->sample_rate;
    st->codecpar->ch_layout.nb_channels = s->channels;

    avpriv_set_pts_info(st, 64, 1, 1000000);  /* 64 bits pts in us */
    return 0;
}

static int audio_read_packet(AVFormatContext *s1, AVPacket *pkt)
{
    OSSAudioData *s = s1->priv_data;
    int ret, bdelay;
    int64_t cur_time;
    struct audio_buf_info abufi;

    if ((ret=av_new_packet(pkt, s->frame_size)) < 0)
        return ret;

    ret = read(s->fd, pkt->data, pkt->size);
    if (ret <= 0){
        av_packet_unref(pkt);
        pkt->size = 0;
        if (ret<0)  return AVERROR(errno);
        else        return AVERROR_EOF;
    }
    pkt->size = ret;

    /* compute pts of the start of the packet */
    cur_time = av_gettime();
    bdelay = ret;
    if (ioctl(s->fd, SNDCTL_DSP_GETISPACE, &abufi) == 0) {
        bdelay += abufi.bytes;
    }
    /* subtract time represented by the number of bytes in the audio fifo */
    cur_time -= (bdelay * 1000000LL) / (s->sample_rate * s->sample_size * s->channels);

    /* convert to wanted units */
    pkt->pts = cur_time;

    if (s->flip_left && s->channels == 2) {
        int i;
        short *p = (short *) pkt->data;

        for (i = 0; i < ret; i += 4) {
            *p = ~*p;
            p += 2;
        }
    }
    return 0;
}

static int audio_read_close(AVFormatContext *s1)
{
    OSSAudioData *s = s1->priv_data;

    ff_oss_audio_close(s);
    return 0;
}

static const AVOption options[] = {
    { "sample_rate", "", offsetof(OSSAudioData, sample_rate), AV_OPT_TYPE_INT, {.i64 = 48000}, 1, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { "channels",    "", offsetof(OSSAudioData, channels),    AV_OPT_TYPE_INT, {.i64 = 2},     1, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

static const AVClass oss_demuxer_class = {
    .class_name     = "OSS indev",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
    .category       = AV_CLASS_CATEGORY_DEVICE_AUDIO_INPUT,
};

const AVInputFormat ff_oss_demuxer = {
    .name           = "oss",
    .long_name      = NULL_IF_CONFIG_SMALL("OSS (Open Sound System) capture"),
    .priv_data_size = sizeof(OSSAudioData),
    .read_header    = audio_read_header,
    .read_packet    = audio_read_packet,
    .read_close     = audio_read_close,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &oss_demuxer_class,
};
