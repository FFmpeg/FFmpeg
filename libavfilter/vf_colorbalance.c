/*
 * Copyright (c) 2013 Paul B Mahol
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
#include "formats.h"
#include "internal.h"
#include "video.h"

#define R 0
#define G 1
#define B 2
#define A 3

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

typedef struct Range {
    float shadows;
    float midtones;
    float highlights;
} Range;

typedef struct ColorBalanceContext {
    const AVClass *class;
    Range cyan_red;
    Range magenta_green;
    Range yellow_blue;
    int preserve_lightness;

    uint8_t rgba_map[4];
    int depth;
    int max;
    int step;

    int (*color_balance)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} ColorBalanceContext;

#define OFFSET(x) offsetof(ColorBalanceContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM
static const AVOption colorbalance_options[] = {
    { "rs", "set red shadows",      OFFSET(cyan_red.shadows),         AV_OPT_TYPE_FLOAT, {.dbl=0}, -1, 1, FLAGS },
    { "gs", "set green shadows",    OFFSET(magenta_green.shadows),    AV_OPT_TYPE_FLOAT, {.dbl=0}, -1, 1, FLAGS },
    { "bs", "set blue shadows",     OFFSET(yellow_blue.shadows),      AV_OPT_TYPE_FLOAT, {.dbl=0}, -1, 1, FLAGS },
    { "rm", "set red midtones",     OFFSET(cyan_red.midtones),        AV_OPT_TYPE_FLOAT, {.dbl=0}, -1, 1, FLAGS },
    { "gm", "set green midtones",   OFFSET(magenta_green.midtones),   AV_OPT_TYPE_FLOAT, {.dbl=0}, -1, 1, FLAGS },
    { "bm", "set blue midtones",    OFFSET(yellow_blue.midtones),     AV_OPT_TYPE_FLOAT, {.dbl=0}, -1, 1, FLAGS },
    { "rh", "set red highlights",   OFFSET(cyan_red.highlights),      AV_OPT_TYPE_FLOAT, {.dbl=0}, -1, 1, FLAGS },
    { "gh", "set green highlights", OFFSET(magenta_green.highlights), AV_OPT_TYPE_FLOAT, {.dbl=0}, -1, 1, FLAGS },
    { "bh", "set blue highlights",  OFFSET(yellow_blue.highlights),   AV_OPT_TYPE_FLOAT, {.dbl=0}, -1, 1, FLAGS },
    { "pl", "preserve lightness",   OFFSET(preserve_lightness),       AV_OPT_TYPE_BOOL,  {.i64=0},  0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(colorbalance);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_RGB24, AV_PIX_FMT_BGR24,
        AV_PIX_FMT_RGBA,  AV_PIX_FMT_BGRA,
        AV_PIX_FMT_ABGR,  AV_PIX_FMT_ARGB,
        AV_PIX_FMT_0BGR,  AV_PIX_FMT_0RGB,
        AV_PIX_FMT_RGB0,  AV_PIX_FMT_BGR0,
        AV_PIX_FMT_RGB48,  AV_PIX_FMT_BGR48,
        AV_PIX_FMT_RGBA64, AV_PIX_FMT_BGRA64,
        AV_PIX_FMT_GBRP,   AV_PIX_FMT_GBRAP,
        AV_PIX_FMT_GBRP9,
        AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRAP10,
        AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRAP12,
        AV_PIX_FMT_GBRP14,
        AV_PIX_FMT_GBRP16, AV_PIX_FMT_GBRAP16,
        AV_PIX_FMT_NONE
    };
    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static float get_component(float v, float l,
                           float s, float m, float h)
{
    const float a = 4.f, b = 0.333f, scale = 0.7f;

    s *= av_clipf((b - l) * a + 0.5f, 0, 1) * scale;
    m *= av_clipf((l - b) * a + 0.5f, 0, 1) * av_clipf((1.0 - l - b) * a + 0.5f, 0, 1) * scale;
    h *= av_clipf((l + b - 1) * a + 0.5f, 0, 1) * scale;

    v += s;
    v += m;
    v += h;

    return av_clipf(v + 0.5f, 0, 1);
}

static float hfun(float n, float h, float s, float l)
{
    float a = s * FFMIN(l, 1. - l);
    float k = fmodf(n + h / 30.f, 12.f);

    return av_clipf(l - a * FFMAX(FFMIN3(k - 3.f, 9.f - k, 1), -1.f), 0, 1);
}

static void preservel(float *r, float *g, float *b, float l)
{
    float max = FFMAX3(*r, *g, *b);
    float min = FFMIN3(*r, *g, *b);
    float h, s;

    l *= 0.5;

    if (*r == *g && *g == *b) {
        h = 0.;
    } else if (max == *r) {
        h = 60. * (0. + (*g - *b) / (max - min));
    } else if (max == *g) {
        h = 60. * (2. + (*b - *r) / (max - min));
    } else if (max == *b) {
        h = 60. * (4. + (*r - *g) / (max - min));
    } else {
        h = 0.;
    }
    if (h < 0.)
        h += 360.;

    if (max == 0. || min == 1.) {
        s = 0.;
    } else {
        s = (max - min) / (1. - FFABS(2. * l - 1));
    }

    *r = hfun(0, h, s, l);
    *g = hfun(8, h, s, l);
    *b = hfun(4, h, s, l);
}

static int color_balance8_p(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorBalanceContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int slice_start = (out->height * jobnr) / nb_jobs;
    const int slice_end = (out->height * (jobnr+1)) / nb_jobs;
    const uint8_t *srcg = in->data[0] + slice_start * in->linesize[0];
    const uint8_t *srcb = in->data[1] + slice_start * in->linesize[1];
    const uint8_t *srcr = in->data[2] + slice_start * in->linesize[2];
    const uint8_t *srca = in->data[3] + slice_start * in->linesize[3];
    uint8_t *dstg = out->data[0] + slice_start * out->linesize[0];
    uint8_t *dstb = out->data[1] + slice_start * out->linesize[1];
    uint8_t *dstr = out->data[2] + slice_start * out->linesize[2];
    uint8_t *dsta = out->data[3] + slice_start * out->linesize[3];
    const float max = s->max;
    int i, j;

    for (i = slice_start; i < slice_end; i++) {
        for (j = 0; j < out->width; j++) {
            float r = srcr[j] / max;
            float g = srcg[j] / max;
            float b = srcb[j] / max;
            const float l = FFMAX3(r, g, b) + FFMIN3(r, g, b);

            r = get_component(r, l, s->cyan_red.shadows, s->cyan_red.midtones, s->cyan_red.highlights);
            g = get_component(g, l, s->magenta_green.shadows, s->magenta_green.midtones, s->magenta_green.highlights);
            b = get_component(b, l, s->yellow_blue.shadows, s->yellow_blue.midtones, s->yellow_blue.highlights);

            if (s->preserve_lightness)
                preservel(&r, &g, &b, l);

            dstr[j] = av_clip_uint8(r * max);
            dstg[j] = av_clip_uint8(g * max);
            dstb[j] = av_clip_uint8(b * max);
            if (in != out && out->linesize[3])
                dsta[j] = srca[j];
        }

        srcg += in->linesize[0];
        srcb += in->linesize[1];
        srcr += in->linesize[2];
        srca += in->linesize[3];
        dstg += out->linesize[0];
        dstb += out->linesize[1];
        dstr += out->linesize[2];
        dsta += out->linesize[3];
    }

    return 0;
}

static int color_balance16_p(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorBalanceContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int slice_start = (out->height * jobnr) / nb_jobs;
    const int slice_end = (out->height * (jobnr+1)) / nb_jobs;
    const uint16_t *srcg = (const uint16_t *)in->data[0] + slice_start * in->linesize[0] / 2;
    const uint16_t *srcb = (const uint16_t *)in->data[1] + slice_start * in->linesize[1] / 2;
    const uint16_t *srcr = (const uint16_t *)in->data[2] + slice_start * in->linesize[2] / 2;
    const uint16_t *srca = (const uint16_t *)in->data[3] + slice_start * in->linesize[3] / 2;
    uint16_t *dstg = (uint16_t *)out->data[0] + slice_start * out->linesize[0] / 2;
    uint16_t *dstb = (uint16_t *)out->data[1] + slice_start * out->linesize[1] / 2;
    uint16_t *dstr = (uint16_t *)out->data[2] + slice_start * out->linesize[2] / 2;
    uint16_t *dsta = (uint16_t *)out->data[3] + slice_start * out->linesize[3] / 2;
    const int depth = s->depth;
    const float max = s->max;
    int i, j;

    for (i = slice_start; i < slice_end; i++) {
        for (j = 0; j < out->width; j++) {
            float r = srcr[j] / max;
            float g = srcg[j] / max;
            float b = srcb[j] / max;
            const float l = (FFMAX3(r, g, b) + FFMIN3(r, g, b));

            r = get_component(r, l, s->cyan_red.shadows, s->cyan_red.midtones, s->cyan_red.highlights);
            g = get_component(g, l, s->magenta_green.shadows, s->magenta_green.midtones, s->magenta_green.highlights);
            b = get_component(b, l, s->yellow_blue.shadows, s->yellow_blue.midtones, s->yellow_blue.highlights);

            if (s->preserve_lightness)
                preservel(&r, &g, &b, l);

            dstr[j] = av_clip_uintp2_c(r * max, depth);
            dstg[j] = av_clip_uintp2_c(g * max, depth);
            dstb[j] = av_clip_uintp2_c(b * max, depth);
            if (in != out && out->linesize[3])
                dsta[j] = srca[j];
        }

        srcg += in->linesize[0] / 2;
        srcb += in->linesize[1] / 2;
        srcr += in->linesize[2] / 2;
        srca += in->linesize[3] / 2;
        dstg += out->linesize[0] / 2;
        dstb += out->linesize[1] / 2;
        dstr += out->linesize[2] / 2;
        dsta += out->linesize[3] / 2;
    }

    return 0;
}

static int color_balance8(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorBalanceContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    AVFilterLink *outlink = ctx->outputs[0];
    const int slice_start = (out->height * jobnr) / nb_jobs;
    const int slice_end = (out->height * (jobnr+1)) / nb_jobs;
    const uint8_t *srcrow = in->data[0] + slice_start * in->linesize[0];
    const uint8_t roffset = s->rgba_map[R];
    const uint8_t goffset = s->rgba_map[G];
    const uint8_t boffset = s->rgba_map[B];
    const uint8_t aoffset = s->rgba_map[A];
    const float max = s->max;
    const int step = s->step;
    uint8_t *dstrow;
    int i, j;

    dstrow = out->data[0] + slice_start * out->linesize[0];
    for (i = slice_start; i < slice_end; i++) {
        const uint8_t *src = srcrow;
        uint8_t *dst = dstrow;

        for (j = 0; j < outlink->w * step; j += step) {
            float r = src[j + roffset] / max;
            float g = src[j + goffset] / max;
            float b = src[j + boffset] / max;
            const float l = (FFMAX3(r, g, b) + FFMIN3(r, g, b));

            r = get_component(r, l, s->cyan_red.shadows, s->cyan_red.midtones, s->cyan_red.highlights);
            g = get_component(g, l, s->magenta_green.shadows, s->magenta_green.midtones, s->magenta_green.highlights);
            b = get_component(b, l, s->yellow_blue.shadows, s->yellow_blue.midtones, s->yellow_blue.highlights);

            if (s->preserve_lightness)
                preservel(&r, &g, &b, l);

            dst[j + roffset] = av_clip_uint8(r * max);
            dst[j + goffset] = av_clip_uint8(g * max);
            dst[j + boffset] = av_clip_uint8(b * max);
            if (in != out && step == 4)
                dst[j + aoffset] = src[j + aoffset];
        }

        srcrow += in->linesize[0];
        dstrow += out->linesize[0];
    }

    return 0;
}

static int color_balance16(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorBalanceContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    AVFilterLink *outlink = ctx->outputs[0];
    const int slice_start = (out->height * jobnr) / nb_jobs;
    const int slice_end = (out->height * (jobnr+1)) / nb_jobs;
    const uint16_t *srcrow = (const uint16_t *)in->data[0] + slice_start * in->linesize[0] / 2;
    const uint8_t roffset = s->rgba_map[R];
    const uint8_t goffset = s->rgba_map[G];
    const uint8_t boffset = s->rgba_map[B];
    const uint8_t aoffset = s->rgba_map[A];
    const int step = s->step / 2;
    const int depth = s->depth;
    const float max = s->max;
    uint16_t *dstrow;
    int i, j;

    dstrow = (uint16_t *)out->data[0] + slice_start * out->linesize[0] / 2;
    for (i = slice_start; i < slice_end; i++) {
        const uint16_t *src = srcrow;
        uint16_t *dst = dstrow;

        for (j = 0; j < outlink->w * step; j += step) {
            float r = src[j + roffset] / max;
            float g = src[j + goffset] / max;
            float b = src[j + boffset] / max;
            const float l = (FFMAX3(r, g, b) + FFMIN3(r, g, b));

            r = get_component(r, l, s->cyan_red.shadows, s->cyan_red.midtones, s->cyan_red.highlights);
            g = get_component(g, l, s->magenta_green.shadows, s->magenta_green.midtones, s->magenta_green.highlights);
            b = get_component(b, l, s->yellow_blue.shadows, s->yellow_blue.midtones, s->yellow_blue.highlights);

            if (s->preserve_lightness)
                preservel(&r, &g, &b, l);

            dst[j + roffset] = av_clip_uintp2_c(r * max, depth);
            dst[j + goffset] = av_clip_uintp2_c(g * max, depth);
            dst[j + boffset] = av_clip_uintp2_c(b * max, depth);
            if (in != out && step == 4)
                dst[j + aoffset] = src[j + aoffset];
        }

        srcrow += in->linesize[0] / 2;
        dstrow += out->linesize[0] / 2;
    }

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    ColorBalanceContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(outlink->format);
    const int depth = desc->comp[0].depth;
    const int max = (1 << depth) - 1;
    const int planar = av_pix_fmt_count_planes(outlink->format) > 1;

    s->depth = depth;
    s->max = max;

    if (max == 255 && planar) {
        s->color_balance = color_balance8_p;
    } else if (planar) {
        s->color_balance = color_balance16_p;
    } else if (max == 255) {
        s->color_balance = color_balance8;
    } else {
        s->color_balance = color_balance16;
    }

    ff_fill_rgba_map(s->rgba_map, outlink->format);
    s->step = av_get_padded_bits_per_pixel(desc) >> 3;

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    ColorBalanceContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    ThreadData td;
    AVFrame *out;

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

    td.in = in;
    td.out = out;
    ctx->internal->execute(ctx, s->color_balance, &td, NULL, FFMIN(outlink->h, ff_filter_get_nb_threads(ctx)));

    if (in != out)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static const AVFilterPad colorbalance_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad colorbalance_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter ff_vf_colorbalance = {
    .name          = "colorbalance",
    .description   = NULL_IF_CONFIG_SMALL("Adjust the color balance."),
    .priv_size     = sizeof(ColorBalanceContext),
    .priv_class    = &colorbalance_class,
    .query_formats = query_formats,
    .inputs        = colorbalance_inputs,
    .outputs       = colorbalance_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
