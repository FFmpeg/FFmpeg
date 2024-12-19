/*
 * G.728 raw demuxer
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

static int g728_read_header(AVFormatContext *s)
{
    AVStream *st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id    = ffifmt(s->iformat)->raw_codec_id;
    st->codecpar->sample_rate = 8000;
    st->codecpar->bit_rate    = 16000;
    st->codecpar->block_align = 5;
    st->codecpar->ch_layout   = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

    return 0;
}

static int g728_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret = av_get_packet(s->pb, pkt, 1020); // a size similar to RAW_PACKET_SIZE divisible by 5
    pkt->flags &= ~AV_PKT_FLAG_CORRUPT;
    pkt->duration = (pkt->size / 5) * 20;
    return ret;
}

const FFInputFormat ff_g728_demuxer = {
    .p.name         = "g728",
    .p.long_name    = NULL_IF_CONFIG_SMALL("raw G.728"),
    .p.extensions   = "g728",
    .p.flags        = AVFMT_GENERIC_INDEX,
    .read_header    = g728_read_header,
    .read_packet    = g728_read_packet,
    .raw_codec_id   = AV_CODEC_ID_G728,
};
