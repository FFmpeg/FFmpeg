/*
 * Copyright (c) 2021 Paul B Mahol
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

#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "internal.h"
#include "video.h"

typedef enum AnalyzeMode {
    MANUAL,
    AVERAGE,
    MINMAX,
    MEDIAN,
    NB_ANALYZE
} AnalyzeMode;

typedef struct ColorCorrectContext {
    const AVClass *class;

    float rl, bl;
    float rh, bh;
    float saturation;
    int analyze;

    int depth;
    float max, imax;

    int chroma_w, chroma_h;
    int planeheight[4];
    int planewidth[4];

    unsigned *uhistogram;
    unsigned *vhistogram;

    float (*analyzeret)[4];

    int (*do_analyze)(AVFilterContext *s, void *arg,
                      int jobnr, int nb_jobs);
    int (*do_slice)(AVFilterContext *s, void *arg,
                    int jobnr, int nb_jobs);
} ColorCorrectContext;

static int average_slice8(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorCorrectContext *s = ctx->priv;
    AVFrame *frame = arg;
    const float imax = s->imax;
    const int width = s->planewidth[1];
    const int height = s->planeheight[1];
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const ptrdiff_t ulinesize = frame->linesize[1];
    const ptrdiff_t vlinesize = frame->linesize[2];
    const uint8_t *uptr = (const uint8_t *)frame->data[1] + slice_start * ulinesize;
    const uint8_t *vptr = (const uint8_t *)frame->data[2] + slice_start * vlinesize;
    int sum_u = 0, sum_v = 0;

    for (int y = slice_start; y < slice_end; y++) {
        for (int x = 0; x < width; x++) {
            sum_u += uptr[x];
            sum_v += vptr[x];
        }

        uptr += ulinesize;
        vptr += vlinesize;
    }

    s->analyzeret[jobnr][0] = s->analyzeret[jobnr][2] = imax * sum_u / (float)((slice_end - slice_start) * width) - 0.5f;
    s->analyzeret[jobnr][1] = s->analyzeret[jobnr][3] = imax * sum_v / (float)((slice_end - slice_start) * width) - 0.5f;

    return 0;
}

static int average_slice16(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorCorrectContext *s = ctx->priv;
    AVFrame *frame = arg;
    const float imax = s->imax;
    const int width = s->planewidth[1];
    const int height = s->planeheight[1];
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const ptrdiff_t ulinesize = frame->linesize[1] / 2;
    const ptrdiff_t vlinesize = frame->linesize[2] / 2;
    const uint16_t *uptr = (const uint16_t *)frame->data[1] + slice_start * ulinesize;
    const uint16_t *vptr = (const uint16_t *)frame->data[2] + slice_start * vlinesize;
    int64_t sum_u = 0, sum_v = 0;

    for (int y = slice_start; y < slice_end; y++) {
        for (int x = 0; x < width; x++) {
            sum_u += uptr[x];
            sum_v += vptr[x];
        }

        uptr += ulinesize;
        vptr += vlinesize;
    }

    s->analyzeret[jobnr][0] = s->analyzeret[jobnr][2] = imax * sum_u / (float)((slice_end - slice_start) * width) - 0.5f;
    s->analyzeret[jobnr][1] = s->analyzeret[jobnr][3] = imax * sum_v / (float)((slice_end - slice_start) * width) - 0.5f;

    return 0;
}

static int minmax_slice8(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorCorrectContext *s = ctx->priv;
    AVFrame *frame = arg;
    const float imax = s->imax;
    const int width = s->planewidth[1];
    const int height = s->planeheight[1];
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const ptrdiff_t ulinesize = frame->linesize[1];
    const ptrdiff_t vlinesize = frame->linesize[2];
    const uint8_t *uptr = (const uint8_t *)frame->data[1] + slice_start * ulinesize;
    const uint8_t *vptr = (const uint8_t *)frame->data[2] + slice_start * vlinesize;
    int min_u = 255, min_v = 255;
    int max_u = 0, max_v = 0;

    for (int y = slice_start; y < slice_end; y++) {
        for (int x = 0; x < width; x++) {
            min_u = FFMIN(min_u, uptr[x]);
            min_v = FFMIN(min_v, vptr[x]);
            max_u = FFMAX(max_u, uptr[x]);
            max_v = FFMAX(max_v, vptr[x]);
        }

        uptr += ulinesize;
        vptr += vlinesize;
    }

    s->analyzeret[jobnr][0] = imax * min_u - 0.5f;
    s->analyzeret[jobnr][1] = imax * min_v - 0.5f;
    s->analyzeret[jobnr][2] = imax * max_u - 0.5f;
    s->analyzeret[jobnr][3] = imax * max_v - 0.5f;

    return 0;
}

static int minmax_slice16(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorCorrectContext *s = ctx->priv;
    AVFrame *frame = arg;
    const float imax = s->imax;
    const int width = s->planewidth[1];
    const int height = s->planeheight[1];
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const ptrdiff_t ulinesize = frame->linesize[1] / 2;
    const ptrdiff_t vlinesize = frame->linesize[2] / 2;
    const uint16_t *uptr = (const uint16_t *)frame->data[1] + slice_start * ulinesize;
    const uint16_t *vptr = (const uint16_t *)frame->data[2] + slice_start * vlinesize;
    int min_u = INT_MAX, min_v = INT_MAX;
    int max_u = INT_MIN, max_v = INT_MIN;

    for (int y = slice_start; y < slice_end; y++) {
        for (int x = 0; x < width; x++) {
            min_u = FFMIN(min_u, uptr[x]);
            min_v = FFMIN(min_v, vptr[x]);
            max_u = FFMAX(max_u, uptr[x]);
            max_v = FFMAX(max_v, vptr[x]);
        }

        uptr += ulinesize;
        vptr += vlinesize;
    }

    s->analyzeret[jobnr][0] = imax * min_u - 0.5f;
    s->analyzeret[jobnr][1] = imax * min_v - 0.5f;
    s->analyzeret[jobnr][2] = imax * max_u - 0.5f;
    s->analyzeret[jobnr][3] = imax * max_v - 0.5f;

    return 0;
}

static int median_8(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorCorrectContext *s = ctx->priv;
    AVFrame *frame = arg;
    const float imax = s->imax;
    const int width = s->planewidth[1];
    const int height = s->planeheight[1];
    const ptrdiff_t ulinesize = frame->linesize[1];
    const ptrdiff_t vlinesize = frame->linesize[2];
    const uint8_t *uptr = (const uint8_t *)frame->data[1];
    const uint8_t *vptr = (const uint8_t *)frame->data[2];
    unsigned *uhistogram = s->uhistogram;
    unsigned *vhistogram = s->vhistogram;
    const int half_size = width * height / 2;
    int umedian = s->max, vmedian = s->max;
    unsigned ucnt = 0, vcnt = 0;

    memset(uhistogram, 0, sizeof(*uhistogram) * (s->max + 1));
    memset(vhistogram, 0, sizeof(*vhistogram) * (s->max + 1));

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uhistogram[uptr[x]]++;
            vhistogram[vptr[x]]++;
        }

        uptr += ulinesize;
        vptr += vlinesize;
    }

    for (int i = 0; i < s->max + 1; i++) {
        ucnt += uhistogram[i];
        if (ucnt >= half_size) {
            umedian = i;
            break;
        }
    }

    for (int i = 0; i < s->max + 1; i++) {
        vcnt += vhistogram[i];
        if (vcnt >= half_size) {
            vmedian = i;
            break;
        }
    }

    s->analyzeret[0][0] = imax * umedian - 0.5f;
    s->analyzeret[0][1] = imax * vmedian - 0.5f;
    s->analyzeret[0][2] = imax * umedian - 0.5f;
    s->analyzeret[0][3] = imax * vmedian - 0.5f;

    return 0;
}

static int median_16(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorCorrectContext *s = ctx->priv;
    AVFrame *frame = arg;
    const float imax = s->imax;
    const int width = s->planewidth[1];
    const int height = s->planeheight[1];
    const ptrdiff_t ulinesize = frame->linesize[1] / 2;
    const ptrdiff_t vlinesize = frame->linesize[2] / 2;
    const uint16_t *uptr = (const uint16_t *)frame->data[1];
    const uint16_t *vptr = (const uint16_t *)frame->data[2];
    unsigned *uhistogram = s->uhistogram;
    unsigned *vhistogram = s->vhistogram;
    const int half_size = width * height / 2;
    int umedian = s->max, vmedian = s->max;
    unsigned ucnt = 0, vcnt = 0;

    memset(uhistogram, 0, sizeof(*uhistogram) * (s->max + 1));
    memset(vhistogram, 0, sizeof(*vhistogram) * (s->max + 1));

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uhistogram[uptr[x]]++;
            vhistogram[vptr[x]]++;
        }

        uptr += ulinesize;
        vptr += vlinesize;
    }

    for (int i = 0; i < s->max + 1; i++) {
        ucnt += uhistogram[i];
        if (ucnt >= half_size) {
            umedian = i;
            break;
        }
    }

    for (int i = 0; i < s->max + 1; i++) {
        vcnt += vhistogram[i];
        if (vcnt >= half_size) {
            vmedian = i;
            break;
        }
    }

    s->analyzeret[0][0] = imax * umedian - 0.5f;
    s->analyzeret[0][1] = imax * vmedian - 0.5f;
    s->analyzeret[0][2] = imax * umedian - 0.5f;
    s->analyzeret[0][3] = imax * vmedian - 0.5f;

    return 0;
}

#define PROCESS()                            \
    float y = yptr[x * chroma_w] * imax;     \
    float u = uptr[x] * imax - .5f;          \
    float v = vptr[x] * imax - .5f;          \
    float nu, nv;                            \
                                             \
    nu = saturation * (u + y * bd + bl);     \
    nv = saturation * (v + y * rd + rl);

static int colorcorrect_slice8(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorCorrectContext *s = ctx->priv;
    AVFrame *frame = arg;
    const float max = s->max;
    const float imax = s->imax;
    const int chroma_w = s->chroma_w;
    const int chroma_h = s->chroma_h;
    const int width = s->planewidth[1];
    const int height = s->planeheight[1];
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const ptrdiff_t ylinesize = frame->linesize[0];
    const ptrdiff_t ulinesize = frame->linesize[1];
    const ptrdiff_t vlinesize = frame->linesize[2];
    uint8_t *yptr = frame->data[0] + slice_start * chroma_h * ylinesize;
    uint8_t *uptr = frame->data[1] + slice_start * ulinesize;
    uint8_t *vptr = frame->data[2] + slice_start * vlinesize;
    const float saturation = s->saturation;
    const float bl = s->bl;
    const float rl = s->rl;
    const float bd = s->bh - bl;
    const float rd = s->rh - rl;

    for (int y = slice_start; y < slice_end; y++) {
        for (int x = 0; x < width; x++) {
            PROCESS()

            uptr[x] = av_clip_uint8((nu + 0.5f) * max);
            vptr[x] = av_clip_uint8((nv + 0.5f) * max);
        }

        yptr += ylinesize * chroma_h;
        uptr += ulinesize;
        vptr += vlinesize;
    }

    return 0;
}

static int colorcorrect_slice16(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorCorrectContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int depth = s->depth;
    const float max = s->max;
    const float imax = s->imax;
    const int chroma_w = s->chroma_w;
    const int chroma_h = s->chroma_h;
    const int width = s->planewidth[1];
    const int height = s->planeheight[1];
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const ptrdiff_t ylinesize = frame->linesize[0] / 2;
    const ptrdiff_t ulinesize = frame->linesize[1] / 2;
    const ptrdiff_t vlinesize = frame->linesize[2] / 2;
    uint16_t *yptr = (uint16_t *)frame->data[0] + slice_start * chroma_h * ylinesize;
    uint16_t *uptr = (uint16_t *)frame->data[1] + slice_start * ulinesize;
    uint16_t *vptr = (uint16_t *)frame->data[2] + slice_start * vlinesize;
    const float saturation = s->saturation;
    const float bl = s->bl;
    const float rl = s->rl;
    const float bd = s->bh - bl;
    const float rd = s->rh - rl;

    for (int y = slice_start; y < slice_end; y++) {
        for (int x = 0; x < width; x++) {
            PROCESS()

            uptr[x] = av_clip_uintp2_c((nu + 0.5f) * max, depth);
            vptr[x] = av_clip_uintp2_c((nv + 0.5f) * max, depth);
        }

        yptr += ylinesize * chroma_h;
        uptr += ulinesize;
        vptr += vlinesize;
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    ColorCorrectContext *s = ctx->priv;
    const int nb_threads = s->analyze == MEDIAN ? 1 : FFMIN(s->planeheight[1], ff_filter_get_nb_threads(ctx));

    if (s->analyze) {
        const int nb_athreads = s->analyze == MEDIAN ? 1 : nb_threads;
        float bl = 0.f, rl = 0.f, bh = 0.f, rh = 0.f;

        ff_filter_execute(ctx, s->do_analyze, frame, NULL, nb_athreads);

        for (int i = 0; i < nb_athreads; i++) {
            bl += s->analyzeret[i][0];
            rl += s->analyzeret[i][1];
            bh += s->analyzeret[i][2];
            rh += s->analyzeret[i][3];
        }

        bl /= nb_athreads;
        rl /= nb_athreads;
        bh /= nb_athreads;
        rh /= nb_athreads;

        s->bl = -bl;
        s->rl = -rl;
        s->bh = -bh;
        s->rh = -rh;
    }

    ff_filter_execute(ctx, s->do_slice, frame, NULL, nb_threads);

    return ff_filter_frame(ctx->outputs[0], frame);
}

static const enum AVPixelFormat pixel_fmts[] = {
    AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ411P,
    AV_PIX_FMT_YUV420P9,   AV_PIX_FMT_YUV422P9,   AV_PIX_FMT_YUV444P9,
    AV_PIX_FMT_YUV420P10,  AV_PIX_FMT_YUV422P10,  AV_PIX_FMT_YUV440P10, AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV444P12,  AV_PIX_FMT_YUV422P12,  AV_PIX_FMT_YUV440P12, AV_PIX_FMT_YUV420P12,
    AV_PIX_FMT_YUV444P14,  AV_PIX_FMT_YUV422P14,  AV_PIX_FMT_YUV420P14,
    AV_PIX_FMT_YUV420P16,  AV_PIX_FMT_YUV422P16,  AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_YUVA420P9,  AV_PIX_FMT_YUVA422P9,  AV_PIX_FMT_YUVA444P9,
    AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA444P10,
    AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA444P12,
    AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_NONE
};

static av_cold int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ColorCorrectContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    s->depth = desc->comp[0].depth;
    s->max = (1 << s->depth) - 1;
    s->imax = 1.f / s->max;
    s->do_slice = s->depth <= 8 ? colorcorrect_slice8 : colorcorrect_slice16;

    s->uhistogram = av_calloc(s->max == 255 ? 256 : 65536, sizeof(*s->uhistogram));
    if (!s->uhistogram)
        return AVERROR(ENOMEM);

    s->vhistogram = av_calloc(s->max == 255 ? 256 : 65536, sizeof(*s->vhistogram));
    if (!s->vhistogram)
        return AVERROR(ENOMEM);

    s->analyzeret = av_calloc(inlink->h, sizeof(*s->analyzeret));
    if (!s->analyzeret)
        return AVERROR(ENOMEM);

    switch (s->analyze) {
    case MANUAL:
        break;
    case AVERAGE:
        s->do_analyze = s->depth <= 8 ? average_slice8 : average_slice16;
        break;
    case MINMAX:
        s->do_analyze = s->depth <= 8 ? minmax_slice8  : minmax_slice16;
        break;
    case MEDIAN:
        s->do_analyze = s->depth <= 8 ? median_8       : median_16;
        break;
    default:
        return AVERROR_BUG;
    }

    s->chroma_w = 1 << desc->log2_chroma_w;
    s->chroma_h = 1 << desc->log2_chroma_h;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;
    s->planewidth[1] = s->planewidth[2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0] = s->planewidth[3] = inlink->w;

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ColorCorrectContext *s = ctx->priv;

    av_freep(&s->analyzeret);
    av_freep(&s->uhistogram);
    av_freep(&s->vhistogram);
}

static const AVFilterPad colorcorrect_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .flags          = AVFILTERPAD_FLAG_NEEDS_WRITABLE,
        .filter_frame   = filter_frame,
        .config_props   = config_input,
    },
};

#define OFFSET(x) offsetof(ColorCorrectContext, x)
#define VF AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption colorcorrect_options[] = {
    { "rl", "set the red shadow spot",              OFFSET(rl), AV_OPT_TYPE_FLOAT, {.dbl=0}, -1, 1, VF },
    { "bl", "set the blue shadow spot",             OFFSET(bl), AV_OPT_TYPE_FLOAT, {.dbl=0}, -1, 1, VF },
    { "rh", "set the red highlight spot",           OFFSET(rh), AV_OPT_TYPE_FLOAT, {.dbl=0}, -1, 1, VF },
    { "bh", "set the blue highlight spot",          OFFSET(bh), AV_OPT_TYPE_FLOAT, {.dbl=0}, -1, 1, VF },
    { "saturation", "set the amount of saturation", OFFSET(saturation), AV_OPT_TYPE_FLOAT, {.dbl=1}, -3, 3, VF },
    { "analyze", "set the analyze mode",            OFFSET(analyze), AV_OPT_TYPE_INT, {.i64=0}, 0, NB_ANALYZE-1, VF, .unit = "analyze" },
    {   "manual",  "manually set options", 0, AV_OPT_TYPE_CONST, {.i64=MANUAL},  0, 0, VF, .unit = "analyze" },
    {   "average", "use average pixels",   0, AV_OPT_TYPE_CONST, {.i64=AVERAGE}, 0, 0, VF, .unit = "analyze" },
    {   "minmax",  "use minmax pixels",    0, AV_OPT_TYPE_CONST, {.i64=MINMAX},  0, 0, VF, .unit = "analyze" },
    {   "median",  "use median pixels",    0, AV_OPT_TYPE_CONST, {.i64=MEDIAN},  0, 0, VF, .unit = "analyze" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(colorcorrect);

const AVFilter ff_vf_colorcorrect = {
    .name          = "colorcorrect",
    .description   = NULL_IF_CONFIG_SMALL("Adjust color white balance selectively for blacks and whites."),
    .priv_size     = sizeof(ColorCorrectContext),
    .priv_class    = &colorcorrect_class,
    .uninit        = uninit,
    FILTER_INPUTS(colorcorrect_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS_ARRAY(pixel_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
