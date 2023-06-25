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
#include "cbs_h264.h"
#include "cbs_h265.h"
#include "cbs_h266.h"
#include "h264.h"
#include "h2645_parse.h"
#include "refstruct.h"
#include "vvc.h"

#include "hevc/hevc.h"

static int cbs_read_ue_golomb(CodedBitstreamContext *ctx, GetBitContext *gbc,
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

static int cbs_read_se_golomb(CodedBitstreamContext *ctx, GetBitContext *gbc,
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

static int cbs_write_ue_golomb(CodedBitstreamContext *ctx, PutBitContext *pbc,
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

static int cbs_write_se_golomb(CodedBitstreamContext *ctx, PutBitContext *pbc,
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

// payload_extension_present() - true if we are before the last 1-bit
// in the payload structure, which must be in the last byte.
static int cbs_h265_payload_extension_present(GetBitContext *gbc, uint32_t payload_size,
                                              int cur_pos)
{
    int bits_left = payload_size * 8 - cur_pos;
    return (bits_left > 0 &&
            (bits_left > 7 || show_bits(gbc, bits_left) & MAX_UINT_BITS(bits_left - 1)));
}

#define HEADER(name) do { \
        ff_cbs_trace_header(ctx, name); \
    } while (0)

#define CHECK(call) do { \
        err = (call); \
        if (err < 0) \
            return err; \
    } while (0)

#define FUNC_NAME2(rw, codec, name) cbs_ ## codec ## _ ## rw ## _ ## name
#define FUNC_NAME1(rw, codec, name) FUNC_NAME2(rw, codec, name)
#define FUNC_H264(name) FUNC_NAME1(READWRITE, h264, name)
#define FUNC_H265(name) FUNC_NAME1(READWRITE, h265, name)
#define FUNC_H266(name) FUNC_NAME1(READWRITE, h266, name)
#define FUNC_SEI(name)  FUNC_NAME1(READWRITE, sei,  name)

#define SEI_FUNC(name, args) \
static int FUNC(name) args;  \
static int FUNC(name ## _internal)(CodedBitstreamContext *ctx, \
                                   RWContext *rw, void *cur,   \
                                   SEIMessageState *state)     \
{ \
    return FUNC(name)(ctx, rw, cur, state); \
} \
static int FUNC(name) args

#define SUBSCRIPTS(subs, ...) (subs > 0 ? ((int[subs + 1]){ subs, __VA_ARGS__ }) : NULL)

#define u(width, name, range_min, range_max) \
        xu(width, name, current->name, range_min, range_max, 0, )
#define flag(name) ub(1, name)
#define ue(name, range_min, range_max) \
        xue(name, current->name, range_min, range_max, 0, )
#define i(width, name, range_min, range_max) \
        xi(width, name, current->name, range_min, range_max, 0, )
#define ib(width, name) \
        xi(width, name, current->name, MIN_INT_BITS(width), MAX_INT_BITS(width), 0, )
#define se(name, range_min, range_max) \
        xse(name, current->name, range_min, range_max, 0, )

#define us(width, name, range_min, range_max, subs, ...) \
        xu(width, name, current->name, range_min, range_max, subs, __VA_ARGS__)
#define ubs(width, name, subs, ...) \
        xu(width, name, current->name, 0, MAX_UINT_BITS(width), subs, __VA_ARGS__)
#define flags(name, subs, ...) \
        xu(1, name, current->name, 0, 1, subs, __VA_ARGS__)
#define ues(name, range_min, range_max, subs, ...) \
        xue(name, current->name, range_min, range_max, subs, __VA_ARGS__)
#define is(width, name, range_min, range_max, subs, ...) \
        xi(width, name, current->name, range_min, range_max, subs, __VA_ARGS__)
#define ibs(width, name, subs, ...) \
        xi(width, name, current->name, MIN_INT_BITS(width), MAX_INT_BITS(width), subs, __VA_ARGS__)
#define ses(name, range_min, range_max, subs, ...) \
        xse(name, current->name, range_min, range_max, subs, __VA_ARGS__)

#define fixed(width, name, value) do { \
        av_unused uint32_t fixed_value = value; \
        xu(width, name, fixed_value, value, value, 0, ); \
    } while (0)


#define READ
#define READWRITE read
#define RWContext GetBitContext

#define ub(width, name) do { \
        uint32_t value; \
        CHECK(ff_cbs_read_simple_unsigned(ctx, rw, width, #name, \
                                          &value)); \
        current->name = value; \
    } while (0)
#define xu(width, name, var, range_min, range_max, subs, ...) do { \
        uint32_t value; \
        CHECK(ff_cbs_read_unsigned(ctx, rw, width, #name, \
                                   SUBSCRIPTS(subs, __VA_ARGS__), \
                                   &value, range_min, range_max)); \
        var = value; \
    } while (0)
#define xue(name, var, range_min, range_max, subs, ...) do { \
        uint32_t value; \
        CHECK(cbs_read_ue_golomb(ctx, rw, #name, \
                                 SUBSCRIPTS(subs, __VA_ARGS__), \
                                 &value, range_min, range_max)); \
        var = value; \
    } while (0)
#define xi(width, name, var, range_min, range_max, subs, ...) do { \
        int32_t value; \
        CHECK(ff_cbs_read_signed(ctx, rw, width, #name, \
                                 SUBSCRIPTS(subs, __VA_ARGS__), \
                                 &value, range_min, range_max)); \
        var = value; \
    } while (0)
#define xse(name, var, range_min, range_max, subs, ...) do { \
        int32_t value; \
        CHECK(cbs_read_se_golomb(ctx, rw, #name, \
                                 SUBSCRIPTS(subs, __VA_ARGS__), \
                                 &value, range_min, range_max)); \
        var = value; \
    } while (0)


#define infer(name, value) do { \
        current->name = value; \
    } while (0)

static int cbs_h2645_read_more_rbsp_data(GetBitContext *gbc)
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

#define more_rbsp_data(var) ((var) = cbs_h2645_read_more_rbsp_data(rw))

#define bit_position(rw)   (get_bits_count(rw))
#define byte_alignment(rw) (get_bits_count(rw) % 8)

/* The CBS SEI code uses the refstruct API for the allocation
 * of its child buffers. */
#define allocate(name, size) do { \
        name  = ff_refstruct_allocz(size + \
                                        AV_INPUT_BUFFER_PADDING_SIZE); \
        if (!name) \
            return AVERROR(ENOMEM); \
    } while (0)

#define FUNC(name) FUNC_SEI(name)
#include "cbs_sei_syntax_template.c"
#undef FUNC

#undef allocate

/* The other code uses the refstruct API for the allocation
 * of its child buffers. */
#define allocate(name, size) do { \
        name ## _ref = av_buffer_allocz(size + \
                                        AV_INPUT_BUFFER_PADDING_SIZE); \
        if (!name ## _ref) \
            return AVERROR(ENOMEM); \
        name = name ## _ref->data; \
    } while (0)

#define FUNC(name) FUNC_H264(name)
#include "cbs_h264_syntax_template.c"
#undef FUNC

#define FUNC(name) FUNC_H265(name)
#include "cbs_h265_syntax_template.c"
#undef FUNC

#define FUNC(name) FUNC_H266(name)
#include "cbs_h266_syntax_template.c"
#undef FUNC

#undef READ
#undef READWRITE
#undef RWContext
#undef ub
#undef xu
#undef xi
#undef xue
#undef xse
#undef infer
#undef more_rbsp_data
#undef bit_position
#undef byte_alignment
#undef allocate


#define WRITE
#define READWRITE write
#define RWContext PutBitContext

#define ub(width, name) do { \
        uint32_t value = current->name; \
        CHECK(ff_cbs_write_simple_unsigned(ctx, rw, width, #name, \
                                           value)); \
    } while (0)
#define xu(width, name, var, range_min, range_max, subs, ...) do { \
        uint32_t value = var; \
        CHECK(ff_cbs_write_unsigned(ctx, rw, width, #name, \
                                    SUBSCRIPTS(subs, __VA_ARGS__), \
                                    value, range_min, range_max)); \
    } while (0)
#define xue(name, var, range_min, range_max, subs, ...) do { \
        uint32_t value = var; \
        CHECK(cbs_write_ue_golomb(ctx, rw, #name, \
                                  SUBSCRIPTS(subs, __VA_ARGS__), \
                                  value, range_min, range_max)); \
    } while (0)
