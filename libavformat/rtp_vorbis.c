/*
 * RTP Vorbis Protocol (RFC5215)
 * Copyright (c) 2009 Colin McQuillan
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

/**
 * @file libavformat/rtp_vorbis.c
 * @brief Vorbis / RTP Code (RFC 5215)
 * @author Colin McQuillan <m.niloc@gmail.com>
 */

#include "libavutil/base64.h"
#include "libavutil/avstring.h"
#include "libavcodec/bytestream.h"

#include <assert.h>

#include "rtpdec.h"
#include "rtp_vorbis.h"

/**
 * RTP/Vorbis specific private data.
 */
struct PayloadContext {
    unsigned ident;             ///< 24-bit stream configuration identifier
};

/**
 * Length encoding described in RFC5215 section 3.1.1.
 */
static int get_base128(const uint8_t ** buf, const uint8_t * buf_end)
{
    int n = 0;
    for (; *buf < buf_end; ++*buf) {
        n <<= 7;
        n += **buf & 0x7f;
        if (!(**buf & 0x80)) {
            ++*buf;
            return n;
        }
    }
    return 0;
}

/**
 * Out-of-band headers, described in RFC 5251 section 3.2.1
 */
static unsigned int
parse_packed_headers(const uint8_t * packed_headers,
                     const uint8_t * packed_headers_end,
                     AVCodecContext * codec, PayloadContext * vorbis_data)
{
    unsigned num_packed, num_headers, length, length1, length2;
    uint8_t *ptr;

    num_packed         = bytestream_get_be32(&packed_headers);
    vorbis_data->ident = bytestream_get_be24(&packed_headers);
    length             = bytestream_get_be16(&packed_headers);
    num_headers        = get_base128(&packed_headers, packed_headers_end);
    length1            = get_base128(&packed_headers, packed_headers_end);
    length2            = get_base128(&packed_headers, packed_headers_end);

    if (num_packed != 1 || num_headers > 3) {
        av_log(codec, AV_LOG_ERROR,
               "Unimplemented number of headers: %d packed headers, %d headers\n",
               num_packed, num_headers);
        return AVERROR_PATCHWELCOME;
    }

    if (packed_headers_end - packed_headers != length ||
        length1 > length || length2 > length - length1) {
        av_log(codec, AV_LOG_ERROR,
               "Bad packed header lengths (%d,%d,%d,%d)\n", length1,
               length2, packed_headers_end - packed_headers, length);
        return AVERROR_INVALIDDATA;
    }

    ptr = codec->extradata = av_mallocz(length + length / 255 + 64);
    if (!ptr) {
        av_log(codec, AV_LOG_ERROR, "Out of memory");
        return AVERROR_NOMEM;
    }
    *ptr++ = 2;
    ptr += av_xiphlacing(ptr, length1);
    ptr += av_xiphlacing(ptr, length2);
    memcpy(ptr, packed_headers, length);
    ptr += length;
    codec->extradata_size = ptr - codec->extradata;

    return 0;
}

int
ff_vorbis_parse_fmtp_config(AVCodecContext * codec,
                            void *vorbis_data, char *attr, char *value)
{
    int result = 0;
    assert(codec->codec_id == CODEC_ID_VORBIS);
    assert(vorbis_data);

    // The configuration value is a base64 encoded packed header
    if (!strcmp(attr, "configuration")) {
        uint8_t *decoded_packet = NULL;
        int packet_size;
        size_t decoded_alloc = strlen(value) / 4 * 3 + 4;

        if (decoded_alloc <= INT_MAX) {
            decoded_packet = av_malloc(decoded_alloc);
            if (decoded_packet) {
                packet_size =
                    av_base64_decode(decoded_packet, value, decoded_alloc);

                result = parse_packed_headers
                    (decoded_packet, decoded_packet + packet_size, codec,
                     vorbis_data);
            } else {
                av_log(codec, AV_LOG_ERROR,
                       "Out of memory while decoding SDP configuration.\n");
                result = AVERROR_NOMEM;
            }
        } else {
            av_log(codec, AV_LOG_ERROR, "Packet too large\n");
            result = AVERROR_INVALIDDATA;
        }
        av_free(decoded_packet);
    }
    return result;
}

static PayloadContext *vorbis_new_extradata(void)
{
    return av_mallocz(sizeof(PayloadContext));
}

static void vorbis_free_extradata(PayloadContext * data)
{
    av_free(data);
}

/**
 * Handle payload as described in RFC 5215 section 2.2
 */
static int
vorbis_handle_packet(AVFormatContext * ctx,
                     PayloadContext * data,
                     AVStream * st,
                     AVPacket * pkt,
                     uint32_t * timestamp,
                     const uint8_t * buf, int len, int flags)
{
    int ident, fragmented, vdt, num_pkts, pkt_len;

    if (len < 6) {
        av_log(ctx, AV_LOG_ERROR, "Invalid %d byte packet\n", len);
        return AVERROR_INVALIDDATA;
    }

    ident = AV_RB24(buf);
    fragmented = buf[3] >> 6;
    vdt = (buf[3] >> 4) & 3;
    num_pkts = buf[3] & 7;
    pkt_len = AV_RB16(buf + 4);

    if (pkt_len > len - 6) {
        av_log(ctx, AV_LOG_ERROR,
               "Invalid packet length %d in %d byte packet\n", pkt_len,
               len);
        return AVERROR_INVALIDDATA;
    }

    if (ident != data->ident) {
        av_log(ctx, AV_LOG_ERROR,
               "Unimplemented Vorbis SDP configuration change detected\n");
        return AVERROR_PATCHWELCOME;
    }

    if (fragmented != 0 || vdt != 0 || num_pkts != 1) {
        av_log(ctx, AV_LOG_ERROR,
               "Unimplemented RTP Vorbis packet settings (%d,%d,%d)\n",
               fragmented, vdt, num_pkts);
        return AVERROR_PATCHWELCOME;
    }

    if (av_new_packet(pkt, pkt_len)) {
        av_log(ctx, AV_LOG_ERROR, "Out of memory.\n");
        return AVERROR_NOMEM;
    }

    memcpy(pkt->data, buf + 6, pkt_len);
    pkt->stream_index = st->index;
    return 0;
}

RTPDynamicProtocolHandler ff_vorbis_dynamic_handler = {
    "vorbis",
    CODEC_TYPE_AUDIO,
    CODEC_ID_VORBIS,
    NULL,
    vorbis_new_extradata,
    vorbis_free_extradata,
    vorbis_handle_packet
};
