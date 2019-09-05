/*
 * Copyright (c) 2013 Stefano Sabatini
 * Copyright (c) 2008 Vitor Sessak
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
 * rotation filter, partially based on the tests/rotozoom.c program
*/

#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/opt.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "drawutils.h"
#include "internal.h"
#include "video.h"

#include <float.h>

static const char * const var_names[] = {
    "in_w" , "iw",  ///< width of the input video
    "in_h" , "ih",  ///< height of the input video
    "out_w", "ow",  ///< width of the input video
    "out_h", "oh",  ///< height of the input video
    "hsub", "vsub",
    "n",            ///< number of frame
    "t",            ///< timestamp expressed in seconds
    NULL
};

enum var_name {
    VAR_IN_W , VAR_IW,
    VAR_IN_H , VAR_IH,
    VAR_OUT_W, VAR_OW,
    VAR_OUT_H, VAR_OH,
    VAR_HSUB, VAR_VSUB,
    VAR_N,
    VAR_T,
    VAR_VARS_NB
};

typedef struct RotContext {
    const AVClass *class;
    double angle;
    char *angle_expr_str;   ///< expression for the angle
    AVExpr *angle_expr;     ///< parsed expression for the angle
    char *outw_expr_str, *outh_expr_str;
    int outh, outw;
    uint8_t fillcolor[4];   ///< color expressed either in YUVA or RGBA colorspace for the padding area
    char *fillcolor_str;
    int fillcolor_enable;
    int hsub, vsub;
    int nb_planes;
    int use_bilinear;
    float sinx, cosx;
    double var_values[VAR_VARS_NB];
    FFDrawContext draw;
    FFDrawColor color;
    uint8_t *(*interpolate_bilinear)(uint8_t *dst_color,
                                    const uint8_t *src, int src_linesize, int src_linestep,
                                    int x, int y, int max_x, int max_y);
} RotContext;

typedef struct ThreadData {
    AVFrame *in, *out;
    int inw,  inh;
    int outw, outh;
    int plane;
    int xi, yi;
    int xprime, yprime;
    int c, s;
} ThreadData;

