/*
 * RTP H.263 Depacketizer, RFC 4629
 * Copyright (c) 2010 Martin Storsjo
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

#include "avformat.h"
#include "rtpdec_h263.h"
#include "libavutil/intreadwrite.h"

static int h263_handle_packet(AVFormatContext *ctx,
                              PayloadContext *data,
                              AVStream *st,
                              AVPacket * pkt,
                              uint32_t * timestamp,
                              const uint8_t * buf,
                              int len, int flags)
{
    uint8_t *ptr;
    uint16_t header;
    int startcode, vrc, picture_header;

    if (len < 2) {
        av_log(ctx, AV_LOG_ERROR, "Too short H.263 RTP packet\n");
        return AVERROR_INVALIDDATA;
    }

    /* Decode the 16 bit H.263+ payload header, as described in section
     * 5.1 of RFC 4629. The fields of this header are:
     * - 5 reserved bits, should be ignored.
     * - One bit (P, startcode), indicating a picture start, picture segment
     *   start or video sequence end. If set, two zero bytes should be
     *   prepended to the payload.
     * - One bit (V, vrc), indicating the presence of an 8 bit Video
     *   Redundancy Coding field after this 16 bit header.
     * - 6 bits (PLEN, picture_header), the length (in bytes) of an extra
     *   picture header, following the VRC field.
     * - 3 bits (PEBIT), the number of bits to ignore of the last byte
     *   of the extra picture header. (Not used at the moment.)
     */
    header = AV_RB16(buf);
    startcode      = (header & 0x0400) >> 9;
    vrc            =  header & 0x0200;
    picture_header = (header & 0x01f8) >> 3;
    buf += 2;
    len -= 2;

    if (vrc) {
        /* Skip VRC header if present, not used at the moment. */
        buf += 1;
        len -= 1;
    }
    if (picture_header) {
        /* Skip extra picture header if present, not used at the moment. */
        buf += picture_header;
        len -= picture_header;
    }

    if (len < 0) {
        av_log(ctx, AV_LOG_ERROR, "Too short H.263 RTP packet\n");
        return AVERROR_INVALIDDATA;
    }

    if (av_new_packet(pkt, len + startcode)) {
        av_log(ctx, AV_LOG_ERROR, "Out of memory\n");
        return AVERROR(ENOMEM);
    }
    pkt->stream_index = st->index;
    ptr = pkt->data;

    if (startcode) {
        *ptr++ = 0;
        *ptr++ = 0;
    }
    memcpy(ptr, buf, len);

    return 0;
}

RTPDynamicProtocolHandler ff_h263_1998_dynamic_handler = {
    .enc_name         = "H263-1998",
    .codec_type       = AVMEDIA_TYPE_VIDEO,
    .codec_id         = CODEC_ID_H263,
    .parse_packet     = h263_handle_packet,
};

RTPDynamicProtocolHandler ff_h263_2000_dynamic_handler = {
    .enc_name         = "H263-2000",
    .codec_type       = AVMEDIA_TYPE_VIDEO,
    .codec_id         = CODEC_ID_H263,
    .parse_packet     = h263_handle_packet,
};

