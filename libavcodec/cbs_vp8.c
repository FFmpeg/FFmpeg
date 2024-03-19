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

#include "libavutil/avassert.h"

#include "cbs.h"
#include "cbs_internal.h"
#include "cbs_vp8.h"

#include <stdbool.h>

#define DEFAULT_PROB 0x80

// The probability table is defined in 'vp8data.c'.
extern const uint8_t ff_vp8_token_update_probs[4][8][3][11];

// Implements VP8 boolean decoder using GetBitContext to read the bitstream.
typedef struct CBSVP8BoolDecoder {
    GetBitContext *gbc;

    uint8_t value;
    uint8_t range;

    uint8_t count; // Store the number of bits in the `value` buffer.

} CBSVP8BoolDecoder;

static int cbs_vp8_bool_decoder_init(CBSVP8BoolDecoder *decoder, GetBitContext *gbc)
{
    av_assert0(decoder);
    av_assert0(gbc);

    decoder->gbc = gbc;
    decoder->value = 0;
    decoder->range = 255;

    decoder->count = 0;

    return 0;
}

static bool cbs_vp8_bool_decoder_fill_value(CBSVP8BoolDecoder *decoder)
{
    int bits = 8 - decoder->count;

    av_assert0(decoder->count <= 8);
    if (decoder->count == 8) {
      return true;
    }

    if (get_bits_left(decoder->gbc) >= bits) {
        decoder->value |= get_bits(decoder->gbc, bits);
        decoder->count += bits;
    }

    return (decoder->count == 8);
}

static int cbs_vp8_bool_decoder_read_bool(CBSVP8BoolDecoder *decoder,
                                          const uint8_t prob, uint8_t *output)
{
    uint8_t split = 1 + (((decoder->range - 1) * prob) >> 8);

    if (!cbs_vp8_bool_decoder_fill_value(decoder)) {
        return AVERROR_INVALIDDATA;
    }

    av_assert0(decoder->count == 8);
    if (decoder->value >= split) {
        *output = 1;
        decoder->range -= split;
        decoder->value -= split;
    } else {
        *output = 0;
        decoder->range = split;
    }

    while (decoder->range < 128) {
        decoder->value <<= 1;
        decoder->range <<= 1;
        --decoder->count;
    }

    return 0;
}

static int cbs_vp8_bool_decoder_read_literal(CBSVP8BoolDecoder *decoder,
                                             const uint8_t prob,
                                             uint32_t num_bits,
                                             uint32_t *output)
{
    int ret = 0;

    av_assert0(num_bits <= 32);

    *output = 0;
    for (; num_bits > 0; --num_bits) {
        uint8_t bit_output = 0;
        if ((ret = cbs_vp8_bool_decoder_read_bool(decoder, prob,
                                                  &bit_output)) != 0) {
            return ret;
        }

        *output = (*output << 1) | bit_output;
    }

    return 0;
}

static int cbs_vp8_bool_decoder_read_unsigned(
    CodedBitstreamContext *ctx, CBSVP8BoolDecoder *bool_decoder, int width,
    uint8_t prob, const char *name, const int *subscripts, uint32_t *write_to,
    bool trace_enable)
{
    int ret = 0;
    GetBitContext *gbc = bool_decoder->gbc;
    uint32_t value;

    CBS_TRACE_READ_START();

    av_assert0(width >= 0 && width <= 8);

    ret = cbs_vp8_bool_decoder_read_literal(bool_decoder, prob, width, &value);
    if (ret != 0) {
        return ret;
    }

    if (trace_enable) {
      CBS_TRACE_READ_END();
    }

    *write_to = value;
    return 0;
}