#define OFFSET(x) offsetof(RotContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption rotate_options[] = {
    { "angle",     "set angle (in radians)",       OFFSET(angle_expr_str), AV_OPT_TYPE_STRING, {.str="0"}, CHAR_MIN, CHAR_MAX, .flags=FLAGS },
    { "a",         "set angle (in radians)",       OFFSET(angle_expr_str), AV_OPT_TYPE_STRING, {.str="0"}, CHAR_MIN, CHAR_MAX, .flags=FLAGS },
    { "out_w",     "set output width expression",  OFFSET(outw_expr_str), AV_OPT_TYPE_STRING, {.str="iw"}, CHAR_MIN, CHAR_MAX, .flags=FLAGS },
    { "ow",        "set output width expression",  OFFSET(outw_expr_str), AV_OPT_TYPE_STRING, {.str="iw"}, CHAR_MIN, CHAR_MAX, .flags=FLAGS },
    { "out_h",     "set output height expression", OFFSET(outh_expr_str), AV_OPT_TYPE_STRING, {.str="ih"}, CHAR_MIN, CHAR_MAX, .flags=FLAGS },
    { "oh",        "set output height expression", OFFSET(outh_expr_str), AV_OPT_TYPE_STRING, {.str="ih"}, CHAR_MIN, CHAR_MAX, .flags=FLAGS },
    { "fillcolor", "set background fill color",    OFFSET(fillcolor_str), AV_OPT_TYPE_STRING, {.str="black"}, CHAR_MIN, CHAR_MAX, .flags=FLAGS },
    { "c",         "set background fill color",    OFFSET(fillcolor_str), AV_OPT_TYPE_STRING, {.str="black"}, CHAR_MIN, CHAR_MAX, .flags=FLAGS },
    { "bilinear",  "use bilinear interpolation",   OFFSET(use_bilinear),  AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1, .flags=FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(rotate);

static av_cold int init(AVFilterContext *ctx)
{
    RotContext *rot = ctx->priv;

    if (!strcmp(rot->fillcolor_str, "none"))
        rot->fillcolor_enable = 0;
    else if (av_parse_color(rot->fillcolor, rot->fillcolor_str, -1, ctx) >= 0)
        rot->fillcolor_enable = 1;
    else
        return AVERROR(EINVAL);
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    RotContext *rot = ctx->priv;

    av_expr_free(rot->angle_expr);
    rot->angle_expr = NULL;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_GBRP,   AV_PIX_FMT_GBRAP,
        AV_PIX_FMT_ARGB,   AV_PIX_FMT_RGBA,
        AV_PIX_FMT_ABGR,   AV_PIX_FMT_BGRA,
        AV_PIX_FMT_0RGB,   AV_PIX_FMT_RGB0,
        AV_PIX_FMT_0BGR,   AV_PIX_FMT_BGR0,
        AV_PIX_FMT_RGB24,  AV_PIX_FMT_BGR24,
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUVJ444P,
        AV_PIX_FMT_YUV420P,  AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_YUV420P10LE, AV_PIX_FMT_YUVA420P10LE,
        AV_PIX_FMT_YUV444P10LE, AV_PIX_FMT_YUVA444P10LE,
        AV_PIX_FMT_YUV420P12LE,
        AV_PIX_FMT_YUV444P12LE,
        AV_PIX_FMT_YUV444P16LE, AV_PIX_FMT_YUVA444P16LE,
        AV_PIX_FMT_YUV420P16LE, AV_PIX_FMT_YUVA420P16LE,
        AV_PIX_FMT_YUV444P9LE, AV_PIX_FMT_YUVA444P9LE,
        AV_PIX_FMT_YUV420P9LE, AV_PIX_FMT_YUVA420P9LE,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static double get_rotated_w(void *opaque, double angle)
{
    RotContext *rot = opaque;
    double inw = rot->var_values[VAR_IN_W];
    double inh = rot->var_values[VAR_IN_H];
    float sinx = sin(angle);
    float cosx = cos(angle);

    return FFMAX(0, inh * sinx) + FFMAX(0, -inw * cosx) +
           FFMAX(0, inw * cosx) + FFMAX(0, -inh * sinx);
}

static double get_rotated_h(void *opaque, double angle)
{
    RotContext *rot = opaque;
    double inw = rot->var_values[VAR_IN_W];
    double inh = rot->var_values[VAR_IN_H];
    float sinx = sin(angle);
    float cosx = cos(angle);

    return FFMAX(0, -inh * cosx) + FFMAX(0, -inw * sinx) +
           FFMAX(0,  inh * cosx) + FFMAX(0,  inw * sinx);
}

static double (* const func1[])(void *, double) = {
    get_rotated_w,
    get_rotated_h,
    NULL
};

static const char * const func1_names[] = {
    "rotw",
    "roth",
    NULL
};

#define FIXP (1<<16)
#define FIXP2 (1<<20)
#define INT_PI 3294199 //(M_PI * FIXP2)

/**
 * Compute the sin of a using integer values.
 * Input is scaled by FIXP2 and output values are scaled by FIXP.
 */
static int64_t int_sin(int64_t a)
{
    int64_t a2, res = 0;
    int i;
    if (a < 0) a = INT_PI-a; // 0..inf
    a %= 2 * INT_PI;         // 0..2PI

    if (a >= INT_PI*3/2) a -= 2*INT_PI;  // -PI/2 .. 3PI/2
    if (a >= INT_PI/2  ) a = INT_PI - a; // -PI/2 ..  PI/2

    /* compute sin using Taylor series approximated to the fifth term */
    a2 = (a*a)/(FIXP2);
    for (i = 2; i < 11; i += 2) {
        res += a;
        a = -a*a2 / (FIXP2*i*(i+1));
    }
    return (res + 8)>>4;
}

/**
 * Interpolate the color in src at position x and y using bilinear
 * interpolation.
 */
static uint8_t *interpolate_bilinear8(uint8_t *dst_color,
                                      const uint8_t *src, int src_linesize, int src_linestep,
                                      int x, int y, int max_x, int max_y)
{
    int int_x = av_clip(x>>16, 0, max_x);
    int int_y = av_clip(y>>16, 0, max_y);
    int frac_x = x&0xFFFF;
    int frac_y = y&0xFFFF;
    int i;
    int int_x1 = FFMIN(int_x+1, max_x);
    int int_y1 = FFMIN(int_y+1, max_y);

    for (i = 0; i < src_linestep; i++) {
        int s00 = src[src_linestep * int_x  + i + src_linesize * int_y ];
        int s01 = src[src_linestep * int_x1 + i + src_linesize * int_y ];
        int s10 = src[src_linestep * int_x  + i + src_linesize * int_y1];
        int s11 = src[src_linestep * int_x1 + i + src_linesize * int_y1];
        int s0 = (((1<<16) - frac_x)*s00 + frac_x*s01);
        int s1 = (((1<<16) - frac_x)*s10 + frac_x*s11);

        dst_color[i] = ((int64_t)((1<<16) - frac_y)*s0 + (int64_t)frac_y*s1) >> 32;
    }

    return dst_color;
}

/**
 * Interpolate the color in src at position x and y using bilinear
 * interpolation.
 */
static uint8_t *interpolate_bilinear16(uint8_t *dst_color,
                                       const uint8_t *src, int src_linesize, int src_linestep,
                                       int x, int y, int max_x, int max_y)
{
    int int_x = av_clip(x>>16, 0, max_x);
    int int_y = av_clip(y>>16, 0, max_y);
    int frac_x = x&0xFFFF;
    int frac_y = y&0xFFFF;
    int i;
    int int_x1 = FFMIN(int_x+1, max_x);
    int int_y1 = FFMIN(int_y+1, max_y);

    for (i = 0; i < src_linestep; i+=2) {
        int s00 = AV_RL16(&src[src_linestep * int_x  + i + src_linesize * int_y ]);
        int s01 = AV_RL16(&src[src_linestep * int_x1 + i + src_linesize * int_y ]);
        int s10 = AV_RL16(&src[src_linestep * int_x  + i + src_linesize * int_y1]);
        int s11 = AV_RL16(&src[src_linestep * int_x1 + i + src_linesize * int_y1]);
        int s0 = (((1<<16) - frac_x)*s00 + frac_x*s01);
        int s1 = (((1<<16) - frac_x)*s10 + frac_x*s11);

        AV_WL16(&dst_color[i], ((int64_t)((1<<16) - frac_y)*s0 + (int64_t)frac_y*s1) >> 32);
    }

    return dst_color;
}

static int config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    RotContext *rot = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    const AVPixFmtDescriptor *pixdesc = av_pix_fmt_desc_get(inlink->format);
    int ret;
    double res;
    char *expr;

    ff_draw_init(&rot->draw, inlink->format, 0);
    ff_draw_color(&rot->draw, &rot->color, rot->fillcolor);

    rot->hsub = pixdesc->log2_chroma_w;
    rot->vsub = pixdesc->log2_chroma_h;

    if (pixdesc->comp[0].depth == 8)
        rot->interpolate_bilinear = interpolate_bilinear8;
    else
        rot->interpolate_bilinear = interpolate_bilinear16;

    rot->var_values[VAR_IN_W] = rot->var_values[VAR_IW] = inlink->w;
    rot->var_values[VAR_IN_H] = rot->var_values[VAR_IH] = inlink->h;
    rot->var_values[VAR_HSUB] = 1<<rot->hsub;
    rot->var_values[VAR_VSUB] = 1<<rot->vsub;
    rot->var_values[VAR_N] = NAN;
    rot->var_values[VAR_T] = NAN;
    rot->var_values[VAR_OUT_W] = rot->var_values[VAR_OW] = NAN;
    rot->var_values[VAR_OUT_H] = rot->var_values[VAR_OH] = NAN;

    av_expr_free(rot->angle_expr);
    rot->angle_expr = NULL;
    if ((ret = av_expr_parse(&rot->angle_expr, expr = rot->angle_expr_str, var_names,
                             func1_names, func1, NULL, NULL, 0, ctx)) < 0) {
        av_log(ctx, AV_LOG_ERROR,
               "Error occurred parsing angle expression '%s'\n", rot->angle_expr_str);
        return ret;
    }

#define SET_SIZE_EXPR(name, opt_name) do {                                         \
    ret = av_expr_parse_and_eval(&res, expr = rot->name##_expr_str,                \
                                 var_names, rot->var_values,                       \
                                 func1_names, func1, NULL, NULL, rot, 0, ctx);     \
    if (ret < 0 || isnan(res) || isinf(res) || res <= 0) {                         \
        av_log(ctx, AV_LOG_ERROR,                                                  \
               "Error parsing or evaluating expression for option %s: "            \
               "invalid expression '%s' or non-positive or indefinite value %f\n", \
               opt_name, expr, res);                                               \
        return ret;                                                                \
    }                                                                              \
} while (0)

    /* evaluate width and height */
    av_expr_parse_and_eval(&res, expr = rot->outw_expr_str, var_names, rot->var_values,
                           func1_names, func1, NULL, NULL, rot, 0, ctx);
    rot->var_values[VAR_OUT_W] = rot->var_values[VAR_OW] = res;
    rot->outw = res + 0.5;
    SET_SIZE_EXPR(outh, "out_h");
    rot->var_values[VAR_OUT_H] = rot->var_values[VAR_OH] = res;
    rot->outh = res + 0.5;

    /* evaluate the width again, as it may depend on the evaluated output height */
    SET_SIZE_EXPR(outw, "out_w");
    rot->var_values[VAR_OUT_W] = rot->var_values[VAR_OW] = res;
    rot->outw = res + 0.5;

    /* compute number of planes */
    rot->nb_planes = av_pix_fmt_count_planes(inlink->format);
    outlink->w = rot->outw;
    outlink->h = rot->outh;
    return 0;
}

