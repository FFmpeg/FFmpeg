/*
 * RAW DTS demuxer
 * Copyright (c) 2008 Benjamin Larsson
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

#include "libavcodec/bytestream.h"
#include "libavcodec/dca_syncwords.h"

#include "avformat.h"
#include "rawdec.h"

static int dts_probe(AVProbeData *p)
{
    const uint8_t *buf, *bufp;
    uint32_t state = -1;
    int markers[3] = {0};
    int sum, max;

    buf = p->buf;

    for(; buf < (p->buf+p->buf_size)-2; buf+=2) {
        bufp = buf;
        state = (state << 16) | bytestream_get_be16(&bufp);

        /* regular bitstream */
        if (state == DCA_SYNCWORD_CORE_BE || state == DCA_SYNCWORD_CORE_LE)
            markers[0]++;

        /* 14 bits big-endian bitstream */
        if (state == DCA_SYNCWORD_CORE_14B_BE)
            if ((bytestream_get_be16(&bufp) & 0xFFF0) == 0x07F0)
                markers[1]++;

        /* 14 bits little-endian bitstream */
        if (state == DCA_SYNCWORD_CORE_14B_LE)
            if ((bytestream_get_be16(&bufp) & 0xF0FF) == 0xF007)
                markers[2]++;
    }
    sum = markers[0] + markers[1] + markers[2];
    max = markers[1] > markers[0];
    max = markers[2] > markers[max] ? 2 : max;
    if (markers[max] > 3 && p->buf_size / markers[max] < 32*1024 &&
        markers[max] * 4 > sum * 3)
        return AVPROBE_SCORE_EXTENSION + 1;

    return 0;
}

AVInputFormat ff_dts_demuxer = {
    .name           = "dts",
    .long_name      = NULL_IF_CONFIG_SMALL("raw DTS"),
    .read_probe     = dts_probe,
    .read_header    = ff_raw_audio_read_header,
    .read_packet    = ff_raw_read_partial_packet,
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "dts",
    .raw_codec_id   = AV_CODEC_ID_DTS,
};
