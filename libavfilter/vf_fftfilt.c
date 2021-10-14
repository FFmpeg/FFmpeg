/*
 * Copyright (c) 2015 Arwa Arif <arwaarif1994@gmail.com>
 * Copyright (c) 2017 Paul B Mahol
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2.1 of the License,
 * or (at your option) any later version.
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
 * FFT domain filtering.
 */

#include "libavfilter/internal.h"
#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavcodec/avfft.h"
#include "libavutil/eval.h"

#define MAX_THREADS 32
#define MAX_PLANES 4

enum EvalMode {
    EVAL_MODE_INIT,
    EVAL_MODE_FRAME,
    EVAL_MODE_NB
};

typedef struct FFTFILTContext {
    const AVClass *class;

    int eval_mode;
    int depth;
    int nb_planes;
    int nb_threads;
    int planewidth[MAX_PLANES];
    int planeheight[MAX_PLANES];

    RDFTContext *hrdft[MAX_THREADS][MAX_PLANES];
    RDFTContext *vrdft[MAX_THREADS][MAX_PLANES];
    RDFTContext *ihrdft[MAX_THREADS][MAX_PLANES];
    RDFTContext *ivrdft[MAX_THREADS][MAX_PLANES];
    int rdft_hbits[MAX_PLANES];
    int rdft_vbits[MAX_PLANES];
    size_t rdft_hlen[MAX_PLANES];
    size_t rdft_vlen[MAX_PLANES];
    FFTSample *rdft_hdata[MAX_PLANES];
    FFTSample *rdft_vdata[MAX_PLANES];

    int dc[MAX_PLANES];
    char *weight_str[MAX_PLANES];
    AVExpr *weight_expr[MAX_PLANES];
    double *weight[MAX_PLANES];

    int (*rdft_horizontal)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
    int (*irdft_horizontal)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} FFTFILTContext;

static const char *const var_names[] = {   "X",   "Y",   "W",   "H",   "N",   "WS",   "HS", NULL        };
enum                                   { VAR_X, VAR_Y, VAR_W, VAR_H, VAR_N, VAR_WS, VAR_HS, VAR_VARS_NB };

enum { Y = 0, U, V };

