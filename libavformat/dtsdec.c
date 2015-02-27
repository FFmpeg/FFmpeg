/*
 * RAW DTS demuxer
 * Copyright (c) 2008 Benjamin Larsson
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

#include "libavcodec/bytestream.h"
#include "libavcodec/get_bits.h"
#include "libavcodec/dca.h"
#include "avformat.h"
#include "rawdec.h"

#define DCA_MARKER_14B_BE 0x1FFFE800
#define DCA_MARKER_14B_LE 0xFF1F00E8
#define DCA_MARKER_RAW_BE 0x7FFE8001
#define DCA_MARKER_RAW_LE 0xFE7F0180

static int dts_probe(AVProbeData *p)
{
    const uint8_t *buf, *bufp;
    uint32_t state = -1;
    int markers[4*16] = {0};
    int sum, max, i;
    int64_t diff = 0;
    uint8_t hdr[12 + FF_INPUT_BUFFER_PADDING_SIZE] = { 0 };

    buf = p->buf + FFMIN(4096, p->buf_size);

    for(; buf < (p->buf+p->buf_size)-2; buf+=2) {
        int marker, sample_blocks, sample_rate, sr_code, framesize;
        int lfe;
        GetBitContext gb;

        bufp = buf;
        state = (state << 16) | bytestream_get_be16(&bufp);

        if (buf - p->buf >= 4)
            diff += FFABS(((int16_t)AV_RL16(buf)) - (int16_t)AV_RL16(buf-4));

        /* regular bitstream */
        if (state == DCA_MARKER_RAW_BE)
            marker = 0;
        else if (state == DCA_MARKER_RAW_LE)
            marker = 1;

        /* 14 bits big-endian bitstream */
        else if (state == DCA_MARKER_14B_BE &&
                 (bytestream_get_be16(&bufp) & 0xFFF0) == 0x07F0)
            marker = 2;

        /* 14 bits little-endian bitstream */
        else if (state == DCA_MARKER_14B_LE &&
                 (bytestream_get_be16(&bufp) & 0xF0FF) == 0xF007)
            marker = 3;
        else
            continue;

        if (avpriv_dca_convert_bitstream(buf-2, 12, hdr, 12) < 0)
            continue;

        init_get_bits(&gb, hdr, 96);
        skip_bits_long(&gb, 39);

        sample_blocks = get_bits(&gb, 7) + 1;
        if (sample_blocks < 8)
            continue;

        framesize = get_bits(&gb, 14) + 1;
        if (framesize < 95)
            continue;

        skip_bits(&gb, 6);
        sr_code = get_bits(&gb, 4);
        sample_rate = avpriv_dca_sample_rates[sr_code];
        if (sample_rate == 0)
            continue;

        get_bits(&gb, 5);
        if (get_bits(&gb, 1))
            continue;

        skip_bits_long(&gb, 9);
        lfe = get_bits(&gb, 2);
        if (lfe > 2)
            continue;

        marker += 4* sr_code;

        markers[marker] ++;
    }

    sum = max = 0;
    for (i=0; i<FF_ARRAY_ELEMS(markers); i++) {
        sum += markers[i];
        if (markers[max] < markers[i])
            max = i;
    }

    if (markers[max] > 3 && p->buf_size / markers[max] < 32*1024 &&
        markers[max] * 4 > sum * 3 &&
        diff / p->buf_size > 200)
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
