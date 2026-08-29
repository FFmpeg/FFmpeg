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

#include "vf_colordetectdsp.h"

enum ColorDetectMode {
    COLOR_DETECT_COLOR_RANGE = 1 << 0,
    COLOR_DETECT_ALPHA_MODE  = 1 << 1,
};

typedef struct ColorDetectContext {
    const AVClass *class;
    FFColorDetectDSPContext dsp;
    unsigned mode;
    float threshold;

    const AVPixFmtDescriptor *desc;
    int nb_threads;
    int depth;
    int range;
    int idx_a;

    /* for color range detection only */
    int mpeg_min;
    int mpeg_max;

    /* for alpha detection only */
    int mpeg_range;
    int offset;

    atomic_int detected_range; // enum AVColorRange
    atomic_int detected_alpha; // enum FFAlphaDetect
} ColorDetectContext;

#define OFFSET(x) offsetof(ColorDetectContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption colordetect_options[] = {
    { "mode", "Image properties to detect", OFFSET(mode), AV_OPT_TYPE_FLAGS, {.i64 = -1}, 0, UINT_MAX, FLAGS, .unit = "mode" },
        { "color_range", "Detect (YUV) color range", 0, AV_OPT_TYPE_CONST, {.i64 = COLOR_DETECT_COLOR_RANGE}, 0, 0, FLAGS, .unit = "mode" },
        { "alpha_mode",  "Detect alpha mode",        0, AV_OPT_TYPE_CONST, {.i64 = COLOR_DETECT_ALPHA_MODE }, 0, 0, FLAGS, .unit = "mode" },
        { "all",         "Detect all supported properties", 0, AV_OPT_TYPE_CONST, {.i64 = -1}, 0, 0, FLAGS, .unit = "mode" },

    /* Note: threshold should not be increased past ~0.4 as it overflows 8-bit SIMD otherwise */
    { "threshold", "Detection threshold, as a fraction of the full range", OFFSET(threshold), AV_OPT_TYPE_FLOAT, {.dbl = 0.001}, 0.0, 0.05, FLAGS },
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
    const int range = (1 << depth) - 1;
    const int threshold = lrintf(s->threshold * range);
    const int mpeg_min =  16 << (depth - 8);
    const int mpeg_max = 235 << (depth - 8);
    if (depth > 16) /* not currently possible; prevent future bugs */
        return AVERROR(ENOTSUP);

    s->desc = desc;
    s->depth = depth;
    s->range = range;
    s->nb_threads = ff_filter_get_nb_threads(ctx);

    /* Color range detection: */
    s->mpeg_min = av_clip_uintp2(mpeg_min - threshold, depth);
    s->mpeg_max = av_clip_uintp2(mpeg_max + threshold, depth);

    /**
     * Alpha mode detection:
     *
     * To check if a value is out of range, we need to compare the color value
     * against the maximum possible color for a given alpha value.
     *   x > ((mpeg_max - mpeg_min) / pixel_max) * a + mpeg_min + threshold
     *
     * This simplifies to:
     *   (x - mpeg_min - threshold) * pixel_max > (mpeg_max - mpeg_min) * a
     *   = range * x - offset > mpeg_range * a in the below formula.
     *
     * We subtract an additional offset of (1 << (depth - 1)) to account for
     * rounding errors in the value of `x`.
     *
     * For full range input this degenerates to `x > a + threshold`, so the
     * threshold is passed through directly, without the `range` scaling.
     */
    if (inlink->color_range == AVCOL_RANGE_JPEG) {
        s->mpeg_range = range;
        s->offset = threshold;
    } else {
        s->mpeg_range = mpeg_max - mpeg_min;
        s->offset = range * (mpeg_min + threshold) + (1 << (depth - 1));
    }

    if (desc->flags & AV_PIX_FMT_FLAG_RGB) {
        atomic_init(&s->detected_range, AVCOL_RANGE_JPEG);
    } else {
        atomic_init(&s->detected_range, AVCOL_RANGE_UNSPECIFIED);
    }

    if (desc->flags & AV_PIX_FMT_FLAG_ALPHA) {
        s->idx_a = desc->comp[desc->nb_components - 1].plane;
        atomic_init(&s->detected_alpha, FF_ALPHA_UNDETERMINED);
    } else {
        atomic_init(&s->detected_alpha, FF_ALPHA_NONE);
    }

    ff_color_detect_dsp_init(&s->dsp, depth, s->offset, inlink->color_range);
    return 0;
}

