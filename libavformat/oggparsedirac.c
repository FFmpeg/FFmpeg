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

#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavcodec/dirac.h"
#include "avformat.h"
#include "internal.h"
#include "oggdec.h"

static int dirac_header(AVFormatContext *s, int idx)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    AVStream *st = s->streams[idx];
    AVDiracSeqHeader *dsh;
    int ret;

    // already parsed the header
    if (st->codecpar->codec_id == AV_CODEC_ID_DIRAC)
        return 0;

    ret = av_dirac_parse_sequence_header(&dsh, os->buf + os->pstart + 13, (os->psize - 13), s);
    if (ret < 0)
        return ret;

    st->codecpar->codec_type      = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id        = AV_CODEC_ID_DIRAC;
    st->codecpar->width           = dsh->width;
    st->codecpar->height          = dsh->height;
    st->codecpar->format          = dsh->pix_fmt;
    st->codecpar->color_range     = dsh->color_range;
    st->codecpar->color_trc       = dsh->color_trc;
    st->codecpar->color_primaries = dsh->color_primaries;
    st->codecpar->color_space     = dsh->colorspace;
    st->codecpar->profile         = dsh->profile;
    st->codecpar->level           = dsh->level;
    if (av_image_check_sar(st->codecpar->width, st->codecpar->height, dsh->sample_aspect_ratio) >= 0)
        st->sample_aspect_ratio = dsh->sample_aspect_ratio;

    // Dirac in Ogg always stores timestamps as though the video were interlaced
    avpriv_set_pts_info(st, 64, dsh->framerate.den, 2 * dsh->framerate.num);

    av_freep(&dsh);
    return 1;
}

// various undocumented things: granule is signed (only for Dirac!)
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

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id = AV_CODEC_ID_DIRAC;
    avpriv_set_pts_info(st, 64, AV_RB32(buf+12), AV_RB32(buf+8));
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
    .nb_header = 1,
};

const struct ogg_codec ff_old_dirac_codec = {
    .magic = "KW-DIRAC",
    .magicsize = 8,
    .header = old_dirac_header,
    .gptopts = old_dirac_gptopts,
    .granule_is_start = 1,
    .nb_header = 1,
};
