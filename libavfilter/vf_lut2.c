/*
 * Copyright (c) 2016 Paul B Mahol
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
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "drawutils.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "framesync.h"

static const char *const var_names[] = {
    "w",        ///< width of the input video
    "h",        ///< height of the input video
    "x",        ///< input value for the pixel from input #1
    "y",        ///< input value for the pixel from input #2
    "bdx",      ///< input #1 video bitdepth
    "bdy",      ///< input #2 video bitdepth
    NULL
};

enum var_name {
    VAR_W,
    VAR_H,
    VAR_X,
    VAR_Y,
    VAR_BITDEPTHX,
    VAR_BITDEPTHY,
    VAR_VARS_NB
};

typedef struct LUT2Context {
    const AVClass *class;
    FFFrameSync fs;

    int odepth;
    char   *comp_expr_str[4];

    AVExpr *comp_expr[4];
    double var_values[VAR_VARS_NB];
    uint16_t *lut[4];  ///< lookup table for each component
    int width[4], height[4];
    int widthx[4], heightx[4];
    int widthy[4], heighty[4];
    int nb_planesx;
    int nb_planesy;
    int nb_planes;
    int depth, depthx, depthy;
    int tlut2;
    AVFrame *prev_frame;        /* only used with tlut2 */

    int (*lut2)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} LUT2Context;

typedef struct ThreadData {
    AVFrame *out, *srcx, *srcy;
} ThreadData;

#define OFFSET(x) offsetof(LUT2Context, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption options[] = {
    { "c0", "set component #0 expression", OFFSET(comp_expr_str[0]),  AV_OPT_TYPE_STRING, { .str = "x" }, .flags = FLAGS },
    { "c1", "set component #1 expression", OFFSET(comp_expr_str[1]),  AV_OPT_TYPE_STRING, { .str = "x" }, .flags = FLAGS },
    { "c2", "set component #2 expression", OFFSET(comp_expr_str[2]),  AV_OPT_TYPE_STRING, { .str = "x" }, .flags = FLAGS },
    { "c3", "set component #3 expression", OFFSET(comp_expr_str[3]),  AV_OPT_TYPE_STRING, { .str = "x" }, .flags = FLAGS },
    { "d",  "set output depth",            OFFSET(odepth),            AV_OPT_TYPE_INT,    { .i64 =  0  }, 0, 16, .flags = FLAGS },
    { NULL }
};

static av_cold void uninit(AVFilterContext *ctx)
{
    LUT2Context *s = ctx->priv;
    int i;

    ff_framesync_uninit(&s->fs);
    av_frame_free(&s->prev_frame);

    for (i = 0; i < 4; i++) {
        av_expr_free(s->comp_expr[i]);
        s->comp_expr[i] = NULL;
        av_freep(&s->comp_expr_str[i]);
        av_freep(&s->lut[i]);
    }
}

#define BIT8_FMTS \
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV440P, \
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P, \
    AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUV420P, \
    AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P, \
    AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P, \
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP,

#define BIT9_FMTS \
    AV_PIX_FMT_GBRP9, AV_PIX_FMT_GRAY9, \
    AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9, \
    AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA444P9,

#define BIT10_FMTS \
    AV_PIX_FMT_GRAY10, AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRAP10, \
    AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10, \
    AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA444P10,

#define BIT12_FMTS \
    AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV440P12, \
    AV_PIX_FMT_GRAY12, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRP12,

#define BIT14_FMTS \
    AV_PIX_FMT_YUV420P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV444P14, \
    AV_PIX_FMT_GRAY12, AV_PIX_FMT_GBRP14,

#define BIT16_FMTS \
    AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16, \
    AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA444P16, \
    AV_PIX_FMT_GBRP16, AV_PIX_FMT_GBRAP16, AV_PIX_FMT_GRAY16,

