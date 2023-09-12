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

#include "cbs.h"
#include "cbs_internal.h"
#include "cbs_h264.h"
#include "cbs_h265.h"
#include "cbs_h266.h"
#include "cbs_sei.h"
#include "refstruct.h"

static void cbs_free_user_data_registered(FFRefStructOpaque unused, void *obj)
{
    SEIRawUserDataRegistered *udr = obj;
    ff_refstruct_unref(&udr->data);
}

static void cbs_free_user_data_unregistered(FFRefStructOpaque unused, void *obj)
{
    SEIRawUserDataUnregistered *udu = obj;
    ff_refstruct_unref(&udu->data);
}

int ff_cbs_sei_alloc_message_payload(SEIRawMessage *message,
                                     const SEIMessageTypeDescriptor *desc)
{
    void (*free_func)(FFRefStructOpaque, void*);
    unsigned flags = 0;

    av_assert0(message->payload     == NULL &&
               message->payload_ref == NULL);
    message->payload_type = desc->type;

    if (desc->type == SEI_TYPE_USER_DATA_REGISTERED_ITU_T_T35)
        free_func = &cbs_free_user_data_registered;
    else if (desc->type == SEI_TYPE_USER_DATA_UNREGISTERED)
        free_func = &cbs_free_user_data_unregistered;
    else {
        free_func = NULL;
        flags = FF_REFSTRUCT_FLAG_NO_ZEROING;
    }

    message->payload_ref = ff_refstruct_alloc_ext(desc->size, flags,
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
        ff_refstruct_unref(&message->payload_ref);
        ff_refstruct_unref(&message->extension_data);
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
        payload_ref = ff_refstruct_ref(payload_ref);
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
    ff_refstruct_unref(&message->payload_ref);
    ff_refstruct_unref(&message->extension_data);

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
