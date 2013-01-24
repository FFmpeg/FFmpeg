/*
 * Copyright 2007 Bobby Bingham
 * Copyright Stefano Sabatini <stefasab gmail com>
 * Copyright Vitor Sessak <vitor1001 gmail com>
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
#include <stdio.h>

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"

#include "avfilter.h"
#include "internal.h"
#include "video.h"

AVFilterBufferRef *ff_null_get_video_buffer(AVFilterLink *link, int perms, int w, int h)
{
    return ff_get_video_buffer(link->dst->outputs[0], perms, w, h);
}

AVFilterBufferRef *ff_default_get_video_buffer(AVFilterLink *link, int perms, int w, int h)
{
    int linesize[4];
    uint8_t *data[4];
    int i;
    AVFilterBufferRef *picref = NULL;
    AVFilterPool *pool = link->pool;
    int full_perms = AV_PERM_READ | AV_PERM_WRITE | AV_PERM_PRESERVE |
                     AV_PERM_REUSE | AV_PERM_REUSE2 | AV_PERM_ALIGN;

    av_assert1(!(perms & ~(full_perms | AV_PERM_NEG_LINESIZES)));

    if (pool) {
        for (i = 0; i < POOL_SIZE; i++) {
            picref = pool->pic[i];
            if (picref && picref->buf->format == link->format && picref->buf->w == w && picref->buf->h == h) {
                AVFilterBuffer *pic = picref->buf;
                pool->pic[i] = NULL;
                pool->count--;
                av_assert0(!picref->video->qp_table);
                picref->video->w = w;
                picref->video->h = h;
                picref->perms = full_perms;
                picref->format = link->format;
                pic->refcount = 1;
                memcpy(picref->data,     pic->data,     sizeof(picref->data));
                memcpy(picref->linesize, pic->linesize, sizeof(picref->linesize));
                pool->refcount++;
                return picref;
            }
        }
    } else {
        pool = link->pool = av_mallocz(sizeof(AVFilterPool));
        pool->refcount = 1;
    }

    // align: +2 is needed for swscaler, +16 to be SIMD-friendly
    if ((i = av_image_alloc(data, linesize, w, h, link->format, 32)) < 0)
        return NULL;

    picref = avfilter_get_video_buffer_ref_from_arrays(data, linesize,
                                                       full_perms, w, h, link->format);
    if (!picref) {
        av_free(data[0]);
        return NULL;
    }

    memset(data[0], 128, i);

    picref->buf->priv = pool;
    picref->buf->free = NULL;
    pool->refcount++;

    return picref;
}

AVFilterBufferRef *
avfilter_get_video_buffer_ref_from_arrays(uint8_t * const data[4], const int linesize[4], int perms,
                                          int w, int h, enum AVPixelFormat format)
{
    AVFilterBuffer *pic = av_mallocz(sizeof(AVFilterBuffer));
    AVFilterBufferRef *picref = av_mallocz(sizeof(AVFilterBufferRef));

    if (!pic || !picref)
        goto fail;

    picref->buf = pic;
    picref->buf->free = ff_avfilter_default_free_buffer;
    if (!(picref->video = av_mallocz(sizeof(AVFilterBufferRefVideoProps))))
        goto fail;

    pic->w = picref->video->w = w;
    pic->h = picref->video->h = h;

    /* make sure the buffer gets read permission or it's useless for output */
    picref->perms = perms | AV_PERM_READ;

    pic->refcount = 1;
    picref->type = AVMEDIA_TYPE_VIDEO;
    pic->format = picref->format = format;

    memcpy(pic->data,        data,          4*sizeof(data[0]));
    memcpy(pic->linesize,    linesize,      4*sizeof(linesize[0]));
    memcpy(picref->data,     pic->data,     sizeof(picref->data));
    memcpy(picref->linesize, pic->linesize, sizeof(picref->linesize));

    pic->   extended_data = pic->data;
    picref->extended_data = picref->data;

    picref->pts = AV_NOPTS_VALUE;

    return picref;

fail:
    if (picref && picref->video)
        av_free(picref->video);
    av_free(picref);
    av_free(pic);
    return NULL;
}

AVFilterBufferRef *ff_get_video_buffer(AVFilterLink *link, int perms, int w, int h)
{
    AVFilterBufferRef *ret = NULL;

    av_unused char buf[16];
    FF_TPRINTF_START(NULL, get_video_buffer); ff_tlog_link(NULL, link, 0);
    ff_tlog(NULL, " perms:%s w:%d h:%d\n", ff_get_ref_perms_string(buf, sizeof(buf), perms), w, h);

    if (link->dstpad->get_video_buffer)
        ret = link->dstpad->get_video_buffer(link, perms, w, h);

    if (!ret)
        ret = ff_default_get_video_buffer(link, perms, w, h);

    if (ret)
        ret->type = AVMEDIA_TYPE_VIDEO;

    FF_TPRINTF_START(NULL, get_video_buffer); ff_tlog_link(NULL, link, 0); ff_tlog(NULL, " returning "); ff_tlog_ref(NULL, ret, 1);

    return ret;
}
