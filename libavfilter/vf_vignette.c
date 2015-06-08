/*
 * Copyright (c) 2013 Clément Bœsch
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

#include <float.h>  /* DBL_MAX */

#include "libavutil/opt.h"
#include "libavutil/eval.h"
#include "libavutil/avassert.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

static const char *const var_names[] = {
    "w",    // stream width
    "h",    // stream height
    "n",    // frame count
    "pts",  // presentation timestamp expressed in AV_TIME_BASE units
    "r",    // frame rate
    "t",    // timestamp expressed in seconds
    "tb",   // timebase
    NULL
};

enum var_name {
    VAR_W,
    VAR_H,
    VAR_N,
    VAR_PTS,
    VAR_R,
    VAR_T,
    VAR_TB,
    VAR_NB
};

enum EvalMode {
    EVAL_MODE_INIT,
    EVAL_MODE_FRAME,
    EVAL_MODE_NB
};

typedef struct {
    const AVClass *class;
    const AVPixFmtDescriptor *desc;
    int backward;
    int eval_mode;                      ///< EvalMode
#define DEF_EXPR_FIELDS(name) AVExpr *name##_pexpr; char *name##_expr; double name
    DEF_EXPR_FIELDS(angle);
    DEF_EXPR_FIELDS(x0);
    DEF_EXPR_FIELDS(y0);
    double var_values[VAR_NB];
    float *fmap;
    int fmap_linesize;
    double dmax;
    float xscale, yscale;
    uint32_t dither;
    int do_dither;
    AVRational aspect;
    AVRational scale;
} VignetteContext;

#define OFFSET(x) offsetof(VignetteContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption vignette_options[] = {
    { "angle", "set lens angle", OFFSET(angle_expr), AV_OPT_TYPE_STRING, {.str="PI/5"}, .flags = FLAGS },
    { "a",     "set lens angle", OFFSET(angle_expr), AV_OPT_TYPE_STRING, {.str="PI/5"}, .flags = FLAGS },
    { "x0", "set circle center position on x-axis", OFFSET(x0_expr), AV_OPT_TYPE_STRING, {.str="w/2"}, .flags = FLAGS },
    { "y0", "set circle center position on y-axis", OFFSET(y0_expr), AV_OPT_TYPE_STRING, {.str="h/2"}, .flags = FLAGS },
    { "mode", "set forward/backward mode", OFFSET(backward), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, FLAGS, "mode" },
        { "forward",  NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0}, INT_MIN, INT_MAX, FLAGS, "mode"},
        { "backward", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 1}, INT_MIN, INT_MAX, FLAGS, "mode"},
    { "eval", "specify when to evaluate expressions", OFFSET(eval_mode), AV_OPT_TYPE_INT, {.i64 = EVAL_MODE_INIT}, 0, EVAL_MODE_NB-1, FLAGS, "eval" },
         { "init",  "eval expressions once during initialization", 0, AV_OPT_TYPE_CONST, {.i64=EVAL_MODE_INIT},  .flags = FLAGS, .unit = "eval" },
         { "frame", "eval expressions for each frame",             0, AV_OPT_TYPE_CONST, {.i64=EVAL_MODE_FRAME}, .flags = FLAGS, .unit = "eval" },
    { "dither", "set dithering", OFFSET(do_dither), AV_OPT_TYPE_INT, {.i64 = 1}, 0, 1, FLAGS },
    { "aspect", "set aspect ratio", OFFSET(aspect), AV_OPT_TYPE_RATIONAL, {.dbl = 1}, 0, DBL_MAX, .flags = FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(vignette);

static av_cold int init(AVFilterContext *ctx)
{
    VignetteContext *s = ctx->priv;

#define PARSE_EXPR(name) do {                                               \
    int ret = av_expr_parse(&s->name##_pexpr,  s->name##_expr, var_names,   \
                            NULL, NULL, NULL, NULL, 0, ctx);                \
    if (ret < 0) {                                                          \
        av_log(ctx, AV_LOG_ERROR, "Unable to parse expression for '"        \
               AV_STRINGIFY(name) "'\n");                                   \
        return ret;                                                         \
    }                                                                       \
} while (0)

    PARSE_EXPR(angle);
    PARSE_EXPR(x0);
    PARSE_EXPR(y0);
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    VignetteContext *s = ctx->priv;
    av_freep(&s->fmap);
    av_expr_free(s->angle_pexpr);
    av_expr_free(s->x0_pexpr);
    av_expr_free(s->y0_pexpr);
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_RGB24,   AV_PIX_FMT_BGR24,
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_NONE
    };
    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static double get_natural_factor(const VignetteContext *s, int x, int y)
{
    const int xx = (x - s->x0) * s->xscale;
    const int yy = (y - s->y0) * s->yscale;
    const double dnorm = hypot(xx, yy) / s->dmax;
    if (dnorm > 1) {
        return 0;
    } else {
        const double c = cos(s->angle * dnorm);
        return (c*c)*(c*c); // do not remove braces, it helps compilers
    }
}

