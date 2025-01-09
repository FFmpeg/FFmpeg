/*
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

#include <stdint.h>

#include "libavutil/avstring.h"
#include "libavutil/log.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"

#include "avfilter.h"
#include "filters.h"

typedef struct SegmentContext {
    const AVClass *class;

    char *timestamps_str;
    char *points_str;
    int use_timestamps;

    int current_point;
    int nb_points;
    int64_t last_pts;

    int64_t *points;
} SegmentContext;

static void count_points(char *item_str, int *nb_items)
{
    char *p;

    if (!item_str)
        return;

    *nb_items = 1;
    for (p = item_str; *p; p++) {
        if (*p == '|')
            (*nb_items)++;
    }
}

static int parse_points(AVFilterContext *ctx, char *item_str, int nb_points, int64_t *points)
{
    SegmentContext *s = ctx->priv;
    char *arg, *p = item_str;
    char *saveptr = NULL;
    int64_t ref, cur = 0;
    int ret = 0;

    for (int i = 0; i < nb_points; i++) {
        if (!(arg = av_strtok(p, "|", &saveptr)))
            return AVERROR(EINVAL);

        p = NULL;
        ref = 0;
        if (*arg == '+') {
            ref = cur;
            arg++;
        }

        if (s->use_timestamps) {
            ret = av_parse_time(&points[i], arg, s->use_timestamps);
        } else {
            if (sscanf(arg, "%"SCNd64, &points[i]) != 1)
                ret = AVERROR(EINVAL);
        }

        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR, "Invalid splits supplied: %s\n", arg);
            return ret;
        }

        cur = points[i];
        points[i] += ref;
    }

    return 0;
}

static av_cold int init(AVFilterContext *ctx, enum AVMediaType type)
{
    SegmentContext *s = ctx->priv;
    char *split_str;
    int ret;

    if (s->timestamps_str && s->points_str) {
        av_log(ctx, AV_LOG_ERROR, "Both timestamps and counts supplied.\n");
        return AVERROR(EINVAL);
    } else if (s->timestamps_str) {
        s->use_timestamps = 1;
        split_str = s->timestamps_str;
    } else if (s->points_str) {
        split_str = s->points_str;
    } else {
        av_log(ctx, AV_LOG_ERROR, "Neither timestamps nor durations nor counts supplied.\n");
        return AVERROR(EINVAL);
    }

    count_points(split_str, &s->nb_points);
    s->nb_points++;

    s->points = av_calloc(s->nb_points, sizeof(*s->points));
    if (!s->points)
        return AVERROR(ENOMEM);

    ret = parse_points(ctx, split_str, s->nb_points - 1, s->points);
    if (ret < 0)
        return ret;

    s->points[s->nb_points - 1] = INT64_MAX;

    for (int i = 0; i < s->nb_points; i++) {
        AVFilterPad pad = { 0 };

        pad.type = type;
        pad.name = av_asprintf("output%d", i);
        if (!pad.name)
            return AVERROR(ENOMEM);

        if ((ret = ff_append_outpad_free_name(ctx, &pad)) < 0)
            return ret;
    }

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    SegmentContext *s = ctx->priv;
    AVRational tb = inlink->time_base;

    if (s->use_timestamps) {
        for (int i = 0; i < s->nb_points - 1; i++)
            s->points[i] = av_rescale_q(s->points[i], AV_TIME_BASE_Q, tb);
    }

    return 0;
}

static int current_segment_finished(AVFilterContext *ctx, AVFrame *frame)
{
    SegmentContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    FilterLink      *inl = ff_filter_link(inlink);
    int ret = 0;

    if (s->use_timestamps) {
        ret = frame->pts >= s->points[s->current_point];
    } else {
        switch (inlink->type) {
        case AVMEDIA_TYPE_VIDEO:
            ret = inl->frame_count_out - 1 >= s->points[s->current_point];
            break;
        case AVMEDIA_TYPE_AUDIO:
            ret = inl->sample_count_out - frame->nb_samples >= s->points[s->current_point];
            break;
        }
    }

    return ret;
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    FilterLink      *inl = ff_filter_link(inlink);
    SegmentContext *s = ctx->priv;
    AVFrame *frame = NULL;
    int ret, status;
    int64_t max_samples;
    int64_t diff;
    int64_t pts;

    for (int i = s->current_point; i < s->nb_points; i++) {
        FF_FILTER_FORWARD_STATUS_BACK_ALL(ctx->outputs[i], ctx);
    }

    switch (inlink->type) {
    case AVMEDIA_TYPE_VIDEO:
        ret = ff_inlink_consume_frame(inlink, &frame);
        break;
    case AVMEDIA_TYPE_AUDIO:
        diff = s->points[s->current_point] - inl->sample_count_out;
        while (diff <= 0) {
            ff_outlink_set_status(ctx->outputs[s->current_point], AVERROR_EOF, s->last_pts);
            s->current_point++;
            if (s->current_point >= s->nb_points)
                return AVERROR(EINVAL);

            diff = s->points[s->current_point] - inl->sample_count_out;
        }
        if (s->use_timestamps) {
            max_samples = av_rescale_q(diff, av_make_q(1, inlink->sample_rate), inlink->time_base);
        } else {
            max_samples = FFMAX(1, FFMIN(diff, INT_MAX));
        }
        if (max_samples <= 0 || max_samples > INT_MAX)
            ret = ff_inlink_consume_frame(inlink, &frame);
        else
            ret = ff_inlink_consume_samples(inlink, 1, max_samples, &frame);
        break;
    default:
        return AVERROR_BUG;
    }

    if (ret > 0) {
        s->last_pts = frame->pts;
        while (current_segment_finished(ctx, frame)) {
            ff_outlink_set_status(ctx->outputs[s->current_point], AVERROR_EOF, frame->pts);
            s->current_point++;
        }

        if (s->current_point >= s->nb_points) {
            av_frame_free(&frame);
            return AVERROR(EINVAL);
        }

        ret = ff_filter_frame(ctx->outputs[s->current_point], frame);
    }

    if (ret < 0) {
        return ret;
    } else if (ff_inlink_acknowledge_status(inlink, &status, &pts)) {
        for (int i = s->current_point; i < s->nb_points; i++)
            ff_outlink_set_status(ctx->outputs[i], status, pts);
        return 0;
    } else {
        for (int i = s->current_point; i < s->nb_points; i++) {
            if (ff_outlink_frame_wanted(ctx->outputs[i]))
                ff_inlink_request_frame(inlink);
        }
        return 0;
    }
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SegmentContext *s = ctx->priv;

    av_freep(&s->points);
}

#define OFFSET(x) offsetof(SegmentContext, x)
#define COMMON_OPTS \
    { "timestamps", "timestamps of input at which to split input", OFFSET(timestamps_str),  AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, FLAGS }, \

#if CONFIG_SEGMENT_FILTER

static av_cold int video_init(AVFilterContext *ctx)
{
    return init(ctx, AVMEDIA_TYPE_VIDEO);
}

#define FLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM
static const AVOption segment_options[] = {
    COMMON_OPTS
    { "frames", "frames at which to split input", OFFSET(points_str), AV_OPT_TYPE_STRING,  { .str = NULL }, 0, 0, FLAGS },
    { NULL }
};
#undef FLAGS

AVFILTER_DEFINE_CLASS(segment);

static const AVFilterPad segment_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
    },
};

const FFFilter ff_vf_segment = {
    .p.name        = "segment",
    .p.description = NULL_IF_CONFIG_SMALL("Segment video stream."),
    .p.priv_class  = &segment_class,
    .p.flags       = AVFILTER_FLAG_DYNAMIC_OUTPUTS | AVFILTER_FLAG_METADATA_ONLY,
    .init        = video_init,
    .uninit      = uninit,
    .priv_size   = sizeof(SegmentContext),
    .activate    = activate,
    FILTER_INPUTS(segment_inputs),
};
#endif // CONFIG_SEGMENT_FILTER

#if CONFIG_ASEGMENT_FILTER

static av_cold int audio_init(AVFilterContext *ctx)
{
    return init(ctx, AVMEDIA_TYPE_AUDIO);
}

#define FLAGS AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_FILTERING_PARAM
static const AVOption asegment_options[] = {
    COMMON_OPTS
    { "samples", "samples at which to split input", OFFSET(points_str), AV_OPT_TYPE_STRING,  { .str = NULL }, 0, 0, FLAGS },
    { NULL }
};
#undef FLAGS

AVFILTER_DEFINE_CLASS(asegment);

static const AVFilterPad asegment_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
    },
};

const FFFilter ff_af_asegment = {
    .p.name        = "asegment",
    .p.description = NULL_IF_CONFIG_SMALL("Segment audio stream."),
    .p.priv_class  = &asegment_class,
    .p.flags       = AVFILTER_FLAG_DYNAMIC_OUTPUTS | AVFILTER_FLAG_METADATA_ONLY,
    .init        = audio_init,
    .uninit      = uninit,
    .priv_size   = sizeof(SegmentContext),
    .activate    = activate,
    FILTER_INPUTS(asegment_inputs),
};
#endif // CONFIG_ASEGMENT_FILTER
