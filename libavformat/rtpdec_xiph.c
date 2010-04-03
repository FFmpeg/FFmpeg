/*
 * Xiph RTP Protocols
 * Copyright (c) 2009 Colin McQuillian
 * Copyright (c) 2010 Josh Allmann
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
 * @file libavformat/rtpdec_xiph.c
 * @brief Xiph / RTP Code
 * @author Colin McQuillan <m.niloc@gmail.com>
 * @author Josh Allmann <joshua.allmann@gmail.com>
 */

#include "libavutil/avstring.h"
#include "libavutil/base64.h"
#include "libavcodec/bytestream.h"

#include <assert.h>

#include "rtpdec.h"
#include "rtpdec_xiph.h"

/**
 * RTP/Xiph specific private data.
 */
struct PayloadContext {
    unsigned ident;             ///< 24-bit stream configuration identifier
    uint32_t timestamp;
    ByteIOContext* fragment;    ///< buffer for split payloads
};

static PayloadContext *xiph_new_context(void)
{
    return av_mallocz(sizeof(PayloadContext));
}

static inline void free_fragment_if_needed(PayloadContext * data)
{
    if (data->fragment) {
        uint8_t* p;
        url_close_dyn_buf(data->fragment, &p);
        av_free(p);
        data->fragment = NULL;
    }
}

static void xiph_free_context(PayloadContext * data)
{
    free_fragment_if_needed(data);
    av_free(data);
}

static int xiph_handle_packet(AVFormatContext * ctx,
                              PayloadContext * data,
                              AVStream * st,
                              AVPacket * pkt,
                              uint32_t * timestamp,
                              const uint8_t * buf, int len, int flags)
{

    int ident, fragmented, tdt, num_pkts, pkt_len;

    if (len < 6) {
        av_log(ctx, AV_LOG_ERROR, "Invalid %d byte packet\n", len);
        return AVERROR_INVALIDDATA;
    }

    // read xiph rtp headers
    ident       = AV_RB24(buf);
    fragmented  = buf[3] >> 6;
    tdt         = (buf[3] >> 4) & 3;
    num_pkts    = buf[3] & 7;
    pkt_len     = AV_RB16(buf + 4);

    if (pkt_len > len - 6) {
        av_log(ctx, AV_LOG_ERROR,
               "Invalid packet length %d in %d byte packet\n", pkt_len,
               len);
        return AVERROR_INVALIDDATA;
    }

    if (ident != data->ident) {
        av_log(ctx, AV_LOG_ERROR,
               "Unimplemented Xiph SDP configuration change detected\n");
        return AVERROR_PATCHWELCOME;
    }

    if (tdt) {
        av_log(ctx, AV_LOG_ERROR,
               "Unimplemented RTP Xiph packet settings (%d,%d,%d)\n",
               fragmented, tdt, num_pkts);
        return AVERROR_PATCHWELCOME;
    }

    buf += 6; // move past header bits
    len -= 6;

    if (fragmented == 0) {
        // whole frame(s)
        int i, data_len, write_len;
        buf -= 2;
        len += 2;

        // fast first pass to calculate total length
        for (i = 0, data_len = 0;  (i < num_pkts) && (len >= 2);  i++) {
            int off   = data_len + (i << 1);
            pkt_len   = AV_RB16(buf + off);
            data_len += pkt_len;
            len      -= pkt_len + 2;
        }

        if (len < 0 || i < num_pkts) {
            av_log(ctx, AV_LOG_ERROR,
                   "Bad packet: %d bytes left at frame %d of %d\n",
                   len, i, num_pkts);
            return AVERROR_INVALIDDATA;
        }

        if (av_new_packet(pkt, data_len)) {
            av_log(ctx, AV_LOG_ERROR, "Out of memory.\n");
            return AVERROR(ENOMEM);
        }
        pkt->stream_index = st->index;

        // concatenate frames
        for (i = 0, write_len = 0; write_len < data_len; i++) {
            pkt_len = AV_RB16(buf);
            buf += 2;
            memcpy(pkt->data + write_len, buf, pkt_len);
            write_len += pkt_len;
            buf += pkt_len;
        }
        assert(write_len == data_len);

        return 0;

    } else if (fragmented == 1) {
        // start of xiph data fragment
        int res;

        // end packet has been lost somewhere, so drop buffered data
        free_fragment_if_needed(data);

        if((res = url_open_dyn_buf(&data->fragment)) < 0)
            return res;

        put_buffer(data->fragment, buf, pkt_len);
        data->timestamp = *timestamp;

    } else {
        assert(fragmented < 4);
        if (data->timestamp != *timestamp) {
            // skip if fragmented timestamp is incorrect;
            // a start packet has been lost somewhere
            free_fragment_if_needed(data);
            av_log(ctx, AV_LOG_ERROR, "RTP timestamps don't match!\n");
            return AVERROR_INVALIDDATA;
        }

        // copy data to fragment buffer
        put_buffer(data->fragment, buf, pkt_len);

        if (fragmented == 3) {
            // end of xiph data packet
            uint8_t* xiph_data;
            int frame_size = url_close_dyn_buf(data->fragment, &xiph_data);

            if (frame_size < 0) {
                av_log(ctx, AV_LOG_ERROR,
                       "Error occurred when getting fragment buffer.");
                return frame_size;
            }

            if (av_new_packet(pkt, frame_size)) {
                av_log(ctx, AV_LOG_ERROR, "Out of memory.\n");
                return AVERROR(ENOMEM);
            }

            memcpy(pkt->data, xiph_data, frame_size);
            pkt->stream_index = st->index;

            av_free(xiph_data);
            data->fragment = NULL;

            return 0;
        }
    }

   return AVERROR(EAGAIN);
}

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
 * Based off parse_packed_headers in Vorbis RTP
 */
