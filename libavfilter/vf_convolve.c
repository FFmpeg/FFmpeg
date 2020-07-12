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

#include <float.h>

#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavcodec/avfft.h"

#include "avfilter.h"
#include "formats.h"
#include "framesync.h"
#include "internal.h"
#include "video.h"

#define MAX_THREADS 16

typedef struct ConvolveContext {
    const AVClass *class;
    FFFrameSync fs;

    FFTContext *fft[4][MAX_THREADS];
    FFTContext *ifft[4][MAX_THREADS];

    int fft_bits[4];
    int fft_len[4];
    int planewidth[4];
    int planeheight[4];

    FFTComplex *fft_hdata[4];
    FFTComplex *fft_vdata[4];
    FFTComplex *fft_hdata_impulse[4];
    FFTComplex *fft_vdata_impulse[4];

    int depth;
    int planes;
    int impulse;
    float noise;
    int nb_planes;
    int got_impulse[4];

    int (*filter)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} ConvolveContext;

#define OFFSET(x) offsetof(ConvolveContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption convolve_options[] = {
    { "planes",  "set planes to convolve",                  OFFSET(planes),   AV_OPT_TYPE_INT,   {.i64=7}, 0, 15, FLAGS },
    { "impulse", "when to process impulses",                OFFSET(impulse),  AV_OPT_TYPE_INT,   {.i64=1}, 0,  1, FLAGS, "impulse" },
    {   "first", "process only first impulse, ignore rest", 0,                AV_OPT_TYPE_CONST, {.i64=0}, 0,  0, FLAGS, "impulse" },
    {   "all",   "process all impulses",                    0,                AV_OPT_TYPE_CONST, {.i64=1}, 0,  0, FLAGS, "impulse" },
    { "noise",   "set noise",                               OFFSET(noise),    AV_OPT_TYPE_FLOAT, {.dbl=0.0000001}, 0,  1, FLAGS },
    { NULL },
};

