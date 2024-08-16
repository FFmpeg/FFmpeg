/*
 * Copyright (c) 2003 LeFunGus, lefungus@altern.org
 *
 * This file is part of FFmpeg
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

#include "libavutil/imgutils.h"
#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"

#include "avfilter.h"
#include "filters.h"
#include "video.h"

typedef struct VagueDenoiserContext {
    const AVClass *class;

    float threshold;
    float percent;
    int method;
    int type;
    int nsteps;
    int planes;

    int depth;
    int bpc;
    int peak;
    int nb_planes;
    int planeheight[4];
    int planewidth[4];

    float *block;
    float *in;
    float *out;
    float *tmp;

    int hlowsize[4][32];
    int hhighsize[4][32];
    int vlowsize[4][32];
    int vhighsize[4][32];

    void (*thresholding)(float *block, const int width, const int height,
                         const int stride, const float threshold,
                         const float percent);
} VagueDenoiserContext;

#define OFFSET(x) offsetof(VagueDenoiserContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM
static const AVOption vaguedenoiser_options[] = {
    { "threshold", "set filtering strength",   OFFSET(threshold), AV_OPT_TYPE_FLOAT, {.dbl=2.},  0,DBL_MAX, FLAGS },
    { "method",    "set filtering method",     OFFSET(method),    AV_OPT_TYPE_INT,   {.i64=2 },  0, 2,      FLAGS, .unit = "method" },
        { "hard",   "hard thresholding",       0,                 AV_OPT_TYPE_CONST, {.i64=0},   0, 0,      FLAGS, .unit = "method" },
        { "soft",   "soft thresholding",       0,                 AV_OPT_TYPE_CONST, {.i64=1},   0, 0,      FLAGS, .unit = "method" },
        { "garrote", "garrote thresholding",   0,                 AV_OPT_TYPE_CONST, {.i64=2},   0, 0,      FLAGS, .unit = "method" },
    { "nsteps",    "set number of steps",      OFFSET(nsteps),    AV_OPT_TYPE_INT,   {.i64=6 },  1, 32,     FLAGS },
    { "percent", "set percent of full denoising", OFFSET(percent),AV_OPT_TYPE_FLOAT, {.dbl=85},  0,100,     FLAGS },
    { "planes",    "set planes to filter",     OFFSET(planes),    AV_OPT_TYPE_INT,   {.i64=15 }, 0, 15,     FLAGS },
    { "type",    "set threshold type",     OFFSET(type),          AV_OPT_TYPE_INT,   {.i64=0 },  0, 1,      FLAGS, .unit = "type" },
        { "universal",  "universal (VisuShrink)", 0,              AV_OPT_TYPE_CONST, {.i64=0},   0, 0,      FLAGS, .unit = "type" },
        { "bayes",      "bayes (BayesShrink)",    0,              AV_OPT_TYPE_CONST, {.i64=1},   0, 0,      FLAGS, .unit = "type" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(vaguedenoiser);

#define NPAD 10

static const float analysis_low[9] = {
    0.037828455506995f, -0.023849465019380f, -0.110624404418423f, 0.377402855612654f,
    0.852698679009403f, 0.377402855612654f, -0.110624404418423f, -0.023849465019380f, 0.037828455506995f
};

static const float analysis_high[7] = {
    -0.064538882628938f, 0.040689417609558f, 0.418092273222212f, -0.788485616405664f,
    0.418092273222212f, 0.040689417609558f, -0.064538882628938f
};

static const float synthesis_low[7] = {
    -0.064538882628938f, -0.040689417609558f, 0.418092273222212f, 0.788485616405664f,
    0.418092273222212f, -0.040689417609558f, -0.064538882628938f
};

static const float synthesis_high[9] = {
    -0.037828455506995f, -0.023849465019380f, 0.110624404418423f, 0.377402855612654f,
    -0.852698679009403f, 0.377402855612654f, 0.110624404418423f, -0.023849465019380f, -0.037828455506995f
};

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10,
    AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
    AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P,
    AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_YUVJ411P,
    AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
    AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV440P10,
    AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV420P12,
    AV_PIX_FMT_YUV440P12,
    AV_PIX_FMT_YUV444P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV420P14,
    AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
    AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
    AV_PIX_FMT_YUVA420P,  AV_PIX_FMT_YUVA422P,   AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_YUVA444P9, AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_YUVA444P12, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA422P16,
    AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA420P16,
    AV_PIX_FMT_GBRAP,     AV_PIX_FMT_GBRAP10,    AV_PIX_FMT_GBRAP12,    AV_PIX_FMT_GBRAP16,
    AV_PIX_FMT_NONE
};

static int config_input(AVFilterLink *inlink)
{
    VagueDenoiserContext *s = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int p, i, nsteps_width, nsteps_height, nsteps_max;

    s->depth = desc->comp[0].depth;
    s->bpc = (s->depth + 7) / 8;
    s->nb_planes = desc->nb_components;

    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;
    s->planewidth[1]  = s->planewidth[2]  = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0]  = s->planewidth[3]  = inlink->w;

    s->block = av_malloc_array(inlink->w * inlink->h, sizeof(*s->block));
    s->in    = av_malloc_array(32 + FFMAX(inlink->w, inlink->h), sizeof(*s->in));
    s->out   = av_malloc_array(32 + FFMAX(inlink->w, inlink->h), sizeof(*s->out));
    s->tmp   = av_malloc_array(32 + FFMAX(inlink->w, inlink->h), sizeof(*s->tmp));

    if (!s->block || !s->in || !s->out || !s->tmp)
        return AVERROR(ENOMEM);

    s->threshold *= 1 << (s->depth - 8);
    s->peak = (1 << s->depth) - 1;

    nsteps_width  = ((s->planes & 2 || s->planes & 4) && s->nb_planes > 1) ? s->planewidth[1] : s->planewidth[0];
    nsteps_height = ((s->planes & 2 || s->planes & 4) && s->nb_planes > 1) ? s->planeheight[1] : s->planeheight[0];

    for (nsteps_max = 1; nsteps_max < 15; nsteps_max++) {
        if (pow(2, nsteps_max) >= nsteps_width || pow(2, nsteps_max) >= nsteps_height)
            break;
    }

    s->nsteps = FFMIN(s->nsteps, nsteps_max - 2);

    for (p = 0; p < 4; p++) {
        s->hlowsize[p][0]  = (s->planewidth[p] + 1) >> 1;
        s->hhighsize[p][0] =  s->planewidth[p] >> 1;
        s->vlowsize[p][0]  = (s->planeheight[p] + 1) >> 1;
        s->vhighsize[p][0] =  s->planeheight[p] >> 1;

        for (i = 1; i < s->nsteps; i++) {
            s->hlowsize[p][i]  = (s->hlowsize[p][i - 1] + 1) >> 1;
            s->hhighsize[p][i] =  s->hlowsize[p][i - 1] >> 1;
            s->vlowsize[p][i]  = (s->vlowsize[p][i - 1] + 1) >> 1;
            s->vhighsize[p][i] =  s->vlowsize[p][i - 1] >> 1;
        }
    }

    return 0;
}

static inline void copy(const float *p1, float *p2, const int length)
{
    memcpy(p2, p1, length * sizeof(float));
}

static inline void copyv(const float *p1, const int stride1, float *p2, const int length)
{
    int i;

    for (i = 0; i < length; i++) {
        p2[i] = *p1;
        p1 += stride1;
    }
}

static inline void copyh(const float *p1, float *p2, const int stride2, const int length)
{
    int i;

    for (i = 0; i < length; i++) {
        *p2 = p1[i];
        p2 += stride2;
    }
}

// Do symmetric extension of data using prescribed symmetries
// Original values are in output[npad] through output[npad+size-1]
// New values will be placed in output[0] through output[npad] and in output[npad+size] through output[2*npad+size-1] (note: end values may not be filled in)
// extension at left bdry is ... 3 2 1 0 | 0 1 2 3 ...
// same for right boundary
// if right_ext=1 then ... 3 2 1 0 | 1 2 3
static void symmetric_extension(float *output, const int size, const int left_ext, const int right_ext)
{
    int first = NPAD;
    int last = NPAD - 1 + size;
    const int originalLast = last;
    int i, nextend, idx;

    if (left_ext == 2)
        output[--first] = output[NPAD];
    if (right_ext == 2)
        output[++last] = output[originalLast];

    // extend left end
    nextend = first;
    for (i = 0; i < nextend; i++)
        output[--first] = output[NPAD + 1 + i];

    idx = NPAD + NPAD - 1 + size;

    // extend right end
    nextend = idx - last;
    for (i = 0; i < nextend; i++)
        output[++last] = output[originalLast - 1 - i];
}

static void transform_step(float *input, float *output, const int size, const int low_size, VagueDenoiserContext *s)
{
    int i;

    symmetric_extension(input, size, 1, 1);

    for (i = NPAD; i < NPAD + low_size; i++) {
        const float a = input[2 * i - 14] * analysis_low[0];
        const float b = input[2 * i - 13] * analysis_low[1];
        const float c = input[2 * i - 12] * analysis_low[2];
        const float d = input[2 * i - 11] * analysis_low[3];
        const float e = input[2 * i - 10] * analysis_low[4];
        const float f = input[2 * i -  9] * analysis_low[3];
        const float g = input[2 * i -  8] * analysis_low[2];
        const float h = input[2 * i -  7] * analysis_low[1];
        const float k = input[2 * i -  6] * analysis_low[0];

        output[i] = a + b + c + d + e + f + g + h + k;
    }

    for (i = NPAD; i < NPAD + low_size; i++) {
        const float a = input[2 * i - 12] * analysis_high[0];
        const float b = input[2 * i - 11] * analysis_high[1];
        const float c = input[2 * i - 10] * analysis_high[2];
        const float d = input[2 * i -  9] * analysis_high[3];
        const float e = input[2 * i -  8] * analysis_high[2];
        const float f = input[2 * i -  7] * analysis_high[1];
        const float g = input[2 * i -  6] * analysis_high[0];

        output[i + low_size] = a + b + c + d + e + f + g;
    }
}

static void invert_step(const float *input, float *output, float *temp, const int size, VagueDenoiserContext *s)
{
    const int low_size = (size + 1) >> 1;
    const int high_size = size >> 1;
    int left_ext = 1, right_ext, i;
    int findex;

    memcpy(temp + NPAD, input + NPAD, low_size * sizeof(float));

    right_ext = (size % 2 == 0) ? 2 : 1;
    symmetric_extension(temp, low_size, left_ext, right_ext);

    memset(output, 0, (NPAD + NPAD + size) * sizeof(float));
    findex = (size + 2) >> 1;

    for (i = 9; i < findex + 11; i++) {
        const float a = temp[i] * synthesis_low[0];
        const float b = temp[i] * synthesis_low[1];
        const float c = temp[i] * synthesis_low[2];
        const float d = temp[i] * synthesis_low[3];

        output[2 * i - 13] += a;
        output[2 * i - 12] += b;
        output[2 * i - 11] += c;
        output[2 * i - 10] += d;
        output[2 * i -  9] += c;
        output[2 * i -  8] += b;
        output[2 * i -  7] += a;
    }

    memcpy(temp + NPAD, input + NPAD + low_size, high_size * sizeof(float));

    left_ext = 2;
    right_ext = (size % 2 == 0) ? 1 : 2;
    symmetric_extension(temp, high_size, left_ext, right_ext);

    for (i = 8; i < findex + 11; i++) {
        const float a = temp[i] * synthesis_high[0];
        const float b = temp[i] * synthesis_high[1];
        const float c = temp[i] * synthesis_high[2];
        const float d = temp[i] * synthesis_high[3];
        const float e = temp[i] * synthesis_high[4];

        output[2 * i - 13] += a;
        output[2 * i - 12] += b;
        output[2 * i - 11] += c;
        output[2 * i - 10] += d;
        output[2 * i -  9] += e;
        output[2 * i -  8] += d;
        output[2 * i -  7] += c;
        output[2 * i -  6] += b;
        output[2 * i -  5] += a;
    }
}

static void hard_thresholding(float *block, const int width, const int height,
                              const int stride, const float threshold,
                              const float percent)
{
    const float frac = 1.f - percent * 0.01f;
    int y, x;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            if (FFABS(block[x]) <= threshold)
                block[x] *= frac;
        }
        block += stride;
    }
}

static void soft_thresholding(float *block, const int width, const int height, const int stride,
                              const float threshold, const float percent)
{
    const float frac = 1.f - percent * 0.01f;
    const float shift = threshold * 0.01f * percent;
    int y, x;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            const float temp = FFABS(block[x]);
            if (temp <= threshold)
                block[x] *= frac;
            else
                block[x] = (block[x] < 0.f ? -1.f : (block[x] > 0.f ? 1.f : 0.f)) * (temp - shift);
        }
        block += stride;
    }
}

static void qian_thresholding(float *block, const int width, const int height,
                              const int stride, const float threshold,
                              const float percent)
{
    const float percent01 = percent * 0.01f;
    const float tr2 = threshold * threshold * percent01;
    const float frac = 1.f - percent01;
    int y, x;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            const float temp = FFABS(block[x]);
            if (temp <= threshold) {
                block[x] *= frac;
            } else {
                const float tp2 = temp * temp;
                block[x] *= (tp2 - tr2) / tp2;
            }
        }
        block += stride;
    }
}

static float bayes_threshold(float *block, const int width, const int height,
                              const int stride, const float threshold)
{
    float mean = 0.f;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            mean += block[x] * block[x];
        }
        block += stride;
    }

    mean /= width * height;

    return threshold * threshold / (FFMAX(sqrtf(mean - threshold), FLT_EPSILON));
}

static void filter(VagueDenoiserContext *s, AVFrame *in, AVFrame *out)
{
    int p, y, x, i, j;

    for (p = 0; p < s->nb_planes; p++) {
        const int height = s->planeheight[p];
        const int width = s->planewidth[p];
        const uint8_t *srcp8 = in->data[p];
        const uint16_t *srcp16 = (const uint16_t *)in->data[p];
        uint8_t *dstp8 = out->data[p];
        uint16_t *dstp16 = (uint16_t *)out->data[p];
        float *output = s->block;
        int h_low_size0 = width;
        int v_low_size0 = height;
        int nsteps_transform = s->nsteps;
        int nsteps_invert = s->nsteps;
        const float *input = s->block;

        if (!((1 << p) & s->planes)) {
            av_image_copy_plane(out->data[p], out->linesize[p], in->data[p], in->linesize[p],
                                s->planewidth[p] * s->bpc, s->planeheight[p]);
            continue;
        }

        if (s->depth <= 8) {
            for (y = 0; y < height; y++) {
                for (x = 0; x < width; x++)
                    output[x] = srcp8[x];
                srcp8 += in->linesize[p];
                output += width;
            }
        } else {
            for (y = 0; y < height; y++) {
                for (x = 0; x < width; x++)
                    output[x] = srcp16[x];
                srcp16 += in->linesize[p] / 2;
                output += width;
            }
        }

        while (nsteps_transform--) {
            int low_size = (h_low_size0 + 1) >> 1;
            float *input = s->block;
            for (j = 0; j < v_low_size0; j++) {
                copy(input, s->in + NPAD, h_low_size0);
                transform_step(s->in, s->out, h_low_size0, low_size, s);
                copy(s->out + NPAD, input, h_low_size0);
                input += width;
            }

            low_size = (v_low_size0 + 1) >> 1;
            input = s->block;
            for (j = 0; j < h_low_size0; j++) {
                copyv(input, width, s->in + NPAD, v_low_size0);
                transform_step(s->in, s->out, v_low_size0, low_size, s);
                copyh(s->out + NPAD, input, width, v_low_size0);
                input++;
            }

            h_low_size0 = (h_low_size0 + 1) >> 1;
            v_low_size0 = (v_low_size0 + 1) >> 1;
        }

        if (s->type == 0) {
            s->thresholding(s->block, width, height, width, s->threshold, s->percent);
        } else {
            for (int n = 0; n < s->nsteps; n++) {
                float threshold;
                float *block;

                if (n == s->nsteps - 1) {
                    threshold = bayes_threshold(s->block, s->hlowsize[p][n], s->vlowsize[p][n], width, s->threshold);
                    s->thresholding(s->block, s->hlowsize[p][n], s->vlowsize[p][n], width, threshold, s->percent);
                }
                block = s->block + s->hlowsize[p][n];
                threshold = bayes_threshold(block, s->hhighsize[p][n], s->vlowsize[p][n], width, s->threshold);
                s->thresholding(block, s->hhighsize[p][n], s->vlowsize[p][n], width, threshold, s->percent);
                block = s->block + s->vlowsize[p][n] * width;
                threshold = bayes_threshold(block, s->hlowsize[p][n], s->vhighsize[p][n], width, s->threshold);
                s->thresholding(block, s->hlowsize[p][n], s->vhighsize[p][n], width, threshold, s->percent);
                block = s->block + s->hlowsize[p][n] + s->vlowsize[p][n] * width;
                threshold = bayes_threshold(block, s->hhighsize[p][n], s->vhighsize[p][n], width, s->threshold);
                s->thresholding(block, s->hhighsize[p][n], s->vhighsize[p][n], width, threshold, s->percent);
            }
        }

        while (nsteps_invert--) {
            const int idx = s->vlowsize[p][nsteps_invert]  + s->vhighsize[p][nsteps_invert];
            const int idx2 = s->hlowsize[p][nsteps_invert] + s->hhighsize[p][nsteps_invert];
            float * idx3 = s->block;
            for (i = 0; i < idx2; i++) {
                copyv(idx3, width, s->in + NPAD, idx);
                invert_step(s->in, s->out, s->tmp, idx, s);
                copyh(s->out + NPAD, idx3, width, idx);
                idx3++;
            }

            idx3 = s->block;
            for (i = 0; i < idx; i++) {
                copy(idx3, s->in + NPAD, idx2);
                invert_step(s->in, s->out, s->tmp, idx2, s);
                copy(s->out + NPAD, idx3, idx2);
                idx3 += width;
            }
        }

        if (s->depth <= 8) {
            for (y = 0; y < height; y++) {
                for (x = 0; x < width; x++)
                    dstp8[x] = av_clip_uint8(input[x] + 0.5f);
                input += width;
                dstp8 += out->linesize[p];
            }
        } else {
            for (y = 0; y < height; y++) {
                for (x = 0; x < width; x++)
                    dstp16[x] = av_clip(input[x] + 0.5f, 0, s->peak);
                input += width;
                dstp16 += out->linesize[p] / 2;
            }
        }
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx  = inlink->dst;
    VagueDenoiserContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    int direct = av_frame_is_writable(in);

    if (direct) {
        out = in;
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }

        av_frame_copy_props(out, in);
    }

    filter(s, in, out);

    if (!direct)
        av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static av_cold int init(AVFilterContext *ctx)
{
    VagueDenoiserContext *s = ctx->priv;

    switch (s->method) {
    case 0:
        s->thresholding = hard_thresholding;
        break;
    case 1:
        s->thresholding = soft_thresholding;
        break;
    case 2:
        s->thresholding = qian_thresholding;
        break;
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    VagueDenoiserContext *s = ctx->priv;

    av_freep(&s->block);
    av_freep(&s->in);
    av_freep(&s->out);
    av_freep(&s->tmp);
}

static const AVFilterPad vaguedenoiser_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
};


const AVFilter ff_vf_vaguedenoiser = {
    .name          = "vaguedenoiser",
    .description   = NULL_IF_CONFIG_SMALL("Apply a Wavelet based Denoiser."),
    .priv_size     = sizeof(VagueDenoiserContext),
    .priv_class    = &vaguedenoiser_class,
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(vaguedenoiser_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
