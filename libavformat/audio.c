/*
 * Linux audio play and grab interface
 * Copyright (c) 2000, 2001 Fabrice Bellard.
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
#include "avformat.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef HAVE_SOUNDCARD_H
#include <soundcard.h>
#else
#include <sys/soundcard.h>
#endif
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>

#define AUDIO_BLOCK_SIZE 4096

typedef struct {
    int fd;
    int sample_rate;
    int channels;
    int frame_size; /* in bytes ! */
    int codec_id;
    int flip_left : 1;
    uint8_t buffer[AUDIO_BLOCK_SIZE];
    int buffer_ptr;
} AudioData;

static int audio_open(AudioData *s, int is_output, const char *audio_device)
{
    int audio_fd;
    int tmp, err;
    char *flip = getenv("AUDIO_FLIP_LEFT");

    if (is_output)
        audio_fd = open(audio_device, O_WRONLY);
    else
        audio_fd = open(audio_device, O_RDONLY);
    if (audio_fd < 0) {
        perror(audio_device);
        return AVERROR_IO;
    }

    if (flip && *flip == '1') {
        s->flip_left = 1;
    }

    /* non blocking mode */
    if (!is_output)
        fcntl(audio_fd, F_SETFL, O_NONBLOCK);

    s->frame_size = AUDIO_BLOCK_SIZE;
#if 0
    tmp = (NB_FRAGMENTS << 16) | FRAGMENT_BITS;
    err = ioctl(audio_fd, SNDCTL_DSP_SETFRAGMENT, &tmp);
    if (err < 0) {
        perror("SNDCTL_DSP_SETFRAGMENT");
    }
#endif

    /* select format : favour native format */
    err = ioctl(audio_fd, SNDCTL_DSP_GETFMTS, &tmp);

#ifdef WORDS_BIGENDIAN
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
        s->codec_id = CODEC_ID_PCM_S16LE;
        break;
    case AFMT_S16_BE:
        s->codec_id = CODEC_ID_PCM_S16BE;
        break;
    default:
        av_log(NULL, AV_LOG_ERROR, "Soundcard does not support 16 bit sample format\n");
        close(audio_fd);
        return AVERROR_IO;
    }
    err=ioctl(audio_fd, SNDCTL_DSP_SETFMT, &tmp);
    if (err < 0) {
        perror("SNDCTL_DSP_SETFMT");
        goto fail;
    }

    tmp = (s->channels == 2);
    err = ioctl(audio_fd, SNDCTL_DSP_STEREO, &tmp);
    if (err < 0) {
        perror("SNDCTL_DSP_STEREO");
        goto fail;
    }
    if (tmp)
        s->channels = 2;

    tmp = s->sample_rate;
    err = ioctl(audio_fd, SNDCTL_DSP_SPEED, &tmp);
    if (err < 0) {
        perror("SNDCTL_DSP_SPEED");
        goto fail;
    }
    s->sample_rate = tmp; /* store real sample rate */
    s->fd = audio_fd;

    return 0;
 fail:
    close(audio_fd);
    return AVERROR_IO;
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
    ret = audio_open(s, 1, NULL);
    if (ret < 0) {
        return AVERROR_IO;
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
        len = AUDIO_BLOCK_SIZE - s->buffer_ptr;
        if (len > size)
            len = size;
        memcpy(s->buffer + s->buffer_ptr, buf, len);
        s->buffer_ptr += len;
        if (s->buffer_ptr >= AUDIO_BLOCK_SIZE) {
            for(;;) {
                ret = write(s->fd, s->buffer, AUDIO_BLOCK_SIZE);
                if (ret > 0)
                    break;
                if (ret < 0 && (errno != EAGAIN && errno != EINTR))
                    return AVERROR_IO;
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

static int audio_read_header(AVFormatContext *s1, AVFormatParameters *ap)
{
    AudioData *s = s1->priv_data;
    AVStream *st;
    int ret;

    if (ap->sample_rate <= 0 || ap->channels <= 0)
        return -1;

    st = av_new_stream(s1, 0);
    if (!st) {
        return AVERROR(ENOMEM);
    }
    s->sample_rate = ap->sample_rate;
    s->channels = ap->channels;

    ret = audio_open(s, 0, s1->filename);
    if (ret < 0) {
        av_free(st);
        return AVERROR_IO;
    }

    /* take real parameters */
    st->codec->codec_type = CODEC_TYPE_AUDIO;
    st->codec->codec_id = s->codec_id;
    st->codec->sample_rate = s->sample_rate;
    st->codec->channels = s->channels;

    av_set_pts_info(st, 64, 1, 1000000);  /* 64 bits pts in us */
    return 0;
}

static int audio_read_packet(AVFormatContext *s1, AVPacket *pkt)
{
    AudioData *s = s1->priv_data;
    int ret, bdelay;
    int64_t cur_time;
    struct audio_buf_info abufi;

    if (av_new_packet(pkt, s->frame_size) < 0)
        return AVERROR_IO;
    for(;;) {
        struct timeval tv;
        fd_set fds;

        tv.tv_sec = 0;
        tv.tv_usec = 30 * 1000; /* 30 msecs -- a bit shorter than 1 frame at 30fps */

        FD_ZERO(&fds);
        FD_SET(s->fd, &fds);

        /* This will block until data is available or we get a timeout */
        (void) select(s->fd + 1, &fds, 0, 0, &tv);

        ret = read(s->fd, pkt->data, pkt->size);
        if (ret > 0)
            break;
        if (ret == -1 && (errno == EAGAIN || errno == EINTR)) {
            av_free_packet(pkt);
            pkt->size = 0;
            pkt->pts = av_gettime();
            return 0;
        }
        if (!(ret == 0 || (ret == -1 && (errno == EAGAIN || errno == EINTR)))) {
            av_free_packet(pkt);
            return AVERROR_IO;
        }
    }
    pkt->size = ret;

    /* compute pts of the start of the packet */
    cur_time = av_gettime();
    bdelay = ret;
    if (ioctl(s->fd, SNDCTL_DSP_GETISPACE, &abufi) == 0) {
        bdelay += abufi.bytes;
    }
    /* substract time represented by the number of bytes in the audio fifo */
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

#ifdef CONFIG_AUDIO_DEMUXER
AVInputFormat audio_demuxer = {
    "audio_device",
    "audio grab and output",
    sizeof(AudioData),
    NULL,
    audio_read_header,
    audio_read_packet,
    audio_read_close,
    .flags = AVFMT_NOFILE,
};
#endif

#ifdef CONFIG_AUDIO_MUXER
AVOutputFormat audio_muxer = {
    "audio_device",
    "audio grab and output",
    "",
    "",
    sizeof(AudioData),
    /* XXX: we make the assumption that the soundcard accepts this format */
    /* XXX: find better solution with "preinit" method, needed also in
       other formats */
#ifdef WORDS_BIGENDIAN
    CODEC_ID_PCM_S16BE,
#else
    CODEC_ID_PCM_S16LE,
#endif
    CODEC_ID_NONE,
    audio_write_header,
    audio_write_packet,
    audio_write_trailer,
    .flags = AVFMT_NOFILE,
};
#endif
