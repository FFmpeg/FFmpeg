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
#include "libavutil/float_dsp.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct FrameData {
    uint8_t *paddedp[3];
    int padded_stride[3];
    int padded_width[3];
    int padded_height[3];

    uint8_t *dstp[3];
    int dst_stride[3];

    int field[3];

    int32_t *lcount[3];
    float *input;
    float *temp;
} FrameData;

typedef struct NNEDIContext {
    const AVClass *class;

    char *weights_file;

    AVFrame *src;
    AVFrame *second;
    AVFrame *dst;
    int eof;
    int64_t cur_pts;

    AVFloatDSPContext *fdsp;
    int nb_planes;
    int linesize[4];
    int planeheight[4];

    float *weights0;
    float *weights1[2];
    int asize;
    int nns;
    int xdia;
    int ydia;

    // Parameters
    int deint;
    int field;
    int process_plane;
    int nsize;
    int nnsparam;
    int qual;
    int etype;
    int pscrn;
    int fapprox;

    int max_value;

    void (*copy_pad)(const AVFrame *, FrameData *, struct NNEDIContext *, int);
    void (*evalfunc_0)(struct NNEDIContext *, FrameData *);
    void (*evalfunc_1)(struct NNEDIContext *, FrameData *);

    // Functions used in evalfunc_0
    void (*readpixels)(const uint8_t *, const int, float *);
    void (*compute_network0)(struct NNEDIContext *s, const float *, const float *, uint8_t *);
    int32_t (*process_line0)(const uint8_t *, int, uint8_t *, const uint8_t *, const int, const int, const int);

    // Functions used in evalfunc_1
    void (*extract)(const uint8_t *, const int, const int, const int, float *, float *);
    void (*dot_prod)(struct NNEDIContext *, const float *, const float *, float *, const int, const int, const float *);
    void (*expfunc)(float *, const int);
    void (*wae5)(const float *, const int, float *);

    FrameData frame_data;
} NNEDIContext;

