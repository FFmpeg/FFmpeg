/*
 * Copyright (c) 2015 Arwa Arif <arwaarif1994@gmail.com>
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

#define MAX_PLANES 4

typedef struct {
    const AVClass *class;

    RDFTContext *rdft;
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

} FFTFILTContext;

static const char *const var_names[] = {   "X",   "Y",   "W",   "H",     NULL    };
enum                                   { VAR_X, VAR_Y, VAR_W, VAR_H, VAR_VARS_NB };

enum { Y = 0, U, V };

#define OFFSET(x) offsetof(FFTFILTContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption fftfilt_options[] = {
    { "dc_Y",  "adjust gain in Y plane",              OFFSET(dc[Y]),      AV_OPT_TYPE_INT,    {.i64 = 0},      0,     1000,     FLAGS },
    { "dc_U",  "adjust gain in U plane",              OFFSET(dc[U]),      AV_OPT_TYPE_INT,    {.i64 = 0},      0,     1000,     FLAGS },
    { "dc_V",  "adjust gain in V plane",              OFFSET(dc[V]),      AV_OPT_TYPE_INT,    {.i64 = 0},      0,     1000,     FLAGS },
    { "weight_Y", "set luminance expression in Y plane",   OFFSET(weight_str[Y]), AV_OPT_TYPE_STRING, {.str = "1"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "weight_U", "set chrominance expression in U plane", OFFSET(weight_str[U]), AV_OPT_TYPE_STRING, {.str = NULL}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "weight_V", "set chrominance expression in V plane", OFFSET(weight_str[V]), AV_OPT_TYPE_STRING, {.str = NULL}, CHAR_MIN, CHAR_MAX, FLAGS },
    {NULL},
};

AVFILTER_DEFINE_CLASS(fftfilt);

static inline double lum(void *priv, double x, double y, int plane)
{
    FFTFILTContext *fftfilt = priv;
    return fftfilt->rdft_vdata[plane][(int)x * fftfilt->rdft_vlen[plane] + (int)y];
}

static double weight_Y(void *priv, double x, double y) { return lum(priv, x, y, Y); }
static double weight_U(void *priv, double x, double y) { return lum(priv, x, y, U); }
static double weight_V(void *priv, double x, double y) { return lum(priv, x, y, V); }

static void copy_rev (FFTSample *dest, int w, int w2)
{
    int i;

    for (i = w; i < w + (w2-w)/2; i++)
        dest[i] = dest[2*w - i - 1];

    for (; i < w2; i++)
        dest[i] = dest[w2 - i];
}

/*Horizontal pass - RDFT*/
static void rdft_horizontal(FFTFILTContext *fftfilt, AVFrame *in, int w, int h, int plane)
{
    int i, j;
    fftfilt->rdft = av_rdft_init(fftfilt->rdft_hbits[plane], DFT_R2C);

    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++)
            fftfilt->rdft_hdata[plane][i * fftfilt->rdft_hlen[plane] + j] = *(in->data[plane] + in->linesize[plane] * i + j);

        copy_rev(fftfilt->rdft_hdata[plane] + i * fftfilt->rdft_hlen[plane], w, fftfilt->rdft_hlen[plane]);
    }

    for (i = 0; i < h; i++)
        av_rdft_calc(fftfilt->rdft, fftfilt->rdft_hdata[plane] + i * fftfilt->rdft_hlen[plane]);

    av_rdft_end(fftfilt->rdft);
}

