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

#include "libavutil/imgutils.h"
#include "libavutil/eval.h"
#include "libavutil/opt.h"
#include "libavutil/pixfmt.h"
#include "avfilter.h"
#include "bufferqueue.h"
#include "formats.h"
#include "internal.h"
#include "dualinput.h"
#include "video.h"

#define TOP    0
#define BOTTOM 1

enum BlendMode {
    BLEND_UNSET = -1,
    BLEND_NORMAL,
    BLEND_ADDITION,
    BLEND_AND,
    BLEND_AVERAGE,
    BLEND_BURN,
    BLEND_DARKEN,
    BLEND_DIFFERENCE,
    BLEND_DIVIDE,
    BLEND_DODGE,
    BLEND_EXCLUSION,
    BLEND_HARDLIGHT,
    BLEND_LIGHTEN,
    BLEND_MULTIPLY,
    BLEND_NEGATION,
    BLEND_OR,
    BLEND_OVERLAY,
    BLEND_PHOENIX,
    BLEND_PINLIGHT,
    BLEND_REFLECT,
    BLEND_SCREEN,
    BLEND_SOFTLIGHT,
    BLEND_SUBTRACT,
    BLEND_VIVIDLIGHT,
    BLEND_XOR,
    BLEND_NB
};

static const char *const var_names[] = {   "X",   "Y",   "W",   "H",   "SW",   "SH",   "T",   "N",   "A",   "B",   "TOP",   "BOTTOM",        NULL };
enum                                   { VAR_X, VAR_Y, VAR_W, VAR_H, VAR_SW, VAR_SH, VAR_T, VAR_N, VAR_A, VAR_B, VAR_TOP, VAR_BOTTOM, VAR_VARS_NB };

typedef struct FilterParams {
    enum BlendMode mode;
    double opacity;
    AVExpr *e;
    char *expr_str;
    void (*blend)(const uint8_t *top, int top_linesize,
                  const uint8_t *bottom, int bottom_linesize,
                  uint8_t *dst, int dst_linesize,
                  int width, int start, int end,
                  struct FilterParams *param, double *values);
} FilterParams;

typedef struct ThreadData {
    const AVFrame *top, *bottom;
    AVFrame *dst;
    AVFilterLink *inlink;
    int plane;
    int w, h;
    FilterParams *param;
} ThreadData;

typedef struct {
    const AVClass *class;
    FFDualInputContext dinput;
    int hsub, vsub;             ///< chroma subsampling values
    int nb_planes;
    char *all_expr;
    enum BlendMode all_mode;
    double all_opacity;

    FilterParams params[4];
} BlendContext;

