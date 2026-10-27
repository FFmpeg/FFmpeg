/*
 * Copyright (c) 2013 Clément Bœsch
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

#include <float.h>  /* DBL_MAX */

#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/eval.h"
#include "libavutil/internal.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "filters.h"
#include "video.h"
#include "wavelet97.h"

static const char *const var_names[] = {
    "w",    // stream width
    "h",    // stream height
    "n",    // frame count
    "pts",  // presentation timestamp expressed in AV_TIME_BASE units
    "r",    // frame rate
    "t",    // timestamp expressed in seconds
    "tb",   // timebase
    NULL
};

enum var_name {
    VAR_W,
    VAR_H,
    VAR_N,
    VAR_PTS,
    VAR_R,
    VAR_T,
    VAR_TB,
    VAR_NB
};

enum EvalMode {
    EVAL_MODE_INIT,
    EVAL_MODE_FRAME,
    EVAL_MODE_NB
};

enum Mode {
    MODE_FORWARD,
    MODE_BACKWARD,
    MODE_WAVELET,
};

typedef struct VignetteContext {
    const AVClass *class;
    const AVPixFmtDescriptor *desc;
    int mode;                           ///< Mode
    int eval_mode;                      ///< EvalMode
    double noise;
    int decimated;
    double sqcoeff[2][5];
    float *wplanes;
    size_t wplane_size;
#define DEF_EXPR_FIELDS(name) AVExpr *name##_pexpr; char *name##_expr; double name
    DEF_EXPR_FIELDS(angle);
    DEF_EXPR_FIELDS(x0);
    DEF_EXPR_FIELDS(y0);
    double var_values[VAR_NB];
    float *fmap;
    int fmap_linesize;
    double dmax;
    float xscale, yscale;
    uint32_t dither;
    int do_dither;
    AVRational aspect;
    AVRational scale;
} VignetteContext;

#define OFFSET(x) offsetof(VignetteContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption vignette_options[] = {
    { "angle", "set lens angle", OFFSET(angle_expr), AV_OPT_TYPE_STRING, {.str="PI/5"}, .flags = FLAGS },
    { "a",     "set lens angle", OFFSET(angle_expr), AV_OPT_TYPE_STRING, {.str="PI/5"}, .flags = FLAGS },
    { "x0", "set circle center position on x-axis", OFFSET(x0_expr), AV_OPT_TYPE_STRING, {.str="w/2"}, .flags = FLAGS },
    { "y0", "set circle center position on y-axis", OFFSET(y0_expr), AV_OPT_TYPE_STRING, {.str="h/2"}, .flags = FLAGS },
    { "mode", "set forward/backward mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64 = MODE_FORWARD}, 0, MODE_WAVELET, FLAGS, .unit = "mode" },
        { "forward",  NULL, 0, AV_OPT_TYPE_CONST, {.i64 = MODE_FORWARD},  INT_MIN, INT_MAX, FLAGS, .unit = "mode"},
        { "backward", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = MODE_BACKWARD}, INT_MIN, INT_MAX, FLAGS, .unit = "mode"},
        { "wavelet",  "backward with noise suppression in the wavelet domain", 0, AV_OPT_TYPE_CONST, {.i64 = MODE_WAVELET}, INT_MIN, INT_MAX, FLAGS, .unit = "mode"},
    { "eval", "specify when to evaluate expressions", OFFSET(eval_mode), AV_OPT_TYPE_INT, {.i64 = EVAL_MODE_INIT}, 0, EVAL_MODE_NB-1, FLAGS, .unit = "eval" },
        { "init",  "eval expressions once during initialization", 0, AV_OPT_TYPE_CONST, {.i64=EVAL_MODE_INIT},  .flags = FLAGS, .unit = "eval" },
        { "frame", "eval expressions for each frame",             0, AV_OPT_TYPE_CONST, {.i64=EVAL_MODE_FRAME}, .flags = FLAGS, .unit = "eval" },
    { "dither", "set dithering", OFFSET(do_dither), AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, FLAGS },
    { "aspect", "set aspect ratio", OFFSET(aspect), AV_OPT_TYPE_RATIONAL, {.dbl = 1}, 0, DBL_MAX, .flags = FLAGS },
    { "noise", "set input noise standard deviation for wavelet mode", OFFSET(noise), AV_OPT_TYPE_DOUBLE, {.dbl = 0.5}, 0.01, 255, FLAGS },
    { "decimated", "use a decimated wavelet transform, faster but lower quality", OFFSET(decimated), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(vignette);

static av_cold int init(AVFilterContext *ctx)
{
    VignetteContext *s = ctx->priv;

#define PARSE_EXPR(name) do {                                               \
    int ret = av_expr_parse(&s->name##_pexpr,  s->name##_expr, var_names,   \
                            NULL, NULL, NULL, NULL, 0, ctx);                \
    if (ret < 0) {                                                          \
        av_log(ctx, AV_LOG_ERROR, "Unable to parse expression for '"        \
               AV_STRINGIFY(name) "'\n");                                   \
        return ret;                                                         \
    }                                                                       \
} while (0)

    PARSE_EXPR(angle);
    PARSE_EXPR(x0);
    PARSE_EXPR(y0);

    // squared taps propagate the noise variance as if the samples were uncorrelated
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 5; j++)
            s->sqcoeff[i][j] = ff_wavelet97_coeff[i][j] * ff_wavelet97_coeff[i][j];
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    VignetteContext *s = ctx->priv;
    av_freep(&s->fmap);
    av_freep(&s->wplanes);
    av_expr_free(s->angle_pexpr);
    av_expr_free(s->x0_pexpr);
    av_expr_free(s->y0_pexpr);
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV411P,
    AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV440P,
    AV_PIX_FMT_RGB24,   AV_PIX_FMT_BGR24,
    AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_NONE
};

