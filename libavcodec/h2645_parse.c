/*
 * H.264/HEVC common parsing code
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

#include <string.h>

#include "config.h"

#include "libavutil/intmath.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"

#include "bytestream.h"
#include "h2645_parse.h"

int ff_h2645_extract_rbsp(const uint8_t *src, int length,
                          H2645NAL *nal)
{
    int i, si, di;
    uint8_t *dst;

#define STARTCODE_TEST                                                  \
        if (i + 2 < length && src[i + 1] == 0 && src[i + 2] <= 3) {     \
            if (src[i + 2] != 3) {                                      \
                /* startcode, so we must be past the end */             \
                length = i;                                             \
            }                                                           \
            break;                                                      \
        }
#if HAVE_FAST_UNALIGNED
#define FIND_FIRST_ZERO                                                 \
        if (i > 0 && !src[i])                                           \
            i--;                                                        \
        while (src[i])                                                  \
            i++
#if HAVE_FAST_64BIT
    for (i = 0; i + 1 < length; i += 9) {
        if (!((~AV_RN64A(src + i) &
               (AV_RN64A(src + i) - 0x0100010001000101ULL)) &
              0x8000800080008080ULL))
            continue;
        FIND_FIRST_ZERO;
        STARTCODE_TEST;
        i -= 7;
    }
#else
    for (i = 0; i + 1 < length; i += 5) {
        if (!((~AV_RN32A(src + i) &
               (AV_RN32A(src + i) - 0x01000101U)) &
              0x80008080U))
            continue;
        FIND_FIRST_ZERO;
        STARTCODE_TEST;
        i -= 3;
    }
#endif /* HAVE_FAST_64BIT */
#else
    for (i = 0; i + 1 < length; i += 2) {
        if (src[i])
            continue;
        if (i > 0 && src[i - 1] == 0)
            i--;
        STARTCODE_TEST;
    }
#endif /* HAVE_FAST_UNALIGNED */

    if (i >= length - 1) { // no escaped 0
        nal->data     =
        nal->raw_data = src;
        nal->size     =
        nal->raw_size = length;
        return length;
    }

    av_fast_malloc(&nal->rbsp_buffer, &nal->rbsp_buffer_size,
                   length + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!nal->rbsp_buffer)
        return AVERROR(ENOMEM);

    dst = nal->rbsp_buffer;

    memcpy(dst, src, i);
    si = di = i;
    while (si + 2 < length) {
        // remove escapes (very rare 1:2^22)
        if (src[si + 2] > 3) {
            dst[di++] = src[si++];
            dst[di++] = src[si++];
        } else if (src[si] == 0 && src[si + 1] == 0) {
            if (src[si + 2] == 3) { // escape
                dst[di++] = 0;
                dst[di++] = 0;
                si       += 3;

                continue;
            } else // next start code
                goto nsc;
        }

        dst[di++] = src[si++];
    }
    while (si < length)
        dst[di++] = src[si++];

nsc:
    memset(dst + di, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    nal->data = dst;
    nal->size = di;
    nal->raw_data = src;
    nal->raw_size = si;
    return si;
}

static int get_bit_length(H2645NAL *nal, int skip_trailing_zeros)
{
    int size = nal->size;
    int v;

    while (skip_trailing_zeros && size > 0 && nal->data[size - 1] == 0)
        size--;

    if (!size)
        return 0;

    v = nal->data[size - 1];

    if (size > INT_MAX / 8)
        return AVERROR(ERANGE);
    size *= 8;

    /* remove the stop bit and following trailing zeros,
     * or nothing for damaged bitstreams */
    if (v)
        size -= av_ctz(v) + 1;

    return size;
}

/**
 * @return AVERROR_INVALIDDATA if the packet is not a valid NAL unit,
 * 0 if the unit should be skipped, 1 otherwise
 */
static int hevc_parse_nal_header(H2645NAL *nal, void *logctx)
{
    GetBitContext *gb = &nal->gb;
    int nuh_layer_id;

    if (get_bits1(gb) != 0)
        return AVERROR_INVALIDDATA;

    nal->type = get_bits(gb, 6);

    nuh_layer_id   = get_bits(gb, 6);
    nal->temporal_id = get_bits(gb, 3) - 1;
    if (nal->temporal_id < 0)
        return AVERROR_INVALIDDATA;

    av_log(logctx, AV_LOG_DEBUG,
           "nal_unit_type: %d, nuh_layer_id: %dtemporal_id: %d\n",
           nal->type, nuh_layer_id, nal->temporal_id);

    return nuh_layer_id == 0;
}

static int h264_parse_nal_header(H2645NAL *nal, void *logctx)
{
    GetBitContext *gb = &nal->gb;

    if (get_bits1(gb) != 0)
        return AVERROR_INVALIDDATA;

    nal->ref_idc = get_bits(gb, 2);
    nal->type    = get_bits(gb, 5);

    av_log(logctx, AV_LOG_DEBUG,
           "nal_unit_type: %d, nal_ref_idc: %d\n",
           nal->type, nal->ref_idc);

    return 1;
}

