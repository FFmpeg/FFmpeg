/*
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

#include "libavutil/colorspace.h"
#include "libavutil/eval.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "drawutils.h"
#include "internal.h"
#include "opencl.h"
#include "opencl_source.h"
#include "video.h"

static const char *const var_names[] = {
    "in_w",   "iw",
    "in_h",   "ih",
    "out_w",  "ow",
    "out_h",  "oh",
    "x",
    "y",
    "a",
    "sar",
    "dar",
    NULL
};

enum var_name {
    VAR_IN_W,   VAR_IW,
    VAR_IN_H,   VAR_IH,
    VAR_OUT_W,  VAR_OW,
    VAR_OUT_H,  VAR_OH,
    VAR_X,
    VAR_Y,
    VAR_A,
    VAR_SAR,
    VAR_DAR,
    VARS_NB
};

typedef struct PadOpenCLContext {
    OpenCLFilterContext ocf;
    int initialized;
    int is_rgb;
    int is_packed;
    int hsub, vsub;

    char *w_expr;
    char *h_expr;
    char *x_expr;
    char *y_expr;
    AVRational aspect;

    cl_command_queue command_queue;
    cl_kernel kernel_pad;

    int w, h;
    int x, y;
    uint8_t pad_rgba[4];
    uint8_t pad_color[4];
    cl_float4 pad_color_float;
    cl_int2 pad_pos;
} PadOpenCLContext;

static int pad_opencl_init(AVFilterContext *avctx, AVFrame *input_frame)
{
    PadOpenCLContext *ctx = avctx->priv;
    AVHWFramesContext *input_frames_ctx = (AVHWFramesContext *)input_frame->hw_frames_ctx->data;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(input_frames_ctx->sw_format);
    uint8_t rgba_map[4];
    cl_int cle;
    int err;

    ff_fill_rgba_map(rgba_map, input_frames_ctx->sw_format);
    ctx->is_rgb = !!(desc->flags & AV_PIX_FMT_FLAG_RGB);
    ctx->is_packed = !(desc->flags & AV_PIX_FMT_FLAG_PLANAR);
    ctx->hsub = desc->log2_chroma_w;
    ctx->vsub = desc->log2_chroma_h;

    err = ff_opencl_filter_load_program(avctx, &ff_source_pad_cl, 1);
    if (err < 0)
        goto fail;

    ctx->command_queue = clCreateCommandQueue(
        ctx->ocf.hwctx->context,
        ctx->ocf.hwctx->device_id,
        0,
        &cle
    );

    if (ctx->is_rgb) {
        ctx->pad_color[rgba_map[0]] = ctx->pad_rgba[0];
        ctx->pad_color[rgba_map[1]] = ctx->pad_rgba[1];
        ctx->pad_color[rgba_map[2]] = ctx->pad_rgba[2];
        ctx->pad_color[rgba_map[3]] = ctx->pad_rgba[3];
    } else {
        ctx->pad_color[0] = RGB_TO_Y_BT709(ctx->pad_rgba[0], ctx->pad_rgba[1], ctx->pad_rgba[2]);
        ctx->pad_color[1] = RGB_TO_U_BT709(ctx->pad_rgba[0], ctx->pad_rgba[1], ctx->pad_rgba[2], 0);
        ctx->pad_color[2] = RGB_TO_V_BT709(ctx->pad_rgba[0], ctx->pad_rgba[1], ctx->pad_rgba[2], 0);
        ctx->pad_color[3] = ctx->pad_rgba[3];
    }

    CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to create OpenCL command queue %d.\n", cle);

    ctx->kernel_pad = clCreateKernel(ctx->ocf.program, "pad", &cle);
    CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to create pad kernel: %d.\n", cle);

    for (int i = 0; i < 4; ++i) {
        ctx->pad_color_float.s[i] = (float)ctx->pad_color[i] / 255.0;
    }

    ctx->pad_pos.s[0] = ctx->x;
    ctx->pad_pos.s[1] = ctx->y;

    ctx->initialized = 1;
    return 0;

fail:
    if (ctx->command_queue)
        clReleaseCommandQueue(ctx->command_queue);
    if (ctx->kernel_pad)
        clReleaseKernel(ctx->kernel_pad);
    return err;
}

static int filter_frame(AVFilterLink *link, AVFrame *input_frame)
{
    AVFilterContext *avctx = link->dst;
    AVFilterLink *outlink = avctx->outputs[0];
    PadOpenCLContext *pad_ctx = avctx->priv;
    AVFrame *output_frame = NULL;
    int err;
    cl_int cle;
    size_t global_work[2];
    cl_mem src, dst;

    if (!input_frame->hw_frames_ctx)
        return AVERROR(EINVAL);

    if (!pad_ctx->initialized) {
        err = pad_opencl_init(avctx, input_frame);
        if (err < 0)
            goto fail;
    }

    output_frame = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!output_frame) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    for (int p = 0; p < FF_ARRAY_ELEMS(output_frame->data); p++) {
        cl_float4 pad_color_float;
        cl_int2 pad_pos;

        if (pad_ctx->is_packed) {
            pad_color_float = pad_ctx->pad_color_float;
        } else {
            pad_color_float.s[0] = pad_ctx->pad_color_float.s[p];
            pad_color_float.s[1] = pad_ctx->pad_color_float.s[2];
        }

        if (p > 0 && p < 3) {
            pad_pos.s[0] = pad_ctx->pad_pos.s[0] >> pad_ctx->hsub;
            pad_pos.s[1] = pad_ctx->pad_pos.s[1] >> pad_ctx->vsub;
        } else {
            pad_pos.s[0] = pad_ctx->pad_pos.s[0];
            pad_pos.s[1] = pad_ctx->pad_pos.s[1];
        }

        src = (cl_mem)input_frame->data[p];
        dst = (cl_mem)output_frame->data[p];

        if (!dst)
            break;

        CL_SET_KERNEL_ARG(pad_ctx->kernel_pad, 0, cl_mem, &src);
        CL_SET_KERNEL_ARG(pad_ctx->kernel_pad, 1, cl_mem, &dst);
        CL_SET_KERNEL_ARG(pad_ctx->kernel_pad, 2, cl_float4, &pad_color_float);
        CL_SET_KERNEL_ARG(pad_ctx->kernel_pad, 3, cl_int2, &pad_pos);

        err = ff_opencl_filter_work_size_from_image(avctx, global_work, output_frame, p, 16);
        if (err < 0)
            goto fail;

        cle = clEnqueueNDRangeKernel(pad_ctx->command_queue, pad_ctx->kernel_pad, 2, NULL,
                                     global_work, NULL, 0, NULL, NULL);

        CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to enqueue pad kernel: %d.\n", cle);
    }

    // Run queued kernel
    cle = clFinish(pad_ctx->command_queue);
    CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to finish command queue: %d.\n", cle);

    err = av_frame_copy_props(output_frame, input_frame);
    if (err < 0)
        goto fail;

    av_frame_free(&input_frame);

    return ff_filter_frame(outlink, output_frame);

fail:
    clFinish(pad_ctx->command_queue);
    av_frame_free(&input_frame);
    av_frame_free(&output_frame);
    return err;
}

static av_cold void pad_opencl_uninit(AVFilterContext *avctx)
{
    PadOpenCLContext *ctx = avctx->priv;
    cl_int cle;

    if (ctx->kernel_pad) {
        cle = clReleaseKernel(ctx->kernel_pad);
        if (cle != CL_SUCCESS)
            av_log(avctx, AV_LOG_ERROR, "Failed to release "
                   "kernel: %d.\n", cle);
    }

    if (ctx->command_queue) {
        cle = clReleaseCommandQueue(ctx->command_queue);
        if (cle != CL_SUCCESS)
            av_log(avctx, AV_LOG_ERROR, "Failed to release "
                   "command queue: %d.\n", cle);
    }

    ff_opencl_filter_uninit(avctx);
}

static int pad_opencl_config_output(AVFilterLink *outlink)
{
    AVFilterContext *avctx = outlink->src;
    AVFilterLink *inlink = avctx->inputs[0];
    PadOpenCLContext *ctx = avctx->priv;
    AVRational adjusted_aspect = ctx->aspect;
    double var_values[VARS_NB], res;
    int err, ret;
    char *expr;

    var_values[VAR_IN_W]  = var_values[VAR_IW] = inlink->w;
    var_values[VAR_IN_H]  = var_values[VAR_IH] = inlink->h;
    var_values[VAR_OUT_W] = var_values[VAR_OW] = NAN;
    var_values[VAR_OUT_H] = var_values[VAR_OH] = NAN;
    var_values[VAR_A]     = (double) inlink->w / inlink->h;
    var_values[VAR_SAR]   = inlink->sample_aspect_ratio.num ?
        (double) inlink->sample_aspect_ratio.num / inlink->sample_aspect_ratio.den : 1;
    var_values[VAR_DAR]   = var_values[VAR_A] * var_values[VAR_SAR];

    av_expr_parse_and_eval(&res, (expr = ctx->w_expr),
                           var_names, var_values,
                           NULL, NULL, NULL, NULL, NULL, 0, ctx);
    ctx->w = var_values[VAR_OUT_W] = var_values[VAR_OW] = res;
    if ((ret = av_expr_parse_and_eval(&res, (expr = ctx->h_expr),
                                      var_names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        return ret;
    ctx->h = var_values[VAR_OUT_H] = var_values[VAR_OH] = res;
    if (!ctx->h)
        var_values[VAR_OUT_H] = var_values[VAR_OH] = ctx->h = inlink->h;

    /* evaluate the width again, as it may depend on the evaluated output height */
    if ((ret = av_expr_parse_and_eval(&res, (expr = ctx->w_expr),
                                      var_names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        return ret;
    ctx->w = var_values[VAR_OUT_W] = var_values[VAR_OW] = res;
    if (!ctx->w)
        var_values[VAR_OUT_W] = var_values[VAR_OW] = ctx->w = inlink->w;

    if (adjusted_aspect.num && adjusted_aspect.den) {
        adjusted_aspect = av_div_q(adjusted_aspect, inlink->sample_aspect_ratio);
        if (ctx->h < av_rescale(ctx->w, adjusted_aspect.den, adjusted_aspect.num)) {
            ctx->h = var_values[VAR_OUT_H] = var_values[VAR_OH] = av_rescale(ctx->w, adjusted_aspect.den, adjusted_aspect.num);
        } else {
            ctx->w = var_values[VAR_OUT_W] = var_values[VAR_OW] = av_rescale(ctx->h, adjusted_aspect.num, adjusted_aspect.den);
        }
    }

    /* evaluate x and y */
    av_expr_parse_and_eval(&res, (expr = ctx->x_expr),
                           var_names, var_values,
                           NULL, NULL, NULL, NULL, NULL, 0, ctx);
    ctx->x = var_values[VAR_X] = res;
    if ((ret = av_expr_parse_and_eval(&res, (expr = ctx->y_expr),
                                      var_names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        return ret;
    ctx->y = var_values[VAR_Y] = res;
    /* evaluate x again, as it may depend on the evaluated y value */
    if ((ret = av_expr_parse_and_eval(&res, (expr = ctx->x_expr),
                                      var_names, var_values,
                                      NULL, NULL, NULL, NULL, NULL, 0, ctx)) < 0)
        return ret;
    ctx->x = var_values[VAR_X] = res;

    if (ctx->x < 0 || ctx->x + inlink->w > ctx->w)
        ctx->x = var_values[VAR_X] = (ctx->w - inlink->w) / 2;
    if (ctx->y < 0 || ctx->y + inlink->h > ctx->h)
        ctx->y = var_values[VAR_Y] = (ctx->h - inlink->h) / 2;

    /* sanity check params */
    if (ctx->w < inlink->w || ctx->h < inlink->h) {
        av_log(ctx, AV_LOG_ERROR, "Padded dimensions cannot be smaller than input dimensions.\n");
        return AVERROR(EINVAL);
    }

    if (ctx->w > avctx->inputs[0]->w) {
        ctx->ocf.output_width  = ctx->w;
    } else {
        ctx->ocf.output_width  = avctx->inputs[0]->w;
    }

    if (ctx->h > avctx->inputs[0]->h) {
        ctx->ocf.output_height = ctx->h;
    } else {
        ctx->ocf.output_height = avctx->inputs[0]->h;
    }

    if (ctx->x + avctx->inputs[0]->w > ctx->ocf.output_width ||
        ctx->y + avctx->inputs[0]->h > ctx->ocf.output_height) {
        return AVERROR(EINVAL);
    }

    err = ff_opencl_filter_config_output(outlink);
    if (err < 0)
        return err;

    return 0;
}

static const AVFilterPad pad_opencl_inputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = &ff_opencl_filter_config_input,
    },
};

static const AVFilterPad pad_opencl_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &pad_opencl_config_output,
    },
};

#define OFFSET(x) offsetof(PadOpenCLContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption pad_opencl_options[] = {
    { "width",  "set the pad area width",       OFFSET(w_expr), AV_OPT_TYPE_STRING, {.str = "iw"}, 0, 0, FLAGS },
    { "w",      "set the pad area width",       OFFSET(w_expr), AV_OPT_TYPE_STRING, {.str = "iw"}, 0, 0, FLAGS },
    { "height", "set the pad area height",      OFFSET(h_expr), AV_OPT_TYPE_STRING, {.str = "ih"}, 0, 0, FLAGS },
    { "h",      "set the pad area height",      OFFSET(h_expr), AV_OPT_TYPE_STRING, {.str = "ih"}, 0, 0, FLAGS },
    { "x",      "set the x offset for the input image position", OFFSET(x_expr), AV_OPT_TYPE_STRING, {.str = "0"}, 0, INT16_MAX, FLAGS },
    { "y",      "set the y offset for the input image position", OFFSET(y_expr), AV_OPT_TYPE_STRING, {.str = "0"}, 0, INT16_MAX, FLAGS },
    { "color", "set the color of the padded area border", OFFSET(pad_rgba), AV_OPT_TYPE_COLOR, { .str = "black" }, 0, 0, FLAGS },
    { "aspect",  "pad to fit an aspect instead of a resolution", OFFSET(aspect), AV_OPT_TYPE_RATIONAL, {.dbl = 0}, 0, INT16_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(pad_opencl);

const AVFilter ff_vf_pad_opencl = {
    .name           = "pad_opencl",
    .description    = NULL_IF_CONFIG_SMALL("Pad the input video."),
    .priv_size      = sizeof(PadOpenCLContext),
    .priv_class     = &pad_opencl_class,
    .init           = &ff_opencl_filter_init,
    .uninit         = &pad_opencl_uninit,
    FILTER_INPUTS(pad_opencl_inputs),
    FILTER_OUTPUTS(pad_opencl_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_OPENCL),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
    .flags          = AVFILTER_FLAG_HWDEVICE,
};