#define OFFSET(x) offsetof(NNEDIContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption nnedi_options[] = {
    {"weights",  "set weights file", OFFSET(weights_file),  AV_OPT_TYPE_STRING, {.str="nnedi3_weights.bin"}, 0, 0, FLAGS },
    {"deint",         "set which frames to deinterlace", OFFSET(deint),         AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS, "deint" },
        {"all",        "deinterlace all frames",                       0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "deint" },
        {"interlaced", "only deinterlace frames marked as interlaced", 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "deint" },
    {"field",  "set mode of operation", OFFSET(field),         AV_OPT_TYPE_INT, {.i64=-1}, -2, 3, FLAGS, "field" },
        {"af", "use frame flags, both fields",  0, AV_OPT_TYPE_CONST, {.i64=-2}, 0, 0, FLAGS, "field" },
        {"a",  "use frame flags, single field", 0, AV_OPT_TYPE_CONST, {.i64=-1}, 0, 0, FLAGS, "field" },
        {"t",  "use top field only",            0, AV_OPT_TYPE_CONST, {.i64=0},  0, 0, FLAGS, "field" },
        {"b",  "use bottom field only",         0, AV_OPT_TYPE_CONST, {.i64=1},  0, 0, FLAGS, "field" },
        {"tf", "use both fields, top first",    0, AV_OPT_TYPE_CONST, {.i64=2}, 0, 0, FLAGS, "field" },
        {"bf", "use both fields, bottom first", 0, AV_OPT_TYPE_CONST, {.i64=3}, 0, 0, FLAGS, "field" },
    {"planes", "set which planes to process", OFFSET(process_plane), AV_OPT_TYPE_INT, {.i64=7}, 0, 7, FLAGS },
    {"nsize",  "set size of local neighborhood around each pixel, used by the predictor neural network", OFFSET(nsize), AV_OPT_TYPE_INT, {.i64=6}, 0, 6, FLAGS, "nsize" },
        {"s8x6",     NULL, 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "nsize" },
        {"s16x6",    NULL, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "nsize" },
        {"s32x6",    NULL, 0, AV_OPT_TYPE_CONST, {.i64=2}, 0, 0, FLAGS, "nsize" },
        {"s48x6",    NULL, 0, AV_OPT_TYPE_CONST, {.i64=3}, 0, 0, FLAGS, "nsize" },
        {"s8x4",     NULL, 0, AV_OPT_TYPE_CONST, {.i64=4}, 0, 0, FLAGS, "nsize" },
        {"s16x4",    NULL, 0, AV_OPT_TYPE_CONST, {.i64=5}, 0, 0, FLAGS, "nsize" },
        {"s32x4",    NULL, 0, AV_OPT_TYPE_CONST, {.i64=6}, 0, 0, FLAGS, "nsize" },
    {"nns",    "set number of neurons in predictor neural network", OFFSET(nnsparam), AV_OPT_TYPE_INT, {.i64=1}, 0, 4, FLAGS, "nns" },
        {"n16",       NULL, 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "nns" },
        {"n32",       NULL, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "nns" },
        {"n64",       NULL, 0, AV_OPT_TYPE_CONST, {.i64=2}, 0, 0, FLAGS, "nns" },
        {"n128",      NULL, 0, AV_OPT_TYPE_CONST, {.i64=3}, 0, 0, FLAGS, "nns" },
        {"n256",      NULL, 0, AV_OPT_TYPE_CONST, {.i64=4}, 0, 0, FLAGS, "nns" },
    {"qual",  "set quality", OFFSET(qual), AV_OPT_TYPE_INT, {.i64=1}, 1, 2, FLAGS, "qual" },
        {"fast", NULL, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "qual" },
        {"slow", NULL, 0, AV_OPT_TYPE_CONST, {.i64=2}, 0, 0, FLAGS, "qual" },
    {"etype", "set which set of weights to use in the predictor", OFFSET(etype), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS, "etype" },
        {"a",  "weights trained to minimize absolute error", 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "etype" },
        {"s",  "weights trained to minimize squared error",  0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "etype" },
    {"pscrn", "set prescreening", OFFSET(pscrn), AV_OPT_TYPE_INT, {.i64=2}, 0, 2, FLAGS, "pscrn" },
        {"none",      NULL, 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "pscrn" },
        {"original",  NULL, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "pscrn" },
        {"new",       NULL, 0, AV_OPT_TYPE_CONST, {.i64=2}, 0, 0, FLAGS, "pscrn" },
    {"fapprox",       NULL, OFFSET(fapprox),       AV_OPT_TYPE_INT, {.i64=0}, 0, 3, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(nnedi);

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    NNEDIContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int ret;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);
    if ((ret = av_image_fill_linesizes(s->linesize, inlink->format, inlink->w)) < 0)
        return ret;

    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    NNEDIContext *s = ctx->priv;

    outlink->time_base.num = ctx->inputs[0]->time_base.num;
    outlink->time_base.den = ctx->inputs[0]->time_base.den * 2;
    outlink->w             = ctx->inputs[0]->w;
    outlink->h             = ctx->inputs[0]->h;

    if (s->field > 1 || s->field == -2)
        outlink->frame_rate = av_mul_q(ctx->inputs[0]->frame_rate,
                                       (AVRational){2, 1});

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUVJ411P,
        AV_PIX_FMT_GBRP,
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static void copy_pad(const AVFrame *src, FrameData *frame_data, NNEDIContext *s, int fn)
{
    const int off = 1 - fn;
    int plane, y, x;

    for (plane = 0; plane < s->nb_planes; plane++) {
        const uint8_t *srcp = (const uint8_t *)src->data[plane];
        uint8_t *dstp = (uint8_t *)frame_data->paddedp[plane];

        const int src_stride = src->linesize[plane];
        const int dst_stride = frame_data->padded_stride[plane];

        const int src_height = s->planeheight[plane];
        const int dst_height = frame_data->padded_height[plane];

        const int src_width = s->linesize[plane];
        const int dst_width = frame_data->padded_width[plane];

        int c = 4;

        if (!(s->process_plane & (1 << plane)))
            continue;

        // Copy.
        for (y = off; y < src_height; y += 2)
            memcpy(dstp + 32 + (6 + y) * dst_stride,
                   srcp + y * src_stride,
                   src_width * sizeof(uint8_t));

        // And pad.
        dstp += (6 + off) * dst_stride;
        for (y = 6 + off; y < dst_height - 6; y += 2) {
            int c = 2;

            for (x = 0; x < 32; x++)
                dstp[x] = dstp[64 - x];

            for (x = dst_width - 32; x < dst_width; x++, c += 2)
                dstp[x] = dstp[x - c];

            dstp += dst_stride * 2;
        }

        dstp = (uint8_t *)frame_data->paddedp[plane];
        for (y = off; y < 6; y += 2)
            memcpy(dstp + y * dst_stride,
                   dstp + (12 + 2 * off - y) * dst_stride,
                   dst_width * sizeof(uint8_t));

        for (y = dst_height - 6 + off; y < dst_height; y += 2, c += 4)
            memcpy(dstp + y * dst_stride,
                   dstp + (y - c) * dst_stride,
                   dst_width * sizeof(uint8_t));
    }
}

static void elliott(float *data, const int n)
{
    int i;

    for (i = 0; i < n; i++)
        data[i] = data[i] / (1.0f + FFABS(data[i]));
}

static void dot_prod(NNEDIContext *s, const float *data, const float *weights, float *vals, const int n, const int len, const float *scale)
{
    int i;

    for (i = 0; i < n; i++) {
        float sum;

        sum = s->fdsp->scalarproduct_float(data, &weights[i * len], len);

        vals[i] = sum * scale[0] + weights[n * len + i];
    }
}

static void dot_prods(NNEDIContext *s, const float *dataf, const float *weightsf, float *vals, const int n, const int len, const float *scale)
{
    const int16_t *data = (int16_t *)dataf;
    const int16_t *weights = (int16_t *)weightsf;
    const float *wf = (float *)&weights[n * len];
    int i, j;

    for (i = 0; i < n; i++) {
        int sum = 0, off = ((i >> 2) << 3) + (i & 3);
        for (j = 0; j < len; j++)
            sum += data[j] * weights[i * len + j];

        vals[i] = sum * wf[off] * scale[0] + wf[off + 4];
    }
}

