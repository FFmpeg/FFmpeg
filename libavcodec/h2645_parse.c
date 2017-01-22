/*
 * H.264/HEVC common parsing code
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

#include <string.h>

#include "config.h"

#include "libavutil/intmath.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"

#include "hevc.h"
#include "h2645_parse.h"

int ff_h2645_extract_rbsp(const uint8_t *src, int length,
                          H2645NAL *nal, int small_padding)
{
    int i, si, di;
    uint8_t *dst;
    int64_t padding = small_padding ? 0 : MAX_MBPAIR_SIZE;

    nal->skipped_bytes = 0;
#define STARTCODE_TEST                                                  \
        if (i + 2 < length && src[i + 1] == 0 && src[i + 2] <= 3) {     \
            if (src[i + 2] != 3 && src[i + 2] != 0) {                   \
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

    if (i >= length - 1 && small_padding) { // no escaped 0
        nal->data     =
        nal->raw_data = src;
        nal->size     =
        nal->raw_size = length;
        return length;
    } else if (i > length)
        i = length;

    av_fast_padded_malloc(&nal->rbsp_buffer, &nal->rbsp_buffer_size,
                          length + padding);
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
        } else if (src[si] == 0 && src[si + 1] == 0 && src[si + 2] != 0) {
            if (src[si + 2] == 3) { // escape
                dst[di++] = 0;
                dst[di++] = 0;
                si       += 3;

                if (nal->skipped_bytes_pos) {
                    nal->skipped_bytes++;
                    if (nal->skipped_bytes_pos_size < nal->skipped_bytes) {
                        nal->skipped_bytes_pos_size *= 2;
                        av_assert0(nal->skipped_bytes_pos_size >= nal->skipped_bytes);
                        av_reallocp_array(&nal->skipped_bytes_pos,
                                nal->skipped_bytes_pos_size,
                                sizeof(*nal->skipped_bytes_pos));
                        if (!nal->skipped_bytes_pos) {
                            nal->skipped_bytes_pos_size = 0;
                            return AVERROR(ENOMEM);
                        }
                    }
                    if (nal->skipped_bytes_pos)
                        nal->skipped_bytes_pos[nal->skipped_bytes-1] = di - 1;
                }
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

static const char *nal_unit_name(int nal_type)
{
    switch(nal_type) {
    case NAL_TRAIL_N    : return "TRAIL_N";
    case NAL_TRAIL_R    : return "TRAIL_R";
    case NAL_TSA_N      : return "TSA_N";
    case NAL_TSA_R      : return "TSA_R";
    case NAL_STSA_N     : return "STSA_N";
    case NAL_STSA_R     : return "STSA_R";
    case NAL_RADL_N     : return "RADL_N";
    case NAL_RADL_R     : return "RADL_R";
    case NAL_RASL_N     : return "RASL_N";
    case NAL_RASL_R     : return "RASL_R";
    case NAL_BLA_W_LP   : return "BLA_W_LP";
    case NAL_BLA_W_RADL : return "BLA_W_RADL";
    case NAL_BLA_N_LP   : return "BLA_N_LP";
    case NAL_IDR_W_RADL : return "IDR_W_RADL";
    case NAL_IDR_N_LP   : return "IDR_N_LP";
    case NAL_CRA_NUT    : return "CRA_NUT";
    case NAL_VPS        : return "VPS";
    case NAL_SPS        : return "SPS";
    case NAL_PPS        : return "PPS";
    case NAL_AUD        : return "AUD";
    case NAL_EOS_NUT    : return "EOS_NUT";
    case NAL_EOB_NUT    : return "EOB_NUT";
    case NAL_FD_NUT     : return "FD_NUT";
    case NAL_SEI_PREFIX : return "SEI_PREFIX";
    case NAL_SEI_SUFFIX : return "SEI_SUFFIX";
    default : return "?";
    }
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
        size -= ff_ctz(v) + 1;

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
           "nal_unit_type: %d(%s), nuh_layer_id: %d, temporal_id: %d\n",
           nal->type, nal_unit_name(nal->type), nuh_layer_id, nal->temporal_id);

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

int ff_h2645_packet_split(H2645Packet *pkt, const uint8_t *buf, int length,
                          void *logctx, int is_nalff, int nal_length_size,
                          enum AVCodecID codec_id, int small_padding)
{
    int consumed, ret = 0;
    const uint8_t *next_avc = is_nalff ? buf : buf + length;

    pkt->nb_nals = 0;
    while (length >= 4) {
        H2645NAL *nal;
        int extract_length = 0;
        int skip_trailing_zeros = 1;

        if (buf == next_avc) {
            int i = 0;
            extract_length = get_nalsize(nal_length_size,
                                         buf, length, &i, logctx);
            if (extract_length < 0)
                return extract_length;

            buf    += nal_length_size;
            length -= nal_length_size;

            next_avc = buf + extract_length;
        } else {
            if (buf > next_avc)
                av_log(logctx, AV_LOG_WARNING, "Exceeded next NALFF position, re-syncing.\n");

            /* search start code */
            while (buf[0] != 0 || buf[1] != 0 || buf[2] != 1) {
                ++buf;
                --length;
                if (length < 4) {
                    if (pkt->nb_nals > 0) {
                        // No more start codes: we discarded some irrelevant
                        // bytes at the end of the packet.
                        return 0;
                    } else {
                        av_log(logctx, AV_LOG_ERROR, "No start code is found.\n");
                        return AVERROR_INVALIDDATA;
                    }
                } else if (buf >= (next_avc - 3))
                    break;
            }

            buf           += 3;
            length        -= 3;
            extract_length = FFMIN(length, next_avc - buf);

            if (buf >= next_avc) {
                /* skip to the start of the next NAL */
                int offset = next_avc - buf;
                buf    += offset;
                length -= offset;
                continue;
            }
        }

        if (pkt->nals_allocated < pkt->nb_nals + 1) {
            int new_size = pkt->nals_allocated + 1;
            void *tmp = av_realloc_array(pkt->nals, new_size, sizeof(*pkt->nals));

            if (!tmp)
                return AVERROR(ENOMEM);

            pkt->nals = tmp;
            memset(pkt->nals + pkt->nals_allocated, 0,
                   (new_size - pkt->nals_allocated) * sizeof(*pkt->nals));

            nal = &pkt->nals[pkt->nb_nals];
            nal->skipped_bytes_pos_size = 1024; // initial buffer size
            nal->skipped_bytes_pos = av_malloc_array(nal->skipped_bytes_pos_size, sizeof(*nal->skipped_bytes_pos));
            if (!nal->skipped_bytes_pos)
                return AVERROR(ENOMEM);

            pkt->nals_allocated = new_size;
        }
        nal = &pkt->nals[pkt->nb_nals];

        consumed = ff_h2645_extract_rbsp(buf, extract_length, nal, small_padding);
        if (consumed < 0)
            return consumed;

        if (is_nalff && (extract_length != consumed) && extract_length)
            av_log(logctx, AV_LOG_DEBUG,
                   "NALFF: Consumed only %d bytes instead of %d\n",
                   consumed, extract_length);

        pkt->nb_nals++;

        /* see commit 3566042a0 */
        if (consumed < length - 3 &&
            buf[consumed]     == 0x00 && buf[consumed + 1] == 0x00 &&
            buf[consumed + 2] == 0x01 && buf[consumed + 3] == 0xE0)
            skip_trailing_zeros = 0;

        nal->size_bits = get_bit_length(nal, skip_trailing_zeros);

        ret = init_get_bits(&nal->gb, nal->data, nal->size_bits);
        if (ret < 0)
            return ret;

        if (codec_id == AV_CODEC_ID_HEVC)
            ret = hevc_parse_nal_header(nal, logctx);
        else
            ret = h264_parse_nal_header(nal, logctx);
        if (ret <= 0 || nal->size <= 0) {
            if (ret < 0) {
                av_log(logctx, AV_LOG_ERROR, "Invalid NAL unit %d, skipping.\n",
                       nal->type);
            }
            pkt->nb_nals--;
        }

        buf    += consumed;
        length -= consumed;
    }

    return 0;
}

void ff_h2645_packet_uninit(H2645Packet *pkt)
{
    int i;
    for (i = 0; i < pkt->nals_allocated; i++) {
        av_freep(&pkt->nals[i].rbsp_buffer);
        av_freep(&pkt->nals[i].skipped_bytes_pos);
    }
    av_freep(&pkt->nals);
    pkt->nals_allocated = 0;
}
