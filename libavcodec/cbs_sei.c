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
#include "cbs.h"
#include "cbs_internal.h"
#include "cbs_h264.h"
#include "cbs_h265.h"
#include "cbs_h266.h"
#include "cbs_sei.h"
#include "libavutil/refstruct.h"

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
#define FUNC_NAME2_EXPORT(rw, codec, name) ff_cbs_ ## codec ## _ ## rw ## _ ## name
#define FUNC_NAME1_EXPORT(rw, codec, name) FUNC_NAME2_EXPORT(rw, codec, name)
#define FUNC_SEI(name)  FUNC_NAME1(READWRITE, sei,  name)
#define FUNC_SEI_EXPORT(name)  FUNC_NAME1_EXPORT(READWRITE, sei,  name)

#define SEI_FUNC(name, args) \
static int FUNC_SEI(name) args;  \
static int FUNC_SEI(name ## _internal)(CodedBitstreamContext *ctx, \
                                       RWContext *rw, void *cur,   \
                                       SEIMessageState *state)     \
{ \
    return FUNC_SEI(name)(ctx, rw, cur, state); \
} \
static int FUNC_SEI(name) args

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


#define infer(name, value) do { \
        current->name = value; \
    } while (0)

#define more_rbsp_data(var) ((var) = ff_cbs_h2645_read_more_rbsp_data(rw))

#define bit_position(rw)   (get_bits_count(rw))
#define byte_alignment(rw) (get_bits_count(rw) % 8)

/* The CBS SEI code uses the refstruct API for the allocation
 * of its child buffers. */
#define allocate(name, size) do { \
        name  = av_refstruct_allocz(size + \
                                        AV_INPUT_BUFFER_PADDING_SIZE); \
        if (!name) \
            return AVERROR(ENOMEM); \
    } while (0)

#define FUNC(name) FUNC_SEI_EXPORT(name)
#include "cbs_sei_syntax_template.c"
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

#define FUNC(name) FUNC_SEI_EXPORT(name)
#include "cbs_sei_syntax_template.c"
#undef FUNC

static void cbs_free_user_data_registered(AVRefStructOpaque unused, void *obj)
{
    SEIRawUserDataRegistered *udr = obj;
    av_refstruct_unref(&udr->data_ref);
}

static void cbs_free_user_data_unregistered(AVRefStructOpaque unused, void *obj)
{
    SEIRawUserDataUnregistered *udu = obj;
    av_refstruct_unref(&udu->data_ref);
}

int ff_cbs_sei_alloc_message_payload(SEIRawMessage *message,
                                     const SEIMessageTypeDescriptor *desc)
{
    void (*free_func)(AVRefStructOpaque, void*);

    av_assert0(message->payload     == NULL &&
               message->payload_ref == NULL);
    message->payload_type = desc->type;

    if (desc->type == SEI_TYPE_USER_DATA_REGISTERED_ITU_T_T35)
        free_func = &cbs_free_user_data_registered;
    else if (desc->type == SEI_TYPE_USER_DATA_UNREGISTERED)
        free_func = &cbs_free_user_data_unregistered;
    else {
        free_func = NULL;
    }

    message->payload_ref = av_refstruct_alloc_ext(desc->size, 0,
                                                  NULL, free_func);
    if (!message->payload_ref)
        return AVERROR(ENOMEM);
    message->payload = message->payload_ref;

    return 0;
}

int ff_cbs_sei_list_add(SEIRawMessageList *list)
{
    void *ptr;
    int old_count = list->nb_messages_allocated;

    av_assert0(list->nb_messages <= old_count);
    if (list->nb_messages + 1 > old_count) {
        int new_count = 2 * old_count + 1;

        ptr = av_realloc_array(list->messages,
                               new_count, sizeof(*list->messages));
        if (!ptr)
            return AVERROR(ENOMEM);

        list->messages = ptr;
        list->nb_messages_allocated = new_count;

        // Zero the newly-added entries.
        memset(list->messages + old_count, 0,
               (new_count - old_count) * sizeof(*list->messages));
    }
    ++list->nb_messages;
    return 0;
}

void ff_cbs_sei_free_message_list(SEIRawMessageList *list)
{
    for (int i = 0; i < list->nb_messages; i++) {
        SEIRawMessage *message = &list->messages[i];
        av_refstruct_unref(&message->payload_ref);
        av_refstruct_unref(&message->extension_data);
    }
    av_free(list->messages);
}

