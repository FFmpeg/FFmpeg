/*
 * Linux audio grab interface
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

#if HAVE_SOUNDCARD_H
#include <soundcard.h>
#else
#include <sys/soundcard.h>
#endif

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "libavutil/internal.h"

#include "libavcodec/avcodec.h"

#include "libavformat/avformat.h"
#include "libavformat/internal.h"

#include "oss.h"

static int audio_write_header(AVFormatContext *s1)
{
    OSSAudioData *s = s1->priv_data;
    AVStream *st;
    int ret;

    st = s1->streams[0];
    s->sample_rate = st->codecpar->sample_rate;
    s->channels = st->codecpar->channels;
    ret = ff_oss_audio_open(s1, 1, s1->filename);
    if (ret < 0) {
        return AVERROR(EIO);
    } else {
        return 0;
    }
}

static int audio_write_packet(AVFormatContext *s1, AVPacket *pkt)
{
    OSSAudioData *s = s1->priv_data;
    int len, ret;
    int size= pkt->size;
    uint8_t *buf= pkt->data;

    while (size > 0) {
        len = FFMIN(OSS_AUDIO_BLOCK_SIZE - s->buffer_ptr, size);
        memcpy(s->buffer + s->buffer_ptr, buf, len);
        s->buffer_ptr += len;
        if (s->buffer_ptr >= OSS_AUDIO_BLOCK_SIZE) {
            for(;;) {
                ret = write(s->fd, s->buffer, OSS_AUDIO_BLOCK_SIZE);
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
    OSSAudioData *s = s1->priv_data;

    ff_oss_audio_close(s);
    return 0;
}

AVOutputFormat ff_oss_muxer = {
    .name           = "oss",
    .long_name      = NULL_IF_CONFIG_SMALL("OSS (Open Sound System) playback"),
    .priv_data_size = sizeof(OSSAudioData),
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