static unsigned int
parse_packed_headers(const uint8_t * packed_headers,
                     const uint8_t * packed_headers_end,
                     AVCodecContext * codec, PayloadContext * xiph_data)
{

    unsigned num_packed, num_headers, length, length1, length2, extradata_alloc;
    uint8_t *ptr;

    if (packed_headers_end - packed_headers < 9) {
        av_log(codec, AV_LOG_ERROR,
               "Invalid %d byte packed header.",
               packed_headers_end - packed_headers);
        return AVERROR_INVALIDDATA;
    }

    num_packed         = bytestream_get_be32(&packed_headers);
    xiph_data->ident   = bytestream_get_be24(&packed_headers);
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

    /* allocate extra space:
     * -- length/255 +2 for xiphlacing
     * -- one for the '2' marker
     * -- FF_INPUT_BUFFER_PADDING_SIZE required */
    extradata_alloc = length + length/255 + 3 + FF_INPUT_BUFFER_PADDING_SIZE;

    ptr = codec->extradata = av_malloc(extradata_alloc);
    if (!ptr) {
        av_log(codec, AV_LOG_ERROR, "Out of memory\n");
        return AVERROR(ENOMEM);
    }
    *ptr++ = 2;
    ptr += av_xiphlacing(ptr, length1);
    ptr += av_xiphlacing(ptr, length2);
    memcpy(ptr, packed_headers, length);
    ptr += length;
    codec->extradata_size = ptr - codec->extradata;
    // clear out remaining parts of the buffer
    memset(ptr, 0, extradata_alloc - codec->extradata_size);

    return 0;
}

static int xiph_parse_fmtp_pair(AVCodecContext * codec,
                                PayloadContext *xiph_data,
                                char *attr, char *value)
{
    int result = 0;

    if (!strcmp(attr, "sampling")) {
        return AVERROR_PATCHWELCOME;
    } else if (!strcmp(attr, "width")) {
        /* This is an integer between 1 and 1048561
         * and MUST be in multiples of 16. */
        codec->width = atoi(value);
        return 0;
    } else if (!strcmp(attr, "height")) {
        /* This is an integer between 1 and 1048561
         * and MUST be in multiples of 16. */
        codec->height = atoi(value);
        return 0;
    } else if (!strcmp(attr, "delivery-method")) {
        /* Possible values are: inline, in_band, out_band/specific_name. */
        return AVERROR_PATCHWELCOME;
    } else if (!strcmp(attr, "configuration-uri")) {
        /* NOTE: configuration-uri is supported only under 2 conditions:
         *--after the delivery-method tag
         * --with a delivery-method value of out_band */
        return AVERROR_PATCHWELCOME;
    } else if (!strcmp(attr, "configuration")) {
        /* NOTE: configuration is supported only AFTER the delivery-method tag
         * The configuration value is a base64 encoded packed header */
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
                    xiph_data);
            } else {
                av_log(codec, AV_LOG_ERROR,
                       "Out of memory while decoding SDP configuration.\n");
                result = AVERROR(ENOMEM);
            }
        } else {
            av_log(codec, AV_LOG_ERROR, "Packet too large\n");
            result = AVERROR_INVALIDDATA;
        }
        av_free(decoded_packet);
    }
    return result;
}

static int xiph_parse_sdp_line(AVFormatContext *s, int st_index,
                                 PayloadContext *data, const char *line)
{
    const char *p;
    char *value;
    char attr[25];
    int value_size = strlen(line), attr_size = sizeof(attr), res = 0;
    AVCodecContext* codec = s->streams[st_index]->codec;

    assert(codec->id == CODEC_ID_THEORA);
    assert(data);

    if (!(value = av_malloc(value_size))) {
        av_log(codec, AV_LOG_ERROR, "Out of memory\n");
        return AVERROR(ENOMEM);
    }

    if (av_strstart(line, "fmtp:", &p)) {
        // remove protocol identifier
        while (*p && *p == ' ') p++; // strip spaces
        while (*p && *p != ' ') p++; // eat protocol identifier
        while (*p && *p == ' ') p++; // strip trailing spaces

        while (ff_rtsp_next_attr_and_value(&p,
                                           attr, attr_size,
                                           value, value_size)) {
            res = xiph_parse_fmtp_pair(codec, data, attr, value);
            if (res < 0 && res != AVERROR_PATCHWELCOME)
                return res;
        }
    }

    av_free(value);
    return 0;
}

RTPDynamicProtocolHandler ff_theora_dynamic_handler = {
    .enc_name         = "theora",
    .codec_type       = AVMEDIA_TYPE_VIDEO,
    .codec_id         = CODEC_ID_THEORA,
    .parse_sdp_a_line = xiph_parse_sdp_line,
    .open             = xiph_new_context,
    .close            = xiph_free_context,
    .parse_packet     = xiph_handle_packet
};

RTPDynamicProtocolHandler ff_vorbis_dynamic_handler = {
    .enc_name         = "vorbis",
    .codec_type       = AVMEDIA_TYPE_AUDIO,
    .codec_id         = CODEC_ID_VORBIS,
    .parse_sdp_a_line = xiph_parse_sdp_line,
    .open             = xiph_new_context,
    .close            = xiph_free_context,
    .parse_packet     = xiph_handle_packet
};
