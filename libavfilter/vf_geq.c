/*
 * Copyright (C) 2006 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (C) 2012 Clément Bœsch <u pkh me>
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
 * Generic equation change filter
 * Originally written by Michael Niedermayer for the MPlayer project, and
 * ported by Clément Bœsch for FFmpeg.
 */

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "internal.h"

#define MAX_NB_THREADS 32
#define NB_PLANES 4

enum InterpolationMethods {
    INTERP_NEAREST,
    INTERP_BILINEAR,
    NB_INTERP
};

static const char *const var_names[] = {   "X",   "Y",   "W",   "H",   "N",   "SW",   "SH",   "T",        NULL };
enum                                   { VAR_X, VAR_Y, VAR_W, VAR_H, VAR_N, VAR_SW, VAR_SH, VAR_T, VAR_VARS_NB };

typedef struct GEQContext {
    const AVClass *class;
    AVExpr *e[NB_PLANES][MAX_NB_THREADS]; ///< expressions for each plane and thread
    char *expr_str[4+3];        ///< expression strings for each plane
    AVFrame *picref;            ///< current input buffer
    uint8_t *dst;               ///< reference pointer to the 8bits output
    uint16_t *dst16;            ///< reference pointer to the 16bits output
    double values[VAR_VARS_NB]; ///< expression values
    int hsub, vsub;             ///< chroma subsampling
    int planes;                 ///< number of planes
    int interpolation;
    int is_rgb;
    int bps;

    double *pixel_sums[NB_PLANES];
    int needs_sum[NB_PLANES];
} GEQContext;

enum { Y = 0, U, V, A, G, B, R };

#define OFFSET(x) offsetof(GEQContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption geq_options[] = {
    { "lum_expr",   "set luminance expression",   OFFSET(expr_str[Y]), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "lum",        "set luminance expression",   OFFSET(expr_str[Y]), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "cb_expr",    "set chroma blue expression", OFFSET(expr_str[U]), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "cb",         "set chroma blue expression", OFFSET(expr_str[U]), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "cr_expr",    "set chroma red expression",  OFFSET(expr_str[V]), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "cr",         "set chroma red expression",  OFFSET(expr_str[V]), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "alpha_expr", "set alpha expression",       OFFSET(expr_str[A]), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "a",          "set alpha expression",       OFFSET(expr_str[A]), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "red_expr",   "set red expression",         OFFSET(expr_str[R]), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "r",          "set red expression",         OFFSET(expr_str[R]), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "green_expr", "set green expression",       OFFSET(expr_str[G]), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "g",          "set green expression",       OFFSET(expr_str[G]), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "blue_expr",  "set blue expression",        OFFSET(expr_str[B]), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "b",          "set blue expression",        OFFSET(expr_str[B]), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "interpolation","set interpolation method", OFFSET(interpolation), AV_OPT_TYPE_INT, {.i64=INTERP_BILINEAR}, 0, NB_INTERP-1, FLAGS, "interp" },
    { "i",          "set interpolation method",   OFFSET(interpolation), AV_OPT_TYPE_INT, {.i64=INTERP_BILINEAR}, 0, NB_INTERP-1, FLAGS, "interp" },
    { "nearest",    "nearest interpolation",      0,                   AV_OPT_TYPE_CONST, {.i64=INTERP_NEAREST},  0, 0, FLAGS, "interp" },
    { "n",          "nearest interpolation",      0,                   AV_OPT_TYPE_CONST, {.i64=INTERP_NEAREST},  0, 0, FLAGS, "interp" },
    { "bilinear",   "bilinear interpolation",     0,                   AV_OPT_TYPE_CONST, {.i64=INTERP_BILINEAR}, 0, 0, FLAGS, "interp" },
    { "b",          "bilinear interpolation",     0,                   AV_OPT_TYPE_CONST, {.i64=INTERP_BILINEAR}, 0, 0, FLAGS, "interp" },
    {NULL},
};

