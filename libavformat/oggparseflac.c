/*
 *    Copyright (C) 2005  Matthieu CASTET
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
#include "libavcodec/get_bits.h"
#include "libavcodec/flac.h"
#include "avformat.h"
#include "internal.h"
#include "oggdec.h"

#define OGG_FLAC_METADATA_TYPE_STREAMINFO 0x7F

static int
flac_header (AVFormatContext * s, int idx)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    AVStream *st = s->streams[idx];
    GetBitContext gb;
    FLACStreaminfo si;
    int mdt;

    if (os->buf[os->pstart] == 0xff)
        return 0;

    init_get_bits(&gb, os->buf + os->pstart, os->psize*8);
    skip_bits1(&gb); /* metadata_last */
    mdt = get_bits(&gb, 7);

    if (mdt == OGG_FLAC_METADATA_TYPE_STREAMINFO) {
        uint8_t *streaminfo_start = os->buf + os->pstart + 5 + 4 + 4 + 4;
        skip_bits_long(&gb, 4*8); /* "FLAC" */
        if(get_bits(&gb, 8) != 1) /* unsupported major version */
            return -1;
        skip_bits_long(&gb, 8 + 16); /* minor version + header count */
        skip_bits_long(&gb, 4*8); /* "fLaC" */

        /* METADATA_BLOCK_HEADER */
        if (get_bits_long(&gb, 32) != FLAC_STREAMINFO_SIZE)
            return -1;

        avpriv_flac_parse_streaminfo(st->codec, &si, streaminfo_start);

        st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codec->codec_id = AV_CODEC_ID_FLAC;
        st->need_parsing = AVSTREAM_PARSE_HEADERS;

        if (ff_alloc_extradata(st->codec, FLAC_STREAMINFO_SIZE) < 0)
            return AVERROR(ENOMEM);
        memcpy(st->codec->extradata, streaminfo_start, st->codec->extradata_size);

        avpriv_set_pts_info(st, 64, 1, st->codec->sample_rate);
    } else if (mdt == FLAC_METADATA_TYPE_VORBIS_COMMENT) {
        ff_vorbis_comment (s, &st->metadata, os->buf + os->pstart + 4, os->psize - 4);
    }

    return 1;
}

static int
old_flac_header (AVFormatContext * s, int idx)
{
    struct ogg *ogg = s->priv_data;
    AVStream *st = s->streams[idx];
    struct ogg_stream *os = ogg->streams + idx;
    AVCodecParserContext *parser = av_parser_init(AV_CODEC_ID_FLAC);
    int size;
    uint8_t *data;

    if (!parser)
        return -1;

    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id = AV_CODEC_ID_FLAC;

    parser->flags = PARSER_FLAG_COMPLETE_FRAMES;
    av_parser_parse2(parser, st->codec,
                     &data, &size, os->buf + os->pstart, os->psize,
                     AV_NOPTS_VALUE, AV_NOPTS_VALUE, -1);

    av_parser_close(parser);

    if (st->codec->sample_rate) {
        avpriv_set_pts_info(st, 64, 1, st->codec->sample_rate);
        return 0;
    }

    return 1;
}

const struct ogg_codec ff_flac_codec = {
    .magic = "\177FLAC",
    .magicsize = 5,
    .header = flac_header,
    .nb_header = 2,
};

const struct ogg_codec ff_old_flac_codec = {
    .magic = "fLaC",
    .magicsize = 4,
    .header = old_flac_header,
    .nb_header = 0,
};