#define xi(width, name, var, range_min, range_max, subs, ...) do { \
        int32_t value = var; \
        CHECK(ff_cbs_write_signed(ctx, rw, width, #name, \
                                  SUBSCRIPTS(subs, __VA_ARGS__), \
                                  value, range_min, range_max)); \
    } while (0)
#define xse(name, var, range_min, range_max, subs, ...) do { \
        int32_t value = var; \
        CHECK(cbs_write_se_golomb(ctx, rw, #name, \
                                  SUBSCRIPTS(subs, __VA_ARGS__), \
                                  value, range_min, range_max)); \
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

#define more_rbsp_data(var) (var)

#define bit_position(rw)   (put_bits_count(rw))
#define byte_alignment(rw) (put_bits_count(rw) % 8)

#define allocate(name, size) do { \
        if (!name) { \
            av_log(ctx->log_ctx, AV_LOG_ERROR, "%s must be set " \
                   "for writing.\n", #name); \
            return AVERROR_INVALIDDATA; \
        } \
    } while (0)

#define FUNC(name) FUNC_SEI(name)
#include "cbs_sei_syntax_template.c"
#undef FUNC

#define FUNC(name) FUNC_H264(name)
#include "cbs_h264_syntax_template.c"
#undef FUNC

#define FUNC(name) FUNC_H265(name)
#include "cbs_h265_syntax_template.c"
#undef FUNC

#define FUNC(name) FUNC_H266(name)
#include "cbs_h266_syntax_template.c"
#undef FUNC

#undef WRITE
#undef READWRITE
#undef RWContext
#undef ub
#undef xu
#undef xi
#undef xue
#undef xse
#undef u
#undef i
#undef flag
#undef ue
#undef se
#undef infer
#undef more_rbsp_data
#undef bit_position
#undef byte_alignment
#undef allocate


static int cbs_h2645_fragment_add_nals(CodedBitstreamContext *ctx,
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

static int cbs_h2645_split_fragment(CodedBitstreamContext *ctx,
                                    CodedBitstreamFragment *frag,
                                    int header)
{
    enum AVCodecID codec_id = ctx->codec->codec_id;
    CodedBitstreamH2645Context *priv = ctx->priv_data;
    GetByteContext gbc;
    int err;

    av_assert0(frag->data && frag->nb_units == 0);
    if (frag->data_size == 0)
        return 0;

    if (header && frag->data[0] && codec_id == AV_CODEC_ID_H264) {
        // AVCC header.
        size_t size, start, end;
        int i, count, version;

        priv->mp4 = 1;

        bytestream2_init(&gbc, frag->data, frag->data_size);

        if (bytestream2_get_bytes_left(&gbc) < 6)
            return AVERROR_INVALIDDATA;

        version = bytestream2_get_byte(&gbc);
        if (version != 1) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid AVCC header: "
                   "first byte %u.\n", version);
            return AVERROR_INVALIDDATA;
        }

        bytestream2_skip(&gbc, 3);
        priv->nal_length_size = (bytestream2_get_byte(&gbc) & 3) + 1;

        // SPS array.
        count = bytestream2_get_byte(&gbc) & 0x1f;
        start = bytestream2_tell(&gbc);
        for (i = 0; i < count; i++) {
            if (bytestream2_get_bytes_left(&gbc) < 2 * (count - i))
                return AVERROR_INVALIDDATA;
            size = bytestream2_get_be16(&gbc);
            if (bytestream2_get_bytes_left(&gbc) < size)
                return AVERROR_INVALIDDATA;
            bytestream2_skip(&gbc, size);
        }
        end = bytestream2_tell(&gbc);

        err = ff_h2645_packet_split(&priv->read_packet,
                                    frag->data + start, end - start,
                                    ctx->log_ctx, 2, AV_CODEC_ID_H264,
                                    H2645_FLAG_IS_NALFF | H2645_FLAG_SMALL_PADDING | H2645_FLAG_USE_REF);
        if (err < 0) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Failed to split AVCC SPS array.\n");
            return err;
        }
        err = cbs_h2645_fragment_add_nals(ctx, frag, &priv->read_packet);
        if (err < 0)
            return err;

        // PPS array.
        count = bytestream2_get_byte(&gbc);
        start = bytestream2_tell(&gbc);
        for (i = 0; i < count; i++) {
            if (bytestream2_get_bytes_left(&gbc) < 2 * (count - i))
                return AVERROR_INVALIDDATA;
            size = bytestream2_get_be16(&gbc);
            if (bytestream2_get_bytes_left(&gbc) < size)
                return AVERROR_INVALIDDATA;
            bytestream2_skip(&gbc, size);
        }
        end = bytestream2_tell(&gbc);

        err = ff_h2645_packet_split(&priv->read_packet,
                                    frag->data + start, end - start,
                                    ctx->log_ctx, 2, AV_CODEC_ID_H264,
                                    H2645_FLAG_IS_NALFF | H2645_FLAG_SMALL_PADDING | H2645_FLAG_USE_REF);
        if (err < 0) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Failed to split AVCC PPS array.\n");
            return err;
        }
        err = cbs_h2645_fragment_add_nals(ctx, frag, &priv->read_packet);
        if (err < 0)
            return err;

        if (bytestream2_get_bytes_left(&gbc) > 0) {
            av_log(ctx->log_ctx, AV_LOG_WARNING, "%u bytes left at end of AVCC "
                   "header.\n", bytestream2_get_bytes_left(&gbc));
        }

    } else if (header && frag->data[0] && codec_id == AV_CODEC_ID_HEVC) {
        // HVCC header.
        size_t size, start, end;
        int i, j, nb_arrays, nal_unit_type, nb_nals, version;

        priv->mp4 = 1;

        bytestream2_init(&gbc, frag->data, frag->data_size);

        if (bytestream2_get_bytes_left(&gbc) < 23)
            return AVERROR_INVALIDDATA;

        version = bytestream2_get_byte(&gbc);
        if (version != 1) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid HVCC header: "
                   "first byte %u.\n", version);
            return AVERROR_INVALIDDATA;
        }

        bytestream2_skip(&gbc, 20);
        priv->nal_length_size = (bytestream2_get_byte(&gbc) & 3) + 1;

        nb_arrays = bytestream2_get_byte(&gbc);
        for (i = 0; i < nb_arrays; i++) {
            nal_unit_type = bytestream2_get_byte(&gbc) & 0x3f;
            nb_nals = bytestream2_get_be16(&gbc);

            start = bytestream2_tell(&gbc);
            for (j = 0; j < nb_nals; j++) {
                if (bytestream2_get_bytes_left(&gbc) < 2)
                    return AVERROR_INVALIDDATA;
                size = bytestream2_get_be16(&gbc);
                if (bytestream2_get_bytes_left(&gbc) < size)
                    return AVERROR_INVALIDDATA;
                bytestream2_skip(&gbc, size);
            }
            end = bytestream2_tell(&gbc);

            err = ff_h2645_packet_split(&priv->read_packet,
                                        frag->data + start, end - start,
                                        ctx->log_ctx, 2, AV_CODEC_ID_HEVC,
                                        H2645_FLAG_IS_NALFF | H2645_FLAG_SMALL_PADDING | H2645_FLAG_USE_REF);
            if (err < 0) {
                av_log(ctx->log_ctx, AV_LOG_ERROR, "Failed to split "
                       "HVCC array %d (%d NAL units of type %d).\n",
                       i, nb_nals, nal_unit_type);
                return err;
            }
            err = cbs_h2645_fragment_add_nals(ctx, frag, &priv->read_packet);
            if (err < 0)
                return err;
        }

    } else if(header && frag->data[0] && codec_id == AV_CODEC_ID_VVC) {
        // VVCC header.
        int ptl_present_flag, num_arrays;
        int b, i, j;

        priv->mp4 = 1;

        bytestream2_init(&gbc, frag->data, frag->data_size);

        b = bytestream2_get_byte(&gbc);
        priv->nal_length_size = ((b >> 1) & 3) + 1;
        ptl_present_flag = b & 1;

        if(ptl_present_flag) {
            int num_sublayers, num_bytes_constraint_info, num_sub_profiles;
            num_sublayers = (bytestream2_get_be16u(&gbc) >> 4) & 7;
            bytestream2_skip(&gbc, 1);

            // begin VvcPTLRecord(num_sublayers);
            num_bytes_constraint_info = bytestream2_get_byte(&gbc) & 0x3f;
            bytestream2_skip(&gbc, 2 + num_bytes_constraint_info);
            if(num_sublayers > 1) {
                int count_present_flags = 0;
                b = bytestream2_get_byte(&gbc);
                for(i = num_sublayers - 2; i >= 0; i--) {
                    if((b >> (7 - (num_sublayers - 2 - i))) & 0x01)
                        count_present_flags++;
                }
                bytestream2_skip(&gbc, count_present_flags);
            }
            num_sub_profiles = bytestream2_get_byte(&gbc);
            bytestream2_skip(&gbc, num_sub_profiles * 4);
            // end VvcPTLRecord(num_sublayers);

            bytestream2_skip(&gbc, 3 * 2);
        }

        num_arrays = bytestream2_get_byte(&gbc);
        for(j = 0; j < num_arrays; j++) {
            size_t start, end, size;
            int nal_unit_type = bytestream2_get_byte(&gbc) & 0x1f;
            unsigned int num_nalus = 1;
            if(nal_unit_type != VVC_DCI_NUT && nal_unit_type != VVC_OPI_NUT)
                num_nalus = bytestream2_get_be16(&gbc);

            start = bytestream2_tell(&gbc);
            for(i = 0; i < num_nalus; i++) {
                if (bytestream2_get_bytes_left(&gbc) < 2)
                    return AVERROR_INVALIDDATA;
                size = bytestream2_get_be16(&gbc);
                if (bytestream2_get_bytes_left(&gbc) < size)
                    return AVERROR_INVALIDDATA;
                bytestream2_skip(&gbc, size);
            }
            end = bytestream2_tell(&gbc);

            err = ff_h2645_packet_split(&priv->read_packet,
                                        frag->data + start, end - start,
                                        ctx->log_ctx, 2, AV_CODEC_ID_VVC,
                                        H2645_FLAG_IS_NALFF | H2645_FLAG_SMALL_PADDING | H2645_FLAG_USE_REF);
            if (err < 0) {
                av_log(ctx->log_ctx, AV_LOG_ERROR, "Failed to split "
                       "VVCC array %d (%d NAL units of type %d).\n",
                       i, num_nalus, nal_unit_type);
                return err;
            }
            err = cbs_h2645_fragment_add_nals(ctx, frag, &priv->read_packet);
            if (err < 0)
                return err;
        }
    } else {
        int flags = (H2645_FLAG_IS_NALFF * !!priv->mp4) | H2645_FLAG_SMALL_PADDING | H2645_FLAG_USE_REF;
        // Annex B, or later MP4 with already-known parameters.

        err = ff_h2645_packet_split(&priv->read_packet,
                                    frag->data, frag->data_size,
                                    ctx->log_ctx,
                                    priv->nal_length_size,
                                    codec_id, flags);
        if (err < 0)
            return err;

        err = cbs_h2645_fragment_add_nals(ctx, frag, &priv->read_packet);
        if (err < 0)
            return err;
    }

    return 0;
}

