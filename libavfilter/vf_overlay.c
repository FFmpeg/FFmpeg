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
#include "dualinput.h"
#include "drawutils.h"
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

enum EOFAction {
    EOF_ACTION_REPEAT,
    EOF_ACTION_ENDALL,
    EOF_ACTION_PASS
};

static const char * const eof_action_str[] = {
    "repeat", "endall", "pass"
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
    OVERLAY_FORMAT_NB
};

typedef struct OverlayContext {
    const AVClass *class;
    int x, y;                   ///< position of overlayed picture

    int allow_packed_rgb;
    uint8_t main_is_packed_rgb;
    uint8_t main_rgba_map[4];
    uint8_t main_has_alpha;
    uint8_t overlay_is_packed_rgb;
    uint8_t overlay_rgba_map[4];
    uint8_t overlay_has_alpha;
    int format;                 ///< OverlayFormat
    int eval_mode;              ///< EvalMode

    FFDualInputContext dinput;

    int main_pix_step[4];       ///< steps per pixel for each plane of the main output
    int overlay_pix_step[4];    ///< steps per pixel for each plane of the overlay
    int hsub, vsub;             ///< chroma subsampling values

    double var_values[VAR_VARS_NB];
    char *x_expr, *y_expr;

    int eof_action;             ///< action to take on EOF from source

    AVExpr *x_pexpr, *y_pexpr;
} OverlayContext;

