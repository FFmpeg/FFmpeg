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
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "drawutils.h"
#include "filters.h"
#include "video.h"

#define R 0
#define G 1
#define B 2
#define A 3

typedef struct VibranceContext {
    const AVClass *class;

    float intensity;
    float balance[3];
    float lcoeffs[3];
    int alternate;

    int step;
    int depth;
    uint8_t rgba_map[4];

    int (*do_slice)(AVFilterContext *s, void *arg,
                    int jobnr, int nb_jobs);
} VibranceContext;

static inline float lerpf(float v0, float v1, float f)
{
    return v0 + (v1 - v0) * f;
}

typedef struct ThreadData {
    AVFrame *out, *in;
} ThreadData;

static int vibrance_slice8(AVFilterContext *avctx, void *arg, int jobnr, int nb_jobs)
{
    VibranceContext *s = avctx->priv;
    ThreadData *td = arg;
    AVFrame *frame = td->out;
    AVFrame *in = td->in;
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
    const ptrdiff_t glinesize = frame->linesize[0];
    const ptrdiff_t blinesize = frame->linesize[1];
    const ptrdiff_t rlinesize = frame->linesize[2];
    const ptrdiff_t alinesize = frame->linesize[3];
    const ptrdiff_t gslinesize = in->linesize[0];
    const ptrdiff_t bslinesize = in->linesize[1];
    const ptrdiff_t rslinesize = in->linesize[2];
    const ptrdiff_t aslinesize = in->linesize[3];
    const uint8_t *gsrc = in->data[0] + slice_start * glinesize;
    const uint8_t *bsrc = in->data[1] + slice_start * blinesize;
    const uint8_t *rsrc = in->data[2] + slice_start * rlinesize;
    uint8_t *gptr = frame->data[0] + slice_start * glinesize;
    uint8_t *bptr = frame->data[1] + slice_start * blinesize;
    uint8_t *rptr = frame->data[2] + slice_start * rlinesize;
    const uint8_t *asrc = in->data[3];
    uint8_t *aptr = frame->data[3];

    for (int y = slice_start; y < slice_end; y++) {
        for (int x = 0; x < width; x++) {
            float g = gsrc[x] * scale;
            float b = bsrc[x] * scale;
            float r = rsrc[x] * scale;
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

        if (aptr && alinesize && frame != in)
            memcpy(aptr + alinesize * y, asrc + aslinesize * y, width);

        gsrc += gslinesize;
        bsrc += bslinesize;
        rsrc += rslinesize;
        gptr += glinesize;
        bptr += blinesize;
        rptr += rlinesize;
    }

    return 0;
}

static int vibrance_slice16(AVFilterContext *avctx, void *arg, int jobnr, int nb_jobs)
{
    VibranceContext *s = avctx->priv;
    ThreadData *td = arg;
    AVFrame *frame = td->out;
    AVFrame *in = td->in;
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
    const ptrdiff_t gslinesize = in->linesize[0] / 2;
    const ptrdiff_t bslinesize = in->linesize[1] / 2;
    const ptrdiff_t rslinesize = in->linesize[2] / 2;
    const ptrdiff_t aslinesize = in->linesize[3] / 2;
    const ptrdiff_t glinesize = frame->linesize[0] / 2;
    const ptrdiff_t blinesize = frame->linesize[1] / 2;
    const ptrdiff_t rlinesize = frame->linesize[2] / 2;
    const ptrdiff_t alinesize = frame->linesize[3] / 2;
    const uint16_t *gsrc = (const uint16_t *)in->data[0] + slice_start * gslinesize;
    const uint16_t *bsrc = (const uint16_t *)in->data[1] + slice_start * bslinesize;
    const uint16_t *rsrc = (const uint16_t *)in->data[2] + slice_start * rslinesize;
    uint16_t *gptr = (uint16_t *)frame->data[0] + slice_start * glinesize;
    uint16_t *bptr = (uint16_t *)frame->data[1] + slice_start * blinesize;
    uint16_t *rptr = (uint16_t *)frame->data[2] + slice_start * rlinesize;
    const uint16_t *asrc = (const uint16_t *)in->data[3];
    uint16_t *aptr = (uint16_t *)frame->data[3];

    for (int y = slice_start; y < slice_end; y++) {
        for (int x = 0; x < width; x++) {
            float g = gsrc[x] * scale;
            float b = bsrc[x] * scale;
            float r = rsrc[x] * scale;
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

        if (aptr && alinesize && frame != in)
            memcpy(aptr + alinesize * y, asrc + aslinesize * y, width * 2);

        gsrc += gslinesize;
        bsrc += bslinesize;
        rsrc += rslinesize;
        gptr += glinesize;
        bptr += blinesize;
        rptr += rlinesize;
    }

    return 0;
}

static int vibrance_slice8p(AVFilterContext *avctx, void *arg, int jobnr, int nb_jobs)
{
    VibranceContext *s = avctx->priv;
    ThreadData *td = arg;
    AVFrame *frame = td->out;
    AVFrame *in = td->in;
    const int step = s->step;
    const int width = frame->width;
    const int height = frame->height;
    const float scale = 1.f / 255.f;
    const float gc = s->lcoeffs[0];
    const float bc = s->lcoeffs[1];
    const float rc = s->lcoeffs[2];
    const uint8_t roffset = s->rgba_map[R];
    const uint8_t goffset = s->rgba_map[G];
    const uint8_t boffset = s->rgba_map[B];
    const uint8_t aoffset = s->rgba_map[A];
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
    const ptrdiff_t linesize = frame->linesize[0];
    const ptrdiff_t slinesize = in->linesize[0];
    const uint8_t *src = in->data[0] + slice_start * slinesize;
    uint8_t *ptr = frame->data[0] + slice_start * linesize;

    for (int y = slice_start; y < slice_end; y++) {
        for (int x = 0; x < width; x++) {
            float g = src[x * step + goffset] * scale;
            float b = src[x * step + boffset] * scale;
            float r = src[x * step + roffset] * scale;
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

            ptr[x * step + goffset] = av_clip_uint8(g * 255.f);
            ptr[x * step + boffset] = av_clip_uint8(b * 255.f);
            ptr[x * step + roffset] = av_clip_uint8(r * 255.f);

            if (frame != in)
                ptr[x * step + aoffset] = src[x * step + aoffset];
        }

        ptr += linesize;
        src += slinesize;
    }

    return 0;
}

static int vibrance_slice16p(AVFilterContext *avctx, void *arg, int jobnr, int nb_jobs)
{
    VibranceContext *s = avctx->priv;
    ThreadData *td = arg;
    AVFrame *frame = td->out;
    AVFrame *in = td->in;
    const int step = s->step;
    const int depth = s->depth;
    const float max = (1 << depth) - 1;
    const float scale = 1.f / max;
    const float gc = s->lcoeffs[0];
    const float bc = s->lcoeffs[1];
    const float rc = s->lcoeffs[2];
    const uint8_t roffset = s->rgba_map[R];
    const uint8_t goffset = s->rgba_map[G];
    const uint8_t boffset = s->rgba_map[B];
    const uint8_t aoffset = s->rgba_map[A];
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
    const ptrdiff_t linesize = frame->linesize[0] / 2;
    const ptrdiff_t slinesize = in->linesize[0] / 2;
    const uint16_t *src = (const uint16_t *)in->data[0] + slice_start * slinesize;
    uint16_t *ptr = (uint16_t *)frame->data[0] + slice_start * linesize;

    for (int y = slice_start; y < slice_end; y++) {
        for (int x = 0; x < width; x++) {
            float g = src[x * step + goffset] * scale;
            float b = src[x * step + boffset] * scale;
            float r = src[x * step + roffset] * scale;
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

            ptr[x * step + goffset] = av_clip_uintp2_c(g * max, depth);
            ptr[x * step + boffset] = av_clip_uintp2_c(b * max, depth);
            ptr[x * step + roffset] = av_clip_uintp2_c(r * max, depth);
            if (frame != in)
                ptr[x * step + aoffset] = src[x * step + aoffset];
        }

        ptr += linesize;
        src += slinesize;
    }

    return 0;
}

static int filter_frame(AVFilterLink *link, AVFrame *in)
{
    AVFilterContext *avctx = link->dst;
    AVFilterLink *outlink = avctx->outputs[0];
    VibranceContext *s = avctx->priv;
    ThreadData td;
    AVFrame *out;
    int res;

    if (av_frame_is_writable(in)) {
        out = in;
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }

    td.out = out;
    td.in = in;
    if (res = ff_filter_execute(avctx, s->do_slice, &td, NULL,
                                FFMIN(out->height, ff_filter_get_nb_threads(avctx))))
        return res;

    if (out != in)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
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
    AVFilterContext *avctx = inlink->dst;
    VibranceContext *s = avctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int planar = desc->flags & AV_PIX_FMT_FLAG_PLANAR;

    s->step = desc->nb_components;
    if (inlink->format == AV_PIX_FMT_RGB0 ||
        inlink->format == AV_PIX_FMT_0RGB ||
        inlink->format == AV_PIX_FMT_BGR0 ||
        inlink->format == AV_PIX_FMT_0BGR)
        s->step = 4;

    s->depth = desc->comp[0].depth;
    s->do_slice = s->depth <= 8 ? vibrance_slice8 : vibrance_slice16;
    if (!planar)
        s->do_slice = s->depth <= 8 ? vibrance_slice8p : vibrance_slice16p;

    ff_fill_rgba_map(s->rgba_map, inlink->format);

    return 0;
}

static const AVFilterPad vibrance_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .filter_frame   = filter_frame,
        .config_props   = config_input,
    },
};

#define OFFSET(x) offsetof(VibranceContext, x)
#define VF AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

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

const FFFilter ff_vf_vibrance = {
    .p.name        = "vibrance",
    .p.description = NULL_IF_CONFIG_SMALL("Boost or alter saturation."),
    .p.priv_class  = &vibrance_class,
    .p.flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .priv_size     = sizeof(VibranceContext),
    FILTER_INPUTS(vibrance_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS_ARRAY(pixel_fmts),
    .process_command = ff_filter_process_command,
};