#define OFFSET(x) offsetof(BlendContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption blend_options[] = {
    { "c0_mode", "set component #0 blend mode", OFFSET(params[0].mode), AV_OPT_TYPE_INT, {.i64=0}, 0, BLEND_NB-1, FLAGS, "mode"},
    { "c1_mode", "set component #1 blend mode", OFFSET(params[1].mode), AV_OPT_TYPE_INT, {.i64=0}, 0, BLEND_NB-1, FLAGS, "mode"},
    { "c2_mode", "set component #2 blend mode", OFFSET(params[2].mode), AV_OPT_TYPE_INT, {.i64=0}, 0, BLEND_NB-1, FLAGS, "mode"},
    { "c3_mode", "set component #3 blend mode", OFFSET(params[3].mode), AV_OPT_TYPE_INT, {.i64=0}, 0, BLEND_NB-1, FLAGS, "mode"},
    { "all_mode", "set blend mode for all components", OFFSET(all_mode), AV_OPT_TYPE_INT, {.i64=-1},-1, BLEND_NB-1, FLAGS, "mode"},
    { "addition",   "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_ADDITION},   0, 0, FLAGS, "mode" },
    { "and",        "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_AND},        0, 0, FLAGS, "mode" },
    { "average",    "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_AVERAGE},    0, 0, FLAGS, "mode" },
    { "burn",       "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_BURN},       0, 0, FLAGS, "mode" },
    { "darken",     "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_DARKEN},     0, 0, FLAGS, "mode" },
    { "difference", "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_DIFFERENCE}, 0, 0, FLAGS, "mode" },
    { "divide",     "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_DIVIDE},     0, 0, FLAGS, "mode" },
    { "dodge",      "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_DODGE},      0, 0, FLAGS, "mode" },
    { "exclusion",  "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_EXCLUSION},  0, 0, FLAGS, "mode" },
    { "hardlight",  "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_HARDLIGHT},  0, 0, FLAGS, "mode" },
    { "lighten",    "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_LIGHTEN},    0, 0, FLAGS, "mode" },
    { "multiply",   "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_MULTIPLY},   0, 0, FLAGS, "mode" },
    { "negation",   "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_NEGATION},   0, 0, FLAGS, "mode" },
    { "normal",     "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_NORMAL},     0, 0, FLAGS, "mode" },
    { "or",         "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_OR},         0, 0, FLAGS, "mode" },
    { "overlay",    "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_OVERLAY},    0, 0, FLAGS, "mode" },
    { "phoenix",    "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_PHOENIX},    0, 0, FLAGS, "mode" },
    { "pinlight",   "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_PINLIGHT},   0, 0, FLAGS, "mode" },
    { "reflect",    "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_REFLECT},    0, 0, FLAGS, "mode" },
    { "screen",     "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_SCREEN},     0, 0, FLAGS, "mode" },
    { "softlight",  "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_SOFTLIGHT},  0, 0, FLAGS, "mode" },
    { "subtract",   "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_SUBTRACT},   0, 0, FLAGS, "mode" },
    { "vividlight", "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_VIVIDLIGHT}, 0, 0, FLAGS, "mode" },
    { "xor",        "", 0, AV_OPT_TYPE_CONST, {.i64=BLEND_XOR},        0, 0, FLAGS, "mode" },
    { "c0_expr",  "set color component #0 expression", OFFSET(params[0].expr_str), AV_OPT_TYPE_STRING, {.str=NULL}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "c1_expr",  "set color component #1 expression", OFFSET(params[1].expr_str), AV_OPT_TYPE_STRING, {.str=NULL}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "c2_expr",  "set color component #2 expression", OFFSET(params[2].expr_str), AV_OPT_TYPE_STRING, {.str=NULL}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "c3_expr",  "set color component #3 expression", OFFSET(params[3].expr_str), AV_OPT_TYPE_STRING, {.str=NULL}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "all_expr", "set expression for all color components", OFFSET(all_expr), AV_OPT_TYPE_STRING, {.str=NULL}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "c0_opacity",  "set color component #0 opacity", OFFSET(params[0].opacity), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS },
    { "c1_opacity",  "set color component #1 opacity", OFFSET(params[1].opacity), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS },
    { "c2_opacity",  "set color component #2 opacity", OFFSET(params[2].opacity), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS },
    { "c3_opacity",  "set color component #3 opacity", OFFSET(params[3].opacity), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS },
    { "all_opacity", "set opacity for all color components", OFFSET(all_opacity), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    { "shortest",    "force termination when the shortest input terminates", OFFSET(dinput.shortest), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS },
    { "repeatlast",  "repeat last bottom frame", OFFSET(dinput.repeatlast), AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(blend);

static void blend_normal(const uint8_t *top, int top_linesize,
                         const uint8_t *bottom, int bottom_linesize,
                         uint8_t *dst, int dst_linesize,
                         int width, int start, int end,
                         FilterParams *param, double *values)
{
    av_image_copy_plane(dst, dst_linesize, top, top_linesize, width, end - start);
}

#define DEFINE_BLEND(name, expr)                                      \
static void blend_## name(const uint8_t *top, int top_linesize,       \
                          const uint8_t *bottom, int bottom_linesize, \
                          uint8_t *dst, int dst_linesize,             \
                          int width, int start, int end,              \
                          FilterParams *param, double *values)        \
{                                                                     \
    double opacity = param->opacity;                                  \
    int i, j;                                                         \
                                                                      \
    for (i = start; i < end; i++) {                                   \
        for (j = 0; j < width; j++) {                                 \
            dst[j] = top[j] + ((expr) - top[j]) * opacity;            \
        }                                                             \
        dst    += dst_linesize;                                       \
        top    += top_linesize;                                       \
        bottom += bottom_linesize;                                    \
    }                                                                 \
}

#define A top[j]
#define B bottom[j]

#define MULTIPLY(x, a, b) (x * ((a * b) / 255))
#define SCREEN(x, a, b)   (255 - x * ((255 - a) * (255 - b) / 255))
#define BURN(a, b)        ((a == 0) ? a : FFMAX(0, 255 - ((255 - b) << 8) / a))
#define DODGE(a, b)       ((a == 255) ? a : FFMIN(255, ((b << 8) / (255 - a))))

DEFINE_BLEND(addition,   FFMIN(255, A + B))
DEFINE_BLEND(average,    (A + B) / 2)
DEFINE_BLEND(subtract,   FFMAX(0, A - B))
DEFINE_BLEND(multiply,   MULTIPLY(1, A, B))
DEFINE_BLEND(negation,   255 - FFABS(255 - A - B))
DEFINE_BLEND(difference, FFABS(A - B))
DEFINE_BLEND(screen,     SCREEN(1, A, B))
DEFINE_BLEND(overlay,    (A < 128) ? MULTIPLY(2, A, B) : SCREEN(2, A, B))
DEFINE_BLEND(hardlight,  (B < 128) ? MULTIPLY(2, B, A) : SCREEN(2, B, A))
DEFINE_BLEND(darken,     FFMIN(A, B))
DEFINE_BLEND(lighten,    FFMAX(A, B))
DEFINE_BLEND(divide,     ((float)A / ((float)B) * 255))
DEFINE_BLEND(dodge,      DODGE(A, B))
DEFINE_BLEND(burn,       BURN(A, B))
DEFINE_BLEND(softlight,  (A > 127) ? B + (255 - B) * (A - 127.5) / 127.5 * (0.5 - FFABS(B - 127.5) / 255): B - B * ((127.5 - A) / 127.5) * (0.5 - FFABS(B - 127.5)/255))
DEFINE_BLEND(exclusion,  A + B - 2 * A * B / 255)
DEFINE_BLEND(pinlight,   (B < 128) ? FFMIN(A, 2 * B) : FFMAX(A, 2 * (B - 128)))
DEFINE_BLEND(phoenix,    FFMIN(A, B) - FFMAX(A, B) + 255)
DEFINE_BLEND(reflect,    (B == 255) ? B : FFMIN(255, (A * A / (255 - B))))
DEFINE_BLEND(and,        A & B)
DEFINE_BLEND(or,         A | B)
DEFINE_BLEND(xor,        A ^ B)
DEFINE_BLEND(vividlight, (B < 128) ? BURN(A, 2 * B) : DODGE(A, 2 * (B - 128)))

static void blend_expr(const uint8_t *top, int top_linesize,
                       const uint8_t *bottom, int bottom_linesize,
                       uint8_t *dst, int dst_linesize,
                       int width, int start, int end,
                       FilterParams *param, double *values)
{
    AVExpr *e = param->e;
    int y, x;

    for (y = start; y < end; y++) {
        values[VAR_Y] = y;
        for (x = 0; x < width; x++) {
            values[VAR_X]      = x;
            values[VAR_TOP]    = values[VAR_A] = top[x];
            values[VAR_BOTTOM] = values[VAR_B] = bottom[x];
            dst[x] = av_expr_eval(e, values, NULL);
        }
        dst    += dst_linesize;
        top    += top_linesize;
        bottom += bottom_linesize;
    }
}

static int filter_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ThreadData *td = arg;
    int slice_start = (td->h *  jobnr   ) / nb_jobs;
    int slice_end   = (td->h * (jobnr+1)) / nb_jobs;
    const uint8_t *top    = td->top->data[td->plane];
    const uint8_t *bottom = td->bottom->data[td->plane];
    uint8_t *dst    = td->dst->data[td->plane];
    double values[VAR_VARS_NB];

    values[VAR_N]  = td->inlink->frame_count;
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
                     td->w, slice_start, slice_end, td->param, &values[0]);
    return 0;
}

static AVFrame *blend_frame(AVFilterContext *ctx, AVFrame *top_buf,
                            const AVFrame *bottom_buf)
{
    BlendContext *b = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *dst_buf;
    int plane;

    dst_buf = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!dst_buf)
        return top_buf;
    av_frame_copy_props(dst_buf, top_buf);

    for (plane = 0; plane < b->nb_planes; plane++) {
        int hsub = plane == 1 || plane == 2 ? b->hsub : 0;
        int vsub = plane == 1 || plane == 2 ? b->vsub : 0;
        int outw = FF_CEIL_RSHIFT(dst_buf->width,  hsub);
        int outh = FF_CEIL_RSHIFT(dst_buf->height, vsub);
        FilterParams *param = &b->params[plane];
        ThreadData td = { .top = top_buf, .bottom = bottom_buf, .dst = dst_buf,
                          .w = outw, .h = outh, .param = param, .plane = plane,
                          .inlink = inlink };

        ctx->internal->execute(ctx, filter_slice, &td, NULL, FFMIN(outh, ctx->graph->nb_threads));
    }

    av_frame_free(&top_buf);

    return dst_buf;
}

static av_cold int init(AVFilterContext *ctx)
{
    BlendContext *b = ctx->priv;
    int ret, plane;

    for (plane = 0; plane < FF_ARRAY_ELEMS(b->params); plane++) {
        FilterParams *param = &b->params[plane];

        if (b->all_mode >= 0)
            param->mode = b->all_mode;
        if (b->all_opacity < 1)
            param->opacity = b->all_opacity;

        switch (param->mode) {
        case BLEND_ADDITION:   param->blend = blend_addition;   break;
        case BLEND_AND:        param->blend = blend_and;        break;
        case BLEND_AVERAGE:    param->blend = blend_average;    break;
        case BLEND_BURN:       param->blend = blend_burn;       break;
        case BLEND_DARKEN:     param->blend = blend_darken;     break;
        case BLEND_DIFFERENCE: param->blend = blend_difference; break;
        case BLEND_DIVIDE:     param->blend = blend_divide;     break;
        case BLEND_DODGE:      param->blend = blend_dodge;      break;
        case BLEND_EXCLUSION:  param->blend = blend_exclusion;  break;
        case BLEND_HARDLIGHT:  param->blend = blend_hardlight;  break;
        case BLEND_LIGHTEN:    param->blend = blend_lighten;    break;
        case BLEND_MULTIPLY:   param->blend = blend_multiply;   break;
        case BLEND_NEGATION:   param->blend = blend_negation;   break;
        case BLEND_NORMAL:     param->blend = blend_normal;     break;
        case BLEND_OR:         param->blend = blend_or;         break;
        case BLEND_OVERLAY:    param->blend = blend_overlay;    break;
        case BLEND_PHOENIX:    param->blend = blend_phoenix;    break;
        case BLEND_PINLIGHT:   param->blend = blend_pinlight;   break;
        case BLEND_REFLECT:    param->blend = blend_reflect;    break;
        case BLEND_SCREEN:     param->blend = blend_screen;     break;
        case BLEND_SOFTLIGHT:  param->blend = blend_softlight;  break;
        case BLEND_SUBTRACT:   param->blend = blend_subtract;   break;
        case BLEND_VIVIDLIGHT: param->blend = blend_vividlight; break;
        case BLEND_XOR:        param->blend = blend_xor;        break;
        }

        if (b->all_expr && !param->expr_str) {
            param->expr_str = av_strdup(b->all_expr);
            if (!param->expr_str)
                return AVERROR(ENOMEM);
        }
        if (param->expr_str) {
            ret = av_expr_parse(&param->e, param->expr_str, var_names,
                                NULL, NULL, NULL, NULL, 0, ctx);
            if (ret < 0)
                return ret;
            param->blend = blend_expr;
        }
    }

    b->dinput.process = blend_frame;
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ422P,AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ411P,
        AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP, AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *toplink = ctx->inputs[TOP];
    AVFilterLink *bottomlink = ctx->inputs[BOTTOM];
    BlendContext *b = ctx->priv;
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(toplink->format);

    if (toplink->format != bottomlink->format) {
        av_log(ctx, AV_LOG_ERROR, "inputs must be of same pixel format\n");
        return AVERROR(EINVAL);
    }
    if (toplink->w                       != bottomlink->w ||
        toplink->h                       != bottomlink->h ||
        toplink->sample_aspect_ratio.num != bottomlink->sample_aspect_ratio.num ||
        toplink->sample_aspect_ratio.den != bottomlink->sample_aspect_ratio.den) {
        av_log(ctx, AV_LOG_ERROR, "First input link %s parameters "
               "(size %dx%d, SAR %d:%d) do not match the corresponding "
               "second input link %s parameters (%dx%d, SAR %d:%d)\n",
               ctx->input_pads[TOP].name, toplink->w, toplink->h,
               toplink->sample_aspect_ratio.num,
               toplink->sample_aspect_ratio.den,
               ctx->input_pads[BOTTOM].name, bottomlink->w, bottomlink->h,
               bottomlink->sample_aspect_ratio.num,
               bottomlink->sample_aspect_ratio.den);
        return AVERROR(EINVAL);
    }

    outlink->w = toplink->w;
    outlink->h = toplink->h;
    outlink->time_base = toplink->time_base;
    outlink->sample_aspect_ratio = toplink->sample_aspect_ratio;
    outlink->frame_rate = toplink->frame_rate;

    b->hsub = pix_desc->log2_chroma_w;
    b->vsub = pix_desc->log2_chroma_h;
    b->nb_planes = av_pix_fmt_count_planes(toplink->format);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    BlendContext *b = ctx->priv;
    int i;

    ff_dualinput_uninit(&b->dinput);
    for (i = 0; i < FF_ARRAY_ELEMS(b->params); i++)
        av_expr_free(b->params[i].e);
}

static int request_frame(AVFilterLink *outlink)
{
    BlendContext *b = outlink->src->priv;
    return ff_dualinput_request_frame(&b->dinput, outlink);
}

static int filter_frame_top(AVFilterLink *inlink, AVFrame *buf)
{
    BlendContext *b = inlink->dst->priv;
    return ff_dualinput_filter_frame_main(&b->dinput, inlink, buf);
}

static int filter_frame_bottom(AVFilterLink *inlink, AVFrame *buf)
{
    BlendContext *b = inlink->dst->priv;
    return ff_dualinput_filter_frame_second(&b->dinput, inlink, buf);
}

static const AVFilterPad blend_inputs[] = {
    {
        .name             = "top",
        .type             = AVMEDIA_TYPE_VIDEO,
        .filter_frame     = filter_frame_top,
    },{
        .name             = "bottom",
        .type             = AVMEDIA_TYPE_VIDEO,
        .filter_frame     = filter_frame_bottom,
    },
    { NULL }
};

static const AVFilterPad blend_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter avfilter_vf_blend = {
    .name          = "blend",
    .description   = NULL_IF_CONFIG_SMALL("Blend two video frames into each other."),
    .init          = init,
    .uninit        = uninit,
    .priv_size     = sizeof(BlendContext),
    .query_formats = query_formats,
    .inputs        = blend_inputs,
    .outputs       = blend_outputs,
    .priv_class    = &blend_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL | AVFILTER_FLAG_SLICE_THREADS,
};
