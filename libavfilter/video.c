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
                                          int w, int h, enum PixelFormat format)
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

int ff_null_start_frame(AVFilterLink *link, AVFilterBufferRef *picref)
{
    AVFilterBufferRef *buf_out = avfilter_ref_buffer(picref, ~0);
    if (!buf_out)
        return AVERROR(ENOMEM);
    return ff_start_frame(link->dst->outputs[0], buf_out);
}

// for filters that support (but don't require) outpic==inpic
int ff_inplace_start_frame(AVFilterLink *inlink, AVFilterBufferRef *inpicref)
{
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFilterBufferRef *outpicref = NULL, *for_next_filter;
    int ret = 0;

    if (inpicref->perms & AV_PERM_WRITE) {
        outpicref = avfilter_ref_buffer(inpicref, ~0);
        if (!outpicref)
            return AVERROR(ENOMEM);
    } else {
        outpicref = ff_get_video_buffer(outlink, AV_PERM_WRITE, outlink->w, outlink->h);
        if (!outpicref)
            return AVERROR(ENOMEM);

        avfilter_copy_buffer_ref_props(outpicref, inpicref);
        outpicref->video->w = outlink->w;
        outpicref->video->h = outlink->h;
    }

    for_next_filter = avfilter_ref_buffer(outpicref, ~0);
    if (for_next_filter)
        ret = ff_start_frame(outlink, for_next_filter);
    else
        ret = AVERROR(ENOMEM);

    if (ret < 0) {
        avfilter_unref_bufferp(&outpicref);
        return ret;
    }

    outlink->out_buf = outpicref;
    return 0;
}

static int default_start_frame(AVFilterLink *inlink, AVFilterBufferRef *picref)
{
    AVFilterLink *outlink = NULL;

    if (inlink->dst->nb_outputs)
        outlink = inlink->dst->outputs[0];

    if (outlink) {
        AVFilterBufferRef *buf_out;
        outlink->out_buf = ff_get_video_buffer(outlink, AV_PERM_WRITE, outlink->w, outlink->h);
        if (!outlink->out_buf)
            return AVERROR(ENOMEM);

        avfilter_copy_buffer_ref_props(outlink->out_buf, picref);
        outlink->out_buf->video->w = outlink->w;
        outlink->out_buf->video->h = outlink->h;
        buf_out = avfilter_ref_buffer(outlink->out_buf, ~0);
        if (!buf_out)
            return AVERROR(ENOMEM);

        return ff_start_frame(outlink, buf_out);
    }
    return 0;
}

static void clear_link(AVFilterLink *link)
{
    avfilter_unref_bufferp(&link->cur_buf);
    avfilter_unref_bufferp(&link->src_buf);
    avfilter_unref_bufferp(&link->out_buf);
    link->cur_buf_copy = NULL; /* we do not own the reference */
}

/* XXX: should we do the duplicating of the picture ref here, instead of
 * forcing the source filter to do it? */
int ff_start_frame(AVFilterLink *link, AVFilterBufferRef *picref)
{
    int (*start_frame)(AVFilterLink *, AVFilterBufferRef *);
    AVFilterPad *src = link->srcpad;
    AVFilterPad *dst = link->dstpad;
    int ret, perms;
    AVFilterCommand *cmd= link->dst->command_queue;
    int64_t pts;

    FF_TPRINTF_START(NULL, start_frame); ff_tlog_link(NULL, link, 0); ff_tlog(NULL, " "); ff_tlog_ref(NULL, picref, 1);

    av_assert1(picref->format                     == link->format);
    av_assert1(picref->video->w                   == link->w);
    av_assert1(picref->video->h                   == link->h);

    if (link->closed) {
        avfilter_unref_buffer(picref);
        return AVERROR_EOF;
    }

    if (!(start_frame = dst->start_frame))
        start_frame = default_start_frame;

    av_assert1((picref->perms & src->min_perms) == src->min_perms);
    picref->perms &= ~ src->rej_perms;
    perms = picref->perms;

    if (picref->linesize[0] < 0)
        perms |= AV_PERM_NEG_LINESIZES;
    /* prepare to copy the picture if it has insufficient permissions */
    if ((dst->min_perms & perms) != dst->min_perms || dst->rej_perms & perms) {
        av_log(link->dst, AV_LOG_DEBUG,
                "frame copy needed (have perms %x, need %x, reject %x)\n",
                picref->perms,
                link->dstpad->min_perms, link->dstpad->rej_perms);

        link->cur_buf = ff_get_video_buffer(link, dst->min_perms, link->w, link->h);
        if (!link->cur_buf) {
            avfilter_unref_bufferp(&picref);
            return AVERROR(ENOMEM);
        }

        link->src_buf = picref;
        avfilter_copy_buffer_ref_props(link->cur_buf, link->src_buf);

        /* copy palette if required */
        if (av_pix_fmt_descriptors[link->format].flags & PIX_FMT_PAL)
            memcpy(link->cur_buf->data[1], link->src_buf-> data[1], AVPALETTE_SIZE);
    }
    else
        link->cur_buf = picref;

    link->cur_buf_copy = link->cur_buf;

    while(cmd && cmd->time <= picref->pts * av_q2d(link->time_base)){
        av_log(link->dst, AV_LOG_DEBUG,
               "Processing command time:%f command:%s arg:%s\n",
               cmd->time, cmd->command, cmd->arg);
        avfilter_process_command(link->dst, cmd->command, cmd->arg, 0, 0, cmd->flags);
        ff_command_queue_pop(link->dst);
        cmd= link->dst->command_queue;
    }
    pts = link->cur_buf->pts;
    ret = start_frame(link, link->cur_buf);
    ff_update_link_current_pts(link, pts);
    if (ret < 0)
        clear_link(link);
    else
        /* incoming buffers must not be freed in start frame,
           because they can still be in use by the automatic copy mechanism */
        av_assert1(link->cur_buf_copy->buf->refcount > 0);

    return ret;
}