static av_cold void uninit(AVFilterContext *ctx)
{
    OverlayContext *s = ctx->priv;

    ff_dualinput_uninit(&s->dinput);
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

static int query_formats(AVFilterContext *ctx)
{
    OverlayContext *s = ctx->priv;

    /* overlay formats contains alpha, for avoiding conversion with alpha information loss */
    static const enum AVPixelFormat main_pix_fmts_yuv420[] = {
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUVA420P, AV_PIX_FMT_NONE
    };
    static const enum AVPixelFormat overlay_pix_fmts_yuv420[] = {
        AV_PIX_FMT_YUVA420P, AV_PIX_FMT_NONE
    };

    static const enum AVPixelFormat main_pix_fmts_yuv422[] = {
        AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_NONE
    };
    static const enum AVPixelFormat overlay_pix_fmts_yuv422[] = {
        AV_PIX_FMT_YUVA422P, AV_PIX_FMT_NONE
    };

    static const enum AVPixelFormat main_pix_fmts_yuv444[] = {
        AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVA444P, AV_PIX_FMT_NONE
    };
    static const enum AVPixelFormat overlay_pix_fmts_yuv444[] = {
        AV_PIX_FMT_YUVA444P, AV_PIX_FMT_NONE
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

    AVFilterFormats *main_formats;
    AVFilterFormats *overlay_formats;

    switch (s->format) {
    case OVERLAY_FORMAT_YUV420:
        main_formats    = ff_make_format_list(main_pix_fmts_yuv420);
        overlay_formats = ff_make_format_list(overlay_pix_fmts_yuv420);
        break;
    case OVERLAY_FORMAT_YUV422:
        main_formats    = ff_make_format_list(main_pix_fmts_yuv422);
        overlay_formats = ff_make_format_list(overlay_pix_fmts_yuv422);
        break;
    case OVERLAY_FORMAT_YUV444:
        main_formats    = ff_make_format_list(main_pix_fmts_yuv444);
        overlay_formats = ff_make_format_list(overlay_pix_fmts_yuv444);
        break;
    case OVERLAY_FORMAT_RGB:
        main_formats    = ff_make_format_list(main_pix_fmts_rgb);
        overlay_formats = ff_make_format_list(overlay_pix_fmts_rgb);
        break;
    default:
        av_assert0(0);
    }

    ff_formats_ref(main_formats,    &ctx->inputs [MAIN   ]->out_formats);
    ff_formats_ref(overlay_formats, &ctx->inputs [OVERLAY]->out_formats);
    ff_formats_ref(main_formats,    &ctx->outputs[MAIN   ]->in_formats );

    return 0;
}

static const enum AVPixelFormat alpha_pix_fmts[] = {
    AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_ARGB, AV_PIX_FMT_ABGR, AV_PIX_FMT_RGBA,
    AV_PIX_FMT_BGRA, AV_PIX_FMT_NONE
};

static int config_input_main(AVFilterLink *inlink)
{
    OverlayContext *s = inlink->dst->priv;
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(inlink->format);

    av_image_fill_max_pixsteps(s->main_pix_step,    NULL, pix_desc);

    s->hsub = pix_desc->log2_chroma_w;
    s->vsub = pix_desc->log2_chroma_h;

    s->main_is_packed_rgb =
        ff_fill_rgba_map(s->main_rgba_map, inlink->format) >= 0;
    s->main_has_alpha = ff_fmt_is_in(inlink->format, alpha_pix_fmts);
    return 0;
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
           "main w:%d h:%d fmt:%s overlay w:%d h:%d fmt:%s eof_action:%s\n",
           ctx->inputs[MAIN]->w, ctx->inputs[MAIN]->h,
           av_get_pix_fmt_name(ctx->inputs[MAIN]->format),
           ctx->inputs[OVERLAY]->w, ctx->inputs[OVERLAY]->h,
           av_get_pix_fmt_name(ctx->inputs[OVERLAY]->format),
           eof_action_str[s->eof_action]);
    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    OverlayContext *s = ctx->priv;
    int ret;

    if ((ret = ff_dualinput_init(ctx, &s->dinput)) < 0)
        return ret;

    outlink->w = ctx->inputs[MAIN]->w;
    outlink->h = ctx->inputs[MAIN]->h;
    outlink->time_base = ctx->inputs[MAIN]->time_base;

    return 0;
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
static void blend_image(AVFilterContext *ctx,
                        AVFrame *dst, const AVFrame *src,
                        int x, int y)
{
    OverlayContext *s = ctx->priv;
    int i, imax, j, jmax, k, kmax;
    const int src_w = src->width;
    const int src_h = src->height;
    const int dst_w = dst->width;
    const int dst_h = dst->height;

    if (x >= dst_w || x+src_w < 0 ||
        y >= dst_h || y+src_h < 0)
        return; /* no intersection */

    if (s->main_is_packed_rgb) {
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
        const int main_has_alpha = s->main_has_alpha;
        uint8_t *s, *sp, *d, *dp;

        i = FFMAX(-y, 0);
        sp = src->data[0] + i     * src->linesize[0];
        dp = dst->data[0] + (y+i) * dst->linesize[0];

        for (imax = FFMIN(-y + dst_h, src_h); i < imax; i++) {
            j = FFMAX(-x, 0);
            s = sp + j     * sstep;
            d = dp + (x+j) * dstep;

            for (jmax = FFMIN(-x + dst_w, src_w); j < jmax; j++) {
                alpha = s[sa];

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
                    d[dr] = s[sr];
                    d[dg] = s[sg];
                    d[db] = s[sb];
                    break;
                default:
                    // main_value = main_value * (1 - alpha) + overlay_value * alpha
                    // since alpha is in the range 0-255, the result must divided by 255
                    d[dr] = FAST_DIV255(d[dr] * (255 - alpha) + s[sr] * alpha);
                    d[dg] = FAST_DIV255(d[dg] * (255 - alpha) + s[sg] * alpha);
                    d[db] = FAST_DIV255(d[db] * (255 - alpha) + s[sb] * alpha);
                }
                if (main_has_alpha) {
                    switch (alpha) {
                    case 0:
                        break;
                    case 255:
                        d[da] = s[sa];
                        break;
                    default:
                        // apply alpha compositing: main_alpha += (1-main_alpha) * overlay_alpha
                        d[da] += FAST_DIV255((255 - d[da]) * s[sa]);
                    }
                }
                d += dstep;
                s += sstep;
            }
            dp += dst->linesize[0];
            sp += src->linesize[0];
        }
    } else {
        const int main_has_alpha = s->main_has_alpha;
        if (main_has_alpha) {
            uint8_t alpha;          ///< the amount of overlay to blend on to main
            uint8_t *s, *sa, *d, *da;

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
        for (i = 0; i < 3; i++) {
            int hsub = i ? s->hsub : 0;
            int vsub = i ? s->vsub : 0;
            int src_wp = FF_CEIL_RSHIFT(src_w, hsub);
            int src_hp = FF_CEIL_RSHIFT(src_h, vsub);
            int dst_wp = FF_CEIL_RSHIFT(dst_w, hsub);
            int dst_hp = FF_CEIL_RSHIFT(dst_h, vsub);
            int yp = y>>vsub;
            int xp = x>>hsub;
            uint8_t *s, *sp, *d, *dp, *a, *ap;

            j = FFMAX(-yp, 0);
            sp = src->data[i] + j         * src->linesize[i];
            dp = dst->data[i] + (yp+j)    * dst->linesize[i];
            ap = src->data[3] + (j<<vsub) * src->linesize[3];

            for (jmax = FFMIN(-yp + dst_hp, src_hp); j < jmax; j++) {
                k = FFMAX(-xp, 0);
                d = dp + xp+k;
                s = sp + k;
                a = ap + (k<<hsub);

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
                            alpha_d = (d[0] + d[src->linesize[3]] +
                                       d[1] + d[src->linesize[3]+1]) >> 2;
                        } else if (hsub || vsub) {
                            alpha_h = hsub && k+1 < src_wp ?
                                (d[0] + d[1]) >> 1 : d[0];
                            alpha_v = vsub && j+1 < src_hp ?
                                (d[0] + d[src->linesize[3]]) >> 1 : d[0];
                            alpha_d = (alpha_v + alpha_h) >> 1;
                        } else
                            alpha_d = d[0];
                        alpha = UNPREMULTIPLY_ALPHA(alpha, alpha_d);
                    }
                    *d = FAST_DIV255(*d * (255 - alpha) + *s * alpha);
                    s++;
                    d++;
                    a += 1 << hsub;
                }
                dp += dst->linesize[i];
                sp += src->linesize[i];
                ap += (1 << vsub) * src->linesize[3];
            }
        }
    }
}

