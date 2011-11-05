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
 * buffer video sink
 */

#include "libavutil/fifo.h"
#include "avfilter.h"
#include "buffersink.h"

AVBufferSinkParams *av_buffersink_params_alloc(void)
{
    static const int pixel_fmts[] = { -1 };
    AVBufferSinkParams *params = av_malloc(sizeof(AVBufferSinkParams));
    if (!params)
        return NULL;

    params->pixel_fmts = pixel_fmts;
    return params;
}

AVABufferSinkParams *av_abuffersink_params_alloc(void)
{
    static const int sample_fmts[] = { -1 };
    static const int packing_fmts[] = { -1 };
    static const int64_t channel_layouts[] = { -1 };
    AVABufferSinkParams *params = av_malloc(sizeof(AVABufferSinkParams));

    if (!params)
        return NULL;

    params->sample_fmts = sample_fmts;
    params->channel_layouts = channel_layouts;
    params->packing_fmts = packing_fmts;
    return params;
}

typedef struct {
    AVFifoBuffer *fifo;                      ///< FIFO buffer of video frame references

    /* only used for video */
    const enum PixelFormat *pixel_fmts;     ///< list of accepted pixel formats, must be terminated with -1

    /* only used for audio */
    const enum AVSampleFormat *sample_fmts; ///< list of accepted sample formats, terminated by AV_SAMPLE_FMT_NONE
    const int64_t *channel_layouts;         ///< list of accepted channel layouts, terminated by -1
    const int *packing_fmts;                ///< list of accepted packing formats, terminated by -1
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

static void end_frame(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    BufferSinkContext *buf = inlink->dst->priv;

    if (av_fifo_space(buf->fifo) < sizeof(AVFilterBufferRef *)) {
        /* realloc fifo size */
        if (av_fifo_realloc2(buf->fifo, av_fifo_size(buf->fifo) * 2) < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Cannot buffer more frames. Consume some available frames "
                   "before adding new ones.\n");
            return;
        }
    }

    /* cache frame */
    av_fifo_generic_write(buf->fifo,
                          &inlink->cur_buf, sizeof(AVFilterBufferRef *), NULL);
}

int av_buffersink_get_buffer_ref(AVFilterContext *ctx,
                                  AVFilterBufferRef **bufref, int flags)
{
    BufferSinkContext *buf = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    int ret;
    *bufref = NULL;

    /* no picref available, fetch it from the filterchain */
    if (!av_fifo_size(buf->fifo)) {
        if ((ret = avfilter_request_frame(inlink)) < 0)
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

#if FF_API_OLD_VSINK_API
int av_vsink_buffer_get_video_buffer_ref(AVFilterContext *ctx,
                                         AVFilterBufferRef **picref, int flags)
{
    return av_buffersink_get_buffer_ref(ctx, picref, flags);
}
#endif

#if CONFIG_BUFFERSINK_FILTER

static av_cold int vsink_init(AVFilterContext *ctx, const char *args, void *opaque)
{
    BufferSinkContext *buf = ctx->priv;
    av_unused AVBufferSinkParams *params;

    if (!opaque) {
        av_log(ctx, AV_LOG_ERROR,
               "No opaque field provided\n");
        return AVERROR(EINVAL);
    } else {
#if FF_API_OLD_VSINK_API
        buf->pixel_fmts = (const enum PixelFormat *)opaque;
#else
        params = (AVBufferSinkParams *)opaque;
        buf->pixel_fmts = params->pixel_fmts;
#endif
    }

    return common_init(ctx);
}

static int vsink_query_formats(AVFilterContext *ctx)
{
    BufferSinkContext *buf = ctx->priv;

    avfilter_set_common_pixel_formats(ctx, avfilter_make_format_list(buf->pixel_fmts));
    return 0;
}

AVFilter avfilter_vsink_buffersink = {
    .name      = "buffersink",
    .description = NULL_IF_CONFIG_SMALL("Buffer video frames, and make them available to the end of the filter graph."),
    .priv_size = sizeof(BufferSinkContext),
    .init      = vsink_init,
    .uninit    = common_uninit,

    .query_formats = vsink_query_formats,

    .inputs    = (const AVFilterPad[]) {{ .name    = "default",
                                    .type          = AVMEDIA_TYPE_VIDEO,
                                    .end_frame     = end_frame,
                                    .min_perms     = AV_PERM_READ, },
                                  { .name = NULL }},
    .outputs   = (const AVFilterPad[]) {{ .name = NULL }},
};

#endif /* CONFIG_BUFFERSINK_FILTER */

#if CONFIG_ABUFFERSINK_FILTER

static void filter_samples(AVFilterLink *link, AVFilterBufferRef *samplesref)
{
    end_frame(link);
}

static av_cold int asink_init(AVFilterContext *ctx, const char *args, void *opaque)
{
    BufferSinkContext *buf = ctx->priv;
    AVABufferSinkParams *params;

    if (!opaque) {
        av_log(ctx, AV_LOG_ERROR,
               "No opaque field provided, an AVABufferSinkParams struct is required\n");
        return AVERROR(EINVAL);
    } else
        params = (AVABufferSinkParams *)opaque;

    buf->sample_fmts     = params->sample_fmts;
    buf->channel_layouts = params->channel_layouts;
    buf->packing_fmts    = params->packing_fmts;

    return common_init(ctx);
}

static int asink_query_formats(AVFilterContext *ctx)
{
    BufferSinkContext *buf = ctx->priv;
    AVFilterFormats *formats = NULL;

    if (!(formats = avfilter_make_format_list(buf->sample_fmts)))
        return AVERROR(ENOMEM);
    avfilter_set_common_sample_formats(ctx, formats);

    if (!(formats = avfilter_make_format64_list(buf->channel_layouts)))
        return AVERROR(ENOMEM);
    avfilter_set_common_channel_layouts(ctx, formats);

    if (!(formats = avfilter_make_format_list(buf->packing_fmts)))
        return AVERROR(ENOMEM);
    avfilter_set_common_packing_formats(ctx, formats);

    return 0;
}

AVFilter avfilter_asink_abuffersink = {
    .name      = "abuffersink",
    .description = NULL_IF_CONFIG_SMALL("Buffer audio frames, and make them available to the end of the filter graph."),
    .init      = asink_init,
    .uninit    = common_uninit,
    .priv_size = sizeof(BufferSinkContext),
    .query_formats = asink_query_formats,

    .inputs    = (const AVFilterPad[]) {{ .name     = "default",
                                    .type           = AVMEDIA_TYPE_AUDIO,
                                    .filter_samples = filter_samples,
                                    .min_perms      = AV_PERM_READ, },
                                  { .name = NULL }},
    .outputs   = (const AVFilterPad[]) {{ .name = NULL }},
};

#endif /* CONFIG_ABUFFERSINK_FILTER */