#define OFFSET(x) offsetof(FFTFILTContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption fftfilt_options[] = {
    { "dc_Y",  "adjust gain in Y plane",              OFFSET(dc[Y]),      AV_OPT_TYPE_INT,    {.i64 = 0},      0,     1000,     FLAGS },
    { "dc_U",  "adjust gain in U plane",              OFFSET(dc[U]),      AV_OPT_TYPE_INT,    {.i64 = 0},      0,     1000,     FLAGS },
    { "dc_V",  "adjust gain in V plane",              OFFSET(dc[V]),      AV_OPT_TYPE_INT,    {.i64 = 0},      0,     1000,     FLAGS },
    { "weight_Y", "set luminance expression in Y plane",   OFFSET(weight_str[Y]), AV_OPT_TYPE_STRING, {.str = "1"}, 0, 0, FLAGS },
    { "weight_U", "set chrominance expression in U plane", OFFSET(weight_str[U]), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
    { "weight_V", "set chrominance expression in V plane", OFFSET(weight_str[V]), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
    { "eval", "specify when to evaluate expressions", OFFSET(eval_mode), AV_OPT_TYPE_INT, {.i64 = EVAL_MODE_INIT}, 0, EVAL_MODE_NB-1, FLAGS, "eval" },
         { "init",  "eval expressions once during initialization", 0, AV_OPT_TYPE_CONST, {.i64=EVAL_MODE_INIT},  .flags = FLAGS, .unit = "eval" },
         { "frame", "eval expressions per-frame",                  0, AV_OPT_TYPE_CONST, {.i64=EVAL_MODE_FRAME}, .flags = FLAGS, .unit = "eval" },
    {NULL},
};

AVFILTER_DEFINE_CLASS(fftfilt);

static inline double lum(void *priv, double x, double y, int plane)
{
    FFTFILTContext *s = priv;
    return s->rdft_vdata[plane][(int)x * s->rdft_vlen[plane] + (int)y];
}

static double weight_Y(void *priv, double x, double y) { return lum(priv, x, y, Y); }
static double weight_U(void *priv, double x, double y) { return lum(priv, x, y, U); }
static double weight_V(void *priv, double x, double y) { return lum(priv, x, y, V); }

static void copy_rev(FFTSample *dest, int w, int w2)
{
    int i;

    for (i = w; i < w + (w2-w)/2; i++)
        dest[i] = dest[2*w - i - 1];

    for (; i < w2; i++)
        dest[i] = dest[w2 - i];
}

static int rdft_horizontal8(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    FFTFILTContext *s = ctx->priv;
    AVFrame *in = arg;

    for (int plane = 0; plane < s->nb_planes; plane++) {
        const int w = s->planewidth[plane];
        const int h = s->planeheight[plane];
        const int slice_start = (h * jobnr) / nb_jobs;
        const int slice_end = (h * (jobnr+1)) / nb_jobs;

        for (int i = slice_start; i < slice_end; i++) {
            const uint8_t *src = in->data[plane] + i * in->linesize[plane];
            float *hdata = s->rdft_hdata[plane] + i * s->rdft_hlen[plane];

            for (int j = 0; j < w; j++)
                hdata[j] = src[j];

            copy_rev(s->rdft_hdata[plane] + i * s->rdft_hlen[plane], w, s->rdft_hlen[plane]);
        }

        for (int i = slice_start; i < slice_end; i++)
            av_rdft_calc(s->hrdft[jobnr][plane], s->rdft_hdata[plane] + i * s->rdft_hlen[plane]);
    }

    return 0;
}

static int rdft_horizontal16(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    FFTFILTContext *s = ctx->priv;
    AVFrame *in = arg;

    for (int plane = 0; plane < s->nb_planes; plane++) {
        const int w = s->planewidth[plane];
        const int h = s->planeheight[plane];
        const int slice_start = (h * jobnr) / nb_jobs;
        const int slice_end = (h * (jobnr+1)) / nb_jobs;

        for (int i = slice_start; i < slice_end; i++) {
            const uint16_t *src = (const uint16_t *)(in->data[plane] + i * in->linesize[plane]);
            float *hdata = s->rdft_hdata[plane] + i * s->rdft_hlen[plane];

            for (int j = 0; j < w; j++)
                hdata[j] = src[j];

            copy_rev(s->rdft_hdata[plane] + i * s->rdft_hlen[plane], w, s->rdft_hlen[plane]);
        }

        for (int i = slice_start; i < slice_end; i++)
            av_rdft_calc(s->hrdft[jobnr][plane], s->rdft_hdata[plane] + i * s->rdft_hlen[plane]);
    }

    return 0;
}

static int irdft_horizontal8(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    FFTFILTContext *s = ctx->priv;
    AVFrame *out = arg;

    for (int plane = 0; plane < s->nb_planes; plane++) {
        const int w = s->planewidth[plane];
        const int h = s->planeheight[plane];
        const int slice_start = (h * jobnr) / nb_jobs;
        const int slice_end = (h * (jobnr+1)) / nb_jobs;

        for (int i = slice_start; i < slice_end; i++)
            av_rdft_calc(s->ihrdft[jobnr][plane], s->rdft_hdata[plane] + i * s->rdft_hlen[plane]);

        for (int i = slice_start; i < slice_end; i++) {
            const float scale = 4.f / (s->rdft_hlen[plane] * s->rdft_vlen[plane]);
            const float *src = s->rdft_hdata[plane] + i * s->rdft_hlen[plane];
            uint8_t *dst = out->data[plane] + i * out->linesize[plane];

            for (int j = 0; j < w; j++)
                dst[j] = av_clip_uint8(lrintf(src[j] * scale));
        }
    }

    return 0;
}

static int irdft_horizontal16(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    FFTFILTContext *s = ctx->priv;
    AVFrame *out = arg;

    for (int plane = 0; plane < s->nb_planes; plane++) {
        int max = (1 << s->depth) - 1;
        const int w = s->planewidth[plane];
        const int h = s->planeheight[plane];
        const int slice_start = (h * jobnr) / nb_jobs;
        const int slice_end = (h * (jobnr+1)) / nb_jobs;

        for (int i = slice_start; i < slice_end; i++)
            av_rdft_calc(s->ihrdft[jobnr][plane], s->rdft_hdata[plane] + i * s->rdft_hlen[plane]);

        for (int i = slice_start; i < slice_end; i++) {
            const float scale = 4.f / (s->rdft_hlen[plane] * s->rdft_vlen[plane]);
            const float *src = s->rdft_hdata[plane] + i * s->rdft_hlen[plane];
            uint16_t *dst = (uint16_t *)(out->data[plane] + i * out->linesize[plane]);

            for (int j = 0; j < w; j++)
                dst[j] = av_clip(lrintf(src[j] * scale), 0, max);
        }
    }

    return 0;
}

static av_cold int initialize(AVFilterContext *ctx)
{
    FFTFILTContext *s = ctx->priv;
    int ret = 0, plane;

    if (!s->dc[U] && !s->dc[V]) {
        s->dc[U] = s->dc[Y];
        s->dc[V] = s->dc[Y];
    } else {
        if (!s->dc[U]) s->dc[U] = s->dc[V];
        if (!s->dc[V]) s->dc[V] = s->dc[U];
    }

    if (!s->weight_str[U] && !s->weight_str[V]) {
        s->weight_str[U] = av_strdup(s->weight_str[Y]);
        s->weight_str[V] = av_strdup(s->weight_str[Y]);
    } else {
        if (!s->weight_str[U]) s->weight_str[U] = av_strdup(s->weight_str[V]);
        if (!s->weight_str[V]) s->weight_str[V] = av_strdup(s->weight_str[U]);
    }

    for (plane = 0; plane < 3; plane++) {
        static double (*p[])(void *, double, double) = { weight_Y, weight_U, weight_V };
        const char *const func2_names[] = {"weight_Y", "weight_U", "weight_V", NULL };
        double (*func2[])(void *, double, double) = { weight_Y, weight_U, weight_V, p[plane], NULL };

        ret = av_expr_parse(&s->weight_expr[plane], s->weight_str[plane], var_names,
                            NULL, NULL, func2_names, func2, 0, ctx);
        if (ret < 0)
            break;
    }
    return ret;
}

static void do_eval(FFTFILTContext *s, AVFilterLink *inlink, int plane)
{
    double values[VAR_VARS_NB];
    int i, j;

    values[VAR_N] = inlink->frame_count_out;
    values[VAR_W] = s->planewidth[plane];
    values[VAR_H] = s->planeheight[plane];
    values[VAR_WS] = s->rdft_hlen[plane];
    values[VAR_HS] = s->rdft_vlen[plane];

    for (i = 0; i < s->rdft_hlen[plane]; i++) {
        values[VAR_X] = i;
        for (j = 0; j < s->rdft_vlen[plane]; j++) {
            values[VAR_Y] = j;
            s->weight[plane][i * s->rdft_vlen[plane] + j] =
            av_expr_eval(s->weight_expr[plane], values, s);
        }
    }
}

static int config_props(AVFilterLink *inlink)
{
    FFTFILTContext *s = inlink->dst->priv;
    const AVPixFmtDescriptor *desc;
    int i, plane;

    desc = av_pix_fmt_desc_get(inlink->format);
    s->depth = desc->comp[0].depth;
    s->planewidth[1] = s->planewidth[2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0] = s->planewidth[3] = inlink->w;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);
    s->nb_threads = FFMIN(32, ff_filter_get_nb_threads(inlink->dst));

    for (i = 0; i < desc->nb_components; i++) {
        int w = s->planewidth[i];
        int h = s->planeheight[i];

        /* RDFT - Array initialization for Horizontal pass*/
        s->rdft_hlen[i] = 1 << (32 - ff_clz(w));
        s->rdft_hbits[i] = av_log2(s->rdft_hlen[i]);
        if (!(s->rdft_hdata[i] = av_malloc_array(h, s->rdft_hlen[i] * sizeof(FFTSample))))
            return AVERROR(ENOMEM);

        for (int j = 0; j < s->nb_threads; j++) {
            if (!(s->hrdft[j][i] = av_rdft_init(s->rdft_hbits[i], DFT_R2C)))
                return AVERROR(ENOMEM);
            if (!(s->ihrdft[j][i] = av_rdft_init(s->rdft_hbits[i], IDFT_C2R)))
                return AVERROR(ENOMEM);
        }

        /* RDFT - Array initialization for Vertical pass*/
        s->rdft_vlen[i] = 1 << (32 - ff_clz(h));
        s->rdft_vbits[i] = av_log2(s->rdft_vlen[i]);
        if (!(s->rdft_vdata[i] = av_malloc_array(s->rdft_hlen[i], s->rdft_vlen[i] * sizeof(FFTSample))))
            return AVERROR(ENOMEM);

        for (int j = 0; j < s->nb_threads; j++) {
            if (!(s->vrdft[j][i] = av_rdft_init(s->rdft_vbits[i], DFT_R2C)))
                return AVERROR(ENOMEM);
            if (!(s->ivrdft[j][i] = av_rdft_init(s->rdft_vbits[i], IDFT_C2R)))
                return AVERROR(ENOMEM);
        }
    }

    /*Luminance value - Array initialization*/
    for (plane = 0; plane < 3; plane++) {
        if(!(s->weight[plane] = av_malloc_array(s->rdft_hlen[plane], s->rdft_vlen[plane] * sizeof(double))))
            return AVERROR(ENOMEM);

        if (s->eval_mode == EVAL_MODE_INIT)
            do_eval(s, inlink, plane);
    }

    if (s->depth <= 8) {
        s->rdft_horizontal = rdft_horizontal8;
        s->irdft_horizontal = irdft_horizontal8;
    } else if (s->depth > 8) {
        s->rdft_horizontal = rdft_horizontal16;
        s->irdft_horizontal = irdft_horizontal16;
    } else {
        return AVERROR_BUG;
    }
    return 0;
}