static av_always_inline void copy_elem(uint8_t *pout, const uint8_t *pin, int elem_size)
{
    int v;
    switch (elem_size) {
    case 1:
        *pout = *pin;
        break;
    case 2:
        *((uint16_t *)pout) = *((uint16_t *)pin);
        break;
    case 3:
        v = AV_RB24(pin);
        AV_WB24(pout, v);
        break;
    case 4:
        *((uint32_t *)pout) = *((uint32_t *)pin);
        break;
    default:
        memcpy(pout, pin, elem_size);
        break;
    }
}

static av_always_inline void simple_rotate_internal(uint8_t *dst, const uint8_t *src, int src_linesize, int angle, int elem_size, int len)
{
    int i;
    switch(angle) {
    case 0:
        memcpy(dst, src, elem_size * len);
        break;
    case 1:
        for (i = 0; i<len; i++)
            copy_elem(dst + i*elem_size, src + (len-i-1)*src_linesize, elem_size);
        break;
    case 2:
        for (i = 0; i<len; i++)
            copy_elem(dst + i*elem_size, src + (len-i-1)*elem_size, elem_size);
        break;
    case 3:
        for (i = 0; i<len; i++)
            copy_elem(dst + i*elem_size, src + i*src_linesize, elem_size);
        break;
    }
}

