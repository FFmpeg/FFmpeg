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

#include "libavutil/audio_fifo.h"
#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"

#include "audio.h"
#include "avfilter.h"
#include "buffersink.h"
#include "internal.h"

typedef struct BufferSinkContext {
    const AVClass *class;
    AVFifoBuffer *fifo;                      ///< FIFO buffer of video frame references
    unsigned warning_limit;

    /* only used for video */
    enum AVPixelFormat *pixel_fmts;           ///< list of accepted pixel formats, must be terminated with -1
    int pixel_fmts_size;

    /* only used for audio */
    enum AVSampleFormat *sample_fmts;       ///< list of accepted sample formats, terminated by AV_SAMPLE_FMT_NONE
    int sample_fmts_size;
    int64_t *channel_layouts;               ///< list of accepted channel layouts, terminated by -1
    int channel_layouts_size;
    int *channel_counts;                    ///< list of accepted channel counts, terminated by -1
    int channel_counts_size;
    int all_channel_counts;
    int *sample_rates;                      ///< list of accepted sample rates, terminated by -1
    int sample_rates_size;

    /* only used for compat API */
    AVAudioFifo *audio_fifo;     ///< FIFO for audio samples
    int64_t next_pts;            ///< interpolating audio pts
} BufferSinkContext;

#define NB_ITEMS(list) (list ## _size / sizeof(*list))

static av_cold void uninit(AVFilterContext *ctx)
{
    BufferSinkContext *sink = ctx->priv;
    AVFrame *frame;

    if (sink->audio_fifo)
        av_audio_fifo_free(sink->audio_fifo);

    if (sink->fifo) {
        while (av_fifo_size(sink->fifo) >= sizeof(AVFilterBufferRef *)) {
            av_fifo_generic_read(sink->fifo, &frame, sizeof(frame), NULL);
            av_frame_free(&frame);
        }
        av_fifo_free(sink->fifo);
        sink->fifo = NULL;
    }
}

static int add_buffer_ref(AVFilterContext *ctx, AVFrame *ref)
{
    BufferSinkContext *buf = ctx->priv;

    if (av_fifo_space(buf->fifo) < sizeof(AVFilterBufferRef *)) {
        /* realloc fifo size */
        if (av_fifo_realloc2(buf->fifo, av_fifo_size(buf->fifo) * 2) < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Cannot buffer more frames. Consume some available frames "
                   "before adding new ones.\n");
            return AVERROR(ENOMEM);
        }
    }

    /* cache frame */
    av_fifo_generic_write(buf->fifo, &ref, sizeof(AVFilterBufferRef *), NULL);
    return 0;
}

static int filter_frame(AVFilterLink *link, AVFrame *frame)
{
    AVFilterContext *ctx = link->dst;
    BufferSinkContext *buf = link->dst->priv;
    int ret;

    if ((ret = add_buffer_ref(ctx, frame)) < 0)
        return ret;
    if (buf->warning_limit &&
        av_fifo_size(buf->fifo) / sizeof(AVFilterBufferRef *) >= buf->warning_limit) {
        av_log(ctx, AV_LOG_WARNING,
               "%d buffers queued in %s, something may be wrong.\n",
               buf->warning_limit,
               (char *)av_x_if_null(ctx->name, ctx->filter->name));
        buf->warning_limit *= 10;
    }
    return 0;
}

int attribute_align_arg av_buffersink_get_frame(AVFilterContext *ctx, AVFrame *frame)
{
    return av_buffersink_get_frame_flags(ctx, frame, 0);
}

