/*
 * Copyright (c) 2011 Stefano Sabatini
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
 * Compute a look-up table for binding the input value to the output
 * value, and apply it to input video.
 */

#include "config_components.h"

#include "libavutil/attributes.h"
#include "libavutil/bswap.h"
#include "libavutil/common.h"
#include "libavutil/eval.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "drawutils.h"
#include "filters.h"
#include "formats.h"
#include "video.h"

static const char *const var_names[] = {
    "w",        ///< width of the input video
    "h",        ///< height of the input video
    "val",      ///< input value for the pixel
    "maxval",   ///< max value for the pixel
    "minval",   ///< min value for the pixel
    "negval",   ///< negated value
    "clipval",
    NULL
};

enum var_name {
    VAR_W,
    VAR_H,
    VAR_VAL,
    VAR_MAXVAL,
    VAR_MINVAL,
    VAR_NEGVAL,
    VAR_CLIPVAL,
    VAR_VARS_NB
};

typedef struct LutContext {
    const AVClass *class;
    uint16_t lut[4][256 * 256];  ///< lookup table for each component
    char   *comp_expr_str[4];
    AVExpr *comp_expr[4];
    int hsub, vsub;
    double var_values[VAR_VARS_NB];
    int is_rgb, is_yuv;
    int is_planar;
    int is_16bit;
    int step;
} LutContext;

#define Y 0
#define U 1
#define V 2
#define R 0
#define G 1
#define B 2
#define A 3

#define OFFSET(x) offsetof(LutContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption options[] = {
    { "c0", "set component #0 expression", OFFSET(comp_expr_str[0]),  AV_OPT_TYPE_STRING, { .str = "clipval" }, .flags = FLAGS },
    { "c1", "set component #1 expression", OFFSET(comp_expr_str[1]),  AV_OPT_TYPE_STRING, { .str = "clipval" }, .flags = FLAGS },
    { "c2", "set component #2 expression", OFFSET(comp_expr_str[2]),  AV_OPT_TYPE_STRING, { .str = "clipval" }, .flags = FLAGS },
    { "c3", "set component #3 expression", OFFSET(comp_expr_str[3]),  AV_OPT_TYPE_STRING, { .str = "clipval" }, .flags = FLAGS },
    { "y",  "set Y expression",            OFFSET(comp_expr_str[Y]),  AV_OPT_TYPE_STRING, { .str = "clipval" }, .flags = FLAGS },
    { "u",  "set U expression",            OFFSET(comp_expr_str[U]),  AV_OPT_TYPE_STRING, { .str = "clipval" }, .flags = FLAGS },
    { "v",  "set V expression",            OFFSET(comp_expr_str[V]),  AV_OPT_TYPE_STRING, { .str = "clipval" }, .flags = FLAGS },
    { "r",  "set R expression",            OFFSET(comp_expr_str[R]),  AV_OPT_TYPE_STRING, { .str = "clipval" }, .flags = FLAGS },
    { "g",  "set G expression",            OFFSET(comp_expr_str[G]),  AV_OPT_TYPE_STRING, { .str = "clipval" }, .flags = FLAGS },
    { "b",  "set B expression",            OFFSET(comp_expr_str[B]),  AV_OPT_TYPE_STRING, { .str = "clipval" }, .flags = FLAGS },
    { "a",  "set A expression",            OFFSET(comp_expr_str[A]),  AV_OPT_TYPE_STRING, { .str = "clipval" }, .flags = FLAGS },
    { NULL }
};

static av_cold void uninit(AVFilterContext *ctx)
{
    LutContext *s = ctx->priv;
    int i;

    for (i = 0; i < 4; i++) {
        av_expr_free(s->comp_expr[i]);
        s->comp_expr[i] = NULL;
        av_freep(&s->comp_expr_str[i]);
    }
}

