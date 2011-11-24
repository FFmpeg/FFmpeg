/*
 * CD Graphics Demuxer
 * Copyright (c) 2009 Michael Tison
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

#define CDG_PACKET_SIZE    24
#define CDG_COMMAND        0x09
#define CDG_MASK           0x3F

static int read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    AVStream *vst;
    int ret;

    vst = avformat_new_stream(s, NULL);
    if (!vst)
        return AVERROR(ENOMEM);

    vst->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    vst->codec->codec_id   = CODEC_ID_CDGRAPHICS;

    /// 75 sectors/sec * 4 packets/sector = 300 packets/sec
    av_set_pts_info(vst, 32, 1, 300);

    ret = avio_size(s->pb);
    if (ret > 0)
        vst->duration = (ret * vst->time_base.den) / (CDG_PACKET_SIZE * 300);

    return 0;
}

static int read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret;

    while (1) {
        ret = av_get_packet(s->pb, pkt, CDG_PACKET_SIZE);
        if (ret < 1 || (pkt->data[0] & CDG_MASK) == CDG_COMMAND)
            break;
        av_free_packet(pkt);
    }

    pkt->stream_index = 0;
    pkt->dts=pkt->pts= s->streams[0]->cur_dts;

    if(ret>5 && (pkt->data[0]&0x3F) == 9 && (pkt->data[1]&0x3F)==1 && !(pkt->data[2+2+1] & 0x0F)){
        pkt->flags = AV_PKT_FLAG_KEY;
    }
    return ret;
}

AVInputFormat ff_cdg_demuxer = {
    .name           = "cdg",
    .long_name      = NULL_IF_CONFIG_SMALL("CD Graphics Format"),
    .read_header    = read_header,
    .read_packet    = read_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "cdg"
};
