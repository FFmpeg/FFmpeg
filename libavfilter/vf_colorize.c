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

#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct ColorizeContext {
    const AVClass *class;

    float hue;
    float saturation;
    float lightness;
    float mix;

    int depth;
    int c[3];
    int planewidth[4];
    int planeheight[4];

    int (*do_plane_slice[2])(AVFilterContext *s, void *arg,
                             int jobnr, int nb_jobs);
} ColorizeContext;

static inline float lerpf(float v0, float v1, float f)
{
    return v0 + (v1 - v0) * f;
}

static int colorizey_slice8(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorizeContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int width = s->planewidth[0];
    const int height = s->planeheight[0];
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const int ylinesize = frame->linesize[0];
    uint8_t *yptr = frame->data[0] + slice_start * ylinesize;
    const int yv = s->c[0];
    const float mix = s->mix;

    for (int y = slice_start; y < slice_end; y++) {
        for (int x = 0; x < width; x++)
            yptr[x] = lerpf(yv, yptr[x], mix);

        yptr += ylinesize;
    }

    return 0;
}

static int colorizey_slice16(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorizeContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int width = s->planewidth[0];
    const int height = s->planeheight[0];
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const int ylinesize = frame->linesize[0] / 2;
    uint16_t *yptr = (uint16_t *)frame->data[0] + slice_start * ylinesize;
    const int yv = s->c[0];
    const float mix = s->mix;

    for (int y = slice_start; y < slice_end; y++) {
        for (int x = 0; x < width; x++)
            yptr[x] = lerpf(yv, yptr[x], mix);

        yptr += ylinesize;
    }

    return 0;
}

static int colorize_slice8(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorizeContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int width = s->planewidth[1];
    const int height = s->planeheight[1];
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const int ulinesize = frame->linesize[1];
    const int vlinesize = frame->linesize[2];
    uint8_t *uptr = frame->data[1] + slice_start * ulinesize;
    uint8_t *vptr = frame->data[2] + slice_start * vlinesize;
    const int u = s->c[1];
    const int v = s->c[2];

    for (int y = slice_start; y < slice_end; y++) {
        for (int x = 0; x < width; x++) {
            uptr[x] = u;
            vptr[x] = v;
        }

        uptr += ulinesize;
        vptr += vlinesize;
    }

    return 0;
}

static int colorize_slice16(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorizeContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int width = s->planewidth[1];
    const int height = s->planeheight[1];
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const int ulinesize = frame->linesize[1] / 2;
    const int vlinesize = frame->linesize[2] / 2;
    uint16_t *uptr = (uint16_t *)frame->data[1] + slice_start * ulinesize;
    uint16_t *vptr = (uint16_t *)frame->data[2] + slice_start * vlinesize;
    const int u = s->c[1];
    const int v = s->c[2];

    for (int y = slice_start; y < slice_end; y++) {
        for (int x = 0; x < width; x++) {
            uptr[x] = u;
            vptr[x] = v;
        }

        uptr += ulinesize;
        vptr += vlinesize;
    }

    return 0;
}

static int do_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorizeContext *s = ctx->priv;

    s->do_plane_slice[0](ctx, arg, jobnr, nb_jobs);
    s->do_plane_slice[1](ctx, arg, jobnr, nb_jobs);

    return 0;
}

static float hue2rgb(float p, float q, float t)
{
    if (t < 0.f) t += 1.f;
    if (t > 1.f) t -= 1.f;
    if (t < 1.f/6.f) return p + (q - p) * 6.f * t;
    if (t < 1.f/2.f) return q;
    if (t < 2.f/3.f) return p + (q - p) * (2.f/3.f - t) * 6.f;

    return p;
}

static void hsl2rgb(float h, float s, float l, float *r, float *g, float *b)
{
    h /= 360.f;

    if (s == 0.f) {
        *r = *g = *b = l;
    } else {
        const float q = l < 0.5f ? l * (1.f + s) : l + s - l * s;
        const float p = 2.f * l - q;

        *r = hue2rgb(p, q, h + 1.f / 3.f);
        *g = hue2rgb(p, q, h);
        *b = hue2rgb(p, q, h - 1.f / 3.f);
    }
}

static void rgb2yuv(float r, float g, float b, int *y, int *u, int *v, int depth)
{
    *y = ((0.21260*219.0/255.0) * r + (0.71520*219.0/255.0) * g +
         (0.07220*219.0/255.0) * b) * ((1 << depth) - 1);
    *u = (-(0.11457*224.0/255.0) * r - (0.38543*224.0/255.0) * g +
         (0.50000*224.0/255.0) * b + 0.5) * ((1 << depth) - 1);
    *v = ((0.50000*224.0/255.0) * r - (0.45415*224.0/255.0) * g -
         (0.04585*224.0/255.0) * b + 0.5) * ((1 << depth) - 1);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    ColorizeContext *s = ctx->priv;
    float c[3];

    hsl2rgb(s->hue, s->saturation, s->lightness, &c[0], &c[1], &c[2]);
    rgb2yuv(c[0], c[1], c[2], &s->c[0], &s->c[1], &s->c[2], s->depth);

    ctx->internal->execute(ctx, do_slice, frame, NULL,
                           FFMIN(s->planeheight[1], ff_filter_get_nb_threads(ctx)));

    return ff_filter_frame(ctx->outputs[0], frame);
}

static av_cold int query_formats(AVFilterContext *ctx)
{
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

    AVFilterFormats *formats = NULL;

    formats = ff_make_format_list(pixel_fmts);
    if (!formats)
        return AVERROR(ENOMEM);

    return ff_set_common_formats(ctx, formats);
}

static av_cold int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ColorizeContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int depth;

    s->depth = depth = desc->comp[0].depth;

    s->planewidth[1] = s->planewidth[2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0] = s->planewidth[3] = inlink->w;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->do_plane_slice[0] = depth <= 8 ? colorizey_slice8 : colorizey_slice16;
    s->do_plane_slice[1] = depth <= 8 ? colorize_slice8 : colorize_slice16;

    return 0;
}

static const AVFilterPad colorize_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .needs_writable = 1,
        .filter_frame   = filter_frame,
        .config_props   = config_input,
    },
    { NULL }
};

static const AVFilterPad colorize_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

#define OFFSET(x) offsetof(ColorizeContext, x)
#define VF AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption colorize_options[] = {
    { "hue",        "set the hue",                     OFFSET(hue),        AV_OPT_TYPE_FLOAT, {.dbl=0},  0, 360, VF },
    { "saturation", "set the saturation",              OFFSET(saturation), AV_OPT_TYPE_FLOAT, {.dbl=0.5},0,   1, VF },
    { "lightness",  "set the lightness",               OFFSET(lightness),  AV_OPT_TYPE_FLOAT, {.dbl=0.5},0,   1, VF },
    { "mix",        "set the mix of source lightness", OFFSET(mix),        AV_OPT_TYPE_FLOAT, {.dbl=1},  0,   1, VF },
    { NULL }
};

AVFILTER_DEFINE_CLASS(colorize);

AVFilter ff_vf_colorize = {
    .name          = "colorize",
    .description   = NULL_IF_CONFIG_SMALL("Overlay a solid color on the video stream."),
    .priv_size     = sizeof(ColorizeContext),
    .priv_class    = &colorize_class,
    .query_formats = query_formats,
    .inputs        = colorize_inputs,
    .outputs       = colorize_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
