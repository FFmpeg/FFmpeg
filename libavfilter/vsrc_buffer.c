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
#include "vsrc_buffer.h"
#include "libavutil/imgutils.h"

typedef struct {
    AVFilterBufferRef *picref;
    int               h, w;
    enum PixelFormat  pix_fmt;
    AVRational        time_base;     ///< time_base to set in the output link
    AVRational        sample_aspect_ratio;
    char              sws_param[256];
} BufferSourceContext;

int av_vsrc_buffer_add_video_buffer_ref(AVFilterContext *buffer_filter,
                                        AVFilterBufferRef *picref, int flags)
{
    BufferSourceContext *c = buffer_filter->priv;
    AVFilterLink *outlink = buffer_filter->outputs[0];
    int ret;

    if (c->picref) {
        if (flags & AV_VSRC_BUF_FLAG_OVERWRITE) {
            avfilter_unref_buffer(c->picref);
            c->picref = NULL;
        } else {
            av_log(buffer_filter, AV_LOG_ERROR,
                   "Buffering several frames is not supported. "
                   "Please consume all available frames before adding a new one.\n");
            return AVERROR(EINVAL);
        }
    }

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

            snprintf(scale_param, sizeof(scale_param)-1, "%d:%d:%s", c->w, c->h, c->sws_param);
            if ((ret = avfilter_init_filter(scale, scale_param, NULL)) < 0) {
                avfilter_free(scale);
                return ret;
            }

            if ((ret = avfilter_insert_filter(buffer_filter->outputs[0], scale, 0, 0)) < 0) {
                avfilter_free(scale);
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

    c->picref = avfilter_get_video_buffer(outlink, AV_PERM_WRITE,
                                          picref->video->w, picref->video->h);
    av_image_copy(c->picref->data, c->picref->linesize,
                  (void*)picref->data, picref->linesize,
                  picref->format, picref->video->w, picref->video->h);
    avfilter_copy_buffer_ref_props(c->picref, picref);

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

    av_log(ctx, AV_LOG_INFO, "w:%d h:%d pixfmt:%s tb:%d/%d sar:%d/%d sws_param:%s\n",
           c->w, c->h, av_pix_fmt_descriptors[c->pix_fmt].name,
           c->time_base.num, c->time_base.den,
           c->sample_aspect_ratio.num, c->sample_aspect_ratio.den, c->sws_param);
    return 0;
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

    if (!c->picref) {
        av_log(link->src, AV_LOG_WARNING,
               "request_frame() called with no available frame!\n");
        return AVERROR(EINVAL);
    }

    avfilter_start_frame(link, avfilter_ref_buffer(c->picref, ~0));
    avfilter_draw_slice(link, 0, link->h, 1);
    avfilter_end_frame(link);
    avfilter_unref_buffer(c->picref);
    c->picref = NULL;

    return 0;
}

static int poll_frame(AVFilterLink *link)
{
    BufferSourceContext *c = link->src->priv;
    return !!(c->picref);
}

AVFilter avfilter_vsrc_buffer = {
    .name      = "buffer",
    .description = NULL_IF_CONFIG_SMALL("Buffer video frames, and make them accessible to the filterchain."),
    .priv_size = sizeof(BufferSourceContext),
    .query_formats = query_formats,

    .init      = init,

    .inputs    = (const AVFilterPad[]) {{ .name = NULL }},
    .outputs   = (const AVFilterPad[]) {{ .name      = "default",
                                    .type            = AVMEDIA_TYPE_VIDEO,
                                    .request_frame   = request_frame,
                                    .poll_frame      = poll_frame,
                                    .config_props    = config_props, },
                                  { .name = NULL}},
};
