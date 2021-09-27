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

typedef struct ExposureContext {
    const AVClass *class;

    float exposure;
    float black;

    float scale;
    int (*do_slice)(AVFilterContext *s, void *arg,
                    int jobnr, int nb_jobs);
} ExposureContext;

static int exposure_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ExposureContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int width = frame->width;
    const int height = frame->height;
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const float black = s->black;
    const float scale = s->scale;

    for (int p = 0; p < 3; p++) {
        const int linesize = frame->linesize[p] / 4;
        float *ptr = (float *)frame->data[p] + slice_start * linesize;
        for (int y = slice_start; y < slice_end; y++) {
            for (int x = 0; x < width; x++)
                ptr[x] = (ptr[x] - black) * scale;

            ptr += linesize;
        }
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    ExposureContext *s = ctx->priv;

    s->scale = 1.f / (exp2f(-s->exposure) - s->black);
    ff_filter_execute(ctx, s->do_slice, frame, NULL,
                      FFMIN(frame->height, ff_filter_get_nb_threads(ctx)));

    return ff_filter_frame(ctx->outputs[0], frame);
}

static av_cold int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ExposureContext *s = ctx->priv;

    s->do_slice = exposure_slice;

    return 0;
}

static const AVFilterPad exposure_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .flags          = AVFILTERPAD_FLAG_NEEDS_WRITABLE,
        .filter_frame   = filter_frame,
        .config_props   = config_input,
    },
};

static const AVFilterPad exposure_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

#define OFFSET(x) offsetof(ExposureContext, x)
#define VF AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption exposure_options[] = {
    { "exposure", "set the exposure correction",    OFFSET(exposure), AV_OPT_TYPE_FLOAT, {.dbl=0}, -3, 3, VF },
    { "black",    "set the black level correction", OFFSET(black),    AV_OPT_TYPE_FLOAT, {.dbl=0}, -1, 1, VF },
    { NULL }
};

AVFILTER_DEFINE_CLASS(exposure);

const AVFilter ff_vf_exposure = {
    .name          = "exposure",
    .description   = NULL_IF_CONFIG_SMALL("Adjust exposure of the video stream."),
    .priv_size     = sizeof(ExposureContext),
    .priv_class    = &exposure_class,
    FILTER_INPUTS(exposure_inputs),
    FILTER_OUTPUTS(exposure_outputs),
    FILTER_PIXFMTS(AV_PIX_FMT_GBRPF32, AV_PIX_FMT_GBRAPF32),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
