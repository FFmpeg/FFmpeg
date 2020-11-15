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
#include "libavutil/opt.h"
#include "libavutil/pixfmt.h"

#include "cbs.h"
#include "cbs_internal.h"
#include "cbs_av1.h"
#include "internal.h"


static int cbs_av1_read_uvlc(CodedBitstreamContext *ctx, GetBitContext *gbc,
                             const char *name, uint32_t *write_to,
                             uint32_t range_min, uint32_t range_max)
{
    uint32_t zeroes, bits_value, value;
    int position;

    if (ctx->trace_enable)
        position = get_bits_count(gbc);

    zeroes = 0;
    while (1) {
        if (get_bits_left(gbc) < 1) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid uvlc code at "
                   "%s: bitstream ended.\n", name);
            return AVERROR_INVALIDDATA;
        }

        if (get_bits1(gbc))
            break;
        ++zeroes;
    }

    if (zeroes >= 32) {
        value = MAX_UINT_BITS(32);
    } else {
        if (get_bits_left(gbc) < zeroes) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid uvlc code at "
                   "%s: bitstream ended.\n", name);
            return AVERROR_INVALIDDATA;
        }

        bits_value = get_bits_long(gbc, zeroes);
        value = bits_value + (UINT32_C(1) << zeroes) - 1;
    }

    if (ctx->trace_enable) {
        char bits[65];
        int i, j, k;

        if (zeroes >= 32) {
            while (zeroes > 32) {
                k = FFMIN(zeroes - 32, 32);
                for (i = 0; i < k; i++)
                    bits[i] = '0';
                bits[i] = 0;
                ff_cbs_trace_syntax_element(ctx, position, name,
                                            NULL, bits, 0);
                zeroes -= k;
                position += k;
            }
        }

        for (i = 0; i < zeroes; i++)
            bits[i] = '0';
        bits[i++] = '1';

        if (zeroes < 32) {
            for (j = 0; j < zeroes; j++)
                bits[i++] = (bits_value >> (zeroes - j - 1) & 1) ? '1' : '0';
        }

        bits[i] = 0;
        ff_cbs_trace_syntax_element(ctx, position, name,
                                    NULL, bits, value);
    }

    if (value < range_min || value > range_max) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "%s out of range: "
               "%"PRIu32", but must be in [%"PRIu32",%"PRIu32"].\n",
               name, value, range_min, range_max);
        return AVERROR_INVALIDDATA;
    }

    *write_to = value;
    return 0;
}

static int cbs_av1_write_uvlc(CodedBitstreamContext *ctx, PutBitContext *pbc,
                              const char *name, uint32_t value,
                              uint32_t range_min, uint32_t range_max)
{
    uint32_t v;
    int position, zeroes;

    if (value < range_min || value > range_max) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "%s out of range: "
               "%"PRIu32", but must be in [%"PRIu32",%"PRIu32"].\n",
               name, value, range_min, range_max);
        return AVERROR_INVALIDDATA;
    }

    if (ctx->trace_enable)
        position = put_bits_count(pbc);

    zeroes = av_log2(value + 1);
    v = value - (1U << zeroes) + 1;
    put_bits(pbc, zeroes, 0);
    put_bits(pbc, 1, 1);
    put_bits(pbc, zeroes, v);

    if (ctx->trace_enable) {
        char bits[65];
        int i, j;
        i = 0;
        for (j = 0; j < zeroes; j++)
            bits[i++] = '0';
        bits[i++] = '1';
        for (j = 0; j < zeroes; j++)
            bits[i++] = (v >> (zeroes - j - 1) & 1) ? '1' : '0';
        bits[i++] = 0;
        ff_cbs_trace_syntax_element(ctx, position, name, NULL,
                                    bits, value);
    }

    return 0;
}

static int cbs_av1_read_leb128(CodedBitstreamContext *ctx, GetBitContext *gbc,
                               const char *name, uint64_t *write_to)
{
    uint64_t value;
    int position, err, i;

    if (ctx->trace_enable)
        position = get_bits_count(gbc);

    value = 0;
    for (i = 0; i < 8; i++) {
        int subscript[2] = { 1, i };
        uint32_t byte;
        err = ff_cbs_read_unsigned(ctx, gbc, 8, "leb128_byte[i]", subscript,
                                   &byte, 0x00, 0xff);
        if (err < 0)
            return err;

        value |= (uint64_t)(byte & 0x7f) << (i * 7);
        if (!(byte & 0x80))
            break;
    }

    if (value > UINT32_MAX)
        return AVERROR_INVALIDDATA;

    if (ctx->trace_enable)
        ff_cbs_trace_syntax_element(ctx, position, name, NULL, "", value);

    *write_to = value;
    return 0;
}

static int cbs_av1_write_leb128(CodedBitstreamContext *ctx, PutBitContext *pbc,
                                const char *name, uint64_t value)
{
    int position, err, len, i;
    uint8_t byte;

    len = (av_log2(value) + 7) / 7;

    if (ctx->trace_enable)
        position = put_bits_count(pbc);

    for (i = 0; i < len; i++) {
        int subscript[2] = { 1, i };

        byte = value >> (7 * i) & 0x7f;
        if (i < len - 1)
            byte |= 0x80;

        err = ff_cbs_write_unsigned(ctx, pbc, 8, "leb128_byte[i]", subscript,
                                    byte, 0x00, 0xff);
        if (err < 0)
            return err;
    }

    if (ctx->trace_enable)
        ff_cbs_trace_syntax_element(ctx, position, name, NULL, "", value);

    return 0;
}