#define TS2D(ts)     ((ts) == AV_NOPTS_VALUE ? NAN : (double)(ts))
#define TS2T(ts, tb) ((ts) == AV_NOPTS_VALUE ? NAN : (double)(ts) * av_q2d(tb))

static void update_context(VignetteContext *s, AVFilterLink *inlink, AVFrame *frame)
{
    int x, y;
    float *dst = s->fmap;
    int dst_linesize = s->fmap_linesize;

    if (frame) {
        s->var_values[VAR_N]   = inlink->frame_count;
        s->var_values[VAR_T]   = TS2T(frame->pts, inlink->time_base);
        s->var_values[VAR_PTS] = TS2D(frame->pts);
    } else {
        s->var_values[VAR_N]   = NAN;
        s->var_values[VAR_T]   = NAN;
        s->var_values[VAR_PTS] = NAN;
    }

    s->angle = av_expr_eval(s->angle_pexpr, s->var_values, NULL);
    s->x0 = av_expr_eval(s->x0_pexpr, s->var_values, NULL);
    s->y0 = av_expr_eval(s->y0_pexpr, s->var_values, NULL);

    if (isnan(s->x0) || isnan(s->y0) || isnan(s->angle))
        s->eval_mode = EVAL_MODE_FRAME;

    s->angle = av_clipf(s->angle, 0, M_PI_2);

    if (s->backward) {
        for (y = 0; y < inlink->h; y++) {
            for (x = 0; x < inlink->w; x++)
                dst[x] = 1. / get_natural_factor(s, x, y);
            dst += dst_linesize;
        }
    } else {
        for (y = 0; y < inlink->h; y++) {
            for (x = 0; x < inlink->w; x++)
                dst[x] = get_natural_factor(s, x, y);
            dst += dst_linesize;
        }
    }
}