#define cbs_h2645_replace_ps(h26n, ps_name, ps_var, id_element) \
static int cbs_h26 ## h26n ## _replace_ ## ps_var(CodedBitstreamContext *ctx, \
                                                  CodedBitstreamUnit *unit)  \
{ \
    CodedBitstreamH26 ## h26n ## Context *priv = ctx->priv_data; \
    H26 ## h26n ## Raw ## ps_name *ps_var = unit->content; \
    unsigned int id = ps_var->id_element; \
    int err = ff_cbs_make_unit_refcounted(ctx, unit); \
    if (err < 0) \
        return err; \
    if (priv->ps_var[id] == priv->active_ ## ps_var) \
        priv->active_ ## ps_var = NULL ; \
    av_assert0(unit->content_ref); \
    ff_refstruct_replace(&priv->ps_var[id], unit->content_ref); \
    return 0; \
}

cbs_h2645_replace_ps(4, SPS, sps, seq_parameter_set_id)
cbs_h2645_replace_ps(4, PPS, pps, pic_parameter_set_id)
cbs_h2645_replace_ps(5, VPS, vps, vps_video_parameter_set_id)
cbs_h2645_replace_ps(5, SPS, sps, sps_seq_parameter_set_id)
cbs_h2645_replace_ps(5, PPS, pps, pps_pic_parameter_set_id)

#define cbs_h266_replace_ps(h26n, ps_name, ps_var, id_element) \
static int cbs_h26 ## h26n ## _replace_ ## ps_var(CodedBitstreamContext *ctx, \
                                                  CodedBitstreamUnit *unit)  \
{ \
    CodedBitstreamH26 ## h26n ## Context *priv = ctx->priv_data; \
    H26 ## h26n ## Raw ## ps_name *ps_var = unit->content; \
    unsigned int id = ps_var->id_element; \
    int err = ff_cbs_make_unit_refcounted(ctx, unit); \
    if (err < 0) \
        return err; \
    av_assert0(unit->content_ref); \
    ff_refstruct_replace(&priv->ps_var[id], unit->content_ref); \
    return 0; \
}

cbs_h266_replace_ps(6, VPS, vps, vps_video_parameter_set_id)
cbs_h266_replace_ps(6, PPS, pps, pps_pic_parameter_set_id)

static int cbs_h266_replace_sps(CodedBitstreamContext *ctx,
                                CodedBitstreamUnit *unit)
{
    CodedBitstreamH266Context *priv = ctx->priv_data;
    H266RawSPS *sps = unit->content;
    unsigned int id = sps->sps_seq_parameter_set_id;
    int err = ff_cbs_make_unit_refcounted(ctx, unit);
    if (err < 0)
        return err;
    av_assert0(unit->content_ref);
    if (priv->sps[id] && memcmp(priv->sps[id], unit->content_ref, sizeof(*priv->sps[id]))) {
        for (unsigned int i = 0; i < VVC_MAX_PPS_COUNT; i++) {
            if (priv->pps[i] && priv->pps[i]->pps_seq_parameter_set_id == id)
                ff_refstruct_unref(&priv->pps[i]);
        }
    }
    ff_refstruct_replace(&priv->sps[id], unit->content_ref);
    return 0;
}

static int cbs_h266_replace_ph(CodedBitstreamContext *ctx,
                               CodedBitstreamUnit *unit,
                               H266RawPictureHeader *ph)
{
    CodedBitstreamH266Context *h266 = ctx->priv_data;
    int err;

    err = ff_cbs_make_unit_refcounted(ctx, unit);
    if (err < 0)
        return err;
    av_assert0(unit->content_ref);
    ff_refstruct_replace(&h266->ph_ref, unit->content_ref);
    h266->ph = ph;
    return 0;
}

static int cbs_h264_read_nal_unit(CodedBitstreamContext *ctx,
                                  CodedBitstreamUnit *unit)
{
    GetBitContext gbc;
    int err;

    err = init_get_bits(&gbc, unit->data, 8 * unit->data_size);
    if (err < 0)
        return err;

    err = ff_cbs_alloc_unit_content(ctx, unit);
    if (err < 0)
        return err;

    switch (unit->type) {
    case H264_NAL_SPS:
        {
            H264RawSPS *sps = unit->content;

            err = cbs_h264_read_sps(ctx, &gbc, sps);
            if (err < 0)
                return err;

            err = cbs_h264_replace_sps(ctx, unit);
            if (err < 0)
                return err;
        }
        break;

    case H264_NAL_SPS_EXT:
        {
            err = cbs_h264_read_sps_extension(ctx, &gbc, unit->content);
            if (err < 0)
                return err;
        }
        break;

    case H264_NAL_PPS:
        {
            H264RawPPS *pps = unit->content;

            err = cbs_h264_read_pps(ctx, &gbc, pps);
            if (err < 0)
                return err;

            err = cbs_h264_replace_pps(ctx, unit);
            if (err < 0)
                return err;
        }
        break;

    case H264_NAL_SLICE:
    case H264_NAL_IDR_SLICE:
    case H264_NAL_AUXILIARY_SLICE:
        {
            H264RawSlice *slice = unit->content;
            int pos, len;

            err = cbs_h264_read_slice_header(ctx, &gbc, &slice->header);
            if (err < 0)
                return err;

            if (!cbs_h2645_read_more_rbsp_data(&gbc))
                return AVERROR_INVALIDDATA;

            pos = get_bits_count(&gbc);
            len = unit->data_size;

            slice->data_size = len - pos / 8;
            slice->data_ref  = av_buffer_ref(unit->data_ref);
            if (!slice->data_ref)
                return AVERROR(ENOMEM);
            slice->data = unit->data + pos / 8;
            slice->data_bit_start = pos % 8;
        }
        break;

    case H264_NAL_AUD:
        {
            err = cbs_h264_read_aud(ctx, &gbc, unit->content);
            if (err < 0)
                return err;
        }
        break;

    case H264_NAL_SEI:
        {
            err = cbs_h264_read_sei(ctx, &gbc, unit->content);
            if (err < 0)
                return err;
        }
        break;

    case H264_NAL_FILLER_DATA:
        {
            err = cbs_h264_read_filler(ctx, &gbc, unit->content);
            if (err < 0)
                return err;
        }
        break;

    case H264_NAL_END_SEQUENCE:
    case H264_NAL_END_STREAM:
        {
            err = (unit->type == H264_NAL_END_SEQUENCE ?
                   cbs_h264_read_end_of_sequence :
                   cbs_h264_read_end_of_stream)(ctx, &gbc, unit->content);
            if (err < 0)
                return err;
        }
        break;

    default:
        return AVERROR(ENOSYS);
    }

    return 0;
}

