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
#include "drawutils.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

#define R 0
#define G 1
#define B 2

typedef struct ColorTemperatureContext {
    const AVClass *class;

    float temperature;
    float mix;
    float preserve;

    float color[3];

    int step;
    int depth;
    uint8_t rgba_map[4];

    int (*do_slice)(AVFilterContext *s, void *arg,
                    int jobnr, int nb_jobs);
} ColorTemperatureContext;

static float saturate(float input)
{
    return av_clipf(input, 0.f, 1.f);
}

static void kelvin2rgb(float k, float *rgb)
{
    float kelvin = k / 100.0f;

    if (kelvin <= 66.0f) {
        rgb[0] = 1.0f;
        rgb[1] = saturate(0.39008157876901960784f * logf(kelvin) - 0.63184144378862745098f);
    } else {
        const float t = fmaxf(kelvin - 60.0f, 0.0f);
        rgb[0] = saturate(1.29293618606274509804f * powf(t, -0.1332047592f));
        rgb[1] = saturate(1.12989086089529411765f * powf(t, -0.0755148492f));
    }

    if (kelvin >= 66.0f)
        rgb[2] = 1.0f;
    else if (kelvin <= 19.0f)
        rgb[2] = 0.0f;
    else
        rgb[2] = saturate(0.54320678911019607843f * logf(kelvin - 10.0f) - 1.19625408914f);
}

static float lerpf(float v0, float v1, float f)
{
    return v0 + (v1 - v0) * f;
}

#define PROCESS()                                                   \
    nr = r * color[0];                                              \
    ng = g * color[1];                                              \
    nb = b * color[2];                                              \
                                                                    \
    nr = lerpf(r, nr, mix);                                         \
    ng = lerpf(g, ng, mix);                                         \
    nb = lerpf(b, nb, mix);                                         \
                                                                    \
    l0 = (FFMAX3(r, g, b) + FFMIN3(r, g, b)) + FLT_EPSILON;         \
    l1 = (FFMAX3(nr, ng, nb) + FFMIN3(nr, ng, nb)) + FLT_EPSILON;   \
    l = l0 / l1;                                                    \
                                                                    \
    r = nr * l;                                                     \
    g = ng * l;                                                     \
    b = nb * l;                                                     \
                                                                    \
    nr = lerpf(nr, r, preserve);                                    \
    ng = lerpf(ng, g, preserve);                                    \
    nb = lerpf(nb, b, preserve);

static int temperature_slice8(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorTemperatureContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int width = frame->width;
    const int height = frame->height;
    const float mix = s->mix;
    const float preserve = s->preserve;
    const float *color = s->color;
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const int glinesize = frame->linesize[0];
    const int blinesize = frame->linesize[1];
    const int rlinesize = frame->linesize[2];
    uint8_t *gptr = frame->data[0] + slice_start * glinesize;
    uint8_t *bptr = frame->data[1] + slice_start * blinesize;
    uint8_t *rptr = frame->data[2] + slice_start * rlinesize;

    for (int y = slice_start; y < slice_end; y++) {
        for (int x = 0; x < width; x++) {
            float g = gptr[x];
            float b = bptr[x];
            float r = rptr[x];
            float nr, ng, nb;
            float l0, l1, l;

            PROCESS()

            gptr[x] = av_clip_uint8(ng);
            bptr[x] = av_clip_uint8(nb);
            rptr[x] = av_clip_uint8(nr);
        }

        gptr += glinesize;
        bptr += blinesize;
        rptr += rlinesize;
    }

    return 0;
}

static int temperature_slice16(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorTemperatureContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int depth = s->depth;
    const int width = frame->width;
    const int height = frame->height;
    const float preserve = s->preserve;
    const float mix = s->mix;
    const float *color = s->color;
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const int glinesize = frame->linesize[0] / sizeof(uint16_t);
    const int blinesize = frame->linesize[1] / sizeof(uint16_t);
    const int rlinesize = frame->linesize[2] / sizeof(uint16_t);
    uint16_t *gptr = (uint16_t *)frame->data[0] + slice_start * glinesize;
    uint16_t *bptr = (uint16_t *)frame->data[1] + slice_start * blinesize;
    uint16_t *rptr = (uint16_t *)frame->data[2] + slice_start * rlinesize;

    for (int y = slice_start; y < slice_end; y++) {
        for (int x = 0; x < width; x++) {
            float g = gptr[x];
            float b = bptr[x];
            float r = rptr[x];
            float nr, ng, nb;
            float l0, l1, l;

            PROCESS()

            gptr[x] = av_clip_uintp2_c(ng, depth);
            bptr[x] = av_clip_uintp2_c(nb, depth);
            rptr[x] = av_clip_uintp2_c(nr, depth);
        }

        gptr += glinesize;
        bptr += blinesize;
        rptr += rlinesize;
    }

    return 0;
}

