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
#include "internal.h"
#include "libavcodec/avfft.h"

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

    float *buffer[BSIZE];
    FFTComplex *hdata[MAX_THREADS], *vdata[MAX_THREADS];
    int data_linesize;
    int buffer_linesize;
} PlaneContext;

typedef struct FFTdnoizContext {
    const AVClass *class;

    float sigma;
    float amount;
    int   block_bits;
    float overlap;
    int   nb_prev;
    int   nb_next;
    int   planesf;

    AVFrame *prev, *cur, *next;

    int depth;
    int nb_planes;
    int nb_threads;
    PlaneContext planes[4];

    FFTContext *fft[MAX_THREADS], *ifft[MAX_THREADS];

    void (*import_row)(FFTComplex *dst, uint8_t *src, int rw);
    void (*export_row)(FFTComplex *src, uint8_t *dst, int rw, float scale, int depth);
} FFTdnoizContext;

#define OFFSET(x) offsetof(FFTdnoizContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
#define TFLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM
static const AVOption fftdnoiz_options[] = {
    { "sigma",   "set denoise strength",
        OFFSET(sigma),      AV_OPT_TYPE_FLOAT, {.dbl=1},        0,  30, .flags = TFLAGS },
    { "amount",  "set amount of denoising",
        OFFSET(amount),     AV_OPT_TYPE_FLOAT, {.dbl=1},     0.01,   1, .flags = TFLAGS },
    { "block",   "set block log2(size)",
        OFFSET(block_bits), AV_OPT_TYPE_INT,   {.i64=4},        3,   6, .flags = FLAGS },
    { "overlap", "set block overlap",
        OFFSET(overlap),    AV_OPT_TYPE_FLOAT, {.dbl=0.5},    0.2, 0.8, .flags = FLAGS },
    { "prev",    "set number of previous frames for temporal denoising",
        OFFSET(nb_prev),    AV_OPT_TYPE_INT,   {.i64=0},        0,   1, .flags = FLAGS },
    { "next",    "set number of next frames for temporal denoising",
        OFFSET(nb_next),    AV_OPT_TYPE_INT,   {.i64=0},        0,   1, .flags = FLAGS },
    { "planes",  "set planes to filter",
        OFFSET(planesf),    AV_OPT_TYPE_INT,   {.i64=7},        0,  15, .flags = TFLAGS },
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

static void import_row8(FFTComplex *dst, uint8_t *src, int rw)
{
    int j;

    for (j = 0; j < rw; j++) {
        dst[j].re = src[j];
        dst[j].im = 0;
    }
}

static void export_row8(FFTComplex *src, uint8_t *dst, int rw, float scale, int depth)
{
    int j;

    for (j = 0; j < rw; j++)
        dst[j] = av_clip_uint8(lrintf(src[j].re * scale));
}

static void import_row16(FFTComplex *dst, uint8_t *srcp, int rw)
{
    uint16_t *src = (uint16_t *)srcp;
    int j;

    for (j = 0; j < rw; j++) {
        dst[j].re = src[j];
        dst[j].im = 0;
    }
}

static void export_row16(FFTComplex *src, uint8_t *dstp, int rw, float scale, int depth)
{
    uint16_t *dst = (uint16_t *)dstp;
    int j;

    for (j = 0; j < rw; j++)
        dst[j] = av_clip_uintp2_c(src[j].re * scale + 0.5f, depth);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    const AVPixFmtDescriptor *desc;
    FFTdnoizContext *s = ctx->priv;
    int i;

    desc = av_pix_fmt_desc_get(inlink->format);
    s->depth = desc->comp[0].depth;

    if (s->depth <= 8) {
        s->import_row = import_row8;
        s->export_row = export_row8;
    } else {
        s->import_row = import_row16;
        s->export_row = export_row16;
        s->sigma *= 1 << (s->depth - 8) * (1 + s->nb_prev + s->nb_next);
    }

    s->planes[1].planewidth = s->planes[2].planewidth = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planes[0].planewidth = s->planes[3].planewidth = inlink->w;
    s->planes[1].planeheight = s->planes[2].planeheight = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planes[0].planeheight = s->planes[3].planeheight = inlink->h;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);
    s->nb_threads = FFMIN(ff_filter_get_nb_threads(ctx), MAX_THREADS);

    for (int i = 0; i < s->nb_threads; i++) {
        s->fft[i]  = av_fft_init(s->block_bits, 0);
        s->ifft[i] = av_fft_init(s->block_bits, 1);
        if (!s->fft[i] || !s->ifft[i])
            return AVERROR(ENOMEM);
    }

    for (i = 0; i < s->nb_planes; i++) {
        PlaneContext *p = &s->planes[i];
        int size;

        p->b = 1 << s->block_bits;
        p->n = 1.f / (p->b * p->b);
        p->o = p->b * s->overlap;
        size = p->b - p->o;
        p->nox = (p->planewidth  + (size - 1)) / size;
        p->noy = (p->planeheight + (size - 1)) / size;

        av_log(ctx, AV_LOG_DEBUG, "nox:%d noy:%d size:%d\n", p->nox, p->noy, size);

        p->buffer_linesize = p->b * p->nox * sizeof(FFTComplex);
        p->buffer[CURRENT] = av_calloc(p->b * p->noy, p->buffer_linesize);
        if (!p->buffer[CURRENT])
            return AVERROR(ENOMEM);
        if (s->nb_prev > 0) {
            p->buffer[PREV] = av_calloc(p->b * p->noy, p->buffer_linesize);
            if (!p->buffer[PREV])
                return AVERROR(ENOMEM);
        }
        if (s->nb_next > 0) {
            p->buffer[NEXT] = av_calloc(p->b * p->noy, p->buffer_linesize);
            if (!p->buffer[NEXT])
                return AVERROR(ENOMEM);
        }
        p->data_linesize = 2 * p->b * sizeof(float);
        for (int j = 0; j < s->nb_threads; j++) {
            p->hdata[j] = av_calloc(p->b, p->data_linesize);
            p->vdata[j] = av_calloc(p->b, p->data_linesize);
            if (!p->hdata[j] || !p->vdata[j])
                return AVERROR(ENOMEM);
        }
    }

    return 0;
}

