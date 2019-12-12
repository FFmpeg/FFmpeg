/*
 * RAW GSM demuxer
 * Copyright (c) 2011 Justin Ruggles
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

#include "libavutil/channel_layout.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "avformat.h"
#include "internal.h"

#define GSM_BLOCK_SIZE    33
#define GSM_BLOCK_SAMPLES 160
#define GSM_SAMPLE_RATE   8000

typedef struct GSMDemuxerContext {
    AVClass *class;
    int sample_rate;
} GSMDemuxerContext;

static int gsm_probe(const AVProbeData *p)
{
    int valid = 0, invalid = 0;
    uint8_t *b = p->buf;
    while (b < p->buf + p->buf_size - 32) {
        if ((*b & 0xf0) == 0xd0) {
            valid++;
        } else {
            invalid++;
        }
        b += 33;
    }
    if (valid >> 5 > invalid)
        return AVPROBE_SCORE_EXTENSION + 1;
    return 0;
}

static int gsm_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, size;

    size = GSM_BLOCK_SIZE;

    pkt->pos = avio_tell(s->pb);
    pkt->stream_index = 0;

    ret = av_get_packet(s->pb, pkt, size);
    if (ret < GSM_BLOCK_SIZE) {
        av_packet_unref(pkt);
        return ret < 0 ? ret : AVERROR(EIO);
    }
    pkt->duration = 1;
    pkt->pts      = pkt->pos / GSM_BLOCK_SIZE;

    return 0;
}

static int gsm_read_header(AVFormatContext *s)
{
    GSMDemuxerContext *c = s->priv_data;
    AVStream *st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id    = s->iformat->raw_codec_id;
    st->codecpar->channels    = 1;
    st->codecpar->channel_layout = AV_CH_LAYOUT_MONO;
    st->codecpar->sample_rate = c->sample_rate;
    st->codecpar->bit_rate    = GSM_BLOCK_SIZE * 8 * c->sample_rate / GSM_BLOCK_SAMPLES;

    avpriv_set_pts_info(st, 64, GSM_BLOCK_SAMPLES, GSM_SAMPLE_RATE);

    return 0;
}

static const AVOption options[] = {
    { "sample_rate", "", offsetof(GSMDemuxerContext, sample_rate),
       AV_OPT_TYPE_INT, {.i64 = GSM_SAMPLE_RATE}, 1, INT_MAX / GSM_BLOCK_SIZE,
       AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

static const AVClass gsm_class = {
    .class_name = "gsm demuxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_gsm_demuxer = {
    .name           = "gsm",
    .long_name      = NULL_IF_CONFIG_SMALL("raw GSM"),
    .priv_data_size = sizeof(GSMDemuxerContext),
    .read_probe     = gsm_probe,
    .read_header    = gsm_read_header,
    .read_packet    = gsm_read_packet,
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "gsm",
    .raw_codec_id   = AV_CODEC_ID_GSM,
    .priv_class     = &gsm_class,
};
