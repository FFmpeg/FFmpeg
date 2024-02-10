/*
 * MODS demuxer
 * Copyright (c) 2015-2016 Florian Nouwt
 * Copyright (c) 2017 Adib Surani
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

#include "libavutil/intreadwrite.h"

#include "avformat.h"
#include "demux.h"
#include "internal.h"

static int mods_probe(const AVProbeData *p)
{
    if (memcmp(p->buf, "MODSN3\x0a\x00", 8))
        return 0;
    if (AV_RB32(p->buf + 8) == 0)
        return 0;
    if (AV_RB32(p->buf + 12) == 0)
        return 0;
    if (AV_RB32(p->buf + 16) == 0)
        return 0;
    return AVPROBE_SCORE_MAX;
}

static int mods_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    AVRational fps;
    int64_t pos;

    AVStream *st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    avio_skip(pb, 8);

    st->nb_frames            = avio_rl32(pb);
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = AV_CODEC_ID_MOBICLIP;
    st->codecpar->width      = avio_rl32(pb);
    st->codecpar->height     = avio_rl32(pb);

    fps.num = avio_rl32(pb);
    fps.den = 0x1000000;
    avpriv_set_pts_info(st, 64, fps.den, fps.num);

    avio_skip(pb, 16);

    pos = avio_rl32(pb) + 4;
    avio_seek(pb, pos, SEEK_SET);
    pos = avio_rl32(pb);
    avio_seek(pb, pos, SEEK_SET);

    return 0;
}

static int mods_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *pb = s->pb;
    unsigned size;
    int64_t pos;
    int ret;

    if (avio_feof(pb))
        return AVERROR_EOF;

    pos = avio_tell(pb);
    size = avio_rl32(pb) >> 14;
    ret = av_get_packet(pb, pkt, size);
    pkt->pos = pos;
    pkt->stream_index = 0;
    pkt->flags |= AV_PKT_FLAG_KEY;

    return ret;
}

const FFInputFormat ff_mods_demuxer = {
    .p.name         = "mods",
    .p.long_name    = NULL_IF_CONFIG_SMALL("MobiClip MODS"),
    .p.extensions   = "mods",
    .p.flags        = AVFMT_GENERIC_INDEX,
    .read_probe     = mods_probe,
    .read_header    = mods_read_header,
    .read_packet    = mods_read_packet,
};
