/*
 *    Copyright (C) 2005  Matthieu CASTET
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdlib.h>
#include "avformat.h"
#include "bitstream.h"
#include "ogg2.h"

#define FLAC_STREAMINFO_SIZE 0x22

static int
flac_header (AVFormatContext * s, int idx)
{
    ogg_t *ogg = s->priv_data;
    ogg_stream_t *os = ogg->streams + idx;
    AVStream *st = s->streams[idx];
    GetBitContext gb;
    int mdt;

    if (os->buf[os->pstart] == 0xff)
        return 0;

    init_get_bits(&gb, os->buf + os->pstart, os->psize*8);
    get_bits(&gb, 1); /* metadata_last */
    mdt = get_bits(&gb, 7);

    if (mdt == 0x7f) {
        skip_bits(&gb, 4*8); /* "FLAC" */
        if(get_bits(&gb, 8) != 1) /* unsupported major version */
            return -1;
        skip_bits(&gb, 8 + 16);      /* minor version + header count */
        skip_bits(&gb, 4*8); /* "fLaC" */
    
        /* METADATA_BLOCK_HEADER */
        if (get_bits(&gb, 32) != FLAC_STREAMINFO_SIZE)
            return -1;

        skip_bits(&gb, 16*2+24*2);

        st->codec->sample_rate = get_bits_long(&gb, 20);
        st->codec->channels = get_bits(&gb, 3) + 1;
    
        st->codec->codec_type = CODEC_TYPE_AUDIO;
        st->codec->codec_id = CODEC_ID_FLAC;

        st->codec->extradata =
            av_malloc(FLAC_STREAMINFO_SIZE + FF_INPUT_BUFFER_PADDING_SIZE);
        memcpy (st->codec->extradata, os->buf + os->pstart + 5 + 4 + 4 + 4,
                FLAC_STREAMINFO_SIZE);
        st->codec->extradata_size = FLAC_STREAMINFO_SIZE;
    } else if (mdt == 4) {
        vorbis_comment (s, os->buf + os->pstart + 4, os->psize - 4);
    }

    return 1;
}

ogg_codec_t flac_codec = {
    .magic = "\177FLAC",
    .magicsize = 5,
    .header = flac_header
};
