/*
 * Copyright (c) 2017 Paul B Mahol
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

#include "config_components.h"

#include <float.h>

#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/tx.h"

#include "avfilter.h"
#include "framesync.h"
#include "internal.h"

#define MAX_THREADS 16

typedef struct ConvolveContext {
    const AVClass *class;
    FFFrameSync fs;

    AVTXContext *fft[4][MAX_THREADS];
    AVTXContext *ifft[4][MAX_THREADS];

    av_tx_fn tx_fn[4];
    av_tx_fn itx_fn[4];

    int fft_len[4];
    int planewidth[4];
    int planeheight[4];

    int primarywidth[4];
    int primaryheight[4];

    int secondarywidth[4];
    int secondaryheight[4];

    AVComplexFloat *fft_hdata_in[4];
    AVComplexFloat *fft_vdata_in[4];
    AVComplexFloat *fft_hdata_out[4];
    AVComplexFloat *fft_vdata_out[4];
    AVComplexFloat *fft_hdata_impulse_in[4];
    AVComplexFloat *fft_vdata_impulse_in[4];
    AVComplexFloat *fft_hdata_impulse_out[4];
    AVComplexFloat *fft_vdata_impulse_out[4];

    int depth;
    int planes;
    int impulse;
    float noise;
    int nb_planes;
    int got_impulse[4];

    void (*get_input)(struct ConvolveContext *s, AVComplexFloat *fft_hdata,
                      AVFrame *in, int w, int h, int n, int plane, float scale);

    void (*get_output)(struct ConvolveContext *s, AVComplexFloat *input, AVFrame *out,
                       int w, int h, int n, int plane, float scale);
    void (*prepare_impulse)(AVFilterContext *ctx, AVFrame *impulsepic, int plane);

    int (*filter)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} ConvolveContext;

#define OFFSET(x) offsetof(ConvolveContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption convolve_options[] = {
    { "planes",  "set planes to convolve",                  OFFSET(planes),   AV_OPT_TYPE_INT,   {.i64=7}, 0, 15, FLAGS },
    { "impulse", "when to process impulses",                OFFSET(impulse),  AV_OPT_TYPE_INT,   {.i64=1}, 0,  1, FLAGS, .unit = "impulse" },
    {   "first", "process only first impulse, ignore rest", 0,                AV_OPT_TYPE_CONST, {.i64=0}, 0,  0, FLAGS, .unit = "impulse" },
    {   "all",   "process all impulses",                    0,                AV_OPT_TYPE_CONST, {.i64=1}, 0,  0, FLAGS, .unit = "impulse" },
    { "noise",   "set noise",                               OFFSET(noise),    AV_OPT_TYPE_FLOAT, {.dbl=0.0000001}, 0,  1, FLAGS },
    { NULL },
};

static const enum AVPixelFormat pixel_fmts_fftfilt[] = {
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
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
    AV_PIX_FMT_NONE
};

static int config_input(AVFilterLink *inlink)
{
    ConvolveContext *s = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    const int w = inlink->w;
    const int h = inlink->h;

    s->planewidth[1] = s->planewidth[2] = AV_CEIL_RSHIFT(w, desc->log2_chroma_w);
    s->planewidth[0] = s->planewidth[3] = w;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = h;

    s->nb_planes = desc->nb_components;
    s->depth = desc->comp[0].depth;

    for (int i = 0; i < s->nb_planes; i++) {
        int w = s->planewidth[i];
        int h = s->planeheight[i];
        int n = FFMAX(w, h);

        s->fft_len[i] = 1 << (av_log2(2 * n - 1));

        if (!(s->fft_hdata_in[i] = av_calloc(s->fft_len[i], s->fft_len[i] * sizeof(AVComplexFloat))))
            return AVERROR(ENOMEM);

        if (!(s->fft_hdata_out[i] = av_calloc(s->fft_len[i], s->fft_len[i] * sizeof(AVComplexFloat))))
            return AVERROR(ENOMEM);

        if (!(s->fft_vdata_in[i] = av_calloc(s->fft_len[i], s->fft_len[i] * sizeof(AVComplexFloat))))
            return AVERROR(ENOMEM);

        if (!(s->fft_vdata_out[i] = av_calloc(s->fft_len[i], s->fft_len[i] * sizeof(AVComplexFloat))))
            return AVERROR(ENOMEM);

        if (!(s->fft_hdata_impulse_in[i] = av_calloc(s->fft_len[i], s->fft_len[i] * sizeof(AVComplexFloat))))
            return AVERROR(ENOMEM);

        if (!(s->fft_vdata_impulse_in[i] = av_calloc(s->fft_len[i], s->fft_len[i] * sizeof(AVComplexFloat))))
            return AVERROR(ENOMEM);

        if (!(s->fft_hdata_impulse_out[i] = av_calloc(s->fft_len[i], s->fft_len[i] * sizeof(AVComplexFloat))))
            return AVERROR(ENOMEM);

        if (!(s->fft_vdata_impulse_out[i] = av_calloc(s->fft_len[i], s->fft_len[i] * sizeof(AVComplexFloat))))
            return AVERROR(ENOMEM);
    }

    return 0;
}

static int config_input_impulse(AVFilterLink *inlink)
{
    AVFilterContext *ctx  = inlink->dst;

    if (ctx->inputs[0]->w != ctx->inputs[1]->w ||
        ctx->inputs[0]->h != ctx->inputs[1]->h) {
        av_log(ctx, AV_LOG_ERROR, "Width and height of input videos must be same.\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

typedef struct ThreadData {
    AVComplexFloat *hdata_in, *vdata_in;
    AVComplexFloat *hdata_out, *vdata_out;
    int plane, n;
} ThreadData;

static int fft_horizontal(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ConvolveContext *s = ctx->priv;
    ThreadData *td = arg;
    AVComplexFloat *hdata_in = td->hdata_in;
    AVComplexFloat *hdata_out = td->hdata_out;
    const int plane = td->plane;
    const int n = td->n;
    int start = (n * jobnr) / nb_jobs;
    int end = (n * (jobnr+1)) / nb_jobs;
    int y;

    for (y = start; y < end; y++) {
        s->tx_fn[plane](s->fft[plane][jobnr], hdata_out + y * n, hdata_in + y * n, sizeof(AVComplexFloat));
    }

    return 0;
}

#define SQR(x) ((x) * (x))

static void get_zeropadded_input(ConvolveContext *s,
                                 AVComplexFloat *fft_hdata,
                                 AVFrame *in, int w, int h,
                                 int n, int plane, float scale)
{
    float sum = 0.f;
    float mean, dev;
    int y, x;

    if (s->depth == 8) {
        for (y = 0; y < h; y++) {
            const uint8_t *src = in->data[plane] + in->linesize[plane] * y;

            for (x = 0; x < w; x++)
                sum += src[x];
        }

        mean = sum / (w * h);
        sum = 0.f;
        for (y = 0; y < h; y++) {
            const uint8_t *src = in->data[plane] + in->linesize[plane] * y;

            for (x = 0; x < w; x++)
                sum += SQR(src[x] - mean);
        }

        dev = sqrtf(sum / (w * h));
        scale /= dev;
        for (y = 0; y < h; y++) {
            const uint8_t *src = in->data[plane] + in->linesize[plane] * y;

            for (x = 0; x < w; x++) {
                fft_hdata[y * n + x].re = (src[x] - mean) * scale;
                fft_hdata[y * n + x].im = 0;
            }

            for (x = w; x < n; x++) {
                fft_hdata[y * n + x].re = 0;
                fft_hdata[y * n + x].im = 0;
            }
        }

        for (y = h; y < n; y++) {
            for (x = 0; x < n; x++) {
                fft_hdata[y * n + x].re = 0;
                fft_hdata[y * n + x].im = 0;
            }
        }
    } else {
        for (y = 0; y < h; y++) {
            const uint16_t *src = (const uint16_t *)(in->data[plane] + in->linesize[plane] * y);

            for (x = 0; x < w; x++)
                sum += src[x];
        }

        mean = sum / (w * h);
        sum = 0.f;
        for (y = 0; y < h; y++) {
            const uint16_t *src = (const uint16_t *)(in->data[plane] + in->linesize[plane] * y);

            for (x = 0; x < w; x++)
                sum += SQR(src[x] - mean);
        }

        dev = sqrtf(sum / (w * h));
        scale /= dev;
        for (y = 0; y < h; y++) {
            const uint16_t *src = (const uint16_t *)(in->data[plane] + in->linesize[plane] * y);

            for (x = 0; x < w; x++) {
                fft_hdata[y * n + x].re = (src[x] - mean) * scale;
                fft_hdata[y * n + x].im = 0;
            }

            for (x = w; x < n; x++) {
                fft_hdata[y * n + x].re = 0;
                fft_hdata[y * n + x].im = 0;
            }
        }

        for (y = h; y < n; y++) {
            for (x = 0; x < n; x++) {
                fft_hdata[y * n + x].re = 0;
                fft_hdata[y * n + x].im = 0;
            }
        }
    }
}

static void get_input(ConvolveContext *s, AVComplexFloat *fft_hdata,
                      AVFrame *in, int w, int h, int n, int plane, float scale)
{
    const int iw = (n - w) / 2, ih = (n - h) / 2;
    int y, x;

    if (s->depth == 8) {
        for (y = 0; y < h; y++) {
            const uint8_t *src = in->data[plane] + in->linesize[plane] * y;

            for (x = 0; x < w; x++) {
                fft_hdata[(y + ih) * n + iw + x].re = src[x] * scale;
                fft_hdata[(y + ih) * n + iw + x].im = 0;
            }

            for (x = 0; x < iw; x++) {
                fft_hdata[(y + ih) * n + x].re = fft_hdata[(y + ih) * n + iw].re;
                fft_hdata[(y + ih) * n + x].im = 0;
            }

            for (x = n - iw; x < n; x++) {
                fft_hdata[(y + ih) * n + x].re = fft_hdata[(y + ih) * n + n - iw - 1].re;
                fft_hdata[(y + ih) * n + x].im = 0;
            }
        }

        for (y = 0; y < ih; y++) {
            for (x = 0; x < n; x++) {
                fft_hdata[y * n + x].re = fft_hdata[ih * n + x].re;
                fft_hdata[y * n + x].im = 0;
            }
        }

        for (y = n - ih; y < n; y++) {
            for (x = 0; x < n; x++) {
                fft_hdata[y * n + x].re = fft_hdata[(n - ih - 1) * n + x].re;
                fft_hdata[y * n + x].im = 0;
            }
        }
    } else {
        for (y = 0; y < h; y++) {
            const uint16_t *src = (const uint16_t *)(in->data[plane] + in->linesize[plane] * y);

            for (x = 0; x < w; x++) {
                fft_hdata[(y + ih) * n + iw + x].re = src[x] * scale;
                fft_hdata[(y + ih) * n + iw + x].im = 0;
            }

            for (x = 0; x < iw; x++) {
                fft_hdata[(y + ih) * n + x].re = fft_hdata[(y + ih) * n + iw].re;
                fft_hdata[(y + ih) * n + x].im = 0;
            }

            for (x = n - iw; x < n; x++) {
                fft_hdata[(y + ih) * n + x].re = fft_hdata[(y + ih) * n + n - iw - 1].re;
                fft_hdata[(y + ih) * n + x].im = 0;
            }
        }

        for (y = 0; y < ih; y++) {
            for (x = 0; x < n; x++) {
                fft_hdata[y * n + x].re = fft_hdata[ih * n + x].re;
                fft_hdata[y * n + x].im = 0;
            }
        }

        for (y = n - ih; y < n; y++) {
            for (x = 0; x < n; x++) {
                fft_hdata[y * n + x].re = fft_hdata[(n - ih - 1) * n + x].re;
                fft_hdata[y * n + x].im = 0;
            }
        }
    }
}

static int fft_vertical(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ConvolveContext *s = ctx->priv;
    ThreadData *td = arg;
    AVComplexFloat *hdata = td->hdata_out;
    AVComplexFloat *vdata_in = td->vdata_in;
    AVComplexFloat *vdata_out = td->vdata_out;
    const int plane = td->plane;
    const int n = td->n;
    int start = (n * jobnr) / nb_jobs;
    int end = (n * (jobnr+1)) / nb_jobs;
    int y, x;

    for (y = start; y < end; y++) {
        for (x = 0; x < n; x++) {
            vdata_in[y * n + x].re = hdata[x * n + y].re;
            vdata_in[y * n + x].im = hdata[x * n + y].im;
        }

        s->tx_fn[plane](s->fft[plane][jobnr], vdata_out + y * n, vdata_in + y * n, sizeof(AVComplexFloat));
    }

    return 0;
}

static int ifft_vertical(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ConvolveContext *s = ctx->priv;
    ThreadData *td = arg;
    AVComplexFloat *hdata = td->hdata_out;
    AVComplexFloat *vdata_out = td->vdata_out;
    AVComplexFloat *vdata_in = td->vdata_in;
    const int plane = td->plane;
    const int n = td->n;
    int start = (n * jobnr) / nb_jobs;
    int end = (n * (jobnr+1)) / nb_jobs;
    int y, x;

    for (y = start; y < end; y++) {
        s->itx_fn[plane](s->ifft[plane][jobnr], vdata_out + y * n, vdata_in + y * n, sizeof(AVComplexFloat));

        for (x = 0; x < n; x++) {
            hdata[x * n + y].re = vdata_out[y * n + x].re;
            hdata[x * n + y].im = vdata_out[y * n + x].im;
        }
    }

    return 0;
}

static int ifft_horizontal(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ConvolveContext *s = ctx->priv;
    ThreadData *td = arg;
    AVComplexFloat *hdata_out = td->hdata_out;
    AVComplexFloat *hdata_in = td->hdata_in;
    const int plane = td->plane;
    const int n = td->n;
    int start = (n * jobnr) / nb_jobs;
    int end = (n * (jobnr+1)) / nb_jobs;
    int y;

    for (y = start; y < end; y++) {
        s->itx_fn[plane](s->ifft[plane][jobnr], hdata_out + y * n, hdata_in + y * n, sizeof(AVComplexFloat));
    }

    return 0;
}

static void get_xoutput(ConvolveContext *s, AVComplexFloat *input, AVFrame *out,
                       int w, int h, int n, int plane, float scale)
{
    const int imax = (1 << s->depth) - 1;

    scale *= imax * 16;
    if (s->depth == 8) {
        for (int y = 0; y < h; y++) {
            uint8_t *dst = out->data[plane] + y * out->linesize[plane];
            for (int x = 0; x < w; x++)
                dst[x] = av_clip_uint8(input[y * n + x].re * scale);
        }
    } else {
        for (int y = 0; y < h; y++) {
            uint16_t *dst = (uint16_t *)(out->data[plane] + y * out->linesize[plane]);
            for (int x = 0; x < w; x++)
                dst[x] = av_clip(input[y * n + x].re * scale, 0, imax);
        }
    }
}

static void get_output(ConvolveContext *s, AVComplexFloat *input, AVFrame *out,
                       int w, int h, int n, int plane, float scale)
{
    const int max = (1 << s->depth) - 1;
    const int hh = h / 2;
    const int hw = w / 2;
    int y, x;

    if (s->depth == 8) {
        for (y = 0; y < hh; y++) {
            uint8_t *dst = out->data[plane] + (y + hh) * out->linesize[plane] + hw;
            for (x = 0; x < hw; x++)
                dst[x] = av_clip_uint8(input[y * n + x].re * scale);
        }
        for (y = 0; y < hh; y++) {
            uint8_t *dst = out->data[plane] + (y + hh) * out->linesize[plane];
            for (x = 0; x < hw; x++)
                dst[x] = av_clip_uint8(input[y * n + n - hw + x].re * scale);
        }
        for (y = 0; y < hh; y++) {
            uint8_t *dst = out->data[plane] + y * out->linesize[plane] + hw;
            for (x = 0; x < hw; x++)
                dst[x] = av_clip_uint8(input[(n - hh + y) * n + x].re * scale);
        }
        for (y = 0; y < hh; y++) {
            uint8_t *dst = out->data[plane] + y * out->linesize[plane];
            for (x = 0; x < hw; x++)
                dst[x] = av_clip_uint8(input[(n - hh + y) * n + n - hw + x].re * scale);
        }
    } else {
        for (y = 0; y < hh; y++) {
            uint16_t *dst = (uint16_t *)(out->data[plane] + (y + hh) * out->linesize[plane] + hw * 2);
            for (x = 0; x < hw; x++)
                dst[x] = av_clip(input[y * n + x].re * scale, 0, max);
        }
        for (y = 0; y < hh; y++) {
            uint16_t *dst = (uint16_t *)(out->data[plane] + (y + hh) * out->linesize[plane]);
            for (x = 0; x < hw; x++)
                dst[x] = av_clip(input[y * n + n - hw + x].re * scale, 0, max);
        }
        for (y = 0; y < hh; y++) {
            uint16_t *dst = (uint16_t *)(out->data[plane] + y * out->linesize[plane] + hw * 2);
            for (x = 0; x < hw; x++)
                dst[x] = av_clip(input[(n - hh + y) * n + x].re * scale, 0, max);
        }
        for (y = 0; y < hh; y++) {
            uint16_t *dst = (uint16_t *)(out->data[plane] + y * out->linesize[plane]);
            for (x = 0; x < hw; x++)
                dst[x] = av_clip(input[(n - hh + y) * n + n - hw + x].re * scale, 0, max);
        }
    }
}

static int complex_multiply(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ConvolveContext *s = ctx->priv;
    ThreadData *td = arg;
    AVComplexFloat *input = td->hdata_in;
    AVComplexFloat *filter = td->vdata_in;
    const float noise = s->noise;
    const int n = td->n;
    int start = (n * jobnr) / nb_jobs;
    int end = (n * (jobnr+1)) / nb_jobs;
    int y, x;

    for (y = start; y < end; y++) {
        int yn = y * n;

        for (x = 0; x < n; x++) {
            float re, im, ire, iim;

            re = input[yn + x].re;
            im = input[yn + x].im;
            ire = filter[yn + x].re + noise;
            iim = filter[yn + x].im;

            input[yn + x].re = ire * re - iim * im;
            input[yn + x].im = iim * re + ire * im;
        }
    }

    return 0;
}

static int complex_xcorrelate(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ThreadData *td = arg;
    AVComplexFloat *input = td->hdata_in;
    AVComplexFloat *filter = td->vdata_in;
    const int n = td->n;
    const float scale = 1.f / (n * n);
    int start = (n * jobnr) / nb_jobs;
    int end = (n * (jobnr+1)) / nb_jobs;

    for (int y = start; y < end; y++) {
        int yn = y * n;

        for (int x = 0; x < n; x++) {
            float re, im, ire, iim;

            re = input[yn + x].re;
            im = input[yn + x].im;
            ire = filter[yn + x].re * scale;
            iim = -filter[yn + x].im * scale;

            input[yn + x].re = ire * re - iim * im;
            input[yn + x].im = iim * re + ire * im;
        }
    }

    return 0;
}

static int complex_divide(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ConvolveContext *s = ctx->priv;
    ThreadData *td = arg;
    AVComplexFloat *input = td->hdata_in;
    AVComplexFloat *filter = td->vdata_in;
    const float noise = s->noise;
    const int n = td->n;
    int start = (n * jobnr) / nb_jobs;
    int end = (n * (jobnr+1)) / nb_jobs;
    int y, x;

    for (y = start; y < end; y++) {
        int yn = y * n;

        for (x = 0; x < n; x++) {
            float re, im, ire, iim, div;

            re = input[yn + x].re;
            im = input[yn + x].im;
            ire = filter[yn + x].re;
            iim = filter[yn + x].im;
            div = ire * ire + iim * iim + noise;

            input[yn + x].re = (ire * re + iim * im) / div;
            input[yn + x].im = (ire * im - iim * re) / div;
        }
    }

    return 0;
}

static void prepare_impulse(AVFilterContext *ctx, AVFrame *impulsepic, int plane)
{
    ConvolveContext *s = ctx->priv;
    const int n = s->fft_len[plane];
    const int w = s->secondarywidth[plane];
    const int h = s->secondaryheight[plane];
    ThreadData td;
    float total = 0;

    if (s->depth == 8) {
        for (int y = 0; y < h; y++) {
            const uint8_t *src = (const uint8_t *)(impulsepic->data[plane] + y * impulsepic->linesize[plane]) ;
            for (int x = 0; x < w; x++) {
                total += src[x];
            }
        }
    } else {
        for (int y = 0; y < h; y++) {
            const uint16_t *src = (const uint16_t *)(impulsepic->data[plane] + y * impulsepic->linesize[plane]) ;
            for (int x = 0; x < w; x++) {
                total += src[x];
            }
        }
    }
    total = FFMAX(1, total);

    s->get_input(s, s->fft_hdata_impulse_in[plane], impulsepic, w, h, n, plane, 1.f / total);

    td.n = n;
    td.plane = plane;
    td.hdata_in  = s->fft_hdata_impulse_in[plane];
    td.vdata_in  = s->fft_vdata_impulse_in[plane];
    td.hdata_out = s->fft_hdata_impulse_out[plane];
    td.vdata_out = s->fft_vdata_impulse_out[plane];

    ff_filter_execute(ctx, fft_horizontal, &td, NULL,
                      FFMIN3(MAX_THREADS, n, ff_filter_get_nb_threads(ctx)));
    ff_filter_execute(ctx, fft_vertical, &td, NULL,
                      FFMIN3(MAX_THREADS, n, ff_filter_get_nb_threads(ctx)));

    s->got_impulse[plane] = 1;
}

static void prepare_secondary(AVFilterContext *ctx, AVFrame *secondary, int plane)
{
    ConvolveContext *s = ctx->priv;
    const int n = s->fft_len[plane];
    ThreadData td;

    s->get_input(s, s->fft_hdata_impulse_in[plane], secondary,
                 s->secondarywidth[plane],
                 s->secondaryheight[plane],
                 n, plane, 1.f);

    td.n = n;
    td.plane = plane;
    td.hdata_in  = s->fft_hdata_impulse_in[plane];
    td.vdata_in  = s->fft_vdata_impulse_in[plane];
    td.hdata_out = s->fft_hdata_impulse_out[plane];
    td.vdata_out = s->fft_vdata_impulse_out[plane];

    ff_filter_execute(ctx, fft_horizontal, &td, NULL,
                      FFMIN3(MAX_THREADS, n, ff_filter_get_nb_threads(ctx)));
    ff_filter_execute(ctx, fft_vertical, &td, NULL,
                      FFMIN3(MAX_THREADS, n, ff_filter_get_nb_threads(ctx)));

    s->got_impulse[plane] = 1;
}

static int do_convolve(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    AVFilterLink *outlink = ctx->outputs[0];
    ConvolveContext *s = ctx->priv;
    AVFrame *mainpic = NULL, *impulsepic = NULL;
    int ret, plane;

    ret = ff_framesync_dualinput_get(fs, &mainpic, &impulsepic);
    if (ret < 0)
        return ret;
    if (!impulsepic)
        return ff_filter_frame(outlink, mainpic);

    for (plane = 0; plane < s->nb_planes; plane++) {
        AVComplexFloat *filter = s->fft_vdata_impulse_out[plane];
        AVComplexFloat *input = s->fft_vdata_out[plane];
        const int n = s->fft_len[plane];
        const int w = s->primarywidth[plane];
        const int h = s->primaryheight[plane];
        const int ow = s->planewidth[plane];
        const int oh = s->planeheight[plane];
        ThreadData td;

        if (!(s->planes & (1 << plane))) {
            continue;
        }

        td.plane = plane, td.n = n;
        s->get_input(s, s->fft_hdata_in[plane], mainpic, w, h, n, plane, 1.f);

        td.hdata_in  = s->fft_hdata_in[plane];
        td.vdata_in  = s->fft_vdata_in[plane];
        td.hdata_out = s->fft_hdata_out[plane];
        td.vdata_out = s->fft_vdata_out[plane];

        ff_filter_execute(ctx, fft_horizontal, &td, NULL,
                          FFMIN3(MAX_THREADS, n, ff_filter_get_nb_threads(ctx)));
        ff_filter_execute(ctx, fft_vertical, &td, NULL,
                          FFMIN3(MAX_THREADS, n, ff_filter_get_nb_threads(ctx)));

        if ((!s->impulse && !s->got_impulse[plane]) || s->impulse) {
            s->prepare_impulse(ctx, impulsepic, plane);
        }

        td.hdata_in = input;
        td.vdata_in = filter;

        ff_filter_execute(ctx, s->filter, &td, NULL,
                          FFMIN3(MAX_THREADS, n, ff_filter_get_nb_threads(ctx)));

        td.hdata_in  = s->fft_hdata_out[plane];
        td.vdata_in  = s->fft_vdata_out[plane];
        td.hdata_out = s->fft_hdata_in[plane];
        td.vdata_out = s->fft_vdata_in[plane];

        ff_filter_execute(ctx, ifft_vertical, &td, NULL,
                          FFMIN3(MAX_THREADS, n, ff_filter_get_nb_threads(ctx)));

        td.hdata_out = s->fft_hdata_out[plane];
        td.hdata_in  = s->fft_hdata_in[plane];

        ff_filter_execute(ctx, ifft_horizontal, &td, NULL,
                          FFMIN3(MAX_THREADS, n, ff_filter_get_nb_threads(ctx)));

        s->get_output(s, s->fft_hdata_out[plane], mainpic, ow, oh, n, plane, 1.f / (n * n));
    }

    return ff_filter_frame(outlink, mainpic);
}

static int config_output(AVFilterLink *outlink)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(outlink->format);
    AVFilterContext *ctx = outlink->src;
    ConvolveContext *s = ctx->priv;
    AVFilterLink *mainlink = ctx->inputs[0];
    AVFilterLink *secondlink = ctx->inputs[1];
    int ret, i, j;

    s->primarywidth[1]  = s->primarywidth[2]  = AV_CEIL_RSHIFT(mainlink->w, desc->log2_chroma_w);
    s->primarywidth[0]  = s->primarywidth[3]  = mainlink->w;
    s->primaryheight[1] = s->primaryheight[2] = AV_CEIL_RSHIFT(mainlink->h, desc->log2_chroma_h);
    s->primaryheight[0] = s->primaryheight[3] = mainlink->h;

    s->secondarywidth[1]  = s->secondarywidth[2]  = AV_CEIL_RSHIFT(secondlink->w, desc->log2_chroma_w);
    s->secondarywidth[0]  = s->secondarywidth[3]  = secondlink->w;
    s->secondaryheight[1] = s->secondaryheight[2] = AV_CEIL_RSHIFT(secondlink->h, desc->log2_chroma_h);
    s->secondaryheight[0] = s->secondaryheight[3] = secondlink->h;

    s->fs.on_event = do_convolve;
    ret = ff_framesync_init_dualinput(&s->fs, ctx);
    if (ret < 0)
        return ret;
    outlink->w = mainlink->w;
    outlink->h = mainlink->h;
    outlink->time_base = mainlink->time_base;
    outlink->sample_aspect_ratio = mainlink->sample_aspect_ratio;
    outlink->frame_rate = mainlink->frame_rate;

    if ((ret = ff_framesync_configure(&s->fs)) < 0)
        return ret;

    for (i = 0; i < s->nb_planes; i++) {
        for (j = 0; j < MAX_THREADS; j++) {
            float scale = 1.f;

            ret = av_tx_init(&s->fft[i][j], &s->tx_fn[i], AV_TX_FLOAT_FFT, 0, s->fft_len[i], &scale, 0);
            if (ret < 0)
                return ret;
            ret = av_tx_init(&s->ifft[i][j], &s->itx_fn[i], AV_TX_FLOAT_FFT, 1, s->fft_len[i], &scale, 0);
            if (ret < 0)
                return ret;
        }
    }

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    ConvolveContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static av_cold int init(AVFilterContext *ctx)
{
    ConvolveContext *s = ctx->priv;

    if (!strcmp(ctx->filter->name, "convolve")) {
        s->filter = complex_multiply;
        s->prepare_impulse = prepare_impulse;
        s->get_input = get_input;
        s->get_output = get_output;
    } else if (!strcmp(ctx->filter->name, "xcorrelate")) {
        s->filter = complex_xcorrelate;
        s->prepare_impulse = prepare_secondary;
        s->get_input = get_zeropadded_input;
        s->get_output = get_xoutput;
    } else if (!strcmp(ctx->filter->name, "deconvolve")) {
        s->filter = complex_divide;
        s->prepare_impulse = prepare_impulse;
        s->get_input = get_input;
        s->get_output = get_output;
    } else {
        return AVERROR_BUG;
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ConvolveContext *s = ctx->priv;
    int i, j;

    for (i = 0; i < 4; i++) {
        av_freep(&s->fft_hdata_in[i]);
        av_freep(&s->fft_vdata_in[i]);
        av_freep(&s->fft_hdata_out[i]);
        av_freep(&s->fft_vdata_out[i]);
        av_freep(&s->fft_hdata_impulse_in[i]);
        av_freep(&s->fft_vdata_impulse_in[i]);
        av_freep(&s->fft_hdata_impulse_out[i]);
        av_freep(&s->fft_vdata_impulse_out[i]);

        for (j = 0; j < MAX_THREADS; j++) {
            av_tx_uninit(&s->fft[i][j]);
            av_tx_uninit(&s->ifft[i][j]);
        }
    }

    ff_framesync_uninit(&s->fs);
}

static const AVFilterPad convolve_inputs[] = {
    {
        .name          = "main",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_input,
    },{
        .name          = "impulse",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_input_impulse,
    },
};

static const AVFilterPad convolve_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

FRAMESYNC_AUXILIARY_FUNCS(convolve, ConvolveContext, fs)

#if CONFIG_CONVOLVE_FILTER

FRAMESYNC_DEFINE_PURE_CLASS(convolve, "convolve", convolve, convolve_options);

const AVFilter ff_vf_convolve = {
    .name          = "convolve",
    .description   = NULL_IF_CONFIG_SMALL("Convolve first video stream with second video stream."),
    .preinit       = convolve_framesync_preinit,
    .init          = init,
    .uninit        = uninit,
    .activate      = activate,
    .priv_size     = sizeof(ConvolveContext),
    .priv_class    = &convolve_class,
    FILTER_INPUTS(convolve_inputs),
    FILTER_OUTPUTS(convolve_outputs),
    FILTER_PIXFMTS_ARRAY(pixel_fmts_fftfilt),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL | AVFILTER_FLAG_SLICE_THREADS,
};

#endif /* CONFIG_CONVOLVE_FILTER */

