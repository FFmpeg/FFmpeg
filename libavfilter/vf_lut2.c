/*
 * Copyright (c) 2016 Paul B Mahol
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

#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "libavutil/eval.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "drawutils.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "framesync.h"

static const char *const var_names[] = {
    "w",        ///< width of the input video
    "h",        ///< height of the input video
    "x",        ///< input value for the pixel from input #1
    "y",        ///< input value for the pixel from input #2
    "bdx",      ///< input #1 video bitdepth
    "bdy",      ///< input #2 video bitdepth
    NULL
};

enum var_name {
    VAR_W,
    VAR_H,
    VAR_X,
    VAR_Y,
    VAR_BITDEPTHX,
    VAR_BITDEPTHY,
    VAR_VARS_NB
};

typedef struct LUT2Context {
    const AVClass *class;
    FFFrameSync fs;

    char   *comp_expr_str[4];

    AVExpr *comp_expr[4];
    double var_values[VAR_VARS_NB];
    uint16_t *lut[4];  ///< lookup table for each component
    int width[4], height[4];
    int nb_planes;
    int depth, depthx, depthy;
    int tlut2;
    AVFrame *prev_frame;        /* only used with tlut2 */

    void (*lut2)(struct LUT2Context *s, AVFrame *dst, AVFrame *srcx, AVFrame *srcy);

} LUT2Context;

#define OFFSET(x) offsetof(LUT2Context, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption options[] = {
    { "c0", "set component #0 expression", OFFSET(comp_expr_str[0]),  AV_OPT_TYPE_STRING, { .str = "x" }, .flags = FLAGS },
    { "c1", "set component #1 expression", OFFSET(comp_expr_str[1]),  AV_OPT_TYPE_STRING, { .str = "x" }, .flags = FLAGS },
    { "c2", "set component #2 expression", OFFSET(comp_expr_str[2]),  AV_OPT_TYPE_STRING, { .str = "x" }, .flags = FLAGS },
    { "c3", "set component #3 expression", OFFSET(comp_expr_str[3]),  AV_OPT_TYPE_STRING, { .str = "x" }, .flags = FLAGS },
    { NULL }
};

static av_cold void uninit(AVFilterContext *ctx)
{
    LUT2Context *s = ctx->priv;
    int i;

    ff_framesync_uninit(&s->fs);
    av_frame_free(&s->prev_frame);

    for (i = 0; i < 4; i++) {
        av_expr_free(s->comp_expr[i]);
        s->comp_expr[i] = NULL;
        av_freep(&s->comp_expr_str[i]);
        av_freep(&s->lut[i]);
    }
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
        AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
        AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV440P12,
        AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA444P9,
        AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA444P10,
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
        AV_PIX_FMT_GBRP12,
        AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP12,
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12,
        AV_PIX_FMT_NONE
    };

    return ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
}

static int config_inputx(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    LUT2Context *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int hsub = desc->log2_chroma_w;
    int vsub = desc->log2_chroma_h;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);
    s->height[1] = s->height[2] = AV_CEIL_RSHIFT(inlink->h, vsub);
    s->height[0] = s->height[3] = inlink->h;
    s->width[1]  = s->width[2]  = AV_CEIL_RSHIFT(inlink->w, hsub);
    s->width[0]  = s->width[3]  = inlink->w;

    s->var_values[VAR_W] = inlink->w;
    s->var_values[VAR_H] = inlink->h;
    s->depthx = desc->comp[0].depth;
    s->var_values[VAR_BITDEPTHX] = s->depthx;

    if (s->tlut2) {
        s->depthy = desc->comp[0].depth;
        s->var_values[VAR_BITDEPTHY] = s->depthy;
    }

    return 0;
}

static int config_inputy(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    LUT2Context *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    s->depthy = desc->comp[0].depth;
    s->var_values[VAR_BITDEPTHY] = s->depthy;

    return 0;
}