static inline double get_dither_value(VignetteContext *s)
{
    double dv = 0;
    if (s->do_dither) {
        dv = s->dither / (double)(1LL<<32);
        s->dither = s->dither * 1664525 + 1013904223;
    }
    return dv;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    unsigned x, y, direct = 0;
    AVFilterContext *ctx = inlink->dst;
    VignetteContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;

    if (av_frame_is_writable(in)) {
        direct = 1;
        out = in;
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }

    if (s->eval_mode == EVAL_MODE_FRAME)
        update_context(s, inlink, in);

    if (s->desc->flags & AV_PIX_FMT_FLAG_RGB) {
        uint8_t       *dst = out->data[0];
        const uint8_t *src = in ->data[0];
        const float *fmap = s->fmap;
        const int dst_linesize = out->linesize[0];
        const int src_linesize = in ->linesize[0];
        const int fmap_linesize = s->fmap_linesize;

        for (y = 0; y < inlink->h; y++) {
            uint8_t       *dstp = dst;
            const uint8_t *srcp = src;

            for (x = 0; x < inlink->w; x++, dstp += 3, srcp += 3) {
                const float f = fmap[x];

                dstp[0] = av_clip_uint8(srcp[0] * f + get_dither_value(s));
                dstp[1] = av_clip_uint8(srcp[1] * f + get_dither_value(s));
                dstp[2] = av_clip_uint8(srcp[2] * f + get_dither_value(s));
            }
            dst += dst_linesize;
            src += src_linesize;
            fmap += fmap_linesize;
        }
    } else {
        int plane;

        for (plane = 0; plane < 4 && in->data[plane] && in->linesize[plane]; plane++) {
            uint8_t       *dst = out->data[plane];
            const uint8_t *src = in ->data[plane];
            const float *fmap = s->fmap;
            const int dst_linesize = out->linesize[plane];
            const int src_linesize = in ->linesize[plane];
            const int fmap_linesize = s->fmap_linesize;
            const int chroma = plane == 1 || plane == 2;
            const int hsub = chroma ? s->desc->log2_chroma_w : 0;
            const int vsub = chroma ? s->desc->log2_chroma_h : 0;
            const int w = FF_CEIL_RSHIFT(inlink->w, hsub);
            const int h = FF_CEIL_RSHIFT(inlink->h, vsub);

            for (y = 0; y < h; y++) {
                uint8_t *dstp = dst;
                const uint8_t *srcp = src;

                for (x = 0; x < w; x++) {
                    const double dv = get_dither_value(s);
                    if (chroma) *dstp++ = av_clip_uint8(fmap[x << hsub] * (*srcp++ - 127) + 127 + dv);
                    else        *dstp++ = av_clip_uint8(fmap[x        ] *  *srcp++              + dv);
                }
                dst += dst_linesize;
                src += src_linesize;
                fmap += fmap_linesize << vsub;
            }
        }
    }

    if (!direct)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int config_props(AVFilterLink *inlink)
{
    VignetteContext *s = inlink->dst->priv;
    AVRational sar = inlink->sample_aspect_ratio;

    s->desc = av_pix_fmt_desc_get(inlink->format);
    s->var_values[VAR_W]  = inlink->w;
    s->var_values[VAR_H]  = inlink->h;
    s->var_values[VAR_TB] = av_q2d(inlink->time_base);
    s->var_values[VAR_R]  = inlink->frame_rate.num == 0 || inlink->frame_rate.den == 0 ?
        NAN : av_q2d(inlink->frame_rate);

    if (!sar.num || !sar.den)
        sar.num = sar.den = 1;
    if (sar.num > sar.den) {
        s->xscale = av_q2d(av_div_q(sar, s->aspect));
        s->yscale = 1;
    } else {
        s->yscale = av_q2d(av_div_q(s->aspect, sar));
        s->xscale = 1;
    }
    s->dmax = hypot(inlink->w / 2., inlink->h / 2.);
    av_log(s, AV_LOG_DEBUG, "xscale=%f yscale=%f dmax=%f\n",
           s->xscale, s->yscale, s->dmax);

    s->fmap_linesize = FFALIGN(inlink->w, 32);
    s->fmap = av_malloc_array(s->fmap_linesize, inlink->h * sizeof(*s->fmap));
    if (!s->fmap)
        return AVERROR(ENOMEM);

    if (s->eval_mode == EVAL_MODE_INIT)
        update_context(s, inlink, NULL);

    return 0;
}

static const AVFilterPad vignette_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_props,
    },
    { NULL }
};

static const AVFilterPad vignette_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_vignette = {
    .name          = "vignette",
    .description   = NULL_IF_CONFIG_SMALL("Make or reverse a vignette effect."),
    .priv_size     = sizeof(VignetteContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = vignette_inputs,
    .outputs       = vignette_outputs,
    .priv_class    = &vignette_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
