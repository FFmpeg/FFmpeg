/*
 * Copyright (c) 2012-2016 Paul B Mahol
 * Copyright (c) 2013 Marton Balint
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
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/xga_font_data.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

enum FilterType {
    LOWPASS,
    FLAT,
    AFLAT,
    CHROMA,
    COLOR,
    ACOLOR,
    NB_FILTERS
};

enum DisplayType {
    OVERLAY,
    STACK,
    PARADE,
    NB_DISPLAYS
};

enum ScaleType {
    DIGITAL,
    MILLIVOLTS,
    IRE,
    NB_SCALES
};

typedef struct GraticuleLine {
    const char *name;
    uint16_t pos;
} GraticuleLine;

typedef struct GraticuleLines {
    struct GraticuleLine line[4];
} GraticuleLines;

typedef struct WaveformContext {
    const AVClass *class;
    int            mode;
    int            acomp;
    int            dcomp;
    int            ncomp;
    int            pcomp;
    uint8_t        bg_color[4];
    float          fintensity;
    int            intensity;
    int            mirror;
    int            display;
    int            envelope;
    int            graticule;
    float          opacity;
    float          bgopacity;
    int            estart[4];
    int            eend[4];
    int            *emax[4][4];
    int            *emin[4][4];
    int            *peak;
    int            filter;
    int            flags;
    int            bits;
    int            max;
    int            size;
    int            scale;
    int            shift_w[4], shift_h[4];
    GraticuleLines *glines;
    int            nb_glines;
    void (*waveform)(struct WaveformContext *s,
                     AVFrame *in, AVFrame *out,
                     int component, int intensity,
                     int offset_y, int offset_x,
                     int column, int mirror);
    void (*graticulef)(struct WaveformContext *s, AVFrame *out);
    const AVPixFmtDescriptor *desc;
    const AVPixFmtDescriptor *odesc;
} WaveformContext;

#define OFFSET(x) offsetof(WaveformContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption waveform_options[] = {
    { "mode", "set mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "mode" },
    { "m",    "set mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "mode" },
        { "row",    NULL, 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "mode" },
        { "column", NULL, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "mode" },
    { "intensity", "set intensity", OFFSET(fintensity), AV_OPT_TYPE_FLOAT, {.dbl=0.04}, 0, 1, FLAGS },
    { "i",         "set intensity", OFFSET(fintensity), AV_OPT_TYPE_FLOAT, {.dbl=0.04}, 0, 1, FLAGS },
    { "mirror", "set mirroring", OFFSET(mirror), AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1, FLAGS },
    { "r",      "set mirroring", OFFSET(mirror), AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1, FLAGS },
    { "display", "set display mode", OFFSET(display), AV_OPT_TYPE_INT, {.i64=STACK}, 0, NB_DISPLAYS-1, FLAGS, "display" },
    { "d",       "set display mode", OFFSET(display), AV_OPT_TYPE_INT, {.i64=STACK}, 0, NB_DISPLAYS-1, FLAGS, "display" },
        { "overlay", NULL, 0, AV_OPT_TYPE_CONST, {.i64=OVERLAY}, 0, 0, FLAGS, "display" },
        { "stack",   NULL, 0, AV_OPT_TYPE_CONST, {.i64=STACK},   0, 0, FLAGS, "display" },
        { "parade",  NULL, 0, AV_OPT_TYPE_CONST, {.i64=PARADE},  0, 0, FLAGS, "display" },
    { "components", "set components to display", OFFSET(pcomp), AV_OPT_TYPE_INT, {.i64=1}, 1, 15, FLAGS },
    { "c",          "set components to display", OFFSET(pcomp), AV_OPT_TYPE_INT, {.i64=1}, 1, 15, FLAGS },
    { "envelope", "set envelope to display", OFFSET(envelope), AV_OPT_TYPE_INT, {.i64=0}, 0, 3, FLAGS, "envelope" },
    { "e",        "set envelope to display", OFFSET(envelope), AV_OPT_TYPE_INT, {.i64=0}, 0, 3, FLAGS, "envelope" },
        { "none",         NULL, 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "envelope" },
        { "instant",      NULL, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "envelope" },
        { "peak",         NULL, 0, AV_OPT_TYPE_CONST, {.i64=2}, 0, 0, FLAGS, "envelope" },
        { "peak+instant", NULL, 0, AV_OPT_TYPE_CONST, {.i64=3}, 0, 0, FLAGS, "envelope" },
    { "filter", "set filter", OFFSET(filter), AV_OPT_TYPE_INT, {.i64=0}, 0, NB_FILTERS-1, FLAGS, "filter" },
    { "f",      "set filter", OFFSET(filter), AV_OPT_TYPE_INT, {.i64=0}, 0, NB_FILTERS-1, FLAGS, "filter" },
        { "lowpass", NULL, 0, AV_OPT_TYPE_CONST, {.i64=LOWPASS}, 0, 0, FLAGS, "filter" },
        { "flat"   , NULL, 0, AV_OPT_TYPE_CONST, {.i64=FLAT},    0, 0, FLAGS, "filter" },
        { "aflat"  , NULL, 0, AV_OPT_TYPE_CONST, {.i64=AFLAT},   0, 0, FLAGS, "filter" },
        { "chroma",  NULL, 0, AV_OPT_TYPE_CONST, {.i64=CHROMA},  0, 0, FLAGS, "filter" },
        { "color",   NULL, 0, AV_OPT_TYPE_CONST, {.i64=COLOR},   0, 0, FLAGS, "filter" },
        { "acolor",  NULL, 0, AV_OPT_TYPE_CONST, {.i64=ACOLOR},  0, 0, FLAGS, "filter" },
    { "graticule", "set graticule", OFFSET(graticule), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS, "graticule" },
    { "g",         "set graticule", OFFSET(graticule), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS, "graticule" },
        { "none",  NULL, 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "graticule" },
        { "green", NULL, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "graticule" },
    { "opacity", "set graticule opacity", OFFSET(opacity), AV_OPT_TYPE_FLOAT, {.dbl=0.75}, 0, 1, FLAGS },
    { "o",       "set graticule opacity", OFFSET(opacity), AV_OPT_TYPE_FLOAT, {.dbl=0.75}, 0, 1, FLAGS },
    { "flags", "set graticule flags", OFFSET(flags), AV_OPT_TYPE_FLAGS, {.i64=1}, 0, 3, FLAGS, "flags" },
    { "fl",    "set graticule flags", OFFSET(flags), AV_OPT_TYPE_FLAGS, {.i64=1}, 0, 3, FLAGS, "flags" },
        { "numbers",  "draw numbers", 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "flags" },
        { "dots",     "draw dots instead of lines", 0, AV_OPT_TYPE_CONST, {.i64=2}, 0, 0, FLAGS, "flags" },
    { "scale", "set scale", OFFSET(scale), AV_OPT_TYPE_INT, {.i64=0}, 0, NB_SCALES-1, FLAGS, "scale" },
    { "s",     "set scale", OFFSET(scale), AV_OPT_TYPE_INT, {.i64=0}, 0, NB_SCALES-1, FLAGS, "scale" },
        { "digital",    NULL, 0, AV_OPT_TYPE_CONST, {.i64=DIGITAL},    0, 0, FLAGS, "scale" },
        { "millivolts", NULL, 0, AV_OPT_TYPE_CONST, {.i64=MILLIVOLTS}, 0, 0, FLAGS, "scale" },
        { "ire",        NULL, 0, AV_OPT_TYPE_CONST, {.i64=IRE},        0, 0, FLAGS, "scale" },
    { "bgopacity", "set background opacity", OFFSET(bgopacity), AV_OPT_TYPE_FLOAT, {.dbl=0.75}, 0, 1, FLAGS },
    { "b",         "set background opacity", OFFSET(bgopacity), AV_OPT_TYPE_FLOAT, {.dbl=0.75}, 0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(waveform);

static const enum AVPixelFormat in_lowpass_pix_fmts[] = {
    AV_PIX_FMT_GBRP,     AV_PIX_FMT_GBRAP,
    AV_PIX_FMT_GBRP9,    AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRP12,
    AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV440P,
    AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA420P,
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12,
    AV_PIX_FMT_YUV444P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV420P9,
    AV_PIX_FMT_YUVA444P9, AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA420P9,
    AV_PIX_FMT_YUV444P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV420P10,
    AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA420P10,
    AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV440P12,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat in_color_pix_fmts[] = {
    AV_PIX_FMT_GBRP,     AV_PIX_FMT_GBRAP,
    AV_PIX_FMT_GBRP9,    AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRP12,
    AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV440P,
    AV_PIX_FMT_YUV411P,
    AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA420P,
    AV_PIX_FMT_YUV444P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV420P9,
    AV_PIX_FMT_YUVA444P9, AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA420P9,
    AV_PIX_FMT_YUV444P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV420P10,
    AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA420P10,
    AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV440P12,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat in_flat_pix_fmts[] = {
    AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV440P,
    AV_PIX_FMT_YUV411P,
    AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA420P,
    AV_PIX_FMT_YUV444P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV420P9,
    AV_PIX_FMT_YUVA444P9, AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA420P9,
    AV_PIX_FMT_YUV444P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV420P10,
    AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA420P10,
    AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV440P12,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat out_rgb8_lowpass_pix_fmts[] = {
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat out_rgb9_lowpass_pix_fmts[] = {
    AV_PIX_FMT_GBRP9,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat out_rgb10_lowpass_pix_fmts[] = {
    AV_PIX_FMT_GBRP10,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat out_rgb12_lowpass_pix_fmts[] = {
    AV_PIX_FMT_GBRP12,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat out_yuv8_lowpass_pix_fmts[] = {
    AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat out_yuv9_lowpass_pix_fmts[] = {
    AV_PIX_FMT_YUV444P9, AV_PIX_FMT_YUVA444P9,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat out_yuv10_lowpass_pix_fmts[] = {
    AV_PIX_FMT_YUV444P10, AV_PIX_FMT_YUVA444P10,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat out_yuv12_lowpass_pix_fmts[] = {
    AV_PIX_FMT_YUV444P12,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat out_gray8_lowpass_pix_fmts[] = {
    AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat out_gray10_lowpass_pix_fmts[] = {
    AV_PIX_FMT_GRAY10,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat out_gray12_lowpass_pix_fmts[] = {
    AV_PIX_FMT_GRAY12,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat flat_pix_fmts[] = {
    AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_YUV444P9, AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV444P12,
    AV_PIX_FMT_NONE
};

static int query_formats(AVFilterContext *ctx)
{
    WaveformContext *s = ctx->priv;
    const enum AVPixelFormat *out_pix_fmts;
    const enum AVPixelFormat *in_pix_fmts;
    const AVPixFmtDescriptor *desc;
    AVFilterFormats *avff;
    int depth, rgb, i, ret, ncomp;

    if (!ctx->inputs[0]->in_formats ||
        !ctx->inputs[0]->in_formats->nb_formats) {
        return AVERROR(EAGAIN);
    }

    switch (s->filter) {
    case LOWPASS: in_pix_fmts = in_lowpass_pix_fmts; break;
    case CHROMA:
    case AFLAT:
    case FLAT:    in_pix_fmts = in_flat_pix_fmts;    break;
    case ACOLOR:
    case COLOR:   in_pix_fmts = in_color_pix_fmts;   break;
    }

    if (!ctx->inputs[0]->out_formats) {
        if ((ret = ff_formats_ref(ff_make_format_list(in_pix_fmts), &ctx->inputs[0]->out_formats)) < 0)
            return ret;
    }

    avff = ctx->inputs[0]->in_formats;
    desc = av_pix_fmt_desc_get(avff->formats[0]);
    ncomp = desc->nb_components;
    rgb = desc->flags & AV_PIX_FMT_FLAG_RGB;
    depth = desc->comp[0].depth;
    for (i = 1; i < avff->nb_formats; i++) {
        desc = av_pix_fmt_desc_get(avff->formats[i]);
        if (rgb != (desc->flags & AV_PIX_FMT_FLAG_RGB) ||
            depth != desc->comp[0].depth)
            return AVERROR(EAGAIN);
    }

    if (s->filter == LOWPASS && ncomp == 1 && depth == 8)
        out_pix_fmts = out_gray8_lowpass_pix_fmts;
    else if (s->filter == LOWPASS && ncomp == 1 && depth == 10)
        out_pix_fmts = out_gray10_lowpass_pix_fmts;
    else if (s->filter == LOWPASS && ncomp == 1 && depth == 12)
        out_pix_fmts = out_gray12_lowpass_pix_fmts;
    else if (rgb && depth == 8 && ncomp > 2)
        out_pix_fmts = out_rgb8_lowpass_pix_fmts;
    else if (rgb && depth == 9 && ncomp > 2)
        out_pix_fmts = out_rgb9_lowpass_pix_fmts;
    else if (rgb && depth == 10 && ncomp > 2)
        out_pix_fmts = out_rgb10_lowpass_pix_fmts;
    else if (rgb && depth == 12 && ncomp > 2)
        out_pix_fmts = out_rgb12_lowpass_pix_fmts;
    else if (depth == 8 && ncomp > 2)
        out_pix_fmts = out_yuv8_lowpass_pix_fmts;
    else if (depth == 9 && ncomp > 2)
        out_pix_fmts = out_yuv9_lowpass_pix_fmts;
    else if (depth == 10 && ncomp > 2)
        out_pix_fmts = out_yuv10_lowpass_pix_fmts;
    else if (depth == 12 && ncomp > 2)
        out_pix_fmts = out_yuv12_lowpass_pix_fmts;
    else
        return AVERROR(EAGAIN);
    if ((ret = ff_formats_ref(ff_make_format_list(out_pix_fmts), &ctx->outputs[0]->in_formats)) < 0)
        return ret;

    return 0;
}

static void envelope_instant16(WaveformContext *s, AVFrame *out, int plane, int component, int offset)
{
    const int dst_linesize = out->linesize[component] / 2;
    const int bg = s->bg_color[component] * (s->max / 256);
    const int limit = s->max - 1;
    const int dst_h = s->display == PARADE ? out->height / s->acomp : out->height;
    const int dst_w = s->display == PARADE ? out->width / s->acomp : out->width;
    const int start = s->estart[plane];
    const int end = s->eend[plane];
    uint16_t *dst;
    int x, y;

    if (s->mode) {
        for (x = offset; x < offset + dst_w; x++) {
            for (y = start; y < end; y++) {
                dst = (uint16_t *)out->data[component] + y * dst_linesize + x;
                if (dst[0] != bg) {
                    dst[0] = limit;
                    break;
                }
            }
            for (y = end - 1; y >= start; y--) {
                dst = (uint16_t *)out->data[component] + y * dst_linesize + x;
                if (dst[0] != bg) {
                    dst[0] = limit;
                    break;
                }
            }
        }
    } else {
        for (y = offset; y < offset + dst_h; y++) {
            dst = (uint16_t *)out->data[component] + y * dst_linesize;
            for (x = start; x < end; x++) {
                if (dst[x] != bg) {
                    dst[x] = limit;
                    break;
                }
            }
            for (x = end - 1; x >= start; x--) {
                if (dst[x] != bg) {
                    dst[x] = limit;
                    break;
                }
            }
        }
    }
}

static void envelope_instant(WaveformContext *s, AVFrame *out, int plane, int component, int offset)
{
    const int dst_linesize = out->linesize[component];
    const uint8_t bg = s->bg_color[component];
    const int dst_h = s->display == PARADE ? out->height / s->acomp : out->height;
    const int dst_w = s->display == PARADE ? out->width / s->acomp : out->width;
    const int start = s->estart[plane];
    const int end = s->eend[plane];
    uint8_t *dst;
    int x, y;

    if (s->mode) {
        for (x = offset; x < offset + dst_w; x++) {
            for (y = start; y < end; y++) {
                dst = out->data[component] + y * dst_linesize + x;
                if (dst[0] != bg) {
                    dst[0] = 255;
                    break;
                }
            }
            for (y = end - 1; y >= start; y--) {
                dst = out->data[component] + y * dst_linesize + x;
                if (dst[0] != bg) {
                    dst[0] = 255;
                    break;
                }
            }
        }
    } else {
        for (y = offset; y < offset + dst_h; y++) {
            dst = out->data[component] + y * dst_linesize;
            for (x = start; x < end; x++) {
                if (dst[x] != bg) {
                    dst[x] = 255;
                    break;
                }
            }
            for (x = end - 1; x >= start; x--) {
                if (dst[x] != bg) {
                    dst[x] = 255;
                    break;
                }
            }
        }
    }
}

static void envelope_peak16(WaveformContext *s, AVFrame *out, int plane, int component, int offset)
{
    const int dst_linesize = out->linesize[component] / 2;
    const int bg = s->bg_color[component] * (s->max / 256);
    const int limit = s->max - 1;
    const int dst_h = s->display == PARADE ? out->height / s->acomp : out->height;
    const int dst_w = s->display == PARADE ? out->width / s->acomp : out->width;
    const int start = s->estart[plane];
    const int end = s->eend[plane];
    int *emax = s->emax[plane][component];
    int *emin = s->emin[plane][component];
    uint16_t *dst;
    int x, y;

    if (s->mode) {
        for (x = offset; x < offset + dst_w; x++) {
            for (y = start; y < end && y < emin[x - offset]; y++) {
                dst = (uint16_t *)out->data[component] + y * dst_linesize + x;
                if (dst[0] != bg) {
                    emin[x - offset] = y;
                    break;
                }
            }
            for (y = end - 1; y >= start && y >= emax[x - offset]; y--) {
                dst = (uint16_t *)out->data[component] + y * dst_linesize + x;
                if (dst[0] != bg) {
                    emax[x - offset] = y;
                    break;
                }
            }
        }

        if (s->envelope == 3)
            envelope_instant16(s, out, plane, component, offset);

        for (x = offset; x < offset + dst_w; x++) {
            dst = (uint16_t *)out->data[component] + emin[x - offset] * dst_linesize + x;
            dst[0] = limit;
            dst = (uint16_t *)out->data[component] + emax[x - offset] * dst_linesize + x;
            dst[0] = limit;
        }
    } else {
        for (y = offset; y < offset + dst_h; y++) {
            dst = (uint16_t *)out->data[component] + y * dst_linesize;
            for (x = start; x < end && x < emin[y - offset]; x++) {
                if (dst[x] != bg) {
                    emin[y - offset] = x;
                    break;
                }
            }
            for (x = end - 1; x >= start && x >= emax[y - offset]; x--) {
                if (dst[x] != bg) {
                    emax[y - offset] = x;
                    break;
                }
            }
        }

        if (s->envelope == 3)
            envelope_instant16(s, out, plane, component, offset);

        for (y = offset; y < offset + dst_h; y++) {
            dst = (uint16_t *)out->data[component] + y * dst_linesize + emin[y - offset];
            dst[0] = limit;
            dst = (uint16_t *)out->data[component] + y * dst_linesize + emax[y - offset];
            dst[0] = limit;
        }
    }
}

static void envelope_peak(WaveformContext *s, AVFrame *out, int plane, int component, int offset)
{
    const int dst_linesize = out->linesize[component];
    const int bg = s->bg_color[component];
    const int dst_h = s->display == PARADE ? out->height / s->acomp : out->height;
    const int dst_w = s->display == PARADE ? out->width / s->acomp : out->width;
    const int start = s->estart[plane];
    const int end = s->eend[plane];
    int *emax = s->emax[plane][component];
    int *emin = s->emin[plane][component];
    uint8_t *dst;
    int x, y;

    if (s->mode) {
        for (x = offset; x < offset + dst_w; x++) {
            for (y = start; y < end && y < emin[x - offset]; y++) {
                dst = out->data[component] + y * dst_linesize + x;
                if (dst[0] != bg) {
                    emin[x - offset] = y;
                    break;
                }
            }
            for (y = end - 1; y >= start && y >= emax[x - offset]; y--) {
                dst = out->data[component] + y * dst_linesize + x;
                if (dst[0] != bg) {
                    emax[x - offset] = y;
                    break;
                }
            }
        }

        if (s->envelope == 3)
            envelope_instant(s, out, plane, component, offset);

        for (x = offset; x < offset + dst_w; x++) {
            dst = out->data[component] + emin[x - offset] * dst_linesize + x;
            dst[0] = 255;
            dst = out->data[component] + emax[x - offset] * dst_linesize + x;
            dst[0] = 255;
        }
    } else {
        for (y = offset; y < offset + dst_h; y++) {
            dst = out->data[component] + y * dst_linesize;
            for (x = start; x < end && x < emin[y - offset]; x++) {
                if (dst[x] != bg) {
                    emin[y - offset] = x;
                    break;
                }
            }
            for (x = end - 1; x >= start && x >= emax[y - offset]; x--) {
                if (dst[x] != bg) {
                    emax[y - offset] = x;
                    break;
                }
            }
        }

        if (s->envelope == 3)
            envelope_instant(s, out, plane, component, offset);

        for (y = offset; y < offset + dst_h; y++) {
            dst = out->data[component] + y * dst_linesize + emin[y - offset];
            dst[0] = 255;
            dst = out->data[component] + y * dst_linesize + emax[y - offset];
            dst[0] = 255;
        }
    }
}

static void envelope16(WaveformContext *s, AVFrame *out, int plane, int component, int offset)
{
    if (s->envelope == 0) {
        return;
    } else if (s->envelope == 1) {
        envelope_instant16(s, out, plane, component, offset);
    } else {
        envelope_peak16(s, out, plane, component, offset);
    }
}

static void envelope(WaveformContext *s, AVFrame *out, int plane, int component, int offset)
{
    if (s->envelope == 0) {
        return;
    } else if (s->envelope == 1) {
        envelope_instant(s, out, plane, component, offset);
    } else {
        envelope_peak(s, out, plane, component, offset);
    }
}

static void update16(uint16_t *target, int max, int intensity, int limit)
{
    if (*target <= max)
        *target += intensity;
    else
        *target = limit;
}

static void update(uint8_t *target, int max, int intensity)
{
    if (*target <= max)
        *target += intensity;
    else
        *target = 255;
}

static av_always_inline void lowpass16(WaveformContext *s,
                                       AVFrame *in, AVFrame *out,
                                       int component, int intensity,
                                       int offset_y, int offset_x,
                                       int column, int mirror)
{
    const int plane = s->desc->comp[component].plane;
    const int shift_w = s->shift_w[component];
    const int shift_h = s->shift_h[component];
    const int src_linesize = in->linesize[plane] / 2;
    const int dst_linesize = out->linesize[plane] / 2;
    const int dst_signed_linesize = dst_linesize * (mirror == 1 ? -1 : 1);
    const int limit = s->max - 1;
    const int max = limit - intensity;
    const int src_h = AV_CEIL_RSHIFT(in->height, shift_h);
    const int src_w = AV_CEIL_RSHIFT(in->width, shift_w);
    const uint16_t *src_data = (const uint16_t *)in->data[plane];
    uint16_t *dst_data = (uint16_t *)out->data[plane] + offset_y * dst_linesize + offset_x;
    uint16_t * const dst_bottom_line = dst_data + dst_linesize * (s->size - 1);
    uint16_t * const dst_line = (mirror ? dst_bottom_line : dst_data);
    const int step = column ? 1 << shift_w : 1 << shift_h;
    const uint16_t *p;
    int y;

    if (!column && mirror)
        dst_data += s->size;

    for (y = 0; y < src_h; y++) {
        const uint16_t *src_data_end = src_data + src_w;
        uint16_t *dst = dst_line;

        for (p = src_data; p < src_data_end; p++) {
            uint16_t *target;
            int i = 0, v = FFMIN(*p, limit);

            if (column) {
                do {
                    target = dst++ + dst_signed_linesize * v;
                    update16(target, max, intensity, limit);
                } while (++i < step);
            } else {
                uint16_t *row = dst_data;
                do {
                    if (mirror)
                        target = row - v - 1;
                    else
                        target = row + v;
                    update16(target, max, intensity, limit);
                    row += dst_linesize;
                } while (++i < step);
            }
        }
        src_data += src_linesize;
        dst_data += dst_linesize * step;
    }

    envelope16(s, out, plane, plane, column ? offset_x : offset_y);
}

#define LOWPASS16_FUNC(name, column, mirror)               \
static void lowpass16_##name(WaveformContext *s,           \
                             AVFrame *in, AVFrame *out,    \
                             int component, int intensity, \
                             int offset_y, int offset_x,   \
                             int unused1, int unused2)     \
{                                                          \
    lowpass16(s, in, out, component, intensity,            \
              offset_y, offset_x, column, mirror);         \
}

LOWPASS16_FUNC(column_mirror, 1, 1)
LOWPASS16_FUNC(column,        1, 0)
LOWPASS16_FUNC(row_mirror,    0, 1)
LOWPASS16_FUNC(row,           0, 0)

static av_always_inline void lowpass(WaveformContext *s,
                                     AVFrame *in, AVFrame *out,
                                     int component, int intensity,
                                     int offset_y, int offset_x,
                                     int column, int mirror)
{
    const int plane = s->desc->comp[component].plane;
    const int shift_w = s->shift_w[component];
    const int shift_h = s->shift_h[component];
    const int src_linesize = in->linesize[plane];
    const int dst_linesize = out->linesize[plane];
    const int dst_signed_linesize = dst_linesize * (mirror == 1 ? -1 : 1);
    const int max = 255 - intensity;
    const int src_h = AV_CEIL_RSHIFT(in->height, shift_h);
    const int src_w = AV_CEIL_RSHIFT(in->width, shift_w);
    const uint8_t *src_data = in->data[plane];
    uint8_t *dst_data = out->data[plane] + offset_y * dst_linesize + offset_x;
    uint8_t * const dst_bottom_line = dst_data + dst_linesize * (s->size - 1);
    uint8_t * const dst_line = (mirror ? dst_bottom_line : dst_data);
    const int step = column ? 1 << shift_w : 1 << shift_h;
    const uint8_t *p;
    int y;

    if (!column && mirror)
        dst_data += s->size;

    for (y = 0; y < src_h; y++) {
        const uint8_t *src_data_end = src_data + src_w;
        uint8_t *dst = dst_line;

        for (p = src_data; p < src_data_end; p++) {
            uint8_t *target;
            if (column) {
                target = dst + dst_signed_linesize * *p;
                dst += step;
                update(target, max, intensity);
            } else {
                uint8_t *row = dst_data;
                if (mirror)
                    target = row - *p - 1;
                else
                    target = row + *p;
                update(target, max, intensity);
                row += dst_linesize;
            }
        }
        src_data += src_linesize;
        dst_data += dst_linesize * step;
    }

    if (column && step > 1) {
        const int dst_w = s->display == PARADE ? out->width / s->acomp : out->width;
        const int dst_h = 256;
        uint8_t *dst;
        int x, z;

        dst = out->data[plane] + offset_y * dst_linesize + offset_x;
        for (y = 0; y < dst_h; y++) {
            for (x = 0; x < dst_w; x+=step) {
                for (z = 1; z < step; z++) {
                    dst[x + z] = dst[x];
                }
            }
            dst += dst_linesize;
        }
    } else if (step > 1) {
        const int dst_h = s->display == PARADE ? out->height / s->acomp : out->height;
        const int dst_w = 256;
        uint8_t *dst;
        int z;

        dst = out->data[plane] + offset_y * dst_linesize + offset_x;
        for (y = 0; y < dst_h; y+=step) {
            for (z = 1; z < step; z++)
                memcpy(dst + dst_linesize * z, dst, dst_w);
            dst += dst_linesize * step;
        }
    }

    envelope(s, out, plane, plane, column ? offset_x : offset_y);
}

#define LOWPASS_FUNC(name, column, mirror)               \
static void lowpass_##name(WaveformContext *s,           \
                           AVFrame *in, AVFrame *out,    \
                           int component, int intensity, \
                           int offset_y, int offset_x,   \
                           int unused1, int unused2)     \
{                                                        \
    lowpass(s, in, out, component, intensity,            \
            offset_y, offset_x, column, mirror);         \
}

LOWPASS_FUNC(column_mirror, 1, 1)
LOWPASS_FUNC(column,        1, 0)
LOWPASS_FUNC(row_mirror,    0, 1)
LOWPASS_FUNC(row,           0, 0)

static av_always_inline void flat16(WaveformContext *s,
                                    AVFrame *in, AVFrame *out,
                                    int component, int intensity,
                                    int offset_y, int offset_x,
                                    int column, int mirror)
{
    const int plane = s->desc->comp[component].plane;
    const int c0_linesize = in->linesize[ plane + 0 ] / 2;
    const int c1_linesize = in->linesize[(plane + 1) % s->ncomp] / 2;
    const int c2_linesize = in->linesize[(plane + 2) % s->ncomp] / 2;
    const int c0_shift_w = s->shift_w[ component + 0 ];
    const int c1_shift_w = s->shift_w[(component + 1) % s->ncomp];
    const int c2_shift_w = s->shift_w[(component + 2) % s->ncomp];
    const int c0_shift_h = s->shift_h[ component + 0 ];
    const int c1_shift_h = s->shift_h[(component + 1) % s->ncomp];
    const int c2_shift_h = s->shift_h[(component + 2) % s->ncomp];
    const int d0_linesize = out->linesize[ plane + 0 ] / 2;
    const int d1_linesize = out->linesize[(plane + 1) % s->ncomp] / 2;
    const int limit = s->max - 1;
    const int max = limit - intensity;
    const int mid = s->max / 2;
    const int src_h = in->height;
    const int src_w = in->width;
    int x, y;

    if (column) {
        const int d0_signed_linesize = d0_linesize * (mirror == 1 ? -1 : 1);
        const int d1_signed_linesize = d1_linesize * (mirror == 1 ? -1 : 1);

        for (x = 0; x < src_w; x++) {
            const uint16_t *c0_data = (uint16_t *)in->data[plane + 0];
            const uint16_t *c1_data = (uint16_t *)in->data[(plane + 1) % s->ncomp];
            const uint16_t *c2_data = (uint16_t *)in->data[(plane + 2) % s->ncomp];
            uint16_t *d0_data = (uint16_t *)(out->data[plane]) + offset_y * d0_linesize + offset_x;
            uint16_t *d1_data = (uint16_t *)(out->data[(plane + 1) % s->ncomp]) + offset_y * d1_linesize + offset_x;
            uint16_t * const d0_bottom_line = d0_data + d0_linesize * (s->size - 1);
            uint16_t * const d0 = (mirror ? d0_bottom_line : d0_data);
            uint16_t * const d1_bottom_line = d1_data + d1_linesize * (s->size - 1);
            uint16_t * const d1 = (mirror ? d1_bottom_line : d1_data);

            for (y = 0; y < src_h; y++) {
                const int c0 = FFMIN(c0_data[x >> c0_shift_w], limit) + s->max;
                const int c1 = FFMIN(FFABS(c1_data[x >> c1_shift_w] - mid) + FFABS(c2_data[x >> c2_shift_w] - mid), limit);
                uint16_t *target;

                target = d0 + x + d0_signed_linesize * c0;
                update16(target, max, intensity, limit);
                target = d1 + x + d1_signed_linesize * (c0 - c1);
                update16(target, max, intensity, limit);
                target = d1 + x + d1_signed_linesize * (c0 + c1);
                update16(target, max, intensity, limit);

                if (!c0_shift_h || (y & c0_shift_h))
                    c0_data += c0_linesize;
                if (!c1_shift_h || (y & c1_shift_h))
                    c1_data += c1_linesize;
                if (!c2_shift_h || (y & c2_shift_h))
                    c2_data += c2_linesize;
                d0_data += d0_linesize;
                d1_data += d1_linesize;
            }
        }
    } else {
        const uint16_t *c0_data = (uint16_t *)in->data[plane];
        const uint16_t *c1_data = (uint16_t *)in->data[(plane + 1) % s->ncomp];
        const uint16_t *c2_data = (uint16_t *)in->data[(plane + 2) % s->ncomp];
        uint16_t *d0_data = (uint16_t *)(out->data[plane]) + offset_y * d0_linesize + offset_x;
        uint16_t *d1_data = (uint16_t *)(out->data[(plane + 1) % s->ncomp]) + offset_y * d1_linesize + offset_x;

        if (mirror) {
            d0_data += s->size - 1;
            d1_data += s->size - 1;
        }

        for (y = 0; y < src_h; y++) {
            for (x = 0; x < src_w; x++) {
                const int c0 = FFMIN(c0_data[x >> c0_shift_w], limit) + s->max;
                const int c1 = FFMIN(FFABS(c1_data[x >> c1_shift_w] - mid) + FFABS(c2_data[x >> c2_shift_w] - mid), limit);
                uint16_t *target;

                if (mirror) {
                    target = d0_data - c0;
                    update16(target, max, intensity, limit);
                    target = d1_data - (c0 - c1);
                    update16(target, max, intensity, limit);
                    target = d1_data - (c0 + c1);
                    update16(target, max, intensity, limit);
                } else {
                    target = d0_data + c0;
                    update16(target, max, intensity, limit);
                    target = d1_data + (c0 - c1);
                    update16(target, max, intensity, limit);
                    target = d1_data + (c0 + c1);
                    update16(target, max, intensity, limit);
                }
            }

            if (!c0_shift_h || (y & c0_shift_h))
                c0_data += c0_linesize;
            if (!c1_shift_h || (y & c1_shift_h))
                c1_data += c1_linesize;
            if (!c2_shift_h || (y & c2_shift_h))
                c2_data += c2_linesize;
            d0_data += d0_linesize;
            d1_data += d1_linesize;
        }
    }

    envelope16(s, out, plane, plane, column ? offset_x : offset_y);
    envelope16(s, out, plane, (plane + 1) % s->ncomp, column ? offset_x : offset_y);
}

static av_always_inline void flat(WaveformContext *s,
                                  AVFrame *in, AVFrame *out,
                                  int component, int intensity,
                                  int offset_y, int offset_x,
                                  int column, int mirror)
{
    const int plane = s->desc->comp[component].plane;
    const int c0_linesize = in->linesize[ plane + 0 ];
    const int c1_linesize = in->linesize[(plane + 1) % s->ncomp];
    const int c2_linesize = in->linesize[(plane + 2) % s->ncomp];
    const int c0_shift_w = s->shift_w[ component + 0 ];
    const int c1_shift_w = s->shift_w[(component + 1) % s->ncomp];
    const int c2_shift_w = s->shift_w[(component + 2) % s->ncomp];
    const int c0_shift_h = s->shift_h[ component + 0 ];
    const int c1_shift_h = s->shift_h[(component + 1) % s->ncomp];
    const int c2_shift_h = s->shift_h[(component + 2) % s->ncomp];
    const int d0_linesize = out->linesize[ plane + 0 ];
    const int d1_linesize = out->linesize[(plane + 1) % s->ncomp];
    const int max = 255 - intensity;
    const int src_h = in->height;
    const int src_w = in->width;
    int x, y;

    if (column) {
        const int d0_signed_linesize = d0_linesize * (mirror == 1 ? -1 : 1);
        const int d1_signed_linesize = d1_linesize * (mirror == 1 ? -1 : 1);

        for (x = 0; x < src_w; x++) {
            const uint8_t *c0_data = in->data[plane + 0];
            const uint8_t *c1_data = in->data[(plane + 1) % s->ncomp];
            const uint8_t *c2_data = in->data[(plane + 2) % s->ncomp];
            uint8_t *d0_data = out->data[plane] + offset_y * d0_linesize + offset_x;
            uint8_t *d1_data = out->data[(plane + 1) % s->ncomp] + offset_y * d1_linesize + offset_x;
            uint8_t * const d0_bottom_line = d0_data + d0_linesize * (s->size - 1);
            uint8_t * const d0 = (mirror ? d0_bottom_line : d0_data);
            uint8_t * const d1_bottom_line = d1_data + d1_linesize * (s->size - 1);
            uint8_t * const d1 = (mirror ? d1_bottom_line : d1_data);

            for (y = 0; y < src_h; y++) {
                const int c0 = c0_data[x >> c0_shift_w] + 256;
                const int c1 = FFABS(c1_data[x >> c1_shift_w] - 128) + FFABS(c2_data[x >> c2_shift_w] - 128);
                uint8_t *target;

                target = d0 + x + d0_signed_linesize * c0;
                update(target, max, intensity);
                target = d1 + x + d1_signed_linesize * (c0 - c1);
                update(target, max, intensity);
                target = d1 + x + d1_signed_linesize * (c0 + c1);
                update(target, max, intensity);

                if (!c0_shift_h || (y & c0_shift_h))
                    c0_data += c0_linesize;
                if (!c1_shift_h || (y & c1_shift_h))
                    c1_data += c1_linesize;
                if (!c2_shift_h || (y & c2_shift_h))
                    c2_data += c2_linesize;
                d0_data += d0_linesize;
                d1_data += d1_linesize;
            }
        }
    } else {
        const uint8_t *c0_data = in->data[plane];
        const uint8_t *c1_data = in->data[(plane + 1) % s->ncomp];
        const uint8_t *c2_data = in->data[(plane + 2) % s->ncomp];
        uint8_t *d0_data = out->data[plane] + offset_y * d0_linesize + offset_x;
        uint8_t *d1_data = out->data[(plane + 1) % s->ncomp] + offset_y * d1_linesize + offset_x;

        if (mirror) {
            d0_data += s->size - 1;
            d1_data += s->size - 1;
        }

        for (y = 0; y < src_h; y++) {
            for (x = 0; x < src_w; x++) {
                int c0 = c0_data[x >> c0_shift_w] + 256;
                const int c1 = FFABS(c1_data[x >> c1_shift_w] - 128) + FFABS(c2_data[x >> c2_shift_w] - 128);
                uint8_t *target;

                if (mirror) {
                    target = d0_data - c0;
                    update(target, max, intensity);
                    target = d1_data - (c0 - c1);
                    update(target, max, intensity);
                    target = d1_data - (c0 + c1);
                    update(target, max, intensity);
                } else {
                    target = d0_data + c0;
                    update(target, max, intensity);
                    target = d1_data + (c0 - c1);
                    update(target, max, intensity);
                    target = d1_data + (c0 + c1);
                    update(target, max, intensity);
                }
            }

            if (!c0_shift_h || (y & c0_shift_h))
                c0_data += c0_linesize;
            if (!c1_shift_h || (y & c1_shift_h))
                c1_data += c1_linesize;
            if (!c2_shift_h || (y & c2_shift_h))
                c2_data += c2_linesize;
            d0_data += d0_linesize;
            d1_data += d1_linesize;
        }
    }

    envelope(s, out, plane, plane, column ? offset_x : offset_y);
    envelope(s, out, plane, (plane + 1) % s->ncomp, column ? offset_x : offset_y);
}

static av_always_inline void aflat16(WaveformContext *s,
                                     AVFrame *in, AVFrame *out,
                                     int component, int intensity,
                                     int offset_y, int offset_x,
                                     int column, int mirror)
{
    const int plane = s->desc->comp[component].plane;
    const int c0_linesize = in->linesize[ plane + 0 ] / 2;
    const int c1_linesize = in->linesize[(plane + 1) % s->ncomp] / 2;
    const int c2_linesize = in->linesize[(plane + 2) % s->ncomp] / 2;
    const int c0_shift_w = s->shift_w[ component + 0 ];
    const int c1_shift_w = s->shift_w[(component + 1) % s->ncomp];
    const int c2_shift_w = s->shift_w[(component + 2) % s->ncomp];
    const int c0_shift_h = s->shift_h[ component + 0 ];
    const int c1_shift_h = s->shift_h[(component + 1) % s->ncomp];
    const int c2_shift_h = s->shift_h[(component + 2) % s->ncomp];
    const int d0_linesize = out->linesize[ plane + 0 ] / 2;
    const int d1_linesize = out->linesize[(plane + 1) % s->ncomp] / 2;
    const int d2_linesize = out->linesize[(plane + 2) % s->ncomp] / 2;
    const int limit = s->max - 1;
    const int max = limit - intensity;
    const int mid = s->max / 2;
    const int src_h = in->height;
    const int src_w = in->width;
    int x, y;

    if (column) {
        const int d0_signed_linesize = d0_linesize * (mirror == 1 ? -1 : 1);
        const int d1_signed_linesize = d1_linesize * (mirror == 1 ? -1 : 1);
        const int d2_signed_linesize = d2_linesize * (mirror == 1 ? -1 : 1);

        for (x = 0; x < src_w; x++) {
            const uint16_t *c0_data = (uint16_t *)in->data[plane + 0];
            const uint16_t *c1_data = (uint16_t *)in->data[(plane + 1) % s->ncomp];
            const uint16_t *c2_data = (uint16_t *)in->data[(plane + 2) % s->ncomp];
            uint16_t *d0_data = (uint16_t *)out->data[plane] + offset_y * d0_linesize + offset_x;
            uint16_t *d1_data = (uint16_t *)out->data[(plane + 1) % s->ncomp] + offset_y * d1_linesize + offset_x;
            uint16_t *d2_data = (uint16_t *)out->data[(plane + 2) % s->ncomp] + offset_y * d2_linesize + offset_x;
            uint16_t * const d0_bottom_line = d0_data + d0_linesize * (s->size - 1);
            uint16_t * const d0 = (mirror ? d0_bottom_line : d0_data);
            uint16_t * const d1_bottom_line = d1_data + d1_linesize * (s->size - 1);
            uint16_t * const d1 = (mirror ? d1_bottom_line : d1_data);
            uint16_t * const d2_bottom_line = d2_data + d2_linesize * (s->size - 1);
            uint16_t * const d2 = (mirror ? d2_bottom_line : d2_data);

            for (y = 0; y < src_h; y++) {
                const int c0 = FFMIN(c0_data[x >> c0_shift_w], limit) + mid;
                const int c1 = FFMIN(c1_data[x >> c1_shift_w], limit) - mid;
                const int c2 = FFMIN(c2_data[x >> c2_shift_w], limit) - mid;
                uint16_t *target;

                target = d0 + x + d0_signed_linesize * c0;
                update16(target, max, intensity, limit);

                target = d1 + x + d1_signed_linesize * (c0 + c1);
                update16(target, max, intensity, limit);

                target = d2 + x + d2_signed_linesize * (c0 + c2);
                update16(target, max, intensity, limit);

                if (!c0_shift_h || (y & c0_shift_h))
                    c0_data += c0_linesize;
                if (!c1_shift_h || (y & c1_shift_h))
                    c1_data += c1_linesize;
                if (!c2_shift_h || (y & c2_shift_h))
                    c2_data += c2_linesize;
                d0_data += d0_linesize;
                d1_data += d1_linesize;
                d2_data += d2_linesize;
            }
        }
    } else {
        const uint16_t *c0_data = (uint16_t *)in->data[plane];
        const uint16_t *c1_data = (uint16_t *)in->data[(plane + 1) % s->ncomp];
        const uint16_t *c2_data = (uint16_t *)in->data[(plane + 2) % s->ncomp];
        uint16_t *d0_data = (uint16_t *)out->data[plane] + offset_y * d0_linesize + offset_x;
        uint16_t *d1_data = (uint16_t *)out->data[(plane + 1) % s->ncomp] + offset_y * d1_linesize + offset_x;
        uint16_t *d2_data = (uint16_t *)out->data[(plane + 2) % s->ncomp] + offset_y * d2_linesize + offset_x;

        if (mirror) {
            d0_data += s->size - 1;
            d1_data += s->size - 1;
            d2_data += s->size - 1;
        }

        for (y = 0; y < src_h; y++) {
            for (x = 0; x < src_w; x++) {
                const int c0 = FFMIN(c0_data[x >> c0_shift_w], limit) + mid;
                const int c1 = FFMIN(c1_data[x >> c1_shift_w], limit) - mid;
                const int c2 = FFMIN(c2_data[x >> c2_shift_w], limit) - mid;
                uint16_t *target;

                if (mirror) {
                    target = d0_data - c0;
                    update16(target, max, intensity, limit);
                    target = d1_data - (c0 + c1);
                    update16(target, max, intensity, limit);
                    target = d2_data - (c0 + c2);
                    update16(target, max, intensity, limit);
                } else {
                    target = d0_data + c0;
                    update16(target, max, intensity, limit);
                    target = d1_data + (c0 + c1);
                    update16(target, max, intensity, limit);
                    target = d2_data + (c0 + c2);
                    update16(target, max, intensity, limit);
                }
            }

            if (!c0_shift_h || (y & c0_shift_h))
                c0_data += c0_linesize;
            if (!c1_shift_h || (y & c1_shift_h))
                c1_data += c1_linesize;
            if (!c2_shift_h || (y & c2_shift_h))
                c2_data += c2_linesize;
            d0_data += d0_linesize;
            d1_data += d1_linesize;
            d2_data += d2_linesize;
        }
    }

    envelope16(s, out, plane, (plane + 0) % s->ncomp, column ? offset_x : offset_y);
    envelope16(s, out, plane, (plane + 1) % s->ncomp, column ? offset_x : offset_y);
    envelope16(s, out, plane, (plane + 2) % s->ncomp, column ? offset_x : offset_y);
}

static av_always_inline void aflat(WaveformContext *s,
                                   AVFrame *in, AVFrame *out,
                                   int component, int intensity,
                                   int offset_y, int offset_x,
                                   int column, int mirror)
{
    const int plane = s->desc->comp[component].plane;
    const int c0_linesize = in->linesize[ plane + 0 ];
    const int c1_linesize = in->linesize[(plane + 1) % s->ncomp];
    const int c2_linesize = in->linesize[(plane + 2) % s->ncomp];
    const int c0_shift_w = s->shift_w[ component + 0 ];
    const int c1_shift_w = s->shift_w[(component + 1) % s->ncomp];
    const int c2_shift_w = s->shift_w[(component + 2) % s->ncomp];
    const int c0_shift_h = s->shift_h[ component + 0 ];
    const int c1_shift_h = s->shift_h[(component + 1) % s->ncomp];
    const int c2_shift_h = s->shift_h[(component + 2) % s->ncomp];
    const int d0_linesize = out->linesize[ plane + 0 ];
    const int d1_linesize = out->linesize[(plane + 1) % s->ncomp];
    const int d2_linesize = out->linesize[(plane + 2) % s->ncomp];
    const int max = 255 - intensity;
    const int src_h = in->height;
    const int src_w = in->width;
    int x, y;

    if (column) {
        const int d0_signed_linesize = d0_linesize * (mirror == 1 ? -1 : 1);
        const int d1_signed_linesize = d1_linesize * (mirror == 1 ? -1 : 1);
        const int d2_signed_linesize = d2_linesize * (mirror == 1 ? -1 : 1);

        for (x = 0; x < src_w; x++) {
            const uint8_t *c0_data = in->data[plane + 0];
            const uint8_t *c1_data = in->data[(plane + 1) % s->ncomp];
            const uint8_t *c2_data = in->data[(plane + 2) % s->ncomp];
            uint8_t *d0_data = out->data[plane] + offset_y * d0_linesize + offset_x;
            uint8_t *d1_data = out->data[(plane + 1) % s->ncomp] + offset_y * d1_linesize + offset_x;
            uint8_t *d2_data = out->data[(plane + 2) % s->ncomp] + offset_y * d2_linesize + offset_x;
            uint8_t * const d0_bottom_line = d0_data + d0_linesize * (s->size - 1);
            uint8_t * const d0 = (mirror ? d0_bottom_line : d0_data);
            uint8_t * const d1_bottom_line = d1_data + d1_linesize * (s->size - 1);
            uint8_t * const d1 = (mirror ? d1_bottom_line : d1_data);
            uint8_t * const d2_bottom_line = d2_data + d2_linesize * (s->size - 1);
            uint8_t * const d2 = (mirror ? d2_bottom_line : d2_data);

            for (y = 0; y < src_h; y++) {
                const int c0 = c0_data[x >> c0_shift_w] + 128;
                const int c1 = c1_data[x >> c1_shift_w] - 128;
                const int c2 = c2_data[x >> c2_shift_w] - 128;
                uint8_t *target;

                target = d0 + x + d0_signed_linesize * c0;
                update(target, max, intensity);

                target = d1 + x + d1_signed_linesize * (c0 + c1);
                update(target, max, intensity);

                target = d2 + x + d2_signed_linesize * (c0 + c2);
                update(target, max, intensity);

                if (!c0_shift_h || (y & c0_shift_h))
                    c0_data += c0_linesize;
                if (!c1_shift_h || (y & c1_shift_h))
                    c1_data += c1_linesize;
                if (!c1_shift_h || (y & c1_shift_h))
                    c2_data += c1_linesize;
                d0_data += d0_linesize;
                d1_data += d1_linesize;
                d2_data += d2_linesize;
            }
        }
    } else {
        const uint8_t *c0_data = in->data[plane];
        const uint8_t *c1_data = in->data[(plane + 1) % s->ncomp];
        const uint8_t *c2_data = in->data[(plane + 2) % s->ncomp];
        uint8_t *d0_data = out->data[plane] + offset_y * d0_linesize + offset_x;
        uint8_t *d1_data = out->data[(plane + 1) % s->ncomp] + offset_y * d1_linesize + offset_x;
        uint8_t *d2_data = out->data[(plane + 2) % s->ncomp] + offset_y * d2_linesize + offset_x;

        if (mirror) {
            d0_data += s->size - 1;
            d1_data += s->size - 1;
            d2_data += s->size - 1;
        }

        for (y = 0; y < src_h; y++) {
            for (x = 0; x < src_w; x++) {
                const int c0 = c0_data[x >> c0_shift_w] + 128;
                const int c1 = c1_data[x >> c1_shift_w] - 128;
                const int c2 = c2_data[x >> c2_shift_w] - 128;
                uint8_t *target;

                if (mirror) {
                    target = d0_data - c0;
                    update(target, max, intensity);
                    target = d1_data - (c0 + c1);
                    update(target, max, intensity);
                    target = d2_data - (c0 + c2);
                    update(target, max, intensity);
                } else {
                    target = d0_data + c0;
                    update(target, max, intensity);
                    target = d1_data + (c0 + c1);
                    update(target, max, intensity);
                    target = d2_data + (c0 + c2);
                    update(target, max, intensity);
                }
            }

            if (!c0_shift_h || (y & c0_shift_h))
                c0_data += c0_linesize;
            if (!c1_shift_h || (y & c1_shift_h))
                c1_data += c1_linesize;
            if (!c2_shift_h || (y & c2_shift_h))
                c2_data += c2_linesize;
            d0_data += d0_linesize;
            d1_data += d1_linesize;
            d2_data += d2_linesize;
        }
    }

    envelope(s, out, plane, (plane + 0) % s->ncomp, column ? offset_x : offset_y);
    envelope(s, out, plane, (plane + 1) % s->ncomp, column ? offset_x : offset_y);
    envelope(s, out, plane, (plane + 2) % s->ncomp, column ? offset_x : offset_y);
}

static av_always_inline void chroma16(WaveformContext *s,
                                      AVFrame *in, AVFrame *out,
                                      int component, int intensity,
                                      int offset_y, int offset_x,
                                      int column, int mirror)
{
    const int plane = s->desc->comp[component].plane;
    const int c0_linesize = in->linesize[(plane + 1) % s->ncomp] / 2;
    const int c1_linesize = in->linesize[(plane + 2) % s->ncomp] / 2;
    const int dst_linesize = out->linesize[plane] / 2;
    const int limit = s->max - 1;
    const int max = limit - intensity;
    const int mid = s->max / 2;
    const int c0_shift_w = s->shift_w[(component + 1) % s->ncomp];
    const int c1_shift_w = s->shift_w[(component + 2) % s->ncomp];
    const int c0_shift_h = s->shift_h[(component + 1) % s->ncomp];
    const int c1_shift_h = s->shift_h[(component + 2) % s->ncomp];
    const int src_h = in->height;
    const int src_w = in->width;
    int x, y;

    if (column) {
        const int dst_signed_linesize = dst_linesize * (mirror == 1 ? -1 : 1);

        for (x = 0; x < src_w; x++) {
            const uint16_t *c0_data = (uint16_t *)in->data[(plane + 1) % s->ncomp];
            const uint16_t *c1_data = (uint16_t *)in->data[(plane + 2) % s->ncomp];
            uint16_t *dst_data = (uint16_t *)out->data[plane] + offset_y * dst_linesize + offset_x;
            uint16_t * const dst_bottom_line = dst_data + dst_linesize * (s->size - 1);
            uint16_t * const dst_line = (mirror ? dst_bottom_line : dst_data);
            uint16_t *dst = dst_line;

            for (y = 0; y < src_h; y++) {
                const int sum = FFMIN(FFABS(c0_data[x >> c0_shift_w] - mid) + FFABS(c1_data[x >> c1_shift_w] - mid - 1), limit);
                uint16_t *target;

                target = dst + x + dst_signed_linesize * sum;
                update16(target, max, intensity, limit);

                if (!c0_shift_h || (y & c0_shift_h))
                    c0_data += c0_linesize;
                if (!c1_shift_h || (y & c1_shift_h))
                    c1_data += c1_linesize;
                dst_data += dst_linesize;
            }
        }
    } else {
        const uint16_t *c0_data = (uint16_t *)in->data[(plane + 1) % s->ncomp];
        const uint16_t *c1_data = (uint16_t *)in->data[(plane + 2) % s->ncomp];
        uint16_t *dst_data = (uint16_t *)out->data[plane] + offset_y * dst_linesize + offset_x;

        if (mirror)
            dst_data += s->size - 1;
        for (y = 0; y < src_h; y++) {
            for (x = 0; x < src_w; x++) {
                const int sum = FFMIN(FFABS(c0_data[x >> c0_shift_w] - mid) + FFABS(c1_data[x >> c1_shift_w] - mid - 1), limit);
                uint16_t *target;

                if (mirror) {
                    target = dst_data - sum;
                    update16(target, max, intensity, limit);
                } else {
                    target = dst_data + sum;
                    update16(target, max, intensity, limit);
                }
            }

            if (!c0_shift_h || (y & c0_shift_h))
                c0_data += c0_linesize;
            if (!c1_shift_h || (y & c1_shift_h))
                c1_data += c1_linesize;
            dst_data += dst_linesize;
        }
    }

    envelope16(s, out, plane, plane, column ? offset_x : offset_y);
}

static av_always_inline void chroma(WaveformContext *s,
                                    AVFrame *in, AVFrame *out,
                                    int component, int intensity,
                                    int offset_y, int offset_x,
                                    int column, int mirror)
{
    const int plane = s->desc->comp[component].plane;
    const int c0_linesize = in->linesize[(plane + 1) % s->ncomp];
    const int c1_linesize = in->linesize[(plane + 2) % s->ncomp];
    const int dst_linesize = out->linesize[plane];
    const int max = 255 - intensity;
    const int c0_shift_w = s->shift_w[(component + 1) % s->ncomp];
    const int c1_shift_w = s->shift_w[(component + 2) % s->ncomp];
    const int c0_shift_h = s->shift_h[(component + 1) % s->ncomp];
    const int c1_shift_h = s->shift_h[(component + 2) % s->ncomp];
    const int src_h = in->height;
    const int src_w = in->width;
    int x, y;

    if (column) {
        const int dst_signed_linesize = dst_linesize * (mirror == 1 ? -1 : 1);

        for (x = 0; x < src_w; x++) {
            const uint8_t *c0_data = in->data[(plane + 1) % s->ncomp];
            const uint8_t *c1_data = in->data[(plane + 2) % s->ncomp];
            uint8_t *dst_data = out->data[plane] + offset_y * dst_linesize + offset_x;
            uint8_t * const dst_bottom_line = dst_data + dst_linesize * (s->size - 1);
            uint8_t * const dst_line = (mirror ? dst_bottom_line : dst_data);
            uint8_t *dst = dst_line;

            for (y = 0; y < src_h; y++) {
                const int sum = FFABS(c0_data[x >> c0_shift_w] - 128) + FFABS(c1_data[x >> c1_shift_w] - 127);
                uint8_t *target;

                target = dst + x + dst_signed_linesize * sum;
                update(target, max, intensity);

                if (!c0_shift_h || (y & c0_shift_h))
                    c0_data += c0_linesize;
                if (!c1_shift_h || (y & c1_shift_h))
                    c1_data += c1_linesize;
                dst_data += dst_linesize;
            }
        }
    } else {
        const uint8_t *c0_data = in->data[(plane + 1) % s->ncomp];
        const uint8_t *c1_data = in->data[(plane + 2) % s->ncomp];
        uint8_t *dst_data = out->data[plane] + offset_y * dst_linesize + offset_x;

        if (mirror)
            dst_data += s->size - 1;
        for (y = 0; y < src_h; y++) {
            for (x = 0; x < src_w; x++) {
                const int sum = FFABS(c0_data[x >> c0_shift_w] - 128) + FFABS(c1_data[x >> c1_shift_w] - 127);
                uint8_t *target;

                if (mirror) {
                    target = dst_data - sum;
                    update(target, max, intensity);
                } else {
                    target = dst_data + sum;
                    update(target, max, intensity);
                }
            }

            if (!c0_shift_h || (y & c0_shift_h))
                c0_data += c0_linesize;
            if (!c1_shift_h || (y & c1_shift_h))
                c1_data += c1_linesize;
            dst_data += dst_linesize;
        }
    }

    envelope(s, out, plane, plane, column ? offset_x : offset_y);
}

static av_always_inline void color16(WaveformContext *s,
                                     AVFrame *in, AVFrame *out,
                                     int component, int intensity,
                                     int offset_y, int offset_x,
                                     int column, int mirror)
{
    const int plane = s->desc->comp[component].plane;
    const int limit = s->max - 1;
    const uint16_t *c0_data = (const uint16_t *)in->data[plane + 0];
    const uint16_t *c1_data = (const uint16_t *)in->data[(plane + 1) % s->ncomp];
    const uint16_t *c2_data = (const uint16_t *)in->data[(plane + 2) % s->ncomp];
    const int c0_linesize = in->linesize[ plane + 0 ] / 2;
    const int c1_linesize = in->linesize[(plane + 1) % s->ncomp] / 2;
    const int c2_linesize = in->linesize[(plane + 2) % s->ncomp] / 2;
    const int d0_linesize = out->linesize[ plane + 0 ] / 2;
    const int d1_linesize = out->linesize[(plane + 1) % s->ncomp] / 2;
    const int d2_linesize = out->linesize[(plane + 2) % s->ncomp] / 2;
    const int c0_shift_w = s->shift_w[ component + 0 ];
    const int c1_shift_w = s->shift_w[(component + 1) % s->ncomp];
    const int c2_shift_w = s->shift_w[(component + 2) % s->ncomp];
    const int c0_shift_h = s->shift_h[ component + 0 ];
    const int c1_shift_h = s->shift_h[(component + 1) % s->ncomp];
    const int c2_shift_h = s->shift_h[(component + 2) % s->ncomp];
    const int src_h = in->height;
    const int src_w = in->width;
    int x, y;

    if (column) {
        const int d0_signed_linesize = d0_linesize * (mirror == 1 ? -1 : 1);
        const int d1_signed_linesize = d1_linesize * (mirror == 1 ? -1 : 1);
        const int d2_signed_linesize = d2_linesize * (mirror == 1 ? -1 : 1);
        uint16_t *d0_data = (uint16_t *)out->data[plane] + offset_y * d0_linesize + offset_x;
        uint16_t *d1_data = (uint16_t *)out->data[(plane + 1) % s->ncomp] + offset_y * d1_linesize + offset_x;
        uint16_t *d2_data = (uint16_t *)out->data[(plane + 2) % s->ncomp] + offset_y * d2_linesize + offset_x;
        uint16_t * const d0_bottom_line = d0_data + d0_linesize * (s->size - 1);
        uint16_t * const d0 = (mirror ? d0_bottom_line : d0_data);
        uint16_t * const d1_bottom_line = d1_data + d1_linesize * (s->size - 1);
        uint16_t * const d1 = (mirror ? d1_bottom_line : d1_data);
        uint16_t * const d2_bottom_line = d2_data + d2_linesize * (s->size - 1);
        uint16_t * const d2 = (mirror ? d2_bottom_line : d2_data);

        for (y = 0; y < src_h; y++) {
            for (x = 0; x < src_w; x++) {
                const int c0 = FFMIN(c0_data[x >> c0_shift_w], limit);
                const int c1 = c1_data[x >> c1_shift_w];
                const int c2 = c2_data[x >> c2_shift_w];

                *(d0 + d0_signed_linesize * c0 + x) = c0;
                *(d1 + d1_signed_linesize * c0 + x) = c1;
                *(d2 + d2_signed_linesize * c0 + x) = c2;
            }

            if (!c0_shift_h || (y & c0_shift_h))
                c0_data += c0_linesize;
            if (!c1_shift_h || (y & c1_shift_h))
                c1_data += c1_linesize;
            if (!c2_shift_h || (y & c2_shift_h))
                c2_data += c2_linesize;
            d0_data += d0_linesize;
            d1_data += d1_linesize;
            d2_data += d2_linesize;
        }
    } else {
        uint16_t *d0_data = (uint16_t *)out->data[plane] + offset_y * d0_linesize + offset_x;
        uint16_t *d1_data = (uint16_t *)out->data[(plane + 1) % s->ncomp] + offset_y * d1_linesize + offset_x;
        uint16_t *d2_data = (uint16_t *)out->data[(plane + 2) % s->ncomp] + offset_y * d2_linesize + offset_x;

        if (mirror) {
            d0_data += s->size - 1;
            d1_data += s->size - 1;
            d2_data += s->size - 1;
        }

        for (y = 0; y < src_h; y++) {
            for (x = 0; x < src_w; x++) {
                const int c0 = FFMIN(c0_data[x >> c0_shift_w], limit);
                const int c1 = c1_data[x >> c1_shift_w];
                const int c2 = c2_data[x >> c2_shift_w];

                if (mirror) {
                    *(d0_data - c0) = c0;
                    *(d1_data - c0) = c1;
                    *(d2_data - c0) = c2;
                } else {
                    *(d0_data + c0) = c0;
                    *(d1_data + c0) = c1;
                    *(d2_data + c0) = c2;
                }
            }

            if (!c0_shift_h || (y & c0_shift_h))
                c0_data += c0_linesize;
            if (!c1_shift_h || (y & c1_shift_h))
                c1_data += c1_linesize;
            if (!c2_shift_h || (y & c2_shift_h))
                c2_data += c2_linesize;
            d0_data += d0_linesize;
            d1_data += d1_linesize;
            d2_data += d2_linesize;
        }
    }

    envelope16(s, out, plane, plane, column ? offset_x : offset_y);
}

static av_always_inline void color(WaveformContext *s,
                                   AVFrame *in, AVFrame *out,
                                   int component, int intensity,
                                   int offset_y, int offset_x,
                                   int column, int mirror)
{
    const int plane = s->desc->comp[component].plane;
    const uint8_t *c0_data = in->data[plane + 0];
    const uint8_t *c1_data = in->data[(plane + 1) % s->ncomp];
    const uint8_t *c2_data = in->data[(plane + 2) % s->ncomp];
    const int c0_linesize = in->linesize[ plane + 0 ];
    const int c1_linesize = in->linesize[(plane + 1) % s->ncomp];
    const int c2_linesize = in->linesize[(plane + 2) % s->ncomp];
    const int d0_linesize = out->linesize[ plane + 0 ];
    const int d1_linesize = out->linesize[(plane + 1) % s->ncomp];
    const int d2_linesize = out->linesize[(plane + 2) % s->ncomp];
    const int c0_shift_w = s->shift_w[ component + 0 ];
    const int c1_shift_w = s->shift_w[(component + 1) % s->ncomp];
    const int c2_shift_w = s->shift_w[(component + 2) % s->ncomp];
    const int c0_shift_h = s->shift_h[ component + 0 ];
    const int c1_shift_h = s->shift_h[(component + 1) % s->ncomp];
    const int c2_shift_h = s->shift_h[(component + 2) % s->ncomp];
    const int src_h = in->height;
    const int src_w = in->width;
    int x, y;

    if (s->mode) {
        const int d0_signed_linesize = d0_linesize * (mirror == 1 ? -1 : 1);
        const int d1_signed_linesize = d1_linesize * (mirror == 1 ? -1 : 1);
        const int d2_signed_linesize = d2_linesize * (mirror == 1 ? -1 : 1);
        uint8_t *d0_data = out->data[plane] + offset_y * d0_linesize + offset_x;
        uint8_t *d1_data = out->data[(plane + 1) % s->ncomp] + offset_y * d1_linesize + offset_x;
        uint8_t *d2_data = out->data[(plane + 2) % s->ncomp] + offset_y * d2_linesize + offset_x;
        uint8_t * const d0_bottom_line = d0_data + d0_linesize * (s->size - 1);
        uint8_t * const d0 = (mirror ? d0_bottom_line : d0_data);
        uint8_t * const d1_bottom_line = d1_data + d1_linesize * (s->size - 1);
        uint8_t * const d1 = (mirror ? d1_bottom_line : d1_data);
        uint8_t * const d2_bottom_line = d2_data + d2_linesize * (s->size - 1);
        uint8_t * const d2 = (mirror ? d2_bottom_line : d2_data);

        for (y = 0; y < src_h; y++) {
            for (x = 0; x < src_w; x++) {
                const int c0 = c0_data[x >> c0_shift_w];
                const int c1 = c1_data[x >> c1_shift_w];
                const int c2 = c2_data[x >> c2_shift_w];

                *(d0 + d0_signed_linesize * c0 + x) = c0;
                *(d1 + d1_signed_linesize * c0 + x) = c1;
                *(d2 + d2_signed_linesize * c0 + x) = c2;
            }

            if (!c0_shift_h || (y & c0_shift_h))
                c0_data += c0_linesize;
            if (!c1_shift_h || (y & c1_shift_h))
                c1_data += c1_linesize;
            if (!c2_shift_h || (y & c2_shift_h))
                c2_data += c2_linesize;
            d0_data += d0_linesize;
            d1_data += d1_linesize;
            d2_data += d2_linesize;
        }
    } else {
        uint8_t *d0_data = out->data[plane] + offset_y * d0_linesize + offset_x;
        uint8_t *d1_data = out->data[(plane + 1) % s->ncomp] + offset_y * d1_linesize + offset_x;
        uint8_t *d2_data = out->data[(plane + 2) % s->ncomp] + offset_y * d2_linesize + offset_x;

        if (mirror) {
            d0_data += s->size - 1;
            d1_data += s->size - 1;
            d2_data += s->size - 1;
        }

        for (y = 0; y < src_h; y++) {
            for (x = 0; x < src_w; x++) {
                const int c0 = c0_data[x >> c0_shift_w];
                const int c1 = c1_data[x >> c1_shift_w];
                const int c2 = c2_data[x >> c2_shift_w];

                if (mirror) {
                    *(d0_data - c0) = c0;
                    *(d1_data - c0) = c1;
                    *(d2_data - c0) = c2;
                } else {
                    *(d0_data + c0) = c0;
                    *(d1_data + c0) = c1;
                    *(d2_data + c0) = c2;
                }
            }

            if (!c0_shift_h || (y & c0_shift_h))
                c0_data += c0_linesize;
            if (!c1_shift_h || (y & c1_shift_h))
                c1_data += c1_linesize;
            if (!c2_shift_h || (y & c2_shift_h))
                c2_data += c2_linesize;
            d0_data += d0_linesize;
            d1_data += d1_linesize;
            d2_data += d2_linesize;
        }
    }

    envelope(s, out, plane, plane, column ? offset_x : offset_y);
}

static av_always_inline void acolor16(WaveformContext *s,
                                      AVFrame *in, AVFrame *out,
                                      int component, int intensity,
                                      int offset_y, int offset_x,
                                      int column, int mirror)
{
    const int plane = s->desc->comp[component].plane;
    const int limit = s->max - 1;
    const int max = limit - intensity;
    const uint16_t *c0_data = (const uint16_t *)in->data[plane + 0];
    const uint16_t *c1_data = (const uint16_t *)in->data[(plane + 1) % s->ncomp];
    const uint16_t *c2_data = (const uint16_t *)in->data[(plane + 2) % s->ncomp];
    const int c0_linesize = in->linesize[ plane + 0 ] / 2;
    const int c1_linesize = in->linesize[(plane + 1) % s->ncomp] / 2;
    const int c2_linesize = in->linesize[(plane + 2) % s->ncomp] / 2;
    const int d0_linesize = out->linesize[ plane + 0 ] / 2;
    const int d1_linesize = out->linesize[(plane + 1) % s->ncomp] / 2;
    const int d2_linesize = out->linesize[(plane + 2) % s->ncomp] / 2;
    const int c0_shift_w = s->shift_w[ component + 0 ];
    const int c1_shift_w = s->shift_w[(component + 1) % s->ncomp];
    const int c2_shift_w = s->shift_w[(component + 2) % s->ncomp];
    const int c0_shift_h = s->shift_h[ component + 0 ];
    const int c1_shift_h = s->shift_h[(component + 1) % s->ncomp];
    const int c2_shift_h = s->shift_h[(component + 2) % s->ncomp];
    const int src_h = in->height;
    const int src_w = in->width;
    int x, y;

    if (s->mode) {
        const int d0_signed_linesize = d0_linesize * (mirror == 1 ? -1 : 1);
        const int d1_signed_linesize = d1_linesize * (mirror == 1 ? -1 : 1);
        const int d2_signed_linesize = d2_linesize * (mirror == 1 ? -1 : 1);
        uint16_t *d0_data = (uint16_t *)out->data[plane] + offset_y * d0_linesize + offset_x;
        uint16_t *d1_data = (uint16_t *)out->data[(plane + 1) % s->ncomp] + offset_y * d1_linesize + offset_x;
        uint16_t *d2_data = (uint16_t *)out->data[(plane + 2) % s->ncomp] + offset_y * d2_linesize + offset_x;
        uint16_t * const d0_bottom_line = d0_data + d0_linesize * (s->size - 1);
        uint16_t * const d0 = (mirror ? d0_bottom_line : d0_data);
        uint16_t * const d1_bottom_line = d1_data + d1_linesize * (s->size - 1);
        uint16_t * const d1 = (mirror ? d1_bottom_line : d1_data);
        uint16_t * const d2_bottom_line = d2_data + d2_linesize * (s->size - 1);
        uint16_t * const d2 = (mirror ? d2_bottom_line : d2_data);

        for (y = 0; y < src_h; y++) {
            for (x = 0; x < src_w; x++) {
                const int c0 = FFMIN(c0_data[x >> c0_shift_w], limit);
                const int c1 = c1_data[x >> c1_shift_w];
                const int c2 = c2_data[x >> c2_shift_w];

                update16(d0 + d0_signed_linesize * c0 + x, max, intensity, limit);
                *(d1 + d1_signed_linesize * c0 + x) = c1;
                *(d2 + d2_signed_linesize * c0 + x) = c2;
            }

            if (!c0_shift_h || (y & c0_shift_h))
                c0_data += c0_linesize;
            if (!c1_shift_h || (y & c1_shift_h))
                c1_data += c1_linesize;
            if (!c2_shift_h || (y & c2_shift_h))
                c2_data += c2_linesize;
            d0_data += d0_linesize;
            d1_data += d1_linesize;
            d2_data += d2_linesize;
        }
    } else {
        uint16_t *d0_data = (uint16_t *)out->data[plane] + offset_y * d0_linesize + offset_x;
        uint16_t *d1_data = (uint16_t *)out->data[(plane + 1) % s->ncomp] + offset_y * d1_linesize + offset_x;
        uint16_t *d2_data = (uint16_t *)out->data[(plane + 2) % s->ncomp] + offset_y * d2_linesize + offset_x;

        if (mirror) {
            d0_data += s->size - 1;
            d1_data += s->size - 1;
            d2_data += s->size - 1;
        }

        for (y = 0; y < src_h; y++) {
            for (x = 0; x < src_w; x++) {
                const int c0 = FFMIN(c0_data[x >> c0_shift_w], limit);
                const int c1 = c1_data[x >> c1_shift_w];
                const int c2 = c2_data[x >> c2_shift_w];

                if (mirror) {
                    update16(d0_data - c0, max, intensity, limit);
                    *(d1_data - c0) = c1;
                    *(d2_data - c0) = c2;
                } else {
                    update16(d0_data + c0, max, intensity, limit);
                    *(d1_data + c0) = c1;
                    *(d2_data + c0) = c2;
                }
            }

            if (!c0_shift_h || (y & c0_shift_h))
                c0_data += c0_linesize;
            if (!c1_shift_h || (y & c1_shift_h))
                c1_data += c1_linesize;
            if (!c2_shift_h || (y & c2_shift_h))
                c2_data += c2_linesize;
            d0_data += d0_linesize;
            d1_data += d1_linesize;
            d2_data += d2_linesize;
        }
    }

    envelope16(s, out, plane, plane, column ? offset_x : offset_y);
}

static av_always_inline void acolor(WaveformContext *s,
                                    AVFrame *in, AVFrame *out,
                                    int component, int intensity,
                                    int offset_y, int offset_x,
                                    int column, int mirror)
{
    const int plane = s->desc->comp[component].plane;
    const uint8_t *c0_data = in->data[plane + 0];
    const uint8_t *c1_data = in->data[(plane + 1) % s->ncomp];
    const uint8_t *c2_data = in->data[(plane + 2) % s->ncomp];
    const int c0_linesize = in->linesize[ plane + 0 ];
    const int c1_linesize = in->linesize[(plane + 1) % s->ncomp];
    const int c2_linesize = in->linesize[(plane + 2) % s->ncomp];
    const int d0_linesize = out->linesize[ plane + 0 ];
    const int d1_linesize = out->linesize[(plane + 1) % s->ncomp];
    const int d2_linesize = out->linesize[(plane + 2) % s->ncomp];
    const int c0_shift_w = s->shift_w[ component + 0 ];
    const int c1_shift_w = s->shift_w[(component + 1) % s->ncomp];
    const int c2_shift_w = s->shift_w[(component + 2) % s->ncomp];
    const int c0_shift_h = s->shift_h[ component + 0 ];
    const int c1_shift_h = s->shift_h[(component + 1) % s->ncomp];
    const int c2_shift_h = s->shift_h[(component + 2) % s->ncomp];
    const int max = 255 - intensity;
    const int src_h = in->height;
    const int src_w = in->width;
    int x, y;

    if (s->mode) {
        const int d0_signed_linesize = d0_linesize * (mirror == 1 ? -1 : 1);
        const int d1_signed_linesize = d1_linesize * (mirror == 1 ? -1 : 1);
        const int d2_signed_linesize = d2_linesize * (mirror == 1 ? -1 : 1);
        uint8_t *d0_data = out->data[plane] + offset_y * d0_linesize + offset_x;
        uint8_t *d1_data = out->data[(plane + 1) % s->ncomp] + offset_y * d1_linesize + offset_x;
        uint8_t *d2_data = out->data[(plane + 2) % s->ncomp] + offset_y * d2_linesize + offset_x;
        uint8_t * const d0_bottom_line = d0_data + d0_linesize * (s->size - 1);
        uint8_t * const d0 = (mirror ? d0_bottom_line : d0_data);
        uint8_t * const d1_bottom_line = d1_data + d1_linesize * (s->size - 1);
        uint8_t * const d1 = (mirror ? d1_bottom_line : d1_data);
        uint8_t * const d2_bottom_line = d2_data + d2_linesize * (s->size - 1);
        uint8_t * const d2 = (mirror ? d2_bottom_line : d2_data);

        for (y = 0; y < src_h; y++) {
            for (x = 0; x < src_w; x++) {
                const int c0 = c0_data[x >> c0_shift_w];
                const int c1 = c1_data[x >> c1_shift_w];
                const int c2 = c2_data[x >> c2_shift_w];

                update(d0 + d0_signed_linesize * c0 + x, max, intensity);
                *(d1 + d1_signed_linesize * c0 + x) = c1;
                *(d2 + d2_signed_linesize * c0 + x) = c2;
            }

            if (!c0_shift_h || (y & c0_shift_h))
                c0_data += c0_linesize;
            if (!c1_shift_h || (y & c1_shift_h))
                c1_data += c1_linesize;
            if (!c2_shift_h || (y & c2_shift_h))
                c2_data += c2_linesize;
            d0_data += d0_linesize;
            d1_data += d1_linesize;
            d2_data += d2_linesize;
        }
    } else {
        uint8_t *d0_data = out->data[plane] + offset_y * d0_linesize + offset_x;
        uint8_t *d1_data = out->data[(plane + 1) % s->ncomp] + offset_y * d1_linesize + offset_x;
        uint8_t *d2_data = out->data[(plane + 2) % s->ncomp] + offset_y * d2_linesize + offset_x;

        if (mirror) {
            d0_data += s->size - 1;
            d1_data += s->size - 1;
            d2_data += s->size - 1;
        }

        for (y = 0; y < src_h; y++) {
            for (x = 0; x < src_w; x++) {
                const int c0 = c0_data[x >> c0_shift_w];
                const int c1 = c1_data[x >> c1_shift_w];
                const int c2 = c2_data[x >> c2_shift_w];

                if (mirror) {
                    update(d0_data - c0, max, intensity);
                    *(d1_data - c0) = c1;
                    *(d2_data - c0) = c2;
                } else {
                    update(d0_data + c0, max, intensity);
                    *(d1_data + c0) = c1;
                    *(d2_data + c0) = c2;
                }
            }

            if (!c0_shift_h || (y & c0_shift_h))
                c0_data += c0_linesize;
            if (!c1_shift_h || (y & c1_shift_h))
                c1_data += c1_linesize;
            if (!c2_shift_h || (y & c2_shift_h))
                c2_data += c2_linesize;
            d0_data += d0_linesize;
            d1_data += d1_linesize;
            d2_data += d2_linesize;
        }
    }

    envelope(s, out, plane, plane, column ? offset_x : offset_y);
}

static const uint8_t black_yuva_color[4] = { 0, 127, 127, 255 };
static const uint8_t green_yuva_color[4] = { 255, 0, 0, 255 };
static const uint8_t black_gbrp_color[4] = { 0, 0, 0, 255 };

static const GraticuleLines aflat_digital8[] = {
    { { {  "16",  16+128 }, {  "16",  16+128 }, {  "16",  16+128 }, {   "0",   0+128 } } },
    { { { "128", 128+128 }, { "128", 128+128 }, { "128", 128+128 }, { "128", 128+128 } } },
    { { { "235", 235+128 }, { "240", 240+128 }, { "240", 240+128 }, { "255", 255+128 } } },
};

static const GraticuleLines aflat_digital9[] = {
    { { {  "32",  32+256 }, {  "32",  32+256 }, {  "32",  32+256 }, {   "0",   0+256 } } },
    { { { "256", 256+256 }, { "256", 256+256 }, { "256", 256+256 }, { "256", 256+256 } } },
    { { { "470", 470+256 }, { "480", 480+256 }, { "480", 480+256 }, { "511", 511+256 } } },
};

static const GraticuleLines aflat_digital10[] = {
    { { {  "64",  64+512 }, {  "64",  64+512 }, {  "64",  64+512 }, {    "0",    0+512 } } },
    { { { "512", 512+512 }, { "512", 512+512 }, { "512", 512+512 }, {  "512",  512+512 } } },
    { { { "940", 940+512 }, { "960", 960+512 }, { "960", 960+512 }, { "1023", 1023+512 } } },
};

static const GraticuleLines aflat_digital12[] = {
    { { {  "256",  256+2048 }, {  "256",  256+2048 }, {  "256",  256+2048 }, {    "0",    0+2048 } } },
    { { { "2048", 2048+2048 }, { "2048", 2048+2048 }, { "2048", 2048+2048 }, { "2048", 2048+2048 } } },
    { { { "3760", 3760+2048 }, { "3840", 3840+2048 }, { "3840", 3840+2048 }, { "4095", 4095+2048 } } },
};

static const GraticuleLines aflat_millivolts8[] = {
    { { {   "0",  16+128 }, {   "0",  16+128 }, {   "0",  16+128 }, {   "0",   0+128 } } },
    { { { "175",  71+128 }, { "175",  72+128 }, { "175",  72+128 }, { "175",  64+128 } } },
    { { { "350", 126+128 }, { "350", 128+128 }, { "350", 128+128 }, { "350", 128+128 } } },
    { { { "525", 180+128 }, { "525", 184+128 }, { "525", 184+128 }, { "525", 192+128 } } },
    { { { "700", 235+128 }, { "700", 240+128 }, { "700", 240+128 }, { "700", 255+128 } } },
};

static const GraticuleLines aflat_millivolts9[] = {
    { { {   "0",  32+256 }, {   "0",  32+256 }, {   "0",  32+256 }, {   "0",   0+256 } } },
    { { { "175", 142+256 }, { "175", 144+256 }, { "175", 144+256 }, { "175", 128+256 } } },
    { { { "350", 251+256 }, { "350", 256+256 }, { "350", 256+256 }, { "350", 256+256 } } },
    { { { "525", 361+256 }, { "525", 368+256 }, { "525", 368+256 }, { "525", 384+256 } } },
    { { { "700", 470+256 }, { "700", 480+256 }, { "700", 480+256 }, { "700", 511+256 } } },
};

static const GraticuleLines aflat_millivolts10[] = {
    { { {   "0",  64+512 }, {   "0",  64+512 }, {   "0",  64+512 }, {   "0",    0+512 } } },
    { { { "175", 283+512 }, { "175", 288+512 }, { "175", 288+512 }, { "175",  256+512 } } },
    { { { "350", 502+512 }, { "350", 512+512 }, { "350", 512+512 }, { "350",  512+512 } } },
    { { { "525", 721+512 }, { "525", 736+512 }, { "525", 736+512 }, { "525",  768+512 } } },
    { { { "700", 940+512 }, { "700", 960+512 }, { "700", 960+512 }, { "700", 1023+512 } } },
};

static const GraticuleLines aflat_millivolts12[] = {
    { { {   "0",  256+2048 }, {   "0",  256+2048 }, {   "0",  256+2048 }, {   "0",    0+2048 } } },
    { { { "175", 1132+2048 }, { "175", 1152+2048 }, { "175", 1152+2048 }, { "175", 1024+2048 } } },
    { { { "350", 2008+2048 }, { "350", 2048+2048 }, { "350", 2048+2048 }, { "350", 2048+2048 } } },
    { { { "525", 2884+2048 }, { "525", 2944+2048 }, { "525", 2944+2048 }, { "525", 3072+2048 } } },
    { { { "700", 3760+2048 }, { "700", 3840+2048 }, { "700", 3840+2048 }, { "700", 4095+2048 } } },
};

static const GraticuleLines aflat_ire8[] = {
    { { { "-25", -39+128 }, { "-25", -40+128 }, { "-25", -40+128 }, { "-25", -64+128 } } },
    { { {   "0",  16+128 }, {   "0",  16+128 }, {   "0",  16+128 }, {   "0",   0+128 } } },
    { { {  "25",  71+128 }, {  "25",  72+128 }, {  "25",  72+128 }, {  "25",  64+128 } } },
    { { {  "50", 126+128 }, {  "50", 128+128 }, {  "50", 128+128 }, {  "50", 128+128 } } },
    { { {  "75", 180+128 }, {  "75", 184+128 }, {  "75", 184+128 }, {  "75", 192+128 } } },
    { { { "100", 235+128 }, { "100", 240+128 }, { "100", 240+128 }, { "100", 256+128 } } },
    { { { "125", 290+128 }, { "125", 296+128 }, { "125", 296+128 }, { "125", 320+128 } } },
};

static const GraticuleLines aflat_ire9[] = {
    { { { "-25", -78+256 }, { "-25", -80+256 }, { "-25", -80+256 }, { "-25",-128+256 } } },
    { { {   "0",  32+256 }, {   "0",  32+256 }, {   "0",  32+256 }, {   "0",   0+256 } } },
    { { {  "25", 142+256 }, {  "25", 144+256 }, {  "25", 144+256 }, {  "25", 128+256 } } },
    { { {  "50", 251+256 }, {  "50", 256+256 }, {  "50", 256+256 }, {  "50", 256+256 } } },
    { { {  "75", 361+256 }, {  "75", 368+256 }, {  "75", 368+256 }, {  "75", 384+256 } } },
    { { { "100", 470+256 }, { "100", 480+256 }, { "100", 480+256 }, { "100", 512+256 } } },
    { { { "125", 580+256 }, { "125", 592+256 }, { "125", 592+256 }, { "125", 640+256 } } },
};

static const GraticuleLines aflat_ire10[] = {
    { { { "-25",-156+512 }, { "-25",-160+512 }, { "-25",-160+512 }, { "-25", -256+512 } } },
    { { {   "0",  64+512 }, {   "0",  64+512 }, {  "0",   64+512 }, {   "0",    0+512 } } },
    { { {  "25", 283+512 }, {  "25", 288+512 }, {  "25", 288+512 }, {  "25",  256+512 } } },
    { { {  "50", 502+512 }, {  "50", 512+512 }, {  "50", 512+512 }, {  "50",  512+512 } } },
    { { {  "75", 721+512 }, {  "75", 736+512 }, {  "75", 736+512 }, {  "75",  768+512 } } },
    { { { "100", 940+512 }, { "100", 960+512 }, { "100", 960+512 }, { "100", 1024+512 } } },
    { { { "125",1160+512 }, { "125",1184+512 }, { "125",1184+512 }, { "125", 1280+512 } } },
};

static const GraticuleLines aflat_ire12[] = {
    { { { "-25", -624+2048 }, { "-25", -640+2048 }, { "-25", -640+2048 }, { "-25",-1024+2048 } } },
    { { {   "0",  256+2048 }, {   "0",  256+2048 }, {   "0",  256+2048 }, {   "0",    0+2048 } } },
    { { {  "25", 1132+2048 }, {  "25", 1152+2048 }, {  "25", 1152+2048 }, {  "25", 1024+2048 } } },
    { { {  "50", 2008+2048 }, {  "50", 2048+2048 }, {  "50", 2048+2048 }, {  "50", 2048+2048 } } },
    { { {  "75", 2884+2048 }, {  "75", 2944+2048 }, {  "75", 2944+2048 }, {  "75", 3072+2048 } } },
    { { { "100", 3760+2048 }, { "100", 3840+2048 }, { "100", 3840+2048 }, { "100", 4096+2048 } } },
    { { { "125", 4640+2048 }, { "125", 4736+2048 }, { "125", 4736+2048 }, { "125", 5120+2048 } } },
};

static const GraticuleLines flat_digital8[] = {
    { { {  "16",  16+256 }, {  "16",  16+256 }, {  "16",  16+256 }, {   "0",   0+256 } } },
    { { { "128", 128+256 }, { "128", 128+256 }, { "128", 128+256 }, { "128", 128+256 } } },
    { { { "235", 235+256 }, { "240", 240+256 }, { "240", 240+256 }, { "255", 255+256 } } },
};

static const GraticuleLines flat_digital9[] = {
    { { {  "32",  32+512 }, {  "32",  32+512 }, {  "32",  32+512 }, {   "0",   0+512 } } },
    { { { "256", 256+512 }, { "256", 256+512 }, { "256", 256+512 }, { "256", 256+512 } } },
    { { { "470", 470+512 }, { "480", 480+512 }, { "480", 480+512 }, { "511", 511+512 } } },
};

static const GraticuleLines flat_digital10[] = {
    { { {  "64",  64+1024 }, {  "64",  64+1024 }, {  "64",  64+1024 }, {    "0",    0+1024 } } },
    { { { "512", 512+1024 }, { "512", 512+1024 }, { "512", 512+1024 }, {  "512",  512+1024 } } },
    { { { "940", 940+1024 }, { "960", 960+1024 }, { "960", 960+1024 }, { "1023", 1023+1024 } } },
};

static const GraticuleLines flat_digital12[] = {
    { { {  "256",  256+4096 }, {  "256",  256+4096 }, {  "256",  256+4096 }, {    "0",    0+4096 } } },
    { { { "2048", 2048+4096 }, { "2048", 2048+4096 }, { "2048", 2048+4096 }, { "2048", 2048+4096 } } },
    { { { "3760", 3760+4096 }, { "3840", 3840+4096 }, { "3840", 3840+4096 }, { "4095", 4095+4096 } } },
};

static const GraticuleLines flat_millivolts8[] = {
    { { {   "0",  16+256 }, {   "0",  16+256 }, {   "0",  16+256 }, {   "0",   0+256 } } },
    { { { "175",  71+256 }, { "175",  72+256 }, { "175",  72+256 }, { "175",  64+256 } } },
    { { { "350", 126+256 }, { "350", 128+256 }, { "350", 128+256 }, { "350", 128+256 } } },
    { { { "525", 180+256 }, { "525", 184+256 }, { "525", 184+256 }, { "525", 192+256 } } },
    { { { "700", 235+256 }, { "700", 240+256 }, { "700", 240+256 }, { "700", 255+256 } } },
};

static const GraticuleLines flat_millivolts9[] = {
    { { {   "0",  32+512 }, {   "0",  32+512 }, {   "0",  32+512 }, {   "0",   0+512 } } },
    { { { "175", 142+512 }, { "175", 144+512 }, { "175", 144+512 }, { "175", 128+512 } } },
    { { { "350", 251+512 }, { "350", 256+512 }, { "350", 256+512 }, { "350", 256+512 } } },
    { { { "525", 361+512 }, { "525", 368+512 }, { "525", 368+512 }, { "525", 384+512 } } },
    { { { "700", 470+512 }, { "700", 480+512 }, { "700", 480+512 }, { "700", 511+512 } } },
};

static const GraticuleLines flat_millivolts10[] = {
    { { {   "0",  64+1024 }, {   "0",  64+1024 }, {   "0",  64+1024 }, {   "0",    0+1024 } } },
    { { { "175", 283+1024 }, { "175", 288+1024 }, { "175", 288+1024 }, { "175",  256+1024 } } },
    { { { "350", 502+1024 }, { "350", 512+1024 }, { "350", 512+1024 }, { "350",  512+1024 } } },
    { { { "525", 721+1024 }, { "525", 736+1024 }, { "525", 736+1024 }, { "525",  768+1024 } } },
    { { { "700", 940+1024 }, { "700", 960+1024 }, { "700", 960+1024 }, { "700", 1023+1024 } } },
};

static const GraticuleLines flat_millivolts12[] = {
    { { {   "0",  256+4096 }, {   "0",  256+4096 }, {   "0",  256+4096 }, {   "0",    0+4096 } } },
    { { { "175", 1132+4096 }, { "175", 1152+4096 }, { "175", 1152+4096 }, { "175", 1024+4096 } } },
    { { { "350", 2008+4096 }, { "350", 2048+4096 }, { "350", 2048+4096 }, { "350", 2048+4096 } } },
    { { { "525", 2884+4096 }, { "525", 2944+4096 }, { "525", 2944+4096 }, { "525", 3072+4096 } } },
    { { { "700", 3760+4096 }, { "700", 3840+4096 }, { "700", 3840+4096 }, { "700", 4095+4096 } } },
};

static const GraticuleLines flat_ire8[] = {
    { { { "-25", -39+256 }, { "-25", -40+256 }, { "-25", -40+256 }, { "-25", -64+256 } } },
    { { {   "0",  16+256 }, {   "0",  16+256 }, {   "0",  16+256 }, {   "0",   0+256 } } },
    { { {  "25",  71+256 }, {  "25",  72+256 }, {  "25",  72+256 }, {  "25",  64+256 } } },
    { { {  "50", 126+256 }, {  "50", 128+256 }, {  "50", 128+256 }, {  "50", 128+256 } } },
    { { {  "75", 180+256 }, {  "75", 184+256 }, {  "75", 184+256 }, {  "75", 192+256 } } },
    { { { "100", 235+256 }, { "100", 240+256 }, { "100", 240+256 }, { "100", 256+256 } } },
    { { { "125", 290+256 }, { "125", 296+256 }, { "125", 296+256 }, { "125", 320+256 } } },
};

static const GraticuleLines flat_ire9[] = {
    { { { "-25", -78+512 }, { "-25", -80+512 }, { "-25", -80+512 }, { "-25",-128+512 } } },
    { { {   "0",  32+512 }, {   "0",  32+512 }, {   "0",  32+512 }, {   "0",   0+512 } } },
    { { {  "25", 142+512 }, {  "25", 144+512 }, {  "25", 144+512 }, {  "25", 128+512 } } },
    { { {  "50", 251+512 }, {  "50", 256+512 }, {  "50", 256+512 }, {  "50", 256+512 } } },
    { { {  "75", 361+512 }, {  "75", 368+512 }, {  "75", 368+512 }, {  "75", 384+512 } } },
    { { { "100", 470+512 }, { "100", 480+512 }, { "100", 480+512 }, { "100", 512+512 } } },
    { { { "125", 580+512 }, { "125", 592+512 }, { "125", 592+512 }, { "125", 640+512 } } },
};

static const GraticuleLines flat_ire10[] = {
    { { { "-25",-156+1024 }, { "-25",-160+1024 }, { "-25",-160+1024 }, { "-25", -256+1024 } } },
    { { {   "0",  64+1024 }, {   "0",  64+1024 }, {  "0",   64+1024 }, {   "0",    0+1024 } } },
    { { {  "25", 283+1024 }, {  "25", 288+1024 }, {  "25", 288+1024 }, {  "25",  256+1024 } } },
    { { {  "50", 502+1024 }, {  "50", 512+1024 }, {  "50", 512+1024 }, {  "50",  512+1024 } } },
    { { {  "75", 721+1024 }, {  "75", 736+1024 }, {  "75", 736+1024 }, {  "75",  768+1024 } } },
    { { { "100", 940+1024 }, { "100", 960+1024 }, { "100", 960+1024 }, { "100", 1024+1024 } } },
    { { { "125",1160+1024 }, { "125",1184+1024 }, { "125",1184+1024 }, { "125", 1280+1024 } } },
};

static const GraticuleLines flat_ire12[] = {
    { { { "-25", -624+4096 }, { "-25", -640+4096 }, { "-25", -640+4096 }, { "-25",-1024+4096 } } },
    { { {   "0",  256+4096 }, {   "0",  256+4096 }, {   "0",  256+4096 }, {   "0",    0+4096 } } },
    { { {  "25", 1132+4096 }, {  "25", 1152+4096 }, {  "25", 1152+4096 }, {  "25", 1024+4096 } } },
    { { {  "50", 2008+4096 }, {  "50", 2048+4096 }, {  "50", 2048+4096 }, {  "50", 2048+4096 } } },
    { { {  "75", 2884+4096 }, {  "75", 2944+4096 }, {  "75", 2944+4096 }, {  "75", 3072+4096 } } },
    { { { "100", 3760+4096 }, { "100", 3840+4096 }, { "100", 3840+4096 }, { "100", 4096+4096 } } },
    { { { "125", 4640+4096 }, { "125", 4736+4096 }, { "125", 4736+4096 }, { "125", 5120+4096 } } },
};

static const GraticuleLines digital8[] = {
    { { {  "16",  16 }, {  "16",  16 }, {  "16",  16 }, {   "0",   0 } } },
    { { { "128", 128 }, { "128", 128 }, { "128", 128 }, { "128", 128 } } },
    { { { "235", 235 }, { "240", 240 }, { "240", 240 }, { "255", 255 } } },
};

static const GraticuleLines digital9[] = {
    { { {  "32",  32 }, {  "32",  32 }, {  "32",  32 }, {   "0",   0 } } },
    { { { "256", 256 }, { "256", 256 }, { "256", 256 }, { "256", 256 } } },
    { { { "470", 470 }, { "480", 480 }, { "480", 480 }, { "511", 511 } } },
};

static const GraticuleLines digital10[] = {
    { { {  "64",  64 }, {  "64",  64 }, {  "64",  64 }, {    "0",    0 } } },
    { { { "512", 512 }, { "512", 512 }, { "512", 512 }, {  "512",  512 } } },
    { { { "940", 940 }, { "960", 960 }, { "960", 960 }, { "1023", 1023 } } },
};

static const GraticuleLines digital12[] = {
    { { {  "256",  256 }, {  "256",  256 }, {  "256",  256 }, {    "0",    0 } } },
    { { { "2048", 2048 }, { "2048", 2048 }, { "2048", 2048 }, { "2048", 2048 } } },
    { { { "3760", 3760 }, { "3840", 3840 }, { "3840", 3840 }, { "4095", 4095 } } },
};

static const GraticuleLines millivolts8[] = {
    { { {   "0",  16 }, {   "0",  16 }, {   "0",  16 }, {   "0",   0 } } },
    { { { "175",  71 }, { "175",  72 }, { "175",  72 }, { "175",  64 } } },
    { { { "350", 126 }, { "350", 128 }, { "350", 128 }, { "350", 128 } } },
    { { { "525", 180 }, { "525", 184 }, { "525", 184 }, { "525", 192 } } },
    { { { "700", 235 }, { "700", 240 }, { "700", 240 }, { "700", 255 } } },
};

static const GraticuleLines millivolts9[] = {
    { { {   "0",  32 }, {   "0",  32 }, {   "0",  32 }, {   "0",   0 } } },
    { { { "175", 142 }, { "175", 144 }, { "175", 144 }, { "175", 128 } } },
    { { { "350", 251 }, { "350", 256 }, { "350", 256 }, { "350", 256 } } },
    { { { "525", 361 }, { "525", 368 }, { "525", 368 }, { "525", 384 } } },
    { { { "700", 470 }, { "700", 480 }, { "700", 480 }, { "700", 511 } } },
};

static const GraticuleLines millivolts10[] = {
    { { {   "0",  64 }, {   "0",  64 }, {   "0",  64 }, {   "0",    0 } } },
    { { { "175", 283 }, { "175", 288 }, { "175", 288 }, { "175",  256 } } },
    { { { "350", 502 }, { "350", 512 }, { "350", 512 }, { "350",  512 } } },
    { { { "525", 721 }, { "525", 736 }, { "525", 736 }, { "525",  768 } } },
    { { { "700", 940 }, { "700", 960 }, { "700", 960 }, { "700", 1023 } } },
};

static const GraticuleLines millivolts12[] = {
    { { {   "0",  256 }, {   "0",  256 }, {   "0",  256 }, {   "0",    0 } } },
    { { { "175", 1132 }, { "175", 1152 }, { "175", 1152 }, { "175", 1024 } } },
    { { { "350", 2008 }, { "350", 2048 }, { "350", 2048 }, { "350", 2048 } } },
    { { { "525", 2884 }, { "525", 2944 }, { "525", 2944 }, { "525", 3072 } } },
    { { { "700", 3760 }, { "700", 3840 }, { "700", 3840 }, { "700", 4095 } } },
};

static const GraticuleLines ire8[] = {
    { { {   "0",  16 }, {   "0",  16 }, {   "0",  16 }, {   "0",   0 } } },
    { { {  "25",  71 }, {  "25",  72 }, {  "25",  72 }, {  "25",  64 } } },
    { { {  "50", 126 }, {  "50", 128 }, {  "50", 128 }, {  "50", 128 } } },
    { { {  "75", 180 }, {  "75", 184 }, {  "75", 184 }, {  "75", 192 } } },
    { { { "100", 235 }, { "100", 240 }, { "100", 240 }, { "100", 255 } } },
};

static const GraticuleLines ire9[] = {
    { { {   "0",  32 }, {   "0",  32 }, {   "0",  32 }, {   "0",   0 } } },
    { { {  "25", 142 }, {  "25", 144 }, {  "25", 144 }, {  "25", 128 } } },
    { { {  "50", 251 }, {  "50", 256 }, {  "50", 256 }, {  "50", 256 } } },
    { { {  "75", 361 }, {  "75", 368 }, {  "75", 368 }, {  "75", 384 } } },
    { { { "100", 470 }, { "100", 480 }, { "100", 480 }, { "100", 511 } } },
};

static const GraticuleLines ire10[] = {
    { { {   "0",  64 }, {   "0",  64 }, {  "0",   64 }, {   "0",    0 } } },
    { { {  "25", 283 }, {  "25", 288 }, {  "25", 288 }, {  "25",  256 } } },
    { { {  "50", 502 }, {  "50", 512 }, {  "50", 512 }, {  "50",  512 } } },
    { { {  "75", 721 }, {  "75", 736 }, {  "75", 736 }, {  "75",  768 } } },
    { { { "100", 940 }, { "100", 960 }, { "100", 960 }, { "100", 1023 } } },
};

static const GraticuleLines ire12[] = {
    { { {   "0",  256 }, {   "0",  256 }, {   "0",  256 }, {   "0",    0 } } },
    { { {  "25", 1132 }, {  "25", 1152 }, {  "25", 1152 }, {  "25", 1024 } } },
    { { {  "50", 2008 }, {  "50", 2048 }, {  "50", 2048 }, {  "50", 2048 } } },
    { { {  "75", 2884 }, {  "75", 2944 }, {  "75", 2944 }, {  "75", 3072 } } },
    { { { "100", 3760 }, { "100", 3840 }, { "100", 3840 }, { "100", 4095 } } },
};

static const GraticuleLines chroma_digital8[] = {
    { { {  "50",  50 }, {  "50",  50 }, {  "50",  50 }, {  "50",  50 } } },
    { { { "100", 100 }, { "100", 100 }, { "100", 100 }, { "100", 100 } } },
    { { { "150", 150 }, { "150", 150 }, { "150", 150 }, { "150", 150 } } },
    { { { "200", 200 }, { "200", 200 }, { "200", 200 }, { "200", 200 } } },
    { { { "255", 255 }, { "255", 255 }, { "255", 255 }, { "255", 255 } } },
};

static const GraticuleLines chroma_digital9[] = {
    { { { "100", 100 }, { "100", 100 }, { "100", 100 }, { "100", 100 } } },
    { { { "200", 200 }, { "200", 200 }, { "200", 200 }, { "200", 200 } } },
    { { { "300", 300 }, { "300", 300 }, { "300", 300 }, { "300", 300 } } },
    { { { "400", 400 }, { "400", 400 }, { "400", 400 }, { "400", 400 } } },
    { { { "500", 500 }, { "500", 500 }, { "500", 500 }, { "500", 500 } } },
};

static const GraticuleLines chroma_digital10[] = {
    { { { "200", 200 }, { "200", 200 }, { "200", 200 }, { "200", 200 } } },
    { { { "400", 400 }, { "400", 400 }, { "400", 400 }, { "400", 400 } } },
    { { { "600", 600 }, { "600", 600 }, { "600", 600 }, { "600", 600 } } },
    { { { "800", 800 }, { "800", 800 }, { "800", 800 }, { "800", 800 } } },
    { { {"1000",1000 }, {"1000",1000 }, {"1000",1000 }, {"1000",1000 } } },
};

static const GraticuleLines chroma_digital12[] = {
    { { {  "800",  800 }, {  "800",  800 }, {  "800",  800 }, {  "800",  800 } } },
    { { { "1600", 1600 }, { "1600", 1600 }, { "1600", 1600 }, { "1600", 1600 } } },
    { { { "2400", 2400 }, { "2400", 2400 }, { "2400", 2400 }, { "2400", 2400 } } },
    { { { "3200", 3200 }, { "3200", 3200 }, { "3200", 3200 }, { "3200", 3200 } } },
    { { { "4000", 4000 }, { "4000", 4000 }, { "4000", 4000 }, { "4000", 4000 } } },
};

static void blend_vline(uint8_t *dst, int height, int linesize, float o1, float o2, int v, int step)
{
    int y;

    for (y = 0; y < height; y += step) {
        dst[0] = v * o1 + dst[0] * o2;

        dst += linesize * step;
    }
}

static void blend_vline16(uint16_t *dst, int height, int linesize, float o1, float o2, int v, int step)
{
    int y;

    for (y = 0; y < height; y += step) {
        dst[0] = v * o1 + dst[0] * o2;

        dst += (linesize / 2) * step;
    }
}

static void blend_hline(uint8_t *dst, int width, float o1, float o2, int v, int step)
{
    int x;

    for (x = 0; x < width; x += step) {
        dst[x] = v * o1 + dst[x] * o2;
    }
}

static void blend_hline16(uint16_t *dst, int width, float o1, float o2, int v, int step)
{
    int x;

    for (x = 0; x < width; x += step) {
        dst[x] = v * o1 + dst[x] * o2;
    }
}

static void draw_htext(AVFrame *out, int x, int y, float o1, float o2, const char *txt, const uint8_t color[4])
{
    const uint8_t *font;
    int font_height;
    int i, plane;

    font = avpriv_cga_font,   font_height =  8;

    for (plane = 0; plane < 4 && out->data[plane]; plane++) {
        for (i = 0; txt[i]; i++) {
            int char_y, mask;
            int v = color[plane];

            uint8_t *p = out->data[plane] + y * out->linesize[plane] + (x + i * 8);
            for (char_y = 0; char_y < font_height; char_y++) {
                for (mask = 0x80; mask; mask >>= 1) {
                    if (font[txt[i] * font_height + char_y] & mask)
                        p[0] = p[0] * o2 + v * o1;
                    p++;
                }
                p += out->linesize[plane] - 8;
            }
        }
    }
}

static void draw_htext16(AVFrame *out, int x, int y, int mult, float o1, float o2, const char *txt, const uint8_t color[4])
{
    const uint8_t *font;
    int font_height;
    int i, plane;

    font = avpriv_cga_font,   font_height =  8;

    for (plane = 0; plane < 4 && out->data[plane]; plane++) {
        for (i = 0; txt[i]; i++) {
            int char_y, mask;
            int v = color[plane] * mult;

            uint16_t *p = (uint16_t *)(out->data[plane] + y * out->linesize[plane]) + (x + i * 8);
            for (char_y = 0; char_y < font_height; char_y++) {
                for (mask = 0x80; mask; mask >>= 1) {
                    if (font[txt[i] * font_height + char_y] & mask)
                        p[0] = p[0] * o2 + v * o1;
                    p++;
                }
                p += out->linesize[plane] / 2 - 8;
            }
        }
    }
}

static void draw_vtext(AVFrame *out, int x, int y, float o1, float o2, const char *txt, const uint8_t color[4])
{
    const uint8_t *font;
    int font_height;
    int i, plane;

    font = avpriv_cga_font,   font_height =  8;

    for (plane = 0; plane < 4 && out->data[plane]; plane++) {
        for (i = 0; txt[i]; i++) {
            int char_y, mask;
            int v = color[plane];

            for (char_y = font_height - 1; char_y >= 0; char_y--) {
                uint8_t *p = out->data[plane] + (y + i * 10) * out->linesize[plane] + x;
                for (mask = 0x80; mask; mask >>= 1) {
                    if (font[txt[i] * font_height + font_height - 1 - char_y] & mask)
                        p[char_y] = p[char_y] * o2 + v * o1;
                    p += out->linesize[plane];
                }
            }
        }
    }
}

static void draw_vtext16(AVFrame *out, int x, int y, int mult, float o1, float o2, const char *txt, const uint8_t color[4])
{
    const uint8_t *font;
    int font_height;
    int i, plane;

    font = avpriv_cga_font,   font_height =  8;

    for (plane = 0; plane < 4 && out->data[plane]; plane++) {
        for (i = 0; txt[i]; i++) {
            int char_y, mask;
            int v = color[plane] * mult;

            for (char_y = 0; char_y < font_height; char_y++) {
                uint16_t *p = (uint16_t *)(out->data[plane] + (y + i * 10) * out->linesize[plane]) + x;
                for (mask = 0x80; mask; mask >>= 1) {
                    if (font[txt[i] * font_height + font_height - 1 - char_y] & mask)
                        p[char_y] = p[char_y] * o2 + v * o1;
                    p += out->linesize[plane] / 2;
                }
            }
        }
    }
}

static void graticule_none(WaveformContext *s, AVFrame *out)
{
}

static void graticule_green_row(WaveformContext *s, AVFrame *out)
{
    const int step = (s->flags & 2) + 1;
    const float o1 = s->opacity;
    const float o2 = 1. - o1;
    const int height = s->display == PARADE ? out->height / s->acomp : out->height;
    int k = 0, c, p, l, offset_x = 0, offset_y = 0;

    for (c = 0; c < s->ncomp; c++) {
        if (!((1 << c) & s->pcomp) || (!s->display && k > 0))
            continue;

        k++;
        for (p = 0; p < s->ncomp; p++) {
            const int v = green_yuva_color[p];
            for (l = 0; l < s->nb_glines; l++) {
                const uint16_t pos = s->glines[l].line[c].pos;
                int x = offset_x + (s->mirror ? s->size - 1 - pos : pos);
                uint8_t *dst = out->data[p] + offset_y * out->linesize[p] + x;

                blend_vline(dst, height, out->linesize[p], o1, o2, v, step);
            }
        }

        for (l = 0; l < s->nb_glines && (s->flags & 1); l++) {
            const char *name = s->glines[l].line[c].name;
            const uint16_t pos = s->glines[l].line[c].pos;
            int x = offset_x + (s->mirror ? s->size - 1 - pos : pos) - 10;

            if (x < 0)
                x = 4;

            draw_vtext(out, x, offset_y + 2, o1, o2, name, green_yuva_color);
        }

        offset_x += s->size * (s->display == STACK);
        offset_y += height * (s->display == PARADE);
    }
}

static void graticule16_green_row(WaveformContext *s, AVFrame *out)
{
    const int step = (s->flags & 2) + 1;
    const float o1 = s->opacity;
    const float o2 = 1. - o1;
    const int mult = s->size / 256;
    const int height = s->display == PARADE ? out->height / s->acomp : out->height;
    int k = 0, c, p, l, offset_x = 0, offset_y = 0;

    for (c = 0; c < s->ncomp; c++) {
        if (!((1 << c) & s->pcomp) || (!s->display && k > 0))
            continue;

        k++;
        for (p = 0; p < s->ncomp; p++) {
            const int v = green_yuva_color[p] * mult;
            for (l = 0; l < s->nb_glines ; l++) {
                const uint16_t pos = s->glines[l].line[c].pos;
                int x = offset_x + (s->mirror ? s->size - 1 - pos : pos);
                uint16_t *dst = (uint16_t *)(out->data[p] + offset_y * out->linesize[p]) + x;

                blend_vline16(dst, height, out->linesize[p], o1, o2, v, step);
            }
        }

        for (l = 0; l < s->nb_glines && (s->flags & 1); l++) {
            const char *name = s->glines[l].line[c].name;
            const uint16_t pos = s->glines[l].line[c].pos;
            int x = offset_x + (s->mirror ? s->size - 1 - pos : pos) - 10;

            if (x < 0)
                x = 4;

            draw_vtext16(out, x, offset_y + 2, mult, o1, o2, name, green_yuva_color);
        }

        offset_x += s->size * (s->display == STACK);
        offset_y += height * (s->display == PARADE);
    }
}

static void graticule_green_column(WaveformContext *s, AVFrame *out)
{
    const int step = (s->flags & 2) + 1;
    const float o1 = s->opacity;
    const float o2 = 1. - o1;
    const int width = s->display == PARADE ? out->width / s->acomp : out->width;
    int k = 0, c, p, l, offset_y = 0, offset_x = 0;

    for (c = 0; c < s->ncomp; c++) {
        if ((!((1 << c) & s->pcomp) || (!s->display && k > 0)))
            continue;

        k++;
        for (p = 0; p < s->ncomp; p++) {
            const int v = green_yuva_color[p];
            for (l = 0; l < s->nb_glines ; l++) {
                const uint16_t pos = s->glines[l].line[c].pos;
                int y = offset_y + (s->mirror ? s->size - 1 - pos : pos);
                uint8_t *dst = out->data[p] + y * out->linesize[p] + offset_x;

                blend_hline(dst, width, o1, o2, v, step);
            }
        }

        for (l = 0; l < s->nb_glines && (s->flags & 1); l++) {
            const char *name = s->glines[l].line[c].name;
            const uint16_t pos = s->glines[l].line[c].pos;
            int y = offset_y + (s->mirror ? s->size - 1 - pos : pos) - 10;

            if (y < 0)
                y = 4;

            draw_htext(out, 2 + offset_x, y, o1, o2, name, green_yuva_color);
        }

        offset_y += s->size * (s->display == STACK);
        offset_x += width * (s->display == PARADE);
    }
}

static void graticule16_green_column(WaveformContext *s, AVFrame *out)
{
    const int step = (s->flags & 2) + 1;
    const float o1 = s->opacity;
    const float o2 = 1. - o1;
    const int mult = s->size / 256;
    const int width = s->display == PARADE ? out->width / s->acomp : out->width;
    int k = 0, c, p, l, offset_x = 0, offset_y = 0;

    for (c = 0; c < s->ncomp; c++) {
        if ((!((1 << c) & s->pcomp) || (!s->display && k > 0)))
            continue;

        k++;
        for (p = 0; p < s->ncomp; p++) {
            const int v = green_yuva_color[p] * mult;
            for (l = 0; l < s->nb_glines ; l++) {
                const uint16_t pos = s->glines[l].line[c].pos;
                int y = offset_y + (s->mirror ? s->size - 1 - pos : pos);
                uint16_t *dst = (uint16_t *)(out->data[p] + y * out->linesize[p]) + offset_x;

                blend_hline16(dst, width, o1, o2, v, step);
            }
        }

        for (l = 0; l < s->nb_glines && (s->flags & 1); l++) {
            const char *name = s->glines[l].line[c].name;
            const uint16_t pos = s->glines[l].line[c].pos;
            int y = offset_y + (s->mirror ? s->size - 1 - pos: pos) - 10;

            if (y < 0)
                y = 4;

            draw_htext16(out, 2 + offset_x, y, mult, o1, o2, name, green_yuva_color);
        }

        offset_y += s->size * (s->display == STACK);
        offset_x += width * (s->display == PARADE);
    }
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    WaveformContext *s = ctx->priv;

    s->desc  = av_pix_fmt_desc_get(inlink->format);
    s->ncomp = s->desc->nb_components;
    s->bits = s->desc->comp[0].depth;
    s->max = 1 << s->bits;
    s->intensity = s->fintensity * (s->max - 1);

    s->shift_w[0] = s->shift_w[3] = 0;
    s->shift_h[0] = s->shift_h[3] = 0;
    s->shift_w[1] = s->shift_w[2] = s->desc->log2_chroma_w;
    s->shift_h[1] = s->shift_h[2] = s->desc->log2_chroma_h;

    s->graticulef = graticule_none;

    switch (s->filter) {
    case AFLAT: s->size = 256 * 2; break;
    case FLAT:  s->size = 256 * 3; break;
    default:    s->size = 256;     break;
    }

    switch (s->filter | ((s->bits > 8) << 4) |
            (s->mode << 8) | (s->mirror << 12)) {
    case 0x1100: s->waveform = lowpass_column_mirror; break;
    case 0x1000: s->waveform = lowpass_row_mirror;    break;
    case 0x0100: s->waveform = lowpass_column;        break;
    case 0x0000: s->waveform = lowpass_row;           break;
    case 0x1110: s->waveform = lowpass16_column_mirror; break;
    case 0x1010: s->waveform = lowpass16_row_mirror;    break;
    case 0x0110: s->waveform = lowpass16_column;        break;
    case 0x0010: s->waveform = lowpass16_row;           break;
    case 0x1101:
    case 0x1001:
    case 0x0101:
    case 0x0001: s->waveform = flat;      break;
    case 0x1111:
    case 0x1011:
    case 0x0111:
    case 0x0011: s->waveform = flat16;    break;
    case 0x1102:
    case 0x1002:
    case 0x0102:
    case 0x0002: s->waveform = aflat;     break;
    case 0x1112:
    case 0x1012:
    case 0x0112:
    case 0x0012: s->waveform = aflat16;   break;
    case 0x1103:
    case 0x1003:
    case 0x0103:
    case 0x0003: s->waveform = chroma;    break;
    case 0x1113:
    case 0x1013:
    case 0x0113:
    case 0x0013: s->waveform = chroma16;  break;
    case 0x1104:
    case 0x1004:
    case 0x0104:
    case 0x0004: s->waveform = color;     break;
    case 0x1114:
    case 0x1014:
    case 0x0114:
    case 0x0014: s->waveform = color16;   break;
    case 0x1105:
    case 0x1005:
    case 0x0105:
    case 0x0005: s->waveform = acolor;    break;
    case 0x1115:
    case 0x1015:
    case 0x0115:
    case 0x0015: s->waveform = acolor16;  break;
    }

    switch (s->filter) {
    case LOWPASS:
    case COLOR:
    case ACOLOR:
    case CHROMA:
    case AFLAT:
    case FLAT:
        if (s->graticule && s->mode == 1)
            s->graticulef = s->bits > 8 ? graticule16_green_column : graticule_green_column;
        else if (s->graticule && s->mode == 0)
            s->graticulef = s->bits > 8 ? graticule16_green_row : graticule_green_row;
        break;
    }

    switch (s->filter) {
    case COLOR:
    case ACOLOR:
    case LOWPASS:
        switch (s->scale) {
        case DIGITAL:
            switch (s->bits) {
            case  8: s->glines = (GraticuleLines *)digital8;  s->nb_glines = FF_ARRAY_ELEMS(digital8);  break;
            case  9: s->glines = (GraticuleLines *)digital9;  s->nb_glines = FF_ARRAY_ELEMS(digital9);  break;
            case 10: s->glines = (GraticuleLines *)digital10; s->nb_glines = FF_ARRAY_ELEMS(digital10); break;
            case 12: s->glines = (GraticuleLines *)digital12; s->nb_glines = FF_ARRAY_ELEMS(digital12); break;
            }
            break;
        case MILLIVOLTS:
            switch (s->bits) {
            case  8: s->glines = (GraticuleLines *)millivolts8;  s->nb_glines = FF_ARRAY_ELEMS(millivolts8);  break;
            case  9: s->glines = (GraticuleLines *)millivolts9;  s->nb_glines = FF_ARRAY_ELEMS(millivolts9);  break;
            case 10: s->glines = (GraticuleLines *)millivolts10; s->nb_glines = FF_ARRAY_ELEMS(millivolts10); break;
            case 12: s->glines = (GraticuleLines *)millivolts12; s->nb_glines = FF_ARRAY_ELEMS(millivolts12); break;
            }
            break;
        case IRE:
            switch (s->bits) {
            case  8: s->glines = (GraticuleLines *)ire8;  s->nb_glines = FF_ARRAY_ELEMS(ire8);  break;
            case  9: s->glines = (GraticuleLines *)ire9;  s->nb_glines = FF_ARRAY_ELEMS(ire9);  break;
            case 10: s->glines = (GraticuleLines *)ire10; s->nb_glines = FF_ARRAY_ELEMS(ire10); break;
            case 12: s->glines = (GraticuleLines *)ire12; s->nb_glines = FF_ARRAY_ELEMS(ire12); break;
            }
            break;
        }
        break;
    case CHROMA:
        switch (s->scale) {
        case DIGITAL:
            switch (s->bits) {
            case  8: s->glines = (GraticuleLines *)chroma_digital8;  s->nb_glines = FF_ARRAY_ELEMS(chroma_digital8);  break;
            case  9: s->glines = (GraticuleLines *)chroma_digital9;  s->nb_glines = FF_ARRAY_ELEMS(chroma_digital9);  break;
            case 10: s->glines = (GraticuleLines *)chroma_digital10; s->nb_glines = FF_ARRAY_ELEMS(chroma_digital10); break;
            case 12: s->glines = (GraticuleLines *)chroma_digital12; s->nb_glines = FF_ARRAY_ELEMS(chroma_digital12); break;
            }
            break;
        case MILLIVOLTS:
            switch (s->bits) {
            case  8: s->glines = (GraticuleLines *)millivolts8;  s->nb_glines = FF_ARRAY_ELEMS(millivolts8);  break;
            case  9: s->glines = (GraticuleLines *)millivolts9;  s->nb_glines = FF_ARRAY_ELEMS(millivolts9);  break;
            case 10: s->glines = (GraticuleLines *)millivolts10; s->nb_glines = FF_ARRAY_ELEMS(millivolts10); break;
            case 12: s->glines = (GraticuleLines *)millivolts12; s->nb_glines = FF_ARRAY_ELEMS(millivolts12); break;
            }
            break;
        case IRE:
            switch (s->bits) {
            case  8: s->glines = (GraticuleLines *)ire8;  s->nb_glines = FF_ARRAY_ELEMS(ire8);  break;
            case  9: s->glines = (GraticuleLines *)ire9;  s->nb_glines = FF_ARRAY_ELEMS(ire9);  break;
            case 10: s->glines = (GraticuleLines *)ire10; s->nb_glines = FF_ARRAY_ELEMS(ire10); break;
            case 12: s->glines = (GraticuleLines *)ire12; s->nb_glines = FF_ARRAY_ELEMS(ire12); break;
            }
            break;
        }
        break;
    case AFLAT:
        switch (s->scale) {
        case DIGITAL:
            switch (s->bits) {
            case  8: s->glines = (GraticuleLines *)aflat_digital8;  s->nb_glines = FF_ARRAY_ELEMS(aflat_digital8);  break;
            case  9: s->glines = (GraticuleLines *)aflat_digital9;  s->nb_glines = FF_ARRAY_ELEMS(aflat_digital9);  break;
            case 10: s->glines = (GraticuleLines *)aflat_digital10; s->nb_glines = FF_ARRAY_ELEMS(aflat_digital10); break;
            case 12: s->glines = (GraticuleLines *)aflat_digital12; s->nb_glines = FF_ARRAY_ELEMS(aflat_digital12); break;
            }
            break;
        case MILLIVOLTS:
            switch (s->bits) {
            case  8: s->glines = (GraticuleLines *)aflat_millivolts8;  s->nb_glines = FF_ARRAY_ELEMS(aflat_millivolts8);  break;
            case  9: s->glines = (GraticuleLines *)aflat_millivolts9;  s->nb_glines = FF_ARRAY_ELEMS(aflat_millivolts9);  break;
            case 10: s->glines = (GraticuleLines *)aflat_millivolts10; s->nb_glines = FF_ARRAY_ELEMS(aflat_millivolts10); break;
            case 12: s->glines = (GraticuleLines *)aflat_millivolts12; s->nb_glines = FF_ARRAY_ELEMS(aflat_millivolts12); break;
            }
            break;
        case IRE:
            switch (s->bits) {
            case  8: s->glines = (GraticuleLines *)aflat_ire8;  s->nb_glines = FF_ARRAY_ELEMS(aflat_ire8);  break;
            case  9: s->glines = (GraticuleLines *)aflat_ire9;  s->nb_glines = FF_ARRAY_ELEMS(aflat_ire9);  break;
            case 10: s->glines = (GraticuleLines *)aflat_ire10; s->nb_glines = FF_ARRAY_ELEMS(aflat_ire10); break;
            case 12: s->glines = (GraticuleLines *)aflat_ire12; s->nb_glines = FF_ARRAY_ELEMS(aflat_ire12); break;
            }
            break;
        }
        break;
    case FLAT:
        switch (s->scale) {
        case DIGITAL:
            switch (s->bits) {
            case  8: s->glines = (GraticuleLines *)flat_digital8;  s->nb_glines = FF_ARRAY_ELEMS(flat_digital8);  break;
            case  9: s->glines = (GraticuleLines *)flat_digital9;  s->nb_glines = FF_ARRAY_ELEMS(flat_digital9);  break;
            case 10: s->glines = (GraticuleLines *)flat_digital10; s->nb_glines = FF_ARRAY_ELEMS(flat_digital10); break;
            case 12: s->glines = (GraticuleLines *)flat_digital12; s->nb_glines = FF_ARRAY_ELEMS(flat_digital12); break;
            }
            break;
        case MILLIVOLTS:
            switch (s->bits) {
            case  8: s->glines = (GraticuleLines *)flat_millivolts8;  s->nb_glines = FF_ARRAY_ELEMS(flat_millivolts8);  break;
            case  9: s->glines = (GraticuleLines *)flat_millivolts9;  s->nb_glines = FF_ARRAY_ELEMS(flat_millivolts9);  break;
            case 10: s->glines = (GraticuleLines *)flat_millivolts10; s->nb_glines = FF_ARRAY_ELEMS(flat_millivolts10); break;
            case 12: s->glines = (GraticuleLines *)flat_millivolts12; s->nb_glines = FF_ARRAY_ELEMS(flat_millivolts12); break;
            }
            break;
        case IRE:
            switch (s->bits) {
            case  8: s->glines = (GraticuleLines *)flat_ire8;  s->nb_glines = FF_ARRAY_ELEMS(flat_ire8);  break;
            case  9: s->glines = (GraticuleLines *)flat_ire9;  s->nb_glines = FF_ARRAY_ELEMS(flat_ire9);  break;
            case 10: s->glines = (GraticuleLines *)flat_ire10; s->nb_glines = FF_ARRAY_ELEMS(flat_ire10); break;
            case 12: s->glines = (GraticuleLines *)flat_ire12; s->nb_glines = FF_ARRAY_ELEMS(flat_ire12); break;
            }
            break;
        }
        break;
    }

    s->size = s->size << (s->bits - 8);

    switch (inlink->format) {
    case AV_PIX_FMT_GBRAP:
    case AV_PIX_FMT_GBRP:
    case AV_PIX_FMT_GBRP9:
    case AV_PIX_FMT_GBRP10:
    case AV_PIX_FMT_GBRP12:
        memcpy(s->bg_color, black_gbrp_color, sizeof(s->bg_color));
        s->graticulef = graticule_none;
        break;
    default:
        memcpy(s->bg_color, black_yuva_color, sizeof(s->bg_color));
    }

    s->bg_color[3] *= s->bgopacity;

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    WaveformContext *s = ctx->priv;
    int comp = 0, i, j = 0, k, p, size;

    for (i = 0; i < s->ncomp; i++) {
        if ((1 << i) & s->pcomp)
            comp++;
    }
    s->acomp = comp;
    s->odesc = av_pix_fmt_desc_get(outlink->format);
    s->dcomp = s->odesc->nb_components;

    av_freep(&s->peak);

    if (s->mode) {
        outlink->h = s->size * FFMAX(comp * (s->display == STACK), 1);
        outlink->w = inlink->w * FFMAX(comp * (s->display == PARADE), 1);
        size = inlink->w;
    } else {
        outlink->w = s->size * FFMAX(comp * (s->display == STACK), 1);
        outlink->h = inlink->h * FFMAX(comp * (s->display == PARADE), 1);
        size = inlink->h;
    }

    s->peak = av_malloc_array(size, 32 * sizeof(*s->peak));
    if (!s->peak)
        return AVERROR(ENOMEM);

    for (p = 0; p < s->ncomp; p++) {
        const int plane = s->desc->comp[p].plane;
        int offset;

        if (!((1 << p) & s->pcomp))
            continue;

        for (k = 0; k < 4; k++) {
            s->emax[plane][k] = s->peak + size * (plane * 4 + k + 0);
            s->emin[plane][k] = s->peak + size * (plane * 4 + k + 16);
        }

        offset = j++ * s->size * (s->display == STACK);
        s->estart[plane] = offset;
        s->eend[plane]   = (offset + s->size - 1);
        for (i = 0; i < size; i++) {
            for (k = 0; k < 4; k++) {
                s->emax[plane][k][i] = s->estart[plane];
                s->emin[plane][k][i] = s->eend[plane];
            }
        }
    }

    outlink->sample_aspect_ratio = (AVRational){1,1};

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx  = inlink->dst;
    WaveformContext *s    = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    int i, j, k;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    out->pts = in->pts;
    av_frame_set_color_range(out, AVCOL_RANGE_JPEG);

    for (k = 0; k < s->dcomp; k++) {
        if (s->bits <= 8) {
            for (i = 0; i < outlink->h ; i++)
                memset(out->data[s->odesc->comp[k].plane] +
                       i * out->linesize[s->odesc->comp[k].plane],
                       s->bg_color[k], outlink->w);
        } else {
            const int mult = s->max / 256;
            uint16_t *dst = (uint16_t *)out->data[s->odesc->comp[k].plane];

            for (i = 0; i < outlink->h ; i++) {
                for (j = 0; j < outlink->w; j++)
                    dst[j] = s->bg_color[k] * mult;
                dst += out->linesize[s->odesc->comp[k].plane] / 2;
            }
        }
    }

    for (k = 0, i = 0; k < s->ncomp; k++) {
        if ((1 << k) & s->pcomp) {
            int offset_y;
            int offset_x;

            if (s->display == PARADE) {
                offset_x = s->mode ? i++ * inlink->w : 0;
                offset_y = s->mode ? 0 : i++ * inlink->h;
            } else {
                offset_y = s->mode ? i++ * s->size * !!s->display : 0;
                offset_x = s->mode ? 0 : i++ * s->size * !!s->display;
            }
            s->waveform(s, in, out, k, s->intensity, offset_y, offset_x, s->mode, s->mirror);
        }
    }
    s->graticulef(s, out);

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    WaveformContext *s = ctx->priv;

    av_freep(&s->peak);
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

AVFilter ff_vf_waveform = {
    .name          = "waveform",
    .description   = NULL_IF_CONFIG_SMALL("Video waveform monitor."),
    .priv_size     = sizeof(WaveformContext),
    .priv_class    = &waveform_class,
    .query_formats = query_formats,
    .uninit        = uninit,
    .inputs        = inputs,
    .outputs       = outputs,
};
