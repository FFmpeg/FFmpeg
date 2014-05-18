/*
 * AVPacket functions for libavcodec
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <string.h>

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "avcodec.h"
#if FF_API_DESTRUCT_PACKET

void av_destruct_packet(AVPacket *pkt)
{
    av_free(pkt->data);
    pkt->data = NULL;
    pkt->size = 0;
}

/* a dummy destruct callback for the callers that assume AVPacket.destruct ==
 * NULL => static data */
static void dummy_destruct_packet(AVPacket *pkt)
{
    av_assert0(0);
}
#endif

void av_init_packet(AVPacket *pkt)
{
    pkt->pts                  = AV_NOPTS_VALUE;
    pkt->dts                  = AV_NOPTS_VALUE;
    pkt->pos                  = -1;
    pkt->duration             = 0;
    pkt->convergence_duration = 0;
    pkt->flags                = 0;
    pkt->stream_index         = 0;
#if FF_API_DESTRUCT_PACKET
FF_DISABLE_DEPRECATION_WARNINGS
    pkt->destruct             = NULL;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    pkt->buf                  = NULL;
    pkt->side_data            = NULL;
    pkt->side_data_elems      = 0;
}

static int packet_alloc(AVBufferRef **buf, int size)
{
    int ret;
    if ((unsigned)size >= (unsigned)size + FF_INPUT_BUFFER_PADDING_SIZE)
        return AVERROR(EINVAL);

    ret = av_buffer_realloc(buf, size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (ret < 0)
        return ret;

    memset((*buf)->data + size, 0, FF_INPUT_BUFFER_PADDING_SIZE);

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
#if FF_API_DESTRUCT_PACKET
FF_DISABLE_DEPRECATION_WARNINGS
    pkt->destruct = dummy_destruct_packet;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    return 0;
}

void av_shrink_packet(AVPacket *pkt, int size)
{
    if (pkt->size <= size)
        return;
    pkt->size = size;
    memset(pkt->data + size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
}

int av_grow_packet(AVPacket *pkt, int grow_by)
{
    int new_size;
    av_assert0((unsigned)pkt->size <= INT_MAX - FF_INPUT_BUFFER_PADDING_SIZE);
    if (!pkt->size)
        return av_new_packet(pkt, grow_by);
    if ((unsigned)grow_by >
        INT_MAX - (pkt->size + FF_INPUT_BUFFER_PADDING_SIZE))
        return -1;

    new_size = pkt->size + grow_by + FF_INPUT_BUFFER_PADDING_SIZE;
    if (pkt->buf) {
        int ret = av_buffer_realloc(&pkt->buf, new_size);
        if (ret < 0)
            return ret;
    } else {
        pkt->buf = av_buffer_alloc(new_size);
        if (!pkt->buf)
            return AVERROR(ENOMEM);
        memcpy(pkt->buf->data, pkt->data, FFMIN(pkt->size, pkt->size + grow_by));
#if FF_API_DESTRUCT_PACKET
FF_DISABLE_DEPRECATION_WARNINGS
        pkt->destruct = dummy_destruct_packet;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    }
    pkt->data  = pkt->buf->data;
    pkt->size += grow_by;
    memset(pkt->data + pkt->size, 0, FF_INPUT_BUFFER_PADDING_SIZE);

    return 0;
}

int av_packet_from_data(AVPacket *pkt, uint8_t *data, int size)
{
    if (size >= INT_MAX - FF_INPUT_BUFFER_PADDING_SIZE)
        return AVERROR(EINVAL);

    pkt->buf = av_buffer_create(data, size + FF_INPUT_BUFFER_PADDING_SIZE,
                                av_buffer_default_free, NULL, 0);
    if (!pkt->buf)
        return AVERROR(ENOMEM);

    pkt->data = data;
    pkt->size = size;
#if FF_API_DESTRUCT_PACKET
FF_DISABLE_DEPRECATION_WARNINGS
    pkt->destruct = dummy_destruct_packet;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    return 0;
}

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
                (unsigned)(size) + FF_INPUT_BUFFER_PADDING_SIZE)        \
                goto failed_alloc;                                      \
            ALLOC(data, size + FF_INPUT_BUFFER_PADDING_SIZE);           \
        } else {                                                        \
            ALLOC(data, size);                                          \
        }                                                               \
        if (!data)                                                      \
            goto failed_alloc;                                          \
        memcpy(data, src, size);                                        \
        if (padding)                                                    \
            memset((uint8_t *)data + size, 0,                           \
                   FF_INPUT_BUFFER_PADDING_SIZE);                       \
        dst = data;                                                     \
    } while (0)