#define YUV_FORMATS                                         \
    AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,    \
    AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV410P,  AV_PIX_FMT_YUV440P,    \
    AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA444P,   \
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,   \
    AV_PIX_FMT_YUVJ440P,                                             \
    AV_PIX_FMT_YUV444P9LE, AV_PIX_FMT_YUV422P9LE, AV_PIX_FMT_YUV420P9LE, \
    AV_PIX_FMT_YUV444P10LE, AV_PIX_FMT_YUV422P10LE, AV_PIX_FMT_YUV420P10LE, AV_PIX_FMT_YUV440P10LE, \
    AV_PIX_FMT_YUV444P12LE, AV_PIX_FMT_YUV422P12LE, AV_PIX_FMT_YUV420P12LE, AV_PIX_FMT_YUV440P12LE, \
    AV_PIX_FMT_YUV444P14LE, AV_PIX_FMT_YUV422P14LE, AV_PIX_FMT_YUV420P14LE, \
    AV_PIX_FMT_YUV444P16LE, AV_PIX_FMT_YUV422P16LE, AV_PIX_FMT_YUV420P16LE, \
    AV_PIX_FMT_YUVA444P16LE, AV_PIX_FMT_YUVA422P16LE, AV_PIX_FMT_YUVA420P16LE

#define RGB_FORMATS                             \
    AV_PIX_FMT_ARGB,         AV_PIX_FMT_RGBA,         \
    AV_PIX_FMT_ABGR,         AV_PIX_FMT_BGRA,         \
    AV_PIX_FMT_RGB24,        AV_PIX_FMT_BGR24,        \
    AV_PIX_FMT_RGB48LE,      AV_PIX_FMT_RGBA64LE,     \
    AV_PIX_FMT_GBRP,         AV_PIX_FMT_GBRAP,        \
    AV_PIX_FMT_GBRP9LE,      AV_PIX_FMT_GBRP10LE,     \
    AV_PIX_FMT_GBRAP10LE,                             \
    AV_PIX_FMT_GBRP12LE,     AV_PIX_FMT_GBRP14LE,     \
    AV_PIX_FMT_GBRP16LE,     AV_PIX_FMT_GBRAP12LE,    \
    AV_PIX_FMT_GBRAP16LE

#define GRAY_FORMATS                            \
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9LE, AV_PIX_FMT_GRAY10LE, \
    AV_PIX_FMT_GRAY12LE, AV_PIX_FMT_GRAY14LE, AV_PIX_FMT_GRAY16LE

static const enum AVPixelFormat yuv_pix_fmts[] = { YUV_FORMATS, AV_PIX_FMT_NONE };
static const enum AVPixelFormat rgb_pix_fmts[] = { RGB_FORMATS, AV_PIX_FMT_NONE };
static const enum AVPixelFormat all_pix_fmts[] = { RGB_FORMATS, YUV_FORMATS, GRAY_FORMATS, AV_PIX_FMT_NONE };

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    const LutContext *s = ctx->priv;

    const enum AVPixelFormat *pix_fmts = s->is_rgb ? rgb_pix_fmts :
                                                     s->is_yuv ? yuv_pix_fmts :
                                                                 all_pix_fmts;
    return ff_set_common_formats_from_list2(ctx, cfg_in, cfg_out, pix_fmts);
}

/**
 * Clip value val in the minval - maxval range.
 */
static double clip(void *opaque, double val)
{
    LutContext *s = opaque;
    double minval = s->var_values[VAR_MINVAL];
    double maxval = s->var_values[VAR_MAXVAL];

    return av_clip(val, minval, maxval);
}

/**
 * Compute gamma correction for value val, assuming the minval-maxval
 * range, val is clipped to a value contained in the same interval.
 */
static double compute_gammaval(void *opaque, double gamma)
{
    LutContext *s = opaque;
    double val    = s->var_values[VAR_CLIPVAL];
    double minval = s->var_values[VAR_MINVAL];
    double maxval = s->var_values[VAR_MAXVAL];

    return pow((val-minval)/(maxval-minval), gamma) * (maxval-minval)+minval;
}

