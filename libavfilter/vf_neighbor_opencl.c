/*
 * Copyright (c) 2018 Danil Iashchenko
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

#include "config_components.h"

#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/avstring.h"


#include "avfilter.h"
#include "filters.h"
#include "opencl.h"
#include "opencl_source.h"
#include "video.h"

typedef struct NeighborOpenCLContext {
    OpenCLFilterContext ocf;

    int              initialised;
    cl_kernel        kernel;
    cl_command_queue command_queue;

    char *matrix_str[4];

    cl_float threshold[AV_VIDEO_MAX_PLANES];
    cl_int coordinates;
    cl_mem coord;

} NeighborOpenCLContext;

static int neighbor_opencl_init(AVFilterContext *avctx)
{
    NeighborOpenCLContext *ctx = avctx->priv;
    const char *kernel_name;
    cl_int cle;
    int err;

    err = ff_opencl_filter_load_program(avctx, &ff_source_neighbor_cl, 1);
    if (err < 0)
        goto fail;

    ctx->command_queue = clCreateCommandQueue(ctx->ocf.hwctx->context,
                                              ctx->ocf.hwctx->device_id,
                                              0, &cle);
    CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to create OpenCL "
                     "command queue %d.\n", cle);

    if (!strcmp(avctx->filter->name, "erosion_opencl")){
        kernel_name = "erosion_global";
    } else if (!strcmp(avctx->filter->name, "dilation_opencl")){
        kernel_name = "dilation_global";
    }
    ctx->kernel = clCreateKernel(ctx->ocf.program, kernel_name, &cle);
    CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to create "
                     "kernel %d.\n", cle);

    ctx->initialised = 1;
    return 0;

fail:
    if (ctx->command_queue)
        clReleaseCommandQueue(ctx->command_queue);
    if (ctx->kernel)
        clReleaseKernel(ctx->kernel);
    return err;
}

static int neighbor_opencl_make_filter_params(AVFilterContext *avctx)
{
    NeighborOpenCLContext *ctx = avctx->priv;
    cl_int matrix[9];
    cl_mem buffer;
    cl_int cle;
    int i;

    for (i = 0; i < AV_VIDEO_MAX_PLANES; i++) {
        ctx->threshold[i] /= 255.0;
    }

    matrix[4] = 0;
    for (i = 0; i < 8; i++) {
        if (ctx->coordinates & (1 << i)) {
            matrix[i > 3 ? i + 1: i] = 1;
        }
    }
    buffer = clCreateBuffer(ctx->ocf.hwctx->context,
                            CL_MEM_READ_ONLY |
                            CL_MEM_COPY_HOST_PTR |
                            CL_MEM_HOST_NO_ACCESS,
                            9 * sizeof(cl_int), matrix, &cle);
    if (!buffer) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create matrix buffer: "
               "%d.\n", cle);
        return AVERROR(EIO);
    }
    ctx->coord = buffer;

    return 0;
}


static int neighbor_opencl_filter_frame(AVFilterLink *inlink, AVFrame *input)
{
    AVFilterContext *avctx = inlink->dst;
    AVFilterLink *outlink = avctx->outputs[0];
    NeighborOpenCLContext *ctx = avctx->priv;
    AVFrame *output = NULL;
    cl_int cle;
    size_t global_work[2];
    cl_mem src, dst;
    int err, p;
    size_t origin[3] = {0, 0, 0};
    size_t region[3] = {0, 0, 1};

    av_log(ctx, AV_LOG_DEBUG, "Filter input: %s, %ux%u (%"PRId64").\n",
           av_get_pix_fmt_name(input->format),
           input->width, input->height, input->pts);

    if (!input->hw_frames_ctx)
        return AVERROR(EINVAL);

    if (!ctx->initialised) {
        err = neighbor_opencl_init(avctx);
        if (err < 0)
            goto fail;

        err = neighbor_opencl_make_filter_params(avctx);
        if (err < 0)
            goto fail;

    }

    output = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!output) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    for (p = 0; p < FF_ARRAY_ELEMS(output->data); p++) {
        src = (cl_mem) input->data[p];
        dst = (cl_mem)output->data[p];

        if (!dst)
            break;

        if (ctx->threshold[p] == 0) {
            err = ff_opencl_filter_work_size_from_image(avctx, region, output, p, 0);
            if (err < 0)
                goto fail;

            cle = clEnqueueCopyImage(ctx->command_queue, src, dst,
                                     origin, origin, region, 0, NULL, NULL);
            CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to copy plane %d: %d.\n",
                             p, cle);
        } else {
            CL_SET_KERNEL_ARG(ctx->kernel, 0, cl_mem,   &dst);
            CL_SET_KERNEL_ARG(ctx->kernel, 1, cl_mem,   &src);
            CL_SET_KERNEL_ARG(ctx->kernel, 2, cl_float, &ctx->threshold[p]);
            CL_SET_KERNEL_ARG(ctx->kernel, 3, cl_mem,   &ctx->coord);

            err = ff_opencl_filter_work_size_from_image(avctx, global_work, output, p, 0);
            if (err < 0)
                goto fail;

            av_log(avctx, AV_LOG_DEBUG, "Run kernel on plane %d "
                   "(%"SIZE_SPECIFIER"x%"SIZE_SPECIFIER").\n",
                   p, global_work[0], global_work[1]);

            cle = clEnqueueNDRangeKernel(ctx->command_queue, ctx->kernel, 2, NULL,
                                         global_work, NULL,
                                         0, NULL, NULL);
            CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to enqueue "
                             "kernel: %d.\n", cle);
        }
    }

    cle = clFinish(ctx->command_queue);
    CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to finish command queue: %d.\n", cle);

    err = av_frame_copy_props(output, input);
    if (err < 0)
        goto fail;

    av_frame_free(&input);

    av_log(ctx, AV_LOG_DEBUG, "Filter output: %s, %ux%u (%"PRId64").\n",
           av_get_pix_fmt_name(output->format),
           output->width, output->height, output->pts);

    return ff_filter_frame(outlink, output);

fail:
    clFinish(ctx->command_queue);
    av_frame_free(&input);
    av_frame_free(&output);
    return err;
}

static av_cold void neighbor_opencl_uninit(AVFilterContext *avctx)
{
    NeighborOpenCLContext *ctx = avctx->priv;
    cl_int cle;

    clReleaseMemObject(ctx->coord);

    if (ctx->kernel) {
        cle = clReleaseKernel(ctx->kernel);
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

static const AVFilterPad neighbor_opencl_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &neighbor_opencl_filter_frame,
        .config_props = &ff_opencl_filter_config_input,
    },
};

static const AVFilterPad neighbor_opencl_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_opencl_filter_config_output,
    },
};

#define OFFSET(x) offsetof(NeighborOpenCLContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)

#if CONFIG_EROSION_OPENCL_FILTER

static const AVOption erosion_opencl_options[] = {
    { "threshold0",  "set threshold for 1st plane",   OFFSET(threshold[0]),   AV_OPT_TYPE_FLOAT, {.dbl=65535.0}, 0.0, 65535, FLAGS },
    { "threshold1",  "set threshold for 2nd plane",   OFFSET(threshold[1]),   AV_OPT_TYPE_FLOAT, {.dbl=65535.0}, 0.0, 65535, FLAGS },
    { "threshold2",  "set threshold for 3rd plane",   OFFSET(threshold[2]),   AV_OPT_TYPE_FLOAT, {.dbl=65535.0}, 0.0, 65535, FLAGS },
    { "threshold3",  "set threshold for 4th plane",   OFFSET(threshold[3]),   AV_OPT_TYPE_FLOAT, {.dbl=65535.0}, 0.0, 65535, FLAGS },
    { "coordinates", "set coordinates",               OFFSET(coordinates),    AV_OPT_TYPE_INT,   {.i64=255},     0,   255,   FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(erosion_opencl);

const AVFilter ff_vf_erosion_opencl = {
    .name           = "erosion_opencl",
    .description    = NULL_IF_CONFIG_SMALL("Apply erosion effect"),
    .priv_size      = sizeof(NeighborOpenCLContext),
    .priv_class     = &erosion_opencl_class,
    .init           = &ff_opencl_filter_init,
    .uninit         = &neighbor_opencl_uninit,
    FILTER_INPUTS(neighbor_opencl_inputs),
    FILTER_OUTPUTS(neighbor_opencl_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_OPENCL),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};

#endif /* CONFIG_EROSION_OPENCL_FILTER */