static int detect_range(AVFilterContext *ctx, void *arg,
                        int jobnr, int nb_jobs)
{
    ColorDetectContext *s = ctx->priv;
    const AVFrame *in = arg;
    const ptrdiff_t stride = in->linesize[0];
    const int y_start = ff_slice_pos(in->height, jobnr, nb_jobs);
    const int y_end = ff_slice_pos(in->height, jobnr + 1, nb_jobs);
    const int h_slice = y_end - y_start;

    if (s->dsp.detect_range(in->data[0] + y_start * stride, stride,
                            in->width, h_slice, s->mpeg_min, s->mpeg_max))
        atomic_store_explicit(&s->detected_range, AVCOL_RANGE_JPEG,
                              memory_order_relaxed);

    return 0;
}

static int detect_alpha(AVFilterContext *ctx, void *arg,
                        int jobnr, int nb_jobs)
{
    ColorDetectContext *s = ctx->priv;
    const AVFrame *in = arg;
    const int w = in->width;
    const int h = in->height;
    const int y_start = ff_slice_pos(h, jobnr, nb_jobs);
    const int y_end = ff_slice_pos(h, jobnr + 1, nb_jobs);
    const int h_slice = y_end - y_start;

    const int nb_planes = (s->desc->flags & AV_PIX_FMT_FLAG_RGB) ? 3 : 1;
    const ptrdiff_t alpha_stride = in->linesize[s->idx_a];
    const uint8_t *alpha = in->data[s->idx_a] + y_start * alpha_stride;

    int ret = 0;
    for (int i = 0; i < nb_planes; i++) {
        const ptrdiff_t stride = in->linesize[i];
        ret = s->dsp.detect_alpha(in->data[i] + y_start * stride, stride,
                                  alpha, alpha_stride, w, h_slice, s->range,
                                  s->mpeg_range, s->offset);
        ret |= atomic_fetch_or_explicit(&s->detected_alpha, ret, memory_order_relaxed);
        if (ret == FF_ALPHA_STRAIGHT)
            break;
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    ColorDetectContext *s = ctx->priv;
    const int nb_threads = FFMIN(inlink->h, s->nb_threads);

    enum AVColorRange detected_range = atomic_load_explicit(&s->detected_range, memory_order_relaxed);
    if (s->mode & COLOR_DETECT_COLOR_RANGE && detected_range == AVCOL_RANGE_UNSPECIFIED)
        ff_filter_execute(ctx, detect_range, in, NULL, nb_threads);

    enum FFAlphaDetect detected_alpha = atomic_load_explicit(&s->detected_alpha, memory_order_relaxed);
    if (s->mode & COLOR_DETECT_ALPHA_MODE && detected_alpha != FF_ALPHA_NONE &&
        detected_alpha != FF_ALPHA_STRAIGHT)
        ff_filter_execute(ctx, detect_alpha, in, NULL, nb_threads);

    return ff_filter_frame(inlink->dst->outputs[0], in);
}

static av_cold void report_detected_props(AVFilterContext *ctx)
{
    ColorDetectContext *s = ctx->priv;
    if (!s->mode)
        return;

    av_log(ctx, AV_LOG_INFO, "Detected color properties:\n");
    if (s->mode & COLOR_DETECT_COLOR_RANGE) {
        enum AVColorRange detected_range = atomic_load_explicit(&s->detected_range,
                                                                memory_order_relaxed);
        av_log(ctx, AV_LOG_INFO, "  Color range: %s\n",
               detected_range == AVCOL_RANGE_JPEG ? "JPEG / full range"
                                                  : "undetermined");
    }

    if (s->mode & COLOR_DETECT_ALPHA_MODE) {
        enum FFAlphaDetect detected_alpha = atomic_load_explicit(&s->detected_alpha,
                                                                 memory_order_relaxed);
        av_log(ctx, AV_LOG_INFO, "  Alpha mode: %s\n",
               detected_alpha == FF_ALPHA_NONE        ? "none" :
               detected_alpha == FF_ALPHA_STRAIGHT    ? "straight" :
               detected_alpha == FF_ALPHA_TRANSPARENT ? "undetermined"
                                                      : "opaque");
    }
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink  = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *frame;
    int64_t pts;
    int ret;

    ret = ff_outlink_get_status(outlink);
    if (ret) {
        ff_inlink_set_status(inlink, ret);
        report_detected_props(ctx);
        return 0;
    }

    ret = ff_inlink_consume_frame(inlink, &frame);
    if (ret < 0) {
        return ret;
    } else if (ret) {
        return filter_frame(inlink, frame);
    }

    if (ff_inlink_acknowledge_status(inlink, &ret, &pts)) {
        ff_outlink_set_status(outlink, ret, pts);
        report_detected_props(ctx);
        return 0;
    }

    FF_FILTER_FORWARD_WANTED(outlink, inlink);
    return FFERROR_NOT_READY;
}

static const AVFilterPad colordetect_inputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_input,
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
    .activate      = activate,
};
