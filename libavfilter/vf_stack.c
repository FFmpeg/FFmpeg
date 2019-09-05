/*
 * Copyright (c) 2015 Paul B. Mahol
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

#include "libavutil/avstring.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "framesync.h"
#include "video.h"

typedef struct StackItem {
    int x[4], y[4];
    int linesize[4];
    int height[4];
} StackItem;

typedef struct StackContext {
    const AVClass *class;
    const AVPixFmtDescriptor *desc;
    int nb_inputs;
    char *layout;
    int shortest;
    int is_vertical;
    int is_horizontal;
    int nb_planes;

    StackItem *items;
    AVFrame **frames;
    FFFrameSync fs;
} StackContext;

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *pix_fmts = NULL;
    int fmt, ret;

    for (fmt = 0; av_pix_fmt_desc_get(fmt); fmt++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
        if (!(desc->flags & AV_PIX_FMT_FLAG_PAL ||
              desc->flags & AV_PIX_FMT_FLAG_HWACCEL ||
              desc->flags & AV_PIX_FMT_FLAG_BITSTREAM) &&
            (ret = ff_add_format(&pix_fmts, fmt)) < 0)
            return ret;
    }

    return ff_set_common_formats(ctx, pix_fmts);
}

static av_cold int init(AVFilterContext *ctx)
{
    StackContext *s = ctx->priv;
    int i, ret;

    if (!strcmp(ctx->filter->name, "vstack"))
        s->is_vertical = 1;

    if (!strcmp(ctx->filter->name, "hstack"))
        s->is_horizontal = 1;

    s->frames = av_calloc(s->nb_inputs, sizeof(*s->frames));
    if (!s->frames)
        return AVERROR(ENOMEM);

    if (!strcmp(ctx->filter->name, "xstack")) {
        if (!s->layout) {
            if (s->nb_inputs == 2) {
                s->layout = av_strdup("0_0|w0_0");
                if (!s->layout)
                    return AVERROR(ENOMEM);
            } else {
                av_log(ctx, AV_LOG_ERROR, "No layout specified.\n");
                return AVERROR(EINVAL);
            }
        }

        s->items = av_calloc(s->nb_inputs, sizeof(*s->items));
        if (!s->items)
            return AVERROR(ENOMEM);
    }

    for (i = 0; i < s->nb_inputs; i++) {
        AVFilterPad pad = { 0 };

        pad.type = AVMEDIA_TYPE_VIDEO;
        pad.name = av_asprintf("input%d", i);
        if (!pad.name)
            return AVERROR(ENOMEM);

        if ((ret = ff_insert_inpad(ctx, i, &pad)) < 0) {
            av_freep(&pad.name);
            return ret;
        }
    }

    return 0;
}

static int process_frame(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    AVFilterLink *outlink = ctx->outputs[0];
    StackContext *s = fs->opaque;
    AVFrame **in = s->frames;
    AVFrame *out;
    int i, p, ret, offset[4] = { 0 };

    for (i = 0; i < s->nb_inputs; i++) {
        if ((ret = ff_framesync_get_frame(&s->fs, i, &in[i], 0)) < 0)
            return ret;
    }

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out)
        return AVERROR(ENOMEM);
    out->pts = av_rescale_q(s->fs.pts, s->fs.time_base, outlink->time_base);
    out->sample_aspect_ratio = outlink->sample_aspect_ratio;

    for (i = 0; i < s->nb_inputs; i++) {
        AVFilterLink *inlink = ctx->inputs[i];
        int linesize[4];
        int height[4];

        if (s->is_horizontal || s->is_vertical) {
            if ((ret = av_image_fill_linesizes(linesize, inlink->format, inlink->w)) < 0) {
                av_frame_free(&out);
                return ret;
            }

            height[1] = height[2] = AV_CEIL_RSHIFT(inlink->h, s->desc->log2_chroma_h);
            height[0] = height[3] = inlink->h;
        }

        for (p = 0; p < s->nb_planes; p++) {
            if (s->is_vertical) {
                av_image_copy_plane(out->data[p] + offset[p] * out->linesize[p],
                                    out->linesize[p],
                                    in[i]->data[p],
                                    in[i]->linesize[p],
                                    linesize[p], height[p]);
                offset[p] += height[p];
            } else if (s->is_horizontal) {
                av_image_copy_plane(out->data[p] + offset[p],
                                    out->linesize[p],
                                    in[i]->data[p],
                                    in[i]->linesize[p],
                                    linesize[p], height[p]);
                offset[p] += linesize[p];
            } else {
                StackItem *item = &s->items[i];

                av_image_copy_plane(out->data[p] + out->linesize[p] * item->y[p] + item->x[p],
                                    out->linesize[p],
                                    in[i]->data[p],
                                    in[i]->linesize[p],
                                    item->linesize[p], item->height[p]);
            }
        }
    }

    return ff_filter_frame(outlink, out);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    StackContext *s = ctx->priv;
    AVRational frame_rate = ctx->inputs[0]->frame_rate;
    AVRational sar = ctx->inputs[0]->sample_aspect_ratio;
    int height = ctx->inputs[0]->h;
    int width = ctx->inputs[0]->w;
    FFFrameSyncIn *in;
    int i, ret;

    s->desc = av_pix_fmt_desc_get(outlink->format);
    if (!s->desc)
        return AVERROR_BUG;

    if (s->is_vertical) {
        for (i = 1; i < s->nb_inputs; i++) {
            if (ctx->inputs[i]->w != width) {
                av_log(ctx, AV_LOG_ERROR, "Input %d width %d does not match input %d width %d.\n", i, ctx->inputs[i]->w, 0, width);
                return AVERROR(EINVAL);
            }
            height += ctx->inputs[i]->h;
        }
    } else if (s->is_horizontal) {
        for (i = 1; i < s->nb_inputs; i++) {
            if (ctx->inputs[i]->h != height) {
                av_log(ctx, AV_LOG_ERROR, "Input %d height %d does not match input %d height %d.\n", i, ctx->inputs[i]->h, 0, height);
                return AVERROR(EINVAL);
            }
            width += ctx->inputs[i]->w;
        }
    } else {
        char *arg, *p = s->layout, *saveptr = NULL;
        char *arg2, *p2, *saveptr2 = NULL;
        char *arg3, *p3, *saveptr3 = NULL;
        int inw, inh, size;

        for (i = 0; i < s->nb_inputs; i++) {
            AVFilterLink *inlink = ctx->inputs[i];
            StackItem *item = &s->items[i];

            if (!(arg = av_strtok(p, "|", &saveptr)))
                return AVERROR(EINVAL);

            p = NULL;

            if ((ret = av_image_fill_linesizes(item->linesize, inlink->format, inlink->w)) < 0) {
                return ret;
            }

            item->height[1] = item->height[2] = AV_CEIL_RSHIFT(inlink->h, s->desc->log2_chroma_h);
            item->height[0] = item->height[3] = inlink->h;

            p2 = arg;
            inw = inh = 0;

            for (int j = 0; j < 2; j++) {
                if (!(arg2 = av_strtok(p2, "_", &saveptr2)))
                    return AVERROR(EINVAL);

                p2 = NULL;
                p3 = arg2;
                while ((arg3 = av_strtok(p3, "+", &saveptr3))) {
                    p3 = NULL;
                    if (sscanf(arg3, "w%d", &size) == 1) {
                        if (size == i || size < 0 || size >= s->nb_inputs)
                            return AVERROR(EINVAL);

                        if (!j)
                            inw += ctx->inputs[size]->w;
                        else
                            inh += ctx->inputs[size]->w;
                    } else if (sscanf(arg3, "h%d", &size) == 1) {
                        if (size == i || size < 0 || size >= s->nb_inputs)
                            return AVERROR(EINVAL);

                        if (!j)
                            inw += ctx->inputs[size]->h;
                        else
                            inh += ctx->inputs[size]->h;
                    } else if (sscanf(arg3, "%d", &size) == 1) {
                        if (size < 0)
                            return AVERROR(EINVAL);

                        if (!j)
                            inw += size;
                        else
                            inh += size;
                    } else {
                        return AVERROR(EINVAL);
                    }
                }
            }

            if ((ret = av_image_fill_linesizes(item->x, inlink->format, inw)) < 0) {
                return ret;
            }

            item->y[1] = item->y[2] = AV_CEIL_RSHIFT(inh, s->desc->log2_chroma_h);
            item->y[0] = item->y[3] = inh;

            width  = FFMAX(width,  inlink->w + inw);
            height = FFMAX(height, inlink->h + inh);
        }
    }

    s->nb_planes = av_pix_fmt_count_planes(outlink->format);

    outlink->w          = width;
    outlink->h          = height;
    outlink->frame_rate = frame_rate;
    outlink->sample_aspect_ratio = sar;

    if ((ret = ff_framesync_init(&s->fs, ctx, s->nb_inputs)) < 0)
        return ret;

    in = s->fs.in;
    s->fs.opaque = s;
    s->fs.on_event = process_frame;

    for (i = 0; i < s->nb_inputs; i++) {
        AVFilterLink *inlink = ctx->inputs[i];

        in[i].time_base = inlink->time_base;
        in[i].sync   = 1;
        in[i].before = EXT_STOP;
        in[i].after  = s->shortest ? EXT_STOP : EXT_INFINITY;
    }

    ret = ff_framesync_configure(&s->fs);
    outlink->time_base = s->fs.time_base;

    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    StackContext *s = ctx->priv;
    int i;

    ff_framesync_uninit(&s->fs);
    av_freep(&s->frames);
    av_freep(&s->items);

    for (i = 0; i < ctx->nb_inputs; i++)
        av_freep(&ctx->input_pads[i].name);
}

static int activate(AVFilterContext *ctx)
{
    StackContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

#define OFFSET(x) offsetof(StackContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM
static const AVOption stack_options[] = {
    { "inputs", "set number of inputs", OFFSET(nb_inputs), AV_OPT_TYPE_INT, {.i64=2}, 2, INT_MAX, .flags = FLAGS },
    { "shortest", "force termination when the shortest input terminates", OFFSET(shortest), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, .flags = FLAGS },
    { NULL },
};

static const AVFilterPad outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
    { NULL }
};

#if CONFIG_HSTACK_FILTER

#define hstack_options stack_options
AVFILTER_DEFINE_CLASS(hstack);

AVFilter ff_vf_hstack = {
    .name          = "hstack",
    .description   = NULL_IF_CONFIG_SMALL("Stack video inputs horizontally."),
    .priv_size     = sizeof(StackContext),
    .priv_class    = &hstack_class,
    .query_formats = query_formats,
    .outputs       = outputs,
    .init          = init,
    .uninit        = uninit,
    .activate      = activate,
    .flags         = AVFILTER_FLAG_DYNAMIC_INPUTS,
};

#endif /* CONFIG_HSTACK_FILTER */

