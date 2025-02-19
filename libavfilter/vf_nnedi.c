/*
 * Copyright (C) 2010-2011 Kevin Stone
 * Copyright (C) 2016 Paul B Mahol
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <float.h>

#include "libavutil/common.h"
#include "libavutil/file_open.h"
#include "libavutil/float_dsp.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "filters.h"
#include "video.h"

static const size_t NNEDI_WEIGHTS_SIZE = 13574928;
static const uint8_t NNEDI_XDIM[] = { 8, 16, 32, 48, 8, 16, 32 };
static const uint8_t NNEDI_YDIM[] = { 6, 6, 6, 6, 4, 4, 4 };
static const uint16_t NNEDI_NNS[] = { 16, 32, 64, 128, 256 };

typedef struct PrescreenerCoefficients {
    DECLARE_ALIGNED(32, float, kernel_l0)[4][16 * 4];
    DECLARE_ALIGNED(32, float, bias_l0)[4];

    DECLARE_ALIGNED(32, float, kernel_l1)[4][4];
    DECLARE_ALIGNED(32, float, bias_l1)[4];

    DECLARE_ALIGNED(32, float, kernel_l2)[4][8];
    DECLARE_ALIGNED(32, float, bias_l2)[4];
} PrescreenerCoefficients;

typedef struct PredictorCoefficients {
    int xdim, ydim, nns, nsize;
    float *data;
    float *softmax_q1;
    float *elliott_q1;
    float *softmax_bias_q1;
    float *elliott_bias_q1;
    float *softmax_q2;
    float *elliott_q2;
    float *softmax_bias_q2;
    float *elliott_bias_q2;
} PredictorCoefficients;

typedef struct NNEDIContext {
    const AVClass *class;

    char *weights_file;

    AVFrame *prev;
    int eof;
    int64_t pts;

    AVFloatDSPContext *fdsp;
    int depth;
    int nb_planes;
    int nb_threads;
    int linesize[4];
    int planewidth[4];
    int planeheight[4];
    int field_n;

    PrescreenerCoefficients prescreener[4];
    PredictorCoefficients coeffs[2][5][7];

    float half;
    float in_scale;
    float out_scale;

    // Parameters
    int deint;
    int field;
    int process_plane;
    int nsize;
    int nnsparam;
    int qual;
    int etype;
    int pscrn;

    int input_size;
    uint8_t **prescreen_buf;
    float **input_buf;
    float **output_buf;

    void (*read)(const uint8_t *src, float *dst,
                 int src_stride, int dst_stride,
                 int width, int height, float scale);
    void (*write)(const float *src, uint8_t *dst,
                  int src_stride, int dst_stride,
                  int width, int height, int depth, float scale);
    void (*prescreen[2])(AVFilterContext *ctx,
                         const void *src, ptrdiff_t src_stride,
                         uint8_t *prescreen, int N,
                         const PrescreenerCoefficients *const coeffs);
} NNEDIContext;

#define OFFSET(x) offsetof(NNEDIContext, x)
#define RFLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption nnedi_options[] = {
    {"weights",  "set weights file", OFFSET(weights_file),  AV_OPT_TYPE_STRING, {.str="nnedi3_weights.bin"}, 0, 0, FLAGS },
    {"deint",         "set which frames to deinterlace", OFFSET(deint),         AV_OPT_TYPE_INT, {.i64=0}, 0, 1, RFLAGS, .unit = "deint" },
        {"all",        "deinterlace all frames",                       0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, RFLAGS, .unit = "deint" },
        {"interlaced", "only deinterlace frames marked as interlaced", 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, RFLAGS, .unit = "deint" },
    {"field",  "set mode of operation", OFFSET(field),         AV_OPT_TYPE_INT, {.i64=-1}, -2, 3, RFLAGS, .unit = "field" },
        {"af", "use frame flags, both fields",  0, AV_OPT_TYPE_CONST, {.i64=-2}, 0, 0, RFLAGS, .unit = "field" },
        {"a",  "use frame flags, single field", 0, AV_OPT_TYPE_CONST, {.i64=-1}, 0, 0, RFLAGS, .unit = "field" },
        {"t",  "use top field only",            0, AV_OPT_TYPE_CONST, {.i64=0},  0, 0, RFLAGS, .unit = "field" },
        {"b",  "use bottom field only",         0, AV_OPT_TYPE_CONST, {.i64=1},  0, 0, RFLAGS, .unit = "field" },
        {"tf", "use both fields, top first",    0, AV_OPT_TYPE_CONST, {.i64=2},  0, 0, RFLAGS, .unit = "field" },
        {"bf", "use both fields, bottom first", 0, AV_OPT_TYPE_CONST, {.i64=3},  0, 0, RFLAGS, .unit = "field" },
    {"planes", "set which planes to process", OFFSET(process_plane), AV_OPT_TYPE_INT, {.i64=7}, 0, 15, RFLAGS },
    {"nsize",  "set size of local neighborhood around each pixel, used by the predictor neural network", OFFSET(nsize), AV_OPT_TYPE_INT, {.i64=6}, 0, 6, RFLAGS, .unit = "nsize" },
        {"s8x6",     NULL, 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, RFLAGS, .unit = "nsize" },
        {"s16x6",    NULL, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, RFLAGS, .unit = "nsize" },
        {"s32x6",    NULL, 0, AV_OPT_TYPE_CONST, {.i64=2}, 0, 0, RFLAGS, .unit = "nsize" },
        {"s48x6",    NULL, 0, AV_OPT_TYPE_CONST, {.i64=3}, 0, 0, RFLAGS, .unit = "nsize" },
        {"s8x4",     NULL, 0, AV_OPT_TYPE_CONST, {.i64=4}, 0, 0, RFLAGS, .unit = "nsize" },
        {"s16x4",    NULL, 0, AV_OPT_TYPE_CONST, {.i64=5}, 0, 0, RFLAGS, .unit = "nsize" },
        {"s32x4",    NULL, 0, AV_OPT_TYPE_CONST, {.i64=6}, 0, 0, RFLAGS, .unit = "nsize" },
    {"nns",    "set number of neurons in predictor neural network", OFFSET(nnsparam), AV_OPT_TYPE_INT, {.i64=1}, 0, 4, RFLAGS, .unit = "nns" },
        {"n16",       NULL, 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, RFLAGS, .unit = "nns" },
        {"n32",       NULL, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, RFLAGS, .unit = "nns" },
        {"n64",       NULL, 0, AV_OPT_TYPE_CONST, {.i64=2}, 0, 0, RFLAGS, .unit = "nns" },
        {"n128",      NULL, 0, AV_OPT_TYPE_CONST, {.i64=3}, 0, 0, RFLAGS, .unit = "nns" },
        {"n256",      NULL, 0, AV_OPT_TYPE_CONST, {.i64=4}, 0, 0, RFLAGS, .unit = "nns" },
    {"qual",  "set quality", OFFSET(qual), AV_OPT_TYPE_INT, {.i64=1}, 1, 2, RFLAGS, .unit = "qual" },
        {"fast", NULL, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, RFLAGS, .unit = "qual" },
        {"slow", NULL, 0, AV_OPT_TYPE_CONST, {.i64=2}, 0, 0, RFLAGS, .unit = "qual" },
    {"etype", "set which set of weights to use in the predictor", OFFSET(etype), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, RFLAGS, .unit = "etype" },
        {"a",  "weights trained to minimize absolute error", 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, RFLAGS, .unit = "etype" },
        {"abs","weights trained to minimize absolute error", 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, RFLAGS, .unit = "etype" },
        {"s",  "weights trained to minimize squared error",  0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, RFLAGS, .unit = "etype" },
        {"mse","weights trained to minimize squared error",  0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, RFLAGS, .unit = "etype" },
    {"pscrn", "set prescreening", OFFSET(pscrn), AV_OPT_TYPE_INT, {.i64=2}, 0, 4, RFLAGS, .unit = "pscrn" },
        {"none",      NULL, 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, RFLAGS, .unit = "pscrn" },
        {"original",  NULL, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, RFLAGS, .unit = "pscrn" },
        {"new",       NULL, 0, AV_OPT_TYPE_CONST, {.i64=2}, 0, 0, RFLAGS, .unit = "pscrn" },
        {"new2",      NULL, 0, AV_OPT_TYPE_CONST, {.i64=3}, 0, 0, RFLAGS, .unit = "pscrn" },
        {"new3",      NULL, 0, AV_OPT_TYPE_CONST, {.i64=4}, 0, 0, RFLAGS, .unit = "pscrn" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(nnedi);

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    const NNEDIContext *const s = ctx->priv;

    outlink->time_base     = av_mul_q(ctx->inputs[0]->time_base, (AVRational){1, 2});
    outlink->w             = ctx->inputs[0]->w;
    outlink->h             = ctx->inputs[0]->h;

    if (s->field == -2 || s->field > 1) {
        FilterLink *il = ff_filter_link(ctx->inputs[0]);
        FilterLink *ol = ff_filter_link(outlink);
        ol->frame_rate = av_mul_q(il->frame_rate, (AVRational){2, 1});
    }

    return 0;
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
    AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P,
    AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P,
    AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUVJ411P,
    AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP,
    AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
    AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV440P10,
    AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV444P12,
    AV_PIX_FMT_YUV440P12,
    AV_PIX_FMT_YUV420P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV444P14,
    AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
    AV_PIX_FMT_YUVA444P9, AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_YUVA444P12, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA422P16,
    AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA420P16,
    AV_PIX_FMT_GBRAP10,   AV_PIX_FMT_GBRAP12,    AV_PIX_FMT_GBRAP16,
    AV_PIX_FMT_NONE
};

static float dot_dsp(const NNEDIContext *const s, const float *kernel, const float *input,
                     int n, float scale, float bias)
{
    float sum, y;

    sum = s->fdsp->scalarproduct_float(kernel, input, n);

    y = sum * scale + bias + 1e-20f;

    return y;
}

static float elliott(float x)
{
    return x / (1.0f + fabsf(x));
}

static void transform_elliott(float *input, int size)
{
    for (int i = 0; i < size; i++)
        input[i] = elliott(input[i]);
}

static void process_old(AVFilterContext *ctx,
                        const void *src, ptrdiff_t src_stride,
                        uint8_t *prescreen, int N,
                        const PrescreenerCoefficients *const m_data)
{
    NNEDIContext *s = ctx->priv;
    const float *src_p = src;

    // Adjust source pointer to point to top-left of filter window.
    const float *window = src_p - 2 * src_stride - 5;

    for (int j = 0; j < N; j++) {
        LOCAL_ALIGNED_32(float, input, [48]);
        float state[12];

        for (int i = 0; i < 4; i++)
            memcpy(input + i * 12, window + i * src_stride + j, 12 * sizeof(float));

        // Layer 0.
        for (int n = 0; n < 4; n++)
            state[n] = dot_dsp(s, m_data->kernel_l0[n], input, 48, 1.0f, m_data->bias_l0[n]);
        transform_elliott(state + 1, 3);

        // Layer 1.
        for (int n = 0; n < 4; n++)
            state[n + 4] = dot_dsp(s, m_data->kernel_l1[n], state, 4, 1.0f, m_data->bias_l1[n]);
        transform_elliott(state + 4, 3);

        // Layer 2.
        for (int n = 0; n < 4; n++)
            state[n + 8] = dot_dsp(s, m_data->kernel_l2[n], state, 8, 1.0f, m_data->bias_l2[n]);

        prescreen[j] = FFMAX(state[10], state[11]) <= FFMAX(state[8], state[9]) ? 255 : 0;
    }
}

static void process_new(AVFilterContext *ctx,
                        const void *src, ptrdiff_t src_stride,
                        uint8_t *prescreen, int N,
                        const PrescreenerCoefficients *const m_data)
{
    NNEDIContext *s = ctx->priv;
    const float *src_p = src;

    // Adjust source pointer to point to top-left of filter window.
    const float *window = src_p - 2 * src_stride - 6;

    for (int j = 0; j < N; j += 4) {
        LOCAL_ALIGNED_32(float, input, [64]);
        float state[8];

        for (int i = 0; i < 4; i++)
            memcpy(input + i * 16, window + i * src_stride + j, 16 * sizeof(float));

        for (int n = 0; n < 4; n++)
            state[n] = dot_dsp(s, m_data->kernel_l0[n], input, 64, 1.0f, m_data->bias_l0[n]);
        transform_elliott(state, 4);

        for (int n = 0; n < 4; n++)
            state[n + 4] = dot_dsp(s, m_data->kernel_l1[n], state, 4, 1.0f, m_data->bias_l1[n]);

        for (int n = 0; n < 4; n++)
            prescreen[j + n] = state[n + 4] > 0.f;
    }
}

static int filter_offset(int nn, const PredictorCoefficients *const model)
{
    return nn * model->nsize;
}

static const float *softmax_q1_filter(int nn,
                                      const PredictorCoefficients *const model)
{
    return model->softmax_q1 + filter_offset(nn, model);
}

static const float *elliott_q1_filter(int nn,
                                      const PredictorCoefficients *const model)
{
    return model->elliott_q1 + filter_offset(nn, model);
}

static const float *softmax_q2_filter(int nn,
                                      const PredictorCoefficients *const model)
{
    return model->softmax_q2 + filter_offset(nn, model);
}

static const float *elliott_q2_filter(int nn,
                                      const PredictorCoefficients *const model)
{
    return model->elliott_q2 + filter_offset(nn, model);
}

static void gather_input(const float *src, ptrdiff_t src_stride,
                         float *buf, float mstd[4],
                         const PredictorCoefficients *const model)
{
    const float scale = 1.f / model->nsize;
    float sum = 0.f;
    float sum_sq = 0.f;
    float tmp;

    for (int i = 0; i < model->ydim; i++) {
        memcpy(buf, src, model->xdim * sizeof(float));

        for (int j = 0; j < model->xdim; j++) {
            const float val = src[j];

            sum += val;
            sum_sq += val * val;
        }

        src += src_stride;
        buf += model->xdim;
    }

    mstd[0] = sum * scale;
    mstd[3] = 0.f;

    tmp = sum_sq * scale - mstd[0] * mstd[0];
    if (tmp < FLT_EPSILON) {
        mstd[1] = 0.0f;
        mstd[2] = 0.0f;
    } else {
        mstd[1] = sqrtf(tmp);
        mstd[2] = 1.0f / mstd[1];
    }
}

static float softmax_exp(float x)
{
    return expf(av_clipf(x, -80.f, 80.f));
}

static void transform_softmax_exp(float *input, int size)
{
    for (int i = 0; i < size; i++)
        input[i] = softmax_exp(input[i]);
}

static void wae5(const float *softmax, const float *el,
                 int n, float mstd[4])
{
    float vsum = 0.0f, wsum = 0.0f;

    for (int i = 0; i < n; i++) {
        vsum += softmax[i] * elliott(el[i]);
        wsum += softmax[i];
    }

    if (wsum > 1e-10f)
        mstd[3] += (5.0f * vsum) / wsum * mstd[1] + mstd[0];
    else
        mstd[3] += mstd[0];
}

static void predictor(AVFilterContext *ctx,
                      const void *src, ptrdiff_t src_stride, void *dst,
                      const uint8_t *prescreen, int N,
                      const PredictorCoefficients *const model, int use_q2)
{
    const NNEDIContext *const s = ctx->priv;
    const float *src_p = src;
    float *dst_p = dst;

    // Adjust source pointer to point to top-left of filter window.
    const float *window = src_p - (model->ydim / 2) * src_stride - (model->xdim / 2 - 1);
    const int filter_size = model->nsize;
    const int nns = model->nns;

    for (int i = 0; i < N; i++) {
        LOCAL_ALIGNED_32(float, input, [48 * 6]);
        float activation[256 * 2];
        float mstd[4];
        float scale;

        if (prescreen[i])
            continue;

        gather_input(window + i, src_stride, input, mstd, model);
        scale = mstd[2];

        for (int nn = 0; nn < nns; nn++)
            activation[nn] = dot_dsp(s, softmax_q1_filter(nn, model), input, filter_size, scale, model->softmax_bias_q1[nn]);

        for (int nn = 0; nn < nns; nn++)
            activation[nns + nn] = dot_dsp(s, elliott_q1_filter(nn, model), input, filter_size, scale, model->elliott_bias_q1[nn]);

        transform_softmax_exp(activation, nns);
        wae5(activation, activation + nns, nns, mstd);

        if (use_q2) {
            for (int nn = 0; nn < nns; nn++)
                activation[nn] = dot_dsp(s, softmax_q2_filter(nn, model), input, filter_size, scale, model->softmax_bias_q2[nn]);

            for (int nn = 0; nn < nns; nn++)
                activation[nns + nn] = dot_dsp(s, elliott_q2_filter(nn, model), input, filter_size, scale, model->elliott_bias_q2[nn]);

            transform_softmax_exp(activation, nns);
            wae5(activation, activation + nns, nns, mstd);
        }

        dst_p[i] = mstd[3] * (use_q2 ? 0.5f : 1.f);
    }
}

static void read_bytes(const uint8_t *src, float *dst,
                       int src_stride, int dst_stride,
                       int width, int height, float scale)
{
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < 32; x++)
            dst[-x - 1] = src[x];

        for (int x = 0; x < width; x++)
            dst[x] = src[x];

        for (int x = 0; x < 32; x++)
            dst[width + x] = src[width - x - 1];

        dst += dst_stride;
        src += src_stride;
    }
}

static void read_words(const uint8_t *srcp, float *dst,
                       int src_stride, int dst_stride,
                       int width, int height, float scale)
{
    const uint16_t *src = (const uint16_t *)srcp;

    src_stride /= 2;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < 32; x++)
            dst[-x - 1] = src[x] * scale;

        for (int x = 0; x < width; x++)
            dst[x] = src[x] * scale;

        for (int x = 0; x < 32; x++)
            dst[width + x] = src[width - x - 1] * scale;

        dst += dst_stride;
        src += src_stride;
    }
}

static void write_bytes(const float *src, uint8_t *dst,
                        int src_stride, int dst_stride,
                        int width, int height, int depth,
                        float scale)
{
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = av_clip_uint8(src[x]);

        dst += dst_stride;
        src += src_stride;
    }
}

static void write_words(const float *src, uint8_t *dstp,
                        int src_stride, int dst_stride,
                        int width, int height, int depth,
                        float scale)
{
    uint16_t *dst = (uint16_t *)dstp;

    dst_stride /= 2;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = av_clip_uintp2_c(src[x] * scale, depth);

        dst += dst_stride;
        src += src_stride;
    }
}

static void interpolation(const void *src, ptrdiff_t src_stride,
                          void *dst, const uint8_t *prescreen, int n)
{
    const float *src_p = src;
    float *dst_p = dst;
    const float *window = src_p - 2 * src_stride;

    for (int i = 0; i < n; i++) {
        float accum = 0.0f;

        if (!prescreen[i])
            continue;

        accum += (-3.0f / 32.0f) * window[0 * src_stride + i];
        accum += (19.0f / 32.0f) * window[1 * src_stride + i];
        accum += (19.0f / 32.0f) * window[2 * src_stride + i];
        accum += (-3.0f / 32.0f) * window[3 * src_stride + i];

        dst_p[i] = accum;
    }
}

static int filter_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    const NNEDIContext *const s = ctx->priv;
    AVFrame *out = arg;
    AVFrame *in = s->prev;
    const float in_scale = s->in_scale;
    const float out_scale = s->out_scale;
    const int depth = s->depth;
    const int interlaced = !!(in->flags & AV_FRAME_FLAG_INTERLACED);
    const int tff = s->field_n == (s->field < 0 ? interlaced ? (in->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST) : 1 :
                                  (s->field & 1) ^ 1);


    for (int p = 0; p < s->nb_planes; p++) {
        const int height = s->planeheight[p];
        const int width = s->planewidth[p];
        const int slice_start = 2 * ((height / 2 * jobnr) / nb_jobs);
        const int slice_end = 2 * ((height / 2 * (jobnr+1)) / nb_jobs);
        const uint8_t *src_data = in->data[p];
        uint8_t *dst_data = out->data[p];
        uint8_t *dst = out->data[p] + slice_start * out->linesize[p];
        const int src_linesize = in->linesize[p];
        const int dst_linesize = out->linesize[p];
        uint8_t *prescreen_buf = s->prescreen_buf[jobnr];
        float *srcbuf = s->input_buf[jobnr];
        const int srcbuf_stride = width + 64;
        float *dstbuf = s->output_buf[jobnr];
        const int dstbuf_stride = width;
        const int slice_height = (slice_end - slice_start) / 2;
        const int last_slice = slice_end == height;
        const uint8_t *in_line;
        uint8_t *out_line;
        int y_out;

        if (!(s->process_plane & (1 << p))) {
            av_image_copy_plane(dst, out->linesize[p],
                                in->data[p] + slice_start * in->linesize[p],
                                in->linesize[p],
                                s->linesize[p], slice_end - slice_start);
            continue;
        }

        y_out    = slice_start + (tff ^ (slice_start & 1));
        in_line  = src_data + (y_out * src_linesize);
        out_line = dst_data + (y_out * dst_linesize);

        while (y_out < slice_end) {
            memcpy(out_line, in_line, s->linesize[p]);
            y_out += 2;
            in_line  += src_linesize * 2;
            out_line += dst_linesize * 2;
        }

        y_out = slice_start + ((!tff) ^ (slice_start & 1));

        s->read(src_data + FFMAX(y_out - 5, tff) * src_linesize,
                srcbuf + 32,
                src_linesize * 2, srcbuf_stride,
                width, 1, in_scale);
        srcbuf += srcbuf_stride;

        s->read(src_data + FFMAX(y_out - 3, tff) * src_linesize,
                srcbuf + 32,
                src_linesize * 2, srcbuf_stride,
                width, 1, in_scale);
        srcbuf += srcbuf_stride;

        s->read(src_data + FFMAX(y_out - 1, tff) * src_linesize,
                srcbuf + 32,
                src_linesize * 2, srcbuf_stride,
                width, 1, in_scale);
        srcbuf += srcbuf_stride;

        in_line  = src_data + FFMIN(y_out + 1, height - 1 - !tff) * src_linesize;
        out_line = dst_data + (y_out * dst_linesize);

        s->read(in_line, srcbuf + 32, src_linesize * 2, srcbuf_stride,
                width, slice_height - last_slice, in_scale);

        y_out += (slice_height - last_slice) * 2;

        s->read(src_data + FFMIN(y_out + 1, height - 1 - !tff) * src_linesize,
                srcbuf + 32 + srcbuf_stride * (slice_height - last_slice),
                src_linesize * 2, srcbuf_stride,
                width, 1, in_scale);

        s->read(src_data + FFMIN(y_out + 3, height - 1 - !tff) * src_linesize,
                srcbuf + 32 + srcbuf_stride * (slice_height + 1 - last_slice),
                src_linesize * 2, srcbuf_stride,
                width, 1, in_scale);

        s->read(src_data + FFMIN(y_out + 5, height - 1 - !tff) * src_linesize,
                srcbuf + 32 + srcbuf_stride * (slice_height + 2 - last_slice),
                src_linesize * 2, srcbuf_stride,
                width, 1, in_scale);

        for (int y = 0; y < slice_end - slice_start; y += 2) {
            if (s->pscrn > 0)
                s->prescreen[s->pscrn > 1](ctx, srcbuf + (y / 2) * srcbuf_stride + 32,
                             srcbuf_stride, prescreen_buf, width,
                             &s->prescreener[s->pscrn - 1]);

            predictor(ctx,
                      srcbuf + (y / 2) * srcbuf_stride + 32,
                      srcbuf_stride,
                      dstbuf + (y / 2) * dstbuf_stride,
                      prescreen_buf, width,
                      &s->coeffs[s->etype][s->nnsparam][s->nsize], s->qual == 2);

            if (s->pscrn > 0)
                interpolation(srcbuf + (y / 2) * srcbuf_stride + 32,
                              srcbuf_stride,
                              dstbuf + (y / 2) * dstbuf_stride,
                              prescreen_buf, width);
        }

        s->write(dstbuf, out_line, dstbuf_stride, dst_linesize * 2,
                 width, slice_height, depth, out_scale);
    }

    return 0;
}

static int get_frame(AVFilterContext *ctx, int is_second)
{
    NNEDIContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *dst;

    dst = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!dst)
        return AVERROR(ENOMEM);
    av_frame_copy_props(dst, s->prev);
    dst->flags &= ~AV_FRAME_FLAG_INTERLACED;
    dst->pts = s->pts;

    ff_filter_execute(ctx, filter_slice, dst, NULL,
                      FFMIN(s->planeheight[1] / 2, s->nb_threads));

    if (s->field == -2 || s->field > 1)
        s->field_n = !s->field_n;

    return ff_filter_frame(outlink, dst);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    NNEDIContext *s = ctx->priv;
    int ret;

    if (!s->prev) {
        s->prev = in;
        return 0;
    }

    if ((s->deint && !(s->prev->flags & AV_FRAME_FLAG_INTERLACED)) || ctx->is_disabled) {
        s->prev->pts *= 2;
        ret = ff_filter_frame(ctx->outputs[0], s->prev);
        s->prev = in;
        return ret;
    }

    s->pts = s->prev->pts * 2;
    ret = get_frame(ctx, 0);
    if (ret < 0 || (s->field > -2 && s->field < 2)) {
        av_frame_free(&s->prev);
        s->prev = in;
        return ret;
    }

    s->pts = s->prev->pts + in->pts;
    ret = get_frame(ctx, 1);
    av_frame_free(&s->prev);
    s->prev = in;
    return ret;
}

static int request_frame(AVFilterLink *link)
{
    AVFilterContext *ctx = link->src;
    NNEDIContext *s = ctx->priv;
    int ret;

    if (s->eof)
        return AVERROR_EOF;

    ret  = ff_request_frame(ctx->inputs[0]);

    if (ret == AVERROR_EOF && s->prev) {
        AVFrame *next = av_frame_clone(s->prev);
        FilterLink *l = ff_filter_link(ctx->outputs[0]);

        if (!next)
            return AVERROR(ENOMEM);

        next->pts = s->prev->pts + av_rescale_q(1, av_inv_q(l->frame_rate),
                                                ctx->outputs[0]->time_base);
        s->eof = 1;

        ret = filter_frame(ctx->inputs[0], next);
    } else if (ret < 0) {
        return ret;
    }

    return ret;
}

static void copy_weights(float *dst, int n, const float **data)
{
    memcpy(dst, *data, n * sizeof(float));
    *data += n;
}

static float *allocate(float **ptr, int size)
{
    float *ret = *ptr;

    *ptr += size;

    return ret;
}

static int allocate_model(PredictorCoefficients *coeffs, int xdim, int ydim, int nns)
{
    int filter_size = nns * xdim * ydim;
    int bias_size = nns;
    float *data;

    data = av_calloc(filter_size + bias_size, 4 * sizeof(float));
    if (!data)
        return AVERROR(ENOMEM);

    coeffs->data = data;
    coeffs->xdim = xdim;
    coeffs->ydim = ydim;
    coeffs->nsize = xdim * ydim;
    coeffs->nns  = nns;

    coeffs->softmax_q1 = allocate(&data, filter_size);
    coeffs->elliott_q1 = allocate(&data, filter_size);
    coeffs->softmax_bias_q1 = allocate(&data, bias_size);
    coeffs->elliott_bias_q1 = allocate(&data, bias_size);

    coeffs->softmax_q2 = allocate(&data, filter_size);
    coeffs->elliott_q2 = allocate(&data, filter_size);
    coeffs->softmax_bias_q2 = allocate(&data, bias_size);
    coeffs->elliott_bias_q2 = allocate(&data, bias_size);

    return 0;
}

static int read_weights(AVFilterContext *ctx, const float *bdata)
{
    NNEDIContext *s = ctx->priv;
    int ret;

    copy_weights(&s->prescreener[0].kernel_l0[0][0], 4 * 48, &bdata);
    copy_weights(s->prescreener[0].bias_l0, 4, &bdata);

    copy_weights(&s->prescreener[0].kernel_l1[0][0], 4 * 4, &bdata);
    copy_weights(s->prescreener[0].bias_l1, 4, &bdata);

    copy_weights(&s->prescreener[0].kernel_l2[0][0], 4 * 8, &bdata);
    copy_weights(s->prescreener[0].bias_l2, 4, &bdata);

    for (int i = 0; i < 3; i++) {
        PrescreenerCoefficients *data = &s->prescreener[i + 1];
        float kernel_l0_shuffled[4 * 64];
        float kernel_l1_shuffled[4 * 4];

        copy_weights(kernel_l0_shuffled, 4 * 64, &bdata);
        copy_weights(data->bias_l0, 4, &bdata);

        copy_weights(kernel_l1_shuffled, 4 * 4, &bdata);
        copy_weights(data->bias_l1, 4, &bdata);

        for (int n = 0; n < 4; n++) {
            for (int k = 0; k < 64; k++)
                data->kernel_l0[n][k] = kernel_l0_shuffled[(k / 8) * 32 + n * 8 + k % 8];
            for (int k = 0; k < 4; k++)
                data->kernel_l1[n][k] = kernel_l1_shuffled[k * 4 + n];
        }
    }

    for (int m = 0; m < 2; m++) {
        // Grouping by neuron count.
        for (int i = 0; i < 5; i++) {
            const int nns = NNEDI_NNS[i];

            // Grouping by window size.
            for (int j = 0; j < 7; j++) {
                PredictorCoefficients *model = &s->coeffs[m][i][j];
                const int xdim = NNEDI_XDIM[j];
                const int ydim = NNEDI_YDIM[j];
                const int filter_size = xdim * ydim;

                ret = allocate_model(model, xdim, ydim, nns);
                if (ret < 0)
                    return ret;

                // Quality 1 model. NNS[i] * (XDIM[j] * YDIM[j]) * 2 coefficients.
                copy_weights(model->softmax_q1, nns * filter_size, &bdata);
                copy_weights(model->elliott_q1, nns * filter_size, &bdata);

                // Quality 1 model bias. NNS[i] * 2 coefficients.
                copy_weights(model->softmax_bias_q1, nns, &bdata);
                copy_weights(model->elliott_bias_q1, nns, &bdata);

                // Quality 2 model. NNS[i] * (XDIM[j] * YDIM[j]) * 2 coefficients.
                copy_weights(model->softmax_q2, nns * filter_size, &bdata);
                copy_weights(model->elliott_q2, nns * filter_size, &bdata);

                // Quality 2 model bias. NNS[i] * 2 coefficients.
                copy_weights(model->softmax_bias_q2, nns, &bdata);
                copy_weights(model->elliott_bias_q2, nns, &bdata);
            }
        }
    }

    return 0;
}

static float mean(const float *input, int size)
{
    float sum = 0.f;

    for (int i = 0; i < size; i++)
        sum += input[i];

    return sum / size;
}

static void transform(float *input, int size, float mean, float half)
{
    for (int i = 0; i < size; i++)
        input[i] = (input[i] - mean) / half;
}

static void subtract_mean_old(PrescreenerCoefficients *coeffs, float half)
{
    for (int n = 0; n < 4; n++) {
        float m = mean(coeffs->kernel_l0[n], 48);

        transform(coeffs->kernel_l0[n], 48, m, half);
    }
}

static void subtract_mean_new(PrescreenerCoefficients *coeffs, float half)
{
    for (int n = 0; n < 4; n++) {
        float m = mean(coeffs->kernel_l0[n], 64);

        transform(coeffs->kernel_l0[n], 64, m, half);
    }
}

static void subtract_mean_predictor(PredictorCoefficients *model)
{
    const int filter_size = model->nsize;
    const int nns = model->nns;
    const float scale = 1.f / nns;

    double softmax_means[256]; // Average of individual softmax filters.
    double elliott_means[256]; // Average of individual elliott filters.
    double mean_filter[48 * 6] = { 0 }; // Pointwise average of all softmax filters.
    double mean_bias;

    // Quality 1.
    for (int nn = 0; nn < nns; nn++) {
        softmax_means[nn] = mean(model->softmax_q1 + nn * filter_size, filter_size);
        elliott_means[nn] = mean(model->elliott_q1 + nn * filter_size, filter_size);

        for (int k = 0; k < filter_size; k++)
            mean_filter[k] += model->softmax_q1[nn * filter_size + k] - softmax_means[nn];
    }

    for (int k = 0; k < filter_size; k++)
        mean_filter[k] *= scale;

    mean_bias = mean(model->softmax_bias_q1, nns);

    for (int nn = 0; nn < nns; nn++) {
        for (int k = 0; k < filter_size; k++) {
            model->softmax_q1[nn * filter_size + k] -= softmax_means[nn] + mean_filter[k];
            model->elliott_q1[nn * filter_size + k] -= elliott_means[nn];
        }
        model->softmax_bias_q1[nn] -= mean_bias;
    }

    // Quality 2.
    memset(mean_filter, 0, sizeof(mean_filter));

    for (int nn = 0; nn < nns; nn++) {
        softmax_means[nn] = mean(model->softmax_q2 + nn * filter_size, filter_size);
        elliott_means[nn] = mean(model->elliott_q2 + nn * filter_size, filter_size);

        for (int k = 0; k < filter_size; k++) {
            mean_filter[k] += model->softmax_q2[nn * filter_size + k] - softmax_means[nn];
        }
    }

    for (int k = 0; k < filter_size; k++)
        mean_filter[k] *= scale;

    mean_bias = mean(model->softmax_bias_q2, nns);

    for (int nn = 0; nn < nns; nn++) {
        for (int k = 0; k < filter_size; k++) {
            model->softmax_q2[nn * filter_size + k] -= softmax_means[nn] + mean_filter[k];
            model->elliott_q2[nn * filter_size + k] -= elliott_means[nn];
        }

        model->softmax_bias_q2[nn] -= mean_bias;
    }
}

static av_cold int init(AVFilterContext *ctx)
{
    NNEDIContext *s = ctx->priv;
    FILE *weights_file = NULL;
    int64_t weights_size;
    float *bdata;
    size_t bytes_read;
    int ret = 0;

    weights_file = avpriv_fopen_utf8(s->weights_file, "rb");
    if (!weights_file) {
        av_log(ctx, AV_LOG_ERROR, "No weights file provided, aborting!\n");
        return AVERROR(EINVAL);
    }

    if (fseek(weights_file, 0, SEEK_END)) {
        av_log(ctx, AV_LOG_ERROR, "Couldn't seek to the end of weights file.\n");
        fclose(weights_file);
        return AVERROR(EINVAL);
    }

    weights_size = ftell(weights_file);

    if (weights_size == -1) {
        fclose(weights_file);
        av_log(ctx, AV_LOG_ERROR, "Couldn't get size of weights file.\n");
        return AVERROR(EINVAL);
    } else if (weights_size != NNEDI_WEIGHTS_SIZE) {
        fclose(weights_file);
        av_log(ctx, AV_LOG_ERROR, "Unexpected weights file size.\n");
        return AVERROR(EINVAL);
    }

    if (fseek(weights_file, 0, SEEK_SET)) {
        fclose(weights_file);
        av_log(ctx, AV_LOG_ERROR, "Couldn't seek to the start of weights file.\n");
        return AVERROR(EINVAL);
    }

    bdata = av_malloc(NNEDI_WEIGHTS_SIZE);
    if (!bdata) {
        fclose(weights_file);
        return AVERROR(ENOMEM);
    }

    bytes_read = fread(bdata, 1, NNEDI_WEIGHTS_SIZE, weights_file);
    if (bytes_read != NNEDI_WEIGHTS_SIZE) {
        fclose(weights_file);
        ret = AVERROR_INVALIDDATA;
        av_log(ctx, AV_LOG_ERROR, "Couldn't read weights file.\n");
        goto fail;
    }

    fclose(weights_file);

    s->fdsp = avpriv_float_dsp_alloc(0);
    if (!s->fdsp) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = read_weights(ctx, bdata);
    if (ret < 0)
        goto fail;

fail:
    av_free(bdata);
    return ret;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    NNEDIContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int ret;

    s->depth = desc->comp[0].depth;
    s->nb_threads = ff_filter_get_nb_threads(ctx);
    s->nb_planes = av_pix_fmt_count_planes(inlink->format);
    if ((ret = av_image_fill_linesizes(s->linesize, inlink->format, inlink->w)) < 0)
        return ret;

    s->planewidth[1] = s->planewidth[2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0] = s->planewidth[3] = inlink->w;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->half = ((1 << 8) - 1) / 2.f;
    s->out_scale = 1 << (s->depth - 8);
    s->in_scale = 1.f / s->out_scale;

    switch (s->depth) {
    case 8:
        s->read  = read_bytes;
        s->write = write_bytes;
        break;
    default:
        s->read  = read_words;
        s->write = write_words;
        break;
    }

    subtract_mean_old(&s->prescreener[0], s->half);
    subtract_mean_new(&s->prescreener[1], s->half);
    subtract_mean_new(&s->prescreener[2], s->half);
    subtract_mean_new(&s->prescreener[3], s->half);

    s->prescreen[0] = process_old;
    s->prescreen[1] = process_new;

    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 5; j++) {
            for (int k = 0; k < 7; k++)
                subtract_mean_predictor(&s->coeffs[i][j][k]);
        }
    }

    s->input_size = (s->planewidth[0] + 64) * (s->planeheight[0] + 6);
    s->input_buf = av_calloc(s->nb_threads, sizeof(*s->input_buf));
    if (!s->input_buf)
        return AVERROR(ENOMEM);

    for (int i = 0; i < s->nb_threads; i++) {
        s->input_buf[i] = av_calloc(s->input_size, sizeof(**s->input_buf));
        if (!s->input_buf[i])
            return AVERROR(ENOMEM);
    }

    s->output_buf = av_calloc(s->nb_threads, sizeof(*s->output_buf));
    if (!s->output_buf)
        return AVERROR(ENOMEM);

    for (int i = 0; i < s->nb_threads; i++) {
        s->output_buf[i] = av_calloc(s->input_size, sizeof(**s->output_buf));
        if (!s->output_buf[i])
            return AVERROR(ENOMEM);
    }

    s->prescreen_buf = av_calloc(s->nb_threads, sizeof(*s->prescreen_buf));
    if (!s->prescreen_buf)
        return AVERROR(ENOMEM);

    for (int i = 0; i < s->nb_threads; i++) {
        s->prescreen_buf[i] = av_calloc(s->planewidth[0], sizeof(**s->prescreen_buf));
        if (!s->prescreen_buf[i])
            return AVERROR(ENOMEM);
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    NNEDIContext *s = ctx->priv;

    for (int i = 0; i < s->nb_threads && s->prescreen_buf; i++)
        av_freep(&s->prescreen_buf[i]);

    av_freep(&s->prescreen_buf);

    for (int i = 0; i < s->nb_threads && s->input_buf; i++)
        av_freep(&s->input_buf[i]);

    av_freep(&s->input_buf);

    for (int i = 0; i < s->nb_threads && s->output_buf; i++)
        av_freep(&s->output_buf[i]);

    av_freep(&s->output_buf);
    av_freep(&s->fdsp);

    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 5; j++) {
            for (int k = 0; k < 7; k++) {
                av_freep(&s->coeffs[i][j][k].data);
            }
        }
    }

    av_frame_free(&s->prev);
}

static const AVFilterPad inputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .filter_frame  = filter_frame,
        .config_props  = config_input,
    },
};

static const AVFilterPad outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
        .request_frame = request_frame,
    },
};

const FFFilter ff_vf_nnedi = {
    .p.name        = "nnedi",
    .p.description = NULL_IF_CONFIG_SMALL("Apply neural network edge directed interpolation intra-only deinterlacer."),
    .p.priv_class  = &nnedi_class,
    .p.flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL | AVFILTER_FLAG_SLICE_THREADS,
    .priv_size     = sizeof(NNEDIContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .process_command = ff_filter_process_command,
};
