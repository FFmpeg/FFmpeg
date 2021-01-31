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

typedef struct ColorContrastContext {
    const AVClass *class;

    float rc, gm, by;
    float rcw, gmw, byw;
    float preserve;

    int step;
    int depth;
    uint8_t rgba_map[4];

    int (*do_slice)(AVFilterContext *s, void *arg,
                    int jobnr, int nb_jobs);
} ColorContrastContext;

static inline float lerpf(float v0, float v1, float f)
{
    return v0 + (v1 - v0) * f;
}

#define PROCESS(max)                                                    \
    br = (b + r) * 0.5f;                                                \
    gb = (g + b) * 0.5f;                                                \
    rg = (r + g) * 0.5f;                                                \
                                                                        \
    gd = g - br;                                                        \
    bd = b - rg;                                                        \
    rd = r - gb;                                                        \
                                                                        \
    g0 = g + gd * gm;                                                   \
    b0 = b - gd * gm;                                                   \
    r0 = r - gd * gm;                                                   \
                                                                        \
    g1 = g - bd * by;                                                   \
    b1 = b + bd * by;                                                   \
    r1 = r - bd * by;                                                   \
                                                                        \
    g2 = g - rd * rc;                                                   \
    b2 = b - rd * rc;                                                   \
    r2 = r + rd * rc;                                                   \
                                                                        \
    ng = av_clipf((g0 * gmw + g1 * byw + g2 * rcw) * scale, 0.f, max);  \
    nb = av_clipf((b0 * gmw + b1 * byw + b2 * rcw) * scale, 0.f, max);  \
    nr = av_clipf((r0 * gmw + r1 * byw + r2 * rcw) * scale, 0.f, max);  \
                                                                        \
    li = FFMAX3(r, g, b) + FFMIN3(r, g, b);                             \
    lo = FFMAX3(nr, ng, nb) + FFMIN3(nr, ng, nb) + FLT_EPSILON;         \
    lf = li / lo;                                                       \
                                                                        \
    r = nr * lf;                                                        \
    g = ng * lf;                                                        \
    b = nb * lf;                                                        \
                                                                        \
    nr = lerpf(nr, r, preserve);                                        \
    ng = lerpf(ng, g, preserve);                                        \
    nb = lerpf(nb, b, preserve);

