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

#include "avfilter.h"
#include "buffersrc.h"
#include "vsrc_buffer.h"
#include "libavutil/imgutils.h"

typedef struct {
    AVFilterBufferRef *buf;
    int               h, w;
    enum PixelFormat  pix_fmt;
    AVRational        time_base;     ///< time_base to set in the output link
    AVRational        pixel_aspect;
} BufferSourceContext;

#define CHECK_PARAM_CHANGE(s, c, width, height, format)\
    if (c->w != width || c->h != height || c->pix_fmt != format) {\
        av_log(s, AV_LOG_ERROR, "Changing frame properties on the fly is not supported.\n");\
        return AVERROR(EINVAL);\
    }

int av_vsrc_buffer_add_frame(AVFilterContext *buffer_filter, AVFrame *frame,
                             int64_t pts, AVRational pixel_aspect)
{
    BufferSourceContext *c = buffer_filter->priv;

    if (c->buf) {
        av_log(buffer_filter, AV_LOG_ERROR,
               "Buffering several frames is not supported. "
               "Please consume all available frames before adding a new one.\n"
            );
        //return -1;
    }

    CHECK_PARAM_CHANGE(buffer_filter, c, frame->width, frame->height, frame->format);

    c->buf = avfilter_get_video_buffer(buffer_filter->outputs[0], AV_PERM_WRITE,
                                       c->w, c->h);
    av_image_copy(c->buf->data, c->buf->linesize, frame->data, frame->linesize,
                  c->pix_fmt, c->w, c->h);

    avfilter_copy_frame_props(c->buf, frame);
    c->buf->pts                    = pts;
    c->buf->video->pixel_aspect    = pixel_aspect;

    return 0;
}

int av_buffersrc_buffer(AVFilterContext *s, AVFilterBufferRef *buf)
{
    BufferSourceContext *c = s->priv;

    if (c->buf) {
        av_log(s, AV_LOG_ERROR,
               "Buffering several frames is not supported. "
               "Please consume all available frames before adding a new one.\n"
            );
        return AVERROR(EINVAL);
    }

    CHECK_PARAM_CHANGE(s, c, buf->video->w, buf->video->h, buf->format);

    c->buf = buf;

    return 0;
}

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
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

    av_log(ctx, AV_LOG_INFO, "w:%d h:%d pixfmt:%s\n", c->w, c->h, av_pix_fmt_descriptors[c->pix_fmt].name);
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    BufferSourceContext *s = ctx->priv;
    if (s->buf)
        avfilter_unref_buffer(s->buf);
    s->buf = NULL;
}

static int query_formats(AVFilterContext *ctx)
{
    BufferSourceContext *c = ctx->priv;
    enum PixelFormat pix_fmts[] = { c->pix_fmt, PIX_FMT_NONE };

    avfilter_set_common_formats(ctx, avfilter_make_format_list(pix_fmts));
    return 0;
}

static int config_props(AVFilterLink *link)
{
    BufferSourceContext *c = link->src->priv;

    link->w = c->w;
    link->h = c->h;
    link->sample_aspect_ratio = c->pixel_aspect;
    link->time_base = c->time_base;

    return 0;
}

static int request_frame(AVFilterLink *link)
{
    BufferSourceContext *c = link->src->priv;

    if (!c->buf) {
        av_log(link->src, AV_LOG_ERROR,
               "request_frame() called with no available frame!\n");
        //return -1;
    }

    avfilter_start_frame(link, avfilter_ref_buffer(c->buf, ~0));
    avfilter_draw_slice(link, 0, link->h, 1);
    avfilter_end_frame(link);
    avfilter_unref_buffer(c->buf);
    c->buf = NULL;

    return 0;
}

static int poll_frame(AVFilterLink *link)
{
    BufferSourceContext *c = link->src->priv;
    return !!c->buf;
}

AVFilter avfilter_vsrc_buffer = {
    .name      = "buffer",
    .description = NULL_IF_CONFIG_SMALL("Buffer video frames, and make them accessible to the filterchain."),
    .priv_size = sizeof(BufferSourceContext),
    .query_formats = query_formats,

    .init      = init,
    .uninit    = uninit,

    .inputs    = (AVFilterPad[]) {{ .name = NULL }},
    .outputs   = (AVFilterPad[]) {{ .name            = "default",
                                    .type            = AVMEDIA_TYPE_VIDEO,
                                    .request_frame   = request_frame,
                                    .poll_frame      = poll_frame,
                                    .config_props    = config_props, },
                                  { .name = NULL}},
};