static int query_formats(AVFilterContext *ctx)
{
    LUT2Context *s = ctx->priv;
    static const enum AVPixelFormat all_pix_fmts[] = {
        BIT8_FMTS
        BIT9_FMTS
        BIT10_FMTS
        BIT12_FMTS
        AV_PIX_FMT_NONE
    };
    static const enum AVPixelFormat bit8_pix_fmts[] = {
        BIT8_FMTS
        AV_PIX_FMT_NONE
    };
    static const enum AVPixelFormat bit9_pix_fmts[] = {
        BIT9_FMTS
        AV_PIX_FMT_NONE
    };
    static const enum AVPixelFormat bit10_pix_fmts[] = {
        BIT10_FMTS
        AV_PIX_FMT_NONE
    };
    static const enum AVPixelFormat bit12_pix_fmts[] = {
        BIT12_FMTS
        AV_PIX_FMT_NONE
    };
    static const enum AVPixelFormat bit14_pix_fmts[] = {
        BIT14_FMTS
        AV_PIX_FMT_NONE
    };
    static const enum AVPixelFormat bit16_pix_fmts[] = {
        BIT16_FMTS
        AV_PIX_FMT_NONE
    };
    const enum AVPixelFormat *pix_fmts;
    int ret;

    if (s->tlut2 || !s->odepth)
        return ff_set_common_formats(ctx, ff_make_format_list(all_pix_fmts));

    ret = ff_formats_ref(ff_make_format_list(all_pix_fmts), &ctx->inputs[0]->out_formats);
    if (ret < 0)
        return ret;

    switch (s->odepth) {
    case 8:  pix_fmts = bit8_pix_fmts;  break;
    case 9:  pix_fmts = bit9_pix_fmts;  break;
    case 10: pix_fmts = bit10_pix_fmts; break;
    case 12: pix_fmts = bit12_pix_fmts; break;
    case 14: pix_fmts = bit14_pix_fmts; break;
    case 16: pix_fmts = bit16_pix_fmts; break;
    default: av_log(ctx, AV_LOG_ERROR, "Unsupported output bit depth %d.\n", s->odepth);
             return AVERROR(EINVAL);
    }

    return ff_formats_ref(ff_make_format_list(pix_fmts), &ctx->outputs[0]->in_formats);
}

static int config_inputx(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    LUT2Context *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int hsub = desc->log2_chroma_w;
    int vsub = desc->log2_chroma_h;

    s->nb_planesx = av_pix_fmt_count_planes(inlink->format);
    s->heightx[1] = s->heightx[2] = AV_CEIL_RSHIFT(inlink->h, vsub);
    s->heightx[0] = s->heightx[3] = inlink->h;
    s->widthx[1]  = s->widthx[2]  = AV_CEIL_RSHIFT(inlink->w, hsub);
    s->widthx[0]  = s->widthx[3]  = inlink->w;

    s->var_values[VAR_W] = inlink->w;
    s->var_values[VAR_H] = inlink->h;
    s->depthx = desc->comp[0].depth;
    s->var_values[VAR_BITDEPTHX] = s->depthx;

    if (s->tlut2) {
        s->depthy = desc->comp[0].depth;
        s->var_values[VAR_BITDEPTHY] = s->depthy;
    }

    return 0;
}

static int config_inputy(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    LUT2Context *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int hsub = desc->log2_chroma_w;
    int vsub = desc->log2_chroma_h;

    s->nb_planesy = av_pix_fmt_count_planes(inlink->format);
    s->depthy = desc->comp[0].depth;
    s->var_values[VAR_BITDEPTHY] = s->depthy;
    s->heighty[1] = s->heighty[2] = AV_CEIL_RSHIFT(inlink->h, vsub);
    s->heighty[0] = s->heighty[3] = inlink->h;
    s->widthy[1]  = s->widthy[2]  = AV_CEIL_RSHIFT(inlink->w, hsub);
    s->widthy[0]  = s->widthy[3]  = inlink->w;

    return 0;
}

#define DEFINE_LUT2(zname, xname, yname, ztype, xtype, ytype, zdiv, xdiv, ydiv)  \
static int lut2_##zname##_##xname##_##yname(AVFilterContext *ctx,                \
                                             void *arg,                          \
                                             int jobnr, int nb_jobs)             \
{                                                                                \
    LUT2Context *s = ctx->priv;                                                  \
    ThreadData *td = arg;                                                        \
    AVFrame *out = td->out;                                                      \
    AVFrame *srcx = td->srcx;                                                    \
    AVFrame *srcy = td->srcy;                                                    \
    const int odepth = s->odepth;                                                \
    int p, y, x;                                                                 \
                                                                                 \
    for (p = 0; p < s->nb_planes; p++) {                                         \
        const int slice_start = (s->heightx[p] * jobnr) / nb_jobs;               \
        const int slice_end = (s->heightx[p] * (jobnr+1)) / nb_jobs;             \
        const uint16_t *lut = s->lut[p];                                         \
        const xtype *srcxx;                                                      \
        const ytype *srcyy;                                                      \
        ztype *dst;                                                              \
                                                                                 \
        dst   = (ztype *)(out->data[p] + slice_start * out->linesize[p]);        \
        srcxx = (const xtype *)(srcx->data[p] + slice_start * srcx->linesize[p]);\
        srcyy = (const ytype *)(srcy->data[p] + slice_start * srcy->linesize[p]);\
                                                                                 \
        for (y = slice_start; y < slice_end; y++) {                              \
            for (x = 0; x < s->widthx[p]; x++) {                                 \
                dst[x] = av_clip_uintp2_c(lut[(srcyy[x] << s->depthx) | srcxx[x]], odepth); \
            }                                                                    \
                                                                                 \
            dst   += out->linesize[p] / zdiv;                                    \
            srcxx += srcx->linesize[p] / xdiv;                                   \
            srcyy += srcy->linesize[p] / ydiv;                                   \
        }                                                                        \
    }                                                                            \
    return 0;                                                                    \
}