#if CONFIG_VSTACK_FILTER

#define vstack_options stack_options
AVFILTER_DEFINE_CLASS(vstack);

AVFilter ff_vf_vstack = {
    .name          = "vstack",
    .description   = NULL_IF_CONFIG_SMALL("Stack video inputs vertically."),
    .priv_size     = sizeof(StackContext),
    .priv_class    = &vstack_class,
    .query_formats = query_formats,
    .outputs       = outputs,
    .init          = init,
    .uninit        = uninit,
    .activate      = activate,
    .flags         = AVFILTER_FLAG_DYNAMIC_INPUTS,
};

#endif /* CONFIG_VSTACK_FILTER */

#if CONFIG_XSTACK_FILTER

static const AVOption xstack_options[] = {
    { "inputs", "set number of inputs", OFFSET(nb_inputs), AV_OPT_TYPE_INT, {.i64=2}, 2, INT_MAX, .flags = FLAGS },
    { "layout", "set custom layout", OFFSET(layout), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, .flags = FLAGS },
    { "shortest", "force termination when the shortest input terminates", OFFSET(shortest), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, .flags = FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(xstack);

AVFilter ff_vf_xstack = {
    .name          = "xstack",
    .description   = NULL_IF_CONFIG_SMALL("Stack video inputs into custom layout."),
    .priv_size     = sizeof(StackContext),
    .priv_class    = &xstack_class,
    .query_formats = query_formats,
    .outputs       = outputs,
    .init          = init,
    .uninit        = uninit,
    .activate      = activate,
    .flags         = AVFILTER_FLAG_DYNAMIC_INPUTS,
};

#endif /* CONFIG_XSTACK_FILTER */
