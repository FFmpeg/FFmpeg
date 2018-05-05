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
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

enum MatrixMode {
    MATRIX_SQUARE,
    MATRIX_ROW,
    MATRIX_COLUMN,
    MATRIX_NBMODES,
};

typedef struct ConvolutionContext {
    const AVClass *class;

    char *matrix_str[4];
    float rdiv[4];
    float bias[4];
    int mode[4];
    float scale;
    float delta;
    int planes;

    int size[4];
    int depth;
    int max;
    int bpc;
    int nb_planes;
    int nb_threads;
    int planewidth[4];
    int planeheight[4];
    int matrix[4][49];
    int matrix_length[4];
    int copy[4];

    void (*setup[4])(int radius, const uint8_t *c[], const uint8_t *src, int stride,
                     int x, int width, int y, int height, int bpc);
    void (*filter[4])(uint8_t *dst, int width,
                      float rdiv, float bias, const int *const matrix,
                      const uint8_t *c[], int peak, int radius,
                      int dstride, int stride);
} ConvolutionContext;

#define OFFSET(x) offsetof(ConvolutionContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption convolution_options[] = {
    { "0m", "set matrix for 1st plane", OFFSET(matrix_str[0]), AV_OPT_TYPE_STRING, {.str="0 0 0 0 1 0 0 0 0"}, 0, 0, FLAGS },
    { "1m", "set matrix for 2nd plane", OFFSET(matrix_str[1]), AV_OPT_TYPE_STRING, {.str="0 0 0 0 1 0 0 0 0"}, 0, 0, FLAGS },
    { "2m", "set matrix for 3rd plane", OFFSET(matrix_str[2]), AV_OPT_TYPE_STRING, {.str="0 0 0 0 1 0 0 0 0"}, 0, 0, FLAGS },
    { "3m", "set matrix for 4th plane", OFFSET(matrix_str[3]), AV_OPT_TYPE_STRING, {.str="0 0 0 0 1 0 0 0 0"}, 0, 0, FLAGS },
    { "0rdiv", "set rdiv for 1st plane", OFFSET(rdiv[0]), AV_OPT_TYPE_FLOAT, {.dbl=0.0}, 0.0, INT_MAX, FLAGS},
    { "1rdiv", "set rdiv for 2nd plane", OFFSET(rdiv[1]), AV_OPT_TYPE_FLOAT, {.dbl=0.0}, 0.0, INT_MAX, FLAGS},
    { "2rdiv", "set rdiv for 3rd plane", OFFSET(rdiv[2]), AV_OPT_TYPE_FLOAT, {.dbl=0.0}, 0.0, INT_MAX, FLAGS},
    { "3rdiv", "set rdiv for 4th plane", OFFSET(rdiv[3]), AV_OPT_TYPE_FLOAT, {.dbl=0.0}, 0.0, INT_MAX, FLAGS},
    { "0bias", "set bias for 1st plane", OFFSET(bias[0]), AV_OPT_TYPE_FLOAT, {.dbl=0.0}, 0.0, INT_MAX, FLAGS},
    { "1bias", "set bias for 2nd plane", OFFSET(bias[1]), AV_OPT_TYPE_FLOAT, {.dbl=0.0}, 0.0, INT_MAX, FLAGS},
    { "2bias", "set bias for 3rd plane", OFFSET(bias[2]), AV_OPT_TYPE_FLOAT, {.dbl=0.0}, 0.0, INT_MAX, FLAGS},
    { "3bias", "set bias for 4th plane", OFFSET(bias[3]), AV_OPT_TYPE_FLOAT, {.dbl=0.0}, 0.0, INT_MAX, FLAGS},
    { "0mode", "set matrix mode for 1st plane", OFFSET(mode[0]), AV_OPT_TYPE_INT, {.i64=MATRIX_SQUARE}, 0, MATRIX_NBMODES-1, FLAGS, "mode" },
    { "1mode", "set matrix mode for 2nd plane", OFFSET(mode[1]), AV_OPT_TYPE_INT, {.i64=MATRIX_SQUARE}, 0, MATRIX_NBMODES-1, FLAGS, "mode" },
    { "2mode", "set matrix mode for 3rd plane", OFFSET(mode[2]), AV_OPT_TYPE_INT, {.i64=MATRIX_SQUARE}, 0, MATRIX_NBMODES-1, FLAGS, "mode" },
    { "3mode", "set matrix mode for 4th plane", OFFSET(mode[3]), AV_OPT_TYPE_INT, {.i64=MATRIX_SQUARE}, 0, MATRIX_NBMODES-1, FLAGS, "mode" },
    { "square", "square matrix",     0, AV_OPT_TYPE_CONST, {.i64=MATRIX_SQUARE}, 0, 0, FLAGS, "mode" },
    { "row",    "single row matrix", 0, AV_OPT_TYPE_CONST, {.i64=MATRIX_ROW}   , 0, 0, FLAGS, "mode" },
    { "column", "single column matrix", 0, AV_OPT_TYPE_CONST, {.i64=MATRIX_COLUMN}, 0, 0, FLAGS, "mode" },
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

