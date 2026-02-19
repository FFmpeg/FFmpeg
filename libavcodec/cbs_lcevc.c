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

#include "libavutil/mem.h"
#include "libavutil/refstruct.h"
#include "bytestream.h"
#include "cbs.h"
#include "cbs_internal.h"
#include "cbs_h2645.h"
#include "cbs_lcevc.h"
#include "cbs_sei.h"
#include "get_bits.h"

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
#define FUNC_LCEVC(name) FUNC_NAME1(READWRITE, lcevc, name)
#define FUNC_NAME2_EXPORT(rw, codec, name) ff_cbs_ ## codec ## _ ## rw ## _ ## name
#define FUNC_NAME1_EXPORT(rw, codec, name) FUNC_NAME2_EXPORT(rw, codec, name)
#define FUNC_SEI(name)  FUNC_NAME1_EXPORT(READWRITE, sei,  name)

#define SEI_FUNC(name, args) \
static int FUNC_LCEVC(name) args;  \
static int FUNC_LCEVC(name ## _internal)(CodedBitstreamContext *ctx, \
                                   RWContext *rw, void *cur,   \
                                   SEIMessageState *state)     \
{ \
    return FUNC_LCEVC(name)(ctx, rw, cur, state); \
} \
static int FUNC_LCEVC(name) args

#define LCEVC_BLOCK_FUNC(name, args) \
static int FUNC(name) args;  \
static int FUNC(name ## _internal)(CodedBitstreamContext *ctx,       \
                                   RWContext *rw, void *cur,         \
                                   LCEVCProcessBlockState *state,    \
                                   int nal_unit_type)                \
{ \
    return FUNC(name)(ctx, rw, cur, state, nal_unit_type); \
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
#define mb(name) \
        xmb(name, current->name)

#define fixed(width, name, value) do { \
        av_unused uint32_t fixed_value = value; \
        xu(width, name, fixed_value, value, value, 0, ); \
    } while (0)


static int cbs_read_multi_byte(CodedBitstreamContext *ctx, GetBitContext *gbc,
                               const char *name, uint32_t *write_to)
{
    uint64_t value;
    uint32_t byte;
    int i;

    CBS_TRACE_READ_START();

    value = 0;
    for (i = 0; i < 10; i++) {
        if (get_bits_left(gbc) < 8) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid multi byte at "
                   "%s: bitstream ended.\n", name);
            return AVERROR_INVALIDDATA;
        }
        byte = get_bits(gbc, 8);
        value = (value << 7) | (byte & 0x7f);
        if (!(byte & 0x80))
            break;
    }

    if (value > UINT32_MAX)
        return AVERROR_INVALIDDATA;

    CBS_TRACE_READ_END_NO_SUBSCRIPTS();

    *write_to = value;
    return 0;
}

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
        CHECK(ff_cbs_read_ue_golomb(ctx, rw, #name, \
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
        CHECK(ff_cbs_read_se_golomb(ctx, rw, #name, \
                                 SUBSCRIPTS(subs, __VA_ARGS__), \
                                 &value, range_min, range_max)); \
        var = value; \
    } while (0)
#define xmb(name, var) do { \
        uint32_t value; \
        CHECK(cbs_read_multi_byte(ctx, rw, #name, &value)); \
        var = value; \
    } while (0)

#define infer(name, value) do { \
        current->name = value; \
    } while (0)

#define more_rbsp_data(var) ((var) = ff_cbs_h2645_read_more_rbsp_data(rw))

#define bit_position(rw)   (get_bits_count(rw))
#define byte_alignment(rw) (get_bits_count(rw) % 8)

/* The CBS LCEVC code uses the refstruct API for the allocation
 * of its child buffers. */
#define allocate(name, size) do { \
        name  = av_refstruct_allocz(size + \
                                        AV_INPUT_BUFFER_PADDING_SIZE); \
        if (!name) \
            return AVERROR(ENOMEM); \
    } while (0)

#define FUNC(name) FUNC_LCEVC(name)
#include "cbs_lcevc_syntax_template.c"
#undef FUNC


#undef READ
#undef READWRITE
#undef RWContext
#undef ub
#undef xu
#undef xi
#undef xue
#undef xse
#undef xmb
#undef infer
#undef more_rbsp_data
#undef bit_position
#undef byte_alignment
#undef allocate