static int multiply_data(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    FFTFILTContext *s = ctx->priv;

    for (int plane = 0; plane < s->nb_planes; plane++) {
        const int height = s->rdft_hlen[plane];
        const int slice_start = (height * jobnr) / nb_jobs;
        const int slice_end = (height * (jobnr+1)) / nb_jobs;
        /*Change user defined parameters*/
        for (int i = slice_start; i < slice_end; i++) {
            const double *weight = s->weight[plane] + i * s->rdft_vlen[plane];
            float *vdata = s->rdft_vdata[plane] + i * s->rdft_vlen[plane];

            for (int j = 0; j < s->rdft_vlen[plane]; j++)
                vdata[j] *= weight[j];
        }
    }

    return 0;
}

static int copy_vertical(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    FFTFILTContext *s = ctx->priv;

    for (int plane = 0; plane < s->nb_planes; plane++) {
        const int hlen = s->rdft_hlen[plane];
        const int vlen = s->rdft_vlen[plane];
        const int slice_start = (hlen * jobnr) / nb_jobs;
        const int slice_end = (hlen * (jobnr+1)) / nb_jobs;
        const int h = s->planeheight[plane];
        FFTSample *hdata = s->rdft_hdata[plane];
        FFTSample *vdata = s->rdft_vdata[plane];

        for (int i = slice_start; i < slice_end; i++) {
            for (int j = 0; j < h; j++)
                vdata[i * vlen + j] = hdata[j * hlen + i];
            copy_rev(vdata + i * vlen, h, vlen);
        }
    }

    return 0;
}