static int cbs_sei_get_unit(CodedBitstreamContext *ctx,
                            CodedBitstreamFragment *au,
                            int prefix,
                            CodedBitstreamUnit **sei_unit)
{
    CodedBitstreamUnit *unit;
    int sei_type, highest_vcl_type, err, i, position;

    switch (ctx->codec->codec_id) {
    case AV_CODEC_ID_H264:
        // (We can ignore auxiliary slices because we only have prefix
        // SEI in H.264 and an auxiliary picture must always follow a
        // primary picture.)
        highest_vcl_type = H264_NAL_IDR_SLICE;
        if (prefix)
            sei_type = H264_NAL_SEI;
        else
            return AVERROR(EINVAL);
        break;
    case AV_CODEC_ID_H265:
        highest_vcl_type = HEVC_NAL_RSV_VCL31;
        if (prefix)
            sei_type = HEVC_NAL_SEI_PREFIX;
        else
            sei_type = HEVC_NAL_SEI_SUFFIX;
        break;
    case AV_CODEC_ID_H266:
        highest_vcl_type = VVC_RSV_IRAP_11;
        if (prefix)
            sei_type = VVC_PREFIX_SEI_NUT;
        else
            sei_type = VVC_SUFFIX_SEI_NUT;
        break;
    default:
        return AVERROR(EINVAL);
    }

    // Find an existing SEI NAL unit of the right type.
    unit = NULL;
    for (i = 0; i < au->nb_units; i++) {
        if (au->units[i].type == sei_type) {
            unit = &au->units[i];
            break;
        }
    }

    if (unit) {
        *sei_unit = unit;
        return 0;
    }

    // Need to add a new SEI NAL unit ...
    if (prefix) {
        // ... before the first VCL NAL unit.
        for (i = 0; i < au->nb_units; i++) {
            if (au->units[i].type < highest_vcl_type)
                break;
        }
        position = i;
    } else {
        // ... after the last VCL NAL unit.
        for (i = au->nb_units - 1; i >= 0; i--) {
            if (au->units[i].type < highest_vcl_type)
                break;
        }
        if (i < 0) {
            // No VCL units; just put it at the end.
            position = au->nb_units;
        } else {
            position = i + 1;
        }
    }

    err = ff_cbs_insert_unit_content(au, position, sei_type,
                                     NULL, NULL);
    if (err < 0)
        return err;
    unit = &au->units[position];
    unit->type = sei_type;

    err = ff_cbs_alloc_unit_content(ctx, unit);
    if (err < 0)
        return err;

    switch (ctx->codec->codec_id) {
    case AV_CODEC_ID_H264:
        {
            H264RawSEI sei = {
                .nal_unit_header = {
                    .nal_ref_idc   = 0,
                    .nal_unit_type = sei_type,
                },
            };
            memcpy(unit->content, &sei, sizeof(sei));
        }
        break;
    case AV_CODEC_ID_H265:
        {
            H265RawSEI sei = {
                .nal_unit_header = {
                    .nal_unit_type         = sei_type,
                    .nuh_layer_id          = 0,
                    .nuh_temporal_id_plus1 = 1,
                },
            };
            memcpy(unit->content, &sei, sizeof(sei));
        }
        break;
    case AV_CODEC_ID_H266:
        {
            H266RawSEI sei = {
                .nal_unit_header = {
                    .nal_unit_type         = sei_type,
                    .nuh_layer_id          = 0,
                    .nuh_temporal_id_plus1 = 1,
                },
            };
            memcpy(unit->content, &sei, sizeof(sei));
        }
        break;
    default:
        av_assert0(0);
    }

    *sei_unit = unit;
    return 0;
}

static int cbs_sei_get_message_list(CodedBitstreamContext *ctx,
                                    CodedBitstreamUnit *unit,
                                    SEIRawMessageList **list)
{
    switch (ctx->codec->codec_id) {
    case AV_CODEC_ID_H264:
        {
            H264RawSEI *sei = unit->content;
            if (unit->type != H264_NAL_SEI)
                return AVERROR(EINVAL);
            *list = &sei->message_list;
        }
        break;
    case AV_CODEC_ID_H265:
        {
            H265RawSEI *sei = unit->content;
            if (unit->type != HEVC_NAL_SEI_PREFIX &&
                unit->type != HEVC_NAL_SEI_SUFFIX)
                return AVERROR(EINVAL);
            *list = &sei->message_list;
        }
        break;
    case AV_CODEC_ID_H266:
        {
            H266RawSEI *sei = unit->content;
            if (unit->type != VVC_PREFIX_SEI_NUT &&
                unit->type != VVC_SUFFIX_SEI_NUT)
                return AVERROR(EINVAL);
            *list = &sei->message_list;
        }
        break;
    default:
        return AVERROR(EINVAL);
    }

    return 0;
}