static void import_plane(FFTdnoizContext *s,
                         uint8_t *srcp, int src_linesize,
                         float *buffer, int buffer_linesize, int plane,
                         int jobnr, int nb_jobs)
{
    PlaneContext *p = &s->planes[plane];
    const int width = p->planewidth;
    const int height = p->planeheight;
    const int block = p->b;
    const int overlap = p->o;
    const int size = block - overlap;
    const int nox = p->nox;
    const int noy = p->noy;
    const int bpp = (s->depth + 7) / 8;
    const int data_linesize = p->data_linesize / sizeof(FFTComplex);
    const int slice_start = (noy * jobnr) / nb_jobs;
    const int slice_end = (noy * (jobnr+1)) / nb_jobs;
    FFTComplex *hdata = p->hdata[jobnr];
    FFTComplex *vdata = p->vdata[jobnr];
    int x, y, i, j;

    buffer_linesize /= sizeof(float);
    for (y = slice_start; y < slice_end; y++) {
        for (x = 0; x < nox; x++) {
            const int rh = FFMIN(block, height - y * size);
            const int rw = FFMIN(block, width  - x * size);
            uint8_t *src = srcp + src_linesize * y * size + x * size * bpp;
            float *bdst = buffer + buffer_linesize * y * block + x * block * 2;
            FFTComplex *ssrc, *dst = hdata;

            for (i = 0; i < rh; i++) {
                s->import_row(dst, src, rw);
                for (j = rw; j < block; j++) {
                    dst[j].re = dst[block - j - 1].re;
                    dst[j].im = 0;
                }
                av_fft_permute(s->fft[jobnr], dst);
                av_fft_calc(s->fft[jobnr], dst);

                src += src_linesize;
                dst += data_linesize;
            }

            dst = hdata;
            for (; i < block; i++) {
                for (j = 0; j < block; j++) {
                    dst[j].re = dst[(block - i - 1) * data_linesize + j].re;
                    dst[j].im = dst[(block - i - 1) * data_linesize + j].im;
                }
            }

            ssrc = hdata;
            dst = vdata;
            for (i = 0; i < block; i++) {
                for (j = 0; j < block; j++)
                    dst[j] = ssrc[j * data_linesize + i];
                av_fft_permute(s->fft[jobnr], dst);
                av_fft_calc(s->fft[jobnr], dst);
                memcpy(bdst, dst, block * sizeof(FFTComplex));

                dst += data_linesize;
                bdst += buffer_linesize;
            }
        }
    }
}

