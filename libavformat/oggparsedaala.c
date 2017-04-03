/*
 * Ogg Daala parser
 * Copyright (C) 2015 Rostislav Pehlivanov <atomnuker gmail com>
 * Copyright (C) 2015 Vittorio Giovara <vittorio.giovara gmail com>
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

#include <stdlib.h>
#include "libavcodec/bytestream.h"
#include "avformat.h"
#include "internal.h"
#include "oggdec.h"

struct DaalaPixFmtMap {
    enum AVPixelFormat ffmpeg_fmt;
    int depth;
    int planes;
    int xdec[4];
    int ydec[4];
};

/* Currently supported formats only */
static const struct DaalaPixFmtMap list_fmts[] = {
    { AV_PIX_FMT_YUV420P,  8, 3, {0, 1, 1, 0}, {0, 1, 1, 0} },
    { AV_PIX_FMT_YUV444P,  8, 3, {0, 0, 0, 0}, {0, 0, 0, 0} }
};

typedef struct DaalaInfoHeader {
    int init_d;
    int fpr;
    int gpshift;
    int gpmask;
    int version_maj;
    int version_min;
    int version_sub;
    int frame_duration;
    int keyframe_granule_shift;
    struct DaalaPixFmtMap format;
} DaalaInfoHeader;

static inline int daala_match_pix_fmt(struct DaalaPixFmtMap *fmt)
{
    int i, j;
    for (i = 0; i < FF_ARRAY_ELEMS(list_fmts); i++) {
        int match = 0;
        if (fmt->depth != list_fmts[i].depth)
            continue;
        if (fmt->planes != list_fmts[i].planes)
            continue;
        for (j = 0; j < fmt->planes; j++) {
            if (fmt->xdec[j] != list_fmts[i].xdec[j])
                continue;
            if (fmt->ydec[j] != list_fmts[i].ydec[j])
                continue;
            match++;
        }
        if (match == fmt->planes)
            return list_fmts[i].ffmpeg_fmt;
    }
    return -1;
}

