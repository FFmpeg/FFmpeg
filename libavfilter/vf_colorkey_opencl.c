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

#include "libavutil/opt.h"
#include "avfilter.h"
#include "internal.h"
#include "opencl.h"
#include "opencl_source.h"
#include "video.h"

typedef struct ColorkeyOpenCLContext {
    OpenCLFilterContext ocf;
    // Whether or not the above `OpenCLFilterContext` has been initialized
    int initialized;

    cl_command_queue command_queue;
    cl_kernel kernel_colorkey;

    // The color we are supposed to replace with transparency
    uint8_t colorkey_rgba[4];
    // Stored as a normalized float for passing to the OpenCL kernel
    cl_float4 colorkey_rgba_float;
    // Similarity percentage compared to `colorkey_rgba`, ranging from `0.01` to `1.0`
    // where `0.01` matches only the key color and `1.0` matches all colors
    float similarity;
    // Blending percentage where `0.0` results in fully transparent pixels, `1.0` results
    // in fully opaque pixels, and numbers in between result in transparency that varies
    // based on the similarity to the key color
    float blend;
} ColorkeyOpenCLContext;

static int colorkey_opencl_init(AVFilterContext *avctx)
{
    ColorkeyOpenCLContext *ctx = avctx->priv;
    cl_int cle;
    int err;

    err = ff_opencl_filter_load_program(avctx, &ff_source_colorkey_cl, 1);
    if (err < 0)
        goto fail;

    ctx->command_queue = clCreateCommandQueue(
        ctx->ocf.hwctx->context,
        ctx->ocf.hwctx->device_id,
        0,
        &cle
    );

    CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to create OpenCL command queue %d.\n", cle);

    if (ctx->blend > 0.0001) {
        ctx->kernel_colorkey = clCreateKernel(ctx->ocf.program, "colorkey_blend", &cle);
        CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to create colorkey_blend kernel: %d.\n", cle);
    } else {
        ctx->kernel_colorkey = clCreateKernel(ctx->ocf.program, "colorkey", &cle);
        CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to create colorkey kernel: %d.\n", cle);
    }

    for (int i = 0; i < 4; ++i) {
        ctx->colorkey_rgba_float.s[i] = (float)ctx->colorkey_rgba[i] / 255.0;
    }

    ctx->initialized = 1;
    return 0;

fail:
    if (ctx->command_queue)
        clReleaseCommandQueue(ctx->command_queue);
    if (ctx->kernel_colorkey)
        clReleaseKernel(ctx->kernel_colorkey);
    return err;
}

static int filter_frame(AVFilterLink *link, AVFrame *input_frame)
{
    AVFilterContext *avctx = link->dst;
    AVFilterLink *outlink = avctx->outputs[0];
    ColorkeyOpenCLContext *colorkey_ctx = avctx->priv;
    AVFrame *output_frame = NULL;
    int err;
    cl_int cle;
    size_t global_work[2];
    cl_mem src, dst;

    if (!input_frame->hw_frames_ctx)
        return AVERROR(EINVAL);

    if (!colorkey_ctx->initialized) {
        AVHWFramesContext *input_frames_ctx =
            (AVHWFramesContext*)input_frame->hw_frames_ctx->data;
        int fmt = input_frames_ctx->sw_format;

        // Make sure the input is a format we support
        if (fmt != AV_PIX_FMT_ARGB &&
            fmt != AV_PIX_FMT_RGBA &&
            fmt != AV_PIX_FMT_ABGR &&
            fmt != AV_PIX_FMT_BGRA
        ) {
            av_log(avctx, AV_LOG_ERROR, "unsupported (non-RGB) format in colorkey_opencl.\n");
            err = AVERROR(ENOSYS);
            goto fail;
        }

        err = colorkey_opencl_init(avctx);
        if (err < 0)
            goto fail;
    }

    // This filter only operates on RGB data and we know that will be on the first plane
    src = (cl_mem)input_frame->data[0];
    output_frame = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!output_frame) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    dst = (cl_mem)output_frame->data[0];

    CL_SET_KERNEL_ARG(colorkey_ctx->kernel_colorkey, 0, cl_mem, &src);
    CL_SET_KERNEL_ARG(colorkey_ctx->kernel_colorkey, 1, cl_mem, &dst);
    CL_SET_KERNEL_ARG(colorkey_ctx->kernel_colorkey, 2, cl_float4, &colorkey_ctx->colorkey_rgba_float);
    CL_SET_KERNEL_ARG(colorkey_ctx->kernel_colorkey, 3, float, &colorkey_ctx->similarity);
    if (colorkey_ctx->blend > 0.0001) {
        CL_SET_KERNEL_ARG(colorkey_ctx->kernel_colorkey, 4, float, &colorkey_ctx->blend);
    }

    err = ff_opencl_filter_work_size_from_image(avctx, global_work, input_frame, 0, 0);
    if (err < 0)
        goto fail;

    cle = clEnqueueNDRangeKernel(
        colorkey_ctx->command_queue,
        colorkey_ctx->kernel_colorkey,
        2,
        NULL,
        global_work,
        NULL,
        0,
        NULL,
        NULL
    );

    CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to enqueue colorkey kernel: %d.\n", cle);

    // Run queued kernel
    cle = clFinish(colorkey_ctx->command_queue);
    CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to finish command queue: %d.\n", cle);

    err = av_frame_copy_props(output_frame, input_frame);
    if (err < 0)
        goto fail;

    av_frame_free(&input_frame);

    return ff_filter_frame(outlink, output_frame);

fail:
    clFinish(colorkey_ctx->command_queue);
    av_frame_free(&input_frame);
    av_frame_free(&output_frame);
    return err;
}

static av_cold void colorkey_opencl_uninit(AVFilterContext *avctx)
{
    ColorkeyOpenCLContext *ctx = avctx->priv;
    cl_int cle;

    if (ctx->kernel_colorkey) {
        cle = clReleaseKernel(ctx->kernel_colorkey);
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

static const AVFilterPad colorkey_opencl_inputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = &ff_opencl_filter_config_input,
    },
};

static const AVFilterPad colorkey_opencl_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_opencl_filter_config_output,
    },
};

#define OFFSET(x) offsetof(ColorkeyOpenCLContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption colorkey_opencl_options[] = {
    { "color", "set the colorkey key color", OFFSET(colorkey_rgba), AV_OPT_TYPE_COLOR, { .str = "black" }, 0, 0, FLAGS },
    { "similarity", "set the colorkey similarity value", OFFSET(similarity), AV_OPT_TYPE_FLOAT, { .dbl = 0.01 }, 0.01, 1.0, FLAGS },
    { "blend", "set the colorkey key blend value", OFFSET(blend), AV_OPT_TYPE_FLOAT, { .dbl = 0.0 }, 0.0, 1.0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(colorkey_opencl);

const AVFilter ff_vf_colorkey_opencl = {
    .name           = "colorkey_opencl",
    .description    = NULL_IF_CONFIG_SMALL("Turns a certain color into transparency. Operates on RGB colors."),
    .priv_size      = sizeof(ColorkeyOpenCLContext),
    .priv_class     = &colorkey_opencl_class,
    .init           = &ff_opencl_filter_init,
    .uninit         = &colorkey_opencl_uninit,
    FILTER_INPUTS(colorkey_opencl_inputs),
    FILTER_OUTPUTS(colorkey_opencl_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_OPENCL),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
    .flags          = AVFILTER_FLAG_HWDEVICE,
};