static int cbs_h265_read_nal_unit(CodedBitstreamContext *ctx,
                                  CodedBitstreamUnit *unit)
{
    GetBitContext gbc;
    int err;

    err = init_get_bits(&gbc, unit->data, 8 * unit->data_size);
    if (err < 0)
        return err;

    err = ff_cbs_alloc_unit_content(ctx, unit);
    if (err < 0)
        return err;

    switch (unit->type) {
    case HEVC_NAL_VPS:
        {
            H265RawVPS *vps = unit->content;

            err = cbs_h265_read_vps(ctx, &gbc, vps);
            if (err < 0)
                return err;

            err = cbs_h265_replace_vps(ctx, unit);
            if (err < 0)
                return err;
        }
        break;
    case HEVC_NAL_SPS:
        {
            H265RawSPS *sps = unit->content;

            err = cbs_h265_read_sps(ctx, &gbc, sps);
            if (err < 0)
                return err;

            err = cbs_h265_replace_sps(ctx, unit);
            if (err < 0)
                return err;
        }
        break;

    case HEVC_NAL_PPS:
        {
            H265RawPPS *pps = unit->content;

            err = cbs_h265_read_pps(ctx, &gbc, pps);
            if (err < 0)
                return err;

            err = cbs_h265_replace_pps(ctx, unit);
            if (err < 0)
                return err;
        }
        break;

    case HEVC_NAL_TRAIL_N:
    case HEVC_NAL_TRAIL_R:
    case HEVC_NAL_TSA_N:
    case HEVC_NAL_TSA_R:
    case HEVC_NAL_STSA_N:
    case HEVC_NAL_STSA_R:
    case HEVC_NAL_RADL_N:
    case HEVC_NAL_RADL_R:
    case HEVC_NAL_RASL_N:
    case HEVC_NAL_RASL_R:
    case HEVC_NAL_BLA_W_LP:
    case HEVC_NAL_BLA_W_RADL:
    case HEVC_NAL_BLA_N_LP:
    case HEVC_NAL_IDR_W_RADL:
    case HEVC_NAL_IDR_N_LP:
    case HEVC_NAL_CRA_NUT:
        {
            H265RawSlice *slice = unit->content;
            int pos, len;

            err = cbs_h265_read_slice_segment_header(ctx, &gbc, &slice->header);
            if (err < 0)
                return err;

            if (!cbs_h2645_read_more_rbsp_data(&gbc))
                return AVERROR_INVALIDDATA;

            pos = get_bits_count(&gbc);
            len = unit->data_size;

            slice->data_size = len - pos / 8;
            slice->data_ref  = av_buffer_ref(unit->data_ref);
            if (!slice->data_ref)
                return AVERROR(ENOMEM);
            slice->data = unit->data + pos / 8;
            slice->data_bit_start = pos % 8;
        }
        break;

    case HEVC_NAL_AUD:
        {
            err = cbs_h265_read_aud(ctx, &gbc, unit->content);
            if (err < 0)
                return err;
        }
        break;

    case HEVC_NAL_FD_NUT:
        {
            err = cbs_h265_read_filler(ctx, &gbc, unit->content);
            if (err < 0)
                return err;
        }
        break;

    case HEVC_NAL_SEI_PREFIX:
    case HEVC_NAL_SEI_SUFFIX:
        {
            err = cbs_h265_read_sei(ctx, &gbc, unit->content,
                                    unit->type == HEVC_NAL_SEI_PREFIX);

            if (err < 0)
                return err;
        }
        break;

    default:
        return AVERROR(ENOSYS);
    }

    return 0;
}

static int cbs_h266_read_nal_unit(CodedBitstreamContext *ctx,
                                  CodedBitstreamUnit *unit)
{
    GetBitContext gbc;
    int err;

    err = init_get_bits8(&gbc, unit->data, unit->data_size);
    if (err < 0)
        return err;

    err = ff_cbs_alloc_unit_content(ctx, unit);
    if (err < 0)
        return err;

    switch (unit->type) {
    case VVC_DCI_NUT:
        {
            err = cbs_h266_read_dci(ctx, &gbc, unit->content);

            if (err < 0)
                return err;
        }
        break;
    case VVC_OPI_NUT:
        {
            err = cbs_h266_read_opi(ctx, &gbc, unit->content);

            if (err < 0)
                return err;
        }
        break;
    case VVC_VPS_NUT:
        {
            H266RawVPS *vps = unit->content;

            err = cbs_h266_read_vps(ctx, &gbc, vps);
            if (err < 0)
                return err;

            err = cbs_h266_replace_vps(ctx, unit);
            if (err < 0)
                return err;
        }
        break;
    case VVC_SPS_NUT:
        {
            H266RawSPS *sps = unit->content;

            err = cbs_h266_read_sps(ctx, &gbc, sps);
            if (err < 0)
                return err;

            err = cbs_h266_replace_sps(ctx, unit);
            if (err < 0)
                return err;
        }
        break;

    case VVC_PPS_NUT:
        {
            H266RawPPS *pps = unit->content;

            err = cbs_h266_read_pps(ctx, &gbc, pps);
            if (err < 0)
                return err;

            err = cbs_h266_replace_pps(ctx, unit);
            if (err < 0)
                return err;
        }
        break;

    case VVC_PREFIX_APS_NUT:
    case VVC_SUFFIX_APS_NUT:
        {
            err = cbs_h266_read_aps(ctx, &gbc, unit->content,
                                    unit->type == VVC_PREFIX_APS_NUT);

            if (err < 0)
                return err;
        }
        break;
    case VVC_PH_NUT:
        {
            H266RawPH *ph = unit->content;
            err = cbs_h266_read_ph(ctx, &gbc, ph);
            if (err < 0)
                return err;
            err = cbs_h266_replace_ph(ctx, unit, &ph->ph_picture_header);
            if (err < 0)
                return err;
        }
        break;

    case VVC_TRAIL_NUT:
    case VVC_STSA_NUT:
    case VVC_RADL_NUT:
    case VVC_RASL_NUT:
    case VVC_IDR_W_RADL:
    case VVC_IDR_N_LP:
    case VVC_CRA_NUT:
    case VVC_GDR_NUT:
        {
            H266RawSlice *slice = unit->content;
            int pos, len;

            err = cbs_h266_read_slice_header(ctx, &gbc, &slice->header);
            if (err < 0)
                return err;

            if (!cbs_h2645_read_more_rbsp_data(&gbc))
                return AVERROR_INVALIDDATA;

            pos = get_bits_count(&gbc);
            len = unit->data_size;

            if (slice->header.sh_picture_header_in_slice_header_flag) {
                err = cbs_h266_replace_ph(ctx, unit, &slice->header.sh_picture_header);
                if (err < 0)
                    return err;
            }

            slice->header_size = pos / 8;
            slice->data_size = len - pos / 8;
            slice->data_ref  = av_buffer_ref(unit->data_ref);
            if (!slice->data_ref)
                return AVERROR(ENOMEM);
            slice->data = unit->data + pos / 8;
            slice->data_bit_start = pos % 8;
        }
        break;

    case VVC_AUD_NUT:
        {
            err = cbs_h266_read_aud(ctx, &gbc, unit->content);
            if (err < 0)
                return err;
        }
        break;

    case VVC_PREFIX_SEI_NUT:
    case VVC_SUFFIX_SEI_NUT:
        {
            err = cbs_h266_read_sei(ctx, &gbc, unit->content,
                                    unit->type == VVC_PREFIX_SEI_NUT);

            if (err < 0)
                return err;
        }
        break;

    default:
        return AVERROR(ENOSYS);
    }
    return 0;
}

