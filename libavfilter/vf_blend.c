/*
 * Copyright (c) 2013 Paul B Mahol
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

#include "config_components.h"

#include "libavutil/eval.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixfmt.h"
#include "avfilter.h"
#include "filters.h"
#include "framesync.h"
#include "vf_blend_init.h"
#include "video.h"
#include "blend.h"

#define TOP    0
#define BOTTOM 1

typedef struct BlendContext {
    const AVClass *class;
    FFFrameSync fs;
    int hsub, vsub;             ///< chroma subsampling values
    int nb_planes;
    char *all_expr;
    enum BlendMode all_mode;
    double all_opacity;

    int depth;
    FilterParams params[4];
    int tblend;
    AVFrame *prev_frame;        /* only used with tblend */
    int nb_threads;
} BlendContext;

static const char *const var_names[] = {   "X",   "Y",   "W",   "H",   "SW",   "SH",   "T",   "N",   "A",   "B",   "TOP",   "BOTTOM",        NULL };
enum                                   { VAR_X, VAR_Y, VAR_W, VAR_H, VAR_SW, VAR_SH, VAR_T, VAR_N, VAR_A, VAR_B, VAR_TOP, VAR_BOTTOM, VAR_VARS_NB };

typedef struct ThreadData {
    const AVFrame *top, *bottom;
    AVFrame *dst;
    AVFilterLink *inlink;
    int plane;
    int w, h;
    FilterParams *param;
} ThreadData;