static int cbs_av1_read_ns(CodedBitstreamContext *ctx, GetBitContext *gbc,
                           uint32_t n, const char *name,
                           const int *subscripts, uint32_t *write_to)
{
    uint32_t m, v, extra_bit, value;
    int position, w;

    av_assert0(n > 0);

    if (ctx->trace_enable)
        position = get_bits_count(gbc);

    w = av_log2(n) + 1;
    m = (1 << w) - n;

    if (get_bits_left(gbc) < w) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid non-symmetric value at "
               "%s: bitstream ended.\n", name);
        return AVERROR_INVALIDDATA;
    }

    if (w - 1 > 0)
        v = get_bits(gbc, w - 1);
    else
        v = 0;

    if (v < m) {
        value = v;
    } else {
        extra_bit = get_bits1(gbc);
        value = (v << 1) - m + extra_bit;
    }

    if (ctx->trace_enable) {
        char bits[33];
        int i;
        for (i = 0; i < w - 1; i++)
            bits[i] = (v >> i & 1) ? '1' : '0';
        if (v >= m)
            bits[i++] = extra_bit ? '1' : '0';
        bits[i] = 0;

        ff_cbs_trace_syntax_element(ctx, position,
                                    name, subscripts, bits, value);
    }

    *write_to = value;
    return 0;
}

static int cbs_av1_write_ns(CodedBitstreamContext *ctx, PutBitContext *pbc,
                            uint32_t n, const char *name,
                            const int *subscripts, uint32_t value)
{
    uint32_t w, m, v, extra_bit;
    int position;

    if (value > n) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "%s out of range: "
               "%"PRIu32", but must be in [0,%"PRIu32"].\n",
               name, value, n);
        return AVERROR_INVALIDDATA;
    }

    if (ctx->trace_enable)
        position = put_bits_count(pbc);

    w = av_log2(n) + 1;
    m = (1 << w) - n;

    if (put_bits_left(pbc) < w)
        return AVERROR(ENOSPC);

    if (value < m) {
        v = value;
        put_bits(pbc, w - 1, v);
    } else {
        v = m + ((value - m) >> 1);
        extra_bit = (value - m) & 1;
        put_bits(pbc, w - 1, v);
        put_bits(pbc, 1, extra_bit);
    }

    if (ctx->trace_enable) {
        char bits[33];
        int i;
        for (i = 0; i < w - 1; i++)
            bits[i] = (v >> i & 1) ? '1' : '0';
        if (value >= m)
            bits[i++] = extra_bit ? '1' : '0';
        bits[i] = 0;

        ff_cbs_trace_syntax_element(ctx, position,
                                    name, subscripts, bits, value);
    }

    return 0;
}

static int cbs_av1_read_increment(CodedBitstreamContext *ctx, GetBitContext *gbc,
                                  uint32_t range_min, uint32_t range_max,
                                  const char *name, uint32_t *write_to)
{
    uint32_t value;
    int position, i;
    char bits[33];

    av_assert0(range_min <= range_max && range_max - range_min < sizeof(bits) - 1);
    if (ctx->trace_enable)
        position = get_bits_count(gbc);

    for (i = 0, value = range_min; value < range_max;) {
        if (get_bits_left(gbc) < 1) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid increment value at "
                   "%s: bitstream ended.\n", name);
            return AVERROR_INVALIDDATA;
        }
        if (get_bits1(gbc)) {
            bits[i++] = '1';
            ++value;
        } else {
            bits[i++] = '0';
            break;
        }
    }

    if (ctx->trace_enable) {
        bits[i] = 0;
        ff_cbs_trace_syntax_element(ctx, position,
                                    name, NULL, bits, value);
    }

    *write_to = value;
    return 0;
}

static int cbs_av1_write_increment(CodedBitstreamContext *ctx, PutBitContext *pbc,
                                   uint32_t range_min, uint32_t range_max,
                                   const char *name, uint32_t value)
{
    int len;

    av_assert0(range_min <= range_max && range_max - range_min < 32);
    if (value < range_min || value > range_max) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "%s out of range: "
               "%"PRIu32", but must be in [%"PRIu32",%"PRIu32"].\n",
               name, value, range_min, range_max);
        return AVERROR_INVALIDDATA;
    }

    if (value == range_max)
        len = range_max - range_min;
    else
        len = value - range_min + 1;
    if (put_bits_left(pbc) < len)
        return AVERROR(ENOSPC);

    if (ctx->trace_enable) {
        char bits[33];
        int i;
        for (i = 0; i < len; i++) {
            if (range_min + i == value)
                bits[i] = '0';
            else
                bits[i] = '1';
        }
        bits[i] = 0;
        ff_cbs_trace_syntax_element(ctx, put_bits_count(pbc),
                                    name, NULL, bits, value);
    }

    if (len > 0)
        put_bits(pbc, len, (1 << len) - 1 - (value != range_max));

    return 0;
}