static const int same7x7[49] = {0, 0, 0, 0, 0, 0, 0,
                                0, 0, 0, 0, 0, 0, 0,
                                0, 0, 0, 0, 0, 0, 0,
                                0, 0, 0, 1, 0, 0, 0,
                                0, 0, 0, 0, 0, 0, 0,
                                0, 0, 0, 0, 0, 0, 0,
                                0, 0, 0, 0, 0, 0, 0};

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

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

static void filter16_prewitt(uint8_t *dstp, int width,
                             float scale, float delta, const int *const matrix,
                             const uint8_t *c[], int peak, int radius,
                             int dstride, int stride)
{
    uint16_t *dst = (uint16_t *)dstp;
    int x;

    for (x = 0; x < width; x++) {
        int suma = AV_RN16A(&c[0][2 * x]) * -1 + AV_RN16A(&c[1][2 * x]) * -1 + AV_RN16A(&c[2][2 * x]) * -1 +
                   AV_RN16A(&c[6][2 * x]) *  1 + AV_RN16A(&c[7][2 * x]) *  1 + AV_RN16A(&c[8][2 * x]) *  1;
        int sumb = AV_RN16A(&c[0][2 * x]) * -1 + AV_RN16A(&c[2][2 * x]) *  1 + AV_RN16A(&c[3][2 * x]) * -1 +
                   AV_RN16A(&c[5][2 * x]) *  1 + AV_RN16A(&c[6][2 * x]) * -1 + AV_RN16A(&c[8][2 * x]) *  1;

        dst[x] = av_clip(sqrt(suma*suma + sumb*sumb) * scale + delta, 0, peak);
    }
}

static void filter16_roberts(uint8_t *dstp, int width,
                             float scale, float delta, const int *const matrix,
                             const uint8_t *c[], int peak, int radius,
                             int dstride, int stride)
{
    uint16_t *dst = (uint16_t *)dstp;
    int x;

    for (x = 0; x < width; x++) {
        int suma = AV_RN16A(&c[0][2 * x]) *  1 + AV_RN16A(&c[1][2 * x]) * -1;
        int sumb = AV_RN16A(&c[4][2 * x]) *  1 + AV_RN16A(&c[3][2 * x]) * -1;

        dst[x] = av_clip(sqrt(suma*suma + sumb*sumb) * scale + delta, 0, peak);
    }
}

