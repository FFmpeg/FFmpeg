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
    int planewidth[MAX_PLANES];
    int planeheight[MAX_PLANES];

    RDFTContext *hrdft[MAX_PLANES];
    RDFTContext *vrdft[MAX_PLANES];
    RDFTContext *ihrdft[MAX_PLANES];
    RDFTContext *ivrdft[MAX_PLANES];
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

    void (*rdft_horizontal)(struct FFTFILTContext *s, AVFrame *in, int w, int h, int plane);
    void (*irdft_horizontal)(struct FFTFILTContext *s, AVFrame *out, int w, int h, int plane);
} FFTFILTContext;

static const char *const var_names[] = {   "X",   "Y",   "W",   "H",   "N", NULL        };
enum                                   { VAR_X, VAR_Y, VAR_W, VAR_H, VAR_N, VAR_VARS_NB };

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

static void copy_rev (FFTSample *dest, int w, int w2)
{
    int i;

    for (i = w; i < w + (w2-w)/2; i++)
        dest[i] = dest[2*w - i - 1];

    for (; i < w2; i++)
        dest[i] = dest[w2 - i];
}

/*Horizontal pass - RDFT*/
static void rdft_horizontal8(FFTFILTContext *s, AVFrame *in, int w, int h, int plane)
{
    int i, j;

    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++)
            s->rdft_hdata[plane][i * s->rdft_hlen[plane] + j] = *(in->data[plane] + in->linesize[plane] * i + j);

        copy_rev(s->rdft_hdata[plane] + i * s->rdft_hlen[plane], w, s->rdft_hlen[plane]);
    }

    for (i = 0; i < h; i++)
        av_rdft_calc(s->hrdft[plane], s->rdft_hdata[plane] + i * s->rdft_hlen[plane]);
}

static void rdft_horizontal16(FFTFILTContext *s, AVFrame *in, int w, int h, int plane)
{
    const uint16_t *src = (const uint16_t *)in->data[plane];
    int linesize = in->linesize[plane] / 2;
    int i, j;

    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++)
            s->rdft_hdata[plane][i * s->rdft_hlen[plane] + j] = *(src + linesize * i + j);

        copy_rev(s->rdft_hdata[plane] + i * s->rdft_hlen[plane], w, s->rdft_hlen[plane]);
    }

    for (i = 0; i < h; i++)
        av_rdft_calc(s->hrdft[plane], s->rdft_hdata[plane] + i * s->rdft_hlen[plane]);
}

/*Vertical pass - RDFT*/
static void rdft_vertical(FFTFILTContext *s, int h, int plane)
{
    int i, j;

    for (i = 0; i < s->rdft_hlen[plane]; i++) {
        for (j = 0; j < h; j++)
            s->rdft_vdata[plane][i * s->rdft_vlen[plane] + j] =
            s->rdft_hdata[plane][j * s->rdft_hlen[plane] + i];
        copy_rev(s->rdft_vdata[plane] + i * s->rdft_vlen[plane], h, s->rdft_vlen[plane]);
    }

    for (i = 0; i < s->rdft_hlen[plane]; i++)
        av_rdft_calc(s->vrdft[plane], s->rdft_vdata[plane] + i * s->rdft_vlen[plane]);
}
/*Vertical pass - IRDFT*/
static void irdft_vertical(FFTFILTContext *s, int h, int plane)
{
    int i, j;

    for (i = 0; i < s->rdft_hlen[plane]; i++)
        av_rdft_calc(s->ivrdft[plane], s->rdft_vdata[plane] + i * s->rdft_vlen[plane]);

    for (i = 0; i < s->rdft_hlen[plane]; i++)
        for (j = 0; j < h; j++)
            s->rdft_hdata[plane][j * s->rdft_hlen[plane] + i] =
            s->rdft_vdata[plane][i * s->rdft_vlen[plane] + j];
}

/*Horizontal pass - IRDFT*/
static void irdft_horizontal8(FFTFILTContext *s, AVFrame *out, int w, int h, int plane)
{
    int i, j;

    for (i = 0; i < h; i++)
        av_rdft_calc(s->ihrdft[plane], s->rdft_hdata[plane] + i * s->rdft_hlen[plane]);

    for (i = 0; i < h; i++)
        for (j = 0; j < w; j++)
            *(out->data[plane] + out->linesize[plane] * i + j) = av_clip(s->rdft_hdata[plane][i
                                                                         *s->rdft_hlen[plane] + j] * 4 /
                                                                         (s->rdft_hlen[plane] *
                                                                          s->rdft_vlen[plane]), 0, 255);
}

