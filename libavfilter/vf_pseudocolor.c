/*
 * Copyright (c) 2017 Paul B Mahol
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

#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "libavutil/eval.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

static const char *const var_names[] = {
    "w",        ///< width of the input video
    "h",        ///< height of the input video
    "val",      ///< input value for the pixel
    "ymin",
    "umin",
    "vmin",
    "amin",
    "ymax",
    "umax",
    "vmax",
    "amax",
    NULL
};

enum var_name {
    VAR_W,
    VAR_H,
    VAR_VAL,
    VAR_YMIN,
    VAR_UMIN,
    VAR_VMIN,
    VAR_AMIN,
    VAR_YMAX,
    VAR_UMAX,
    VAR_VMAX,
    VAR_AMAX,
    VAR_VARS_NB
};

typedef struct PseudoColorContext {
    const AVClass *class;
    int max;
    int index;
    int nb_planes;
    int color;
    int linesize[4];
    int width[4], height[4];
    double var_values[VAR_VARS_NB];
    char   *comp_expr_str[4];
    AVExpr *comp_expr[4];
    float lut[4][256*256];

    void (*filter[4])(int max, int width, int height,
                      const uint8_t *index, const uint8_t *src,
                      uint8_t *dst,
                      ptrdiff_t ilinesize,
                      ptrdiff_t slinesize,
                      ptrdiff_t dlinesize,
                      float *lut);
} PseudoColorContext;

#define OFFSET(x) offsetof(PseudoColorContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption pseudocolor_options[] = {
    { "c0", "set component #0 expression", OFFSET(comp_expr_str[0]), AV_OPT_TYPE_STRING, {.str="val"},   .flags = FLAGS },
    { "c1", "set component #1 expression", OFFSET(comp_expr_str[1]), AV_OPT_TYPE_STRING, {.str="val"},   .flags = FLAGS },
    { "c2", "set component #2 expression", OFFSET(comp_expr_str[2]), AV_OPT_TYPE_STRING, {.str="val"},   .flags = FLAGS },
    { "c3", "set component #3 expression", OFFSET(comp_expr_str[3]), AV_OPT_TYPE_STRING, {.str="val"},   .flags = FLAGS },
    { "i",  "set component as base",       OFFSET(index),            AV_OPT_TYPE_INT,    {.i64=0}, 0, 3, .flags = FLAGS },
    { NULL }
};

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY16,
    AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUVA420P,
    AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVA422P,
    AV_PIX_FMT_YUV444P, AV_PIX_FMT_GBRP,
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_GBRAP,
    AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUVA422P9,
    AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUVA420P9,
    AV_PIX_FMT_YUV444P9, AV_PIX_FMT_YUVA444P9,
    AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUVA420P10,
    AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUVA422P10,
    AV_PIX_FMT_YUV444P10, AV_PIX_FMT_YUVA444P10,
    AV_PIX_FMT_YUV420P12,
    AV_PIX_FMT_YUV422P12,
    AV_PIX_FMT_YUV444P12,
    AV_PIX_FMT_YUV420P14,
    AV_PIX_FMT_YUV422P14,
    AV_PIX_FMT_YUV444P14,
    AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUVA420P16,
    AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUVA422P16,
    AV_PIX_FMT_YUV444P16, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_GBRP9,
    AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRAP10,
    AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRAP12,
    AV_PIX_FMT_GBRP14,
    AV_PIX_FMT_GBRP16, AV_PIX_FMT_GBRAP16,
    AV_PIX_FMT_NONE
};

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static void pseudocolor_filter(int max, int width, int height,
                               const uint8_t *index,
                               const uint8_t *src,
                               uint8_t *dst,
                               ptrdiff_t ilinesize,
                               ptrdiff_t slinesize,
                               ptrdiff_t dlinesize,
                               float *lut)
{
    int x, y;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            int v = lut[index[x]];

            if (v >= 0 && v <= max) {
                dst[x] = v;
            } else {
                dst[x] = src[x];
            }
        }
        index += ilinesize;
        src += slinesize;
        dst += dlinesize;
    }
}

static void pseudocolor_filter_11(int max, int width, int height,
                                  const uint8_t *index,
                                  const uint8_t *src,
                                  uint8_t *dst,
                                  ptrdiff_t ilinesize,
                                  ptrdiff_t slinesize,
                                  ptrdiff_t dlinesize,
                                  float *lut)
{
    int x, y;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            int v = lut[index[(y << 1) * ilinesize + (x << 1)]];

            if (v >= 0 && v <= max) {
                dst[x] = v;
            } else {
                dst[x] = src[x];
            }
        }
        src += slinesize;
        dst += dlinesize;
    }
}

static void pseudocolor_filter_11d(int max, int width, int height,
                                   const uint8_t *index,
                                   const uint8_t *src,
                                   uint8_t *dst,
                                   ptrdiff_t ilinesize,
                                   ptrdiff_t slinesize,
                                   ptrdiff_t dlinesize,
                                   float *lut)
{
    int x, y;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            int v = lut[index[(y >> 1) * ilinesize + (x >> 1)]];

            if (v >= 0 && v <= max) {
                dst[x] = v;
            } else {
                dst[x] = src[x];
            }
        }
        src += slinesize;
        dst += dlinesize;
    }
}

static void pseudocolor_filter_10(int max, int width, int height,
                                  const uint8_t *index,
                                  const uint8_t *src,
                                  uint8_t *dst,
                                  ptrdiff_t ilinesize,
                                  ptrdiff_t slinesize,
                                  ptrdiff_t dlinesize,
                                  float *lut)
{
    int x, y;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            int v = lut[index[x << 1]];

            if (v >= 0 && v <= max) {
                dst[x] = v;
            } else {
                dst[x] = src[x];
            }
        }
        index += ilinesize;
        src += slinesize;
        dst += dlinesize;
    }
}

static void pseudocolor_filter_10d(int max, int width, int height,
                                   const uint8_t *index,
                                   const uint8_t *src,
                                   uint8_t *dst,
                                   ptrdiff_t ilinesize,
                                   ptrdiff_t slinesize,
                                   ptrdiff_t dlinesize,
                                   float *lut)
{
    int x, y;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            int v = lut[index[x >> 1]];

            if (v >= 0 && v <= max) {
                dst[x] = v;
            } else {
                dst[x] = src[x];
            }
        }
        index += ilinesize;
        src += slinesize;
        dst += dlinesize;
    }
}

static void pseudocolor_filter_16(int max, int width, int height,
                                  const uint8_t *iindex,
                                  const uint8_t *ssrc,
                                  uint8_t *ddst,
                                  ptrdiff_t ilinesize,
                                  ptrdiff_t slinesize,
                                  ptrdiff_t dlinesize,
                                  float *lut)
{
    const uint16_t *index = (const uint16_t *)iindex;
    const uint16_t *src = (const uint16_t *)ssrc;
    uint16_t *dst = (uint16_t *)ddst;
    int x, y;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            int v = lut[index[x]];

            if (v >= 0 && v <= max) {
                dst[x] = v;
            } else {
                dst[x] = src[x];
            }
        }
        index += ilinesize / 2;
        src += slinesize / 2;
        dst += dlinesize / 2;
    }
}

static void pseudocolor_filter_16_10(int max, int width, int height,
                                     const uint8_t *iindex,
                                     const uint8_t *ssrc,
                                     uint8_t *ddst,
                                     ptrdiff_t ilinesize,
                                     ptrdiff_t slinesize,
                                     ptrdiff_t dlinesize,
                                     float *lut)
{
    const uint16_t *index = (const uint16_t *)iindex;
    const uint16_t *src = (const uint16_t *)ssrc;
    uint16_t *dst = (uint16_t *)ddst;
    int x, y;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            int v = lut[index[x << 1]];

            if (v >= 0 && v <= max) {
                dst[x] = v;
            } else {
                dst[x] = src[x];
            }
        }
        index += ilinesize / 2;
        src += slinesize / 2;
        dst += dlinesize / 2;
    }
}

static void pseudocolor_filter_16_10d(int max, int width, int height,
                                      const uint8_t *iindex,
                                      const uint8_t *ssrc,
                                      uint8_t *ddst,
                                      ptrdiff_t ilinesize,
                                      ptrdiff_t slinesize,
                                      ptrdiff_t dlinesize,
                                      float *lut)
{
    const uint16_t *index = (const uint16_t *)iindex;
    const uint16_t *src = (const uint16_t *)ssrc;
    uint16_t *dst = (uint16_t *)ddst;
    int x, y;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            int v = lut[index[x >> 1]];

            if (v >= 0 && v <= max) {
                dst[x] = v;
            } else {
                dst[x] = src[x];
            }
        }
        index += ilinesize / 2;
        src += slinesize / 2;
        dst += dlinesize / 2;
    }
}

static void pseudocolor_filter_16_11(int max, int width, int height,
                                     const uint8_t *iindex,
                                     const uint8_t *ssrc,
                                     uint8_t *ddst,
                                     ptrdiff_t ilinesize,
                                     ptrdiff_t slinesize,
                                     ptrdiff_t dlinesize,
                                     float *lut)
{
    const uint16_t *index = (const uint16_t *)iindex;
    const uint16_t *src = (const uint16_t *)ssrc;
    uint16_t *dst = (uint16_t *)ddst;
    int x, y;

    ilinesize /= 2;
    dlinesize /= 2;
    slinesize /= 2;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            int v = lut[index[(y << 1) * ilinesize + (x << 1)]];

            if (v >= 0 && v <= max) {
                dst[x] = v;
            } else {
                dst[x] = src[x];
            }
        }
        src += slinesize;
        dst += dlinesize;
    }
}

static void pseudocolor_filter_16_11d(int max, int width, int height,
                                      const uint8_t *iindex,
                                      const uint8_t *ssrc,
                                      uint8_t *ddst,
                                      ptrdiff_t ilinesize,
                                      ptrdiff_t slinesize,
                                      ptrdiff_t dlinesize,
                                      float *lut)
{
    const uint16_t *index = (const uint16_t *)iindex;
    const uint16_t *src = (const uint16_t *)ssrc;
    uint16_t *dst = (uint16_t *)ddst;
    int x, y;

    ilinesize /= 2;
    dlinesize /= 2;
    slinesize /= 2;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            int v = lut[index[(y >> 1) * ilinesize + (x >> 1)]];

            if (v >= 0 && v <= max) {
                dst[x] = v;
            } else {
                dst[x] = src[x];
            }
        }
        src += slinesize;
        dst += dlinesize;
    }
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    PseudoColorContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int depth, ret, hsub, vsub, color;

    depth = desc->comp[0].depth;
    s->max = (1 << depth) - 1;
    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    if (s->index >= s->nb_planes) {
        av_log(ctx, AV_LOG_ERROR, "index out of allowed range\n");
        return AVERROR(EINVAL);
    }

    if ((ret = av_image_fill_linesizes(s->linesize, inlink->format, inlink->w)) < 0)
        return ret;

    hsub = desc->log2_chroma_w;
    vsub = desc->log2_chroma_h;
    s->height[1] = s->height[2] = AV_CEIL_RSHIFT(inlink->h, vsub);
    s->height[0] = s->height[3] = inlink->h;
    s->width[1]  = s->width[2]  = AV_CEIL_RSHIFT(inlink->w, hsub);
    s->width[0]  = s->width[3]  = inlink->w;

    s->var_values[VAR_W] = inlink->w;
    s->var_values[VAR_H] = inlink->h;

    s->var_values[VAR_YMIN] = 16 * (1 << (depth - 8));
    s->var_values[VAR_UMIN] = 16 * (1 << (depth - 8));
    s->var_values[VAR_VMIN] = 16 * (1 << (depth - 8));
    s->var_values[VAR_AMIN] = 0;
    s->var_values[VAR_YMAX] = 235 * (1 << (depth - 8));
    s->var_values[VAR_UMAX] = 240 * (1 << (depth - 8));
    s->var_values[VAR_VMAX] = 240 * (1 << (depth - 8));
    s->var_values[VAR_AMAX] = s->max;

    for (color = 0; color < s->nb_planes; color++) {
        double res;
        int val;

        /* create the parsed expression */
        av_expr_free(s->comp_expr[color]);
        s->comp_expr[color] = NULL;
        ret = av_expr_parse(&s->comp_expr[color], s->comp_expr_str[color],
                            var_names, NULL, NULL, NULL, NULL, 0, ctx);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Error when parsing the expression '%s' for the component %d and color %d.\n",
                   s->comp_expr_str[color], color, color);
            return AVERROR(EINVAL);
        }

        /* compute the lut */
        for (val = 0; val < FF_ARRAY_ELEMS(s->lut[color]); val++) {
            s->var_values[VAR_VAL] = val;

            res = av_expr_eval(s->comp_expr[color], s->var_values, s);
            if (isnan(res)) {
                av_log(ctx, AV_LOG_ERROR,
                       "Error when evaluating the expression '%s' for the value %d for the component %d.\n",
                       s->comp_expr_str[color], val, color);
                return AVERROR(EINVAL);
            }
            s->lut[color][val] = res;
        }
    }

    switch (inlink->format) {
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUVA444P:
    case AV_PIX_FMT_GBRP:
    case AV_PIX_FMT_GBRAP:
    case AV_PIX_FMT_GRAY8:
        s->filter[0] = s->filter[1] = s->filter[2] = s->filter[3] = pseudocolor_filter;
        break;
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVA420P:
        switch (s->index) {
        case 0:
        case 3:
            s->filter[0] = s->filter[3] = pseudocolor_filter;
            s->filter[1] = s->filter[2] = pseudocolor_filter_11;
            break;
        case 1:
        case 2:
            s->filter[0] = s->filter[3] = pseudocolor_filter_11d;
            s->filter[1] = s->filter[2] = pseudocolor_filter;
            break;
        }
        break;
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUVA422P:
        switch (s->index) {
        case 0:
        case 3:
            s->filter[0] = s->filter[3] = pseudocolor_filter;
            s->filter[1] = s->filter[2] = pseudocolor_filter_10;
            break;
        case 1:
        case 2:
            s->filter[0] = s->filter[3] = pseudocolor_filter_10d;
            s->filter[1] = s->filter[2] = pseudocolor_filter;
            break;
        }
        break;
    case AV_PIX_FMT_YUV444P9:
    case AV_PIX_FMT_YUVA444P9:
    case AV_PIX_FMT_YUV444P10:
    case AV_PIX_FMT_YUVA444P10:
    case AV_PIX_FMT_YUV444P12:
    case AV_PIX_FMT_YUV444P14:
    case AV_PIX_FMT_YUV444P16:
    case AV_PIX_FMT_YUVA444P16:
    case AV_PIX_FMT_GBRP9:
    case AV_PIX_FMT_GBRP10:
    case AV_PIX_FMT_GBRP12:
    case AV_PIX_FMT_GBRP14:
    case AV_PIX_FMT_GBRP16:
    case AV_PIX_FMT_GBRAP10:
    case AV_PIX_FMT_GBRAP12:
    case AV_PIX_FMT_GBRAP16:
    case AV_PIX_FMT_GRAY9:
    case AV_PIX_FMT_GRAY10:
    case AV_PIX_FMT_GRAY12:
    case AV_PIX_FMT_GRAY16:
        s->filter[0] = s->filter[1] = s->filter[2] = s->filter[3] = pseudocolor_filter_16;
        break;
    case AV_PIX_FMT_YUV422P9:
    case AV_PIX_FMT_YUVA422P9:
    case AV_PIX_FMT_YUV422P10:
    case AV_PIX_FMT_YUVA422P10:
    case AV_PIX_FMT_YUV422P12:
    case AV_PIX_FMT_YUV422P14:
    case AV_PIX_FMT_YUV422P16:
    case AV_PIX_FMT_YUVA422P16:
        switch (s->index) {
        case 0:
        case 3:
            s->filter[0] = s->filter[3] = pseudocolor_filter_16;
            s->filter[1] = s->filter[2] = pseudocolor_filter_16_10;
            break;
        case 1:
        case 2:
            s->filter[0] = s->filter[3] = pseudocolor_filter_16_10d;
            s->filter[1] = s->filter[2] = pseudocolor_filter_16;
            break;
        }
        break;
    case AV_PIX_FMT_YUV420P9:
    case AV_PIX_FMT_YUVA420P9:
    case AV_PIX_FMT_YUV420P10:
    case AV_PIX_FMT_YUVA420P10:
    case AV_PIX_FMT_YUV420P12:
    case AV_PIX_FMT_YUV420P14:
    case AV_PIX_FMT_YUV420P16:
    case AV_PIX_FMT_YUVA420P16:
        switch (s->index) {
        case 0:
        case 3:
            s->filter[0] = s->filter[3] = pseudocolor_filter_16;
            s->filter[1] = s->filter[2] = pseudocolor_filter_16_11;
            break;
        case 1:
        case 2:
            s->filter[0] = s->filter[3] = pseudocolor_filter_16_11d;
            s->filter[1] = s->filter[2] = pseudocolor_filter_16;
            break;
        }
        break;
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    PseudoColorContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    int plane;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    for (plane = 0; plane < s->nb_planes; plane++) {
        const uint8_t *index = in->data[s->index];
        const uint8_t *src = in->data[plane];
        uint8_t *dst = out->data[plane];
        ptrdiff_t ilinesize = in->linesize[s->index];
        ptrdiff_t slinesize = in->linesize[plane];
        ptrdiff_t dlinesize = out->linesize[plane];

        s->filter[plane](s->max, s->width[plane], s->height[plane],
                         index, src, dst, ilinesize, slinesize,
                         dlinesize, s->lut[plane]);
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
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

static av_cold void uninit(AVFilterContext *ctx)
{
    PseudoColorContext *s = ctx->priv;
    int i;

    for (i = 0; i < 4; i++) {
        av_expr_free(s->comp_expr[i]);
        s->comp_expr[i] = NULL;
    }
}

AVFILTER_DEFINE_CLASS(pseudocolor);

AVFilter ff_vf_pseudocolor = {
    .name          = "pseudocolor",
    .description   = NULL_IF_CONFIG_SMALL("Make pseudocolored video frames."),
    .priv_size     = sizeof(PseudoColorContext),
    .priv_class    = &pseudocolor_class,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = inputs,
    .outputs       = outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
