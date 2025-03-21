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
#include "defs.h"
#include "libavutil/refstruct.h"


#if CBS_READ
static int cbs_av1_read_uvlc(CodedBitstreamContext *ctx, GetBitContext *gbc,
                             const char *name, uint32_t *write_to,
                             uint32_t range_min, uint32_t range_max)
{
    uint32_t zeroes, bits_value, value;

    CBS_TRACE_READ_START();

    zeroes = 0;
    while (zeroes < 32) {
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
        // The spec allows at least thirty-two zero bits followed by a
        // one to mean 2^32-1, with no constraint on the number of
        // zeroes.  The libaom reference decoder does not match this,
        // instead reading thirty-two zeroes but not the following one
        // to mean 2^32-1.  These two interpretations are incompatible
        // and other implementations may follow one or the other.
        // Therefore we reject thirty-two zeroes because the intended
        // behaviour is not clear.
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Thirty-two zero bits in "
               "%s uvlc code: considered invalid due to conflicting "
               "standard and reference decoder behaviour.\n", name);
        return AVERROR_INVALIDDATA;
    } else {
        if (get_bits_left(gbc) < zeroes) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid uvlc code at "
                   "%s: bitstream ended.\n", name);
            return AVERROR_INVALIDDATA;
        }

        bits_value = get_bits_long(gbc, zeroes);
        value = bits_value + (UINT32_C(1) << zeroes) - 1;
    }

    CBS_TRACE_READ_END_NO_SUBSCRIPTS();

    if (value < range_min || value > range_max) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "%s out of range: "
               "%"PRIu32", but must be in [%"PRIu32",%"PRIu32"].\n",
               name, value, range_min, range_max);
        return AVERROR_INVALIDDATA;
    }

    *write_to = value;
    return 0;
}
#endif

#if CBS_WRITE
static int cbs_av1_write_uvlc(CodedBitstreamContext *ctx, PutBitContext *pbc,
                              const char *name, uint32_t value,
                              uint32_t range_min, uint32_t range_max)
{
    uint32_t v;
    int zeroes;

    CBS_TRACE_WRITE_START();

    if (value < range_min || value > range_max) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "%s out of range: "
               "%"PRIu32", but must be in [%"PRIu32",%"PRIu32"].\n",
               name, value, range_min, range_max);
        return AVERROR_INVALIDDATA;
    }

    zeroes = av_log2(value + 1);
    v = value - (1U << zeroes) + 1;

    if (put_bits_left(pbc) < 2 * zeroes + 1)
        return AVERROR(ENOSPC);

    put_bits(pbc, zeroes, 0);
    put_bits(pbc, 1, 1);
    put_bits(pbc, zeroes, v);

    CBS_TRACE_WRITE_END_NO_SUBSCRIPTS();

    return 0;
}
#endif

#if CBS_READ
static int cbs_av1_read_leb128(CodedBitstreamContext *ctx, GetBitContext *gbc,
                               const char *name, uint64_t *write_to)
{
    uint64_t value;
    uint32_t byte;
    int i;

    CBS_TRACE_READ_START();

    value = 0;
    for (i = 0; i < 8; i++) {
        if (get_bits_left(gbc) < 8) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid leb128 at "
                   "%s: bitstream ended.\n", name);
            return AVERROR_INVALIDDATA;
        }
        byte = get_bits(gbc, 8);
        value |= (uint64_t)(byte & 0x7f) << (i * 7);
        if (!(byte & 0x80))
            break;
    }

    if (value > UINT32_MAX)
        return AVERROR_INVALIDDATA;

    CBS_TRACE_READ_END_NO_SUBSCRIPTS();

    *write_to = value;
    return 0;
}
#endif

