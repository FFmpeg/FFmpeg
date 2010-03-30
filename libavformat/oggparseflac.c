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

        ff_flac_parse_streaminfo(st->codec, &si, streaminfo_start);

        st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codec->codec_id = CODEC_ID_FLAC;

        st->codec->extradata =
            av_malloc(FLAC_STREAMINFO_SIZE + FF_INPUT_BUFFER_PADDING_SIZE);
        memcpy(st->codec->extradata, streaminfo_start, FLAC_STREAMINFO_SIZE);
        st->codec->extradata_size = FLAC_STREAMINFO_SIZE;

        st->time_base.num = 1;
        st->time_base.den = st->codec->sample_rate;
    } else if (mdt == FLAC_METADATA_TYPE_VORBIS_COMMENT) {
        ff_vorbis_comment (s, &st->metadata, os->buf + os->pstart + 4, os->psize - 4);
    }

    return 1;
}

static int
old_flac_header (AVFormatContext * s, int idx)
{
    AVStream *st = s->streams[idx];
    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id = CODEC_ID_FLAC;

    return 0;
}

const struct ogg_codec ff_flac_codec = {
    .magic = "\177FLAC",
    .magicsize = 5,
    .header = flac_header
};

const struct ogg_codec ff_old_flac_codec = {
    .magic = "fLaC",
    .magicsize = 4,
    .header = old_flac_header
};