DEFINE_LUT2(8,   8,  8,  uint8_t,  uint8_t,  uint8_t, 1, 1, 1)
DEFINE_LUT2(8,   8, 16,  uint8_t,  uint8_t, uint16_t, 1, 1, 2)
DEFINE_LUT2(8,  16,  8,  uint8_t, uint16_t,  uint8_t, 1, 2, 1)
DEFINE_LUT2(8,  16, 16,  uint8_t, uint16_t, uint16_t, 1, 2, 2)
DEFINE_LUT2(16,  8,  8, uint16_t,  uint8_t,  uint8_t, 2, 1, 1)
DEFINE_LUT2(16,  8, 16, uint16_t,  uint8_t, uint16_t, 2, 1, 2)
DEFINE_LUT2(16, 16,  8, uint16_t, uint16_t,  uint8_t, 2, 2, 1)
DEFINE_LUT2(16, 16, 16, uint16_t, uint16_t, uint16_t, 2, 2, 2)

static int process_frame(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    LUT2Context *s = fs->opaque;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out, *srcx = NULL, *srcy = NULL;
    int ret;

    if ((ret = ff_framesync_get_frame(&s->fs, 0, &srcx, 0)) < 0 ||
        (ret = ff_framesync_get_frame(&s->fs, 1, &srcy, 0)) < 0)
        return ret;

    if (ctx->is_disabled || !srcy) {
        out = av_frame_clone(srcx);
        if (!out)
            return AVERROR(ENOMEM);
    } else {
        ThreadData td;

        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out)
            return AVERROR(ENOMEM);
        av_frame_copy_props(out, srcx);

        td.out  = out;
        td.srcx = srcx;
        td.srcy = srcy;
        ctx->internal->execute(ctx, s->lut2, &td, NULL, FFMIN(s->heightx[1], ff_filter_get_nb_threads(ctx)));
    }

    out->pts = av_rescale_q(s->fs.pts, s->fs.time_base, outlink->time_base);

    return ff_filter_frame(outlink, out);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    LUT2Context *s = ctx->priv;
    int p, ret;

    s->depth = s->depthx + s->depthy;
    s->nb_planes = s->nb_planesx;

    s->lut2 = s->depth > 16 ? lut2_16_16_16 : lut2_8_8_8;
    if (s->odepth) {
        if (s->depthx == 8 && s->depthy == 8 && s->odepth > 8)
            s->lut2 = lut2_16_8_8;
        if (s->depthx > 8 && s->depthy == 8 && s->odepth > 8)
            s->lut2 = lut2_16_16_8;
        if (s->depthx == 8 && s->depthy > 8 && s->odepth > 8)
            s->lut2 = lut2_16_8_16;
        if (s->depthx == 8 && s->depthy == 8 && s->odepth == 8)
            s->lut2 = lut2_8_8_8;
        if (s->depthx > 8 && s->depthy == 8 && s->odepth == 8)
            s->lut2 = lut2_8_16_8;
        if (s->depthx == 8 && s->depthy > 8 && s->odepth == 8)
            s->lut2 = lut2_8_8_16;
        if (s->depthx > 8 && s->depthy > 8 && s->odepth == 8)
            s->lut2 = lut2_8_16_16;
    } else {
        s->odepth = s->depthx;
    }

    for (p = 0; p < s->nb_planes; p++) {
        s->lut[p] = av_malloc_array(1 << s->depth, sizeof(uint16_t));
        if (!s->lut[p])
            return AVERROR(ENOMEM);
    }

    for (p = 0; p < s->nb_planes; p++) {
        double res;
        int x, y;

        /* create the parsed expression */
        av_expr_free(s->comp_expr[p]);
        s->comp_expr[p] = NULL;
        ret = av_expr_parse(&s->comp_expr[p], s->comp_expr_str[p],
                            var_names, NULL, NULL, NULL, NULL, 0, ctx);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Error when parsing the expression '%s' for the component %d.\n",
                   s->comp_expr_str[p], p);
            return AVERROR(EINVAL);
        }

        /* compute the lut */
        for (y = 0; y < (1 << s->depthy); y++) {
            s->var_values[VAR_Y] = y;
            for (x = 0; x < (1 << s->depthx); x++) {
                s->var_values[VAR_X] = x;
                res = av_expr_eval(s->comp_expr[p], s->var_values, s);
                if (isnan(res)) {
                    av_log(ctx, AV_LOG_ERROR,
                           "Error when evaluating the expression '%s' for the values %d and %d for the component %d.\n",
                           s->comp_expr_str[p], x, y, p);
                    return AVERROR(EINVAL);
                }

                s->lut[p][(y << s->depthx) + x] = res;
            }
        }
    }

    return 0;
}

