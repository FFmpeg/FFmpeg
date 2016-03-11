/*
 * Copyright (c) 2012-2015 Paul B Mahol
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
    ACHROMA,
    COLOR,
    NB_FILTERS
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
    int            ncomp;
    int            pcomp;
    const uint8_t  *bg_color;
    float          fintensity;
    int            intensity;
    int            mirror;
    int            display;
    int            envelope;
    int            graticule;
    float          opacity;
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
    GraticuleLines *glines;
    int            nb_glines;
    void (*waveform)(struct WaveformContext *s, AVFrame *in, AVFrame *out,
                     int component, int intensity, int offset, int column);
    void (*graticulef)(struct WaveformContext *s, AVFrame *out);
    const AVPixFmtDescriptor *desc;
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
    { "display", "set display mode", OFFSET(display), AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "display" },
    { "d",       "set display mode", OFFSET(display), AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "display" },
        { "overlay", NULL, 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "display" },
        { "parade",  NULL, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "display" },
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
        { "achroma", NULL, 0, AV_OPT_TYPE_CONST, {.i64=ACHROMA}, 0, 0, FLAGS, "filter" },
        { "color",   NULL, 0, AV_OPT_TYPE_CONST, {.i64=COLOR},   0, 0, FLAGS, "filter" },
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
        { "ire",        NULL, 0, AV_OPT_TYPE_CONST, {.i64=IRE},        0, 0, FLAGS, "scale" },
        { "millivolts", NULL, 0, AV_OPT_TYPE_CONST, {.i64=MILLIVOLTS}, 0, 0, FLAGS, "scale" },
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
    AV_PIX_FMT_GRAY8,
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

static const enum AVPixelFormat flat_pix_fmts[] = {
    AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_NONE
};

static const enum AVPixelFormat color_pix_fmts[] = {
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP,
    AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRP12,
    AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_YUV444P9, AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV444P12,
    AV_PIX_FMT_NONE
};

static int query_formats(AVFilterContext *ctx)
{
    WaveformContext *s = ctx->priv;
    AVFilterFormats *fmts_list;
    const enum AVPixelFormat *out_pix_fmts;
    const enum AVPixelFormat *pix_fmts;
    const AVPixFmtDescriptor *desc;
    AVFilterFormats *avff;
    int depth, rgb, i, ret, ncomp;

    if (s->filter != LOWPASS) {
        switch (s->filter) {
        case FLAT:
        case AFLAT:
        case CHROMA:
        case ACHROMA: pix_fmts = flat_pix_fmts;    break;
        case COLOR:   pix_fmts = color_pix_fmts;   break;
        }

        fmts_list = ff_make_format_list(pix_fmts);
        if (!fmts_list)
            return AVERROR(ENOMEM);
        return ff_set_common_formats(ctx, fmts_list);
    }

    if (!ctx->inputs[0]->in_formats ||
        !ctx->inputs[0]->in_formats->nb_formats) {
        return AVERROR(EAGAIN);
    }

    if (!ctx->inputs[0]->out_formats) {
        if ((ret = ff_formats_ref(ff_make_format_list(in_lowpass_pix_fmts), &ctx->inputs[0]->out_formats)) < 0)
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

    if (ncomp == 1 && depth == 8)
        out_pix_fmts = out_gray8_lowpass_pix_fmts;
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

static void envelope_instant16(WaveformContext *s, AVFrame *out, int plane, int component)
{
    const int dst_linesize = out->linesize[component] / 2;
    const int bg = s->bg_color[component] * (s->max / 256);
    const int limit = s->max - 1;
    const int dst_h = out->height;
    const int dst_w = out->width;
    const int start = s->estart[plane];
    const int end = s->eend[plane];
    uint16_t *dst;
    int x, y;

    if (s->mode) {
        for (x = 0; x < dst_w; x++) {
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
        for (y = 0; y < dst_h; y++) {
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

static void envelope_instant(WaveformContext *s, AVFrame *out, int plane, int component)
{
    const int dst_linesize = out->linesize[component];
    const uint8_t bg = s->bg_color[component];
    const int dst_h = out->height;
    const int dst_w = out->width;
    const int start = s->estart[plane];
    const int end = s->eend[plane];
    uint8_t *dst;
    int x, y;

    if (s->mode) {
        for (x = 0; x < dst_w; x++) {
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
        for (y = 0; y < dst_h; y++) {
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

static void envelope_peak16(WaveformContext *s, AVFrame *out, int plane, int component)
{
    const int dst_linesize = out->linesize[component] / 2;
    const int bg = s->bg_color[component] * (s->max / 256);
    const int limit = s->max - 1;
    const int dst_h = out->height;
    const int dst_w = out->width;
    const int start = s->estart[plane];
    const int end = s->eend[plane];
    int *emax = s->emax[plane][component];
    int *emin = s->emin[plane][component];
    uint16_t *dst;
    int x, y;

    if (s->mode) {
        for (x = 0; x < dst_w; x++) {
            for (y = start; y < end && y < emin[x]; y++) {
                dst = (uint16_t *)out->data[component] + y * dst_linesize + x;
                if (dst[0] != bg) {
                    emin[x] = y;
                    break;
                }
            }
            for (y = end - 1; y >= start && y >= emax[x]; y--) {
                dst = (uint16_t *)out->data[component] + y * dst_linesize + x;
                if (dst[0] != bg) {
                    emax[x] = y;
                    break;
                }
            }
        }

        if (s->envelope == 3)
            envelope_instant16(s, out, plane, component);

        for (x = 0; x < dst_w; x++) {
            dst = (uint16_t *)out->data[component] + emin[x] * dst_linesize + x;
            dst[0] = limit;
            dst = (uint16_t *)out->data[component] + emax[x] * dst_linesize + x;
            dst[0] = limit;
        }
    } else {
        for (y = 0; y < dst_h; y++) {
            dst = (uint16_t *)out->data[component] + y * dst_linesize;
            for (x = start; x < end && x < emin[y]; x++) {
                if (dst[x] != bg) {
                    emin[y] = x;
                    break;
                }
            }
            for (x = end - 1; x >= start && x >= emax[y]; x--) {
                if (dst[x] != bg) {
                    emax[y] = x;
                    break;
                }
            }
        }

        if (s->envelope == 3)
            envelope_instant16(s, out, plane, component);

        for (y = 0; y < dst_h; y++) {
            dst = (uint16_t *)out->data[component] + y * dst_linesize + emin[y];
            dst[0] = limit;
            dst = (uint16_t *)out->data[component] + y * dst_linesize + emax[y];
            dst[0] = limit;
        }
    }
}

static void envelope_peak(WaveformContext *s, AVFrame *out, int plane, int component)
{
    const int dst_linesize = out->linesize[component];
    const int bg = s->bg_color[component];
    const int dst_h = out->height;
    const int dst_w = out->width;
    const int start = s->estart[plane];
    const int end = s->eend[plane];
    int *emax = s->emax[plane][component];
    int *emin = s->emin[plane][component];
    uint8_t *dst;
    int x, y;

    if (s->mode) {
        for (x = 0; x < dst_w; x++) {
            for (y = start; y < end && y < emin[x]; y++) {
                dst = out->data[component] + y * dst_linesize + x;
                if (dst[0] != bg) {
                    emin[x] = y;
                    break;
                }
            }
            for (y = end - 1; y >= start && y >= emax[x]; y--) {
                dst = out->data[component] + y * dst_linesize + x;
                if (dst[0] != bg) {
                    emax[x] = y;
                    break;
                }
            }
        }

        if (s->envelope == 3)
            envelope_instant(s, out, plane, component);

        for (x = 0; x < dst_w; x++) {
            dst = out->data[component] + emin[x] * dst_linesize + x;
            dst[0] = 255;
            dst = out->data[component] + emax[x] * dst_linesize + x;
            dst[0] = 255;
        }
    } else {
        for (y = 0; y < dst_h; y++) {
            dst = out->data[component] + y * dst_linesize;
            for (x = start; x < end && x < emin[y]; x++) {
                if (dst[x] != bg) {
                    emin[y] = x;
                    break;
                }
            }
            for (x = end - 1; x >= start && x >= emax[y]; x--) {
                if (dst[x] != bg) {
                    emax[y] = x;
                    break;
                }
            }
        }

        if (s->envelope == 3)
            envelope_instant(s, out, plane, component);

        for (y = 0; y < dst_h; y++) {
            dst = out->data[component] + y * dst_linesize + emin[y];
            dst[0] = 255;
            dst = out->data[component] + y * dst_linesize + emax[y];
            dst[0] = 255;
        }
    }
}

static void envelope16(WaveformContext *s, AVFrame *out, int plane, int component)
{
    if (s->envelope == 0) {
        return;
    } else if (s->envelope == 1) {
        envelope_instant16(s, out, plane, component);
    } else {
        envelope_peak16(s, out, plane, component);
    }
}

static void envelope(WaveformContext *s, AVFrame *out, int plane, int component)
{
    if (s->envelope == 0) {
        return;
    } else if (s->envelope == 1) {
        envelope_instant(s, out, plane, component);
    } else {
        envelope_peak(s, out, plane, component);
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

static void lowpass16(WaveformContext *s, AVFrame *in, AVFrame *out,
                      int component, int intensity, int offset, int column)
{
    const int plane = s->desc->comp[component].plane;
    const int mirror = s->mirror;
    const int is_chroma = (component == 1 || component == 2);
    const int shift_w = (is_chroma ? s->desc->log2_chroma_w : 0);
    const int shift_h = (is_chroma ? s->desc->log2_chroma_h : 0);
    const int src_linesize = in->linesize[plane] / 2;
    const int dst_linesize = out->linesize[plane] / 2;
    const int dst_signed_linesize = dst_linesize * (mirror == 1 ? -1 : 1);
    const int limit = s->max - 1;
    const int max = limit - intensity;
    const int src_h = AV_CEIL_RSHIFT(in->height, shift_h);
    const int src_w = AV_CEIL_RSHIFT(in->width, shift_w);
    const uint16_t *src_data = (const uint16_t *)in->data[plane];
    uint16_t *dst_data = (uint16_t *)out->data[plane] + (column ? offset * dst_linesize : offset);
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

    envelope16(s, out, plane, plane);
}

static void lowpass(WaveformContext *s, AVFrame *in, AVFrame *out,
                    int component, int intensity, int offset, int column)
{
    const int plane = s->desc->comp[component].plane;
    const int mirror = s->mirror;
    const int is_chroma = (component == 1 || component == 2);
    const int shift_w = (is_chroma ? s->desc->log2_chroma_w : 0);
    const int shift_h = (is_chroma ? s->desc->log2_chroma_h : 0);
    const int src_linesize = in->linesize[plane];
    const int dst_linesize = out->linesize[plane];
    const int dst_signed_linesize = dst_linesize * (mirror == 1 ? -1 : 1);
    const int max = 255 - intensity;
    const int src_h = AV_CEIL_RSHIFT(in->height, shift_h);
    const int src_w = AV_CEIL_RSHIFT(in->width, shift_w);
    const uint8_t *src_data = in->data[plane];
    uint8_t *dst_data = out->data[plane] + (column ? offset * dst_linesize : offset);
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
            int i = 0;
            uint8_t *target;
            if (column) {
                do {
                    target = dst++ + dst_signed_linesize * *p;
                    update(target, max, intensity);
                } while (++i < step);
            } else {
                uint8_t *row = dst_data;
                do {
                    if (mirror)
                        target = row - *p - 1;
                    else
                        target = row + *p;
                    update(target, max, intensity);
                    row += dst_linesize;
                } while (++i < step);
            }
        }
        src_data += src_linesize;
        dst_data += dst_linesize * step;
    }

    envelope(s, out, plane, plane);
}

static void flat(WaveformContext *s, AVFrame *in, AVFrame *out,
                 int component, int intensity, int offset, int column)
{
    const int plane = s->desc->comp[component].plane;
    const int mirror = s->mirror;
    const int c0_linesize = in->linesize[ plane + 0 ];
    const int c1_linesize = in->linesize[(plane + 1) % s->ncomp];
    const int c2_linesize = in->linesize[(plane + 2) % s->ncomp];
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
            uint8_t *d0_data = out->data[plane] + offset * d0_linesize;
            uint8_t *d1_data = out->data[(plane + 1) % s->ncomp] + offset * d1_linesize;
            uint8_t * const d0_bottom_line = d0_data + d0_linesize * (s->size - 1);
            uint8_t * const d0 = (mirror ? d0_bottom_line : d0_data);
            uint8_t * const d1_bottom_line = d1_data + d1_linesize * (s->size - 1);
            uint8_t * const d1 = (mirror ? d1_bottom_line : d1_data);

            for (y = 0; y < src_h; y++) {
                const int c0 = c0_data[x] + 256;
                const int c1 = FFABS(c1_data[x] - 128) + FFABS(c2_data[x] - 128);
                uint8_t *target;

                target = d0 + x + d0_signed_linesize * c0;
                update(target, max, intensity);
                target = d1 + x + d1_signed_linesize * (c0 - c1);
                update(target, max, intensity);
                target = d1 + x + d1_signed_linesize * (c0 + c1);
                update(target, max, intensity);

                c0_data += c0_linesize;
                c1_data += c1_linesize;
                c2_data += c2_linesize;
                d0_data += d0_linesize;
                d1_data += d1_linesize;
            }
        }
    } else {
        const uint8_t *c0_data = in->data[plane];
        const uint8_t *c1_data = in->data[(plane + 1) % s->ncomp];
        const uint8_t *c2_data = in->data[(plane + 2) % s->ncomp];
        uint8_t *d0_data = out->data[plane] + offset;
        uint8_t *d1_data = out->data[(plane + 1) % s->ncomp] + offset;

        if (mirror) {
            d0_data += s->size - 1;
            d1_data += s->size - 1;
        }

        for (y = 0; y < src_h; y++) {
            for (x = 0; x < src_w; x++) {
                int c0 = c0_data[x] + 256;
                const int c1 = FFABS(c1_data[x] - 128) + FFABS(c2_data[x] - 128);
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

            c0_data += c0_linesize;
            c1_data += c1_linesize;
            c2_data += c2_linesize;
            d0_data += d0_linesize;
            d1_data += d1_linesize;
        }
    }

    envelope(s, out, plane, plane);
    envelope(s, out, plane, (plane + 1) % s->ncomp);
}

static void aflat(WaveformContext *s, AVFrame *in, AVFrame *out,
                  int component, int intensity, int offset, int column)
{
    const int plane = s->desc->comp[component].plane;
    const int mirror = s->mirror;
    const int c0_linesize = in->linesize[ plane + 0 ];
    const int c1_linesize = in->linesize[(plane + 1) % s->ncomp];
    const int c2_linesize = in->linesize[(plane + 2) % s->ncomp];
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
            uint8_t *d0_data = out->data[plane] + offset * d0_linesize;
            uint8_t *d1_data = out->data[(plane + 1) % s->ncomp] + offset * d1_linesize;
            uint8_t *d2_data = out->data[(plane + 2) % s->ncomp] + offset * d2_linesize;
            uint8_t * const d0_bottom_line = d0_data + d0_linesize * (s->size - 1);
            uint8_t * const d0 = (mirror ? d0_bottom_line : d0_data);
            uint8_t * const d1_bottom_line = d1_data + d1_linesize * (s->size - 1);
            uint8_t * const d1 = (mirror ? d1_bottom_line : d1_data);
            uint8_t * const d2_bottom_line = d2_data + d2_linesize * (s->size - 1);
            uint8_t * const d2 = (mirror ? d2_bottom_line : d2_data);

            for (y = 0; y < src_h; y++) {
                const int c0 = c0_data[x] + 128;
                const int c1 = c1_data[x] - 128;
                const int c2 = c2_data[x] - 128;
                uint8_t *target;

                target = d0 + x + d0_signed_linesize * c0;
                update(target, max, intensity);

                target = d1 + x + d1_signed_linesize * (c0 + c1);
                update(target, max, intensity);

                target = d2 + x + d2_signed_linesize * (c0 + c2);
                update(target, max, intensity);

                c0_data += c0_linesize;
                c1_data += c1_linesize;
                c2_data += c2_linesize;
                d0_data += d0_linesize;
                d1_data += d1_linesize;
                d2_data += d2_linesize;
            }
        }
    } else {
        const uint8_t *c0_data = in->data[plane];
        const uint8_t *c1_data = in->data[(plane + 1) % s->ncomp];
        const uint8_t *c2_data = in->data[(plane + 2) % s->ncomp];
        uint8_t *d0_data = out->data[plane] + offset;
        uint8_t *d1_data = out->data[(plane + 1) % s->ncomp] + offset;
        uint8_t *d2_data = out->data[(plane + 2) % s->ncomp] + offset;

        if (mirror) {
            d0_data += s->size - 1;
            d1_data += s->size - 1;
            d2_data += s->size - 1;
        }

        for (y = 0; y < src_h; y++) {
            for (x = 0; x < src_w; x++) {
                const int c0 = c0_data[x] + 128;
                const int c1 = c1_data[x] - 128;
                const int c2 = c2_data[x] - 128;
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

            c0_data += c0_linesize;
            c1_data += c1_linesize;
            c2_data += c2_linesize;
            d0_data += d0_linesize;
            d1_data += d1_linesize;
            d2_data += d2_linesize;
        }
    }

    envelope(s, out, plane, (plane + 0) % s->ncomp);
    envelope(s, out, plane, (plane + 1) % s->ncomp);
    envelope(s, out, plane, (plane + 2) % s->ncomp);
}

static void chroma(WaveformContext *s, AVFrame *in, AVFrame *out,
                   int component, int intensity, int offset, int column)
{
    const int plane = s->desc->comp[component].plane;
    const int mirror = s->mirror;
    const int c0_linesize = in->linesize[(plane + 1) % s->ncomp];
    const int c1_linesize = in->linesize[(plane + 2) % s->ncomp];
    const int dst_linesize = out->linesize[plane];
    const int max = 255 - intensity;
    const int src_h = in->height;
    const int src_w = in->width;
    int x, y;

    if (column) {
        const int dst_signed_linesize = dst_linesize * (mirror == 1 ? -1 : 1);

        for (x = 0; x < src_w; x++) {
            const uint8_t *c0_data = in->data[(plane + 1) % s->ncomp];
            const uint8_t *c1_data = in->data[(plane + 2) % s->ncomp];
            uint8_t *dst_data = out->data[plane] + offset * dst_linesize;
            uint8_t * const dst_bottom_line = dst_data + dst_linesize * (s->size - 1);
            uint8_t * const dst_line = (mirror ? dst_bottom_line : dst_data);
            uint8_t *dst = dst_line;

            for (y = 0; y < src_h; y++) {
                const int sum = FFABS(c0_data[x] - 128) + FFABS(c1_data[x] - 128);
                uint8_t *target;

                target = dst + x + dst_signed_linesize * (256 - sum);
                update(target, max, intensity);
                target = dst + x + dst_signed_linesize * (255 + sum);
                update(target, max, intensity);

                c0_data += c0_linesize;
                c1_data += c1_linesize;
                dst_data += dst_linesize;
            }
        }
    } else {
        const uint8_t *c0_data = in->data[(plane + 1) % s->ncomp];
        const uint8_t *c1_data = in->data[(plane + 2) % s->ncomp];
        uint8_t *dst_data = out->data[plane] + offset;

        if (mirror)
            dst_data += s->size - 1;
        for (y = 0; y < src_h; y++) {
            for (x = 0; x < src_w; x++) {
                const int sum = FFABS(c0_data[x] - 128) + FFABS(c1_data[x] - 128);
                uint8_t *target;

                if (mirror) {
                    target = dst_data - (256 - sum);
                    update(target, max, intensity);
                    target = dst_data - (255 + sum);
                    update(target, max, intensity);
                } else {
                    target = dst_data + (256 - sum);
                    update(target, max, intensity);
                    target = dst_data + (255 + sum);
                    update(target, max, intensity);
                }
            }

            c0_data += c0_linesize;
            c1_data += c1_linesize;
            dst_data += dst_linesize;
        }
    }

    envelope(s, out, plane, (plane + 0) % s->ncomp);
}

static void achroma(WaveformContext *s, AVFrame *in, AVFrame *out,
                    int component, int intensity, int offset, int column)
{
    const int plane = s->desc->comp[component].plane;
    const int mirror = s->mirror;
    const int c1_linesize = in->linesize[(plane + 1) % s->ncomp];
    const int c2_linesize = in->linesize[(plane + 2) % s->ncomp];
    const int d1_linesize = out->linesize[(plane + 1) % s->ncomp];
    const int d2_linesize = out->linesize[(plane + 2) % s->ncomp];
    const int max = 255 - intensity;
    const int src_h = in->height;
    const int src_w = in->width;
    int x, y;

    if (column) {
        const int d1_signed_linesize = d1_linesize * (mirror == 1 ? -1 : 1);
        const int d2_signed_linesize = d2_linesize * (mirror == 1 ? -1 : 1);

        for (x = 0; x < src_w; x++) {
            const uint8_t *c1_data = in->data[(plane + 1) % s->ncomp];
            const uint8_t *c2_data = in->data[(plane + 2) % s->ncomp];
            uint8_t *d1_data = out->data[(plane + 1) % s->ncomp] + offset * d1_linesize;
            uint8_t *d2_data = out->data[(plane + 2) % s->ncomp] + offset * d2_linesize;
            uint8_t * const d1_bottom_line = d1_data + d1_linesize * (s->size - 1);
            uint8_t * const d1 = (mirror ? d1_bottom_line : d1_data);
            uint8_t * const d2_bottom_line = d2_data + d2_linesize * (s->size - 1);
            uint8_t * const d2 = (mirror ? d2_bottom_line : d2_data);

            for (y = 0; y < src_h; y++) {
                const int c1 = c1_data[x] - 128;
                const int c2 = c2_data[x] - 128;
                uint8_t *target;

                target = d1 + x + d1_signed_linesize * (128 + c1);
                update(target, max, intensity);

                target = d2 + x + d2_signed_linesize * (128 + c2);
                update(target, max, intensity);

                c1_data += c1_linesize;
                c2_data += c2_linesize;
                d1_data += d1_linesize;
                d2_data += d2_linesize;
            }
        }
    } else {
        const uint8_t *c1_data = in->data[(plane + 1) % s->ncomp];
        const uint8_t *c2_data = in->data[(plane + 2) % s->ncomp];
        uint8_t *d0_data = out->data[plane] + offset;
        uint8_t *d1_data = out->data[(plane + 1) % s->ncomp] + offset;
        uint8_t *d2_data = out->data[(plane + 2) % s->ncomp] + offset;

        if (mirror) {
            d0_data += s->size - 1;
            d1_data += s->size - 1;
            d2_data += s->size - 1;
        }

        for (y = 0; y < src_h; y++) {
            for (x = 0; x < src_w; x++) {
                const int c1 = c1_data[x] - 128;
                const int c2 = c2_data[x] - 128;
                uint8_t *target;

                if (mirror) {
                    target = d1_data - (128 + c1);
                    update(target, max, intensity);
                    target = d2_data - (128 + c2);
                    update(target, max, intensity);
                } else {
                    target = d1_data + (128 + c1);
                    update(target, max, intensity);
                    target = d2_data + (128 + c2);
                    update(target, max, intensity);
                }
            }

            c1_data += c1_linesize;
            c2_data += c2_linesize;
            d1_data += d1_linesize;
            d2_data += d2_linesize;
        }
    }

    envelope(s, out, plane, (plane + 1) % s->ncomp);
    envelope(s, out, plane, (plane + 2) % s->ncomp);
}

static void color16(WaveformContext *s, AVFrame *in, AVFrame *out,
                    int component, int intensity, int offset, int column)
{
    const int plane = s->desc->comp[component].plane;
    const int mirror = s->mirror;
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
    const int src_h = in->height;
    const int src_w = in->width;
    int x, y;

    if (s->mode) {
        const int d0_signed_linesize = d0_linesize * (mirror == 1 ? -1 : 1);
        const int d1_signed_linesize = d1_linesize * (mirror == 1 ? -1 : 1);
        const int d2_signed_linesize = d2_linesize * (mirror == 1 ? -1 : 1);
        uint16_t *d0_data = (uint16_t *)out->data[plane] + offset * d0_linesize;
        uint16_t *d1_data = (uint16_t *)out->data[(plane + 1) % s->ncomp] + offset * d1_linesize;
        uint16_t *d2_data = (uint16_t *)out->data[(plane + 2) % s->ncomp] + offset * d2_linesize;
        uint16_t * const d0_bottom_line = d0_data + d0_linesize * (s->size - 1);
        uint16_t * const d0 = (mirror ? d0_bottom_line : d0_data);
        uint16_t * const d1_bottom_line = d1_data + d1_linesize * (s->size - 1);
        uint16_t * const d1 = (mirror ? d1_bottom_line : d1_data);
        uint16_t * const d2_bottom_line = d2_data + d2_linesize * (s->size - 1);
        uint16_t * const d2 = (mirror ? d2_bottom_line : d2_data);

        for (y = 0; y < src_h; y++) {
            for (x = 0; x < src_w; x++) {
                const int c0 = FFMIN(c0_data[x], limit);
                const int c1 = c1_data[x];
                const int c2 = c2_data[x];

                *(d0 + d0_signed_linesize * c0 + x) = c0;
                *(d1 + d1_signed_linesize * c0 + x) = c1;
                *(d2 + d2_signed_linesize * c0 + x) = c2;
            }

            c0_data += c0_linesize;
            c1_data += c1_linesize;
            c2_data += c2_linesize;
            d0_data += d0_linesize;
            d1_data += d1_linesize;
            d2_data += d2_linesize;
        }
    } else {
        uint16_t *d0_data = (uint16_t *)out->data[plane] + offset;
        uint16_t *d1_data = (uint16_t *)out->data[(plane + 1) % s->ncomp] + offset;
        uint16_t *d2_data = (uint16_t *)out->data[(plane + 2) % s->ncomp] + offset;

        if (mirror) {
            d0_data += s->size - 1;
            d1_data += s->size - 1;
            d2_data += s->size - 1;
        }

        for (y = 0; y < src_h; y++) {
            for (x = 0; x < src_w; x++) {
                const int c0 = FFMIN(c0_data[x], limit);
                const int c1 = c1_data[x];
                const int c2 = c2_data[x];

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

            c0_data += c0_linesize;
            c1_data += c1_linesize;
            c2_data += c2_linesize;
            d0_data += d0_linesize;
            d1_data += d1_linesize;
            d2_data += d2_linesize;
        }
    }

    envelope16(s, out, plane, plane);
}

static void color(WaveformContext *s, AVFrame *in, AVFrame *out,
                  int component, int intensity, int offset, int column)
{
    const int plane = s->desc->comp[component].plane;
    const int mirror = s->mirror;
    const uint8_t *c0_data = in->data[plane + 0];
    const uint8_t *c1_data = in->data[(plane + 1) % s->ncomp];
    const uint8_t *c2_data = in->data[(plane + 2) % s->ncomp];
    const int c0_linesize = in->linesize[ plane + 0 ];
    const int c1_linesize = in->linesize[(plane + 1) % s->ncomp];
    const int c2_linesize = in->linesize[(plane + 2) % s->ncomp];
    const int d0_linesize = out->linesize[ plane + 0 ];
    const int d1_linesize = out->linesize[(plane + 1) % s->ncomp];
    const int d2_linesize = out->linesize[(plane + 2) % s->ncomp];
    const int src_h = in->height;
    const int src_w = in->width;
    int x, y;

    if (s->mode) {
        const int d0_signed_linesize = d0_linesize * (mirror == 1 ? -1 : 1);
        const int d1_signed_linesize = d1_linesize * (mirror == 1 ? -1 : 1);
        const int d2_signed_linesize = d2_linesize * (mirror == 1 ? -1 : 1);
        uint8_t *d0_data = out->data[plane] + offset * d0_linesize;
        uint8_t *d1_data = out->data[(plane + 1) % s->ncomp] + offset * d1_linesize;
        uint8_t *d2_data = out->data[(plane + 2) % s->ncomp] + offset * d2_linesize;
        uint8_t * const d0_bottom_line = d0_data + d0_linesize * (s->size - 1);
        uint8_t * const d0 = (mirror ? d0_bottom_line : d0_data);
        uint8_t * const d1_bottom_line = d1_data + d1_linesize * (s->size - 1);
        uint8_t * const d1 = (mirror ? d1_bottom_line : d1_data);
        uint8_t * const d2_bottom_line = d2_data + d2_linesize * (s->size - 1);
        uint8_t * const d2 = (mirror ? d2_bottom_line : d2_data);

        for (y = 0; y < src_h; y++) {
            for (x = 0; x < src_w; x++) {
                const int c0 = c0_data[x];
                const int c1 = c1_data[x];
                const int c2 = c2_data[x];

                *(d0 + d0_signed_linesize * c0 + x) = c0;
                *(d1 + d1_signed_linesize * c0 + x) = c1;
                *(d2 + d2_signed_linesize * c0 + x) = c2;
            }

            c0_data += c0_linesize;
            c1_data += c1_linesize;
            c2_data += c2_linesize;
            d0_data += d0_linesize;
            d1_data += d1_linesize;
            d2_data += d2_linesize;
        }
    } else {
        uint8_t *d0_data = out->data[plane] + offset;
        uint8_t *d1_data = out->data[(plane + 1) % s->ncomp] + offset;
        uint8_t *d2_data = out->data[(plane + 2) % s->ncomp] + offset;

        if (mirror) {
            d0_data += s->size - 1;
            d1_data += s->size - 1;
            d2_data += s->size - 1;
        }

        for (y = 0; y < src_h; y++) {
            for (x = 0; x < src_w; x++) {
                const int c0 = c0_data[x];
                const int c1 = c1_data[x];
                const int c2 = c2_data[x];

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

            c0_data += c0_linesize;
            c1_data += c1_linesize;
            c2_data += c2_linesize;
            d0_data += d0_linesize;
            d1_data += d1_linesize;
            d2_data += d2_linesize;
        }
    }

    envelope(s, out, plane, plane);
}

static const uint8_t black_yuva_color[4] = { 0, 127, 127, 255 };
static const uint8_t green_yuva_color[4] = { 255, 0, 0, 255 };
static const uint8_t black_gbrp_color[4] = { 0, 0, 0, 255 };

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
    const int step = s->flags & 2 + 1;
    const float o1 = s->opacity;
    const float o2 = 1. - o1;
    int k = 0, c, p, l, offset = 0;

    for (c = 0; c < s->ncomp; c++) {
        if (!((1 << c) & s->pcomp) || (!s->display && k > 0))
            continue;

        k++;
        for (p = 0; p < s->ncomp; p++) {
            const int v = green_yuva_color[p];
            for (l = 0; l < s->nb_glines; l++) {
                const uint16_t pos = s->glines[l].line[c].pos;
                int x = offset + (s->mirror ? 255 - pos : pos);
                uint8_t *dst = out->data[p] + x;

                blend_vline(dst, out->height, out->linesize[p], o1, o2, v, step);
            }
        }

        for (l = 0; l < s->nb_glines && (s->flags & 1); l++) {
            const char *name = s->glines[l].line[c].name;
            const uint16_t pos = s->glines[l].line[c].pos;
            int x = offset + (s->mirror ? 255 - pos : pos) - 10;

            if (x < 0)
                x = 4;

            draw_vtext(out, x, 2, o1, o2, name, green_yuva_color);
        }

        offset += 256 * s->display;
    }
}

static void graticule16_green_row(WaveformContext *s, AVFrame *out)
{
    const int step = s->flags & 2 + 1;
    const float o1 = s->opacity;
    const float o2 = 1. - o1;
    const int mult = s->size / 256;
    int k = 0, c, p, l, offset = 0;

    for (c = 0; c < s->ncomp; c++) {
        if (!((1 << c) & s->pcomp) || (!s->display && k > 0))
            continue;

        k++;
        for (p = 0; p < s->ncomp; p++) {
            const int v = green_yuva_color[p] * mult;
            for (l = 0; l < s->nb_glines ; l++) {
                const uint16_t pos = s->glines[l].line[c].pos;
                int x = offset + (s->mirror ? s->size - 1 - pos : pos);
                uint16_t *dst = (uint16_t *)(out->data[p]) + x;

                blend_vline16(dst, out->height, out->linesize[p], o1, o2, v, step);
            }
        }

        for (l = 0; l < s->nb_glines && (s->flags & 1); l++) {
            const char *name = s->glines[l].line[c].name;
            const uint16_t pos = s->glines[l].line[c].pos;
            int x = offset + (s->mirror ? s->size - 1 - pos : pos) - 10;

            if (x < 0)
                x = 4;

            draw_vtext16(out, x, 2, mult, o1, o2, name, green_yuva_color);
        }

        offset += s->size * s->display;
    }
}

static void graticule_green_column(WaveformContext *s, AVFrame *out)
{
    const int step = s->flags & 2 + 1;
    const float o1 = s->opacity;
    const float o2 = 1. - o1;
    int k = 0, c, p, l, offset = 0;

    for (c = 0; c < s->ncomp; c++) {
        if ((!((1 << c) & s->pcomp) || (!s->display && k > 0)))
            continue;

        k++;
        for (p = 0; p < s->ncomp; p++) {
            const int v = green_yuva_color[p];
            for (l = 0; l < s->nb_glines ; l++) {
                const uint16_t pos = s->glines[l].line[c].pos;
                int y = offset + (s->mirror ? 255 - pos : pos);
                uint8_t *dst = out->data[p] + y * out->linesize[p];

                blend_hline(dst, out->width, o1, o2, v, step);
            }
        }

        for (l = 0; l < s->nb_glines && (s->flags & 1); l++) {
            const char *name = s->glines[l].line[c].name;
            const uint16_t pos = s->glines[l].line[c].pos;
            int y = offset + (s->mirror ? 255 - pos : pos) - 10;

            if (y < 0)
                y = 4;

            draw_htext(out, 2, y, o1, o2, name, green_yuva_color);
        }

        offset += 256 * s->display;
    }
}

static void graticule16_green_column(WaveformContext *s, AVFrame *out)
{
    const int step = s->flags & 2 + 1;
    const float o1 = s->opacity;
    const float o2 = 1. - o1;
    const int mult = s->size / 256;
    int k = 0, c, p, l, offset = 0;

    for (c = 0; c < s->ncomp; c++) {
        if ((!((1 << c) & s->pcomp) || (!s->display && k > 0)))
            continue;

        k++;
        for (p = 0; p < s->ncomp; p++) {
            const int v = green_yuva_color[p] * mult;
            for (l = 0; l < s->nb_glines ; l++) {
                const uint16_t pos = s->glines[l].line[c].pos;
                int y = offset + (s->mirror ? s->size - 1 - pos : pos);
                uint16_t *dst = (uint16_t *)(out->data[p] + y * out->linesize[p]);

                blend_hline16(dst, out->width, o1, o2, v, step);
            }
        }

        for (l = 0; l < s->nb_glines && (s->flags & 1); l++) {
            const char *name = s->glines[l].line[c].name;
            const uint16_t pos = s->glines[l].line[c].pos;
            int y = offset + (s->mirror ? s->size - 1 - pos: pos) - 10;

            if (y < 0)
                y = 4;

            draw_htext16(out, 2, y, mult, o1, o2, name, green_yuva_color);
        }

        offset += s->size * s->display;
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

    s->graticulef = graticule_none;

    switch (s->filter) {
    case LOWPASS:
            s->size = 256;
            if (s->graticule && s->mode == 1)
                s->graticulef = s->bits > 8 ? graticule16_green_column : graticule_green_column;
            else if (s->graticule && s->mode == 0)
                s->graticulef = s->bits > 8 ? graticule16_green_row : graticule_green_row;
            s->waveform = s->bits > 8 ? lowpass16 : lowpass; break;
    case FLAT:
            s->size = 256 * 3;
            s->waveform = flat;    break;
    case AFLAT:
            s->size = 256 * 2;
            s->waveform = aflat;   break;
    case CHROMA:
            s->size = 256 * 2;
            s->waveform = chroma;  break;
    case ACHROMA:
            s->size = 256;
            s->waveform = achroma; break;
    case COLOR:
            s->size = 256;
            if (s->graticule && s->mode == 1)
                s->graticulef = s->bits > 8 ? graticule16_green_column : graticule_green_column;
            else if (s->graticule && s->mode == 0)
                s->graticulef = s->bits > 8 ? graticule16_green_row : graticule_green_row;
            s->waveform = s->bits > 8 ?   color16 :   color; break;
    }

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

    s->size = s->size << (s->bits - 8);

    switch (inlink->format) {
    case AV_PIX_FMT_GBRAP:
    case AV_PIX_FMT_GBRP:
    case AV_PIX_FMT_GBRP9:
    case AV_PIX_FMT_GBRP10:
    case AV_PIX_FMT_GBRP12:
        s->bg_color = black_gbrp_color;
        s->graticulef = graticule_none;
        break;
    default:
        s->bg_color = black_yuva_color;
    }

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

    av_freep(&s->peak);

    if (s->mode) {
        outlink->h = s->size * FFMAX(comp * s->display, 1);
        size = inlink->w;
    } else {
        outlink->w = s->size * FFMAX(comp * s->display, 1);
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

        offset = j++ * s->size * s->display;
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

    for (k = 0; k < s->ncomp; k++) {
        if (s->bits <= 8) {
            for (i = 0; i < outlink->h ; i++)
                memset(out->data[s->desc->comp[k].plane] +
                       i * out->linesize[s->desc->comp[k].plane],
                       s->bg_color[k], outlink->w);
        } else {
            const int mult = s->size / 256;
            uint16_t *dst = (uint16_t *)out->data[s->desc->comp[k].plane];

            for (i = 0; i < outlink->h ; i++) {
                for (j = 0; j < outlink->w; j++)
                    dst[j] = s->bg_color[k] * mult;
                dst += out->linesize[s->desc->comp[k].plane] / 2;
            }
        }
    }

    for (k = 0, i = 0; k < s->ncomp; k++) {
        if ((1 << k) & s->pcomp) {
            const int offset = i++ * s->size * s->display;
            s->waveform(s, in, out, k, s->intensity, offset, s->mode);
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
