/*
 * Copyright (c) 2021 Paul B Mahol
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

#include "config_components.h"

#include "audio.h"
#include "avfilter.h"
#include "filters.h"
#include "internal.h"
#include "video.h"

typedef struct LatencyContext {
    int64_t min_latency;
    int64_t max_latency;
    int64_t sum;
} LatencyContext;

static av_cold int init(AVFilterContext *ctx)
{
    LatencyContext *s = ctx->priv;

    s->min_latency = INT64_MAX;
    s->max_latency = INT64_MIN;

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    LatencyContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    if (!ctx->is_disabled && ctx->inputs[0]->src &&
        ctx->inputs[0]->src->nb_inputs > 0) {
        AVFilterLink *prevlink = ctx->inputs[0]->src->inputs[0];
        int64_t delta = 0;

        switch (prevlink->type) {
        case AVMEDIA_TYPE_AUDIO:
            delta = prevlink->sample_count_in - inlink->sample_count_out;
            break;
        case AVMEDIA_TYPE_VIDEO:
            delta = prevlink->frame_count_in - inlink->frame_count_out;
            break;
        }

        if (delta > 0) {
            s->min_latency = FFMIN(s->min_latency, delta);
            s->max_latency = FFMAX(s->max_latency, delta);
        }
    }

    if (ff_inlink_queued_frames(inlink)) {
        AVFrame *frame = NULL;
        int ret;

        ret = ff_inlink_consume_frame(inlink, &frame);
        if (ret < 0)
            return ret;
        if (ret > 0)
            return ff_filter_frame(outlink, frame);
    }

    FF_FILTER_FORWARD_STATUS(inlink, outlink);
    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    LatencyContext *s = ctx->priv;

    if (s->min_latency != INT64_MAX)
        av_log(ctx, AV_LOG_INFO, "Min latency: %"PRId64"\n", s->min_latency);
    if (s->max_latency != INT64_MIN)
        av_log(ctx, AV_LOG_INFO, "Max latency: %"PRId64"\n", s->max_latency);
}

#if CONFIG_LATENCY_FILTER

const AVFilter ff_vf_latency = {
    .name          = "latency",
    .description   = NULL_IF_CONFIG_SMALL("Report video filtering latency."),
    .priv_size     = sizeof(LatencyContext),
    .init          = init,
    .uninit        = uninit,
    .activate      = activate,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                     AVFILTER_FLAG_METADATA_ONLY,
    FILTER_INPUTS(ff_video_default_filterpad),
    FILTER_OUTPUTS(ff_video_default_filterpad),
};

#endif // CONFIG_LATENCY_FILTER

#if CONFIG_ALATENCY_FILTER

const AVFilter ff_af_alatency = {
    .name          = "alatency",
    .description   = NULL_IF_CONFIG_SMALL("Report audio filtering latency."),
    .priv_size     = sizeof(LatencyContext),
    .init          = init,
    .uninit        = uninit,
    .activate      = activate,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
    FILTER_INPUTS(ff_audio_default_filterpad),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
};
#endif // CONFIG_ALATENCY_FILTER
