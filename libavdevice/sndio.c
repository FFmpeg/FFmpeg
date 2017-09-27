/*
 * sndio grab interface
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

#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"

#include "libavformat/avformat.h"
#include "libavformat/internal.h"

typedef struct SndioData {
    AVClass *class;
    struct sio_hdl *hdl;
    enum AVCodecID codec_id;
    int64_t hwpos;
    int64_t softpos;
    uint8_t *buffer;
    int bps;
    int buffer_size;
    int buffer_offset;
    int channels;
    int sample_rate;
} SndioData;

static inline void movecb(void *addr, int delta)
{
    SndioData *s = addr;

    s->hwpos += delta * s->channels * s->bps;
}

static av_cold int sndio_open(AVFormatContext *s1, int is_output,
                              const char *audio_device)
{
    SndioData *s = s1->priv_data;
    struct sio_hdl *hdl;
    struct sio_par par;

    hdl = sio_open(audio_device, is_output ? SIO_PLAY : SIO_REC, 0);
    if (!hdl) {
        av_log(s1, AV_LOG_ERROR, "Could not open sndio device\n");
        return AVERROR(EIO);
    }

    sio_initpar(&par);

    par.bits = 16;
    par.sig  = 1;
    par.le   = SIO_LE_NATIVE;

    if (is_output)
        par.pchan = s->channels;
    else
        par.rchan = s->channels;
    par.rate = s->sample_rate;

    if (!sio_setpar(hdl, &par) || !sio_getpar(hdl, &par)) {
        av_log(s1, AV_LOG_ERROR, "Impossible to set sndio parameters, "
               "channels: %d sample rate: %d\n", s->channels, s->sample_rate);
        goto fail;
    }

    if (par.bits != 16 || par.sig != 1 ||
        (is_output  && (par.pchan != s->channels)) ||
        (!is_output && (par.rchan != s->channels)) ||
        (par.rate != s->sample_rate)) {
        av_log(s1, AV_LOG_ERROR, "Could not set appropriate sndio parameters, "
               "channels: %d sample rate: %d\n", s->channels, s->sample_rate);
        goto fail;
    }

    s->buffer_size = par.round * par.bps *
                     (is_output ? par.pchan : par.rchan);

    if (is_output) {
        s->buffer = av_malloc(s->buffer_size);
        if (!s->buffer) {
            av_log(s1, AV_LOG_ERROR, "Could not allocate buffer\n");
            goto fail;
        }
    }

    s->codec_id    = par.le ? AV_CODEC_ID_PCM_S16LE : AV_CODEC_ID_PCM_S16BE;
    s->channels    = is_output ? par.pchan : par.rchan;
    s->sample_rate = par.rate;
    s->bps         = par.bps;

    sio_onmove(hdl, movecb, s);

    if (!sio_start(hdl)) {
        av_log(s1, AV_LOG_ERROR, "Could not start sndio\n");
        goto fail;
    }

    s->hdl = hdl;

    return 0;

fail:
    av_freep(&s->buffer);

    if (hdl)
        sio_close(hdl);

    return AVERROR(EIO);
}

static av_cold int audio_read_header(AVFormatContext *s1)
{
    SndioData *s = s1->priv_data;
    AVStream *st;
    int ret;

    st = avformat_new_stream(s1, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    ret = sndio_open(s1, 0, s1->filename);
    if (ret < 0)
        return ret;

    /* take real parameters */
    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id    = s->codec_id;
    st->codecpar->sample_rate = s->sample_rate;
    st->codecpar->channels    = s->channels;

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
        av_packet_unref(pkt);
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

    av_freep(&s->buffer);

    if (s->hdl)
        sio_close(s->hdl);

    return 0;
}

static const AVOption options[] = {
    { "sample_rate", "", offsetof(SndioData, sample_rate), AV_OPT_TYPE_INT, {.i64 = 48000}, 1, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { "channels",    "", offsetof(SndioData, channels),    AV_OPT_TYPE_INT, {.i64 = 2},     1, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
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