static int temperature_slice8p(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorTemperatureContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int step = s->step;
    const int width = frame->width;
    const int height = frame->height;
    const float mix = s->mix;
    const float preserve = s->preserve;
    const float *color = s->color;
    const uint8_t roffset = s->rgba_map[R];
    const uint8_t goffset = s->rgba_map[G];
    const uint8_t boffset = s->rgba_map[B];
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const int linesize = frame->linesize[0];
    uint8_t *ptr = frame->data[0] + slice_start * linesize;

    for (int y = slice_start; y < slice_end; y++) {
        for (int x = 0; x < width; x++) {
            float g = ptr[x * step + goffset];
            float b = ptr[x * step + boffset];
            float r = ptr[x * step + roffset];
            float nr, ng, nb;
            float l0, l1, l;

            PROCESS()

            ptr[x * step + goffset] = av_clip_uint8(ng);
            ptr[x * step + boffset] = av_clip_uint8(nb);
            ptr[x * step + roffset] = av_clip_uint8(nr);
        }

        ptr += linesize;
    }

    return 0;
}

static int temperature_slice16p(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorTemperatureContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int step = s->step;
    const int depth = s->depth;
    const int width = frame->width;
    const int height = frame->height;
    const float preserve = s->preserve;
    const float mix = s->mix;
    const float *color = s->color;
    const uint8_t roffset = s->rgba_map[R];
    const uint8_t goffset = s->rgba_map[G];
    const uint8_t boffset = s->rgba_map[B];
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const int linesize = frame->linesize[0] / sizeof(uint16_t);
    uint16_t *ptr = (uint16_t *)frame->data[0] + slice_start * linesize;

    for (int y = slice_start; y < slice_end; y++) {
        for (int x = 0; x < width; x++) {
            float g = ptr[x * step + goffset];
            float b = ptr[x * step + boffset];
            float r = ptr[x * step + roffset];
            float nr, ng, nb;
            float l0, l1, l;

            PROCESS()

            ptr[x * step + goffset] = av_clip_uintp2_c(ng, depth);
            ptr[x * step + boffset] = av_clip_uintp2_c(nb, depth);
            ptr[x * step + roffset] = av_clip_uintp2_c(nr, depth);
        }

        ptr += linesize;
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    ColorTemperatureContext *s = ctx->priv;

    kelvin2rgb(s->temperature, s->color);

    ff_filter_execute(ctx, s->do_slice, frame, NULL,
                      FFMIN(frame->height, ff_filter_get_nb_threads(ctx)));

    return ff_filter_frame(ctx->outputs[0], frame);
}

static const enum AVPixelFormat pixel_fmts[] = {
    AV_PIX_FMT_RGB24, AV_PIX_FMT_BGR24,
    AV_PIX_FMT_RGBA, AV_PIX_FMT_BGRA,
    AV_PIX_FMT_ARGB, AV_PIX_FMT_ABGR,
    AV_PIX_FMT_0RGB, AV_PIX_FMT_0BGR,
    AV_PIX_FMT_RGB0, AV_PIX_FMT_BGR0,
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP,
    AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRP12,
    AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
    AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRAP16,
    AV_PIX_FMT_RGB48,  AV_PIX_FMT_BGR48,
    AV_PIX_FMT_RGBA64, AV_PIX_FMT_BGRA64,
    AV_PIX_FMT_NONE
};

static av_cold int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ColorTemperatureContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int planar = desc->flags & AV_PIX_FMT_FLAG_PLANAR;

    s->step = desc->nb_components;
    if (inlink->format == AV_PIX_FMT_RGB0 ||
        inlink->format == AV_PIX_FMT_0RGB ||
        inlink->format == AV_PIX_FMT_BGR0 ||
        inlink->format == AV_PIX_FMT_0BGR)
        s->step = 4;

    s->depth = desc->comp[0].depth;
    s->do_slice = s->depth <= 8 ? temperature_slice8 : temperature_slice16;
    if (!planar)
        s->do_slice = s->depth <= 8 ? temperature_slice8p : temperature_slice16p;

    ff_fill_rgba_map(s->rgba_map, inlink->format);

    return 0;
}

static const AVFilterPad inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .flags          = AVFILTERPAD_FLAG_NEEDS_WRITABLE,
        .filter_frame   = filter_frame,
        .config_props   = config_input,
    },
};

static const AVFilterPad outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

#define OFFSET(x) offsetof(ColorTemperatureContext, x)
#define VF AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption colortemperature_options[] = {
    { "temperature", "set the temperature in Kelvin",          OFFSET(temperature), AV_OPT_TYPE_FLOAT, {.dbl=6500}, 1000,  40000, VF },
    { "mix",         "set the mix with filtered output",       OFFSET(mix),         AV_OPT_TYPE_FLOAT, {.dbl=1},       0,      1, VF },
    { "pl",          "set the amount of preserving lightness", OFFSET(preserve),    AV_OPT_TYPE_FLOAT, {.dbl=0},       0,      1, VF },
    { NULL }
};

AVFILTER_DEFINE_CLASS(colortemperature);

const AVFilter ff_vf_colortemperature = {
    .name          = "colortemperature",
    .description   = NULL_IF_CONFIG_SMALL("Adjust color temperature of video."),
    .priv_size     = sizeof(ColorTemperatureContext),
    .priv_class    = &colortemperature_class,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_PIXFMTS_ARRAY(pixel_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