static int cbs_h2645_write_slice_data(CodedBitstreamContext *ctx,
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

static int cbs_h264_write_nal_unit(CodedBitstreamContext *ctx,
                                   CodedBitstreamUnit *unit,
                                   PutBitContext *pbc)
{
    int err;

    switch (unit->type) {
    case H264_NAL_SPS:
        {
            H264RawSPS *sps = unit->content;

            err = cbs_h264_write_sps(ctx, pbc, sps);
            if (err < 0)
                return err;

            err = cbs_h264_replace_sps(ctx, unit);
            if (err < 0)
                return err;
        }
        break;

    case H264_NAL_SPS_EXT:
        {
            H264RawSPSExtension *sps_ext = unit->content;

            err = cbs_h264_write_sps_extension(ctx, pbc, sps_ext);
            if (err < 0)
                return err;
        }
        break;

    case H264_NAL_PPS:
        {
            H264RawPPS *pps = unit->content;

            err = cbs_h264_write_pps(ctx, pbc, pps);
            if (err < 0)
                return err;

            err = cbs_h264_replace_pps(ctx, unit);
            if (err < 0)
                return err;
        }
        break;

    case H264_NAL_SLICE:
    case H264_NAL_IDR_SLICE:
    case H264_NAL_AUXILIARY_SLICE:
        {
            H264RawSlice *slice = unit->content;

            err = cbs_h264_write_slice_header(ctx, pbc, &slice->header);
            if (err < 0)
                return err;

            if (slice->data) {
                err = cbs_h2645_write_slice_data(ctx, pbc, slice->data,
                                                 slice->data_size,
                                                 slice->data_bit_start);
                if (err < 0)
                    return err;
            } else {
                // No slice data - that was just the header.
                // (Bitstream may be unaligned!)
            }
        }
        break;

    case H264_NAL_AUD:
        {
            err = cbs_h264_write_aud(ctx, pbc, unit->content);
            if (err < 0)
                return err;
        }
        break;

    case H264_NAL_SEI:
        {
            err = cbs_h264_write_sei(ctx, pbc, unit->content);
            if (err < 0)
                return err;
        }
        break;

    case H264_NAL_FILLER_DATA:
        {
            err = cbs_h264_write_filler(ctx, pbc, unit->content);
            if (err < 0)
                return err;
        }
        break;

    case H264_NAL_END_SEQUENCE:
        {
            err = cbs_h264_write_end_of_sequence(ctx, pbc, unit->content);
            if (err < 0)
                return err;
        }
        break;

    case H264_NAL_END_STREAM:
        {
            err = cbs_h264_write_end_of_stream(ctx, pbc, unit->content);
            if (err < 0)
                return err;
        }
        break;

    default:
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Write unimplemented for "
               "NAL unit type %"PRIu32".\n", unit->type);
        return AVERROR_PATCHWELCOME;
    }

    return 0;
}

static int cbs_h265_write_nal_unit(CodedBitstreamContext *ctx,
                                   CodedBitstreamUnit *unit,
                                   PutBitContext *pbc)
{
    int err;

    switch (unit->type) {
    case HEVC_NAL_VPS:
        {
            H265RawVPS *vps = unit->content;

            err = cbs_h265_write_vps(ctx, pbc, vps);
            if (err < 0)
                return err;

            err = cbs_h265_replace_vps(ctx, unit);
            if (err < 0)
                return err;
        }
        break;

    case HEVC_NAL_SPS:
        {
            H265RawSPS *sps = unit->content;

            err = cbs_h265_write_sps(ctx, pbc, sps);
            if (err < 0)
                return err;

            err = cbs_h265_replace_sps(ctx, unit);
            if (err < 0)
                return err;
        }
        break;

    case HEVC_NAL_PPS:
        {
            H265RawPPS *pps = unit->content;

            err = cbs_h265_write_pps(ctx, pbc, pps);
            if (err < 0)
                return err;

            err = cbs_h265_replace_pps(ctx, unit);
            if (err < 0)
                return err;
        }
        break;

    case HEVC_NAL_TRAIL_N:
    case HEVC_NAL_TRAIL_R:
    case HEVC_NAL_TSA_N:
    case HEVC_NAL_TSA_R:
    case HEVC_NAL_STSA_N:
    case HEVC_NAL_STSA_R:
    case HEVC_NAL_RADL_N:
    case HEVC_NAL_RADL_R:
    case HEVC_NAL_RASL_N:
    case HEVC_NAL_RASL_R:
    case HEVC_NAL_BLA_W_LP:
    case HEVC_NAL_BLA_W_RADL:
    case HEVC_NAL_BLA_N_LP:
    case HEVC_NAL_IDR_W_RADL:
    case HEVC_NAL_IDR_N_LP:
    case HEVC_NAL_CRA_NUT:
        {
            H265RawSlice *slice = unit->content;

            err = cbs_h265_write_slice_segment_header(ctx, pbc, &slice->header);
            if (err < 0)
                return err;

            if (slice->data) {
                err = cbs_h2645_write_slice_data(ctx, pbc, slice->data,
                                                 slice->data_size,
                                                 slice->data_bit_start);
                if (err < 0)
                    return err;
            } else {
                // No slice data - that was just the header.
            }
        }
        break;

    case HEVC_NAL_AUD:
        {
            err = cbs_h265_write_aud(ctx, pbc, unit->content);
            if (err < 0)
                return err;
        }
        break;

    case HEVC_NAL_FD_NUT:
        {
            err = cbs_h265_write_filler(ctx, pbc, unit->content);
            if (err < 0)
                return err;
        }
        break;

    case HEVC_NAL_SEI_PREFIX:
    case HEVC_NAL_SEI_SUFFIX:
        {
            err = cbs_h265_write_sei(ctx, pbc, unit->content,
                                     unit->type == HEVC_NAL_SEI_PREFIX);

            if (err < 0)
                return err;
        }
        break;

    default:
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Write unimplemented for "
               "NAL unit type %"PRIu32".\n", unit->type);
        return AVERROR_PATCHWELCOME;
    }

    return 0;
}

static int cbs_h264_discarded_nal_unit(CodedBitstreamContext *ctx,
                                       const CodedBitstreamUnit *unit,
                                       enum AVDiscard skip)
{
    H264RawNALUnitHeader *header;
    H264RawSliceHeader *slice;
    int slice_type_i, slice_type_b, slice_type_si;

    if (skip <= AVDISCARD_DEFAULT)
        return 0;

    // keep non-VCL
    if (unit->type != H264_NAL_SLICE &&
        unit->type != H264_NAL_IDR_SLICE &&
        unit->type != H264_NAL_AUXILIARY_SLICE)
        return 0;

    if (skip >= AVDISCARD_ALL)
        return 1;

    if (skip >= AVDISCARD_NONKEY && unit->type != H264_NAL_IDR_SLICE)
        return 1;

    header = (H264RawNALUnitHeader *)unit->content;
    if (!header) {
        av_log(ctx->log_ctx, AV_LOG_WARNING,
                "h264 nal unit header is null, missing decompose?\n");
        return 0;
    }

    if (skip >= AVDISCARD_NONREF && !header->nal_ref_idc)
        return 1;

    slice = (H264RawSliceHeader *)unit->content;
    if (!slice) {
        av_log(ctx->log_ctx, AV_LOG_WARNING,
                "h264 slice header is null, missing decompose?\n");
        return 0;
    }

    slice_type_i  = slice->slice_type % 5 == 2;
    slice_type_b  = slice->slice_type % 5 == 1;
    slice_type_si = slice->slice_type % 5 == 4;

    if (skip >= AVDISCARD_BIDIR && slice_type_b)
        return 1;
    if (skip >= AVDISCARD_NONINTRA && !slice_type_i && !slice_type_si)
        return 1;

