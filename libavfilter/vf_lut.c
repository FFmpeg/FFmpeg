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
 * Compute a look-up table for binding the input value to the output
 * value, and apply it to input video.
 */

#include "libavutil/eval.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "internal.h"

static const char * const var_names[] = {
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

static const AVOption lut_options[] = {
    {"c0", "set component #0 expression", OFFSET(comp_expr_str[0]),  AV_OPT_TYPE_STRING, {.str="val"}, CHAR_MIN, CHAR_MAX},
    {"c1", "set component #1 expression", OFFSET(comp_expr_str[1]),  AV_OPT_TYPE_STRING, {.str="val"}, CHAR_MIN, CHAR_MAX},
    {"c2", "set component #2 expression", OFFSET(comp_expr_str[2]),  AV_OPT_TYPE_STRING, {.str="val"}, CHAR_MIN, CHAR_MAX},
    {"c3", "set component #3 expression", OFFSET(comp_expr_str[3]),  AV_OPT_TYPE_STRING, {.str="val"}, CHAR_MIN, CHAR_MAX},
    {"y",  "set Y expression", OFFSET(comp_expr_str[Y]),  AV_OPT_TYPE_STRING, {.str="val"}, CHAR_MIN, CHAR_MAX},
    {"u",  "set U expression", OFFSET(comp_expr_str[U]),  AV_OPT_TYPE_STRING, {.str="val"}, CHAR_MIN, CHAR_MAX},
    {"v",  "set V expression", OFFSET(comp_expr_str[V]),  AV_OPT_TYPE_STRING, {.str="val"}, CHAR_MIN, CHAR_MAX},
    {"r",  "set R expression", OFFSET(comp_expr_str[R]),  AV_OPT_TYPE_STRING, {.str="val"}, CHAR_MIN, CHAR_MAX},
    {"g",  "set G expression", OFFSET(comp_expr_str[G]),  AV_OPT_TYPE_STRING, {.str="val"}, CHAR_MIN, CHAR_MAX},
    {"b",  "set B expression", OFFSET(comp_expr_str[B]),  AV_OPT_TYPE_STRING, {.str="val"}, CHAR_MIN, CHAR_MAX},
    {"a",  "set A expression", OFFSET(comp_expr_str[A]),  AV_OPT_TYPE_STRING, {.str="val"}, CHAR_MIN, CHAR_MAX},
    {NULL},
};

static const char *lut_get_name(void *ctx)
{
    return "lut";
}

static const AVClass lut_class = {
    "LutContext",
    lut_get_name,
    lut_options
};

static int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    LutContext *lut = ctx->priv;
    int ret;

    lut->class = &lut_class;
    av_opt_set_defaults(lut);

    lut->is_rgb = !strcmp(ctx->filter->name, "lutrgb");
    lut->is_yuv = !strcmp(ctx->filter->name, "lutyuv");
    if (args && (ret = av_set_options_string(lut, args, "=", ":")) < 0)
        return ret;

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    LutContext *lut = ctx->priv;
    int i;

    for (i = 0; i < 4; i++) {
        av_expr_free(lut->comp_expr[i]);
        lut->comp_expr[i] = NULL;
        av_freep(&lut->comp_expr_str[i]);
    }
}

#define YUV_FORMATS                                         \
    PIX_FMT_YUV444P,  PIX_FMT_YUV422P,  PIX_FMT_YUV420P,    \
    PIX_FMT_YUV411P,  PIX_FMT_YUV410P,  PIX_FMT_YUV440P,    \
    PIX_FMT_YUVA420P,                                       \
    PIX_FMT_YUVJ444P, PIX_FMT_YUVJ422P, PIX_FMT_YUVJ420P,   \
    PIX_FMT_YUVJ440P

#define RGB_FORMATS                             \
    PIX_FMT_ARGB,         PIX_FMT_RGBA,         \
    PIX_FMT_ABGR,         PIX_FMT_BGRA,         \
    PIX_FMT_RGB24,        PIX_FMT_BGR24

static const enum PixelFormat yuv_pix_fmts[] = { YUV_FORMATS, PIX_FMT_NONE };
static const enum PixelFormat rgb_pix_fmts[] = { RGB_FORMATS, PIX_FMT_NONE };
static const enum PixelFormat all_pix_fmts[] = { RGB_FORMATS, YUV_FORMATS, PIX_FMT_NONE };

static int query_formats(AVFilterContext *ctx)
{
    LutContext *lut = ctx->priv;

    const enum PixelFormat *pix_fmts = lut->is_rgb ? rgb_pix_fmts :
                                       lut->is_yuv ? yuv_pix_fmts : all_pix_fmts;

    avfilter_set_common_pixel_formats(ctx, avfilter_make_format_list(pix_fmts));
    return 0;
}

