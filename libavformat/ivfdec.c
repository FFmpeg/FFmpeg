/*
 * Copyright (c) 2010 David Conrad
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
#include "riff.h"
#include "libavutil/intreadwrite.h"

static int probe(const AVProbeData *p)
{
    if (AV_RL32(p->buf) == MKTAG('D','K','I','F')
        && !AV_RL16(p->buf+4) && AV_RL16(p->buf+6) == 32)
        return AVPROBE_SCORE_MAX-2;

    return 0;
}

static int read_header(AVFormatContext *s)
{
    AVStream *st;
    AVRational time_base;

    avio_rl32(s->pb); // DKIF
    avio_rl16(s->pb); // version
    avio_rl16(s->pb); // header size

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);


    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_tag  = avio_rl32(s->pb);
    st->codecpar->codec_id   = ff_codec_get_id(ff_codec_bmp_tags, st->codecpar->codec_tag);
    st->codecpar->width      = avio_rl16(s->pb);
    st->codecpar->height     = avio_rl16(s->pb);
    time_base.den            = avio_rl32(s->pb);
    time_base.num            = avio_rl32(s->pb);
    st->nb_frames            = avio_rl32(s->pb);
    avio_skip(s->pb, 4); // unused

    // Infer duration from nb_frames, in order to be backward compatible with
    // previous IVF demuxer.
    // It is popular to configure time_base to 1/frame_rate by IVF muxer, that
    // the duration happens to be the same with nb_frames. See
    // `https://chromium.googlesource.com/webm/vp8-test-vectors/+/refs/heads/main`
    st->duration             = st->nb_frames;

    ffstream(st)->need_parsing = AVSTREAM_PARSE_HEADERS;

    if (!time_base.den || !time_base.num) {
        av_log(s, AV_LOG_ERROR, "Invalid frame rate\n");
        return AVERROR_INVALIDDATA;
    }

    avpriv_set_pts_info(st, 64, time_base.num, time_base.den);

    return 0;
}

static int read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, size = avio_rl32(s->pb);
    int64_t   pts = avio_rl64(s->pb);

    ret = av_get_packet(s->pb, pkt, size);
    pkt->stream_index = 0;
    pkt->pts          = pts;
    pkt->pos         -= 12;

    return ret;
}

const FFInputFormat ff_ivf_demuxer = {
    .p.name         = "ivf",
    .p.long_name    = NULL_IF_CONFIG_SMALL("On2 IVF"),
    .p.flags        = AVFMT_GENERIC_INDEX,
    .p.codec_tag    = (const AVCodecTag* const []){ ff_codec_bmp_tags, 0 },
    .read_probe     = probe,
    .read_header    = read_header,
    .read_packet    = read_packet,
};
