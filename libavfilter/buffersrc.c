/*
 * Copyright (c) 2008 Vitor Sessak
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
 * memory buffer source filter
 */

#include <float.h>

#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/fifo.h"
#include "libavutil/frame.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"
#include "audio.h"
#include "avfilter.h"
#include "buffersrc.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct BufferSourceContext {
    const AVClass    *class;
    AVFifoBuffer     *fifo;
    AVRational        time_base;     ///< time_base to set in the output link
    AVRational        frame_rate;    ///< frame_rate to set in the output link

    /* video only */
    int               h, w;
    enum AVPixelFormat  pix_fmt;
    char               *pix_fmt_str;
    AVRational        pixel_aspect;

    AVBufferRef *hw_frames_ctx;

    /* audio only */
    int sample_rate;
    enum AVSampleFormat sample_fmt;
    char               *sample_fmt_str;
    uint64_t channel_layout;
    char    *channel_layout_str;

    int got_format_from_params;
    int eof;
} BufferSourceContext;

#define CHECK_VIDEO_PARAM_CHANGE(s, c, width, height, format)\
    if (c->w != width || c->h != height || c->pix_fmt != format) {\
        av_log(s, AV_LOG_ERROR, "Changing frame properties on the fly is not supported.\n");\
        return AVERROR(EINVAL);\
    }

#define CHECK_AUDIO_PARAM_CHANGE(s, c, srate, ch_layout, format)\
    if (c->sample_fmt != format || c->sample_rate != srate ||\
        c->channel_layout != ch_layout) {\
        av_log(s, AV_LOG_ERROR, "Changing frame properties on the fly is not supported.\n");\
        return AVERROR(EINVAL);\
    }

AVBufferSrcParameters *av_buffersrc_parameters_alloc(void)
{
    AVBufferSrcParameters *par = av_mallocz(sizeof(*par));
    if (!par)
        return NULL;

    par->format = -1;

    return par;
}

int av_buffersrc_parameters_set(AVFilterContext *ctx, AVBufferSrcParameters *param)
{
    BufferSourceContext *s = ctx->priv;

    if (param->time_base.num > 0 && param->time_base.den > 0)
        s->time_base = param->time_base;

    switch (ctx->filter->outputs[0].type) {
    case AVMEDIA_TYPE_VIDEO:
        if (param->format != AV_PIX_FMT_NONE) {
            s->got_format_from_params = 1;
            s->pix_fmt = param->format;
        }
        if (param->width > 0)
            s->w = param->width;
        if (param->height > 0)
            s->h = param->height;
        if (param->sample_aspect_ratio.num > 0 && param->sample_aspect_ratio.den > 0)
            s->pixel_aspect = param->sample_aspect_ratio;
        if (param->frame_rate.num > 0 && param->frame_rate.den > 0)
            s->frame_rate = param->frame_rate;
        if (param->hw_frames_ctx) {
            av_buffer_unref(&s->hw_frames_ctx);
            s->hw_frames_ctx = av_buffer_ref(param->hw_frames_ctx);
            if (!s->hw_frames_ctx)
                return AVERROR(ENOMEM);
        }
        break;
    case AVMEDIA_TYPE_AUDIO:
        if (param->format != AV_SAMPLE_FMT_NONE) {
            s->got_format_from_params = 1;
            s->sample_fmt = param->format;
        }
        if (param->sample_rate > 0)
            s->sample_rate = param->sample_rate;
        if (param->channel_layout)
            s->channel_layout = param->channel_layout;
        break;
    default:
        return AVERROR_BUG;
    }

    return 0;
}

int attribute_align_arg av_buffersrc_write_frame(AVFilterContext *ctx, const AVFrame *frame)
{
    AVFrame *copy;
    int ret = 0;

    if (!(copy = av_frame_alloc()))
        return AVERROR(ENOMEM);
    ret = av_frame_ref(copy, frame);
    if (ret >= 0)
        ret = av_buffersrc_add_frame(ctx, copy);

    av_frame_free(&copy);
    return ret;
}