static double get_natural_factor(const VignetteContext *s, int x, int y)
{
    const int xx = (x - s->x0) * s->xscale;
    const int yy = (y - s->y0) * s->yscale;
    const double dnorm = hypot(xx, yy) / s->dmax;
    if (dnorm > 1) {
        return 0;
    } else {
        const double c = cos(s->angle * dnorm);
        return (c*c)*(c*c); // do not remove braces, it helps compilers
    }
}

static void update_context(VignetteContext *s, AVFilterLink *inlink, AVFrame *frame)
{
    FilterLink *inl = ff_filter_link(inlink);
    int x, y;
    float *dst = s->fmap;
    int dst_linesize = s->fmap_linesize;

    if (frame) {
        s->var_values[VAR_N]   = inl->frame_count_out;
        s->var_values[VAR_T]   = TS2T(frame->pts, inlink->time_base);
        s->var_values[VAR_PTS] = TS2D(frame->pts);
    } else {
        s->var_values[VAR_N]   = NAN;
        s->var_values[VAR_T]   = NAN;
        s->var_values[VAR_PTS] = NAN;
    }

    s->angle = av_expr_eval(s->angle_pexpr, s->var_values, NULL);
    s->x0 = av_expr_eval(s->x0_pexpr, s->var_values, NULL);
    s->y0 = av_expr_eval(s->y0_pexpr, s->var_values, NULL);

    if (isnan(s->x0) || isnan(s->y0) || isnan(s->angle))
        s->eval_mode = EVAL_MODE_FRAME;

    s->angle = av_clipf(s->angle, 0, M_PI_2);

    if (s->mode == MODE_BACKWARD) {
        for (y = 0; y < inlink->h; y++) {
            for (x = 0; x < inlink->w; x++) {
                const double f = get_natural_factor(s, x, y);
                /* f is 0 beyond dmax and can be tiny for an angle close to
                 * PI/2: bound the gain to keep the scaled samples in the
                 * range of int, 256 already saturates any non-zero 8-bit
                 * sample */
                dst[x] = f > 1. / 256 ? 1. / f : 256;
            }
            dst += dst_linesize;
        }
    } else {
        for (y = 0; y < inlink->h; y++) {
            for (x = 0; x < inlink->w; x++)
                dst[x] = get_natural_factor(s, x, y);
            dst += dst_linesize;
        }
    }
}

static inline double get_dither_value(VignetteContext *s)
{
    double dv = 0;
    if (s->do_dither) {
        dv = s->dither / (double)(1LL<<32);
        s->dither = s->dither * 1664525 + 1013904223;
    }
    return dv;
}

static int wavelet_levels(int w, int h)
{
    return FFMAX(av_log2(FFMIN(w, h)) - 2, 0);
}

#define BOX_RADIUS 3

enum PassType {
    PASS_DECOMPOSE,
    PASS_COMPOSE,
    PASS_DECOMPOSE_DECIMATED,
    PASS_COMPOSE_DECIMATED,
    PASS_BOX,
    PASS_ENERGY,
    PASS_GAIN,
};

