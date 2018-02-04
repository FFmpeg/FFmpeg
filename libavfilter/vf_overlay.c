/*
 * Copyright (c) 2010 Stefano Sabatini
 * Copyright (c) 2010 Baptiste Coudurier
 * Copyright (c) 2007 Bobby Bingham
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
 * overlay one video on top of another
 */

#include "avfilter.h"
#include "formats.h"
#include "libavutil/common.h"
#include "libavutil/eval.h"
#include "libavutil/avstring.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/timestamp.h"
#include "internal.h"
#include "drawutils.h"
#include "framesync.h"
#include "video.h"

static const char *const var_names[] = {
    "main_w",    "W", ///< width  of the main    video
    "main_h",    "H", ///< height of the main    video
    "overlay_w", "w", ///< width  of the overlay video
    "overlay_h", "h", ///< height of the overlay video
    "hsub",
    "vsub",
    "x",
    "y",
    "n",            ///< number of frame
    "pos",          ///< position in the file
    "t",            ///< timestamp expressed in seconds
    NULL
};

enum var_name {
    VAR_MAIN_W,    VAR_MW,
    VAR_MAIN_H,    VAR_MH,
    VAR_OVERLAY_W, VAR_OW,
    VAR_OVERLAY_H, VAR_OH,
    VAR_HSUB,
    VAR_VSUB,
    VAR_X,
    VAR_Y,
    VAR_N,
    VAR_POS,
    VAR_T,
    VAR_VARS_NB
};

#define MAIN    0
#define OVERLAY 1

#define R 0
#define G 1
#define B 2
#define A 3

#define Y 0
#define U 1
#define V 2

enum EvalMode {
    EVAL_MODE_INIT,
    EVAL_MODE_FRAME,
    EVAL_MODE_NB
};

enum OverlayFormat {
    OVERLAY_FORMAT_YUV420,
    OVERLAY_FORMAT_YUV422,
    OVERLAY_FORMAT_YUV444,
    OVERLAY_FORMAT_RGB,
    OVERLAY_FORMAT_GBRP,
    OVERLAY_FORMAT_AUTO,
    OVERLAY_FORMAT_NB
};

typedef struct OverlayContext {
    const AVClass *class;
    int x, y;                   ///< position of overlaid picture

    uint8_t main_is_packed_rgb;
    uint8_t main_rgba_map[4];
    uint8_t main_has_alpha;
    uint8_t overlay_is_packed_rgb;
    uint8_t overlay_rgba_map[4];
    uint8_t overlay_has_alpha;
    int format;                 ///< OverlayFormat
    int alpha_format;
    int eval_mode;              ///< EvalMode

    FFFrameSync fs;

    int main_pix_step[4];       ///< steps per pixel for each plane of the main output
    int overlay_pix_step[4];    ///< steps per pixel for each plane of the overlay
    int hsub, vsub;             ///< chroma subsampling values
    const AVPixFmtDescriptor *main_desc; ///< format descriptor for main input

    double var_values[VAR_VARS_NB];
    char *x_expr, *y_expr;

    AVExpr *x_pexpr, *y_pexpr;

    void (*blend_image)(AVFilterContext *ctx, AVFrame *dst, const AVFrame *src, int x, int y);
} OverlayContext;

static av_cold void uninit(AVFilterContext *ctx)
{
    OverlayContext *s = ctx->priv;

    ff_framesync_uninit(&s->fs);
    av_expr_free(s->x_pexpr); s->x_pexpr = NULL;
    av_expr_free(s->y_pexpr); s->y_pexpr = NULL;
}

static inline int normalize_xy(double d, int chroma_sub)
{
    if (isnan(d))
        return INT_MAX;
    return (int)d & ~((1 << chroma_sub) - 1);
}

static void eval_expr(AVFilterContext *ctx)
{
    OverlayContext *s = ctx->priv;

    s->var_values[VAR_X] = av_expr_eval(s->x_pexpr, s->var_values, NULL);
    s->var_values[VAR_Y] = av_expr_eval(s->y_pexpr, s->var_values, NULL);
    /* It is necessary if x is expressed from y  */
    s->var_values[VAR_X] = av_expr_eval(s->x_pexpr, s->var_values, NULL);
    s->x = normalize_xy(s->var_values[VAR_X], s->hsub);
    s->y = normalize_xy(s->var_values[VAR_Y], s->vsub);
}

static int set_expr(AVExpr **pexpr, const char *expr, const char *option, void *log_ctx)
{
    int ret;
    AVExpr *old = NULL;

    if (*pexpr)
        old = *pexpr;
    ret = av_expr_parse(pexpr, expr, var_names,
                        NULL, NULL, NULL, NULL, 0, log_ctx);
    if (ret < 0) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Error when evaluating the expression '%s' for %s\n",
               expr, option);
        *pexpr = old;
        return ret;
    }

    av_expr_free(old);
    return 0;
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    OverlayContext *s = ctx->priv;
    int ret;

    if      (!strcmp(cmd, "x"))
        ret = set_expr(&s->x_pexpr, args, cmd, ctx);
    else if (!strcmp(cmd, "y"))
        ret = set_expr(&s->y_pexpr, args, cmd, ctx);
    else
        ret = AVERROR(ENOSYS);

    if (ret < 0)
        return ret;

    if (s->eval_mode == EVAL_MODE_INIT) {
        eval_expr(ctx);
        av_log(ctx, AV_LOG_VERBOSE, "x:%f xi:%d y:%f yi:%d\n",
               s->var_values[VAR_X], s->x,
               s->var_values[VAR_Y], s->y);
    }
    return ret;
}

