/*
 * Copyright (c) 2012-2013 Oka Motofumi (chikuzen.mo at gmail dot com)
 * Copyright (c) 2015 Paul B Mahol
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

#include "libavutil/avstring.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct ConvolutionContext {
    const AVClass *class;

    char *matrix_str[4];
    float rdiv[4];
    float bias[4];
    float scale;
    float delta;
    int planes;

    int size[4];
    int depth;
    int bpc;
    int bstride;
    uint8_t *buffer;
    uint8_t **bptrs;
    int nb_planes;
    int nb_threads;
    int planewidth[4];
    int planeheight[4];
    int matrix[4][25];
    int matrix_length[4];
    int copy[4];

    int (*filter[4])(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} ConvolutionContext;

#define OFFSET(x) offsetof(ConvolutionContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption convolution_options[] = {
    { "0m", "set matrix for 1st plane", OFFSET(matrix_str[0]), AV_OPT_TYPE_STRING, {.str="0 0 0 0 1 0 0 0 0"}, 0, 0, FLAGS },
    { "1m", "set matrix for 2nd plane", OFFSET(matrix_str[1]), AV_OPT_TYPE_STRING, {.str="0 0 0 0 1 0 0 0 0"}, 0, 0, FLAGS },
    { "2m", "set matrix for 3rd plane", OFFSET(matrix_str[2]), AV_OPT_TYPE_STRING, {.str="0 0 0 0 1 0 0 0 0"}, 0, 0, FLAGS },
    { "3m", "set matrix for 4th plane", OFFSET(matrix_str[3]), AV_OPT_TYPE_STRING, {.str="0 0 0 0 1 0 0 0 0"}, 0, 0, FLAGS },
    { "0rdiv", "set rdiv for 1st plane", OFFSET(rdiv[0]), AV_OPT_TYPE_FLOAT, {.dbl=1.0}, 0.0, INT_MAX, FLAGS},
    { "1rdiv", "set rdiv for 2nd plane", OFFSET(rdiv[1]), AV_OPT_TYPE_FLOAT, {.dbl=1.0}, 0.0, INT_MAX, FLAGS},
    { "2rdiv", "set rdiv for 3rd plane", OFFSET(rdiv[2]), AV_OPT_TYPE_FLOAT, {.dbl=1.0}, 0.0, INT_MAX, FLAGS},
    { "3rdiv", "set rdiv for 4th plane", OFFSET(rdiv[3]), AV_OPT_TYPE_FLOAT, {.dbl=1.0}, 0.0, INT_MAX, FLAGS},
    { "0bias", "set bias for 1st plane", OFFSET(bias[0]), AV_OPT_TYPE_FLOAT, {.dbl=0.0}, 0.0, INT_MAX, FLAGS},
    { "1bias", "set bias for 2nd plane", OFFSET(bias[1]), AV_OPT_TYPE_FLOAT, {.dbl=0.0}, 0.0, INT_MAX, FLAGS},
    { "2bias", "set bias for 3rd plane", OFFSET(bias[2]), AV_OPT_TYPE_FLOAT, {.dbl=0.0}, 0.0, INT_MAX, FLAGS},
    { "3bias", "set bias for 4th plane", OFFSET(bias[3]), AV_OPT_TYPE_FLOAT, {.dbl=0.0}, 0.0, INT_MAX, FLAGS},
    { NULL }
};

AVFILTER_DEFINE_CLASS(convolution);

static const int same3x3[9] = {0, 0, 0,
                               0, 1, 0,
                               0, 0, 0};

static const int same5x5[25] = {0, 0, 0, 0, 0,
                                0, 0, 0, 0, 0,
                                0, 0, 1, 0, 0,
                                0, 0, 0, 0, 0,
                                0, 0, 0, 0, 0};

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
        AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
        AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV440P12,
        AV_PIX_FMT_YUV420P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV444P14,
        AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
        AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA444P9,
        AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA444P10,
        AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA444P16,
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
        AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
        AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRAP16,
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY16,
        AV_PIX_FMT_NONE
    };

    return ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
}

static inline void line_copy8(uint8_t *line, const uint8_t *srcp, int width, int mergin)
{
    int i;

    memcpy(line, srcp, width);

    for (i = mergin; i > 0; i--) {
        line[-i] = line[i];
        line[width - 1 + i] = line[width - 1 - i];
    }
}

static inline void line_copy16(uint16_t *line, const uint16_t *srcp, int width, int mergin)
{
    int i;

    memcpy(line, srcp, width * 2);

    for (i = mergin; i > 0; i--) {
        line[-i] = line[i];
        line[width - 1 + i] = line[width - 1 - i];
    }
}

typedef struct ThreadData {
    AVFrame *in, *out;
    int plane;
} ThreadData;

static int filter16_prewitt(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ConvolutionContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int plane = td->plane;
    const int peak = (1 << s->depth) - 1;
    const int stride = in->linesize[plane] / 2;
    const int bstride = s->bstride;
    const int height = s->planeheight[plane];
    const int width  = s->planewidth[plane];
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr+1)) / nb_jobs;
    const uint16_t *src = (const uint16_t *)in->data[plane] + slice_start * stride;
    uint16_t *dst = (uint16_t *)out->data[plane] + slice_start * (out->linesize[plane] / 2);
    const float scale = s->scale;
    const float delta = s->delta;
    uint16_t *p0 = (uint16_t *)s->bptrs[jobnr] + 16;
    uint16_t *p1 = p0 + bstride;
    uint16_t *p2 = p1 + bstride;
    uint16_t *orig = p0, *end = p2;
    int y, x;

    line_copy16(p0, src + stride * (slice_start == 0 ? 1 : -1), width, 1);
    line_copy16(p1, src, width, 1);

    for (y = slice_start; y < slice_end; y++) {
        src += stride * (y < height - 1 ? 1 : -1);
        line_copy16(p2, src, width, 1);

        for (x = 0; x < width; x++) {
            int suma = p0[x - 1] * -1 +
                       p0[x] *     -1 +
                       p0[x + 1] * -1 +
                       p2[x - 1] *  1 +
                       p2[x] *      1 +
                       p2[x + 1] *  1;
            int sumb = p0[x - 1] * -1 +
                       p0[x + 1] *  1 +
                       p1[x - 1] * -1 +
                       p1[x + 1] *  1 +
                       p2[x - 1] * -1 +
                       p2[x + 1] *  1;

            dst[x] = av_clip(sqrt(suma*suma + sumb*sumb) * scale + delta, 0, peak);
        }

        p0 = p1;
        p1 = p2;
        p2 = (p2 == end) ? orig: p2 + bstride;
        dst += out->linesize[plane] / 2;
    }

    return 0;
}

static int filter16_roberts(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ConvolutionContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int plane = td->plane;
    const int peak = (1 << s->depth) - 1;
    const int stride = in->linesize[plane] / 2;
    const int bstride = s->bstride;
    const int height = s->planeheight[plane];
    const int width  = s->planewidth[plane];
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr+1)) / nb_jobs;
    const uint16_t *src = (const uint16_t *)in->data[plane] + slice_start * stride;
    uint16_t *dst = (uint16_t *)out->data[plane] + slice_start * (out->linesize[plane] / 2);
    const float scale = s->scale;
    const float delta = s->delta;
    uint16_t *p0 = (uint16_t *)s->bptrs[jobnr] + 16;
    uint16_t *p1 = p0 + bstride;
    uint16_t *p2 = p1 + bstride;
    uint16_t *orig = p0, *end = p2;
    int y, x;

    line_copy16(p0, src + stride * (slice_start == 0 ? 1 : -1), width, 1);
    line_copy16(p1, src, width, 1);

    for (y = slice_start; y < slice_end; y++) {
        src += stride * (y < height - 1 ? 1 : -1);
        line_copy16(p2, src, width, 1);

        for (x = 0; x < width; x++) {
            int suma = p0[x - 1] *  1 +
                       p1[x    ] * -1;
            int sumb = p0[x    ] *  1 +
                       p1[x - 1] * -1;

            dst[x] = av_clip(sqrt(suma*suma + sumb*sumb) * scale + delta, 0, peak);
        }

        p0 = p1;
        p1 = p2;
        p2 = (p2 == end) ? orig: p2 + bstride;
        dst += out->linesize[plane] / 2;
    }

    return 0;
}

static int filter16_sobel(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ConvolutionContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int plane = td->plane;
    const int peak = (1 << s->depth) - 1;
    const int stride = in->linesize[plane] / 2;
    const int bstride = s->bstride;
    const int height = s->planeheight[plane];
    const int width  = s->planewidth[plane];
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr+1)) / nb_jobs;
    const uint16_t *src = (const uint16_t *)in->data[plane] + slice_start * stride;
    uint16_t *dst = (uint16_t *)out->data[plane] + slice_start * (out->linesize[plane] / 2);
    const float scale = s->scale;
    const float delta = s->delta;
    uint16_t *p0 = (uint16_t *)s->bptrs[jobnr] + 16;
    uint16_t *p1 = p0 + bstride;
    uint16_t *p2 = p1 + bstride;
    uint16_t *orig = p0, *end = p2;
    int y, x;

    line_copy16(p0, src + stride * (slice_start == 0 ? 1 : -1), width, 1);
    line_copy16(p1, src, width, 1);

    for (y = slice_start; y < slice_end; y++) {
        src += stride * (y < height - 1 ? 1 : -1);
        line_copy16(p2, src, width, 1);

        for (x = 0; x < width; x++) {
            int suma = p0[x - 1] * -1 +
                       p0[x] *     -2 +
                       p0[x + 1] * -1 +
                       p2[x - 1] *  1 +
                       p2[x] *      2 +
                       p2[x + 1] *  1;
            int sumb = p0[x - 1] * -1 +
                       p0[x + 1] *  1 +
                       p1[x - 1] * -2 +
                       p1[x + 1] *  2 +
                       p2[x - 1] * -1 +
                       p2[x + 1] *  1;

            dst[x] = av_clip(sqrt(suma*suma + sumb*sumb) * scale + delta, 0, peak);
        }

        p0 = p1;
        p1 = p2;
        p2 = (p2 == end) ? orig: p2 + bstride;
        dst += out->linesize[plane] / 2;
    }

    return 0;
}

static int filter_prewitt(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ConvolutionContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int plane = td->plane;
    const int stride = in->linesize[plane];
    const int bstride = s->bstride;
    const int height = s->planeheight[plane];
    const int width  = s->planewidth[plane];
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr+1)) / nb_jobs;
    const uint8_t *src = in->data[plane] + slice_start * stride;
    uint8_t *dst = out->data[plane] + slice_start * out->linesize[plane];
    const float scale = s->scale;
    const float delta = s->delta;
    uint8_t *p0 = s->bptrs[jobnr] + 16;
    uint8_t *p1 = p0 + bstride;
    uint8_t *p2 = p1 + bstride;
    uint8_t *orig = p0, *end = p2;
    int y, x;

    line_copy8(p0, src + stride * (slice_start == 0 ? 1 : -1), width, 1);
    line_copy8(p1, src, width, 1);

    for (y = slice_start; y < slice_end; y++) {
        src += stride * (y < height - 1 ? 1 : -1);
        line_copy8(p2, src, width, 1);

        for (x = 0; x < width; x++) {
            int suma = p0[x - 1] * -1 +
                       p0[x] *     -1 +
                       p0[x + 1] * -1 +
                       p2[x - 1] *  1 +
                       p2[x] *      1 +
                       p2[x + 1] *  1;
            int sumb = p0[x - 1] * -1 +
                       p0[x + 1] *  1 +
                       p1[x - 1] * -1 +
                       p1[x + 1] *  1 +
                       p2[x - 1] * -1 +
                       p2[x + 1] *  1;

            dst[x] = av_clip_uint8(sqrt(suma*suma + sumb*sumb) * scale + delta);
        }

        p0 = p1;
        p1 = p2;
        p2 = (p2 == end) ? orig: p2 + bstride;
        dst += out->linesize[plane];
    }

    return 0;
}

static int filter_roberts(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ConvolutionContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int plane = td->plane;
    const int stride = in->linesize[plane];
    const int bstride = s->bstride;
    const int height = s->planeheight[plane];
    const int width  = s->planewidth[plane];
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr+1)) / nb_jobs;
    const uint8_t *src = in->data[plane] + slice_start * stride;
    uint8_t *dst = out->data[plane] + slice_start * out->linesize[plane];
    const float scale = s->scale;
    const float delta = s->delta;
    uint8_t *p0 = s->bptrs[jobnr] + 16;
    uint8_t *p1 = p0 + bstride;
    uint8_t *p2 = p1 + bstride;
    uint8_t *orig = p0, *end = p2;
    int y, x;

    line_copy8(p0, src + stride * (slice_start == 0 ? 1 : -1), width, 1);
    line_copy8(p1, src, width, 1);

    for (y = slice_start; y < slice_end; y++) {
        src += stride * (y < height - 1 ? 1 : -1);
        line_copy8(p2, src, width, 1);

        for (x = 0; x < width; x++) {
            int suma = p0[x - 1] *  1 +
                       p1[x    ] * -1;
            int sumb = p0[x    ] *  1 +
                       p1[x - 1] * -1;

            dst[x] = av_clip_uint8(sqrt(suma*suma + sumb*sumb) * scale + delta);
        }

        p0 = p1;
        p1 = p2;
        p2 = (p2 == end) ? orig: p2 + bstride;
        dst += out->linesize[plane];
    }

    return 0;
}

static int filter_sobel(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ConvolutionContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int plane = td->plane;
    const int stride = in->linesize[plane];
    const int bstride = s->bstride;
    const int height = s->planeheight[plane];
    const int width  = s->planewidth[plane];
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr+1)) / nb_jobs;
    const uint8_t *src = in->data[plane] + slice_start * stride;
    uint8_t *dst = out->data[plane] + slice_start * out->linesize[plane];
    const float scale = s->scale;
    const float delta = s->delta;
    uint8_t *p0 = s->bptrs[jobnr] + 16;
    uint8_t *p1 = p0 + bstride;
    uint8_t *p2 = p1 + bstride;
    uint8_t *orig = p0, *end = p2;
    int y, x;

    line_copy8(p0, src + stride * (slice_start == 0 ? 1 : -1), width, 1);
    line_copy8(p1, src, width, 1);

    for (y = slice_start; y < slice_end; y++) {
        src += stride * (y < height - 1 ? 1 : -1);
        line_copy8(p2, src, width, 1);

        for (x = 0; x < width; x++) {
            int suma = p0[x - 1] * -1 +
                       p0[x] *     -2 +
                       p0[x + 1] * -1 +
                       p2[x - 1] *  1 +
                       p2[x] *      2 +
                       p2[x + 1] *  1;
            int sumb = p0[x - 1] * -1 +
                       p0[x + 1] *  1 +
                       p1[x - 1] * -2 +
                       p1[x + 1] *  2 +
                       p2[x - 1] * -1 +
                       p2[x + 1] *  1;

            dst[x] = av_clip_uint8(sqrt(suma*suma + sumb*sumb) * scale + delta);
        }

        p0 = p1;
        p1 = p2;
        p2 = (p2 == end) ? orig: p2 + bstride;
        dst += out->linesize[plane];
    }

    return 0;
}

static int filter16_3x3(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ConvolutionContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int plane = td->plane;
    const int peak = (1 << s->depth) - 1;
    const int stride = in->linesize[plane] / 2;
    const int bstride = s->bstride;
    const int height = s->planeheight[plane];
    const int width  = s->planewidth[plane];
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr+1)) / nb_jobs;
    const uint16_t *src = (const uint16_t *)in->data[plane] + slice_start * stride;
    uint16_t *dst = (uint16_t *)out->data[plane] + slice_start * (out->linesize[plane] / 2);
    uint16_t *p0 = (uint16_t *)s->bptrs[jobnr] + 16;
    uint16_t *p1 = p0 + bstride;
    uint16_t *p2 = p1 + bstride;
    uint16_t *orig = p0, *end = p2;
    const int *matrix = s->matrix[plane];
    const float rdiv = s->rdiv[plane];
    const float bias = s->bias[plane];
    int y, x;

    line_copy16(p0, src + stride * (slice_start == 0 ? 1 : -1), width, 1);
    line_copy16(p1, src, width, 1);

    for (y = slice_start; y < slice_end; y++) {
        src += stride * (y < height - 1 ? 1 : -1);
        line_copy16(p2, src, width, 1);

        for (x = 0; x < width; x++) {
            int sum = p0[x - 1] * matrix[0] +
                      p0[x] *     matrix[1] +
                      p0[x + 1] * matrix[2] +
                      p1[x - 1] * matrix[3] +
                      p1[x] *     matrix[4] +
                      p1[x + 1] * matrix[5] +
                      p2[x - 1] * matrix[6] +
                      p2[x] *     matrix[7] +
                      p2[x + 1] * matrix[8];
            sum = (int)(sum * rdiv + bias + 0.5f);
            dst[x] = av_clip(sum, 0, peak);
        }

        p0 = p1;
        p1 = p2;
        p2 = (p2 == end) ? orig: p2 + bstride;
        dst += out->linesize[plane] / 2;
    }

    return 0;
}

static int filter16_5x5(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ConvolutionContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int plane = td->plane;
    const int peak = (1 << s->depth) - 1;
    const int stride = in->linesize[plane] / 2;
    const int bstride = s->bstride;
    const int height = s->planeheight[plane];
    const int width  = s->planewidth[plane];
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr+1)) / nb_jobs;
    const uint16_t *src = (const uint16_t *)in->data[plane] + slice_start * stride;
    uint16_t *dst = (uint16_t *)out->data[plane] + slice_start * (out->linesize[plane] / 2);
    uint16_t *p0 = (uint16_t *)s->bptrs[jobnr] + 16;
    uint16_t *p1 = p0 + bstride;
    uint16_t *p2 = p1 + bstride;
    uint16_t *p3 = p2 + bstride;
    uint16_t *p4 = p3 + bstride;
    uint16_t *orig = p0, *end = p4;
    const int *matrix = s->matrix[plane];
    float rdiv = s->rdiv[plane];
    float bias = s->bias[plane];
    int y, x, i;

    line_copy16(p0, src + 2 * stride * (slice_start < 2 ? 1 : -1), width, 2);
    line_copy16(p1, src + stride * (slice_start == 0 ? 1 : -1), width, 2);
    line_copy16(p2, src, width, 2);
    src += stride;
    line_copy16(p3, src, width, 2);

    for (y = slice_start; y < slice_end; y++) {
        uint16_t *array[] = {
            p0 - 2, p0 - 1, p0, p0 + 1, p0 + 2,
            p1 - 2, p1 - 1, p1, p1 + 1, p1 + 2,
            p2 - 2, p2 - 1, p2, p2 + 1, p2 + 2,
            p3 - 2, p3 - 1, p3, p3 + 1, p3 + 2,
            p4 - 2, p4 - 1, p4, p4 + 1, p4 + 2
        };

        src += stride * (y < height - 2 ? 1 : -1);
        line_copy16(p4, src, width, 2);

        for (x = 0; x < width; x++) {
            int sum = 0;

            for (i = 0; i < 25; i++) {
                sum += *(array[i] + x) * matrix[i];
            }
            sum = (int)(sum * rdiv + bias + 0.5f);
            dst[x] = av_clip(sum, 0, peak);
        }

        p0 = p1;
        p1 = p2;
        p2 = p3;
        p3 = p4;
        p4 = (p4 == end) ? orig: p4 + bstride;
        dst += out->linesize[plane] / 2;
    }

    return 0;
}

static int filter_3x3(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ConvolutionContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int plane = td->plane;
    const int stride = in->linesize[plane];
    const int bstride = s->bstride;
    const int height = s->planeheight[plane];
    const int width  = s->planewidth[plane];
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr+1)) / nb_jobs;
    const uint8_t *src = in->data[plane] + slice_start * stride;
    uint8_t *dst = out->data[plane] + slice_start * out->linesize[plane];
    uint8_t *p0 = s->bptrs[jobnr] + 16;
    uint8_t *p1 = p0 + bstride;
    uint8_t *p2 = p1 + bstride;
    uint8_t *orig = p0, *end = p2;
    const int *matrix = s->matrix[plane];
    const float rdiv = s->rdiv[plane];
    const float bias = s->bias[plane];
    int y, x;

    line_copy8(p0, src + stride * (slice_start == 0 ? 1 : -1), width, 1);
    line_copy8(p1, src, width, 1);

    for (y = slice_start; y < slice_end; y++) {
        src += stride * (y < height - 1 ? 1 : -1);
        line_copy8(p2, src, width, 1);

        for (x = 0; x < width; x++) {
            int sum = p0[x - 1] * matrix[0] +
                      p0[x] *     matrix[1] +
                      p0[x + 1] * matrix[2] +
                      p1[x - 1] * matrix[3] +
                      p1[x] *     matrix[4] +
                      p1[x + 1] * matrix[5] +
                      p2[x - 1] * matrix[6] +
                      p2[x] *     matrix[7] +
                      p2[x + 1] * matrix[8];
            sum = (int)(sum * rdiv + bias + 0.5f);
            dst[x] = av_clip_uint8(sum);
        }

        p0 = p1;
        p1 = p2;
        p2 = (p2 == end) ? orig: p2 + bstride;
        dst += out->linesize[plane];
    }

    return 0;
}

static int filter_5x5(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ConvolutionContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int plane = td->plane;
    const int stride = in->linesize[plane];
    const int bstride = s->bstride;
    const int height = s->planeheight[plane];
    const int width  = s->planewidth[plane];
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr+1)) / nb_jobs;
    const uint8_t *src = in->data[plane] + slice_start * stride;
    uint8_t *dst = out->data[plane] + slice_start * out->linesize[plane];
    uint8_t *p0 = s->bptrs[jobnr] + 16;
    uint8_t *p1 = p0 + bstride;
    uint8_t *p2 = p1 + bstride;
    uint8_t *p3 = p2 + bstride;
    uint8_t *p4 = p3 + bstride;
    uint8_t *orig = p0, *end = p4;
    const int *matrix = s->matrix[plane];
    float rdiv = s->rdiv[plane];
    float bias = s->bias[plane];
    int y, x, i;

    line_copy8(p0, src + 2 * stride * (slice_start < 2 ? 1 : -1), width, 2);
    line_copy8(p1, src + stride * (slice_start == 0 ? 1 : -1), width, 2);
    line_copy8(p2, src, width, 2);
    src += stride;
    line_copy8(p3, src, width, 2);


    for (y = slice_start; y < slice_end; y++) {
        uint8_t *array[] = {
            p0 - 2, p0 - 1, p0, p0 + 1, p0 + 2,
            p1 - 2, p1 - 1, p1, p1 + 1, p1 + 2,
            p2 - 2, p2 - 1, p2, p2 + 1, p2 + 2,
            p3 - 2, p3 - 1, p3, p3 + 1, p3 + 2,
            p4 - 2, p4 - 1, p4, p4 + 1, p4 + 2
        };

        src += stride * (y < height - 2 ? 1 : -1);
        line_copy8(p4, src, width, 2);

        for (x = 0; x < width; x++) {
            int sum = 0;

            for (i = 0; i < 25; i++) {
                sum += *(array[i] + x) * matrix[i];
            }
            sum = (int)(sum * rdiv + bias + 0.5f);
            dst[x] = av_clip_uint8(sum);
        }

        p0 = p1;
        p1 = p2;
        p2 = p3;
        p3 = p4;
        p4 = (p4 == end) ? orig: p4 + bstride;
        dst += out->linesize[plane];
    }

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ConvolutionContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int p;

    s->depth = desc->comp[0].depth;

    s->planewidth[1] = s->planewidth[2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0] = s->planewidth[3] = inlink->w;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);
    s->nb_threads = ff_filter_get_nb_threads(ctx);
    s->bptrs = av_calloc(s->nb_threads, sizeof(*s->bptrs));
    if (!s->bptrs)
        return AVERROR(ENOMEM);

    s->bstride = s->planewidth[0] + 32;
    s->bpc = (s->depth + 7) / 8;
    s->buffer = av_malloc_array(5 * s->bstride * s->nb_threads, s->bpc);
    if (!s->buffer)
        return AVERROR(ENOMEM);

    for (p = 0; p < s->nb_threads; p++) {
        s->bptrs[p] = s->buffer + 5 * s->bstride * s->bpc * p;
    }

    if (!strcmp(ctx->filter->name, "convolution")) {
        if (s->depth > 8) {
            for (p = 0; p < s->nb_planes; p++) {
                if (s->size[p] == 3)
                    s->filter[p] = filter16_3x3;
                else if (s->size[p] == 5)
                    s->filter[p] = filter16_5x5;
            }
        }
    } else if (!strcmp(ctx->filter->name, "prewitt")) {
        if (s->depth > 8)
            for (p = 0; p < s->nb_planes; p++)
                s->filter[p] = filter16_prewitt;
    } else if (!strcmp(ctx->filter->name, "roberts")) {
        if (s->depth > 8)
            for (p = 0; p < s->nb_planes; p++)
                s->filter[p] = filter16_roberts;
    } else if (!strcmp(ctx->filter->name, "sobel")) {
        if (s->depth > 8)
            for (p = 0; p < s->nb_planes; p++)
                s->filter[p] = filter16_sobel;
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    ConvolutionContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    int plane;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    for (plane = 0; plane < s->nb_planes; plane++) {
        ThreadData td;

        if (s->copy[plane]) {
            av_image_copy_plane(out->data[plane], out->linesize[plane],
                                in->data[plane], in->linesize[plane],
                                s->planewidth[plane] * s->bpc,
                                s->planeheight[plane]);
            continue;
        }

        td.in = in;
        td.out = out;
        td.plane = plane;
        ctx->internal->execute(ctx, s->filter[plane], &td, NULL, FFMIN(s->planeheight[plane], s->nb_threads));
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold int init(AVFilterContext *ctx)
{
    ConvolutionContext *s = ctx->priv;
    int i;

    if (!strcmp(ctx->filter->name, "convolution")) {
        for (i = 0; i < 4; i++) {
            int *matrix = (int *)s->matrix[i];
            char *p, *arg, *saveptr = NULL;

            p = s->matrix_str[i];
            while (s->matrix_length[i] < 25) {
                if (!(arg = av_strtok(p, " ", &saveptr)))
                    break;

                p = NULL;
                sscanf(arg, "%d", &matrix[s->matrix_length[i]]);
                s->matrix_length[i]++;
            }

            if (s->matrix_length[i] == 9) {
                s->size[i] = 3;
                if (!memcmp(matrix, same3x3, sizeof(same3x3)))
                    s->copy[i] = 1;
                else
                    s->filter[i] = filter_3x3;
            } else if (s->matrix_length[i] == 25) {
                s->size[i] = 5;
                if (!memcmp(matrix, same5x5, sizeof(same5x5)))
                    s->copy[i] = 1;
                else
                    s->filter[i] = filter_5x5;
            } else {
                return AVERROR(EINVAL);
            }
        }
    } else if (!strcmp(ctx->filter->name, "prewitt")) {
        for (i = 0; i < 4; i++) {
            if ((1 << i) & s->planes)
                s->filter[i] = filter_prewitt;
            else
                s->copy[i] = 1;
        }
    } else if (!strcmp(ctx->filter->name, "roberts")) {
        for (i = 0; i < 4; i++) {
            if ((1 << i) & s->planes)
                s->filter[i] = filter_roberts;
            else
                s->copy[i] = 1;
        }
    } else if (!strcmp(ctx->filter->name, "sobel")) {
        for (i = 0; i < 4; i++) {
            if ((1 << i) & s->planes)
                s->filter[i] = filter_sobel;
            else
                s->copy[i] = 1;
        }
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ConvolutionContext *s = ctx->priv;

    av_freep(&s->bptrs);
    av_freep(&s->buffer);
}

static const AVFilterPad convolution_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad convolution_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

#if CONFIG_CONVOLUTION_FILTER

AVFilter ff_vf_convolution = {
    .name          = "convolution",
    .description   = NULL_IF_CONFIG_SMALL("Apply convolution filter."),
    .priv_size     = sizeof(ConvolutionContext),
    .priv_class    = &convolution_class,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = convolution_inputs,
    .outputs       = convolution_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};

#endif /* CONFIG_CONVOLUTION_FILTER */

