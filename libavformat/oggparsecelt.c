/*
 * Xiph CELT parser for Ogg
 * Copyright (c) 2011 Nicolas George
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

#include <string.h>

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"
#include "oggdec.h"

struct oggcelt_private {
    int extra_headers_left;
};

static int celt_header(AVFormatContext *s, int idx)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    AVStream *st = s->streams[idx];
    struct oggcelt_private *priv = os->private;
    uint8_t *p = os->buf + os->pstart;

    if (os->psize == 60 &&
        !memcmp(p, ff_celt_codec.magic, ff_celt_codec.magicsize)) {
        /* Main header */

        uint32_t version, sample_rate, nb_channels, frame_size;
        uint32_t overlap, extra_headers;

        priv = av_malloc(sizeof(struct oggcelt_private));
        if (!priv)
            return AVERROR(ENOMEM);
        if (ff_alloc_extradata(st->codec, 2 * sizeof(uint32_t)) < 0)
            return AVERROR(ENOMEM);
        version          = AV_RL32(p + 28);
        /* unused header size field skipped */
        sample_rate      = AV_RL32(p + 36);
        nb_channels      = AV_RL32(p + 40);
        frame_size       = AV_RL32(p + 44);
        overlap          = AV_RL32(p + 48);
        /* unused bytes per packet field skipped */
        extra_headers    = AV_RL32(p + 56);
        st->codec->codec_type     = AVMEDIA_TYPE_AUDIO;
        st->codec->codec_id       = AV_CODEC_ID_CELT;
        st->codec->sample_rate    = sample_rate;
        st->codec->channels       = nb_channels;
        st->codec->frame_size     = frame_size;
        if (sample_rate)
            avpriv_set_pts_info(st, 64, 1, sample_rate);
        priv->extra_headers_left  = 1 + extra_headers;
        av_free(os->private);
        os->private = priv;
        AV_WL32(st->codec->extradata + 0, overlap);
        AV_WL32(st->codec->extradata + 4, version);
        return 1;
    } else if (priv && priv->extra_headers_left) {
        /* Extra headers (vorbiscomment) */

        ff_vorbis_comment(s, &st->metadata, p, os->psize);
        priv->extra_headers_left--;
        return 1;
    } else {
        return 0;
    }
}

const struct ogg_codec ff_celt_codec = {
    .magic     = "CELT    ",
    .magicsize = 8,
    .header    = celt_header,
    .nb_header = 2,
};
