/*
 * Discworld II BMV demuxer
 * Copyright (c) 2011 Konstantin Shishkov
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
#include "avformat.h"
#include "demux.h"
#include "internal.h"

enum BMVFlags {
    BMV_NOP = 0,
    BMV_END,
    BMV_DELTA,
    BMV_INTRA,

    BMV_AUDIO   = 0x20,
};

typedef struct BMVContext {
    uint8_t *packet;
    int      size;
    int      get_next;
    int64_t  audio_pos;
} BMVContext;

static int bmv_read_header(AVFormatContext *s)
{
    AVStream *st, *ast;
    BMVContext *c = s->priv_data;

    st = avformat_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = AV_CODEC_ID_BMV_VIDEO;
    st->codecpar->width      = 640;
    st->codecpar->height     = 429;
    st->codecpar->format     = AV_PIX_FMT_PAL8;
    avpriv_set_pts_info(st, 16, 1, 12);
    ast = avformat_new_stream(s, 0);
    if (!ast)
        return AVERROR(ENOMEM);
    ast->codecpar->codec_type      = AVMEDIA_TYPE_AUDIO;
    ast->codecpar->codec_id        = AV_CODEC_ID_BMV_AUDIO;
    ast->codecpar->ch_layout       = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
    ast->codecpar->sample_rate     = 22050;
    avpriv_set_pts_info(ast, 16, 1, 22050);

    c->get_next  = 1;
    c->audio_pos = 0;
    return 0;
}

static int bmv_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    BMVContext *c = s->priv_data;
    int type, err;

    while (c->get_next) {
        if (s->pb->eof_reached)
            return AVERROR_EOF;
        type = avio_r8(s->pb);
        if (type == BMV_NOP)
            continue;
        if (type == BMV_END)
            return AVERROR_EOF;
        c->size = avio_rl24(s->pb);
        if (!c->size)
            return AVERROR_INVALIDDATA;
        if ((err = av_reallocp(&c->packet, c->size + 1)) < 0)
            return err;
        c->packet[0] = type;
        if (avio_read(s->pb, c->packet + 1, c->size) != c->size)
            return AVERROR(EIO);
        if (type & BMV_AUDIO) {
            int audio_size = c->packet[1] * 65 + 1;
            if (audio_size >= c->size) {
                av_log(s, AV_LOG_ERROR, "Reported audio size %d is bigger than packet size (%d)\n",
                       audio_size, c->size);
                return AVERROR_INVALIDDATA;
            }
            if ((err = av_new_packet(pkt, audio_size)) < 0)
                return err;
            memcpy(pkt->data, c->packet + 1, pkt->size);
            pkt->stream_index = 1;
            pkt->pts          = c->audio_pos;
            pkt->duration     = c->packet[1] * 32;
            c->audio_pos += pkt->duration;
            c->get_next   = 0;
            return pkt->size;
        } else
            break;
    }
    if ((err = av_new_packet(pkt, c->size + 1)) < 0)
        return err;
    pkt->stream_index = 0;
    c->get_next = 1;
    memcpy(pkt->data, c->packet, pkt->size);
    return pkt->size;
}

static int bmv_read_close(AVFormatContext *s)
{
    BMVContext *c = s->priv_data;

    av_freep(&c->packet);

    return 0;
}

const FFInputFormat ff_bmv_demuxer = {
    .p.name         = "bmv",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Discworld II BMV"),
    .p.extensions   = "bmv",
    .priv_data_size = sizeof(BMVContext),
    .read_header    = bmv_read_header,
    .read_packet    = bmv_read_packet,
    .read_close     = bmv_read_close,
};