static void compute_network0(NNEDIContext *s, const float *input, const float *weights, uint8_t *d)
{
    float t, temp[12], scale = 1.0f;

    dot_prod(s, input, weights, temp, 4, 48, &scale);
    t = temp[0];
    elliott(temp, 4);
    temp[0] = t;
    dot_prod(s, temp, weights + 4 * 49, temp + 4, 4, 4, &scale);
    elliott(temp + 4, 4);
    dot_prod(s, temp, weights + 4 * 49 + 4 * 5, temp + 8, 4, 8, &scale);
    if (FFMAX(temp[10], temp[11]) <= FFMAX(temp[8], temp[9]))
        d[0] = 1;
    else
        d[0] = 0;
}

static void compute_network0_i16(NNEDIContext *s, const float *inputf, const float *weightsf, uint8_t *d)
{
    const float *wf = weightsf + 2 * 48;
    float t, temp[12], scale = 1.0f;

    dot_prods(s, inputf, weightsf, temp, 4, 48, &scale);
    t = temp[0];
    elliott(temp, 4);
    temp[0] = t;
    dot_prod(s, temp, wf + 8, temp + 4, 4, 4, &scale);
    elliott(temp + 4, 4);
    dot_prod(s, temp, wf + 8 + 4 * 5, temp + 8, 4, 8, &scale);
    if (FFMAX(temp[10], temp[11]) <= FFMAX(temp[8], temp[9]))
        d[0] = 1;
    else
        d[0] = 0;
}

static void pixel2float48(const uint8_t *t8, const int pitch, float *p)
{
    const uint8_t *t = (const uint8_t *)t8;
    int y, x;

    for (y = 0; y < 4; y++)
        for (x = 0; x < 12; x++)
            p[y * 12 + x] = t[y * pitch * 2 + x];
}

static void byte2word48(const uint8_t *t, const int pitch, float *pf)
{
    int16_t *p = (int16_t *)pf;
    int y, x;

    for (y = 0; y < 4; y++)
        for (x = 0; x < 12; x++)
            p[y * 12 + x] = t[y * pitch * 2 + x];
}

static int32_t process_line0(const uint8_t *tempu, int width, uint8_t *dstp8, const uint8_t *src3p8, const int src_pitch, const int max_value, const int chroma)
{
    uint8_t *dstp = (uint8_t *)dstp8;
    const uint8_t *src3p = (const uint8_t *)src3p8;
    int minimum = 0;
    int maximum = max_value - 1; // Technically the -1 is only needed for 8 and 16 bit input.
    int count = 0, x;
    for (x = 0; x < width; x++) {
        if (tempu[x]) {
            int tmp = 19 * (src3p[x + src_pitch * 2] + src3p[x + src_pitch * 4]) - 3 * (src3p[x] + src3p[x + src_pitch * 6]);
            tmp /= 32;
            dstp[x] = FFMAX(FFMIN(tmp, maximum), minimum);
        } else {
            dstp[x] = 255;
            count++;
        }
    }
    return count;
}

// new prescreener functions
static void byte2word64(const uint8_t *t, const int pitch, float *p)
{
    int16_t *ps = (int16_t *)p;
    int y, x;

    for (y = 0; y < 4; y++)
        for (x = 0; x < 16; x++)
            ps[y * 16 + x] = t[y * pitch * 2 + x];
}

static void compute_network0new(NNEDIContext *s, const float *datai, const float *weights, uint8_t *d)
{
    int16_t *data = (int16_t *)datai;
    int16_t *ws = (int16_t *)weights;
    float *wf = (float *)&ws[4 * 64];
    float vals[8];
    int mask, i, j;

    for (i = 0; i < 4; i++) {
        int sum = 0;
        float t;

        for (j = 0; j < 64; j++)
            sum += data[j] * ws[(i << 3) + ((j >> 3) << 5) + (j & 7)];
        t = sum * wf[i] + wf[4 + i];
        vals[i] = t / (1.0f + FFABS(t));
    }

    for (i = 0; i < 4; i++) {
        float sum = 0.0f;

        for (j = 0; j < 4; j++)
            sum += vals[j] * wf[8 + i + (j << 2)];
        vals[4 + i] = sum + wf[8 + 16 + i];
    }

    mask = 0;
    for (i = 0; i < 4; i++) {
        if (vals[4 + i] > 0.0f)
            mask |= (0x1 << (i << 3));
    }

    ((int *)d)[0] = mask;
}

