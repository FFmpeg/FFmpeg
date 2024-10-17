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

#include "avassert.h"
#include "container_fifo.h"
#include "error.h"
#include "fifo.h"
#include "frame.h"
#include "mem.h"
#include "refstruct.h"

struct AVContainerFifo {
    AVFifo             *fifo;
    AVRefStructPool    *pool;

    void               *opaque;
    void*             (*container_alloc)(void *opaque);
    void              (*container_reset)(void *opaque, void *obj);
    void              (*container_free) (void *opaque, void *obj);
    int               (*fifo_transfer)  (void *opaque, void *dst, void *src, unsigned flags);

};

static int container_fifo_init_entry(AVRefStructOpaque opaque, void *obj)
{
    AVContainerFifo *cf = opaque.nc;
    void **pobj = obj;

    *pobj = cf->container_alloc(cf->opaque);
    if (!*pobj)
        return AVERROR(ENOMEM);

    return 0;
}

static void container_fifo_reset_entry(AVRefStructOpaque opaque, void *obj)
{
    AVContainerFifo *cf = opaque.nc;
    cf->container_reset(cf->opaque, *(void**)obj);
}

static void container_fifo_free_entry(AVRefStructOpaque opaque, void *obj)
{
    AVContainerFifo *cf = opaque.nc;
    cf->container_free(cf->opaque, *(void**)obj);
}

AVContainerFifo*
av_container_fifo_alloc(void *opaque,
                        void* (*container_alloc)(void *opaque),
                        void  (*container_reset)(void *opaque, void *obj),
                        void  (*container_free) (void *opaque, void *obj),
                        int   (*fifo_transfer)  (void *opaque, void *dst, void *src, unsigned flags),
                        unsigned flags)
{
    AVContainerFifo *cf;

    cf = av_mallocz(sizeof(*cf));
    if (!cf)
        return NULL;

    cf->opaque          = opaque;
    cf->container_alloc = container_alloc;
    cf->container_reset = container_reset;
    cf->container_free  = container_free;
    cf->fifo_transfer   = fifo_transfer;

    cf->fifo = av_fifo_alloc2(1, sizeof(void*), AV_FIFO_FLAG_AUTO_GROW);
    if (!cf->fifo)
        goto fail;

    cf->pool = av_refstruct_pool_alloc_ext(sizeof(void*), 0, cf,
                                           container_fifo_init_entry,
                                           container_fifo_reset_entry,
                                           container_fifo_free_entry,
                                           NULL);
    if (!cf->pool)
        goto fail;

    return cf;
fail:
    av_container_fifo_free(&cf);
    return NULL;
}

void av_container_fifo_free(AVContainerFifo **pcf)
{
    AVContainerFifo *cf;

    if (!*pcf)
        return;

    cf = *pcf;

    if (cf->fifo) {
        void *obj;
        while (av_fifo_read(cf->fifo, &obj, 1) >= 0)
            av_refstruct_unref(&obj);
        av_fifo_freep2(&cf->fifo);
    }

    av_refstruct_pool_uninit(&cf->pool);

    av_freep(pcf);
}

int av_container_fifo_read(AVContainerFifo *cf, void *obj, unsigned flags)
{
    void **psrc;
    int ret;

    ret = av_fifo_read(cf->fifo, &psrc, 1);
    if (ret < 0)
        return ret;

    ret = cf->fifo_transfer(cf->opaque, obj, *psrc, flags);
    av_refstruct_unref(&psrc);

    return ret;
}

int av_container_fifo_peek(AVContainerFifo *cf, void **pdst, size_t offset)
{
    void **pobj;
    int ret;

    ret = av_fifo_peek(cf->fifo, &pobj, 1, offset);
    if (ret < 0)
        return ret;

    *pdst = *pobj;

    return 0;
}

void av_container_fifo_drain(AVContainerFifo *cf, size_t nb_elems)
{
    av_assert0(nb_elems <= av_fifo_can_read(cf->fifo));
    while (nb_elems--) {
        void **pobj;
        int ret = av_fifo_read(cf->fifo, &pobj, 1);
        av_assert0(ret >= 0);
        av_refstruct_unref(&pobj);
    }
}

int av_container_fifo_write(AVContainerFifo *cf, void *obj, unsigned flags)
{
    void **pdst;
    int ret;

    pdst = av_refstruct_pool_get(cf->pool);
    if (!pdst)
        return AVERROR(ENOMEM);

    ret = cf->fifo_transfer(cf->opaque, *pdst, obj, flags);
    if (ret < 0)
        goto fail;

    ret = av_fifo_write(cf->fifo, &pdst, 1);
    if (ret < 0)
        goto fail;

    return 0;
fail:
    av_refstruct_unref(&pdst);
    return ret;
}

size_t av_container_fifo_can_read(const AVContainerFifo *cf)
{
    return av_fifo_can_read(cf->fifo);
}

static void *frame_alloc(void *opaque)
{
    return av_frame_alloc();
}

static void frame_reset(void *opaque, void *obj)
{
    av_frame_unref(obj);
}

static void frame_free(void *opaque, void *obj)
{
    AVFrame *frame = obj;
    av_frame_free(&frame);
}

static int frame_transfer(void *opaque, void *dst, void *src, unsigned flags)
{
    if (flags & AV_CONTAINER_FIFO_FLAG_REF)
        return av_frame_ref(dst, src);

    av_frame_move_ref(dst, src);
    return 0;
}

AVContainerFifo *av_container_fifo_alloc_avframe(unsigned flags)
{
    return av_container_fifo_alloc(NULL, frame_alloc, frame_reset, frame_free,
                                   frame_transfer, 0);
}