int attribute_align_arg av_buffersink_get_frame_flags(AVFilterContext *ctx, AVFrame *frame, int flags)
{
    BufferSinkContext *buf = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    int ret;
    AVFrame *cur_frame;

    /* no picref available, fetch it from the filterchain */
    if (!av_fifo_size(buf->fifo)) {
        if (flags & AV_BUFFERSINK_FLAG_NO_REQUEST)
            return AVERROR(EAGAIN);
        if ((ret = ff_request_frame(inlink)) < 0)
            return ret;
    }

    if (!av_fifo_size(buf->fifo))
        return AVERROR(EINVAL);

    if (flags & AV_BUFFERSINK_FLAG_PEEK) {
        cur_frame = *((AVFrame **)av_fifo_peek2(buf->fifo, 0));
        if ((ret = av_frame_ref(frame, cur_frame)) < 0)
            return ret;
    } else {
        av_fifo_generic_read(buf->fifo, &cur_frame, sizeof(cur_frame), NULL);
        av_frame_move_ref(frame, cur_frame);
        av_frame_free(&cur_frame);
    }

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
    if (s->next_pts != AV_NOPTS_VALUE)
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
    AVFrame *cur_frame;
    int ret = 0;

    if (!s->audio_fifo) {
        int nb_channels = link->channels;
        if (!(s->audio_fifo = av_audio_fifo_alloc(link->format, nb_channels, nb_samples)))
            return AVERROR(ENOMEM);
    }

    while (ret >= 0) {
        if (av_audio_fifo_size(s->audio_fifo) >= nb_samples)
            return read_from_fifo(ctx, frame, nb_samples);

        if (!(cur_frame = av_frame_alloc()))
            return AVERROR(ENOMEM);
        ret = av_buffersink_get_frame_flags(ctx, cur_frame, 0);
        if (ret == AVERROR_EOF && av_audio_fifo_size(s->audio_fifo)) {
            av_frame_free(&cur_frame);
            return read_from_fifo(ctx, frame, av_audio_fifo_size(s->audio_fifo));
        } else if (ret < 0) {
            av_frame_free(&cur_frame);
            return ret;
        }

        if (cur_frame->pts != AV_NOPTS_VALUE) {
            s->next_pts = cur_frame->pts -
                          av_rescale_q(av_audio_fifo_size(s->audio_fifo),
                                       (AVRational){ 1, link->sample_rate },
                                       link->time_base);
        }

        ret = av_audio_fifo_write(s->audio_fifo, (void**)cur_frame->extended_data,
                                  cur_frame->nb_samples);
        av_frame_free(&cur_frame);
    }

    return ret;
}

AVBufferSinkParams *av_buffersink_params_alloc(void)
{
    static const int pixel_fmts[] = { AV_PIX_FMT_NONE };
    AVBufferSinkParams *params = av_malloc(sizeof(AVBufferSinkParams));
    if (!params)
        return NULL;

    params->pixel_fmts = pixel_fmts;
    return params;
}

AVABufferSinkParams *av_abuffersink_params_alloc(void)
{
    AVABufferSinkParams *params = av_mallocz(sizeof(AVABufferSinkParams));

    if (!params)
        return NULL;
    return params;
}

#define FIFO_INIT_SIZE 8

static av_cold int common_init(AVFilterContext *ctx)
{
    BufferSinkContext *buf = ctx->priv;

    buf->fifo = av_fifo_alloc(FIFO_INIT_SIZE*sizeof(AVFilterBufferRef *));
    if (!buf->fifo) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate fifo\n");
        return AVERROR(ENOMEM);
    }
    buf->warning_limit = 100;
    buf->next_pts = AV_NOPTS_VALUE;
    return 0;
}

void av_buffersink_set_frame_size(AVFilterContext *ctx, unsigned frame_size)
{
    AVFilterLink *inlink = ctx->inputs[0];

    inlink->min_samples = inlink->max_samples =
    inlink->partial_buf_size = frame_size;
}

#if FF_API_AVFILTERBUFFER
FF_DISABLE_DEPRECATION_WARNINGS
static void compat_free_buffer(AVFilterBuffer *buf)
{
    AVFrame *frame = buf->priv;
    av_frame_free(&frame);
    av_free(buf);
}

static int compat_read(AVFilterContext *ctx,
                       AVFilterBufferRef **pbuf, int nb_samples, int flags)
{
    AVFilterBufferRef *buf;
    AVFrame *frame;
    int ret;

    if (!pbuf)
        return ff_poll_frame(ctx->inputs[0]);

    frame = av_frame_alloc();
    if (!frame)
        return AVERROR(ENOMEM);

    if (!nb_samples)
        ret = av_buffersink_get_frame_flags(ctx, frame, flags);
    else
        ret = av_buffersink_get_samples(ctx, frame, nb_samples);

    if (ret < 0)
        goto fail;

    AV_NOWARN_DEPRECATED(
    if (ctx->inputs[0]->type == AVMEDIA_TYPE_VIDEO) {
        buf = avfilter_get_video_buffer_ref_from_arrays(frame->data, frame->linesize,
                                                        AV_PERM_READ,
                                                        frame->width, frame->height,
                                                        frame->format);
    } else {
        buf = avfilter_get_audio_buffer_ref_from_arrays(frame->extended_data,
                                                        frame->linesize[0], AV_PERM_READ,
                                                        frame->nb_samples,
                                                        frame->format,
                                                        frame->channel_layout);
    }
    if (!buf) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    avfilter_copy_frame_props(buf, frame);
    )

    buf->buf->priv = frame;
    buf->buf->free = compat_free_buffer;

    *pbuf = buf;

    return 0;
fail:
    av_frame_free(&frame);
    return ret;
}

int attribute_align_arg av_buffersink_read(AVFilterContext *ctx, AVFilterBufferRef **buf)
{
    return compat_read(ctx, buf, 0, 0);
}

