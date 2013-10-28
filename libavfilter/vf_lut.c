/*
 * Copyright (c) 2011 Stefano Sabatini
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
 * Compute a look-up table for binding the input value to the output
 * value, and apply it to input video.
 */

#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "libavutil/eval.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

static const char *const var_names[] = {
    "E",
    "PHI",
    "PI",
    "w",        ///< width of the input video
    "h",        ///< height of the input video
    "val",      ///< input value for the pixel
    "maxval",   ///< max value for the pixel
    "minval",   ///< min value for the pixel
    "negval",   ///< negated value
    "clipval",
    NULL
};

enum var_name {
    VAR_E,
    VAR_PHI,
    VAR_PI,
    VAR_W,
    VAR_H,
    VAR_VAL,
    VAR_MAXVAL,
    VAR_MINVAL,
    VAR_NEGVAL,
    VAR_CLIPVAL,
    VAR_VARS_NB
};

typedef struct {
    const AVClass *class;
    uint8_t lut[4][256];  ///< lookup table for each component
    char   *comp_expr_str[4];
    AVExpr *comp_expr[4];
    int hsub, vsub;
    double var_values[VAR_VARS_NB];
    int is_rgb, is_yuv;
    int rgba_map[4];
    int step;
    int negate_alpha; /* only used by negate */
} LutContext;

#define Y 0
#define U 1
#define V 2
#define R 0
#define G 1
#define B 2
#define A 3

#define OFFSET(x) offsetof(LutContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM

static const AVOption lut_options[] = {
    { "c0", "set component #0 expression", OFFSET(comp_expr_str[0]),  AV_OPT_TYPE_STRING, { .str = "val" }, .flags = FLAGS },
    { "c1", "set component #1 expression", OFFSET(comp_expr_str[1]),  AV_OPT_TYPE_STRING, { .str = "val" }, .flags = FLAGS },
    { "c2", "set component #2 expression", OFFSET(comp_expr_str[2]),  AV_OPT_TYPE_STRING, { .str = "val" }, .flags = FLAGS },
    { "c3", "set component #3 expression", OFFSET(comp_expr_str[3]),  AV_OPT_TYPE_STRING, { .str = "val" }, .flags = FLAGS },
    { "y",  "set Y expression",            OFFSET(comp_expr_str[Y]),  AV_OPT_TYPE_STRING, { .str = "val" }, .flags = FLAGS },
    { "u",  "set U expression",            OFFSET(comp_expr_str[U]),  AV_OPT_TYPE_STRING, { .str = "val" }, .flags = FLAGS },
    { "v",  "set V expression",            OFFSET(comp_expr_str[V]),  AV_OPT_TYPE_STRING, { .str = "val" }, .flags = FLAGS },
    { "r",  "set R expression",            OFFSET(comp_expr_str[R]),  AV_OPT_TYPE_STRING, { .str = "val" }, .flags = FLAGS },
    { "g",  "set G expression",            OFFSET(comp_expr_str[G]),  AV_OPT_TYPE_STRING, { .str = "val" }, .flags = FLAGS },
    { "b",  "set B expression",            OFFSET(comp_expr_str[B]),  AV_OPT_TYPE_STRING, { .str = "val" }, .flags = FLAGS },
    { "a",  "set A expression",            OFFSET(comp_expr_str[A]),  AV_OPT_TYPE_STRING, { .str = "val" }, .flags = FLAGS },
    { NULL },
};

static av_cold int init(AVFilterContext *ctx)
{
    LutContext *s = ctx->priv;

    s->var_values[VAR_PHI] = M_PHI;
    s->var_values[VAR_PI]  = M_PI;
    s->var_values[VAR_E ]  = M_E;

    s->is_rgb = !strcmp(ctx->filter->name, "lutrgb");
    s->is_yuv = !strcmp(ctx->filter->name, "lutyuv");

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    LutContext *s = ctx->priv;
    int i;

    for (i = 0; i < 4; i++) {
        av_expr_free(s->comp_expr[i]);
        s->comp_expr[i] = NULL;
        av_freep(&s->comp_expr_str[i]);
    }
}

#define YUV_FORMATS                                         \
    AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,    \
    AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV410P,  AV_PIX_FMT_YUV440P,    \
    AV_PIX_FMT_YUVA420P,                                       \
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,   \
    AV_PIX_FMT_YUVJ440P

#define RGB_FORMATS                             \
    AV_PIX_FMT_ARGB,         AV_PIX_FMT_RGBA,         \
    AV_PIX_FMT_ABGR,         AV_PIX_FMT_BGRA,         \
    AV_PIX_FMT_RGB24,        AV_PIX_FMT_BGR24