static AVFrame *do_blend(AVFilterContext *ctx, AVFrame *mainpic,
                         const AVFrame *second)
{
    OverlayContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];

    if (s->eval_mode == EVAL_MODE_FRAME) {
        int64_t pos = av_frame_get_pkt_pos(mainpic);

        s->var_values[VAR_N] = inlink->frame_count;
        s->var_values[VAR_T] = mainpic->pts == AV_NOPTS_VALUE ?
            NAN : mainpic->pts * av_q2d(inlink->time_base);
        s->var_values[VAR_POS] = pos == -1 ? NAN : pos;

        eval_expr(ctx);
        av_log(ctx, AV_LOG_DEBUG, "n:%f t:%f pos:%f x:%f xi:%d y:%f yi:%d\n",
               s->var_values[VAR_N], s->var_values[VAR_T], s->var_values[VAR_POS],
               s->var_values[VAR_X], s->x,
               s->var_values[VAR_Y], s->y);
    }

    blend_image(ctx, mainpic, second, s->x, s->y);
    return mainpic;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *inpicref)
{
    OverlayContext *s = inlink->dst->priv;
    av_log(inlink->dst, AV_LOG_DEBUG, "Incoming frame (time:%s) from link #%d\n", av_ts2timestr(inpicref->pts, &inlink->time_base), FF_INLINK_IDX(inlink));
    return ff_dualinput_filter_frame(&s->dinput, inlink, inpicref);
}

static int request_frame(AVFilterLink *outlink)
{
    OverlayContext *s = outlink->src->priv;
    return ff_dualinput_request_frame(&s->dinput, outlink);
}

static av_cold int init(AVFilterContext *ctx)
{
    OverlayContext *s = ctx->priv;

    if (s->allow_packed_rgb) {
        av_log(ctx, AV_LOG_WARNING,
               "The rgb option is deprecated and is overriding the format option, use format instead\n");
        s->format = OVERLAY_FORMAT_RGB;
    }
    if (!s->dinput.repeatlast || s->eof_action == EOF_ACTION_PASS) {
        s->dinput.repeatlast = 0;
        s->eof_action = EOF_ACTION_PASS;
    }
    if (s->dinput.shortest || s->eof_action == EOF_ACTION_ENDALL) {
        s->dinput.shortest = 1;
        s->eof_action = EOF_ACTION_ENDALL;
    }

    s->dinput.process = do_blend;
    return 0;
}

