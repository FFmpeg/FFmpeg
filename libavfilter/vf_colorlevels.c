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
#include "internal.h"
#include "video.h"
#include "preserve_color.h"

#define R 0
#define G 1
#define B 2
#define A 3

typedef struct Range {
    double in_min, in_max;
    double out_min, out_max;
} Range;

typedef struct ColorLevelsContext {
    const AVClass *class;
    Range range[4];
    int preserve_color;

    int nb_comp;
    int depth;
    int max;
    int planar;
    int bpp;
    int step;
    uint8_t rgba_map[4];
    int linesize;

    int (*colorlevels_slice[2])(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} ColorLevelsContext;

#define OFFSET(x) offsetof(ColorLevelsContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM
static const AVOption colorlevels_options[] = {
    { "rimin", "set input red black point",    OFFSET(range[R].in_min),  AV_OPT_TYPE_DOUBLE, {.dbl=0}, -1, 1, FLAGS },
    { "gimin", "set input green black point",  OFFSET(range[G].in_min),  AV_OPT_TYPE_DOUBLE, {.dbl=0}, -1, 1, FLAGS },
    { "bimin", "set input blue black point",   OFFSET(range[B].in_min),  AV_OPT_TYPE_DOUBLE, {.dbl=0}, -1, 1, FLAGS },
    { "aimin", "set input alpha black point",  OFFSET(range[A].in_min),  AV_OPT_TYPE_DOUBLE, {.dbl=0}, -1, 1, FLAGS },
    { "rimax", "set input red white point",    OFFSET(range[R].in_max),  AV_OPT_TYPE_DOUBLE, {.dbl=1}, -1, 1, FLAGS },
    { "gimax", "set input green white point",  OFFSET(range[G].in_max),  AV_OPT_TYPE_DOUBLE, {.dbl=1}, -1, 1, FLAGS },
    { "bimax", "set input blue white point",   OFFSET(range[B].in_max),  AV_OPT_TYPE_DOUBLE, {.dbl=1}, -1, 1, FLAGS },
    { "aimax", "set input alpha white point",  OFFSET(range[A].in_max),  AV_OPT_TYPE_DOUBLE, {.dbl=1}, -1, 1, FLAGS },
    { "romin", "set output red black point",   OFFSET(range[R].out_min), AV_OPT_TYPE_DOUBLE, {.dbl=0},  0, 1, FLAGS },
    { "gomin", "set output green black point", OFFSET(range[G].out_min), AV_OPT_TYPE_DOUBLE, {.dbl=0},  0, 1, FLAGS },
    { "bomin", "set output blue black point",  OFFSET(range[B].out_min), AV_OPT_TYPE_DOUBLE, {.dbl=0},  0, 1, FLAGS },
    { "aomin", "set output alpha black point", OFFSET(range[A].out_min), AV_OPT_TYPE_DOUBLE, {.dbl=0},  0, 1, FLAGS },
    { "romax", "set output red white point",   OFFSET(range[R].out_max), AV_OPT_TYPE_DOUBLE, {.dbl=1},  0, 1, FLAGS },
    { "gomax", "set output green white point", OFFSET(range[G].out_max), AV_OPT_TYPE_DOUBLE, {.dbl=1},  0, 1, FLAGS },
    { "bomax", "set output blue white point",  OFFSET(range[B].out_max), AV_OPT_TYPE_DOUBLE, {.dbl=1},  0, 1, FLAGS },
    { "aomax", "set output alpha white point", OFFSET(range[A].out_max), AV_OPT_TYPE_DOUBLE, {.dbl=1},  0, 1, FLAGS },
    { "preserve", "set preserve color mode",   OFFSET(preserve_color),   AV_OPT_TYPE_INT,    {.i64=0},  0, NB_PRESERVE-1, FLAGS, .unit = "preserve" },
    { "none",  "disabled",                     0,                        AV_OPT_TYPE_CONST,  {.i64=P_NONE}, 0, 0, FLAGS, .unit = "preserve" },
    { "lum",   "luminance",                    0,                        AV_OPT_TYPE_CONST,  {.i64=P_LUM},  0, 0, FLAGS, .unit = "preserve" },
    { "max",   "max",                          0,                        AV_OPT_TYPE_CONST,  {.i64=P_MAX},  0, 0, FLAGS, .unit = "preserve" },
    { "avg",   "average",                      0,                        AV_OPT_TYPE_CONST,  {.i64=P_AVG},  0, 0, FLAGS, .unit = "preserve" },
    { "sum",   "sum",                          0,                        AV_OPT_TYPE_CONST,  {.i64=P_SUM},  0, 0, FLAGS, .unit = "preserve" },
    { "nrm",   "norm",                         0,                        AV_OPT_TYPE_CONST,  {.i64=P_NRM},  0, 0, FLAGS, .unit = "preserve" },
    { "pwr",   "power",                        0,                        AV_OPT_TYPE_CONST,  {.i64=P_PWR},  0, 0, FLAGS, .unit = "preserve" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(colorlevels);

typedef struct ThreadData {
    const uint8_t *srcrow[4];
    uint8_t *dstrow[4];
    int dst_linesize;
    int src_linesize;

    float coeff[4];

    int h;

    float fimin[4];
    float fomin[4];
    int imin[4];
    int omin[4];
} ThreadData;

#define DO_COMMON(type, ptype, clip, preserve, planar)                          \
    const ThreadData *td = arg;                                                 \
    const int linesize = s->linesize;                                           \
    const int step = s->step;                                                   \
    const int process_h = td->h;                                                \
    const int slice_start = (process_h *  jobnr   ) / nb_jobs;                  \
    const int slice_end   = (process_h * (jobnr+1)) / nb_jobs;                  \
    const int src_linesize = td->src_linesize / sizeof(type);                   \
    const int dst_linesize = td->dst_linesize / sizeof(type);                   \
    const type *src_r = (const type *)(td->srcrow[R]) + src_linesize * slice_start; \
    const type *src_g = (const type *)(td->srcrow[G]) + src_linesize * slice_start; \
    const type *src_b = (const type *)(td->srcrow[B]) + src_linesize * slice_start; \
    const type *src_a = (const type *)(td->srcrow[A]) + src_linesize * slice_start; \
    type *dst_r = (type *)(td->dstrow[R]) + src_linesize * slice_start;         \
    type *dst_g = (type *)(td->dstrow[G]) + src_linesize * slice_start;         \
    type *dst_b = (type *)(td->dstrow[B]) + src_linesize * slice_start;         \
    type *dst_a = (type *)(td->dstrow[A]) + src_linesize * slice_start;         \
    const ptype imin_r = s->depth == 32 ? td->fimin[R] : td->imin[R];           \
    const ptype imin_g = s->depth == 32 ? td->fimin[G] : td->imin[G];           \
    const ptype imin_b = s->depth == 32 ? td->fimin[B] : td->imin[B];           \
    const ptype imin_a = s->depth == 32 ? td->fimin[A] : td->imin[A];           \
    const ptype omin_r = s->depth == 32 ? td->fomin[R] : td->omin[R];           \
    const ptype omin_g = s->depth == 32 ? td->fomin[G] : td->omin[G];           \
    const ptype omin_b = s->depth == 32 ? td->fomin[B] : td->omin[B];           \
    const ptype omin_a = s->depth == 32 ? td->fomin[A] : td->omin[A];           \
    const float coeff_r = td->coeff[R];                                         \
    const float coeff_g = td->coeff[G];                                         \
    const float coeff_b = td->coeff[B];                                         \
    const float coeff_a = td->coeff[A];                                         \
                                                                                \
    for (int y = slice_start; y < slice_end; y++) {                             \
        for (int x = 0; x < linesize; x += step) {                              \
            ptype ir, ig, ib, or, og, ob;                                       \
            ir = src_r[x];                                                      \
            ig = src_g[x];                                                      \
            ib = src_b[x];                                                      \
            if (preserve) {                                                     \
                float ratio, icolor, ocolor, max = s->depth==32 ? 1.f : s->max; \
                                                                                \
                or = (ir - imin_r) * coeff_r + omin_r;                          \
                og = (ig - imin_g) * coeff_g + omin_g;                          \
                ob = (ib - imin_b) * coeff_b + omin_b;                          \
                                                                                \
                preserve_color(s->preserve_color, ir, ig, ib, or, og, ob, max,  \
                              &icolor, &ocolor);                                \
                if (ocolor > 0.f) {                                             \
                    ratio = icolor / ocolor;                                    \
                                                                                \
                    or *= ratio;                                                \
                    og *= ratio;                                                \
                    ob *= ratio;                                                \
                }                                                               \
                                                                                \
                dst_r[x] = clip(or, depth);                                     \
                dst_g[x] = clip(og, depth);                                     \
                dst_b[x] = clip(ob, depth);                                     \
            } else {                                                            \
                dst_r[x] = clip((ir - imin_r) * coeff_r + omin_r, depth);       \
                dst_g[x] = clip((ig - imin_g) * coeff_g + omin_g, depth);       \
                dst_b[x] = clip((ib - imin_b) * coeff_b + omin_b, depth);       \
            }                                                                   \
        }                                                                       \
                                                                                \
        for (int x = 0; x < linesize && s->nb_comp == 4; x += step)             \
            dst_a[x] = clip((src_a[x] - imin_a) * coeff_a + omin_a, depth);     \
                                                                                \
        src_r += src_linesize;                                                  \
        src_g += src_linesize;                                                  \
        src_b += src_linesize;                                                  \
        src_a += src_linesize;                                                  \
                                                                                \
        dst_r += dst_linesize;                                                  \
        dst_g += dst_linesize;                                                  \
        dst_b += dst_linesize;                                                  \
        dst_a += dst_linesize;                                                  \
    }

#define CLIP8(x, depth) av_clip_uint8(x)
#define CLIP16(x, depth) av_clip_uint16(x)
#define NOCLIP(x, depth) (x)

static int colorlevels_slice_8(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorLevelsContext *s = ctx->priv;
    DO_COMMON(uint8_t, int, CLIP8, 0, 0)
    return 0;
}

static int colorlevels_slice_16(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorLevelsContext *s = ctx->priv;\
    DO_COMMON(uint16_t, int, CLIP16, 0, 0)
    return 0;
}

static int colorlevels_preserve_slice_8(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorLevelsContext *s = ctx->priv;
    DO_COMMON(uint8_t, int, CLIP8, 1, 0)
    return 0;
}

static int colorlevels_preserve_slice_16(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorLevelsContext *s = ctx->priv;
    DO_COMMON(uint16_t, int, CLIP16, 1, 0)
    return 0;
}

static int colorlevels_slice_8_planar(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorLevelsContext *s = ctx->priv;
    DO_COMMON(uint8_t, int, CLIP8, 0, 1)
    return 0;
}

static int colorlevels_slice_9_planar(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorLevelsContext *s = ctx->priv;
    const int depth = 9;
    DO_COMMON(uint16_t, int, av_clip_uintp2, 0, 1)
    return 0;
}

static int colorlevels_slice_10_planar(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorLevelsContext *s = ctx->priv;
    const int depth = 10;
    DO_COMMON(uint16_t, int, av_clip_uintp2, 0, 1)
    return 0;
}

static int colorlevels_slice_12_planar(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorLevelsContext *s = ctx->priv;
    const int depth = 12;
    DO_COMMON(uint16_t, int, av_clip_uintp2, 0, 1)
    return 0;
}

static int colorlevels_slice_14_planar(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorLevelsContext *s = ctx->priv;
    const int depth = 14;
    DO_COMMON(uint16_t, int, av_clip_uintp2, 0, 1)
    return 0;
}

static int colorlevels_slice_16_planar(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorLevelsContext *s = ctx->priv;
    DO_COMMON(uint16_t, int, CLIP16, 0, 1)
    return 0;
}

static int colorlevels_slice_32_planar(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorLevelsContext *s = ctx->priv;
    DO_COMMON(float, float, NOCLIP, 0, 1)
    return 0;
}

static int colorlevels_preserve_slice_8_planar(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorLevelsContext *s = ctx->priv;
    DO_COMMON(uint8_t, int, CLIP8, 1, 1)
    return 0;
}

static int colorlevels_preserve_slice_9_planar(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorLevelsContext *s = ctx->priv;
    const int depth = 9;
    DO_COMMON(uint16_t, int, av_clip_uintp2, 1, 1)
    return 0;
}

static int colorlevels_preserve_slice_10_planar(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorLevelsContext *s = ctx->priv;
    const int depth = 10;
    DO_COMMON(uint16_t, int, av_clip_uintp2, 1, 1)
    return 0;
}

static int colorlevels_preserve_slice_12_planar(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorLevelsContext *s = ctx->priv;
    const int depth = 12;
    DO_COMMON(uint16_t, int, av_clip_uintp2, 1, 1)
    return 0;
}

static int colorlevels_preserve_slice_14_planar(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorLevelsContext *s = ctx->priv;
    const int depth = 14;
    DO_COMMON(uint16_t, int, av_clip_uintp2, 1, 1)
    return 0;
}

static int colorlevels_preserve_slice_16_planar(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorLevelsContext *s = ctx->priv;
    DO_COMMON(uint16_t, int, CLIP16, 1, 1)
    return 0;
}

static int colorlevels_preserve_slice_32_planar(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorLevelsContext *s = ctx->priv;
    DO_COMMON(float, float, NOCLIP, 1, 1)
    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ColorLevelsContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    s->nb_comp = desc->nb_components;
    s->planar = desc->flags & AV_PIX_FMT_FLAG_PLANAR;
    s->depth = desc->comp[0].depth;
    s->max = (1 << s->depth) - 1;
    s->bpp = (desc->comp[0].depth + 7) >> 3;
    s->step = s->planar ? 1 : av_get_padded_bits_per_pixel(desc) >> (3 + (s->bpp == 2));
    s->linesize = inlink->w * s->step;
    ff_fill_rgba_map(s->rgba_map, inlink->format);

    if (!s->planar) {
        s->colorlevels_slice[0] = colorlevels_slice_8;
        s->colorlevels_slice[1] = colorlevels_preserve_slice_8;
        if (s->bpp == 2) {
            s->colorlevels_slice[0] = colorlevels_slice_16;
            s->colorlevels_slice[1] = colorlevels_preserve_slice_16;
        }
    } else {
        switch (s->depth) {
        case 8:
            s->colorlevels_slice[0] = colorlevels_slice_8_planar;
            s->colorlevels_slice[1] = colorlevels_preserve_slice_8_planar;
            break;
        case 9:
            s->colorlevels_slice[0] = colorlevels_slice_9_planar;
            s->colorlevels_slice[1] = colorlevels_preserve_slice_9_planar;
            break;
        case 10:
            s->colorlevels_slice[0] = colorlevels_slice_10_planar;
            s->colorlevels_slice[1] = colorlevels_preserve_slice_10_planar;
            break;
        case 12:
            s->colorlevels_slice[0] = colorlevels_slice_12_planar;
            s->colorlevels_slice[1] = colorlevels_preserve_slice_12_planar;
            break;
        case 14:
            s->colorlevels_slice[0] = colorlevels_slice_14_planar;
            s->colorlevels_slice[1] = colorlevels_preserve_slice_14_planar;
            break;
        case 16:
            s->colorlevels_slice[0] = colorlevels_slice_16_planar;
            s->colorlevels_slice[1] = colorlevels_preserve_slice_16_planar;
            break;
        case 32:
            s->colorlevels_slice[0] = colorlevels_slice_32_planar;
            s->colorlevels_slice[1] = colorlevels_preserve_slice_32_planar;
            break;
        }
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    ColorLevelsContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    const int step = s->step;
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

    td.h             = inlink->h;
    td.dst_linesize  = out->linesize[0];
    td.src_linesize  = in->linesize[0];
    if (s->planar) {
        td.srcrow[R] = in->data[2];
        td.dstrow[R] = out->data[2];
        td.srcrow[G] = in->data[0];
        td.dstrow[G] = out->data[0];
        td.srcrow[B] = in->data[1];
        td.dstrow[B] = out->data[1];
        td.srcrow[A] = in->data[3];
        td.dstrow[A] = out->data[3];
    } else {
        td.srcrow[R] = in->data[0]  + s->rgba_map[R] * s->bpp;
        td.dstrow[R] = out->data[0] + s->rgba_map[R] * s->bpp;
        td.srcrow[G] = in->data[0]  + s->rgba_map[G] * s->bpp;
        td.dstrow[G] = out->data[0] + s->rgba_map[G] * s->bpp;
        td.srcrow[B] = in->data[0]  + s->rgba_map[B] * s->bpp;
        td.dstrow[B] = out->data[0] + s->rgba_map[B] * s->bpp;
        td.srcrow[A] = in->data[0]  + s->rgba_map[A] * s->bpp;
        td.dstrow[A] = out->data[0] + s->rgba_map[A] * s->bpp;
    }

    switch (s->bpp) {
    case 1:
        for (int i = 0; i < s->nb_comp; i++) {
            Range *r = &s->range[i];
            const uint8_t offset = s->rgba_map[i];
            const uint8_t *srcrow = in->data[0];
            int imin = lrint(r->in_min  * UINT8_MAX);
            int imax = lrint(r->in_max  * UINT8_MAX);
            int omin = lrint(r->out_min * UINT8_MAX);
            int omax = lrint(r->out_max * UINT8_MAX);
            float coeff;

            if (imin < 0) {
                imin = UINT8_MAX;
                for (int y = 0; y < inlink->h; y++) {
                    const uint8_t *src = srcrow;

                    for (int x = 0; x < s->linesize; x += step)
                        imin = FFMIN(imin, src[x + offset]);
                    srcrow += in->linesize[0];
                }
            }
            if (imax < 0) {
                srcrow = in->data[0];
                imax = 0;
                for (int y = 0; y < inlink->h; y++) {
                    const uint8_t *src = srcrow;

                    for (int x = 0; x < s->linesize; x += step)
                        imax = FFMAX(imax, src[x + offset]);
                    srcrow += in->linesize[0];
                }
            }

            coeff = (omax - omin) / (double)(imax - imin);

            td.coeff[i] = coeff;
            td.imin[i]  = imin;
            td.omin[i]  = omin;
        }
        break;
    case 2:
        for (int i = 0; i < s->nb_comp; i++) {
            Range *r = &s->range[i];
            const uint8_t offset = s->rgba_map[i];
            const uint8_t *srcrow = in->data[0];
            int imin = lrint(r->in_min  * UINT16_MAX);
            int imax = lrint(r->in_max  * UINT16_MAX);
            int omin = lrint(r->out_min * UINT16_MAX);
            int omax = lrint(r->out_max * UINT16_MAX);
            float coeff;

            if (imin < 0) {
                imin = UINT16_MAX;
                for (int y = 0; y < inlink->h; y++) {
                    const uint16_t *src = (const uint16_t *)srcrow;

                    for (int x = 0; x < s->linesize; x += step)
                        imin = FFMIN(imin, src[x + offset]);
                    srcrow += in->linesize[0];
                }
            }
            if (imax < 0) {
                srcrow = in->data[0];
                imax = 0;
                for (int y = 0; y < inlink->h; y++) {
                    const uint16_t *src = (const uint16_t *)srcrow;

                    for (int x = 0; x < s->linesize; x += step)
                        imax = FFMAX(imax, src[x + offset]);
                    srcrow += in->linesize[0];
                }
            }

            coeff = (omax - omin) / (double)(imax - imin);

            td.coeff[i] = coeff;
            td.imin[i]  = imin;
            td.omin[i]  = omin;
        }
        break;
    case 4:
        for (int i = 0; i < s->nb_comp; i++) {
            Range *r = &s->range[i];
            const uint8_t offset = s->rgba_map[i];
            const uint8_t *srcrow = in->data[0];
            float imin = r->in_min;
            float imax = r->in_max;
            float omin = r->out_min;
            float omax = r->out_max;
            float coeff;

            if (imin < 0.f) {
                imin = 1.f;
                for (int y = 0; y < inlink->h; y++) {
                    const float *src = (const float *)srcrow;

                    for (int x = 0; x < s->linesize; x += step)
                        imin = fminf(imin, src[x + offset]);
                    srcrow += in->linesize[0];
                }
            }
            if (imax < 0.f) {
                srcrow = in->data[0];
                imax = 0.f;
                for (int y = 0; y < inlink->h; y++) {
                    const float *src = (const float *)srcrow;

                    for (int x = 0; x < s->linesize; x += step)
                        imax = fmaxf(imax, src[x + offset]);
                    srcrow += in->linesize[0];
                }
            }

            coeff = (omax - omin) / (double)(imax - imin);

            td.coeff[i] = coeff;
            td.fimin[i] = imin;
            td.fomin[i] = omin;
        }
        break;
    }

    ff_filter_execute(ctx, s->colorlevels_slice[s->preserve_color > 0], &td, NULL,
                      FFMIN(inlink->h, ff_filter_get_nb_threads(ctx)));

    if (in != out)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static const AVFilterPad colorlevels_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

const AVFilter ff_vf_colorlevels = {
    .name          = "colorlevels",
    .description   = NULL_IF_CONFIG_SMALL("Adjust the color levels."),
    .priv_size     = sizeof(ColorLevelsContext),
    .priv_class    = &colorlevels_class,
    FILTER_INPUTS(colorlevels_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS(AV_PIX_FMT_0RGB,   AV_PIX_FMT_0BGR,
                   AV_PIX_FMT_ARGB,   AV_PIX_FMT_ABGR,
                   AV_PIX_FMT_RGB0,   AV_PIX_FMT_BGR0,
                   AV_PIX_FMT_RGB24,  AV_PIX_FMT_BGR24,
                   AV_PIX_FMT_RGB48,  AV_PIX_FMT_BGR48,
                   AV_PIX_FMT_RGBA64, AV_PIX_FMT_BGRA64,
                   AV_PIX_FMT_RGBA,   AV_PIX_FMT_BGRA,
                   AV_PIX_FMT_GBRP,   AV_PIX_FMT_GBRAP,
                   AV_PIX_FMT_GBRP9,
                   AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRAP10,
                   AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRAP12,
                   AV_PIX_FMT_GBRP14,
                   AV_PIX_FMT_GBRP16, AV_PIX_FMT_GBRAP16,
                   AV_PIX_FMT_GBRPF32, AV_PIX_FMT_GBRAPF32),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
