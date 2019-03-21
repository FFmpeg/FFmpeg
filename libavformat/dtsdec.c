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

#include "libavutil/crc.h"

#include "libavcodec/bytestream.h"
#include "libavcodec/dca.h"
#include "libavcodec/dca_syncwords.h"
#include "libavcodec/get_bits.h"

#include "avformat.h"
#include "rawdec.h"

static int dts_probe(const AVProbeData *p)
{
    const uint8_t *buf, *bufp;
    uint32_t state = -1;
    int markers[4*16] = {0};
    int exss_markers = 0, exss_nextpos = 0;
    int sum, max, pos, ret, i;
    int64_t diff = 0;
    uint8_t hdr[DCA_CORE_FRAME_HEADER_SIZE + AV_INPUT_BUFFER_PADDING_SIZE] = { 0 };

    for (pos = FFMIN(4096, p->buf_size); pos < p->buf_size - 2; pos += 2) {
        int marker, wide_hdr, hdr_size, framesize;
        DCACoreFrameHeader h;
        GetBitContext gb;

        bufp = buf = p->buf + pos;
        state = (state << 16) | bytestream_get_be16(&bufp);

        if (pos >= 4)
            diff += FFABS(((int16_t)AV_RL16(buf)) - (int16_t)AV_RL16(buf-4));

        /* extension substream (EXSS) */
        if (state == DCA_SYNCWORD_SUBSTREAM) {
            if (pos < exss_nextpos)
                continue;

            init_get_bits(&gb, buf - 2, 96);
            skip_bits_long(&gb, 42);

            wide_hdr  = get_bits1(&gb);
            hdr_size  = get_bits(&gb,  8 + 4 * wide_hdr) + 1;
            framesize = get_bits(&gb, 16 + 4 * wide_hdr) + 1;
            if (hdr_size & 3 || framesize & 3)
                continue;
            if (hdr_size < 16 || framesize < hdr_size)
                continue;
            if (pos - 2 + hdr_size > p->buf_size)
                continue;
            if (av_crc(av_crc_get_table(AV_CRC_16_CCITT), 0xffff, buf + 3, hdr_size - 5))
                continue;

            if (pos == exss_nextpos)
                exss_markers++;
            else
                exss_markers = FFMAX(1, exss_markers - 1);
            exss_nextpos = pos + framesize;
            continue;
        }

        /* regular bitstream */
        if (state == DCA_SYNCWORD_CORE_BE &&
            (bytestream_get_be16(&bufp) & 0xFC00) == 0xFC00)
            marker = 0;
        else if (state == DCA_SYNCWORD_CORE_LE &&
                 (bytestream_get_be16(&bufp) & 0x00FC) == 0x00FC)
            marker = 1;

        /* 14 bits big-endian bitstream */
        else if (state == DCA_SYNCWORD_CORE_14B_BE &&
                 (bytestream_get_be16(&bufp) & 0xFFF0) == 0x07F0)
            marker = 2;

        /* 14 bits little-endian bitstream */
        else if (state == DCA_SYNCWORD_CORE_14B_LE &&
                 (bytestream_get_be16(&bufp) & 0xF0FF) == 0xF007)
            marker = 3;
        else
            continue;

        if ((ret = avpriv_dca_convert_bitstream(buf - 2, DCA_CORE_FRAME_HEADER_SIZE,
                                                hdr,     DCA_CORE_FRAME_HEADER_SIZE)) < 0)
            continue;
        if (avpriv_dca_parse_core_frame_header(&h, hdr, ret) < 0)
            continue;

        marker += 4 * h.sr_code;

        markers[marker] ++;
    }

    if (exss_markers > 3)
        return AVPROBE_SCORE_EXTENSION + 1;

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
