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

#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct SwapRectContext {
    const AVClass *class;
    char *w, *h;
    char *x1, *y1;
    char *x2, *y2;

    int nb_planes;
    int pixsteps[4];

    const AVPixFmtDescriptor *desc;
    uint8_t *temp;
} SwapRectContext;

#define OFFSET(x) offsetof(SwapRectContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM
static const AVOption swaprect_options[] = {
    { "w",  "set rect width",                     OFFSET(w),  AV_OPT_TYPE_STRING, {.str="w/2"}, 0, 0, .flags = FLAGS },
    { "h",  "set rect height",                    OFFSET(h),  AV_OPT_TYPE_STRING, {.str="h/2"}, 0, 0, .flags = FLAGS },
    { "x1", "set 1st rect x top left coordinate", OFFSET(x1), AV_OPT_TYPE_STRING, {.str="w/2"}, 0, 0, .flags = FLAGS },
    { "y1", "set 1st rect y top left coordinate", OFFSET(y1), AV_OPT_TYPE_STRING, {.str="h/2"}, 0, 0, .flags = FLAGS },
    { "x2", "set 2nd rect x top left coordinate", OFFSET(x2), AV_OPT_TYPE_STRING, {.str="0"},   0, 0, .flags = FLAGS },
    { "y2", "set 2nd rect y top left coordinate", OFFSET(y2), AV_OPT_TYPE_STRING, {.str="0"},   0, 0, .flags = FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(swaprect);

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *pix_fmts = NULL;
    int fmt, ret;

    for (fmt = 0; av_pix_fmt_desc_get(fmt); fmt++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
        if (!(desc->flags & AV_PIX_FMT_FLAG_PAL ||
              desc->flags & AV_PIX_FMT_FLAG_HWACCEL ||
              desc->flags & AV_PIX_FMT_FLAG_BITSTREAM) &&
            (ret = ff_add_format(&pix_fmts, fmt)) < 0)
            return ret;
    }

    return ff_set_common_formats(ctx, pix_fmts);
}

static const char *const var_names[] = {   "w",   "h",   "a",   "n",   "t",   "pos",   "sar",   "dar",        NULL };
enum                                   { VAR_W, VAR_H, VAR_A, VAR_N, VAR_T, VAR_POS, VAR_SAR, VAR_DAR, VAR_VARS_NB };

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    SwapRectContext *s = ctx->priv;
    double var_values[VAR_VARS_NB];
    int x1[4], y1[4];
    int x2[4], y2[4];
    int aw[4], ah[4];
    int lw[4], lh[4];
    int pw[4], ph[4];
    double dw,  dh;
    double dx1, dy1;
    double dx2, dy2;
    int y, p, w, h, ret;

    var_values[VAR_W]   = inlink->w;
    var_values[VAR_H]   = inlink->h;
    var_values[VAR_A]   = (float) inlink->w / inlink->h;
    var_values[VAR_SAR] = inlink->sample_aspect_ratio.num ? av_q2d(inlink->sample_aspect_ratio) : 1;
    var_values[VAR_DAR] = var_values[VAR_A] * var_values[VAR_SAR];
    var_values[VAR_N]   = inlink->frame_count_out;
    var_values[VAR_T]   = in->pts == AV_NOPTS_VALUE ? NAN : in->pts * av_q2d(inlink->time_base);
    var_values[VAR_POS] = in->pkt_pos ? NAN : in->pkt_pos;

    ret = av_expr_parse_and_eval(&dw, s->w,
                                 var_names, &var_values[0],
                                 NULL, NULL, NULL, NULL,
                                 0, 0, ctx);
    if (ret < 0)
        return ret;

    ret = av_expr_parse_and_eval(&dh, s->h,
                                 var_names, &var_values[0],
                                 NULL, NULL, NULL, NULL,
                                 0, 0, ctx);
    if (ret < 0)
        return ret;

    ret = av_expr_parse_and_eval(&dx1, s->x1,
                                 var_names, &var_values[0],
                                 NULL, NULL, NULL, NULL,
                                 0, 0, ctx);
    if (ret < 0)
        return ret;

    ret = av_expr_parse_and_eval(&dy1, s->y1,
                                 var_names, &var_values[0],
                                 NULL, NULL, NULL, NULL,
                                 0, 0, ctx);
    if (ret < 0)
        return ret;

    ret = av_expr_parse_and_eval(&dx2, s->x2,
                                 var_names, &var_values[0],
                                 NULL, NULL, NULL, NULL,
                                 0, 0, ctx);
    if (ret < 0)
        return ret;

    ret = av_expr_parse_and_eval(&dy2, s->y2,
                                 var_names, &var_values[0],
                                 NULL, NULL, NULL, NULL,
                                 0, 0, ctx);
    if (ret < 0)
        return ret;

    w = dw; h = dh; x1[0] = dx1; y1[0] = dy1; x2[0] = dx2; y2[0] = dy2;

    x1[0] = av_clip(x1[0], 0, inlink->w - 1);
    y1[0] = av_clip(y1[0], 0, inlink->w - 1);

    x2[0] = av_clip(x2[0], 0, inlink->w - 1);
    y2[0] = av_clip(y2[0], 0, inlink->w - 1);

    ah[1] = ah[2] = AV_CEIL_RSHIFT(h, s->desc->log2_chroma_h);
    ah[0] = ah[3] = h;
    aw[1] = aw[2] = AV_CEIL_RSHIFT(w, s->desc->log2_chroma_w);
    aw[0] = aw[3] = w;

    w = FFMIN3(w, inlink->w - x1[0], inlink->w - x2[0]);
    h = FFMIN3(h, inlink->h - y1[0], inlink->h - y2[0]);

    ph[1] = ph[2] = AV_CEIL_RSHIFT(h, s->desc->log2_chroma_h);
    ph[0] = ph[3] = h;
    pw[1] = pw[2] = AV_CEIL_RSHIFT(w, s->desc->log2_chroma_w);
    pw[0] = pw[3] = w;

    lh[1] = lh[2] = AV_CEIL_RSHIFT(inlink->h, s->desc->log2_chroma_h);
    lh[0] = lh[3] = inlink->h;
    lw[1] = lw[2] = AV_CEIL_RSHIFT(inlink->w, s->desc->log2_chroma_w);
    lw[0] = lw[3] = inlink->w;

    x1[1] = x1[2] = AV_CEIL_RSHIFT(x1[0], s->desc->log2_chroma_w);
    x1[0] = x1[3] = x1[0];
    y1[1] = y1[2] = AV_CEIL_RSHIFT(y1[0], s->desc->log2_chroma_h);
    y1[0] = y1[3] = y1[0];

    x2[1] = x2[2] = AV_CEIL_RSHIFT(x2[0], s->desc->log2_chroma_w);
    x2[0] = x2[3] = x2[0];
    y2[1] = y2[2] = AV_CEIL_RSHIFT(y2[0], s->desc->log2_chroma_h);
    y2[0] = y2[3] = y2[0];

    for (p = 0; p < s->nb_planes; p++) {
        if (ph[p] == ah[p] && pw[p] == aw[p]) {
            uint8_t *src = in->data[p] + y1[p] * in->linesize[p] + x1[p] * s->pixsteps[p];
            uint8_t *dst = in->data[p] + y2[p] * in->linesize[p] + x2[p] * s->pixsteps[p];

            for (y = 0; y < ph[p]; y++) {
                memcpy(s->temp, src, pw[p] * s->pixsteps[p]);
                memmove(src, dst, pw[p] * s->pixsteps[p]);
                memcpy(dst, s->temp, pw[p] * s->pixsteps[p]);
                src += in->linesize[p];
                dst += in->linesize[p];
            }
        }
    }

    return ff_filter_frame(outlink, in);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    SwapRectContext *s = ctx->priv;

    if (!s->w  || !s->h  ||
        !s->x1 || !s->y1 ||
        !s->x2 || !s->y2)
        return AVERROR(EINVAL);

    s->desc = av_pix_fmt_desc_get(inlink->format);
    av_image_fill_max_pixsteps(s->pixsteps, NULL, s->desc);
    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    s->temp = av_malloc_array(inlink->w, s->pixsteps[0]);
    if (!s->temp)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SwapRectContext *s = ctx->priv;
    av_freep(&s->temp);
}

static const AVFilterPad inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .filter_frame   = filter_frame,
        .config_props   = config_input,
        .needs_writable = 1,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_swaprect = {
    .name          = "swaprect",
    .description   = NULL_IF_CONFIG_SMALL("Swap 2 rectangular objects in video."),
    .priv_size     = sizeof(SwapRectContext),
    .priv_class    = &swaprect_class,
    .query_formats = query_formats,
    .uninit        = uninit,
    .inputs        = inputs,
    .outputs       = outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