static void lut2_8bit(struct LUT2Context *s, AVFrame *out, AVFrame *srcx, AVFrame *srcy)
{
    int p, y, x;

    for (p = 0; p < s->nb_planes; p++) {
        const uint16_t *lut = s->lut[p];
        const uint8_t *srcxx, *srcyy;
        uint8_t *dst;

        dst   = out->data[p];
        srcxx = srcx->data[p];
        srcyy = srcy->data[p];

        for (y = 0; y < s->height[p]; y++) {
            for (x = 0; x < s->width[p]; x++) {
                dst[x] = lut[(srcyy[x] << s->depthx) | srcxx[x]];
            }

            dst   += out->linesize[p];
            srcxx += srcx->linesize[p];
            srcyy += srcy->linesize[p];
        }
    }
}

static void lut2_16bit(struct LUT2Context *s, AVFrame *out, AVFrame *srcx, AVFrame *srcy)
{
    int p, y, x;

    for (p = 0; p < s->nb_planes; p++) {
        const uint16_t *lut = s->lut[p];
        const uint16_t *srcxx, *srcyy;
        uint16_t *dst;

        dst   = (uint16_t *)out->data[p];
        srcxx = (uint16_t *)srcx->data[p];
        srcyy = (uint16_t *)srcy->data[p];

        for (y = 0; y < s->height[p]; y++) {
            for (x = 0; x < s->width[p]; x++) {
                dst[x] = lut[(srcyy[x] << s->depthx) | srcxx[x]];
            }

            dst   += out->linesize[p]  / 2;
            srcxx += srcx->linesize[p] / 2;
            srcyy += srcy->linesize[p] / 2;
        }
    }
}

static int process_frame(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    LUT2Context *s = fs->opaque;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out, *srcx = NULL, *srcy = NULL;
    int ret;

    if ((ret = ff_framesync_get_frame(&s->fs, 0, &srcx, 0)) < 0 ||
        (ret = ff_framesync_get_frame(&s->fs, 1, &srcy, 0)) < 0)
        return ret;

    if (ctx->is_disabled || !srcy) {
        out = av_frame_clone(srcx);
        if (!out)
            return AVERROR(ENOMEM);
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out)
            return AVERROR(ENOMEM);
        av_frame_copy_props(out, srcx);

        s->lut2(s, out, srcx, srcy);
    }

    out->pts = av_rescale_q(s->fs.pts, s->fs.time_base, outlink->time_base);

    return ff_filter_frame(outlink, out);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    LUT2Context *s = ctx->priv;
    int p, ret;

    s->depth = s->depthx + s->depthy;

    s->lut2 = s->depth > 16 ? lut2_16bit : lut2_8bit;

    for (p = 0; p < s->nb_planes; p++) {
        s->lut[p] = av_malloc_array(1 << s->depth, sizeof(uint16_t));
        if (!s->lut[p])
            return AVERROR(ENOMEM);
    }

    for (p = 0; p < s->nb_planes; p++) {
        double res;
        int x, y;

        /* create the parsed expression */
        av_expr_free(s->comp_expr[p]);
        s->comp_expr[p] = NULL;
        ret = av_expr_parse(&s->comp_expr[p], s->comp_expr_str[p],
                            var_names, NULL, NULL, NULL, NULL, 0, ctx);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Error when parsing the expression '%s' for the component %d.\n",
                   s->comp_expr_str[p], p);
            return AVERROR(EINVAL);
        }

        /* compute the lut */
        for (y = 0; y < (1 << s->depthx); y++) {
            s->var_values[VAR_Y] = y;
            for (x = 0; x < (1 << s->depthx); x++) {
                s->var_values[VAR_X] = x;
                res = av_expr_eval(s->comp_expr[p], s->var_values, s);
                if (isnan(res)) {
                    av_log(ctx, AV_LOG_ERROR,
                           "Error when evaluating the expression '%s' for the values %d and %d for the component %d.\n",
                           s->comp_expr_str[p], x, y, p);
                    return AVERROR(EINVAL);
                }

                s->lut[p][(y << s->depthx) + x] = res;
            }
        }
    }

    return 0;
}

