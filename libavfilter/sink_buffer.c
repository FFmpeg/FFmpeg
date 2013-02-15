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
#include "libavutil/channel_layout.h"
#include "libavutil/fifo.h"
#include "avfilter.h"
#include "buffersink.h"
#include "audio.h"
#include "internal.h"

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

typedef struct {
    AVFifoBuffer *fifo;                      ///< FIFO buffer of video frame references
    unsigned warning_limit;

    /* only used for video */
    enum AVPixelFormat *pixel_fmts;           ///< list of accepted pixel formats, must be terminated with -1

    /* only used for audio */
    enum AVSampleFormat *sample_fmts;       ///< list of accepted sample formats, terminated by AV_SAMPLE_FMT_NONE
    int64_t *channel_layouts;               ///< list of accepted channel layouts, terminated by -1
    int all_channel_counts;
} BufferSinkContext;

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
    return 0;
}

static av_cold void common_uninit(AVFilterContext *ctx)
{
    BufferSinkContext *buf = ctx->priv;
    AVFilterBufferRef *picref;

    if (buf->fifo) {
        while (av_fifo_size(buf->fifo) >= sizeof(AVFilterBufferRef *)) {
            av_fifo_generic_read(buf->fifo, &picref, sizeof(picref), NULL);
            avfilter_unref_buffer(picref);
        }
        av_fifo_free(buf->fifo);
        buf->fifo = NULL;
    }
}

static int add_buffer_ref(AVFilterContext *ctx, AVFilterBufferRef *ref)
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

static int filter_frame(AVFilterLink *inlink, AVFilterBufferRef *ref)
{
    AVFilterContext *ctx = inlink->dst;
    BufferSinkContext *buf = inlink->dst->priv;
    int ret;

    if ((ret = add_buffer_ref(ctx, ref)) < 0)
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

void av_buffersink_set_frame_size(AVFilterContext *ctx, unsigned frame_size)
{
    AVFilterLink *inlink = ctx->inputs[0];

    inlink->min_samples = inlink->max_samples =
    inlink->partial_buf_size = frame_size;
}

int av_buffersink_get_buffer_ref(AVFilterContext *ctx,
                                  AVFilterBufferRef **bufref, int flags)
{
    BufferSinkContext *buf = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    int ret;
    *bufref = NULL;

    av_assert0(    !strcmp(ctx->filter->name, "buffersink")
                || !strcmp(ctx->filter->name, "abuffersink")
                || !strcmp(ctx->filter->name, "ffbuffersink")
                || !strcmp(ctx->filter->name, "ffabuffersink"));

    /* no picref available, fetch it from the filterchain */
    if (!av_fifo_size(buf->fifo)) {
        if (flags & AV_BUFFERSINK_FLAG_NO_REQUEST)
            return AVERROR(EAGAIN);
        if ((ret = ff_request_frame(inlink)) < 0)
            return ret;
    }

    if (!av_fifo_size(buf->fifo))
        return AVERROR(EINVAL);

    if (flags & AV_BUFFERSINK_FLAG_PEEK)
        *bufref = *((AVFilterBufferRef **)av_fifo_peek2(buf->fifo, 0));
    else
        av_fifo_generic_read(buf->fifo, bufref, sizeof(*bufref), NULL);

    return 0;
}

AVRational av_buffersink_get_frame_rate(AVFilterContext *ctx)
{
    av_assert0(   !strcmp(ctx->filter->name, "buffersink")
               || !strcmp(ctx->filter->name, "ffbuffersink"));

    return ctx->inputs[0]->frame_rate;
}

int av_buffersink_poll_frame(AVFilterContext *ctx)
{
    BufferSinkContext *buf = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];

    av_assert0(   !strcmp(ctx->filter->name, "buffersink")
               || !strcmp(ctx->filter->name, "abuffersink")
               || !strcmp(ctx->filter->name, "ffbuffersink")
               || !strcmp(ctx->filter->name, "ffabuffersink"));

    return av_fifo_size(buf->fifo)/sizeof(AVFilterBufferRef *) + ff_poll_frame(inlink);
}

static av_cold int vsink_init(AVFilterContext *ctx, const char *args, void *opaque)
{
    BufferSinkContext *buf = ctx->priv;
    AVBufferSinkParams *params = opaque;

    if (params && params->pixel_fmts) {
        const int *pixel_fmts = params->pixel_fmts;

        buf->pixel_fmts = ff_copy_int_list(pixel_fmts);
        if (!buf->pixel_fmts)
            return AVERROR(ENOMEM);
    }

    return common_init(ctx);
}

static av_cold void vsink_uninit(AVFilterContext *ctx)
{
    BufferSinkContext *buf = ctx->priv;
    av_freep(&buf->pixel_fmts);
    common_uninit(ctx);
}