static const enum AVPixelFormat yuv_pix_fmts[] = { YUV_FORMATS, AV_PIX_FMT_NONE };
static const enum AVPixelFormat rgb_pix_fmts[] = { RGB_FORMATS, AV_PIX_FMT_NONE };
static const enum AVPixelFormat all_pix_fmts[] = { RGB_FORMATS, YUV_FORMATS, AV_PIX_FMT_NONE };

static int query_formats(AVFilterContext *ctx)
{
    LutContext *s = ctx->priv;

    const enum AVPixelFormat *pix_fmts = s->is_rgb ? rgb_pix_fmts :
                                                     s->is_yuv ? yuv_pix_fmts :
                                                                 all_pix_fmts;

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

/**
 * Clip value val in the minval - maxval range.
 */
static double clip(void *opaque, double val)
{
    LutContext *s = opaque;
    double minval = s->var_values[VAR_MINVAL];
    double maxval = s->var_values[VAR_MAXVAL];

    return av_clip(val, minval, maxval);
}

/**
 * Compute gamma correction for value val, assuming the minval-maxval
 * range, val is clipped to a value contained in the same interval.
 */
static double compute_gammaval(void *opaque, double gamma)
{
    LutContext *s = opaque;
    double val    = s->var_values[VAR_CLIPVAL];
    double minval = s->var_values[VAR_MINVAL];
    double maxval = s->var_values[VAR_MAXVAL];

    return pow((val-minval)/(maxval-minval), gamma) * (maxval-minval)+minval;
}

static double (* const funcs1[])(void *, double) = {
    clip,
    compute_gammaval,
    NULL
};

static const char * const funcs1_names[] = {
    "clip",
    "gammaval",
    NULL
};

static int config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    LutContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int min[4], max[4];
    int val, comp, ret;

    s->hsub = desc->log2_chroma_w;
    s->vsub = desc->log2_chroma_h;

    s->var_values[VAR_W] = inlink->w;
    s->var_values[VAR_H] = inlink->h;

    switch (inlink->format) {
    case AV_PIX_FMT_YUV410P:
    case AV_PIX_FMT_YUV411P:
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUV440P:
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUVA420P:
        min[Y] = min[U] = min[V] = 16;
        max[Y] = 235;
        max[U] = max[V] = 240;
        min[A] = 0; max[A] = 255;
        break;
    default:
        min[0] = min[1] = min[2] = min[3] = 0;
        max[0] = max[1] = max[2] = max[3] = 255;
    }

    s->is_yuv = s->is_rgb = 0;
    if      (ff_fmt_is_in(inlink->format, yuv_pix_fmts)) s->is_yuv = 1;
    else if (ff_fmt_is_in(inlink->format, rgb_pix_fmts)) s->is_rgb = 1;

    if (s->is_rgb) {
        switch (inlink->format) {
        case AV_PIX_FMT_ARGB:  s->rgba_map[A] = 0; s->rgba_map[R] = 1; s->rgba_map[G] = 2; s->rgba_map[B] = 3; break;
        case AV_PIX_FMT_ABGR:  s->rgba_map[A] = 0; s->rgba_map[B] = 1; s->rgba_map[G] = 2; s->rgba_map[R] = 3; break;
        case AV_PIX_FMT_RGBA:
        case AV_PIX_FMT_RGB24: s->rgba_map[R] = 0; s->rgba_map[G] = 1; s->rgba_map[B] = 2; s->rgba_map[A] = 3; break;
        case AV_PIX_FMT_BGRA:
        case AV_PIX_FMT_BGR24: s->rgba_map[B] = 0; s->rgba_map[G] = 1; s->rgba_map[R] = 2; s->rgba_map[A] = 3; break;
        }
        s->step = av_get_bits_per_pixel(desc) >> 3;
    }

    for (comp = 0; comp < desc->nb_components; comp++) {
        double res;

        /* create the parsed expression */
        av_expr_free(s->comp_expr[comp]);
        s->comp_expr[comp] = NULL;
        ret = av_expr_parse(&s->comp_expr[comp], s->comp_expr_str[comp],
                            var_names, funcs1_names, funcs1, NULL, NULL, 0, ctx);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Error when parsing the expression '%s' for the component %d.\n",
                   s->comp_expr_str[comp], comp);
            return AVERROR(EINVAL);
        }

        /* compute the s */
        s->var_values[VAR_MAXVAL] = max[comp];
        s->var_values[VAR_MINVAL] = min[comp];

        for (val = 0; val < 256; val++) {
            s->var_values[VAR_VAL] = val;
            s->var_values[VAR_CLIPVAL] = av_clip(val, min[comp], max[comp]);
            s->var_values[VAR_NEGVAL] =
                av_clip(min[comp] + max[comp] - s->var_values[VAR_VAL],
                        min[comp], max[comp]);

            res = av_expr_eval(s->comp_expr[comp], s->var_values, s);
            if (isnan(res)) {
                av_log(ctx, AV_LOG_ERROR,
                       "Error when evaluating the expression '%s' for the value %d for the component #%d.\n",
                       s->comp_expr_str[comp], val, comp);
                return AVERROR(EINVAL);
            }
            s->lut[comp][val] = av_clip((int)res, min[comp], max[comp]);
            av_log(ctx, AV_LOG_DEBUG, "val[%d][%d] = %d\n", comp, val, s->lut[comp][val]);
        }
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    LutContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    uint8_t *inrow, *outrow, *inrow0, *outrow0;
    int i, j, k, plane;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    if (s->is_rgb) {
        /* packed */
        inrow0  = in ->data[0];
        outrow0 = out->data[0];

        for (i = 0; i < in->height; i ++) {
            inrow  = inrow0;
            outrow = outrow0;
            for (j = 0; j < inlink->w; j++) {
                for (k = 0; k < s->step; k++)
                    outrow[k] = s->lut[s->rgba_map[k]][inrow[k]];
                outrow += s->step;
                inrow  += s->step;
            }
            inrow0  += in ->linesize[0];
            outrow0 += out->linesize[0];
        }
    } else {
        /* planar */
        for (plane = 0; plane < 4 && in->data[plane]; plane++) {
            int vsub = plane == 1 || plane == 2 ? s->vsub : 0;
            int hsub = plane == 1 || plane == 2 ? s->hsub : 0;

            inrow  = in ->data[plane];
            outrow = out->data[plane];

            for (i = 0; i < in->height >> vsub; i ++) {
                for (j = 0; j < inlink->w>>hsub; j++)
                    outrow[j] = s->lut[plane][inrow[j]];
                inrow  += in ->linesize[plane];
                outrow += out->linesize[plane];
            }
        }
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static const AVFilterPad inputs[] = {
    { .name            = "default",
      .type            = AVMEDIA_TYPE_VIDEO,
      .filter_frame    = filter_frame,
      .config_props    = config_props,
    },
    { .name = NULL}
};
static const AVFilterPad outputs[] = {
    { .name            = "default",
      .type            = AVMEDIA_TYPE_VIDEO, },
    { .name = NULL}
};
#define DEFINE_LUT_FILTER(name_, description_, init_, options)          \
    static const AVClass name_ ## _class = {                            \
        .class_name = #name_,                                           \
        .item_name  = av_default_item_name,                             \
        .option     = options,                                          \
        .version    = LIBAVUTIL_VERSION_INT,                            \
    };                                                                  \
    AVFilter ff_vf_##name_ = {                                          \
        .name          = #name_,                                        \
        .description   = NULL_IF_CONFIG_SMALL(description_),            \
        .priv_size     = sizeof(LutContext),                            \
        .priv_class    = &name_ ## _class,                              \
                                                                        \
        .init          = init_,                                         \
        .uninit        = uninit,                                        \
        .query_formats = query_formats,                                 \
                                                                        \
        .inputs        = inputs,                                        \
        .outputs       = outputs,                                       \
    }

