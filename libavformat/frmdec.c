/*
 * Megalux Frame demuxer
 * Copyright (c) 2010 Peter Ross <pross@xvid.org>
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
 * Megalux Frame demuxer
 */

#include "libavcodec/raw.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"

static const PixelFormatTag frm_pix_fmt_tags[] = {
    { AV_PIX_FMT_RGB555, 1 },
    { AV_PIX_FMT_RGB0,   2 },
    { AV_PIX_FMT_RGB24,  3 },
    { AV_PIX_FMT_BGR0,   4 },
    { AV_PIX_FMT_BGRA,   5 },
    { AV_PIX_FMT_NONE,   0 },
};

typedef struct {
    int count;
} FrmContext;

static int frm_read_probe(const AVProbeData *p)
{
    if (p->buf_size > 8 &&
        p->buf[0] == 'F' && p->buf[1] == 'R' && p->buf[2] == 'M' &&
        AV_RL16(&p->buf[4]) && AV_RL16(&p->buf[6]))
        return AVPROBE_SCORE_MAX / 4;
    return 0;
}

static int frm_read_header(AVFormatContext *avctx)
{
    AVIOContext *pb = avctx->pb;
    AVStream *st = avformat_new_stream(avctx, 0);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = AV_CODEC_ID_RAWVIDEO;
    avio_skip(pb, 3);

    st->codecpar->format    = avpriv_find_pix_fmt(frm_pix_fmt_tags, avio_r8(pb));
    if (!st->codecpar->format)
        return AVERROR_INVALIDDATA;

    st->codecpar->codec_tag  = 0;
    st->codecpar->width      = avio_rl16(pb);
    st->codecpar->height     = avio_rl16(pb);
    return 0;
}

static int frm_read_packet(AVFormatContext *avctx, AVPacket *pkt)
{
    FrmContext *s = avctx->priv_data;
    AVCodecParameters *par = avctx->streams[0]->codecpar;
    int packet_size, ret;

    if (s->count)
        return AVERROR_EOF;

    packet_size = av_image_get_buffer_size(par->format, par->width, par->height, 1);
    if (packet_size < 0)
        return AVERROR_INVALIDDATA;

    ret = av_get_packet(avctx->pb, pkt, packet_size);
    if (ret < 0)
        return ret;

    if (par->format == AV_PIX_FMT_BGRA) {
        int i;
        for (i = 3; i + 1 <= pkt->size; i += 4)
            pkt->data[i] = 0xFF - pkt->data[i];
    }

    pkt->stream_index = 0;
    s->count++;

    return 0;
}

AVInputFormat ff_frm_demuxer = {
    .name           = "frm",
    .priv_data_size = sizeof(FrmContext),
    .long_name      = NULL_IF_CONFIG_SMALL("Megalux Frame"),
    .read_probe     = frm_read_probe,
    .read_header    = frm_read_header,
    .read_packet    = frm_read_packet,
};