/**
 * Compute ITU Rec.709 gamma correction of value val.
 */
static double compute_gammaval709(void *opaque, double gamma)
{
    LutContext *s = opaque;
    double val    = s->var_values[VAR_CLIPVAL];
    double minval = s->var_values[VAR_MINVAL];
    double maxval = s->var_values[VAR_MAXVAL];
    double level = (val - minval) / (maxval - minval);
    level = level < 0.018 ? 4.5 * level
                          : 1.099 * pow(level, 1.0 / gamma) - 0.099;
    return level * (maxval - minval) + minval;
}

static double (* const funcs1[])(void *, double) = {
    clip,
    compute_gammaval,
    compute_gammaval709,
    NULL
};

static const char * const funcs1_names[] = {
    "clip",
    "gammaval",
    "gammaval709",
    NULL
};

static int config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    LutContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    uint8_t rgba_map[4]; /* component index -> RGBA color index map */
    int min[4], max[4];
    int val, color, ret;

    s->hsub = desc->log2_chroma_w;
    s->vsub = desc->log2_chroma_h;

    s->var_values[VAR_W] = inlink->w;
    s->var_values[VAR_H] = inlink->h;
    s->is_16bit = desc->comp[0].depth > 8;

    switch (inlink->format) {
    case AV_PIX_FMT_YUV410P:
    case AV_PIX_FMT_YUV411P:
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUV440P:
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUVA420P:
    case AV_PIX_FMT_YUVA422P:
    case AV_PIX_FMT_YUVA444P:
    case AV_PIX_FMT_YUV420P9LE:
    case AV_PIX_FMT_YUV422P9LE:
    case AV_PIX_FMT_YUV444P9LE:
    case AV_PIX_FMT_YUVA420P9LE:
    case AV_PIX_FMT_YUVA422P9LE:
    case AV_PIX_FMT_YUVA444P9LE:
    case AV_PIX_FMT_YUV420P10LE:
    case AV_PIX_FMT_YUV422P10LE:
    case AV_PIX_FMT_YUV440P10LE:
    case AV_PIX_FMT_YUV444P10LE:
    case AV_PIX_FMT_YUVA420P10LE:
    case AV_PIX_FMT_YUVA422P10LE:
    case AV_PIX_FMT_YUVA444P10LE:
    case AV_PIX_FMT_YUV420P12LE:
    case AV_PIX_FMT_YUV422P12LE:
    case AV_PIX_FMT_YUV440P12LE:
    case AV_PIX_FMT_YUV444P12LE:
    case AV_PIX_FMT_YUV420P14LE:
    case AV_PIX_FMT_YUV422P14LE:
    case AV_PIX_FMT_YUV444P14LE:
    case AV_PIX_FMT_YUV420P16LE:
    case AV_PIX_FMT_YUV422P16LE:
    case AV_PIX_FMT_YUV444P16LE:
    case AV_PIX_FMT_YUVA420P16LE:
    case AV_PIX_FMT_YUVA422P16LE:
    case AV_PIX_FMT_YUVA444P16LE:
        min[Y] = 16 * (1 << (desc->comp[0].depth - 8));
        min[U] = 16 * (1 << (desc->comp[1].depth - 8));
        min[V] = 16 * (1 << (desc->comp[2].depth - 8));
        min[A] = 0;
        max[Y] = 235 * (1 << (desc->comp[0].depth - 8));
        max[U] = 240 * (1 << (desc->comp[1].depth - 8));
        max[V] = 240 * (1 << (desc->comp[2].depth - 8));
        max[A] = (1 << desc->comp[0].depth) - 1;
        break;
    case AV_PIX_FMT_RGB48LE:
    case AV_PIX_FMT_RGBA64LE:
        min[0] = min[1] = min[2] = min[3] = 0;
        max[0] = max[1] = max[2] = max[3] = 65535;
        break;
    default:
        min[0] = min[1] = min[2] = min[3] = 0;
        max[0] = max[1] = max[2] = max[3] = 255 * (1 << (desc->comp[0].depth - 8));
    }

    s->is_yuv = s->is_rgb = 0;
    s->is_planar = desc->flags & AV_PIX_FMT_FLAG_PLANAR;
    if      (ff_fmt_is_in(inlink->format, yuv_pix_fmts)) s->is_yuv = 1;
    else if (ff_fmt_is_in(inlink->format, rgb_pix_fmts)) s->is_rgb = 1;

    if (s->is_rgb) {
        ff_fill_rgba_map(rgba_map, inlink->format);
        s->step = av_get_bits_per_pixel(desc) >> 3;
        if (s->is_16bit) {
            s->step = s->step >> 1;
        }
    }

    for (color = 0; color < desc->nb_components; color++) {
        double res;
        int comp = s->is_rgb ? rgba_map[color] : color;

        /* create the parsed expression */
        av_expr_free(s->comp_expr[color]);
        s->comp_expr[color] = NULL;
        ret = av_expr_parse(&s->comp_expr[color], s->comp_expr_str[color],
                            var_names, funcs1_names, funcs1, NULL, NULL, 0, ctx);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Error when parsing the expression '%s' for the component %d and color %d.\n",
                   s->comp_expr_str[comp], comp, color);
            return AVERROR(EINVAL);
        }

        /* compute the lut */
        s->var_values[VAR_MAXVAL] = max[color];
        s->var_values[VAR_MINVAL] = min[color];

        for (val = 0; val < FF_ARRAY_ELEMS(s->lut[comp]); val++) {
            s->var_values[VAR_VAL] = val;
            s->var_values[VAR_CLIPVAL] = av_clip(val, min[color], max[color]);
            s->var_values[VAR_NEGVAL] =
                av_clip(min[color] + max[color] - s->var_values[VAR_VAL],
                        min[color], max[color]);

            res = av_expr_eval(s->comp_expr[color], s->var_values, s);
            if (isnan(res)) {
                av_log(ctx, AV_LOG_ERROR,
                       "Error when evaluating the expression '%s' for the value %d for the component %d.\n",
                       s->comp_expr_str[color], val, comp);
                return AVERROR(EINVAL);
            }
            s->lut[comp][val] = av_clip((int)res, 0, max[A]);
            av_log(ctx, AV_LOG_DEBUG, "val[%d][%d] = %d\n", comp, val, s->lut[comp][val]);
        }
    }

    return 0;
}

