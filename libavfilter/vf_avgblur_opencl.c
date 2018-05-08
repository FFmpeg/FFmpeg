/*
 * Copyright (c) 2018 Dylan Fernando
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

#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "internal.h"
#include "opencl.h"
#include "opencl_source.h"
#include "video.h"


typedef struct AverageBlurOpenCLContext {
    OpenCLFilterContext ocf;

    int              initialised;
    cl_kernel        kernel_horiz;
    cl_kernel        kernel_vert;
    cl_command_queue command_queue;

    int radius;
    int radiusV;
    int planes;

} AverageBlurOpenCLContext;


static int avgblur_opencl_init(AVFilterContext *avctx)
{
    AverageBlurOpenCLContext *ctx = avctx->priv;
    cl_int cle;
    int err;

    err = ff_opencl_filter_load_program(avctx, &ff_opencl_source_avgblur, 1);
    if (err < 0)
        goto fail;

    ctx->command_queue = clCreateCommandQueue(ctx->ocf.hwctx->context,
                                              ctx->ocf.hwctx->device_id,
                                              0, &cle);
    if (!ctx->command_queue) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create OpenCL "
               "command queue: %d.\n", cle);
        err = AVERROR(EIO);
        goto fail;
    }

    ctx->kernel_horiz = clCreateKernel(ctx->ocf.program,"avgblur_horiz", &cle);
    if (!ctx->kernel_horiz) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create kernel: %d.\n", cle);
        err = AVERROR(EIO);
        goto fail;
    }

    ctx->kernel_vert = clCreateKernel(ctx->ocf.program,"avgblur_vert", &cle);
    if (!ctx->kernel_vert) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create kernel: %d.\n", cle);
        err = AVERROR(EIO);
        goto fail;
    }

    if (ctx->radiusV <= 0) {
        ctx->radiusV = ctx->radius;
    }

    ctx->initialised = 1;
    return 0;

fail:
    if (ctx->command_queue)
        clReleaseCommandQueue(ctx->command_queue);
    if (ctx->kernel_horiz)
        clReleaseKernel(ctx->kernel_horiz);
    if (ctx->kernel_vert)
        clReleaseKernel(ctx->kernel_vert);
    return err;
}

static int avgblur_opencl_filter_frame(AVFilterLink *inlink, AVFrame *input)
{
    AVFilterContext    *avctx = inlink->dst;
    AVFilterLink     *outlink = avctx->outputs[0];
    AverageBlurOpenCLContext *ctx = avctx->priv;
    AVFrame *output = NULL;
    AVFrame *intermediate = NULL;
    cl_int cle;
    size_t global_work[2];
    cl_mem src, dst, inter;
    int err, p, radius_x, radius_y;

    av_log(ctx, AV_LOG_DEBUG, "Filter input: %s, %ux%u (%"PRId64").\n",
           av_get_pix_fmt_name(input->format),
           input->width, input->height, input->pts);

    if (!input->hw_frames_ctx)
        return AVERROR(EINVAL);

    if (!ctx->initialised) {
        err = avgblur_opencl_init(avctx);
        if (err < 0)
            goto fail;

    }

    output = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!output) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    intermediate = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!intermediate) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    for (p = 0; p < FF_ARRAY_ELEMS(output->data); p++) {
        src = (cl_mem) input->data[p];
        dst = (cl_mem)output->data[p];
        inter = (cl_mem) intermediate->data[p];

        if (!dst)
            break;

        radius_x = ctx->radius;
        radius_y = ctx->radiusV;

        if (!(ctx->planes & (1 << p))) {
            radius_x = 0;
            radius_y = 0;
        }

        cle = clSetKernelArg(ctx->kernel_horiz, 0, sizeof(cl_mem), &inter);
        if (cle != CL_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to set kernel "
                   "destination image argument: %d.\n", cle);
            err = AVERROR_UNKNOWN;
            goto fail;
        }
        cle = clSetKernelArg(ctx->kernel_horiz, 1, sizeof(cl_mem), &src);
        if (cle != CL_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to set kernel "
                   "source image argument: %d.\n", cle);
            err = AVERROR_UNKNOWN;
            goto fail;
        }
        cle = clSetKernelArg(ctx->kernel_horiz, 2, sizeof(cl_int), &radius_x);
        if (cle != CL_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to set kernel "
                   "sizeX argument: %d.\n", cle);
            err = AVERROR_UNKNOWN;
            goto fail;
        }

        err = ff_opencl_filter_work_size_from_image(avctx, global_work,
                                                    intermediate, p, 0);
        if (err < 0)
            goto fail;

        av_log(avctx, AV_LOG_DEBUG, "Run kernel on plane %d "
               "(%"SIZE_SPECIFIER"x%"SIZE_SPECIFIER").\n",
               p, global_work[0], global_work[1]);

        cle = clEnqueueNDRangeKernel(ctx->command_queue, ctx->kernel_horiz, 2, NULL,
                                     global_work, NULL,
                                     0, NULL, NULL);
        if (cle != CL_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to enqueue kernel: %d.\n",
                   cle);
            err = AVERROR(EIO);
            goto fail;
        }

        cle = clSetKernelArg(ctx->kernel_vert, 0, sizeof(cl_mem), &dst);
        if (cle != CL_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to set kernel "
                   "destination image argument: %d.\n", cle);
            err = AVERROR_UNKNOWN;
            goto fail;
        }
        cle = clSetKernelArg(ctx->kernel_vert, 1, sizeof(cl_mem), &inter);
        if (cle != CL_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to set kernel "
                   "source image argument: %d.\n", cle);
            err = AVERROR_UNKNOWN;
            goto fail;
        }
        cle = clSetKernelArg(ctx->kernel_vert, 2, sizeof(cl_int), &radius_y);
        if (cle != CL_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to set kernel "
                   "sizeY argument: %d.\n", cle);
            err = AVERROR_UNKNOWN;
            goto fail;
        }

        err = ff_opencl_filter_work_size_from_image(avctx, global_work,
                                                    output, p, 0);
        if (err < 0)
            goto fail;

        av_log(avctx, AV_LOG_DEBUG, "Run kernel on plane %d "
               "(%"SIZE_SPECIFIER"x%"SIZE_SPECIFIER").\n",
               p, global_work[0], global_work[1]);

        cle = clEnqueueNDRangeKernel(ctx->command_queue, ctx->kernel_vert, 2, NULL,
                                     global_work, NULL,
                                     0, NULL, NULL);
        if (cle != CL_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to enqueue kernel: %d.\n",
                   cle);
            err = AVERROR(EIO);
            goto fail;
        }

    }

    cle = clFinish(ctx->command_queue);
    if (cle != CL_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to finish command queue: %d.\n",
               cle);
        err = AVERROR(EIO);
        goto fail;
    }

    err = av_frame_copy_props(output, input);
    if (err < 0)
        goto fail;

    av_frame_free(&input);
    av_frame_free(&intermediate);

    av_log(ctx, AV_LOG_DEBUG, "Filter output: %s, %ux%u (%"PRId64").\n",
           av_get_pix_fmt_name(output->format),
           output->width, output->height, output->pts);

    return ff_filter_frame(outlink, output);

fail:
    clFinish(ctx->command_queue);
    av_frame_free(&input);
    av_frame_free(&output);
    av_frame_free(&intermediate);
    return err;
}

static av_cold void avgblur_opencl_uninit(AVFilterContext *avctx)
{
    AverageBlurOpenCLContext *ctx = avctx->priv;
    cl_int cle;


    if (ctx->kernel_horiz) {
        cle = clReleaseKernel(ctx->kernel_horiz);
        if (cle != CL_SUCCESS)
            av_log(avctx, AV_LOG_ERROR, "Failed to release "
                   "kernel: %d.\n", cle);
    }

    if (ctx->kernel_vert) {
        cle = clReleaseKernel(ctx->kernel_vert);
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

#define OFFSET(x) offsetof(AverageBlurOpenCLContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption avgblur_opencl_options[] = {
    { "sizeX",  "set horizontal size",  OFFSET(radius),  AV_OPT_TYPE_INT, {.i64=1},   1, 1024, FLAGS },
    { "planes", "set planes to filter", OFFSET(planes),  AV_OPT_TYPE_INT, {.i64=0xF}, 0,  0xF, FLAGS },
    { "sizeY",  "set vertical size",    OFFSET(radiusV), AV_OPT_TYPE_INT, {.i64=0},   0, 1024, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(avgblur_opencl);

static const AVFilterPad avgblur_opencl_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &avgblur_opencl_filter_frame,
        .config_props = &ff_opencl_filter_config_input,
    },
    { NULL }
};

static const AVFilterPad avgblur_opencl_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_opencl_filter_config_output,
    },
    { NULL }
};

AVFilter ff_vf_avgblur_opencl = {
    .name           = "avgblur_opencl",
    .description    = NULL_IF_CONFIG_SMALL("Apply average blur filter"),
    .priv_size      = sizeof(AverageBlurOpenCLContext),
    .priv_class     = &avgblur_opencl_class,
    .init           = &ff_opencl_filter_init,
    .uninit         = &avgblur_opencl_uninit,
    .query_formats  = &ff_opencl_filter_query_formats,
    .inputs         = avgblur_opencl_inputs,
    .outputs        = avgblur_opencl_outputs,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
