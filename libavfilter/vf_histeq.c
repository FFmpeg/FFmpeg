/*
 * Copyright (c) 2012 Jeremy Tran
 * Copyright (c) 2001 Donald A. Graft
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * @file
 * Histogram equalization filter, based on the VirtualDub filter by
 * Donald A. Graft  <neuron2 AT home DOT com>.
 * Implements global automatic contrast adjustment by means of
 * histogram equalization.
 */

#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "drawutils.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

// #define DEBUG

// Linear Congruential Generator, see "Numerical Recipes"
#define LCG_A 4096
#define LCG_C 150889
#define LCG_M 714025
#define LCG(x) (((x) * LCG_A + LCG_C) % LCG_M)
#define LCG_SEED 739187

enum HisteqAntibanding {
    HISTEQ_ANTIBANDING_NONE   = 0,
    HISTEQ_ANTIBANDING_WEAK   = 1,
    HISTEQ_ANTIBANDING_STRONG = 2,
    HISTEQ_ANTIBANDING_NB,
};

typedef struct {
    const AVClass *class;
    float strength;
    float intensity;
    int antibanding;               ///< HisteqAntibanding
    int in_histogram [256];        ///< input histogram
    int out_histogram[256];        ///< output histogram
    int LUT[256];                  ///< lookup table derived from histogram[]
    uint8_t rgba_map[4];           ///< components position
    int bpp;                       ///< bytes per pixel
} HisteqContext;

#define OFFSET(x) offsetof(HisteqContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define CONST(name, help, val, unit) { name, help, 0, AV_OPT_TYPE_CONST, {.i64=val}, INT_MIN, INT_MAX, FLAGS, unit }

static const AVOption histeq_options[] = {
    { "strength",    "set the strength", OFFSET(strength), AV_OPT_TYPE_FLOAT, {.dbl=0.2}, 0, 1, FLAGS },
    { "intensity",   "set the intensity", OFFSET(intensity), AV_OPT_TYPE_FLOAT, {.dbl=0.21}, 0, 1, FLAGS },
    { "antibanding", "set the antibanding level", OFFSET(antibanding), AV_OPT_TYPE_INT, {.i64=HISTEQ_ANTIBANDING_NONE}, 0, HISTEQ_ANTIBANDING_NB-1, FLAGS, "antibanding" },
    CONST("none",    "apply no antibanding",     HISTEQ_ANTIBANDING_NONE,   "antibanding"),
    CONST("weak",    "apply weak antibanding",   HISTEQ_ANTIBANDING_WEAK,   "antibanding"),
    CONST("strong",  "apply strong antibanding", HISTEQ_ANTIBANDING_STRONG, "antibanding"),
    { NULL }
};

AVFILTER_DEFINE_CLASS(histeq);

static av_cold int init(AVFilterContext *ctx)
{
    HisteqContext *histeq = ctx->priv;

    av_log(ctx, AV_LOG_VERBOSE,
           "strength:%0.3f intensity:%0.3f antibanding:%d\n",
           histeq->strength, histeq->intensity, histeq->antibanding);

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_ARGB, AV_PIX_FMT_RGBA, AV_PIX_FMT_ABGR, AV_PIX_FMT_BGRA,
        AV_PIX_FMT_RGB24, AV_PIX_FMT_BGR24,
        AV_PIX_FMT_NONE
    };
    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    HisteqContext *histeq = ctx->priv;
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(inlink->format);

    histeq->bpp = av_get_bits_per_pixel(pix_desc) / 8;
    ff_fill_rgba_map(histeq->rgba_map, inlink->format);

    return 0;
}

#define R 0
#define G 1
#define B 2
#define A 3

#define GET_RGB_VALUES(r, g, b, src, map) do { \
    r = src[x + map[R]];                       \
    g = src[x + map[G]];                       \
    b = src[x + map[B]];                       \
} while (0)

