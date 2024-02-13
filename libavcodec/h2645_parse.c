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

#include "bytestream.h"
#include "hevc.h"
#include "h264.h"
#include "h2645_parse.h"
#include "vvc.h"

int ff_h2645_extract_rbsp(const uint8_t *src, int length,
                          H2645RBSP *rbsp, H2645NAL *nal, int small_padding)
{
    int i, si, di;
    uint8_t *dst;

    nal->skipped_bytes = 0;
#define STARTCODE_TEST                                                  \
        if (i + 2 < length && src[i + 1] == 0 &&                        \
           (src[i + 2] == 3 || src[i + 2] == 1)) {                      \
            if (src[i + 2] == 1) {                                      \
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
        if (!((~AV_RN64(src + i) &
               (AV_RN64(src + i) - 0x0100010001000101ULL)) &
              0x8000800080008080ULL))
            continue;
        FIND_FIRST_ZERO;
        STARTCODE_TEST;
        i -= 7;
    }
#else
    for (i = 0; i + 1 < length; i += 5) {
        if (!((~AV_RN32(src + i) &
               (AV_RN32(src + i) - 0x01000101U)) &
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

    dst = &rbsp->rbsp_buffer[rbsp->rbsp_buffer_size];

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
    rbsp->rbsp_buffer_size += si;

    return si;
}

static const char *const vvc_nal_type_name[32] = {
    "TRAIL_NUT", // VVC_TRAIL_NUT
    "STSA_NUT", // VVC_STSA_NUT
    "RADL_NUT", // VVC_RADL_NUT
    "RASL_NUT", // VVC_RASL_NUT
    "RSV_VCL4", // VVC_RSV_VCL_4
    "RSV_VCL5", // VVC_RSV_VCL_5
    "RSV_VCL6", // VVC_RSV_VCL_6
    "IDR_W_RADL", // VVC_IDR_W_RADL
    "IDR_N_LP", // VVC_IDR_N_LP
    "CRA_NUT", // VVC_CRA_NUT
    "GDR_NUT", // VVC_GDR_NUT
    "RSV_IRAP_11", // VVC_RSV_IRAP_11
    "OPI_NUT", // VVC_OPI_NUT
    "DCI_NUT", // VVC_DCI_NUT
    "VPS_NUT", // VVC_VPS_NUT
    "SPS_NUT", // VVC_SPS_NUT
    "PPS_NUT", // VVC_PPS_NUT
    "APS_PREFIX", // VVC_PREFIX_APS_NUT
    "APS_SUFFIX", // VVC_SUFFIX_APS_NUT
    "PH_NUT", // VVC_PH_NUT
    "AUD_NUT", // VVC_AUD_NUT
    "EOS_NUT", // VVC_EOS_NUT
    "EOB_NUT", // VVC_EOB_NUT
    "SEI_PREFIX", // VVC_PREFIX_SEI_NUT
    "SEI_SUFFIX", // VVC_SUFFIX_SEI_NUT
    "FD_NUT", // VVC_FD_NUT
    "RSV_NVCL26", // VVC_RSV_NVCL_26
    "RSV_NVCL27", // VVC_RSV_NVCL_27
    "UNSPEC28", // VVC_UNSPEC_28
    "UNSPEC29", // VVC_UNSPEC_29
    "UNSPEC30", // VVC_UNSPEC_30
    "UNSPEC31", // VVC_UNSPEC_31
};

static const char *vvc_nal_unit_name(int nal_type)
{
    av_assert0(nal_type >= 0 && nal_type < 32);
    return vvc_nal_type_name[nal_type];
}

static const char *const hevc_nal_type_name[64] = {
    "TRAIL_N", // HEVC_NAL_TRAIL_N
    "TRAIL_R", // HEVC_NAL_TRAIL_R
    "TSA_N", // HEVC_NAL_TSA_N
    "TSA_R", // HEVC_NAL_TSA_R
    "STSA_N", // HEVC_NAL_STSA_N
    "STSA_R", // HEVC_NAL_STSA_R
    "RADL_N", // HEVC_NAL_RADL_N
    "RADL_R", // HEVC_NAL_RADL_R
    "RASL_N", // HEVC_NAL_RASL_N
    "RASL_R", // HEVC_NAL_RASL_R
    "RSV_VCL_N10", // HEVC_NAL_VCL_N10
    "RSV_VCL_R11", // HEVC_NAL_VCL_R11
    "RSV_VCL_N12", // HEVC_NAL_VCL_N12
    "RSV_VLC_R13", // HEVC_NAL_VCL_R13
    "RSV_VCL_N14", // HEVC_NAL_VCL_N14
    "RSV_VCL_R15", // HEVC_NAL_VCL_R15
    "BLA_W_LP", // HEVC_NAL_BLA_W_LP
    "BLA_W_RADL", // HEVC_NAL_BLA_W_RADL
    "BLA_N_LP", // HEVC_NAL_BLA_N_LP
    "IDR_W_RADL", // HEVC_NAL_IDR_W_RADL
    "IDR_N_LP", // HEVC_NAL_IDR_N_LP
    "CRA_NUT", // HEVC_NAL_CRA_NUT
    "RSV_IRAP_VCL22", // HEVC_NAL_RSV_IRAP_VCL22
    "RSV_IRAP_VCL23", // HEVC_NAL_RSV_IRAP_VCL23
    "RSV_VCL24", // HEVC_NAL_RSV_VCL24
    "RSV_VCL25", // HEVC_NAL_RSV_VCL25
    "RSV_VCL26", // HEVC_NAL_RSV_VCL26
    "RSV_VCL27", // HEVC_NAL_RSV_VCL27
    "RSV_VCL28", // HEVC_NAL_RSV_VCL28
    "RSV_VCL29", // HEVC_NAL_RSV_VCL29
    "RSV_VCL30", // HEVC_NAL_RSV_VCL30
    "RSV_VCL31", // HEVC_NAL_RSV_VCL31
    "VPS", // HEVC_NAL_VPS
    "SPS", // HEVC_NAL_SPS
    "PPS", // HEVC_NAL_PPS
    "AUD", // HEVC_NAL_AUD
    "EOS_NUT", // HEVC_NAL_EOS_NUT
    "EOB_NUT", // HEVC_NAL_EOB_NUT
    "FD_NUT", // HEVC_NAL_FD_NUT
    "SEI_PREFIX", // HEVC_NAL_SEI_PREFIX
    "SEI_SUFFIX", // HEVC_NAL_SEI_SUFFIX
    "RSV_NVCL41", // HEVC_NAL_RSV_NVCL41
    "RSV_NVCL42", // HEVC_NAL_RSV_NVCL42
    "RSV_NVCL43", // HEVC_NAL_RSV_NVCL43
    "RSV_NVCL44", // HEVC_NAL_RSV_NVCL44
    "RSV_NVCL45", // HEVC_NAL_RSV_NVCL45
    "RSV_NVCL46", // HEVC_NAL_RSV_NVCL46
    "RSV_NVCL47", // HEVC_NAL_RSV_NVCL47
    "UNSPEC48", // HEVC_NAL_UNSPEC48
    "UNSPEC49", // HEVC_NAL_UNSPEC49
    "UNSPEC50", // HEVC_NAL_UNSPEC50
    "UNSPEC51", // HEVC_NAL_UNSPEC51
    "UNSPEC52", // HEVC_NAL_UNSPEC52
    "UNSPEC53", // HEVC_NAL_UNSPEC53
    "UNSPEC54", // HEVC_NAL_UNSPEC54
    "UNSPEC55", // HEVC_NAL_UNSPEC55
    "UNSPEC56", // HEVC_NAL_UNSPEC56
    "UNSPEC57", // HEVC_NAL_UNSPEC57
    "UNSPEC58", // HEVC_NAL_UNSPEC58
    "UNSPEC59", // HEVC_NAL_UNSPEC59
    "UNSPEC60", // HEVC_NAL_UNSPEC60
    "UNSPEC61", // HEVC_NAL_UNSPEC61
    "UNSPEC62", // HEVC_NAL_UNSPEC62
    "UNSPEC63", // HEVC_NAL_UNSPEC63
};

static const char *hevc_nal_unit_name(int nal_type)
{
    av_assert0(nal_type >= 0 && nal_type < 64);
    return hevc_nal_type_name[nal_type];
}

static const char *const h264_nal_type_name[32] = {
    "Unspecified 0", //H264_NAL_UNSPECIFIED
    "Coded slice of a non-IDR picture", // H264_NAL_SLICE
    "Coded slice data partition A", // H264_NAL_DPA
    "Coded slice data partition B", // H264_NAL_DPB
    "Coded slice data partition C", // H264_NAL_DPC
    "IDR", // H264_NAL_IDR_SLICE
    "SEI", // H264_NAL_SEI
    "SPS", // H264_NAL_SPS
    "PPS", // H264_NAL_PPS
    "AUD", // H264_NAL_AUD
    "End of sequence", // H264_NAL_END_SEQUENCE
    "End of stream", // H264_NAL_END_STREAM
    "Filler data", // H264_NAL_FILLER_DATA
    "SPS extension", // H264_NAL_SPS_EXT
    "Prefix", // H264_NAL_PREFIX
    "Subset SPS", // H264_NAL_SUB_SPS
    "Depth parameter set", // H264_NAL_DPS
    "Reserved 17", // H264_NAL_RESERVED17
    "Reserved 18", // H264_NAL_RESERVED18
    "Auxiliary coded picture without partitioning", // H264_NAL_AUXILIARY_SLICE
    "Slice extension", // H264_NAL_EXTEN_SLICE
    "Slice extension for a depth view or a 3D-AVC texture view", // H264_NAL_DEPTH_EXTEN_SLICE
    "Reserved 22", // H264_NAL_RESERVED22
    "Reserved 23", // H264_NAL_RESERVED23
    "Unspecified 24", // H264_NAL_UNSPECIFIED24
    "Unspecified 25", // H264_NAL_UNSPECIFIED25
    "Unspecified 26", // H264_NAL_UNSPECIFIED26
    "Unspecified 27", // H264_NAL_UNSPECIFIED27
    "Unspecified 28", // H264_NAL_UNSPECIFIED28
    "Unspecified 29", // H264_NAL_UNSPECIFIED29
    "Unspecified 30", // H264_NAL_UNSPECIFIED30
    "Unspecified 31", // H264_NAL_UNSPECIFIED31
};

static const char *h264_nal_unit_name(int nal_type)
{
    av_assert0(nal_type >= 0 && nal_type < 32);
    return h264_nal_type_name[nal_type];
}

static int get_bit_length(H2645NAL *nal, int min_size, int skip_trailing_zeros)
{
    int size = nal->size;
    int trailing_padding = 0;

    while (skip_trailing_zeros && size > 0 && nal->data[size - 1] == 0)
        size--;

    if (!size)
        return 0;

    if (size <= min_size) {
        if (nal->size < min_size)
            return AVERROR_INVALIDDATA;
        size = min_size;
    } else {
        int v = nal->data[size - 1];
        /* remove the stop bit and following trailing zeros,
         * or nothing for damaged bitstreams */
        if (v)
            trailing_padding = ff_ctz(v) + 1;
    }

    if (size > INT_MAX / 8)
        return AVERROR(ERANGE);
    size *= 8;

    return size - trailing_padding;
}

/**
 * @return AVERROR_INVALIDDATA if the packet is not a valid NAL unit,
 * 0 otherwise
 */
static int vvc_parse_nal_header(H2645NAL *nal, void *logctx)
{
    GetBitContext *gb = &nal->gb;

    if (get_bits1(gb) != 0)     //forbidden_zero_bit
        return AVERROR_INVALIDDATA;

    skip_bits1(gb);             //nuh_reserved_zero_bit

    nal->nuh_layer_id = get_bits(gb, 6);
    nal->type = get_bits(gb, 5);
    nal->temporal_id = get_bits(gb, 3) - 1;
    if (nal->temporal_id < 0)
        return AVERROR_INVALIDDATA;

    if ((nal->type >= VVC_IDR_W_RADL && nal->type <= VVC_RSV_IRAP_11) && nal->temporal_id)
        return AVERROR_INVALIDDATA;

    av_log(logctx, AV_LOG_DEBUG,
           "nal_unit_type: %d(%s), nuh_layer_id: %d, temporal_id: %d\n",
           nal->type, vvc_nal_unit_name(nal->type), nal->nuh_layer_id, nal->temporal_id);

    return 0;
}

static int hevc_parse_nal_header(H2645NAL *nal, void *logctx)
{
    GetBitContext *gb = &nal->gb;

    if (get_bits1(gb) != 0)
        return AVERROR_INVALIDDATA;

    nal->type = get_bits(gb, 6);

    nal->nuh_layer_id = get_bits(gb, 6);
    nal->temporal_id = get_bits(gb, 3) - 1;
    if (nal->temporal_id < 0)
        return AVERROR_INVALIDDATA;

    av_log(logctx, AV_LOG_DEBUG,
           "nal_unit_type: %d(%s), nuh_layer_id: %d, temporal_id: %d\n",
           nal->type, hevc_nal_unit_name(nal->type), nal->nuh_layer_id, nal->temporal_id);

    return 0;
}

static int h264_parse_nal_header(H2645NAL *nal, void *logctx)
{
    GetBitContext *gb = &nal->gb;

    if (get_bits1(gb) != 0)
        return AVERROR_INVALIDDATA;

    nal->ref_idc = get_bits(gb, 2);
    nal->type    = get_bits(gb, 5);

    av_log(logctx, AV_LOG_DEBUG,
           "nal_unit_type: %d(%s), nal_ref_idc: %d\n",
           nal->type, h264_nal_unit_name(nal->type), nal->ref_idc);

    return 0;
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

static void alloc_rbsp_buffer(H2645RBSP *rbsp, unsigned int size, int use_ref)
{
    int min_size = size;

    if (size > INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE)
        goto fail;
    size += AV_INPUT_BUFFER_PADDING_SIZE;

    if (rbsp->rbsp_buffer_alloc_size >= size &&
        (!rbsp->rbsp_buffer_ref || av_buffer_is_writable(rbsp->rbsp_buffer_ref))) {
        av_assert0(rbsp->rbsp_buffer);
        memset(rbsp->rbsp_buffer + min_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        return;
    }

    size = FFMIN(size + size / 16 + 32, INT_MAX);

    if (rbsp->rbsp_buffer_ref)
        av_buffer_unref(&rbsp->rbsp_buffer_ref);
    else
        av_free(rbsp->rbsp_buffer);

    rbsp->rbsp_buffer = av_mallocz(size);
    if (!rbsp->rbsp_buffer)
        goto fail;
    rbsp->rbsp_buffer_alloc_size = size;

    if (use_ref) {
        rbsp->rbsp_buffer_ref = av_buffer_create(rbsp->rbsp_buffer, size,
                                                 NULL, NULL, 0);
        if (!rbsp->rbsp_buffer_ref)
            goto fail;
    }

    return;

fail:
    rbsp->rbsp_buffer_alloc_size = 0;
    if (rbsp->rbsp_buffer_ref) {
        av_buffer_unref(&rbsp->rbsp_buffer_ref);
        rbsp->rbsp_buffer = NULL;
    } else
        av_freep(&rbsp->rbsp_buffer);

    return;
}

int ff_h2645_packet_split(H2645Packet *pkt, const uint8_t *buf, int length,
                          void *logctx, int is_nalff, int nal_length_size,
                          enum AVCodecID codec_id, int small_padding, int use_ref)
{
    GetByteContext bc;
    int consumed, ret = 0;
    int next_avc = is_nalff ? 0 : length;
    int64_t padding = small_padding ? 0 : MAX_MBPAIR_SIZE;

    bytestream2_init(&bc, buf, length);
    alloc_rbsp_buffer(&pkt->rbsp, length + padding, use_ref);

    if (!pkt->rbsp.rbsp_buffer)
        return AVERROR(ENOMEM);

    pkt->rbsp.rbsp_buffer_size = 0;
    pkt->nb_nals = 0;
    while (bytestream2_get_bytes_left(&bc) >= 4) {
        H2645NAL *nal;
        int extract_length = 0;
        int skip_trailing_zeros = 1;

        if (bytestream2_tell(&bc) == next_avc) {
            int i = 0;
            extract_length = get_nalsize(nal_length_size,
                                         bc.buffer, bytestream2_get_bytes_left(&bc), &i, logctx);
            if (extract_length < 0)
                return extract_length;

            bytestream2_skip(&bc, nal_length_size);

            next_avc = bytestream2_tell(&bc) + extract_length;
        } else {
            int buf_index;

            if (bytestream2_tell(&bc) > next_avc)
                av_log(logctx, AV_LOG_WARNING, "Exceeded next NALFF position, re-syncing.\n");

            /* search start code */
            buf_index = find_next_start_code(bc.buffer, buf + next_avc);

            bytestream2_skip(&bc, buf_index);

            if (!bytestream2_get_bytes_left(&bc)) {
                if (pkt->nb_nals > 0) {
                    // No more start codes: we discarded some irrelevant
                    // bytes at the end of the packet.
                    return 0;
                } else {
                    av_log(logctx, AV_LOG_ERROR, "No start code is found.\n");
                    return AVERROR_INVALIDDATA;
                }
            }

            extract_length = FFMIN(bytestream2_get_bytes_left(&bc), next_avc - bytestream2_tell(&bc));

            if (bytestream2_tell(&bc) >= next_avc) {
                /* skip to the start of the next NAL */
                bytestream2_skip(&bc, next_avc - bytestream2_tell(&bc));
                continue;
            }
        }

        if (pkt->nals_allocated < pkt->nb_nals + 1) {
            int new_size = pkt->nals_allocated + 1;
            void *tmp;

            if (new_size >= INT_MAX / sizeof(*pkt->nals))
                return AVERROR(ENOMEM);

            tmp = av_fast_realloc(pkt->nals, &pkt->nal_buffer_size, new_size * sizeof(*pkt->nals));
            if (!tmp)
                return AVERROR(ENOMEM);

            pkt->nals = tmp;
            memset(pkt->nals + pkt->nals_allocated, 0, sizeof(*pkt->nals));

            nal = &pkt->nals[pkt->nb_nals];
            nal->skipped_bytes_pos_size = FFMIN(1024, extract_length/3+1); // initial buffer size
            nal->skipped_bytes_pos = av_malloc_array(nal->skipped_bytes_pos_size, sizeof(*nal->skipped_bytes_pos));
            if (!nal->skipped_bytes_pos)
                return AVERROR(ENOMEM);

            pkt->nals_allocated = new_size;
        }
        nal = &pkt->nals[pkt->nb_nals];

        consumed = ff_h2645_extract_rbsp(bc.buffer, extract_length, &pkt->rbsp, nal, small_padding);
        if (consumed < 0)
            return consumed;

        if (is_nalff && (extract_length != consumed) && extract_length)
            av_log(logctx, AV_LOG_DEBUG,
                   "NALFF: Consumed only %d bytes instead of %d\n",
                   consumed, extract_length);

        bytestream2_skip(&bc, consumed);

        /* see commit 3566042a0 */
        if (bytestream2_get_bytes_left(&bc) >= 4 &&
            bytestream2_peek_be32(&bc) == 0x000001E0)
            skip_trailing_zeros = 0;

        nal->size_bits = get_bit_length(nal, 1 + (codec_id == AV_CODEC_ID_HEVC),
                                        skip_trailing_zeros);

        if (nal->size <= 0 || nal->size_bits <= 0)
            continue;

        ret = init_get_bits(&nal->gb, nal->data, nal->size_bits);
        if (ret < 0)
            return ret;

        /* Reset type in case it contains a stale value from a previously parsed NAL */
        nal->type = 0;

        if (codec_id == AV_CODEC_ID_VVC)
            ret = vvc_parse_nal_header(nal, logctx);
        else if (codec_id == AV_CODEC_ID_HEVC)
            ret = hevc_parse_nal_header(nal, logctx);
        else
            ret = h264_parse_nal_header(nal, logctx);
        if (ret < 0) {
            av_log(logctx, AV_LOG_WARNING, "Invalid NAL unit %d, skipping.\n",
                   nal->type);
            continue;
        }

        pkt->nb_nals++;
    }

    return 0;
}

void ff_h2645_packet_uninit(H2645Packet *pkt)
{
    int i;
    for (i = 0; i < pkt->nals_allocated; i++) {
        av_freep(&pkt->nals[i].skipped_bytes_pos);
    }
    av_freep(&pkt->nals);
    pkt->nals_allocated = pkt->nal_buffer_size = 0;
    if (pkt->rbsp.rbsp_buffer_ref) {
        av_buffer_unref(&pkt->rbsp.rbsp_buffer_ref);
        pkt->rbsp.rbsp_buffer = NULL;
    } else
        av_freep(&pkt->rbsp.rbsp_buffer);
    pkt->rbsp.rbsp_buffer_alloc_size = pkt->rbsp.rbsp_buffer_size = 0;
}