static int cbs_av1_read_subexp(CodedBitstreamContext *ctx, GetBitContext *gbc,
                               uint32_t range_max, const char *name,
                               const int *subscripts, uint32_t *write_to)
{
    uint32_t value;
    int position, err;
    uint32_t max_len, len, range_offset, range_bits;

    if (ctx->trace_enable)
        position = get_bits_count(gbc);

    av_assert0(range_max > 0);
    max_len = av_log2(range_max - 1) - 3;

    err = cbs_av1_read_increment(ctx, gbc, 0, max_len,
                                 "subexp_more_bits", &len);
    if (err < 0)
        return err;

    if (len) {
        range_bits   = 2 + len;
        range_offset = 1 << range_bits;
    } else {
        range_bits   = 3;
        range_offset = 0;
    }

    if (len < max_len) {
        err = ff_cbs_read_unsigned(ctx, gbc, range_bits,
                                   "subexp_bits", NULL, &value,
                                   0, MAX_UINT_BITS(range_bits));
        if (err < 0)
            return err;

    } else {
        err = cbs_av1_read_ns(ctx, gbc, range_max - range_offset,
                              "subexp_final_bits", NULL, &value);
        if (err < 0)
            return err;
    }
    value += range_offset;

    if (ctx->trace_enable)
        ff_cbs_trace_syntax_element(ctx, position,
                                    name, subscripts, "", value);

    *write_to = value;
    return err;
}

static int cbs_av1_write_subexp(CodedBitstreamContext *ctx, PutBitContext *pbc,
                                uint32_t range_max, const char *name,
                                const int *subscripts, uint32_t value)
{
    int position, err;
    uint32_t max_len, len, range_offset, range_bits;

    if (value > range_max) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "%s out of range: "
               "%"PRIu32", but must be in [0,%"PRIu32"].\n",
               name, value, range_max);
        return AVERROR_INVALIDDATA;
    }

    if (ctx->trace_enable)
        position = put_bits_count(pbc);

    av_assert0(range_max > 0);
    max_len = av_log2(range_max - 1) - 3;

    if (value < 8) {
        range_bits   = 3;
        range_offset = 0;
        len = 0;
    } else {
        range_bits = av_log2(value);
        len = range_bits - 2;
        if (len > max_len) {
            // The top bin is combined with the one below it.
            av_assert0(len == max_len + 1);
            --range_bits;
            len = max_len;
        }
        range_offset = 1 << range_bits;
    }

    err = cbs_av1_write_increment(ctx, pbc, 0, max_len,
                                  "subexp_more_bits", len);
    if (err < 0)
        return err;

    if (len < max_len) {
        err = ff_cbs_write_unsigned(ctx, pbc, range_bits,
                                    "subexp_bits", NULL,
                                    value - range_offset,
                                    0, MAX_UINT_BITS(range_bits));
        if (err < 0)
            return err;

    } else {
        err = cbs_av1_write_ns(ctx, pbc, range_max - range_offset,
                               "subexp_final_bits", NULL,
                               value - range_offset);
        if (err < 0)
            return err;
    }

    if (ctx->trace_enable)
        ff_cbs_trace_syntax_element(ctx, position,
                                    name, subscripts, "", value);

    return err;
}


static int cbs_av1_tile_log2(int blksize, int target)
{
    int k;
    for (k = 0; (blksize << k) < target; k++);
    return k;
}

static int cbs_av1_get_relative_dist(const AV1RawSequenceHeader *seq,
                                     unsigned int a, unsigned int b)
{
    unsigned int diff, m;
    if (!seq->enable_order_hint)
        return 0;
    diff = a - b;
    m = 1 << seq->order_hint_bits_minus_1;
    diff = (diff & (m - 1)) - (diff & m);
    return diff;
}

static size_t cbs_av1_get_payload_bytes_left(GetBitContext *gbc)
{
    GetBitContext tmp = *gbc;
    size_t size = 0;
    for (int i = 0; get_bits_left(&tmp) >= 8; i++) {
        if (get_bits(&tmp, 8))
            size = i;
    }
    return size;
}


#define HEADER(name) do { \
        ff_cbs_trace_header(ctx, name); \
    } while (0)

#define CHECK(call) do { \
        err = (call); \
        if (err < 0) \
            return err; \
    } while (0)

#define FUNC_NAME(rw, codec, name) cbs_ ## codec ## _ ## rw ## _ ## name
#define FUNC_AV1(rw, name) FUNC_NAME(rw, av1, name)
#define FUNC(name) FUNC_AV1(READWRITE, name)

#define SUBSCRIPTS(subs, ...) (subs > 0 ? ((int[subs + 1]){ subs, __VA_ARGS__ }) : NULL)

#define fb(width, name) \
        xf(width, name, current->name, 0, MAX_UINT_BITS(width), 0, )
#define fc(width, name, range_min, range_max) \
        xf(width, name, current->name, range_min, range_max, 0, )
#define flag(name) fb(1, name)
#define su(width, name) \
        xsu(width, name, current->name, 0, )

#define fbs(width, name, subs, ...) \
        xf(width, name, current->name, 0, MAX_UINT_BITS(width), subs, __VA_ARGS__)