struct thread_data {
    AVFrame *in;
    AVFrame *out;

    int w;
    int h;
};

#define LOAD_PACKED_COMMON\
    LutContext *s = ctx->priv;\
    const struct thread_data *td = arg;\
\
    int i, j;\
    const int w = td->w;\
    const int h = td->h;\
    AVFrame *in = td->in;\
    AVFrame *out = td->out;\
    const uint16_t (*tab)[256*256] = (const uint16_t (*)[256*256])s->lut;\
    const int step = s->step;\
\
    const int slice_start = (h *  jobnr   ) / nb_jobs;\
    const int slice_end   = (h * (jobnr+1)) / nb_jobs;\

/* packed, 16-bit */
static int lut_packed_16bits(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    LOAD_PACKED_COMMON

    uint16_t *inrow, *outrow, *inrow0, *outrow0;
    const int in_linesize  =  in->linesize[0] / 2;
    const int out_linesize = out->linesize[0] / 2;
    inrow0  = (uint16_t *)in ->data[0];
    outrow0 = (uint16_t *)out->data[0];

    for (i = slice_start; i < slice_end; i++) {
        inrow  = inrow0 + i * in_linesize;
        outrow = outrow0 + i * out_linesize;
        for (j = 0; j < w; j++) {

            switch (step) {
#if HAVE_BIGENDIAN
            case 4:  outrow[3] = av_bswap16(tab[3][av_bswap16(inrow[3])]); // Fall-through
            case 3:  outrow[2] = av_bswap16(tab[2][av_bswap16(inrow[2])]); // Fall-through
            case 2:  outrow[1] = av_bswap16(tab[1][av_bswap16(inrow[1])]); // Fall-through
            default: outrow[0] = av_bswap16(tab[0][av_bswap16(inrow[0])]);
#else
            case 4:  outrow[3] = tab[3][inrow[3]]; // Fall-through
            case 3:  outrow[2] = tab[2][inrow[2]]; // Fall-through
            case 2:  outrow[1] = tab[1][inrow[1]]; // Fall-through
            default: outrow[0] = tab[0][inrow[0]];
#endif
            }
            outrow += step;
            inrow  += step;
        }
    }

    return 0;
}

