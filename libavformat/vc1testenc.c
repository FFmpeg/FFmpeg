/*
 * VC-1 test bitstreams format muxer.
 * Copyright (c) 2008 Konstantin Shishkov
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

typedef struct RCVContext {
    int frames;
} RCVContext;

static int vc1test_write_header(AVFormatContext *s)
{
    AVCodecContext *avc = s->streams[0]->codec;
    AVIOContext *pb = s->pb;

    if (avc->codec_id != CODEC_ID_WMV3) {
        av_log(s, AV_LOG_ERROR, "Only WMV3 is accepted!\n");
        return -1;
    }
    avio_wl24(pb, 0); //frames count will be here
    avio_w8(pb, 0xC5);
    avio_wl32(pb, 4);
    avio_write(pb, avc->extradata, 4);
    avio_wl32(pb, avc->height);
    avio_wl32(pb, avc->width);
    avio_wl32(pb, 0xC);
    avio_wl24(pb, 0); // hrd_buffer
    avio_w8(pb, 0x80); // level|cbr|res1
    avio_wl32(pb, 0); // hrd_rate
    if (s->streams[0]->r_frame_rate.den && s->streams[0]->r_frame_rate.num == 1)
        avio_wl32(pb, s->streams[0]->r_frame_rate.den);
    else
        avio_wl32(pb, 0xFFFFFFFF); //variable framerate
    av_set_pts_info(s->streams[0], 32, 1, 1000);

    return 0;
}

static int vc1test_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    RCVContext *ctx = s->priv_data;
    AVIOContext *pb = s->pb;

    if (!pkt->size)
        return 0;
    avio_wl32(pb, pkt->size | ((pkt->flags & AV_PKT_FLAG_KEY) ? 0x80000000 : 0));
    avio_wl32(pb, pkt->pts);
    avio_write(pb, pkt->data, pkt->size);
    avio_flush(pb);
    ctx->frames++;

    return 0;
}

static int vc1test_write_trailer(AVFormatContext *s)
{
    RCVContext *ctx = s->priv_data;
    AVIOContext *pb = s->pb;

    if (s->pb->seekable) {
        avio_seek(pb, 0, SEEK_SET);
        avio_wl24(pb, ctx->frames);
        avio_flush(pb);
    }
    return 0;
}

AVOutputFormat ff_vc1t_muxer = {
    "rcv",
    NULL_IF_CONFIG_SMALL("VC-1 test bitstream"),
    "",
    "rcv",
    sizeof(RCVContext),
    CODEC_ID_NONE,
    CODEC_ID_WMV3,
    vc1test_write_header,
    vc1test_write_packet,
    vc1test_write_trailer,
};
