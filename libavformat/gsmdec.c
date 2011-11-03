/*
 * RAW GSM demuxer
 * Copyright (c) 2011 Justin Ruggles
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

#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "avformat.h"

#define GSM_BLOCK_SIZE    33
#define GSM_BLOCK_SAMPLES 160
#define GSM_SAMPLE_RATE   8000

typedef struct {
    AVClass *class;
    int sample_rate;
} GSMDemuxerContext;

static int gsm_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, size;

    size = GSM_BLOCK_SIZE * 32;

    pkt->pos = avio_tell(s->pb);
    pkt->stream_index = 0;

    ret = av_get_packet(s->pb, pkt, size);
    if (ret < GSM_BLOCK_SIZE) {
        av_free_packet(pkt);
        return ret < 0 ? ret : AVERROR(EIO);
    }
    pkt->size     = ret;
    pkt->duration = ret      / GSM_BLOCK_SIZE;
    pkt->pts      = pkt->pos / GSM_BLOCK_SIZE;

    return 0;
}

static int gsm_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    GSMDemuxerContext *c = s->priv_data;
    AVStream *st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codec->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id    = s->iformat->value;
    st->codec->channels    = 1;
    st->codec->sample_rate = c->sample_rate;
    st->codec->block_align = GSM_BLOCK_SIZE;
    st->codec->bit_rate    = GSM_BLOCK_SIZE * 8 * c->sample_rate / GSM_BLOCK_SAMPLES;

    av_set_pts_info(st, 64, GSM_BLOCK_SAMPLES, GSM_SAMPLE_RATE);

    return 0;
}

static int gsm_read_seek2(AVFormatContext *s, int stream_index, int64_t min_ts,
                          int64_t ts, int64_t max_ts, int flags)
{
    GSMDemuxerContext *c = s->priv_data;

    /* convert timestamps to file positions */
    if (!(flags & AVSEEK_FLAG_BYTE)) {
        if (stream_index < 0) {
            AVRational bitrate_q = { GSM_BLOCK_SAMPLES, c->sample_rate * GSM_BLOCK_SIZE };
            ts     = av_rescale_q(ts,     AV_TIME_BASE_Q, bitrate_q);
            min_ts = av_rescale_q(min_ts, AV_TIME_BASE_Q, bitrate_q);
            max_ts = av_rescale_q(max_ts, AV_TIME_BASE_Q, bitrate_q);
        } else {
            ts     *= GSM_BLOCK_SIZE;
            min_ts *= GSM_BLOCK_SIZE;
            max_ts *= GSM_BLOCK_SIZE;
        }
    }
    /* round to nearest block boundary */
    ts = (ts + GSM_BLOCK_SIZE / 2) / GSM_BLOCK_SIZE * GSM_BLOCK_SIZE;
    ts = FFMAX(0, ts);

    /* handle min/max */
    while (ts < min_ts)
        ts += GSM_BLOCK_SIZE;
    while (ts > max_ts)
        ts -= GSM_BLOCK_SIZE;
    if (ts < min_ts || ts > max_ts)
        return -1;

    return avio_seek(s->pb, ts, SEEK_SET);
}

static const AVOption options[] = {
    { "sample_rate", "", offsetof(GSMDemuxerContext, sample_rate),
       AV_OPT_TYPE_INT, {.dbl = GSM_SAMPLE_RATE}, 1, INT_MAX / GSM_BLOCK_SIZE,
       AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

static const AVClass class = {
    .class_name = "gsm demuxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_gsm_demuxer = {
    .name           = "gsm",
    .long_name      = NULL_IF_CONFIG_SMALL("raw GSM"),
    .priv_data_size = sizeof(GSMDemuxerContext),
    .read_header    = gsm_read_header,
    .read_packet    = gsm_read_packet,
    .read_seek2     = gsm_read_seek2,
    .extensions     = "gsm",
    .value          = CODEC_ID_GSM,
    .priv_class     = &class,
};