/* packed, 8-bit */
static int lut_packed_8bits(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    LOAD_PACKED_COMMON

    uint8_t *inrow, *outrow, *inrow0, *outrow0;
    const int in_linesize  =  in->linesize[0];
    const int out_linesize = out->linesize[0];
    inrow0  = in ->data[0];
    outrow0 = out->data[0];

    for (i = slice_start; i < slice_end; i++) {
        inrow  = inrow0 + i * in_linesize;
        outrow = outrow0 + i * out_linesize;
        for (j = 0; j < w; j++) {
            switch (step) {
            case 4:  outrow[3] = tab[3][inrow[3]]; // Fall-through
            case 3:  outrow[2] = tab[2][inrow[2]]; // Fall-through
            case 2:  outrow[1] = tab[1][inrow[1]]; // Fall-through
            default: outrow[0] = tab[0][inrow[0]];
            }
            outrow += step;
            inrow  += step;
        }
    }

    return 0;
}

#define LOAD_PLANAR_COMMON\
    LutContext *s = ctx->priv;\
    const struct thread_data *td = arg;\
    int i, j, plane;\
    AVFrame *in = td->in;\
    AVFrame *out = td->out;\

#define PLANAR_COMMON\
        int vsub = plane == 1 || plane == 2 ? s->vsub : 0;\
        int hsub = plane == 1 || plane == 2 ? s->hsub : 0;\
        int h = AV_CEIL_RSHIFT(td->h, vsub);\
        int w = AV_CEIL_RSHIFT(td->w, hsub);\
        const uint16_t *tab = s->lut[plane];\
\
        const int slice_start = (h *  jobnr   ) / nb_jobs;\
        const int slice_end   = (h * (jobnr+1)) / nb_jobs;\

/* planar >8 bit depth */
static int lut_planar_16bits(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    LOAD_PLANAR_COMMON

    uint16_t *inrow, *outrow;

    for (plane = 0; plane < 4 && in->data[plane] && in->linesize[plane]; plane++) {
        PLANAR_COMMON

        const int in_linesize  =  in->linesize[plane] / 2;
        const int out_linesize = out->linesize[plane] / 2;

        inrow  = (uint16_t *)in ->data[plane] + slice_start * in_linesize;
        outrow = (uint16_t *)out->data[plane] + slice_start * out_linesize;

        for (i = slice_start; i < slice_end; i++) {
            for (j = 0; j < w; j++) {
#if HAVE_BIGENDIAN
                outrow[j] = av_bswap16(tab[av_bswap16(inrow[j])]);
#else
                outrow[j] = tab[inrow[j]];
#endif
            }
            inrow  += in_linesize;
            outrow += out_linesize;
        }
    }

    return 0;
}

/* planar 8bit depth */
static int lut_planar_8bits(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    LOAD_PLANAR_COMMON

    uint8_t *inrow, *outrow;

    for (plane = 0; plane < 4 && in->data[plane] && in->linesize[plane]; plane++) {
        PLANAR_COMMON

        const int in_linesize  =  in->linesize[plane];
        const int out_linesize = out->linesize[plane];

        inrow  = in ->data[plane] + slice_start * in_linesize;
        outrow = out->data[plane] + slice_start * out_linesize;

        for (i = slice_start; i < slice_end; i++) {
            for (j = 0; j < w; j++)
                outrow[j] = tab[inrow[j]];
            inrow  += in_linesize;
            outrow += out_linesize;
        }
    }

    return 0;
}