AVFILTER_DEFINE_CLASS(geq);

static inline double getpix(void *priv, double x, double y, int plane)
{
    int xi, yi;
    GEQContext *geq = priv;
    AVFrame *picref = geq->picref;
    const uint8_t *src = picref->data[plane];
    int linesize = picref->linesize[plane];
    const int w = (plane == 1 || plane == 2) ? AV_CEIL_RSHIFT(picref->width,  geq->hsub) : picref->width;
    const int h = (plane == 1 || plane == 2) ? AV_CEIL_RSHIFT(picref->height, geq->vsub) : picref->height;

    if (!src)
        return 0;

    if (geq->interpolation == INTERP_BILINEAR) {
        xi = x = av_clipd(x, 0, w - 2);
        yi = y = av_clipd(y, 0, h - 2);

        x -= xi;
        y -= yi;

        if (geq->bps > 8) {
            const uint16_t *src16 = (const uint16_t*)src;
            linesize /= 2;

            return (1-y)*((1-x)*src16[xi +  yi    * linesize] + x*src16[xi + 1 +  yi    * linesize])
                  +   y *((1-x)*src16[xi + (yi+1) * linesize] + x*src16[xi + 1 + (yi+1) * linesize]);
        } else {
            return (1-y)*((1-x)*src[xi +  yi    * linesize] + x*src[xi + 1 +  yi    * linesize])
                  +   y *((1-x)*src[xi + (yi+1) * linesize] + x*src[xi + 1 + (yi+1) * linesize]);
        }
    } else {
        xi = av_clipd(x, 0, w - 1);
        yi = av_clipd(y, 0, h - 1);

        if (geq->bps > 8) {
            const uint16_t *src16 = (const uint16_t*)src;
            linesize /= 2;

            return src16[xi + yi * linesize];
        } else {
            return src[xi + yi * linesize];
        }
    }
}

static int calculate_sums(GEQContext *geq, int plane, int w, int h)
{
    int xi, yi;
    AVFrame *picref = geq->picref;
    const uint8_t *src = picref->data[plane];
    int linesize = picref->linesize[plane];

    if (!geq->pixel_sums[plane])
        geq->pixel_sums[plane] = av_malloc_array(w, h * sizeof (*geq->pixel_sums[plane]));
    if (!geq->pixel_sums[plane])
        return AVERROR(ENOMEM);
    if (geq->bps > 8)
        linesize /= 2;
    for (yi = 0; yi < h; yi ++) {
        if (geq->bps > 8) {
            const uint16_t *src16 = (const uint16_t*)src;
            double linesum = 0;

            for (xi = 0; xi < w; xi ++) {
                linesum += src16[xi + yi * linesize];
                geq->pixel_sums[plane][xi + yi * w] = linesum;
            }
        } else {
            double linesum = 0;

            for (xi = 0; xi < w; xi ++) {
                linesum += src[xi + yi * linesize];
                geq->pixel_sums[plane][xi + yi * w] = linesum;
            }
        }
        if (yi)
            for (xi = 0; xi < w; xi ++) {
                geq->pixel_sums[plane][xi + yi * w] += geq->pixel_sums[plane][xi + yi * w - w];
            }
    }
    return 0;
}

static inline double getpix_integrate_internal(GEQContext *geq, int x, int y, int plane, int w, int h)
{
    if (x > w - 1) {
        double boundary =   getpix_integrate_internal(geq, w - 1, y, plane, w, h);
        return 2*boundary - getpix_integrate_internal(geq, 2*(w - 1) - x, y, plane, w, h);
    } else if (y > h - 1) {
        double boundary =   getpix_integrate_internal(geq, x, h - 1, plane, w, h);
        return 2*boundary - getpix_integrate_internal(geq, x, 2*(h - 1) - y, plane, w, h);
    } else if (x < 0) {
        if (x == -1) return 0;
        return - getpix_integrate_internal(geq, -x-2, y, plane, w, h);
    } else if (y < 0) {
        if (y == -1) return 0;
        return - getpix_integrate_internal(geq, x, -y-2, plane, w, h);
    }

    return geq->pixel_sums[plane][x + y * w];
}

