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

#include "config_components.h"

#include "libavutil/avstring.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "drawutils.h"
#include "filters.h"
#include "formats.h"
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
    int nb_grid_columns;
    int nb_grid_rows;
    uint8_t fillcolor[4];
    char *fillcolor_str;
    int fillcolor_enable;

    FFDrawContext draw;
    FFDrawColor color;

    StackItem *items;
    AVFrame **frames;
    FFFrameSync fs;
} StackContext;

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    const StackContext *s = ctx->priv;
    int reject_flags = AV_PIX_FMT_FLAG_BITSTREAM |
                       AV_PIX_FMT_FLAG_HWACCEL   |
                       AV_PIX_FMT_FLAG_PAL;

    if (s->fillcolor_enable) {
        return ff_set_common_formats2(ctx, cfg_in, cfg_out,
                                      ff_draw_supported_pixel_formats(0));
    }

    return ff_set_common_formats2(ctx, cfg_in, cfg_out,
                                  ff_formats_pixdesc_filter(0, reject_flags));
}

static av_cold int init(AVFilterContext *ctx)
{
    StackContext *s = ctx->priv;
    int i, ret;

    if (!strcmp(ctx->filter->name, "vstack"))
        s->is_vertical = 1;

    if (!strcmp(ctx->filter->name, "hstack"))
        s->is_horizontal = 1;

    if (!strcmp(ctx->filter->name, "xstack")) {
        int is_grid;
        if (strcmp(s->fillcolor_str, "none") &&
            av_parse_color(s->fillcolor, s->fillcolor_str, -1, ctx) >= 0) {
            s->fillcolor_enable = 1;
        } else {
            s->fillcolor_enable = 0;
        }
        is_grid = s->nb_grid_rows && s->nb_grid_columns;
        if (s->layout && is_grid) {
            av_log(ctx, AV_LOG_ERROR, "Both layout and grid were specified. Only one is allowed.\n");
            return AVERROR(EINVAL);
        }
        if (!s->layout && !is_grid) {
            if (s->nb_inputs == 2) {
                s->nb_grid_rows = 1;
                s->nb_grid_columns = 2;
                is_grid = 1;
            } else {
                av_log(ctx, AV_LOG_ERROR, "No layout or grid specified.\n");
                return AVERROR(EINVAL);
            }
        }

        if (is_grid)
            s->nb_inputs = s->nb_grid_rows * s->nb_grid_columns;
    }

    s->frames = av_calloc(s->nb_inputs, sizeof(*s->frames));
    if (!s->frames)
        return AVERROR(ENOMEM);

    s->items = av_calloc(s->nb_inputs, sizeof(*s->items));
    if (!s->items)
        return AVERROR(ENOMEM);

    for (i = 0; i < s->nb_inputs; i++) {
        AVFilterPad pad = { 0 };

        pad.type = AVMEDIA_TYPE_VIDEO;
        pad.name = av_asprintf("input%d", i);
        if (!pad.name)
            return AVERROR(ENOMEM);

        if ((ret = ff_append_inpad_free_name(ctx, &pad)) < 0)
            return ret;
    }

    return 0;
}