static int cbs_write_multi_byte(CodedBitstreamContext *ctx, PutBitContext *pbc,
                                const char *name, uint32_t value)
{
    int len, i;
    uint8_t byte;

    CBS_TRACE_WRITE_START();

    len = (av_log2(value) + 7) / 7;

    for (i = len - 1; i >= 0; i--) {
        if (put_bits_left(pbc) < 8)
            return AVERROR(ENOSPC);

        byte = value >> (7 * i) & 0x7f;
        if (i > 0)
            byte |= 0x80;

        put_bits(pbc, 8, byte);
    }

    CBS_TRACE_WRITE_END_NO_SUBSCRIPTS();

    return 0;
}

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
        CHECK(ff_cbs_write_ue_golomb(ctx, rw, #name, \
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
        CHECK(ff_cbs_write_se_golomb(ctx, rw, #name, \
                                  SUBSCRIPTS(subs, __VA_ARGS__), \
                                  value, range_min, range_max)); \
    } while (0)
#define xmb(name, var) do { \
        uint32_t value = var; \
        CHECK(cbs_write_multi_byte(ctx, rw, #name, value)); \
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

#define FUNC(name) FUNC_LCEVC(name)
#include "cbs_lcevc_syntax_template.c"
#undef FUNC

#undef WRITE
#undef READWRITE
#undef RWContext
#undef ub
#undef xu
#undef xi
#undef xue
#undef xse
#undef xmb
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


static int cbs_lcevc_split_fragment(CodedBitstreamContext *ctx,
                                    CodedBitstreamFragment *frag,
                                    int header)
{
    enum AVCodecID codec_id = ctx->codec->codec_id;
    CodedBitstreamLCEVCContext *priv = ctx->priv_data;
    CodedBitstreamH2645Context *h2645 = &priv->common;
    GetByteContext gbc;
    int err;

    av_assert0(frag->data && frag->nb_units == 0);
    if (frag->data_size == 0)
        return 0;

    if (header && frag->data[0]) {
        // LVCC header.
        size_t size, start, end;
        int i, j, nb_arrays, nal_unit_type, nb_nals, version;

        h2645->mp4 = 1;

        bytestream2_init(&gbc, frag->data, frag->data_size);

        if (bytestream2_get_bytes_left(&gbc) < 14)
            return AVERROR_INVALIDDATA;

        version = bytestream2_get_byte(&gbc);
        if (version != 1) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid LVCC header: "
                   "first byte %u.\n", version);
            return AVERROR_INVALIDDATA;
        }

        bytestream2_skip(&gbc, 3);
        h2645->nal_length_size = (bytestream2_get_byte(&gbc) >> 6) + 1;

        bytestream2_skip(&gbc, 9);
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

            err = ff_h2645_packet_split(&h2645->read_packet,
                                        frag->data + start, end - start,
                                        ctx->log_ctx, 2, AV_CODEC_ID_LCEVC,
                                        H2645_FLAG_IS_NALFF | H2645_FLAG_SMALL_PADDING | H2645_FLAG_USE_REF);
            if (err < 0) {
                av_log(ctx->log_ctx, AV_LOG_ERROR, "Failed to split "
                       "LVCC array %d (%d NAL units of type %d).\n",
                       i, nb_nals, nal_unit_type);
                return err;
            }
            err = ff_cbs_h2645_fragment_add_nals(ctx, frag, &h2645->read_packet);
            if (err < 0)
                return err;
        }
    } else {
        int flags = (H2645_FLAG_IS_NALFF * !!h2645->mp4) | H2645_FLAG_SMALL_PADDING | H2645_FLAG_USE_REF;
        // Annex B, or later MP4 with already-known parameters.

        err = ff_h2645_packet_split(&h2645->read_packet,
                                    frag->data, frag->data_size,
                                    ctx->log_ctx,
                                    h2645->nal_length_size,
                                    codec_id, flags);
        if (err < 0)
            return err;

        err = ff_cbs_h2645_fragment_add_nals(ctx, frag, &h2645->read_packet);
        if (err < 0)
            return err;
    }

    return 0;
}