#if CBS_WRITE
static int cbs_av1_write_leb128(CodedBitstreamContext *ctx, PutBitContext *pbc,
                                const char *name, uint64_t value, int fixed_length)
{
    int len, i;
    uint8_t byte;

    CBS_TRACE_WRITE_START();

    len = (av_log2(value) + 7) / 7;

    if (fixed_length) {
        if (fixed_length < len) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "OBU is too large for "
                   "fixed length size field (%d > %d).\n",
                   len, fixed_length);
            return AVERROR(EINVAL);
        }
        len = fixed_length;
    }

    for (i = 0; i < len; i++) {
        if (put_bits_left(pbc) < 8)
            return AVERROR(ENOSPC);

        byte = value >> (7 * i) & 0x7f;
        if (i < len - 1)
            byte |= 0x80;

        put_bits(pbc, 8, byte);
    }

    CBS_TRACE_WRITE_END_NO_SUBSCRIPTS();

    return 0;
}
#endif

#if CBS_READ
static int cbs_av1_read_ns(CodedBitstreamContext *ctx, GetBitContext *gbc,
                           uint32_t n, const char *name,
                           const int *subscripts, uint32_t *write_to)
{
    uint32_t m, v, extra_bit, value;
    int w;

    CBS_TRACE_READ_START();

    av_assert0(n > 0);

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

    CBS_TRACE_READ_END();

    *write_to = value;
    return 0;
}
#endif

#if CBS_WRITE
static int cbs_av1_write_ns(CodedBitstreamContext *ctx, PutBitContext *pbc,
                            uint32_t n, const char *name,
                            const int *subscripts, uint32_t value)
{
    uint32_t w, m, v, extra_bit;

    CBS_TRACE_WRITE_START();

    if (value > n) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "%s out of range: "
               "%"PRIu32", but must be in [0,%"PRIu32"].\n",
               name, value, n);
        return AVERROR_INVALIDDATA;
    }

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

    CBS_TRACE_WRITE_END();

    return 0;
}
#endif

#if CBS_READ
static int cbs_av1_read_increment(CodedBitstreamContext *ctx, GetBitContext *gbc,
                                  uint32_t range_min, uint32_t range_max,
                                  const char *name, uint32_t *write_to)
{
    uint32_t value;

    CBS_TRACE_READ_START();

    av_assert0(range_min <= range_max && range_max - range_min < 32);

    for (value = range_min; value < range_max;) {
        if (get_bits_left(gbc) < 1) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid increment value at "
                   "%s: bitstream ended.\n", name);
            return AVERROR_INVALIDDATA;
        }
        if (get_bits1(gbc))
            ++value;
        else
            break;
    }

    CBS_TRACE_READ_END_NO_SUBSCRIPTS();

    *write_to = value;
    return 0;
}
#endif

#if CBS_WRITE
static int cbs_av1_write_increment(CodedBitstreamContext *ctx, PutBitContext *pbc,
                                   uint32_t range_min, uint32_t range_max,
                                   const char *name, uint32_t value)
{
    int len;

    CBS_TRACE_WRITE_START();

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

    if (len > 0)
        put_bits(pbc, len, (1U << len) - 1 - (value != range_max));

    CBS_TRACE_WRITE_END_NO_SUBSCRIPTS();

    return 0;
}
#endif

#if CBS_READ
static int cbs_av1_read_subexp(CodedBitstreamContext *ctx, GetBitContext *gbc,
                               uint32_t range_max, const char *name,
                               const int *subscripts, uint32_t *write_to)
{
    uint32_t value, max_len, len, range_offset, range_bits;
    int err;

    CBS_TRACE_READ_START();

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
        err = CBS_FUNC(read_simple_unsigned)(ctx, gbc, range_bits,
                                          "subexp_bits", &value);
        if (err < 0)
            return err;

    } else {
        err = cbs_av1_read_ns(ctx, gbc, range_max - range_offset,
                              "subexp_final_bits", NULL, &value);
        if (err < 0)
            return err;
    }
    value += range_offset;

    CBS_TRACE_READ_END_VALUE_ONLY();

    *write_to = value;
    return err;
}
#endif