int attribute_align_arg av_buffersrc_add_frame(AVFilterContext *ctx,
                                               AVFrame *frame)
{
    BufferSourceContext *s = ctx->priv;
    AVFrame *copy;
    int refcounted, ret;

    if (!frame) {
        s->eof = 1;
        return 0;
    } else if (s->eof)
        return AVERROR(EINVAL);

    refcounted = !!frame->buf[0];

    switch (ctx->outputs[0]->type) {
    case AVMEDIA_TYPE_VIDEO:
        CHECK_VIDEO_PARAM_CHANGE(ctx, s, frame->width, frame->height,
                                 frame->format);
        break;
    case AVMEDIA_TYPE_AUDIO:
        CHECK_AUDIO_PARAM_CHANGE(ctx, s, frame->sample_rate, frame->channel_layout,
                                 frame->format);
        break;
    default:
        return AVERROR(EINVAL);
    }

    if (!av_fifo_space(s->fifo) &&
        (ret = av_fifo_realloc2(s->fifo, av_fifo_size(s->fifo) +
                                         sizeof(copy))) < 0)
        return ret;

    if (!(copy = av_frame_alloc()))
        return AVERROR(ENOMEM);

    if (refcounted) {
        av_frame_move_ref(copy, frame);
    } else {
        ret = av_frame_ref(copy, frame);
        if (ret < 0) {
            av_frame_free(&copy);
            return ret;
        }
    }

    if ((ret = av_fifo_generic_write(s->fifo, &copy, sizeof(copy), NULL)) < 0) {
        if (refcounted)
            av_frame_move_ref(frame, copy);
        av_frame_free(&copy);
        return ret;
    }

    return 0;
}

static av_cold int init_video(AVFilterContext *ctx)
{
    BufferSourceContext *c = ctx->priv;

    if (!(c->pix_fmt_str || c->got_format_from_params)  || !c->w || !c->h ||
        av_q2d(c->time_base) <= 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid parameters provided.\n");
        return AVERROR(EINVAL);
    }

    if (c->pix_fmt_str) {
        if ((c->pix_fmt = av_get_pix_fmt(c->pix_fmt_str)) == AV_PIX_FMT_NONE) {
            char *tail;
            c->pix_fmt = strtol(c->pix_fmt_str, &tail, 10);
            if (*tail || c->pix_fmt < 0 || !av_pix_fmt_desc_get(c->pix_fmt)) {
                av_log(ctx, AV_LOG_ERROR, "Invalid pixel format string '%s'\n", c->pix_fmt_str);
                return AVERROR(EINVAL);
            }
        }
    }

    if (!(c->fifo = av_fifo_alloc(sizeof(AVFrame*))))
        return AVERROR(ENOMEM);

    av_log(ctx, AV_LOG_VERBOSE, "w:%d h:%d pixfmt:%s tb:%d/%d sar:%d/%d\n",
           c->w, c->h, av_get_pix_fmt_name(c->pix_fmt),
           c->time_base.num, c->time_base.den,
           c->pixel_aspect.num, c->pixel_aspect.den);
    return 0;
}

#define OFFSET(x) offsetof(BufferSourceContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
#define V AV_OPT_FLAG_VIDEO_PARAM

