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

typedef struct MonochromeContext {
    const AVClass *class;

    float b, r;
    float size;
    float high;

    int depth;
    int subw, subh;

    int (*do_slice)(AVFilterContext *s, void *arg,
                    int jobnr, int nb_jobs);
    int (*clear_uv)(AVFilterContext *s, void *arg,
                    int jobnr, int nb_jobs);
} MonochromeContext;

static float envelope(const float x)
{
    const float beta = 0.6f;

    if (x < beta) {
        const float tmp = fabsf(x / beta - 1.f);

        return 1.f - tmp * tmp;
    } else {
        const float tmp = (1.f - x) / (1.f - beta);

        return tmp * tmp * (3.f - 2.f * tmp);
    }
}

static float filter(float b, float r, float u, float v, float size)
{
    return expf(-av_clipf(((b - u) * (b - u) +
                           (r - v) * (r - v)) *
                            size, 0.f, 1.f));
}

#define PROCESS()                            \
    const int cx = x >> subw;                \
    float y = yptr[x] * imax;                \
    float u = uptr[cx] * imax - .5f;         \
    float v = vptr[cx] * imax - .5f;         \
    float tt, t, ny;                         \
                                             \
    ny = filter(b, r, u, v, size);           \
    tt = envelope(y);                        \
    t = tt + (1.f - tt) * ihigh;             \
    ny = (1.f - t) * y + t * ny * y;

static int monochrome_slice8(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    MonochromeContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int depth = s->depth;
    const int subw = s->subw;
    const int subh = s->subh;
    const float max = (1 << depth) - 1;
    const float imax = 1.f / max;
    const int width = frame->width;
    const int height = frame->height;
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const ptrdiff_t ylinesize = frame->linesize[0];
    const ptrdiff_t ulinesize = frame->linesize[1];
    const ptrdiff_t vlinesize = frame->linesize[2];
    uint8_t *yptr = frame->data[0] + slice_start * ylinesize;
    const float ihigh = 1.f - s->high;
    const float size = 1.f / s->size;
    const float b = s->b * .5f;
    const float r = s->r * .5f;

    for (int y = slice_start; y < slice_end; y++) {
        const int cy = y >> subh;
        uint8_t *uptr = frame->data[1] + cy * ulinesize;
        uint8_t *vptr = frame->data[2] + cy * vlinesize;

        for (int x = 0; x < width; x++) {
            PROCESS()

            yptr[x] = av_clip_uint8(lrintf(ny * max));
        }

        yptr += ylinesize;
    }

    return 0;
}

static int monochrome_slice16(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    MonochromeContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int depth = s->depth;
    const int subw = s->subw;
    const int subh = s->subh;
    const float max = (1 << depth) - 1;
    const float imax = 1.f / max;
    const int width = frame->width;
    const int height = frame->height;
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const ptrdiff_t ylinesize = frame->linesize[0] / 2;
    const ptrdiff_t ulinesize = frame->linesize[1] / 2;
    const ptrdiff_t vlinesize = frame->linesize[2] / 2;
    uint16_t *yptr = (uint16_t *)frame->data[0] + slice_start * ylinesize;
    const float ihigh = 1.f - s->high;
    const float size = 1.f / s->size;
    const float b = s->b * .5f;
    const float r = s->r * .5f;

    for (int y = slice_start; y < slice_end; y++) {
        const int cy = y >> subh;
        uint16_t *uptr = (uint16_t *)frame->data[1] + cy * ulinesize;
        uint16_t *vptr = (uint16_t *)frame->data[2] + cy * vlinesize;

        for (int x = 0; x < width; x++) {
            PROCESS()

            yptr[x] = av_clip_uintp2_c(lrintf(ny * max), depth);
        }

        yptr += ylinesize;
    }

    return 0;
}

