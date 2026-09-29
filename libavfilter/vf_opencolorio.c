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
#include "filters.h"
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
    const int slice_start = ff_slice_pos(height, jobnr, nb_jobs);
    const int slice_end = ff_slice_pos(height, jobnr + 1, nb_jobs);
    const int slice_h = slice_end - slice_start;

    return ocio_apply(ctx, s->ocio, in, out, slice_start, slice_h);
}

// Formats supported for output (also accepted as input).
static const enum AVPixelFormat output_pix_fmts[] = {
    // 8-bit
    AV_PIX_FMT_RGBA, AV_PIX_FMT_RGB24,
    // 16-bit
    AV_PIX_FMT_RGBA64, AV_PIX_FMT_RGB48,
    // 10-bit
    AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRAP10,
    // 12-bit
    AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRAP12,
    // Float
    AV_PIX_FMT_GBRPF32, AV_PIX_FMT_GBRAPF32,
    AV_PIX_FMT_NONE
};

// All formats accepted as input, including half-float.
// Half-float is input-only because swscale has no F16 write support.
// We promote half-float to float on output, which is lossless. swscale's
// legacy F16 read path also clamps to [0,1], and its new backend has no
// half-float support yet.
static const enum AVPixelFormat input_pix_fmts[] = {
    // 8-bit
    AV_PIX_FMT_RGBA, AV_PIX_FMT_RGB24,
    // 16-bit
    AV_PIX_FMT_RGBA64, AV_PIX_FMT_RGB48,
    // 10-bit
    AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRAP10,
    // 12-bit
    AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRAP12,
    // Half-float (input only)
    AV_PIX_FMT_GBRPF16, AV_PIX_FMT_GBRAPF16,
    // Float
    AV_PIX_FMT_GBRPF32, AV_PIX_FMT_GBRAPF32,
    AV_PIX_FMT_NONE
};


static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    static const enum AVPixelFormat float_fmts[] = {
        AV_PIX_FMT_GBRPF32, AV_PIX_FMT_GBRAPF32, AV_PIX_FMT_NONE
    };
    const OCIOContext *s = ctx->priv;
    const AVFilterFormats *in;
    int has_f16 = 0;
    int ret;

    // Input accepts all supported formats including half-float
    if (!cfg_in[0]->formats) {
        ret = ff_formats_ref(ff_make_pixel_format_list(input_pix_fmts),
                             &cfg_in[0]->formats);
        if (ret < 0)
            return ret;

        // Returning AVERROR(EAGAIN) skips the generic defaults, but the
        // graph may insert a converter on the input before this filter
        // publishes anything, and that code expects these lists to exist.
        if ((ret = ff_formats_ref(ff_all_color_spaces(), &cfg_in[0]->color_spaces)) < 0 ||
            (ret = ff_formats_ref(ff_all_color_ranges(), &cfg_in[0]->color_ranges)) < 0 ||
            (ret = ff_formats_ref(ff_all_alpha_modes(), &cfg_in[0]->alpha_modes)) < 0 ||
            (ret = ff_formats_ref(ff_all_chroma_locations(), &cfg_in[0]->chroma_locations)) < 0)
            return ret;
    }

    // The default output depends on the input, so wait until the input list
    // has been merged with the upstream one. Until then it is still this
    // filter's own list and says nothing about the actual input format.
    // The merged list need not be a singleton. This inspects the link
    // because the merge cannot be observed through cfg_in alone.
    if (s->output_format == AV_PIX_FMT_NONE &&
        ctx->inputs[0]->incfg.formats != cfg_in[0]->formats)
        return AVERROR(EAGAIN);

    // Only publish the output attributes together with the output pixel
    // formats: a converter inserted on the output would otherwise find
    // them set while the format list is still missing.
    if ((ret = ff_formats_ref(cfg_in[0]->color_spaces, &cfg_out[0]->color_spaces)) < 0 ||
        (ret = ff_formats_ref(cfg_in[0]->color_ranges, &cfg_out[0]->color_ranges)) < 0 ||
        (ret = ff_formats_ref(cfg_in[0]->alpha_modes, &cfg_out[0]->alpha_modes)) < 0 ||
        (ret = ff_formats_ref(cfg_in[0]->chroma_locations, &cfg_out[0]->chroma_locations)) < 0)
        return ret;

    // Output: if user specified a format, constrain to just that format
    if (s->output_format != AV_PIX_FMT_NONE) {
        const enum AVPixelFormat user_fmts[] = { s->output_format, AV_PIX_FMT_NONE };
        return ff_formats_ref(ff_make_pixel_format_list(user_fmts),
                              &cfg_out[0]->formats);
    }

    // No user-specified format: the default output is the input format,
    // except that half-float input is promoted to the matching float
    // format, because swscale clips F16 to [0,1] when converting it.
    in = cfg_in[0]->formats;
    for (unsigned i = 0; i < in->nb_formats; i++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(in->formats[i]);
        if (desc->comp[0].depth == 16 && (desc->flags & AV_PIX_FMT_FLAG_FLOAT))
            has_f16 = 1;
    }

    // No half-float candidate: the output is the input format, so share
    // the list and let downstream constraints narrow the input as well.
    if (!has_f16)
        return ff_formats_ref(cfg_in[0]->formats, &cfg_out[0]->formats);

    // The input may be half-float (possibly among other candidates, e.g.
    // behind zscale): only offer float output, which is lossless for every
    // input format. Negotiation picks the one matching the input's alpha.
    return ff_formats_ref(ff_make_pixel_format_list(float_fmts),
                          &cfg_out[0]->formats);
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

    // The output format was already constrained during negotiation.
    // Read it from the negotiated output link if not set by the user.
    if (s->output_format == AV_PIX_FMT_NONE)
        s->output_format = ctx->outputs[0]->format;

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
    if (s->out_format_string != NULL) {
        s->output_format = av_get_pix_fmt(s->out_format_string);
        if (s->output_format == AV_PIX_FMT_NONE) {
            av_log(ctx, AV_LOG_ERROR, "Unknown pixel format: '%s'\n",
                   s->out_format_string);
            return AVERROR(EINVAL);
        }
        // Verify the format is a supported output format
        int valid = 0;
        for (int i = 0; output_pix_fmts[i] != AV_PIX_FMT_NONE; i++) {
            if (s->output_format == output_pix_fmts[i]) {
                valid = 1;
                break;
            }
        }
        if (!valid) {
            av_log(ctx, AV_LOG_ERROR,
                   "Unsupported output format: '%s'\n", s->out_format_string);
            return AVERROR(EINVAL);
        }
    } else {
        s->output_format = AV_PIX_FMT_NONE; // Default to same as input format (see later).
    }

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
    FILTER_QUERY_FUNC2(query_formats)};
