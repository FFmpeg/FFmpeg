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
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem.h"
#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"

void av_init_packet(AVPacket *pkt)
{
    pkt->pts                  = AV_NOPTS_VALUE;
    pkt->dts                  = AV_NOPTS_VALUE;
    pkt->pos                  = -1;
    pkt->duration             = 0;
#if FF_API_CONVERGENCE_DURATION
FF_DISABLE_DEPRECATION_WARNINGS
    pkt->convergence_duration = 0;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    pkt->flags                = 0;
    pkt->stream_index         = 0;
    pkt->buf                  = NULL;
    pkt->side_data            = NULL;
    pkt->side_data_elems      = 0;
}

AVPacket *av_packet_alloc(void)
{
    AVPacket *pkt = av_mallocz(sizeof(AVPacket));
    if (!pkt)
        return pkt;

    av_init_packet(pkt);

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

    av_init_packet(pkt);
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

        if (new_size + data_offset > pkt->buf->size) {
            int ret = av_buffer_realloc(&pkt->buf, new_size + data_offset);
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

#if FF_API_AVPACKET_OLD_API
FF_DISABLE_DEPRECATION_WARNINGS
#define ALLOC_MALLOC(data, size) data = av_malloc(size)
#define ALLOC_BUF(data, size)                \
do {                                         \
    av_buffer_realloc(&pkt->buf, size);      \
    data = pkt->buf ? pkt->buf->data : NULL; \
} while (0)

#define DUP_DATA(dst, src, size, padding, ALLOC)                        \
    do {                                                                \
        void *data;                                                     \
        if (padding) {                                                  \
            if ((unsigned)(size) >                                      \
                (unsigned)(size) + AV_INPUT_BUFFER_PADDING_SIZE)        \
                goto failed_alloc;                                      \
            ALLOC(data, size + AV_INPUT_BUFFER_PADDING_SIZE);           \
        } else {                                                        \
            ALLOC(data, size);                                          \
        }                                                               \
        if (!data)                                                      \
            goto failed_alloc;                                          \
        memcpy(data, src, size);                                        \
        if (padding)                                                    \
            memset((uint8_t *)data + size, 0,                           \
                   AV_INPUT_BUFFER_PADDING_SIZE);                       \
        dst = data;                                                     \
    } while (0)

/* Makes duplicates of data, side_data, but does not copy any other fields */
static int copy_packet_data(AVPacket *pkt, const AVPacket *src, int dup)
{
    pkt->data      = NULL;
    pkt->side_data = NULL;
    pkt->side_data_elems = 0;
    if (pkt->buf) {
        AVBufferRef *ref = av_buffer_ref(src->buf);
        if (!ref)
            return AVERROR(ENOMEM);
        pkt->buf  = ref;
        pkt->data = ref->data;
    } else {
        DUP_DATA(pkt->data, src->data, pkt->size, 1, ALLOC_BUF);
    }
    if (src->side_data_elems && dup) {
        pkt->side_data = src->side_data;
        pkt->side_data_elems = src->side_data_elems;
    }
    if (src->side_data_elems && !dup) {
        return av_copy_packet_side_data(pkt, src);
    }
    return 0;

failed_alloc:
    av_packet_unref(pkt);
    return AVERROR(ENOMEM);
}

int av_copy_packet_side_data(AVPacket *pkt, const AVPacket *src)
{
    if (src->side_data_elems) {
        int i;
        DUP_DATA(pkt->side_data, src->side_data,
                src->side_data_elems * sizeof(*src->side_data), 0, ALLOC_MALLOC);
        if (src != pkt) {
            memset(pkt->side_data, 0,
                   src->side_data_elems * sizeof(*src->side_data));
        }
        for (i = 0; i < src->side_data_elems; i++) {
            DUP_DATA(pkt->side_data[i].data, src->side_data[i].data,
                    src->side_data[i].size, 1, ALLOC_MALLOC);
            pkt->side_data[i].size = src->side_data[i].size;
            pkt->side_data[i].type = src->side_data[i].type;
        }
    }
    pkt->side_data_elems = src->side_data_elems;
    return 0;

failed_alloc:
    av_packet_unref(pkt);
    return AVERROR(ENOMEM);
}

int av_dup_packet(AVPacket *pkt)
{
    AVPacket tmp_pkt;

    if (!pkt->buf && pkt->data) {
        tmp_pkt = *pkt;
        return copy_packet_data(pkt, &tmp_pkt, 1);
    }
    return 0;
}

int av_copy_packet(AVPacket *dst, const AVPacket *src)
{
    *dst = *src;
    return copy_packet_data(dst, src, 0);
}
FF_ENABLE_DEPRECATION_WARNINGS
#endif

void av_packet_free_side_data(AVPacket *pkt)
{
    int i;
    for (i = 0; i < pkt->side_data_elems; i++)
        av_freep(&pkt->side_data[i].data);
    av_freep(&pkt->side_data);
    pkt->side_data_elems = 0;
}

#if FF_API_AVPACKET_OLD_API
FF_DISABLE_DEPRECATION_WARNINGS
void av_free_packet(AVPacket *pkt)
{
    if (pkt) {
        if (pkt->buf)
            av_buffer_unref(&pkt->buf);
        pkt->data            = NULL;
        pkt->size            = 0;

        av_packet_free_side_data(pkt);
    }
}
FF_ENABLE_DEPRECATION_WARNINGS
#endif

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
                                 int size)
{
    int ret;
    uint8_t *data;

    if ((unsigned)size > INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE)
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
                                 int *size)
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
    }
    return NULL;
}

