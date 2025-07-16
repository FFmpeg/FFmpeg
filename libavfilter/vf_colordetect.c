/*
 * Copyright (c) 2025 Niklas Haas
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

/**
 * @file
 * Video color space detector, tries to auto-detect YUV range and alpha mode.
 */

#include <stdbool.h>
#include <stdatomic.h>

#include "config.h"

#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "video.h"

#include "vf_colordetect.h"

enum AlphaMode {
    ALPHA_NONE = -1,
    ALPHA_UNDETERMINED = 0,
    ALPHA_STRAIGHT,
    /* No way to positively identify premultiplied alpha */
};

enum ColorDetectMode {
    COLOR_DETECT_COLOR_RANGE = 1 << 0,
    COLOR_DETECT_ALPHA_MODE  = 1 << 1,
};

typedef struct ColorDetectContext {
    const AVClass *class;
    FFColorDetectDSPContext dsp;
    unsigned mode;

    const AVPixFmtDescriptor *desc;
    int nb_threads;
    int depth;
    int idx_a;
    int mpeg_min;
    int mpeg_max;

    atomic_int detected_range; // enum AVColorRange
    atomic_int detected_alpha; // enum AlphaMode
} ColorDetectContext;

#define OFFSET(x) offsetof(ColorDetectContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption colordetect_options[] = {
    { "mode", "Image properties to detect", OFFSET(mode), AV_OPT_TYPE_FLAGS, {.i64 = -1}, 0, UINT_MAX, FLAGS, .unit = "mode" },
        { "color_range", "Detect (YUV) color range", 0, AV_OPT_TYPE_CONST, {.i64 = COLOR_DETECT_COLOR_RANGE}, 0, 0, FLAGS, .unit = "mode" },
        { "alpha_mode",  "Detect alpha mode",        0, AV_OPT_TYPE_CONST, {.i64 = COLOR_DETECT_ALPHA_MODE }, 0, 0, FLAGS, .unit = "mode" },
        { "all",         "Detect all supported properties", 0, AV_OPT_TYPE_CONST, {.i64 = -1}, 0, 0, FLAGS, .unit = "mode" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(colordetect);

static int query_format(const AVFilterContext *ctx,
                        AVFilterFormatsConfig **cfg_in,
                        AVFilterFormatsConfig **cfg_out)
{
    int want_flags = AV_PIX_FMT_FLAG_PLANAR;
    int reject_flags = AV_PIX_FMT_FLAG_PAL | AV_PIX_FMT_FLAG_HWACCEL |
                       AV_PIX_FMT_FLAG_BITSTREAM | AV_PIX_FMT_FLAG_FLOAT |
                       AV_PIX_FMT_FLAG_BAYER | AV_PIX_FMT_FLAG_XYZ;

    if (HAVE_BIGENDIAN) {
        want_flags |= AV_PIX_FMT_FLAG_BE;
    } else {
        reject_flags |= AV_PIX_FMT_FLAG_BE;
    }

    AVFilterFormats *formats = ff_formats_pixdesc_filter(want_flags, reject_flags);
    return ff_set_common_formats2(ctx, cfg_in, cfg_out, formats);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ColorDetectContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    const int depth = desc->comp[0].depth;
    const int mpeg_min =  16 << (depth - 8);
    const int mpeg_max = 235 << (depth - 8);
    if (depth > 16) /* not currently possible; prevent future bugs */
        return AVERROR(ENOTSUP);

    s->desc = desc;
    s->depth = depth;
    s->mpeg_min = mpeg_min;
    s->mpeg_max = mpeg_max;
    s->nb_threads = ff_filter_get_nb_threads(ctx);

    if (desc->flags & AV_PIX_FMT_FLAG_RGB) {
        atomic_init(&s->detected_range, AVCOL_RANGE_JPEG);
    } else {
        atomic_init(&s->detected_range, AVCOL_RANGE_UNSPECIFIED);
    }

    if (desc->flags & AV_PIX_FMT_FLAG_ALPHA) {
        s->idx_a = desc->comp[desc->nb_components - 1].plane;
        atomic_init(&s->detected_alpha, ALPHA_UNDETERMINED);
    } else {
        atomic_init(&s->detected_alpha, ALPHA_NONE);
    }

    ff_color_detect_dsp_init(&s->dsp, depth, inlink->color_range);
    return 0;
}

static int detect_range(AVFilterContext *ctx, void *arg,
                        int jobnr, int nb_jobs)
{
    ColorDetectContext *s = ctx->priv;
    const AVFrame *in = arg;
    const ptrdiff_t stride = in->linesize[0];
    const int y_start = (in->height * jobnr) / nb_jobs;
    const int y_end = (in->height * (jobnr + 1)) / nb_jobs;
    const int h_slice = y_end - y_start;

    if (s->dsp.detect_range(in->data[0] + y_start * stride, stride,
                            in->width, h_slice, s->mpeg_min, s->mpeg_max))
        atomic_store(&s->detected_range, AVCOL_RANGE_JPEG);

    return 0;
}

static int detect_alpha(AVFilterContext *ctx, void *arg,
                        int jobnr, int nb_jobs)
{
    ColorDetectContext *s = ctx->priv;
    const AVFrame *in = arg;
    const int w = in->width;
    const int h = in->height;
    const int y_start = (h * jobnr) / nb_jobs;
    const int y_end = (h * (jobnr + 1)) / nb_jobs;
    const int h_slice = y_end - y_start;

    const int nb_planes = (s->desc->flags & AV_PIX_FMT_FLAG_RGB) ? 3 : 1;
    const ptrdiff_t alpha_stride = in->linesize[s->idx_a];
    const uint8_t *alpha = in->data[s->idx_a] + y_start * alpha_stride;

    /**
     * To check if a value is out of range, we need to compare the color value
     * against the maximum possible color for a given alpha value.
     *   x > ((mpeg_max - mpeg_min) / pixel_max) * a + mpeg_min
     *
     * This simplifies to:
     *   (x - mpeg_min) * pixel_max > (mpeg_max - mpeg_min) * a
     *   = P * x - K > Q * a in the below formula.
     *
     * We subtract an additional offset of (1 << (depth - 1)) to account for
     * rounding errors in the value of `x`, and an extra safety margin of
     * Q because vf_premultiply.c et al. add an offset of (a >> 1) & 1.
     */
    const int p = (1 << s->depth) - 1;
    const int q = s->mpeg_max - s->mpeg_min;
    const int k = p * s->mpeg_min + q + (1 << (s->depth - 1));

    for (int i = 0; i < nb_planes; i++) {
        const ptrdiff_t stride = in->linesize[i];
        if (s->dsp.detect_alpha(in->data[i] + y_start * stride, stride,
                                alpha, alpha_stride, w, h_slice, p, q, k)) {
            atomic_store(&s->detected_alpha, ALPHA_STRAIGHT);
            return 0;
        }
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    ColorDetectContext *s = ctx->priv;
    const int nb_threads = FFMIN(inlink->h, s->nb_threads);

    if (s->mode & COLOR_DETECT_COLOR_RANGE && s->detected_range == AVCOL_RANGE_UNSPECIFIED)
        ff_filter_execute(ctx, detect_range, in, NULL, nb_threads);
    if (s->mode & COLOR_DETECT_ALPHA_MODE && s->detected_alpha == ALPHA_UNDETERMINED)
        ff_filter_execute(ctx, detect_alpha, in, NULL, nb_threads);

    return ff_filter_frame(inlink->dst->outputs[0], in);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ColorDetectContext *s = ctx->priv;
    if (!s->mode)
        return;

    av_log(ctx, AV_LOG_INFO, "Detected color properties:\n");
    if (s->mode & COLOR_DETECT_COLOR_RANGE) {
        av_log(ctx, AV_LOG_INFO, "  Color range: %s\n",
               s->detected_range == AVCOL_RANGE_JPEG ? "JPEG / full range"
                                                     : "undetermined");
    }

    if (s->mode & COLOR_DETECT_ALPHA_MODE) {
        av_log(ctx, AV_LOG_INFO, "  Alpha mode: %s\n",
               s->detected_alpha == ALPHA_NONE     ? "none" :
               s->detected_alpha == ALPHA_STRAIGHT ? "straight / independent"
                                                   : "undetermined");
    }
}

av_cold void ff_color_detect_dsp_init(FFColorDetectDSPContext *dsp, int depth,
                                      enum AVColorRange color_range)
{
#if ARCH_X86
    ff_color_detect_dsp_init_x86(dsp, depth, color_range);
#endif

    if (!dsp->detect_range)
        dsp->detect_range = depth > 8 ? ff_detect_range16_c : ff_detect_range_c;
    if (!dsp->detect_alpha) {
        if (color_range == AVCOL_RANGE_JPEG) {
            dsp->detect_alpha = depth > 8 ? ff_detect_alpha16_full_c : ff_detect_alpha_full_c;
        } else {
            dsp->detect_alpha = depth > 8 ? ff_detect_alpha16_limited_c : ff_detect_alpha_limited_c;
        }
    }
}

static const AVFilterPad colordetect_inputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_input,
        .filter_frame  = filter_frame,
    },
};

const FFFilter ff_vf_colordetect = {
    .p.name        = "colordetect",
    .p.description = NULL_IF_CONFIG_SMALL("Detect video color properties."),
    .p.priv_class  = &colordetect_class,
    .p.flags       = AVFILTER_FLAG_SLICE_THREADS | AVFILTER_FLAG_METADATA_ONLY,
    .priv_size     = sizeof(ColorDetectContext),
    FILTER_INPUTS(colordetect_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_QUERY_FUNC2(query_format),
    .uninit        = uninit,
};