#if CBS_WRITE
static int cbs_av1_write_subexp(CodedBitstreamContext *ctx, PutBitContext *pbc,
                                uint32_t range_max, const char *name,
                                const int *subscripts, uint32_t value)
{
    int err;
    uint32_t max_len, len, range_offset, range_bits;

    CBS_TRACE_WRITE_START();

    if (value > range_max) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "%s out of range: "
               "%"PRIu32", but must be in [0,%"PRIu32"].\n",
               name, value, range_max);
        return AVERROR_INVALIDDATA;
    }

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
        err = CBS_FUNC(write_simple_unsigned)(ctx, pbc, range_bits,
                                           "subexp_bits",
                                           value - range_offset);
        if (err < 0)
            return err;

    } else {
        err = cbs_av1_write_ns(ctx, pbc, range_max - range_offset,
                               "subexp_final_bits", NULL,
                               value - range_offset);
        if (err < 0)
            return err;
    }

    CBS_TRACE_WRITE_END_VALUE_ONLY();

    return err;
}
#endif


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

static av_unused size_t cbs_av1_get_payload_bytes_left(GetBitContext *gbc)
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
        CBS_FUNC(trace_header)(ctx, name); \
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

#if CBS_READ
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

#define fb(width, name) do { \
        uint32_t value; \
        CHECK(CBS_FUNC(read_simple_unsigned)(ctx, rw, width, \
                                          #name, &value)); \
        current->name = value; \
    } while (0)

#define xf(width, name, var, range_min, range_max, subs, ...) do { \
        uint32_t value; \
        CHECK(CBS_FUNC(read_unsigned)(ctx, rw, width, #name, \
                                   SUBSCRIPTS(subs, __VA_ARGS__), \
                                   &value, range_min, range_max)); \
        var = value; \
    } while (0)

#define xsu(width, name, var, subs, ...) do { \
        int32_t value; \
        CHECK(CBS_FUNC(read_signed)(ctx, rw, width, #name, \
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
#undef fb
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
#endif // CBS_READ


#if CBS_WRITE
#define WRITE
#define READWRITE write
#define RWContext PutBitContext

#define fb(width, name) do { \
        CHECK(CBS_FUNC(write_simple_unsigned)(ctx, rw, width, #name, \
                                           current->name)); \
    } while (0)

#define xf(width, name, var, range_min, range_max, subs, ...) do { \
        CHECK(CBS_FUNC(write_unsigned)(ctx, rw, width, #name, \
                                    SUBSCRIPTS(subs, __VA_ARGS__), \
                                    var, range_min, range_max)); \
    } while (0)