/**
 * Samples of a plane processed by a pass, sample x of line y is at
 * y * ystride + x * xstride, x < w, y < h.
 */
typedef struct Lattice {
    int xstride, ystride, w, h;
} Lattice;

static Lattice transpose(Lattice l)
{
    return (Lattice){ l.ystride, l.xstride, l.h, l.w };
}

/**
 * One operation on the lines of a lattice, run in parallel over slices of lines
 */
typedef struct WaveletPass {
    enum PassType type;
    Lattice lat;
    float *dst[2];
    const float *src[3];
    const double (*coeff)[5];
    int step;                           ///< filter dilation
} WaveletPass;

static void box_lines(const WaveletPass *p, int line0, int line1)
{
    const Lattice l = p->lat;

    for (int y = line0; y < line1; y++) {
        for (int x = 0; x < l.w; x++) {
            double sum = 0;

            for (int k = -BOX_RADIUS; k <= BOX_RADIUS; k++)
                sum += p->src[0][y * l.ystride + avpriv_mirror(x + k * p->step, l.w - 1) * l.xstride];
            p->dst[0][y * l.ystride + x * l.xstride] = sum / (2 * BOX_RADIUS + 1);
        }
    }
}

static void energy_lines(const WaveletPass *p, int line0, int line1)
{
    const Lattice l = p->lat;

    for (int y = line0; y < line1; y++) {
        for (int x = 0; x < l.w; x++) {
            const int i = y * l.ystride + x * l.xstride;
            const float band = p->src[0][i], var = p->src[1][i];

            p->dst[0][i] = band * band / var;
            p->dst[1][i] = 1 / var;
        }
    }
}

static void gain_lines(const WaveletPass *p, int line0, int line1)
{
    const Lattice l = p->lat;
    // energy a box must have to count as signal: noise + 3 standard errors, assuming independent coefficients
    const double threshold = 1 + 3 * sqrt(2.0 / ((2 * BOX_RADIUS + 1) * (2 * BOX_RADIUS + 1)));

    for (int y = line0; y < line1; y++) {
        for (int x = 0; x < l.w; x++) {
            const int i = y * l.ystride + x * l.xstride;
            const float signal = FFMAX(p->src[0][i] - threshold, 0) / p->src[1][i];

            p->dst[0][i] *= signal / (signal + p->src[2][i]);
        }
    }
}

static int run_pass_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    const WaveletPass *p = arg;
    const Lattice l = p->lat;
    const int line0 = l.h *  jobnr      / nb_jobs;
    const int line1 = l.h * (jobnr + 1) / nb_jobs;

    switch (p->type) {
    case PASS_DECOMPOSE:
        ff_wavelet97_decompose_lines(p->dst[0], p->dst[1], p->src[0], p->coeff,
                                     l.xstride, l.ystride, p->step, l.w, line0, line1);
        break;
    case PASS_COMPOSE:
        ff_wavelet97_compose_lines(p->dst[0], p->src[0], p->src[1],
                                   l.xstride, l.ystride, p->step, l.w, line0, line1);
        break;
    case PASS_DECOMPOSE_DECIMATED:
        ff_wavelet97_decompose_decimated_lines(p->dst[0], p->src[0], p->coeff,
                                               l.xstride, l.ystride, l.w, line0, line1);
        break;
    case PASS_COMPOSE_DECIMATED:
        ff_wavelet97_compose_decimated_lines(p->dst[0], p->src[0],
                                             l.xstride, l.ystride, l.w, line0, line1);
        break;
    case PASS_BOX:    box_lines(p, line0, line1);    break;
    case PASS_ENERGY: energy_lines(p, line0, line1); break;
    case PASS_GAIN:   gain_lines(p, line0, line1);   break;
    }
    return 0;
}

static void run_pass(AVFilterContext *ctx, WaveletPass *p)
{
    if (p->lat.h > 0)
        ff_filter_execute(ctx, run_pass_slice, p, NULL,
                          FFMIN(p->lat.h, ff_filter_get_nb_threads(ctx)));
}

static void decompose(AVFilterContext *ctx, float *dst[4], const float *src, float *temp[2],
                      const double coeff[2][5], int step, Lattice lat)
{
    WaveletPass p = { PASS_DECOMPOSE, lat, { temp[0], temp[1] }, { src }, coeff, step };

    run_pass(ctx, &p);
    p.lat = transpose(lat);
    p.src[0] = temp[0]; p.dst[0] = dst[0]; p.dst[1] = dst[1];
    run_pass(ctx, &p);
    p.src[0] = temp[1]; p.dst[0] = dst[2]; p.dst[1] = dst[3];
    run_pass(ctx, &p);
}