static int colorcontrast_slice8(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorContrastContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int width = frame->width;
    const int height = frame->height;
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const int glinesize = frame->linesize[0];
    const int blinesize = frame->linesize[1];
    const int rlinesize = frame->linesize[2];
    uint8_t *gptr = frame->data[0] + slice_start * glinesize;
    uint8_t *bptr = frame->data[1] + slice_start * blinesize;
    uint8_t *rptr = frame->data[2] + slice_start * rlinesize;
    const float preserve = s->preserve;
    const float gm = s->gm * 0.5f;
    const float by = s->by * 0.5f;
    const float rc = s->rc * 0.5f;
    const float gmw = s->gmw;
    const float byw = s->byw;
    const float rcw = s->rcw;
    const float sum = gmw + byw + rcw;
    const float scale = 1.f / sum;

    for (int y = slice_start; y < slice_end && sum > FLT_EPSILON; y++) {
        for (int x = 0; x < width; x++) {
            float g = gptr[x];
            float b = bptr[x];
            float r = rptr[x];
            float g0, g1, g2;
            float b0, b1, b2;
            float r0, r1, r2;
            float gd, bd, rd;
            float gb, br, rg;
            float nr, ng, nb;
            float li, lo, lf;

            PROCESS(255.f);

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

static int colorcontrast_slice16(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorContrastContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int depth = s->depth;
    const float max = (1 << depth) - 1;
    const int width = frame->width;
    const int height = frame->height;
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const int glinesize = frame->linesize[0] / 2;
    const int blinesize = frame->linesize[1] / 2;
    const int rlinesize = frame->linesize[2] / 2;
    uint16_t *gptr = (uint16_t *)frame->data[0] + slice_start * glinesize;
    uint16_t *bptr = (uint16_t *)frame->data[1] + slice_start * blinesize;
    uint16_t *rptr = (uint16_t *)frame->data[2] + slice_start * rlinesize;
    const float preserve = s->preserve;
    const float gm = s->gm * 0.5f;
    const float by = s->by * 0.5f;
    const float rc = s->rc * 0.5f;
    const float gmw = s->gmw;
    const float byw = s->byw;
    const float rcw = s->rcw;
    const float sum = gmw + byw + rcw;
    const float scale = 1.f / sum;

    for (int y = slice_start; y < slice_end && sum > FLT_EPSILON; y++) {
        for (int x = 0; x < width; x++) {
            float g = gptr[x];
            float b = bptr[x];
            float r = rptr[x];
            float g0, g1, g2;
            float b0, b1, b2;
            float r0, r1, r2;
            float gd, bd, rd;
            float gb, br, rg;
            float nr, ng, nb;
            float li, lo, lf;

            PROCESS(max);

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

static int colorcontrast_slice8p(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorContrastContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int step = s->step;
    const int width = frame->width;
    const int height = frame->height;
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const int linesize = frame->linesize[0];
    const uint8_t roffset = s->rgba_map[R];
    const uint8_t goffset = s->rgba_map[G];
    const uint8_t boffset = s->rgba_map[B];
    uint8_t *ptr = frame->data[0] + slice_start * linesize;
    const float preserve = s->preserve;
    const float gm = s->gm * 0.5f;
    const float by = s->by * 0.5f;
    const float rc = s->rc * 0.5f;
    const float gmw = s->gmw;
    const float byw = s->byw;
    const float rcw = s->rcw;
    const float sum = gmw + byw + rcw;
    const float scale = 1.f / sum;

    for (int y = slice_start; y < slice_end && sum > FLT_EPSILON; y++) {
        for (int x = 0; x < width; x++) {
            float g = ptr[x * step + goffset];
            float b = ptr[x * step + boffset];
            float r = ptr[x * step + roffset];
            float g0, g1, g2;
            float b0, b1, b2;
            float r0, r1, r2;
            float gd, bd, rd;
            float gb, br, rg;
            float nr, ng, nb;
            float li, lo, lf;

            PROCESS(255.f);

            ptr[x * step + goffset] = av_clip_uint8(ng);
            ptr[x * step + boffset] = av_clip_uint8(nb);
            ptr[x * step + roffset] = av_clip_uint8(nr);
        }

        ptr += linesize;
    }

    return 0;
}

static int colorcontrast_slice16p(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorContrastContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int step = s->step;
    const int depth = s->depth;
    const float max = (1 << depth) - 1;
    const int width = frame->width;
    const int height = frame->height;
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const int linesize = frame->linesize[0] / 2;
    const uint8_t roffset = s->rgba_map[R];
    const uint8_t goffset = s->rgba_map[G];
    const uint8_t boffset = s->rgba_map[B];
    uint16_t *ptr = (uint16_t *)frame->data[0] + slice_start * linesize;
    const float preserve = s->preserve;
    const float gm = s->gm * 0.5f;
    const float by = s->by * 0.5f;
    const float rc = s->rc * 0.5f;
    const float gmw = s->gmw;
    const float byw = s->byw;
    const float rcw = s->rcw;
    const float sum = gmw + byw + rcw;
    const float scale = 1.f / sum;

    for (int y = slice_start; y < slice_end && sum > FLT_EPSILON; y++) {
        for (int x = 0; x < width; x++) {
            float g = ptr[x * step + goffset];
            float b = ptr[x * step + boffset];
            float r = ptr[x * step + roffset];
            float g0, g1, g2;
            float b0, b1, b2;
            float r0, r1, r2;
            float gd, bd, rd;
            float gb, br, rg;
            float nr, ng, nb;
            float li, lo, lf;

            PROCESS(max);

            ptr[x * step + goffset] = av_clip_uintp2_c(ng, depth);
            ptr[x * step + boffset] = av_clip_uintp2_c(nb, depth);
            ptr[x * step + roffset] = av_clip_uintp2_c(nr, depth);
        }

        ptr += linesize;
    }

    return 0;
}

static int filter_frame(AVFilterLink *link, AVFrame *frame)
{
    AVFilterContext *ctx = link->dst;
    ColorContrastContext *s = ctx->priv;
    int res;

    if (res = ctx->internal->execute(ctx, s->do_slice, frame, NULL,
                                       FFMIN(frame->height, ff_filter_get_nb_threads(ctx))))
        return res;

    return ff_filter_frame(ctx->outputs[0], frame);
}

static av_cold int query_formats(AVFilterContext *ctx)
{
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

    AVFilterFormats *formats = NULL;

    formats = ff_make_format_list(pixel_fmts);
    if (!formats)
        return AVERROR(ENOMEM);

    return ff_set_common_formats(ctx, formats);
}

static av_cold int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ColorContrastContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int planar = desc->flags & AV_PIX_FMT_FLAG_PLANAR;

    s->step = desc->nb_components;
    if (inlink->format == AV_PIX_FMT_RGB0 ||
        inlink->format == AV_PIX_FMT_0RGB ||
        inlink->format == AV_PIX_FMT_BGR0 ||
        inlink->format == AV_PIX_FMT_0BGR)
        s->step = 4;

    s->depth = desc->comp[0].depth;
    s->do_slice = s->depth <= 8 ? colorcontrast_slice8 : colorcontrast_slice16;
    if (!planar)
        s->do_slice = s->depth <= 8 ? colorcontrast_slice8p : colorcontrast_slice16p;

    ff_fill_rgba_map(s->rgba_map, inlink->format);

    return 0;
}

static const AVFilterPad colorcontrast_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .needs_writable = 1,
        .filter_frame   = filter_frame,
        .config_props   = config_input,
    },
    { NULL }
};

static const AVFilterPad colorcontrast_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

#define OFFSET(x) offsetof(ColorContrastContext, x)
#define VF AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption colorcontrast_options[] = {
    { "rc",  "set the red-cyan contrast",      OFFSET(rc),  AV_OPT_TYPE_FLOAT, {.dbl=0}, -1, 1, VF },
    { "gm",  "set the green-magenta contrast", OFFSET(gm),  AV_OPT_TYPE_FLOAT, {.dbl=0}, -1, 1, VF },
    { "by",  "set the blue-yellow contrast",   OFFSET(by),  AV_OPT_TYPE_FLOAT, {.dbl=0}, -1, 1, VF },
    { "rcw", "set the red-cyan weight",        OFFSET(rcw), AV_OPT_TYPE_FLOAT, {.dbl=0},  0, 1, VF },
    { "gmw", "set the green-magenta weight",   OFFSET(gmw), AV_OPT_TYPE_FLOAT, {.dbl=0},  0, 1, VF },
    { "byw", "set the blue-yellow weight",     OFFSET(byw), AV_OPT_TYPE_FLOAT, {.dbl=0},  0, 1, VF },
    { "pl",  "set the amount of preserving lightness", OFFSET(preserve), AV_OPT_TYPE_FLOAT, {.dbl=0}, 0, 1, VF },
    { NULL }
};

AVFILTER_DEFINE_CLASS(colorcontrast);

AVFilter ff_vf_colorcontrast = {
    .name          = "colorcontrast",
    .description   = NULL_IF_CONFIG_SMALL("Adjust color contrast between RGB components."),
    .priv_size     = sizeof(ColorContrastContext),
    .priv_class    = &colorcontrast_class,
    .query_formats = query_formats,
    .inputs        = colorcontrast_inputs,
    .outputs       = colorcontrast_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