static int cbs_vp8_bool_decoder_read_signed(
    CodedBitstreamContext *ctx, CBSVP8BoolDecoder *bool_decoder, int width,
    uint8_t prob, const char *name, const int *subscripts, int32_t *write_to)
{
    int ret = 0;
    GetBitContext *gbc = bool_decoder->gbc;
    int32_t value;
    uint8_t sign = 0;

    CBS_TRACE_READ_START();

    av_assert0(width >= 0 && width <= 8);

    ret = cbs_vp8_bool_decoder_read_literal(bool_decoder, prob, width, &value);
    if (ret != 0) {
        return ret;
    }

    ret = cbs_vp8_bool_decoder_read_bool(bool_decoder, prob, &sign);
    if (ret != 0) {
        return ret;
    }

    if (sign) {
        value = -value;
    }

    CBS_TRACE_READ_END();

    *write_to = value;
    return 0;
}

static int cbs_vp8_read_unsigned_le(CodedBitstreamContext *ctx,
                                    GetBitContext *gbc, int width,
                                    const char *name, const int *subscripts,
                                    uint32_t *write_to, uint32_t range_min,
                                    uint32_t range_max)
{
    int32_t value;

    CBS_TRACE_READ_START();

    av_assert0(width > 0 && width <= 24);

    if (get_bits_left(gbc) < width) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid value: bitstream ended.\n");
        return AVERROR_INVALIDDATA;
    }

    value = get_bits_le(gbc, width);

    CBS_TRACE_READ_END();

    if (value < range_min || value > range_max) {
        av_log(ctx->log_ctx, AV_LOG_ERROR,
               "%s out of range: "
               "%" PRIu32 ", but must be in [%" PRIu32 ",%" PRIu32 "].\n",
               name, value, range_min, range_max);
        return AVERROR_INVALIDDATA;
    }

    *write_to = value;
    return 0;
}

#define HEADER(name) \
    do { \
        ff_cbs_trace_header(ctx, name); \
    } while (0)

#define CHECK(call) \
    do { \
        int err = (call); \
        if (err < 0) \
            return err; \
    } while (0)

#define FUNC_NAME(rw, codec, name) cbs_##codec##_##rw##_##name
#define FUNC_VP8(rw, name) FUNC_NAME(rw, vp8, name)
#define FUNC(name) FUNC_VP8(READWRITE, name)

#define SUBSCRIPTS(subs, ...) \
    (subs > 0 ? ((int[subs + 1]){subs, __VA_ARGS__}) : NULL)

#define f(width, name) xf(width, name, 0, )

// bool [de|en]coder methods.
#define bc_f(width, name) bc_unsigned_subs(width, DEFAULT_PROB, true, name, 0, )
#define bc_s(width, name) bc_signed_subs(width, DEFAULT_PROB, name, 0, )
#define bc_fs(width, name, subs, ...) \
    bc_unsigned_subs(width, DEFAULT_PROB, true, name, subs, __VA_ARGS__)
#define bc_ss(width, name, subs, ...) \
    bc_signed_subs(width, DEFAULT_PROB, name, subs, __VA_ARGS__)

// bool [de|en]coder methods for boolean value and disable tracing.
#define bc_b(name) bc_unsigned_subs(1, DEFAULT_PROB, false, name, 0, )
#define bc_b_prob(prob, name) bc_unsigned_subs(1, prob, false, name, 0, )

#define READ
#define READWRITE read
#define RWContext GetBitContext
#define CBSVP8BoolCodingRW CBSVP8BoolDecoder

#define xf(width, name, subs, ...) \
    do { \
        uint32_t value; \
        CHECK(cbs_vp8_read_unsigned_le(ctx, rw, width, #name, \
                                       SUBSCRIPTS(subs, __VA_ARGS__), &value, \
                                       0, MAX_UINT_BITS(width))); \
        current->name = value; \
    } while (0)

#define fixed(width, name, value) \
    do { \
        uint32_t fixed_value; \
        CHECK(cbs_vp8_read_unsigned_le(ctx, rw, width, #name, 0, &fixed_value, \
                                       value, value)); \
    } while (0)