#if CONFIG_DECONVOLVE_FILTER

static const AVOption deconvolve_options[] = {
    { "planes",  "set planes to deconvolve",                OFFSET(planes),   AV_OPT_TYPE_INT,   {.i64=7}, 0, 15, FLAGS },
    { "impulse", "when to process impulses",                OFFSET(impulse),  AV_OPT_TYPE_INT,   {.i64=1}, 0,  1, FLAGS, .unit = "impulse" },
    {   "first", "process only first impulse, ignore rest", 0,                AV_OPT_TYPE_CONST, {.i64=0}, 0,  0, FLAGS, .unit = "impulse" },
    {   "all",   "process all impulses",                    0,                AV_OPT_TYPE_CONST, {.i64=1}, 0,  0, FLAGS, .unit = "impulse" },
    { "noise",   "set noise",                               OFFSET(noise),    AV_OPT_TYPE_FLOAT, {.dbl=0.0000001}, 0,  1, FLAGS },
    { NULL },
};

FRAMESYNC_DEFINE_PURE_CLASS(deconvolve, "deconvolve", convolve, deconvolve_options);

const AVFilter ff_vf_deconvolve = {
    .name          = "deconvolve",
    .description   = NULL_IF_CONFIG_SMALL("Deconvolve first video stream with second video stream."),
    .preinit       = convolve_framesync_preinit,
    .init          = init,
    .uninit        = uninit,
    .activate      = activate,
    .priv_size     = sizeof(ConvolveContext),
    .priv_class    = &deconvolve_class,
    FILTER_INPUTS(convolve_inputs),
    FILTER_OUTPUTS(convolve_outputs),
    FILTER_PIXFMTS_ARRAY(pixel_fmts_fftfilt),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL | AVFILTER_FLAG_SLICE_THREADS,
};

