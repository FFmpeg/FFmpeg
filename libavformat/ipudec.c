/*
 * IPU video demuxer
 * Copyright (c) 2020 Paul B Mahol
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
#include "internal.h"
#include "avio_internal.h"
#include "rawdec.h"

#include "libavutil/intreadwrite.h"

static int ipu_read_probe(const AVProbeData *p)
{
    if (AV_RB32(p->buf) != MKBETAG('i', 'p', 'u', 'm'))
        return 0;

    if (AV_RL32(p->buf + 4) == 0)
        return 0;

    if (AV_RL16(p->buf + 8) == 0)
        return 0;

    if (AV_RL16(p->buf + 10) == 0)
        return 0;

    if (AV_RL32(p->buf + 12) == 0)
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int ipu_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    AVStream *st = avformat_new_stream(s, NULL);

    if (!st)
        return AVERROR(ENOMEM);
    avio_skip(pb, 8);
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id = AV_CODEC_ID_IPU;
    st->codecpar->width    = avio_rl16(pb);
    st->codecpar->height   = avio_rl16(pb);
    st->start_time         = 0;
    st->duration           =
    st->nb_frames          = avio_rl32(pb);
    ffstream(st)->need_parsing = AVSTREAM_PARSE_FULL_RAW;
    avpriv_set_pts_info(st, 64, 1, 25);

    return 0;
}

const AVInputFormat ff_ipu_demuxer = {
    .name           = "ipu",
    .long_name      = NULL_IF_CONFIG_SMALL("raw IPU Video"),
    .read_probe     = ipu_read_probe,
    .read_header    = ipu_read_header,
    .read_packet    = ff_raw_read_partial_packet,
    .extensions     = "ipu",
    .flags          = AVFMT_GENERIC_INDEX,
    .raw_codec_id   = AV_CODEC_ID_IPU,
    .priv_data_size = sizeof(FFRawDemuxerContext),
    .priv_class     = &ff_raw_demuxer_class,
};
