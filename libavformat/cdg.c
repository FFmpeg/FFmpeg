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
#include "demux.h"
#include "internal.h"

#define CDG_PACKET_SIZE    24
#define CDG_COMMAND        0x09
#define CDG_MASK           0x3F

static int read_probe(const AVProbeData *p)
{
    const int cnt = p->buf_size / CDG_PACKET_SIZE;
    int score = 0;

    for (int i = 0; i < cnt; i++) {
        const int x = p->buf[i * CDG_PACKET_SIZE] & CDG_MASK;

        score += x == CDG_COMMAND;
        if (x != CDG_COMMAND && x != 0)
            return 0;
    }

    return FFMIN(score, AVPROBE_SCORE_MAX);
}

static int read_header(AVFormatContext *s)
{
    AVStream *vst;
    int64_t ret;

    vst = avformat_new_stream(s, NULL);
    if (!vst)
        return AVERROR(ENOMEM);

    vst->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    vst->codecpar->codec_id   = AV_CODEC_ID_CDGRAPHICS;

    /// 75 sectors/sec * 4 packets/sector = 300 packets/sec
    avpriv_set_pts_info(vst, 32, 1, 300);

    ret = avio_size(s->pb);
    if (ret < 0) {
        av_log(s, AV_LOG_WARNING, "Cannot calculate duration as file size cannot be determined\n");
    } else
        vst->duration = (ret * (int64_t)vst->time_base.den) / (CDG_PACKET_SIZE * 300);

    return 0;
}

static int read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret;

    ret = av_get_packet(s->pb, pkt, CDG_PACKET_SIZE);
    pkt->stream_index = 0;
    pkt->dts=
    pkt->pts= pkt->pos / CDG_PACKET_SIZE;

    if (!pkt->pos || (ret > 5 &&
         (pkt->data[0] & CDG_MASK) == CDG_COMMAND &&
         (pkt->data[1] & CDG_MASK) == 1 && !(pkt->data[2+2+1] & 0x0F))) {
        pkt->flags = AV_PKT_FLAG_KEY;
    }
    return ret;
}

const FFInputFormat ff_cdg_demuxer = {
    .p.name         = "cdg",
    .p.long_name    = NULL_IF_CONFIG_SMALL("CD Graphics"),
    .p.flags        = AVFMT_GENERIC_INDEX,
    .p.extensions   = "cdg",
    .read_probe     = read_probe,
    .read_header    = read_header,
    .read_packet    = read_packet,
};
