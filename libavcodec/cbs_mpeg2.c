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
#include "cbs_mpeg2.h"
#include "internal.h"


#define HEADER(name) do { \
        ff_cbs_trace_header(ctx, name); \
    } while (0)

#define CHECK(call) do { \
        err = (call); \
        if (err < 0) \
            return err; \
    } while (0)

#define FUNC_NAME(rw, codec, name) cbs_ ## codec ## _ ## rw ## _ ## name
#define FUNC_MPEG2(rw, name) FUNC_NAME(rw, mpeg2, name)
#define FUNC(name) FUNC_MPEG2(READWRITE, name)

#define SUBSCRIPTS(subs, ...) (subs > 0 ? ((int[subs + 1]){ subs, __VA_ARGS__ }) : NULL)

#define ui(width, name) \
        xui(width, name, current->name, 0, MAX_UINT_BITS(width), 0, )
#define uir(width, name) \
        xui(width, name, current->name, 1, MAX_UINT_BITS(width), 0, )
#define uis(width, name, subs, ...) \
        xui(width, name, current->name, 0, MAX_UINT_BITS(width), subs, __VA_ARGS__)
#define uirs(width, name, subs, ...) \
        xui(width, name, current->name, 1, MAX_UINT_BITS(width), subs, __VA_ARGS__)
#define xui(width, name, var, range_min, range_max, subs, ...) \
        xuia(width, #name, var, range_min, range_max, subs, __VA_ARGS__)
#define sis(width, name, subs, ...) \
        xsi(width, name, current->name, subs, __VA_ARGS__)

#define marker_bit() \
        bit("marker_bit", 1)
#define bit(string, value) do { \
        av_unused uint32_t bit = value; \
        xuia(1, string, bit, value, value, 0, ); \
    } while (0)


#define READ
#define READWRITE read
#define RWContext GetBitContext

#define xuia(width, string, var, range_min, range_max, subs, ...) do { \
        uint32_t value; \
        CHECK(ff_cbs_read_unsigned(ctx, rw, width, string, \
                                   SUBSCRIPTS(subs, __VA_ARGS__), \
                                   &value, range_min, range_max)); \
        var = value; \
    } while (0)

#define xsi(width, name, var, subs, ...) do { \
        int32_t value; \
        CHECK(ff_cbs_read_signed(ctx, rw, width, #name, \
                                 SUBSCRIPTS(subs, __VA_ARGS__), &value, \
                                 MIN_INT_BITS(width), \
                                 MAX_INT_BITS(width))); \
        var = value; \
    } while (0)

#define nextbits(width, compare, var) \
    (get_bits_left(rw) >= width && \
     (var = show_bits(rw, width)) == (compare))

#define infer(name, value) do { \
        current->name = value; \
    } while (0)

#include "cbs_mpeg2_syntax_template.c"

#undef READ
#undef READWRITE
#undef RWContext
#undef xuia
#undef xsi
#undef nextbits
#undef infer


#define WRITE
#define READWRITE write
#define RWContext PutBitContext

#define xuia(width, string, var, range_min, range_max, subs, ...) do { \
        CHECK(ff_cbs_write_unsigned(ctx, rw, width, string, \
                                    SUBSCRIPTS(subs, __VA_ARGS__), \
                                    var, range_min, range_max)); \
    } while (0)

#define xsi(width, name, var, subs, ...) do { \
        CHECK(ff_cbs_write_signed(ctx, rw, width, #name, \
                                  SUBSCRIPTS(subs, __VA_ARGS__), var, \
                                  MIN_INT_BITS(width), \
                                  MAX_INT_BITS(width))); \
    } while (0)

#define nextbits(width, compare, var) (var)

#define infer(name, value) do { \
        if (current->name != (value)) { \
            av_log(ctx->log_ctx, AV_LOG_WARNING, "Warning: " \
                   "%s does not match inferred value: " \
                   "%"PRId64", but should be %"PRId64".\n", \
                   #name, (int64_t)current->name, (int64_t)(value)); \
        } \
    } while (0)

#include "cbs_mpeg2_syntax_template.c"

#undef WRITE
#undef READWRITE
#undef RWContext
#undef xuia
#undef xsi
#undef nextbits
#undef infer


static void cbs_mpeg2_free_picture_header(void *opaque, uint8_t *content)
{
    MPEG2RawPictureHeader *picture = (MPEG2RawPictureHeader*)content;
    av_buffer_unref(&picture->extra_information_picture.extra_information_ref);
    av_freep(&content);
}

static void cbs_mpeg2_free_user_data(void *opaque, uint8_t *content)
{
    MPEG2RawUserData *user = (MPEG2RawUserData*)content;
    av_buffer_unref(&user->user_data_ref);
    av_freep(&content);
}

