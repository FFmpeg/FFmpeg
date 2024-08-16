/*
 * Copyright (c) 2011 Stefano Sabatini
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
 * buffer sink
 */

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"

#include "audio.h"
#include "avfilter.h"
#include "avfilter_internal.h"
#include "buffersink.h"
#include "filters.h"
#include "formats.h"
#include "framequeue.h"
#include "video.h"

typedef struct BufferSinkContext {
    const AVClass *class;
    unsigned warning_limit;

    /* only used for video */
    enum AVPixelFormat *pixel_fmts;     ///< list of accepted pixel formats
    int pixel_fmts_size;
    enum AVColorSpace *color_spaces;    ///< list of accepted color spaces
    int color_spaces_size;
    enum AVColorRange *color_ranges;    ///< list of accepted color ranges
    int color_ranges_size;

    /* only used for audio */
    enum AVSampleFormat *sample_fmts;   ///< list of accepted sample formats
    int sample_fmts_size;
    char *channel_layouts_str;          ///< list of accepted channel layouts
    int all_channel_counts;
    int *sample_rates;                  ///< list of accepted sample rates
    int sample_rates_size;

    AVFrame *peeked_frame;
} BufferSinkContext;

#define NB_ITEMS(list) (list ## _size / sizeof(*list))

int attribute_align_arg av_buffersink_get_frame(AVFilterContext *ctx, AVFrame *frame)
{
    return av_buffersink_get_frame_flags(ctx, frame, 0);
}

static int return_or_keep_frame(BufferSinkContext *buf, AVFrame *out, AVFrame *in, int flags)
{
    if ((flags & AV_BUFFERSINK_FLAG_PEEK)) {
        buf->peeked_frame = in;
        return out ? av_frame_ref(out, in) : 0;
    } else {
        av_assert1(out);
        buf->peeked_frame = NULL;
        av_frame_move_ref(out, in);
        av_frame_free(&in);
        return 0;
    }
}

static int get_frame_internal(AVFilterContext *ctx, AVFrame *frame, int flags, int samples)
{
    BufferSinkContext *buf = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    FilterLinkInternal *li = ff_link_internal(inlink);
    int status, ret;
    AVFrame *cur_frame;
    int64_t pts;

    if (buf->peeked_frame)
        return return_or_keep_frame(buf, frame, buf->peeked_frame, flags);

    while (1) {
        ret = samples ? ff_inlink_consume_samples(inlink, samples, samples, &cur_frame) :
                        ff_inlink_consume_frame(inlink, &cur_frame);
        if (ret < 0) {
            return ret;
        } else if (ret) {
            /* TODO return the frame instead of copying it */
            return return_or_keep_frame(buf, frame, cur_frame, flags);
        } else if (ff_inlink_acknowledge_status(inlink, &status, &pts)) {
            return status;
        } else if ((flags & AV_BUFFERSINK_FLAG_NO_REQUEST)) {
            return AVERROR(EAGAIN);
        } else if (li->frame_wanted_out) {
            ret = ff_filter_graph_run_once(ctx->graph);
            if (ret < 0)
                return ret;
        } else {
            ff_inlink_request_frame(inlink);
        }
    }
}

int attribute_align_arg av_buffersink_get_frame_flags(AVFilterContext *ctx, AVFrame *frame, int flags)
{
    return get_frame_internal(ctx, frame, flags,
                              ff_filter_link(ctx->inputs[0])->min_samples);
}

int attribute_align_arg av_buffersink_get_samples(AVFilterContext *ctx,
                                                  AVFrame *frame, int nb_samples)
{
    return get_frame_internal(ctx, frame, 0, nb_samples);
}

static av_cold int common_init(AVFilterContext *ctx)
{
    BufferSinkContext *buf = ctx->priv;

    buf->warning_limit = 100;
    return 0;
}

static void uninit(AVFilterContext *ctx)
{
    BufferSinkContext *buf = ctx->priv;

    av_frame_free(&buf->peeked_frame);
}

static int activate(AVFilterContext *ctx)
{
    BufferSinkContext *buf = ctx->priv;
    FilterLinkInternal * const li = ff_link_internal(ctx->inputs[0]);

    if (buf->warning_limit &&
        ff_framequeue_queued_frames(&li->fifo) >= buf->warning_limit) {
        av_log(ctx, AV_LOG_WARNING,
               "%d buffers queued in %s, something may be wrong.\n",
               buf->warning_limit,
               (char *)av_x_if_null(ctx->name, ctx->filter->name));
        buf->warning_limit *= 10;
    }

    /* The frame is queued, the rest is up to get_frame_internal */
    return 0;
}

