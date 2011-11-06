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

#include "avcodec.h"
#include "libavutil/avassert.h"
#include "bytestream.h"

void av_destruct_packet_nofree(AVPacket *pkt)
{
    pkt->data = NULL; pkt->size = 0;
    pkt->side_data       = NULL;
    pkt->side_data_elems = 0;
}

void av_destruct_packet(AVPacket *pkt)
{
    int i;

    av_free(pkt->data);
    pkt->data = NULL; pkt->size = 0;

    for (i = 0; i < pkt->side_data_elems; i++)
        av_free(pkt->side_data[i].data);
    av_freep(&pkt->side_data);
    pkt->side_data_elems = 0;
}

void av_init_packet(AVPacket *pkt)
{
    pkt->pts   = AV_NOPTS_VALUE;
    pkt->dts   = AV_NOPTS_VALUE;
    pkt->pos   = -1;
    pkt->duration = 0;
    pkt->convergence_duration = 0;
    pkt->flags = 0;
    pkt->stream_index = 0;
    pkt->destruct= NULL;
    pkt->side_data       = NULL;
    pkt->side_data_elems = 0;
}

int av_new_packet(AVPacket *pkt, int size)
{
    uint8_t *data= NULL;
    if((unsigned)size < (unsigned)size + FF_INPUT_BUFFER_PADDING_SIZE)
        data = av_malloc(size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (data){
        memset(data + size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
    }else
        size=0;

    av_init_packet(pkt);
    pkt->data = data;
    pkt->size = size;
    pkt->destruct = av_destruct_packet;
    if(!data)
        return AVERROR(ENOMEM);
    return 0;
}

void av_shrink_packet(AVPacket *pkt, int size)
{
    if (pkt->size <= size) return;
    pkt->size = size;
    memset(pkt->data + size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
}

int av_grow_packet(AVPacket *pkt, int grow_by)
{
    void *new_ptr;
    av_assert0((unsigned)pkt->size <= INT_MAX - FF_INPUT_BUFFER_PADDING_SIZE);
    if (!pkt->size)
        return av_new_packet(pkt, grow_by);
    if ((unsigned)grow_by > INT_MAX - (pkt->size + FF_INPUT_BUFFER_PADDING_SIZE))
        return -1;
    new_ptr = av_realloc(pkt->data, pkt->size + grow_by + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!new_ptr)
        return AVERROR(ENOMEM);
    pkt->data = new_ptr;
    pkt->size += grow_by;
    memset(pkt->data + pkt->size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
    return 0;
}

#define DUP_DATA(dst, src, size, padding) \
    do { \
        void *data; \
        if (padding) { \
            if ((unsigned)(size) > (unsigned)(size) + FF_INPUT_BUFFER_PADDING_SIZE) \
                goto failed_alloc; \
            data = av_malloc(size + FF_INPUT_BUFFER_PADDING_SIZE); \
        } else { \
            data = av_malloc(size); \
        } \
        if (!data) \
            goto failed_alloc; \
        memcpy(data, src, size); \
        if (padding) \
            memset((uint8_t*)data + size, 0, FF_INPUT_BUFFER_PADDING_SIZE); \
        dst = data; \
    } while(0)

int av_dup_packet(AVPacket *pkt)
{
    AVPacket tmp_pkt;

    if (((pkt->destruct == av_destruct_packet_nofree) || (pkt->destruct == NULL)) && pkt->data) {
        tmp_pkt = *pkt;

        pkt->data      = NULL;
        pkt->side_data = NULL;
        DUP_DATA(pkt->data, tmp_pkt.data, pkt->size, 1);
        pkt->destruct = av_destruct_packet;

        if (pkt->side_data_elems) {
            int i;

            DUP_DATA(pkt->side_data, tmp_pkt.side_data,
                     pkt->side_data_elems * sizeof(*pkt->side_data), 0);
            memset(pkt->side_data, 0, pkt->side_data_elems * sizeof(*pkt->side_data));
            for (i = 0; i < pkt->side_data_elems; i++) {
                DUP_DATA(pkt->side_data[i].data, tmp_pkt.side_data[i].data,
                         pkt->side_data[i].size, 1);
            }
        }
    }
    return 0;
failed_alloc:
    av_destruct_packet(pkt);
    return AVERROR(ENOMEM);
}

void av_free_packet(AVPacket *pkt)
{
    if (pkt) {
        if (pkt->destruct) pkt->destruct(pkt);
        pkt->data = NULL; pkt->size = 0;
        pkt->side_data       = NULL;
        pkt->side_data_elems = 0;
    }
}

uint8_t* av_packet_new_side_data(AVPacket *pkt, enum AVPacketSideDataType type,
                                 int size)
{
    int elems = pkt->side_data_elems;

    if ((unsigned)elems + 1 > INT_MAX / sizeof(*pkt->side_data))
        return NULL;
    if ((unsigned)size > INT_MAX - FF_INPUT_BUFFER_PADDING_SIZE)
        return NULL;

    pkt->side_data = av_realloc(pkt->side_data, (elems + 1) * sizeof(*pkt->side_data));
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

uint8_t* av_packet_get_side_data(AVPacket *pkt, enum AVPacketSideDataType type,
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

#define FF_MERGE_MARKER 0x8c4d9d108e25e9feULL

int av_packet_merge_side_data(AVPacket *pkt){
    if(pkt->side_data_elems){
        int i;
        uint8_t *p;
        uint64_t size= pkt->size + 8LL + FF_INPUT_BUFFER_PADDING_SIZE;
        AVPacket old= *pkt;
        for (i=0; i<old.side_data_elems; i++) {
            size += old.side_data[i].size + 5LL;
        }
        if (size > INT_MAX)
            return AVERROR(EINVAL);
        p = av_malloc(size);
        if (!p)
            return AVERROR(ENOMEM);
        pkt->data = p;
        pkt->destruct = av_destruct_packet;
        pkt->size = size - FF_INPUT_BUFFER_PADDING_SIZE;
        bytestream_put_buffer(&p, old.data, old.size);
        for (i=old.side_data_elems-1; i>=0; i--) {
            bytestream_put_buffer(&p, old.side_data[i].data, old.side_data[i].size);
            bytestream_put_be32(&p, old.side_data[i].size);
            *p++ = old.side_data[i].type | ((i==old.side_data_elems-1)*128);
        }
        bytestream_put_be64(&p, FF_MERGE_MARKER);
        av_assert0(p-pkt->data == pkt->size);
        memset(p, 0, FF_INPUT_BUFFER_PADDING_SIZE);
        av_free_packet(&old);
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

        av_dup_packet(pkt);

        p = pkt->data + pkt->size - 8 - 5;
        for (i=1; ; i++){
            size = AV_RB32(p);
            if (size>INT_MAX || p - pkt->data <= size)
                return 0;
            if (p[4]&128)
                break;
            p-= size+5;
        }

        pkt->side_data = av_malloc(i * sizeof(*pkt->side_data));
        if (!pkt->side_data)
            return AVERROR(ENOMEM);

        p= pkt->data + pkt->size - 8 - 5;
        for (i=0; ; i++){
            size= AV_RB32(p);
            av_assert0(size<=INT_MAX && p - pkt->data > size);
            pkt->side_data[i].data = av_malloc(size + FF_INPUT_BUFFER_PADDING_SIZE);
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