/*Vertical pass - RDFT*/
static void rdft_vertical(FFTFILTContext *fftfilt, int h, int plane)
{
    int i, j;
    fftfilt->rdft = av_rdft_init(fftfilt->rdft_vbits[plane], DFT_R2C);

    for (i = 0; i < fftfilt->rdft_hlen[plane]; i++) {
        for (j = 0; j < h; j++)
            fftfilt->rdft_vdata[plane][i * fftfilt->rdft_vlen[plane] + j] =
            fftfilt->rdft_hdata[plane][j * fftfilt->rdft_hlen[plane] + i];
        copy_rev(fftfilt->rdft_vdata[plane] + i * fftfilt->rdft_vlen[plane], h, fftfilt->rdft_vlen[plane]);
    }

    for (i = 0; i < fftfilt->rdft_hlen[plane]; i++)
        av_rdft_calc(fftfilt->rdft, fftfilt->rdft_vdata[plane] + i * fftfilt->rdft_vlen[plane]);

    av_rdft_end(fftfilt->rdft);
}
/*Vertical pass - IRDFT*/
static void irdft_vertical(FFTFILTContext *fftfilt, int h, int plane)
{
    int i, j;
    fftfilt->rdft = av_rdft_init(fftfilt->rdft_vbits[plane], IDFT_C2R);
    for (i = 0; i < fftfilt->rdft_hlen[plane]; i++)
        av_rdft_calc(fftfilt->rdft, fftfilt->rdft_vdata[plane] + i * fftfilt->rdft_vlen[plane]);

    for (i = 0; i < fftfilt->rdft_hlen[plane]; i++)
        for (j = 0; j < h; j++)
            fftfilt->rdft_hdata[plane][j * fftfilt->rdft_hlen[plane] + i] =
            fftfilt->rdft_vdata[plane][i * fftfilt->rdft_vlen[plane] + j];

    av_rdft_end(fftfilt->rdft);
}

/*Horizontal pass - IRDFT*/
static void irdft_horizontal(FFTFILTContext *fftfilt, AVFrame *out, int w, int h, int plane)
{
    int i, j;
    fftfilt->rdft = av_rdft_init(fftfilt->rdft_hbits[plane], IDFT_C2R);
    for (i = 0; i < h; i++)
        av_rdft_calc(fftfilt->rdft, fftfilt->rdft_hdata[plane] + i * fftfilt->rdft_hlen[plane]);

    for (i = 0; i < h; i++)
        for (j = 0; j < w; j++)
            *(out->data[plane] + out->linesize[plane] * i + j) = av_clip(fftfilt->rdft_hdata[plane][i
                                                                         *fftfilt->rdft_hlen[plane] + j] * 4 /
                                                                         (fftfilt->rdft_hlen[plane] *
                                                                          fftfilt->rdft_vlen[plane]), 0, 255);

    av_rdft_end(fftfilt->rdft);
}

static av_cold int initialize(AVFilterContext *ctx)
{
    FFTFILTContext *fftfilt = ctx->priv;
    int ret = 0, plane;

    if (!fftfilt->dc[U] && !fftfilt->dc[V]) {
        fftfilt->dc[U] = fftfilt->dc[Y];
        fftfilt->dc[V] = fftfilt->dc[Y];
    } else {
        if (!fftfilt->dc[U]) fftfilt->dc[U] = fftfilt->dc[V];
        if (!fftfilt->dc[V]) fftfilt->dc[V] = fftfilt->dc[U];
    }

    if (!fftfilt->weight_str[U] && !fftfilt->weight_str[V]) {
        fftfilt->weight_str[U] = av_strdup(fftfilt->weight_str[Y]);
        fftfilt->weight_str[V] = av_strdup(fftfilt->weight_str[Y]);
    } else {
        if (!fftfilt->weight_str[U]) fftfilt->weight_str[U] = av_strdup(fftfilt->weight_str[V]);
        if (!fftfilt->weight_str[V]) fftfilt->weight_str[V] = av_strdup(fftfilt->weight_str[U]);
    }

    for (plane = 0; plane < 3; plane++) {
        static double (*p[])(void *, double, double) = { weight_Y, weight_U, weight_V };
        const char *const func2_names[] = {"weight_Y", "weight_U", "weight_V", NULL };
        double (*func2[])(void *, double, double) = { weight_Y, weight_U, weight_V, p[plane], NULL };

        ret = av_expr_parse(&fftfilt->weight_expr[plane], fftfilt->weight_str[plane], var_names,
                            NULL, NULL, func2_names, func2, 0, ctx);
        if (ret < 0)
            break;
    }
    return ret;
}