static void filter16_sobel(uint8_t *dstp, int width,
                           float scale, float delta, const int *const matrix,
                           const uint8_t *c[], int peak, int radius,
                           int dstride, int stride)
{
    uint16_t *dst = (uint16_t *)dstp;
    int x;

    for (x = 0; x < width; x++) {
        int suma = AV_RN16A(&c[0][2 * x]) * -1 + AV_RN16A(&c[1][2 * x]) * -2 + AV_RN16A(&c[2][2 * x]) * -1 +
                   AV_RN16A(&c[6][2 * x]) *  1 + AV_RN16A(&c[7][2 * x]) *  2 + AV_RN16A(&c[8][2 * x]) *  1;
        int sumb = AV_RN16A(&c[0][2 * x]) * -1 + AV_RN16A(&c[2][2 * x]) *  1 + AV_RN16A(&c[3][2 * x]) * -2 +
                   AV_RN16A(&c[5][2 * x]) *  2 + AV_RN16A(&c[6][2 * x]) * -1 + AV_RN16A(&c[8][2 * x]) *  1;

        dst[x] = av_clip(sqrt(suma*suma + sumb*sumb) * scale + delta, 0, peak);
    }
}

static void filter_prewitt(uint8_t *dst, int width,
                           float scale, float delta, const int *const matrix,
                           const uint8_t *c[], int peak, int radius,
                           int dstride, int stride)
{
    const uint8_t *c0 = c[0], *c1 = c[1], *c2 = c[2];
    const uint8_t *c3 = c[3], *c5 = c[5];
    const uint8_t *c6 = c[6], *c7 = c[7], *c8 = c[8];
    int x;

    for (x = 0; x < width; x++) {
        int suma = c0[x] * -1 + c1[x] * -1 + c2[x] * -1 +
                   c6[x] *  1 + c7[x] *  1 + c8[x] *  1;
        int sumb = c0[x] * -1 + c2[x] *  1 + c3[x] * -1 +
                   c5[x] *  1 + c6[x] * -1 + c8[x] *  1;

        dst[x] = av_clip_uint8(sqrt(suma*suma + sumb*sumb) * scale + delta);
    }
}

static void filter_roberts(uint8_t *dst, int width,
                           float scale, float delta, const int *const matrix,
                           const uint8_t *c[], int peak, int radius,
                           int dstride, int stride)
{
    int x;

    for (x = 0; x < width; x++) {
        int suma = c[0][x] *  1 + c[1][x] * -1;
        int sumb = c[4][x] *  1 + c[3][x] * -1;

        dst[x] = av_clip_uint8(sqrt(suma*suma + sumb*sumb) * scale + delta);
    }
}

static void filter_sobel(uint8_t *dst, int width,
                         float scale, float delta, const int *const matrix,
                         const uint8_t *c[], int peak, int radius,
                         int dstride, int stride)
{
    const uint8_t *c0 = c[0], *c1 = c[1], *c2 = c[2];
    const uint8_t *c3 = c[3], *c5 = c[5];
    const uint8_t *c6 = c[6], *c7 = c[7], *c8 = c[8];
    int x;

    for (x = 0; x < width; x++) {
        int suma = c0[x] * -1 + c1[x] * -2 + c2[x] * -1 +
                   c6[x] *  1 + c7[x] *  2 + c8[x] *  1;
        int sumb = c0[x] * -1 + c2[x] *  1 + c3[x] * -2 +
                   c5[x] *  2 + c6[x] * -1 + c8[x] *  1;

        dst[x] = av_clip_uint8(sqrt(suma*suma + sumb*sumb) * scale + delta);
    }
}

static void filter16_3x3(uint8_t *dstp, int width,
                         float rdiv, float bias, const int *const matrix,
                         const uint8_t *c[], int peak, int radius,
                         int dstride, int stride)
{
    uint16_t *dst = (uint16_t *)dstp;
    int x;

    for (x = 0; x < width; x++) {
        int sum = AV_RN16A(&c[0][2 * x]) * matrix[0] +
                  AV_RN16A(&c[1][2 * x]) * matrix[1] +
                  AV_RN16A(&c[2][2 * x]) * matrix[2] +
                  AV_RN16A(&c[3][2 * x]) * matrix[3] +
                  AV_RN16A(&c[4][2 * x]) * matrix[4] +
                  AV_RN16A(&c[5][2 * x]) * matrix[5] +
                  AV_RN16A(&c[6][2 * x]) * matrix[6] +
                  AV_RN16A(&c[7][2 * x]) * matrix[7] +
                  AV_RN16A(&c[8][2 * x]) * matrix[8];
        sum = (int)(sum * rdiv + bias + 0.5f);
        dst[x] = av_clip(sum, 0, peak);
    }
}

