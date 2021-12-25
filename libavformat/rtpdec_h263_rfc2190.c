/*
 * RTP H.263 Depacketizer, RFC 2190
 * Copyright (c) 2012 Martin Storsjo
 * Based on the GStreamer H.263 Depayloder:
 * Copyright 2005 Wim Taymans
 * Copyright 2007 Edward Hervey
 * Copyright 2007 Nokia Corporation
 * Copyright 2007 Collabora Ltd, Philippe Kalaf
 * Copyright 2010 Mark Nauwelaerts
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
#include "avio_internal.h"
#include "rtpdec_formats.h"
#include "libavutil/attributes.h"
#include "libavutil/intreadwrite.h"
#include "libavcodec/get_bits.h"

struct PayloadContext {
    AVIOContext *buf;
    uint8_t      endbyte;
    int          endbyte_bits;
    uint32_t     timestamp;
    int          newformat;
};

static void h263_close_context(PayloadContext *data)
{
    ffio_free_dyn_buf(&data->buf);
}

static int h263_handle_packet(AVFormatContext *ctx, PayloadContext *data,
                              AVStream *st, AVPacket *pkt, uint32_t *timestamp,
                              const uint8_t *buf, int len, uint16_t seq,
                              int flags)
{
    /* Corresponding to header fields in the RFC */
    int f, p, i, sbit, ebit, src, r;
    int header_size, ret;

    if (data->newformat)
        return ff_h263_handle_packet(ctx, data, st, pkt, timestamp, buf, len,
                                     seq, flags);

    if (data->buf && data->timestamp != *timestamp) {
        /* Dropping old buffered, unfinished data */
        ffio_free_dyn_buf(&data->buf);
        data->endbyte_bits = 0;
    }

    if (len < 4) {
        av_log(ctx, AV_LOG_ERROR, "Too short H.263 RTP packet: %d\n", len);
        return AVERROR_INVALIDDATA;
    }

    f = buf[0] & 0x80;
    p = buf[0] & 0x40;
    if (!f) {
        /* Mode A */
        header_size = 4;
        i = buf[1] & 0x10;
        r = ((buf[1] & 0x01) << 3) | ((buf[2] & 0xe0) >> 5);
    } else if (!p) {
        /* Mode B */
        header_size = 8;
        if (len < header_size) {
            av_log(ctx, AV_LOG_ERROR,
                   "Too short H.263 RTP packet: %d bytes, %d header bytes\n",
                   len, header_size);
            return AVERROR_INVALIDDATA;
        }
        r = buf[3] & 0x03;
        i = buf[4] & 0x80;
    } else {
        /* Mode C */
        header_size = 12;
        if (len < header_size) {
            av_log(ctx, AV_LOG_ERROR,
                   "Too short H.263 RTP packet: %d bytes, %d header bytes\n",
                   len, header_size);
            return AVERROR_INVALIDDATA;
        }
        r = buf[3] & 0x03;
        i = buf[4] & 0x80;
    }
    sbit = (buf[0] >> 3) & 0x7;
    ebit =  buf[0]       & 0x7;
    src  = (buf[1] & 0xe0) >> 5;
    if (!(buf[0] & 0xf8)) { /* Reserved bits in RFC 2429/4629 are zero */
        if ((src == 0 || src >= 6) && r) {
            /* Invalid src for this format, and bits that should be zero
             * according to RFC 2190 aren't zero. */
            av_log(ctx, AV_LOG_WARNING,
                   "Interpreting H.263 RTP data as RFC 2429/4629 even though "
                   "signalled with a static payload type.\n");
            data->newformat = 1;
            return ff_h263_handle_packet(ctx, data, st, pkt, timestamp, buf,
                                         len, seq, flags);
        }
    }

    buf += header_size;
    len -= header_size;

    if (!data->buf) {
        /* Check the picture start code, only start buffering a new frame
         * if this is correct */
        if (len > 4 && AV_RB32(buf) >> 10 == 0x20) {
            ret = avio_open_dyn_buf(&data->buf);
            if (ret < 0)
                return ret;
            data->timestamp = *timestamp;
        } else {
            /* Frame not started yet, skipping */
            return AVERROR(EAGAIN);
        }
    }

    if (data->endbyte_bits || sbit) {
        if (data->endbyte_bits == sbit) {
            data->endbyte |= buf[0] & (0xff >> sbit);
            data->endbyte_bits = 0;
            buf++;
            len--;
            avio_w8(data->buf, data->endbyte);
        } else {
            /* Start/end skip bits not matching - missed packets? */
            GetBitContext gb;
            ret = init_get_bits(&gb, buf, len*8 - ebit);
            if (ret < 0)
                return ret;
            skip_bits(&gb, sbit);
            if (data->endbyte_bits) {
                data->endbyte |= get_bits(&gb, 8 - data->endbyte_bits);
                avio_w8(data->buf, data->endbyte);
            }
            while (get_bits_left(&gb) >= 8)
                avio_w8(data->buf, get_bits(&gb, 8));
            data->endbyte_bits = get_bits_left(&gb);
            if (data->endbyte_bits)
                data->endbyte = get_bits(&gb, data->endbyte_bits) <<
                                (8 - data->endbyte_bits);
            ebit = 0;
            len = 0;
        }
    }
    if (ebit) {
        if (len > 0)
            avio_write(data->buf, buf, len - 1);
        data->endbyte_bits = 8 - ebit;
        data->endbyte = buf[len - 1] & (0xff << ebit);
    } else {
        avio_write(data->buf, buf, len);
    }

    if (!(flags & RTP_FLAG_MARKER))
        return AVERROR(EAGAIN);

    if (data->endbyte_bits)
        avio_w8(data->buf, data->endbyte);
    data->endbyte_bits = 0;

    ret = ff_rtp_finalize_packet(pkt, &data->buf, st->index);
    if (ret < 0)
        return ret;
    if (!i)
        pkt->flags   |= AV_PKT_FLAG_KEY;

    return 0;
}

const RTPDynamicProtocolHandler ff_h263_rfc2190_dynamic_handler = {
    .codec_type        = AVMEDIA_TYPE_VIDEO,
    .codec_id          = AV_CODEC_ID_H263,
    .need_parsing      = AVSTREAM_PARSE_FULL,
    .parse_packet      = h263_handle_packet,
    .priv_data_size    = sizeof(PayloadContext),
    .close             = h263_close_context,
    .static_payload_id = 34,
};
