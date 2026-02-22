/*
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

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/mem.h"

#include "bytestream.h"
#include "cbs.h"
#include "cbs_internal.h"
#include "cbs_h2645.h"
#include "h264.h"
#include "h2645_parse.h"
#include "vvc.h"

#include "hevc/hevc.h"

int ff_cbs_h2645_payload_extension_present(GetBitContext *gbc, uint32_t payload_size,
                                             int cur_pos)
{
    int bits_left = payload_size * 8 - cur_pos;
    return (bits_left > 0 &&
            (bits_left > 7 || show_bits(gbc, bits_left) & MAX_UINT_BITS(bits_left - 1)));
}

int ff_cbs_read_ue_golomb(CodedBitstreamContext *ctx, GetBitContext *gbc,
                              const char *name, const int *subscripts,
                              uint32_t *write_to,
                              uint32_t range_min, uint32_t range_max)
{
    uint32_t leading_bits, value;
    int max_length, leading_zeroes;

    CBS_TRACE_READ_START();

    max_length = FFMIN(get_bits_left(gbc), 32);

    leading_bits = max_length ? show_bits_long(gbc, max_length) : 0;
    if (leading_bits == 0) {
        if (max_length >= 32) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid ue-golomb code at "
                   "%s: more than 31 zeroes.\n", name);
            return AVERROR_INVALIDDATA;
        } else {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid ue-golomb code at "
                   "%s: bitstream ended.\n", name);
            return AVERROR_INVALIDDATA;
        }
    }

    leading_zeroes = max_length - 1 - av_log2(leading_bits);
    skip_bits_long(gbc, leading_zeroes);

    if (get_bits_left(gbc) < leading_zeroes + 1) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid ue-golomb code at "
               "%s: bitstream ended.\n", name);
        return AVERROR_INVALIDDATA;
    }

    value = get_bits_long(gbc, leading_zeroes + 1) - 1;

    CBS_TRACE_READ_END();

    if (value < range_min || value > range_max) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "%s out of range: "
               "%"PRIu32", but must be in [%"PRIu32",%"PRIu32"].\n",
               name, value, range_min, range_max);
        return AVERROR_INVALIDDATA;
    }

    *write_to = value;
    return 0;
}

int ff_cbs_read_se_golomb(CodedBitstreamContext *ctx, GetBitContext *gbc,
                              const char *name, const int *subscripts,
                              int32_t *write_to,
                              int32_t range_min, int32_t range_max)
{
    uint32_t leading_bits, unsigned_value;
    int max_length, leading_zeroes;
    int32_t value;

    CBS_TRACE_READ_START();

    max_length = FFMIN(get_bits_left(gbc), 32);

    leading_bits = max_length ? show_bits_long(gbc, max_length) : 0;
    if (leading_bits == 0) {
        if (max_length >= 32) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid se-golomb code at "
                   "%s: more than 31 zeroes.\n", name);
            return AVERROR_INVALIDDATA;
        } else {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid se-golomb code at "
                   "%s: bitstream ended.\n", name);
            return AVERROR_INVALIDDATA;
        }
    }

    leading_zeroes = max_length - 1 - av_log2(leading_bits);
    skip_bits_long(gbc, leading_zeroes);

    if (get_bits_left(gbc) < leading_zeroes + 1) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid se-golomb code at "
               "%s: bitstream ended.\n", name);
        return AVERROR_INVALIDDATA;
    }

    unsigned_value = get_bits_long(gbc, leading_zeroes + 1);

    if (unsigned_value & 1)
        value = -(int32_t)(unsigned_value / 2);
    else
        value = unsigned_value / 2;

    CBS_TRACE_READ_END();

    if (value < range_min || value > range_max) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "%s out of range: "
               "%"PRId32", but must be in [%"PRId32",%"PRId32"].\n",
               name, value, range_min, range_max);
        return AVERROR_INVALIDDATA;
    }

    *write_to = value;
    return 0;
}

int ff_cbs_write_ue_golomb(CodedBitstreamContext *ctx, PutBitContext *pbc,
                               const char *name, const int *subscripts,
                               uint32_t value,
                               uint32_t range_min, uint32_t range_max)
{
    int len;

    CBS_TRACE_WRITE_START();

    if (value < range_min || value > range_max) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "%s out of range: "
               "%"PRIu32", but must be in [%"PRIu32",%"PRIu32"].\n",
               name, value, range_min, range_max);
        return AVERROR_INVALIDDATA;
    }
    av_assert0(value != UINT32_MAX);

    len = av_log2(value + 1);
    if (put_bits_left(pbc) < 2 * len + 1)
        return AVERROR(ENOSPC);

    put_bits(pbc, len, 0);
    if (len + 1 < 32)
        put_bits(pbc, len + 1, value + 1);
    else
        put_bits32(pbc, value + 1);

    CBS_TRACE_WRITE_END();

    return 0;
}

int ff_cbs_write_se_golomb(CodedBitstreamContext *ctx, PutBitContext *pbc,
                               const char *name, const int *subscripts,
                               int32_t value,
                               int32_t range_min, int32_t range_max)
{
    int len;
    uint32_t uvalue;

    CBS_TRACE_WRITE_START();

    if (value < range_min || value > range_max) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "%s out of range: "
               "%"PRId32", but must be in [%"PRId32",%"PRId32"].\n",
               name, value, range_min, range_max);
        return AVERROR_INVALIDDATA;
    }
    av_assert0(value != INT32_MIN);

    if (value == 0)
        uvalue = 0;
    else if (value > 0)
        uvalue = 2 * (uint32_t)value - 1;
    else
        uvalue = 2 * (uint32_t)-value;

    len = av_log2(uvalue + 1);
    if (put_bits_left(pbc) < 2 * len + 1)
        return AVERROR(ENOSPC);

    put_bits(pbc, len, 0);
    if (len + 1 < 32)
        put_bits(pbc, len + 1, uvalue + 1);
    else
        put_bits32(pbc, uvalue + 1);

    CBS_TRACE_WRITE_END();

    return 0;
}

int ff_cbs_h2645_read_more_rbsp_data(GetBitContext *gbc)
{
    int bits_left = get_bits_left(gbc);
    if (bits_left > 8)
        return 1;
    if (bits_left == 0)
        return 0;
    if (show_bits(gbc, bits_left) & MAX_UINT_BITS(bits_left - 1))
        return 1;
    return 0;
}

int ff_cbs_h2645_fragment_add_nals(CodedBitstreamContext *ctx,
                                       CodedBitstreamFragment *frag,
                                       const H2645Packet *packet)
{
    int err, i;

    for (i = 0; i < packet->nb_nals; i++) {
        const H2645NAL *nal = &packet->nals[i];
        AVBufferRef *ref;
        size_t size = nal->size;
        enum AVCodecID codec_id = ctx->codec->codec_id;

        if (codec_id == AV_CODEC_ID_HEVC && nal->nuh_layer_id > 0 &&
            (nal->type < HEVC_NAL_VPS || nal->type > HEVC_NAL_PPS))
            continue;

        // Remove trailing zeroes.
        while (size > 0 && nal->data[size - 1] == 0)
            --size;
        if (size == 0) {
            av_log(ctx->log_ctx, AV_LOG_VERBOSE, "Discarding empty 0 NAL unit\n");
            continue;
        }

        ref = (nal->data == nal->raw_data) ? frag->data_ref
                                           : packet->rbsp.rbsp_buffer_ref;

        err = ff_cbs_append_unit_data(frag, nal->type,
                            (uint8_t*)nal->data, size, ref);
        if (err < 0)
            return err;
    }

    return 0;
}

int ff_cbs_h2645_write_slice_data(CodedBitstreamContext *ctx,
                                      PutBitContext *pbc, const uint8_t *data,
                                      size_t data_size, int data_bit_start)
{
    size_t rest  = data_size - (data_bit_start + 7) / 8;
    const uint8_t *pos = data + data_bit_start / 8;

    av_assert0(data_bit_start >= 0 &&
               data_size > data_bit_start / 8);

    if (data_size * 8 + 8 > put_bits_left(pbc))
        return AVERROR(ENOSPC);

    if (!rest)
        goto rbsp_stop_one_bit;

    // First copy the remaining bits of the first byte
    // The above check ensures that we do not accidentally
    // copy beyond the rbsp_stop_one_bit.
    if (data_bit_start % 8)
        put_bits(pbc, 8 - data_bit_start % 8,
                 *pos++ & MAX_UINT_BITS(8 - data_bit_start % 8));

    if (put_bits_count(pbc) % 8 == 0) {
        // If the writer is aligned at this point,
        // memcpy can be used to improve performance.
        // This happens normally for CABAC.
        flush_put_bits(pbc);
        memcpy(put_bits_ptr(pbc), pos, rest);
        skip_put_bytes(pbc, rest);
    } else {
        // If not, we have to copy manually.
        // rbsp_stop_one_bit forces us to special-case
        // the last byte.
        uint8_t temp;
        int i;

        for (; rest > 4; rest -= 4, pos += 4)
            put_bits32(pbc, AV_RB32(pos));

        for (; rest > 1; rest--, pos++)
            put_bits(pbc, 8, *pos);

    rbsp_stop_one_bit:
        temp = rest ? *pos : *pos & MAX_UINT_BITS(8 - data_bit_start % 8);

        av_assert0(temp);
        i = ff_ctz(*pos);
        temp = temp >> i;
        i = rest ? (8 - i) : (8 - i - data_bit_start % 8);
        put_bits(pbc, i, temp);
        if (put_bits_count(pbc) % 8)
            put_bits(pbc, 8 - put_bits_count(pbc) % 8, 0);
    }

    return 0;
}

int ff_cbs_h2645_unit_requires_zero_byte(enum AVCodecID codec_id,
                                             CodedBitstreamUnitType type,
                                             int nal_unit_index)
{
    // Section B.1.2 in H.264, section B.2.2 in H.265, H.266.
    if (nal_unit_index == 0) {
        // Assume that this is the first NAL unit in an access unit.
        return 1;
    }
    if (codec_id == AV_CODEC_ID_H264)
        return type == H264_NAL_SPS || type == H264_NAL_PPS;
    if (codec_id == AV_CODEC_ID_HEVC)
        return type == HEVC_NAL_VPS || type == HEVC_NAL_SPS || type == HEVC_NAL_PPS;
    if (codec_id == AV_CODEC_ID_VVC)
        return type >= VVC_OPI_NUT && type <= VVC_SUFFIX_APS_NUT;
    return 0;
}

int ff_cbs_h2645_assemble_fragment(CodedBitstreamContext *ctx,
                                       CodedBitstreamFragment *frag)
{
    uint8_t *data;
    size_t max_size, dp, sp;
    int err, i, zero_run;

    for (i = 0; i < frag->nb_units; i++) {
        // Data should already all have been written when we get here.
        av_assert0(frag->units[i].data);
    }

    max_size = 0;
    for (i = 0; i < frag->nb_units; i++) {
        // Start code + content with worst-case emulation prevention.
        max_size += 4 + frag->units[i].data_size * 3 / 2;
    }

    data = av_realloc(NULL, max_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!data)
        return AVERROR(ENOMEM);

    dp = 0;
    for (i = 0; i < frag->nb_units; i++) {
        CodedBitstreamUnit *unit = &frag->units[i];

        if (unit->data_bit_padding > 0) {
            if (i < frag->nb_units - 1)
                av_log(ctx->log_ctx, AV_LOG_WARNING, "Probably invalid "
                       "unaligned padding on non-final NAL unit.\n");
            else
                frag->data_bit_padding = unit->data_bit_padding;
        }

        if (ff_cbs_h2645_unit_requires_zero_byte(ctx->codec->codec_id, unit->type, i)) {
            // zero_byte
            data[dp++] = 0;
        }
        // start_code_prefix_one_3bytes
        data[dp++] = 0;
        data[dp++] = 0;
        data[dp++] = 1;

        zero_run = 0;
        for (sp = 0; sp < unit->data_size; sp++) {
            if (zero_run < 2) {
                if (unit->data[sp] == 0)
                    ++zero_run;
                else
                    zero_run = 0;
            } else {
                if ((unit->data[sp] & ~3) == 0) {
                    // emulation_prevention_three_byte
                    data[dp++] = 3;
                }
                zero_run = unit->data[sp] == 0;
            }
            data[dp++] = unit->data[sp];
        }
    }

    av_assert0(dp <= max_size);
    err = av_reallocp(&data, dp + AV_INPUT_BUFFER_PADDING_SIZE);
    if (err)
        return err;
    memset(data + dp, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    frag->data_ref = av_buffer_create(data, dp + AV_INPUT_BUFFER_PADDING_SIZE,
                                      NULL, NULL, 0);
    if (!frag->data_ref) {
        av_freep(&data);
        return AVERROR(ENOMEM);
    }

    frag->data = data;
    frag->data_size = dp;

    return 0;
}