#define fcs(width, name, range_min, range_max, subs, ...) \
        xf(width, name, current->name, range_min, range_max, subs, __VA_ARGS__)
#define flags(name, subs, ...) \
        xf(1, name, current->name, 0, 1, subs, __VA_ARGS__)
#define sus(width, name, subs, ...) \
        xsu(width, name, current->name, subs, __VA_ARGS__)

#define fixed(width, name, value) do { \
        av_unused uint32_t fixed_value = value; \
        xf(width, name, fixed_value, value, value, 0, ); \
    } while (0)


#define READ
#define READWRITE read
#define RWContext GetBitContext

#define xf(width, name, var, range_min, range_max, subs, ...) do { \
        uint32_t value; \
        CHECK(ff_cbs_read_unsigned(ctx, rw, width, #name, \
                                   SUBSCRIPTS(subs, __VA_ARGS__), \
                                   &value, range_min, range_max)); \
        var = value; \
    } while (0)

#define xsu(width, name, var, subs, ...) do { \
        int32_t value; \
        CHECK(ff_cbs_read_signed(ctx, rw, width, #name, \
                                 SUBSCRIPTS(subs, __VA_ARGS__), &value, \
                                 MIN_INT_BITS(width), \
                                 MAX_INT_BITS(width))); \
        var = value; \
    } while (0)

#define uvlc(name, range_min, range_max) do { \
        uint32_t value; \
        CHECK(cbs_av1_read_uvlc(ctx, rw, #name, \
                                &value, range_min, range_max)); \
        current->name = value; \
    } while (0)

#define ns(max_value, name, subs, ...) do { \
        uint32_t value; \
        CHECK(cbs_av1_read_ns(ctx, rw, max_value, #name, \
                              SUBSCRIPTS(subs, __VA_ARGS__), &value)); \
        current->name = value; \
    } while (0)

#define increment(name, min, max) do { \
        uint32_t value; \
        CHECK(cbs_av1_read_increment(ctx, rw, min, max, #name, &value)); \
        current->name = value; \
    } while (0)

#define subexp(name, max, subs, ...) do { \
        uint32_t value; \
        CHECK(cbs_av1_read_subexp(ctx, rw, max, #name, \
                                  SUBSCRIPTS(subs, __VA_ARGS__), &value)); \
        current->name = value; \
    } while (0)

#define delta_q(name) do { \
        uint8_t delta_coded; \
        int8_t delta_q; \
        xf(1, name.delta_coded, delta_coded, 0, 1, 0, ); \
        if (delta_coded) \
            xsu(1 + 6, name.delta_q, delta_q, 0, ); \
        else \
            delta_q = 0; \
        current->name = delta_q; \
    } while (0)

#define leb128(name) do { \
        uint64_t value; \
        CHECK(cbs_av1_read_leb128(ctx, rw, #name, &value)); \
        current->name = value; \
    } while (0)

#define infer(name, value) do { \
        current->name = value; \
    } while (0)

#define byte_alignment(rw) (get_bits_count(rw) % 8)

#include "cbs_av1_syntax_template.c"

#undef READ
#undef READWRITE
#undef RWContext
#undef xf
#undef xsu
#undef uvlc
#undef ns
#undef increment
#undef subexp
#undef delta_q
#undef leb128
#undef infer
#undef byte_alignment


#define WRITE
#define READWRITE write
#define RWContext PutBitContext

#define xf(width, name, var, range_min, range_max, subs, ...) do { \
        CHECK(ff_cbs_write_unsigned(ctx, rw, width, #name, \
                                    SUBSCRIPTS(subs, __VA_ARGS__), \
                                    var, range_min, range_max)); \
    } while (0)

#define xsu(width, name, var, subs, ...) do { \
        CHECK(ff_cbs_write_signed(ctx, rw, width, #name, \
                                  SUBSCRIPTS(subs, __VA_ARGS__), var, \
                                  MIN_INT_BITS(width), \
                                  MAX_INT_BITS(width))); \
    } while (0)

#define uvlc(name, range_min, range_max) do { \
        CHECK(cbs_av1_write_uvlc(ctx, rw, #name, current->name, \
                                 range_min, range_max)); \
    } while (0)

#define ns(max_value, name, subs, ...) do { \
        CHECK(cbs_av1_write_ns(ctx, rw, max_value, #name, \
                               SUBSCRIPTS(subs, __VA_ARGS__), \
                               current->name)); \
    } while (0)

#define increment(name, min, max) do { \
        CHECK(cbs_av1_write_increment(ctx, rw, min, max, #name, \
                                      current->name)); \
    } while (0)

#define subexp(name, max, subs, ...) do { \
        CHECK(cbs_av1_write_subexp(ctx, rw, max, #name, \
                                   SUBSCRIPTS(subs, __VA_ARGS__), \
                                   current->name)); \
    } while (0)

#define delta_q(name) do { \
        xf(1, name.delta_coded, current->name != 0, 0, 1, 0, ); \
        if (current->name) \
            xsu(1 + 6, name.delta_q, current->name, 0, ); \
    } while (0)