int av_dup_packet(AVPacket *pkt)
{
    AVPacket tmp_pkt;

FF_DISABLE_DEPRECATION_WARNINGS
    if (!pkt->buf && pkt->data
#if FF_API_DESTRUCT_PACKET
        && !pkt->destruct
#endif
        ) {
FF_ENABLE_DEPRECATION_WARNINGS
        tmp_pkt = *pkt;

        pkt->data      = NULL;
        pkt->side_data = NULL;
        DUP_DATA(pkt->data, tmp_pkt.data, pkt->size, 1, ALLOC_BUF);
#if FF_API_DESTRUCT_PACKET
FF_DISABLE_DEPRECATION_WARNINGS
        pkt->destruct = dummy_destruct_packet;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

        if (pkt->side_data_elems) {
            int i;

            DUP_DATA(pkt->side_data, tmp_pkt.side_data,
                     pkt->side_data_elems * sizeof(*pkt->side_data), 0, ALLOC_MALLOC);
            memset(pkt->side_data, 0,
                   pkt->side_data_elems * sizeof(*pkt->side_data));
            for (i = 0; i < pkt->side_data_elems; i++) {
                DUP_DATA(pkt->side_data[i].data, tmp_pkt.side_data[i].data,
                         tmp_pkt.side_data[i].size, 1, ALLOC_MALLOC);
                pkt->side_data[i].size = tmp_pkt.side_data[i].size;
                pkt->side_data[i].type = tmp_pkt.side_data[i].type;
            }
        }
    }
    return 0;

failed_alloc:
    av_free_packet(pkt);
    return AVERROR(ENOMEM);
}

void av_packet_free_side_data(AVPacket *pkt)
{
    int i;
    for (i = 0; i < pkt->side_data_elems; i++)
        av_free(pkt->side_data[i].data);
    av_freep(&pkt->side_data);
    pkt->side_data_elems = 0;
}

void av_free_packet(AVPacket *pkt)
{
    if (pkt) {
FF_DISABLE_DEPRECATION_WARNINGS
        if (pkt->buf)
            av_buffer_unref(&pkt->buf);
#if FF_API_DESTRUCT_PACKET
        else if (pkt->destruct)
            pkt->destruct(pkt);
        pkt->destruct = NULL;
#endif
FF_ENABLE_DEPRECATION_WARNINGS
        pkt->data            = NULL;
        pkt->size            = 0;

        av_packet_free_side_data(pkt);
    }
}

uint8_t *av_packet_new_side_data(AVPacket *pkt, enum AVPacketSideDataType type,
                                 int size)
{
    int elems = pkt->side_data_elems;

    if ((unsigned)elems + 1 > INT_MAX / sizeof(*pkt->side_data))
        return NULL;
    if ((unsigned)size > INT_MAX - FF_INPUT_BUFFER_PADDING_SIZE)
        return NULL;

    pkt->side_data = av_realloc(pkt->side_data,
                                (elems + 1) * sizeof(*pkt->side_data));
    if (!pkt->side_data)
        return NULL;

    pkt->side_data[elems].data = av_malloc(size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!pkt->side_data[elems].data)
        return NULL;
    pkt->side_data[elems].size = size;
    pkt->side_data[elems].type = type;
    pkt->side_data_elems++;

    return pkt->side_data[elems].data;
}

uint8_t *av_packet_get_side_data(AVPacket *pkt, enum AVPacketSideDataType type,
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
    return NULL;
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
    dst->convergence_duration = src->convergence_duration;
    dst->flags                = src->flags;
    dst->stream_index         = src->stream_index;

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

int av_packet_ref(AVPacket *dst, AVPacket *src)
{
    int ret;

    ret = av_packet_copy_props(dst, src);
    if (ret < 0)
        return ret;

    if (!src->buf) {
        ret = packet_alloc(&dst->buf, src->size);
        if (ret < 0)
            goto fail;
        memcpy(dst->buf->data, src->data, src->size);
    } else
        dst->buf = av_buffer_ref(src->buf);

    dst->size = src->size;
    dst->data = dst->buf->data;
    return 0;
fail:
    av_packet_free_side_data(dst);
    return ret;
}

void av_packet_move_ref(AVPacket *dst, AVPacket *src)
{
    *dst = *src;
    av_init_packet(src);
}
