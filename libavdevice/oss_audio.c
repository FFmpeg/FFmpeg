/*
 * Linux audio play and grab interface
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
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#if HAVE_SOUNDCARD_H
#include <soundcard.h>
#else
#include <sys/soundcard.h>
#endif
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavcodec/avcodec.h"
#include "avdevice.h"
#include "libavformat/internal.h"

#define AUDIO_BLOCK_SIZE 4096

typedef struct {
    AVClass *class;
    int fd;
    int sample_rate;
    int channels;
    int frame_size; /* in bytes ! */
    enum AVCodecID codec_id;
    unsigned int flip_left : 1;
    uint8_t buffer[AUDIO_BLOCK_SIZE];
    int buffer_ptr;
} AudioData;

static int audio_open(AVFormatContext *s1, int is_output, const char *audio_device)
{
    AudioData *s = s1->priv_data;
    int audio_fd;
    int tmp, err;
    char *flip = getenv("AUDIO_FLIP_LEFT");

    if (is_output)
        audio_fd = open(audio_device, O_WRONLY);
    else
        audio_fd = open(audio_device, O_RDONLY);
    if (audio_fd < 0) {
        av_log(s1, AV_LOG_ERROR, "%s: %s\n", audio_device, strerror(errno));
        return AVERROR(EIO);
    }

    if (flip && *flip == '1') {
        s->flip_left = 1;
    }

    /* non blocking mode */
    if (!is_output) {
        if (fcntl(audio_fd, F_SETFL, O_NONBLOCK) < 0) {
            av_log(s1, AV_LOG_WARNING, "%s: Could not enable non block mode (%s)\n", audio_device, strerror(errno));
        }
    }

    s->frame_size = AUDIO_BLOCK_SIZE;

    /* select format : favour native format */
    err = ioctl(audio_fd, SNDCTL_DSP_GETFMTS, &tmp);

#if HAVE_BIGENDIAN
    if (tmp & AFMT_S16_BE) {
        tmp = AFMT_S16_BE;
    } else if (tmp & AFMT_S16_LE) {
        tmp = AFMT_S16_LE;
    } else {
        tmp = 0;
    }
#else
    if (tmp & AFMT_S16_LE) {
        tmp = AFMT_S16_LE;
    } else if (tmp & AFMT_S16_BE) {
        tmp = AFMT_S16_BE;
    } else {
        tmp = 0;
    }
#endif

    switch(tmp) {
    case AFMT_S16_LE:
        s->codec_id = AV_CODEC_ID_PCM_S16LE;
        break;
    case AFMT_S16_BE:
        s->codec_id = AV_CODEC_ID_PCM_S16BE;
        break;
    default:
        av_log(s1, AV_LOG_ERROR, "Soundcard does not support 16 bit sample format\n");
        close(audio_fd);
        return AVERROR(EIO);
    }
    err=ioctl(audio_fd, SNDCTL_DSP_SETFMT, &tmp);
    if (err < 0) {
        av_log(s1, AV_LOG_ERROR, "SNDCTL_DSP_SETFMT: %s\n", strerror(errno));
        goto fail;
    }

    tmp = (s->channels == 2);
    err = ioctl(audio_fd, SNDCTL_DSP_STEREO, &tmp);
    if (err < 0) {
        av_log(s1, AV_LOG_ERROR, "SNDCTL_DSP_STEREO: %s\n", strerror(errno));
        goto fail;
    }

    tmp = s->sample_rate;
    err = ioctl(audio_fd, SNDCTL_DSP_SPEED, &tmp);
    if (err < 0) {
        av_log(s1, AV_LOG_ERROR, "SNDCTL_DSP_SPEED: %s\n", strerror(errno));
        goto fail;
    }
    s->sample_rate = tmp; /* store real sample rate */
    s->fd = audio_fd;

    return 0;
 fail:
    close(audio_fd);
    return AVERROR(EIO);
}

static int audio_close(AudioData *s)
{
    close(s->fd);
    return 0;
}

