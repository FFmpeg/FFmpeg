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
 * @file
 * @brief Xiph / RTP Code
 * @author Colin McQuillan <m.niloc@gmail.com>
 * @author Josh Allmann <joshua.allmann@gmail.com>
 */

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/base64.h"
#include "libavcodec/bytestream.h"

#include "rtpdec.h"
#include "rtpdec_formats.h"

/**
 * RTP/Xiph specific private data.
 */
struct PayloadContext {
    unsigned ident;             ///< 24-bit stream configuration identifier
    uint32_t timestamp;
    AVIOContext* fragment;    ///< buffer for split payloads
    uint8_t *split_buf;
    int split_pos, split_buf_len, split_buf_size;
    int split_pkts;
};

static PayloadContext *xiph_new_context(void)
{
    return av_mallocz(sizeof(PayloadContext));
}

static inline void free_fragment_if_needed(PayloadContext * data)
{
    if (data->fragment) {
        uint8_t* p;
        avio_close_dyn_buf(data->fragment, &p);
        av_free(p);
        data->fragment = NULL;
    }
}

static void xiph_free_context(PayloadContext * data)
{
    free_fragment_if_needed(data);
    av_free(data->split_buf);
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

    if (!buf) {
        if (!data->split_buf || data->split_pos + 2 > data->split_buf_len ||
            data->split_pkts <= 0) {
            av_log(ctx, AV_LOG_ERROR, "No more data to return\n");
            return AVERROR_INVALIDDATA;
        }
        pkt_len = AV_RB16(data->split_buf + data->split_pos);
        data->split_pos += 2;
        if (data->split_pos + pkt_len > data->split_buf_len) {
            av_log(ctx, AV_LOG_ERROR, "Not enough data to return\n");
            return AVERROR_INVALIDDATA;
        }
        if (av_new_packet(pkt, pkt_len)) {
            av_log(ctx, AV_LOG_ERROR, "Out of memory.\n");
            return AVERROR(ENOMEM);
        }
        pkt->stream_index = st->index;
        memcpy(pkt->data, data->split_buf + data->split_pos, pkt_len);
        data->split_pos += pkt_len;
        data->split_pkts--;
        return data->split_pkts > 0;
    }

    if (len < 6) {
        av_log(ctx, AV_LOG_ERROR, "Invalid %d byte packet\n", len);
        return AVERROR_INVALIDDATA;
    }

    // read xiph rtp headers
    ident       = AV_RB24(buf);
    fragmented  = buf[3] >> 6;
    tdt         = (buf[3] >> 4) & 3;
    num_pkts    = buf[3] & 0xf;
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
        if (av_new_packet(pkt, pkt_len)) {
            av_log(ctx, AV_LOG_ERROR, "Out of memory.\n");
            return AVERROR(ENOMEM);
        }
        pkt->stream_index = st->index;
        memcpy(pkt->data, buf, pkt_len);
        buf += pkt_len;
        len -= pkt_len;
        num_pkts--;

        if (num_pkts > 0) {
            if (len > data->split_buf_size || !data->split_buf) {
                av_freep(&data->split_buf);
                data->split_buf_size = 2 * len;
                data->split_buf = av_malloc(data->split_buf_size);
                if (!data->split_buf) {
                    av_log(ctx, AV_LOG_ERROR, "Out of memory.\n");
                    av_free_packet(pkt);
                    return AVERROR(ENOMEM);
                }
            }
            memcpy(data->split_buf, buf, len);
            data->split_buf_len = len;
            data->split_pos = 0;
            data->split_pkts = num_pkts;
            return 1;
        }

        return 0;

    } else if (fragmented == 1) {
        // start of xiph data fragment
        int res;

        // end packet has been lost somewhere, so drop buffered data
        free_fragment_if_needed(data);

        if((res = avio_open_dyn_buf(&data->fragment)) < 0)
            return res;

        avio_write(data->fragment, buf, pkt_len);
        data->timestamp = *timestamp;

    } else {
        av_assert1(fragmented < 4);
        if (data->timestamp != *timestamp) {
            // skip if fragmented timestamp is incorrect;
            // a start packet has been lost somewhere
            free_fragment_if_needed(data);
            av_log(ctx, AV_LOG_ERROR, "RTP timestamps don't match!\n");
            return AVERROR_INVALIDDATA;
        }
        if (!data->fragment) {
            av_log(ctx, AV_LOG_WARNING,
                   "Received packet without a start fragment; dropping.\n");
            return AVERROR(EAGAIN);
        }

        // copy data to fragment buffer
        avio_write(data->fragment, buf, pkt_len);

        if (fragmented == 3) {
            // end of xiph data packet
            av_init_packet(pkt);
            pkt->size = avio_close_dyn_buf(data->fragment, &pkt->data);

            if (pkt->size < 0) {
                av_log(ctx, AV_LOG_ERROR,
                       "Error occurred when getting fragment buffer.");
                return pkt->size;
            }

            pkt->stream_index = st->index;
            pkt->destruct = av_destruct_packet;

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
               "Invalid %td byte packed header.",
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
               "Bad packed header lengths (%d,%d,%td,%d)\n", length1,
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

static int xiph_parse_fmtp_pair(AVStream* stream,
                                PayloadContext *xiph_data,
                                char *attr, char *value)
{
    AVCodecContext *codec = stream->codec;
    int result = 0;

    if (!strcmp(attr, "sampling")) {
        if (!strcmp(value, "YCbCr-4:2:0")) {
            codec->pix_fmt = PIX_FMT_YUV420P;
        } else if (!strcmp(value, "YCbCr-4:4:2")) {
            codec->pix_fmt = PIX_FMT_YUV422P;
        } else if (!strcmp(value, "YCbCr-4:4:4")) {
            codec->pix_fmt = PIX_FMT_YUV444P;
        } else {
            av_log(codec, AV_LOG_ERROR,
                   "Unsupported pixel format %s\n", attr);
            return AVERROR_INVALIDDATA;
        }
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

    if (st_index < 0)
        return 0;

    if (av_strstart(line, "fmtp:", &p)) {
        return ff_parse_fmtp(s->streams[st_index], data, p,
                             xiph_parse_fmtp_pair);
    }

    return 0;
}

RTPDynamicProtocolHandler ff_theora_dynamic_handler = {
    .enc_name         = "theora",
    .codec_type       = AVMEDIA_TYPE_VIDEO,
    .codec_id         = AV_CODEC_ID_THEORA,
    .parse_sdp_a_line = xiph_parse_sdp_line,
    .alloc            = xiph_new_context,
    .free             = xiph_free_context,
    .parse_packet     = xiph_handle_packet
};

RTPDynamicProtocolHandler ff_vorbis_dynamic_handler = {
    .enc_name         = "vorbis",
    .codec_type       = AVMEDIA_TYPE_AUDIO,
    .codec_id         = AV_CODEC_ID_VORBIS,
    .parse_sdp_a_line = xiph_parse_sdp_line,
    .alloc            = xiph_new_context,
    .free             = xiph_free_context,
    .parse_packet     = xiph_handle_packet
};