/**
 * Clip value val in the minval - maxval range.
 */
static double clip(void *opaque, double val)
{
    LutContext *lut = opaque;
    double minval = lut->var_values[VAR_MINVAL];
    double maxval = lut->var_values[VAR_MAXVAL];

    return av_clip(val, minval, maxval);
}

/**
 * Compute gamma correction for value val, assuming the minval-maxval
 * range, val is clipped to a value contained in the same interval.
 */
static double compute_gammaval(void *opaque, double gamma)
{
    LutContext *lut = opaque;
    double val    = lut->var_values[VAR_CLIPVAL];
    double minval = lut->var_values[VAR_MINVAL];
    double maxval = lut->var_values[VAR_MAXVAL];

    return pow((val-minval)/(maxval-minval), gamma) * (maxval-minval)+minval;
}

static double (* const funcs1[])(void *, double) = {
    (void *)clip,
    (void *)compute_gammaval,
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
    LutContext *lut = ctx->priv;
    const AVPixFmtDescriptor *desc = &av_pix_fmt_descriptors[inlink->format];
    int min[4], max[4];
    int val, comp, ret;

    lut->hsub = desc->log2_chroma_w;
    lut->vsub = desc->log2_chroma_h;

    lut->var_values[VAR_W] = inlink->w;
    lut->var_values[VAR_H] = inlink->h;

    switch (inlink->format) {
    case PIX_FMT_YUV410P:
    case PIX_FMT_YUV411P:
    case PIX_FMT_YUV420P:
    case PIX_FMT_YUV422P:
    case PIX_FMT_YUV440P:
    case PIX_FMT_YUV444P:
    case PIX_FMT_YUVA420P:
        min[Y] = min[U] = min[V] = 16;
        max[Y] = 235;
        max[U] = max[V] = 240;
        min[A] = 0; max[A] = 255;
        break;
    default:
        min[0] = min[1] = min[2] = min[3] = 0;
        max[0] = max[1] = max[2] = max[3] = 255;
    }

    lut->is_yuv = lut->is_rgb = 0;
    if      (ff_fmt_is_in(inlink->format, yuv_pix_fmts)) lut->is_yuv = 1;
    else if (ff_fmt_is_in(inlink->format, rgb_pix_fmts)) lut->is_rgb = 1;

    if (lut->is_rgb) {
        switch (inlink->format) {
        case PIX_FMT_ARGB:  lut->rgba_map[A] = 0; lut->rgba_map[R] = 1; lut->rgba_map[G] = 2; lut->rgba_map[B] = 3; break;
        case PIX_FMT_ABGR:  lut->rgba_map[A] = 0; lut->rgba_map[B] = 1; lut->rgba_map[G] = 2; lut->rgba_map[R] = 3; break;
        case PIX_FMT_RGBA:
        case PIX_FMT_RGB24: lut->rgba_map[R] = 0; lut->rgba_map[G] = 1; lut->rgba_map[B] = 2; lut->rgba_map[A] = 3; break;
        case PIX_FMT_BGRA:
        case PIX_FMT_BGR24: lut->rgba_map[B] = 0; lut->rgba_map[G] = 1; lut->rgba_map[R] = 2; lut->rgba_map[A] = 3; break;
        }
        lut->step = av_get_bits_per_pixel(desc) >> 3;
    }

    for (comp = 0; comp < desc->nb_components; comp++) {
        double res;

        /* create the parsed expression */
        ret = av_expr_parse(&lut->comp_expr[comp], lut->comp_expr_str[comp],
                            var_names, funcs1_names, funcs1, NULL, NULL, 0, ctx);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Error when parsing the expression '%s' for the component %d.\n",
                   lut->comp_expr_str[comp], comp);
            return AVERROR(EINVAL);
        }

        /* compute the lut */
        lut->var_values[VAR_MAXVAL] = max[comp];
        lut->var_values[VAR_MINVAL] = min[comp];

        for (val = 0; val < 256; val++) {
            lut->var_values[VAR_VAL] = val;
            lut->var_values[VAR_CLIPVAL] = av_clip(val, min[comp], max[comp]);
            lut->var_values[VAR_NEGVAL] =
                av_clip(min[comp] + max[comp] - lut->var_values[VAR_VAL],
                        min[comp], max[comp]);

            res = av_expr_eval(lut->comp_expr[comp], lut->var_values, lut);
            if (isnan(res)) {
                av_log(ctx, AV_LOG_ERROR,
                       "Error when evaluating the expression '%s' for the value %d for the component #%d.\n",
                       lut->comp_expr_str[comp], val, comp);
                return AVERROR(EINVAL);
            }
            lut->lut[comp][val] = av_clip((int)res, min[comp], max[comp]);
            av_log(ctx, AV_LOG_DEBUG, "val[%d][%d] = %d\n", comp, val, lut->lut[comp][val]);
        }
    }

    return 0;
}