#endif /* CONFIG_DECONVOLVE_FILTER */

#if CONFIG_XCORRELATE_FILTER

static const AVOption xcorrelate_options[] = {
    { "planes",  "set planes to cross-correlate",     OFFSET(planes),   AV_OPT_TYPE_INT,   {.i64=7}, 0, 15, FLAGS },
    { "secondary", "when to process secondary frame", OFFSET(impulse),  AV_OPT_TYPE_INT,   {.i64=1}, 0,  1, FLAGS, .unit = "impulse" },
    {   "first", "process only first secondary frame, ignore rest", 0,  AV_OPT_TYPE_CONST, {.i64=0}, 0,  0, FLAGS, .unit = "impulse" },
    {   "all",   "process all secondary frames",                    0,  AV_OPT_TYPE_CONST, {.i64=1}, 0,  0, FLAGS, .unit = "impulse" },
    { NULL },
};

FRAMESYNC_DEFINE_PURE_CLASS(xcorrelate, "xcorrelate", convolve, xcorrelate_options);

static int config_input_secondary(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;

    if (ctx->inputs[0]->w <= ctx->inputs[1]->w ||
        ctx->inputs[0]->h <= ctx->inputs[1]->h) {
        av_log(ctx, AV_LOG_ERROR, "Width and height of second input videos must be less than first input.\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static const AVFilterPad xcorrelate_inputs[] = {
    {
        .name          = "primary",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_input,
    },{
        .name          = "secondary",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_input_secondary,
    },
};

#define xcorrelate_outputs convolve_outputs

const AVFilter ff_vf_xcorrelate = {
    .name          = "xcorrelate",
    .description   = NULL_IF_CONFIG_SMALL("Cross-correlate first video stream with second video stream."),
    .preinit       = convolve_framesync_preinit,
    .init          = init,
    .uninit        = uninit,
    .activate      = activate,
    .priv_size     = sizeof(ConvolveContext),
    .priv_class    = &xcorrelate_class,
    FILTER_INPUTS(xcorrelate_inputs),
    FILTER_OUTPUTS(xcorrelate_outputs),
    FILTER_PIXFMTS_ARRAY(pixel_fmts_fftfilt),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL | AVFILTER_FLAG_SLICE_THREADS,
};

#endif /* CONFIG_XCORRELATE_FILTER */