static int query_formats(AVFilterContext *ctx)
{
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

    AVFilterFormats *fmts_list = ff_make_format_list(pixel_fmts_fftfilt);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int config_input_main(AVFilterLink *inlink)
{
    ConvolveContext *s = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int fft_bits, i;

    s->planewidth[1] = s->planewidth[2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0] = s->planewidth[3] = inlink->w;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->nb_planes = desc->nb_components;
    s->depth = desc->comp[0].depth;

    for (i = 0; i < s->nb_planes; i++) {
        int w = s->planewidth[i];
        int h = s->planeheight[i];
        int n = FFMAX(w, h);

        for (fft_bits = 1; 1 << fft_bits < n; fft_bits++);

        s->fft_bits[i] = fft_bits;
        s->fft_len[i] = 1 << s->fft_bits[i];

        if (!(s->fft_hdata[i] = av_calloc(s->fft_len[i], s->fft_len[i] * sizeof(FFTComplex))))
            return AVERROR(ENOMEM);

        if (!(s->fft_vdata[i] = av_calloc(s->fft_len[i], s->fft_len[i] * sizeof(FFTComplex))))
            return AVERROR(ENOMEM);

        if (!(s->fft_hdata_impulse[i] = av_calloc(s->fft_len[i], s->fft_len[i] * sizeof(FFTComplex))))
            return AVERROR(ENOMEM);

        if (!(s->fft_vdata_impulse[i] = av_calloc(s->fft_len[i], s->fft_len[i] * sizeof(FFTComplex))))
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
    if (ctx->inputs[0]->format != ctx->inputs[1]->format) {
        av_log(ctx, AV_LOG_ERROR, "Inputs must be of same pixel format.\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

typedef struct ThreadData {
    FFTComplex *hdata, *vdata;
    int plane, n;
} ThreadData;

static int fft_horizontal(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ConvolveContext *s = ctx->priv;
    ThreadData *td = arg;
    FFTComplex *hdata = td->hdata;
    const int plane = td->plane;
    const int n = td->n;
    int start = (n * jobnr) / nb_jobs;
    int end = (n * (jobnr+1)) / nb_jobs;
    int y;

    for (y = start; y < end; y++) {
        av_fft_permute(s->fft[plane][jobnr], hdata + y * n);
        av_fft_calc(s->fft[plane][jobnr], hdata + y * n);
    }

    return 0;
}

static void get_input(ConvolveContext *s, FFTComplex *fft_hdata,
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
    FFTComplex *hdata = td->hdata;
    FFTComplex *vdata = td->vdata;
    const int plane = td->plane;
    const int n = td->n;
    int start = (n * jobnr) / nb_jobs;
    int end = (n * (jobnr+1)) / nb_jobs;
    int y, x;

    for (y = start; y < end; y++) {
        for (x = 0; x < n; x++) {
            vdata[y * n + x].re = hdata[x * n + y].re;
            vdata[y * n + x].im = hdata[x * n + y].im;
        }

        av_fft_permute(s->fft[plane][jobnr], vdata + y * n);
        av_fft_calc(s->fft[plane][jobnr], vdata + y * n);
    }

    return 0;
}

static int ifft_vertical(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ConvolveContext *s = ctx->priv;
    ThreadData *td = arg;
    FFTComplex *hdata = td->hdata;
    FFTComplex *vdata = td->vdata;
    const int plane = td->plane;
    const int n = td->n;
    int start = (n * jobnr) / nb_jobs;
    int end = (n * (jobnr+1)) / nb_jobs;
    int y, x;

    for (y = start; y < end; y++) {
        av_fft_permute(s->ifft[plane][jobnr], vdata + y * n);
        av_fft_calc(s->ifft[plane][jobnr], vdata + y * n);

        for (x = 0; x < n; x++) {
            hdata[x * n + y].re = vdata[y * n + x].re;
            hdata[x * n + y].im = vdata[y * n + x].im;
        }
    }

    return 0;
}

static int ifft_horizontal(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ConvolveContext *s = ctx->priv;
    ThreadData *td = arg;
    FFTComplex *hdata = td->hdata;
    const int plane = td->plane;
    const int n = td->n;
    int start = (n * jobnr) / nb_jobs;
    int end = (n * (jobnr+1)) / nb_jobs;
    int y;

    for (y = start; y < end; y++) {
        av_fft_permute(s->ifft[plane][jobnr], hdata + y * n);
        av_fft_calc(s->ifft[plane][jobnr], hdata + y * n);
    }

    return 0;
}

static void get_output(ConvolveContext *s, FFTComplex *input, AVFrame *out,
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
    FFTComplex *input = td->hdata;
    FFTComplex *filter = td->vdata;
    const float noise = s->noise;
    const int n = td->n;
    int start = (n * jobnr) / nb_jobs;
    int end = (n * (jobnr+1)) / nb_jobs;
    int y, x;

    for (y = start; y < end; y++) {
        int yn = y * n;

        for (x = 0; x < n; x++) {
            FFTSample re, im, ire, iim;

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

static int complex_divide(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ConvolveContext *s = ctx->priv;
    ThreadData *td = arg;
    FFTComplex *input = td->hdata;
    FFTComplex *filter = td->vdata;
    const float noise = s->noise;
    const int n = td->n;
    int start = (n * jobnr) / nb_jobs;
    int end = (n * (jobnr+1)) / nb_jobs;
    int y, x;

    for (y = start; y < end; y++) {
        int yn = y * n;

        for (x = 0; x < n; x++) {
            FFTSample re, im, ire, iim, div;

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

static int do_convolve(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    AVFilterLink *outlink = ctx->outputs[0];
    ConvolveContext *s = ctx->priv;
    AVFrame *mainpic = NULL, *impulsepic = NULL;
    int ret, y, x, plane;

    ret = ff_framesync_dualinput_get(fs, &mainpic, &impulsepic);
    if (ret < 0)
        return ret;
    if (!impulsepic)
        return ff_filter_frame(outlink, mainpic);

    for (plane = 0; plane < s->nb_planes; plane++) {
        FFTComplex *filter = s->fft_vdata_impulse[plane];
        FFTComplex *input = s->fft_vdata[plane];
        const int n = s->fft_len[plane];
        const int w = s->planewidth[plane];
        const int h = s->planeheight[plane];
        float total = 0;
        ThreadData td;

        if (!(s->planes & (1 << plane))) {
            continue;
        }

        td.plane = plane, td.n = n;
        get_input(s, s->fft_hdata[plane], mainpic, w, h, n, plane, 1.f);

        td.hdata = s->fft_hdata[plane];
        td.vdata = s->fft_vdata[plane];

        ctx->internal->execute(ctx, fft_horizontal, &td, NULL, FFMIN3(MAX_THREADS, n, ff_filter_get_nb_threads(ctx)));
        ctx->internal->execute(ctx, fft_vertical, &td, NULL, FFMIN3(MAX_THREADS, n, ff_filter_get_nb_threads(ctx)));

        if ((!s->impulse && !s->got_impulse[plane]) || s->impulse) {
            if (s->depth == 8) {
                for (y = 0; y < h; y++) {
                    const uint8_t *src = (const uint8_t *)(impulsepic->data[plane] + y * impulsepic->linesize[plane]) ;
                    for (x = 0; x < w; x++) {
                        total += src[x];
                    }
                }
            } else {
                for (y = 0; y < h; y++) {
                    const uint16_t *src = (const uint16_t *)(impulsepic->data[plane] + y * impulsepic->linesize[plane]) ;
                    for (x = 0; x < w; x++) {
                        total += src[x];
                    }
                }
            }
            total = FFMAX(1, total);

            get_input(s, s->fft_hdata_impulse[plane], impulsepic, w, h, n, plane, 1.f / total);

            td.hdata = s->fft_hdata_impulse[plane];
            td.vdata = s->fft_vdata_impulse[plane];

            ctx->internal->execute(ctx, fft_horizontal, &td, NULL, FFMIN3(MAX_THREADS, n, ff_filter_get_nb_threads(ctx)));
            ctx->internal->execute(ctx, fft_vertical, &td, NULL, FFMIN3(MAX_THREADS, n, ff_filter_get_nb_threads(ctx)));

            s->got_impulse[plane] = 1;
        }

        td.hdata = input;
        td.vdata = filter;

        ctx->internal->execute(ctx, s->filter, &td, NULL, FFMIN3(MAX_THREADS, n, ff_filter_get_nb_threads(ctx)));

        td.hdata = s->fft_hdata[plane];
        td.vdata = s->fft_vdata[plane];

        ctx->internal->execute(ctx, ifft_vertical, &td, NULL, FFMIN3(MAX_THREADS, n, ff_filter_get_nb_threads(ctx)));
        ctx->internal->execute(ctx, ifft_horizontal, &td, NULL, FFMIN3(MAX_THREADS, n, ff_filter_get_nb_threads(ctx)));

        get_output(s, s->fft_hdata[plane], mainpic, w, h, n, plane, 1.f / (n * n));
    }

    return ff_filter_frame(outlink, mainpic);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    ConvolveContext *s = ctx->priv;
    AVFilterLink *mainlink = ctx->inputs[0];
    int ret, i, j;

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
            s->fft[i][j]  = av_fft_init(s->fft_bits[i], 0);
            s->ifft[i][j] = av_fft_init(s->fft_bits[i], 1);
            if (!s->fft[i][j] || !s->ifft[i][j])
                return AVERROR(ENOMEM);
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
    } else if (!strcmp(ctx->filter->name, "deconvolve")) {
        s->filter = complex_divide;
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
        av_freep(&s->fft_hdata[i]);
        av_freep(&s->fft_vdata[i]);
        av_freep(&s->fft_hdata_impulse[i]);
        av_freep(&s->fft_vdata_impulse[i]);

        for (j = 0; j < MAX_THREADS; j++) {
            av_fft_end(s->fft[i][j]);
            s->fft[i][j] = NULL;
            av_fft_end(s->ifft[i][j]);
            s->ifft[i][j] = NULL;
        }
    }

    ff_framesync_uninit(&s->fs);
}

static const AVFilterPad convolve_inputs[] = {
    {
        .name          = "main",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_input_main,
    },{
        .name          = "impulse",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_input_impulse,
    },
    { NULL }
};

static const AVFilterPad convolve_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
    { NULL }
};

#if CONFIG_CONVOLVE_FILTER

FRAMESYNC_DEFINE_CLASS(convolve, ConvolveContext, fs);

AVFilter ff_vf_convolve = {
    .name          = "convolve",
    .description   = NULL_IF_CONFIG_SMALL("Convolve first video stream with second video stream."),
    .preinit       = convolve_framesync_preinit,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .activate      = activate,
    .priv_size     = sizeof(ConvolveContext),
    .priv_class    = &convolve_class,
    .inputs        = convolve_inputs,
    .outputs       = convolve_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL | AVFILTER_FLAG_SLICE_THREADS,
};

#endif /* CONFIG_CONVOLVE_FILTER */

#if CONFIG_DECONVOLVE_FILTER

static const AVOption deconvolve_options[] = {
    { "planes",  "set planes to deconvolve",                OFFSET(planes),   AV_OPT_TYPE_INT,   {.i64=7}, 0, 15, FLAGS },
    { "impulse", "when to process impulses",                OFFSET(impulse),  AV_OPT_TYPE_INT,   {.i64=1}, 0,  1, FLAGS, "impulse" },
    {   "first", "process only first impulse, ignore rest", 0,                AV_OPT_TYPE_CONST, {.i64=0}, 0,  0, FLAGS, "impulse" },
    {   "all",   "process all impulses",                    0,                AV_OPT_TYPE_CONST, {.i64=1}, 0,  0, FLAGS, "impulse" },
    { "noise",   "set noise",                               OFFSET(noise),    AV_OPT_TYPE_FLOAT, {.dbl=0.0000001}, 0,  1, FLAGS },
    { NULL },
};

FRAMESYNC_DEFINE_CLASS(deconvolve, ConvolveContext, fs);

AVFilter ff_vf_deconvolve = {
    .name          = "deconvolve",
    .description   = NULL_IF_CONFIG_SMALL("Deconvolve first video stream with second video stream."),
    .preinit       = deconvolve_framesync_preinit,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .activate      = activate,
    .priv_size     = sizeof(ConvolveContext),
    .priv_class    = &deconvolve_class,
    .inputs        = convolve_inputs,
    .outputs       = convolve_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL | AVFILTER_FLAG_SLICE_THREADS,
};

#endif /* CONFIG_DECONVOLVE_FILTER */