#define leb128(name) do { \
        CHECK(cbs_av1_write_leb128(ctx, rw, #name, current->name)); \
    } while (0)

#define infer(name, value) do { \
        if (current->name != (value)) { \
            av_log(ctx->log_ctx, AV_LOG_ERROR, \
                   "%s does not match inferred value: " \
                   "%"PRId64", but should be %"PRId64".\n", \
                   #name, (int64_t)current->name, (int64_t)(value)); \
            return AVERROR_INVALIDDATA; \
        } \
    } while (0)

#define byte_alignment(rw) (put_bits_count(rw) % 8)

#include "cbs_av1_syntax_template.c"

#undef WRITE
#undef READWRITE
#undef RWContext
#undef xf
#undef xsu
#undef uvlc
#undef ns
#undef increment
#undef subexp
#undef delta_q
#undef leb128
#undef infer
#undef byte_alignment


static int cbs_av1_split_fragment(CodedBitstreamContext *ctx,
                                  CodedBitstreamFragment *frag,
                                  int header)
{
    GetBitContext gbc;
    uint8_t *data;
    size_t size;
    uint64_t obu_length;
    int pos, err, trace;

    // Don't include this parsing in trace output.
    trace = ctx->trace_enable;
    ctx->trace_enable = 0;

    data = frag->data;
    size = frag->data_size;

    if (INT_MAX / 8 < size) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid fragment: "
               "too large (%"SIZE_SPECIFIER" bytes).\n", size);
        err = AVERROR_INVALIDDATA;
        goto fail;
    }

    if (header && size && data[0] & 0x80) {
        // first bit is nonzero, the extradata does not consist purely of
        // OBUs. Expect MP4/Matroska AV1CodecConfigurationRecord
        int config_record_version = data[0] & 0x7f;

        if (config_record_version != 1) {
            av_log(ctx->log_ctx, AV_LOG_ERROR,
                   "Unknown version %d of AV1CodecConfigurationRecord "
                   "found!\n",
                   config_record_version);
            err = AVERROR_INVALIDDATA;
            goto fail;
        }

        if (size <= 4) {
            if (size < 4) {
                av_log(ctx->log_ctx, AV_LOG_WARNING,
                       "Undersized AV1CodecConfigurationRecord v%d found!\n",
                       config_record_version);
                err = AVERROR_INVALIDDATA;
                goto fail;
            }

            goto success;
        }

        // In AV1CodecConfigurationRecord v1, actual OBUs start after
        // four bytes. Thus set the offset as required for properly
        // parsing them.
        data += 4;
        size -= 4;
    }

    while (size > 0) {
        AV1RawOBUHeader header;
        uint64_t obu_size;

        init_get_bits(&gbc, data, 8 * size);

        err = cbs_av1_read_obu_header(ctx, &gbc, &header);
        if (err < 0)
            goto fail;

        if (header.obu_has_size_field) {
            if (get_bits_left(&gbc) < 8) {
                av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid OBU: fragment "
                       "too short (%"SIZE_SPECIFIER" bytes).\n", size);
                err = AVERROR_INVALIDDATA;
                goto fail;
            }
            err = cbs_av1_read_leb128(ctx, &gbc, "obu_size", &obu_size);
            if (err < 0)
                goto fail;
        } else
            obu_size = size - 1 - header.obu_extension_flag;

        pos = get_bits_count(&gbc);
        av_assert0(pos % 8 == 0 && pos / 8 <= size);

        obu_length = pos / 8 + obu_size;

        if (size < obu_length) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid OBU length: "
                   "%"PRIu64", but only %"SIZE_SPECIFIER" bytes remaining in fragment.\n",
                   obu_length, size);
            err = AVERROR_INVALIDDATA;
            goto fail;
        }

        err = ff_cbs_insert_unit_data(frag, -1, header.obu_type,
                                      data, obu_length, frag->data_ref);
        if (err < 0)
            goto fail;

        data += obu_length;
        size -= obu_length;
    }

success:
    err = 0;
fail:
    ctx->trace_enable = trace;
    return err;
}

static int cbs_av1_ref_tile_data(CodedBitstreamContext *ctx,
                                 CodedBitstreamUnit *unit,
                                 GetBitContext *gbc,
                                 AV1RawTileData *td)
{
    int pos;

    pos = get_bits_count(gbc);
    if (pos >= 8 * unit->data_size) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Bitstream ended before "
               "any data in tile group (%d bits read).\n", pos);
        return AVERROR_INVALIDDATA;
    }
    // Must be byte-aligned at this point.
    av_assert0(pos % 8 == 0);

    td->data_ref = av_buffer_ref(unit->data_ref);
    if (!td->data_ref)
        return AVERROR(ENOMEM);

    td->data      = unit->data      + pos / 8;
    td->data_size = unit->data_size - pos / 8;

    return 0;
}