static void evalfunc_0(NNEDIContext *s, FrameData *frame_data)
{
    float *input = frame_data->input;
    const float *weights0 = s->weights0;
    float *temp = frame_data->temp;
    uint8_t *tempu = (uint8_t *)temp;
    int plane, x, y;

    // And now the actual work.
    for (plane = 0; plane < s->nb_planes; plane++) {
        const uint8_t *srcp = (const uint8_t *)frame_data->paddedp[plane];
        const int src_stride = frame_data->padded_stride[plane] / sizeof(uint8_t);

        const int width = frame_data->padded_width[plane];
        const int height = frame_data->padded_height[plane];

        uint8_t *dstp = (uint8_t *)frame_data->dstp[plane];
        const int dst_stride = frame_data->dst_stride[plane] / sizeof(uint8_t);
        const uint8_t *src3p;
        int ystart, ystop;
        int32_t *lcount;

        if (!(s->process_plane & (1 << plane)))
            continue;

        for (y = 1 - frame_data->field[plane]; y < height - 12; y += 2) {
            memcpy(dstp + y * dst_stride,
                   srcp + 32 + (6 + y) * src_stride,
                   (width - 64) * sizeof(uint8_t));

        }

        ystart = 6 + frame_data->field[plane];
        ystop = height - 6;
        srcp += ystart * src_stride;
        dstp += (ystart - 6) * dst_stride - 32;
        src3p = srcp - src_stride * 3;
        lcount = frame_data->lcount[plane] - 6;

        if (s->pscrn == 1) { // original
            for (y = ystart; y < ystop; y += 2) {
                for (x = 32; x < width - 32; x++) {
                    s->readpixels((const uint8_t *)(src3p + x - 5), src_stride, input);
                    s->compute_network0(s, input, weights0, tempu+x);
                }
                lcount[y] += s->process_line0(tempu + 32, width - 64, (uint8_t *)(dstp + 32), (const uint8_t *)(src3p + 32), src_stride, s->max_value, plane);
                src3p += src_stride * 2;
                dstp += dst_stride * 2;
            }
        } else if (s->pscrn > 1) { // new
            for (y = ystart; y < ystop; y += 2) {
                for (x = 32; x < width - 32; x += 4) {
                    s->readpixels((const uint8_t *)(src3p + x - 6), src_stride, input);
                    s->compute_network0(s, input, weights0, tempu + x);
                }
                lcount[y] += s->process_line0(tempu + 32, width - 64, (uint8_t *)(dstp + 32), (const uint8_t *)(src3p + 32), src_stride, s->max_value, plane);
                src3p += src_stride * 2;
                dstp += dst_stride * 2;
            }
        } else { // no prescreening
            for (y = ystart; y < ystop; y += 2) {
                memset(dstp + 32, 255, (width - 64) * sizeof(uint8_t));
                lcount[y] += width - 64;
                dstp += dst_stride * 2;
            }
        }
    }
}

static void extract_m8(const uint8_t *srcp8, const int stride, const int xdia, const int ydia, float *mstd, float *input)
{
    // uint8_t or uint16_t or float
    const uint8_t *srcp = (const uint8_t *)srcp8;
    float scale;
    double tmp;

    // int32_t or int64_t or double
    int64_t sum = 0, sumsq = 0;
    int y, x;

    for (y = 0; y < ydia; y++) {
        const uint8_t *srcpT = srcp + y * stride * 2;

        for (x = 0; x < xdia; x++) {
            sum += srcpT[x];
            sumsq += (uint32_t)srcpT[x] * (uint32_t)srcpT[x];
            input[x] = srcpT[x];
        }
        input += xdia;
    }
    scale = 1.0f / (xdia * ydia);
    mstd[0] = sum * scale;
    tmp = (double)sumsq * scale - (double)mstd[0] * mstd[0];
    mstd[3] = 0.0f;
    if (tmp <= FLT_EPSILON)
        mstd[1] = mstd[2] = 0.0f;
    else {
        mstd[1] = sqrt(tmp);
        mstd[2] = 1.0f / mstd[1];
    }
}

static void extract_m8_i16(const uint8_t *srcp, const int stride, const int xdia, const int ydia, float *mstd, float *inputf)
{
    int16_t *input = (int16_t *)inputf;
    float scale;
    int sum = 0, sumsq = 0;
    int y, x;

    for (y = 0; y < ydia; y++) {
        const uint8_t *srcpT = srcp + y * stride * 2;
        for (x = 0; x < xdia; x++) {
            sum += srcpT[x];
            sumsq += srcpT[x] * srcpT[x];
            input[x] = srcpT[x];
        }
        input += xdia;
    }
    scale = 1.0f / (float)(xdia * ydia);
    mstd[0] = sum * scale;
    mstd[1] = sumsq * scale - mstd[0] * mstd[0];
    mstd[3] = 0.0f;
    if (mstd[1] <= FLT_EPSILON)
        mstd[1] = mstd[2] = 0.0f;
    else {
        mstd[1] = sqrt(mstd[1]);
        mstd[2] = 1.0f / mstd[1];
    }
}


static const float exp_lo = -80.0f;
static const float exp_hi = +80.0f;

static void e2_m16(float *s, const int n)
{
    int i;

    for (i = 0; i < n; i++)
        s[i] = exp(av_clipf(s[i], exp_lo, exp_hi));
}

const float min_weight_sum = 1e-10f;

static void weighted_avg_elliott_mul5_m16(const float *w, const int n, float *mstd)
{
    float vsum = 0.0f, wsum = 0.0f;
    int i;

    for (i = 0; i < n; i++) {
        vsum += w[i] * (w[n + i] / (1.0f + FFABS(w[n + i])));
        wsum += w[i];
    }
    if (wsum > min_weight_sum)
        mstd[3] += ((5.0f * vsum) / wsum) * mstd[1] + mstd[0];
    else
        mstd[3] += mstd[0];
}