static av_always_inline void simple_rotate(uint8_t *dst, const uint8_t *src, int src_linesize, int angle, int elem_size, int len)
{
    switch(elem_size) {
    case 1 : simple_rotate_internal(dst, src, src_linesize, angle, 1, len); break;
    case 2 : simple_rotate_internal(dst, src, src_linesize, angle, 2, len); break;
    case 3 : simple_rotate_internal(dst, src, src_linesize, angle, 3, len); break;
    case 4 : simple_rotate_internal(dst, src, src_linesize, angle, 4, len); break;
    default: simple_rotate_internal(dst, src, src_linesize, angle, elem_size, len); break;
    }
}

#define TS2T(ts, tb) ((ts) == AV_NOPTS_VALUE ? NAN : (double)(ts)*av_q2d(tb))

static int filter_slice(AVFilterContext *ctx, void *arg, int job, int nb_jobs)
{
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    RotContext *rot = ctx->priv;
    const int outw = td->outw, outh = td->outh;
    const int inw = td->inw, inh = td->inh;
    const int plane = td->plane;
    const int xi = td->xi, yi = td->yi;
    const int c = td->c, s = td->s;
    const int start = (outh *  job   ) / nb_jobs;
    const int end   = (outh * (job+1)) / nb_jobs;
    int xprime = td->xprime + start * s;
    int yprime = td->yprime + start * c;
    int i, j, x, y;

    for (j = start; j < end; j++) {
        x = xprime + xi + FIXP*(inw-1)/2;
        y = yprime + yi + FIXP*(inh-1)/2;

        if (fabs(rot->angle - 0) < FLT_EPSILON && outw == inw && outh == inh) {
            simple_rotate(out->data[plane] + j * out->linesize[plane],
                           in->data[plane] + j *  in->linesize[plane],
                          in->linesize[plane], 0, rot->draw.pixelstep[plane], outw);
        } else if (fabs(rot->angle - M_PI/2) < FLT_EPSILON && outw == inh && outh == inw) {
            simple_rotate(out->data[plane] + j * out->linesize[plane],
                           in->data[plane] + j * rot->draw.pixelstep[plane],
                          in->linesize[plane], 1, rot->draw.pixelstep[plane], outw);
        } else if (fabs(rot->angle - M_PI) < FLT_EPSILON && outw == inw && outh == inh) {
            simple_rotate(out->data[plane] + j * out->linesize[plane],
                           in->data[plane] + (outh-j-1) *  in->linesize[plane],
                          in->linesize[plane], 2, rot->draw.pixelstep[plane], outw);
        } else if (fabs(rot->angle - 3*M_PI/2) < FLT_EPSILON && outw == inh && outh == inw) {
            simple_rotate(out->data[plane] + j * out->linesize[plane],
                           in->data[plane] + (outh-j-1) * rot->draw.pixelstep[plane],
                          in->linesize[plane], 3, rot->draw.pixelstep[plane], outw);
        } else {

        for (i = 0; i < outw; i++) {
            int32_t v;
            int x1, y1;
            uint8_t *pin, *pout;
            x1 = x>>16;
            y1 = y>>16;

            /* the out-of-range values avoid border artifacts */
            if (x1 >= -1 && x1 <= inw && y1 >= -1 && y1 <= inh) {
                uint8_t inp_inv[4]; /* interpolated input value */
                pout = out->data[plane] + j * out->linesize[plane] + i * rot->draw.pixelstep[plane];
                if (rot->use_bilinear) {
                    pin = rot->interpolate_bilinear(inp_inv,
                                                    in->data[plane], in->linesize[plane], rot->draw.pixelstep[plane],
                                                    x, y, inw-1, inh-1);
                } else {
                    int x2 = av_clip(x1, 0, inw-1);
                    int y2 = av_clip(y1, 0, inh-1);
                    pin = in->data[plane] + y2 * in->linesize[plane] + x2 * rot->draw.pixelstep[plane];
                }
                switch (rot->draw.pixelstep[plane]) {
                case 1:
                    *pout = *pin;
                    break;
                case 2:
                    v = AV_RL16(pin);
                    AV_WL16(pout, v);
                    break;
                case 3:
                    v = AV_RB24(pin);
                    AV_WB24(pout, v);
                    break;
                case 4:
                    *((uint32_t *)pout) = *((uint32_t *)pin);
                    break;
                default:
                    memcpy(pout, pin, rot->draw.pixelstep[plane]);
                    break;
                }
            }
            x += c;
            y -= s;
        }
        }
        xprime += s;
        yprime += c;
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    RotContext *rot = ctx->priv;
    int angle_int, s, c, plane;
    double res;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    rot->var_values[VAR_N] = inlink->frame_count_out;
    rot->var_values[VAR_T] = TS2T(in->pts, inlink->time_base);
    rot->angle = res = av_expr_eval(rot->angle_expr, rot->var_values, rot);

    av_log(ctx, AV_LOG_DEBUG, "n:%f time:%f angle:%f/PI\n",
           rot->var_values[VAR_N], rot->var_values[VAR_T], rot->angle/M_PI);

    angle_int = res * FIXP * 16;
    s = int_sin(angle_int);
    c = int_sin(angle_int + INT_PI/2);

    /* fill background */
    if (rot->fillcolor_enable)
        ff_fill_rectangle(&rot->draw, &rot->color, out->data, out->linesize,
                          0, 0, outlink->w, outlink->h);

    for (plane = 0; plane < rot->nb_planes; plane++) {
        int hsub = plane == 1 || plane == 2 ? rot->hsub : 0;
        int vsub = plane == 1 || plane == 2 ? rot->vsub : 0;
        const int outw = AV_CEIL_RSHIFT(outlink->w, hsub);
        const int outh = AV_CEIL_RSHIFT(outlink->h, vsub);
        ThreadData td = { .in = in,   .out  = out,
                          .inw  = AV_CEIL_RSHIFT(inlink->w, hsub),
                          .inh  = AV_CEIL_RSHIFT(inlink->h, vsub),
                          .outh = outh, .outw = outw,
                          .xi = -(outw-1) * c / 2, .yi =  (outw-1) * s / 2,
                          .xprime = -(outh-1) * s / 2,
                          .yprime = -(outh-1) * c / 2,
                          .plane = plane, .c = c, .s = s };


        ctx->internal->execute(ctx, filter_slice, &td, NULL, FFMIN(outh, ff_filter_get_nb_threads(ctx)));
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    RotContext *rot = ctx->priv;
    int ret;

    if (!strcmp(cmd, "angle") || !strcmp(cmd, "a")) {
        AVExpr *old = rot->angle_expr;
        ret = av_expr_parse(&rot->angle_expr, args, var_names,
                            NULL, NULL, NULL, NULL, 0, ctx);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Error when parsing the expression '%s' for angle command\n", args);
            rot->angle_expr = old;
            return ret;
        }
        av_expr_free(old);
    } else
        ret = AVERROR(ENOSYS);

    return ret;
}

static const AVFilterPad rotate_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad rotate_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
    },
    { NULL }
};

AVFilter ff_vf_rotate = {
    .name          = "rotate",
    .description   = NULL_IF_CONFIG_SMALL("Rotate the input image."),
    .priv_size     = sizeof(RotContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .process_command = process_command,
    .inputs        = rotate_inputs,
    .outputs       = rotate_outputs,
    .priv_class    = &rotate_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};