int ff_null_end_frame(AVFilterLink *link)
{
    return ff_end_frame(link->dst->outputs[0]);
}

static int default_end_frame(AVFilterLink *inlink)
{
    AVFilterLink *outlink = NULL;

    if (inlink->dst->nb_outputs)
        outlink = inlink->dst->outputs[0];

    if (outlink) {
        return ff_end_frame(outlink);
    }
    return 0;
}

int ff_end_frame(AVFilterLink *link)
{
    int (*end_frame)(AVFilterLink *);
    int ret;

    if (!(end_frame = link->dstpad->end_frame))
        end_frame = default_end_frame;

    ret = end_frame(link);

    clear_link(link);

    return ret;
}

int ff_null_draw_slice(AVFilterLink *link, int y, int h, int slice_dir)
{
    return ff_draw_slice(link->dst->outputs[0], y, h, slice_dir);
}

static int default_draw_slice(AVFilterLink *inlink, int y, int h, int slice_dir)
{
    AVFilterLink *outlink = NULL;

    if (inlink->dst->nb_outputs)
        outlink = inlink->dst->outputs[0];

    if (outlink)
        return ff_draw_slice(outlink, y, h, slice_dir);
    return 0;
}

int ff_draw_slice(AVFilterLink *link, int y, int h, int slice_dir)
{
    uint8_t *src[4], *dst[4];
    int i, j, vsub, ret;
    int (*draw_slice)(AVFilterLink *, int, int, int);

    FF_TPRINTF_START(NULL, draw_slice); ff_tlog_link(NULL, link, 0); ff_tlog(NULL, " y:%d h:%d dir:%d\n", y, h, slice_dir);

    /* copy the slice if needed for permission reasons */
    if (link->src_buf) {
        vsub = av_pix_fmt_descriptors[link->format].log2_chroma_h;

        for (i = 0; i < 4; i++) {
            if (link->src_buf->data[i]) {
                src[i] = link->src_buf-> data[i] +
                    (y >> (i==1 || i==2 ? vsub : 0)) * link->src_buf-> linesize[i];
                dst[i] = link->cur_buf_copy->data[i] +
                    (y >> (i==1 || i==2 ? vsub : 0)) * link->cur_buf_copy->linesize[i];
            } else
                src[i] = dst[i] = NULL;
        }

        for (i = 0; i < 4; i++) {
            int planew =
                av_image_get_linesize(link->format, link->cur_buf_copy->video->w, i);

            if (!src[i]) continue;

            for (j = 0; j < h >> (i==1 || i==2 ? vsub : 0); j++) {
                memcpy(dst[i], src[i], planew);
                src[i] += link->src_buf->linesize[i];
                dst[i] += link->cur_buf_copy->linesize[i];
            }
        }
    }

    if (!(draw_slice = link->dstpad->draw_slice))
        draw_slice = default_draw_slice;
    ret = draw_slice(link, y, h, slice_dir);
    if (ret < 0)
        clear_link(link);
    else
        /* incoming buffers must not be freed in start frame,
           because they can still be in use by the automatic copy mechanism */
        av_assert1(link->cur_buf_copy->buf->refcount > 0);
    return ret;
}
