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

#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct DespillContext {
    const AVClass *class;

    int co[4]; /* color offsets rgba */

    int alpha;
    int type;
    float spillmix;
    float spillexpand;
    float redscale;
    float greenscale;
    float bluescale;
    float brightness;
} DespillContext;

static int do_despill_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    DespillContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int ro = s->co[0], go = s->co[1], bo = s->co[2], ao = s->co[3];
    const int slice_start = (frame->height * jobnr) / nb_jobs;
    const int slice_end = (frame->height * (jobnr + 1)) / nb_jobs;
    const float brightness = s->brightness;
    const float redscale = s->redscale;
    const float greenscale = s->greenscale;
    const float bluescale = s->bluescale;
    const float spillmix = s->spillmix;
    const float factor = (1.f - spillmix) * (1.f - s->spillexpand);
    float red, green, blue;
    int x, y;

    for (y = slice_start; y < slice_end; y++) {
        uint8_t *dst = frame->data[0] + y * frame->linesize[0];

        for (x = 0; x < frame->width; x++) {
            float spillmap;

            red   = dst[x * 4 + ro] / 255.f;
            green = dst[x * 4 + go] / 255.f;
            blue  = dst[x * 4 + bo] / 255.f;

            if (s->type) {
                spillmap = FFMAX(blue  - (red * spillmix + green * factor), 0.f);
            } else {
                spillmap = FFMAX(green - (red * spillmix + blue  * factor), 0.f);
            }

            red   = FFMAX(red   + spillmap * redscale   + brightness * spillmap, 0.f);
            green = FFMAX(green + spillmap * greenscale + brightness * spillmap, 0.f);
            blue  = FFMAX(blue  + spillmap * bluescale  + brightness * spillmap, 0.f);

            dst[x * 4 + ro] = av_clip_uint8(red   * 255);
            dst[x * 4 + go] = av_clip_uint8(green * 255);
            dst[x * 4 + bo] = av_clip_uint8(blue  * 255);
            if (s->alpha) {
                spillmap = 1.f - spillmap;
                dst[x * 4 + ao] = av_clip_uint8(spillmap * 255);
            }
        }
    }

    return 0;
}

static int filter_frame(AVFilterLink *link, AVFrame *frame)
{
    AVFilterContext *ctx = link->dst;
    int ret;

    if (ret = ff_filter_execute(ctx, do_despill_slice, frame, NULL,
                                FFMIN(frame->height, ff_filter_get_nb_threads(ctx))))
        return ret;

    return ff_filter_frame(ctx->outputs[0], frame);
}

static av_cold int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    DespillContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(outlink->format);
    int i;

    for (i = 0; i < 4; ++i)
        s->co[i] = desc->comp[i].offset;

    return 0;
}

static const enum AVPixelFormat pixel_fmts[] = {
    AV_PIX_FMT_ARGB,
    AV_PIX_FMT_RGBA,
    AV_PIX_FMT_ABGR,
    AV_PIX_FMT_BGRA,
    AV_PIX_FMT_NONE
};

static const AVFilterPad despill_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .flags        = AVFILTERPAD_FLAG_NEEDS_WRITABLE,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad despill_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
};

#define OFFSET(x) offsetof(DespillContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption despill_options[] = {
    { "type",       "set the screen type",     OFFSET(type),        AV_OPT_TYPE_INT,     {.i64=0},     0,   1, FLAGS, "type" },
    {   "green",    "greenscreen",             0,                   AV_OPT_TYPE_CONST,   {.i64=0},     0,   0, FLAGS, "type" },
    {   "blue",     "bluescreen",              0,                   AV_OPT_TYPE_CONST,   {.i64=1},     0,   0, FLAGS, "type" },
    { "mix",        "set the spillmap mix",    OFFSET(spillmix),    AV_OPT_TYPE_FLOAT,   {.dbl=0.5},   0,   1, FLAGS },
    { "expand",     "set the spillmap expand", OFFSET(spillexpand), AV_OPT_TYPE_FLOAT,   {.dbl=0},     0,   1, FLAGS },
    { "red",        "set red scale",           OFFSET(redscale),    AV_OPT_TYPE_FLOAT,   {.dbl=0},  -100, 100, FLAGS },
    { "green",      "set green scale",         OFFSET(greenscale),  AV_OPT_TYPE_FLOAT,   {.dbl=-1}, -100, 100, FLAGS },
    { "blue",       "set blue scale",          OFFSET(bluescale),   AV_OPT_TYPE_FLOAT,   {.dbl=0},  -100, 100, FLAGS },
    { "brightness", "set brightness",          OFFSET(brightness),  AV_OPT_TYPE_FLOAT,   {.dbl=0},   -10,  10, FLAGS },
    { "alpha",      "change alpha component",  OFFSET(alpha),       AV_OPT_TYPE_BOOL,    {.i64=0},     0,   1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(despill);

const AVFilter ff_vf_despill = {
    .name          = "despill",
    .description   = NULL_IF_CONFIG_SMALL("Despill video."),
    .priv_size     = sizeof(DespillContext),
    .priv_class    = &despill_class,
    FILTER_INPUTS(despill_inputs),
    FILTER_OUTPUTS(despill_outputs),
    FILTER_PIXFMTS_ARRAY(pixel_fmts),
    .process_command = ff_filter_process_command,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};
