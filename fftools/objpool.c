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

#include <stdint.h>

#include "libavcodec/packet.h"

#include "libavutil/common.h"
#include "libavutil/error.h"
#include "libavutil/frame.h"
#include "libavutil/mem.h"

#include "objpool.h"

struct ObjPool {
    void        *pool[32];
    unsigned int pool_count;

    ObjPoolCBAlloc alloc;
    ObjPoolCBReset reset;
    ObjPoolCBFree  free;
};

ObjPool *objpool_alloc(ObjPoolCBAlloc cb_alloc, ObjPoolCBReset cb_reset,
                       ObjPoolCBFree cb_free)
{
    ObjPool *op = av_mallocz(sizeof(*op));

    if (!op)
        return NULL;

    op->alloc = cb_alloc;
    op->reset = cb_reset;
    op->free  = cb_free;

    return op;
}

void objpool_free(ObjPool **pop)
{
    ObjPool *op = *pop;

    if (!op)
        return;

    for (unsigned int i = 0; i < op->pool_count; i++)
        op->free(&op->pool[i]);

    av_freep(pop);
}

int  objpool_get(ObjPool *op, void **obj)
{
    if (op->pool_count) {
        *obj = op->pool[--op->pool_count];
        op->pool[op->pool_count] = NULL;
    } else
        *obj = op->alloc();

    return *obj ? 0 : AVERROR(ENOMEM);
}

void objpool_release(ObjPool *op, void **obj)
{
    if (!*obj)
        return;

    op->reset(*obj);

    if (op->pool_count < FF_ARRAY_ELEMS(op->pool))
        op->pool[op->pool_count++] = *obj;
    else
        op->free(obj);

    *obj = NULL;
}

static void *alloc_packet(void)
{
    return av_packet_alloc();
}
static void *alloc_frame(void)
{
    return av_frame_alloc();
}

static void reset_packet(void *obj)
{
    av_packet_unref(obj);
}
static void reset_frame(void *obj)
{
    av_frame_unref(obj);
}

static void free_packet(void **obj)
{
    AVPacket *pkt = *obj;
    av_packet_free(&pkt);
    *obj = NULL;
}
static void free_frame(void **obj)
{
    AVFrame *frame = *obj;
    av_frame_free(&frame);
    *obj = NULL;
}

ObjPool *objpool_alloc_packets(void)
{
    return objpool_alloc(alloc_packet, reset_packet, free_packet);
}
ObjPool *objpool_alloc_frames(void)
{
    return objpool_alloc(alloc_frame, reset_frame, free_frame);
}
