/*
 * Copyright (C) 2008  David Conrad
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

#include "libavcodec/get_bits.h"
#include "libavcodec/dirac.h"
#include "avformat.h"
#include "oggdec.h"

static int dirac_header(AVFormatContext *s, int idx)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    AVStream *st = s->streams[idx];
    dirac_source_params source;
    GetBitContext gb;

    // already parsed the header
    if (st->codec->codec_id == CODEC_ID_DIRAC)
        return 0;

    init_get_bits(&gb, os->buf + os->pstart + 13, (os->psize - 13) * 8);
    if (ff_dirac_parse_sequence_header(st->codec, &gb, &source) < 0)
        return -1;

    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id = CODEC_ID_DIRAC;
    // dirac in ogg always stores timestamps as though the video were interlaced
    st->time_base = (AVRational){st->codec->time_base.num, 2*st->codec->time_base.den};
    return 1;
}

// various undocument things: granule is signed (only for dirac!)
static uint64_t dirac_gptopts(AVFormatContext *s, int idx, uint64_t granule,
                              int64_t *dts_out)
{
    int64_t gp = granule;
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;

    unsigned dist  = ((gp >> 14) & 0xff00) | (gp & 0xff);
    int64_t  dts   = (gp >> 31);
    int64_t  pts   = dts + ((gp >> 9) & 0x1fff);

    if (!dist)
        os->pflags |= AV_PKT_FLAG_KEY;

    if (dts_out)
        *dts_out = dts;

    return pts;
}

static int old_dirac_header(AVFormatContext *s, int idx)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    AVStream *st = s->streams[idx];
    uint8_t *buf = os->buf + os->pstart;

    if (buf[0] != 'K')
        return 0;

    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id = CODEC_ID_DIRAC;
    st->time_base.den = AV_RB32(buf+8);
    st->time_base.num = AV_RB32(buf+12);
    return 1;
}

static uint64_t old_dirac_gptopts(AVFormatContext *s, int idx, uint64_t gp,
                                  int64_t *dts)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    uint64_t iframe = gp >> 30;
    uint64_t pframe = gp & 0x3fffffff;

    if (!pframe)
        os->pflags |= AV_PKT_FLAG_KEY;

    return iframe + pframe;
}

const struct ogg_codec ff_dirac_codec = {
    .magic = "BBCD\0",
    .magicsize = 5,
    .header = dirac_header,
    .gptopts = dirac_gptopts,
    .granule_is_start = 1,
};

const struct ogg_codec ff_old_dirac_codec = {
    .magic = "KW-DIRAC",
    .magicsize = 8,
    .header = old_dirac_header,
    .gptopts = old_dirac_gptopts,
    .granule_is_start = 1,
};