static int vsink_query_formats(AVFilterContext *ctx)
{
    BufferSinkContext *buf = ctx->priv;

    if (buf->pixel_fmts)
        ff_set_common_formats(ctx, ff_make_format_list(buf->pixel_fmts));
    else
        ff_default_query_formats(ctx);

    return 0;
}

static const AVFilterPad ffbuffersink_inputs[] = {
    {
        .name      = "default",
        .type      = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .min_perms = AV_PERM_READ | AV_PERM_PRESERVE,
    },
    { NULL },
};

AVFilter avfilter_vsink_ffbuffersink = {
    .name      = "ffbuffersink",
    .description = NULL_IF_CONFIG_SMALL("Buffer video frames, and make them available to the end of the filter graph."),
    .priv_size = sizeof(BufferSinkContext),
    .init_opaque = vsink_init,
    .uninit    = vsink_uninit,

    .query_formats = vsink_query_formats,
    .inputs        = ffbuffersink_inputs,
    .outputs       = NULL,
};

static const AVFilterPad buffersink_inputs[] = {
    {
        .name      = "default",
        .type      = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .min_perms = AV_PERM_READ | AV_PERM_PRESERVE,
    },
    { NULL },
};

AVFilter avfilter_vsink_buffersink = {
    .name      = "buffersink",
    .description = NULL_IF_CONFIG_SMALL("Buffer video frames, and make them available to the end of the filter graph."),
    .priv_size = sizeof(BufferSinkContext),
    .init_opaque = vsink_init,
    .uninit    = vsink_uninit,

    .query_formats = vsink_query_formats,
    .inputs        = buffersink_inputs,
    .outputs       = NULL,
};

static int64_t *concat_channels_lists(const int64_t *layouts, const int *counts)
{
    int nb_layouts = 0, nb_counts = 0, i;
    int64_t *list;

    if (layouts)
        for (; layouts[nb_layouts] != -1; nb_layouts++);
    if (counts)
        for (; counts[nb_counts] != -1; nb_counts++);
    if (nb_counts > INT_MAX - 1 - nb_layouts)
        return NULL;
    if (!(list = av_calloc(nb_layouts + nb_counts + 1, sizeof(*list))))
        return NULL;
    for (i = 0; i < nb_layouts; i++)
        list[i] = layouts[i];
    for (i = 0; i < nb_counts; i++)
        list[nb_layouts + i] = FF_COUNT2LAYOUT(counts[i]);
    list[nb_layouts + nb_counts] = -1;
    return list;
}

static av_cold int asink_init(AVFilterContext *ctx, const char *args, void *opaque)
{
    BufferSinkContext *buf = ctx->priv;
    AVABufferSinkParams *params = opaque;

    if (params && params->sample_fmts) {
        buf->sample_fmts     = ff_copy_int_list  (params->sample_fmts);
        if (!buf->sample_fmts)
            return AVERROR(ENOMEM);
    }
    if (params && (params->channel_layouts || params->channel_counts)) {
        if (params->all_channel_counts) {
            av_log(ctx, AV_LOG_ERROR,
                   "Conflicting all_channel_counts and list in parameters\n");
            return AVERROR(EINVAL);
        }
        buf->channel_layouts = concat_channels_lists(params->channel_layouts,
                                                     params->channel_counts);
        if (!buf->channel_layouts)
            return AVERROR(ENOMEM);
    }
    if (params)
        buf->all_channel_counts = params->all_channel_counts;
    return common_init(ctx);
}

static av_cold void asink_uninit(AVFilterContext *ctx)
{
    BufferSinkContext *buf = ctx->priv;

    av_freep(&buf->sample_fmts);
    av_freep(&buf->channel_layouts);
    common_uninit(ctx);
}

static int asink_query_formats(AVFilterContext *ctx)
{
    BufferSinkContext *buf = ctx->priv;
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;

    if (buf->sample_fmts) {
    if (!(formats = ff_make_format_list(buf->sample_fmts)))
        return AVERROR(ENOMEM);
    ff_set_common_formats(ctx, formats);
    }

    if (buf->channel_layouts || buf->all_channel_counts) {
            layouts = buf->all_channel_counts ? ff_all_channel_counts() :
                      avfilter_make_format64_list(buf->channel_layouts);
        if (!layouts)
            return AVERROR(ENOMEM);
        ff_set_common_channel_layouts(ctx, layouts);
    }

    return 0;
}

static const AVFilterPad ffabuffersink_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_AUDIO,
        .filter_frame   = filter_frame,
        .min_perms      = AV_PERM_READ | AV_PERM_PRESERVE,
    },
    { NULL },
};