static int cbs_av1_read_unit(CodedBitstreamContext *ctx,
                             CodedBitstreamUnit *unit)
{
    CodedBitstreamAV1Context *priv = ctx->priv_data;
    AV1RawOBU *obu;
    GetBitContext gbc;
    int err, start_pos, end_pos;

    err = ff_cbs_alloc_unit_content2(ctx, unit);
    if (err < 0)
        return err;
    obu = unit->content;

    err = init_get_bits(&gbc, unit->data, 8 * unit->data_size);
    if (err < 0)
        return err;

    err = cbs_av1_read_obu_header(ctx, &gbc, &obu->header);
    if (err < 0)
        return err;
    av_assert0(obu->header.obu_type == unit->type);

    if (obu->header.obu_has_size_field) {
        uint64_t obu_size;
        err = cbs_av1_read_leb128(ctx, &gbc, "obu_size", &obu_size);
        if (err < 0)
            return err;
        obu->obu_size = obu_size;
    } else {
        if (unit->data_size < 1 + obu->header.obu_extension_flag) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid OBU length: "
                   "unit too short (%"SIZE_SPECIFIER").\n", unit->data_size);
            return AVERROR_INVALIDDATA;
        }
        obu->obu_size = unit->data_size - 1 - obu->header.obu_extension_flag;
    }

    start_pos = get_bits_count(&gbc);

    if (obu->header.obu_extension_flag) {
        if (obu->header.obu_type != AV1_OBU_SEQUENCE_HEADER &&
            obu->header.obu_type != AV1_OBU_TEMPORAL_DELIMITER &&
            priv->operating_point_idc) {
            int in_temporal_layer =
                (priv->operating_point_idc >>  priv->temporal_id    ) & 1;
            int in_spatial_layer  =
                (priv->operating_point_idc >> (priv->spatial_id + 8)) & 1;
            if (!in_temporal_layer || !in_spatial_layer) {
                return AVERROR(EAGAIN); // drop_obu()
            }
        }
    }

    switch (obu->header.obu_type) {
    case AV1_OBU_SEQUENCE_HEADER:
        {
            err = cbs_av1_read_sequence_header_obu(ctx, &gbc,
                                                   &obu->obu.sequence_header);
            if (err < 0)
                return err;

            if (priv->operating_point >= 0) {
                AV1RawSequenceHeader *sequence_header = &obu->obu.sequence_header;

                if (priv->operating_point > sequence_header->operating_points_cnt_minus_1) {
                    av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid Operating Point %d requested. "
                                                       "Must not be higher than %u.\n",
                           priv->operating_point, sequence_header->operating_points_cnt_minus_1);
                    return AVERROR(EINVAL);
                }
                priv->operating_point_idc = sequence_header->operating_point_idc[priv->operating_point];
            }

            av_buffer_unref(&priv->sequence_header_ref);
            priv->sequence_header = NULL;

            priv->sequence_header_ref = av_buffer_ref(unit->content_ref);
            if (!priv->sequence_header_ref)
                return AVERROR(ENOMEM);
            priv->sequence_header = &obu->obu.sequence_header;
        }
        break;
    case AV1_OBU_TEMPORAL_DELIMITER:
        {
            err = cbs_av1_read_temporal_delimiter_obu(ctx, &gbc);
            if (err < 0)
                return err;
        }
        break;
    case AV1_OBU_FRAME_HEADER:
    case AV1_OBU_REDUNDANT_FRAME_HEADER:
        {
            err = cbs_av1_read_frame_header_obu(ctx, &gbc,
                                                &obu->obu.frame_header,
                                                obu->header.obu_type ==
                                                AV1_OBU_REDUNDANT_FRAME_HEADER,
                                                unit->data_ref);
            if (err < 0)
                return err;
        }
        break;
    case AV1_OBU_TILE_GROUP:
        {
            err = cbs_av1_read_tile_group_obu(ctx, &gbc,
                                              &obu->obu.tile_group);
            if (err < 0)
                return err;

            err = cbs_av1_ref_tile_data(ctx, unit, &gbc,
                                        &obu->obu.tile_group.tile_data);
            if (err < 0)
                return err;
        }
        break;
    case AV1_OBU_FRAME:
        {
            err = cbs_av1_read_frame_obu(ctx, &gbc, &obu->obu.frame,
                                         unit->data_ref);
            if (err < 0)
                return err;

            err = cbs_av1_ref_tile_data(ctx, unit, &gbc,
                                        &obu->obu.frame.tile_group.tile_data);
            if (err < 0)
                return err;
        }
        break;
    case AV1_OBU_TILE_LIST:
        {
            err = cbs_av1_read_tile_list_obu(ctx, &gbc,
                                             &obu->obu.tile_list);
            if (err < 0)
                return err;

            err = cbs_av1_ref_tile_data(ctx, unit, &gbc,
                                        &obu->obu.tile_list.tile_data);
            if (err < 0)
                return err;
        }
        break;
    case AV1_OBU_METADATA:
        {
            err = cbs_av1_read_metadata_obu(ctx, &gbc, &obu->obu.metadata);
            if (err < 0)
                return err;
        }
        break;
    case AV1_OBU_PADDING:
        {
            err = cbs_av1_read_padding_obu(ctx, &gbc, &obu->obu.padding);
            if (err < 0)
                return err;
        }
        break;
    default:
        return AVERROR(ENOSYS);
    }

    end_pos = get_bits_count(&gbc);
    av_assert0(end_pos <= unit->data_size * 8);

    if (obu->obu_size > 0 &&
        obu->header.obu_type != AV1_OBU_TILE_GROUP &&
        obu->header.obu_type != AV1_OBU_TILE_LIST &&
        obu->header.obu_type != AV1_OBU_FRAME) {
        int nb_bits = obu->obu_size * 8 + start_pos - end_pos;

        if (nb_bits <= 0)
            return AVERROR_INVALIDDATA;

        err = cbs_av1_read_trailing_bits(ctx, &gbc, nb_bits);
        if (err < 0)
            return err;
    }

    return 0;
}