static int daala_header(AVFormatContext *s, int idx)
{
    int i, err;
    uint8_t *cdp;
    GetByteContext gb;
    AVRational timebase;
    struct ogg *ogg        = s->priv_data;
    struct ogg_stream *os  = ogg->streams + idx;
    AVStream *st           = s->streams[idx];
    int cds                = st->codecpar->extradata_size + os->psize + 2;
    DaalaInfoHeader *hdr   = os->private;

    if (!(os->buf[os->pstart] & 0x80))
        return 0;

    if (!hdr) {
        hdr = av_mallocz(sizeof(*hdr));
        if (!hdr)
            return AVERROR(ENOMEM);
        os->private = hdr;
    }

    switch (os->buf[os->pstart]) {
    case 0x80:
        bytestream2_init(&gb, os->buf + os->pstart, os->psize);
        bytestream2_skip(&gb, ff_daala_codec.magicsize);

        hdr->version_maj = bytestream2_get_byte(&gb);
        hdr->version_min = bytestream2_get_byte(&gb);
        hdr->version_sub = bytestream2_get_byte(&gb);

        st->codecpar->width  = bytestream2_get_ne32(&gb);
        st->codecpar->height = bytestream2_get_ne32(&gb);

        st->sample_aspect_ratio.num = bytestream2_get_ne32(&gb);
        st->sample_aspect_ratio.den = bytestream2_get_ne32(&gb);

        timebase.num = bytestream2_get_ne32(&gb);
        timebase.den = bytestream2_get_ne32(&gb);
        if (timebase.num < 0 && timebase.den < 0) {
            av_log(s, AV_LOG_WARNING, "Invalid timebase, assuming 30 FPS\n");
            timebase.num = 1;
            timebase.den = 30;
        }
        avpriv_set_pts_info(st, 64, timebase.den, timebase.num);

        hdr->frame_duration = bytestream2_get_ne32(&gb);
        hdr->gpshift = bytestream2_get_byte(&gb);
        if (hdr->gpshift >= 32) {
            av_log(s, AV_LOG_ERROR, "Too large gpshift %d (>= 32).\n",
                   hdr->gpshift);
            hdr->gpshift = 0;
            return AVERROR_INVALIDDATA;
        }
        hdr->gpmask  = (1U << hdr->gpshift) - 1;

        hdr->format.depth  = 8 + 2*(bytestream2_get_byte(&gb)-1);

        hdr->fpr = bytestream2_get_byte(&gb);

        hdr->format.planes = bytestream2_get_byte(&gb);
        if (hdr->format.planes > 4) {
            av_log(s, AV_LOG_ERROR,
                   "Invalid number of planes %d in daala pixel format map.\n",
                   hdr->format.planes);
            return AVERROR_INVALIDDATA;
        }
        for (i = 0; i < hdr->format.planes; i++) {
            hdr->format.xdec[i] = bytestream2_get_byte(&gb);
            hdr->format.ydec[i] = bytestream2_get_byte(&gb);
        }

        if ((st->codecpar->format = daala_match_pix_fmt(&hdr->format)) < 0)
            av_log(s, AV_LOG_ERROR, "Unsupported pixel format - %i %i\n",
                   hdr->format.depth, hdr->format.planes);

        st->codecpar->codec_id   = AV_CODEC_ID_DAALA;
        st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        st->need_parsing         = AVSTREAM_PARSE_HEADERS;

        hdr->init_d = 1;
        break;
    case 0x81:
        if (!hdr->init_d)
            return AVERROR_INVALIDDATA;
        ff_vorbis_stream_comment(s, st,
                                 os->buf + os->pstart + ff_daala_codec.magicsize,
                                 os->psize - ff_daala_codec.magicsize);
        break;
    case 0x82:
        if (!hdr->init_d)
            return AVERROR_INVALIDDATA;
        break;
    default:
        av_log(s, AV_LOG_ERROR, "Unknown header type %X\n", os->buf[os->pstart]);
        return AVERROR_INVALIDDATA;
        break;
    }

    if ((err = av_reallocp(&st->codecpar->extradata,
                           cds + AV_INPUT_BUFFER_PADDING_SIZE)) < 0) {
        st->codecpar->extradata_size = 0;
        return err;
    }

    memset(st->codecpar->extradata + cds, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    cdp    = st->codecpar->extradata + st->codecpar->extradata_size;
    *cdp++ = os->psize >> 8;
    *cdp++ = os->psize & 0xff;
    memcpy(cdp, os->buf + os->pstart, os->psize);
    st->codecpar->extradata_size = cds;

    return 1;
}

static uint64_t daala_gptopts(AVFormatContext *ctx, int idx, uint64_t gp,
                              int64_t *dts)
{
    uint64_t iframe, pframe;
    struct ogg *ogg       = ctx->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    DaalaInfoHeader *hdr  = os->private;

    if (!hdr)
        return AV_NOPTS_VALUE;

    iframe = gp >> hdr->gpshift;
    pframe = gp  & hdr->gpmask;

    if (!pframe)
        os->pflags |= AV_PKT_FLAG_KEY;

    if (dts)
        *dts = iframe + pframe;

    return iframe + pframe;
}

static int daala_packet(AVFormatContext *s, int idx)
{
    int seg, duration = 1;
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;

    /*
     * first packet handling: here we parse the duration of each packet in the
     * first page and compare the total duration to the page granule to find the
     * encoder delay and set the first timestamp
     */

    if ((!os->lastpts || os->lastpts == AV_NOPTS_VALUE) && !(os->flags & OGG_FLAG_EOS)) {
        for (seg = os->segp; seg < os->nsegs; seg++)
            if (os->segments[seg] < 255)
                duration++;

        os->lastpts = os->lastdts = daala_gptopts(s, idx, os->granule, NULL) - duration;
        if(s->streams[idx]->start_time == AV_NOPTS_VALUE) {
            s->streams[idx]->start_time = os->lastpts;
            if (s->streams[idx]->duration != AV_NOPTS_VALUE)
                s->streams[idx]->duration -= s->streams[idx]->start_time;
        }
    }

    /* parse packet duration */
    if (os->psize > 0)
        os->pduration = 1;

    return 0;
}

const struct ogg_codec ff_daala_codec = {
    .name             = "Daala",
    .magic            = "\200daala",
    .magicsize        = 6,
    .header           = daala_header,
    .packet           = daala_packet,
    .gptopts          = daala_gptopts,
    .granule_is_start = 1,
    .nb_header        = 3,
};
