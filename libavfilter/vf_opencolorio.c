/*
 * Copyright (c) 2026 Sam Richards
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

#include "avfilter.h"
#include "formats.h"
#include "libavutil/half2float.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/time.h"
#include "ocio_wrapper.hpp"
#include "video.h"

typedef struct {
    const AVClass *class;
    char *config_path;
    char *input_space;
    char *output_space;
    char *display;
    char *view;
    char *filetransform;
    int inverse;
    OCIOHandle ocio;
    int output_format;
    char *out_format_string; // e.g. "rgb48le" which is converted to AVPixelFormat
                           // as output_format
    int channels;            // 3 or 4 depending on pixfmt
    AVDictionary *context_params;
} OCIOContext;

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

static int ocio_filter_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    OCIOContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int height = out->height;
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const int slice_h = slice_end - slice_start;

    return ocio_apply(ctx, s->ocio, in, out, slice_start, slice_h);
}

static int query_formats(AVFilterContext *ctx) {
    static const enum AVPixelFormat pix_fmts[] = {
      // 8-bit
      AV_PIX_FMT_RGBA, AV_PIX_FMT_RGB24,
      // 16-bit
      AV_PIX_FMT_RGBA64, AV_PIX_FMT_RGB48,
      // 10-bit
      AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRAP10,
      // 12-bit
      AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRAP12,
      // Half-float and float
      AV_PIX_FMT_GBRPF16, AV_PIX_FMT_GBRAPF16,
      // Float
      AV_PIX_FMT_GBRPF32, AV_PIX_FMT_GBRAPF32, AV_PIX_FMT_NONE};
    return ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
}

static int config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    OCIOContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    if (!desc) {
        av_log(ctx, AV_LOG_ERROR, "Invalid pixel format\n");
        return AVERROR(EINVAL);
    }

    int is_half =
        (desc->comp[0].depth == 16 && desc->flags & AV_PIX_FMT_FLAG_FLOAT);
    if (s->output_format == AV_PIX_FMT_NONE) {
        // Need to set the output format now, if not known.
        if (is_half) {
            // If its half-float, we output float, due to a bug in ffmpeg with
            // half-float frames
            s->output_format = AV_PIX_FMT_GBRAPF32;
        } else {
            // If output format not set, use same as input
            s->output_format = inlink->format;
        }
    }

    s->channels = desc->nb_components; // 3 or 4

    av_log(ctx, AV_LOG_INFO,
         "Configuring OCIO for %s (bit depth: %d, channels: %d), output "
         "format: (%s)\n",
         av_get_pix_fmt_name(inlink->format), desc->comp[0].depth, s->channels,
         av_get_pix_fmt_name(s->output_format));

    // Now finalize the OCIO processor with the correct bit depth
    int ret = ocio_finalize_processor(ctx, s->ocio, inlink->format, s->output_format);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR,
               "Failed to finalize OCIO processor for bit depth\n");
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    OCIOContext *s = ctx->priv;
    if (s->out_format_string != NULL)
        s->output_format = av_get_pix_fmt(s->out_format_string);
    else
        s->output_format = AV_PIX_FMT_NONE; // Default to same as input format (see later).

    if (s->filetransform && strlen(s->filetransform) > 0) {
        s->ocio = ocio_create_file_transform_processor(
            ctx, s->filetransform, s->inverse);
        av_log(ctx, AV_LOG_INFO,
           "Creating OCIO processor with FileTransform: %s, Inverse: %d\n",
           s->filetransform, s->inverse);
    } else if (s->output_space && strlen(s->output_space) > 0) {
        s->ocio = ocio_create_output_colorspace_processor(
            ctx, s->config_path, s->input_space, s->output_space, s->context_params);
        av_log(ctx, AV_LOG_INFO,
           "Creating OCIO processor with config: %s, input: %s, output: %s\n",
           s->config_path, s->input_space, s->output_space);
    } else {
        s->ocio = ocio_create_display_view_processor(
            ctx, s->config_path, s->input_space, s->display, s->view, s->inverse, s->context_params);
        av_log(ctx, AV_LOG_INFO,
           "Creating OCIO processor with config: %s, input: %s, display: %s, "
           "view: %s, Inverse: %d\n",
           s->config_path, s->input_space, s->display, s->view, s->inverse);
    }
    if (!s->ocio) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create OCIO processor.\n");
        return AVERROR(EINVAL);
    }

  return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    OCIOContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(frame->format);

    if (!desc)
        return AVERROR(EINVAL);

    int ret;
    AVFrame *output_frame;
    ThreadData td;

    if (s->output_format == inlink->format) {
        /* No pixel-format conversion needed. If the input frame is
         * writable we can apply OCIO in-place, otherwise allocate a
         * separate output frame to avoid mutating shared buffers. */
        if (av_frame_is_writable(frame)) {
            output_frame = frame;
        } else {
            output_frame = av_frame_alloc();
            if (!output_frame) {
                av_frame_free(&frame);
                return AVERROR(ENOMEM);
            }
            output_frame->format = s->output_format;
            output_frame->width = frame->width;
            output_frame->height = frame->height;
            ret = av_frame_get_buffer(output_frame, 32);

            if (ret < 0) {
                av_frame_free(&output_frame);
                av_frame_free(&frame);
                return ret;
            }
            av_frame_copy_props(output_frame, frame);
        }
    } else {
        // Allocate new output frame
        output_frame = av_frame_alloc();
        if (!output_frame) {
            av_frame_free(&frame);
            return AVERROR(ENOMEM);
        }
        output_frame->format = s->output_format;
        output_frame->width = frame->width;
        output_frame->height = frame->height;
        ret = av_frame_get_buffer(output_frame, 32);

        if (ret < 0) {
            av_frame_free(&output_frame);
            av_frame_free(&frame);
            return ret;
        }
        av_frame_copy_props(output_frame, frame);
    }

    td.in = frame;
    td.out = output_frame;

    // Use threads from context if set, otherwise let ffmpeg decide based on global settings or defaults
    // Note: ctx->graph->nb_threads is usually the global thread count.
    // ff_filter_get_nb_threads(ctx) gives the number of threads available for this filter.

    int nb_jobs = ff_filter_get_nb_threads(ctx);

    ret = ff_filter_execute(ctx, ocio_filter_slice, &td, NULL, FFMIN(output_frame->height, nb_jobs));

    if (frame != output_frame)
        av_frame_free(&frame);

    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "OCIO apply failed.\n");
        return AVERROR(EINVAL);
    }

    return ff_filter_frame(ctx->outputs[0], output_frame);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    OCIOContext *s = ctx->priv;
    if (s->ocio) {
        ocio_destroy_processor(ctx, s->ocio);
        s->ocio = NULL;
    }
}