static void filter16_5x5(uint8_t *dstp, int width,
                         float rdiv, float bias, const int *const matrix,
                         const uint8_t *c[], int peak, int radius,
                         int dstride, int stride)
{
    uint16_t *dst = (uint16_t *)dstp;
    int x;

    for (x = 0; x < width; x++) {
        int i, sum = 0;

        for (i = 0; i < 25; i++)
            sum += AV_RN16A(&c[i][2 * x]) * matrix[i];

        sum = (int)(sum * rdiv + bias + 0.5f);
        dst[x] = av_clip(sum, 0, peak);
    }
}

static void filter16_7x7(uint8_t *dstp, int width,
                         float rdiv, float bias, const int *const matrix,
                         const uint8_t *c[], int peak, int radius,
                         int dstride, int stride)
{
    uint16_t *dst = (uint16_t *)dstp;
    int x;

    for (x = 0; x < width; x++) {
        int i, sum = 0;

        for (i = 0; i < 49; i++)
            sum += AV_RN16A(&c[i][2 * x]) * matrix[i];

        sum = (int)(sum * rdiv + bias + 0.5f);
        dst[x] = av_clip(sum, 0, peak);
    }
}

static void filter16_row(uint8_t *dstp, int width,
                         float rdiv, float bias, const int *const matrix,
                         const uint8_t *c[], int peak, int radius,
                         int dstride, int stride)
{
    uint16_t *dst = (uint16_t *)dstp;
    int x;

    for (x = 0; x < width; x++) {
        int i, sum = 0;

        for (i = 0; i < 2 * radius + 1; i++)
            sum += AV_RN16A(&c[i][2 * x]) * matrix[i];

        sum = (int)(sum * rdiv + bias + 0.5f);
        dst[x] = av_clip(sum, 0, peak);
    }
}

static void filter16_column(uint8_t *dstp, int height,
                            float rdiv, float bias, const int *const matrix,
                            const uint8_t *c[], int peak, int radius,
                            int dstride, int stride)
{
    uint16_t *dst = (uint16_t *)dstp;
    int y;

    for (y = 0; y < height; y++) {
        int i, sum = 0;

        for (i = 0; i < 2 * radius + 1; i++)
            sum += AV_RN16A(&c[i][0 + y * stride]) * matrix[i];

        sum = (int)(sum * rdiv + bias + 0.5f);
        dst[0] = av_clip(sum, 0, peak);
        dst += dstride / 2;
    }
}

static void filter_7x7(uint8_t *dst, int width,
                       float rdiv, float bias, const int *const matrix,
                       const uint8_t *c[], int peak, int radius,
                       int dstride, int stride)
{
    int x;

    for (x = 0; x < width; x++) {
        int i, sum = 0;

        for (i = 0; i < 49; i++)
            sum += c[i][x] * matrix[i];

        sum = (int)(sum * rdiv + bias + 0.5f);
        dst[x] = av_clip_uint8(sum);
    }
}

static void filter_5x5(uint8_t *dst, int width,
                       float rdiv, float bias, const int *const matrix,
                       const uint8_t *c[], int peak, int radius,
                       int dstride, int stride)
{
    int x;

    for (x = 0; x < width; x++) {
        int i, sum = 0;

        for (i = 0; i < 25; i++)
            sum += c[i][x] * matrix[i];

        sum = (int)(sum * rdiv + bias + 0.5f);
        dst[x] = av_clip_uint8(sum);
    }
}

