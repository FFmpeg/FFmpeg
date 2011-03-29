/*
 * sndio play and grab interface
 * Copyright (c) 2010 Jacob Meuser
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

#include <stdint.h>
#include <sndio.h>

#include "libavformat/avformat.h"

#include "sndio_common.h"

static av_cold int audio_write_header(AVFormatContext *s1)
{
    SndioData *s = s1->priv_data;
    AVStream *st;
    int ret;

    st             = s1->streams[0];
    s->sample_rate = st->codec->sample_rate;
    s->channels    = st->codec->channels;

    ret = ff_sndio_open(s1, 1, s1->filename);

    return ret;
}

static int audio_write_packet(AVFormatContext *s1, AVPacket *pkt)
{
    SndioData *s = s1->priv_data;
    uint8_t *buf= pkt->data;
    int size = pkt->size;
    int len, ret;

    while (size > 0) {
        len = FFMIN(s->buffer_size - s->buffer_offset, size);
        memcpy(s->buffer + s->buffer_offset, buf, len);
        buf  += len;
        size -= len;
        s->buffer_offset += len;
        if (s->buffer_offset >= s->buffer_size) {
            ret = sio_write(s->hdl, s->buffer, s->buffer_size);
            if (ret == 0 || sio_eof(s->hdl))
                return AVERROR(EIO);
            s->softpos      += ret;
            s->buffer_offset = 0;
        }
    }

    return 0;
}

static int audio_write_trailer(AVFormatContext *s1)
{
    SndioData *s = s1->priv_data;

    sio_write(s->hdl, s->buffer, s->buffer_offset);

    ff_sndio_close(s);

    return 0;
}

AVOutputFormat ff_sndio_muxer = {
    .name           = "sndio",
    .long_name      = NULL_IF_CONFIG_SMALL("sndio audio playback"),
    .priv_data_size = sizeof(SndioData),
    /* XXX: we make the assumption that the soundcard accepts this format */
    /* XXX: find better solution with "preinit" method, needed also in
       other formats */
    .audio_codec    = AV_NE(CODEC_ID_PCM_S16BE, CODEC_ID_PCM_S16LE),
    .video_codec    = CODEC_ID_NONE,
    .write_header   = audio_write_header,
    .write_packet   = audio_write_packet,
    .write_trailer  = audio_write_trailer,
    .flags          = AVFMT_NOFILE,
};