static void export_plane(FFTdnoizContext *s,
                         uint8_t *dstp, int dst_linesize,
                         float *buffer, int buffer_linesize, int plane,
                         int jobnr, int nb_jobs)
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
    const int nox = p->nox;
    const int noy = p->noy;
    const int data_linesize = p->data_linesize / sizeof(FFTComplex);
    const float scale = 1.f / (block * block);
    const int slice_start = (noy * jobnr) / nb_jobs;
    const int slice_end = (noy * (jobnr+1)) / nb_jobs;
    FFTComplex *hdata = p->hdata[jobnr];
    FFTComplex *vdata = p->vdata[jobnr];
    int x, y, i, j;

    buffer_linesize /= sizeof(float);
    for (y = slice_start; y < slice_end; y++) {
        for (x = 0; x < nox; x++) {
            const int woff = x == 0 ? 0 : hoverlap;
            const int hoff = y == 0 ? 0 : hoverlap;
            const int rw = x == 0 ? block : FFMIN(size, width  - x * size - woff);
            const int rh = y == 0 ? block : FFMIN(size, height - y * size - hoff);
            float *bsrc = buffer + buffer_linesize * y * block + x * block * 2;
            uint8_t *dst = dstp + dst_linesize * (y * size + hoff) + (x * size + woff) * bpp;
            FFTComplex *hdst, *ddst = vdata;

            hdst = hdata;
            for (i = 0; i < block; i++) {
                memcpy(ddst, bsrc, block * sizeof(FFTComplex));
                av_fft_permute(s->ifft[jobnr], ddst);
                av_fft_calc(s->ifft[jobnr], ddst);
                for (j = 0; j < block; j++) {
                    hdst[j * data_linesize + i] = ddst[j];
                }

                ddst += data_linesize;
                bsrc += buffer_linesize;
            }

            hdst = hdata + hoff * data_linesize;
            for (i = 0; i < rh; i++) {
                av_fft_permute(s->ifft[jobnr], hdst);
                av_fft_calc(s->ifft[jobnr], hdst);
                s->export_row(hdst + woff, dst, rw, scale, depth);

                hdst += data_linesize;
                dst += dst_linesize;
            }
        }
    }
}

static void filter_plane3d2(FFTdnoizContext *s, int plane, float *pbuffer, float *nbuffer,
                            int jobnr, int nb_jobs)
{
    PlaneContext *p = &s->planes[plane];
    const int block = p->b;
    const int nox = p->nox;
    const int noy = p->noy;
    const int buffer_linesize = p->buffer_linesize / sizeof(float);
    const float sigma = s->sigma * s->sigma * block * block;
    const int slice_start = (noy * jobnr) / nb_jobs;
    const int slice_end = (noy * (jobnr+1)) / nb_jobs;
    const float limit = 1.f - s->amount;
    float *cbuffer = p->buffer[CURRENT];
    const float cfactor = sqrtf(3.f) * 0.5f;
    const float scale = 1.f / 3.f;
    int y, x, i, j;

    for (y = slice_start; y < slice_end; y++) {
        for (x = 0; x < nox; x++) {
            float *cbuff = cbuffer + buffer_linesize * y * block + x * block * 2;
            float *pbuff = pbuffer + buffer_linesize * y * block + x * block * 2;
            float *nbuff = nbuffer + buffer_linesize * y * block + x * block * 2;

            for (i = 0; i < block; i++) {
                for (j = 0; j < block; j++) {
                    float sumr, sumi, difr, difi, mpr, mpi, mnr, mni;
                    float factor, power, sumpnr, sumpni;

                    sumpnr = pbuff[2 * j    ] + nbuff[2 * j    ];
                    sumpni = pbuff[2 * j + 1] + nbuff[2 * j + 1];
                    sumr = cbuff[2 * j    ] + sumpnr;
                    sumi = cbuff[2 * j + 1] + sumpni;
                    difr = cfactor * (nbuff[2 * j    ] - pbuff[2 * j    ]);
                    difi = cfactor * (pbuff[2 * j + 1] - nbuff[2 * j + 1]);
                    mpr = cbuff[2 * j    ] - 0.5f * sumpnr + difi;
                    mnr = mpr - difi - difi;
                    mpi = cbuff[2 * j + 1] - 0.5f * sumpni + difr;
                    mni = mpi - difr - difr;
                    power = sumr * sumr + sumi * sumi + 1e-15f;
                    factor = FFMAX((power - sigma) / power, limit);
                    sumr *= factor;
                    sumi *= factor;
                    power = mpr * mpr + mpi * mpi + 1e-15f;
                    factor = FFMAX((power - sigma) / power, limit);
                    mpr *= factor;
                    mpi *= factor;
                    power = mnr * mnr + mni * mni + 1e-15f;
                    factor = FFMAX((power - sigma) / power, limit);
                    mnr *= factor;
                    mni *= factor;
                    cbuff[2 * j    ] = (sumr + mpr + mnr) * scale;
                    cbuff[2 * j + 1] = (sumi + mpi + mni) * scale;

                }

                cbuff += buffer_linesize;
                pbuff += buffer_linesize;
                nbuff += buffer_linesize;
            }
        }
    }
}

