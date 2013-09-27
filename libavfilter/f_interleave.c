/*
 * Copyright (c) 2013 Stefano Sabatini
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
 * audio and video interleaver
 */

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "bufferqueue.h"
#include "formats.h"
#include "internal.h"
#include "audio.h"
#include "video.h"

typedef struct {
    const AVClass *class;
    int nb_inputs;
    struct FFBufQueue *queues;
} InterleaveContext;

#define OFFSET(x) offsetof(InterleaveContext, x)

#define DEFINE_OPTIONS(filt_name, flags_)                           \
static const AVOption filt_name##_options[] = {                     \
   { "nb_inputs", "set number of inputs", OFFSET(nb_inputs), AV_OPT_TYPE_INT, {.i64 = 2}, 1, INT_MAX, .flags = flags_ }, \
   { "n",         "set number of inputs", OFFSET(nb_inputs), AV_OPT_TYPE_INT, {.i64 = 2}, 1, INT_MAX, .flags = flags_ }, \
   { NULL }                                                         \
}

inline static int push_frame(AVFilterContext *ctx)
{
    InterleaveContext *interleave = ctx->priv;
    AVFrame *frame;
    int i, queue_idx = -1;
    int64_t pts_min = INT64_MAX;

    /* look for oldest frame */
    for (i = 0; i < ctx->nb_inputs; i++) {
        struct FFBufQueue *q = &interleave->queues[i];

        if (!q->available && !ctx->inputs[i]->closed)
            return 0;
        if (q->available) {
            frame = ff_bufqueue_peek(q, 0);
            if (frame->pts < pts_min) {
                pts_min = frame->pts;
                queue_idx = i;
            }
        }
    }

    /* all inputs are closed */
    if (queue_idx < 0)
        return AVERROR_EOF;

    frame = ff_bufqueue_get(&interleave->queues[queue_idx]);
    av_log(ctx, AV_LOG_DEBUG, "queue:%d -> frame time:%f\n",
           queue_idx, frame->pts * av_q2d(AV_TIME_BASE_Q));
    return ff_filter_frame(ctx->outputs[0], frame);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    InterleaveContext *interleave = ctx->priv;
    unsigned in_no = FF_INLINK_IDX(inlink);

    if (frame->pts == AV_NOPTS_VALUE) {
        av_log(ctx, AV_LOG_WARNING,
               "NOPTS value for input frame cannot be accepted, frame discarded\n");
        av_frame_free(&frame);
        return AVERROR_INVALIDDATA;
    }

    /* queue frame */
    frame->pts = av_rescale_q(frame->pts, inlink->time_base, AV_TIME_BASE_Q);
    av_log(ctx, AV_LOG_DEBUG, "frame pts:%f -> queue idx:%d available:%d\n",
           frame->pts * av_q2d(AV_TIME_BASE_Q), in_no, interleave->queues[in_no].available);
    ff_bufqueue_add(ctx, &interleave->queues[in_no], frame);

    return push_frame(ctx);
}

static av_cold int init(AVFilterContext *ctx)
{
    InterleaveContext *interleave = ctx->priv;
    const AVFilterPad *outpad = &ctx->filter->outputs[0];
    int i;

    interleave->queues = av_calloc(interleave->nb_inputs, sizeof(interleave->queues[0]));
    if (!interleave->queues)
        return AVERROR(ENOMEM);

    for (i = 0; i < interleave->nb_inputs; i++) {
        AVFilterPad inpad = { 0 };

        inpad.name = av_asprintf("input%d", i);
        if (!inpad.name)
            return AVERROR(ENOMEM);
        inpad.type         = outpad->type;
        inpad.filter_frame = filter_frame;

        switch (outpad->type) {
        case AVMEDIA_TYPE_VIDEO:
            inpad.get_video_buffer = ff_null_get_video_buffer; break;
        case AVMEDIA_TYPE_AUDIO:
            inpad.get_audio_buffer = ff_null_get_audio_buffer; break;
        default:
            av_assert0(0);
        }
        ff_insert_inpad(ctx, i, &inpad);
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    InterleaveContext *interleave = ctx->priv;
    int i;

    for (i = 0; i < ctx->nb_inputs; i++) {
        ff_bufqueue_discard_all(&interleave->queues[i]);
        av_freep(&interleave->queues[i]);
        av_freep(&ctx->input_pads[i].name);
    }
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink0 = ctx->inputs[0];
    int i;

    if (outlink->type == AVMEDIA_TYPE_VIDEO) {
        outlink->time_base           = AV_TIME_BASE_Q;
        outlink->w                   = inlink0->w;
        outlink->h                   = inlink0->h;
        outlink->sample_aspect_ratio = inlink0->sample_aspect_ratio;
        outlink->format              = inlink0->format;
        outlink->frame_rate = (AVRational) {1, 0};
        for (i = 1; i < ctx->nb_inputs; i++) {
            AVFilterLink *inlink = ctx->inputs[i];

            if (outlink->w                       != inlink->w                       ||
                outlink->h                       != inlink->h                       ||
                outlink->sample_aspect_ratio.num != inlink->sample_aspect_ratio.num ||
                outlink->sample_aspect_ratio.den != inlink->sample_aspect_ratio.den) {
                av_log(ctx, AV_LOG_ERROR, "Parameters for input link %s "
                       "(size %dx%d, SAR %d:%d) do not match the corresponding "
                       "output link parameters (%dx%d, SAR %d:%d)\n",
                       ctx->input_pads[i].name, inlink->w, inlink->h,
                       inlink->sample_aspect_ratio.num,
                       inlink->sample_aspect_ratio.den,
                       outlink->w, outlink->h,
                       outlink->sample_aspect_ratio.num,
                       outlink->sample_aspect_ratio.den);
                return AVERROR(EINVAL);
            }
        }
    }

    outlink->flags |= FF_LINK_FLAG_REQUEST_LOOP;
    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    InterleaveContext *interleave = ctx->priv;
    int i, ret;

    for (i = 0; i < ctx->nb_inputs; i++) {
        if (!interleave->queues[i].available && !ctx->inputs[i]->closed) {
            ret = ff_request_frame(ctx->inputs[i]);
            if (ret != AVERROR_EOF)
                return ret;
        }
    }

    return push_frame(ctx);
}

#if CONFIG_INTERLEAVE_FILTER

DEFINE_OPTIONS(interleave, AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM);
AVFILTER_DEFINE_CLASS(interleave);

static const AVFilterPad interleave_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter avfilter_vf_interleave = {
    .name        = "interleave",
    .description = NULL_IF_CONFIG_SMALL("Temporally interleave video inputs."),
    .priv_size   = sizeof(InterleaveContext),
    .init        = init,
    .uninit      = uninit,
    .outputs     = interleave_outputs,
    .priv_class  = &interleave_class,
    .flags       = AVFILTER_FLAG_DYNAMIC_INPUTS,
};

#endif

#if CONFIG_AINTERLEAVE_FILTER

DEFINE_OPTIONS(ainterleave, AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM);
AVFILTER_DEFINE_CLASS(ainterleave);

static const AVFilterPad ainterleave_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_output,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter avfilter_af_ainterleave = {
    .name        = "ainterleave",
    .description = NULL_IF_CONFIG_SMALL("Temporally interleave audio inputs."),
    .priv_size   = sizeof(InterleaveContext),
    .init        = init,
    .uninit      = uninit,
    .outputs     = ainterleave_outputs,
    .priv_class  = &ainterleave_class,
    .flags       = AVFILTER_FLAG_DYNAMIC_INPUTS,
};

#endif
