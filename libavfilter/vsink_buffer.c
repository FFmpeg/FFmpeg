/*
 * Copyright (c) 2011 Stefano Sabatini
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

/**
 * @file
 * buffer video sink
 */

#include "libavutil/fifo.h"
#include "avfilter.h"
#include "vsink_buffer.h"

typedef struct {
    AVFifoBuffer *fifo;          ///< FIFO buffer of video frame references
    enum PixelFormat *pix_fmts;  ///< accepted pixel formats, must be terminated with -1
} BufferSinkContext;

#define FIFO_INIT_SIZE 8

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    BufferSinkContext *buf = ctx->priv;

    if (!opaque) {
        av_log(ctx, AV_LOG_ERROR, "No opaque field provided, which is required.\n");
        return AVERROR(EINVAL);
    }

    buf->fifo = av_fifo_alloc(FIFO_INIT_SIZE*sizeof(AVFilterBufferRef *));
    if (!buf->fifo) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate fifo\n");
        return AVERROR(ENOMEM);
    }

    buf->pix_fmts = opaque;
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    BufferSinkContext *buf = ctx->priv;
    AVFilterBufferRef *picref;

    if (buf->fifo) {
        while (av_fifo_size(buf->fifo) >= sizeof(AVFilterBufferRef *)) {
            av_fifo_generic_read(buf->fifo, &picref, sizeof(picref), NULL);
            avfilter_unref_buffer(picref);
        }
        av_fifo_free(buf->fifo);
        buf->fifo = NULL;
    }
}

static void end_frame(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    BufferSinkContext *buf = inlink->dst->priv;

    if (av_fifo_space(buf->fifo) < sizeof(AVFilterBufferRef *)) {
        /* realloc fifo size */
        if (av_fifo_realloc2(buf->fifo, av_fifo_size(buf->fifo) * 2) < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Cannot buffer more frames. Consume some available frames "
                   "before adding new ones.\n");
            return;
        }
    }

    /* cache frame */
    av_fifo_generic_write(buf->fifo,
                          &inlink->cur_buf, sizeof(AVFilterBufferRef *), NULL);
}

static int query_formats(AVFilterContext *ctx)
{
    BufferSinkContext *buf = ctx->priv;

    avfilter_set_common_pixel_formats(ctx, avfilter_make_format_list(buf->pix_fmts));
    return 0;
}

int av_vsink_buffer_get_video_buffer_ref(AVFilterContext *ctx,
                                         AVFilterBufferRef **picref, int flags)
{
    BufferSinkContext *buf = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    int ret;
    *picref = NULL;

    /* no picref available, fetch it from the filterchain */
    if (!av_fifo_size(buf->fifo)) {
        if ((ret = avfilter_request_frame(inlink)) < 0)
            return ret;
    }

    if (!av_fifo_size(buf->fifo))
        return AVERROR(EINVAL);

    if (flags & AV_VSINK_BUF_FLAG_PEEK)
        *picref = (AVFilterBufferRef *)av_fifo_peek2(buf->fifo, 0);
    else
        av_fifo_generic_read(buf->fifo, picref, sizeof(*picref), NULL);

    return 0;
}

AVFilter avfilter_vsink_buffersink = {
    .name      = "buffersink",
    .description = NULL_IF_CONFIG_SMALL("Buffer video frames, and make them available to the end of the filter graph."),
    .priv_size = sizeof(BufferSinkContext),
    .init      = init,
    .uninit    = uninit,

    .query_formats = query_formats,

    .inputs    = (AVFilterPad[]) {{ .name          = "default",
                                    .type          = AVMEDIA_TYPE_VIDEO,
                                    .end_frame     = end_frame,
                                    .min_perms     = AV_PERM_READ, },
                                  { .name = NULL }},
    .outputs   = (AVFilterPad[]) {{ .name = NULL }},
};
