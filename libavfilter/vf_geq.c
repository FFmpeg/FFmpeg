/*
 * Copyright (C) 2006 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (C) 2012 Clément Bœsch <ubitux@gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * @file
 * Generic equation change filter
 * Originally written by Michael Niedermayer for the MPlayer project, and
 * ported by Clément Bœsch for FFmpeg.
 */

#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "internal.h"

typedef struct {
    const AVClass *class;
    AVExpr *e[3];               ///< expressions for each plane
    char *expr_str[3];          ///< expression strings for each plane
    int framenum;               ///< frame counter
    AVFilterBufferRef *picref;  ///< current input buffer
    int hsub, vsub;             ///< chroma subsampling
} GEQContext;

#define OFFSET(x) offsetof(GEQContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption geq_options[] = {
    { "lum_expr",   "set luminance expression",   OFFSET(expr_str),                   AV_OPT_TYPE_STRING, {.str=NULL}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "cb_expr",    "set chroma blue expression", OFFSET(expr_str) +   sizeof(char*), AV_OPT_TYPE_STRING, {.str=NULL}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "cr_expr",    "set chroma red expression",  OFFSET(expr_str) + 2*sizeof(char*), AV_OPT_TYPE_STRING, {.str=NULL}, CHAR_MIN, CHAR_MAX, FLAGS },
    {NULL},
};

AVFILTER_DEFINE_CLASS(geq);

static inline double getpix(void *priv, double x, double y, int plane)
{
    int xi, yi;
    GEQContext *geq = priv;
    AVFilterBufferRef *picref = geq->picref;
    const uint8_t *src = picref->data[plane];
    const int linesize = picref->linesize[plane];
    const int w = picref->video->w >> (plane ? geq->hsub : 0);
    const int h = picref->video->h >> (plane ? geq->vsub : 0);

    xi = x = av_clipf(x, 0, w - 2);
    yi = y = av_clipf(y, 0, h - 2);

    x -= xi;
    y -= yi;

    return (1-y)*((1-x)*src[xi +  yi    * linesize] + x*src[xi + 1 +  yi    * linesize])
          +   y *((1-x)*src[xi + (yi+1) * linesize] + x*src[xi + 1 + (yi+1) * linesize]);
}

//TODO: cubic interpolate
//TODO: keep the last few frames
static double lum(void *priv, double x, double y) { return getpix(priv, x, y, 0); }
static double  cb(void *priv, double x, double y) { return getpix(priv, x, y, 1); }
static double  cr(void *priv, double x, double y) { return getpix(priv, x, y, 2); }

static const char *const var_names[] = {   "X",   "Y",   "W",   "H",   "N",   "SW",   "SH",   "T",        NULL };
enum                                   { VAR_X, VAR_Y, VAR_W, VAR_H, VAR_N, VAR_SW, VAR_SH, VAR_T, VAR_VARS_NB };

static av_cold int geq_init(AVFilterContext *ctx, const char *args)
{
    GEQContext *geq = ctx->priv;
    int plane, ret = 0;
    static const char *shorthand[] = { "lum_expr", "cb_expr", "cr_expr", NULL };

    geq->class = &geq_class;
    av_opt_set_defaults(geq);

    if ((ret = av_opt_set_from_string(geq, args, shorthand, "=", ":")) < 0)
        return ret;

    if (!geq->expr_str[0]) {
        av_log(ctx, AV_LOG_ERROR, "Luminance expression is mandatory\n");
        ret = AVERROR(EINVAL);
        goto end;
    }

    if (!geq->expr_str[1] && !geq->expr_str[2]) {
        /* No chroma at all: fallback on luma */
        geq->expr_str[1] = av_strdup(geq->expr_str[0]);
        geq->expr_str[2] = av_strdup(geq->expr_str[0]);
    } else {
        /* One chroma unspecified, fallback on the other */
        if (!geq->expr_str[1]) geq->expr_str[1] = av_strdup(geq->expr_str[2]);
        if (!geq->expr_str[2]) geq->expr_str[2] = av_strdup(geq->expr_str[1]);
    }

    if (!geq->expr_str[1] || !geq->expr_str[2]) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    for (plane = 0; plane < 3; plane++) {
        static double (*p[])(void *, double, double) = { lum, cb, cr };
        static const char *const func2_names[]    = { "lum", "cb", "cr", "p", NULL };
        double (*func2[])(void *, double, double) = { lum, cb, cr, p[plane], NULL };

        ret = av_expr_parse(&geq->e[plane], geq->expr_str[plane], var_names,
                            NULL, NULL, func2_names, func2, 0, ctx);
        if (ret < 0)
            break;
    }

end:
    return ret;
}

static int geq_query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV410P,  AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_NONE
    };
    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static int geq_config_props(AVFilterLink *inlink)
{
    GEQContext *geq = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    geq->hsub = desc->log2_chroma_w;
    geq->vsub = desc->log2_chroma_h;
    return 0;
}

static int geq_filter_frame(AVFilterLink *inlink, AVFilterBufferRef *in)
{
    int plane;
    GEQContext *geq = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFilterBufferRef *out;
    double values[VAR_VARS_NB] = {
        [VAR_N] = geq->framenum++,
        [VAR_T] = in->pts == AV_NOPTS_VALUE ? NAN : in->pts * av_q2d(inlink->time_base),
    };

    geq->picref = in;
    out = ff_get_video_buffer(outlink, AV_PERM_WRITE, outlink->w, outlink->h);
    if (!out) {
        avfilter_unref_bufferp(&in);
        return AVERROR(ENOMEM);
    }
    avfilter_copy_buffer_ref_props(out, in);

    for (plane = 0; plane < 3; plane++) {
        int x, y;
        uint8_t *dst = out->data[plane];
        const int linesize = out->linesize[plane];
        const int w = inlink->w >> (plane ? geq->hsub : 0);
        const int h = inlink->h >> (plane ? geq->vsub : 0);

        values[VAR_W]  = w;
        values[VAR_H]  = h;
        values[VAR_SW] = w / (double)inlink->w;
        values[VAR_SH] = h / (double)inlink->h;

        for (y = 0; y < h; y++) {
            values[VAR_Y] = y;
            for (x = 0; x < w; x++) {
                values[VAR_X] = x;
                dst[x] = av_expr_eval(geq->e[plane], values, geq);
            }
            dst += linesize;
        }
    }

    avfilter_unref_bufferp(&geq->picref);
    return ff_filter_frame(outlink, out);
}

static av_cold void geq_uninit(AVFilterContext *ctx)
{
    int i;
    GEQContext *geq = ctx->priv;

    for (i = 0; i < FF_ARRAY_ELEMS(geq->e); i++)
        av_expr_free(geq->e[i]);
    av_opt_free(geq);
}

static const AVFilterPad geq_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = geq_config_props,
        .filter_frame = geq_filter_frame,
        .min_perms    = AV_PERM_READ,
    },
    { NULL }
};

static const AVFilterPad geq_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter avfilter_vf_geq = {
    .name          = "geq",
    .description   = NULL_IF_CONFIG_SMALL("Apply generic equation to each pixel."),
    .priv_size     = sizeof(GEQContext),
    .init          = geq_init,
    .uninit        = geq_uninit,
    .query_formats = geq_query_formats,
    .inputs        = geq_inputs,
    .outputs       = geq_outputs,
    .priv_class    = &geq_class,
};