void av_buffersink_set_frame_size(AVFilterContext *ctx, unsigned frame_size)
{
    FilterLink *inlink = ff_filter_link(ctx->inputs[0]);

    inlink->min_samples = inlink->max_samples = frame_size;
}

#define MAKE_AVFILTERLINK_ACCESSOR(type, field) \
type av_buffersink_get_##field(const AVFilterContext *ctx) { \
    av_assert0(ctx->filter->activate == activate); \
    return ctx->inputs[0]->field; \
}

MAKE_AVFILTERLINK_ACCESSOR(enum AVMediaType , type               )
MAKE_AVFILTERLINK_ACCESSOR(AVRational       , time_base          )
MAKE_AVFILTERLINK_ACCESSOR(int              , format             )

MAKE_AVFILTERLINK_ACCESSOR(int              , w                  )
MAKE_AVFILTERLINK_ACCESSOR(int              , h                  )
MAKE_AVFILTERLINK_ACCESSOR(AVRational       , sample_aspect_ratio)
MAKE_AVFILTERLINK_ACCESSOR(enum AVColorSpace, colorspace)
MAKE_AVFILTERLINK_ACCESSOR(enum AVColorRange, color_range)

MAKE_AVFILTERLINK_ACCESSOR(int              , sample_rate        )

AVRational av_buffersink_get_frame_rate(const AVFilterContext *ctx)
{
    FilterLink *l = ff_filter_link(ctx->inputs[0]);
    av_assert0(ctx->filter->activate == activate);
    return l->frame_rate;
}

AVBufferRef* av_buffersink_get_hw_frames_ctx(const AVFilterContext *ctx)
{
    FilterLink *l = ff_filter_link(ctx->inputs[0]);
    av_assert0(ctx->filter->activate == activate);
    return l->hw_frames_ctx;
}

int av_buffersink_get_channels(const AVFilterContext *ctx)
{
    av_assert0(ctx->filter->activate == activate);
    return ctx->inputs[0]->ch_layout.nb_channels;
}

int av_buffersink_get_ch_layout(const AVFilterContext *ctx, AVChannelLayout *out)
{
    AVChannelLayout ch_layout = { 0 };
    int ret;

    av_assert0(ctx->filter->activate == activate);
    ret = av_channel_layout_copy(&ch_layout, &ctx->inputs[0]->ch_layout);
    if (ret < 0)
        return ret;
    *out = ch_layout;
    return 0;
}

