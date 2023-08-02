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
#include "filters.h"
#include "internal.h"

typedef struct APadContext {
    const AVClass *class;
    int64_t next_pts;

    int eof;
    int packet_size;
    int64_t pad_len, pad_len_left;
    int64_t whole_len, whole_len_left;
    int64_t pad_dur;
    int64_t whole_dur;
} APadContext;

#define OFFSET(x) offsetof(APadContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption apad_options[] = {
    { "packet_size", "set silence packet size",                                  OFFSET(packet_size), AV_OPT_TYPE_INT,   { .i64 = 4096 }, 0, INT_MAX, A },
    { "pad_len",     "set number of samples of silence to add",                  OFFSET(pad_len),     AV_OPT_TYPE_INT64, { .i64 = -1 }, -1, INT64_MAX, A },
    { "whole_len",   "set minimum target number of samples in the audio stream", OFFSET(whole_len),   AV_OPT_TYPE_INT64, { .i64 = -1 }, -1, INT64_MAX, A },
    { "pad_dur",     "set duration of silence to add",                           OFFSET(pad_dur),     AV_OPT_TYPE_DURATION, { .i64 = -1 }, -1, INT64_MAX, A },
    { "whole_dur",   "set minimum target duration in the audio stream",          OFFSET(whole_dur),   AV_OPT_TYPE_DURATION, { .i64 = -1 }, -1, INT64_MAX, A },
    { NULL }
};

AVFILTER_DEFINE_CLASS(apad);

static av_cold int init(AVFilterContext *ctx)
{
    APadContext *s = ctx->priv;

    s->next_pts = AV_NOPTS_VALUE;
    if (s->whole_len >= 0 && s->pad_len >= 0) {
        av_log(ctx, AV_LOG_ERROR, "Both whole and pad length are set, this is not possible\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    APadContext *s = ctx->priv;

    if (s->whole_len >= 0) {
        s->whole_len_left = FFMAX(s->whole_len_left - frame->nb_samples, 0);
        av_log(ctx, AV_LOG_DEBUG,
               "n_out:%d whole_len_left:%"PRId64"\n", frame->nb_samples, s->whole_len_left);
    }

    s->next_pts = frame->pts + av_rescale_q(frame->nb_samples, (AVRational){1, inlink->sample_rate}, inlink->time_base);
    return ff_filter_frame(ctx->outputs[0], frame);
}

static int push_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    APadContext *s = ctx->priv;
    AVFrame *outsamplesref;
    int n_out;

    if (ctx->is_disabled)
        return 0;
    n_out = s->packet_size;

    if (s->whole_len >= 0 && s->pad_len < 0) {
        s->pad_len = s->pad_len_left = s->whole_len_left;
    }
    if (s->pad_len >=0 || s->whole_len >= 0) {
        n_out = FFMIN(n_out, s->pad_len_left);
        s->pad_len_left -= n_out;
        av_log(ctx, AV_LOG_DEBUG,
               "padding n_out:%d pad_len_left:%"PRId64"\n", n_out, s->pad_len_left);
    }

    if (!n_out)
        return AVERROR_EOF;

    outsamplesref = ff_get_audio_buffer(outlink, n_out);
    if (!outsamplesref)
        return AVERROR(ENOMEM);

    av_assert0(outsamplesref->sample_rate == outlink->sample_rate);
    av_assert0(outsamplesref->nb_samples  == n_out);

    av_samples_set_silence(outsamplesref->extended_data, 0,
                           n_out,
                           outsamplesref->ch_layout.nb_channels,
                           outsamplesref->format);

    outsamplesref->pts = s->next_pts;
    if (s->next_pts != AV_NOPTS_VALUE)
        s->next_pts += av_rescale_q(n_out, (AVRational){1, outlink->sample_rate}, outlink->time_base);

    return ff_filter_frame(outlink, outsamplesref);
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    APadContext *s = ctx->priv;
    int64_t pts;
    int status;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    if (!s->eof && ff_inlink_queued_frames(inlink)) {
        AVFrame *frame = NULL;
        int ret;

        ret = ff_inlink_consume_frame(inlink, &frame);
        if (ret < 0)
            return ret;
        if (ret > 0)
            return filter_frame(inlink, frame);
    }

    if (!s->eof && ff_inlink_acknowledge_status(inlink, &status, &pts))
        s->eof = status == AVERROR_EOF;

    if (s->eof) {
        int ret = push_frame(outlink);

        if (ret == AVERROR_EOF) {
            ff_outlink_set_status(outlink, AVERROR_EOF, s->next_pts);
            return 0;
        }
        return ret;
    }

    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    APadContext *s  = ctx->priv;

    if (s->pad_dur >= 0)
        s->pad_len = av_rescale(s->pad_dur, outlink->sample_rate, AV_TIME_BASE);
    if (s->whole_dur >= 0)
        s->whole_len = av_rescale(s->whole_dur, outlink->sample_rate, AV_TIME_BASE);

    s->pad_len_left   = s->pad_len;
    s->whole_len_left = s->whole_len;

    return 0;
}

static const AVFilterPad apad_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_output,
    },
};

const AVFilter ff_af_apad = {
    .name          = "apad",
    .description   = NULL_IF_CONFIG_SMALL("Pad audio with silence."),
    .init          = init,
    .activate      = activate,
    .priv_size     = sizeof(APadContext),
    FILTER_INPUTS(ff_audio_default_filterpad),
    FILTER_OUTPUTS(apad_outputs),
    .priv_class    = &apad_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
};