static int lut2_config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    LUT2Context *s = ctx->priv;
    AVFilterLink *srcx = ctx->inputs[0];
    AVFilterLink *srcy = ctx->inputs[1];
    FFFrameSyncIn *in;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(outlink->format);
    int hsub = desc->log2_chroma_w;
    int vsub = desc->log2_chroma_h;
    int ret;

    outlink->w = srcx->w;
    outlink->h = srcx->h;
    outlink->time_base = srcx->time_base;
    outlink->sample_aspect_ratio = srcx->sample_aspect_ratio;
    outlink->frame_rate = srcx->frame_rate;

    s->nb_planes = av_pix_fmt_count_planes(outlink->format);
    s->height[1] = s->height[2] = AV_CEIL_RSHIFT(outlink->h, vsub);
    s->height[0] = s->height[3] = outlink->h;
    s->width[1]  = s->width[2]  = AV_CEIL_RSHIFT(outlink->w, hsub);
    s->width[0]  = s->width[3]  = outlink->w;

    if (!s->odepth && srcx->format != srcy->format) {
        av_log(ctx, AV_LOG_ERROR, "inputs must be of same pixel format\n");
        return AVERROR(EINVAL);
    }

    if (srcx->w != srcy->w || srcx->h != srcy->h) {
        av_log(ctx, AV_LOG_ERROR, "First input link %s parameters "
               "(size %dx%d) do not match the corresponding "
               "second input link %s parameters (size %dx%d)\n",
               ctx->input_pads[0].name, srcx->w, srcx->h,
               ctx->input_pads[1].name,
               srcy->w, srcy->h);
        return AVERROR(EINVAL);
    }

    if (s->nb_planesx != s->nb_planesy) {
        av_log(ctx, AV_LOG_ERROR, "First input link %s number of planes "
               "(%d) do not match the corresponding "
               "second input link %s number of planes (%d)\n",
               ctx->input_pads[0].name, s->nb_planesx,
               ctx->input_pads[1].name, s->nb_planesy);
        return AVERROR(EINVAL);
    }

    if (s->nb_planesx != s->nb_planes) {
        av_log(ctx, AV_LOG_ERROR, "First input link %s number of planes "
               "(%d) do not match the corresponding "
               "output link %s number of planes (%d)\n",
               ctx->input_pads[0].name, s->nb_planesx,
               ctx->output_pads[0].name, s->nb_planes);
        return AVERROR(EINVAL);
    }

    if (s->widthx[1] != s->widthy[1] || s->heightx[1] != s->heighty[1]) {
        av_log(ctx, AV_LOG_ERROR, "First input link %s 2nd plane "
               "(size %dx%d) do not match the corresponding "
               "second input link %s 2nd plane (size %dx%d)\n",
               ctx->input_pads[0].name, s->widthx[1], s->heightx[1],
               ctx->input_pads[1].name,
               s->widthy[1], s->heighty[1]);
        return AVERROR(EINVAL);
    }

    if (s->widthx[2] != s->widthy[2] || s->heightx[2] != s->heighty[2]) {
        av_log(ctx, AV_LOG_ERROR, "First input link %s 3rd plane "
               "(size %dx%d) do not match the corresponding "
               "second input link %s 3rd plane (size %dx%d)\n",
               ctx->input_pads[0].name, s->widthx[2], s->heightx[2],
               ctx->input_pads[1].name,
               s->widthy[2], s->heighty[2]);
        return AVERROR(EINVAL);
    }

    if (s->widthx[1] != s->width[1] || s->heightx[1] != s->height[1]) {
        av_log(ctx, AV_LOG_ERROR, "First input link %s 2nd plane "
               "(size %dx%d) do not match the corresponding "
               "output link %s 2nd plane (size %dx%d)\n",
               ctx->input_pads[0].name, s->widthx[1], s->heightx[1],
               ctx->output_pads[0].name, s->width[1], s->height[1]);
        return AVERROR(EINVAL);
    }

    if (s->widthx[2] != s->width[2] || s->heightx[2] != s->height[2]) {
        av_log(ctx, AV_LOG_ERROR, "First input link %s 3rd plane "
               "(size %dx%d) do not match the corresponding "
               "output link %s 3rd plane (size %dx%d)\n",
               ctx->input_pads[0].name, s->widthx[2], s->heightx[2],
               ctx->output_pads[0].name, s->width[2], s->height[2]);
        return AVERROR(EINVAL);
    }

    if ((ret = ff_framesync_init(&s->fs, ctx, 2)) < 0)
        return ret;

    in = s->fs.in;
    in[0].time_base = srcx->time_base;
    in[1].time_base = srcy->time_base;
    in[0].sync   = 2;
    in[0].before = EXT_STOP;
    in[0].after  = EXT_INFINITY;
    in[1].sync   = 1;
    in[1].before = EXT_STOP;
    in[1].after  = EXT_INFINITY;
    s->fs.opaque   = s;
    s->fs.on_event = process_frame;

    if ((ret = config_output(outlink)) < 0)
        return ret;

    return ff_framesync_configure(&s->fs);
}