static int config_props(AVFilterLink *inlink)
{
    FFTFILTContext *fftfilt = inlink->dst->priv;
    const AVPixFmtDescriptor *desc;
    int rdft_hbits, rdft_vbits, i, j, plane;
    double values[VAR_VARS_NB];

    desc = av_pix_fmt_desc_get(inlink->format);
    for (i = 0; i < desc->nb_components; i++) {
        int w = inlink->w;
        int h = inlink->h;

        /* RDFT - Array initialization for Horizontal pass*/
        for (rdft_hbits = 1; 1 << rdft_hbits < w*10/9; rdft_hbits++);
        fftfilt->rdft_hbits[i] = rdft_hbits;
        fftfilt->rdft_hlen[i] = 1 << rdft_hbits;
        if (!(fftfilt->rdft_hdata[i] = av_malloc_array(h, fftfilt->rdft_hlen[i] * sizeof(FFTSample))))
            return AVERROR(ENOMEM);

        /* RDFT - Array initialization for Vertical pass*/
        for (rdft_vbits = 1; 1 << rdft_vbits < h*10/9; rdft_vbits++);
        fftfilt->rdft_vbits[i] = rdft_vbits;
        fftfilt->rdft_vlen[i] = 1 << rdft_vbits;
        if (!(fftfilt->rdft_vdata[i] = av_malloc_array(fftfilt->rdft_hlen[i], fftfilt->rdft_vlen[i] * sizeof(FFTSample))))
            return AVERROR(ENOMEM);
    }

    /*Luminance value - Array initialization*/
    values[VAR_W] = inlink->w;
    values[VAR_H] = inlink->h;
    for (plane = 0; plane < 3; plane++)
    {
        if(!(fftfilt->weight[plane] = av_malloc_array(fftfilt->rdft_hlen[plane], fftfilt->rdft_vlen[plane] * sizeof(double))))
            return AVERROR(ENOMEM);
        for (i = 0; i < fftfilt->rdft_hlen[plane]; i++)
        {
            values[VAR_X] = i;
            for (j = 0; j < fftfilt->rdft_vlen[plane]; j++)
            {
                values[VAR_Y] = j;
                fftfilt->weight[plane][i * fftfilt->rdft_vlen[plane] + j] =
                av_expr_eval(fftfilt->weight_expr[plane], values, fftfilt);
            }
        }
    }
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    const AVPixFmtDescriptor *desc;
    FFTFILTContext *fftfilt = ctx->priv;
    AVFrame *out;
    int i, j, plane;

    out = ff_get_video_buffer(outlink, inlink->w, inlink->h);
    if (!out)
        return AVERROR(ENOMEM);

    av_frame_copy_props(out, in);

    desc = av_pix_fmt_desc_get(inlink->format);
    for (plane = 0; plane < desc->nb_components; plane++) {
        int w = inlink->w;
        int h = inlink->h;

        if (plane == 1 || plane == 2) {
            w = FF_CEIL_RSHIFT(w, desc->log2_chroma_w);
            h = FF_CEIL_RSHIFT(h, desc->log2_chroma_h);
        }

        rdft_horizontal(fftfilt, in, w, h, plane);
        rdft_vertical(fftfilt, h, plane);

        /*Change user defined parameters*/
        for (i = 0; i < fftfilt->rdft_hlen[plane]; i++)
            for (j = 0; j < fftfilt->rdft_vlen[plane]; j++)
                fftfilt->rdft_vdata[plane][i * fftfilt->rdft_vlen[plane] + j] *=
                  fftfilt->weight[plane][i * fftfilt->rdft_vlen[plane] + j];

        fftfilt->rdft_vdata[plane][0] += fftfilt->rdft_hlen[plane] * fftfilt->rdft_vlen[plane] * fftfilt->dc[plane];

        irdft_vertical(fftfilt, h, plane);
        irdft_horizontal(fftfilt, out, w, h, plane);
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    FFTFILTContext *fftfilt = ctx->priv;
    int i;
    for (i = 0; i < MAX_PLANES; i++) {
        av_free(fftfilt->rdft_hdata[i]);
        av_free(fftfilt->rdft_vdata[i]);
        av_expr_free(fftfilt->weight_expr[i]);
        av_free(fftfilt->weight[i]);
    }
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pixel_fmts_fftfilt[] = {
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *fmts_list = ff_make_format_list(pixel_fmts_fftfilt);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static const AVFilterPad fftfilt_inputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad fftfilt_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_fftfilt = {
    .name            = "fftfilt",
    .description     = NULL_IF_CONFIG_SMALL("Apply arbitrary expressions to samples in frequency domain."),
    .priv_size       = sizeof(FFTFILTContext),
    .priv_class      = &fftfilt_class,
    .inputs          = fftfilt_inputs,
    .outputs         = fftfilt_outputs,
    .query_formats   = query_formats,
    .init            = initialize,
    .uninit          = uninit,
};