static void evalfunc_1(NNEDIContext *s, FrameData *frame_data)
{
    float *input = frame_data->input;
    float *temp = frame_data->temp;
    float **weights1 = s->weights1;
    const int qual = s->qual;
    const int asize = s->asize;
    const int nns = s->nns;
    const int xdia = s->xdia;
    const int xdiad2m1 = (xdia / 2) - 1;
    const int ydia = s->ydia;
    const float scale = 1.0f / (float)qual;
    int plane, y, x, i;

    for (plane = 0; plane < s->nb_planes; plane++) {
        const uint8_t *srcp = (const uint8_t *)frame_data->paddedp[plane];
        const int src_stride = frame_data->padded_stride[plane] / sizeof(uint8_t);

        const int width = frame_data->padded_width[plane];
        const int height = frame_data->padded_height[plane];

        uint8_t *dstp = (uint8_t *)frame_data->dstp[plane];
        const int dst_stride = frame_data->dst_stride[plane] / sizeof(uint8_t);

        const int ystart = frame_data->field[plane];
        const int ystop = height - 12;
        const uint8_t *srcpp;

        if (!(s->process_plane & (1 << plane)))
            continue;

        srcp += (ystart + 6) * src_stride;
        dstp += ystart * dst_stride - 32;
        srcpp = srcp - (ydia - 1) * src_stride - xdiad2m1;

        for (y = ystart; y < ystop; y += 2) {
            for (x = 32; x < width - 32; x++) {
                float mstd[4];

                if (dstp[x] != 255)
                    continue;

                s->extract((const uint8_t *)(srcpp + x), src_stride, xdia, ydia, mstd, input);
                for (i = 0; i < qual; i++) {
                    s->dot_prod(s, input, weights1[i], temp, nns * 2, asize, mstd + 2);
                    s->expfunc(temp, nns);
                    s->wae5(temp, nns, mstd);
                }

                dstp[x] = FFMIN(FFMAX((int)(mstd[3] * scale + 0.5f), 0), s->max_value);
            }
            srcpp += src_stride * 2;
            dstp += dst_stride * 2;
        }
    }
}

#define NUM_NSIZE 7
#define NUM_NNS 5

static int roundds(const double f)
{
    if (f - floor(f) >= 0.5)
        return FFMIN((int)ceil(f), 32767);
    return FFMAX((int)floor(f), -32768);
}

static void select_functions(NNEDIContext *s)
{
    s->copy_pad = copy_pad;
    s->evalfunc_0 = evalfunc_0;
    s->evalfunc_1 = evalfunc_1;

    // evalfunc_0
    s->process_line0 = process_line0;

    if (s->pscrn < 2) { // original prescreener
        if (s->fapprox & 1) { // int16 dot products
            s->readpixels = byte2word48;
            s->compute_network0 = compute_network0_i16;
        } else {
            s->readpixels = pixel2float48;
            s->compute_network0 = compute_network0;
        }
    } else { // new prescreener
        // only int16 dot products
        s->readpixels = byte2word64;
        s->compute_network0 = compute_network0new;
    }

    // evalfunc_1
    s->wae5 = weighted_avg_elliott_mul5_m16;

    if (s->fapprox & 2) { // use int16 dot products
        s->extract = extract_m8_i16;
        s->dot_prod = dot_prods;
    } else { // use float dot products
        s->extract = extract_m8;
        s->dot_prod = dot_prod;
    }

    s->expfunc = e2_m16;
}

static int modnpf(const int m, const int n)
{
    if ((m % n) == 0)
        return m;
    return m + n - (m % n);
}

