/*
 * Copyright (c) 2018 Paul B Mahol
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

typedef struct VibranceContext {
    const AVClass *class;

    float intensity;
    float balance[3];
    float lcoeffs[3];
    int alternate;

    int depth;

    int (*do_slice)(AVFilterContext *s, void *arg,
                    int jobnr, int nb_jobs);
} VibranceContext;

static inline float lerpf(float v0, float v1, float f)
{
    return v0 + (v1 - v0) * f;
}

static int vibrance_slice8(AVFilterContext *avctx, void *arg, int jobnr, int nb_jobs)
{
    VibranceContext *s = avctx->priv;
    AVFrame *frame = arg;
    const int width = frame->width;
    const int height = frame->height;
    const float scale = 1.f / 255.f;
    const float gc = s->lcoeffs[0];
    const float bc = s->lcoeffs[1];
    const float rc = s->lcoeffs[2];
    const float intensity = s->intensity;
    const float alternate = s->alternate ? 1.f : -1.f;
    const float gintensity = intensity * s->balance[0];
    const float bintensity = intensity * s->balance[1];
    const float rintensity = intensity * s->balance[2];
    const float sgintensity = alternate * FFSIGN(gintensity);
    const float sbintensity = alternate * FFSIGN(bintensity);
    const float srintensity = alternate * FFSIGN(rintensity);
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
            float g = gptr[x] * scale;
            float b = bptr[x] * scale;
            float r = rptr[x] * scale;
            float max_color = FFMAX3(r, g, b);
            float min_color = FFMIN3(r, g, b);
            float color_saturation = max_color - min_color;
            float luma = g * gc + r * rc + b * bc;
            const float cg = 1.f + gintensity * (1.f - sgintensity * color_saturation);
            const float cb = 1.f + bintensity * (1.f - sbintensity * color_saturation);
            const float cr = 1.f + rintensity * (1.f - srintensity * color_saturation);

            g = lerpf(luma, g, cg);
            b = lerpf(luma, b, cb);
            r = lerpf(luma, r, cr);

            gptr[x] = av_clip_uint8(g * 255.f);
            bptr[x] = av_clip_uint8(b * 255.f);
            rptr[x] = av_clip_uint8(r * 255.f);
        }

        gptr += glinesize;
        bptr += blinesize;
        rptr += rlinesize;
    }

    return 0;
}

static int vibrance_slice16(AVFilterContext *avctx, void *arg, int jobnr, int nb_jobs)
{
    VibranceContext *s = avctx->priv;
    AVFrame *frame = arg;
    const int depth = s->depth;
    const float max = (1 << depth) - 1;
    const float scale = 1.f / max;
    const float gc = s->lcoeffs[0];
    const float bc = s->lcoeffs[1];
    const float rc = s->lcoeffs[2];
    const int width = frame->width;
    const int height = frame->height;
    const float intensity = s->intensity;
    const float alternate = s->alternate ? 1.f : -1.f;
    const float gintensity = intensity * s->balance[0];
    const float bintensity = intensity * s->balance[1];
    const float rintensity = intensity * s->balance[2];
    const float sgintensity = alternate * FFSIGN(gintensity);
    const float sbintensity = alternate * FFSIGN(bintensity);
    const float srintensity = alternate * FFSIGN(rintensity);
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const int glinesize = frame->linesize[0] / 2;
    const int blinesize = frame->linesize[1] / 2;
    const int rlinesize = frame->linesize[2] / 2;
    uint16_t *gptr = (uint16_t *)frame->data[0] + slice_start * glinesize;
    uint16_t *bptr = (uint16_t *)frame->data[1] + slice_start * blinesize;
    uint16_t *rptr = (uint16_t *)frame->data[2] + slice_start * rlinesize;

    for (int y = slice_start; y < slice_end; y++) {
        for (int x = 0; x < width; x++) {
            float g = gptr[x] * scale;
            float b = bptr[x] * scale;
            float r = rptr[x] * scale;
            float max_color = FFMAX3(r, g, b);
            float min_color = FFMIN3(r, g, b);
            float color_saturation = max_color - min_color;
            float luma = g * gc + r * rc + b * bc;
            const float cg = 1.f + gintensity * (1.f - sgintensity * color_saturation);
            const float cb = 1.f + bintensity * (1.f - sbintensity * color_saturation);
            const float cr = 1.f + rintensity * (1.f - srintensity * color_saturation);

            g = lerpf(luma, g, cg);
            b = lerpf(luma, b, cb);
            r = lerpf(luma, r, cr);

            gptr[x] = av_clip_uintp2_c(g * max, depth);
            bptr[x] = av_clip_uintp2_c(b * max, depth);
            rptr[x] = av_clip_uintp2_c(r * max, depth);
        }

        gptr += glinesize;
        bptr += blinesize;
        rptr += rlinesize;
    }

    return 0;
}

static int filter_frame(AVFilterLink *link, AVFrame *frame)
{
    AVFilterContext *avctx = link->dst;
    VibranceContext *s = avctx->priv;
    int res;

    if (res = avctx->internal->execute(avctx, s->do_slice, frame, NULL,
                                       FFMIN(frame->height, ff_filter_get_nb_threads(avctx))))
        return res;

    return ff_filter_frame(avctx->outputs[0], frame);
}

static av_cold int query_formats(AVFilterContext *avctx)
{
    static const enum AVPixelFormat pixel_fmts[] = {
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP,
        AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRP12,
        AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
        AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRAP16,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *formats = NULL;

    formats = ff_make_format_list(pixel_fmts);
    if (!formats)
        return AVERROR(ENOMEM);

    return ff_set_common_formats(avctx, formats);
}

static av_cold int config_input(AVFilterLink *inlink)
{
    AVFilterContext *avctx = inlink->dst;
    VibranceContext *s = avctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    s->depth = desc->comp[0].depth;
    s->do_slice = s->depth <= 8 ? vibrance_slice8 : vibrance_slice16;

    return 0;
}

static const AVFilterPad vibrance_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .needs_writable = 1,
        .filter_frame   = filter_frame,
        .config_props   = config_input,
    },
    { NULL }
};

static const AVFilterPad vibrance_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

#define OFFSET(x) offsetof(VibranceContext, x)
#define VF AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption vibrance_options[] = {
    { "intensity", "set the intensity value",   OFFSET(intensity),  AV_OPT_TYPE_FLOAT, {.dbl=0},       -2,  2, VF },
    { "rbal", "set the red balance value",      OFFSET(balance[2]), AV_OPT_TYPE_FLOAT, {.dbl=1},      -10, 10, VF },
    { "gbal", "set the green balance value",    OFFSET(balance[0]), AV_OPT_TYPE_FLOAT, {.dbl=1},      -10, 10, VF },
    { "bbal", "set the blue balance value",     OFFSET(balance[1]), AV_OPT_TYPE_FLOAT, {.dbl=1},      -10, 10, VF },
    { "rlum", "set the red luma coefficient",   OFFSET(lcoeffs[2]), AV_OPT_TYPE_FLOAT, {.dbl=0.072186}, 0,  1, VF },
    { "glum", "set the green luma coefficient", OFFSET(lcoeffs[0]), AV_OPT_TYPE_FLOAT, {.dbl=0.715158}, 0,  1, VF },
    { "blum", "set the blue luma coefficient",  OFFSET(lcoeffs[1]), AV_OPT_TYPE_FLOAT, {.dbl=0.212656}, 0,  1, VF },
    { "alternate", "use alternate colors",      OFFSET(alternate),  AV_OPT_TYPE_BOOL,  {.i64=0},        0,  1, VF },
    { NULL }
};

AVFILTER_DEFINE_CLASS(vibrance);

AVFilter ff_vf_vibrance = {
    .name          = "vibrance",
    .description   = NULL_IF_CONFIG_SMALL("Boost or alter saturation."),
    .priv_size     = sizeof(VibranceContext),
    .priv_class    = &vibrance_class,
    .query_formats = query_formats,
    .inputs        = vibrance_inputs,
    .outputs       = vibrance_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};
