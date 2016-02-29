/*
 * Copyright (c) 2015 Paul B Mahol
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

#include "libavutil/avassert.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

enum VectorscopeMode {
    GRAY,
    COLOR,
    COLOR2,
    COLOR3,
    COLOR4,
    COLOR5,
    MODE_NB
};

typedef struct VectorscopeContext {
    const AVClass *class;
    int mode;
    int intensity;
    float fintensity;
    const uint8_t *bg_color;
    int planewidth[4];
    int planeheight[4];
    int hsub, vsub;
    int x, y, pd;
    int is_yuv;
    int size;
    int depth;
    int mult;
    int envelope;
    int graticule;
    float opacity;
    int flags;
    int cs;
    uint8_t peak[1024][1024];

    void (*vectorscope)(struct VectorscopeContext *s,
                        AVFrame *in, AVFrame *out, int pd);
    void (*graticulef)(struct VectorscopeContext *s, AVFrame *out,
                       int X, int Y, int D, int P);
} VectorscopeContext;

#define OFFSET(x) offsetof(VectorscopeContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption vectorscope_options[] = {
    { "mode", "set vectorscope mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=0}, 0, MODE_NB-1, FLAGS, "mode"},
    { "m",    "set vectorscope mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=0}, 0, MODE_NB-1, FLAGS, "mode"},
    {   "gray",   0, 0, AV_OPT_TYPE_CONST, {.i64=GRAY},   0, 0, FLAGS, "mode" },
    {   "color",  0, 0, AV_OPT_TYPE_CONST, {.i64=COLOR},  0, 0, FLAGS, "mode" },
    {   "color2", 0, 0, AV_OPT_TYPE_CONST, {.i64=COLOR2}, 0, 0, FLAGS, "mode" },
    {   "color3", 0, 0, AV_OPT_TYPE_CONST, {.i64=COLOR3}, 0, 0, FLAGS, "mode" },
    {   "color4", 0, 0, AV_OPT_TYPE_CONST, {.i64=COLOR4}, 0, 0, FLAGS, "mode" },
    {   "color5", 0, 0, AV_OPT_TYPE_CONST, {.i64=COLOR5}, 0, 0, FLAGS, "mode" },
    { "x", "set color component on X axis", OFFSET(x), AV_OPT_TYPE_INT, {.i64=1}, 0, 2, FLAGS},
    { "y", "set color component on Y axis", OFFSET(y), AV_OPT_TYPE_INT, {.i64=2}, 0, 2, FLAGS},
    { "intensity", "set intensity", OFFSET(fintensity), AV_OPT_TYPE_FLOAT, {.dbl=0.004}, 0, 1, FLAGS},
    { "i",         "set intensity", OFFSET(fintensity), AV_OPT_TYPE_FLOAT, {.dbl=0.004}, 0, 1, FLAGS},
    { "envelope",  "set envelope", OFFSET(envelope), AV_OPT_TYPE_INT, {.i64=0}, 0, 3, FLAGS, "envelope"},
    { "e",         "set envelope", OFFSET(envelope), AV_OPT_TYPE_INT, {.i64=0}, 0, 3, FLAGS, "envelope"},
    {   "none",         0, 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "envelope" },
    {   "instant",      0, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "envelope" },
    {   "peak",         0, 0, AV_OPT_TYPE_CONST, {.i64=2}, 0, 0, FLAGS, "envelope" },
    {   "peak+instant", 0, 0, AV_OPT_TYPE_CONST, {.i64=3}, 0, 0, FLAGS, "envelope" },
    { "graticule", "set graticule", OFFSET(graticule), AV_OPT_TYPE_INT, {.i64=0}, 0, 2, FLAGS, "graticule"},
    { "g",         "set graticule", OFFSET(graticule), AV_OPT_TYPE_INT, {.i64=0}, 0, 2, FLAGS, "graticule"},
    {   "none",         0, 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "graticule" },
    {   "green",        0, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "graticule" },
    {   "color",        0, 0, AV_OPT_TYPE_CONST, {.i64=2}, 0, 0, FLAGS, "graticule" },
    { "opacity", "set graticule opacity", OFFSET(opacity), AV_OPT_TYPE_FLOAT, {.dbl=0.75}, 0, 1, FLAGS},
    { "o",       "set graticule opacity", OFFSET(opacity), AV_OPT_TYPE_FLOAT, {.dbl=0.75}, 0, 1, FLAGS},
    { "flags", "set graticule flags", OFFSET(flags), AV_OPT_TYPE_FLAGS, {.i64=0}, 0, 3, FLAGS, "flags"},
    { "f",     "set graticule flags", OFFSET(flags), AV_OPT_TYPE_FLAGS, {.i64=0}, 0, 3, FLAGS, "flags"},
    {   "white", "draw white point", 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "flags" },
    {   "black", "draw black point", 0, AV_OPT_TYPE_CONST, {.i64=2}, 0, 0, FLAGS, "flags" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(vectorscope);

static const enum AVPixelFormat out_yuv8_pix_fmts[] = {
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat out_yuv9_pix_fmts[] = {
    AV_PIX_FMT_YUV444P9,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat out_yuv10_pix_fmts[] = {
    AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat out_rgb8_pix_fmts[] = {
    AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRP,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat out_rgb9_pix_fmts[] = {
    AV_PIX_FMT_GBRP9,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat out_rgb10_pix_fmts[] = {
    AV_PIX_FMT_GBRP10,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat in1_pix_fmts[] = {
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_YUV444P9, AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRP,
    AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat in2_pix_fmts[] = {
    AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUVJ411P,
    AV_PIX_FMT_YUV440P,  AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRP,
    AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
    AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
    AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_NONE
};

static int query_formats(AVFilterContext *ctx)
{
    VectorscopeContext *s = ctx->priv;
    const enum AVPixelFormat *out_pix_fmts;
    const AVPixFmtDescriptor *desc;
    AVFilterFormats *avff;
    int depth, rgb, i, ret;

    if (!ctx->inputs[0]->in_formats ||
        !ctx->inputs[0]->in_formats->nb_formats) {
        return AVERROR(EAGAIN);
    }

    if (!ctx->inputs[0]->out_formats) {
        const enum AVPixelFormat *in_pix_fmts;

        if ((s->x == 1 && s->y == 2) || (s->x == 2 && s->y == 1))
            in_pix_fmts = in2_pix_fmts;
        else
            in_pix_fmts = in1_pix_fmts;
        if ((ret = ff_formats_ref(ff_make_format_list(in_pix_fmts), &ctx->inputs[0]->out_formats)) < 0)
            return ret;
    }

    avff = ctx->inputs[0]->in_formats;
    desc = av_pix_fmt_desc_get(avff->formats[0]);
    rgb = desc->flags & AV_PIX_FMT_FLAG_RGB;
    depth = desc->comp[0].depth;
    for (i = 1; i < avff->nb_formats; i++) {
        desc = av_pix_fmt_desc_get(avff->formats[i]);
        if (rgb != (desc->flags & AV_PIX_FMT_FLAG_RGB) ||
            depth != desc->comp[0].depth)
            return AVERROR(EAGAIN);
    }

    if (rgb && depth == 8)
        out_pix_fmts = out_rgb8_pix_fmts;
    else if (rgb && depth == 9)
        out_pix_fmts = out_rgb9_pix_fmts;
    else if (rgb && depth == 10)
        out_pix_fmts = out_rgb10_pix_fmts;
    else if (depth == 9)
        out_pix_fmts = out_yuv9_pix_fmts;
    else if (depth == 10)
        out_pix_fmts = out_yuv10_pix_fmts;
    else if (depth == 8)
        out_pix_fmts = out_yuv8_pix_fmts;
    else
        return AVERROR(EAGAIN);
    if ((ret = ff_formats_ref(ff_make_format_list(out_pix_fmts), &ctx->outputs[0]->in_formats)) < 0)
        return ret;

    return 0;
}

static const uint8_t black_yuva_color[4] = { 0, 127, 127, 0 };
static const uint8_t black_gbrp_color[4] = { 0, 0, 0, 0 };

static int config_output(AVFilterLink *outlink)
{
    VectorscopeContext *s = outlink->src->priv;

    s->intensity = s->fintensity * (s->size - 1);
    outlink->h = outlink->w = s->size;
    outlink->sample_aspect_ratio = (AVRational){1,1};
    return 0;
}

static void envelope_instant16(VectorscopeContext *s, AVFrame *out)
{
    const int dlinesize = out->linesize[0] / 2;
    uint16_t *dpd = s->mode == COLOR || !s->is_yuv ? (uint16_t *)out->data[s->pd] : (uint16_t *)out->data[0];
    const int max = s->size - 1;
    int i, j;

    for (i = 0; i < out->height; i++) {
        for (j = 0; j < out->width; j++) {
            const int pos = i * dlinesize + j;
            const int poa = (i - 1) * dlinesize + j;
            const int pob = (i + 1) * dlinesize + j;

            if (dpd[pos] && (((!j || !dpd[pos - 1]) || ((j == (out->width - 1)) || !dpd[pos + 1]))
                         || ((!i || !dpd[poa]) || ((i == (out->height - 1)) || !dpd[pob])))) {
                dpd[pos] = max;
            }
        }
    }
}

static void envelope_peak16(VectorscopeContext *s, AVFrame *out)
{
    const int dlinesize = out->linesize[0] / 2;
    uint16_t *dpd = s->mode == COLOR || !s->is_yuv ? (uint16_t *)out->data[s->pd] : (uint16_t *)out->data[0];
    const int max = s->size - 1;
    int i, j;

    for (i = 0; i < out->height; i++) {
        for (j = 0; j < out->width; j++) {
            const int pos = i * dlinesize + j;

            if (dpd[pos])
                s->peak[i][j] = 1;
        }
    }

    if (s->envelope == 3)
        envelope_instant16(s, out);

    for (i = 0; i < out->height; i++) {
        for (j = 0; j < out->width; j++) {
            const int pos = i * dlinesize + j;

            if (s->peak[i][j] && (((!j || !s->peak[i][j-1]) || ((j == (out->width - 1)) || !s->peak[i][j + 1]))
                              || ((!i || !s->peak[i-1][j]) || ((i == (out->height - 1)) || !s->peak[i + 1][j])))) {
                dpd[pos] = max;
            }
        }
    }
}

static void envelope_instant(VectorscopeContext *s, AVFrame *out)
{
    const int dlinesize = out->linesize[0];
    uint8_t *dpd = s->mode == COLOR || !s->is_yuv ? out->data[s->pd] : out->data[0];
    int i, j;

    for (i = 0; i < out->height; i++) {
        for (j = 0; j < out->width; j++) {
            const int pos = i * dlinesize + j;
            const int poa = (i - 1) * dlinesize + j;
            const int pob = (i + 1) * dlinesize + j;

            if (dpd[pos] && (((!j || !dpd[pos - 1]) || ((j == (out->width - 1)) || !dpd[pos + 1]))
                         || ((!i || !dpd[poa]) || ((i == (out->height - 1)) || !dpd[pob])))) {
                dpd[pos] = 255;
            }
        }
    }
}

static void envelope_peak(VectorscopeContext *s, AVFrame *out)
{
    const int dlinesize = out->linesize[0];
    uint8_t *dpd = s->mode == COLOR || !s->is_yuv ? out->data[s->pd] : out->data[0];
    int i, j;

    for (i = 0; i < out->height; i++) {
        for (j = 0; j < out->width; j++) {
            const int pos = i * dlinesize + j;

            if (dpd[pos])
                s->peak[i][j] = 1;
        }
    }

    if (s->envelope == 3)
        envelope_instant(s, out);

    for (i = 0; i < out->height; i++) {
        for (j = 0; j < out->width; j++) {
            const int pos = i * dlinesize + j;

            if (s->peak[i][j] && (((!j || !s->peak[i][j-1]) || ((j == (out->width - 1)) || !s->peak[i][j + 1]))
                              || ((!i || !s->peak[i-1][j]) || ((i == (out->height - 1)) || !s->peak[i + 1][j])))) {
                dpd[pos] = 255;
            }
        }
    }
}

static void envelope16(VectorscopeContext *s, AVFrame *out)
{
    if (!s->envelope) {
        return;
    } else if (s->envelope == 1) {
        envelope_instant16(s, out);
    } else {
        envelope_peak16(s, out);
    }
}

static void envelope(VectorscopeContext *s, AVFrame *out)
{
    if (!s->envelope) {
        return;
    } else if (s->envelope == 1) {
        envelope_instant(s, out);
    } else {
        envelope_peak(s, out);
    }
}

static void vectorscope16(VectorscopeContext *s, AVFrame *in, AVFrame *out, int pd)
{
    const uint16_t * const *src = (const uint16_t * const *)in->data;
    const int slinesizex = in->linesize[s->x] / 2;
    const int slinesizey = in->linesize[s->y] / 2;
    const int slinesized = in->linesize[pd] / 2;
    const int dlinesize = out->linesize[0] / 2;
    const int intensity = s->intensity;
    const int px = s->x, py = s->y;
    const int h = s->planeheight[py];
    const int w = s->planewidth[px];
    const uint16_t *spx = src[px];
    const uint16_t *spy = src[py];
    const uint16_t *spd = src[pd];
    const int hsub = s->hsub;
    const int vsub = s->vsub;
    uint16_t **dst = (uint16_t **)out->data;
    uint16_t *dpx = dst[px];
    uint16_t *dpy = dst[py];
    uint16_t *dpd = dst[pd];
    const int max = s->size - 1;
    const int mid = s->size / 2;
    int i, j, k;

    for (k = 0; k < 4 && dst[k]; k++) {
        const int mult = s->mult;

        for (i = 0; i < out->height ; i++)
            for (j = 0; j < out->width; j++)
                AV_WN16(out->data[k] + i * out->linesize[k] + j * 2,
                        (s->mode == COLOR || s->mode == COLOR5) && k == s->pd ? 0 : s->bg_color[k] * mult);
    }

    switch (s->mode) {
    case COLOR:
    case COLOR5:
    case GRAY:
        if (s->is_yuv) {
            for (i = 0; i < h; i++) {
                const int iwx = i * slinesizex;
                const int iwy = i * slinesizey;
                for (j = 0; j < w; j++) {
                    const int x = FFMIN(spx[iwx + j], max);
                    const int y = FFMIN(spy[iwy + j], max);
                    const int pos = y * dlinesize + x;

                    dpd[pos] = FFMIN(dpd[pos] + intensity, max);
                    if (dst[3])
                        dst[3][pos] = max;
                }
            }
        } else {
            for (i = 0; i < h; i++) {
                const int iwx = i * slinesizex;
                const int iwy = i * slinesizey;
                for (j = 0; j < w; j++) {
                    const int x = FFMIN(spx[iwx + j], max);
                    const int y = FFMIN(spy[iwy + j], max);
                    const int pos = y * dlinesize + x;

                    dst[0][pos] = FFMIN(dst[0][pos] + intensity, max);
                    dst[1][pos] = FFMIN(dst[1][pos] + intensity, max);
                    dst[2][pos] = FFMIN(dst[2][pos] + intensity, max);
                    if (dst[3])
                        dst[3][pos] = max;
                }
            }
        }
        break;
    case COLOR2:
        if (s->is_yuv) {
            for (i = 0; i < h; i++) {
                const int iw1 = i * slinesizex;
                const int iw2 = i * slinesizey;
                for (j = 0; j < w; j++) {
                    const int x = FFMIN(spx[iw1 + j], max);
                    const int y = FFMIN(spy[iw2 + j], max);
                    const int pos = y * dlinesize + x;

                    if (!dpd[pos])
                        dpd[pos] = FFABS(mid - x) + FFABS(mid - y);
                    dpx[pos] = x;
                    dpy[pos] = y;
                    if (dst[3])
                        dst[3][pos] = max;
                }
            }
        } else {
            for (i = 0; i < h; i++) {
                const int iw1 = i * slinesizex;
                const int iw2 = i * slinesizey;
                for (j = 0; j < w; j++) {
                    const int x = FFMIN(spx[iw1 + j], max);
                    const int y = FFMIN(spy[iw2 + j], max);
                    const int pos = y * dlinesize + x;

                    if (!dpd[pos])
                        dpd[pos] = FFMIN(x + y, max);
                    dpx[pos] = x;
                    dpy[pos] = y;
                    if (dst[3])
                        dst[3][pos] = max;
                }
            }
        }
        break;
    case COLOR3:
        for (i = 0; i < h; i++) {
            const int iw1 = i * slinesizex;
            const int iw2 = i * slinesizey;
            for (j = 0; j < w; j++) {
                const int x = FFMIN(spx[iw1 + j], max);
                const int y = FFMIN(spy[iw2 + j], max);
                const int pos = y * dlinesize + x;

                dpd[pos] = FFMIN(max, dpd[pos] + intensity);
                dpx[pos] = x;
                dpy[pos] = y;
                if (dst[3])
                    dst[3][pos] = max;
            }
        }
        break;
    case COLOR4:
        for (i = 0; i < in->height; i++) {
            const int iwx = (i >> vsub) * slinesizex;
            const int iwy = (i >> vsub) * slinesizey;
            const int iwd = i * slinesized;
            for (j = 0; j < in->width; j++) {
                const int x = FFMIN(spx[iwx + (j >> hsub)], max);
                const int y = FFMIN(spy[iwy + (j >> hsub)], max);
                const int pos = y * dlinesize + x;

                dpd[pos] = FFMAX(spd[iwd + j], dpd[pos]);
                dpx[pos] = x;
                dpy[pos] = y;
                if (dst[3])
                    dst[3][pos] = max;
            }
        }
        break;
    default:
        av_assert0(0);
    }

    envelope16(s, out);

    if (s->mode == COLOR) {
        for (i = 0; i < out->height; i++) {
            for (j = 0; j < out->width; j++) {
                if (!dpd[i * dlinesize + j]) {
                    dpx[i * dlinesize + j] = j;
                    dpy[i * dlinesize + j] = i;
                    dpd[i * dlinesize + j] = mid;
                }
            }
        }
    } else if (s->mode == COLOR5) {
        for (i = 0; i < out->height; i++) {
            for (j = 0; j < out->width; j++) {
                if (!dpd[i * dlinesize + j]) {
                    dpx[i * dlinesize + j] = j;
                    dpy[i * dlinesize + j] = i;
                    dpd[i * dlinesize + j] = mid * M_SQRT2 - hypot(i - mid, j - mid);
                }
            }
        }
    }
}

static void vectorscope8(VectorscopeContext *s, AVFrame *in, AVFrame *out, int pd)
{
    const uint8_t * const *src = (const uint8_t * const *)in->data;
    const int slinesizex = in->linesize[s->x];
    const int slinesizey = in->linesize[s->y];
    const int slinesized = in->linesize[pd];
    const int dlinesize = out->linesize[0];
    const int intensity = s->intensity;
    const int px = s->x, py = s->y;
    const int h = s->planeheight[py];
    const int w = s->planewidth[px];
    const uint8_t *spx = src[px];
    const uint8_t *spy = src[py];
    const uint8_t *spd = src[pd];
    const int hsub = s->hsub;
    const int vsub = s->vsub;
    uint8_t **dst = out->data;
    uint8_t *dpx = dst[px];
    uint8_t *dpy = dst[py];
    uint8_t *dpd = dst[pd];
    int i, j, k;

    for (k = 0; k < 4 && dst[k]; k++)
        for (i = 0; i < out->height ; i++)
            memset(dst[k] + i * out->linesize[k],
                   (s->mode == COLOR || s->mode == COLOR5) && k == s->pd ? 0 : s->bg_color[k], out->width);

    switch (s->mode) {
    case COLOR5:
    case COLOR:
    case GRAY:
        if (s->is_yuv) {
            for (i = 0; i < h; i++) {
                const int iwx = i * slinesizex;
                const int iwy = i * slinesizey;
                for (j = 0; j < w; j++) {
                    const int x = spx[iwx + j];
                    const int y = spy[iwy + j];
                    const int pos = y * dlinesize + x;

                    dpd[pos] = FFMIN(dpd[pos] + intensity, 255);
                    if (dst[3])
                        dst[3][pos] = 255;
                }
            }
        } else {
            for (i = 0; i < h; i++) {
                const int iwx = i * slinesizex;
                const int iwy = i * slinesizey;
                for (j = 0; j < w; j++) {
                    const int x = spx[iwx + j];
                    const int y = spy[iwy + j];
                    const int pos = y * dlinesize + x;

                    dst[0][pos] = FFMIN(dst[0][pos] + intensity, 255);
                    dst[1][pos] = FFMIN(dst[1][pos] + intensity, 255);
                    dst[2][pos] = FFMIN(dst[2][pos] + intensity, 255);
                    if (dst[3])
                        dst[3][pos] = 255;
                }
            }
        }
        break;
    case COLOR2:
        if (s->is_yuv) {
            for (i = 0; i < h; i++) {
                const int iw1 = i * slinesizex;
                const int iw2 = i * slinesizey;
                for (j = 0; j < w; j++) {
                    const int x = spx[iw1 + j];
                    const int y = spy[iw2 + j];
                    const int pos = y * dlinesize + x;

                    if (!dpd[pos])
                        dpd[pos] = FFABS(128 - x) + FFABS(128 - y);
                    dpx[pos] = x;
                    dpy[pos] = y;
                    if (dst[3])
                        dst[3][pos] = 255;
                }
            }
        } else {
            for (i = 0; i < h; i++) {
                const int iw1 = i * slinesizex;
                const int iw2 = i * slinesizey;
                for (j = 0; j < w; j++) {
                    const int x = spx[iw1 + j];
                    const int y = spy[iw2 + j];
                    const int pos = y * dlinesize + x;

                    if (!dpd[pos])
                        dpd[pos] = FFMIN(x + y, 255);
                    dpx[pos] = x;
                    dpy[pos] = y;
                    if (dst[3])
                        dst[3][pos] = 255;
                }
            }
        }
        break;
    case COLOR3:
        for (i = 0; i < h; i++) {
            const int iw1 = i * slinesizex;
            const int iw2 = i * slinesizey;
            for (j = 0; j < w; j++) {
                const int x = spx[iw1 + j];
                const int y = spy[iw2 + j];
                const int pos = y * dlinesize + x;

                dpd[pos] = FFMIN(255, dpd[pos] + intensity);
                dpx[pos] = x;
                dpy[pos] = y;
                if (dst[3])
                    dst[3][pos] = 255;
            }
        }
        break;
    case COLOR4:
        for (i = 0; i < in->height; i++) {
            const int iwx = (i >> vsub) * slinesizex;
            const int iwy = (i >> vsub) * slinesizey;
            const int iwd = i * slinesized;
            for (j = 0; j < in->width; j++) {
                const int x = spx[iwx + (j >> hsub)];
                const int y = spy[iwy + (j >> hsub)];
                const int pos = y * dlinesize + x;

                dpd[pos] = FFMAX(spd[iwd + j], dpd[pos]);
                dpx[pos] = x;
                dpy[pos] = y;
                if (dst[3])
                    dst[3][pos] = 255;
            }
        }
        break;
    default:
        av_assert0(0);
    }

    envelope(s, out);

    if (s->mode == COLOR) {
        for (i = 0; i < out->height; i++) {
            for (j = 0; j < out->width; j++) {
                if (!dpd[i * out->linesize[pd] + j]) {
                    dpx[i * out->linesize[px] + j] = j;
                    dpy[i * out->linesize[py] + j] = i;
                    dpd[i * out->linesize[pd] + j] = 128;
                }
            }
        }
    } else if (s->mode == COLOR5) {
        for (i = 0; i < out->height; i++) {
            for (j = 0; j < out->width; j++) {
                if (!dpd[i * out->linesize[pd] + j]) {
                    dpx[i * out->linesize[px] + j] = j;
                    dpy[i * out->linesize[py] + j] = i;
                    dpd[i * out->linesize[pd] + j] = 128 * M_SQRT2 - hypot(i - 128, j - 128);
                }
            }
        }
    }
}

const static uint16_t positions[][14][3] = {
  { { 210,  16, 146 }, { 170, 166,  16 }, { 145,  54,  34 },
    { 106, 202, 222 }, {  81,  90, 240 }, {  41, 240, 110 },
    { 162,  44, 142 }, { 131, 156,  44 }, { 112,  72,  58 },
    {  84, 184, 198 }, {  65, 100, 212 }, {  35, 212, 114 },
    { 235, 128, 128 }, { 16, 128, 128 } },
  { {  63, 102, 240 }, {  32, 240, 118 }, { 188, 154,  16 },
    { 219,  16, 138 }, { 173,  42,  26 }, {  78, 214, 230 },
    {  28, 212, 120 }, {  51, 109, 212 }, {  63, 193, 204 },
    { 133,  63,  52 }, { 145, 147,  44 }, { 168,  44, 136 },
    { 235, 128, 128 }, { 16, 128, 128 } },
  { { 210*2,  16*2, 146*2 }, { 170*2, 166*2,  16*2 }, { 145*2,  54*2,  34*2 },
    { 106*2, 202*2, 222*2 }, {  81*2,  90*2, 240*2 }, {  41*2, 240*2, 110*2 },
    { 162*2,  44*2, 142*2 }, { 131*2, 156*2,  44*2 }, { 112*2,  72*2,  58*2 },
    {  84*2, 184*2, 198*2 }, {  65*2, 100*2, 212*2 }, {  35*2, 212*2, 114*2 },
    { 470, 256, 256 }, { 32, 256, 256 } },
  { {  63*2, 102*2, 240*2 }, {  32*2, 240*2, 118*2 }, { 188*2, 154*2,  16*2 },
    { 219*2,  16*2, 138*2 }, { 173*2,  42*2,  26*2 }, {  78*2, 214*2, 230*2 },
    {  28*2, 212*2, 120*2 }, {  51*2, 109*2, 212*2 }, {  63*2, 193*2, 204*2 },
    { 133*2,  63*2,  52*2 }, { 145*2, 147*2,  44*2 }, { 168*2,  44*2, 136*2 },
    { 470, 256, 256 }, { 32, 256, 256 } },
  { { 210*4,  16*4, 146*4 }, { 170*4, 166*4,  16*4 }, { 145*4,  54*4,  34*4 },
    { 106*4, 202*4, 222*4 }, {  81*4,  90*4, 240*4 }, {  41*4, 240*4, 110*4 },
    { 162*4,  44*4, 142*4 }, { 131*4, 156*4,  44*4 }, { 112*4,  72*4,  58*4 },
    {  84*4, 184*4, 198*4 }, {  65*4, 100*4, 212*4 }, {  35*4, 212*4, 114*4 },
    { 940, 512, 512 }, { 64, 512, 512 } },
  { {  63*4, 102*4, 240*4 }, {  32*4, 240*4, 118*4 }, { 188*4, 154*4,  16*4 },
    { 219*4,  16*4, 138*4 }, { 173*4,  42*4,  26*4 }, {  78*4, 214*4, 230*4 },
    {  28*4, 212*4, 120*4 }, {  51*4, 109*4, 212*4 }, {  63*4, 193*4, 204*4 },
    { 133*4,  63*4,  52*4 }, { 145*4, 147*4,  44*4 }, { 168*4,  44*4, 136*4 },
    { 940, 512, 512 }, { 64, 512, 512 } },
};

static void draw_dots(uint8_t *dst, int L, int v, float o)
{
    const float f = 1. - o;
    const float V = o * v;
    int l = L * 2;

    dst[ l - 3] = dst[ l - 3] * f + V;
    dst[ l + 3] = dst[ l + 3] * f + V;
    dst[-l - 3] = dst[-l - 3] * f + V;
    dst[-l + 3] = dst[-l + 3] * f + V;

    l += L;

    dst[ l - 3] = dst[ l - 3] * f + V;
    dst[ l + 3] = dst[ l + 3] * f + V;
    dst[ l - 2] = dst[ l - 2] * f + V;
    dst[ l + 2] = dst[ l + 2] * f + V;
    dst[-l - 3] = dst[-l - 3] * f + V;
    dst[-l + 3] = dst[-l + 3] * f + V;
    dst[-l - 2] = dst[-l - 2] * f + V;
    dst[-l + 2] = dst[-l + 2] * f + V;
}

static void draw_dots16(uint16_t *dst, int L, int v, float o)
{
    const float f = 1. - o;
    const float V = o * v;
    int l = L * 2;

    dst[ l - 3] = dst[ l - 3] * f + V;
    dst[ l + 3] = dst[ l + 3] * f + V;
    dst[-l - 3] = dst[-l - 3] * f + V;
    dst[-l + 3] = dst[-l + 3] * f + V;

    l += L;

    dst[ l - 3] = dst[ l - 3] * f + V;
    dst[ l + 3] = dst[ l + 3] * f + V;
    dst[ l - 2] = dst[ l - 2] * f + V;
    dst[ l + 2] = dst[ l + 2] * f + V;
    dst[-l - 3] = dst[-l - 3] * f + V;
    dst[-l + 3] = dst[-l + 3] * f + V;
    dst[-l - 2] = dst[-l - 2] * f + V;
    dst[-l + 2] = dst[-l + 2] * f + V;
}

static void none_graticule(VectorscopeContext *s, AVFrame *out, int X, int Y, int D, int P)
{
}

static void color_graticule16(VectorscopeContext *s, AVFrame *out, int X, int Y, int D, int P)
{
    const float o = s->opacity;
    int i;

    for (i = 0; i < 12; i++) {
        int x = positions[P][i][X];
        int y = positions[P][i][Y];
        int d = positions[P][i][D];

        draw_dots16((uint16_t *)(out->data[D] + y * out->linesize[D] + x * 2), out->linesize[D] / 2, d, o);
        draw_dots16((uint16_t *)(out->data[X] + y * out->linesize[X] + x * 2), out->linesize[X] / 2, x, o);
        draw_dots16((uint16_t *)(out->data[Y] + y * out->linesize[Y] + x * 2), out->linesize[Y] / 2, y, o);
    }

    if (s->flags & 1) {
        int x = positions[P][12][X];
        int y = positions[P][12][Y];
        int d = positions[P][12][D];

        draw_dots16((uint16_t *)(out->data[D] + y * out->linesize[D] + x * 2), out->linesize[D] / 2, d, o);
        draw_dots16((uint16_t *)(out->data[X] + y * out->linesize[X] + x * 2), out->linesize[X] / 2, x, o);
        draw_dots16((uint16_t *)(out->data[Y] + y * out->linesize[Y] + x * 2), out->linesize[Y] / 2, y, o);
    }

    if (s->flags & 2) {
        int x = positions[P][13][X];
        int y = positions[P][13][Y];
        int d = positions[P][13][D];

        draw_dots16((uint16_t *)(out->data[D] + y * out->linesize[D] + x * 2), out->linesize[D] / 2, d, o);
        draw_dots16((uint16_t *)(out->data[X] + y * out->linesize[X] + x * 2), out->linesize[X] / 2, x, o);
        draw_dots16((uint16_t *)(out->data[Y] + y * out->linesize[Y] + x * 2), out->linesize[Y] / 2, y, o);
    }
}

static void color_graticule(VectorscopeContext *s, AVFrame *out, int X, int Y, int D, int P)
{
    const float o = s->opacity;
    int i;

    for (i = 0; i < 12; i++) {
        int x = positions[P][i][X];
        int y = positions[P][i][Y];
        int d = positions[P][i][D];

        draw_dots(out->data[D] + y * out->linesize[D] + x, out->linesize[D], d, o);
        draw_dots(out->data[X] + y * out->linesize[X] + x, out->linesize[X], x, o);
        draw_dots(out->data[Y] + y * out->linesize[Y] + x, out->linesize[Y], y, o);
    }

    if (s->flags & 1) {
        int x = positions[P][12][X];
        int y = positions[P][12][Y];
        int d = positions[P][12][D];

        draw_dots(out->data[D] + y * out->linesize[D] + x, out->linesize[D], d, o);
        draw_dots(out->data[X] + y * out->linesize[X] + x, out->linesize[X], x, o);
        draw_dots(out->data[Y] + y * out->linesize[Y] + x, out->linesize[Y], y, o);
    }

    if (s->flags & 2) {
        int x = positions[P][13][X];
        int y = positions[P][13][Y];
        int d = positions[P][12][D];

        draw_dots(out->data[D] + y * out->linesize[D] + x, out->linesize[D], d, o);
        draw_dots(out->data[X] + y * out->linesize[X] + x, out->linesize[X], x, o);
        draw_dots(out->data[Y] + y * out->linesize[Y] + x, out->linesize[Y], y, o);
    }
}

static void green_graticule16(VectorscopeContext *s, AVFrame *out, int X, int Y, int D, int P)
{
    const float o = s->opacity;
    const int m = s->mult;
    int i;

    for (i = 0; i < 12; i++) {
        int x = positions[P][i][X];
        int y = positions[P][i][Y];

        draw_dots16((uint16_t *)(out->data[D] + y * out->linesize[D] + x * 2), out->linesize[D] / 2, 128 * m, o);
        draw_dots16((uint16_t *)(out->data[X] + y * out->linesize[X] + x * 2), out->linesize[X] / 2, 0, o);
        draw_dots16((uint16_t *)(out->data[Y] + y * out->linesize[Y] + x * 2), out->linesize[Y] / 2, 0, o);
    }

    if (s->flags & 1) {
        int x = positions[P][12][X];
        int y = positions[P][12][Y];

        draw_dots16((uint16_t *)(out->data[D] + y * out->linesize[D] + x * 2), out->linesize[D] / 2, 128 * m, o);
        draw_dots16((uint16_t *)(out->data[X] + y * out->linesize[X] + x * 2), out->linesize[X] / 2, 0, o);
        draw_dots16((uint16_t *)(out->data[Y] + y * out->linesize[Y] + x * 2), out->linesize[Y] / 2, 0, o);
    }

    if (s->flags & 2) {
        int x = positions[P][13][X];
        int y = positions[P][13][Y];

        draw_dots16((uint16_t *)(out->data[D] + y * out->linesize[D] + x * 2), out->linesize[D] / 2, 128 * m, o);
        draw_dots16((uint16_t *)(out->data[X] + y * out->linesize[X] + x * 2), out->linesize[X] / 2, 0, o);
        draw_dots16((uint16_t *)(out->data[Y] + y * out->linesize[Y] + x * 2), out->linesize[Y] / 2, 0, o);
    }
}

static void green_graticule(VectorscopeContext *s, AVFrame *out, int X, int Y, int D, int P)
{
    const float o = s->opacity;
    int i;

    for (i = 0; i < 12; i++) {
        int x = positions[P][i][X];
        int y = positions[P][i][Y];

        draw_dots(out->data[D] + y * out->linesize[D] + x, out->linesize[D], 128, o);
        draw_dots(out->data[X] + y * out->linesize[X] + x, out->linesize[X], 0, o);
        draw_dots(out->data[Y] + y * out->linesize[Y] + x, out->linesize[Y], 0, o);
    }

    if (s->flags & 1) {
        int x = positions[P][12][X];
        int y = positions[P][12][Y];

        draw_dots(out->data[D] + y * out->linesize[D] + x, out->linesize[D], 128, o);
        draw_dots(out->data[X] + y * out->linesize[X] + x, out->linesize[X], 0, o);
        draw_dots(out->data[Y] + y * out->linesize[Y] + x, out->linesize[Y], 0, o);
    }

    if (s->flags & 2) {
        int x = positions[P][13][X];
        int y = positions[P][13][Y];

        draw_dots(out->data[D] + y * out->linesize[D] + x, out->linesize[D], 128, o);
        draw_dots(out->data[X] + y * out->linesize[X] + x, out->linesize[X], 0, o);
        draw_dots(out->data[Y] + y * out->linesize[Y] + x, out->linesize[Y], 0, o);
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx  = inlink->dst;
    VectorscopeContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;

    switch (av_frame_get_colorspace(in)) {
    case AVCOL_SPC_SMPTE170M:
    case AVCOL_SPC_BT470BG:
        s->cs = (s->depth - 8) * 2 + 0;
        break;
    case AVCOL_SPC_BT709:
    default:
        s->cs = (s->depth - 8) * 2 + 1;
    }

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    out->pts = in->pts;

    s->vectorscope(s, in, out, s->pd);
    s->graticulef(s, out, s->x, s->y, s->pd, s->cs);

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int config_input(AVFilterLink *inlink)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    VectorscopeContext *s = inlink->dst->priv;

    s->is_yuv = !(desc->flags & AV_PIX_FMT_FLAG_RGB);
    s->size = 1 << desc->comp[0].depth;
    s->mult = s->size / 256;
    s->depth = desc->comp[0].depth;

    if (s->mode == GRAY && s->is_yuv)
        s->pd = 0;
    else {
        if ((s->x == 1 && s->y == 2) || (s->x == 2 && s->y == 1))
            s->pd = 0;
        else if ((s->x == 0 && s->y == 2) || (s->x == 2 && s->y == 0))
            s->pd = 1;
        else if ((s->x == 0 && s->y == 1) || (s->x == 1 && s->y == 0))
            s->pd = 2;
    }

    if (s->size == 256)
        s->vectorscope = vectorscope8;
    else
        s->vectorscope = vectorscope16;

    s->graticulef = none_graticule;

    if (s->is_yuv && s->size == 256) {
        if (s->graticule == 1)
            s->graticulef = green_graticule;
        else if (s->graticule == 2)
            s->graticulef = color_graticule;
    } else if (s->is_yuv) {
        if (s->graticule == 1)
            s->graticulef = green_graticule16;
        else if (s->graticule == 2)
            s->graticulef = color_graticule16;
    }

    switch (inlink->format) {
    case AV_PIX_FMT_GBRP10:
    case AV_PIX_FMT_GBRP9:
    case AV_PIX_FMT_GBRAP:
    case AV_PIX_FMT_GBRP:
        s->bg_color = black_gbrp_color;
        break;
    default:
        s->bg_color = black_yuva_color;
    }

    s->hsub = desc->log2_chroma_w;
    s->vsub = desc->log2_chroma_h;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;
    s->planewidth[1]  = s->planewidth[2]  = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0]  = s->planewidth[3]  = inlink->w;

    return 0;
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter ff_vf_vectorscope = {
    .name          = "vectorscope",
    .description   = NULL_IF_CONFIG_SMALL("Video vectorscope."),
    .priv_size     = sizeof(VectorscopeContext),
    .priv_class    = &vectorscope_class,
    .query_formats = query_formats,
    .inputs        = inputs,
    .outputs       = outputs,
};