/* sound output support */
static int audio_write_header(AVFormatContext *s1)
{
    AudioData *s = s1->priv_data;
    AVStream *st;
    int ret;

    st = s1->streams[0];
    s->sample_rate = st->codec->sample_rate;
    s->channels = st->codec->channels;
    ret = audio_open(s1, 1, s1->filename);
    if (ret < 0) {
        return AVERROR(EIO);
    } else {
        return 0;
    }
}

static int audio_write_packet(AVFormatContext *s1, AVPacket *pkt)
{
    AudioData *s = s1->priv_data;
    int len, ret;
    int size= pkt->size;
    uint8_t *buf= pkt->data;

    while (size > 0) {
        len = FFMIN(AUDIO_BLOCK_SIZE - s->buffer_ptr, size);
        memcpy(s->buffer + s->buffer_ptr, buf, len);
        s->buffer_ptr += len;
        if (s->buffer_ptr >= AUDIO_BLOCK_SIZE) {
            for(;;) {
                ret = write(s->fd, s->buffer, AUDIO_BLOCK_SIZE);
                if (ret > 0)
                    break;
                if (ret < 0 && (errno != EAGAIN && errno != EINTR))
                    return AVERROR(EIO);
            }
            s->buffer_ptr = 0;
        }
        buf += len;
        size -= len;
    }
    return 0;
}

static int audio_write_trailer(AVFormatContext *s1)
{
    AudioData *s = s1->priv_data;

    audio_close(s);
    return 0;
}

/* grab support */

static int audio_read_header(AVFormatContext *s1)
{
    AudioData *s = s1->priv_data;
    AVStream *st;
    int ret;

    st = avformat_new_stream(s1, NULL);
    if (!st) {
        return AVERROR(ENOMEM);
    }

    ret = audio_open(s1, 0, s1->filename);
    if (ret < 0) {
        return AVERROR(EIO);
    }

    /* take real parameters */
    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id = s->codec_id;
    st->codec->sample_rate = s->sample_rate;
    st->codec->channels = s->channels;

    avpriv_set_pts_info(st, 64, 1, 1000000);  /* 64 bits pts in us */
    return 0;
}

static int audio_read_packet(AVFormatContext *s1, AVPacket *pkt)
{
    AudioData *s = s1->priv_data;
    int ret, bdelay;
    int64_t cur_time;
    struct audio_buf_info abufi;

    if ((ret=av_new_packet(pkt, s->frame_size)) < 0)
        return ret;

    ret = read(s->fd, pkt->data, pkt->size);
    if (ret <= 0){
        av_free_packet(pkt);
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
    cur_time -= (bdelay * 1000000LL) / (s->sample_rate * s->channels);

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
    AudioData *s = s1->priv_data;

    audio_close(s);
    return 0;
}

#if CONFIG_OSS_INDEV
static const AVOption options[] = {
    { "sample_rate", "", offsetof(AudioData, sample_rate), AV_OPT_TYPE_INT, {.i64 = 48000}, 1, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { "channels",    "", offsetof(AudioData, channels),    AV_OPT_TYPE_INT, {.i64 = 2},     1, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

static const AVClass oss_demuxer_class = {
    .class_name     = "OSS demuxer",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_oss_demuxer = {
    .name           = "oss",
    .long_name      = NULL_IF_CONFIG_SMALL("OSS (Open Sound System) capture"),
    .priv_data_size = sizeof(AudioData),
    .read_header    = audio_read_header,
    .read_packet    = audio_read_packet,
    .read_close     = audio_read_close,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &oss_demuxer_class,
};
#endif

#if CONFIG_OSS_OUTDEV
AVOutputFormat ff_oss_muxer = {
    .name           = "oss",
    .long_name      = NULL_IF_CONFIG_SMALL("OSS (Open Sound System) playback"),
    .priv_data_size = sizeof(AudioData),
    /* XXX: we make the assumption that the soundcard accepts this format */
    /* XXX: find better solution with "preinit" method, needed also in
       other formats */
    .audio_codec    = AV_NE(AV_CODEC_ID_PCM_S16BE, AV_CODEC_ID_PCM_S16LE),
    .video_codec    = AV_CODEC_ID_NONE,
    .write_header   = audio_write_header,
    .write_packet   = audio_write_packet,
    .write_trailer  = audio_write_trailer,
    .flags          = AVFMT_NOFILE,
};
#endif