static inline double getpix_integrate(void *priv, double x, double y, int plane) {
    GEQContext *geq = priv;
    AVFrame *picref = geq->picref;
    const uint8_t *src = picref->data[plane];
    const int w = (plane == 1 || plane == 2) ? AV_CEIL_RSHIFT(picref->width,  geq->hsub) : picref->width;
    const int h = (plane == 1 || plane == 2) ? AV_CEIL_RSHIFT(picref->height, geq->vsub) : picref->height;

    if (!src)
        return 0;

    return getpix_integrate_internal(geq, lrint(av_clipd(x, -w, 2*w)), lrint(av_clipd(y, -h, 2*h)), plane, w, h);
}

//TODO: cubic interpolate
//TODO: keep the last few frames
static double lum(void *priv, double x, double y) { return getpix(priv, x, y, 0); }
static double  cb(void *priv, double x, double y) { return getpix(priv, x, y, 1); }
static double  cr(void *priv, double x, double y) { return getpix(priv, x, y, 2); }
static double alpha(void *priv, double x, double y) { return getpix(priv, x, y, 3); }

static double   lumsum(void *priv, double x, double y) { return getpix_integrate(priv, x, y, 0); }
static double    cbsum(void *priv, double x, double y) { return getpix_integrate(priv, x, y, 1); }
static double    crsub(void *priv, double x, double y) { return getpix_integrate(priv, x, y, 2); }
static double alphasum(void *priv, double x, double y) { return getpix_integrate(priv, x, y, 3); }

static av_cold int geq_init(AVFilterContext *ctx)
{
    GEQContext *geq = ctx->priv;
    int plane, ret = 0;

    if (!geq->expr_str[Y] && !geq->expr_str[G] && !geq->expr_str[B] && !geq->expr_str[R]) {
        av_log(ctx, AV_LOG_ERROR, "A luminance or RGB expression is mandatory\n");
        ret = AVERROR(EINVAL);
        goto end;
    }
    geq->is_rgb = !geq->expr_str[Y];

    if ((geq->expr_str[Y] || geq->expr_str[U] || geq->expr_str[V]) && (geq->expr_str[G] || geq->expr_str[B] || geq->expr_str[R])) {
        av_log(ctx, AV_LOG_ERROR, "Either YCbCr or RGB but not both must be specified\n");
        ret = AVERROR(EINVAL);
        goto end;
    }

    if (!geq->expr_str[U] && !geq->expr_str[V]) {
        /* No chroma at all: fallback on luma */
        geq->expr_str[U] = av_strdup(geq->expr_str[Y]);
        geq->expr_str[V] = av_strdup(geq->expr_str[Y]);
    } else {
        /* One chroma unspecified, fallback on the other */
        if (!geq->expr_str[U]) geq->expr_str[U] = av_strdup(geq->expr_str[V]);
        if (!geq->expr_str[V]) geq->expr_str[V] = av_strdup(geq->expr_str[U]);
    }

    if (!geq->expr_str[A]) {
        geq->expr_str[A] = av_asprintf("%d", (1<<geq->bps) - 1);
    }
    if (!geq->expr_str[G])
        geq->expr_str[G] = av_strdup("g(X,Y)");
    if (!geq->expr_str[B])
        geq->expr_str[B] = av_strdup("b(X,Y)");
    if (!geq->expr_str[R])
        geq->expr_str[R] = av_strdup("r(X,Y)");

    if (geq->is_rgb ?
            (!geq->expr_str[G] || !geq->expr_str[B] || !geq->expr_str[R])
                    :
            (!geq->expr_str[U] || !geq->expr_str[V] || !geq->expr_str[A])) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    for (plane = 0; plane < NB_PLANES; plane++) {
        static double (*const p[])(void *, double, double) = {
            lum   , cb   , cr   , alpha   ,
            lumsum, cbsum, crsub, alphasum,
        };
        static const char *const func2_yuv_names[]    = {
            "lum"   , "cb"   , "cr"   , "alpha"   , "p",
            "lumsum", "cbsum", "crsum", "alphasum", "psum",
            NULL };
        static const char *const func2_rgb_names[]    = {
            "g"   , "b"   , "r"   , "alpha"   , "p",
            "gsum", "bsum", "rsum", "alphasum", "psum",
            NULL };
        const char *const *func2_names       = geq->is_rgb ? func2_rgb_names : func2_yuv_names;
        double (*const func2[])(void *, double, double) = {
            lum   , cb   , cr   , alpha   , p[plane],
            lumsum, cbsum, crsub, alphasum, p[plane + 4],
            NULL };
        int counter[10] = {0};

        for (int i = 0; i < MAX_NB_THREADS; i++) {
            ret = av_expr_parse(&geq->e[plane][i], geq->expr_str[plane < 3 && geq->is_rgb ? plane+4 : plane], var_names,
                                NULL, NULL, func2_names, func2, 0, ctx);
            if (ret < 0)
                goto end;
        }

        av_expr_count_func(geq->e[plane][0], counter, FF_ARRAY_ELEMS(counter), 2);
        geq->needs_sum[plane] = counter[5] + counter[6] + counter[7] + counter[8] + counter[9];
    }

end:
    return ret;
}