static int rdft_vertical(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    FFTFILTContext *s = ctx->priv;

    for (int plane = 0; plane < s->nb_planes; plane++) {
        const int height = s->rdft_hlen[plane];
        const int slice_start = (height * jobnr) / nb_jobs;
        const int slice_end = (height * (jobnr+1)) / nb_jobs;

        for (int i = slice_start; i < slice_end; i++)
            av_rdft_calc(s->vrdft[jobnr][plane], s->rdft_vdata[plane] + i * s->rdft_vlen[plane]);
    }

    return 0;
}

static int irdft_vertical(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    FFTFILTContext *s = ctx->priv;

    for (int plane = 0; plane < s->nb_planes; plane++) {
        const int height = s->rdft_hlen[plane];
        const int slice_start = (height * jobnr) / nb_jobs;
        const int slice_end = (height * (jobnr+1)) / nb_jobs;

        for (int i = slice_start; i < slice_end; i++)
            av_rdft_calc(s->ivrdft[jobnr][plane], s->rdft_vdata[plane] + i * s->rdft_vlen[plane]);
    }

    return 0;
}

static int copy_horizontal(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    FFTFILTContext *s = ctx->priv;

    for (int plane = 0; plane < s->nb_planes; plane++) {
        const int hlen = s->rdft_hlen[plane];
        const int vlen = s->rdft_vlen[plane];
        const int slice_start = (hlen * jobnr) / nb_jobs;
        const int slice_end = (hlen * (jobnr+1)) / nb_jobs;
        const int h = s->planeheight[plane];
        FFTSample *hdata = s->rdft_hdata[plane];
        FFTSample *vdata = s->rdft_vdata[plane];

        for (int i = slice_start; i < slice_end; i++)
            for (int j = 0; j < h; j++)
                hdata[j * hlen + i] = vdata[i * vlen + j];
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    FFTFILTContext *s = ctx->priv;
    AVFrame *out;

    out = ff_get_video_buffer(outlink, inlink->w, inlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }

    av_frame_copy_props(out, in);

    for (int plane = 0; plane < s->nb_planes; plane++) {
        if (s->eval_mode == EVAL_MODE_FRAME)
            do_eval(s, inlink, plane);
    }

    ff_filter_execute(ctx, s->rdft_horizontal, in, NULL,
                      FFMIN(s->planeheight[1], s->nb_threads));

    ff_filter_execute(ctx, copy_vertical, NULL, NULL,
                      FFMIN(s->planeheight[1], s->nb_threads));

    ff_filter_execute(ctx, rdft_vertical, NULL, NULL,
                      FFMIN(s->planeheight[1], s->nb_threads));

    ff_filter_execute(ctx, multiply_data, NULL, NULL,
                      FFMIN(s->planeheight[1], s->nb_threads));

    for (int plane = 0; plane < s->nb_planes; plane++)
        s->rdft_vdata[plane][0] += s->rdft_hlen[plane] * s->rdft_vlen[plane] * s->dc[plane];

    ff_filter_execute(ctx, irdft_vertical, NULL, NULL,
                      FFMIN(s->planeheight[1], s->nb_threads));

    ff_filter_execute(ctx, copy_horizontal, NULL, NULL,
                      FFMIN(s->planeheight[1], s->nb_threads));

    ff_filter_execute(ctx, s->irdft_horizontal, out, NULL,
                      FFMIN(s->planeheight[1], s->nb_threads));

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    FFTFILTContext *s = ctx->priv;
    int i;
    for (i = 0; i < MAX_PLANES; i++) {
        av_free(s->rdft_hdata[i]);
        av_free(s->rdft_vdata[i]);
        av_expr_free(s->weight_expr[i]);
        av_free(s->weight[i]);
        for (int j = 0; j < s->nb_threads; j++) {
            av_rdft_end(s->hrdft[j][i]);
            av_rdft_end(s->ihrdft[j][i]);
            av_rdft_end(s->vrdft[j][i]);
            av_rdft_end(s->ivrdft[j][i]);
        }
    }
}

static const enum AVPixelFormat pixel_fmts_fftfilt[] = {
    AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12,
    AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
    AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV420P10,
    AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV420P14,
    AV_PIX_FMT_YUV420P16,
    AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV422P10,
    AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV422P14,
    AV_PIX_FMT_YUV422P16,
    AV_PIX_FMT_YUV444P9, AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV444P14,
    AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_NONE
};

static const AVFilterPad fftfilt_inputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad fftfilt_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

const AVFilter ff_vf_fftfilt = {
    .name            = "fftfilt",
    .description     = NULL_IF_CONFIG_SMALL("Apply arbitrary expressions to pixels in frequency domain."),
    .priv_size       = sizeof(FFTFILTContext),
    .priv_class      = &fftfilt_class,
    FILTER_INPUTS(fftfilt_inputs),
    FILTER_OUTPUTS(fftfilt_outputs),
    FILTER_PIXFMTS_ARRAY(pixel_fmts_fftfilt),
    .init            = initialize,
    .uninit          = uninit,
    .flags           = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};