static int filter_frame(AVFilterLink *inlink, AVFrame *inpic)
{
    AVFilterContext   *ctx     = inlink->dst;
    HisteqContext     *histeq  = ctx->priv;
    AVFilterLink      *outlink = ctx->outputs[0];
    int strength  = histeq->strength  * 1000;
    int intensity = histeq->intensity * 1000;
    int x, y, i, luthi, lutlo, lut, luma, oluma, m;
    AVFrame *outpic;
    unsigned int r, g, b, jran;
    uint8_t *src, *dst;

    outpic = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!outpic) {
        av_frame_free(&inpic);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(outpic, inpic);

    /* Seed random generator for antibanding. */
    jran = LCG_SEED;

    /* Calculate and store the luminance and calculate the global histogram
       based on the luminance. */
    memset(histeq->in_histogram, 0, sizeof(histeq->in_histogram));
    src = inpic->data[0];
    dst = outpic->data[0];
    for (y = 0; y < inlink->h; y++) {
        for (x = 0; x < inlink->w * histeq->bpp; x += histeq->bpp) {
            GET_RGB_VALUES(r, g, b, src, histeq->rgba_map);
            luma = (55 * r + 182 * g + 19 * b) >> 8;
            dst[x + histeq->rgba_map[A]] = luma;
            histeq->in_histogram[luma]++;
        }
        src += inpic->linesize[0];
        dst += outpic->linesize[0];
    }

#ifdef DEBUG
    for (x = 0; x < 256; x++)
        av_dlog(ctx, "in[%d]: %u\n", x, histeq->in_histogram[x]);
#endif

    /* Calculate the lookup table. */
    histeq->LUT[0] = histeq->in_histogram[0];
    /* Accumulate */
    for (x = 1; x < 256; x++)
        histeq->LUT[x] = histeq->LUT[x-1] + histeq->in_histogram[x];

    /* Normalize */
    for (x = 0; x < 256; x++)
        histeq->LUT[x] = (histeq->LUT[x] * intensity) / (inlink->h * inlink->w);

    /* Adjust the LUT based on the selected strength. This is an alpha
       mix of the calculated LUT and a linear LUT with gain 1. */
    for (x = 0; x < 256; x++)
        histeq->LUT[x] = (strength * histeq->LUT[x]) / 255 +
                         ((255 - strength) * x)      / 255;

    /* Output the equalized frame. */
    memset(histeq->out_histogram, 0, sizeof(histeq->out_histogram));

    src = inpic->data[0];
    dst = outpic->data[0];
    for (y = 0; y < inlink->h; y++) {
        for (x = 0; x < inlink->w * histeq->bpp; x += histeq->bpp) {
            luma = dst[x + histeq->rgba_map[A]];
            if (luma == 0) {
                for (i = 0; i < histeq->bpp; ++i)
                    dst[x + i] = 0;
                histeq->out_histogram[0]++;
            } else {
                lut = histeq->LUT[luma];
                if (histeq->antibanding != HISTEQ_ANTIBANDING_NONE) {
                    if (luma > 0) {
                        lutlo = histeq->antibanding == HISTEQ_ANTIBANDING_WEAK ?
                                (histeq->LUT[luma] + histeq->LUT[luma - 1]) / 2 :
                                 histeq->LUT[luma - 1];
                    } else
                        lutlo = lut;

                    if (luma < 255) {
                        luthi = (histeq->antibanding == HISTEQ_ANTIBANDING_WEAK) ?
                            (histeq->LUT[luma] + histeq->LUT[luma + 1]) / 2 :
                             histeq->LUT[luma + 1];
                    } else
                        luthi = lut;

                    if (lutlo != luthi) {
                        jran = LCG(jran);
                        lut = lutlo + ((luthi - lutlo + 1) * jran) / LCG_M;
                    }
                }

                GET_RGB_VALUES(r, g, b, src, histeq->rgba_map);
                if (((m = FFMAX3(r, g, b)) * lut) / luma > 255) {
                    r = (r * 255) / m;
                    g = (g * 255) / m;
                    b = (b * 255) / m;
                } else {
                    r = (r * lut) / luma;
                    g = (g * lut) / luma;
                    b = (b * lut) / luma;
                }
                dst[x + histeq->rgba_map[R]] = r;
                dst[x + histeq->rgba_map[G]] = g;
                dst[x + histeq->rgba_map[B]] = b;
                oluma = av_clip_uint8((55 * r + 182 * g + 19 * b) >> 8);
                histeq->out_histogram[oluma]++;
            }
        }
        src += inpic->linesize[0];
        dst += outpic->linesize[0];
    }
#ifdef DEBUG
    for (x = 0; x < 256; x++)
        av_dlog(ctx, "out[%d]: %u\n", x, histeq->out_histogram[x]);
#endif

    av_frame_free(&inpic);
    return ff_filter_frame(outlink, outpic);
}

static const AVFilterPad histeq_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad histeq_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_histeq = {
    .name          = "histeq",
    .description   = NULL_IF_CONFIG_SMALL("Apply global color histogram equalization."),
    .priv_size     = sizeof(HisteqContext),
    .init          = init,
    .query_formats = query_formats,
    .inputs        = histeq_inputs,
    .outputs       = histeq_outputs,
    .priv_class    = &histeq_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