#define OFFSET(x) offsetof(OCIOContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM

static const AVOption ocio_options[] = {
    { "config",  "OCIO config path, overriding OCIO environment variable.", OFFSET(config_path), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "input",   "Input color space", OFFSET(input_space), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "output",  "Output color space", OFFSET(output_space), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "filetransform", "Specify a File Transform", OFFSET(filetransform), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "display", "Output display, used instead of output color space.", OFFSET(display), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "view",    "View, output view transform, used in combination with display.", OFFSET(view), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "inverse", "Invert output display/view transform.", OFFSET(inverse), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS },
    { "format",  "Output video format", OFFSET(out_format_string), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS  },
    { "context_params", "OCIO context parameters", OFFSET(context_params), AV_OPT_TYPE_DICT, { .str = NULL }, 0, 0, FLAGS },{ NULL }
};

AVFILTER_DEFINE_CLASS(ocio);

static const AVFilterPad inputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_props,
    },
};

static const AVFilterPad outputs[] = {
    {.name = "default", .type = AVMEDIA_TYPE_VIDEO}};

const FFFilter ff_vf_ocio = {
    .p.name = "ocio",
    .p.description = NULL_IF_CONFIG_SMALL("Apply OCIO Display/View transform"),
    .p.priv_class = &ocio_class,
    .p.flags = AVFILTER_FLAG_SLICE_THREADS,
    .priv_size = sizeof(OCIOContext),
    .init = init,
    .uninit = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_QUERY_FUNC(query_formats)};