static const AVOption video_options[] = {
    { "width",         NULL,                     OFFSET(w),                AV_OPT_TYPE_INT,      { .i64 = 0 }, 0, INT_MAX, V },
    { "height",        NULL,                     OFFSET(h),                AV_OPT_TYPE_INT,      { .i64 = 0 }, 0, INT_MAX, V },
    { "pix_fmt",       NULL,                     OFFSET(pix_fmt_str),      AV_OPT_TYPE_STRING,                    .flags = V },
#if FF_API_OLD_FILTER_OPTS
    /* those 4 are for compatibility with the old option passing system where each filter
     * did its own parsing */
    { "time_base_num", "deprecated, do not use", OFFSET(time_base.num),    AV_OPT_TYPE_INT,      { .i64 = 0 }, 0, INT_MAX, V },
    { "time_base_den", "deprecated, do not use", OFFSET(time_base.den),    AV_OPT_TYPE_INT,      { .i64 = 0 }, 0, INT_MAX, V },
    { "sar_num",       "deprecated, do not use", OFFSET(pixel_aspect.num), AV_OPT_TYPE_INT,      { .i64 = 0 }, 0, INT_MAX, V },
    { "sar_den",       "deprecated, do not use", OFFSET(pixel_aspect.den), AV_OPT_TYPE_INT,      { .i64 = 0 }, 0, INT_MAX, V },
#endif
    { "sar",           "sample aspect ratio",    OFFSET(pixel_aspect),     AV_OPT_TYPE_RATIONAL, { .dbl = 0 }, 0, DBL_MAX, V },
    { "time_base",     NULL,                     OFFSET(time_base),        AV_OPT_TYPE_RATIONAL, { .dbl = 0 }, 0, DBL_MAX, V },
    { "frame_rate",    NULL,                     OFFSET(frame_rate),       AV_OPT_TYPE_RATIONAL, { .dbl = 0 }, 0, DBL_MAX, V },
    { NULL },
};

