/*
 * AVPacket functions for libavcodec
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
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

#include "libavutil/avassert.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem.h"
#include "libavutil/rational.h"

#include "defs.h"
#include "packet.h"
#include "packet_internal.h"

#if FF_API_INIT_PACKET
void av_init_packet(AVPacket *pkt)
{
    pkt->pts                  = AV_NOPTS_VALUE;
    pkt->dts                  = AV_NOPTS_VALUE;
    pkt->pos                  = -1;
    pkt->duration             = 0;
    pkt->flags                = 0;
    pkt->stream_index         = 0;
    pkt->buf                  = NULL;
    pkt->side_data            = NULL;
    pkt->side_data_elems      = 0;
    pkt->opaque               = NULL;
    pkt->opaque_ref           = NULL;
    pkt->time_base            = av_make_q(0, 1);
}
#endif

static void get_packet_defaults(AVPacket *pkt)
{
    memset(pkt, 0, sizeof(*pkt));

    pkt->pts             = AV_NOPTS_VALUE;
    pkt->dts             = AV_NOPTS_VALUE;
    pkt->pos             = -1;
    pkt->time_base       = av_make_q(0, 1);
}

AVPacket *av_packet_alloc(void)
{
    AVPacket *pkt = av_malloc(sizeof(AVPacket));
    if (!pkt)
        return pkt;

    get_packet_defaults(pkt);

    return pkt;
}

void av_packet_free(AVPacket **pkt)
{
    if (!pkt || !*pkt)
        return;

    av_packet_unref(*pkt);
    av_freep(pkt);
}

static int packet_alloc(AVBufferRef **buf, int size)
{
    int ret;
    if (size < 0 || size >= INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE)
        return AVERROR(EINVAL);

    ret = av_buffer_realloc(buf, size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (ret < 0)
        return ret;

    memset((*buf)->data + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    return 0;
}

int av_new_packet(AVPacket *pkt, int size)
{
    AVBufferRef *buf = NULL;
    int ret = packet_alloc(&buf, size);
    if (ret < 0)
        return ret;

    get_packet_defaults(pkt);
    pkt->buf      = buf;
    pkt->data     = buf->data;
    pkt->size     = size;

    return 0;
}

void av_shrink_packet(AVPacket *pkt, int size)
{
    if (pkt->size <= size)
        return;
    pkt->size = size;
    memset(pkt->data + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
}

int av_grow_packet(AVPacket *pkt, int grow_by)
{
    int new_size;
    av_assert0((unsigned)pkt->size <= INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE);
    if ((unsigned)grow_by >
        INT_MAX - (pkt->size + AV_INPUT_BUFFER_PADDING_SIZE))
        return AVERROR(ENOMEM);

    new_size = pkt->size + grow_by + AV_INPUT_BUFFER_PADDING_SIZE;
    if (pkt->buf) {
        size_t data_offset;
        uint8_t *old_data = pkt->data;
        if (pkt->data == NULL) {
            data_offset = 0;
            pkt->data = pkt->buf->data;
        } else {
            data_offset = pkt->data - pkt->buf->data;
            if (data_offset > INT_MAX - new_size)
                return AVERROR(ENOMEM);
        }

        if (new_size + data_offset > pkt->buf->size ||
            !av_buffer_is_writable(pkt->buf)) {
            int ret;

            // allocate slightly more than requested to avoid excessive
            // reallocations
            if (new_size + data_offset < INT_MAX - new_size/16)
                new_size += new_size/16;

            ret = av_buffer_realloc(&pkt->buf, new_size + data_offset);
            if (ret < 0) {
                pkt->data = old_data;
                return ret;
            }
            pkt->data = pkt->buf->data + data_offset;
        }
    } else {
        pkt->buf = av_buffer_alloc(new_size);
        if (!pkt->buf)
            return AVERROR(ENOMEM);
        if (pkt->size > 0)
            memcpy(pkt->buf->data, pkt->data, pkt->size);
        pkt->data = pkt->buf->data;
    }
    pkt->size += grow_by;
    memset(pkt->data + pkt->size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    return 0;
}

int av_packet_from_data(AVPacket *pkt, uint8_t *data, int size)
{
    if (size >= INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE)
        return AVERROR(EINVAL);

    pkt->buf = av_buffer_create(data, size + AV_INPUT_BUFFER_PADDING_SIZE,
                                av_buffer_default_free, NULL, 0);
    if (!pkt->buf)
        return AVERROR(ENOMEM);

    pkt->data = data;
    pkt->size = size;

    return 0;
}

void av_packet_free_side_data(AVPacket *pkt)
{
    int i;
    for (i = 0; i < pkt->side_data_elems; i++)
        av_freep(&pkt->side_data[i].data);
    av_freep(&pkt->side_data);
    pkt->side_data_elems = 0;
}

int av_packet_add_side_data(AVPacket *pkt, enum AVPacketSideDataType type,
                            uint8_t *data, size_t size)
{
    AVPacketSideData *tmp;
    int i, elems = pkt->side_data_elems;

    for (i = 0; i < elems; i++) {
        AVPacketSideData *sd = &pkt->side_data[i];

        if (sd->type == type) {
            av_free(sd->data);
            sd->data = data;
            sd->size = size;
            return 0;
        }
    }

    if ((unsigned)elems + 1 > AV_PKT_DATA_NB)
        return AVERROR(ERANGE);

    tmp = av_realloc(pkt->side_data, (elems + 1) * sizeof(*tmp));
    if (!tmp)
        return AVERROR(ENOMEM);

    pkt->side_data = tmp;
    pkt->side_data[elems].data = data;
    pkt->side_data[elems].size = size;
    pkt->side_data[elems].type = type;
    pkt->side_data_elems++;

    return 0;
}


uint8_t *av_packet_new_side_data(AVPacket *pkt, enum AVPacketSideDataType type,
                                 size_t size)
{
    int ret;
    uint8_t *data;

    if (size > SIZE_MAX - AV_INPUT_BUFFER_PADDING_SIZE)
        return NULL;
    data = av_mallocz(size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!data)
        return NULL;

    ret = av_packet_add_side_data(pkt, type, data, size);
    if (ret < 0) {
        av_freep(&data);
        return NULL;
    }

    return data;
}

uint8_t *av_packet_get_side_data(const AVPacket *pkt, enum AVPacketSideDataType type,
                                 size_t *size)
{
    int i;

    for (i = 0; i < pkt->side_data_elems; i++) {
        if (pkt->side_data[i].type == type) {
            if (size)
                *size = pkt->side_data[i].size;
            return pkt->side_data[i].data;
        }
    }
    if (size)
        *size = 0;
    return NULL;
}

const char *av_packet_side_data_name(enum AVPacketSideDataType type)
{
    switch(type) {
    case AV_PKT_DATA_PALETTE:                    return "Palette";
    case AV_PKT_DATA_NEW_EXTRADATA:              return "New Extradata";
    case AV_PKT_DATA_PARAM_CHANGE:               return "Param Change";
    case AV_PKT_DATA_H263_MB_INFO:               return "H263 MB Info";
    case AV_PKT_DATA_REPLAYGAIN:                 return "Replay Gain";
    case AV_PKT_DATA_DISPLAYMATRIX:              return "Display Matrix";
    case AV_PKT_DATA_STEREO3D:                   return "Stereo 3D";
    case AV_PKT_DATA_AUDIO_SERVICE_TYPE:         return "Audio Service Type";
    case AV_PKT_DATA_QUALITY_STATS:              return "Quality stats";
    case AV_PKT_DATA_FALLBACK_TRACK:             return "Fallback track";
    case AV_PKT_DATA_CPB_PROPERTIES:             return "CPB properties";
    case AV_PKT_DATA_SKIP_SAMPLES:               return "Skip Samples";
    case AV_PKT_DATA_JP_DUALMONO:                return "JP Dual Mono";
    case AV_PKT_DATA_STRINGS_METADATA:           return "Strings Metadata";
    case AV_PKT_DATA_SUBTITLE_POSITION:          return "Subtitle Position";
    case AV_PKT_DATA_MATROSKA_BLOCKADDITIONAL:   return "Matroska BlockAdditional";
    case AV_PKT_DATA_WEBVTT_IDENTIFIER:          return "WebVTT ID";
    case AV_PKT_DATA_WEBVTT_SETTINGS:            return "WebVTT Settings";
    case AV_PKT_DATA_METADATA_UPDATE:            return "Metadata Update";
    case AV_PKT_DATA_MPEGTS_STREAM_ID:           return "MPEGTS Stream ID";
    case AV_PKT_DATA_MASTERING_DISPLAY_METADATA: return "Mastering display metadata";
    case AV_PKT_DATA_CONTENT_LIGHT_LEVEL:        return "Content light level metadata";
    case AV_PKT_DATA_SPHERICAL:                  return "Spherical Mapping";
    case AV_PKT_DATA_A53_CC:                     return "A53 Closed Captions";
    case AV_PKT_DATA_ENCRYPTION_INIT_INFO:       return "Encryption initialization data";
    case AV_PKT_DATA_ENCRYPTION_INFO:            return "Encryption info";
    case AV_PKT_DATA_AFD:                        return "Active Format Description data";
    case AV_PKT_DATA_PRFT:                       return "Producer Reference Time";
    case AV_PKT_DATA_ICC_PROFILE:                return "ICC Profile";
    case AV_PKT_DATA_DOVI_CONF:                  return "DOVI configuration record";
    case AV_PKT_DATA_S12M_TIMECODE:              return "SMPTE ST 12-1:2014 timecode";
    case AV_PKT_DATA_DYNAMIC_HDR10_PLUS:         return "HDR10+ Dynamic Metadata (SMPTE 2094-40)";
    }
    return NULL;
}

uint8_t *av_packet_pack_dictionary(AVDictionary *dict, size_t *size)
{
    uint8_t *data = NULL;
    *size = 0;

    if (!dict)
        return NULL;

    for (int pass = 0; pass < 2; pass++) {
        const AVDictionaryEntry *t = NULL;
        size_t total_length = 0;

        while ((t = av_dict_iterate(dict, t))) {
            for (int i = 0; i < 2; i++) {
                const char  *str = i ? t->value : t->key;
                const size_t len = strlen(str) + 1;

                if (pass)
                    memcpy(data + total_length, str, len);
                else if (len > SIZE_MAX - total_length)
                    return NULL;
                total_length += len;
            }
        }
        if (pass)
            break;
        data = av_malloc(total_length);
        if (!data)
            return NULL;
        *size = total_length;
    }

    return data;
}

int av_packet_unpack_dictionary(const uint8_t *data, size_t size,
                                AVDictionary **dict)
{
    const uint8_t *end;
    int ret;

    if (!dict || !data || !size)
        return 0;
    end = data + size;
    if (size && end[-1])
        return AVERROR_INVALIDDATA;
    while (data < end) {
        const uint8_t *key = data;
        const uint8_t *val = data + strlen(key) + 1;

        if (val >= end || !*key)
            return AVERROR_INVALIDDATA;

        ret = av_dict_set(dict, key, val, 0);
        if (ret < 0)
            return ret;
        data = val + strlen(val) + 1;
    }

    return 0;
}

int av_packet_shrink_side_data(AVPacket *pkt, enum AVPacketSideDataType type,
                               size_t size)
{
    int i;

    for (i = 0; i < pkt->side_data_elems; i++) {
        if (pkt->side_data[i].type == type) {
            if (size > pkt->side_data[i].size)
                return AVERROR(ENOMEM);
            pkt->side_data[i].size = size;
            return 0;
        }
    }
    return AVERROR(ENOENT);
}

int av_packet_copy_props(AVPacket *dst, const AVPacket *src)
{
    int i, ret;

    dst->pts                  = src->pts;
    dst->dts                  = src->dts;
    dst->pos                  = src->pos;
    dst->duration             = src->duration;
    dst->flags                = src->flags;
    dst->stream_index         = src->stream_index;
    dst->opaque               = src->opaque;
    dst->time_base            = src->time_base;
    dst->opaque_ref           = NULL;
    dst->side_data            = NULL;
    dst->side_data_elems      = 0;

    ret = av_buffer_replace(&dst->opaque_ref, src->opaque_ref);
    if (ret < 0)
        return ret;

    for (i = 0; i < src->side_data_elems; i++) {
        enum AVPacketSideDataType type = src->side_data[i].type;
        size_t size = src->side_data[i].size;
        uint8_t *src_data = src->side_data[i].data;
        uint8_t *dst_data = av_packet_new_side_data(dst, type, size);

        if (!dst_data) {
            av_buffer_unref(&dst->opaque_ref);
            av_packet_free_side_data(dst);
            return AVERROR(ENOMEM);
        }
        memcpy(dst_data, src_data, size);
    }

    return 0;
}

void av_packet_unref(AVPacket *pkt)
{
    av_packet_free_side_data(pkt);
    av_buffer_unref(&pkt->opaque_ref);
    av_buffer_unref(&pkt->buf);
    get_packet_defaults(pkt);
}

int av_packet_ref(AVPacket *dst, const AVPacket *src)
{
    int ret;

    dst->buf = NULL;

    ret = av_packet_copy_props(dst, src);
    if (ret < 0)
        goto fail;

    if (!src->buf) {
        ret = packet_alloc(&dst->buf, src->size);
        if (ret < 0)
            goto fail;
        av_assert1(!src->size || src->data);
        if (src->size)
            memcpy(dst->buf->data, src->data, src->size);

        dst->data = dst->buf->data;
    } else {
        dst->buf = av_buffer_ref(src->buf);
        if (!dst->buf) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        dst->data = src->data;
    }

    dst->size = src->size;

    return 0;
fail:
    av_packet_unref(dst);
    return ret;
}

AVPacket *av_packet_clone(const AVPacket *src)
{
    AVPacket *ret = av_packet_alloc();

    if (!ret)
        return ret;

    if (av_packet_ref(ret, src))
        av_packet_free(&ret);

    return ret;
}

void av_packet_move_ref(AVPacket *dst, AVPacket *src)
{
    *dst = *src;
    get_packet_defaults(src);
}

int av_packet_make_refcounted(AVPacket *pkt)
{
    int ret;

    if (pkt->buf)
        return 0;

    ret = packet_alloc(&pkt->buf, pkt->size);
    if (ret < 0)
        return ret;
    av_assert1(!pkt->size || pkt->data);
    if (pkt->size)
        memcpy(pkt->buf->data, pkt->data, pkt->size);

    pkt->data = pkt->buf->data;

    return 0;
}

int av_packet_make_writable(AVPacket *pkt)
{
    AVBufferRef *buf = NULL;
    int ret;

    if (pkt->buf && av_buffer_is_writable(pkt->buf))
        return 0;

    ret = packet_alloc(&buf, pkt->size);
    if (ret < 0)
        return ret;
    av_assert1(!pkt->size || pkt->data);
    if (pkt->size)
        memcpy(buf->data, pkt->data, pkt->size);

    av_buffer_unref(&pkt->buf);
    pkt->buf  = buf;
    pkt->data = buf->data;

    return 0;
}

void av_packet_rescale_ts(AVPacket *pkt, AVRational src_tb, AVRational dst_tb)
{
    if (pkt->pts != AV_NOPTS_VALUE)
        pkt->pts = av_rescale_q(pkt->pts, src_tb, dst_tb);
    if (pkt->dts != AV_NOPTS_VALUE)
        pkt->dts = av_rescale_q(pkt->dts, src_tb, dst_tb);
    if (pkt->duration > 0)
        pkt->duration = av_rescale_q(pkt->duration, src_tb, dst_tb);
}

int avpriv_packet_list_put(PacketList *packet_buffer,
                           AVPacket      *pkt,
                           int (*copy)(AVPacket *dst, const AVPacket *src),
                           int flags)
{
    PacketListEntry *pktl = av_malloc(sizeof(*pktl));
    int ret;

    if (!pktl)
        return AVERROR(ENOMEM);

    if (copy) {
        get_packet_defaults(&pktl->pkt);
        ret = copy(&pktl->pkt, pkt);
        if (ret < 0) {
            av_free(pktl);
            return ret;
        }
    } else {
        ret = av_packet_make_refcounted(pkt);
        if (ret < 0) {
            av_free(pktl);
            return ret;
        }
        av_packet_move_ref(&pktl->pkt, pkt);
    }

    pktl->next = NULL;

    if (packet_buffer->head)
        packet_buffer->tail->next = pktl;
    else
        packet_buffer->head = pktl;

    /* Add the packet in the buffered packet list. */
    packet_buffer->tail = pktl;
    return 0;
}

