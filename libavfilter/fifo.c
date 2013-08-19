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

#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/mathematics.h"
#include "libavutil/samplefmt.h"

#include "audio.h"
#include "avfilter.h"
#include "internal.h"
#include "video.h"

typedef struct Buf {
    AVFrame *frame;
    struct Buf        *next;
} Buf;

typedef struct {
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
    FifoContext *fifo = ctx->priv;
    fifo->last = &fifo->root;

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    FifoContext *fifo = ctx->priv;
    Buf *buf, *tmp;

    for (buf = fifo->root.next; buf; buf = tmp) {
        tmp = buf->next;
        av_frame_free(&buf->frame);
        av_free(buf);
    }

    av_frame_free(&fifo->out);
}

static int add_to_queue(AVFilterLink *inlink, AVFrame *frame)
{
    FifoContext *fifo = inlink->dst->priv;

    fifo->last->next = av_mallocz(sizeof(Buf));
    if (!fifo->last->next) {
        av_frame_free(&frame);
        return AVERROR(ENOMEM);
    }

    fifo->last = fifo->last->next;
    fifo->last->frame = frame;

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

/**
 * Move data pointers and pts offset samples forward.
 */
static void buffer_offset(AVFilterLink *link, AVFrame *frame,
                          int offset)
{
    int nb_channels = av_get_channel_layout_nb_channels(link->channel_layout);
    int planar = av_sample_fmt_is_planar(link->format);
    int planes = planar ? nb_channels : 1;
    int block_align = av_get_bytes_per_sample(link->format) * (planar ? 1 : nb_channels);
    int i;

    av_assert0(frame->nb_samples > offset);

    for (i = 0; i < planes; i++)
        frame->extended_data[i] += block_align * offset;
    if (frame->data != frame->extended_data)
        memcpy(frame->data, frame->extended_data,
               FFMIN(planes, FF_ARRAY_ELEMS(frame->data)) * sizeof(*frame->data));
    frame->linesize[0] -= block_align*offset;
    frame->nb_samples -= offset;

    if (frame->pts != AV_NOPTS_VALUE) {
        frame->pts += av_rescale_q(offset, (AVRational){1, link->sample_rate},
                                   link->time_base);
    }
}

static int calc_ptr_alignment(AVFrame *frame)
{
    int planes = av_sample_fmt_is_planar(frame->format) ?
                 av_get_channel_layout_nb_channels(frame->channel_layout) : 1;
    int min_align = 128;
    int p;

    for (p = 0; p < planes; p++) {
        int cur_align = 128;
        while ((intptr_t)frame->extended_data[p] % cur_align)
            cur_align >>= 1;
        if (cur_align < min_align)
            min_align = cur_align;
    }
    return min_align;
}

static int return_audio_frame(AVFilterContext *ctx)
{
    AVFilterLink *link = ctx->outputs[0];
    FifoContext *s = ctx->priv;
    AVFrame *head = s->root.next ? s->root.next->frame : NULL;
    AVFrame *out;
    int ret;

    /* if head is NULL then we're flushing the remaining samples in out */
    if (!head && !s->out)
        return AVERROR_EOF;

    if (!s->out &&
        head->nb_samples >= link->request_samples &&
        calc_ptr_alignment(head) >= 32) {
        if (head->nb_samples == link->request_samples) {
            out = head;
            queue_pop(s);
        } else {
            out = av_frame_clone(head);
            if (!out)
                return AVERROR(ENOMEM);

            out->nb_samples = link->request_samples;
            buffer_offset(link, head, link->request_samples);
        }
    } else {
        int nb_channels = av_get_channel_layout_nb_channels(link->channel_layout);

        if (!s->out) {
            s->out = ff_get_audio_buffer(link, link->request_samples);
            if (!s->out)
                return AVERROR(ENOMEM);

            s->out->nb_samples = 0;
            s->out->pts                   = head->pts;
            s->allocated_samples          = link->request_samples;
        } else if (link->request_samples != s->allocated_samples) {
            av_log(ctx, AV_LOG_ERROR, "request_samples changed before the "
                   "buffer was returned.\n");
            return AVERROR(EINVAL);
        }

        while (s->out->nb_samples < s->allocated_samples) {
            int len;

            if (!s->root.next) {
                ret = ff_request_frame(ctx->inputs[0]);
                if (ret == AVERROR_EOF) {
                    av_samples_set_silence(s->out->extended_data,
                                           s->out->nb_samples,
                                           s->allocated_samples -
                                           s->out->nb_samples,
                                           nb_channels, link->format);
                    s->out->nb_samples = s->allocated_samples;
                    break;
                } else if (ret < 0)
                    return ret;
                av_assert0(s->root.next); // If ff_request_frame() succeeded then we should have a frame
            }
            head = s->root.next->frame;

            len = FFMIN(s->allocated_samples - s->out->nb_samples,
                        head->nb_samples);

            av_samples_copy(s->out->extended_data, head->extended_data,
                            s->out->nb_samples, 0, len, nb_channels,
                            link->format);
            s->out->nb_samples += len;

            if (len == head->nb_samples) {
                av_frame_free(&head);
                queue_pop(s);
            } else {
                buffer_offset(link, head, len);
            }
        }
        out = s->out;
        s->out = NULL;
    }
    return ff_filter_frame(link, out);
}

static int request_frame(AVFilterLink *outlink)
{
    FifoContext *fifo = outlink->src->priv;
    int ret = 0;

    if (!fifo->root.next) {
        if ((ret = ff_request_frame(outlink->src->inputs[0])) < 0) {
            if (ret == AVERROR_EOF && outlink->request_samples)
                return return_audio_frame(outlink->src);
            return ret;
        }
        av_assert0(fifo->root.next);
    }

    if (outlink->request_samples) {
        return return_audio_frame(outlink->src);
    } else {
        ret = ff_filter_frame(outlink, fifo->root.next->frame);
        queue_pop(fifo);
    }

    return ret;
}

static const AVFilterPad avfilter_vf_fifo_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .get_video_buffer = ff_null_get_video_buffer,
        .filter_frame     = add_to_queue,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_fifo_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter avfilter_vf_fifo = {
    .name      = "fifo",
    .description = NULL_IF_CONFIG_SMALL("Buffer input images and send them when they are requested."),

    .init      = init,
    .uninit    = uninit,

    .priv_size = sizeof(FifoContext),

    .inputs    = avfilter_vf_fifo_inputs,
    .outputs   = avfilter_vf_fifo_outputs,
};

static const AVFilterPad avfilter_af_afifo_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_AUDIO,
        .get_audio_buffer = ff_null_get_audio_buffer,
        .filter_frame     = add_to_queue,
    },
    { NULL }
};

static const AVFilterPad avfilter_af_afifo_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter avfilter_af_afifo = {
    .name        = "afifo",
    .description = NULL_IF_CONFIG_SMALL("Buffer input frames and send them when they are requested."),

    .init      = init,
    .uninit    = uninit,

    .priv_size = sizeof(FifoContext),

    .inputs    = avfilter_af_afifo_inputs,
    .outputs   = avfilter_af_afifo_outputs,
};
