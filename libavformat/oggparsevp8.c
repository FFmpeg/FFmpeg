/*
 * On2 VP8 parser for Ogg
 * Copyright (C) 2013 James Almer
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/intreadwrite.h"

#include "avformat.h"
#include "internal.h"
#include "oggdec.h"

#define VP8_HEADER_SIZE 26

static int vp8_header(AVFormatContext *s, int idx)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    uint8_t *p = os->buf + os->pstart;
    AVStream *st = s->streams[idx];
    AVRational framerate;

    if (os->psize < 7 || p[0] != 0x4f)
        return 0;

    switch (p[5]){
    case 0x01:
        if (os->psize < VP8_HEADER_SIZE) {
            av_log(s, AV_LOG_ERROR, "Invalid OggVP8 header packet");
            return AVERROR_INVALIDDATA;
        }

        if (p[6] != 1) {
            av_log(s, AV_LOG_WARNING,
                   "Unknown OggVP8 version %d.%d\n", p[6], p[7]);
            return AVERROR_INVALIDDATA;
        }

        st->codecpar->width         = AV_RB16(p +  8);
        st->codecpar->height        = AV_RB16(p + 10);
        st->sample_aspect_ratio.num = AV_RB24(p + 12);
        st->sample_aspect_ratio.den = AV_RB24(p + 15);
        framerate.num               = AV_RB32(p + 18);
        framerate.den               = AV_RB32(p + 22);

        avpriv_set_pts_info(st, 64, framerate.den, framerate.num);
        st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        st->codecpar->codec_id   = AV_CODEC_ID_VP8;
        st->need_parsing      = AVSTREAM_PARSE_HEADERS;
        break;
    case 0x02:
        if (p[6] != 0x20)
            return AVERROR_INVALIDDATA;
        ff_vorbis_stream_comment(s, st, p + 7, os->psize - 7);
        break;
    default:
        av_log(s, AV_LOG_ERROR, "Unknown VP8 header type 0x%02X\n", p[5]);
        return AVERROR_INVALIDDATA;
    }

    return 1;
}

static uint64_t vp8_gptopts(AVFormatContext *s, int idx,
                            uint64_t granule, int64_t *dts)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;

    uint64_t pts  = (granule >> 32);
    uint32_t dist = (granule >>  3) & 0x07ffffff;

    if (!dist)
        os->pflags |= AV_PKT_FLAG_KEY;

    if (dts)
        *dts = pts;

    return pts;
}

static int vp8_packet(AVFormatContext *s, int idx)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    uint8_t *p = os->buf + os->pstart;

    if ((!os->lastpts || os->lastpts == AV_NOPTS_VALUE) &&
        !(os->flags & OGG_FLAG_EOS)) {
        int seg;
        int duration;
        uint8_t *last_pkt = p;
        uint8_t *next_pkt;

        seg = os->segp;
        duration = (last_pkt[0] >> 4) & 1;
        next_pkt = last_pkt += os->psize;
        for (; seg < os->nsegs; seg++) {
            if (os->segments[seg] < 255) {
                duration += (last_pkt[0] >> 4) & 1;
                last_pkt  = next_pkt + os->segments[seg];
            }
            next_pkt += os->segments[seg];
        }
        os->lastpts =
        os->lastdts = vp8_gptopts(s, idx, os->granule, NULL) - duration;
        if(s->streams[idx]->start_time == AV_NOPTS_VALUE) {
            s->streams[idx]->start_time = os->lastpts;
            if (s->streams[idx]->duration)
                s->streams[idx]->duration -= s->streams[idx]->start_time;
        }
    }

    if (os->psize > 0)
        os->pduration = (p[0] >> 4) & 1;

    return 0;
}

const struct ogg_codec ff_vp8_codec = {
    .magic     = "OVP80",
    .magicsize = 5,
    .header    = vp8_header,
    .packet    = vp8_packet,
    .gptopts   = vp8_gptopts,
    .nb_header = 1,
};