int avpriv_packet_list_get(PacketList *pkt_buffer,
                           AVPacket      *pkt)
{
    PacketListEntry *pktl = pkt_buffer->head;
    if (!pktl)
        return AVERROR(EAGAIN);
    *pkt        = pktl->pkt;
    pkt_buffer->head = pktl->next;
    if (!pkt_buffer->head)
        pkt_buffer->tail = NULL;
    av_freep(&pktl);
    return 0;
}

void avpriv_packet_list_free(PacketList *pkt_buf)
{
    PacketListEntry *tmp = pkt_buf->head;

    while (tmp) {
        PacketListEntry *pktl = tmp;
        tmp = pktl->next;
        av_packet_unref(&pktl->pkt);
        av_freep(&pktl);
    }
    pkt_buf->head = pkt_buf->tail = NULL;
}

int ff_side_data_set_encoder_stats(AVPacket *pkt, int quality, int64_t *error, int error_count, int pict_type)
{
    uint8_t *side_data;
    size_t side_data_size;
    int i;

    side_data = av_packet_get_side_data(pkt, AV_PKT_DATA_QUALITY_STATS, &side_data_size);
    if (!side_data) {
        side_data_size = 4+4+8*error_count;
        side_data = av_packet_new_side_data(pkt, AV_PKT_DATA_QUALITY_STATS,
                                            side_data_size);
    }

    if (!side_data || side_data_size < 4+4+8*error_count)
        return AVERROR(ENOMEM);

    AV_WL32(side_data   , quality  );
    side_data[4] = pict_type;
    side_data[5] = error_count;
    for (i = 0; i<error_count; i++)
        AV_WL64(side_data+8 + 8*i , error[i]);

    return 0;
}

int ff_side_data_set_prft(AVPacket *pkt, int64_t timestamp)
{
    AVProducerReferenceTime *prft;
    uint8_t *side_data;
    size_t side_data_size;

    side_data = av_packet_get_side_data(pkt, AV_PKT_DATA_PRFT, &side_data_size);
    if (!side_data) {
        side_data_size = sizeof(AVProducerReferenceTime);
        side_data = av_packet_new_side_data(pkt, AV_PKT_DATA_PRFT, side_data_size);
    }

    if (!side_data || side_data_size < sizeof(AVProducerReferenceTime))
        return AVERROR(ENOMEM);

    prft = (AVProducerReferenceTime *)side_data;
    prft->wallclock = timestamp;
    prft->flags = 0;

    return 0;
}