int attribute_align_arg av_buffersink_read_samples(AVFilterContext *ctx, AVFilterBufferRef **buf,
                                                   int nb_samples)
{
    return compat_read(ctx, buf, nb_samples, 0);
}

int attribute_align_arg av_buffersink_get_buffer_ref(AVFilterContext *ctx,
                                                     AVFilterBufferRef **bufref, int flags)
{
    *bufref = NULL;

    av_assert0(    !strcmp(ctx->filter->name, "buffersink")
                || !strcmp(ctx->filter->name, "abuffersink")
                || !strcmp(ctx->filter->name, "ffbuffersink")
                || !strcmp(ctx->filter->name, "ffabuffersink"));

    return compat_read(ctx, bufref, 0, flags);
}
FF_ENABLE_DEPRECATION_WARNINGS
#endif

AVRational av_buffersink_get_frame_rate(AVFilterContext *ctx)
{
    av_assert0(   !strcmp(ctx->filter->name, "buffersink")
               || !strcmp(ctx->filter->name, "ffbuffersink"));

    return ctx->inputs[0]->frame_rate;
}

int attribute_align_arg av_buffersink_poll_frame(AVFilterContext *ctx)
{
    BufferSinkContext *buf = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];

    av_assert0(   !strcmp(ctx->filter->name, "buffersink")
               || !strcmp(ctx->filter->name, "abuffersink")
               || !strcmp(ctx->filter->name, "ffbuffersink")
               || !strcmp(ctx->filter->name, "ffabuffersink"));

    return av_fifo_size(buf->fifo)/sizeof(AVFilterBufferRef *) + ff_poll_frame(inlink);
}

static av_cold int vsink_init(AVFilterContext *ctx, void *opaque)
{
    BufferSinkContext *buf = ctx->priv;
    AVBufferSinkParams *params = opaque;
    int ret;

    if (params) {
        if ((ret = av_opt_set_int_list(buf, "pix_fmts", params->pixel_fmts, AV_PIX_FMT_NONE, 0)) < 0)
            return ret;
    }

    return common_init(ctx);
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
    AVFilterFormats *formats = NULL;
    unsigned i;
    int ret;

    CHECK_LIST_SIZE(pixel_fmts)
    if (buf->pixel_fmts_size) {
        for (i = 0; i < NB_ITEMS(buf->pixel_fmts); i++)
            if ((ret = ff_add_format(&formats, buf->pixel_fmts[i])) < 0) {
                ff_formats_unref(&formats);
                return ret;
            }
        ff_set_common_formats(ctx, formats);
    } else {
        ff_default_query_formats(ctx);
    }

    return 0;
}

static av_cold int asink_init(AVFilterContext *ctx, void *opaque)
{
    BufferSinkContext *buf = ctx->priv;
    AVABufferSinkParams *params = opaque;
    int ret;

    if (params) {
        if ((ret = av_opt_set_int_list(buf, "sample_fmts",     params->sample_fmts,  AV_SAMPLE_FMT_NONE, 0)) < 0 ||
            (ret = av_opt_set_int_list(buf, "sample_rates",    params->sample_rates,    -1, 0)) < 0 ||
            (ret = av_opt_set_int_list(buf, "channel_layouts", params->channel_layouts, -1, 0)) < 0 ||
            (ret = av_opt_set_int_list(buf, "channel_counts",  params->channel_counts,  -1, 0)) < 0 ||
            (ret = av_opt_set_int(buf, "all_channel_counts", params->all_channel_counts, 0)) < 0)
            return ret;
    }
    return common_init(ctx);
}

static int asink_query_formats(AVFilterContext *ctx)
{
    BufferSinkContext *buf = ctx->priv;
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    unsigned i;
    int ret;

    CHECK_LIST_SIZE(sample_fmts)
    CHECK_LIST_SIZE(sample_rates)
    CHECK_LIST_SIZE(channel_layouts)
    CHECK_LIST_SIZE(channel_counts)

    if (buf->sample_fmts_size) {
        for (i = 0; i < NB_ITEMS(buf->sample_fmts); i++)
            if ((ret = ff_add_format(&formats, buf->sample_fmts[i])) < 0) {
                ff_formats_unref(&formats);
                return ret;
            }
        ff_set_common_formats(ctx, formats);
    }

    if (buf->channel_layouts_size || buf->channel_counts_size ||
        buf->all_channel_counts) {
        for (i = 0; i < NB_ITEMS(buf->channel_layouts); i++)
            if ((ret = ff_add_channel_layout(&layouts, buf->channel_layouts[i])) < 0) {
                ff_channel_layouts_unref(&layouts);
                return ret;
            }
        for (i = 0; i < NB_ITEMS(buf->channel_counts); i++)
            if ((ret = ff_add_channel_layout(&layouts, FF_COUNT2LAYOUT(buf->channel_counts[i]))) < 0) {
                ff_channel_layouts_unref(&layouts);
                return ret;
            }
        if (buf->all_channel_counts) {
            if (layouts)
                av_log(ctx, AV_LOG_WARNING,
                       "Conflicting all_channel_counts and list in options\n");
            else if (!(layouts = ff_all_channel_counts()))
                return AVERROR(ENOMEM);
        }
        ff_set_common_channel_layouts(ctx, layouts);
    }

    if (buf->sample_rates_size) {
        formats = NULL;
        for (i = 0; i < NB_ITEMS(buf->sample_rates); i++)
            if ((ret = ff_add_format(&formats, buf->sample_rates[i])) < 0) {
                ff_formats_unref(&formats);
                return ret;
            }
        ff_set_common_samplerates(ctx, formats);
    }

    return 0;
}