static void filter_3x3(uint8_t *dst, int width,
                       float rdiv, float bias, const int *const matrix,
                       const uint8_t *c[], int peak, int radius,
                       int dstride, int stride)
{
    const uint8_t *c0 = c[0], *c1 = c[1], *c2 = c[2];
    const uint8_t *c3 = c[3], *c4 = c[4], *c5 = c[5];
    const uint8_t *c6 = c[6], *c7 = c[7], *c8 = c[8];
    int x;

    for (x = 0; x < width; x++) {
        int sum = c0[x] * matrix[0] + c1[x] * matrix[1] + c2[x] * matrix[2] +
                  c3[x] * matrix[3] + c4[x] * matrix[4] + c5[x] * matrix[5] +
                  c6[x] * matrix[6] + c7[x] * matrix[7] + c8[x] * matrix[8];
        sum = (int)(sum * rdiv + bias + 0.5f);
        dst[x] = av_clip_uint8(sum);
    }
}

static void filter_row(uint8_t *dst, int width,
                       float rdiv, float bias, const int *const matrix,
                       const uint8_t *c[], int peak, int radius,
                       int dstride, int stride)
{
    int x;

    for (x = 0; x < width; x++) {
        int i, sum = 0;

        for (i = 0; i < 2 * radius + 1; i++)
            sum += c[i][x] * matrix[i];

        sum = (int)(sum * rdiv + bias + 0.5f);
        dst[x] = av_clip_uint8(sum);
    }
}

static void filter_column(uint8_t *dst, int height,
                          float rdiv, float bias, const int *const matrix,
                          const uint8_t *c[], int peak, int radius,
                          int dstride, int stride)
{
    int y;

    for (y = 0; y < height; y++) {
        int i, sum = 0;

        for (i = 0; i < 2 * radius + 1; i++)
            sum += c[i][0 + y * stride] * matrix[i];

        sum = (int)(sum * rdiv + bias + 0.5f);
        dst[0] = av_clip_uint8(sum);
        dst += dstride;
    }
}

static void setup_3x3(int radius, const uint8_t *c[], const uint8_t *src, int stride,
                      int x, int w, int y, int h, int bpc)
{
    int i;

    for (i = 0; i < 9; i++) {
        int xoff = FFABS(x + ((i % 3) - 1));
        int yoff = FFABS(y + (i / 3) - 1);

        xoff = xoff >= w ? 2 * w - 1 - xoff : xoff;
        yoff = yoff >= h ? 2 * h - 1 - yoff : yoff;

        c[i] = src + xoff * bpc + yoff * stride;
    }
}

static void setup_5x5(int radius, const uint8_t *c[], const uint8_t *src, int stride,
                      int x, int w, int y, int h, int bpc)
{
    int i;

    for (i = 0; i < 25; i++) {
        int xoff = FFABS(x + ((i % 5) - 2));
        int yoff = FFABS(y + (i / 5) - 2);

        xoff = xoff >= w ? 2 * w - 1 - xoff : xoff;
        yoff = yoff >= h ? 2 * h - 1 - yoff : yoff;

        c[i] = src + xoff * bpc + yoff * stride;
    }
}

static void setup_7x7(int radius, const uint8_t *c[], const uint8_t *src, int stride,
                      int x, int w, int y, int h, int bpc)
{
    int i;

    for (i = 0; i < 49; i++) {
        int xoff = FFABS(x + ((i % 7) - 3));
        int yoff = FFABS(y + (i / 7) - 3);

        xoff = xoff >= w ? 2 * w - 1 - xoff : xoff;
        yoff = yoff >= h ? 2 * h - 1 - yoff : yoff;

        c[i] = src + xoff * bpc + yoff * stride;
    }
}

static void setup_row(int radius, const uint8_t *c[], const uint8_t *src, int stride,
                      int x, int w, int y, int h, int bpc)
{
    int i;

    for (i = 0; i < radius * 2 + 1; i++) {
        int xoff = FFABS(x + i - radius);

        xoff = xoff >= w ? 2 * w - 1 - xoff : xoff;

        c[i] = src + xoff * bpc + y * stride;
    }
}