static int activate(AVFilterContext *ctx)
{
    LUT2Context *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "srcx",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_inputx,
    },
    {
        .name         = "srcy",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_inputy,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = lut2_config_output,
    },
    { NULL }
};

#define lut2_options options

FRAMESYNC_DEFINE_CLASS(lut2, LUT2Context, fs);

AVFilter ff_vf_lut2 = {
    .name          = "lut2",
    .description   = NULL_IF_CONFIG_SMALL("Compute and apply a lookup table from two video inputs."),
    .preinit       = lut2_framesync_preinit,
    .priv_size     = sizeof(LUT2Context),
    .priv_class    = &lut2_class,
    .uninit        = uninit,
    .query_formats = query_formats,
    .activate      = activate,
    .inputs        = inputs,
    .outputs       = outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                     AVFILTER_FLAG_SLICE_THREADS,
};

#if CONFIG_TLUT2_FILTER

static av_cold int init(AVFilterContext *ctx)
{
    LUT2Context *s = ctx->priv;

    s->tlut2 = !strcmp(ctx->filter->name, "tlut2");

    return 0;
}

static int tlut2_filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    LUT2Context *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    if (s->prev_frame) {
        AVFrame *out;

        if (ctx->is_disabled) {
            out = av_frame_clone(frame);
        } else {
            ThreadData td;

            out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
            if (!out) {
                av_frame_free(&s->prev_frame);
                s->prev_frame = frame;
                return AVERROR(ENOMEM);
            }

            av_frame_copy_props(out, frame);

            td.out  = out;
            td.srcx = frame;
            td.srcy = s->prev_frame;
            ctx->internal->execute(ctx, s->lut2, &td, NULL, FFMIN(s->heightx[1], ff_filter_get_nb_threads(ctx)));
        }
        av_frame_free(&s->prev_frame);
        s->prev_frame = frame;
        return ff_filter_frame(outlink, out);
    }
    s->prev_frame = frame;
    return 0;
}

static const AVOption tlut2_options[] = {
    { "c0", "set component #0 expression", OFFSET(comp_expr_str[0]),  AV_OPT_TYPE_STRING, { .str = "x" }, .flags = FLAGS },
    { "c1", "set component #1 expression", OFFSET(comp_expr_str[1]),  AV_OPT_TYPE_STRING, { .str = "x" }, .flags = FLAGS },
    { "c2", "set component #2 expression", OFFSET(comp_expr_str[2]),  AV_OPT_TYPE_STRING, { .str = "x" }, .flags = FLAGS },
    { "c3", "set component #3 expression", OFFSET(comp_expr_str[3]),  AV_OPT_TYPE_STRING, { .str = "x" }, .flags = FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(tlut2);

static const AVFilterPad tlut2_inputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .filter_frame  = tlut2_filter_frame,
        .config_props  = config_inputx,
    },
    { NULL }
};

static const AVFilterPad tlut2_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
    { NULL }
};

AVFilter ff_vf_tlut2 = {
    .name          = "tlut2",
    .description   = NULL_IF_CONFIG_SMALL("Compute and apply a lookup table from two successive frames."),
    .priv_size     = sizeof(LUT2Context),
    .priv_class    = &tlut2_class,
    .query_formats = query_formats,
    .init          = init,
    .uninit        = uninit,
    .inputs        = tlut2_inputs,
    .outputs       = tlut2_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                     AVFILTER_FLAG_SLICE_THREADS,
};

#endif