static void filter_plane3d1(FFTdnoizContext *s, int plane, float *pbuffer,
                            int jobnr, int nb_jobs)
{
    PlaneContext *p = &s->planes[plane];
    const int block = p->b;
    const int nox = p->nox;
    const int noy = p->noy;
    const int buffer_linesize = p->buffer_linesize / sizeof(float);
    const float sigma = s->sigma * s->sigma * block * block;
    const int slice_start = (noy * jobnr) / nb_jobs;
    const int slice_end = (noy * (jobnr+1)) / nb_jobs;
    const float limit = 1.f - s->amount;
    float *cbuffer = p->buffer[CURRENT];
    int y, x, i, j;

    for (y = slice_start; y < slice_end; y++) {
        for (x = 0; x < nox; x++) {
            float *cbuff = cbuffer + buffer_linesize * y * block + x * block * 2;
            float *pbuff = pbuffer + buffer_linesize * y * block + x * block * 2;

            for (i = 0; i < block; i++) {
                for (j = 0; j < block; j++) {
                    float factor, power, re, im, pre, pim;
                    float sumr, sumi, difr, difi;

                    re = cbuff[j * 2    ];
                    pre = pbuff[j * 2    ];
                    im = cbuff[j * 2 + 1];
                    pim = pbuff[j * 2 + 1];

                    sumr = re + pre;
                    sumi = im + pim;
                    difr = re - pre;
                    difi = im - pim;

                    power = sumr * sumr + sumi * sumi + 1e-15f;
                    factor = FFMAX(limit, (power - sigma) / power);
                    sumr *= factor;
                    sumi *= factor;
                    power = difr * difr + difi * difi + 1e-15f;
                    factor = FFMAX(limit, (power - sigma) / power);
                    difr *= factor;
                    difi *= factor;

                    cbuff[j * 2    ] = (sumr + difr) * 0.5f;
                    cbuff[j * 2 + 1] = (sumi + difi) * 0.5f;
                }

                cbuff += buffer_linesize;
                pbuff += buffer_linesize;
            }
        }
    }
}

static void filter_plane2d(FFTdnoizContext *s, int plane,
                           int jobnr, int nb_jobs)
{
    PlaneContext *p = &s->planes[plane];
    const int block = p->b;
    const int nox = p->nox;
    const int noy = p->noy;
    const int buffer_linesize = p->buffer_linesize / 4;
    const float sigma = s->sigma * s->sigma * block * block;
    const float limit = 1.f - s->amount;
    const int slice_start = (noy * jobnr) / nb_jobs;
    const int slice_end = (noy * (jobnr+1)) / nb_jobs;
    float *buffer = p->buffer[CURRENT];

    for (int y = slice_start; y < slice_end; y++) {
        for (int x = 0; x < nox; x++) {
            float *buff = buffer + buffer_linesize * y * block + x * block * 2;

            for (int i = 0; i < block; i++) {
                for (int j = 0; j < block; j++) {
                    float factor, power, re, im;

                    re = buff[j * 2    ];
                    im = buff[j * 2 + 1];
                    power = re * re + im * im + 1e-15f;
                    factor = FFMAX(limit, (power - sigma) / power);
                    buff[j * 2    ] *= factor;
                    buff[j * 2 + 1] *= factor;
                }

                buff += buffer_linesize;
            }
        }
    }
}