static int clear_slice8(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    MonochromeContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int depth = s->depth;
    const int half = 1 << (depth - 1);
    const int subw = s->subw;
    const int subh = s->subh;
    const int width = AV_CEIL_RSHIFT(frame->width, subw);
    const int height = AV_CEIL_RSHIFT(frame->height, subh);
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const ptrdiff_t ulinesize = frame->linesize[1];
    const ptrdiff_t vlinesize = frame->linesize[2];

    for (int y = slice_start; y < slice_end; y++) {
        uint8_t *uptr = frame->data[1] + y * ulinesize;
        uint8_t *vptr = frame->data[2] + y * vlinesize;

        memset(uptr, half, width);
        memset(vptr, half, width);
    }

    return 0;
}

static int clear_slice16(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    MonochromeContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int depth = s->depth;
    const int half = 1 << (depth - 1);
    const int subw = s->subw;
    const int subh = s->subh;
    const int width = AV_CEIL_RSHIFT(frame->width, subw);
    const int height = AV_CEIL_RSHIFT(frame->height, subh);
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const ptrdiff_t ulinesize = frame->linesize[1] / 2;
    const ptrdiff_t vlinesize = frame->linesize[2] / 2;

    for (int y = slice_start; y < slice_end; y++) {
        uint16_t *uptr = (uint16_t *)frame->data[1] + y * ulinesize;
        uint16_t *vptr = (uint16_t *)frame->data[2] + y * vlinesize;

        for (int x = 0; x < width; x++) {
            uptr[x] = half;
            vptr[x] = half;
        }
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    MonochromeContext *s = ctx->priv;

    ff_filter_execute(ctx, s->do_slice, frame, NULL,
                      FFMIN(frame->height, ff_filter_get_nb_threads(ctx)));
    ff_filter_execute(ctx, s->clear_uv, frame, NULL,
                      FFMIN(frame->height >> s->subh, ff_filter_get_nb_threads(ctx)));

    return ff_filter_frame(ctx->outputs[0], frame);
}

static const enum AVPixelFormat pixel_fmts[] = {
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
    AV_PIX_FMT_YUVA420P,  AV_PIX_FMT_YUVA422P,   AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_YUVA444P9, AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_YUVA444P12, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA422P16,
    AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA420P16,
    AV_PIX_FMT_NONE
};

static av_cold int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    MonochromeContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    s->depth = desc->comp[0].depth;
    s->do_slice = s->depth <= 8 ? monochrome_slice8 : monochrome_slice16;
    s->clear_uv = s->depth <= 8 ? clear_slice8 : clear_slice16;
    s->subw = desc->log2_chroma_w;
    s->subh = desc->log2_chroma_h;

    return 0;
}

static const AVFilterPad monochrome_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .flags          = AVFILTERPAD_FLAG_NEEDS_WRITABLE,
        .filter_frame   = filter_frame,
        .config_props   = config_input,
    },
};

#define OFFSET(x) offsetof(MonochromeContext, x)
#define VF AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption monochrome_options[] = {
    { "cb",   "set the chroma blue spot",    OFFSET(b),    AV_OPT_TYPE_FLOAT, {.dbl=0},-1, 1, VF },
    { "cr",   "set the chroma red spot",     OFFSET(r),    AV_OPT_TYPE_FLOAT, {.dbl=0},-1, 1, VF },
    { "size", "set the color filter size",   OFFSET(size), AV_OPT_TYPE_FLOAT, {.dbl=1},.1,10, VF },
    { "high", "set the highlights strength", OFFSET(high), AV_OPT_TYPE_FLOAT, {.dbl=0}, 0, 1, VF },
    { NULL }
};

AVFILTER_DEFINE_CLASS(monochrome);

const AVFilter ff_vf_monochrome = {
    .name          = "monochrome",
    .description   = NULL_IF_CONFIG_SMALL("Convert video to gray using custom color filter."),
    .priv_size     = sizeof(MonochromeContext),
    .priv_class    = &monochrome_class,
    FILTER_INPUTS(monochrome_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS_ARRAY(pixel_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
