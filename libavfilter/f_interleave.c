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
#include "formats.h"
#include "filters.h"
#include "internal.h"
#include "audio.h"
#include "video.h"

typedef struct InterleaveContext {
    const AVClass *class;
    int nb_inputs;
    int duration_mode;
    int64_t pts;
} InterleaveContext;

#define DURATION_LONGEST  0
#define DURATION_SHORTEST 1
#define DURATION_FIRST    2

#define OFFSET(x) offsetof(InterleaveContext, x)

#define DEFINE_OPTIONS(filt_name, flags_)                           \
static const AVOption filt_name##_options[] = {                     \
   { "nb_inputs", "set number of inputs", OFFSET(nb_inputs), AV_OPT_TYPE_INT, {.i64 = 2}, 1, INT_MAX, .flags = flags_ }, \
   { "n",         "set number of inputs", OFFSET(nb_inputs), AV_OPT_TYPE_INT, {.i64 = 2}, 1, INT_MAX, .flags = flags_ }, \
   { "duration", "how to determine the end-of-stream",              \
       OFFSET(duration_mode), AV_OPT_TYPE_INT, { .i64 = DURATION_LONGEST }, 0,  2, flags_, "duration" }, \
       { "longest",  "Duration of longest input",  0, AV_OPT_TYPE_CONST, { .i64 = DURATION_LONGEST  }, 0, 0, flags_, "duration" }, \
       { "shortest", "Duration of shortest input", 0, AV_OPT_TYPE_CONST, { .i64 = DURATION_SHORTEST }, 0, 0, flags_, "duration" }, \
       { "first",    "Duration of first input",    0, AV_OPT_TYPE_CONST, { .i64 = DURATION_FIRST    }, 0, 0, flags_, "duration" }, \
   { NULL }                                                         \
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *outlink = ctx->outputs[0];
    InterleaveContext *s = ctx->priv;
    int64_t q_pts, pts = INT64_MAX;
    int i, nb_eofs = 0, input_idx = -1;
    int first_eof = 0;
    int64_t rpts;
    int status;
    int nb_inputs_with_frames = 0;

    FF_FILTER_FORWARD_STATUS_BACK_ALL(outlink, ctx);

    for (i = 0; i < ctx->nb_inputs; i++) {
        int is_eof = !!ff_inlink_acknowledge_status(ctx->inputs[i], &status, &rpts);

        nb_eofs += is_eof;
        if (i == 0)
            first_eof = is_eof;
    }

    if ((nb_eofs > 0 && s->duration_mode == DURATION_SHORTEST) ||
        (nb_eofs == ctx->nb_inputs && s->duration_mode == DURATION_LONGEST) ||
        (first_eof && s->duration_mode == DURATION_FIRST)) {
        ff_outlink_set_status(outlink, AVERROR_EOF, s->pts);
        return 0;
    }

    for (i = 0; i < ctx->nb_inputs; i++) {
        if (!ff_inlink_queued_frames(ctx->inputs[i]))
            continue;
        nb_inputs_with_frames++;
    }

    if (nb_inputs_with_frames >= ctx->nb_inputs - nb_eofs) {
        for (i = 0; i < ctx->nb_inputs; i++) {
            AVFrame *frame;

            if (ff_inlink_queued_frames(ctx->inputs[i]) == 0)
                continue;

            frame = ff_inlink_peek_frame(ctx->inputs[i], 0);
            if (frame->pts == AV_NOPTS_VALUE) {
                int ret;

                av_log(ctx, AV_LOG_WARNING,
                       "NOPTS value for input frame cannot be accepted, frame discarded\n");
                ret = ff_inlink_consume_frame(ctx->inputs[i], &frame);
                if (ret < 0)
                    return ret;
                av_frame_free(&frame);
                return AVERROR_INVALIDDATA;
            }

            q_pts = av_rescale_q(frame->pts, ctx->inputs[i]->time_base, AV_TIME_BASE_Q);
            if (q_pts < pts) {
                pts = q_pts;
                input_idx = i;
            }
        }

        if (input_idx >= 0) {
            AVFrame *frame;
            int ret;

            ret = ff_inlink_consume_frame(ctx->inputs[input_idx], &frame);
            if (ret < 0)
                return ret;

            frame->pts = s->pts = pts;
            return ff_filter_frame(outlink, frame);
        }
    }

    for (i = 0; i < ctx->nb_inputs; i++) {
        if (ff_inlink_queued_frames(ctx->inputs[i]))
            continue;
        if (ff_outlink_frame_wanted(outlink) &&
            !ff_outlink_get_status(ctx->inputs[i])) {
            ff_inlink_request_frame(ctx->inputs[i]);
            return 0;
        }
    }

    if (i == ctx->nb_inputs - nb_eofs && ff_outlink_frame_wanted(outlink)) {
        ff_filter_set_ready(ctx, 100);
        return 0;
    }

    return FFERROR_NOT_READY;
}

static av_cold int init(AVFilterContext *ctx)
{
    InterleaveContext *s = ctx->priv;
    const AVFilterPad *outpad = &ctx->filter->outputs[0];
    int i, ret;

    for (i = 0; i < s->nb_inputs; i++) {
        AVFilterPad inpad = { 0 };

        inpad.name = av_asprintf("input%d", i);
        if (!inpad.name)
            return AVERROR(ENOMEM);
        inpad.type         = outpad->type;

        switch (outpad->type) {
        case AVMEDIA_TYPE_VIDEO:
            inpad.get_buffer.video = ff_null_get_video_buffer; break;
        case AVMEDIA_TYPE_AUDIO:
            inpad.get_buffer.audio = ff_null_get_audio_buffer; break;
        default:
            av_assert0(0);
        }
        if ((ret = ff_append_inpad_free_name(ctx, &inpad)) < 0)
            return ret;
    }

    return 0;
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
    return 0;
}

#if CONFIG_INTERLEAVE_FILTER

DEFINE_OPTIONS(interleave, AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM);
AVFILTER_DEFINE_CLASS(interleave);

static const AVFilterPad interleave_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

const AVFilter ff_vf_interleave = {
    .name        = "interleave",
    .description = NULL_IF_CONFIG_SMALL("Temporally interleave video inputs."),
    .priv_size   = sizeof(InterleaveContext),
    .init        = init,
    .activate    = activate,
    FILTER_OUTPUTS(interleave_outputs),
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
    },
};

const AVFilter ff_af_ainterleave = {
    .name        = "ainterleave",
    .description = NULL_IF_CONFIG_SMALL("Temporally interleave audio inputs."),
    .priv_size   = sizeof(InterleaveContext),
    .init        = init,
    .activate    = activate,
    FILTER_OUTPUTS(ainterleave_outputs),
    .priv_class  = &ainterleave_class,
    .flags       = AVFILTER_FLAG_DYNAMIC_INPUTS,
};

#endif