static int get_frame(AVFilterContext *ctx, int is_second)
{
    NNEDIContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *src = s->src;
    FrameData *frame_data;
    int effective_field = s->field;
    size_t temp_size;
    int field_n;
    int plane;

    if (effective_field > 1)
        effective_field -= 2;
    else if (effective_field < 0)
        effective_field += 2;

    if (s->field < 0 && src->interlaced_frame && src->top_field_first == 0)
        effective_field = 0;
    else if (s->field < 0 && src->interlaced_frame && src->top_field_first == 1)
        effective_field = 1;
    else
        effective_field = !effective_field;

    if (s->field > 1 || s->field == -2) {
        if (is_second) {
            field_n = (effective_field == 0);
        } else {
            field_n = (effective_field == 1);
        }
    } else {
        field_n = effective_field;
    }

    s->dst = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!s->dst)
        return AVERROR(ENOMEM);
    av_frame_copy_props(s->dst, src);
    s->dst->interlaced_frame = 0;

    frame_data = &s->frame_data;

    for (plane = 0; plane < s->nb_planes; plane++) {
        int dst_height = s->planeheight[plane];
        int dst_width = s->linesize[plane];

        const int min_alignment = 16;
        const int min_pad = 10;

        if (!(s->process_plane & (1 << plane))) {
            av_image_copy_plane(s->dst->data[plane], s->dst->linesize[plane],
                                src->data[plane], src->linesize[plane],
                                s->linesize[plane],
                                s->planeheight[plane]);
            continue;
        }

        frame_data->padded_width[plane]  = dst_width + 64;
        frame_data->padded_height[plane] = dst_height + 12;
        frame_data->padded_stride[plane] = modnpf(frame_data->padded_width[plane] + min_pad, min_alignment); // TODO: maybe min_pad is in pixels too?
        if (!frame_data->paddedp[plane]) {
            frame_data->paddedp[plane] = av_malloc_array(frame_data->padded_stride[plane], frame_data->padded_height[plane]);
            if (!frame_data->paddedp[plane])
                return AVERROR(ENOMEM);
        }

        frame_data->dstp[plane] = s->dst->data[plane];
        frame_data->dst_stride[plane] = s->dst->linesize[plane];

        if (!frame_data->lcount[plane]) {
            frame_data->lcount[plane] = av_calloc(dst_height, sizeof(int32_t) * 16);
            if (!frame_data->lcount[plane])
                return AVERROR(ENOMEM);
        } else {
            memset(frame_data->lcount[plane], 0, dst_height * sizeof(int32_t) * 16);
        }

        frame_data->field[plane] = field_n;
    }

    if (!frame_data->input) {
        frame_data->input = av_malloc(512 * sizeof(float));
        if (!frame_data->input)
            return AVERROR(ENOMEM);
    }
    // evalfunc_0 requires at least padded_width[0] bytes.
    // evalfunc_1 requires at least 512 floats.
    if (!frame_data->temp) {
        temp_size = FFMAX(frame_data->padded_width[0], 512 * sizeof(float));
        frame_data->temp = av_malloc(temp_size);
        if (!frame_data->temp)
            return AVERROR(ENOMEM);
    }

    // Copy src to a padded "frame" in frame_data and mirror the edges.
    s->copy_pad(src, frame_data, s, field_n);

    // Handles prescreening and the cubic interpolation.
    s->evalfunc_0(s, frame_data);

    // The rest.
    s->evalfunc_1(s, frame_data);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *src)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    NNEDIContext *s = ctx->priv;
    int ret;

    if ((s->field > 1 ||
         s->field == -2) && !s->second) {
        goto second;
    } else if (s->field > 1 ||
               s->field == -2) {
        AVFrame *dst;

        s->src = s->second;
        ret = get_frame(ctx, 1);
        if (ret < 0) {
            av_frame_free(&s->dst);
            av_frame_free(&s->second);
            s->src = NULL;
            return ret;
        }
        dst = s->dst;

        if (src->pts != AV_NOPTS_VALUE &&
            dst->pts != AV_NOPTS_VALUE)
            dst->pts += src->pts;
        else
            dst->pts = AV_NOPTS_VALUE;

        ret = ff_filter_frame(outlink, dst);
        if (ret < 0)
            return ret;
        if (s->eof)
            return 0;
        s->cur_pts = s->second->pts;
        av_frame_free(&s->second);
second:
        if ((s->deint && src->interlaced_frame &&
             !ctx->is_disabled) ||
            (!s->deint && !ctx->is_disabled)) {
            s->second = src;
        }
    }

    if ((s->deint && !src->interlaced_frame) || ctx->is_disabled) {
        AVFrame *dst = av_frame_clone(src);
        if (!dst) {
            av_frame_free(&src);
            av_frame_free(&s->second);
            return AVERROR(ENOMEM);
        }

        if (s->field > 1 || s->field == -2) {
            av_frame_free(&s->second);
            if ((s->deint && src->interlaced_frame) ||
                (!s->deint))
                s->second = src;
        } else {
            av_frame_free(&src);
        }
        if (dst->pts != AV_NOPTS_VALUE)
            dst->pts *= 2;
        return ff_filter_frame(outlink, dst);
    }

    s->src = src;
    ret = get_frame(ctx, 0);
    if (ret < 0) {
        av_frame_free(&s->dst);
        av_frame_free(&s->src);
        av_frame_free(&s->second);
        return ret;
    }

    if (src->pts != AV_NOPTS_VALUE)
        s->dst->pts = src->pts * 2;
    if (s->field <= 1 && s->field > -2) {
        av_frame_free(&src);
        s->src = NULL;
    }

    return ff_filter_frame(outlink, s->dst);
}