#if CONFIG_PREWITT_FILTER

static const AVOption prewitt_options[] = {
    { "planes", "set planes to filter", OFFSET(planes), AV_OPT_TYPE_INT,  {.i64=15}, 0, 15, FLAGS},
    { "scale",  "set scale",            OFFSET(scale), AV_OPT_TYPE_FLOAT, {.dbl=1.0}, 0.0,  65535, FLAGS},
    { "delta",  "set delta",            OFFSET(delta), AV_OPT_TYPE_FLOAT, {.dbl=0}, -65535, 65535, FLAGS},
    { NULL }
};

AVFILTER_DEFINE_CLASS(prewitt);

AVFilter ff_vf_prewitt = {
    .name          = "prewitt",
    .description   = NULL_IF_CONFIG_SMALL("Apply prewitt operator."),
    .priv_size     = sizeof(ConvolutionContext),
    .priv_class    = &prewitt_class,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = convolution_inputs,
    .outputs       = convolution_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};

#endif /* CONFIG_PREWITT_FILTER */

#if CONFIG_SOBEL_FILTER

static const AVOption sobel_options[] = {
    { "planes", "set planes to filter", OFFSET(planes), AV_OPT_TYPE_INT,  {.i64=15}, 0, 15, FLAGS},
    { "scale",  "set scale",            OFFSET(scale), AV_OPT_TYPE_FLOAT, {.dbl=1.0}, 0.0,  65535, FLAGS},
    { "delta",  "set delta",            OFFSET(delta), AV_OPT_TYPE_FLOAT, {.dbl=0}, -65535, 65535, FLAGS},
    { NULL }
};