static const enum AVPixelFormat alpha_pix_fmts[] = {
    AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_ARGB, AV_PIX_FMT_ABGR, AV_PIX_FMT_RGBA,
    AV_PIX_FMT_BGRA, AV_PIX_FMT_GBRAP, AV_PIX_FMT_NONE
};

static int query_formats(AVFilterContext *ctx)
{
    OverlayContext *s = ctx->priv;

    /* overlay formats contains alpha, for avoiding conversion with alpha information loss */
    static const enum AVPixelFormat main_pix_fmts_yuv420[] = {
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_NV12, AV_PIX_FMT_NV21,
        AV_PIX_FMT_NONE
    };
    static const enum AVPixelFormat overlay_pix_fmts_yuv420[] = {
        AV_PIX_FMT_YUVA420P, AV_PIX_FMT_NONE
    };

    static const enum AVPixelFormat main_pix_fmts_yuv422[] = {
        AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_NONE
    };
    static const enum AVPixelFormat overlay_pix_fmts_yuv422[] = {
        AV_PIX_FMT_YUVA422P, AV_PIX_FMT_NONE
    };

    static const enum AVPixelFormat main_pix_fmts_yuv444[] = {
        AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVA444P, AV_PIX_FMT_NONE
    };
    static const enum AVPixelFormat overlay_pix_fmts_yuv444[] = {
        AV_PIX_FMT_YUVA444P, AV_PIX_FMT_NONE
    };

    static const enum AVPixelFormat main_pix_fmts_gbrp[] = {
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP, AV_PIX_FMT_NONE
    };
    static const enum AVPixelFormat overlay_pix_fmts_gbrp[] = {
        AV_PIX_FMT_GBRAP, AV_PIX_FMT_NONE
    };

    static const enum AVPixelFormat main_pix_fmts_rgb[] = {
        AV_PIX_FMT_ARGB,  AV_PIX_FMT_RGBA,
        AV_PIX_FMT_ABGR,  AV_PIX_FMT_BGRA,
        AV_PIX_FMT_RGB24, AV_PIX_FMT_BGR24,
        AV_PIX_FMT_NONE
    };
    static const enum AVPixelFormat overlay_pix_fmts_rgb[] = {
        AV_PIX_FMT_ARGB,  AV_PIX_FMT_RGBA,
        AV_PIX_FMT_ABGR,  AV_PIX_FMT_BGRA,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *main_formats = NULL;
    AVFilterFormats *overlay_formats = NULL;
    int ret;

    switch (s->format) {
    case OVERLAY_FORMAT_YUV420:
        if (!(main_formats    = ff_make_format_list(main_pix_fmts_yuv420)) ||
            !(overlay_formats = ff_make_format_list(overlay_pix_fmts_yuv420))) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
        break;
    case OVERLAY_FORMAT_YUV422:
        if (!(main_formats    = ff_make_format_list(main_pix_fmts_yuv422)) ||
            !(overlay_formats = ff_make_format_list(overlay_pix_fmts_yuv422))) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
        break;
    case OVERLAY_FORMAT_YUV444:
        if (!(main_formats    = ff_make_format_list(main_pix_fmts_yuv444)) ||
            !(overlay_formats = ff_make_format_list(overlay_pix_fmts_yuv444))) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
        break;
    case OVERLAY_FORMAT_RGB:
        if (!(main_formats    = ff_make_format_list(main_pix_fmts_rgb)) ||
            !(overlay_formats = ff_make_format_list(overlay_pix_fmts_rgb))) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
        break;
    case OVERLAY_FORMAT_GBRP:
        if (!(main_formats    = ff_make_format_list(main_pix_fmts_gbrp)) ||
            !(overlay_formats = ff_make_format_list(overlay_pix_fmts_gbrp))) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
        break;
    case OVERLAY_FORMAT_AUTO:
        if (!(main_formats    = ff_make_format_list(alpha_pix_fmts))) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
        break;
    default:
        av_assert0(0);
    }

    if (s->format == OVERLAY_FORMAT_AUTO) {
        ret = ff_set_common_formats(ctx, main_formats);
        if (ret < 0)
            goto fail;
    } else {
        if ((ret = ff_formats_ref(main_formats   , &ctx->inputs[MAIN]->out_formats   )) < 0 ||
            (ret = ff_formats_ref(overlay_formats, &ctx->inputs[OVERLAY]->out_formats)) < 0 ||
            (ret = ff_formats_ref(main_formats   , &ctx->outputs[MAIN]->in_formats   )) < 0)
                goto fail;
    }

    return 0;
fail:
    if (main_formats)
        av_freep(&main_formats->formats);
    av_freep(&main_formats);
    if (overlay_formats)
        av_freep(&overlay_formats->formats);
    av_freep(&overlay_formats);
    return ret;
}

static int config_input_overlay(AVFilterLink *inlink)
{
    AVFilterContext *ctx  = inlink->dst;
    OverlayContext  *s = inlink->dst->priv;
    int ret;
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(inlink->format);

    av_image_fill_max_pixsteps(s->overlay_pix_step, NULL, pix_desc);

    /* Finish the configuration by evaluating the expressions
       now when both inputs are configured. */
    s->var_values[VAR_MAIN_W   ] = s->var_values[VAR_MW] = ctx->inputs[MAIN   ]->w;
    s->var_values[VAR_MAIN_H   ] = s->var_values[VAR_MH] = ctx->inputs[MAIN   ]->h;
    s->var_values[VAR_OVERLAY_W] = s->var_values[VAR_OW] = ctx->inputs[OVERLAY]->w;
    s->var_values[VAR_OVERLAY_H] = s->var_values[VAR_OH] = ctx->inputs[OVERLAY]->h;
    s->var_values[VAR_HSUB]  = 1<<pix_desc->log2_chroma_w;
    s->var_values[VAR_VSUB]  = 1<<pix_desc->log2_chroma_h;
    s->var_values[VAR_X]     = NAN;
    s->var_values[VAR_Y]     = NAN;
    s->var_values[VAR_N]     = 0;
    s->var_values[VAR_T]     = NAN;
    s->var_values[VAR_POS]   = NAN;

    if ((ret = set_expr(&s->x_pexpr,      s->x_expr,      "x",      ctx)) < 0 ||
        (ret = set_expr(&s->y_pexpr,      s->y_expr,      "y",      ctx)) < 0)
        return ret;

    s->overlay_is_packed_rgb =
        ff_fill_rgba_map(s->overlay_rgba_map, inlink->format) >= 0;
    s->overlay_has_alpha = ff_fmt_is_in(inlink->format, alpha_pix_fmts);

    if (s->eval_mode == EVAL_MODE_INIT) {
        eval_expr(ctx);
        av_log(ctx, AV_LOG_VERBOSE, "x:%f xi:%d y:%f yi:%d\n",
               s->var_values[VAR_X], s->x,
               s->var_values[VAR_Y], s->y);
    }

    av_log(ctx, AV_LOG_VERBOSE,
           "main w:%d h:%d fmt:%s overlay w:%d h:%d fmt:%s\n",
           ctx->inputs[MAIN]->w, ctx->inputs[MAIN]->h,
           av_get_pix_fmt_name(ctx->inputs[MAIN]->format),
           ctx->inputs[OVERLAY]->w, ctx->inputs[OVERLAY]->h,
           av_get_pix_fmt_name(ctx->inputs[OVERLAY]->format));
    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    OverlayContext *s = ctx->priv;
    int ret;

    if ((ret = ff_framesync_init_dualinput(&s->fs, ctx)) < 0)
        return ret;

    outlink->w = ctx->inputs[MAIN]->w;
    outlink->h = ctx->inputs[MAIN]->h;
    outlink->time_base = ctx->inputs[MAIN]->time_base;

    return ff_framesync_configure(&s->fs);
}

// divide by 255 and round to nearest
// apply a fast variant: (X+127)/255 = ((X+127)*257+257)>>16 = ((X+128)*257)>>16
#define FAST_DIV255(x) ((((x) + 128) * 257) >> 16)

// calculate the unpremultiplied alpha, applying the general equation:
// alpha = alpha_overlay / ( (alpha_main + alpha_overlay) - (alpha_main * alpha_overlay) )
// (((x) << 16) - ((x) << 9) + (x)) is a faster version of: 255 * 255 * x
// ((((x) + (y)) << 8) - ((x) + (y)) - (y) * (x)) is a faster version of: 255 * (x + y)
#define UNPREMULTIPLY_ALPHA(x, y) ((((x) << 16) - ((x) << 9) + (x)) / ((((x) + (y)) << 8) - ((x) + (y)) - (y) * (x)))

/**
 * Blend image in src to destination buffer dst at position (x, y).
 */

static av_always_inline void blend_image_packed_rgb(AVFilterContext *ctx,
                                   AVFrame *dst, const AVFrame *src,
                                   int main_has_alpha, int x, int y,
                                   int is_straight)
{
    OverlayContext *s = ctx->priv;
    int i, imax, j, jmax;
    const int src_w = src->width;
    const int src_h = src->height;
    const int dst_w = dst->width;
    const int dst_h = dst->height;
    uint8_t alpha;          ///< the amount of overlay to blend on to main
    const int dr = s->main_rgba_map[R];
    const int dg = s->main_rgba_map[G];
    const int db = s->main_rgba_map[B];
    const int da = s->main_rgba_map[A];
    const int dstep = s->main_pix_step[0];
    const int sr = s->overlay_rgba_map[R];
    const int sg = s->overlay_rgba_map[G];
    const int sb = s->overlay_rgba_map[B];
    const int sa = s->overlay_rgba_map[A];
    const int sstep = s->overlay_pix_step[0];
    uint8_t *S, *sp, *d, *dp;

    i = FFMAX(-y, 0);
    sp = src->data[0] + i     * src->linesize[0];
    dp = dst->data[0] + (y+i) * dst->linesize[0];

    for (imax = FFMIN(-y + dst_h, src_h); i < imax; i++) {
        j = FFMAX(-x, 0);
        S = sp + j     * sstep;
        d = dp + (x+j) * dstep;

        for (jmax = FFMIN(-x + dst_w, src_w); j < jmax; j++) {
            alpha = S[sa];

            // if the main channel has an alpha channel, alpha has to be calculated
            // to create an un-premultiplied (straight) alpha value
            if (main_has_alpha && alpha != 0 && alpha != 255) {
                uint8_t alpha_d = d[da];
                alpha = UNPREMULTIPLY_ALPHA(alpha, alpha_d);
            }

            switch (alpha) {
            case 0:
                break;
            case 255:
                d[dr] = S[sr];
                d[dg] = S[sg];
                d[db] = S[sb];
                break;
            default:
                // main_value = main_value * (1 - alpha) + overlay_value * alpha
                // since alpha is in the range 0-255, the result must divided by 255
                d[dr] = is_straight ? FAST_DIV255(d[dr] * (255 - alpha) + S[sr] * alpha) :
                        FFMIN(FAST_DIV255(d[dr] * (255 - alpha)) + S[sr], 255);
                d[dg] = is_straight ? FAST_DIV255(d[dg] * (255 - alpha) + S[sg] * alpha) :
                        FFMIN(FAST_DIV255(d[dg] * (255 - alpha)) + S[sg], 255);
                d[db] = is_straight ? FAST_DIV255(d[db] * (255 - alpha) + S[sb] * alpha) :
                        FFMIN(FAST_DIV255(d[db] * (255 - alpha)) + S[sb], 255);
            }
            if (main_has_alpha) {
                switch (alpha) {
                case 0:
                    break;
                case 255:
                    d[da] = S[sa];
                    break;
                default:
                    // apply alpha compositing: main_alpha += (1-main_alpha) * overlay_alpha
                    d[da] += FAST_DIV255((255 - d[da]) * S[sa]);
                }
            }
            d += dstep;
            S += sstep;
        }
        dp += dst->linesize[0];
        sp += src->linesize[0];
    }
}

static av_always_inline void blend_plane(AVFilterContext *ctx,
                                         AVFrame *dst, const AVFrame *src,
                                         int src_w, int src_h,
                                         int dst_w, int dst_h,
                                         int i, int hsub, int vsub,
                                         int x, int y,
                                         int main_has_alpha,
                                         int dst_plane,
                                         int dst_offset,
                                         int dst_step,
                                         int straight,
                                         int yuv)
{
    int src_wp = AV_CEIL_RSHIFT(src_w, hsub);
    int src_hp = AV_CEIL_RSHIFT(src_h, vsub);
    int dst_wp = AV_CEIL_RSHIFT(dst_w, hsub);
    int dst_hp = AV_CEIL_RSHIFT(dst_h, vsub);
    int yp = y>>vsub;
    int xp = x>>hsub;
    uint8_t *s, *sp, *d, *dp, *dap, *a, *da, *ap;
    int jmax, j, k, kmax;

    j = FFMAX(-yp, 0);
    sp = src->data[i] + j         * src->linesize[i];
    dp = dst->data[dst_plane]
                      + (yp+j)    * dst->linesize[dst_plane]
                      + dst_offset;
    ap = src->data[3] + (j<<vsub) * src->linesize[3];
    dap = dst->data[3] + ((yp+j) << vsub) * dst->linesize[3];

    for (jmax = FFMIN(-yp + dst_hp, src_hp); j < jmax; j++) {
        k = FFMAX(-xp, 0);
        d = dp + (xp+k) * dst_step;
        s = sp + k;
        a = ap + (k<<hsub);
        da = dap + ((xp+k) << hsub);

        for (kmax = FFMIN(-xp + dst_wp, src_wp); k < kmax; k++) {
            int alpha_v, alpha_h, alpha;

            // average alpha for color components, improve quality
            if (hsub && vsub && j+1 < src_hp && k+1 < src_wp) {
                alpha = (a[0] + a[src->linesize[3]] +
                         a[1] + a[src->linesize[3]+1]) >> 2;
            } else if (hsub || vsub) {
                alpha_h = hsub && k+1 < src_wp ?
                    (a[0] + a[1]) >> 1 : a[0];
                alpha_v = vsub && j+1 < src_hp ?
                    (a[0] + a[src->linesize[3]]) >> 1 : a[0];
                alpha = (alpha_v + alpha_h) >> 1;
            } else
                alpha = a[0];
            // if the main channel has an alpha channel, alpha has to be calculated
            // to create an un-premultiplied (straight) alpha value
            if (main_has_alpha && alpha != 0 && alpha != 255) {
                // average alpha for color components, improve quality
                uint8_t alpha_d;
                if (hsub && vsub && j+1 < src_hp && k+1 < src_wp) {
                    alpha_d = (da[0] + da[dst->linesize[3]] +
                               da[1] + da[dst->linesize[3]+1]) >> 2;
                } else if (hsub || vsub) {
                    alpha_h = hsub && k+1 < src_wp ?
                        (da[0] + da[1]) >> 1 : da[0];
                    alpha_v = vsub && j+1 < src_hp ?
                        (da[0] + da[dst->linesize[3]]) >> 1 : da[0];
                    alpha_d = (alpha_v + alpha_h) >> 1;
                } else
                    alpha_d = da[0];
                alpha = UNPREMULTIPLY_ALPHA(alpha, alpha_d);
            }
            if (straight) {
                *d = FAST_DIV255(*d * (255 - alpha) + *s * alpha);
            } else {
                if (i && yuv)
                    *d = av_clip(FAST_DIV255((*d - 128) * (255 - alpha)) + *s - 128, -128, 128) + 128;
                else
                    *d = FFMIN(FAST_DIV255(*d * (255 - alpha)) + *s, 255);
            }
            s++;
            d += dst_step;
            da += 1 << hsub;
            a += 1 << hsub;
        }
        dp += dst->linesize[dst_plane];
        sp += src->linesize[i];
        ap += (1 << vsub) * src->linesize[3];
        dap += (1 << vsub) * dst->linesize[3];
    }
}

static inline void alpha_composite(const AVFrame *src, const AVFrame *dst,
                                   int src_w, int src_h,
                                   int dst_w, int dst_h,
                                   int x, int y)
{
    uint8_t alpha;          ///< the amount of overlay to blend on to main
    uint8_t *s, *sa, *d, *da;
    int i, imax, j, jmax;

    i = FFMAX(-y, 0);
    sa = src->data[3] + i     * src->linesize[3];
    da = dst->data[3] + (y+i) * dst->linesize[3];

    for (imax = FFMIN(-y + dst_h, src_h); i < imax; i++) {
        j = FFMAX(-x, 0);
        s = sa + j;
        d = da + x+j;

        for (jmax = FFMIN(-x + dst_w, src_w); j < jmax; j++) {
            alpha = *s;
            if (alpha != 0 && alpha != 255) {
                uint8_t alpha_d = *d;
                alpha = UNPREMULTIPLY_ALPHA(alpha, alpha_d);
            }
            switch (alpha) {
            case 0:
                break;
            case 255:
                *d = *s;
                break;
            default:
                // apply alpha compositing: main_alpha += (1-main_alpha) * overlay_alpha
                *d += FAST_DIV255((255 - *d) * *s);
            }
            d += 1;
            s += 1;
        }
        da += dst->linesize[3];
        sa += src->linesize[3];
    }
}

static av_always_inline void blend_image_yuv(AVFilterContext *ctx,
                                             AVFrame *dst, const AVFrame *src,
                                             int hsub, int vsub,
                                             int main_has_alpha,
                                             int x, int y,
                                             int is_straight)
{
    OverlayContext *s = ctx->priv;
    const int src_w = src->width;
    const int src_h = src->height;
    const int dst_w = dst->width;
    const int dst_h = dst->height;

    blend_plane(ctx, dst, src, src_w, src_h, dst_w, dst_h, 0, 0,       0, x, y, main_has_alpha,
                s->main_desc->comp[0].plane, s->main_desc->comp[0].offset, s->main_desc->comp[0].step, is_straight, 1);
    blend_plane(ctx, dst, src, src_w, src_h, dst_w, dst_h, 1, hsub, vsub, x, y, main_has_alpha,
                s->main_desc->comp[1].plane, s->main_desc->comp[1].offset, s->main_desc->comp[1].step, is_straight, 1);
    blend_plane(ctx, dst, src, src_w, src_h, dst_w, dst_h, 2, hsub, vsub, x, y, main_has_alpha,
                s->main_desc->comp[2].plane, s->main_desc->comp[2].offset, s->main_desc->comp[2].step, is_straight, 1);

    if (main_has_alpha)
        alpha_composite(src, dst, src_w, src_h, dst_w, dst_h, x, y);
}

static av_always_inline void blend_image_planar_rgb(AVFilterContext *ctx,
                                                    AVFrame *dst, const AVFrame *src,
                                                    int hsub, int vsub,
                                                    int main_has_alpha,
                                                    int x, int y,
                                                    int is_straight)
{
    OverlayContext *s = ctx->priv;
    const int src_w = src->width;
    const int src_h = src->height;
    const int dst_w = dst->width;
    const int dst_h = dst->height;

    blend_plane(ctx, dst, src, src_w, src_h, dst_w, dst_h, 0, 0,       0, x, y, main_has_alpha,
                s->main_desc->comp[1].plane, s->main_desc->comp[1].offset, s->main_desc->comp[1].step, is_straight, 0);
    blend_plane(ctx, dst, src, src_w, src_h, dst_w, dst_h, 1, hsub, vsub, x, y, main_has_alpha,
                s->main_desc->comp[2].plane, s->main_desc->comp[2].offset, s->main_desc->comp[2].step, is_straight, 0);
    blend_plane(ctx, dst, src, src_w, src_h, dst_w, dst_h, 2, hsub, vsub, x, y, main_has_alpha,
                s->main_desc->comp[0].plane, s->main_desc->comp[0].offset, s->main_desc->comp[0].step, is_straight, 0);

    if (main_has_alpha)
        alpha_composite(src, dst, src_w, src_h, dst_w, dst_h, x, y);
}

static void blend_image_yuv420(AVFilterContext *ctx, AVFrame *dst, const AVFrame *src, int x, int y)
{
    blend_image_yuv(ctx, dst, src, 1, 1, 0, x, y, 1);
}

static void blend_image_yuva420(AVFilterContext *ctx, AVFrame *dst, const AVFrame *src, int x, int y)
{
    blend_image_yuv(ctx, dst, src, 1, 1, 1, x, y, 1);
}

static void blend_image_yuv422(AVFilterContext *ctx, AVFrame *dst, const AVFrame *src, int x, int y)
{
    blend_image_yuv(ctx, dst, src, 1, 0, 0, x, y, 1);
}

static void blend_image_yuva422(AVFilterContext *ctx, AVFrame *dst, const AVFrame *src, int x, int y)
{
    blend_image_yuv(ctx, dst, src, 1, 0, 1, x, y, 1);
}

static void blend_image_yuv444(AVFilterContext *ctx, AVFrame *dst, const AVFrame *src, int x, int y)
{
    blend_image_yuv(ctx, dst, src, 0, 0, 0, x, y, 1);
}

static void blend_image_yuva444(AVFilterContext *ctx, AVFrame *dst, const AVFrame *src, int x, int y)
{
    blend_image_yuv(ctx, dst, src, 0, 0, 1, x, y, 1);
}

static void blend_image_gbrp(AVFilterContext *ctx, AVFrame *dst, const AVFrame *src, int x, int y)
{
    blend_image_planar_rgb(ctx, dst, src, 0, 0, 0, x, y, 1);
}

static void blend_image_gbrap(AVFilterContext *ctx, AVFrame *dst, const AVFrame *src, int x, int y)
{
    blend_image_planar_rgb(ctx, dst, src, 0, 0, 1, x, y, 1);
}

static void blend_image_yuv420_pm(AVFilterContext *ctx, AVFrame *dst, const AVFrame *src, int x, int y)
{
    blend_image_yuv(ctx, dst, src, 1, 1, 0, x, y, 0);
}

static void blend_image_yuva420_pm(AVFilterContext *ctx, AVFrame *dst, const AVFrame *src, int x, int y)
{
    blend_image_yuv(ctx, dst, src, 1, 1, 1, x, y, 0);
}

static void blend_image_yuv422_pm(AVFilterContext *ctx, AVFrame *dst, const AVFrame *src, int x, int y)
{
    blend_image_yuv(ctx, dst, src, 1, 0, 0, x, y, 0);
}

static void blend_image_yuva422_pm(AVFilterContext *ctx, AVFrame *dst, const AVFrame *src, int x, int y)
{
    blend_image_yuv(ctx, dst, src, 1, 0, 1, x, y, 0);
}

static void blend_image_yuv444_pm(AVFilterContext *ctx, AVFrame *dst, const AVFrame *src, int x, int y)
{
    blend_image_yuv(ctx, dst, src, 0, 0, 0, x, y, 0);
}

static void blend_image_yuva444_pm(AVFilterContext *ctx, AVFrame *dst, const AVFrame *src, int x, int y)
{
    blend_image_yuv(ctx, dst, src, 0, 0, 1, x, y, 0);
}

static void blend_image_gbrp_pm(AVFilterContext *ctx, AVFrame *dst, const AVFrame *src, int x, int y)
{
    blend_image_planar_rgb(ctx, dst, src, 0, 0, 0, x, y, 0);
}

static void blend_image_gbrap_pm(AVFilterContext *ctx, AVFrame *dst, const AVFrame *src, int x, int y)
{
    blend_image_planar_rgb(ctx, dst, src, 0, 0, 1, x, y, 0);
}

static void blend_image_rgb(AVFilterContext *ctx, AVFrame *dst, const AVFrame *src, int x, int y)
{
    blend_image_packed_rgb(ctx, dst, src, 0, x, y, 1);
}

static void blend_image_rgba(AVFilterContext *ctx, AVFrame *dst, const AVFrame *src, int x, int y)
{
    blend_image_packed_rgb(ctx, dst, src, 1, x, y, 1);
}

static void blend_image_rgb_pm(AVFilterContext *ctx, AVFrame *dst, const AVFrame *src, int x, int y)
{
    blend_image_packed_rgb(ctx, dst, src, 0, x, y, 0);
}

static void blend_image_rgba_pm(AVFilterContext *ctx, AVFrame *dst, const AVFrame *src, int x, int y)
{
    blend_image_packed_rgb(ctx, dst, src, 1, x, y, 0);
}

static int config_input_main(AVFilterLink *inlink)
{
    OverlayContext *s = inlink->dst->priv;
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(inlink->format);

    av_image_fill_max_pixsteps(s->main_pix_step,    NULL, pix_desc);

    s->hsub = pix_desc->log2_chroma_w;
    s->vsub = pix_desc->log2_chroma_h;

    s->main_desc = pix_desc;

    s->main_is_packed_rgb =
        ff_fill_rgba_map(s->main_rgba_map, inlink->format) >= 0;
    s->main_has_alpha = ff_fmt_is_in(inlink->format, alpha_pix_fmts);
    switch (s->format) {
    case OVERLAY_FORMAT_YUV420:
        s->blend_image = s->main_has_alpha ? blend_image_yuva420 : blend_image_yuv420;
        break;
    case OVERLAY_FORMAT_YUV422:
        s->blend_image = s->main_has_alpha ? blend_image_yuva422 : blend_image_yuv422;
        break;
    case OVERLAY_FORMAT_YUV444:
        s->blend_image = s->main_has_alpha ? blend_image_yuva444 : blend_image_yuv444;
        break;
    case OVERLAY_FORMAT_RGB:
        s->blend_image = s->main_has_alpha ? blend_image_rgba : blend_image_rgb;
        break;
    case OVERLAY_FORMAT_GBRP:
        s->blend_image = s->main_has_alpha ? blend_image_gbrap : blend_image_gbrp;
        break;
    case OVERLAY_FORMAT_AUTO:
        switch (inlink->format) {
        case AV_PIX_FMT_YUVA420P:
            s->blend_image = blend_image_yuva420;
            break;
        case AV_PIX_FMT_YUVA422P:
            s->blend_image = blend_image_yuva422;
            break;
        case AV_PIX_FMT_YUVA444P:
            s->blend_image = blend_image_yuva444;
            break;
        case AV_PIX_FMT_ARGB:
        case AV_PIX_FMT_RGBA:
        case AV_PIX_FMT_BGRA:
        case AV_PIX_FMT_ABGR:
            s->blend_image = blend_image_rgba;
            break;
        case AV_PIX_FMT_GBRAP:
            s->blend_image = blend_image_gbrap;
            break;
        default:
            av_assert0(0);
            break;
        }
        break;
    }

    if (!s->alpha_format)
        return 0;

    switch (s->format) {
    case OVERLAY_FORMAT_YUV420:
        s->blend_image = s->main_has_alpha ? blend_image_yuva420_pm : blend_image_yuv420_pm;
        break;
    case OVERLAY_FORMAT_YUV422:
        s->blend_image = s->main_has_alpha ? blend_image_yuva422_pm : blend_image_yuv422_pm;
        break;
    case OVERLAY_FORMAT_YUV444:
        s->blend_image = s->main_has_alpha ? blend_image_yuva444_pm : blend_image_yuv444_pm;
        break;
    case OVERLAY_FORMAT_RGB:
        s->blend_image = s->main_has_alpha ? blend_image_rgba_pm : blend_image_rgb_pm;
        break;
    case OVERLAY_FORMAT_GBRP:
        s->blend_image = s->main_has_alpha ? blend_image_gbrap_pm : blend_image_gbrp_pm;
        break;
    case OVERLAY_FORMAT_AUTO:
        switch (inlink->format) {
        case AV_PIX_FMT_YUVA420P:
            s->blend_image = blend_image_yuva420_pm;
            break;
        case AV_PIX_FMT_YUVA422P:
            s->blend_image = blend_image_yuva422_pm;
            break;
        case AV_PIX_FMT_YUVA444P:
            s->blend_image = blend_image_yuva444_pm;
            break;
        case AV_PIX_FMT_ARGB:
        case AV_PIX_FMT_RGBA:
        case AV_PIX_FMT_BGRA:
        case AV_PIX_FMT_ABGR:
            s->blend_image = blend_image_rgba_pm;
            break;
        case AV_PIX_FMT_GBRAP:
            s->blend_image = blend_image_gbrap_pm;
            break;
        default:
            av_assert0(0);
            break;
        }
        break;
    }
    return 0;
}

static int do_blend(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    AVFrame *mainpic, *second;
    OverlayContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    int ret;

    ret = ff_framesync_dualinput_get_writable(fs, &mainpic, &second);
    if (ret < 0)
        return ret;
    if (!second)
        return ff_filter_frame(ctx->outputs[0], mainpic);

    if (s->eval_mode == EVAL_MODE_FRAME) {
        int64_t pos = mainpic->pkt_pos;

        s->var_values[VAR_N] = inlink->frame_count_out;
        s->var_values[VAR_T] = mainpic->pts == AV_NOPTS_VALUE ?
            NAN : mainpic->pts * av_q2d(inlink->time_base);
        s->var_values[VAR_POS] = pos == -1 ? NAN : pos;

        s->var_values[VAR_OVERLAY_W] = s->var_values[VAR_OW] = second->width;
        s->var_values[VAR_OVERLAY_H] = s->var_values[VAR_OH] = second->height;
        s->var_values[VAR_MAIN_W   ] = s->var_values[VAR_MW] = mainpic->width;
        s->var_values[VAR_MAIN_H   ] = s->var_values[VAR_MH] = mainpic->height;

        eval_expr(ctx);
        av_log(ctx, AV_LOG_DEBUG, "n:%f t:%f pos:%f x:%f xi:%d y:%f yi:%d\n",
               s->var_values[VAR_N], s->var_values[VAR_T], s->var_values[VAR_POS],
               s->var_values[VAR_X], s->x,
               s->var_values[VAR_Y], s->y);
    }

    if (s->x < mainpic->width  && s->x + second->width  >= 0 ||
        s->y < mainpic->height && s->y + second->height >= 0)
        s->blend_image(ctx, mainpic, second, s->x, s->y);
    return ff_filter_frame(ctx->outputs[0], mainpic);
}

static av_cold int init(AVFilterContext *ctx)
{
    OverlayContext *s = ctx->priv;

    s->fs.on_event = do_blend;
    return 0;
}

static int activate(AVFilterContext *ctx)
{
    OverlayContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

#define OFFSET(x) offsetof(OverlayContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption overlay_options[] = {
    { "x", "set the x expression", OFFSET(x_expr), AV_OPT_TYPE_STRING, {.str = "0"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "y", "set the y expression", OFFSET(y_expr), AV_OPT_TYPE_STRING, {.str = "0"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "eof_action", "Action to take when encountering EOF from secondary input ",
        OFFSET(fs.opt_eof_action), AV_OPT_TYPE_INT, { .i64 = EOF_ACTION_REPEAT },
        EOF_ACTION_REPEAT, EOF_ACTION_PASS, .flags = FLAGS, "eof_action" },
        { "repeat", "Repeat the previous frame.",   0, AV_OPT_TYPE_CONST, { .i64 = EOF_ACTION_REPEAT }, .flags = FLAGS, "eof_action" },
        { "endall", "End both streams.",            0, AV_OPT_TYPE_CONST, { .i64 = EOF_ACTION_ENDALL }, .flags = FLAGS, "eof_action" },
        { "pass",   "Pass through the main input.", 0, AV_OPT_TYPE_CONST, { .i64 = EOF_ACTION_PASS },   .flags = FLAGS, "eof_action" },
    { "eval", "specify when to evaluate expressions", OFFSET(eval_mode), AV_OPT_TYPE_INT, {.i64 = EVAL_MODE_FRAME}, 0, EVAL_MODE_NB-1, FLAGS, "eval" },
         { "init",  "eval expressions once during initialization", 0, AV_OPT_TYPE_CONST, {.i64=EVAL_MODE_INIT},  .flags = FLAGS, .unit = "eval" },
         { "frame", "eval expressions per-frame",                  0, AV_OPT_TYPE_CONST, {.i64=EVAL_MODE_FRAME}, .flags = FLAGS, .unit = "eval" },
    { "shortest", "force termination when the shortest input terminates", OFFSET(fs.opt_shortest), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
    { "format", "set output format", OFFSET(format), AV_OPT_TYPE_INT, {.i64=OVERLAY_FORMAT_YUV420}, 0, OVERLAY_FORMAT_NB-1, FLAGS, "format" },
        { "yuv420", "", 0, AV_OPT_TYPE_CONST, {.i64=OVERLAY_FORMAT_YUV420}, .flags = FLAGS, .unit = "format" },
        { "yuv422", "", 0, AV_OPT_TYPE_CONST, {.i64=OVERLAY_FORMAT_YUV422}, .flags = FLAGS, .unit = "format" },
        { "yuv444", "", 0, AV_OPT_TYPE_CONST, {.i64=OVERLAY_FORMAT_YUV444}, .flags = FLAGS, .unit = "format" },
        { "rgb",    "", 0, AV_OPT_TYPE_CONST, {.i64=OVERLAY_FORMAT_RGB},    .flags = FLAGS, .unit = "format" },
        { "gbrp",   "", 0, AV_OPT_TYPE_CONST, {.i64=OVERLAY_FORMAT_GBRP},   .flags = FLAGS, .unit = "format" },
        { "auto",   "", 0, AV_OPT_TYPE_CONST, {.i64=OVERLAY_FORMAT_AUTO},   .flags = FLAGS, .unit = "format" },
    { "repeatlast", "repeat overlay of the last overlay frame", OFFSET(fs.opt_repeatlast), AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1, FLAGS },
    { "alpha", "alpha format", OFFSET(alpha_format), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS, "alpha_format" },
        { "straight",      "", 0, AV_OPT_TYPE_CONST, {.i64=0}, .flags = FLAGS, .unit = "alpha_format" },
        { "premultiplied", "", 0, AV_OPT_TYPE_CONST, {.i64=1}, .flags = FLAGS, .unit = "alpha_format" },
    { NULL }
};

FRAMESYNC_DEFINE_CLASS(overlay, OverlayContext, fs);

static const AVFilterPad avfilter_vf_overlay_inputs[] = {
    {
        .name         = "main",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input_main,
    },
    {
        .name         = "overlay",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input_overlay,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_overlay_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
    { NULL }
};

AVFilter ff_vf_overlay = {
    .name          = "overlay",
    .description   = NULL_IF_CONFIG_SMALL("Overlay a video source on top of the input."),
    .preinit       = overlay_framesync_preinit,
    .init          = init,
    .uninit        = uninit,
    .priv_size     = sizeof(OverlayContext),
    .priv_class    = &overlay_class,
    .query_formats = query_formats,
    .activate      = activate,
    .process_command = process_command,
    .inputs        = avfilter_vf_overlay_inputs,
    .outputs       = avfilter_vf_overlay_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
};