static int geq_query_formats(AVFilterContext *ctx)
{
    GEQContext *geq = ctx->priv;
    static const enum AVPixelFormat yuv_pix_fmts[] = {
        AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV410P,  AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_YUV444P9,  AV_PIX_FMT_YUV422P9,  AV_PIX_FMT_YUV420P9,
        AV_PIX_FMT_YUVA444P9, AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA420P9,
        AV_PIX_FMT_YUV444P10,  AV_PIX_FMT_YUV422P10,  AV_PIX_FMT_YUV420P10,
        AV_PIX_FMT_YUV440P10,
        AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA420P10,
        AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10,
        AV_PIX_FMT_YUV444P12,  AV_PIX_FMT_YUV422P12,  AV_PIX_FMT_YUV420P12,
        AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14,
        AV_PIX_FMT_YUV444P14,  AV_PIX_FMT_YUV422P14,  AV_PIX_FMT_YUV420P14,
        AV_PIX_FMT_YUV444P16,  AV_PIX_FMT_YUV422P16,  AV_PIX_FMT_YUV420P16,
        AV_PIX_FMT_YUVA444P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA420P16,
        AV_PIX_FMT_GRAY16,
        AV_PIX_FMT_NONE
    };
    static const enum AVPixelFormat rgb_pix_fmts[] = {
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP,
        AV_PIX_FMT_GBRP9,
        AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRAP10,
        AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRAP12,
        AV_PIX_FMT_GBRP14,
        AV_PIX_FMT_GBRP16, AV_PIX_FMT_GBRAP16,
        AV_PIX_FMT_NONE
    };
    const enum AVPixelFormat *pix_fmts = geq->is_rgb ? rgb_pix_fmts : yuv_pix_fmts;

    return ff_set_common_formats_from_list(ctx, pix_fmts);
}

static int geq_config_props(AVFilterLink *inlink)
{
    GEQContext *geq = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    av_assert0(desc);

    geq->hsub = desc->log2_chroma_w;
    geq->vsub = desc->log2_chroma_h;
    geq->bps = desc->comp[0].depth;
    geq->planes = desc->nb_components;
    return 0;
}

typedef struct ThreadData {
    int height;
    int width;
    int plane;
    int linesize;
} ThreadData;