static void draw_slice(AVFilterLink *inlink, int y, int h, int slice_dir)
{
    AVFilterContext *ctx = inlink->dst;
    LutContext *lut = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFilterBufferRef *inpic  = inlink ->cur_buf;
    AVFilterBufferRef *outpic = outlink->out_buf;
    uint8_t *inrow, *outrow, *inrow0, *outrow0;
    int i, j, k, plane;

    if (lut->is_rgb) {
        /* packed */
        inrow0  = inpic ->data[0] + y * inpic ->linesize[0];
        outrow0 = outpic->data[0] + y * outpic->linesize[0];

        for (i = 0; i < h; i ++) {
            inrow  = inrow0;
            outrow = outrow0;
            for (j = 0; j < inlink->w; j++) {
                for (k = 0; k < lut->step; k++)
                    outrow[k] = lut->lut[lut->rgba_map[k]][inrow[k]];
                outrow += lut->step;
                inrow  += lut->step;
            }
            inrow0  += inpic ->linesize[0];
            outrow0 += outpic->linesize[0];
        }
    } else {
        /* planar */
        for (plane = 0; plane < 4 && inpic->data[plane]; plane++) {
            int vsub = plane == 1 || plane == 2 ? lut->vsub : 0;
            int hsub = plane == 1 || plane == 2 ? lut->hsub : 0;

            inrow  = inpic ->data[plane] + (y>>vsub) * inpic ->linesize[plane];
            outrow = outpic->data[plane] + (y>>vsub) * outpic->linesize[plane];

            for (i = 0; i < h>>vsub; i ++) {
                for (j = 0; j < inlink->w>>hsub; j++)
                    outrow[j] = lut->lut[plane][inrow[j]];
                inrow  += inpic ->linesize[plane];
                outrow += outpic->linesize[plane];
            }
        }
    }

    avfilter_draw_slice(outlink, y, h, slice_dir);
}

#define DEFINE_LUT_FILTER(name_, description_, init_)                   \
    AVFilter avfilter_vf_##name_ = {                                    \
        .name          = #name_,                                        \
        .description   = NULL_IF_CONFIG_SMALL(description_),            \
        .priv_size     = sizeof(LutContext),                            \
                                                                        \
        .init          = init_,                                         \
        .uninit        = uninit,                                        \
        .query_formats = query_formats,                                 \
                                                                        \
        .inputs    = (const AVFilterPad[]) {{ .name      = "default",   \
                                        .type            = AVMEDIA_TYPE_VIDEO, \
                                        .draw_slice      = draw_slice,  \
                                        .config_props    = config_props, \
                                        .min_perms       = AV_PERM_READ, }, \
                                      { .name = NULL}},                 \
        .outputs   = (const AVFilterPad[]) {{ .name      = "default",   \
                                        .type            = AVMEDIA_TYPE_VIDEO, }, \
                                      { .name = NULL}},                 \
    }

#if CONFIG_LUT_FILTER
DEFINE_LUT_FILTER(lut,    "Compute and apply a lookup table to the RGB/YUV input video.", init);
#endif
#if CONFIG_LUTYUV_FILTER
DEFINE_LUT_FILTER(lutyuv, "Compute and apply a lookup table to the YUV input video.",     init);
#endif
#if CONFIG_LUTRGB_FILTER
DEFINE_LUT_FILTER(lutrgb, "Compute and apply a lookup table to the RGB input video.",     init);
#endif

#if CONFIG_NEGATE_FILTER

static int negate_init(AVFilterContext *ctx, const char *args, void *opaque)
{
    LutContext *lut = ctx->priv;
    char lut_params[64];

    if (args)
        sscanf(args, "%d", &lut->negate_alpha);

    av_log(ctx, AV_LOG_DEBUG, "negate_alpha:%d\n", lut->negate_alpha);

    snprintf(lut_params, sizeof(lut_params), "c0=negval:c1=negval:c2=negval:a=%s",
             lut->negate_alpha ? "negval" : "val");

    return init(ctx, lut_params, opaque);
}

DEFINE_LUT_FILTER(negate, "Negate input video.", negate_init);

#endif