AVFILTER_DEFINE_CLASS(sobel);

AVFilter ff_vf_sobel = {
    .name          = "sobel",
    .description   = NULL_IF_CONFIG_SMALL("Apply sobel operator."),
    .priv_size     = sizeof(ConvolutionContext),
    .priv_class    = &sobel_class,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = convolution_inputs,
    .outputs       = convolution_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};

#endif /* CONFIG_SOBEL_FILTER */

#if CONFIG_ROBERTS_FILTER

static const AVOption roberts_options[] = {
    { "planes", "set planes to filter", OFFSET(planes), AV_OPT_TYPE_INT,  {.i64=15}, 0, 15, FLAGS},
    { "scale",  "set scale",            OFFSET(scale), AV_OPT_TYPE_FLOAT, {.dbl=1.0}, 0.0,  65535, FLAGS},
    { "delta",  "set delta",            OFFSET(delta), AV_OPT_TYPE_FLOAT, {.dbl=0}, -65535, 65535, FLAGS},
    { NULL }
};

AVFILTER_DEFINE_CLASS(roberts);

AVFilter ff_vf_roberts = {
    .name          = "roberts",
    .description   = NULL_IF_CONFIG_SMALL("Apply roberts cross operator."),
    .priv_size     = sizeof(ConvolutionContext),
    .priv_class    = &roberts_class,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = convolution_inputs,
    .outputs       = convolution_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};

#endif /* CONFIG_ROBERTS_FILTER */