static void compose(AVFilterContext *ctx, float *dst, float *const src[4], float *temp[2],
                    int step, Lattice lat)
{
    WaveletPass p = { PASS_COMPOSE, transpose(lat), { temp[0] }, { src[0], src[1] }, NULL, step };

    run_pass(ctx, &p);
    p.dst[0] = temp[1]; p.src[0] = src[2]; p.src[1] = src[3];
    run_pass(ctx, &p);
    p.lat = lat;
    p.dst[0] = dst; p.src[0] = temp[0]; p.src[1] = temp[1];
    run_pass(ctx, &p);
}

static void decompose_decimated(AVFilterContext *ctx, float *plane, float *temp,
                                const double coeff[2][5], Lattice lat)
{
    WaveletPass p = { PASS_DECOMPOSE_DECIMATED, lat, { temp }, { plane }, coeff };

    run_pass(ctx, &p);
    p.lat = transpose(lat);
    p.dst[0] = plane; p.src[0] = temp;
    run_pass(ctx, &p);
}

static void compose_decimated(AVFilterContext *ctx, float *plane, float *temp, Lattice lat)
{
    WaveletPass p = { PASS_COMPOSE_DECIMATED, transpose(lat), { temp }, { plane } };

    run_pass(ctx, &p);
    p.lat = lat;
    p.dst[0] = plane; p.src[0] = temp;
    run_pass(ctx, &p);
}

static void box_filter(AVFilterContext *ctx, float *plane, float *temp, int step, Lattice lat)
{
    WaveletPass p = { PASS_BOX, lat, { temp }, { plane }, NULL, step };

    run_pass(ctx, &p);
    p.lat = transpose(lat);
    p.dst[0] = plane; p.src[0] = temp;
    run_pass(ctx, &p);
}

/**
 * Wiener shrinkage of a detail band toward 0, the signal variance is estimated
 * from a box of coefficients, weighted by their precision.
 */
static void shrink_band(AVFilterContext *ctx, float *band, const float *var, float *temp[3],
                        int step, Lattice lat)
{
    WaveletPass p = { PASS_ENERGY, lat, { temp[0], temp[1] }, { band, var } };

    run_pass(ctx, &p);
    box_filter(ctx, temp[0], temp[2], step, lat);
    box_filter(ctx, temp[1], temp[2], step, lat);
    p.type = PASS_GAIN;
    p.dst[0] = band; p.src[0] = temp[0]; p.src[1] = temp[1]; p.src[2] = var;
    run_pass(ctx, &p);
}

static void transform_undecimated(AVFilterContext *ctx, float *cur, float *var, int levels, Lattice lat)
{
    VignetteContext *s = ctx->priv;
    const size_t plane_size = s->wplane_size;
    float *next      = var + plane_size;
    float *temp[2]   = { var + 2 * plane_size, var + 3 * plane_size };
    float *vbands[4] = { var + 4 * plane_size, var + 5 * plane_size,
                         var + 6 * plane_size, var + 7 * plane_size };
    float *hbands    = var + 8 * plane_size;

    for (int i = 0; i < levels; i++) {
        float *bands[4] = { next, hbands + 3 * i * plane_size,
                            hbands + (3 * i + 1) * plane_size, hbands + (3 * i + 2) * plane_size };
        float *scratch[3] = { temp[0], temp[1], cur };

        decompose(ctx, bands,  cur, temp, ff_wavelet97_coeff, 1 << i, lat);
        decompose(ctx, vbands, var, temp, s->sqcoeff,         1 << i, lat);
        for (int j = 1; j < 4; j++)
            shrink_band(ctx, bands[j], vbands[j], scratch, 1 << i, lat);
        FFSWAP(float *, cur, next);
        FFSWAP(float *, var, vbands[0]);
    }

    for (int i = levels - 1; i >= 0; i--) {
        float *bands[4] = { cur, hbands + 3 * i * plane_size,
                            hbands + (3 * i + 1) * plane_size, hbands + (3 * i + 2) * plane_size };

        compose(ctx, next, bands, temp, 1 << i, lat);
        FFSWAP(float *, cur, next);
    }
}

/**
 * Lattice of the lowpass coefficients of the given level of a decimated transform
 */
