/*
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

#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/tx.h"
#include "internal.h"
#include "window_func.h"

#define MAX_BLOCK 256
#define MAX_THREADS 32

enum BufferTypes {
    CURRENT,
    PREV,
    NEXT,
    BSIZE
};

typedef struct PlaneContext {
    int planewidth, planeheight;
    int nox, noy;
    int b;
    int o;
    float n;

    float *buffer[MAX_THREADS][BSIZE];
    AVComplexFloat *hdata[MAX_THREADS], *vdata[MAX_THREADS];
    AVComplexFloat *hdata_out[MAX_THREADS], *vdata_out[MAX_THREADS];
    int data_linesize;
    int buffer_linesize;
} PlaneContext;

typedef struct FFTdnoizContext {
    const AVClass *class;

    float sigma;
    float amount;
    int   block_size;
    float overlap;
    int   method;
    int   window;
    int   nb_prev;
    int   nb_next;
    int   planesf;

    AVFrame *prev, *cur, *next;

    int depth;
    int nb_planes;
    int nb_threads;
    PlaneContext planes[4];
    float win[MAX_BLOCK][MAX_BLOCK];

    AVTXContext *fft[MAX_THREADS], *ifft[MAX_THREADS];
    AVTXContext *fft_r[MAX_THREADS], *ifft_r[MAX_THREADS];

    av_tx_fn tx_fn, itx_fn;
    av_tx_fn tx_r_fn, itx_r_fn;

    void (*import_row)(AVComplexFloat *dst, uint8_t *src, int rw, float scale, float *win, int off);
    void (*export_row)(AVComplexFloat *src, uint8_t *dst, int rw, int depth, float *win);
} FFTdnoizContext;

#define OFFSET(x) offsetof(FFTdnoizContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
#define TFLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM
static const AVOption fftdnoiz_options[] = {
    { "sigma",   "set denoise strength",
        OFFSET(sigma),      AV_OPT_TYPE_FLOAT, {.dbl=1},        0, 100, .flags = TFLAGS },
    { "amount",  "set amount of denoising",
        OFFSET(amount),     AV_OPT_TYPE_FLOAT, {.dbl=1},     0.01,   1, .flags = TFLAGS },
    { "block",   "set block size",
        OFFSET(block_size), AV_OPT_TYPE_INT,   {.i64=32}, 8, MAX_BLOCK, .flags = FLAGS },
    { "overlap", "set block overlap",
        OFFSET(overlap),    AV_OPT_TYPE_FLOAT, {.dbl=0.5},    0.2, 0.8, .flags = FLAGS },
    { "method",  "set method of denoising",
        OFFSET(method),     AV_OPT_TYPE_INT,   {.i64=0},        0,   1, .flags = TFLAGS, "method" },
    { "wiener", "wiener method",
        0,                  AV_OPT_TYPE_CONST, {.i64=0},        0,   0, .flags = TFLAGS, "method" },
    { "hard",   "hard thresholding",
        0,                  AV_OPT_TYPE_CONST, {.i64=1},        0,   0, .flags = TFLAGS, "method" },
    { "prev",    "set number of previous frames for temporal denoising",
        OFFSET(nb_prev),    AV_OPT_TYPE_INT,   {.i64=0},        0,   1, .flags = FLAGS },
    { "next",    "set number of next frames for temporal denoising",
        OFFSET(nb_next),    AV_OPT_TYPE_INT,   {.i64=0},        0,   1, .flags = FLAGS },
    { "planes",  "set planes to filter",
        OFFSET(planesf),    AV_OPT_TYPE_INT,   {.i64=7},        0,  15, .flags = TFLAGS },
    WIN_FUNC_OPTION("window", OFFSET(window), FLAGS, WFUNC_HANNING),
    { NULL }
};

AVFILTER_DEFINE_CLASS(fftdnoiz);

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9,
    AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12,
    AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
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

typedef struct ThreadData {
    float *src, *dst;
} ThreadData;

static void import_row8(AVComplexFloat *dst, uint8_t *src, int rw,
                        float scale, float *win, int off)
{
    for (int j = 0; j < rw; j++) {
        const int i = abs(j + off);
        dst[j].re = src[i] * scale * win[j];
        dst[j].im = 0.f;
    }
}

static void export_row8(AVComplexFloat *src, uint8_t *dst, int rw, int depth, float *win)
{
    for (int j = 0; j < rw; j++)
        dst[j] = av_clip_uint8(lrintf(src[j].re / win[j]));
}

static void import_row16(AVComplexFloat *dst, uint8_t *srcp, int rw,
                         float scale, float *win, int off)
{
    uint16_t *src = (uint16_t *)srcp;

    for (int j = 0; j < rw; j++) {
        const int i = abs(j + off);
        dst[j].re = src[i] * scale * win[j];
        dst[j].im = 0;
    }
}

static void export_row16(AVComplexFloat *src, uint8_t *dstp, int rw, int depth, float *win)
{
    uint16_t *dst = (uint16_t *)dstp;

    for (int j = 0; j < rw; j++)
        dst[j] = av_clip_uintp2_c(lrintf(src[j].re / win[j]), depth);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    const AVPixFmtDescriptor *desc;
    FFTdnoizContext *s = ctx->priv;
    float lut[MAX_BLOCK + 1];
    float overlap;
    int i;

    desc = av_pix_fmt_desc_get(inlink->format);
    s->depth = desc->comp[0].depth;

    if (s->depth <= 8) {
        s->import_row = import_row8;
        s->export_row = export_row8;
    } else {
        s->import_row = import_row16;
        s->export_row = export_row16;
    }

    s->planes[1].planewidth = s->planes[2].planewidth = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planes[0].planewidth = s->planes[3].planewidth = inlink->w;
    s->planes[1].planeheight = s->planes[2].planeheight = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planes[0].planeheight = s->planes[3].planeheight = inlink->h;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);
    s->nb_threads = FFMIN(ff_filter_get_nb_threads(ctx), MAX_THREADS);

    for (int i = 0; i < s->nb_threads; i++) {
        float scale = 1.f, iscale = 1.f;
        int ret;

        if ((ret = av_tx_init(&s->fft[i],    &s->tx_fn,    AV_TX_FLOAT_FFT,
                              0, s->block_size,               &scale,  0)) < 0 ||
            (ret = av_tx_init(&s->ifft[i],   &s->itx_fn,   AV_TX_FLOAT_FFT,
                              1, s->block_size,               &iscale, 0)) < 0 ||
            (ret = av_tx_init(&s->fft_r[i],  &s->tx_r_fn,  AV_TX_FLOAT_FFT,
                              0, 1 + s->nb_prev + s->nb_next, &scale,  0)) < 0 ||
            (ret = av_tx_init(&s->ifft_r[i], &s->itx_r_fn, AV_TX_FLOAT_FFT,
                              1, 1 + s->nb_prev + s->nb_next, &iscale, 0)) < 0)
            return ret;
    }

    for (i = 0; i < s->nb_planes; i++) {
        PlaneContext *p = &s->planes[i];
        int size;

        p->b = s->block_size;
        p->n = 1.f / (p->b * p->b);
        p->o = lrintf(p->b * s->overlap);
        size = p->b - p->o;
        p->nox = (p->planewidth  + (size - 1)) / size;
        p->noy = (p->planeheight + (size - 1)) / size;

        av_log(ctx, AV_LOG_DEBUG, "nox:%d noy:%d size:%d\n", p->nox, p->noy, size);

        p->buffer_linesize = p->b * sizeof(AVComplexFloat);
        p->data_linesize = 2 * p->b * sizeof(float);
        for (int j = 0; j < s->nb_threads; j++) {
            p->hdata[j] = av_calloc(p->b, p->data_linesize);
            p->hdata_out[j] = av_calloc(p->b, p->data_linesize);
            p->vdata[j] = av_calloc(p->b, p->data_linesize);
            p->vdata_out[j] = av_calloc(p->b, p->data_linesize);
            p->buffer[j][CURRENT] = av_calloc(p->b, p->buffer_linesize);
            if (!p->buffer[j][CURRENT])
                return AVERROR(ENOMEM);
            if (s->nb_prev > 0) {
                p->buffer[j][PREV] = av_calloc(p->b, p->buffer_linesize);
                if (!p->buffer[j][PREV])
                    return AVERROR(ENOMEM);
            }
            if (s->nb_next > 0) {
                p->buffer[j][NEXT] = av_calloc(p->b, p->buffer_linesize);
                if (!p->buffer[j][NEXT])
                    return AVERROR(ENOMEM);
            }
            if (!p->hdata[j] || !p->vdata[j] ||
                !p->hdata_out[j] || !p->vdata_out[j])
                return AVERROR(ENOMEM);
        }
    }

    generate_window_func(lut, s->block_size + 1, s->window, &overlap);

    for (int y = 0; y < s->block_size; y++) {
        for (int x = 0; x < s->block_size; x++)
            s->win[y][x] = lut[y] * lut[x];
    }

    return 0;
}

static void import_block(FFTdnoizContext *s,
                         uint8_t *srcp, int src_linesize,
                         float *buffer, int buffer_linesize, int plane,
                         int jobnr, int y, int x)
{
    PlaneContext *p = &s->planes[plane];
    const int width = p->planewidth;
    const int height = p->planeheight;
    const int block = p->b;
    const int overlap = p->o;
    const int hoverlap = overlap / 2;
    const int size = block - overlap;
    const int bpp = (s->depth + 7) / 8;
    const int data_linesize = p->data_linesize / sizeof(AVComplexFloat);
    const float scale = 1.f / ((1.f + s->nb_prev + s->nb_next) * s->block_size * s->block_size);
    AVComplexFloat *hdata = p->hdata[jobnr];
    AVComplexFloat *hdata_out = p->hdata_out[jobnr];
    AVComplexFloat *vdata_out = p->vdata_out[jobnr];
    const int woff = -hoverlap;
    const int hoff = -hoverlap;
    const int rh = FFMIN(block, height - y * size + hoverlap);
    const int rw = FFMIN(block, width  - x * size + hoverlap);
    AVComplexFloat *ssrc, *ddst, *dst = hdata, *dst_out = hdata_out;
    float *bdst = buffer;

    buffer_linesize /= sizeof(float);

    for (int i = 0; i < rh; i++) {
        uint8_t *src = srcp + src_linesize * abs(y * size + i + hoff) + x * size * bpp;

        s->import_row(dst, src, rw, scale, s->win[i], woff);
        for (int j = rw; j < block; j++) {
            dst[j].re = dst[rw - 1].re;
            dst[j].im = 0.f;
        }
        s->tx_fn(s->fft[jobnr], dst_out, dst, sizeof(AVComplexFloat));

        ddst = dst_out;
        dst += data_linesize;
        dst_out += data_linesize;
    }

    dst = dst_out;
    for (int i = rh; i < block; i++) {
        for (int j = 0; j < block; j++) {
            dst[j].re = ddst[j].re;
            dst[j].im = ddst[j].im;
        }

        dst += data_linesize;
    }

    ssrc = hdata_out;
    dst = vdata_out;
    for (int i = 0; i < block; i++) {
        for (int j = 0; j < block; j++)
            dst[j] = ssrc[j * data_linesize + i];
        s->tx_fn(s->fft[jobnr], bdst, dst, sizeof(AVComplexFloat));

        dst += data_linesize;
        bdst += buffer_linesize;
    }
}

static void export_block(FFTdnoizContext *s,
                         uint8_t *dstp, int dst_linesize,
                         float *buffer, int buffer_linesize, int plane,
                         int jobnr, int y, int x)
{
    PlaneContext *p = &s->planes[plane];
    const int depth = s->depth;
    const int bpp = (depth + 7) / 8;
    const int width = p->planewidth;
    const int height = p->planeheight;
    const int block = p->b;
    const int overlap = p->o;
    const int hoverlap = overlap / 2;
    const int size = block - overlap;
    const int data_linesize = p->data_linesize / sizeof(AVComplexFloat);
    AVComplexFloat *hdata = p->hdata[jobnr];
    AVComplexFloat *hdata_out = p->hdata_out[jobnr];
    AVComplexFloat *vdata_out = p->vdata_out[jobnr];
    const int rw = FFMIN(size, width  - x * size);
    const int rh = FFMIN(size, height - y * size);
    AVComplexFloat *hdst, *vdst = vdata_out, *hdst_out = hdata_out;
    float *bsrc = buffer;

    hdst = hdata;
    buffer_linesize /= sizeof(float);

    for (int i = 0; i < block; i++) {
        s->itx_fn(s->ifft[jobnr], vdst, bsrc, sizeof(AVComplexFloat));
        for (int j = 0; j < block; j++)
            hdst[j * data_linesize + i] = vdst[j];

        vdst += data_linesize;
        bsrc += buffer_linesize;
    }

    hdst = hdata + hoverlap * data_linesize;
    for (int i = 0; i < rh && (y * size + i) < height; i++) {
        uint8_t *dst = dstp + dst_linesize * (y * size + i) + x * size * bpp;

        s->itx_fn(s->ifft[jobnr], hdst_out, hdst, sizeof(AVComplexFloat));
        s->export_row(hdst_out + hoverlap, dst, rw, depth, s->win[i + hoverlap] + hoverlap);

        hdst += data_linesize;
        hdst_out += data_linesize;
    }
}

static void filter_block3d2(FFTdnoizContext *s, int plane, float *pbuffer, float *nbuffer,
                            int jobnr)
{
    PlaneContext *p = &s->planes[plane];
    const int block = p->b;
    const int buffer_linesize = p->buffer_linesize / sizeof(float);
    const float depthx = (1 << (s->depth - 8)) * (1 << (s->depth - 8));
    const float sigma = s->sigma * depthx / (3.f * s->block_size * s->block_size);
    const float limit = 1.f - s->amount;
    float *cbuffer = p->buffer[jobnr][CURRENT];
    const int method = s->method;
    float *cbuff = cbuffer;
    float *pbuff = pbuffer;
    float *nbuff = nbuffer;

    for (int i = 0; i < block; i++) {
        for (int j = 0; j < block; j++) {
            AVComplexFloat buffer[BSIZE];
            AVComplexFloat outbuffer[BSIZE];

            buffer[0].re = pbuff[2 * j    ];
            buffer[0].im = pbuff[2 * j + 1];

            buffer[1].re = cbuff[2 * j    ];
            buffer[1].im = cbuff[2 * j + 1];

            buffer[2].re = nbuff[2 * j    ];
            buffer[2].im = nbuff[2 * j + 1];

            s->tx_r_fn(s->fft_r[jobnr], outbuffer, buffer, sizeof(AVComplexFloat));

            for (int z = 0; z < 3; z++) {
                const float re = outbuffer[z].re;
                const float im = outbuffer[z].im;
                const float power = re * re + im * im;
                float factor;

                switch (method) {
                case 0:
                    factor = fmaxf(limit, (power - sigma) / (power + 1e-15f));
                    break;
                case 1:
                    factor = power < sigma ? limit : 1.f;
                    break;
                }

                outbuffer[z].re *= factor;
                outbuffer[z].im *= factor;
            }

            s->itx_r_fn(s->ifft_r[jobnr], buffer, outbuffer, sizeof(AVComplexFloat));

            cbuff[2 * j + 0] = buffer[1].re;
            cbuff[2 * j + 1] = buffer[1].im;
        }

        cbuff += buffer_linesize;
        pbuff += buffer_linesize;
        nbuff += buffer_linesize;
    }
}

static void filter_block3d1(FFTdnoizContext *s, int plane, float *pbuffer,
                            int jobnr)
{
    PlaneContext *p = &s->planes[plane];
    const int block = p->b;
    const int buffer_linesize = p->buffer_linesize / sizeof(float);
    const float depthx = (1 << (s->depth - 8)) * (1 << (s->depth - 8));
    const float sigma = s->sigma * depthx / (2.f * s->block_size * s->block_size);
    const float limit = 1.f - s->amount;
    float *cbuffer = p->buffer[jobnr][CURRENT];
    const int method = s->method;
    float *cbuff = cbuffer;
    float *pbuff = pbuffer;

    for (int i = 0; i < block; i++) {
        for (int j = 0; j < block; j++) {
            AVComplexFloat buffer[BSIZE];
            AVComplexFloat outbuffer[BSIZE];

            buffer[0].re = pbuff[2 * j    ];
            buffer[0].im = pbuff[2 * j + 1];

            buffer[1].re = cbuff[2 * j    ];
            buffer[1].im = cbuff[2 * j + 1];

            s->tx_r_fn(s->fft_r[jobnr], outbuffer, buffer, sizeof(AVComplexFloat));

            for (int z = 0; z < 2; z++) {
                const float re = outbuffer[z].re;
                const float im = outbuffer[z].im;
                const float power = re * re + im * im;
                float factor;

                switch (method) {
                case 0:
                    factor = fmaxf(limit, (power - sigma) / (power + 1e-15f));
                    break;
                case 1:
                    factor = power < sigma ? limit : 1.f;
                    break;
                }

                outbuffer[z].re *= factor;
                outbuffer[z].im *= factor;
            }

            s->itx_r_fn(s->ifft_r[jobnr], buffer, outbuffer, sizeof(AVComplexFloat));

            cbuff[2 * j + 0] = buffer[1].re;
            cbuff[2 * j + 1] = buffer[1].im;
        }

        cbuff += buffer_linesize;
        pbuff += buffer_linesize;
    }
}

static void filter_block2d(FFTdnoizContext *s, int plane,
                           int jobnr)
{
    PlaneContext *p = &s->planes[plane];
    const int block = p->b;
    const int method = s->method;
    const int buffer_linesize = p->buffer_linesize / sizeof(float);
    const float depthx = (1 << (s->depth - 8)) * (1 << (s->depth - 8));
    const float sigma = s->sigma * depthx / (s->block_size * s->block_size);
    const float limit = 1.f - s->amount;
    float *buff = p->buffer[jobnr][CURRENT];

    for (int i = 0; i < block; i++) {
        for (int j = 0; j < block; j++) {
            float factor, power, re, im;

            re = buff[j * 2    ];
            im = buff[j * 2 + 1];
            power = re * re + im * im;
            switch (method) {
            case 0:
                factor = fmaxf(limit, (power - sigma) / (power + 1e-15f));
                break;
            case 1:
                factor = power < sigma ? limit : 1.f;
                break;
            }

            buff[j * 2    ] *= factor;
            buff[j * 2 + 1] *= factor;
        }

        buff += buffer_linesize;
    }
}

static int denoise(AVFilterContext *ctx, void *arg,
                   int jobnr, int nb_jobs)
{
    FFTdnoizContext *s = ctx->priv;
    AVFrame *out = arg;

    for (int plane = 0; plane < s->nb_planes; plane++) {
        PlaneContext *p = &s->planes[plane];
        const int nox = p->nox;
        const int noy = p->noy;
        const int slice_start = (noy * jobnr) / nb_jobs;
        const int slice_end = (noy * (jobnr+1)) / nb_jobs;

        if (!((1 << plane) & s->planesf) || ctx->is_disabled)
            continue;

        for (int y = slice_start; y < slice_end; y++) {
            for (int x = 0; x < nox; x++) {
                if (s->next) {
                    import_block(s, s->next->data[plane], s->next->linesize[plane],
                                 p->buffer[jobnr][NEXT], p->buffer_linesize, plane,
                                 jobnr, y, x);
                }

                if (s->prev) {
                    import_block(s, s->prev->data[plane], s->prev->linesize[plane],
                                 p->buffer[jobnr][PREV], p->buffer_linesize, plane,
                                 jobnr, y, x);
                }

                import_block(s, s->cur->data[plane], s->cur->linesize[plane],
                             p->buffer[jobnr][CURRENT], p->buffer_linesize, plane,
                             jobnr, y, x);

                if (s->next && s->prev) {
                    filter_block3d2(s, plane, p->buffer[jobnr][PREV], p->buffer[jobnr][NEXT], jobnr);
                } else if (s->next) {
                    filter_block3d1(s, plane, p->buffer[jobnr][NEXT], jobnr);
                } else  if (s->prev) {
                    filter_block3d1(s, plane, p->buffer[jobnr][PREV], jobnr);
                } else {
                    filter_block2d(s, plane, jobnr);
                }

                export_block(s, out->data[plane], out->linesize[plane],
                             p->buffer[jobnr][CURRENT], p->buffer_linesize, plane,
                             jobnr, y, x);
            }
        }
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    FFTdnoizContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int direct, plane;
    AVFrame *out;

    if (s->nb_next > 0 && s->nb_prev > 0) {
        av_frame_free(&s->prev);
        s->prev = s->cur;
        s->cur = s->next;
        s->next = in;

        if (!s->prev && s->cur) {
            s->prev = av_frame_clone(s->cur);
            if (!s->prev)
                return AVERROR(ENOMEM);
        }
        if (!s->cur)
            return 0;
    } else if (s->nb_next > 0) {
        av_frame_free(&s->cur);
        s->cur = s->next;
        s->next = in;

        if (!s->cur)
            return 0;
    } else if (s->nb_prev > 0) {
        av_frame_free(&s->prev);
        s->prev = s->cur;
        s->cur = in;

        if (!s->prev)
            s->prev = av_frame_clone(s->cur);
        if (!s->prev)
            return AVERROR(ENOMEM);
    } else {
        s->cur = in;
    }

    if (av_frame_is_writable(in) && s->nb_next == 0 && s->nb_prev == 0) {
        direct = 1;
        out = in;
    } else {
        direct = 0;
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out)
            return AVERROR(ENOMEM);
        av_frame_copy_props(out, s->cur);
    }

    ff_filter_execute(ctx, denoise, out, NULL,
                      FFMIN(s->planes[0].noy, s->nb_threads));

    for (plane = 0; plane < s->nb_planes; plane++) {
        PlaneContext *p = &s->planes[plane];

        if (!((1 << plane) & s->planesf) || ctx->is_disabled) {
            if (!direct)
                av_image_copy_plane(out->data[plane], out->linesize[plane],
                                    s->cur->data[plane], s->cur->linesize[plane],
                                    p->planewidth * (1 + (s->depth > 8)), p->planeheight);
            continue;
        }
    }

    if (s->nb_next == 0 && s->nb_prev == 0) {
        if (direct) {
            s->cur = NULL;
        } else {
            av_frame_free(&s->cur);
        }
    }
    return ff_filter_frame(outlink, out);
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    FFTdnoizContext *s = ctx->priv;
    int ret = 0;

    ret = ff_request_frame(ctx->inputs[0]);

    if (ret == AVERROR_EOF && (s->nb_next > 0)) {
        AVFrame *buf;

        if (s->next && s->nb_next > 0)
            buf = av_frame_clone(s->next);
        else if (s->cur)
            buf = av_frame_clone(s->cur);
        else
            buf = av_frame_clone(s->prev);
        if (!buf)
            return AVERROR(ENOMEM);

        ret = filter_frame(ctx->inputs[0], buf);
        if (ret < 0)
            return ret;
        ret = AVERROR_EOF;
    }

    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    FFTdnoizContext *s = ctx->priv;
    int i;

    for (i = 0; i < 4; i++) {
        PlaneContext *p = &s->planes[i];

        for (int j = 0; j < s->nb_threads; j++) {
            av_freep(&p->hdata[j]);
            av_freep(&p->vdata[j]);
            av_freep(&p->hdata_out[j]);
            av_freep(&p->vdata_out[j]);
            av_freep(&p->buffer[j][PREV]);
            av_freep(&p->buffer[j][CURRENT]);
            av_freep(&p->buffer[j][NEXT]);
        }
    }

    for (i = 0; i < s->nb_threads; i++) {
        av_tx_uninit(&s->fft[i]);
        av_tx_uninit(&s->ifft[i]);
        av_tx_uninit(&s->fft_r[i]);
        av_tx_uninit(&s->ifft_r[i]);
    }

    av_frame_free(&s->prev);
    av_frame_free(&s->cur);
    av_frame_free(&s->next);
}

static const AVFilterPad fftdnoiz_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

static const AVFilterPad fftdnoiz_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
    },
};

const AVFilter ff_vf_fftdnoiz = {
    .name          = "fftdnoiz",
    .description   = NULL_IF_CONFIG_SMALL("Denoise frames using 3D FFT."),
    .priv_size     = sizeof(FFTdnoizContext),
    .uninit        = uninit,
    FILTER_INPUTS(fftdnoiz_inputs),
    FILTER_OUTPUTS(fftdnoiz_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .priv_class    = &fftdnoiz_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                     AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