static int cbs_av1_write_obu(CodedBitstreamContext *ctx,
                             CodedBitstreamUnit *unit,
                             PutBitContext *pbc)
{
    CodedBitstreamAV1Context *priv = ctx->priv_data;
    AV1RawOBU *obu = unit->content;
    PutBitContext pbc_tmp;
    AV1RawTileData *td;
    size_t header_size;
    int err, start_pos, end_pos, data_pos;

    // OBUs in the normal bitstream format must contain a size field
    // in every OBU (in annex B it is optional, but we don't support
    // writing that).
    obu->header.obu_has_size_field = 1;

    err = cbs_av1_write_obu_header(ctx, pbc, &obu->header);
    if (err < 0)
        return err;

    if (obu->header.obu_has_size_field) {
        pbc_tmp = *pbc;
        // Add space for the size field to fill later.
        put_bits32(pbc, 0);
        put_bits32(pbc, 0);
    }

    td = NULL;
    start_pos = put_bits_count(pbc);

    switch (obu->header.obu_type) {
    case AV1_OBU_SEQUENCE_HEADER:
        {
            err = cbs_av1_write_sequence_header_obu(ctx, pbc,
                                                    &obu->obu.sequence_header);
            if (err < 0)
                return err;

            av_buffer_unref(&priv->sequence_header_ref);
            priv->sequence_header = NULL;

            err = ff_cbs_make_unit_refcounted(ctx, unit);
            if (err < 0)
                return err;

            priv->sequence_header_ref = av_buffer_ref(unit->content_ref);
            if (!priv->sequence_header_ref)
                return AVERROR(ENOMEM);
            priv->sequence_header = &obu->obu.sequence_header;
        }
        break;
    case AV1_OBU_TEMPORAL_DELIMITER:
        {
            err = cbs_av1_write_temporal_delimiter_obu(ctx, pbc);
            if (err < 0)
                return err;
        }
        break;
    case AV1_OBU_FRAME_HEADER:
    case AV1_OBU_REDUNDANT_FRAME_HEADER:
        {
            err = cbs_av1_write_frame_header_obu(ctx, pbc,
                                                 &obu->obu.frame_header,
                                                 obu->header.obu_type ==
                                                 AV1_OBU_REDUNDANT_FRAME_HEADER,
                                                 NULL);
            if (err < 0)
                return err;
        }
        break;
    case AV1_OBU_TILE_GROUP:
        {
            err = cbs_av1_write_tile_group_obu(ctx, pbc,
                                               &obu->obu.tile_group);
            if (err < 0)
                return err;

            td = &obu->obu.tile_group.tile_data;
        }
        break;
    case AV1_OBU_FRAME:
        {
            err = cbs_av1_write_frame_obu(ctx, pbc, &obu->obu.frame, NULL);
            if (err < 0)
                return err;

            td = &obu->obu.frame.tile_group.tile_data;
        }
        break;
    case AV1_OBU_TILE_LIST:
        {
            err = cbs_av1_write_tile_list_obu(ctx, pbc, &obu->obu.tile_list);
            if (err < 0)
                return err;

            td = &obu->obu.tile_list.tile_data;
        }
        break;
    case AV1_OBU_METADATA:
        {
            err = cbs_av1_write_metadata_obu(ctx, pbc, &obu->obu.metadata);
            if (err < 0)
                return err;
        }
        break;
    case AV1_OBU_PADDING:
        {
            err = cbs_av1_write_padding_obu(ctx, pbc, &obu->obu.padding);
            if (err < 0)
                return err;
        }
        break;
    default:
        return AVERROR(ENOSYS);
    }

    end_pos = put_bits_count(pbc);
    header_size = (end_pos - start_pos + 7) / 8;
    if (td) {
        obu->obu_size = header_size + td->data_size;
    } else if (header_size > 0) {
        // Add trailing bits and recalculate.
        err = cbs_av1_write_trailing_bits(ctx, pbc, 8 - end_pos % 8);
        if (err < 0)
            return err;
        end_pos = put_bits_count(pbc);
        obu->obu_size = header_size = (end_pos - start_pos + 7) / 8;
    } else {
        // Empty OBU.
        obu->obu_size = 0;
    }

    end_pos = put_bits_count(pbc);
    // Must now be byte-aligned.
    av_assert0(end_pos % 8 == 0);
    flush_put_bits(pbc);
    start_pos /= 8;
    end_pos   /= 8;

    *pbc = pbc_tmp;
    err = cbs_av1_write_leb128(ctx, pbc, "obu_size", obu->obu_size);
    if (err < 0)
        return err;

    data_pos = put_bits_count(pbc) / 8;
    flush_put_bits(pbc);
    av_assert0(data_pos <= start_pos);

    if (8 * obu->obu_size > put_bits_left(pbc))
        return AVERROR(ENOSPC);

    if (obu->obu_size > 0) {
        memmove(pbc->buf + data_pos,
                pbc->buf + start_pos, header_size);
        skip_put_bytes(pbc, header_size);

        if (td) {
            memcpy(pbc->buf + data_pos + header_size,
                   td->data, td->data_size);
            skip_put_bytes(pbc, td->data_size);
        }
    }

    // OBU data must be byte-aligned.
    av_assert0(put_bits_count(pbc) % 8 == 0);

    return 0;
}