static int process_slice(AVFilterContext *ctx, void *arg, int job, int nb_jobs)
{
    StackContext *s = ctx->priv;
    AVFrame *out = arg;
    AVFrame **in = s->frames;
    const int start = (s->nb_inputs *  job   ) / nb_jobs;
    const int end   = (s->nb_inputs * (job+1)) / nb_jobs;

    for (int i = start; i < end; i++) {
        StackItem *item = &s->items[i];

        for (int p = 0; p < s->nb_planes; p++) {
            av_image_copy_plane(out->data[p] + out->linesize[p] * item->y[p] + item->x[p],
                                out->linesize[p],
                                in[i]->data[p],
                                in[i]->linesize[p],
                                item->linesize[p], item->height[p]);
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
    int i, ret;

    for (i = 0; i < s->nb_inputs; i++) {
        if ((ret = ff_framesync_get_frame(&s->fs, i, &in[i], 0)) < 0)
            return ret;
    }

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out)
        return AVERROR(ENOMEM);
    out->pts = av_rescale_q(s->fs.pts, s->fs.time_base, outlink->time_base);
    out->sample_aspect_ratio = outlink->sample_aspect_ratio;

    if (s->fillcolor_enable)
        ff_fill_rectangle(&s->draw, &s->color, out->data, out->linesize,
                          0, 0, outlink->w, outlink->h);

    ff_filter_execute(ctx, process_slice, out, NULL,
                      FFMIN(s->nb_inputs, ff_filter_get_nb_threads(ctx)));

    return ff_filter_frame(outlink, out);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    StackContext *s = ctx->priv;
    FilterLink *il = ff_filter_link(ctx->inputs[0]);
    FilterLink *ol = ff_filter_link(outlink);
    AVRational frame_rate = il->frame_rate;
    AVRational sar = ctx->inputs[0]->sample_aspect_ratio;
    int height = ctx->inputs[0]->h;
    int width = ctx->inputs[0]->w;
    FFFrameSyncIn *in;
    int i, ret;

    s->desc = av_pix_fmt_desc_get(outlink->format);
    if (!s->desc)
        return AVERROR_BUG;

    if (s->is_vertical) {
        for (i = 0; i < s->nb_inputs; i++) {
            AVFilterLink *inlink = ctx->inputs[i];
            StackItem *item = &s->items[i];

            if (ctx->inputs[i]->w != width) {
                av_log(ctx, AV_LOG_ERROR, "Input %d width %d does not match input %d width %d.\n", i, ctx->inputs[i]->w, 0, width);
                return AVERROR(EINVAL);
            }

            if ((ret = av_image_fill_linesizes(item->linesize, inlink->format, inlink->w)) < 0) {
                return ret;
            }

            item->height[1] = item->height[2] = AV_CEIL_RSHIFT(inlink->h, s->desc->log2_chroma_h);
            item->height[0] = item->height[3] = inlink->h;

            if (i) {
                item->y[1] = item->y[2] = AV_CEIL_RSHIFT(height, s->desc->log2_chroma_h);
                item->y[0] = item->y[3] = height;

                height += ctx->inputs[i]->h;
            }
        }
    } else if (s->is_horizontal) {
        for (i = 0; i < s->nb_inputs; i++) {
            AVFilterLink *inlink = ctx->inputs[i];
            StackItem *item = &s->items[i];

            if (ctx->inputs[i]->h != height) {
                av_log(ctx, AV_LOG_ERROR, "Input %d height %d does not match input %d height %d.\n", i, ctx->inputs[i]->h, 0, height);
                return AVERROR(EINVAL);
            }

            if ((ret = av_image_fill_linesizes(item->linesize, inlink->format, inlink->w)) < 0) {
                return ret;
            }

            item->height[1] = item->height[2] = AV_CEIL_RSHIFT(inlink->h, s->desc->log2_chroma_h);
            item->height[0] = item->height[3] = inlink->h;

            if (i) {
                if ((ret = av_image_fill_linesizes(item->x, inlink->format, width)) < 0) {
                    return ret;
                }

                width += ctx->inputs[i]->w;
            }
        }
    } else if (s->nb_grid_rows && s->nb_grid_columns) {
        int inw = 0, inh = 0;
        int k = 0;
        int row_height;
        height = 0;
        width = 0;
        for (i = 0; i < s->nb_grid_rows; i++, inh += row_height) {
            row_height = ctx->inputs[i * s->nb_grid_columns]->h;
            inw = 0;
            for (int j = 0; j < s->nb_grid_columns; j++, k++) {
                AVFilterLink *inlink = ctx->inputs[k];
                StackItem *item = &s->items[k];

                if (ctx->inputs[k]->h != row_height) {
                    av_log(ctx, AV_LOG_ERROR, "Input %d height %d does not match current row's height %d.\n",
                           k, ctx->inputs[k]->h, row_height);
                    return AVERROR(EINVAL);
                }

                if ((ret = av_image_fill_linesizes(item->linesize, inlink->format, inlink->w)) < 0) {
                    return ret;
                }

                item->height[1] = item->height[2] = AV_CEIL_RSHIFT(inlink->h, s->desc->log2_chroma_h);
                item->height[0] = item->height[3] = inlink->h;

                if ((ret = av_image_fill_linesizes(item->x, inlink->format, inw)) < 0) {
                    return ret;
                }

                item->y[1] = item->y[2] = AV_CEIL_RSHIFT(inh, s->desc->log2_chroma_h);
                item->y[0] = item->y[3] = inh;
                inw += ctx->inputs[k]->w;
            }
            height += row_height;
            if (!i)
                width = inw;
            if (i && width != inw) {
                av_log(ctx, AV_LOG_ERROR, "Row %d width %d does not match previous row width %d.\n", i, inw, width);
                return AVERROR(EINVAL);
            }
        }
    } else {
        char *arg, *p = s->layout, *saveptr = NULL;
        char *arg2, *p2, *saveptr2 = NULL;
        char *arg3, *p3, *saveptr3 = NULL;
        int inw, inh, size;

        if (s->fillcolor_enable) {
            const AVFilterLink *inlink = ctx->inputs[0];
            ret = ff_draw_init2(&s->draw, inlink->format, inlink->colorspace, inlink->color_range, 0);
            if (ret < 0) {
                av_log(ctx, AV_LOG_ERROR, "Failed to initialize FFDrawContext\n");
                return ret;
            }
            ff_draw_color(&s->draw, &s->color, s->fillcolor);
        }

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
    ol->frame_rate      = frame_rate;
    outlink->sample_aspect_ratio = sar;

    for (i = 1; i < s->nb_inputs; i++) {
        il = ff_filter_link(ctx->inputs[i]);
        if (ol->frame_rate.num != il->frame_rate.num ||
            ol->frame_rate.den != il->frame_rate.den) {
            av_log(ctx, AV_LOG_VERBOSE,
                    "Video inputs have different frame rates, output will be VFR\n");
            ol->frame_rate = av_make_q(1, 0);
            break;
        }
    }

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

    ff_framesync_uninit(&s->fs);
    av_freep(&s->frames);
    av_freep(&s->items);
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

AVFILTER_DEFINE_CLASS_EXT(stack, "(h|v)stack", stack_options);

static const AVFilterPad outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

#if CONFIG_HSTACK_FILTER

const FFFilter ff_vf_hstack = {
    .p.name        = "hstack",
    .p.description = NULL_IF_CONFIG_SMALL("Stack video inputs horizontally."),
    .p.priv_class  = &stack_class,
    .p.flags       = AVFILTER_FLAG_DYNAMIC_INPUTS | AVFILTER_FLAG_SLICE_THREADS,
    .priv_size     = sizeof(StackContext),
    FILTER_OUTPUTS(outputs),
    FILTER_QUERY_FUNC2(query_formats),
    .init          = init,
    .uninit        = uninit,
    .activate      = activate,
};

#endif /* CONFIG_HSTACK_FILTER */

#if CONFIG_VSTACK_FILTER

const FFFilter ff_vf_vstack = {
    .p.name        = "vstack",
    .p.description = NULL_IF_CONFIG_SMALL("Stack video inputs vertically."),
    .p.priv_class  = &stack_class,
    .p.flags       = AVFILTER_FLAG_DYNAMIC_INPUTS | AVFILTER_FLAG_SLICE_THREADS,
    .priv_size     = sizeof(StackContext),
    FILTER_OUTPUTS(outputs),
    FILTER_QUERY_FUNC2(query_formats),
    .init          = init,
    .uninit        = uninit,
    .activate      = activate,
};

#endif /* CONFIG_VSTACK_FILTER */

#if CONFIG_XSTACK_FILTER

static const AVOption xstack_options[] = {
    { "inputs", "set number of inputs", OFFSET(nb_inputs), AV_OPT_TYPE_INT, {.i64=2}, 2, INT_MAX, .flags = FLAGS },
    { "layout", "set custom layout", OFFSET(layout), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, .flags = FLAGS },
    { "grid", "set fixed size grid layout", OFFSET(nb_grid_columns), AV_OPT_TYPE_IMAGE_SIZE, {.str=NULL}, 0, 0, .flags = FLAGS },
    { "shortest", "force termination when the shortest input terminates", OFFSET(shortest), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, .flags = FLAGS },
    { "fill",  "set the color for unused pixels", OFFSET(fillcolor_str), AV_OPT_TYPE_STRING, {.str = "none"}, .flags = FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(xstack);

const FFFilter ff_vf_xstack = {
    .p.name        = "xstack",
    .p.description = NULL_IF_CONFIG_SMALL("Stack video inputs into custom layout."),
    .p.priv_class  = &xstack_class,
    .p.flags       = AVFILTER_FLAG_DYNAMIC_INPUTS | AVFILTER_FLAG_SLICE_THREADS,
    .priv_size     = sizeof(StackContext),
    FILTER_OUTPUTS(outputs),
    FILTER_QUERY_FUNC2(query_formats),
    .init          = init,
    .uninit        = uninit,
    .activate      = activate,
};

#endif /* CONFIG_XSTACK_FILTER */
