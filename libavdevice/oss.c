/*
 * Linux audio play and grab interface
 * Copyright (c) 2000, 2001 Fabrice Bellard
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

#include "config.h"

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>

#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"

#include "libavcodec/avcodec.h"

#include "libavformat/avformat.h"
#include "libavformat/internal.h"

#define OSS_AUDIO_BLOCK_SIZE 4096

typedef struct OSSAudioData {
    AVClass *class;
    int fd;
    int sample_rate;
    int channels;
    int frame_size; /* in bytes ! */
    enum AVCodecID codec_id;
    unsigned int flip_left : 1;
    uint8_t buffer[OSS_AUDIO_BLOCK_SIZE];
    int buffer_ptr;
} OSSAudioData;

static int oss_audio_open(AVFormatContext *s1, int is_output,
                          const char *audio_device)
{
    OSSAudioData *s = s1->priv_data;
    int audio_fd;
    int tmp, err;
    char *flip = getenv("AUDIO_FLIP_LEFT");
    char errbuff[128];

    if (is_output)
        audio_fd = avpriv_open(audio_device, O_WRONLY);
    else
        audio_fd = avpriv_open(audio_device, O_RDONLY);
    if (audio_fd < 0) {
        av_log(s1, AV_LOG_ERROR, "%s: %s\n", audio_device, strerror(errno));
        return AVERROR(EIO);
    }

    if (flip && *flip == '1') {
        s->flip_left = 1;
    }

    /* non blocking mode */
    if (!is_output)
        fcntl(audio_fd, F_SETFL, O_NONBLOCK);

    s->frame_size = OSS_AUDIO_BLOCK_SIZE;

#define CHECK_IOCTL_ERROR(event)                                              \
    if (err < 0) {                                                            \
        av_strerror(AVERROR(errno), errbuff, sizeof(errbuff));                \
        av_log(s1, AV_LOG_ERROR, #event ": %s\n", errbuff);                   \
        goto fail;                                                            \
    }

    /* select format : favour native format
     * We don't CHECK_IOCTL_ERROR here because even if failed OSS still may be
     * usable. If OSS is not usable the SNDCTL_DSP_SETFMTS later is going to
     * fail anyway. */
    (void) ioctl(audio_fd, SNDCTL_DSP_GETFMTS, &tmp);

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
    CHECK_IOCTL_ERROR(SNDCTL_DSP_SETFMTS)

    tmp = (s->channels == 2);
    err = ioctl(audio_fd, SNDCTL_DSP_STEREO, &tmp);
    CHECK_IOCTL_ERROR(SNDCTL_DSP_STEREO)

    tmp = s->sample_rate;
    err = ioctl(audio_fd, SNDCTL_DSP_SPEED, &tmp);
    CHECK_IOCTL_ERROR(SNDCTL_DSP_SPEED)
    s->sample_rate = tmp; /* store real sample rate */
    s->fd = audio_fd;

    return 0;
 fail:
    close(audio_fd);
    return AVERROR(EIO);
#undef CHECK_IOCTL_ERROR
}

static int audio_read_header(AVFormatContext *s1)
{
    OSSAudioData *s = s1->priv_data;
    AVStream *st;
    int ret;

    st = avformat_new_stream(s1, NULL);
    if (!st) {
        return AVERROR(ENOMEM);
    }

    ret = oss_audio_open(s1, 0, s1->filename);
    if (ret < 0) {
        return AVERROR(EIO);
    }

    /* take real parameters */
    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id = s->codec_id;
    st->codecpar->sample_rate = s->sample_rate;
    st->codecpar->channels = s->channels;

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
    OSSAudioData *s = s1->priv_data;

    close(s->fd);
    return 0;
}

static const AVOption options[] = {
    { "sample_rate", "", offsetof(OSSAudioData, sample_rate), AV_OPT_TYPE_INT, {.i64 = 48000}, 1, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { "channels",    "", offsetof(OSSAudioData, channels),    AV_OPT_TYPE_INT, {.i64 = 2},     1, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
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
    .priv_data_size = sizeof(OSSAudioData),
    .read_header    = audio_read_header,
    .read_packet    = audio_read_packet,
    .read_close     = audio_read_close,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &oss_demuxer_class,
};