static void irdft_horizontal16(FFTFILTContext *s, AVFrame *out, int w, int h, int plane)
{
    uint16_t *dst = (uint16_t *)out->data[plane];
    int linesize = out->linesize[plane] / 2;
    int max = (1 << s->depth) - 1;
    int i, j;

    for (i = 0; i < h; i++)
        av_rdft_calc(s->ihrdft[plane], s->rdft_hdata[plane] + i * s->rdft_hlen[plane]);

    for (i = 0; i < h; i++)
        for (j = 0; j < w; j++)
            *(dst + linesize * i + j) = av_clip(s->rdft_hdata[plane][i
                                                *s->rdft_hlen[plane] + j] * 4 /
                                                (s->rdft_hlen[plane] *
                                                s->rdft_vlen[plane]), 0, max);
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
    int rdft_hbits, rdft_vbits, i, plane;

    desc = av_pix_fmt_desc_get(inlink->format);
    s->depth = desc->comp[0].depth;
    s->planewidth[1] = s->planewidth[2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0] = s->planewidth[3] = inlink->w;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    for (i = 0; i < desc->nb_components; i++) {
        int w = s->planewidth[i];
        int h = s->planeheight[i];

        /* RDFT - Array initialization for Horizontal pass*/
        for (rdft_hbits = 1; 1 << rdft_hbits < w*10/9; rdft_hbits++);
        s->rdft_hbits[i] = rdft_hbits;
        s->rdft_hlen[i] = 1 << rdft_hbits;
        if (!(s->rdft_hdata[i] = av_malloc_array(h, s->rdft_hlen[i] * sizeof(FFTSample))))
            return AVERROR(ENOMEM);

        if (!(s->hrdft[i] = av_rdft_init(s->rdft_hbits[i], DFT_R2C)))
            return AVERROR(ENOMEM);
        if (!(s->ihrdft[i] = av_rdft_init(s->rdft_hbits[i], IDFT_C2R)))
            return AVERROR(ENOMEM);

        /* RDFT - Array initialization for Vertical pass*/
        for (rdft_vbits = 1; 1 << rdft_vbits < h*10/9; rdft_vbits++);
        s->rdft_vbits[i] = rdft_vbits;
        s->rdft_vlen[i] = 1 << rdft_vbits;
        if (!(s->rdft_vdata[i] = av_malloc_array(s->rdft_hlen[i], s->rdft_vlen[i] * sizeof(FFTSample))))
            return AVERROR(ENOMEM);

        if (!(s->vrdft[i] = av_rdft_init(s->rdft_vbits[i], DFT_R2C)))
            return AVERROR(ENOMEM);
        if (!(s->ivrdft[i] = av_rdft_init(s->rdft_vbits[i], IDFT_C2R)))
            return AVERROR(ENOMEM);
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

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    FFTFILTContext *s = ctx->priv;
    AVFrame *out;
    int i, j, plane;

    out = ff_get_video_buffer(outlink, inlink->w, inlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }

    av_frame_copy_props(out, in);

    for (plane = 0; plane < s->nb_planes; plane++) {
        int w = s->planewidth[plane];
        int h = s->planeheight[plane];

        if (s->eval_mode == EVAL_MODE_FRAME)
            do_eval(s, inlink, plane);

        s->rdft_horizontal(s, in, w, h, plane);
        rdft_vertical(s, h, plane);

        /*Change user defined parameters*/
        for (i = 0; i < s->rdft_hlen[plane]; i++)
            for (j = 0; j < s->rdft_vlen[plane]; j++)
                s->rdft_vdata[plane][i * s->rdft_vlen[plane] + j] *=
                  s->weight[plane][i * s->rdft_vlen[plane] + j];

        s->rdft_vdata[plane][0] += s->rdft_hlen[plane] * s->rdft_vlen[plane] * s->dc[plane];

        irdft_vertical(s, h, plane);
        s->irdft_horizontal(s, out, w, h, plane);
    }

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
        av_rdft_end(s->hrdft[i]);
        av_rdft_end(s->ihrdft[i]);
        av_rdft_end(s->vrdft[i]);
        av_rdft_end(s->ivrdft[i]);
    }
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pixel_fmts_fftfilt[] = {
        AV_PIX_FMT_GRAY8,
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
    .description     = NULL_IF_CONFIG_SMALL("Apply arbitrary expressions to pixels in frequency domain."),
    .priv_size       = sizeof(FFTFILTContext),
    .priv_class      = &fftfilt_class,
    .inputs          = fftfilt_inputs,
    .outputs         = fftfilt_outputs,
    .query_formats   = query_formats,
    .init            = initialize,
    .uninit          = uninit,
    .flags           = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