static int cbs_av1_assemble_fragment(CodedBitstreamContext *ctx,
                                     CodedBitstreamFragment *frag)
{
    size_t size, pos;
    int i;

    size = 0;
    for (i = 0; i < frag->nb_units; i++)
        size += frag->units[i].data_size;

    frag->data_ref = av_buffer_alloc(size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!frag->data_ref)
        return AVERROR(ENOMEM);
    frag->data = frag->data_ref->data;
    memset(frag->data + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    pos = 0;
    for (i = 0; i < frag->nb_units; i++) {
        memcpy(frag->data + pos, frag->units[i].data,
               frag->units[i].data_size);
        pos += frag->units[i].data_size;
    }
    av_assert0(pos == size);
    frag->data_size = size;

    return 0;
}

static void cbs_av1_flush(CodedBitstreamContext *ctx)
{
    CodedBitstreamAV1Context *priv = ctx->priv_data;

    av_buffer_unref(&priv->frame_header_ref);
    priv->sequence_header = NULL;
    priv->frame_header = NULL;

    memset(priv->ref, 0, sizeof(priv->ref));
    priv->operating_point_idc = 0;
    priv->seen_frame_header = 0;
    priv->tile_num = 0;
}

static void cbs_av1_close(CodedBitstreamContext *ctx)
{
    CodedBitstreamAV1Context *priv = ctx->priv_data;

    av_buffer_unref(&priv->sequence_header_ref);
    av_buffer_unref(&priv->frame_header_ref);
}

static void cbs_av1_free_metadata(void *unit, uint8_t *content)
{
    AV1RawOBU *obu = (AV1RawOBU*)content;
    AV1RawMetadata *md;

    av_assert0(obu->header.obu_type == AV1_OBU_METADATA);
    md = &obu->obu.metadata;

    switch (md->metadata_type) {
    case AV1_METADATA_TYPE_ITUT_T35:
        av_buffer_unref(&md->metadata.itut_t35.payload_ref);
        break;
    }
    av_free(content);
}

static const CodedBitstreamUnitTypeDescriptor cbs_av1_unit_types[] = {
    CBS_UNIT_TYPE_POD(AV1_OBU_SEQUENCE_HEADER,        AV1RawOBU),
    CBS_UNIT_TYPE_POD(AV1_OBU_TEMPORAL_DELIMITER,     AV1RawOBU),
    CBS_UNIT_TYPE_POD(AV1_OBU_FRAME_HEADER,           AV1RawOBU),
    CBS_UNIT_TYPE_POD(AV1_OBU_REDUNDANT_FRAME_HEADER, AV1RawOBU),

    CBS_UNIT_TYPE_INTERNAL_REF(AV1_OBU_TILE_GROUP, AV1RawOBU,
                               obu.tile_group.tile_data.data),
    CBS_UNIT_TYPE_INTERNAL_REF(AV1_OBU_FRAME,      AV1RawOBU,
                               obu.frame.tile_group.tile_data.data),
    CBS_UNIT_TYPE_INTERNAL_REF(AV1_OBU_TILE_LIST,  AV1RawOBU,
                               obu.tile_list.tile_data.data),
    CBS_UNIT_TYPE_INTERNAL_REF(AV1_OBU_PADDING,    AV1RawOBU,
                               obu.padding.payload),

    CBS_UNIT_TYPE_COMPLEX(AV1_OBU_METADATA, AV1RawOBU,
                          &cbs_av1_free_metadata),

    CBS_UNIT_TYPE_END_OF_LIST
};

#define OFFSET(x) offsetof(CodedBitstreamAV1Context, x)
static const AVOption cbs_av1_options[] = {
    { "operating_point",  "Set operating point to select layers to parse from a scalable bitstream",
                          OFFSET(operating_point), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, AV1_MAX_OPERATING_POINTS - 1, 0 },
    { NULL }
};

static const AVClass cbs_av1_class = {
    .class_name = "cbs_av1",
    .item_name  = av_default_item_name,
    .option     = cbs_av1_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const CodedBitstreamType ff_cbs_type_av1 = {
    .codec_id          = AV_CODEC_ID_AV1,

    .priv_class        = &cbs_av1_class,
    .priv_data_size    = sizeof(CodedBitstreamAV1Context),

    .unit_types        = cbs_av1_unit_types,

    .split_fragment    = &cbs_av1_split_fragment,
    .read_unit         = &cbs_av1_read_unit,
    .write_unit        = &cbs_av1_write_obu,
    .assemble_fragment = &cbs_av1_assemble_fragment,

    .flush             = &cbs_av1_flush,
    .close             = &cbs_av1_close,
};