static const AVClass buffer_class = {
    .class_name = "buffer source",
    .item_name  = av_default_item_name,
    .option     = video_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVOption audio_options[] = {
    { "time_base",      NULL, OFFSET(time_base),           AV_OPT_TYPE_RATIONAL, { .dbl = 0 }, 0, INT_MAX, A },
    { "sample_rate",    NULL, OFFSET(sample_rate),         AV_OPT_TYPE_INT,      { .i64 = 0 }, 0, INT_MAX, A },
    { "sample_fmt",     NULL, OFFSET(sample_fmt_str),      AV_OPT_TYPE_STRING,             .flags = A },
    { "channel_layout", NULL, OFFSET(channel_layout_str),  AV_OPT_TYPE_STRING,             .flags = A },
    { NULL },
};

static const AVClass abuffer_class = {
    .class_name = "abuffer source",
    .item_name  = av_default_item_name,
    .option     = audio_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static av_cold int init_audio(AVFilterContext *ctx)
{
    BufferSourceContext *s = ctx->priv;
    int ret = 0;

    if (!(s->sample_fmt_str || s->got_format_from_params)) {
        av_log(ctx, AV_LOG_ERROR, "Sample format not provided\n");
        return AVERROR(EINVAL);
    }
    if (s->sample_fmt_str)
        s->sample_fmt = av_get_sample_fmt(s->sample_fmt_str);

    if (s->sample_fmt == AV_SAMPLE_FMT_NONE) {
        av_log(ctx, AV_LOG_ERROR, "Invalid sample format %s.\n",
               s->sample_fmt_str);
        return AVERROR(EINVAL);
    }

    if (s->channel_layout_str)
        s->channel_layout = av_get_channel_layout(s->channel_layout_str);

    if (!s->channel_layout) {
        av_log(ctx, AV_LOG_ERROR, "Invalid channel layout %s.\n",
               s->channel_layout_str);
        return AVERROR(EINVAL);
    }

    if (!(s->fifo = av_fifo_alloc(sizeof(AVFrame*))))
        return AVERROR(ENOMEM);

    if (!s->time_base.num)
        s->time_base = (AVRational){1, s->sample_rate};

    av_log(ctx, AV_LOG_VERBOSE, "tb:%d/%d samplefmt:%s samplerate: %d "
           "ch layout:%s\n", s->time_base.num, s->time_base.den, s->sample_fmt_str,
           s->sample_rate, s->channel_layout_str);

    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    BufferSourceContext *s = ctx->priv;
    while (s->fifo && av_fifo_size(s->fifo)) {
        AVFrame *frame;
        av_fifo_generic_read(s->fifo, &frame, sizeof(frame), NULL);
        av_frame_free(&frame);
    }
    av_buffer_unref(&s->hw_frames_ctx);
    av_fifo_free(s->fifo);
    s->fifo = NULL;
}

static int query_formats(AVFilterContext *ctx)
{
    BufferSourceContext *c = ctx->priv;
    AVFilterChannelLayouts *channel_layouts = NULL;
    AVFilterFormats *formats = NULL;
    AVFilterFormats *samplerates = NULL;

    switch (ctx->outputs[0]->type) {
    case AVMEDIA_TYPE_VIDEO:
        ff_add_format(&formats, c->pix_fmt);
        ff_set_common_formats(ctx, formats);
        break;
    case AVMEDIA_TYPE_AUDIO:
        ff_add_format(&formats,           c->sample_fmt);
        ff_set_common_formats(ctx, formats);

        ff_add_format(&samplerates,       c->sample_rate);
        ff_set_common_samplerates(ctx, samplerates);

        ff_add_channel_layout(&channel_layouts, c->channel_layout);
        ff_set_common_channel_layouts(ctx, channel_layouts);
        break;
    default:
        return AVERROR(EINVAL);
    }

    return 0;
}

static int config_props(AVFilterLink *link)
{
    BufferSourceContext *c = link->src->priv;

    switch (link->type) {
    case AVMEDIA_TYPE_VIDEO:
        link->w = c->w;
        link->h = c->h;
        link->sample_aspect_ratio = c->pixel_aspect;

        if (c->hw_frames_ctx) {
            link->hw_frames_ctx = av_buffer_ref(c->hw_frames_ctx);
            if (!link->hw_frames_ctx)
                return AVERROR(ENOMEM);
        }
        break;
    case AVMEDIA_TYPE_AUDIO:
        link->channel_layout = c->channel_layout;
        link->sample_rate    = c->sample_rate;
        break;
    default:
        return AVERROR(EINVAL);
    }

    link->time_base = c->time_base;
    link->frame_rate = c->frame_rate;
    return 0;
}

static int request_frame(AVFilterLink *link)
{
    BufferSourceContext *c = link->src->priv;
    AVFrame *frame;
    int ret = 0;

    if (!av_fifo_size(c->fifo)) {
        if (c->eof)
            return AVERROR_EOF;
        return AVERROR(EAGAIN);
    }
    av_fifo_generic_read(c->fifo, &frame, sizeof(frame), NULL);

    ret = ff_filter_frame(link, frame);

    return ret;
}

static int poll_frame(AVFilterLink *link)
{
    BufferSourceContext *c = link->src->priv;
    int size = av_fifo_size(c->fifo);
    if (!size && c->eof)
        return AVERROR_EOF;
    return size/sizeof(AVFrame*);
}

static const AVFilterPad avfilter_vsrc_buffer_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
        .poll_frame    = poll_frame,
        .config_props  = config_props,
    },
    { NULL }
};

AVFilter ff_vsrc_buffer = {
    .name      = "buffer",
    .description = NULL_IF_CONFIG_SMALL("Buffer video frames, and make them accessible to the filterchain."),
    .priv_size = sizeof(BufferSourceContext),
    .priv_class = &buffer_class,
    .query_formats = query_formats,

    .init      = init_video,
    .uninit    = uninit,

    .inputs    = NULL,
    .outputs   = avfilter_vsrc_buffer_outputs,
};

static const AVFilterPad avfilter_asrc_abuffer_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .request_frame = request_frame,
        .poll_frame    = poll_frame,
        .config_props  = config_props,
    },
    { NULL }
};

AVFilter ff_asrc_abuffer = {
    .name          = "abuffer",
    .description   = NULL_IF_CONFIG_SMALL("Buffer audio frames, and make them accessible to the filterchain."),
    .priv_size     = sizeof(BufferSourceContext),
    .priv_class    = &abuffer_class,
    .query_formats = query_formats,

    .init      = init_audio,
    .uninit    = uninit,

    .inputs    = NULL,
    .outputs   = avfilter_asrc_abuffer_outputs,
};