static int import_pass(AVFilterContext *ctx, void *arg,
                       int jobnr, int nb_jobs)
{
    FFTdnoizContext *s = ctx->priv;

    for (int plane = 0; plane < s->nb_planes; plane++) {
        PlaneContext *p = &s->planes[plane];

        if (!((1 << plane) & s->planesf) || ctx->is_disabled)
            continue;

        if (s->next) {
            import_plane(s, s->next->data[plane], s->next->linesize[plane],
                         p->buffer[NEXT], p->buffer_linesize, plane,
                         jobnr, nb_jobs);
        }

        if (s->prev) {
            import_plane(s, s->prev->data[plane], s->prev->linesize[plane],
                         p->buffer[PREV], p->buffer_linesize, plane,
                         jobnr, nb_jobs);
        }

        import_plane(s, s->cur->data[plane], s->cur->linesize[plane],
                     p->buffer[CURRENT], p->buffer_linesize, plane,
                     jobnr, nb_jobs);
    }

    return 0;
}

static int filter_pass(AVFilterContext *ctx, void *arg,
                       int jobnr, int nb_jobs)
{
    FFTdnoizContext *s = ctx->priv;

    for (int plane = 0; plane < s->nb_planes; plane++) {
        PlaneContext *p = &s->planes[plane];

        if (!((1 << plane) & s->planesf) || ctx->is_disabled)
            continue;

        if (s->next && s->prev) {
            filter_plane3d2(s, plane, p->buffer[PREV], p->buffer[NEXT], jobnr, nb_jobs);
        } else if (s->next) {
            filter_plane3d1(s, plane, p->buffer[NEXT], jobnr, nb_jobs);
        } else  if (s->prev) {
            filter_plane3d1(s, plane, p->buffer[PREV], jobnr, nb_jobs);
        } else {
            filter_plane2d(s, plane, jobnr, nb_jobs);
        }
    }

    return 0;
}

static int export_pass(AVFilterContext *ctx, void *arg,
                       int jobnr, int nb_jobs)
{
    FFTdnoizContext *s = ctx->priv;
    AVFrame *out = arg;

    for (int plane = 0; plane < s->nb_planes; plane++) {
        PlaneContext *p = &s->planes[plane];

        if (!((1 << plane) & s->planesf) || ctx->is_disabled)
            continue;

        export_plane(s, out->data[plane], out->linesize[plane],
                     p->buffer[CURRENT], p->buffer_linesize, plane,
                     jobnr, nb_jobs);
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

    ff_filter_execute(ctx, import_pass, NULL, NULL,
                      FFMIN(s->planes[0].noy, s->nb_threads));

    ff_filter_execute(ctx, filter_pass, NULL, NULL,
                      FFMIN(s->planes[0].noy, s->nb_threads));

    ff_filter_execute(ctx, export_pass, out, NULL,
                      FFMIN(s->planes[0].noy, s->nb_threads));

    for (plane = 0; plane < s->nb_planes; plane++) {
        PlaneContext *p = &s->planes[plane];

        if (!((1 << plane) & s->planesf) || ctx->is_disabled) {
            if (!direct)
                av_image_copy_plane(out->data[plane], out->linesize[plane],
                                    s->cur->data[plane], s->cur->linesize[plane],
                                    p->planewidth, p->planeheight);
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
        }

        av_freep(&p->buffer[PREV]);
        av_freep(&p->buffer[CURRENT]);
        av_freep(&p->buffer[NEXT]);
    }

    for (i = 0; i < s->nb_threads; i++) {
        av_fft_end(s->fft[i]);
        av_fft_end(s->ifft[i]);
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