#define bc_unsigned_subs(width, prob, enable_trace, name, subs, ...) \
    do { \
        uint32_t value; \
        CHECK(cbs_vp8_bool_decoder_read_unsigned( \
            ctx, bool_coding_rw, width, prob, #name, \
            SUBSCRIPTS(subs, __VA_ARGS__), &value, enable_trace)); \
        current->name = value; \
    } while (0)

#define bc_signed_subs(width, prob, name, subs, ...) \
    do { \
        int32_t value; \
        CHECK(cbs_vp8_bool_decoder_read_signed( \
            ctx, bool_coding_rw, width, prob, #name, \
            SUBSCRIPTS(subs, __VA_ARGS__), &value)); \
        current->name = value; \
    } while (0)

#include "cbs_vp8_syntax_template.c"

static int cbs_vp8_split_fragment(CodedBitstreamContext *ctx,
                                  CodedBitstreamFragment *frag, int header)
{
    int err;

    if (frag->data_size == 0)
        return AVERROR_INVALIDDATA;

    err = ff_cbs_append_unit_data(frag, 0, frag->data, frag->data_size,
                                  frag->data_ref);
    if (err < 0)
        return err;

    return 0;
}

static int cbs_vp8_read_unit(CodedBitstreamContext *ctx,
                             CodedBitstreamUnit *unit)
{
    VP8RawFrame *frame;
    GetBitContext gbc;
    CBSVP8BoolDecoder bool_decoder;
    int err, pos;

    err = ff_cbs_alloc_unit_content(ctx, unit);
    if (err < 0)
        return err;
    frame = unit->content;

    // Create GetBitContext for uncompressed header.
    err = init_get_bits8_le(&gbc, unit->data, unit->data_size);
    if (err < 0)
        return err;

    err = cbs_vp8_read_uncompressed_header(ctx, &gbc, frame);
    if (err < 0)
        return err;

    pos = get_bits_count(&gbc);
    av_assert0(pos % 8 == 0);

    // Create boolean decoder for compressed header.
    err = cbs_vp8_bool_decoder_init(&bool_decoder, &gbc);
    if (err < 0)
        return err;

    err = cbs_vp8_read_compressed_header(ctx, &bool_decoder, frame);
    if (err < 0)
        return err;

    pos = get_bits_count(&gbc);
    // Position may not be byte-aligned after compressed header; Round up byte
    // count for accurate data positioning.
    pos = (pos + 7) / 8;
    av_assert0(pos <= unit->data_size);

    frame->data_ref = av_buffer_ref(unit->data_ref);
    if (!frame->data_ref)
        return AVERROR(ENOMEM);

    frame->data = unit->data + pos;
    frame->data_size = unit->data_size - pos;

    return 0;
}

static int cbs_vp8_write_unit(CodedBitstreamContext *ctx,
                              CodedBitstreamUnit *unit, PutBitContext *pbc)
{
    return AVERROR_PATCHWELCOME;
}

static int cbs_vp8_assemble_fragment(CodedBitstreamContext *ctx,
                                     CodedBitstreamFragment *frag)
{
    return AVERROR_PATCHWELCOME;
}

static const CodedBitstreamUnitTypeDescriptor cbs_vp8_unit_types[] = {
    CBS_UNIT_TYPE_INTERNAL_REF(0, VP8RawFrame, data),
    CBS_UNIT_TYPE_END_OF_LIST,
};

const CodedBitstreamType ff_cbs_type_vp8 = {
    .codec_id          = AV_CODEC_ID_VP8,

    .priv_data_size    = 0,

    .unit_types        = cbs_vp8_unit_types,

    .split_fragment    = &cbs_vp8_split_fragment,
    .read_unit         = &cbs_vp8_read_unit,
    .write_unit        = &cbs_vp8_write_unit,

    .assemble_fragment = &cbs_vp8_assemble_fragment,
};