static void setup_column(int radius, const uint8_t *c[], const uint8_t *src, int stride,
                         int x, int w, int y, int h, int bpc)
{
    int i;

    for (i = 0; i < radius * 2 + 1; i++) {
        int xoff = FFABS(x + i - radius);

        xoff = xoff >= h ? 2 * h - 1 - xoff : xoff;

        c[i] = src + y * bpc + xoff * stride;
    }
}

static int filter_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ConvolutionContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    int plane;

    for (plane = 0; plane < s->nb_planes; plane++) {
        const int mode = s->mode[plane];
        const int bpc = s->bpc;
        const int radius = s->size[plane] / 2;
        const int height = s->planeheight[plane];
        const int width  = s->planewidth[plane];
        const int stride = in->linesize[plane];
        const int dstride = out->linesize[plane];
        const int sizeh = mode == MATRIX_COLUMN ? width : height;
        const int sizew = mode == MATRIX_COLUMN ? height : width;
        const int slice_start = (sizeh * jobnr) / nb_jobs;
        const int slice_end = (sizeh * (jobnr+1)) / nb_jobs;
        const float rdiv = s->rdiv[plane];
        const float bias = s->bias[plane];
        const uint8_t *src = in->data[plane];
        const int dst_pos = slice_start * (mode == MATRIX_COLUMN ? bpc : dstride);
        uint8_t *dst = out->data[plane] + dst_pos;
        const int *matrix = s->matrix[plane];
        const uint8_t *c[49];
        int y, x;

        if (s->copy[plane]) {
            if (mode == MATRIX_COLUMN)
                av_image_copy_plane(dst, dstride, src + slice_start * bpc, stride,
                                    (slice_end - slice_start) * bpc, height);
            else
                av_image_copy_plane(dst, dstride, src + slice_start * stride, stride,
                                    width * bpc, slice_end - slice_start);
            continue;
        }

        for (y = slice_start; y < slice_end; y++) {
            const int xoff = mode == MATRIX_COLUMN ? (y - slice_start) * bpc : radius * bpc;
            const int yoff = mode == MATRIX_COLUMN ? radius * stride : 0;

            for (x = 0; x < radius; x++) {
                const int xoff = mode == MATRIX_COLUMN ? (y - slice_start) * bpc : x * bpc;
                const int yoff = mode == MATRIX_COLUMN ? x * stride : 0;

                s->setup[plane](radius, c, src, stride, x, width, y, height, bpc);
                s->filter[plane](dst + yoff + xoff, 1, rdiv,
                                 bias, matrix, c, s->max, radius,
                                 dstride, stride);
            }
            s->setup[plane](radius, c, src, stride, radius, width, y, height, bpc);
            s->filter[plane](dst + yoff + xoff, sizew - 2 * radius,
                             rdiv, bias, matrix, c, s->max, radius,
                             dstride, stride);
            for (x = sizew - radius; x < sizew; x++) {
                const int xoff = mode == MATRIX_COLUMN ? (y - slice_start) * bpc : x * bpc;
                const int yoff = mode == MATRIX_COLUMN ? x * stride : 0;

                s->setup[plane](radius, c, src, stride, x, width, y, height, bpc);
                s->filter[plane](dst + yoff + xoff, 1, rdiv,
                                 bias, matrix, c, s->max, radius,
                                 dstride, stride);
            }
            if (mode != MATRIX_COLUMN)
                dst += dstride;
        }
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
    s->max = (1 << s->depth) - 1;

    s->planewidth[1] = s->planewidth[2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0] = s->planewidth[3] = inlink->w;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);
    s->nb_threads = ff_filter_get_nb_threads(ctx);
    s->bpc = (s->depth + 7) / 8;

    if (!strcmp(ctx->filter->name, "convolution")) {
        if (s->depth > 8) {
            for (p = 0; p < s->nb_planes; p++) {
                if (s->mode[p] == MATRIX_ROW)
                    s->filter[p] = filter16_row;
                else if (s->mode[p] == MATRIX_COLUMN)
                    s->filter[p] = filter16_column;
                else if (s->size[p] == 3)
                    s->filter[p] = filter16_3x3;
                else if (s->size[p] == 5)
                    s->filter[p] = filter16_5x5;
                else if (s->size[p] == 7)
                    s->filter[p] = filter16_7x7;
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
    ThreadData td;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    td.in = in;
    td.out = out;
    ctx->internal->execute(ctx, filter_slice, &td, NULL, FFMIN3(s->planeheight[1], s->planewidth[1], s->nb_threads));

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
            float sum = 0;

            p = s->matrix_str[i];
            while (s->matrix_length[i] < 49) {
                if (!(arg = av_strtok(p, " ", &saveptr)))
                    break;

                p = NULL;
                sscanf(arg, "%d", &matrix[s->matrix_length[i]]);
                sum += matrix[s->matrix_length[i]];
                s->matrix_length[i]++;
            }

            if (!(s->matrix_length[i] & 1)) {
                av_log(ctx, AV_LOG_ERROR, "number of matrix elements must be odd\n");
                return AVERROR(EINVAL);
            }
            if (s->mode[i] == MATRIX_ROW) {
                s->filter[i] = filter_row;
                s->setup[i] = setup_row;
                s->size[i] = s->matrix_length[i];
            } else if (s->mode[i] == MATRIX_COLUMN) {
                s->filter[i] = filter_column;
                s->setup[i] = setup_column;
                s->size[i] = s->matrix_length[i];
            } else if (s->matrix_length[i] == 9) {
                s->size[i] = 3;
                if (!memcmp(matrix, same3x3, sizeof(same3x3)))
                    s->copy[i] = 1;
                else
                    s->filter[i] = filter_3x3;
                s->setup[i] = setup_3x3;
            } else if (s->matrix_length[i] == 25) {
                s->size[i] = 5;
                if (!memcmp(matrix, same5x5, sizeof(same5x5)))
                    s->copy[i] = 1;
                else
                    s->filter[i] = filter_5x5;
                s->setup[i] = setup_5x5;
            } else if (s->matrix_length[i] == 49) {
                s->size[i] = 7;
                if (!memcmp(matrix, same7x7, sizeof(same7x7)))
                    s->copy[i] = 1;
                else
                    s->filter[i] = filter_7x7;
                s->setup[i] = setup_7x7;
            } else {
                return AVERROR(EINVAL);
            }

            if (sum == 0)
                sum = 1;
            if (s->rdiv[i] == 0)
                s->rdiv[i] = 1. / sum;

            if (s->copy[i] && (s->rdiv[i] != 1. || s->bias[i] != 0.))
                s->copy[i] = 0;
        }
    } else if (!strcmp(ctx->filter->name, "prewitt")) {
        for (i = 0; i < 4; i++) {
            if ((1 << i) & s->planes)
                s->filter[i] = filter_prewitt;
            else
                s->copy[i] = 1;
            s->size[i] = 3;
            s->setup[i] = setup_3x3;
            s->rdiv[i] = s->scale;
            s->bias[i] = s->delta;
        }
    } else if (!strcmp(ctx->filter->name, "roberts")) {
        for (i = 0; i < 4; i++) {
            if ((1 << i) & s->planes)
                s->filter[i] = filter_roberts;
            else
                s->copy[i] = 1;
            s->size[i] = 3;
            s->setup[i] = setup_3x3;
            s->rdiv[i] = s->scale;
            s->bias[i] = s->delta;
        }
    } else if (!strcmp(ctx->filter->name, "sobel")) {
        for (i = 0; i < 4; i++) {
            if ((1 << i) & s->planes)
                s->filter[i] = filter_sobel;
            else
                s->copy[i] = 1;
            s->size[i] = 3;
            s->setup[i] = setup_3x3;
            s->rdiv[i] = s->scale;
            s->bias[i] = s->delta;
        }
    }

    return 0;
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
    .query_formats = query_formats,
    .inputs        = convolution_inputs,
    .outputs       = convolution_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};

#endif /* CONFIG_ROBERTS_FILTER */