#define CHECK_LIST_SIZE(field) \
        if (buf->field ## _size % sizeof(*buf->field)) { \
            av_log(ctx, AV_LOG_ERROR, "Invalid size for " #field ": %d, " \
                   "should be multiple of %d\n", \
                   buf->field ## _size, (int)sizeof(*buf->field)); \
            return AVERROR(EINVAL); \
        }
static int vsink_query_formats(AVFilterContext *ctx)
{
    BufferSinkContext *buf = ctx->priv;
    unsigned i;
    int ret;

    CHECK_LIST_SIZE(pixel_fmts)
    CHECK_LIST_SIZE(color_spaces)
    CHECK_LIST_SIZE(color_ranges)
    if (buf->pixel_fmts_size) {
        AVFilterFormats *formats = NULL;
        for (i = 0; i < NB_ITEMS(buf->pixel_fmts); i++)
            if ((ret = ff_add_format(&formats, buf->pixel_fmts[i])) < 0)
                return ret;
        if ((ret = ff_set_common_formats(ctx, formats)) < 0)
            return ret;
    }

    if (buf->color_spaces_size) {
        AVFilterFormats *formats = NULL;
        for (i = 0; i < NB_ITEMS(buf->color_spaces); i++)
            if ((ret = ff_add_format(&formats, buf->color_spaces[i])) < 0)
                return ret;
        if ((ret = ff_set_common_color_spaces(ctx, formats)) < 0)
            return ret;
    }

    if (buf->color_ranges_size) {
        AVFilterFormats *formats = NULL;
        for (i = 0; i < NB_ITEMS(buf->color_ranges); i++)
            if ((ret = ff_add_format(&formats, buf->color_ranges[i])) < 0)
                return ret;
        if ((ret = ff_set_common_color_ranges(ctx, formats)) < 0)
            return ret;
    }

    return 0;
}

static int asink_query_formats(AVFilterContext *ctx)
{
    BufferSinkContext *buf = ctx->priv;
    AVFilterFormats *formats = NULL;
    AVChannelLayout layout = { 0 };
    AVFilterChannelLayouts *layouts = NULL;
    unsigned i;
    int ret;

    CHECK_LIST_SIZE(sample_fmts)
    CHECK_LIST_SIZE(sample_rates)

    if (buf->sample_fmts_size) {
        for (i = 0; i < NB_ITEMS(buf->sample_fmts); i++)
            if ((ret = ff_add_format(&formats, buf->sample_fmts[i])) < 0)
                return ret;
        if ((ret = ff_set_common_formats(ctx, formats)) < 0)
            return ret;
    }

    if (buf->channel_layouts_str || buf->all_channel_counts) {
        if (buf->channel_layouts_str) {
            const char *cur = buf->channel_layouts_str;

            while (cur) {
                char *next = strchr(cur, '|');
                if (next)
                    *next++ = 0;

                ret = av_channel_layout_from_string(&layout, cur);
                if (ret < 0) {
                    av_log(ctx, AV_LOG_ERROR, "Error parsing channel layout: %s.\n", cur);
                    return ret;
                }
                ret = ff_add_channel_layout(&layouts, &layout);
                av_channel_layout_uninit(&layout);
                if (ret < 0)
                    return ret;

                cur = next;
            }
        }

        if (buf->all_channel_counts) {
            if (layouts)
                av_log(ctx, AV_LOG_WARNING,
                       "Conflicting all_channel_counts and list in options\n");
            else if (!(layouts = ff_all_channel_counts()))
                return AVERROR(ENOMEM);
        }
        if ((ret = ff_set_common_channel_layouts(ctx, layouts)) < 0)
            return ret;
    }

    if (buf->sample_rates_size) {
        formats = NULL;
        for (i = 0; i < NB_ITEMS(buf->sample_rates); i++)
            if ((ret = ff_add_format(&formats, buf->sample_rates[i])) < 0)
                return ret;
        if ((ret = ff_set_common_samplerates(ctx, formats)) < 0)
            return ret;
    }

    return 0;
}

#define OFFSET(x) offsetof(BufferSinkContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption buffersink_options[] = {
    { "pix_fmts", "set the supported pixel formats", OFFSET(pixel_fmts), AV_OPT_TYPE_BINARY, .flags = FLAGS },
    { "color_spaces", "set the supported color spaces", OFFSET(color_spaces), AV_OPT_TYPE_BINARY, .flags = FLAGS },
    { "color_ranges", "set the supported color ranges", OFFSET(color_ranges), AV_OPT_TYPE_BINARY, .flags = FLAGS },
    { NULL },
};
#undef FLAGS
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_AUDIO_PARAM
static const AVOption abuffersink_options[] = {
    { "sample_fmts",     "set the supported sample formats",  OFFSET(sample_fmts),     AV_OPT_TYPE_BINARY, .flags = FLAGS },
    { "sample_rates",    "set the supported sample rates",    OFFSET(sample_rates),    AV_OPT_TYPE_BINARY, .flags = FLAGS },
    { "ch_layouts",      "set a '|'-separated list of supported channel layouts",
                         OFFSET(channel_layouts_str), AV_OPT_TYPE_STRING, .flags = FLAGS },
    { "all_channel_counts", "accept all channel counts", OFFSET(all_channel_counts), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, FLAGS },
    { NULL },
};
#undef FLAGS

AVFILTER_DEFINE_CLASS(buffersink);
AVFILTER_DEFINE_CLASS(abuffersink);

const AVFilter ff_vsink_buffer = {
    .name          = "buffersink",
    .description   = NULL_IF_CONFIG_SMALL("Buffer video frames, and make them available to the end of the filter graph."),
    .priv_size     = sizeof(BufferSinkContext),
    .priv_class    = &buffersink_class,
    .init          = common_init,
    .uninit        = uninit,
    .activate      = activate,
    FILTER_INPUTS(ff_video_default_filterpad),
    .outputs       = NULL,
    FILTER_QUERY_FUNC(vsink_query_formats),
};

const AVFilter ff_asink_abuffer = {
    .name          = "abuffersink",
    .description   = NULL_IF_CONFIG_SMALL("Buffer audio frames, and make them available to the end of the filter graph."),
    .priv_class    = &abuffersink_class,
    .priv_size     = sizeof(BufferSinkContext),
    .init          = common_init,
    .uninit        = uninit,
    .activate      = activate,
    FILTER_INPUTS(ff_audio_default_filterpad),
    .outputs       = NULL,
    FILTER_QUERY_FUNC(asink_query_formats),
};
