/*
 * Copyright (c) 2008 Vitor Sessak
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
 * memory buffer source filter
 */

#include "avfilter.h"
#include "internal.h"
#include "avcodec.h"
#include "buffersrc.h"
#include "vsrc_buffer.h"
#include "libavutil/fifo.h"
#include "libavutil/imgutils.h"

typedef struct {
    AVFilterContext  *scale;
    AVFifoBuffer     *fifo;
    int               h, w;
    enum PixelFormat  pix_fmt;
    AVRational        time_base;     ///< time_base to set in the output link
    AVRational        sample_aspect_ratio;
    char              sws_param[256];
} BufferSourceContext;

#define CHECK_PARAM_CHANGE(s, c, width, height, format)\
    if (c->w != width || c->h != height || c->pix_fmt != format) {\
        av_log(s, AV_LOG_ERROR, "Changing frame properties on the fly is not supported.\n");\
        return AVERROR(EINVAL);\
    }

int av_vsrc_buffer_add_video_buffer_ref(AVFilterContext *buffer_filter,
                                        AVFilterBufferRef *picref, int flags)
{
    BufferSourceContext *c = buffer_filter->priv;
    AVFilterLink *outlink = buffer_filter->outputs[0];
    AVFilterBufferRef *buf;
    int ret;

    if (!av_fifo_space(c->fifo) &&
        (ret = av_fifo_realloc2(c->fifo, av_fifo_size(c->fifo) +
                                         sizeof(buf))) < 0)
        return ret;

    if (picref->video->w != c->w || picref->video->h != c->h || picref->format != c->pix_fmt) {
        AVFilterContext *scale = buffer_filter->outputs[0]->dst;
        AVFilterLink *link;
        char scale_param[1024];

        av_log(buffer_filter, AV_LOG_INFO,
               "Buffer video input changed from size:%dx%d fmt:%s to size:%dx%d fmt:%s\n",
               c->w, c->h, av_pix_fmt_descriptors[c->pix_fmt].name,
               picref->video->w, picref->video->h, av_pix_fmt_descriptors[picref->format].name);

        if (!scale || strcmp(scale->filter->name, "scale")) {
            AVFilter *f = avfilter_get_by_name("scale");

            av_log(buffer_filter, AV_LOG_INFO, "Inserting scaler filter\n");
            if ((ret = avfilter_open(&scale, f, "Input equalizer")) < 0)
                return ret;

            c->scale = scale;

            snprintf(scale_param, sizeof(scale_param)-1, "%d:%d:%s", c->w, c->h, c->sws_param);
            if ((ret = avfilter_init_filter(scale, scale_param, NULL)) < 0) {
                return ret;
            }

            if ((ret = avfilter_insert_filter(buffer_filter->outputs[0], scale, 0, 0)) < 0) {
                return ret;
            }
            scale->outputs[0]->time_base = scale->inputs[0]->time_base;

            scale->outputs[0]->format= c->pix_fmt;
        } else if (!strcmp(scale->filter->name, "scale")) {
            snprintf(scale_param, sizeof(scale_param)-1, "%d:%d:%s",
                     scale->outputs[0]->w, scale->outputs[0]->h, c->sws_param);
            scale->filter->init(scale, scale_param, NULL);
        }

        c->pix_fmt = scale->inputs[0]->format = picref->format;
        c->w       = scale->inputs[0]->w      = picref->video->w;
        c->h       = scale->inputs[0]->h      = picref->video->h;

        link = scale->outputs[0];
        if ((ret =  link->srcpad->config_props(link)) < 0)
            return ret;
    }

    buf = avfilter_get_video_buffer(outlink, AV_PERM_WRITE,
                                    picref->video->w, picref->video->h);
    av_image_copy(buf->data, buf->linesize,
                  (void*)picref->data, picref->linesize,
                  picref->format, picref->video->w, picref->video->h);
    avfilter_copy_buffer_ref_props(buf, picref);

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

    if (!av_fifo_space(c->fifo) &&
        (ret = av_fifo_realloc2(c->fifo, av_fifo_size(c->fifo) +
                                         sizeof(buf))) < 0)
        return ret;

//     CHECK_PARAM_CHANGE(s, c, buf->video->w, buf->video->h, buf->format);

    if ((ret = av_fifo_generic_write(c->fifo, &buf, sizeof(buf), NULL)) < 0)
        return ret;

    return 0;
}

#if CONFIG_AVCODEC
#include "avcodec.h"