static int cbs_lcevc_read_nal_unit(CodedBitstreamContext *ctx,
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
    case LCEVC_NON_IDR_NUT:
    case LCEVC_IDR_NUT:
        {
            LCEVCRawNAL *nal = unit->content;
            LCEVCRawProcessBlockList *block_list;

            err = cbs_lcevc_read_nal(ctx, &gbc, unit->content, unit->type);

            if (err < 0)
                return err;

            block_list = &nal->process_block_list;
            for (int i = 0; i < block_list->nb_blocks; i++) {
                LCEVCRawProcessBlock *block = &block_list->blocks[i];
                LCEVCRawEncodedData *slice;

                if (block->payload_type != LCEVC_PAYLOAD_TYPE_ENCODED_DATA)
                    continue;

                slice = block->payload;
                slice->data_ref = av_buffer_ref(unit->data_ref);
                if (!slice->data_ref)
                     return AVERROR(ENOMEM);
                slice->data = unit->data + slice->header_size;
            }

            if (err < 0)
                return err;
        }
        break;
    default:
        return AVERROR(ENOSYS);
    }

    return 0;
}

static int cbs_lcevc_write_nal_unit(CodedBitstreamContext *ctx,
                                    CodedBitstreamUnit *unit,
                                    PutBitContext *pbc)
{
    int err;

    switch (unit->type) {
    case LCEVC_NON_IDR_NUT:
    case LCEVC_IDR_NUT:
        {
            err = cbs_lcevc_write_nal(ctx, pbc, unit->content, unit->type);

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

static void free_picture_config(AVRefStructOpaque unused, void *obj)
{
    LCEVCRawPictureConfig *picture_config = obj;

    av_refstruct_unref(&picture_config->gc);
}

static void free_encoded_data(AVRefStructOpaque unused, void *obj)
{
    LCEVCRawEncodedData *slice = obj;

    av_buffer_unref(&slice->data_ref);

    av_refstruct_unref(&slice->sc);
    av_refstruct_unref(&slice->gc);
    av_refstruct_unref(&slice->pc);
}

static void free_additional_info(AVRefStructOpaque unused, void *obj)
{
    LCEVCRawAdditionalInfo *additional_info = obj;
    LCEVCRawSEI *sei = &additional_info->sei;
    SEIRawMessage *message = &sei->message;

    av_refstruct_unref(&additional_info->payload_ref);
    av_refstruct_unref(&sei->payload_ref);
    av_refstruct_unref(&message->payload_ref);
    av_refstruct_unref(&message->extension_data);
}

int ff_cbs_lcevc_alloc_process_block_payload(LCEVCRawProcessBlock *block,
                                             const LCEVCProcessBlockTypeDescriptor *desc)
{
    void (*free_func)(AVRefStructOpaque, void*);

    av_assert0(block->payload     == NULL &&
               block->payload_ref == NULL);
    block->payload_type = desc->payload_type;

    if (desc->payload_type == LCEVC_PAYLOAD_TYPE_PICTURE_CONFIG)
        free_func = &free_picture_config;
    else if (desc->payload_type == LCEVC_PAYLOAD_TYPE_ENCODED_DATA)
        free_func = &free_encoded_data;
    else if (desc->payload_type == LCEVC_PAYLOAD_TYPE_ADDITIONAL_INFO)
        free_func = &free_additional_info;
    else
        free_func = NULL;

    block->payload_ref = av_refstruct_alloc_ext(desc->payload_size, 0,
                                                NULL, free_func);
    if (!block->payload_ref)
        return AVERROR(ENOMEM);
    block->payload = block->payload_ref;

    return 0;
}

int ff_cbs_lcevc_list_add(LCEVCRawProcessBlockList *list, int position)
{
    LCEVCRawProcessBlock *blocks;

    if (position == -1)
        position = list->nb_blocks;
    av_assert0(position >= 0 && position <= list->nb_blocks);

    if (list->nb_blocks < list->nb_blocks_allocated) {
        blocks = list->blocks;

        if (position < list->nb_blocks)
            memmove(blocks + position + 1, blocks + position,
                    (list->nb_blocks - position) * sizeof(*blocks));
    } else {
        blocks = av_malloc_array(list->nb_blocks*2 + 1, sizeof(*blocks));
        if (!blocks)
            return AVERROR(ENOMEM);

        list->nb_blocks_allocated = 2*list->nb_blocks_allocated + 1;

        if (position > 0)
            memcpy(blocks, list->blocks, position * sizeof(*blocks));

        if (position < list->nb_blocks)
            memcpy(blocks + position + 1, list->blocks + position,
                   (list->nb_blocks - position) * sizeof(*blocks));

        av_free(list->blocks);
        list->blocks = blocks;
    }

    memset(blocks + position, 0, sizeof(*blocks));

    ++list->nb_blocks;

    return 0;
}

void ff_cbs_lcevc_free_process_block_list(LCEVCRawProcessBlockList *list)
{
    for (int i = 0; i < list->nb_blocks; i++) {
        LCEVCRawProcessBlock *block = &list->blocks[i];
        av_refstruct_unref(&block->payload_ref);
        av_refstruct_unref(&block->extension_data);
    }
    av_free(list->blocks);
}

static int cbs_lcevc_get_process_block_list(CodedBitstreamContext *ctx,
                                            CodedBitstreamUnit *unit,
                                            LCEVCRawProcessBlockList **list)
{
    LCEVCRawNAL *nal = unit->content;
    if (unit->type != LCEVC_NON_IDR_NUT && unit->type != LCEVC_IDR_NUT)
        return AVERROR(EINVAL);
    *list = &nal->process_block_list;

    return 0;
}

int ff_cbs_lcevc_add_process_block(CodedBitstreamContext *ctx,
                                   CodedBitstreamFragment *au,
                                   int position,
                                   uint32_t     payload_type,
                                   void        *payload_data,
                                   void        *payload_ref)
{
    const LCEVCProcessBlockTypeDescriptor *desc;
    CodedBitstreamUnit *unit = NULL;
    LCEVCRawProcessBlockList *list;
    LCEVCRawProcessBlock *block;
    int err;

    desc = ff_cbs_lcevc_process_block_find_type(ctx, payload_type);
    if (!desc)
        return AVERROR(EINVAL);

    for (int i = 0; i < au->nb_units; i++) {
        if (au->units[i].type == LCEVC_NON_IDR_NUT ||
            au->units[i].type == LCEVC_IDR_NUT) {
            unit = &au->units[i];
            break;
        }
    }
    if (!unit)
        return AVERROR(EINVAL);

    // Find the block list inside the codec-dependent unit.
    err = cbs_lcevc_get_process_block_list(ctx, unit, &list);
    if (err < 0)
        return err;

    // Add a new block to the message list.
    err = ff_cbs_lcevc_list_add(list, position);
    if (err < 0)
        return err;

    if (payload_ref) {
        /* The following just increments payload_ref's refcount,
         * so that payload_ref is now owned by us. */
        payload_ref = av_refstruct_ref(payload_ref);
    }

    block = &list->blocks[position];

    block->payload_type = payload_type;
    block->payload      = payload_data;
    block->payload_ref  = payload_ref;

    return 0;
}

int ff_cbs_lcevc_find_process_block(CodedBitstreamContext *ctx,
                                    CodedBitstreamFragment *au,
                                    uint32_t payload_type,
                                    LCEVCRawProcessBlock **iter)
{
    int err, found;

    found = 0;
    for (int i = 0; i < au->nb_units; i++) {
        CodedBitstreamUnit *unit = &au->units[i];
        LCEVCRawProcessBlockList *list;

        err = cbs_lcevc_get_process_block_list(ctx, unit, &list);
        if (err < 0)
            continue;

        for (int j = 0; j < list->nb_blocks; j++) {
            LCEVCRawProcessBlock *block = &list->blocks[j];

            if (block->payload_type == payload_type) {
                if (!*iter || found) {
                    *iter = block;
                    return j;
                }
                if (block == *iter)
                    found = 1;
            }
        }
    }

    return AVERROR(ENOENT);
}

static void cbs_lcevc_delete_process_block(LCEVCRawProcessBlockList *list,
                                           int position)
{
    LCEVCRawProcessBlock *block;

    av_assert0(0 <= position && position < list->nb_blocks);

    block = &list->blocks[position];
    av_refstruct_unref(&block->payload_ref);

    --list->nb_blocks;

    if (list->nb_blocks > 0) {
        memmove(list->blocks + position,
                list->blocks + position + 1,
                (list->nb_blocks - position) * sizeof(*list->blocks));
    }
}

void ff_cbs_lcevc_delete_process_block_type(CodedBitstreamContext *ctx,
                                            CodedBitstreamFragment *au,
                                            uint32_t payload_type)
{
    int err;

    for (int i = 0; i < au->nb_units; i++) {
        CodedBitstreamUnit *unit = &au->units[i];
        LCEVCRawProcessBlockList *list;

        err = cbs_lcevc_get_process_block_list(ctx, unit, &list);
        if (err < 0)
            continue;

        for (int j = list->nb_blocks - 1; j >= 0; j--) {
            if (list->blocks[j].payload_type == payload_type)
                cbs_lcevc_delete_process_block(list, j);
        }
    }
}

static av_cold void cbs_lcevc_flush(CodedBitstreamContext *ctx)
{
    CodedBitstreamLCEVCContext *lcevc = ctx->priv_data;

    av_refstruct_unref(&lcevc->sc);
    av_refstruct_unref(&lcevc->gc);
    av_refstruct_unref(&lcevc->pc);
}

static av_cold void cbs_lcevc_close(CodedBitstreamContext *ctx)
{
    CodedBitstreamLCEVCContext *lcevc = ctx->priv_data;

    cbs_lcevc_flush(ctx);
    ff_h2645_packet_uninit(&lcevc->common.read_packet);
}

static void cbs_lcevc_free_nal(AVRefStructOpaque unused, void *content)
{
    LCEVCRawNAL *nal = content;
    ff_cbs_lcevc_free_process_block_list(&nal->process_block_list);
}

static CodedBitstreamUnitTypeDescriptor cbs_lcevc_unit_types[] = {
    CBS_UNIT_TYPES_COMPLEX((LCEVC_NON_IDR_NUT, LCEVC_IDR_NUT),
                           LCEVCRawNAL, cbs_lcevc_free_nal),

    CBS_UNIT_TYPE_END_OF_LIST
};

// Macro for the read/write pair.
#define LCEVC_PROCESS_BLOCK_RW(codec, name) \
    .read  = cbs_ ## codec ## _read_  ## name ## _internal, \
    .write = cbs_ ## codec ## _write_ ## name ## _internal

static const LCEVCProcessBlockTypeDescriptor cbs_lcevc_process_block_types[] = {
    {
        LCEVC_PAYLOAD_TYPE_SEQUENCE_CONFIG,
        sizeof(LCEVCRawSequenceConfig),
        LCEVC_PROCESS_BLOCK_RW(lcevc, sequence_config),
    },
    {
        LCEVC_PAYLOAD_TYPE_GLOBAL_CONFIG,
        sizeof(LCEVCRawGlobalConfig),
        LCEVC_PROCESS_BLOCK_RW(lcevc, global_config),
    },
    {
        LCEVC_PAYLOAD_TYPE_PICTURE_CONFIG,
        sizeof(LCEVCRawPictureConfig),
        LCEVC_PROCESS_BLOCK_RW(lcevc, picture_config),
    },
    {
        LCEVC_PAYLOAD_TYPE_ENCODED_DATA,
        sizeof(LCEVCRawEncodedData),
        LCEVC_PROCESS_BLOCK_RW(lcevc, encoded_data),
    },
    {
        LCEVC_PAYLOAD_TYPE_ADDITIONAL_INFO,
        sizeof(LCEVCRawAdditionalInfo),
        LCEVC_PROCESS_BLOCK_RW(lcevc, additional_info),
    },
    {
        LCEVC_PAYLOAD_TYPE_FILLER,
        sizeof(LCEVCRawFiller),
        LCEVC_PROCESS_BLOCK_RW(lcevc, filler),
    },
    LCEVC_PROCESS_BLOCK_TYPE_END,
};

const LCEVCProcessBlockTypeDescriptor
    *ff_cbs_lcevc_process_block_find_type(CodedBitstreamContext *ctx,
                                          int payload_type)
{
    for (int i = 0; cbs_lcevc_process_block_types[i].payload_type >= 0; i++) {
        if (cbs_lcevc_process_block_types[i].payload_type == payload_type)
            return &cbs_lcevc_process_block_types[i];
    }

    return NULL;
}

const CodedBitstreamType ff_cbs_type_lcevc = {
    .codec_id          = AV_CODEC_ID_LCEVC,

    .priv_data_size    = sizeof(CodedBitstreamLCEVCContext),

    .unit_types        = cbs_lcevc_unit_types,

    .split_fragment    = &cbs_lcevc_split_fragment,
    .read_unit         = &cbs_lcevc_read_nal_unit,
    .write_unit        = &cbs_lcevc_write_nal_unit,
    .assemble_fragment = &ff_cbs_h2645_assemble_fragment,

    .flush             = &cbs_lcevc_flush,
    .close             = &cbs_lcevc_close,
};