#define OFFSET(x) offsetof(OverlayContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption overlay_options[] = {
    { "x", "set the x expression", OFFSET(x_expr), AV_OPT_TYPE_STRING, {.str = "0"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "y", "set the y expression", OFFSET(y_expr), AV_OPT_TYPE_STRING, {.str = "0"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "eof_action", "Action to take when encountering EOF from secondary input ",
        OFFSET(eof_action), AV_OPT_TYPE_INT, { .i64 = EOF_ACTION_REPEAT },
        EOF_ACTION_REPEAT, EOF_ACTION_PASS, .flags = FLAGS, "eof_action" },
        { "repeat", "Repeat the previous frame.",   0, AV_OPT_TYPE_CONST, { .i64 = EOF_ACTION_REPEAT }, .flags = FLAGS, "eof_action" },
        { "endall", "End both streams.",            0, AV_OPT_TYPE_CONST, { .i64 = EOF_ACTION_ENDALL }, .flags = FLAGS, "eof_action" },
        { "pass",   "Pass through the main input.", 0, AV_OPT_TYPE_CONST, { .i64 = EOF_ACTION_PASS },   .flags = FLAGS, "eof_action" },
    { "eval", "specify when to evaluate expressions", OFFSET(eval_mode), AV_OPT_TYPE_INT, {.i64 = EVAL_MODE_FRAME}, 0, EVAL_MODE_NB-1, FLAGS, "eval" },
         { "init",  "eval expressions once during initialization", 0, AV_OPT_TYPE_CONST, {.i64=EVAL_MODE_INIT},  .flags = FLAGS, .unit = "eval" },
         { "frame", "eval expressions per-frame",                  0, AV_OPT_TYPE_CONST, {.i64=EVAL_MODE_FRAME}, .flags = FLAGS, .unit = "eval" },
    { "rgb", "force packed RGB in input and output (deprecated)", OFFSET(allow_packed_rgb), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS },
    { "shortest", "force termination when the shortest input terminates", OFFSET(dinput.shortest), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, FLAGS },
    { "format", "set output format", OFFSET(format), AV_OPT_TYPE_INT, {.i64=OVERLAY_FORMAT_YUV420}, 0, OVERLAY_FORMAT_NB-1, FLAGS, "format" },
        { "yuv420", "", 0, AV_OPT_TYPE_CONST, {.i64=OVERLAY_FORMAT_YUV420}, .flags = FLAGS, .unit = "format" },
        { "yuv422", "", 0, AV_OPT_TYPE_CONST, {.i64=OVERLAY_FORMAT_YUV422}, .flags = FLAGS, .unit = "format" },
        { "yuv444", "", 0, AV_OPT_TYPE_CONST, {.i64=OVERLAY_FORMAT_YUV444}, .flags = FLAGS, .unit = "format" },
        { "rgb",    "", 0, AV_OPT_TYPE_CONST, {.i64=OVERLAY_FORMAT_RGB},    .flags = FLAGS, .unit = "format" },
    { "repeatlast", "repeat overlay of the last overlay frame", OFFSET(dinput.repeatlast), AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(overlay);

static const AVFilterPad avfilter_vf_overlay_inputs[] = {
    {
        .name         = "main",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input_main,
        .filter_frame = filter_frame,
        .needs_writable = 1,
    },
    {
        .name         = "overlay",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input_overlay,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_overlay_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter ff_vf_overlay = {
    .name          = "overlay",
    .description   = NULL_IF_CONFIG_SMALL("Overlay a video source on top of the input."),
    .init          = init,
    .uninit        = uninit,
    .priv_size     = sizeof(OverlayContext),
    .priv_class    = &overlay_class,
    .query_formats = query_formats,
    .process_command = process_command,
    .inputs        = avfilter_vf_overlay_inputs,
    .outputs       = avfilter_vf_overlay_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
};