int av_vsrc_buffer_add_frame(AVFilterContext *buffer_src,
                             const AVFrame *frame, int flags)
{
    int ret;
    AVFilterBufferRef *picref =
        avfilter_get_video_buffer_ref_from_frame(frame, AV_PERM_WRITE);
    if (!picref)
        return AVERROR(ENOMEM);
    ret = av_vsrc_buffer_add_video_buffer_ref(buffer_src, picref, flags);
    picref->buf->data[0] = NULL;
    avfilter_unref_buffer(picref);

    return ret;
}
#endif

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    BufferSourceContext *c = ctx->priv;
    char pix_fmt_str[128];
    int ret, n = 0;
    *c->sws_param = 0;

    if (!args ||
        (n = sscanf(args, "%d:%d:%127[^:]:%d:%d:%d:%d:%255c", &c->w, &c->h, pix_fmt_str,
                    &c->time_base.num, &c->time_base.den,
                    &c->sample_aspect_ratio.num, &c->sample_aspect_ratio.den, c->sws_param)) < 7) {
        av_log(ctx, AV_LOG_ERROR, "Expected at least 7 arguments, but only %d found in '%s'\n", n, args);
        return AVERROR(EINVAL);
    }

    if ((ret = ff_parse_pixel_format(&c->pix_fmt, pix_fmt_str, ctx)) < 0)
        return ret;

    if (!(c->fifo = av_fifo_alloc(sizeof(AVFilterBufferRef*))))
        return AVERROR(ENOMEM);

    av_log(ctx, AV_LOG_INFO, "w:%d h:%d pixfmt:%s tb:%d/%d sar:%d/%d sws_param:%s\n",
           c->w, c->h, av_pix_fmt_descriptors[c->pix_fmt].name,
           c->time_base.num, c->time_base.den,
           c->sample_aspect_ratio.num, c->sample_aspect_ratio.den, c->sws_param);
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    BufferSourceContext *s = ctx->priv;
    while (av_fifo_size(s->fifo)) {
        AVFilterBufferRef *buf;
        av_fifo_generic_read(s->fifo, &buf, sizeof(buf), NULL);
        avfilter_unref_buffer(buf);
    }
    av_fifo_free(s->fifo);
    s->fifo = NULL;
    avfilter_free(s->scale);
    s->scale = NULL;
}

static int query_formats(AVFilterContext *ctx)
{
    BufferSourceContext *c = ctx->priv;
    enum PixelFormat pix_fmts[] = { c->pix_fmt, PIX_FMT_NONE };

    avfilter_set_common_pixel_formats(ctx, avfilter_make_format_list(pix_fmts));
    return 0;
}

static int config_props(AVFilterLink *link)
{
    BufferSourceContext *c = link->src->priv;

    link->w = c->w;
    link->h = c->h;
    link->sample_aspect_ratio = c->sample_aspect_ratio;
    link->time_base = c->time_base;

    return 0;
}

static int request_frame(AVFilterLink *link)
{
    BufferSourceContext *c = link->src->priv;
    AVFilterBufferRef *buf;

    if (!av_fifo_size(c->fifo)) {
        av_log(link->src, AV_LOG_WARNING,
               "request_frame() called with no available frame!\n");
        return AVERROR(EINVAL);
    }
    av_fifo_generic_read(c->fifo, &buf, sizeof(buf), NULL);

    avfilter_start_frame(link, avfilter_ref_buffer(buf, ~0));
    avfilter_draw_slice(link, 0, link->h, 1);
    avfilter_end_frame(link);
    avfilter_unref_buffer(buf);

    return 0;
}

static int poll_frame(AVFilterLink *link)
{
    BufferSourceContext *c = link->src->priv;
    return !!av_fifo_size(c->fifo);
}

AVFilter avfilter_vsrc_buffer = {
    .name      = "buffer",
    .description = NULL_IF_CONFIG_SMALL("Buffer video frames, and make them accessible to the filterchain."),
    .priv_size = sizeof(BufferSourceContext),
    .query_formats = query_formats,

    .init      = init,
    .uninit    = uninit,

    .inputs    = (const AVFilterPad[]) {{ .name = NULL }},
    .outputs   = (const AVFilterPad[]) {{ .name      = "default",
                                    .type            = AVMEDIA_TYPE_VIDEO,
                                    .request_frame   = request_frame,
                                    .poll_frame      = poll_frame,
                                    .config_props    = config_props, },
                                  { .name = NULL}},
};