static int slice_geq_filter(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    GEQContext *geq = ctx->priv;
    ThreadData *td = arg;
    const int height = td->height;
    const int width = td->width;
    const int plane = td->plane;
    const int linesize = td->linesize;
    const int slice_start = (height *  jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr+1)) / nb_jobs;
    int x, y;

    double values[VAR_VARS_NB];
    values[VAR_W] = geq->values[VAR_W];
    values[VAR_H] = geq->values[VAR_H];
    values[VAR_N] = geq->values[VAR_N];
    values[VAR_SW] = geq->values[VAR_SW];
    values[VAR_SH] = geq->values[VAR_SH];
    values[VAR_T] = geq->values[VAR_T];

    if (geq->bps == 8) {
        uint8_t *ptr = geq->dst + linesize * slice_start;
        for (y = slice_start; y < slice_end; y++) {
            values[VAR_Y] = y;

            for (x = 0; x < width; x++) {
                values[VAR_X] = x;
                ptr[x] = av_expr_eval(geq->e[plane][jobnr], values, geq);
            }
            ptr += linesize;
        }
    } else {
        uint16_t *ptr16 = geq->dst16 + (linesize/2) * slice_start;
        for (y = slice_start; y < slice_end; y++) {
            values[VAR_Y] = y;
            for (x = 0; x < width; x++) {
                values[VAR_X] = x;
                ptr16[x] = av_expr_eval(geq->e[plane][jobnr], values, geq);
            }
            ptr16 += linesize/2;
        }
    }

    return 0;
}

static int geq_filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    int plane;
    AVFilterContext *ctx = inlink->dst;
    const int nb_threads = FFMIN(MAX_NB_THREADS, ff_filter_get_nb_threads(ctx));
    GEQContext *geq = ctx->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFrame *out;

    geq->values[VAR_N] = inlink->frame_count_out,
    geq->values[VAR_T] = in->pts == AV_NOPTS_VALUE ? NAN : in->pts * av_q2d(inlink->time_base),

    geq->picref = in;
    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    for (plane = 0; plane < geq->planes && out->data[plane]; plane++) {
        const int width = (plane == 1 || plane == 2) ? AV_CEIL_RSHIFT(inlink->w, geq->hsub) : inlink->w;
        const int height = (plane == 1 || plane == 2) ? AV_CEIL_RSHIFT(inlink->h, geq->vsub) : inlink->h;
        const int linesize = out->linesize[plane];
        ThreadData td;

        geq->dst = out->data[plane];
        geq->dst16 = (uint16_t*)out->data[plane];

        geq->values[VAR_W]  = width;
        geq->values[VAR_H]  = height;
        geq->values[VAR_SW] = width / (double)inlink->w;
        geq->values[VAR_SH] = height / (double)inlink->h;

        td.width = width;
        td.height = height;
        td.plane = plane;
        td.linesize = linesize;

        if (geq->needs_sum[plane])
            calculate_sums(geq, plane, width, height);

        ff_filter_execute(ctx, slice_geq_filter, &td,
                          NULL, FFMIN(height, nb_threads));
    }

    av_frame_free(&geq->picref);
    return ff_filter_frame(outlink, out);
}

static av_cold void geq_uninit(AVFilterContext *ctx)
{
    int i;
    GEQContext *geq = ctx->priv;

    for (i = 0; i < NB_PLANES; i++)
        for (int j = 0; j < MAX_NB_THREADS; j++)
            av_expr_free(geq->e[i][j]);
    for (i = 0; i < NB_PLANES; i++)
        av_freep(&geq->pixel_sums);
}

static const AVFilterPad geq_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = geq_config_props,
        .filter_frame = geq_filter_frame,
    },
};

static const AVFilterPad geq_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

const AVFilter ff_vf_geq = {
    .name          = "geq",
    .description   = NULL_IF_CONFIG_SMALL("Apply generic equation to each pixel."),
    .priv_size     = sizeof(GEQContext),
    .init          = geq_init,
    .uninit        = geq_uninit,
    FILTER_INPUTS(geq_inputs),
    FILTER_OUTPUTS(geq_outputs),
    FILTER_QUERY_FUNC(geq_query_formats),
    .priv_class    = &geq_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};
