/*
 * Copyright (c) 2011 Stefano Sabatini
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * buffer sink
 */

#include "libavutil/audio_fifo.h"
#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/mathematics.h"

#include "audio.h"
#include "avfilter.h"
#include "buffersink.h"
#include "internal.h"

typedef struct BufferSinkContext {
    AVFrame *cur_frame;          ///< last frame delivered on the sink
    AVAudioFifo *audio_fifo;     ///< FIFO for audio samples
    int64_t next_pts;            ///< interpolating audio pts
} BufferSinkContext;

static av_cold void uninit(AVFilterContext *ctx)
{
    BufferSinkContext *sink = ctx->priv;

    if (sink->audio_fifo)
        av_audio_fifo_free(sink->audio_fifo);
}

static int filter_frame(AVFilterLink *link, AVFrame *frame)
{
    BufferSinkContext *s = link->dst->priv;

    av_assert0(!s->cur_frame);
    s->cur_frame = frame;

    return 0;
}

int attribute_align_arg av_buffersink_get_frame(AVFilterContext *ctx,
                                                AVFrame *frame)
{
    BufferSinkContext *s    = ctx->priv;
    AVFilterLink      *link = ctx->inputs[0];
    int ret;

    if ((ret = ff_request_frame(link)) < 0)
        return ret;

    if (!s->cur_frame)
        return AVERROR(EINVAL);

    av_frame_move_ref(frame, s->cur_frame);
    av_frame_free(&s->cur_frame);

    return 0;
}

static int read_from_fifo(AVFilterContext *ctx, AVFrame *frame,
                          int nb_samples)
{
    BufferSinkContext *s = ctx->priv;
    AVFilterLink   *link = ctx->inputs[0];
    AVFrame *tmp;

    if (!(tmp = ff_get_audio_buffer(link, nb_samples)))
        return AVERROR(ENOMEM);
    av_audio_fifo_read(s->audio_fifo, (void**)tmp->extended_data, nb_samples);

    tmp->pts = s->next_pts;
    s->next_pts += av_rescale_q(nb_samples, (AVRational){1, link->sample_rate},
                                link->time_base);

    av_frame_move_ref(frame, tmp);
    av_frame_free(&tmp);

    return 0;
}

int attribute_align_arg av_buffersink_get_samples(AVFilterContext *ctx,
                                                  AVFrame *frame, int nb_samples)
{
    BufferSinkContext *s = ctx->priv;
    AVFilterLink   *link = ctx->inputs[0];
    int ret = 0;

    if (!s->audio_fifo) {
        int nb_channels = av_get_channel_layout_nb_channels(link->channel_layout);
        if (!(s->audio_fifo = av_audio_fifo_alloc(link->format, nb_channels, nb_samples)))
            return AVERROR(ENOMEM);
    }

    while (ret >= 0) {
        if (av_audio_fifo_size(s->audio_fifo) >= nb_samples)
            return read_from_fifo(ctx, frame, nb_samples);

        ret = ff_request_frame(link);
        if (ret == AVERROR_EOF && av_audio_fifo_size(s->audio_fifo))
            return read_from_fifo(ctx, frame, av_audio_fifo_size(s->audio_fifo));
        else if (ret < 0)
            return ret;

        if (s->cur_frame->pts != AV_NOPTS_VALUE) {
            s->next_pts = s->cur_frame->pts -
                          av_rescale_q(av_audio_fifo_size(s->audio_fifo),
                                       (AVRational){ 1, link->sample_rate },
                                       link->time_base);
        }

        ret = av_audio_fifo_write(s->audio_fifo, (void**)s->cur_frame->extended_data,
                                  s->cur_frame->nb_samples);
        av_frame_free(&s->cur_frame);
    }

    return ret;
}

static const AVFilterPad avfilter_vsink_buffer_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .needs_fifo   = 1
    },
    { NULL }
};

AVFilter ff_vsink_buffer = {
    .name        = "buffersink",
    .description = NULL_IF_CONFIG_SMALL("Buffer video frames, and make them available to the end of the filter graph."),
    .priv_size   = sizeof(BufferSinkContext),
    .uninit      = uninit,

    .inputs      = avfilter_vsink_buffer_inputs,
    .outputs     = NULL,
};

static const AVFilterPad avfilter_asink_abuffer_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
        .needs_fifo   = 1
    },
    { NULL }
};

AVFilter ff_asink_abuffer = {
    .name        = "abuffersink",
    .description = NULL_IF_CONFIG_SMALL("Buffer audio frames, and make them available to the end of the filter graph."),
    .priv_size   = sizeof(BufferSinkContext),
    .uninit      = uninit,

    .inputs      = avfilter_asink_abuffer_inputs,
    .outputs     = NULL,
};
