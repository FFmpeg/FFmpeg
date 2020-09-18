/*
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

/**
 * @file
 * CRI ADX demuxer
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"

#define BLOCK_SIZE    18
#define BLOCK_SAMPLES 32

typedef struct ADXDemuxerContext {
    int header_size;
} ADXDemuxerContext;

static int adx_probe(const AVProbeData *p)
{
    int offset;
    if (AV_RB16(p->buf) != 0x8000)
        return 0;
    offset = AV_RB16(&p->buf[2]);
    if (   offset < 8
        || offset > p->buf_size - 4
        || memcmp(p->buf + offset - 2, "(c)CRI", 6))
        return 0;
    return AVPROBE_SCORE_MAX * 3 / 4;
}

static int adx_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    ADXDemuxerContext *c = s->priv_data;
    AVCodecParameters *par = s->streams[0]->codecpar;
    int ret, size;

    if (avio_feof(s->pb))
        return AVERROR_EOF;

    if (par->channels <= 0) {
        av_log(s, AV_LOG_ERROR, "invalid number of channels %d\n", par->channels);
        return AVERROR_INVALIDDATA;
    }

    size = BLOCK_SIZE * par->channels;

    pkt->pos = avio_tell(s->pb);
    pkt->stream_index = 0;

    ret = av_get_packet(s->pb, pkt, size * 128);
    if (ret < 0)
        return ret;
    if ((ret % size) && ret >= size) {
        size = ret - (ret % size);
        av_shrink_packet(pkt, size);
        pkt->flags &= ~AV_PKT_FLAG_CORRUPT;
    } else if (ret < size) {
        return AVERROR(EIO);
    } else {
        size = ret;
    }

    pkt->duration = size / (BLOCK_SIZE * par->channels);
    pkt->pts      = (pkt->pos - c->header_size) / (BLOCK_SIZE * par->channels);

    return 0;
}

static int adx_read_header(AVFormatContext *s)
{
    ADXDemuxerContext *c = s->priv_data;
    AVCodecParameters *par;
    int ret;
    AVStream *st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    par = s->streams[0]->codecpar;

    if (avio_rb16(s->pb) != 0x8000)
        return AVERROR_INVALIDDATA;
    c->header_size = avio_rb16(s->pb) + 4;
    avio_seek(s->pb, -4, SEEK_CUR);

    if ((ret = ff_get_extradata(s, par, s->pb, c->header_size)) < 0)
        return ret;

    if (par->extradata_size < 12) {
        av_log(s, AV_LOG_ERROR, "Invalid extradata size.\n");
        return AVERROR_INVALIDDATA;
    }
    par->channels    = AV_RB8 (par->extradata + 7);
    par->sample_rate = AV_RB32(par->extradata + 8);

    if (par->channels <= 0) {
        av_log(s, AV_LOG_ERROR, "invalid number of channels %d\n", par->channels);
        return AVERROR_INVALIDDATA;
    }

    if (par->sample_rate <= 0) {
        av_log(s, AV_LOG_ERROR, "Invalid sample rate %d\n", par->sample_rate);
        return AVERROR_INVALIDDATA;
    }

    par->codec_type  = AVMEDIA_TYPE_AUDIO;
    par->codec_id    = s->iformat->raw_codec_id;
    par->bit_rate    = (int64_t)par->sample_rate * par->channels * BLOCK_SIZE * 8LL / BLOCK_SAMPLES;

    avpriv_set_pts_info(st, 64, BLOCK_SAMPLES, par->sample_rate);

    return 0;
}

AVInputFormat ff_adx_demuxer = {
    .name           = "adx",
    .long_name      = NULL_IF_CONFIG_SMALL("CRI ADX"),
    .read_probe     = adx_probe,
    .priv_data_size = sizeof(ADXDemuxerContext),
    .read_header    = adx_read_header,
    .read_packet    = adx_read_packet,
    .extensions     = "adx",
    .raw_codec_id   = AV_CODEC_ID_ADPCM_ADX,
    .flags          = AVFMT_GENERIC_INDEX,
};