#define PACKED_THREAD_DATA\
 struct thread_data td = {\
            .in  = in,\
            .out = out,\
            .w   = inlink->w,\
            .h   = in->height,\
        };\

#define PLANAR_THREAD_DATA\
 struct thread_data td = {\
            .in  = in,\
            .out = out,\
            .w   = inlink->w,\
            .h   = inlink->h,\
        };\

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    LutContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    int direct = 0;

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

    if (s->is_rgb && s->is_16bit && !s->is_planar) {
        /* packed, 16-bit */
        PACKED_THREAD_DATA
        ff_filter_execute(ctx, lut_packed_16bits, &td, NULL,
                          FFMIN(in->height, ff_filter_get_nb_threads(ctx)));
    } else if (s->is_rgb && !s->is_planar) {
        /* packed 8 bits */
        PACKED_THREAD_DATA
        ff_filter_execute(ctx, lut_packed_8bits, &td, NULL,
                          FFMIN(in->height, ff_filter_get_nb_threads(ctx)));
    } else if (s->is_16bit) {
        /* planar >8 bit depth */
        PLANAR_THREAD_DATA
        ff_filter_execute(ctx, lut_planar_16bits, &td, NULL,
                          FFMIN(in->height, ff_filter_get_nb_threads(ctx)));
    } else {
        /* planar 8bit depth */
        PLANAR_THREAD_DATA
        ff_filter_execute(ctx, lut_planar_8bits, &td, NULL,
                          FFMIN(in->height, ff_filter_get_nb_threads(ctx)));
    }

    if (!direct)
        av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    int ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);

    if (ret < 0)
        return ret;

    return config_props(ctx->inputs[0]);
}

static const AVFilterPad inputs[] = {
    { .name         = "default",
      .type         = AVMEDIA_TYPE_VIDEO,
      .filter_frame = filter_frame,
      .config_props = config_props,
    },
};

#define DEFINE_LUT_FILTER(name_, description_, priv_class_)             \
    const AVFilter ff_vf_##name_ = {                                    \
        .name          = #name_,                                        \
        .description   = NULL_IF_CONFIG_SMALL(description_),            \
        .priv_class    = &priv_class_ ## _class,                        \
        .priv_size     = sizeof(LutContext),                            \
        .init          = name_##_init,                                  \
        .uninit        = uninit,                                        \
        FILTER_INPUTS(inputs),                                          \
        FILTER_OUTPUTS(ff_video_default_filterpad),                     \
        FILTER_QUERY_FUNC2(query_formats),                              \
        .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC |       \
                         AVFILTER_FLAG_SLICE_THREADS,                   \
        .process_command = process_command,                             \
    }

AVFILTER_DEFINE_CLASS_EXT(lut, "lut/lutyuv/lutrgb", options);

#if CONFIG_LUT_FILTER

#define lut_init NULL
DEFINE_LUT_FILTER(lut, "Compute and apply a lookup table to the RGB/YUV input video.",
                  lut);
#undef lut_init
#endif

#if CONFIG_LUTYUV_FILTER

static av_cold int lutyuv_init(AVFilterContext *ctx)
{
    LutContext *s = ctx->priv;

    s->is_yuv = 1;

    return 0;
}

DEFINE_LUT_FILTER(lutyuv, "Compute and apply a lookup table to the YUV input video.",
                  lut);
#endif

#if CONFIG_LUTRGB_FILTER

static av_cold int lutrgb_init(AVFilterContext *ctx)
{
    LutContext *s = ctx->priv;

    s->is_rgb = 1;

    return 0;
}

DEFINE_LUT_FILTER(lutrgb, "Compute and apply a lookup table to the RGB input video.",
                  lut);
#endif
