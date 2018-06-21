/*
 * RTP parser for H.261 payload format (RFC 4587)
 * Copyright (c) 2014 Thomas Volkert <thomas@homer-conferencing.com>
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

#include "libavcodec/get_bits.h"
#include "avformat.h"
#include "avio_internal.h"
#include "rtpdec_formats.h"

#define RTP_H261_PAYLOAD_HEADER_SIZE 4

struct PayloadContext {
    AVIOContext *buf;
    uint8_t      endbyte;
    int          endbyte_bits;
    uint32_t     timestamp;
};

static av_cold void h261_close_context(PayloadContext *pl_ctx)
{
    /* return if context is invalid */
    if (!pl_ctx)
        return;

    /* free buffer if it is valid */
    ffio_free_dyn_buf(&pl_ctx->buf);
}

static int h261_handle_packet(AVFormatContext *ctx, PayloadContext *rtp_h261_ctx,
                              AVStream *st, AVPacket *pkt, uint32_t *timestamp,
                              const uint8_t *buf, int len, uint16_t seq,
                              int flags)
{
    int sbit, ebit, gobn, mbap, quant;
    int res;

    /* drop data of previous packets in case of non-continuous (lossy) packet stream */
    if (rtp_h261_ctx->buf && rtp_h261_ctx->timestamp != *timestamp) {
        ffio_free_dyn_buf(&rtp_h261_ctx->buf);
        rtp_h261_ctx->endbyte_bits = 0;
    }

    /* sanity check for size of input packet: 1 byte payload at least */
    if (len < RTP_H261_PAYLOAD_HEADER_SIZE + 1) {
        av_log(ctx, AV_LOG_ERROR, "Too short RTP/H.261 packet, got %d bytes\n", len);
        return AVERROR_INVALIDDATA;
    }

    /*
     * decode the H.261 payload header according to section 4.1 of RFC 4587:
     * (uses 4 bytes between RTP header and H.261 stream per packet)
     *
     *    0                   1                   2                   3
     *    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     *   |SBIT |EBIT |I|V| GOBN  |   MBAP  |  QUANT  |  HMVD   |  VMVD   |
     *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     *
     *      Start bit position (SBIT): 3 bits
     *      End bit position (EBIT): 3 bits
     *      INTRA-frame encoded data (I): 1 bit
     *      Motion Vector flag (V): 1 bit
     *      GOB number (GOBN): 4 bits
     *      Macroblock address predictor (MBAP): 5 bits
     *      Quantizer (QUANT): 5 bits
     *      Horizontal motion vector data (HMVD): 5 bits
     *      Vertical motion vector data (VMVD): 5 bits
     */
    sbit  =  (buf[0] >> 5) & 0x07;
    ebit  =  (buf[0] >> 2) & 0x07;
    gobn  =  (buf[1] >> 4) & 0x0f;
    mbap  = ((buf[1] << 1) & 0x1e) | ((buf[2] >> 7) & 0x01);
    quant =  (buf[2] >> 2) & 0x1f;

    /* pass the H.261 payload header and continue with the actual payload */
    buf += RTP_H261_PAYLOAD_HEADER_SIZE;
    len -= RTP_H261_PAYLOAD_HEADER_SIZE;

    /* start frame buffering with new dynamic buffer */
    if (!rtp_h261_ctx->buf) {
        /* sanity check: a new frame starts with gobn=0, sbit=0, mbap=0, quant=0 */
        if (!gobn && !sbit && !mbap && !quant) {
            res = avio_open_dyn_buf(&rtp_h261_ctx->buf);
            if (res < 0)
                return res;
            /* update the timestamp in the frame packet with the one from the RTP packet */
            rtp_h261_ctx->timestamp = *timestamp;
        } else {
            /* frame not started yet, need more packets */
            return AVERROR(EAGAIN);
        }
    }

    /* do the "byte merging" at the boundaries of two consecutive frame fragments */
    if (rtp_h261_ctx->endbyte_bits || sbit) {
        if (rtp_h261_ctx->endbyte_bits == sbit) {
            rtp_h261_ctx->endbyte     |= buf[0] & (0xff >> sbit);
            rtp_h261_ctx->endbyte_bits = 0;
            buf++;
            len--;
            avio_w8(rtp_h261_ctx->buf, rtp_h261_ctx->endbyte);
        } else {
            /* ebit/sbit values inconsistent, assuming packet loss */
            GetBitContext gb;
            init_get_bits(&gb, buf, len*8 - ebit);
            skip_bits(&gb, sbit);
            if (rtp_h261_ctx->endbyte_bits) {
                rtp_h261_ctx->endbyte |= get_bits(&gb, 8 - rtp_h261_ctx->endbyte_bits);
                avio_w8(rtp_h261_ctx->buf, rtp_h261_ctx->endbyte);
            }
            while (get_bits_left(&gb) >= 8)
                avio_w8(rtp_h261_ctx->buf, get_bits(&gb, 8));
            rtp_h261_ctx->endbyte_bits = get_bits_left(&gb);
            if (rtp_h261_ctx->endbyte_bits)
                rtp_h261_ctx->endbyte = get_bits(&gb, rtp_h261_ctx->endbyte_bits) <<
                                        (8 - rtp_h261_ctx->endbyte_bits);
            ebit = 0;
            len  = 0;
        }
    }
    if (ebit) {
        if (len > 0)
            avio_write(rtp_h261_ctx->buf, buf, len - 1);
        rtp_h261_ctx->endbyte_bits = 8 - ebit;
        rtp_h261_ctx->endbyte      = buf[len - 1] & (0xff << ebit);
    } else {
        avio_write(rtp_h261_ctx->buf, buf, len);
    }

    /* RTP marker bit means: last fragment of current frame was received;
       otherwise, an additional fragment is needed for the current frame */
    if (!(flags & RTP_FLAG_MARKER))
        return AVERROR(EAGAIN);

    /* write the completed last byte from the "byte merging" */
    if (rtp_h261_ctx->endbyte_bits)
        avio_w8(rtp_h261_ctx->buf, rtp_h261_ctx->endbyte);
    rtp_h261_ctx->endbyte_bits = 0;

    /* close frame buffering and create resulting A/V packet */
    res = ff_rtp_finalize_packet(pkt, &rtp_h261_ctx->buf, st->index);
    if (res < 0)
        return res;

    return 0;
}

const RTPDynamicProtocolHandler ff_h261_dynamic_handler = {
    .enc_name          = "H261",
    .codec_type        = AVMEDIA_TYPE_VIDEO,
    .codec_id          = AV_CODEC_ID_H261,
    .need_parsing      = AVSTREAM_PARSE_FULL,
    .priv_data_size    = sizeof(PayloadContext),
    .close             = h261_close_context,
    .parse_packet      = h261_handle_packet,
    .static_payload_id = 31,
};