#if FF_API_MERGE_SD_API

#define FF_MERGE_MARKER 0x8c4d9d108e25e9feULL

int av_packet_merge_side_data(AVPacket *pkt){
    if(pkt->side_data_elems){
        AVBufferRef *buf;
        int i;
        uint8_t *p;
        uint64_t size= pkt->size + 8LL + AV_INPUT_BUFFER_PADDING_SIZE;
        AVPacket old= *pkt;
        for (i=0; i<old.side_data_elems; i++) {
            size += old.side_data[i].size + 5LL;
        }
        if (size > INT_MAX)
            return AVERROR(EINVAL);
        buf = av_buffer_alloc(size);
        if (!buf)
            return AVERROR(ENOMEM);
        pkt->buf = buf;
        pkt->data = p = buf->data;
        pkt->size = size - AV_INPUT_BUFFER_PADDING_SIZE;
        bytestream_put_buffer(&p, old.data, old.size);
        for (i=old.side_data_elems-1; i>=0; i--) {
            bytestream_put_buffer(&p, old.side_data[i].data, old.side_data[i].size);
            bytestream_put_be32(&p, old.side_data[i].size);
            *p++ = old.side_data[i].type | ((i==old.side_data_elems-1)*128);
        }
        bytestream_put_be64(&p, FF_MERGE_MARKER);
        av_assert0(p-pkt->data == pkt->size);
        memset(p, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        av_packet_unref(&old);
        pkt->side_data_elems = 0;
        pkt->side_data = NULL;
        return 1;
    }
    return 0;
}

int av_packet_split_side_data(AVPacket *pkt){
    if (!pkt->side_data_elems && pkt->size >12 && AV_RB64(pkt->data + pkt->size - 8) == FF_MERGE_MARKER){
        int i;
        unsigned int size;
        uint8_t *p;

        p = pkt->data + pkt->size - 8 - 5;
        for (i=1; ; i++){
            size = AV_RB32(p);
            if (size>INT_MAX - 5 || p - pkt->data < size)
                return 0;
            if (p[4]&128)
                break;
            if (p - pkt->data < size + 5)
                return 0;
            p-= size+5;
        }

        if (i > AV_PKT_DATA_NB)
            return AVERROR(ERANGE);

        pkt->side_data = av_malloc_array(i, sizeof(*pkt->side_data));
        if (!pkt->side_data)
            return AVERROR(ENOMEM);

        p= pkt->data + pkt->size - 8 - 5;
        for (i=0; ; i++){
            size= AV_RB32(p);
            av_assert0(size<=INT_MAX - 5 && p - pkt->data >= size);
            pkt->side_data[i].data = av_mallocz(size + AV_INPUT_BUFFER_PADDING_SIZE);
            pkt->side_data[i].size = size;
            pkt->side_data[i].type = p[4]&127;
            if (!pkt->side_data[i].data)
                return AVERROR(ENOMEM);
            memcpy(pkt->side_data[i].data, p-size, size);
            pkt->size -= size + 5;
            if(p[4]&128)
                break;
            p-= size+5;
        }
        pkt->size -= 8;
        pkt->side_data_elems = i+1;
        return 1;
    }
    return 0;
}
#endif

uint8_t *av_packet_pack_dictionary(AVDictionary *dict, int *size)
{
    AVDictionaryEntry *t = NULL;
    uint8_t *data = NULL;
    *size = 0;

    if (!dict)
        return NULL;

    while ((t = av_dict_get(dict, "", t, AV_DICT_IGNORE_SUFFIX))) {
        const size_t keylen   = strlen(t->key);
        const size_t valuelen = strlen(t->value);
        const size_t new_size = *size + keylen + 1 + valuelen + 1;
        uint8_t *const new_data = av_realloc(data, new_size);

        if (!new_data)
            goto fail;
        data = new_data;
        if (new_size > INT_MAX)
            goto fail;

        memcpy(data + *size, t->key, keylen + 1);
        memcpy(data + *size + keylen + 1, t->value, valuelen + 1);

        *size = new_size;
    }

    return data;

fail:
    av_freep(&data);
    *size = 0;
    return NULL;
}

int av_packet_unpack_dictionary(const uint8_t *data, int size, AVDictionary **dict)
{
    const uint8_t *end;
    int ret = 0;

    if (!dict || !data || !size)
        return ret;
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
            break;
        data = val + strlen(val) + 1;
    }

    return ret;
}