static void cbs_mpeg2_free_slice(void *opaque, uint8_t *content)
{
    MPEG2RawSlice *slice = (MPEG2RawSlice*)content;
    av_buffer_unref(&slice->header.extra_information_slice.extra_information_ref);
    av_buffer_unref(&slice->data_ref);
    av_freep(&content);
}

static int cbs_mpeg2_split_fragment(CodedBitstreamContext *ctx,
                                    CodedBitstreamFragment *frag,
                                    int header)
{
    const uint8_t *start, *end;
    CodedBitstreamUnitType unit_type;
    uint32_t start_code = -1;
    size_t unit_size;
    int err, i, final = 0;

    start = avpriv_find_start_code(frag->data, frag->data + frag->data_size,
                                   &start_code);
    if (start_code >> 8 != 0x000001) {
        // No start code found.
        return AVERROR_INVALIDDATA;
    }

    for (i = 0;; i++) {
        unit_type = start_code & 0xff;

        if (start == frag->data + frag->data_size) {
            // The last four bytes form a start code which constitutes
            // a unit of its own.  In this situation avpriv_find_start_code
            // won't modify start_code at all so modify start_code so that
            // the next unit will be treated as the last unit.
            start_code = 0;
        }

        end = avpriv_find_start_code(start--, frag->data + frag->data_size,
                                     &start_code);

        // start points to the byte containing the start_code_identifier
        // (may be the last byte of fragment->data); end points to the byte
        // following the byte containing the start code identifier (or to
        // the end of fragment->data).
        if (start_code >> 8 == 0x000001) {
            // Unit runs from start to the beginning of the start code
            // pointed to by end (including any padding zeroes).
            unit_size = (end - 4) - start;
        } else {
           // We didn't find a start code, so this is the final unit.
           unit_size = end - start;
           final     = 1;
        }

        err = ff_cbs_insert_unit_data(frag, i, unit_type, (uint8_t*)start,
                                      unit_size, frag->data_ref);
        if (err < 0)
            return err;

        if (final)
            break;

        start = end;
    }

    return 0;
}

static int cbs_mpeg2_read_unit(CodedBitstreamContext *ctx,
                               CodedBitstreamUnit *unit)
{
    GetBitContext gbc;
    int err;

    err = init_get_bits(&gbc, unit->data, 8 * unit->data_size);
    if (err < 0)
        return err;

    if (MPEG2_START_IS_SLICE(unit->type)) {
        MPEG2RawSlice *slice;
        int pos, len;

        err = ff_cbs_alloc_unit_content(unit, sizeof(*slice),
                                        &cbs_mpeg2_free_slice);
        if (err < 0)
            return err;
        slice = unit->content;

        err = cbs_mpeg2_read_slice_header(ctx, &gbc, &slice->header);
        if (err < 0)
            return err;

        if (!get_bits_left(&gbc))
            return AVERROR_INVALIDDATA;

        pos = get_bits_count(&gbc);
        len = unit->data_size;

        slice->data_size = len - pos / 8;
        slice->data_ref  = av_buffer_ref(unit->data_ref);
        if (!slice->data_ref)
            return AVERROR(ENOMEM);
        slice->data = unit->data + pos / 8;

        slice->data_bit_start = pos % 8;

    } else {
        switch (unit->type) {
#define START(start_code, type, read_func, free_func) \
        case start_code: \
            { \
                type *header; \
                err = ff_cbs_alloc_unit_content(unit, \
                                                sizeof(*header), free_func); \
                if (err < 0) \
                    return err; \
                header = unit->content; \
                err = cbs_mpeg2_read_ ## read_func(ctx, &gbc, header); \
                if (err < 0) \
                    return err; \
            } \
            break;
            START(MPEG2_START_PICTURE,   MPEG2RawPictureHeader,
                  picture_header,           &cbs_mpeg2_free_picture_header);
            START(MPEG2_START_USER_DATA, MPEG2RawUserData,
                  user_data,                &cbs_mpeg2_free_user_data);
            START(MPEG2_START_SEQUENCE_HEADER, MPEG2RawSequenceHeader,
                  sequence_header,          NULL);
            START(MPEG2_START_EXTENSION, MPEG2RawExtensionData,
                  extension_data,           NULL);
            START(MPEG2_START_GROUP,     MPEG2RawGroupOfPicturesHeader,
                  group_of_pictures_header, NULL);
            START(MPEG2_START_SEQUENCE_END, MPEG2RawSequenceEnd,
                  sequence_end,             NULL);
#undef START
        default:
            return AVERROR(ENOSYS);
        }
    }

    return 0;
}

