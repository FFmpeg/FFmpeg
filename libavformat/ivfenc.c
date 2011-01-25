/*
 * Copyright (c) 2010 Reimar Döffinger
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
#include "avformat.h"
#include "libavutil/intreadwrite.h"

static int ivf_write_header(AVFormatContext *s)
{
    AVCodecContext *ctx;
    ByteIOContext *pb = s->pb;

    if (s->nb_streams != 1) {
        av_log(s, AV_LOG_ERROR, "Format supports only exactly one video stream\n");
        return AVERROR(EINVAL);
    }
    ctx = s->streams[0]->codec;
    if (ctx->codec_type != CODEC_TYPE_VIDEO || ctx->codec_id != CODEC_ID_VP8) {
        av_log(s, AV_LOG_ERROR, "Currently only VP8 is supported!\n");
        return AVERROR(EINVAL);
    }
    put_buffer(pb, "DKIF", 4);
    put_le16(pb, 0); // version
    put_le16(pb, 32); // header length
    put_le32(pb, ctx->codec_tag ? ctx->codec_tag : AV_RL32("VP80"));
    put_le16(pb, ctx->width);
    put_le16(pb, ctx->height);
    put_le32(pb, s->streams[0]->time_base.den);
    put_le32(pb, s->streams[0]->time_base.num);
    put_le64(pb, s->streams[0]->duration); // TODO: duration or number of frames?!?

    return 0;
}

static int ivf_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    ByteIOContext *pb = s->pb;
    put_le32(pb, pkt->size);
    put_le64(pb, pkt->pts);
    put_buffer(pb, pkt->data, pkt->size);
    put_flush_packet(pb);

    return 0;
}

AVOutputFormat ff_ivf_muxer = {
    .name = "ivf",
    .long_name = NULL_IF_CONFIG_SMALL("On2 IVF"),
    .extensions = "ivf",
    .audio_codec = CODEC_ID_NONE,
    .video_codec = CODEC_ID_VP8,
    .write_header = ivf_write_header,
    .write_packet = ivf_write_packet,
};