AVFilter avfilter_asink_ffabuffersink = {
    .name      = "ffabuffersink",
    .description = NULL_IF_CONFIG_SMALL("Buffer audio frames, and make them available to the end of the filter graph."),
    .init_opaque = asink_init,
    .uninit    = asink_uninit,
    .priv_size = sizeof(BufferSinkContext),
    .query_formats = asink_query_formats,
    .inputs        = ffabuffersink_inputs,
    .outputs       = NULL,
};

static const AVFilterPad abuffersink_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_AUDIO,
        .filter_frame   = filter_frame,
        .min_perms      = AV_PERM_READ | AV_PERM_PRESERVE,
    },
    { NULL },
};

AVFilter avfilter_asink_abuffersink = {
    .name      = "abuffersink",
    .description = NULL_IF_CONFIG_SMALL("Buffer audio frames, and make them available to the end of the filter graph."),
    .init_opaque = asink_init,
    .uninit    = asink_uninit,
    .priv_size = sizeof(BufferSinkContext),
    .query_formats = asink_query_formats,
    .inputs        = abuffersink_inputs,
    .outputs       = NULL,
};

/* Libav compatibility API */

extern AVFilter avfilter_vsink_buffer;
extern AVFilter avfilter_asink_abuffer;

int av_buffersink_read(AVFilterContext *ctx, AVFilterBufferRef **buf)
{
    AVFilterBufferRef *tbuf;
    int ret;

    if (ctx->filter->          inputs[0].start_frame ==
        avfilter_vsink_buffer. inputs[0].start_frame ||
        ctx->filter->          inputs[0].filter_frame ==
        avfilter_asink_abuffer.inputs[0].filter_frame)
        return ff_buffersink_read_compat(ctx, buf);
    av_assert0(ctx->filter->                inputs[0].end_frame ==
               avfilter_vsink_ffbuffersink. inputs[0].end_frame ||
               ctx->filter->                inputs[0].filter_frame ==
               avfilter_asink_ffabuffersink.inputs[0].filter_frame);

    ret = av_buffersink_get_buffer_ref(ctx, &tbuf,
                                       buf ? 0 : AV_BUFFERSINK_FLAG_PEEK);
    if (!buf)
        return ret >= 0;
    if (ret < 0)
        return ret;
    *buf = tbuf;
    return 0;
}

int av_buffersink_read_samples(AVFilterContext *ctx, AVFilterBufferRef **buf,
                               int nb_samples)
{
    BufferSinkContext *sink = ctx->priv;
    int ret = 0, have_samples = 0, need_samples;
    AVFilterBufferRef *tbuf, *in_buf;
    AVFilterLink *link = ctx->inputs[0];
    int nb_channels = av_get_channel_layout_nb_channels(link->channel_layout);

    if (ctx->filter->          inputs[0].filter_frame ==
        avfilter_asink_abuffer.inputs[0].filter_frame)
        return ff_buffersink_read_samples_compat(ctx, buf, nb_samples);
    av_assert0(ctx->filter->                inputs[0].filter_frame ==
               avfilter_asink_ffabuffersink.inputs[0].filter_frame);

    tbuf = ff_get_audio_buffer(link, AV_PERM_WRITE, nb_samples);
    if (!tbuf)
        return AVERROR(ENOMEM);

    while (have_samples < nb_samples) {
        ret = av_buffersink_get_buffer_ref(ctx, &in_buf,
                                           AV_BUFFERSINK_FLAG_PEEK);
        if (ret < 0) {
            if (ret == AVERROR_EOF && have_samples) {
                nb_samples = have_samples;
                ret = 0;
            }
            break;
        }

        need_samples = FFMIN(in_buf->audio->nb_samples,
                             nb_samples - have_samples);
        av_samples_copy(tbuf->extended_data, in_buf->extended_data,
                        have_samples, 0, need_samples,
                        nb_channels, in_buf->format);
        have_samples += need_samples;
        if (need_samples < in_buf->audio->nb_samples) {
            in_buf->audio->nb_samples -= need_samples;
            av_samples_copy(in_buf->extended_data, in_buf->extended_data,
                            0, need_samples, in_buf->audio->nb_samples,
                            nb_channels, in_buf->format);
        } else {
            av_buffersink_get_buffer_ref(ctx, &in_buf, 0);
            avfilter_unref_buffer(in_buf);
        }
    }
    tbuf->audio->nb_samples = have_samples;

    if (ret < 0) {
        av_assert0(!av_fifo_size(sink->fifo));
        if (have_samples)
            add_buffer_ref(ctx, tbuf);
        else
            avfilter_unref_buffer(tbuf);
        return ret;
    }

    *buf = tbuf;
    return 0;
}
