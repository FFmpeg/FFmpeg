/*
 * Maxis XA (.xa) File Demuxer
 * Copyright (c) 2008 Robert Marston
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
 * Maxis XA File Demuxer
 * by Robert Marston (rmarston@gmail.com)
 * for more information on the XA audio format see
 *   http://wiki.multimedia.cx/index.php?title=Maxis_XA
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"

#define XA00_TAG MKTAG('X', 'A', 0, 0)
#define XAI0_TAG MKTAG('X', 'A', 'I', 0)
#define XAJ0_TAG MKTAG('X', 'A', 'J', 0)

typedef struct MaxisXADemuxContext {
    uint32_t out_size;
    uint32_t sent_bytes;
} MaxisXADemuxContext;

static int xa_probe(const AVProbeData *p)
{
    int channels, srate, bits_per_sample;
    if (p->buf_size < 24)
        return 0;
    switch(AV_RL32(p->buf)) {
    case XA00_TAG:
    case XAI0_TAG:
    case XAJ0_TAG:
        break;
    default:
        return 0;
    }
    channels        = AV_RL16(p->buf + 10);
    srate           = AV_RL32(p->buf + 12);
    bits_per_sample = AV_RL16(p->buf + 22);
    if (!channels || channels > 8 || !srate || srate > 192000 ||
        bits_per_sample < 4 || bits_per_sample > 32)
        return 0;
    return AVPROBE_SCORE_EXTENSION;
}

static int xa_read_header(AVFormatContext *s)
{
    MaxisXADemuxContext *xa = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st;

    /*Set up the XA Audio Decoder*/
    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type   = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id     = AV_CODEC_ID_ADPCM_EA_MAXIS_XA;
    avio_skip(pb, 4);       /* Skip the XA ID */
    xa->out_size            =  avio_rl32(pb);
    avio_skip(pb, 2);       /* Skip the tag */
    st->codecpar->channels     = avio_rl16(pb);
    st->codecpar->sample_rate  = avio_rl32(pb);
    avio_skip(pb, 4);       /* Skip average byte rate */
    avio_skip(pb, 2);       /* Skip block align */
    avio_skip(pb, 2);       /* Skip bits-per-sample */

    if (!st->codecpar->channels || !st->codecpar->sample_rate)
        return AVERROR_INVALIDDATA;

    st->codecpar->bit_rate = av_clip(15LL * st->codecpar->channels * 8 *
                                  st->codecpar->sample_rate / 28, 0, INT_MAX);

    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
    st->start_time = 0;

    return 0;
}

static int xa_read_packet(AVFormatContext *s,
                          AVPacket *pkt)
{
    MaxisXADemuxContext *xa = s->priv_data;
    AVStream *st = s->streams[0];
    AVIOContext *pb = s->pb;
    unsigned int packet_size;
    int ret;

    if (xa->sent_bytes >= xa->out_size)
        return AVERROR_EOF;
    /* 1 byte header and 14 bytes worth of samples * number channels per block */
    packet_size = 15*st->codecpar->channels;

    ret = av_get_packet(pb, pkt, packet_size);
    if(ret < 0)
        return ret;

    pkt->stream_index = st->index;
    xa->sent_bytes += packet_size;
    pkt->duration = 28;

    return ret;
}

AVInputFormat ff_xa_demuxer = {
    .name           = "xa",
    .long_name      = NULL_IF_CONFIG_SMALL("Maxis XA"),
    .priv_data_size = sizeof(MaxisXADemuxContext),
    .read_probe     = xa_probe,
    .read_header    = xa_read_header,
    .read_packet    = xa_read_packet,
};