#define OFFSET(x) offsetof(BufferSinkContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption buffersink_options[] = {
    { "pix_fmts", "set the supported pixel formats", OFFSET(pixel_fmts), AV_OPT_TYPE_BINARY, .flags = FLAGS },
    { NULL },
};
#undef FLAGS
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption abuffersink_options[] = {
    { "sample_fmts",     "set the supported sample formats",  OFFSET(sample_fmts),     AV_OPT_TYPE_BINARY, .flags = FLAGS },
    { "sample_rates",    "set the supported sample rates",    OFFSET(sample_rates),    AV_OPT_TYPE_BINARY, .flags = FLAGS },
    { "channel_layouts", "set the supported channel layouts", OFFSET(channel_layouts), AV_OPT_TYPE_BINARY, .flags = FLAGS },
    { "channel_counts",  "set the supported channel counts",  OFFSET(channel_counts),  AV_OPT_TYPE_BINARY, .flags = FLAGS },
    { "all_channel_counts", "accept all channel counts", OFFSET(all_channel_counts), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, FLAGS },
    { NULL },
};
#undef FLAGS

AVFILTER_DEFINE_CLASS(buffersink);
AVFILTER_DEFINE_CLASS(abuffersink);

#if FF_API_AVFILTERBUFFER

#define ffbuffersink_options buffersink_options
#define ffabuffersink_options abuffersink_options
AVFILTER_DEFINE_CLASS(ffbuffersink);
AVFILTER_DEFINE_CLASS(ffabuffersink);

static const AVFilterPad ffbuffersink_inputs[] = {
    {
        .name      = "default",
        .type      = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL },
};

AVFilter ff_vsink_ffbuffersink = {
    .name      = "ffbuffersink",
    .description = NULL_IF_CONFIG_SMALL("Buffer video frames, and make them available to the end of the filter graph."),
    .priv_size = sizeof(BufferSinkContext),
    .priv_class = &ffbuffersink_class,
    .init_opaque = vsink_init,
    .uninit    = uninit,

    .query_formats = vsink_query_formats,
    .inputs        = ffbuffersink_inputs,
    .outputs       = NULL,
};

static const AVFilterPad ffabuffersink_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_AUDIO,
        .filter_frame   = filter_frame,
    },
    { NULL },
};

AVFilter ff_asink_ffabuffersink = {
    .name      = "ffabuffersink",
    .description = NULL_IF_CONFIG_SMALL("Buffer audio frames, and make them available to the end of the filter graph."),
    .init_opaque = asink_init,
    .uninit    = uninit,
    .priv_size = sizeof(BufferSinkContext),
    .priv_class = &ffabuffersink_class,
    .query_formats = asink_query_formats,
    .inputs        = ffabuffersink_inputs,
    .outputs       = NULL,
};
#endif /* FF_API_AVFILTERBUFFER */

static const AVFilterPad avfilter_vsink_buffer_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

AVFilter ff_vsink_buffer = {
    .name        = "buffersink",
    .description = NULL_IF_CONFIG_SMALL("Buffer video frames, and make them available to the end of the filter graph."),
    .priv_size   = sizeof(BufferSinkContext),
    .priv_class  = &buffersink_class,
    .init_opaque = vsink_init,
    .uninit      = uninit,

    .query_formats = vsink_query_formats,
    .inputs      = avfilter_vsink_buffer_inputs,
    .outputs     = NULL,
};

static const AVFilterPad avfilter_asink_abuffer_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

AVFilter ff_asink_abuffer = {
    .name        = "abuffersink",
    .description = NULL_IF_CONFIG_SMALL("Buffer audio frames, and make them available to the end of the filter graph."),
    .priv_class  = &abuffersink_class,
    .priv_size   = sizeof(BufferSinkContext),
    .init_opaque = asink_init,
    .uninit      = uninit,

    .query_formats = asink_query_formats,
    .inputs      = avfilter_asink_abuffer_inputs,
    .outputs     = NULL,
};
