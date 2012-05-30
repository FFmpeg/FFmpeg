/*
 * Copyright (c) 2002 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2011 Stefano Sabatini
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Libav; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * @file
 * Apply a boxblur filter to the input video.
 * Ported from MPlayer libmpcodecs/vf_boxblur.c.
 */

#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "video.h"

static const char *const var_names[] = {
    "w",
    "h",
    "cw",
    "ch",
    "hsub",
    "vsub",
    NULL
};

enum var_name {
    VAR_W,
    VAR_H,
    VAR_CW,
    VAR_CH,
    VAR_HSUB,
    VAR_VSUB,
    VARS_NB
};

typedef struct {
    int radius;
    int power;
} FilterParam;

typedef struct {
    FilterParam luma_param;
    FilterParam chroma_param;
    FilterParam alpha_param;
    char luma_radius_expr  [256];
    char chroma_radius_expr[256];
    char alpha_radius_expr [256];

    int hsub, vsub;
    int radius[4];
    int power[4];
    uint8_t *temp[2]; ///< temporary buffer used in blur_power()
} BoxBlurContext;

#define Y 0
#define U 1
#define V 2
#define A 3

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    BoxBlurContext *boxblur = ctx->priv;
    int e;

    if (!args) {
        av_log(ctx, AV_LOG_ERROR,
               "Filter expects 2 or 4 or 6 arguments, none provided\n");
        return AVERROR(EINVAL);
    }

    e = sscanf(args, "%255[^:]:%d:%255[^:]:%d:%255[^:]:%d",
               boxblur->luma_radius_expr,   &boxblur->luma_param  .power,
               boxblur->chroma_radius_expr, &boxblur->chroma_param.power,
               boxblur->alpha_radius_expr,  &boxblur->alpha_param .power);

    if (e != 2 && e != 4 && e != 6) {
        av_log(ctx, AV_LOG_ERROR,
               "Filter expects 2 or 4 or 6 params, provided %d\n", e);
        return AVERROR(EINVAL);
    }

    if (e < 4) {
        boxblur->chroma_param.power = boxblur->luma_param.power;
        av_strlcpy(boxblur->chroma_radius_expr, boxblur->luma_radius_expr,
                   sizeof(boxblur->chroma_radius_expr));
    }
    if (e < 6) {
        boxblur->alpha_param.power = boxblur->luma_param.power;
        av_strlcpy(boxblur->alpha_radius_expr, boxblur->luma_radius_expr,
                   sizeof(boxblur->alpha_radius_expr));
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    BoxBlurContext *boxblur = ctx->priv;

    av_freep(&boxblur->temp[0]);
    av_freep(&boxblur->temp[1]);
}