int ff_cbs_sei_add_message(CodedBitstreamContext *ctx,
                           CodedBitstreamFragment *au,
                           int prefix,
                           uint32_t     payload_type,
                           void        *payload_data,
                           void        *payload_ref)
{
    const SEIMessageTypeDescriptor *desc;
    CodedBitstreamUnit *unit;
    SEIRawMessageList *list;
    SEIRawMessage *message;
    int err;

    desc = ff_cbs_sei_find_type(ctx, payload_type);
    if (!desc)
        return AVERROR(EINVAL);

    // Find an existing SEI unit or make a new one to add to.
    err = cbs_sei_get_unit(ctx, au, prefix, &unit);
    if (err < 0)
        return err;

    // Find the message list inside the codec-dependent unit.
    err = cbs_sei_get_message_list(ctx, unit, &list);
    if (err < 0)
        return err;

    // Add a new message to the message list.
    err = ff_cbs_sei_list_add(list);
    if (err < 0)
        return err;

    if (payload_ref) {
        /* The following just increments payload_ref's refcount,
         * so that payload_ref is now owned by us. */
        payload_ref = av_refstruct_ref(payload_ref);
    }

    message = &list->messages[list->nb_messages - 1];

    message->payload_type = payload_type;
    message->payload      = payload_data;
    message->payload_ref  = payload_ref;

    return 0;
}

int ff_cbs_sei_find_message(CodedBitstreamContext *ctx,
                            CodedBitstreamFragment *au,
                            uint32_t payload_type,
                            SEIRawMessage **iter)
{
    int err, i, j, found;

    found = 0;
    for (i = 0; i < au->nb_units; i++) {
        CodedBitstreamUnit *unit = &au->units[i];
        SEIRawMessageList *list;

        err = cbs_sei_get_message_list(ctx, unit, &list);
        if (err < 0)
            continue;

        for (j = 0; j < list->nb_messages; j++) {
            SEIRawMessage *message = &list->messages[j];

            if (message->payload_type == payload_type) {
                if (!*iter || found) {
                    *iter = message;
                    return 0;
                }
                if (message == *iter)
                    found = 1;
            }
        }
    }

    return AVERROR(ENOENT);
}

static void cbs_sei_delete_message(SEIRawMessageList *list,
                                   int position)
{
    SEIRawMessage *message;

    av_assert0(0 <= position && position < list->nb_messages);

    message = &list->messages[position];
    av_refstruct_unref(&message->payload_ref);
    av_refstruct_unref(&message->extension_data);

    --list->nb_messages;

    if (list->nb_messages > 0) {
        memmove(list->messages + position,
                list->messages + position + 1,
                (list->nb_messages - position) * sizeof(*list->messages));
    }
}

void ff_cbs_sei_delete_message_type(CodedBitstreamContext *ctx,
                                    CodedBitstreamFragment *au,
                                    uint32_t payload_type)
{
    int err, i, j;

    for (i = 0; i < au->nb_units; i++) {
        CodedBitstreamUnit *unit = &au->units[i];
        SEIRawMessageList *list;

        err = cbs_sei_get_message_list(ctx, unit, &list);
        if (err < 0)
            continue;

        for (j = list->nb_messages - 1; j >= 0; j--) {
            if (list->messages[j].payload_type == payload_type)
                cbs_sei_delete_message(list, j);
        }
    }
}

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

static const SEIMessageTypeDescriptor cbs_sei_h274_types[] = {
    {
        SEI_TYPE_FILM_GRAIN_CHARACTERISTICS,
        1, 0,
        sizeof(SEIRawFilmGrainCharacteristics),
        SEI_MESSAGE_RW(sei, film_grain_characteristics),
    },
    {
        SEI_TYPE_DISPLAY_ORIENTATION,
        1, 0,
        sizeof(SEIRawDisplayOrientation),
        SEI_MESSAGE_RW(sei, display_orientation)
    },
    {
        SEI_TYPE_FRAME_FIELD_INFO,
        1, 0,
        sizeof(SEIRawFrameFieldInformation),
        SEI_MESSAGE_RW(sei, frame_field_information)
    },
    SEI_MESSAGE_TYPE_END,
};

const SEIMessageTypeDescriptor *ff_cbs_sei_find_type(CodedBitstreamContext *ctx,
                                                     int payload_type)
{
    const SEIMessageTypeDescriptor *codec_list = NULL;
    int i;

    switch (ctx->codec->codec_id) {
#if CBS_H264
    case AV_CODEC_ID_H264:
        codec_list = ff_cbs_sei_h264_types;
        break;
#endif
#if CBS_H265
    case AV_CODEC_ID_H265:
        codec_list = ff_cbs_sei_h265_types;
        break;
#endif
    case AV_CODEC_ID_H266:
        codec_list = cbs_sei_h274_types;
        break;
    }

    for (i = 0; codec_list && codec_list[i].type >= 0; i++) {
        if (codec_list[i].type == payload_type)
            return &codec_list[i];
    }

    for (i = 0; cbs_sei_common_types[i].type >= 0; i++) {
        if (cbs_sei_common_types[i].type == payload_type)
            return &cbs_sei_common_types[i];
    }

    return NULL;
}
