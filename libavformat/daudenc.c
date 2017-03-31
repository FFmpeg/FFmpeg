/*
 * D-Cinema audio muxer
 * Copyright (c) 2005 Reimar Döffinger
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

static int daud_init(struct AVFormatContext *s)
{
    AVCodecParameters *par = s->streams[0]->codecpar;
    if (par->ch_layout.nb_channels != 6 || par->sample_rate != 96000)
        return AVERROR(EINVAL);
    return 0;
}

static int daud_write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    if (pkt->size > 65535) {
        av_log(s, AV_LOG_ERROR,
               "Packet size too large for s302m. (%d > 65535)\n", pkt->size);
        return AVERROR_INVALIDDATA;
    }
    avio_wb16(s->pb, pkt->size);
    avio_wb16(s->pb, 0x8010); // unknown
    avio_write(s->pb, pkt->data, pkt->size);
    return 0;
}

const AVOutputFormat ff_daud_muxer = {
    .name         = "daud",
    .long_name    = NULL_IF_CONFIG_SMALL("D-Cinema audio"),
    .extensions   = "302",
    .audio_codec  = AV_CODEC_ID_PCM_S24DAUD,
    .video_codec  = AV_CODEC_ID_NONE,
    .init         = daud_init,
    .write_packet = daud_write_packet,
    .flags        = AVFMT_NOTIMESTAMPS,
};
