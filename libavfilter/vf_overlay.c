/*
 * Copyright (c) 2010 Stefano Sabatini
 * Copyright (c) 2010 Baptiste Coudurier
 * Copyright (c) 2007 Bobby Bingham
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
 * overlay one video on top of another
 */

#include "avfilter.h"
#include "libavutil/eval.h"
#include "libavutil/avstring.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/mathematics.h"
#include "internal.h"

static const char *var_names[] = {
    "E",
    "PHI",
    "PI",
    "main_w",    "W", ///< width  of the main    video
    "main_h",    "H", ///< height of the main    video
    "overlay_w", "w", ///< width  of the overlay video
    "overlay_h", "h", ///< height of the overlay video
    NULL
};

enum var_name {
    VAR_E,
    VAR_PHI,
    VAR_PI,
    VAR_MAIN_W,    VAR_MW,
    VAR_MAIN_H,    VAR_MH,
    VAR_OVERLAY_W, VAR_OW,
    VAR_OVERLAY_H, VAR_OH,
    VAR_VARS_NB
};

#define MAIN    0
#define OVERLAY 1

typedef struct {
    int x, y;                   ///< position of overlayed picture

    AVFilterBufferRef *overpicref;

    int max_plane_step[4];      ///< steps per pixel for each plane
    int hsub, vsub;             ///< chroma subsampling values

    char x_expr[256], y_expr[256];
} OverlayContext;

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    OverlayContext *over = ctx->priv;

    av_strlcpy(over->x_expr, "0", sizeof(over->x_expr));
    av_strlcpy(over->y_expr, "0", sizeof(over->y_expr));

    if (args)
        sscanf(args, "%255[^:]:%255[^:]", over->x_expr, over->y_expr);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    OverlayContext *over = ctx->priv;

    if (over->overpicref)
        avfilter_unref_buffer(over->overpicref);
}

static int query_formats(AVFilterContext *ctx)
{
    const enum PixelFormat inout_pix_fmts[] = { PIX_FMT_YUV420P,  PIX_FMT_NONE };
    const enum PixelFormat blend_pix_fmts[] = { PIX_FMT_YUVA420P, PIX_FMT_NONE };
    AVFilterFormats *inout_formats = avfilter_make_format_list(inout_pix_fmts);
    AVFilterFormats *blend_formats = avfilter_make_format_list(blend_pix_fmts);

    avfilter_formats_ref(inout_formats, &ctx->inputs [MAIN   ]->out_formats);
    avfilter_formats_ref(blend_formats, &ctx->inputs [OVERLAY]->out_formats);
    avfilter_formats_ref(inout_formats, &ctx->outputs[MAIN   ]->in_formats );

    return 0;
}

static int config_input_main(AVFilterLink *inlink)
{
    OverlayContext *over = inlink->dst->priv;
    const AVPixFmtDescriptor *pix_desc = &av_pix_fmt_descriptors[inlink->format];

    av_image_fill_max_pixsteps(over->max_plane_step, NULL, pix_desc);
    over->hsub = pix_desc->log2_chroma_w;
    over->vsub = pix_desc->log2_chroma_h;

    return 0;
}

