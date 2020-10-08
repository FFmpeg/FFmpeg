/*
 * Microsoft Paint (MSP) demuxer
 * Copyright (c) 2020 Peter Ross (pross@xvid.org)
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
 * Microsoft Paint (MSP) demuxer
 */

#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"
#include "avformat.h"
#include "internal.h"

typedef struct {
    int packet_size;
} MSPContext;

static int msp_probe(const AVProbeData *p)
{
    unsigned int i, sum;

    if (p->buf_size <= 32 || (memcmp(p->buf, "DanM", 4) && memcmp(p->buf, "LinS", 4)))
        return 0;

    sum = 0;
    for (i = 0; i < 24; i += 2)
        sum ^= AV_RL16(p->buf + i);

    return AV_RL16(p->buf + 24) == sum ? AVPROBE_SCORE_MAX : 0;
}

static int msp_read_header(AVFormatContext *s)
{
    MSPContext * cntx = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id = avio_rl32(pb) == MKTAG('D', 'a', 'n', 'M') ? AV_CODEC_ID_RAWVIDEO : AV_CODEC_ID_MSP2;

    st->codecpar->width  = avio_rl16(pb);
    st->codecpar->height = avio_rl16(pb);
    st->codecpar->format = AV_PIX_FMT_MONOBLACK;

    st->sample_aspect_ratio.num = avio_rl16(pb);
    st->sample_aspect_ratio.den = avio_rl16(pb);
    avio_skip(pb, 20);

    if (st->codecpar->codec_id == AV_CODEC_ID_RAWVIDEO) {
        cntx->packet_size = av_image_get_buffer_size(st->codecpar->format, st->codecpar->width, st->codecpar->height, 1);
        if (cntx->packet_size < 0)
            return cntx->packet_size;
    } else
        cntx->packet_size = 2 * st->codecpar->height;

    return 0;
}

static int msp_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVStream *st = s->streams[0];
    MSPContext *cntx = s->priv_data;
    int ret;

    ret = av_get_packet(s->pb, pkt, cntx->packet_size);
    if (ret < 0)
        return ret;

    if (st->codecpar->codec_id == AV_CODEC_ID_MSP2) {
        unsigned int size, i;
        if (pkt->size != 2 * st->codecpar->height)
            return AVERROR_INVALIDDATA;
        size = 0;
        for (i = 0; i < st->codecpar->height; i++)
             size += AV_RL16(&pkt->data[i * 2]);
        ret = av_append_packet(s->pb, pkt, size);
        if (ret < 0)
            return ret;
    }

    pkt->stream_index = 0;
    pkt->flags |= AV_PKT_FLAG_KEY;
    return 0;
}

AVInputFormat ff_msp_demuxer = {
    .name         = "msp",
    .long_name    = NULL_IF_CONFIG_SMALL("Microsoft Paint (MSP))"),
    .read_probe   = msp_probe,
    .read_header  = msp_read_header,
    .read_packet  = msp_read_packet,
    .flags        = AVFMT_NOTIMESTAMPS,
    .priv_data_size = sizeof(MSPContext),
};