static int cbs_mpeg2_write_header(CodedBitstreamContext *ctx,
                                  CodedBitstreamUnit *unit,
                                  PutBitContext *pbc)
{
    int err;

    switch (unit->type) {
#define START(start_code, type, func) \
    case start_code: \
        err = cbs_mpeg2_write_ ## func(ctx, pbc, unit->content); \
        break;
        START(MPEG2_START_PICTURE,         MPEG2RawPictureHeader,  picture_header);
        START(MPEG2_START_USER_DATA,       MPEG2RawUserData,       user_data);
        START(MPEG2_START_SEQUENCE_HEADER, MPEG2RawSequenceHeader, sequence_header);
        START(MPEG2_START_EXTENSION,       MPEG2RawExtensionData,  extension_data);
        START(MPEG2_START_GROUP,           MPEG2RawGroupOfPicturesHeader,
                                                         group_of_pictures_header);
        START(MPEG2_START_SEQUENCE_END,    MPEG2RawSequenceEnd,    sequence_end);
#undef START
    default:
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Write unimplemented for start "
               "code %02"PRIx32".\n", unit->type);
        return AVERROR_PATCHWELCOME;
    }

    return err;
}

static int cbs_mpeg2_write_slice(CodedBitstreamContext *ctx,
                                 CodedBitstreamUnit *unit,
                                 PutBitContext *pbc)
{
    MPEG2RawSlice *slice = unit->content;
    int err;

    err = cbs_mpeg2_write_slice_header(ctx, pbc, &slice->header);
    if (err < 0)
        return err;

    if (slice->data) {
        size_t rest = slice->data_size - (slice->data_bit_start + 7) / 8;
        uint8_t *pos = slice->data + slice->data_bit_start / 8;

        av_assert0(slice->data_bit_start >= 0 &&
                   slice->data_size > slice->data_bit_start / 8);

        if (slice->data_size * 8 + 8 > put_bits_left(pbc))
            return AVERROR(ENOSPC);

        // First copy the remaining bits of the first byte
        if (slice->data_bit_start % 8)
            put_bits(pbc, 8 - slice->data_bit_start % 8,
                     *pos++ & MAX_UINT_BITS(8 - slice->data_bit_start % 8));

        if (put_bits_count(pbc) % 8 == 0) {
            // If the writer is aligned at this point,
            // memcpy can be used to improve performance.
            // This is the normal case.
            flush_put_bits(pbc);
            memcpy(put_bits_ptr(pbc), pos, rest);
            skip_put_bytes(pbc, rest);
        } else {
            // If not, we have to copy manually:
            for (; rest > 3; rest -= 4, pos += 4)
                put_bits32(pbc, AV_RB32(pos));

            for (; rest; rest--, pos++)
                put_bits(pbc, 8, *pos);

            // Align with zeros
            put_bits(pbc, 8 - put_bits_count(pbc) % 8, 0);
        }
    }

    return 0;
}

static int cbs_mpeg2_write_unit(CodedBitstreamContext *ctx,
                                CodedBitstreamUnit *unit,
                                PutBitContext *pbc)
{
    if (MPEG2_START_IS_SLICE(unit->type))
        return cbs_mpeg2_write_slice (ctx, unit, pbc);
    else
        return cbs_mpeg2_write_header(ctx, unit, pbc);
}

static int cbs_mpeg2_assemble_fragment(CodedBitstreamContext *ctx,
                                       CodedBitstreamFragment *frag)
{
    uint8_t *data;
    size_t size, dp;
    int i;

    size = 0;
    for (i = 0; i < frag->nb_units; i++)
        size += 3 + frag->units[i].data_size;

    frag->data_ref = av_buffer_alloc(size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!frag->data_ref)
        return AVERROR(ENOMEM);
    data = frag->data_ref->data;

    dp = 0;
    for (i = 0; i < frag->nb_units; i++) {
        CodedBitstreamUnit *unit = &frag->units[i];

        data[dp++] = 0;
        data[dp++] = 0;
        data[dp++] = 1;

        memcpy(data + dp, unit->data, unit->data_size);
        dp += unit->data_size;
    }

    av_assert0(dp == size);

    memset(data + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    frag->data      = data;
    frag->data_size = size;

    return 0;
}

const CodedBitstreamType ff_cbs_type_mpeg2 = {
    .codec_id          = AV_CODEC_ID_MPEG2VIDEO,

    .priv_data_size    = sizeof(CodedBitstreamMPEG2Context),

    .split_fragment    = &cbs_mpeg2_split_fragment,
    .read_unit         = &cbs_mpeg2_read_unit,
    .write_unit        = &cbs_mpeg2_write_unit,
    .assemble_fragment = &cbs_mpeg2_assemble_fragment,
};
