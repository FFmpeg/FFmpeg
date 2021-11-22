/*
 * Copyright (c) 2007 Bobby Bingham
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
 * FIFO buffering filter
 */

#include "libavutil/common.h"
#include "libavutil/mathematics.h"

#include "audio.h"
#include "avfilter.h"
#include "internal.h"

typedef struct Buf {
    AVFrame *frame;
    struct Buf *next;
} Buf;

typedef struct FifoContext {
    Buf  root;
    Buf *last;   ///< last buffered frame

    /**
     * When a specific number of output samples is requested, the partial
     * buffer is stored here
     */
    AVFrame *out;
    int allocated_samples;      ///< number of samples out was allocated for
} FifoContext;

static av_cold int init(AVFilterContext *ctx)
{
    FifoContext *s = ctx->priv;
    s->last = &s->root;

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    FifoContext *s = ctx->priv;
    Buf *buf, *tmp;

    for (buf = s->root.next; buf; buf = tmp) {
        tmp = buf->next;
        av_frame_free(&buf->frame);
        av_free(buf);
    }

    av_frame_free(&s->out);
}

static int add_to_queue(AVFilterLink *inlink, AVFrame *frame)
{
    FifoContext *s = inlink->dst->priv;

    s->last->next = av_mallocz(sizeof(Buf));
    if (!s->last->next) {
        av_frame_free(&frame);
        return AVERROR(ENOMEM);
    }

    s->last = s->last->next;
    s->last->frame = frame;

    return 0;
}

static void queue_pop(FifoContext *s)
{
    Buf *tmp = s->root.next->next;
    if (s->last == s->root.next)
        s->last = &s->root;
    av_freep(&s->root.next);
    s->root.next = tmp;
}

static int request_frame(AVFilterLink *outlink)
{
    FifoContext *s = outlink->src->priv;
    int ret = 0;

    if (!s->root.next) {
        if ((ret = ff_request_frame(outlink->src->inputs[0])) < 0)
            return ret;
        if (!s->root.next)
            return 0;
    }
    ret = ff_filter_frame(outlink, s->root.next->frame);
    queue_pop(s);
    return ret;
}

static const AVFilterPad avfilter_vf_fifo_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = add_to_queue,
    },
};

static const AVFilterPad avfilter_vf_fifo_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
    },
};

const AVFilter ff_vf_fifo = {
    .name        = "fifo",
    .description = NULL_IF_CONFIG_SMALL("Buffer input images and send them when they are requested."),
    .init        = init,
    .uninit      = uninit,
    .priv_size   = sizeof(FifoContext),
    .flags       = AVFILTER_FLAG_METADATA_ONLY,
    FILTER_INPUTS(avfilter_vf_fifo_inputs),
    FILTER_OUTPUTS(avfilter_vf_fifo_outputs),
};

static const AVFilterPad avfilter_af_afifo_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = add_to_queue,
    },
};

static const AVFilterPad avfilter_af_afifo_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .request_frame = request_frame,
    },
};

const AVFilter ff_af_afifo = {
    .name        = "afifo",
    .description = NULL_IF_CONFIG_SMALL("Buffer input frames and send them when they are requested."),
    .init        = init,
    .uninit      = uninit,
    .priv_size   = sizeof(FifoContext),
    .flags       = AVFILTER_FLAG_METADATA_ONLY,
    FILTER_INPUTS(avfilter_af_afifo_inputs),
    FILTER_OUTPUTS(avfilter_af_afifo_outputs),
};