    return 0;
}

static int cbs_h265_discarded_nal_unit(CodedBitstreamContext *ctx,
                                       const CodedBitstreamUnit *unit,
                                       enum AVDiscard skip)
{
    H265RawSliceHeader *slice;

    if (skip <= AVDISCARD_DEFAULT)
        return 0;

    switch (unit->type) {
    case HEVC_NAL_BLA_W_LP:
    case HEVC_NAL_BLA_W_RADL:
    case HEVC_NAL_BLA_N_LP:
    case HEVC_NAL_IDR_W_RADL:
    case HEVC_NAL_IDR_N_LP:
    case HEVC_NAL_CRA_NUT:
        // IRAP slice
        if (skip < AVDISCARD_ALL)
            return 0;
        break;

    case HEVC_NAL_TRAIL_R:
    case HEVC_NAL_TRAIL_N:
    case HEVC_NAL_TSA_N:
    case HEVC_NAL_TSA_R:
    case HEVC_NAL_STSA_N:
    case HEVC_NAL_STSA_R:
    case HEVC_NAL_RADL_N:
    case HEVC_NAL_RADL_R:
    case HEVC_NAL_RASL_N:
    case HEVC_NAL_RASL_R:
        // Slice
        break;
    default:
        // Don't discard non-slice nal.
        return 0;
    }

    if (skip >= AVDISCARD_NONKEY)
        return 1;

    slice = (H265RawSliceHeader *)unit->content;
    if (!slice) {
        av_log(ctx->log_ctx, AV_LOG_WARNING,
                "h265 slice header is null, missing decompose?\n");
        return 0;
    }

    if (skip >= AVDISCARD_NONINTRA && slice->slice_type != HEVC_SLICE_I)
        return 1;
    if (skip >= AVDISCARD_BIDIR && slice->slice_type == HEVC_SLICE_B)
        return 1;

    if (skip >= AVDISCARD_NONREF) {
        switch (unit->type) {
        case HEVC_NAL_TRAIL_N:
        case HEVC_NAL_TSA_N:
        case HEVC_NAL_STSA_N:
        case HEVC_NAL_RADL_N:
        case HEVC_NAL_RASL_N:
        case HEVC_NAL_VCL_N10:
        case HEVC_NAL_VCL_N12:
        case HEVC_NAL_VCL_N14:
            // non-ref
            return 1;
        default:
            break;
        }
    }

    return 0;
}

static int cbs_h266_write_nal_unit(CodedBitstreamContext *ctx,
                                   CodedBitstreamUnit *unit,
                                   PutBitContext *pbc)
{
    int err;

    switch (unit->type) {
    case VVC_DCI_NUT:
        {
            H266RawDCI *dci = unit->content;

            err = cbs_h266_write_dci(ctx, pbc, dci);
            if (err < 0)
                return err;
        }
        break;
    case VVC_OPI_NUT:
        {
            H266RawOPI *opi = unit->content;

            err = cbs_h266_write_opi(ctx, pbc, opi);
            if (err < 0)
                return err;
        }
        break;
    case VVC_VPS_NUT:
        {
            H266RawVPS *vps = unit->content;

            err = cbs_h266_write_vps(ctx, pbc, vps);
            if (err < 0)
                return err;

            err = cbs_h266_replace_vps(ctx, unit);
            if (err < 0)
                return err;
        }
        break;
    case VVC_SPS_NUT:
        {
            H266RawSPS *sps = unit->content;

            err = cbs_h266_write_sps(ctx, pbc, sps);
            if (err < 0)
                return err;

            err = cbs_h266_replace_sps(ctx, unit);
            if (err < 0)
                return err;
        }
        break;

    case VVC_PPS_NUT:
        {
            H266RawPPS *pps = unit->content;

            err = cbs_h266_write_pps(ctx, pbc, pps);
            if (err < 0)
                return err;

            err = cbs_h266_replace_pps(ctx, unit);
            if (err < 0)
                return err;
        }
        break;

    case VVC_PREFIX_APS_NUT:
    case VVC_SUFFIX_APS_NUT:
        {
            err = cbs_h266_write_aps(ctx, pbc, unit->content,
                                     unit->type == VVC_PREFIX_APS_NUT);
            if (err < 0)
                return err;
        }
        break;
    case VVC_PH_NUT:
        {
            H266RawPH *ph = unit->content;
            err = cbs_h266_write_ph(ctx, pbc, ph);
            if (err < 0)
                return err;

            err = cbs_h266_replace_ph(ctx, unit, &ph->ph_picture_header);
            if (err < 0)
                return err;
        }
        break;

    case VVC_TRAIL_NUT:
    case VVC_STSA_NUT:
    case VVC_RADL_NUT:
    case VVC_RASL_NUT:
    case VVC_IDR_W_RADL:
    case VVC_IDR_N_LP:
    case VVC_CRA_NUT:
    case VVC_GDR_NUT:
        {
            H266RawSlice *slice = unit->content;

            err = cbs_h266_write_slice_header(ctx, pbc, &slice->header);
            if (err < 0)
                return err;

            if (slice->header.sh_picture_header_in_slice_header_flag) {
                err = cbs_h266_replace_ph(ctx, unit, &slice->header.sh_picture_header);
                if (err < 0)
                    return err;
            }

            if (slice->data) {
                err = cbs_h2645_write_slice_data(ctx, pbc, slice->data,
                                                 slice->data_size,
                                                 slice->data_bit_start);
                if (err < 0)
                    return err;
            } else {
                // No slice data - that was just the header.
            }
        }
        break;

    case VVC_AUD_NUT:
        {
            err = cbs_h266_write_aud(ctx, pbc, unit->content);
            if (err < 0)
                return err;
        }
        break;

    case VVC_PREFIX_SEI_NUT:
    case VVC_SUFFIX_SEI_NUT:
        {
            err = cbs_h266_write_sei(ctx, pbc, unit->content,
                                     unit->type == VVC_PREFIX_SEI_NUT);

            if (err < 0)
                return err;
        }
        break;

    default:
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Write unimplemented for "
               "NAL unit type %"PRIu32".\n", unit->type);
        return AVERROR_PATCHWELCOME;
    }

    return 0;
}