#define OFFSET(x) offsetof(BlendContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption blend_options[] = {
    { "c0_mode", "set component #0 blend mode", OFFSET(params[0].mode), AV_OPT_TYPE_INT, {.i64=0}, 0, BLEND_NB-1, FLAGS, .unit = "mode" },
    { "c1_mode", "set component #1 blend mode", OFFSET(params[1].mode), AV_OPT_TYPE_INT, {.i64=0}, 0, BLEND_NB-1, FLAGS, .unit = "mode" },
    { "c2_mode", "set component #2 blend mode", OFFSET(params[2].mode), AV_OPT_TYPE_INT, {.i64=0}, 0, BLEND_NB-1, FLAGS, .unit = "mode" },
    { "c3_mode", "set component #3 blend mode", OFFSET(params[3].mode), AV_OPT_TYPE_INT, {.i64=0}, 0, BLEND_NB-1, FLAGS, .unit = "mode" },
    { "all_mode", "set blend mode for all components", OFFSET(all_mode), AV_OPT_TYPE_INT, {.i64=-1},-1, BLEND_NB-1, FLAGS, .unit = "mode" },
    { "addition",   "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_ADDITION},   0, 0, FLAGS, .unit = "mode" },
    { "addition128","", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_GRAINMERGE}, 0, 0, FLAGS, .unit = "mode" },
    { "grainmerge", "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_GRAINMERGE}, 0, 0, FLAGS, .unit = "mode" },
    { "and",        "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_AND},        0, 0, FLAGS, .unit = "mode" },
    { "average",    "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_AVERAGE},    0, 0, FLAGS, .unit = "mode" },
    { "burn",       "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_BURN},       0, 0, FLAGS, .unit = "mode" },
    { "darken",     "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_DARKEN},     0, 0, FLAGS, .unit = "mode" },
    { "difference", "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_DIFFERENCE}, 0, 0, FLAGS, .unit = "mode" },
    { "difference128", "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_GRAINEXTRACT}, 0, 0, FLAGS, .unit = "mode" },
    { "grainextract", "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_GRAINEXTRACT}, 0, 0, FLAGS, .unit = "mode" },
    { "divide",     "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_DIVIDE},     0, 0, FLAGS, .unit = "mode" },
    { "dodge",      "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_DODGE},      0, 0, FLAGS, .unit = "mode" },
    { "exclusion",  "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_EXCLUSION},  0, 0, FLAGS, .unit = "mode" },
    { "extremity",  "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_EXTREMITY},  0, 0, FLAGS, .unit = "mode" },
    { "freeze",     "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_FREEZE},     0, 0, FLAGS, .unit = "mode" },
    { "glow",       "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_GLOW},       0, 0, FLAGS, .unit = "mode" },
    { "hardlight",  "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_HARDLIGHT},  0, 0, FLAGS, .unit = "mode" },
    { "hardmix",    "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_HARDMIX},    0, 0, FLAGS, .unit = "mode" },
    { "heat",       "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_HEAT},       0, 0, FLAGS, .unit = "mode" },
    { "lighten",    "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_LIGHTEN},    0, 0, FLAGS, .unit = "mode" },
    { "linearlight","", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_LINEARLIGHT},0, 0, FLAGS, .unit = "mode" },
    { "multiply",   "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_MULTIPLY},   0, 0, FLAGS, .unit = "mode" },
    { "multiply128","", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_MULTIPLY128},0, 0, FLAGS, .unit = "mode" },
    { "negation",   "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_NEGATION},   0, 0, FLAGS, .unit = "mode" },
    { "normal",     "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_NORMAL},     0, 0, FLAGS, .unit = "mode" },
    { "or",         "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_OR},         0, 0, FLAGS, .unit = "mode" },
    { "overlay",    "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_OVERLAY},    0, 0, FLAGS, .unit = "mode" },
    { "phoenix",    "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_PHOENIX},    0, 0, FLAGS, .unit = "mode" },
    { "pinlight",   "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_PINLIGHT},   0, 0, FLAGS, .unit = "mode" },
    { "reflect",    "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_REFLECT},    0, 0, FLAGS, .unit = "mode" },
    { "screen",     "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_SCREEN},     0, 0, FLAGS, .unit = "mode" },
    { "softlight",  "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_SOFTLIGHT},  0, 0, FLAGS, .unit = "mode" },
    { "subtract",   "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_SUBTRACT},   0, 0, FLAGS, .unit = "mode" },
    { "vividlight", "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_VIVIDLIGHT}, 0, 0, FLAGS, .unit = "mode" },
    { "xor",        "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_XOR},        0, 0, FLAGS, .unit = "mode" },
    { "softdifference","", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_SOFTDIFFERENCE}, 0, 0, FLAGS, .unit = "mode" },
    { "geometric",  "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_GEOMETRIC},  0, 0, FLAGS, .unit = "mode" },
    { "harmonic",   "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_HARMONIC},   0, 0, FLAGS, .unit = "mode" },
    { "bleach",     "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_BLEACH},     0, 0, FLAGS, .unit = "mode" },
    { "stain",      "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_STAIN},      0, 0, FLAGS, .unit = "mode" },
    { "interpolate","", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_INTERPOLATE},0, 0, FLAGS, .unit = "mode" },
    { "hardoverlay","", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_HARDOVERLAY},0, 0, FLAGS, .unit = "mode" },
    { "c0_expr",  "set color component #0 expression", OFFSET(params[0].expr_str), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "c1_expr",  "set color component #1 expression", OFFSET(params[1].expr_str), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "c2_expr",  "set color component #2 expression", OFFSET(params[2].expr_str), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "c3_expr",  "set color component #3 expression", OFFSET(params[3].expr_str), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "all_expr", "set expression for all color components", OFFSET(all_expr), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "c0_opacity",  "set color component #0 opacity", OFFSET(params[0].opacity), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS },
    { "c1_opacity",  "set color component #1 opacity", OFFSET(params[1].opacity), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS },
    { "c2_opacity",  "set color component #2 opacity", OFFSET(params[2].opacity), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS },
    { "c3_opacity",  "set color component #3 opacity", OFFSET(params[3].opacity), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS },
    { "all_opacity", "set opacity for all color components", OFFSET(all_opacity), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS },
    { NULL }
};

FRAMESYNC_DEFINE_CLASS(blend, BlendContext, fs);

#define DEFINE_BLEND_EXPR(type, name, div)                                     \
static void blend_expr_## name(const uint8_t *_top, ptrdiff_t top_linesize,          \
                               const uint8_t *_bottom, ptrdiff_t bottom_linesize,    \
                               uint8_t *_dst, ptrdiff_t dst_linesize,                \
                               ptrdiff_t width, ptrdiff_t height,              \
                               FilterParams *param, SliceParams *sliceparam)   \
{                                                                              \
    const type *top = (const type*)_top;                                       \
    const type *bottom = (const type*)_bottom;                                 \
    double *values = sliceparam->values;                                       \
    int starty = sliceparam->starty;                                           \
    type *dst = (type*)_dst;                                                   \
    AVExpr *e = sliceparam->e;                                                 \
    int y, x;                                                                  \
    dst_linesize /= div;                                                       \
    top_linesize /= div;                                                       \
    bottom_linesize /= div;                                                    \
                                                                               \
    for (y = 0; y < height; y++) {                                             \
        values[VAR_Y] = y + starty;                                            \
        for (x = 0; x < width; x++) {                                          \
            values[VAR_X]      = x;                                            \
            values[VAR_TOP]    = values[VAR_A] = top[x];                       \
            values[VAR_BOTTOM] = values[VAR_B] = bottom[x];                    \
            dst[x] = av_expr_eval(e, values, NULL);                            \
        }                                                                      \
        dst    += dst_linesize;                                                \
        top    += top_linesize;                                                \
        bottom += bottom_linesize;                                             \
    }                                                                          \
}

DEFINE_BLEND_EXPR(uint8_t, 8bit, 1)
DEFINE_BLEND_EXPR(uint16_t, 16bit, 2)
DEFINE_BLEND_EXPR(float, 32bit, 4)

static int filter_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ThreadData *td = arg;
    int slice_start = (td->h *  jobnr   ) / nb_jobs;
    int slice_end   = (td->h * (jobnr+1)) / nb_jobs;
    int height      = slice_end - slice_start;
    const uint8_t *top    = td->top->data[td->plane];
    const uint8_t *bottom = td->bottom->data[td->plane];
    uint8_t *dst    = td->dst->data[td->plane];
    FilterLink *inl = ff_filter_link(td->inlink);
    double values[VAR_VARS_NB];
    SliceParams sliceparam = {.values = &values[0], .starty = slice_start, .e = td->param->e ? td->param->e[jobnr] : NULL};

    values[VAR_N]  = inl->frame_count_out;
    values[VAR_T]  = td->dst->pts == AV_NOPTS_VALUE ? NAN : td->dst->pts * av_q2d(td->inlink->time_base);
    values[VAR_W]  = td->w;
    values[VAR_H]  = td->h;
    values[VAR_SW] = td->w / (double)td->dst->width;
    values[VAR_SH] = td->h / (double)td->dst->height;

    td->param->blend(top + slice_start * td->top->linesize[td->plane],
                     td->top->linesize[td->plane],
                     bottom + slice_start * td->bottom->linesize[td->plane],
                     td->bottom->linesize[td->plane],
                     dst + slice_start * td->dst->linesize[td->plane],
                     td->dst->linesize[td->plane],
                     td->w, height, td->param, &sliceparam);
    return 0;
}

static AVFrame *blend_frame(AVFilterContext *ctx, AVFrame *top_buf,
                            const AVFrame *bottom_buf)
{
    BlendContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *dst_buf;
    int plane;

    dst_buf = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!dst_buf)
        return top_buf;

    if (av_frame_copy_props(dst_buf, top_buf) < 0) {
        av_frame_free(&dst_buf);
        return top_buf;
    }

    for (plane = 0; plane < s->nb_planes; plane++) {
        int hsub = plane == 1 || plane == 2 ? s->hsub : 0;
        int vsub = plane == 1 || plane == 2 ? s->vsub : 0;
        int outw = AV_CEIL_RSHIFT(dst_buf->width,  hsub);
        int outh = AV_CEIL_RSHIFT(dst_buf->height, vsub);
        FilterParams *param = &s->params[plane];
        ThreadData td = { .top = top_buf, .bottom = bottom_buf, .dst = dst_buf,
                          .w = outw, .h = outh, .param = param, .plane = plane,
                          .inlink = inlink };

        ff_filter_execute(ctx, filter_slice, &td, NULL,
                          FFMIN(outh, s->nb_threads));
    }

    if (!s->tblend)
        av_frame_free(&top_buf);

    return dst_buf;
}

static int blend_frame_for_dualinput(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    AVFrame *top_buf, *bottom_buf, *dst_buf;
    int ret;

    ret = ff_framesync_dualinput_get(fs, &top_buf, &bottom_buf);
    if (ret < 0)
        return ret;
    if (!bottom_buf)
        return ff_filter_frame(ctx->outputs[0], top_buf);
    dst_buf = blend_frame(ctx, top_buf, bottom_buf);
    return ff_filter_frame(ctx->outputs[0], dst_buf);
}

static av_cold int init(AVFilterContext *ctx)
{
    BlendContext *s = ctx->priv;

    s->tblend = !strcmp(ctx->filter->name, "tblend");
    s->nb_threads = ff_filter_get_nb_threads(ctx);

    s->fs.on_event = blend_frame_for_dualinput;
    return 0;
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA420P,
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ422P,AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ411P,
    AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP, AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
    AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA444P9, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GRAY9,
    AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10, AV_PIX_FMT_YUV440P10,
    AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA444P10,
    AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GRAY10,
    AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV440P12,
    AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA444P12,
    AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GRAY12,
    AV_PIX_FMT_YUV420P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV444P14, AV_PIX_FMT_GBRP14,
    AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_GBRP16, AV_PIX_FMT_GBRAP16, AV_PIX_FMT_GRAY16,
    AV_PIX_FMT_GBRPF32, AV_PIX_FMT_GBRAPF32, AV_PIX_FMT_GRAYF32,
    AV_PIX_FMT_NONE
};

static av_cold void uninit(AVFilterContext *ctx)
{
    BlendContext *s = ctx->priv;
    int i;

    ff_framesync_uninit(&s->fs);
    av_frame_free(&s->prev_frame);

    for (i = 0; i < FF_ARRAY_ELEMS(s->params); i++) {
        if (s->params[i].e) {
            for (int j = 0; j < s->nb_threads; j++)
                av_expr_free(s->params[i].e[j]);
            av_freep(&s->params[i].e);
        }
    }

}

static int config_params(AVFilterContext *ctx)
{
    BlendContext *s = ctx->priv;
    int ret;

    for (int plane = 0; plane < FF_ARRAY_ELEMS(s->params); plane++) {
        FilterParams *param = &s->params[plane];

        if (s->all_mode >= 0)
            param->mode = s->all_mode;
        if (s->all_opacity < 1)
            param->opacity = s->all_opacity;

        ff_blend_init(param, s->depth);

        if (s->all_expr && !param->expr_str) {
            param->expr_str = av_strdup(s->all_expr);
            if (!param->expr_str)
                return AVERROR(ENOMEM);
        }
        if (param->expr_str) {
            if (!param->e) {
                param->e = av_calloc(s->nb_threads, sizeof(*param->e));
                if (!param->e)
                    return AVERROR(ENOMEM);
            }
            for (int i = 0; i < s->nb_threads; i++) {
                av_expr_free(param->e[i]);
                param->e[i] = NULL;
                ret = av_expr_parse(&param->e[i], param->expr_str, var_names,
                                    NULL, NULL, NULL, NULL, 0, ctx);
                if (ret < 0)
                    return ret;
            }
            param->blend = s->depth > 8 ? s->depth > 16 ? blend_expr_32bit : blend_expr_16bit : blend_expr_8bit;
        }
    }

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    FilterLink *outl = ff_filter_link(outlink);
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *toplink = ctx->inputs[TOP];
    FilterLink *tl = ff_filter_link(toplink);
    BlendContext *s = ctx->priv;
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(toplink->format);
    int ret;

    if (!s->tblend) {
        AVFilterLink *bottomlink = ctx->inputs[BOTTOM];

        if (toplink->w != bottomlink->w || toplink->h != bottomlink->h) {
            av_log(ctx, AV_LOG_ERROR, "First input link %s parameters "
                   "(size %dx%d) do not match the corresponding "
                   "second input link %s parameters (size %dx%d)\n",
                   ctx->input_pads[TOP].name, toplink->w, toplink->h,
                   ctx->input_pads[BOTTOM].name, bottomlink->w, bottomlink->h);
            return AVERROR(EINVAL);
        }
    }

    outlink->w = toplink->w;
    outlink->h = toplink->h;
    outlink->time_base = toplink->time_base;
    outlink->sample_aspect_ratio = toplink->sample_aspect_ratio;
    outl->frame_rate = tl->frame_rate;

    s->hsub = pix_desc->log2_chroma_w;
    s->vsub = pix_desc->log2_chroma_h;

    s->depth = pix_desc->comp[0].depth;
    s->nb_planes = av_pix_fmt_count_planes(toplink->format);

    if (!s->tblend)
        if ((ret = ff_framesync_init_dualinput(&s->fs, ctx)) < 0)
            return ret;

    ret = config_params(ctx);
    if (ret < 0)
        return ret;

    if (s->tblend)
        return 0;

    ret = ff_framesync_configure(&s->fs);
    outlink->time_base = s->fs.time_base;

    return ret;
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    return config_params(ctx);
}

static const AVFilterPad blend_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

#if CONFIG_BLEND_FILTER

static int activate(AVFilterContext *ctx)
{
    BlendContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static const AVFilterPad blend_inputs[] = {
    {
        .name          = "top",
        .type          = AVMEDIA_TYPE_VIDEO,
    },{
        .name          = "bottom",
        .type          = AVMEDIA_TYPE_VIDEO,
    },
};

const FFFilter ff_vf_blend = {
    .p.name        = "blend",
    .p.description = NULL_IF_CONFIG_SMALL("Blend two video frames into each other."),
    .p.priv_class  = &blend_class,
    .p.flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL | AVFILTER_FLAG_SLICE_THREADS,
    .preinit       = blend_framesync_preinit,
    .init          = init,
    .uninit        = uninit,
    .priv_size     = sizeof(BlendContext),
    .activate      = activate,
    FILTER_INPUTS(blend_inputs),
    FILTER_OUTPUTS(blend_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .process_command = process_command,
};

#endif

#if CONFIG_TBLEND_FILTER

static int tblend_filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    BlendContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    if (s->prev_frame) {
        AVFrame *out;

        if (ctx->is_disabled)
            out = av_frame_clone(frame);
        else
            out = blend_frame(ctx, frame, s->prev_frame);
        av_frame_free(&s->prev_frame);
        s->prev_frame = frame;
        return ff_filter_frame(outlink, out);
    }
    s->prev_frame = frame;
    return 0;
}

AVFILTER_DEFINE_CLASS_EXT(tblend, "tblend", blend_options);

static const AVFilterPad tblend_inputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .filter_frame  = tblend_filter_frame,
    },
};

const FFFilter ff_vf_tblend = {
    .p.name        = "tblend",
    .p.description = NULL_IF_CONFIG_SMALL("Blend successive frames."),
    .p.priv_class  = &tblend_class,
    .p.flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL | AVFILTER_FLAG_SLICE_THREADS,
    .priv_size     = sizeof(BlendContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(tblend_inputs),
    FILTER_OUTPUTS(blend_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .process_command = process_command,
};

#endif
