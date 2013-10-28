/*
 * Copyright (c) 2012 Michael Niedermayer
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
 *
 */

/**
 * @file
 * audio pad filter.
 *
 * Based on af_aresample.c
 */

#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"
#include "libavutil/avassert.h"
#include "avfilter.h"
#include "audio.h"
#include "internal.h"

typedef struct {
    const AVClass *class;
    int64_t next_pts;

    int packet_size;
    int64_t pad_len;
    int64_t whole_len;
} APadContext;

#define OFFSET(x) offsetof(APadContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption apad_options[] = {
    { "packet_size", "set silence packet size", OFFSET(packet_size), AV_OPT_TYPE_INT, { .i64 = 4096 }, 0, INT_MAX, A },
    { "pad_len",     "number of samples of silence to add",          OFFSET(pad_len),   AV_OPT_TYPE_INT64, { .i64 = 0 }, 0, INT64_MAX, A },
    { "whole_len",   "target number of samples in the audio stream", OFFSET(whole_len), AV_OPT_TYPE_INT64, { .i64 = 0 }, 0, INT64_MAX, A },
    { NULL }
};

AVFILTER_DEFINE_CLASS(apad);

static av_cold int init(AVFilterContext *ctx)
{
    APadContext *apad = ctx->priv;

    apad->next_pts = AV_NOPTS_VALUE;
    if (apad->whole_len && apad->pad_len) {
        av_log(ctx, AV_LOG_ERROR, "Both whole and pad length are set, this is not possible\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    APadContext *apad = ctx->priv;

    if (apad->whole_len)
        apad->whole_len -= frame->nb_samples;

    apad->next_pts = frame->pts + av_rescale_q(frame->nb_samples, (AVRational){1, inlink->sample_rate}, inlink->time_base);
    return ff_filter_frame(ctx->outputs[0], frame);
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    APadContext *apad = ctx->priv;
    int ret;

    ret = ff_request_frame(ctx->inputs[0]);

    if (ret == AVERROR_EOF && !ctx->is_disabled) {
        int n_out = apad->packet_size;
        AVFrame *outsamplesref;

        if (apad->whole_len > 0) {
            apad->pad_len = apad->whole_len;
            apad->whole_len = 0;
        }
        if (apad->pad_len > 0) {
            n_out = FFMIN(n_out, apad->pad_len);
            apad->pad_len -= n_out;
        }

        if(!n_out)
            return AVERROR_EOF;

        outsamplesref = ff_get_audio_buffer(outlink, n_out);
        if (!outsamplesref)
            return AVERROR(ENOMEM);

        av_assert0(outsamplesref->sample_rate == outlink->sample_rate);
        av_assert0(outsamplesref->nb_samples  == n_out);

        av_samples_set_silence(outsamplesref->extended_data, 0,
                               n_out,
                               av_frame_get_channels(outsamplesref),
                               outsamplesref->format);

        outsamplesref->pts = apad->next_pts;
        if (apad->next_pts != AV_NOPTS_VALUE)
            apad->next_pts += av_rescale_q(n_out, (AVRational){1, outlink->sample_rate}, outlink->time_base);

        return ff_filter_frame(outlink, outsamplesref);
    }
    return ret;
}

static const AVFilterPad apad_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad apad_outputs[] = {
    {
        .name          = "default",
        .request_frame = request_frame,
        .type          = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter avfilter_af_apad = {
    .name          = "apad",
    .description   = NULL_IF_CONFIG_SMALL("Pad audio with silence."),
    .init          = init,
    .priv_size     = sizeof(APadContext),
    .inputs        = apad_inputs,
    .outputs       = apad_outputs,
    .priv_class    = &apad_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
};