static int cbs_h2645_unit_requires_zero_byte(enum AVCodecID codec_id,
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

static int cbs_h2645_assemble_fragment(CodedBitstreamContext *ctx,
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

        if (cbs_h2645_unit_requires_zero_byte(ctx->codec->codec_id, unit->type, i)) {
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

static void cbs_h264_flush(CodedBitstreamContext *ctx)
{
    CodedBitstreamH264Context *h264 = ctx->priv_data;

    for (int i = 0; i < FF_ARRAY_ELEMS(h264->sps); i++)
        ff_refstruct_unref(&h264->sps[i]);
    for (int i = 0; i < FF_ARRAY_ELEMS(h264->pps); i++)
        ff_refstruct_unref(&h264->pps[i]);

    h264->active_sps = NULL;
    h264->active_pps = NULL;
    h264->last_slice_nal_unit_type = 0;
}

static void cbs_h264_close(CodedBitstreamContext *ctx)
{
    CodedBitstreamH264Context *h264 = ctx->priv_data;
    int i;

    ff_h2645_packet_uninit(&h264->common.read_packet);

    for (i = 0; i < FF_ARRAY_ELEMS(h264->sps); i++)
        ff_refstruct_unref(&h264->sps[i]);
    for (i = 0; i < FF_ARRAY_ELEMS(h264->pps); i++)
        ff_refstruct_unref(&h264->pps[i]);
}

static void cbs_h265_flush(CodedBitstreamContext *ctx)
{
    CodedBitstreamH265Context *h265 = ctx->priv_data;

    for (int i = 0; i < FF_ARRAY_ELEMS(h265->vps); i++)
        ff_refstruct_unref(&h265->vps[i]);
    for (int i = 0; i < FF_ARRAY_ELEMS(h265->sps); i++)
        ff_refstruct_unref(&h265->sps[i]);
    for (int i = 0; i < FF_ARRAY_ELEMS(h265->pps); i++)
        ff_refstruct_unref(&h265->pps[i]);

    h265->active_vps = NULL;
    h265->active_sps = NULL;
    h265->active_pps = NULL;
}

static void cbs_h265_close(CodedBitstreamContext *ctx)
{
    CodedBitstreamH265Context *h265 = ctx->priv_data;
    int i;

    ff_h2645_packet_uninit(&h265->common.read_packet);

    for (i = 0; i < FF_ARRAY_ELEMS(h265->vps); i++)
        ff_refstruct_unref(&h265->vps[i]);
    for (i = 0; i < FF_ARRAY_ELEMS(h265->sps); i++)
        ff_refstruct_unref(&h265->sps[i]);
    for (i = 0; i < FF_ARRAY_ELEMS(h265->pps); i++)
        ff_refstruct_unref(&h265->pps[i]);
}

static void cbs_h266_flush(CodedBitstreamContext *ctx)
{
    CodedBitstreamH266Context *h266 = ctx->priv_data;

    for (int i = 0; i < FF_ARRAY_ELEMS(h266->vps); i++)
        ff_refstruct_unref(&h266->vps[i]);
    for (int i = 0; i < FF_ARRAY_ELEMS(h266->sps); i++)
        ff_refstruct_unref(&h266->sps[i]);
    for (int i = 0; i < FF_ARRAY_ELEMS(h266->pps); i++)
        ff_refstruct_unref(&h266->pps[i]);
    ff_refstruct_unref(&h266->ph_ref);
}

static void cbs_h266_close(CodedBitstreamContext *ctx)
{
    CodedBitstreamH266Context *h266 = ctx->priv_data;

    cbs_h266_flush(ctx);
    ff_h2645_packet_uninit(&h266->common.read_packet);
 }

static void cbs_h264_free_sei(FFRefStructOpaque unused, void *content)
{
    H264RawSEI *sei = content;
    ff_cbs_sei_free_message_list(&sei->message_list);
}

static const CodedBitstreamUnitTypeDescriptor cbs_h264_unit_types[] = {
    CBS_UNIT_TYPE_POD(H264_NAL_SPS,     H264RawSPS),
    CBS_UNIT_TYPE_POD(H264_NAL_SPS_EXT, H264RawSPSExtension),

    CBS_UNIT_TYPE_INTERNAL_REF(H264_NAL_PPS, H264RawPPS, slice_group_id),

    CBS_UNIT_TYPES_INTERNAL_REF((H264_NAL_IDR_SLICE,
                                 H264_NAL_SLICE,
                                 H264_NAL_AUXILIARY_SLICE), H264RawSlice, data),

    CBS_UNIT_TYPE_POD(H264_NAL_AUD,          H264RawAUD),
    CBS_UNIT_TYPE_POD(H264_NAL_FILLER_DATA,  H264RawFiller),
    CBS_UNIT_TYPE_POD(H264_NAL_END_SEQUENCE, H264RawNALUnitHeader),
    CBS_UNIT_TYPE_POD(H264_NAL_END_STREAM,   H264RawNALUnitHeader),

    CBS_UNIT_TYPE_COMPLEX(H264_NAL_SEI, H264RawSEI, &cbs_h264_free_sei),

    CBS_UNIT_TYPE_END_OF_LIST
};

static void cbs_h265_free_sei(FFRefStructOpaque unused, void *content)
{
    H265RawSEI *sei = content;
    ff_cbs_sei_free_message_list(&sei->message_list);
}

static const CodedBitstreamUnitTypeDescriptor cbs_h265_unit_types[] = {
    CBS_UNIT_TYPE_INTERNAL_REF(HEVC_NAL_VPS, H265RawVPS, extension_data.data),
    CBS_UNIT_TYPE_INTERNAL_REF(HEVC_NAL_SPS, H265RawSPS, extension_data.data),
    CBS_UNIT_TYPE_INTERNAL_REF(HEVC_NAL_PPS, H265RawPPS, extension_data.data),

    CBS_UNIT_TYPE_POD(HEVC_NAL_AUD, H265RawAUD),
    CBS_UNIT_TYPE_POD(HEVC_NAL_FD_NUT, H265RawFiller),

    // Slices of non-IRAP pictures.
    CBS_UNIT_RANGE_INTERNAL_REF(HEVC_NAL_TRAIL_N, HEVC_NAL_RASL_R,
                                H265RawSlice, data),
    // Slices of IRAP pictures.
    CBS_UNIT_RANGE_INTERNAL_REF(HEVC_NAL_BLA_W_LP, HEVC_NAL_CRA_NUT,
                                H265RawSlice, data),

    CBS_UNIT_TYPES_COMPLEX((HEVC_NAL_SEI_PREFIX, HEVC_NAL_SEI_SUFFIX),
                           H265RawSEI, cbs_h265_free_sei),

    CBS_UNIT_TYPE_END_OF_LIST
};

static void cbs_h266_free_sei(FFRefStructOpaque unused, void *content)
{
    H266RawSEI *sei = content;
    ff_cbs_sei_free_message_list(&sei->message_list);
}

static const CodedBitstreamUnitTypeDescriptor cbs_h266_unit_types[] = {
    CBS_UNIT_TYPE_INTERNAL_REF(VVC_DCI_NUT, H266RawDCI, extension_data.data),
    CBS_UNIT_TYPE_INTERNAL_REF(VVC_OPI_NUT, H266RawOPI, extension_data.data),
    CBS_UNIT_TYPE_INTERNAL_REF(VVC_VPS_NUT, H266RawVPS, extension_data.data),
    {
        .nb_unit_types     = 1,
        .unit_type.list[0] = VVC_SPS_NUT,
        .content_type      = CBS_CONTENT_TYPE_INTERNAL_REFS,
        .content_size      = sizeof(H266RawSPS),
        .type.ref          = {
            .nb_offsets = 2,
            .offsets    = { offsetof(H266RawSPS, extension_data.data),
                            offsetof(H266RawSPS, vui.extension_data.data) }
        },
    },
    CBS_UNIT_TYPE_INTERNAL_REF(VVC_PPS_NUT, H266RawPPS, extension_data.data),
    CBS_UNIT_TYPE_INTERNAL_REF(VVC_PREFIX_APS_NUT, H266RawAPS, extension_data.data),
    CBS_UNIT_TYPE_INTERNAL_REF(VVC_SUFFIX_APS_NUT, H266RawAPS, extension_data.data),

    CBS_UNIT_TYPE_POD(VVC_PH_NUT , H266RawPH),
    CBS_UNIT_TYPE_POD(VVC_AUD_NUT, H266RawAUD),

    CBS_UNIT_RANGE_INTERNAL_REF(VVC_TRAIL_NUT, VVC_RASL_NUT,
                                H266RawSlice, data),

    CBS_UNIT_RANGE_INTERNAL_REF(VVC_IDR_W_RADL, VVC_GDR_NUT,
                                H266RawSlice, data),

    CBS_UNIT_TYPES_COMPLEX((VVC_PREFIX_SEI_NUT, VVC_SUFFIX_SEI_NUT),
                           H266RawSEI, cbs_h266_free_sei),

    CBS_UNIT_TYPE_END_OF_LIST
};

const CodedBitstreamType ff_cbs_type_h264 = {
    .codec_id          = AV_CODEC_ID_H264,

    .priv_data_size    = sizeof(CodedBitstreamH264Context),

    .unit_types        = cbs_h264_unit_types,

    .split_fragment    = &cbs_h2645_split_fragment,
    .read_unit         = &cbs_h264_read_nal_unit,
    .write_unit        = &cbs_h264_write_nal_unit,
    .discarded_unit    = &cbs_h264_discarded_nal_unit,
    .assemble_fragment = &cbs_h2645_assemble_fragment,

    .flush             = &cbs_h264_flush,
    .close             = &cbs_h264_close,
};

const CodedBitstreamType ff_cbs_type_h265 = {
    .codec_id          = AV_CODEC_ID_HEVC,

    .priv_data_size    = sizeof(CodedBitstreamH265Context),

    .unit_types        = cbs_h265_unit_types,

    .split_fragment    = &cbs_h2645_split_fragment,
    .read_unit         = &cbs_h265_read_nal_unit,
    .write_unit        = &cbs_h265_write_nal_unit,
    .discarded_unit    = &cbs_h265_discarded_nal_unit,
    .assemble_fragment = &cbs_h2645_assemble_fragment,

    .flush             = &cbs_h265_flush,
    .close             = &cbs_h265_close,
};

const CodedBitstreamType ff_cbs_type_h266 = {
    .codec_id          = AV_CODEC_ID_VVC,

    .priv_data_size    = sizeof(CodedBitstreamH266Context),

    .unit_types        = cbs_h266_unit_types,

    .split_fragment    = &cbs_h2645_split_fragment,
    .read_unit         = &cbs_h266_read_nal_unit,
    .write_unit        = &cbs_h266_write_nal_unit,
    .assemble_fragment = &cbs_h2645_assemble_fragment,

    .flush             = &cbs_h266_flush,
    .close             = &cbs_h266_close,
};

// Macro for the read/write pair.
#define SEI_MESSAGE_RW(codec, name) \
    .read  = cbs_ ## codec ## _read_  ## name ## _internal, \
    .write = cbs_ ## codec ## _write_ ## name ## _internal

static const SEIMessageTypeDescriptor cbs_sei_common_types[] = {
    {
        SEI_TYPE_FILLER_PAYLOAD,
        1, 1,
        sizeof(SEIRawFillerPayload),
        SEI_MESSAGE_RW(sei, filler_payload),
    },
    {
        SEI_TYPE_USER_DATA_REGISTERED_ITU_T_T35,
        1, 1,
        sizeof(SEIRawUserDataRegistered),
        SEI_MESSAGE_RW(sei, user_data_registered),
    },
    {
        SEI_TYPE_USER_DATA_UNREGISTERED,
        1, 1,
        sizeof(SEIRawUserDataUnregistered),
        SEI_MESSAGE_RW(sei, user_data_unregistered),
    },
    {
        SEI_TYPE_FRAME_PACKING_ARRANGEMENT,
        1, 0,
        sizeof(SEIRawFramePackingArrangement),
        SEI_MESSAGE_RW(sei, frame_packing_arrangement),
    },
    {
        SEI_TYPE_DECODED_PICTURE_HASH,
        0, 1,
        sizeof(SEIRawDecodedPictureHash),
        SEI_MESSAGE_RW(sei, decoded_picture_hash),
    },
    {
        SEI_TYPE_MASTERING_DISPLAY_COLOUR_VOLUME,
        1, 0,
        sizeof(SEIRawMasteringDisplayColourVolume),
        SEI_MESSAGE_RW(sei, mastering_display_colour_volume),
    },
    {
        SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO,
        1, 0,
        sizeof(SEIRawContentLightLevelInfo),
        SEI_MESSAGE_RW(sei, content_light_level_info),
    },
    {
        SEI_TYPE_ALTERNATIVE_TRANSFER_CHARACTERISTICS,
        1, 0,
        sizeof(SEIRawAlternativeTransferCharacteristics),
        SEI_MESSAGE_RW(sei, alternative_transfer_characteristics),
    },
    {
        SEI_TYPE_AMBIENT_VIEWING_ENVIRONMENT,
        1, 0,
        sizeof(SEIRawAmbientViewingEnvironment),
        SEI_MESSAGE_RW(sei, ambient_viewing_environment),
    },
    SEI_MESSAGE_TYPE_END,
};

static const SEIMessageTypeDescriptor cbs_sei_h264_types[] = {
    {
        SEI_TYPE_BUFFERING_PERIOD,
        1, 0,
        sizeof(H264RawSEIBufferingPeriod),
        SEI_MESSAGE_RW(h264, sei_buffering_period),
    },
    {
        SEI_TYPE_PIC_TIMING,
        1, 0,
        sizeof(H264RawSEIPicTiming),
        SEI_MESSAGE_RW(h264, sei_pic_timing),
    },
    {
        SEI_TYPE_PAN_SCAN_RECT,
        1, 0,
        sizeof(H264RawSEIPanScanRect),
        SEI_MESSAGE_RW(h264, sei_pan_scan_rect),
    },
    {
        SEI_TYPE_RECOVERY_POINT,
        1, 0,
        sizeof(H264RawSEIRecoveryPoint),
        SEI_MESSAGE_RW(h264, sei_recovery_point),
    },
    {
        SEI_TYPE_FILM_GRAIN_CHARACTERISTICS,
        1, 0,
        sizeof(H264RawFilmGrainCharacteristics),
        SEI_MESSAGE_RW(h264, film_grain_characteristics),
    },
    {
        SEI_TYPE_FRAME_PACKING_ARRANGEMENT,
        1, 0,
        sizeof(H264RawSEIFramePackingArrangement),
        SEI_MESSAGE_RW(h264, sei_frame_packing_arrangement),
    },
    {
        SEI_TYPE_DISPLAY_ORIENTATION,
        1, 0,
        sizeof(H264RawSEIDisplayOrientation),
        SEI_MESSAGE_RW(h264, sei_display_orientation),
    },
    SEI_MESSAGE_TYPE_END
};

static const SEIMessageTypeDescriptor cbs_sei_h265_types[] = {
    {
        SEI_TYPE_BUFFERING_PERIOD,
        1, 0,
        sizeof(H265RawSEIBufferingPeriod),
        SEI_MESSAGE_RW(h265, sei_buffering_period),
    },
    {
        SEI_TYPE_PIC_TIMING,
        1, 0,
        sizeof(H265RawSEIPicTiming),
        SEI_MESSAGE_RW(h265, sei_pic_timing),
    },
    {
        SEI_TYPE_PAN_SCAN_RECT,
        1, 0,
        sizeof(H265RawSEIPanScanRect),
        SEI_MESSAGE_RW(h265, sei_pan_scan_rect),
    },
    {
        SEI_TYPE_RECOVERY_POINT,
        1, 0,
        sizeof(H265RawSEIRecoveryPoint),
        SEI_MESSAGE_RW(h265, sei_recovery_point),
    },
    {
        SEI_TYPE_FILM_GRAIN_CHARACTERISTICS,
        1, 0,
        sizeof(H265RawFilmGrainCharacteristics),
        SEI_MESSAGE_RW(h265, film_grain_characteristics),
    },
    {
        SEI_TYPE_DISPLAY_ORIENTATION,
        1, 0,
        sizeof(H265RawSEIDisplayOrientation),
        SEI_MESSAGE_RW(h265, sei_display_orientation),
    },
    {
        SEI_TYPE_ACTIVE_PARAMETER_SETS,
        1, 0,
        sizeof(H265RawSEIActiveParameterSets),
        SEI_MESSAGE_RW(h265, sei_active_parameter_sets),
    },
    {
        SEI_TYPE_DECODED_PICTURE_HASH,
        0, 1,
        sizeof(H265RawSEIDecodedPictureHash),
        SEI_MESSAGE_RW(h265, sei_decoded_picture_hash),
    },
    {
        SEI_TYPE_TIME_CODE,
        1, 0,
        sizeof(H265RawSEITimeCode),
        SEI_MESSAGE_RW(h265, sei_time_code),
    },
    {
        SEI_TYPE_ALPHA_CHANNEL_INFO,
        1, 0,
        sizeof(H265RawSEIAlphaChannelInfo),
        SEI_MESSAGE_RW(h265, sei_alpha_channel_info),
    },
    {
        SEI_TYPE_THREE_DIMENSIONAL_REFERENCE_DISPLAYS_INFO,
        1, 0,
        sizeof(H265RawSEI3DReferenceDisplaysInfo),
        SEI_MESSAGE_RW(h265, sei_3d_reference_displays_info),
    },
    SEI_MESSAGE_TYPE_END
};

static const SEIMessageTypeDescriptor cbs_sei_h266_types[] = {
    SEI_MESSAGE_TYPE_END
};

const SEIMessageTypeDescriptor *ff_cbs_sei_find_type(CodedBitstreamContext *ctx,
                                                     int payload_type)
{
    const SEIMessageTypeDescriptor *codec_list;
    int i;

    switch (ctx->codec->codec_id) {
    case AV_CODEC_ID_H264:
        codec_list = cbs_sei_h264_types;
        break;
    case AV_CODEC_ID_H265:
        codec_list = cbs_sei_h265_types;
        break;
    case AV_CODEC_ID_H266:
        codec_list = cbs_sei_h266_types;
        break;
    default:
        return NULL;
    }

    for (i = 0; codec_list[i].type >= 0; i++) {
        if (codec_list[i].type == payload_type)
            return &codec_list[i];
    }

    for (i = 0; cbs_sei_common_types[i].type >= 0; i++) {
        if (cbs_sei_common_types[i].type == payload_type)
            return &cbs_sei_common_types[i];
    }

    return NULL;
}
