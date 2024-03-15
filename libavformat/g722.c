/*
 * g722 raw demuxer
 * Copyright (c) 2010 Martin Storsjo
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
#include "rawdec.h"

static int g722_read_header(AVFormatContext *s)
{
    AVStream *st;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id    = AV_CODEC_ID_ADPCM_G722;
    st->codecpar->sample_rate = 16000;
    st->codecpar->ch_layout   = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;

    st->codecpar->bits_per_coded_sample = 4;

    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
    return 0;
}

const FFInputFormat ff_g722_demuxer = {
    .p.name         = "g722",
    .p.long_name    = NULL_IF_CONFIG_SMALL("raw G.722"),
    .p.flags        = AVFMT_GENERIC_INDEX,
    .p.extensions   = "g722,722",
    .p.priv_class   = &ff_raw_demuxer_class,
    .read_header    = g722_read_header,
    .read_packet    = ff_raw_read_partial_packet,
    .raw_codec_id   = AV_CODEC_ID_ADPCM_G722,
    .priv_data_size = sizeof(FFRawDemuxerContext),
};
