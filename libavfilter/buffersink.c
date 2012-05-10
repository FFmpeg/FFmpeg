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
 * buffer sink
 */

#include "libavutil/fifo.h"

#include "avfilter.h"
#include "buffersink.h"

typedef struct {
    AVFifoBuffer *fifo;          ///< FIFO buffer of video frame references
} BufferSinkContext;

#define FIFO_INIT_SIZE 8

static av_cold void uninit(AVFilterContext *ctx)
{
    BufferSinkContext *sink = ctx->priv;

    while (sink->fifo && av_fifo_size(sink->fifo)) {
        AVFilterBufferRef *buf;
        av_fifo_generic_read(sink->fifo, &buf, sizeof(buf), NULL);
        avfilter_unref_buffer(buf);
    }
    av_fifo_free(sink->fifo);
}

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    BufferSinkContext *sink = ctx->priv;

    if (!(sink->fifo = av_fifo_alloc(FIFO_INIT_SIZE*sizeof(AVFilterBufferRef*)))) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate fifo\n");
        return AVERROR(ENOMEM);
    }

    return 0;
}

static void end_frame(AVFilterLink *link)
{
    AVFilterContext   *ctx = link->dst;
    BufferSinkContext *sink = ctx->priv;

    if (av_fifo_space(sink->fifo) < sizeof(AVFilterBufferRef *) &&
        (av_fifo_realloc2(sink->fifo, av_fifo_size(sink->fifo) * 2) < 0)) {
            av_log(ctx, AV_LOG_ERROR, "Error reallocating the FIFO.\n");
            return;
    }

    av_fifo_generic_write(sink->fifo, &link->cur_buf, sizeof(link->cur_buf), NULL);
    link->cur_buf = NULL;
}

int av_buffersink_read(AVFilterContext *ctx, AVFilterBufferRef **buf)
{
    BufferSinkContext *sink = ctx->priv;
    AVFilterLink      *link = ctx->inputs[0];
    int ret;

    if (!buf) {
        if (av_fifo_size(sink->fifo))
            return av_fifo_size(sink->fifo)/sizeof(*buf);
        else
            return avfilter_poll_frame(ctx->inputs[0]);
    }

    if (!av_fifo_size(sink->fifo) &&
        (ret = avfilter_request_frame(link)) < 0)
        return ret;

    if (!av_fifo_size(sink->fifo))
        return AVERROR(EINVAL);

    av_fifo_generic_read(sink->fifo, buf, sizeof(*buf), NULL);

    return 0;
}

AVFilter avfilter_vsink_buffer = {
    .name      = "buffersink_old",
    .description = NULL_IF_CONFIG_SMALL("Buffer video frames, and make them available to the end of the filter graph."),
    .priv_size = sizeof(BufferSinkContext),
    .init      = init,
    .uninit    = uninit,

    .inputs    = (AVFilterPad[]) {{ .name          = "default",
                                    .type          = AVMEDIA_TYPE_VIDEO,
                                    .end_frame     = end_frame,
                                    .min_perms     = AV_PERM_READ, },
                                  { .name = NULL }},
    .outputs   = (AVFilterPad[]) {{ .name = NULL }},
};