static int request_frame(AVFilterLink *link)
{
    AVFilterContext *ctx = link->src;
    NNEDIContext *s = ctx->priv;
    int ret;

    if (s->eof)
        return AVERROR_EOF;

    ret  = ff_request_frame(ctx->inputs[0]);

    if (ret == AVERROR_EOF && s->second) {
        AVFrame *next = av_frame_clone(s->second);

        if (!next)
            return AVERROR(ENOMEM);

        next->pts = s->second->pts * 2 - s->cur_pts;
        s->eof = 1;

        filter_frame(ctx->inputs[0], next);
    } else if (ret < 0) {
        return ret;
    }

    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    NNEDIContext *s = ctx->priv;
    FILE *weights_file = NULL;
    int64_t expected_size = 13574928;
    int64_t weights_size;
    float *bdata;
    size_t bytes_read;
    const int xdia_table[NUM_NSIZE] = { 8, 16, 32, 48, 8, 16, 32 };
    const int ydia_table[NUM_NSIZE] = { 6, 6, 6, 6, 4, 4, 4 };
    const int nns_table[NUM_NNS] = { 16, 32, 64, 128, 256 };
    const int dims0 = 49 * 4 + 5 * 4 + 9 * 4;
    const int dims0new = 4 * 65 + 4 * 5;
    const int dims1 = nns_table[s->nnsparam] * 2 * (xdia_table[s->nsize] * ydia_table[s->nsize] + 1);
    int dims1tsize = 0;
    int dims1offset = 0;
    int ret = 0, i, j, k;

    weights_file = fopen(s->weights_file, "rb");
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
    } else if (weights_size != expected_size) {
        fclose(weights_file);
        av_log(ctx, AV_LOG_ERROR, "Unexpected weights file size.\n");
        return AVERROR(EINVAL);
    }

    if (fseek(weights_file, 0, SEEK_SET)) {
        fclose(weights_file);
        av_log(ctx, AV_LOG_ERROR, "Couldn't seek to the start of weights file.\n");
        return AVERROR(EINVAL);
    }

    bdata = (float *)av_malloc(expected_size);
    if (!bdata) {
        fclose(weights_file);
        return AVERROR(ENOMEM);
    }

    bytes_read = fread(bdata, 1, expected_size, weights_file);

    if (bytes_read != (size_t)expected_size) {
        fclose(weights_file);
        ret = AVERROR_INVALIDDATA;
        av_log(ctx, AV_LOG_ERROR, "Couldn't read weights file.\n");
        goto fail;
    }

    fclose(weights_file);

    for (j = 0; j < NUM_NNS; j++) {
        for (i = 0; i < NUM_NSIZE; i++) {
            if (i == s->nsize && j == s->nnsparam)
                dims1offset = dims1tsize;
            dims1tsize += nns_table[j] * 2 * (xdia_table[i] * ydia_table[i] + 1) * 2;
        }
    }

    s->weights0 = av_malloc_array(FFMAX(dims0, dims0new), sizeof(float));
    if (!s->weights0) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    for (i = 0; i < 2; i++) {
        s->weights1[i] = av_malloc_array(dims1, sizeof(float));
        if (!s->weights1[i]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    // Adjust prescreener weights
    if (s->pscrn >= 2) {// using new prescreener
        const float *bdw;
        int16_t *ws;
        float *wf;
        double mean[4] = { 0.0, 0.0, 0.0, 0.0 };
        int *offt = av_calloc(4 * 64, sizeof(int));

        if (!offt) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        for (j = 0; j < 4; j++)
            for (k = 0; k < 64; k++)
                offt[j * 64 + k] = ((k >> 3) << 5) + ((j & 3) << 3) + (k & 7);

        bdw = bdata + dims0 + dims0new * (s->pscrn - 2);
        ws = (int16_t *)s->weights0;
        wf = (float *)&ws[4 * 64];
        // Calculate mean weight of each first layer neuron
        for (j = 0; j < 4; j++) {
            double cmean = 0.0;
            for (k = 0; k < 64; k++)
                cmean += bdw[offt[j * 64 + k]];
            mean[j] = cmean / 64.0;
        }
        // Factor mean removal and 1.0/127.5 scaling
        // into first layer weights. scale to int16 range
        for (j = 0; j < 4; j++) {
            double scale, mval = 0.0;

            for (k = 0; k < 64; k++)
                mval = FFMAX(mval, FFABS((bdw[offt[j * 64 + k]] - mean[j]) / 127.5));
            scale = 32767.0 / mval;
            for (k = 0; k < 64; k++)
                ws[offt[j * 64 + k]] = roundds(((bdw[offt[j * 64 + k]] - mean[j]) / 127.5) * scale);
            wf[j] = (float)(mval / 32767.0);
        }
        memcpy(wf + 4, bdw + 4 * 64, (dims0new - 4 * 64) * sizeof(float));
        av_free(offt);
    } else { // using old prescreener
        double mean[4] = { 0.0, 0.0, 0.0, 0.0 };
        // Calculate mean weight of each first layer neuron
        for (j = 0; j < 4; j++) {
            double cmean = 0.0;
            for (k = 0; k < 48; k++)
                cmean += bdata[j * 48 + k];
            mean[j] = cmean / 48.0;
        }
        if (s->fapprox & 1) {// use int16 dot products in first layer
            int16_t *ws = (int16_t *)s->weights0;
            float *wf = (float *)&ws[4 * 48];
            // Factor mean removal and 1.0/127.5 scaling
            // into first layer weights. scale to int16 range
            for (j = 0; j < 4; j++) {
                double scale, mval = 0.0;
                for (k = 0; k < 48; k++)
                    mval = FFMAX(mval, FFABS((bdata[j * 48 + k] - mean[j]) / 127.5));
                scale = 32767.0 / mval;
                for (k = 0; k < 48; k++)
                    ws[j * 48 + k] = roundds(((bdata[j * 48 + k] - mean[j]) / 127.5) * scale);
                wf[j] = (float)(mval / 32767.0);
            }
            memcpy(wf + 4, bdata + 4 * 48, (dims0 - 4 * 48) * sizeof(float));
        } else {// use float dot products in first layer
            double half = (1 << 8) - 1;

            half /= 2;

            // Factor mean removal and 1.0/half scaling
            // into first layer weights.
            for (j = 0; j < 4; j++)
                for (k = 0; k < 48; k++)
                    s->weights0[j * 48 + k] = (float)((bdata[j * 48 + k] - mean[j]) / half);
            memcpy(s->weights0 + 4 * 48, bdata + 4 * 48, (dims0 - 4 * 48) * sizeof(float));
        }
    }

    // Adjust prediction weights
    for (i = 0; i < 2; i++) {
        const float *bdataT = bdata + dims0 + dims0new * 3 + dims1tsize * s->etype + dims1offset + i * dims1;
        const int nnst = nns_table[s->nnsparam];
        const int asize = xdia_table[s->nsize] * ydia_table[s->nsize];
        const int boff = nnst * 2 * asize;
        double *mean = (double *)av_calloc(asize + 1 + nnst * 2, sizeof(double));

        if (!mean) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        // Calculate mean weight of each neuron (ignore bias)
        for (j = 0; j < nnst * 2; j++) {
            double cmean = 0.0;
            for (k = 0; k < asize; k++)
                cmean += bdataT[j * asize + k];
            mean[asize + 1 + j] = cmean / (double)asize;
        }
        // Calculate mean softmax neuron
        for (j = 0; j < nnst; j++) {
            for (k = 0; k < asize; k++)
                mean[k] += bdataT[j * asize + k] - mean[asize + 1 + j];
            mean[asize] += bdataT[boff + j];
        }
        for (j = 0; j < asize + 1; j++)
            mean[j] /= (double)(nnst);

        if (s->fapprox & 2) { // use int16 dot products
            int16_t *ws = (int16_t *)s->weights1[i];
            float *wf = (float *)&ws[nnst * 2 * asize];
            // Factor mean removal into weights, remove global offset from
            // softmax neurons, and scale weights to int16 range.
            for (j = 0; j < nnst; j++) { // softmax neurons
                double scale, mval = 0.0;
                for (k = 0; k < asize; k++)
                    mval = FFMAX(mval, FFABS(bdataT[j * asize + k] - mean[asize + 1 + j] - mean[k]));
                scale = 32767.0 / mval;
                for (k = 0; k < asize; k++)
                    ws[j * asize + k] = roundds((bdataT[j * asize + k] - mean[asize + 1 + j] - mean[k]) * scale);
                wf[(j >> 2) * 8 + (j & 3)] = (float)(mval / 32767.0);
                wf[(j >> 2) * 8 + (j & 3) + 4] = (float)(bdataT[boff + j] - mean[asize]);
            }
            for (j = nnst; j < nnst * 2; j++) { // elliott neurons
                double scale, mval = 0.0;
                for (k = 0; k < asize; k++)
                    mval = FFMAX(mval, FFABS(bdataT[j * asize + k] - mean[asize + 1 + j]));
                scale = 32767.0 / mval;
                for (k = 0; k < asize; k++)
                    ws[j * asize + k] = roundds((bdataT[j * asize + k] - mean[asize + 1 + j]) * scale);
                wf[(j >> 2) * 8 + (j & 3)] = (float)(mval / 32767.0);
                wf[(j >> 2) * 8 + (j & 3) + 4] = bdataT[boff + j];
            }
        } else { // use float dot products
            // Factor mean removal into weights, and remove global
            // offset from softmax neurons.
            for (j = 0; j < nnst * 2; j++) {
                for (k = 0; k < asize; k++) {
                    const double q = j < nnst ? mean[k] : 0.0;
                    s->weights1[i][j * asize + k] = (float)(bdataT[j * asize + k] - mean[asize + 1 + j] - q);
                }
                s->weights1[i][boff + j] = (float)(bdataT[boff + j] - (j < nnst ? mean[asize] : 0.0));
            }
        }
        av_free(mean);
    }

    s->nns = nns_table[s->nnsparam];
    s->xdia = xdia_table[s->nsize];
    s->ydia = ydia_table[s->nsize];
    s->asize = xdia_table[s->nsize] * ydia_table[s->nsize];

    s->max_value = 65535 >> 8;

    select_functions(s);

    s->fdsp = avpriv_float_dsp_alloc(0);
    if (!s->fdsp)
        ret = AVERROR(ENOMEM);

fail:
    av_free(bdata);
    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    NNEDIContext *s = ctx->priv;
    int i;

    av_freep(&s->weights0);

    for (i = 0; i < 2; i++)
        av_freep(&s->weights1[i]);

    for (i = 0; i < s->nb_planes; i++) {
        av_freep(&s->frame_data.paddedp[i]);
        av_freep(&s->frame_data.lcount[i]);
    }

    av_freep(&s->frame_data.input);
    av_freep(&s->frame_data.temp);
    av_freep(&s->fdsp);
    av_frame_free(&s->second);
}

static const AVFilterPad inputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .filter_frame  = filter_frame,
        .config_props  = config_input,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter ff_vf_nnedi = {
    .name          = "nnedi",
    .description   = NULL_IF_CONFIG_SMALL("Apply neural network edge directed interpolation intra-only deinterlacer."),
    .priv_size     = sizeof(NNEDIContext),
    .priv_class    = &nnedi_class,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = inputs,
    .outputs       = outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
};
