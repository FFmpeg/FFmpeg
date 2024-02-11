/*
 * Copyright (c) 2002 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2013 Paul B Mahol
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/avassert.h"
#include "libavutil/eval.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "internal.h"
#include "video.h"

#define SUB_PIXEL_BITS  8
#define SUB_PIXELS      (1 << SUB_PIXEL_BITS)
#define COEFF_BITS      11

#define LINEAR 0
#define CUBIC  1

typedef struct PerspectiveContext {
    const AVClass *class;
    char *expr_str[4][2];
    double ref[4][2];
    int32_t (*pv)[2];
    int32_t coeff[SUB_PIXELS][4];
    int interpolation;
    int linesize[4];
    int height[4];
    int hsub, vsub;
    int nb_planes;
    int sense;
    int eval_mode;

    int (*perspective)(AVFilterContext *ctx,
                       void *arg, int job, int nb_jobs);
} PerspectiveContext;

#define OFFSET(x) offsetof(PerspectiveContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

enum PERSPECTIVESense {
    PERSPECTIVE_SENSE_SOURCE      = 0, ///< coordinates give locations in source of corners of destination.
    PERSPECTIVE_SENSE_DESTINATION = 1, ///< coordinates give locations in destination of corners of source.
};

enum EvalMode {
    EVAL_MODE_INIT,
    EVAL_MODE_FRAME,
    EVAL_MODE_NB
};

static const AVOption perspective_options[] = {
    { "x0", "set top left x coordinate",     OFFSET(expr_str[0][0]), AV_OPT_TYPE_STRING, {.str="0"}, 0, 0, FLAGS },
    { "y0", "set top left y coordinate",     OFFSET(expr_str[0][1]), AV_OPT_TYPE_STRING, {.str="0"}, 0, 0, FLAGS },
    { "x1", "set top right x coordinate",    OFFSET(expr_str[1][0]), AV_OPT_TYPE_STRING, {.str="W"}, 0, 0, FLAGS },
    { "y1", "set top right y coordinate",    OFFSET(expr_str[1][1]), AV_OPT_TYPE_STRING, {.str="0"}, 0, 0, FLAGS },
    { "x2", "set bottom left x coordinate",  OFFSET(expr_str[2][0]), AV_OPT_TYPE_STRING, {.str="0"}, 0, 0, FLAGS },
    { "y2", "set bottom left y coordinate",  OFFSET(expr_str[2][1]), AV_OPT_TYPE_STRING, {.str="H"}, 0, 0, FLAGS },
    { "x3", "set bottom right x coordinate", OFFSET(expr_str[3][0]), AV_OPT_TYPE_STRING, {.str="W"}, 0, 0, FLAGS },
    { "y3", "set bottom right y coordinate", OFFSET(expr_str[3][1]), AV_OPT_TYPE_STRING, {.str="H"}, 0, 0, FLAGS },
    { "interpolation", "set interpolation", OFFSET(interpolation), AV_OPT_TYPE_INT, {.i64=LINEAR}, 0, 1, FLAGS, .unit = "interpolation" },
    {      "linear", "", 0, AV_OPT_TYPE_CONST, {.i64=LINEAR}, 0, 0, FLAGS, .unit = "interpolation" },
    {       "cubic", "", 0, AV_OPT_TYPE_CONST, {.i64=CUBIC},  0, 0, FLAGS, .unit = "interpolation" },
    { "sense",   "specify the sense of the coordinates", OFFSET(sense), AV_OPT_TYPE_INT, {.i64=PERSPECTIVE_SENSE_SOURCE}, 0, 1, FLAGS, .unit = "sense"},
    {       "source", "specify locations in source to send to corners in destination",
                0, AV_OPT_TYPE_CONST, {.i64=PERSPECTIVE_SENSE_SOURCE}, 0, 0, FLAGS, .unit = "sense"},
    {       "destination", "specify locations in destination to send corners of source",
                0, AV_OPT_TYPE_CONST, {.i64=PERSPECTIVE_SENSE_DESTINATION}, 0, 0, FLAGS, .unit = "sense"},
    { "eval", "specify when to evaluate expressions", OFFSET(eval_mode), AV_OPT_TYPE_INT, {.i64 = EVAL_MODE_INIT}, 0, EVAL_MODE_NB-1, FLAGS, .unit = "eval" },
         { "init",  "eval expressions once during initialization", 0, AV_OPT_TYPE_CONST, {.i64=EVAL_MODE_INIT},  .flags = FLAGS, .unit = "eval" },
         { "frame", "eval expressions per-frame",                  0, AV_OPT_TYPE_CONST, {.i64=EVAL_MODE_FRAME}, .flags = FLAGS, .unit = "eval" },

    { NULL }
};

AVFILTER_DEFINE_CLASS(perspective);

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA420P,
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ422P,AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ411P,
    AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP, AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE
};

static inline double get_coeff(double d)
{
    double coeff, A = -0.60;

    d = fabs(d);

    if (d < 1.0)
        coeff = (1.0 - (A + 3.0) * d * d + (A + 2.0) * d * d * d);
    else if (d < 2.0)
        coeff = (-4.0 * A + 8.0 * A * d - 5.0 * A * d * d + A * d * d * d);
    else
        coeff = 0.0;

    return coeff;
}

static const char *const var_names[] = {   "W",   "H",   "in",   "on",        NULL };
enum                                   { VAR_W, VAR_H, VAR_IN, VAR_ON, VAR_VARS_NB };

static int calc_persp_luts(AVFilterContext *ctx, AVFilterLink *inlink)
{
    PerspectiveContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    double (*ref)[2]      = s->ref;

    double values[VAR_VARS_NB] = { [VAR_W] = inlink->w, [VAR_H] = inlink->h,
                                   [VAR_IN] = inlink->frame_count_out + 1,
                                   [VAR_ON] = outlink->frame_count_in + 1 };
    const int h = values[VAR_H];
    const int w = values[VAR_W];
    double x0, x1, x2, x3, x4, x5, x6, x7, x8, q;
    double t0, t1, t2, t3;
    int x, y, i, j, ret;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 2; j++) {
            if (!s->expr_str[i][j])
                return AVERROR(EINVAL);
            ret = av_expr_parse_and_eval(&s->ref[i][j], s->expr_str[i][j],
                                         var_names, &values[0],
                                         NULL, NULL, NULL, NULL,
                                         0, 0, ctx);
            if (ret < 0)
                return ret;
        }
    }

    switch (s->sense) {
    case PERSPECTIVE_SENSE_SOURCE:
        x6 = ((ref[0][0] - ref[1][0] - ref[2][0] + ref[3][0]) *
              (ref[2][1] - ref[3][1]) -
             ( ref[0][1] - ref[1][1] - ref[2][1] + ref[3][1]) *
              (ref[2][0] - ref[3][0])) * h;
        x7 = ((ref[0][1] - ref[1][1] - ref[2][1] + ref[3][1]) *
              (ref[1][0] - ref[3][0]) -
             ( ref[0][0] - ref[1][0] - ref[2][0] + ref[3][0]) *
              (ref[1][1] - ref[3][1])) * w;
        q =  ( ref[1][0] - ref[3][0]) * (ref[2][1] - ref[3][1]) -
             ( ref[2][0] - ref[3][0]) * (ref[1][1] - ref[3][1]);

        x0 = q * (ref[1][0] - ref[0][0]) * h + x6 * ref[1][0];
        x1 = q * (ref[2][0] - ref[0][0]) * w + x7 * ref[2][0];
        x2 = q *  ref[0][0] * w * h;
        x3 = q * (ref[1][1] - ref[0][1]) * h + x6 * ref[1][1];
        x4 = q * (ref[2][1] - ref[0][1]) * w + x7 * ref[2][1];
        x5 = q *  ref[0][1] * w * h;
        x8 = q * w * h;
        break;
    case PERSPECTIVE_SENSE_DESTINATION:
        t0 = ref[0][0] * (ref[3][1] - ref[1][1]) +
             ref[1][0] * (ref[0][1] - ref[3][1]) +
             ref[3][0] * (ref[1][1] - ref[0][1]);
        t1 = ref[1][0] * (ref[2][1] - ref[3][1]) +
             ref[2][0] * (ref[3][1] - ref[1][1]) +
             ref[3][0] * (ref[1][1] - ref[2][1]);
        t2 = ref[0][0] * (ref[3][1] - ref[2][1]) +
             ref[2][0] * (ref[0][1] - ref[3][1]) +
             ref[3][0] * (ref[2][1] - ref[0][1]);
        t3 = ref[0][0] * (ref[1][1] - ref[2][1]) +
             ref[1][0] * (ref[2][1] - ref[0][1]) +
             ref[2][0] * (ref[0][1] - ref[1][1]);

        x0 = t0 * t1 * w * (ref[2][1] - ref[0][1]);
        x1 = t0 * t1 * w * (ref[0][0] - ref[2][0]);
        x2 = t0 * t1 * w * (ref[0][1] * ref[2][0] - ref[0][0] * ref[2][1]);
        x3 = t1 * t2 * h * (ref[1][1] - ref[0][1]);
        x4 = t1 * t2 * h * (ref[0][0] - ref[1][0]);
        x5 = t1 * t2 * h * (ref[0][1] * ref[1][0] - ref[0][0] * ref[1][1]);
        x6 = t1 * t2 * (ref[1][1] - ref[0][1]) +
             t0 * t3 * (ref[2][1] - ref[3][1]);
        x7 = t1 * t2 * (ref[0][0] - ref[1][0]) +
             t0 * t3 * (ref[3][0] - ref[2][0]);
        x8 = t1 * t2 * (ref[0][1] * ref[1][0] - ref[0][0] * ref[1][1]) +
             t0 * t3 * (ref[2][0] * ref[3][1] - ref[2][1] * ref[3][0]);
        break;
    default:
        av_assert0(0);
    }

    for (y = 0; y < h; y++){
        for (x = 0; x < w; x++){
            int u, v;

            u =      lrint(SUB_PIXELS * (x0 * x + x1 * y + x2) /
                                        (x6 * x + x7 * y + x8));
            v =      lrint(SUB_PIXELS * (x3 * x + x4 * y + x5) /
                                        (x6 * x + x7 * y + x8));

            s->pv[x + y * w][0] = u;
            s->pv[x + y * w][1] = v;
        }
    }

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    PerspectiveContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int h = inlink->h;
    int w = inlink->w;
    int i, j, ret;
    s->hsub = desc->log2_chroma_w;
    s->vsub = desc->log2_chroma_h;
    s->nb_planes = av_pix_fmt_count_planes(inlink->format);
    if ((ret = av_image_fill_linesizes(s->linesize, inlink->format, inlink->w)) < 0)
        return ret;

    s->height[1] = s->height[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->height[0] = s->height[3] = inlink->h;

    s->pv = av_realloc_f(s->pv, w * h, 2 * sizeof(*s->pv));
    if (!s->pv)
        return AVERROR(ENOMEM);

    if (s->eval_mode == EVAL_MODE_INIT) {
        if ((ret = calc_persp_luts(ctx, inlink)) < 0) {
            return ret;
        }
    }

    for (i = 0; i < SUB_PIXELS; i++){
        double d = i / (double)SUB_PIXELS;
        double temp[4];
        double sum = 0;

        for (j = 0; j < 4; j++)
            temp[j] = get_coeff(j - d - 1);

        for (j = 0; j < 4; j++)
            sum += temp[j];

        for (j = 0; j < 4; j++)
            s->coeff[i][j] = lrint((1 << COEFF_BITS) * temp[j] / sum);
    }

    return 0;
}

typedef struct ThreadData {
    uint8_t *dst;
    int dst_linesize;
    uint8_t *src;
    int src_linesize;
    int w, h;
    int hsub, vsub;
} ThreadData;

static int resample_cubic(AVFilterContext *ctx, void *arg,
                          int job, int nb_jobs)
{
    PerspectiveContext *s = ctx->priv;
    ThreadData *td = arg;
    uint8_t *dst = td->dst;
    int dst_linesize = td->dst_linesize;
    uint8_t *src = td->src;
    int src_linesize = td->src_linesize;
    int w = td->w;
    int h = td->h;
    int hsub = td->hsub;
    int vsub = td->vsub;
    int start = (h * job) / nb_jobs;
    int end   = (h * (job+1)) / nb_jobs;
    const int linesize = s->linesize[0];
    int x, y;

    for (y = start; y < end; y++) {
        int sy = y << vsub;
        for (x = 0; x < w; x++) {
            int u, v, subU, subV, sum, sx;

            sx   = x << hsub;
            u    = s->pv[sx + sy * linesize][0] >> hsub;
            v    = s->pv[sx + sy * linesize][1] >> vsub;
            subU = u & (SUB_PIXELS - 1);
            subV = v & (SUB_PIXELS - 1);
            u  >>= SUB_PIXEL_BITS;
            v  >>= SUB_PIXEL_BITS;

            if (u > 0 && v > 0 && u < w - 2 && v < h - 2){
                const int index = u + v*src_linesize;
                const int a = s->coeff[subU][0];
                const int b = s->coeff[subU][1];
                const int c = s->coeff[subU][2];
                const int d = s->coeff[subU][3];

                sum = s->coeff[subV][0] * (a * src[index - 1 -     src_linesize] + b * src[index - 0 -     src_linesize]  +
                                      c *      src[index + 1 -     src_linesize] + d * src[index + 2 -     src_linesize]) +
                      s->coeff[subV][1] * (a * src[index - 1                   ] + b * src[index - 0                   ]  +
                                      c *      src[index + 1                   ] + d * src[index + 2                   ]) +
                      s->coeff[subV][2] * (a * src[index - 1 +     src_linesize] + b * src[index - 0 +     src_linesize]  +
                                      c *      src[index + 1 +     src_linesize] + d * src[index + 2 +     src_linesize]) +
                      s->coeff[subV][3] * (a * src[index - 1 + 2 * src_linesize] + b * src[index - 0 + 2 * src_linesize]  +
                                      c *      src[index + 1 + 2 * src_linesize] + d * src[index + 2 + 2 * src_linesize]);
            } else {
                int dx, dy;

                sum = 0;

                for (dy = 0; dy < 4; dy++) {
                    int iy = v + dy - 1;

                    if (iy < 0)
                        iy = 0;
                    else if (iy >= h)
                        iy = h-1;
                    for (dx = 0; dx < 4; dx++) {
                        int ix = u + dx - 1;

                        if (ix < 0)
                            ix = 0;
                        else if (ix >= w)
                            ix = w - 1;

                        sum += s->coeff[subU][dx] * s->coeff[subV][dy] * src[ ix + iy * src_linesize];
                    }
                }
            }

            sum = (sum + (1<<(COEFF_BITS * 2 - 1))) >> (COEFF_BITS * 2);
            sum = av_clip_uint8(sum);
            dst[x + y * dst_linesize] = sum;
        }
    }
    return 0;
}

static int resample_linear(AVFilterContext *ctx, void *arg,
                           int job, int nb_jobs)
{
    PerspectiveContext *s = ctx->priv;
    ThreadData *td = arg;
    uint8_t *dst = td->dst;
    int dst_linesize = td->dst_linesize;
    uint8_t *src = td->src;
    int src_linesize = td->src_linesize;
    int w = td->w;
    int h = td->h;
    int hsub = td->hsub;
    int vsub = td->vsub;
    int start = (h * job) / nb_jobs;
    int end   = (h * (job+1)) / nb_jobs;
    const int linesize = s->linesize[0];
    int x, y;

    for (y = start; y < end; y++){
        int sy = y << vsub;
        for (x = 0; x < w; x++){
            int u, v, subU, subV, sum, sx, index, subUI, subVI;

            sx   = x << hsub;
            u    = s->pv[sx + sy * linesize][0] >> hsub;
            v    = s->pv[sx + sy * linesize][1] >> vsub;
            subU = u & (SUB_PIXELS - 1);
            subV = v & (SUB_PIXELS - 1);
            u  >>= SUB_PIXEL_BITS;
            v  >>= SUB_PIXEL_BITS;

            index = u + v * src_linesize;
            subUI = SUB_PIXELS - subU;
            subVI = SUB_PIXELS - subV;

            if ((unsigned)u < (unsigned)(w - 1)){
                if((unsigned)v < (unsigned)(h - 1)){
                    sum = subVI * (subUI * src[index] + subU * src[index + 1]) +
                          subV  * (subUI * src[index + src_linesize] + subU * src[index + src_linesize + 1]);
                    sum = (sum + (1 << (SUB_PIXEL_BITS * 2 - 1)))>> (SUB_PIXEL_BITS * 2);
                } else {
                    if (v < 0)
                        v = 0;
                    else
                        v = h - 1;
                    index = u + v * src_linesize;
                    sum   = subUI * src[index] + subU * src[index + 1];
                    sum   = (sum + (1 << (SUB_PIXEL_BITS - 1))) >> SUB_PIXEL_BITS;
                }
            } else {
                if (u < 0)
                    u = 0;
                else
                    u = w - 1;
                if ((unsigned)v < (unsigned)(h - 1)){
                    index = u + v * src_linesize;
                    sum   = subVI * src[index] + subV * src[index + src_linesize];
                    sum   = (sum + (1 << (SUB_PIXEL_BITS - 1))) >> SUB_PIXEL_BITS;
                } else {
                    if (v < 0)
                        v = 0;
                    else
                        v = h - 1;
                    index = u + v * src_linesize;
                    sum   = src[index];
                }
            }

            sum = av_clip_uint8(sum);
            dst[x + y * dst_linesize] = sum;
        }
    }
    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    PerspectiveContext *s = ctx->priv;

    switch (s->interpolation) {
    case LINEAR: s->perspective = resample_linear; break;
    case CUBIC:  s->perspective = resample_cubic;  break;
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    PerspectiveContext *s = ctx->priv;
    AVFrame *out;
    int plane;
    int ret;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&frame);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, frame);

    if (s->eval_mode == EVAL_MODE_FRAME) {
        if ((ret = calc_persp_luts(ctx, inlink)) < 0) {
            av_frame_free(&out);
            return ret;
        }
    }

    for (plane = 0; plane < s->nb_planes; plane++) {
        int hsub = plane == 1 || plane == 2 ? s->hsub : 0;
        int vsub = plane == 1 || plane == 2 ? s->vsub : 0;
        ThreadData td = {.dst = out->data[plane],
                         .dst_linesize = out->linesize[plane],
                         .src = frame->data[plane],
                         .src_linesize = frame->linesize[plane],
                         .w = s->linesize[plane],
                         .h = s->height[plane],
                         .hsub = hsub,
                         .vsub = vsub };
        ff_filter_execute(ctx, s->perspective, &td, NULL,
                          FFMIN(td.h, ff_filter_get_nb_threads(ctx)));
    }

    av_frame_free(&frame);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    PerspectiveContext *s = ctx->priv;

    av_freep(&s->pv);
}

static const AVFilterPad perspective_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

const AVFilter ff_vf_perspective = {
    .name          = "perspective",
    .description   = NULL_IF_CONFIG_SMALL("Correct the perspective of video."),
    .priv_size     = sizeof(PerspectiveContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(perspective_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .priv_class    = &perspective_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};