static int config_input_overlay(AVFilterLink *inlink)
{
    AVFilterContext *ctx  = inlink->dst;
    OverlayContext  *over = inlink->dst->priv;
    char *expr;
    double var_values[VAR_VARS_NB], res;
    int ret;

    /* Finish the configuration by evaluating the expressions
       now when both inputs are configured. */
    var_values[VAR_E  ] = M_E;
    var_values[VAR_PHI] = M_PHI;
    var_values[VAR_PI ] = M_PI;

    var_values[VAR_MAIN_W   ] = var_values[VAR_MW] = ctx->inputs[MAIN   ]->w;
    var_values[VAR_MAIN_H   ] = var_values[VAR_MH] = ctx->inputs[MAIN   ]->h;
    var_values[VAR_OVERLAY_W] = var_values[VAR_OW] = ctx->inputs[OVERLAY]->w;
    var_values[VAR_OVERLAY_H] = var_values[VAR_OH] = ctx->inputs[OVERLAY]->h;

    if ((ret = av_expr_parse_and_eval(&res, (expr = over->x_expr), var_names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        goto fail;
    over->x = res;
    if ((ret = av_expr_parse_and_eval(&res, (expr = over->y_expr), var_names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)))
        goto fail;
    over->y = res;
    /* x may depend on y */
    if ((ret = av_expr_parse_and_eval(&res, (expr = over->x_expr), var_names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        goto fail;
    over->x = res;

    av_log(ctx, AV_LOG_INFO,
           "main w:%d h:%d fmt:%s overlay x:%d y:%d w:%d h:%d fmt:%s\n",
           ctx->inputs[MAIN]->w, ctx->inputs[MAIN]->h,
           av_pix_fmt_descriptors[ctx->inputs[MAIN]->format].name,
           over->x, over->y,
           ctx->inputs[OVERLAY]->w, ctx->inputs[OVERLAY]->h,
           av_pix_fmt_descriptors[ctx->inputs[OVERLAY]->format].name);

    if (over->x < 0 || over->y < 0 ||
        over->x + var_values[VAR_OVERLAY_W] > var_values[VAR_MAIN_W] ||
        over->y + var_values[VAR_OVERLAY_H] > var_values[VAR_MAIN_H]) {
        av_log(ctx, AV_LOG_ERROR,
               "Overlay area (%d,%d)<->(%d,%d) not within the main area (0,0)<->(%d,%d) or zero-sized\n",
               over->x, over->y,
               (int)(over->x + var_values[VAR_OVERLAY_W]),
               (int)(over->y + var_values[VAR_OVERLAY_H]),
               (int)var_values[VAR_MAIN_W], (int)var_values[VAR_MAIN_H]);
        return AVERROR(EINVAL);
    }
    return 0;

fail:
    av_log(NULL, AV_LOG_ERROR,
           "Error when evaluating the expression '%s'\n", expr);
    return ret;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    int exact;
    // common timebase computation:
    AVRational tb1 = ctx->inputs[MAIN   ]->time_base;
    AVRational tb2 = ctx->inputs[OVERLAY]->time_base;
    AVRational *tb = &ctx->outputs[0]->time_base;
    exact = av_reduce(&tb->num, &tb->den,
                      av_gcd((int64_t)tb1.num * tb2.den,
                             (int64_t)tb2.num * tb1.den),
                      (int64_t)tb1.den * tb2.den, INT_MAX);
    av_log(ctx, AV_LOG_INFO,
           "main_tb:%d/%d overlay_tb:%d/%d -> tb:%d/%d exact:%d\n",
           tb1.num, tb1.den, tb2.num, tb2.den, tb->num, tb->den, exact);
    if (!exact)
        av_log(ctx, AV_LOG_WARNING,
               "Timestamp conversion inexact, timestamp information loss may occurr\n");

    outlink->w = ctx->inputs[MAIN]->w;
    outlink->h = ctx->inputs[MAIN]->h;

    return 0;
}

static AVFilterBufferRef *get_video_buffer(AVFilterLink *link, int perms, int w, int h)
{
    return avfilter_get_video_buffer(link->dst->outputs[0], perms, w, h);
}

static void start_frame(AVFilterLink *inlink, AVFilterBufferRef *inpicref)
{
    AVFilterBufferRef *outpicref = avfilter_ref_buffer(inpicref, ~0);
    AVFilterContext *ctx = inlink->dst;
    OverlayContext *over = ctx->priv;

    inlink->dst->outputs[0]->out_buf = outpicref;
    outpicref->pts = av_rescale_q(outpicref->pts, ctx->inputs[MAIN]->time_base,
                                  ctx->outputs[0]->time_base);

    if (!over->overpicref || over->overpicref->pts < outpicref->pts) {
        AVFilterBufferRef *old = over->overpicref;
        over->overpicref = NULL;
        avfilter_request_frame(ctx->inputs[OVERLAY]);
        if (over->overpicref) {
            if (old)
                avfilter_unref_buffer(old);
        } else
            over->overpicref = old;
    }

    avfilter_start_frame(inlink->dst->outputs[0], outpicref);
}

static void start_frame_overlay(AVFilterLink *inlink, AVFilterBufferRef *inpicref)
{
    AVFilterContext *ctx = inlink->dst;
    OverlayContext *over = ctx->priv;

    over->overpicref = inpicref;
    over->overpicref->pts = av_rescale_q(inpicref->pts, ctx->inputs[OVERLAY]->time_base,
                                         ctx->outputs[0]->time_base);
}

static void blend_slice(AVFilterContext *ctx,
                        AVFilterBufferRef *dst, AVFilterBufferRef *src,
                        int x, int y, int w, int h,
                        int slice_y, int slice_w, int slice_h)
{
    OverlayContext *over = ctx->priv;
    int i, j, k;
    int width, height;
    int overlay_end_y = y+h;
    int slice_end_y = slice_y+slice_h;
    int end_y, start_y;

    width = FFMIN(slice_w - x, w);
    end_y = FFMIN(slice_end_y, overlay_end_y);
    start_y = FFMAX(y, slice_y);
    height = end_y - start_y;

    if (dst->format == PIX_FMT_BGR24 || dst->format == PIX_FMT_RGB24) {
        uint8_t *dp = dst->data[0] + x * 3 + start_y * dst->linesize[0];
        uint8_t *sp = src->data[0];
        int b = dst->format == PIX_FMT_BGR24 ? 2 : 0;
        int r = dst->format == PIX_FMT_BGR24 ? 0 : 2;
        if (slice_y > y)
            sp += (slice_y - y) * src->linesize[0];
        for (i = 0; i < height; i++) {
            uint8_t *d = dp, *s = sp;
            for (j = 0; j < width; j++) {
                d[r] = (d[r] * (0xff - s[3]) + s[0] * s[3] + 128) >> 8;
                d[1] = (d[1] * (0xff - s[3]) + s[1] * s[3] + 128) >> 8;
                d[b] = (d[b] * (0xff - s[3]) + s[2] * s[3] + 128) >> 8;
                d += 3;
                s += 4;
            }
            dp += dst->linesize[0];
            sp += src->linesize[0];
        }
    } else {
        for (i = 0; i < 3; i++) {
            int hsub = i ? over->hsub : 0;
            int vsub = i ? over->vsub : 0;
            uint8_t *dp = dst->data[i] + (x >> hsub) +
                (start_y >> vsub) * dst->linesize[i];
            uint8_t *sp = src->data[i];
            uint8_t *ap = src->data[3];
            int wp = FFALIGN(width, 1<<hsub) >> hsub;
            int hp = FFALIGN(height, 1<<vsub) >> vsub;
            if (slice_y > y) {
                sp += ((slice_y - y) >> vsub) * src->linesize[i];
                ap += (slice_y - y) * src->linesize[3];
            }
            for (j = 0; j < hp; j++) {
                uint8_t *d = dp, *s = sp, *a = ap;
                for (k = 0; k < wp; k++) {
                    // average alpha for color components, improve quality
                    int alpha_v, alpha_h, alpha;
                    if (hsub && vsub && j+1 < hp && k+1 < wp) {
                        alpha = (a[0] + a[src->linesize[3]] +
                                 a[1] + a[src->linesize[3]+1]) >> 2;
                    } else if (hsub || vsub) {
                        alpha_h = hsub && k+1 < wp ?
                            (a[0] + a[1]) >> 1 : a[0];
                        alpha_v = vsub && j+1 < hp ?
                            (a[0] + a[src->linesize[3]]) >> 1 : a[0];
                        alpha = (alpha_v + alpha_h) >> 1;
                    } else
                        alpha = a[0];
                    *d = (*d * (0xff - alpha) + *s++ * alpha + 128) >> 8;
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

static void draw_slice(AVFilterLink *inlink, int y, int h, int slice_dir)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFilterBufferRef *outpicref = outlink->out_buf;
    OverlayContext *over = ctx->priv;

    if (over->overpicref &&
        !(over->x >= outpicref->video->w || over->y >= outpicref->video->h ||
          y+h < over->y || y >= over->y + over->overpicref->video->h)) {
        blend_slice(ctx, outpicref, over->overpicref, over->x, over->y,
                    over->overpicref->video->w, over->overpicref->video->h,
                    y, outpicref->video->w, h);
    }
    avfilter_draw_slice(outlink, y, h, slice_dir);
}

static void end_frame(AVFilterLink *inlink)
{
    avfilter_end_frame(inlink->dst->outputs[0]);
    avfilter_unref_buffer(inlink->cur_buf);
}

static void null_draw_slice(AVFilterLink *inlink, int y, int h, int slice_dir) { }

static void null_end_frame(AVFilterLink *inlink) { }

AVFilter avfilter_vf_overlay = {
    .name      = "overlay",
    .description = NULL_IF_CONFIG_SMALL("Overlay a video source on top of the input."),

    .init      = init,
    .uninit    = uninit,

    .priv_size = sizeof(OverlayContext),

    .query_formats = query_formats,

    .inputs    = (AVFilterPad[]) {{ .name            = "main",
                                    .type            = AVMEDIA_TYPE_VIDEO,
                                    .start_frame     = start_frame,
                                    .get_video_buffer= get_video_buffer,
                                    .config_props    = config_input_main,
                                    .draw_slice      = draw_slice,
                                    .end_frame       = end_frame,
                                    .min_perms       = AV_PERM_READ,
                                    .rej_perms       = AV_PERM_REUSE2|AV_PERM_PRESERVE, },
                                  { .name            = "overlay",
                                    .type            = AVMEDIA_TYPE_VIDEO,
                                    .start_frame     = start_frame_overlay,
                                    .config_props    = config_input_overlay,
                                    .draw_slice      = null_draw_slice,
                                    .end_frame       = null_end_frame,
                                    .min_perms       = AV_PERM_READ,
                                    .rej_perms       = AV_PERM_REUSE2, },
                                  { .name = NULL}},
    .outputs   = (AVFilterPad[]) {{ .name            = "default",
                                    .type            = AVMEDIA_TYPE_VIDEO,
                                    .config_props    = config_output, },
                                  { .name = NULL}},
};
