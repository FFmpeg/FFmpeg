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
#include "libavutil/audioconvert.h"
#include "libavutil/common.h"
#include "libavutil/mathematics.h"
#include "libavutil/samplefmt.h"

#include "audio.h"
#include "avfilter.h"
#include "internal.h"
#include "video.h"

typedef struct Buf {
    AVFilterBufferRef *buf;
    struct Buf        *next;
} Buf;

typedef struct {
    Buf  root;
    Buf *last;   ///< last buffered frame

    /**
     * When a specific number of output samples is requested, the partial
     * buffer is stored here
     */
    AVFilterBufferRef *buf_out;
    int allocated_samples;      ///< number of samples buf_out was allocated for
} FifoContext;

static av_cold int init(AVFilterContext *ctx, const char *args)
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
        avfilter_unref_bufferp(&buf->buf);
        av_free(buf);
    }

    avfilter_unref_bufferp(&fifo->buf_out);
}

static int add_to_queue(AVFilterLink *inlink, AVFilterBufferRef *buf)
{
    FifoContext *fifo = inlink->dst->priv;

    inlink->cur_buf = NULL;
    fifo->last->next = av_mallocz(sizeof(Buf));
    if (!fifo->last->next) {
        avfilter_unref_buffer(buf);
        return AVERROR(ENOMEM);
    }

    fifo->last = fifo->last->next;
    fifo->last->buf = buf;

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

static int end_frame(AVFilterLink *inlink)
{
    return 0;
}

static int draw_slice(AVFilterLink *inlink, int y, int h, int slice_dir)
{
    return 0;
}

/**
 * Move data pointers and pts offset samples forward.
 */
static void buffer_offset(AVFilterLink *link, AVFilterBufferRef *buf,
                          int offset)
{
    int nb_channels = av_get_channel_layout_nb_channels(link->channel_layout);
    int planar = av_sample_fmt_is_planar(link->format);
    int planes = planar ? nb_channels : 1;
    int block_align = av_get_bytes_per_sample(link->format) * (planar ? 1 : nb_channels);
    int i;

    av_assert0(buf->audio->nb_samples > offset);

    for (i = 0; i < planes; i++)
        buf->extended_data[i] += block_align*offset;
    if (buf->data != buf->extended_data)
        memcpy(buf->data, buf->extended_data,
               FFMIN(planes, FF_ARRAY_ELEMS(buf->data)) * sizeof(*buf->data));
    buf->linesize[0] -= block_align*offset;
    buf->audio->nb_samples -= offset;

    if (buf->pts != AV_NOPTS_VALUE) {
        buf->pts += av_rescale_q(offset, (AVRational){1, link->sample_rate},
                                 link->time_base);
    }
}

static int calc_ptr_alignment(AVFilterBufferRef *buf)
{
    int planes = av_sample_fmt_is_planar(buf->format) ?
                 av_get_channel_layout_nb_channels(buf->audio->channel_layout) : 1;
    int min_align = 128;
    int p;

    for (p = 0; p < planes; p++) {
        int cur_align = 128;
        while ((intptr_t)buf->extended_data[p] % cur_align)
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
    AVFilterBufferRef *head = s->root.next->buf;
    AVFilterBufferRef *buf_out;
    int ret;

    if (!s->buf_out &&
        head->audio->nb_samples >= link->request_samples &&
        calc_ptr_alignment(head) >= 32) {
        if (head->audio->nb_samples == link->request_samples) {
            buf_out = head;
            queue_pop(s);
        } else {
            buf_out = avfilter_ref_buffer(head, AV_PERM_READ);
            if (!buf_out)
                return AVERROR(ENOMEM);

            buf_out->audio->nb_samples = link->request_samples;
            buffer_offset(link, head, link->request_samples);
        }
    } else {
        int nb_channels = av_get_channel_layout_nb_channels(link->channel_layout);

        if (!s->buf_out) {
            s->buf_out = ff_get_audio_buffer(link, AV_PERM_WRITE,
                                             link->request_samples);
            if (!s->buf_out)
                return AVERROR(ENOMEM);

            s->buf_out->audio->nb_samples = 0;
            s->buf_out->pts               = head->pts;
            s->allocated_samples          = link->request_samples;
        } else if (link->request_samples != s->allocated_samples) {
            av_log(ctx, AV_LOG_ERROR, "request_samples changed before the "
                   "buffer was returned.\n");
            return AVERROR(EINVAL);
        }

        while (s->buf_out->audio->nb_samples < s->allocated_samples) {
            int len = FFMIN(s->allocated_samples - s->buf_out->audio->nb_samples,
                            head->audio->nb_samples);

            av_samples_copy(s->buf_out->extended_data, head->extended_data,
                            s->buf_out->audio->nb_samples, 0, len, nb_channels,
                            link->format);
            s->buf_out->audio->nb_samples += len;

            if (len == head->audio->nb_samples) {
                avfilter_unref_buffer(head);
                queue_pop(s);

                if (!s->root.next &&
                    (ret = ff_request_frame(ctx->inputs[0])) < 0) {
                    if (ret == AVERROR_EOF) {
                        av_samples_set_silence(s->buf_out->extended_data,
                                               s->buf_out->audio->nb_samples,
                                               s->allocated_samples -
                                               s->buf_out->audio->nb_samples,
                                               nb_channels, link->format);
                        s->buf_out->audio->nb_samples = s->allocated_samples;
                        break;
                    }
                    return ret;
                }
                head = s->root.next->buf;
            } else {
                buffer_offset(link, head, len);
            }
        }
        buf_out = s->buf_out;
        s->buf_out = NULL;
    }
    return ff_filter_samples(link, buf_out);
}

static int request_frame(AVFilterLink *outlink)
{
    FifoContext *fifo = outlink->src->priv;
    int ret = 0;

    if (!fifo->root.next) {
        if ((ret = ff_request_frame(outlink->src->inputs[0])) < 0)
            return ret;
    }

    /* by doing this, we give ownership of the reference to the next filter,
     * so we don't have to worry about dereferencing it ourselves. */
    switch (outlink->type) {
    case AVMEDIA_TYPE_VIDEO:
        if ((ret = ff_start_frame(outlink, fifo->root.next->buf)) < 0 ||
            (ret = ff_draw_slice(outlink, 0, outlink->h, 1)) < 0 ||
            (ret = ff_end_frame(outlink)) < 0)
            return ret;

        queue_pop(fifo);
        break;
    case AVMEDIA_TYPE_AUDIO:
        if (outlink->request_samples) {
            return return_audio_frame(outlink->src);
        } else {
            ret = ff_filter_samples(outlink, fifo->root.next->buf);
            queue_pop(fifo);
        }
        break;
    default:
        return AVERROR(EINVAL);
    }

    return ret;
}

AVFilter avfilter_vf_fifo = {
    .name      = "fifo",
    .description = NULL_IF_CONFIG_SMALL("Buffer input images and send them when they are requested."),

    .init      = init,
    .uninit    = uninit,

    .priv_size = sizeof(FifoContext),

    .inputs    = (const AVFilterPad[]) {{ .name            = "default",
                                          .type            = AVMEDIA_TYPE_VIDEO,
                                          .get_video_buffer= ff_null_get_video_buffer,
                                          .start_frame     = add_to_queue,
                                          .draw_slice      = draw_slice,
                                          .end_frame       = end_frame,
                                          .min_perms       = AV_PERM_PRESERVE, },
                                        { .name = NULL}},
    .outputs   = (const AVFilterPad[]) {{ .name            = "default",
                                          .type            = AVMEDIA_TYPE_VIDEO,
                                          .request_frame   = request_frame, },
                                        { .name = NULL}},
};

AVFilter avfilter_af_afifo = {
    .name        = "afifo",
    .description = NULL_IF_CONFIG_SMALL("Buffer input frames and send them when they are requested."),

    .init      = init,
    .uninit    = uninit,

    .priv_size = sizeof(FifoContext),

    .inputs    = (const AVFilterPad[]) {{ .name             = "default",
                                          .type             = AVMEDIA_TYPE_AUDIO,
                                          .get_audio_buffer = ff_null_get_audio_buffer,
                                          .filter_samples   = add_to_queue,
                                          .min_perms        = AV_PERM_PRESERVE, },
                                        { .name = NULL}},
    .outputs   = (const AVFilterPad[]) {{ .name             = "default",
                                          .type             = AVMEDIA_TYPE_AUDIO,
                                          .request_frame    = request_frame, },
                                        { .name = NULL}},
};