static Lattice level_lattice(Lattice lat, int level)
{
    return (Lattice){ lat.xstride << level, lat.ystride << level,
                      AV_CEIL_RSHIFT(lat.w, level), AV_CEIL_RSHIFT(lat.h, level) };
}

static void transform_decimated(AVFilterContext *ctx, float *cur, float *var, int levels, Lattice lat)
{
    VignetteContext *s = ctx->priv;
    const size_t plane_size = s->wplane_size;
    float *temp[3] = { var + plane_size, var + 2 * plane_size, var + 3 * plane_size };

    for (int i = 0; i < levels; i++) {
        const Lattice level = level_lattice(lat, i);

        decompose_decimated(ctx, cur, temp[0], ff_wavelet97_coeff, level);
        decompose_decimated(ctx, var, temp[0], s->sqcoeff,         level);
        for (int j = 1; j < 4; j++) {
            const int offset = (j & 1) * level.xstride + (j >> 1) * level.ystride;
            const Lattice band = { 2 * level.xstride, 2 * level.ystride,
                                   (level.w + !(j & 1)) / 2, (level.h + !(j >> 1)) / 2 };
            float *scratch[3] = { temp[0] + offset, temp[1] + offset, temp[2] + offset };

            shrink_band(ctx, cur + offset, var + offset, scratch, 1, band);
        }
    }

    for (int i = levels - 1; i >= 0; i--)
        compose_decimated(ctx, cur, temp[0], level_lattice(lat, i));
}

static void filter_plane_wavelet(AVFilterContext *ctx,
                                 uint8_t *dst, int dst_linesize,
                                 const uint8_t *src, int src_linesize, int pixel_step,
                                 int w, int h, int hsub, int vsub, int bias)
{
    VignetteContext *s = ctx->priv;
    const int linesize = s->fmap_linesize;
    const Lattice lat  = { 1, linesize, w, h };
    float *cur = s->wplanes;
    float *var = cur + s->wplane_size;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const double a = FFMAX(s->fmap[(y << vsub) * linesize + (x << hsub)], 1e-6);

            cur[y * linesize + x] = av_clipd((src[y * src_linesize + x * pixel_step] - bias) / a,
                                             -bias, 255 - bias);
            var[y * linesize + x] = s->noise * s->noise / (a * a);
        }
    }

    if (s->decimated)
        transform_decimated(ctx, cur, var, wavelet_levels(w, h), lat);
    else
        transform_undecimated(ctx, cur, var, wavelet_levels(w, h), lat);

    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            dst[y * dst_linesize + x * pixel_step] =
                av_clip_uint8(cur[y * linesize + x] + bias + get_dither_value(s));
}

