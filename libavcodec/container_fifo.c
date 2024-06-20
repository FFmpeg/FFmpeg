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

#include "libavutil/error.h"
#include "libavutil/fifo.h"
#include "libavutil/frame.h"
#include "libavutil/mem.h"

#include "container_fifo.h"
#include "refstruct.h"

struct ContainerFifo {
    AVFifo             *fifo;
    FFRefStructPool    *pool;

    void*             (*container_alloc)(void);
    void              (*container_reset)(void *obj);
    void              (*container_free) (void *obj);
    int               (*fifo_write)     (void *dst, void *src);
    int               (*fifo_read)      (void *dst, void *src);

};

static int container_fifo_init_entry(FFRefStructOpaque opaque, void *obj)
{
    ContainerFifo *cf = opaque.nc;
    void **pobj = obj;

    *pobj = cf->container_alloc();
    if (!*pobj)
        return AVERROR(ENOMEM);

    return 0;
}

static void container_fifo_reset_entry(FFRefStructOpaque opaque, void *obj)
{
    ContainerFifo *cf = opaque.nc;
    cf->container_reset(*(void**)obj);
}

static void container_fifo_free_entry(FFRefStructOpaque opaque, void *obj)
{
    ContainerFifo *cf = opaque.nc;
    cf->container_free(*(void**)obj);
}

ContainerFifo*
ff_container_fifo_alloc(void* (*container_alloc)(void),
                        void  (*container_reset)(void *obj),
                        void  (*container_free) (void *obj),
                        int   (*fifo_write)     (void *dst, void *src),
                        int   (*fifo_read)      (void *dst, void *src))
{
    ContainerFifo *cf;

    cf = av_mallocz(sizeof(*cf));
    if (!cf)
        return NULL;

    cf->container_alloc = container_alloc;
    cf->container_reset = container_reset;
    cf->container_free  = container_free;
    cf->fifo_write      = fifo_write;
    cf->fifo_read       = fifo_read;

    cf->fifo = av_fifo_alloc2(1, sizeof(void*), AV_FIFO_FLAG_AUTO_GROW);
    if (!cf->fifo)
        goto fail;

    cf->pool = ff_refstruct_pool_alloc_ext(sizeof(void*), 0, cf,
                                           container_fifo_init_entry,
                                           container_fifo_reset_entry,
                                           container_fifo_free_entry,
                                           NULL);
    if (!cf->pool)
        goto fail;

    return cf;
fail:
    ff_container_fifo_free(&cf);
    return NULL;
}

void ff_container_fifo_free(ContainerFifo **pcf)
{
    ContainerFifo *cf;

    if (!*pcf)
        return;

    cf = *pcf;

    if (cf->fifo) {
        void *obj;
        while (av_fifo_read(cf->fifo, &obj, 1) >= 0)
            ff_refstruct_unref(&obj);
        av_fifo_freep2(&cf->fifo);
    }

    ff_refstruct_pool_uninit(&cf->pool);

    av_freep(pcf);
}

int ff_container_fifo_read(ContainerFifo *cf, void *obj)
{
    void **psrc;
    int ret;

    ret = av_fifo_read(cf->fifo, &psrc, 1);
    if (ret < 0)
        return ret;

    ret = cf->fifo_read(obj, *psrc);
    ff_refstruct_unref(&psrc);

    return ret;
}

int ff_container_fifo_write(ContainerFifo *cf, void *obj)
{
    void **pdst;
    int ret;

    pdst = ff_refstruct_pool_get(cf->pool);
    if (!pdst)
        return AVERROR(ENOMEM);

    ret = cf->fifo_write(*pdst, obj);
    if (ret < 0)
        goto fail;

    ret = av_fifo_write(cf->fifo, &pdst, 1);
    if (ret < 0)
        goto fail;

    return 0;
fail:
    ff_refstruct_unref(&pdst);
    return ret;
}

size_t ff_container_fifo_can_read(ContainerFifo *cf)
{
    return av_fifo_can_read(cf->fifo);
}

static void *frame_alloc(void)
{
    return av_frame_alloc();
}

static void frame_reset(void *obj)
{
    av_frame_unref(obj);
}

static void frame_free(void *obj)
{
    AVFrame *frame = obj;
    av_frame_free(&frame);
}

static int frame_ref(void *dst, void *src)
{
    return av_frame_ref(dst, src);
}

static int frame_move_ref(void *dst, void *src)
{
    av_frame_move_ref(dst, src);
    return 0;
}

ContainerFifo *ff_container_fifo_alloc_avframe(unsigned flags)
{
    return ff_container_fifo_alloc(frame_alloc, frame_reset, frame_free,
                                   frame_ref, frame_move_ref);
}
