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

#include "audio.h"
#include "avfilter.h"
#include "buffersrc.h"
#include "formats.h"
#include "video.h"
#include "vsrc_buffer.h"

#include "libavutil/audioconvert.h"
#include "libavutil/fifo.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"

typedef struct {
    const AVClass    *class;
    AVFifoBuffer     *fifo;
    AVRational        time_base;     ///< time_base to set in the output link

    /* video only */
    int               h, w;
    enum PixelFormat  pix_fmt;
    AVRational        pixel_aspect;

    /* audio only */
    int sample_rate;
    enum AVSampleFormat sample_fmt;
    char               *sample_fmt_str;
    uint64_t channel_layout;
    char    *channel_layout_str;

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

#if FF_API_VSRC_BUFFER_ADD_FRAME
int av_vsrc_buffer_add_frame(AVFilterContext *buffer_filter, AVFrame *frame,
                             int64_t pts, AVRational pixel_aspect)
{
    int64_t orig_pts = frame->pts;
    AVRational orig_sar = frame->sample_aspect_ratio;
    int ret;

    frame->pts = pts;
    frame->sample_aspect_ratio = pixel_aspect;
    if ((ret = av_buffersrc_write_frame(buffer_filter, frame)) < 0)
        return ret;
    frame->pts = orig_pts;
    frame->sample_aspect_ratio = orig_sar;

    return 0;
}
#endif

int av_buffersrc_write_frame(AVFilterContext *buffer_filter, AVFrame *frame)
{
    BufferSourceContext *c = buffer_filter->priv;
    AVFilterBufferRef *buf;
    int ret;

    if (!frame) {
        c->eof = 1;
        return 0;
    } else if (c->eof)
        return AVERROR(EINVAL);

    if (!av_fifo_space(c->fifo) &&
        (ret = av_fifo_realloc2(c->fifo, av_fifo_size(c->fifo) +
                                         sizeof(buf))) < 0)
        return ret;

    switch (buffer_filter->outputs[0]->type) {
    case AVMEDIA_TYPE_VIDEO:
        CHECK_VIDEO_PARAM_CHANGE(buffer_filter, c, frame->width, frame->height,
                                 frame->format);
        buf = avfilter_get_video_buffer(buffer_filter->outputs[0], AV_PERM_WRITE,
                                        c->w, c->h);
        av_image_copy(buf->data, buf->linesize, frame->data, frame->linesize,
                      c->pix_fmt, c->w, c->h);
        break;
    case AVMEDIA_TYPE_AUDIO:
        CHECK_AUDIO_PARAM_CHANGE(buffer_filter, c, frame->sample_rate, frame->channel_layout,
                                 frame->format);
        buf = ff_get_audio_buffer(buffer_filter->outputs[0], AV_PERM_WRITE,
                                  frame->nb_samples);
        av_samples_copy(buf->extended_data, frame->extended_data,
                        0, 0, frame->nb_samples,
                        av_get_channel_layout_nb_channels(frame->channel_layout),
                        frame->format);
        break;
    default:
        return AVERROR(EINVAL);
    }

    avfilter_copy_frame_props(buf, frame);

    if ((ret = av_fifo_generic_write(c->fifo, &buf, sizeof(buf), NULL)) < 0) {
        avfilter_unref_buffer(buf);
        return ret;
    }

    return 0;
}

int av_buffersrc_buffer(AVFilterContext *s, AVFilterBufferRef *buf)
{
    BufferSourceContext *c = s->priv;
    int ret;

    if (!buf) {
        c->eof = 1;
        return 0;
    } else if (c->eof)
        return AVERROR(EINVAL);

    if (!av_fifo_space(c->fifo) &&
        (ret = av_fifo_realloc2(c->fifo, av_fifo_size(c->fifo) +
                                         sizeof(buf))) < 0)
        return ret;

    switch (s->outputs[0]->type) {
    case AVMEDIA_TYPE_VIDEO:
        CHECK_VIDEO_PARAM_CHANGE(s, c, buf->video->w, buf->video->h, buf->format);
        break;
    case AVMEDIA_TYPE_AUDIO:
        CHECK_AUDIO_PARAM_CHANGE(s, c, buf->audio->sample_rate, buf->audio->channel_layout,
                                 buf->format);
        break;
    default:
        return AVERROR(EINVAL);
    }

    if ((ret = av_fifo_generic_write(c->fifo, &buf, sizeof(buf), NULL)) < 0)
        return ret;

    return 0;
}

static av_cold int init_video(AVFilterContext *ctx, const char *args, void *opaque)
{
    BufferSourceContext *c = ctx->priv;
    char pix_fmt_str[128];
    int n = 0;

    if (!args ||
        (n = sscanf(args, "%d:%d:%127[^:]:%d:%d:%d:%d", &c->w, &c->h, pix_fmt_str,
                    &c->time_base.num, &c->time_base.den,
                    &c->pixel_aspect.num, &c->pixel_aspect.den)) != 7) {
        av_log(ctx, AV_LOG_ERROR, "Expected 7 arguments, but %d found in '%s'\n", n, args);
        return AVERROR(EINVAL);
    }
    if ((c->pix_fmt = av_get_pix_fmt(pix_fmt_str)) == PIX_FMT_NONE) {
        char *tail;
        c->pix_fmt = strtol(pix_fmt_str, &tail, 10);
        if (*tail || c->pix_fmt < 0 || c->pix_fmt >= PIX_FMT_NB) {
            av_log(ctx, AV_LOG_ERROR, "Invalid pixel format string '%s'\n", pix_fmt_str);
            return AVERROR(EINVAL);
        }
    }

    if (!(c->fifo = av_fifo_alloc(sizeof(AVFilterBufferRef*))))
        return AVERROR(ENOMEM);

    av_log(ctx, AV_LOG_INFO, "w:%d h:%d pixfmt:%s\n", c->w, c->h, av_pix_fmt_descriptors[c->pix_fmt].name);
    return 0;
}

#define OFFSET(x) offsetof(BufferSourceContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
static const AVOption audio_options[] = {
    { "time_base",      NULL, OFFSET(time_base),           AV_OPT_TYPE_RATIONAL, { 0 }, 0, INT_MAX, A },
    { "sample_rate",    NULL, OFFSET(sample_rate),         AV_OPT_TYPE_INT,      { 0 }, 0, INT_MAX, A },
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

static av_cold int init_audio(AVFilterContext *ctx, const char *args, void *opaque)
{
    BufferSourceContext *s = ctx->priv;
    int ret = 0;

    s->class = &abuffer_class;
    av_opt_set_defaults(s);

    if ((ret = av_set_options_string(s, args, "=", ":")) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error parsing options string: %s.\n", args);
        goto fail;
    }

    s->sample_fmt = av_get_sample_fmt(s->sample_fmt_str);
    if (s->sample_fmt == AV_SAMPLE_FMT_NONE) {
        av_log(ctx, AV_LOG_ERROR, "Invalid sample format %s.\n",
               s->sample_fmt_str);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    s->channel_layout = av_get_channel_layout(s->channel_layout_str);
    if (!s->channel_layout) {
        av_log(ctx, AV_LOG_ERROR, "Invalid channel layout %s.\n",
               s->channel_layout_str);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    if (!(s->fifo = av_fifo_alloc(sizeof(AVFilterBufferRef*)))) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if (!s->time_base.num)
        s->time_base = (AVRational){1, s->sample_rate};

    av_log(ctx, AV_LOG_VERBOSE, "tb:%d/%d samplefmt:%s samplerate: %d "
           "ch layout:%s\n", s->time_base.num, s->time_base.den, s->sample_fmt_str,
           s->sample_rate, s->channel_layout_str);

fail:
    av_opt_free(s);
    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    BufferSourceContext *s = ctx->priv;
    while (s->fifo && av_fifo_size(s->fifo)) {
        AVFilterBufferRef *buf;
        av_fifo_generic_read(s->fifo, &buf, sizeof(buf), NULL);
        avfilter_unref_buffer(buf);
    }
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
        break;
    case AVMEDIA_TYPE_AUDIO:
        link->channel_layout = c->channel_layout;
        link->sample_rate    = c->sample_rate;
        break;
    default:
        return AVERROR(EINVAL);
    }

    link->time_base = c->time_base;
    return 0;
}

static int request_frame(AVFilterLink *link)
{
    BufferSourceContext *c = link->src->priv;
    AVFilterBufferRef *buf;

    if (!av_fifo_size(c->fifo)) {
        if (c->eof)
            return AVERROR_EOF;
        return AVERROR(EAGAIN);
    }
    av_fifo_generic_read(c->fifo, &buf, sizeof(buf), NULL);

    switch (link->type) {
    case AVMEDIA_TYPE_VIDEO:
        ff_start_frame(link, avfilter_ref_buffer(buf, ~0));
        ff_draw_slice(link, 0, link->h, 1);
        ff_end_frame(link);
        break;
    case AVMEDIA_TYPE_AUDIO:
        ff_filter_samples(link, avfilter_ref_buffer(buf, ~0));
        break;
    default:
        return AVERROR(EINVAL);
    }

    avfilter_unref_buffer(buf);

    return 0;
}

static int poll_frame(AVFilterLink *link)
{
    BufferSourceContext *c = link->src->priv;
    int size = av_fifo_size(c->fifo);
    if (!size && c->eof)
        return AVERROR_EOF;
    return size/sizeof(AVFilterBufferRef*);
}

AVFilter avfilter_vsrc_buffer = {
    .name      = "buffer",
    .description = NULL_IF_CONFIG_SMALL("Buffer video frames, and make them accessible to the filterchain."),
    .priv_size = sizeof(BufferSourceContext),
    .query_formats = query_formats,

    .init      = init_video,
    .uninit    = uninit,

    .inputs    = (AVFilterPad[]) {{ .name = NULL }},
    .outputs   = (AVFilterPad[]) {{ .name            = "default",
                                    .type            = AVMEDIA_TYPE_VIDEO,
                                    .request_frame   = request_frame,
                                    .poll_frame      = poll_frame,
                                    .config_props    = config_props, },
                                  { .name = NULL}},
};

AVFilter avfilter_asrc_abuffer = {
    .name          = "abuffer",
    .description   = NULL_IF_CONFIG_SMALL("Buffer audio frames, and make them accessible to the filterchain."),
    .priv_size     = sizeof(BufferSourceContext),
    .query_formats = query_formats,

    .init      = init_audio,
    .uninit    = uninit,

    .inputs    = (AVFilterPad[]) {{ .name = NULL }},
    .outputs   = (AVFilterPad[]) {{ .name            = "default",
                                    .type            = AVMEDIA_TYPE_AUDIO,
                                    .request_frame   = request_frame,
                                    .poll_frame      = poll_frame,
                                    .config_props    = config_props, },
                                  { .name = NULL}},
};