static void filter_frame_wavelet(AVFilterContext *ctx, AVFrame *out, const AVFrame *in)
{
    VignetteContext *s = ctx->priv;

    if (s->desc->flags & AV_PIX_FMT_FLAG_RGB) {
        for (int c = 0; c < 3; c++)
            filter_plane_wavelet(ctx, out->data[0] + c, out->linesize[0],
                                 in->data[0] + c, in->linesize[0], 3,
                                 in->width, in->height, 0, 0, 0);
        return;
    }

    for (int plane = 0; plane < 4 && in->data[plane] && in->linesize[plane]; plane++) {
        const int chroma = plane == 1 || plane == 2;
        const int hsub = chroma ? s->desc->log2_chroma_w : 0;
        const int vsub = chroma ? s->desc->log2_chroma_h : 0;

        filter_plane_wavelet(ctx, out->data[plane], out->linesize[plane],
                             in->data[plane], in->linesize[plane], 1,
                             AV_CEIL_RSHIFT(in->width, hsub), AV_CEIL_RSHIFT(in->height, vsub),
                             hsub, vsub, chroma ? 127 : 0);
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    unsigned x, y, direct = 0;
    AVFilterContext *ctx = inlink->dst;
    VignetteContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;

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

    if (s->eval_mode == EVAL_MODE_FRAME)
        update_context(s, inlink, in);

    if (s->mode == MODE_WAVELET) {
        filter_frame_wavelet(ctx, out, in);
    } else if (s->desc->flags & AV_PIX_FMT_FLAG_RGB) {
        uint8_t       *dst = out->data[0];
        const uint8_t *src = in ->data[0];
        const float *fmap = s->fmap;
        const int dst_linesize = out->linesize[0];
        const int src_linesize = in ->linesize[0];
        const int fmap_linesize = s->fmap_linesize;

        for (y = 0; y < inlink->h; y++) {
            uint8_t       *dstp = dst;
            const uint8_t *srcp = src;

            for (x = 0; x < inlink->w; x++, dstp += 3, srcp += 3) {
                const float f = fmap[x];

                dstp[0] = av_clip_uint8(srcp[0] * f + get_dither_value(s));
                dstp[1] = av_clip_uint8(srcp[1] * f + get_dither_value(s));
                dstp[2] = av_clip_uint8(srcp[2] * f + get_dither_value(s));
            }
            dst += dst_linesize;
            src += src_linesize;
            fmap += fmap_linesize;
        }
    } else {
        int plane;

        for (plane = 0; plane < 4 && in->data[plane] && in->linesize[plane]; plane++) {
            uint8_t       *dst = out->data[plane];
            const uint8_t *src = in ->data[plane];
            const float *fmap = s->fmap;
            const int dst_linesize = out->linesize[plane];
            const int src_linesize = in ->linesize[plane];
            const int fmap_linesize = s->fmap_linesize;
            const int chroma = plane == 1 || plane == 2;
            const int hsub = chroma ? s->desc->log2_chroma_w : 0;
            const int vsub = chroma ? s->desc->log2_chroma_h : 0;
            const int w = AV_CEIL_RSHIFT(inlink->w, hsub);
            const int h = AV_CEIL_RSHIFT(inlink->h, vsub);

            for (y = 0; y < h; y++) {
                uint8_t *dstp = dst;
                const uint8_t *srcp = src;

                for (x = 0; x < w; x++) {
                    const double dv = get_dither_value(s);
                    if (chroma) *dstp++ = av_clip_uint8(fmap[x << hsub] * (*srcp++ - 127) + 127 + dv);
                    else        *dstp++ = av_clip_uint8(fmap[x        ] *  *srcp++              + dv);
                }
                dst += dst_linesize;
                src += src_linesize;
                fmap += fmap_linesize << vsub;
            }
        }
    }

    if (!direct)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int config_props(AVFilterLink *inlink)
{
    AVFilterContext * ctx = inlink->dst;
    VignetteContext *s = ctx->priv;
    FilterLink *l = ff_filter_link(inlink);
    AVRational sar = inlink->sample_aspect_ratio;

    s->desc = av_pix_fmt_desc_get(inlink->format);
    s->var_values[VAR_W]  = inlink->w;
    s->var_values[VAR_H]  = inlink->h;
    s->var_values[VAR_TB] = av_q2d(inlink->time_base);
    s->var_values[VAR_R]  = l->frame_rate.num == 0 || l->frame_rate.den == 0 ?
        NAN : av_q2d(l->frame_rate);

    if (!sar.num || !sar.den)
        sar.num = sar.den = 1;
    if (sar.num > sar.den) {
        s->xscale = av_q2d(av_div_q(sar, s->aspect));
        s->yscale = 1;
    } else {
        s->yscale = av_q2d(av_div_q(s->aspect, sar));
        s->xscale = 1;
    }
    s->dmax = hypot(inlink->w / 2., inlink->h / 2.);
    av_log(ctx, AV_LOG_DEBUG, "xscale=%f yscale=%f dmax=%f\n",
           s->xscale, s->yscale, s->dmax);

    s->fmap_linesize = FFALIGN(inlink->w, 32);
    s->fmap = av_malloc_array(s->fmap_linesize, inlink->h * sizeof(*s->fmap));
    if (!s->fmap)
        return AVERROR(ENOMEM);

    if (s->mode == MODE_WAVELET) {
        s->wplane_size = (size_t)s->fmap_linesize * inlink->h;
        s->wplanes = av_malloc_array(s->wplane_size, (s->decimated ? 5 : 9 + 3 * wavelet_levels(inlink->w, inlink->h)) * sizeof(*s->wplanes));
        if (!s->wplanes)
            return AVERROR(ENOMEM);
    }

    if (s->eval_mode == EVAL_MODE_INIT)
        update_context(s, inlink, NULL);

    return 0;
}

static const AVFilterPad vignette_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_props,
    },
};

const FFFilter ff_vf_vignette = {
    .p.name        = "vignette",
    .p.description = NULL_IF_CONFIG_SMALL("Make or reverse a vignette effect."),
    .p.priv_class  = &vignette_class,
    .p.flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .priv_size     = sizeof(VignetteContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(vignette_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
};
