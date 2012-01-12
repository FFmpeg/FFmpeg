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
#include "libavformat/internal.h"
#include "libavutil/opt.h"

#include "sndio_common.h"

static av_cold int audio_read_header(AVFormatContext *s1)
{
    SndioData *s = s1->priv_data;
    AVStream *st;
    int ret;

    st = avformat_new_stream(s1, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    ret = ff_sndio_open(s1, 0, s1->filename);
    if (ret < 0)
        return ret;

    /* take real parameters */
    st->codec->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id    = s->codec_id;
    st->codec->sample_rate = s->sample_rate;
    st->codec->channels    = s->channels;

    avpriv_set_pts_info(st, 64, 1, 1000000);  /* 64 bits pts in us */

    return 0;
}

static int audio_read_packet(AVFormatContext *s1, AVPacket *pkt)
{
    SndioData *s = s1->priv_data;
    int64_t bdelay, cur_time;
    int ret;

    if ((ret = av_new_packet(pkt, s->buffer_size)) < 0)
        return ret;

    ret = sio_read(s->hdl, pkt->data, pkt->size);
    if (ret == 0 || sio_eof(s->hdl)) {
        av_free_packet(pkt);
        return AVERROR_EOF;
    }

    pkt->size   = ret;
    s->softpos += ret;

    /* compute pts of the start of the packet */
    cur_time = av_gettime();

    bdelay = ret + s->hwpos - s->softpos;

    /* convert to pts */
    pkt->pts = cur_time - ((bdelay * 1000000) /
        (s->bps * s->channels * s->sample_rate));

    return 0;
}

static av_cold int audio_read_close(AVFormatContext *s1)
{
    SndioData *s = s1->priv_data;

    ff_sndio_close(s);

    return 0;
}

static const AVOption options[] = {
    { "sample_rate", "", offsetof(SndioData, sample_rate), AV_OPT_TYPE_INT, {.dbl = 48000}, 1, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { "channels",    "", offsetof(SndioData, channels),    AV_OPT_TYPE_INT, {.dbl = 2},     1, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

static const AVClass sndio_demuxer_class = {
    .class_name     = "sndio indev",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_sndio_demuxer = {
    .name           = "sndio",
    .long_name      = NULL_IF_CONFIG_SMALL("sndio audio capture"),
    .priv_data_size = sizeof(SndioData),
    .read_header    = audio_read_header,
    .read_packet    = audio_read_packet,
    .read_close     = audio_read_close,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &sndio_demuxer_class,
};