#define xsu(width, name, var, subs, ...) do { \
        CHECK(CBS_FUNC(write_signed)(ctx, rw, width, #name, \
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
        CHECK(cbs_av1_write_leb128(ctx, rw, #name, current->name, 0)); \
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
#undef fb
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
#endif // CBS_WRITE

static int cbs_av1_split_fragment(CodedBitstreamContext *ctx,
                                  CodedBitstreamFragment *frag,
                                  int header)
{
#if CBS_READ
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
        AV1RawOBUHeader obu_header;
        uint64_t obu_size;

        init_get_bits(&gbc, data, 8 * size);

        err = cbs_av1_read_obu_header(ctx, &gbc, &obu_header);
        if (err < 0)
            goto fail;

        if (obu_header.obu_has_size_field) {
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
            obu_size = size - 1 - obu_header.obu_extension_flag;

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

        err = CBS_FUNC(append_unit_data)(frag, obu_header.obu_type,
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
#else
    return AVERROR(ENOSYS);
#endif
}

#if CBS_READ
static int cbs_av1_ref_tile_data(CodedBitstreamContext *ctx,
                                 CodedBitstreamUnit *unit,
                                 GetBitContext *gbc,
                                 AVBufferRef **data_ref,
                                 uint8_t **data, size_t *data_size)
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

    *data_ref = av_buffer_ref(unit->data_ref);
    if (!*data_ref)
        return AVERROR(ENOMEM);

    *data      = unit->data      + pos / 8;
    *data_size = unit->data_size - pos / 8;

    return 0;
}
#endif

static int cbs_av1_read_unit(CodedBitstreamContext *ctx,
                             CodedBitstreamUnit *unit)
{
#if CBS_READ
    CodedBitstreamAV1Context *priv = ctx->priv_data;
    AV1RawOBU *obu;
    GetBitContext gbc;
    int err, start_pos, end_pos;

    err = CBS_FUNC(alloc_unit_content)(ctx, unit);
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

            av_refstruct_replace(&priv->sequence_header_ref, unit->content_ref);
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
    case AV1_OBU_FRAME:
            err = cbs_av1_read_frame_obu(ctx, &gbc, &obu->obu.frame,
                                         unit->data_ref);
            if (err < 0)
                return err;
    // fall-through
    case AV1_OBU_TILE_GROUP:
        {
            AV1RawTileGroup *tile_group = obu->header.obu_type == AV1_OBU_FRAME ? &obu->obu.frame.tile_group
                                                                                : &obu->obu.tile_group;
            err = cbs_av1_ref_tile_data(ctx, unit, &gbc,
                                        &tile_group->data_ref,
                                        &tile_group->data,
                                        &tile_group->data_size);
            if (err < 0)
                return err;

            err = cbs_av1_read_tile_group_obu(ctx, &gbc, tile_group);
            if (err < 0)
                return err;

            err = cbs_av1_ref_tile_data(ctx, unit, &gbc,
                                        &tile_group->tile_data.data_ref,
                                        &tile_group->tile_data.data,
                                        &tile_group->tile_data.data_size);
            if (err < 0)
                return err;
        }
        break;
#if CBS_AV1_OBU_TILE_LIST
    case AV1_OBU_TILE_LIST:
        {
            err = cbs_av1_read_tile_list_obu(ctx, &gbc,
                                             &obu->obu.tile_list);
            if (err < 0)
                return err;

            err = cbs_av1_ref_tile_data(ctx, unit, &gbc,
                                        &obu->obu.tile_list.tile_data.data_ref,
                                        &obu->obu.tile_list.tile_data.data,
                                        &obu->obu.tile_list.tile_data.data_size);
            if (err < 0)
                return err;
        }
        break;
#endif
#if CBS_AV1_OBU_METADATA
    case AV1_OBU_METADATA:
        {
            err = cbs_av1_read_metadata_obu(ctx, &gbc, &obu->obu.metadata);
            if (err < 0)
                return err;
        }
        break;
#endif
#if CBS_AV1_OBU_PADDING
    case AV1_OBU_PADDING:
        {
            err = cbs_av1_read_padding_obu(ctx, &gbc, &obu->obu.padding);
            if (err < 0)
                return err;
        }
        break;
#endif
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
#else
    return AVERROR(ENOSYS);
#endif
}

static int cbs_av1_write_obu(CodedBitstreamContext *ctx,
                             CodedBitstreamUnit *unit,
                             PutBitContext *pbc)
{
#if CBS_WRITE
    CodedBitstreamAV1Context *priv = ctx->priv_data;
    AV1RawOBU *obu = unit->content;
    PutBitContext pbc_tmp;
    AV1RawTileData *td;
    size_t header_size;
    int err, start_pos, end_pos, data_pos;
    CodedBitstreamAV1Context av1ctx;

    // OBUs in the normal bitstream format must contain a size field
    // in every OBU (in annex B it is optional, but we don't support
    // writing that).
    obu->header.obu_has_size_field = 1;
    av1ctx = *priv;

    if (priv->sequence_header_ref) {
        av1ctx.sequence_header_ref = av_refstruct_ref(priv->sequence_header_ref);
    }

    if (priv->frame_header_ref) {
        av1ctx.frame_header_ref = av_buffer_ref(priv->frame_header_ref);
        if (!av1ctx.frame_header_ref) {
            err = AVERROR(ENOMEM);
            goto error;
        }
    }

    err = cbs_av1_write_obu_header(ctx, pbc, &obu->header);
    if (err < 0)
        goto error;

    if (obu->header.obu_has_size_field) {
        pbc_tmp = *pbc;
        if (priv->fixed_obu_size_length) {
            for (int i = 0; i < priv->fixed_obu_size_length; i++)
                put_bits(pbc, 8, 0);
        } else {
            // Add space for the size field to fill later.
            put_bits32(pbc, 0);
            put_bits32(pbc, 0);
        }
    }

    td = NULL;
    start_pos = put_bits_count(pbc);

    switch (obu->header.obu_type) {
    case AV1_OBU_SEQUENCE_HEADER:
        {
            err = cbs_av1_write_sequence_header_obu(ctx, pbc,
                                                    &obu->obu.sequence_header);
            if (err < 0)
                goto error;

            av_refstruct_unref(&priv->sequence_header_ref);
            priv->sequence_header = NULL;

            err = CBS_FUNC(make_unit_refcounted)(ctx, unit);
            if (err < 0)
                goto error;

            priv->sequence_header_ref = av_refstruct_ref(unit->content_ref);
            priv->sequence_header = &obu->obu.sequence_header;
        }
        break;
    case AV1_OBU_TEMPORAL_DELIMITER:
        {
            err = cbs_av1_write_temporal_delimiter_obu(ctx, pbc);
            if (err < 0)
                goto error;
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
                goto error;
        }
        break;
    case AV1_OBU_FRAME:
            err = cbs_av1_write_frame_obu(ctx, pbc, &obu->obu.frame, NULL);
            if (err < 0)
                goto error;
    // fall-through
    case AV1_OBU_TILE_GROUP:
        {
            AV1RawTileGroup *tile_group = obu->header.obu_type == AV1_OBU_FRAME ? &obu->obu.frame.tile_group
                                                                                : &obu->obu.tile_group;
            err = cbs_av1_write_tile_group_obu(ctx, pbc, tile_group);
            if (err < 0)
                goto error;

            td = &tile_group->tile_data;
        }
        break;
#if CBS_AV1_OBU_TILE_LIST
    case AV1_OBU_TILE_LIST:
        {
            err = cbs_av1_write_tile_list_obu(ctx, pbc, &obu->obu.tile_list);
            if (err < 0)
                goto error;

            td = &obu->obu.tile_list.tile_data;
        }
        break;
#endif
#if CBS_AV1_OBU_METADATA
    case AV1_OBU_METADATA:
        {
            err = cbs_av1_write_metadata_obu(ctx, pbc, &obu->obu.metadata);
            if (err < 0)
                goto error;
        }
        break;
#endif
#if CBS_AV1_OBU_PADDING
    case AV1_OBU_PADDING:
        {
            err = cbs_av1_write_padding_obu(ctx, pbc, &obu->obu.padding);
            if (err < 0)
                goto error;
        }
        break;
#endif
    default:
        err = AVERROR(ENOSYS);
        goto error;
    }

    end_pos = put_bits_count(pbc);
    header_size = (end_pos - start_pos + 7) / 8;
    if (td) {
        obu->obu_size = header_size + td->data_size;
    } else if (header_size > 0) {
        // Add trailing bits and recalculate.
        err = cbs_av1_write_trailing_bits(ctx, pbc, 8 - end_pos % 8);
        if (err < 0)
            goto error;
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
    err = cbs_av1_write_leb128(ctx, pbc, "obu_size", obu->obu_size,
                               priv->fixed_obu_size_length);
    if (err < 0)
        goto error;

    data_pos = put_bits_count(pbc) / 8;
    flush_put_bits(pbc);
    av_assert0(data_pos <= start_pos);

    if (8 * obu->obu_size > put_bits_left(pbc)) {
        av_refstruct_unref(&priv->sequence_header_ref);
        av_buffer_unref(&priv->frame_header_ref);
        *priv = av1ctx;

        return AVERROR(ENOSPC);
    }

    if (obu->obu_size > 0) {
        if (!priv->fixed_obu_size_length) {
            memmove(pbc->buf + data_pos,
                    pbc->buf + start_pos, header_size);
        } else {
            // The size was fixed so the following data was
            // already written in the correct place.
        }
        skip_put_bytes(pbc, header_size);

        if (td) {
            memcpy(pbc->buf + data_pos + header_size,
                   td->data, td->data_size);
            skip_put_bytes(pbc, td->data_size);
        }
    }

    // OBU data must be byte-aligned.
    av_assert0(put_bits_count(pbc) % 8 == 0);
    err = 0;

error:
    av_refstruct_unref(&av1ctx.sequence_header_ref);
    av_buffer_unref(&av1ctx.frame_header_ref);

    return err;
#else
    return AVERROR(ENOSYS);
#endif
}

static int cbs_av1_assemble_fragment(CodedBitstreamContext *ctx,
                                     CodedBitstreamFragment *frag)
{
#if CBS_WRITE
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
#else
    return AVERROR(ENOSYS);
#endif
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

    av_refstruct_unref(&priv->sequence_header_ref);
    av_buffer_unref(&priv->frame_header_ref);
}

#if CBS_AV1_OBU_METADATA
static void cbs_av1_free_metadata(AVRefStructOpaque unused, void *content)
{
    AV1RawOBU *obu = content;
    AV1RawMetadata *md;

    av_assert0(obu->header.obu_type == AV1_OBU_METADATA);
    md = &obu->obu.metadata;

    switch (md->metadata_type) {
    case AV1_METADATA_TYPE_HDR_CLL:
    case AV1_METADATA_TYPE_HDR_MDCV:
    case AV1_METADATA_TYPE_SCALABILITY:
    case AV1_METADATA_TYPE_TIMECODE:
        break;
    case AV1_METADATA_TYPE_ITUT_T35:
        av_buffer_unref(&md->metadata.itut_t35.payload_ref);
        break;
    default:
        av_buffer_unref(&md->metadata.unknown.payload_ref);
    }
}
#endif

static const CodedBitstreamUnitTypeDescriptor cbs_av1_unit_types[] = {
    CBS_UNIT_TYPE_POD(AV1_OBU_SEQUENCE_HEADER,        AV1RawOBU),
    CBS_UNIT_TYPE_POD(AV1_OBU_TEMPORAL_DELIMITER,     AV1RawOBU),
    CBS_UNIT_TYPE_POD(AV1_OBU_FRAME_HEADER,           AV1RawOBU),
    CBS_UNIT_TYPE_POD(AV1_OBU_REDUNDANT_FRAME_HEADER, AV1RawOBU),
    {
        .nb_unit_types     = 1,
        .unit_type.list[0] = AV1_OBU_TILE_GROUP,
        .content_type      = CBS_CONTENT_TYPE_INTERNAL_REFS,
        .content_size      = sizeof(AV1RawOBU),
        .type.ref          = {
            .nb_offsets = 2,
            .offsets    = { offsetof(AV1RawOBU, obu.tile_group.data),
                            offsetof(AV1RawOBU, obu.tile_group.tile_data.data) }
        },
    },

    {
        .nb_unit_types     = 1,
        .unit_type.list[0] = AV1_OBU_FRAME,
        .content_type      = CBS_CONTENT_TYPE_INTERNAL_REFS,
        .content_size      = sizeof(AV1RawOBU),
        .type.ref          = {
            .nb_offsets = 2,
            .offsets    = { offsetof(AV1RawOBU, obu.frame.tile_group.data),
                            offsetof(AV1RawOBU, obu.frame.tile_group.tile_data.data) }
        },
    },
#if CBS_AV1_OBU_TILE_LIST
    CBS_UNIT_TYPE_INTERNAL_REF(AV1_OBU_TILE_LIST,  AV1RawOBU,
                               obu.tile_list.tile_data.data),
#endif
#if CBS_AV1_OBU_PADDING
    CBS_UNIT_TYPE_INTERNAL_REF(AV1_OBU_PADDING,    AV1RawOBU,
                               obu.padding.payload),
#endif

#if CBS_AV1_OBU_METADATA
    CBS_UNIT_TYPE_COMPLEX(AV1_OBU_METADATA, AV1RawOBU,
                          &cbs_av1_free_metadata),
#endif

    CBS_UNIT_TYPE_END_OF_LIST
};

#define OFFSET(x) offsetof(CodedBitstreamAV1Context, x)
static const AVOption cbs_av1_options[] = {
    { "operating_point",  "Set operating point to select layers to parse from a scalable bitstream",
                          OFFSET(operating_point), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, AV1_MAX_OPERATING_POINTS - 1, 0 },
    { "fixed_obu_size_length", "Set fixed length of the obu_size field",
      OFFSET(fixed_obu_size_length), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 8, 0 },
    { NULL }
};

static const AVClass cbs_av1_class = {
    .class_name = "cbs_av1",
    .item_name  = av_default_item_name,
    .option     = cbs_av1_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const CodedBitstreamType CBS_FUNC(type_av1) = {
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