static int find_next_start_code(const uint8_t *buf, const uint8_t *next_avc)
{
    int i = 0;

    if (buf + 3 >= next_avc)
        return next_avc - buf;

    while (buf + i + 3 < next_avc) {
        if (buf[i] == 0 && buf[i + 1] == 0 && buf[i + 2] == 1)
            break;
        i++;
    }
    return i + 3;
}

int ff_h2645_packet_split(H2645Packet *pkt, const uint8_t *buf, int length,
                          void *logctx, int is_nalff, int nal_length_size,
                          enum AVCodecID codec_id)
{
    GetByteContext bc;
    int consumed, ret = 0;
    size_t next_avc = is_nalff ? 0 : length;

    bytestream2_init(&bc, buf, length);

    pkt->nb_nals = 0;
    while (bytestream2_get_bytes_left(&bc) >= 4) {
        H2645NAL *nal;
        int extract_length = 0;
        int skip_trailing_zeros = 1;

        /*
         * Only parse an AVC1 length field if one is expected at the current
         * buffer position. There are unfortunately streams with multiple
         * NAL units covered by the length field. Those NAL units are delimited
         * by Annex B start code prefixes. ff_h2645_extract_rbsp() detects it
         * correctly and consumes only the first NAL unit. The additional NAL
         * units are handled here in the Annex B parsing code.
         */
        if (bytestream2_tell(&bc) == next_avc) {
            int i;
            for (i = 0; i < nal_length_size; i++)
                extract_length = (extract_length << 8) | bytestream2_get_byte(&bc);

            if (extract_length > bytestream2_get_bytes_left(&bc)) {
                av_log(logctx, AV_LOG_ERROR,
                       "Invalid NAL unit size (%d > %d).\n",
                       extract_length, bytestream2_get_bytes_left(&bc));
                return AVERROR_INVALIDDATA;
            }
            // keep track of the next AVC1 length field
            next_avc = bytestream2_tell(&bc) + extract_length;
        } else {
            /*
             * expected to return immediately except for streams with mixed
             * NAL unit coding
             */
            int buf_index = find_next_start_code(bc.buffer, buf + next_avc);

            bytestream2_skip(&bc, buf_index);

            /*
             * break if an AVC1 length field is expected at the current buffer
             * position
             */
            if (bytestream2_tell(&bc) == next_avc)
                continue;

            if (bytestream2_get_bytes_left(&bc) > 0) {
                extract_length = bytestream2_get_bytes_left(&bc);
            } else if (pkt->nb_nals == 0) {
                av_log(logctx, AV_LOG_ERROR, "No NAL unit found\n");
                return AVERROR_INVALIDDATA;
            } else {
                break;
            }
        }

        if (pkt->nals_allocated < pkt->nb_nals + 1) {
            int new_size = pkt->nals_allocated + 1;
            H2645NAL *tmp = av_realloc_array(pkt->nals, new_size, sizeof(*tmp));
            if (!tmp)
                return AVERROR(ENOMEM);

            pkt->nals = tmp;
            memset(pkt->nals + pkt->nals_allocated, 0,
                   (new_size - pkt->nals_allocated) * sizeof(*tmp));
            pkt->nals_allocated = new_size;
        }
        nal = &pkt->nals[pkt->nb_nals++];

        consumed = ff_h2645_extract_rbsp(bc.buffer, extract_length, nal);
        if (consumed < 0)
            return consumed;

        bytestream2_skip(&bc, consumed);

        /* see commit 3566042a0 */
        if (bytestream2_get_bytes_left(&bc) >= 4 &&
            bytestream2_peek_be32(&bc) == 0x000001E0)
            skip_trailing_zeros = 0;

        nal->size_bits = get_bit_length(nal, skip_trailing_zeros);

        ret = init_get_bits(&nal->gb, nal->data, nal->size_bits);
        if (ret < 0)
            return ret;

        if (codec_id == AV_CODEC_ID_HEVC)
            ret = hevc_parse_nal_header(nal, logctx);
        else
            ret = h264_parse_nal_header(nal, logctx);
        if (ret <= 0) {
            if (ret < 0) {
                av_log(logctx, AV_LOG_ERROR, "Invalid NAL unit %d, skipping.\n",
                       nal->type);
            }
            pkt->nb_nals--;
        }
    }

    return 0;
}

void ff_h2645_packet_uninit(H2645Packet *pkt)
{
    int i;
    for (i = 0; i < pkt->nals_allocated; i++)
        av_freep(&pkt->nals[i].rbsp_buffer);
    av_freep(&pkt->nals);
    pkt->nals_allocated = 0;
}