static int lut2_config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    LUT2Context *s = ctx->priv;
    AVFilterLink *srcx = ctx->inputs[0];
    AVFilterLink *srcy = ctx->inputs[1];
    FFFrameSyncIn *in;
    int ret;

    if (srcx->format != srcy->format) {
        av_log(ctx, AV_LOG_ERROR, "inputs must be of same pixel format\n");
        return AVERROR(EINVAL);
    }
    if (srcx->w                       != srcy->w ||
        srcx->h                       != srcy->h ||
        srcx->sample_aspect_ratio.num != srcy->sample_aspect_ratio.num ||
        srcx->sample_aspect_ratio.den != srcy->sample_aspect_ratio.den) {
        av_log(ctx, AV_LOG_ERROR, "First input link %s parameters "
               "(size %dx%d, SAR %d:%d) do not match the corresponding "
               "second input link %s parameters (%dx%d, SAR %d:%d)\n",
               ctx->input_pads[0].name, srcx->w, srcx->h,
               srcx->sample_aspect_ratio.num,
               srcx->sample_aspect_ratio.den,
               ctx->input_pads[1].name,
               srcy->w, srcy->h,
               srcy->sample_aspect_ratio.num,
               srcy->sample_aspect_ratio.den);
        return AVERROR(EINVAL);
    }

    outlink->w = srcx->w;
    outlink->h = srcx->h;
    outlink->time_base = srcx->time_base;
    outlink->sample_aspect_ratio = srcx->sample_aspect_ratio;
    outlink->frame_rate = srcx->frame_rate;

    if ((ret = ff_framesync_init(&s->fs, ctx, 2)) < 0)
        return ret;

    in = s->fs.in;
    in[0].time_base = srcx->time_base;
    in[1].time_base = srcy->time_base;
    in[0].sync   = 2;
    in[0].before = EXT_STOP;
    in[0].after  = EXT_INFINITY;
    in[1].sync   = 1;
    in[1].before = EXT_STOP;
    in[1].after  = EXT_INFINITY;
    s->fs.opaque   = s;
    s->fs.on_event = process_frame;

    if ((ret = config_output(outlink)) < 0)
        return ret;

    return ff_framesync_configure(&s->fs);
}

static int activate(AVFilterContext *ctx)
{
    LUT2Context *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "srcx",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_inputx,
    },
    {
        .name         = "srcy",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_inputy,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = lut2_config_output,
    },
    { NULL }
};

#define lut2_options options

FRAMESYNC_DEFINE_CLASS(lut2, LUT2Context, fs);

AVFilter ff_vf_lut2 = {
    .name          = "lut2",
    .description   = NULL_IF_CONFIG_SMALL("Compute and apply a lookup table from two video inputs."),
    .preinit       = lut2_framesync_preinit,
    .priv_size     = sizeof(LUT2Context),
    .priv_class    = &lut2_class,
    .uninit        = uninit,
    .query_formats = query_formats,
    .activate      = activate,
    .inputs        = inputs,
    .outputs       = outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
};

#if CONFIG_TLUT2_FILTER

static av_cold int init(AVFilterContext *ctx)
{
    LUT2Context *s = ctx->priv;

    s->tlut2 = !strcmp(ctx->filter->name, "tlut2");

    return 0;
}

static int tlut2_filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    LUT2Context *s = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];

    if (s->prev_frame) {
        AVFrame *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&s->prev_frame);
            s->prev_frame = frame;
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, frame);
        s->lut2(s, out, frame, s->prev_frame);
        av_frame_free(&s->prev_frame);
        s->prev_frame = frame;
        return ff_filter_frame(outlink, out);
    }
    s->prev_frame = frame;
    return 0;
}

#define tlut2_options options

AVFILTER_DEFINE_CLASS(tlut2);

static const AVFilterPad tlut2_inputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .filter_frame  = tlut2_filter_frame,
        .config_props  = config_inputx,
    },
    { NULL }
};

static const AVFilterPad tlut2_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
    { NULL }
};

AVFilter ff_vf_tlut2 = {
    .name          = "tlut2",
    .description   = NULL_IF_CONFIG_SMALL("Compute and apply a lookup table from two successive frames."),
    .priv_size     = sizeof(LUT2Context),
    .priv_class    = &tlut2_class,
    .query_formats = query_formats,
    .init          = init,
    .uninit        = uninit,
    .inputs        = tlut2_inputs,
    .outputs       = tlut2_outputs,
};

#endif