#if CONFIG_LUT_FILTER
DEFINE_LUT_FILTER(lut,    "Compute and apply a lookup table to the RGB/YUV input video.", init, lut_options);
#endif
#if CONFIG_LUTYUV_FILTER
DEFINE_LUT_FILTER(lutyuv, "Compute and apply a lookup table to the YUV input video.",     init, lut_options);
#endif
#if CONFIG_LUTRGB_FILTER
DEFINE_LUT_FILTER(lutrgb, "Compute and apply a lookup table to the RGB input video.",     init, lut_options);
#endif

#if CONFIG_NEGATE_FILTER

static const AVOption negate_options[] = {
    { "negate_alpha", NULL, OFFSET(negate_alpha), AV_OPT_TYPE_INT, { .i64 = 0 }, .flags = FLAGS },
    { NULL },
};

static av_cold int negate_init(AVFilterContext *ctx)
{
    LutContext *s = ctx->priv;
    int i;

    av_log(ctx, AV_LOG_DEBUG, "negate_alpha:%d\n", s->negate_alpha);

    for (i = 0; i < 4; i++) {
        s->comp_expr_str[i] = av_strdup((i == 3 && s->negate_alpha) ?
                                          "val" : "negval");
        if (!s->comp_expr_str[i]) {
            uninit(ctx);
            return AVERROR(ENOMEM);
        }
    }

    return init(ctx);
}

DEFINE_LUT_FILTER(negate, "Negate input video.", negate_init, negate_options);

#endif