int av_packet_shrink_side_data(AVPacket *pkt, enum AVPacketSideDataType type,
                               int size)
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
    int i;

    dst->pts                  = src->pts;
    dst->dts                  = src->dts;
    dst->pos                  = src->pos;
    dst->duration             = src->duration;
#if FF_API_CONVERGENCE_DURATION
FF_DISABLE_DEPRECATION_WARNINGS
    dst->convergence_duration = src->convergence_duration;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    dst->flags                = src->flags;
    dst->stream_index         = src->stream_index;

    dst->side_data            = NULL;
    dst->side_data_elems      = 0;
    for (i = 0; i < src->side_data_elems; i++) {
        enum AVPacketSideDataType type = src->side_data[i].type;
        int size          = src->side_data[i].size;
        uint8_t *src_data = src->side_data[i].data;
        uint8_t *dst_data = av_packet_new_side_data(dst, type, size);

        if (!dst_data) {
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
    av_buffer_unref(&pkt->buf);
    av_init_packet(pkt);
    pkt->data = NULL;
    pkt->size = 0;
}

int av_packet_ref(AVPacket *dst, const AVPacket *src)
{
    int ret;

    ret = av_packet_copy_props(dst, src);
    if (ret < 0)
        return ret;

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
    av_packet_free_side_data(dst);
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
    av_init_packet(src);
    src->data = NULL;
    src->size = 0;
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
#if FF_API_CONVERGENCE_DURATION
FF_DISABLE_DEPRECATION_WARNINGS
    if (pkt->convergence_duration > 0)
        pkt->convergence_duration = av_rescale_q(pkt->convergence_duration, src_tb, dst_tb);
FF_ENABLE_DEPRECATION_WARNINGS
#endif
}

int ff_side_data_set_encoder_stats(AVPacket *pkt, int quality, int64_t *error, int error_count, int pict_type)
{
    uint8_t *side_data;
    int side_data_size;
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