#if CONFIG_DILATION_OPENCL_FILTER

static const AVOption dilation_opencl_options[] = {
    { "threshold0",  "set threshold for 1st plane",   OFFSET(threshold[0]),   AV_OPT_TYPE_FLOAT, {.dbl=65535.0}, 0.0, 65535, FLAGS },
    { "threshold1",  "set threshold for 2nd plane",   OFFSET(threshold[1]),   AV_OPT_TYPE_FLOAT, {.dbl=65535.0}, 0.0, 65535, FLAGS },
    { "threshold2",  "set threshold for 3rd plane",   OFFSET(threshold[2]),   AV_OPT_TYPE_FLOAT, {.dbl=65535.0}, 0.0, 65535, FLAGS },
    { "threshold3",  "set threshold for 4th plane",   OFFSET(threshold[3]),   AV_OPT_TYPE_FLOAT, {.dbl=65535.0}, 0.0, 65535, FLAGS },
    { "coordinates", "set coordinates",               OFFSET(coordinates),    AV_OPT_TYPE_INT,   {.i64=255},     0,   255,   FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(dilation_opencl);

const AVFilter ff_vf_dilation_opencl = {
    .name           = "dilation_opencl",
    .description    = NULL_IF_CONFIG_SMALL("Apply dilation effect"),
    .priv_size      = sizeof(NeighborOpenCLContext),
    .priv_class     = &dilation_opencl_class,
    .init           = &ff_opencl_filter_init,
    .uninit         = &neighbor_opencl_uninit,
    FILTER_INPUTS(neighbor_opencl_inputs),
    FILTER_OUTPUTS(neighbor_opencl_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_OPENCL),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
    .flags          = AVFILTER_FLAG_HWDEVICE,
};

#endif /* CONFIG_DILATION_OPENCL_FILTER */
