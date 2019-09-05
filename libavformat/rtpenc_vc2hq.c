/*
 * RTP packetizer for VC-2 HQ payload format (draft version 1) - experimental
 * Copyright (c) 2016 Thomas Volkert <thomas@netzeal.de>
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

#include "libavutil/intreadwrite.h"
#include "libavcodec/dirac.h"
#include "libavcodec/get_bits.h"
#include "libavcodec/golomb.h"

#include "avformat.h"
#include "rtpenc.h"

#define RTP_VC2HQ_PL_HEADER_SIZE             4

#define DIRAC_DATA_UNIT_HEADER_SIZE          13
#define DIRAC_PIC_NR_SIZE                    4
#define DIRAC_RTP_PCODE_HQ_PIC_FRAGMENT      0xEC

static void send_packet(AVFormatContext *ctx, uint8_t parse_code, int info_hdr_size, const uint8_t *buf, int size, int i, int f, int rtp_m)
{
    RTPMuxContext *rtp_ctx = ctx->priv_data;

    AV_WB16(&rtp_ctx->buf[0], 0); /* extended sequence number */
    AV_WB8 (&rtp_ctx->buf[2], i ? (f ? (0x03) : (0x02)) : 0x00); /* flags: interlaced, second field */
    AV_WB8 (&rtp_ctx->buf[3], parse_code);
    if (size > 0)
        memcpy(&rtp_ctx->buf[4 + info_hdr_size], buf, size);
    ff_rtp_send_data(ctx, rtp_ctx->buf, RTP_VC2HQ_PL_HEADER_SIZE + info_hdr_size + size, rtp_m);
}

static void send_picture(AVFormatContext *ctx, const uint8_t *buf, int size, int interlaced)
{
    RTPMuxContext *rtp_ctx = ctx->priv_data;
    GetBitContext gc;
    int lvl, second_field;
    uint32_t pic_nr, wavelet_depth, prefix_bytes, size_scaler;
    uint16_t frag_len;
    char *info_hdr = &rtp_ctx->buf[4];

    pic_nr = AV_RB32(&buf[0]);
    buf += DIRAC_PIC_NR_SIZE;
    size -= DIRAC_PIC_NR_SIZE;
    second_field = interlaced && (pic_nr & 0x01);

    init_get_bits(&gc, buf, 8  * size);
                    get_interleaved_ue_golomb(&gc); /* wavelet_idx */
    wavelet_depth = get_interleaved_ue_golomb(&gc);
                    get_interleaved_ue_golomb(&gc); /* num_x */
                    get_interleaved_ue_golomb(&gc); /* num_y */
    prefix_bytes  = get_interleaved_ue_golomb(&gc);
    size_scaler   = get_interleaved_ue_golomb(&gc);
    /* pass the quantization matrices */
    get_interleaved_ue_golomb(&gc);
    for(lvl = 0; lvl < wavelet_depth; lvl++)
    {
        get_interleaved_ue_golomb(&gc);
        get_interleaved_ue_golomb(&gc);
        get_interleaved_ue_golomb(&gc);
    }

    frag_len = (get_bits_count(&gc) + 7) / 8; /* length of transform parameters */

    AV_WB32(&info_hdr[ 0], pic_nr);
    AV_WB16(&info_hdr[ 4], prefix_bytes);
    AV_WB16(&info_hdr[ 6], size_scaler);
    AV_WB16(&info_hdr[ 8], frag_len);
    AV_WB16(&info_hdr[10], 0 /* nr. of slices */);
    send_packet(ctx, DIRAC_RTP_PCODE_HQ_PIC_FRAGMENT, 12, buf, frag_len, interlaced, second_field, 0);
    buf += frag_len;
    size -= frag_len;

    while (size > 0) {
        frag_len = FFMIN(rtp_ctx->max_payload_size - 20 /* pl header */, size);
        AV_WB16(&info_hdr[ 8], frag_len);
        AV_WB16(&info_hdr[10], 1 /* nr. of slices */);
        AV_WB16(&info_hdr[12], 0 /* slice x */);
        AV_WB16(&info_hdr[14], 0 /* slice y */);

        size -= frag_len;
        send_packet(ctx, DIRAC_RTP_PCODE_HQ_PIC_FRAGMENT, 16, buf, frag_len, interlaced, second_field, size > 0 ? 0 : 1);
        buf += frag_len;
    }
}

void ff_rtp_send_vc2hq(AVFormatContext *ctx, const uint8_t *frame_buf, int frame_size, int interlaced)
{
    const uint8_t *end = frame_buf + frame_size;
    const uint8_t *unit = frame_buf;
    uint8_t parse_code;
    uint32_t unit_size;

    while (unit < end) {
        parse_code = unit[4];
        unit_size = AV_RB32(&unit[5]);

        switch (parse_code) {
        /* sequence header */
        /* end of sequence */
        case DIRAC_PCODE_SEQ_HEADER:
        case DIRAC_PCODE_END_SEQ:
            send_packet(ctx, parse_code, 0, unit + DIRAC_DATA_UNIT_HEADER_SIZE, unit_size - DIRAC_DATA_UNIT_HEADER_SIZE, 0, 0, 0);
            break;
        /* HQ picture */
        case DIRAC_PCODE_PICTURE_HQ:
            send_picture(ctx, unit + DIRAC_DATA_UNIT_HEADER_SIZE, unit_size - DIRAC_DATA_UNIT_HEADER_SIZE, interlaced);
            break;
        /* parse codes without specification */
        case DIRAC_PCODE_AUX:
        case DIRAC_PCODE_PAD:
            break;
        default:
            avpriv_report_missing_feature(ctx, "VC-2 parse code %d", parse_code);
            break;
        }
        unit += unit_size;
    }
}