static int query_formats(AVFilterContext *ctx)
{
    enum PixelFormat pix_fmts[] = {
        PIX_FMT_YUV444P,  PIX_FMT_YUV422P,  PIX_FMT_YUV420P,
        PIX_FMT_YUV411P,  PIX_FMT_YUV410P,  PIX_FMT_YUVA420P,
        PIX_FMT_YUV440P,  PIX_FMT_GRAY8,
        PIX_FMT_YUVJ444P, PIX_FMT_YUVJ422P, PIX_FMT_YUVJ420P,
        PIX_FMT_YUVJ440P,
        PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    const AVPixFmtDescriptor *desc = &av_pix_fmt_descriptors[inlink->format];
    AVFilterContext    *ctx = inlink->dst;
    BoxBlurContext *boxblur = ctx->priv;
    int w = inlink->w, h = inlink->h;
    int cw, ch;
    double var_values[VARS_NB], res;
    char *expr;
    int ret;

    av_freep(&boxblur->temp[0]);
    av_freep(&boxblur->temp[1]);
    if (!(boxblur->temp[0] = av_malloc(FFMAX(w, h))))
       return AVERROR(ENOMEM);
    if (!(boxblur->temp[1] = av_malloc(FFMAX(w, h)))) {
        av_freep(&boxblur->temp[0]);
        return AVERROR(ENOMEM);
    }

    boxblur->hsub = desc->log2_chroma_w;
    boxblur->vsub = desc->log2_chroma_h;

    var_values[VAR_W]       = inlink->w;
    var_values[VAR_H]       = inlink->h;
    var_values[VAR_CW] = cw = w>>boxblur->hsub;
    var_values[VAR_CH] = ch = h>>boxblur->vsub;
    var_values[VAR_HSUB]    = 1<<boxblur->hsub;
    var_values[VAR_VSUB]    = 1<<boxblur->vsub;

#define EVAL_RADIUS_EXPR(comp)                                          \
    expr = boxblur->comp##_radius_expr;                                 \
    ret = av_expr_parse_and_eval(&res, expr, var_names, var_values,     \
                                 NULL, NULL, NULL, NULL, NULL, 0, ctx); \
    boxblur->comp##_param.radius = res;                                 \
    if (ret < 0) {                                                      \
        av_log(NULL, AV_LOG_ERROR,                                      \
               "Error when evaluating " #comp " radius expression '%s'\n", expr); \
        return ret;                                                     \
    }
    EVAL_RADIUS_EXPR(luma);
    EVAL_RADIUS_EXPR(chroma);
    EVAL_RADIUS_EXPR(alpha);

    av_log(ctx, AV_LOG_DEBUG,
           "luma_radius:%d luma_power:%d "
           "chroma_radius:%d chroma_power:%d "
           "alpha_radius:%d alpha_power:%d "
           "w:%d chroma_w:%d h:%d chroma_h:%d\n",
           boxblur->luma_param  .radius, boxblur->luma_param  .power,
           boxblur->chroma_param.radius, boxblur->chroma_param.power,
           boxblur->alpha_param .radius, boxblur->alpha_param .power,
           w, cw, h, ch);

#define CHECK_RADIUS_VAL(w_, h_, comp)                                  \
    if (boxblur->comp##_param.radius < 0 ||                             \
        2*boxblur->comp##_param.radius > FFMIN(w_, h_)) {               \
        av_log(ctx, AV_LOG_ERROR,                                       \
               "Invalid " #comp " radius value %d, must be >= 0 and <= %d\n", \
               boxblur->comp##_param.radius, FFMIN(w_, h_)/2);          \
        return AVERROR(EINVAL);                                         \
    }
    CHECK_RADIUS_VAL(w,  h,  luma);
    CHECK_RADIUS_VAL(cw, ch, chroma);
    CHECK_RADIUS_VAL(w,  h,  alpha);

    boxblur->radius[Y] = boxblur->luma_param.radius;
    boxblur->radius[U] = boxblur->radius[V] = boxblur->chroma_param.radius;
    boxblur->radius[A] = boxblur->alpha_param.radius;

    boxblur->power[Y] = boxblur->luma_param.power;
    boxblur->power[U] = boxblur->power[V] = boxblur->chroma_param.power;
    boxblur->power[A] = boxblur->alpha_param.power;

    return 0;
}

static inline void blur(uint8_t *dst, int dst_step, const uint8_t *src, int src_step,
                        int len, int radius)
{
    /* Naive boxblur would sum source pixels from x-radius .. x+radius
     * for destination pixel x. That would be O(radius*width).
     * If you now look at what source pixels represent 2 consecutive
     * output pixels, then you see they are almost identical and only
     * differ by 2 pixels, like:
     * src0       111111111
     * dst0           1
     * src1        111111111
     * dst1            1
     * src0-src1  1       -1
     * so when you know one output pixel you can find the next by just adding
     * and subtracting 1 input pixel.
     * The following code adopts this faster variant.
     */
    const int length = radius*2 + 1;
    const int inv = ((1<<16) + length/2)/length;
    int x, sum = 0;

    for (x = 0; x < radius; x++)
        sum += src[x*src_step]<<1;
    sum += src[radius*src_step];

    for (x = 0; x <= radius; x++) {
        sum += src[(radius+x)*src_step] - src[(radius-x)*src_step];
        dst[x*dst_step] = (sum*inv + (1<<15))>>16;
    }

    for (; x < len-radius; x++) {
        sum += src[(radius+x)*src_step] - src[(x-radius-1)*src_step];
        dst[x*dst_step] = (sum*inv + (1<<15))>>16;
    }

    for (; x < len; x++) {
        sum += src[(2*len-radius-x-1)*src_step] - src[(x-radius-1)*src_step];
        dst[x*dst_step] = (sum*inv + (1<<15))>>16;
    }
}

static inline void blur_power(uint8_t *dst, int dst_step, const uint8_t *src, int src_step,
                              int len, int radius, int power, uint8_t *temp[2])
{
    uint8_t *a = temp[0], *b = temp[1];

    if (radius && power) {
        blur(a, 1, src, src_step, len, radius);
        for (; power > 2; power--) {
            uint8_t *c;
            blur(b, 1, a, 1, len, radius);
            c = a; a = b; b = c;
        }
        if (power > 1) {
            blur(dst, dst_step, a, 1, len, radius);
        } else {
            int i;
            for (i = 0; i < len; i++)
                dst[i*dst_step] = a[i];
        }
    } else {
        int i;
        for (i = 0; i < len; i++)
            dst[i*dst_step] = src[i*src_step];
    }
}

static void hblur(uint8_t *dst, int dst_linesize, const uint8_t *src, int src_linesize,
                  int w, int h, int radius, int power, uint8_t *temp[2])
{
    int y;

    if (radius == 0 && dst == src)
        return;

    for (y = 0; y < h; y++)
        blur_power(dst + y*dst_linesize, 1, src + y*src_linesize, 1,
                   w, radius, power, temp);
}

static void vblur(uint8_t *dst, int dst_linesize, const uint8_t *src, int src_linesize,
                  int w, int h, int radius, int power, uint8_t *temp[2])
{
    int x;

    if (radius == 0 && dst == src)
        return;

    for (x = 0; x < w; x++)
        blur_power(dst + x, dst_linesize, src + x, src_linesize,
                   h, radius, power, temp);
}

static void draw_slice(AVFilterLink *inlink, int y0, int h0, int slice_dir)
{
    AVFilterContext *ctx = inlink->dst;
    BoxBlurContext *boxblur = ctx->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFilterBufferRef *inpicref  = inlink ->cur_buf;
    AVFilterBufferRef *outpicref = outlink->out_buf;
    int plane;
    int cw = inlink->w >> boxblur->hsub, ch = h0 >> boxblur->vsub;
    int w[4] = { inlink->w, cw, cw, inlink->w };
    int h[4] = { h0, ch, ch, h0 };

    for (plane = 0; inpicref->data[plane] && plane < 4; plane++)
        hblur(outpicref->data[plane], outpicref->linesize[plane],
              inpicref ->data[plane], inpicref ->linesize[plane],
              w[plane], h[plane], boxblur->radius[plane], boxblur->power[plane],
              boxblur->temp);

    for (plane = 0; inpicref->data[plane] && plane < 4; plane++)
        vblur(outpicref->data[plane], outpicref->linesize[plane],
              outpicref->data[plane], outpicref->linesize[plane],
              w[plane], h[plane], boxblur->radius[plane], boxblur->power[plane],
              boxblur->temp);

    ff_draw_slice(outlink, y0, h0, slice_dir);
}

AVFilter avfilter_vf_boxblur = {
    .name          = "boxblur",
    .description   = NULL_IF_CONFIG_SMALL("Blur the input."),
    .priv_size     = sizeof(BoxBlurContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,

    .inputs    = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .config_props     = config_input,
                                    .draw_slice       = draw_slice,
                                    .min_perms        = AV_PERM_READ },
                                  { .name = NULL}},
    .outputs   = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO, },
                                  { .name = NULL}},
};
