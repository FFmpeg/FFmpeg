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
#include "libavutil/imgutils.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct ColorCorrectContext {
    const AVClass *class;

    float rl, bl;
    float rh, bh;
    float saturation;

    int depth;

    int (*do_slice)(AVFilterContext *s, void *arg,
                    int jobnr, int nb_jobs);
} ColorCorrectContext;

#define PROCESS()                            \
    float y = yptr[x] * imax;                \
    float u = uptr[x] * imax - .5f;          \
    float v = vptr[x] * imax - .5f;          \
    float ny, nu, nv;                        \
                                             \
    ny = y;                                  \
    nu = saturation * (u + y * bd + bl);     \
    nv = saturation * (v + y * rd + rl);

static int colorcorrect_slice8(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorCorrectContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int depth = s->depth;
    const float max = (1 << depth) - 1;
    const float imax = 1.f / max;
    const int width = frame->width;
    const int height = frame->height;
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const int ylinesize = frame->linesize[0];
    const int ulinesize = frame->linesize[1];
    const int vlinesize = frame->linesize[2];
    uint8_t *yptr = frame->data[0] + slice_start * ylinesize;
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

            yptr[x] = av_clip_uint8( ny         * max);
            uptr[x] = av_clip_uint8((nu + 0.5f) * max);
            vptr[x] = av_clip_uint8((nv + 0.5f) * max);
        }

        yptr += ylinesize;
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
    const float max = (1 << depth) - 1;
    const float imax = 1.f / max;
    const int width = frame->width;
    const int height = frame->height;
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const int ylinesize = frame->linesize[0] / 2;
    const int ulinesize = frame->linesize[1] / 2;
    const int vlinesize = frame->linesize[2] / 2;
    uint16_t *yptr = (uint16_t *)frame->data[0] + slice_start * ylinesize;
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

            yptr[x] = av_clip_uintp2_c( ny         * max, depth);
            uptr[x] = av_clip_uintp2_c((nu + 0.5f) * max, depth);
            vptr[x] = av_clip_uintp2_c((nv + 0.5f) * max, depth);
        }

        yptr += ylinesize;
        uptr += ulinesize;
        vptr += vlinesize;
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    ColorCorrectContext *s = ctx->priv;

    ctx->internal->execute(ctx, s->do_slice, frame, NULL,
                           FFMIN(frame->height, ff_filter_get_nb_threads(ctx)));

    return ff_filter_frame(ctx->outputs[0], frame);
}

static av_cold int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pixel_fmts[] = {
        AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVJ444P,
        AV_PIX_FMT_YUV444P9, AV_PIX_FMT_YUV444P10, AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV444P14, AV_PIX_FMT_YUV444P16,
        AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA444P9, AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_YUVA444P12, AV_PIX_FMT_YUVA444P16,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *formats = NULL;

    formats = ff_make_format_list(pixel_fmts);
    if (!formats)
        return AVERROR(ENOMEM);

    return ff_set_common_formats(ctx, formats);
}

static av_cold int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ColorCorrectContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    s->depth = desc->comp[0].depth;
    s->do_slice = s->depth <= 8 ? colorcorrect_slice8 : colorcorrect_slice16;

    return 0;
}

static const AVFilterPad colorcorrect_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .needs_writable = 1,
        .filter_frame   = filter_frame,
        .config_props   = config_input,
    },
    { NULL }
};

static const AVFilterPad colorcorrect_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

#define OFFSET(x) offsetof(ColorCorrectContext, x)
#define VF AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption colorcorrect_options[] = {
    { "rl", "set the red shadow spot",              OFFSET(rl), AV_OPT_TYPE_FLOAT, {.dbl=0}, -1, 1, VF },
    { "bl", "set the blue shadow spot",             OFFSET(bl), AV_OPT_TYPE_FLOAT, {.dbl=0}, -1, 1, VF },
    { "rh", "set the red highlight spot",           OFFSET(rh), AV_OPT_TYPE_FLOAT, {.dbl=0}, -1, 1, VF },
    { "bh", "set the blue highlight spot",          OFFSET(bh), AV_OPT_TYPE_FLOAT, {.dbl=0}, -1, 1, VF },
    { "saturation", "set the amount of saturation", OFFSET(saturation), AV_OPT_TYPE_FLOAT, {.dbl=1}, -3, 3, VF },
    { NULL }
};

AVFILTER_DEFINE_CLASS(colorcorrect);

AVFilter ff_vf_colorcorrect = {
    .name          = "colorcorrect",
    .description   = NULL_IF_CONFIG_SMALL("Adjust color white balance selectively for blacks and whites."),
    .priv_size     = sizeof(ColorCorrectContext),
    .priv_class    = &colorcorrect_class,
    .query_formats = query_formats,
    .inputs        = colorcorrect_inputs,
    .outputs       = colorcorrect_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
