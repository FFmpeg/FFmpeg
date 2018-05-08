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

#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/avstring.h"


#include "avfilter.h"
#include "internal.h"
#include "opencl.h"
#include "opencl_source.h"
#include "video.h"

typedef struct ConvolutionOpenCLContext {
    OpenCLFilterContext ocf;

    int              initialised;
    cl_kernel        kernel;
    cl_command_queue command_queue;

    char *matrix_str[4];

    cl_mem matrix[4];
    cl_int matrix_sizes[4];
    cl_int dims[4];
    cl_float rdivs[4];
    cl_float biases[4];

} ConvolutionOpenCLContext;


static int convolution_opencl_init(AVFilterContext *avctx)
{
    ConvolutionOpenCLContext *ctx = avctx->priv;
    cl_int cle;
    int err;

    err = ff_opencl_filter_load_program(avctx, &ff_opencl_source_convolution, 1);
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

    ctx->kernel = clCreateKernel(ctx->ocf.program, "convolution_global", &cle);
    if (!ctx->kernel) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create kernel: %d.\n", cle);
        err = AVERROR(EIO);
        goto fail;
    }

    ctx->initialised = 1;
    return 0;

fail:
    if (ctx->command_queue)
        clReleaseCommandQueue(ctx->command_queue);
    if (ctx->kernel)
        clReleaseKernel(ctx->kernel);
    return err;
}



static int convolution_opencl_make_filter_params(AVFilterContext *avctx)
{
    ConvolutionOpenCLContext *ctx = avctx->priv;
    float *matrix = NULL;
    size_t matrix_bytes;
    cl_mem buffer;
    cl_int cle;
    int i, j;
    int sscanf_err;
    char *p, *arg, *saveptr = NULL;
    float input_matrix[4][49];

    for (i = 0; i < 4; i++) {
        ctx->biases[i] = ctx->biases[i] / 255.0;
    }

    for (i = 0; i < 4; i++) {
        p = ctx->matrix_str[i];
        while (ctx->matrix_sizes[i] < 49) {
            arg = av_strtok(p, " ", &saveptr);
            if (!arg) {
                break;
            }
            p = NULL;
            sscanf_err = sscanf(arg, "%f", &input_matrix[i][ctx->matrix_sizes[i]]);
            if (sscanf_err != 1) {
                av_log(ctx, AV_LOG_ERROR, "Matrix is sequence of 9, 25 or 49 signed numbers\n");
                return AVERROR(EINVAL);
            }
            ctx->matrix_sizes[i]++;
        }
        if (ctx->matrix_sizes[i] == 9) {
            ctx->dims[i] = 3;
        } else if (ctx->matrix_sizes[i] == 25) {
            ctx->dims[i] = 5;
        } else if (ctx->matrix_sizes[i] == 49) {
            ctx->dims[i] = 7;
        } else {
            av_log(ctx, AV_LOG_ERROR, "Invalid matrix size:%d\n", ctx->matrix_sizes[i]);
            return AVERROR(EINVAL);
        }

    }

    for (j = 0; j < 4; j++) {
        matrix_bytes = sizeof(float)*ctx->matrix_sizes[j];
        matrix = av_malloc(matrix_bytes);
        if (!matrix) {
            av_freep(&matrix);
            return AVERROR(ENOMEM);
        }

        for (i = 0; i < ctx->matrix_sizes[j]; i++)
            matrix[i] = input_matrix[j][i];

        buffer = clCreateBuffer(ctx->ocf.hwctx->context,
                                CL_MEM_READ_ONLY |
                                CL_MEM_COPY_HOST_PTR |
                                CL_MEM_HOST_NO_ACCESS,
                                matrix_bytes, matrix, &cle);
        if (!buffer) {
            av_log(avctx, AV_LOG_ERROR, "Failed to create matrix buffer: "
                   "%d.\n", cle);
            av_freep(&matrix);
            return AVERROR(EIO);
        }
        ctx->matrix[j] = buffer;
        av_freep(&matrix);
    }

    return 0;
}

static int convolution_opencl_filter_frame(AVFilterLink *inlink, AVFrame *input)
{
    AVFilterContext *avctx = inlink->dst;
    AVFilterLink *outlink = avctx->outputs[0];
    ConvolutionOpenCLContext *ctx = avctx->priv;
    AVFrame *output = NULL;
    cl_int cle;
    size_t global_work[2];
    cl_mem src, dst;
    int err, p;

    av_log(ctx, AV_LOG_DEBUG, "Filter input: %s, %ux%u (%"PRId64").\n",
           av_get_pix_fmt_name(input->format),
           input->width, input->height, input->pts);

    if (!input->hw_frames_ctx)
        return AVERROR(EINVAL);

    if (!ctx->initialised) {
        err = convolution_opencl_init(avctx);
        if (err < 0)
            goto fail;

        err = convolution_opencl_make_filter_params(avctx);
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

        cle = clSetKernelArg(ctx->kernel, 0, sizeof(cl_mem), &dst);
        if (cle != CL_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to set kernel "
                   "destination image argument: %d.\n", cle);
            goto fail;
        }
        cle = clSetKernelArg(ctx->kernel, 1, sizeof(cl_mem), &src);
        if (cle != CL_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to set kernel "
                   "source image argument: %d.\n", cle);
            goto fail;
        }
        cle = clSetKernelArg(ctx->kernel, 2, sizeof(cl_int), &ctx->dims[p]);
        if (cle != CL_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to set kernel "
                   "matrix size argument: %d.\n", cle);
            goto fail;
        }
        cle = clSetKernelArg(ctx->kernel, 3, sizeof(cl_mem), &ctx->matrix[p]);
        if (cle != CL_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to set kernel "
                   "matrix argument: %d.\n", cle);
            goto fail;
        }
        cle = clSetKernelArg(ctx->kernel, 4, sizeof(cl_float), &ctx->rdivs[p]);
        if (cle != CL_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to set kernel "
                   "rdiv argument: %d.\n", cle);
            goto fail;
        }
        cle = clSetKernelArg(ctx->kernel, 5, sizeof(cl_float), &ctx->biases[p]);
        if (cle != CL_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to set kernel "
                   "bias argument: %d.\n", cle);
            goto fail;
        }


        err = ff_opencl_filter_work_size_from_image(avctx, global_work, output, p, 0);
        if (err < 0)
            goto fail;

        av_log(avctx, AV_LOG_DEBUG, "Run kernel on plane %d "
               "(%"SIZE_SPECIFIER"x%"SIZE_SPECIFIER").\n",
               p, global_work[0], global_work[1]);

        cle = clEnqueueNDRangeKernel(ctx->command_queue, ctx->kernel, 2, NULL,
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

static av_cold void convolution_opencl_uninit(AVFilterContext *avctx)
{
    ConvolutionOpenCLContext *ctx = avctx->priv;
    cl_int cle;
    int i;

    for (i = 0; i < 4; i++) {
        clReleaseMemObject(ctx->matrix[i]);
    }

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

#define OFFSET(x) offsetof(ConvolutionOpenCLContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption convolution_opencl_options[] = {
    { "0m", "set matrix for 2nd plane", OFFSET(matrix_str[0]), AV_OPT_TYPE_STRING, {.str="0 0 0 0 1 0 0 0 0"}, 0, 0, FLAGS },
    { "1m", "set matrix for 2nd plane", OFFSET(matrix_str[1]), AV_OPT_TYPE_STRING, {.str="0 0 0 0 1 0 0 0 0"}, 0, 0, FLAGS },
    { "2m", "set matrix for 3rd plane", OFFSET(matrix_str[2]), AV_OPT_TYPE_STRING, {.str="0 0 0 0 1 0 0 0 0"}, 0, 0, FLAGS },
    { "3m", "set matrix for 4th plane", OFFSET(matrix_str[3]), AV_OPT_TYPE_STRING, {.str="0 0 0 0 1 0 0 0 0"}, 0, 0, FLAGS },
    { "0rdiv", "set rdiv for 1nd plane", OFFSET(rdivs[0]), AV_OPT_TYPE_FLOAT, {.dbl=1.0}, 0.0, INT_MAX, FLAGS},
    { "1rdiv", "set rdiv for 2nd plane", OFFSET(rdivs[1]), AV_OPT_TYPE_FLOAT, {.dbl=1.0}, 0.0, INT_MAX, FLAGS},
    { "2rdiv", "set rdiv for 3rd plane", OFFSET(rdivs[2]), AV_OPT_TYPE_FLOAT, {.dbl=1.0}, 0.0, INT_MAX, FLAGS},
    { "3rdiv", "set rdiv for 4th plane", OFFSET(rdivs[3]), AV_OPT_TYPE_FLOAT, {.dbl=1.0}, 0.0, INT_MAX, FLAGS},
    { "0bias", "set bias for 1st plane", OFFSET(biases[0]), AV_OPT_TYPE_FLOAT, {.dbl=0.0}, 0.0, INT_MAX, FLAGS},
    { "1bias", "set bias for 2nd plane", OFFSET(biases[1]), AV_OPT_TYPE_FLOAT, {.dbl=0.0}, 0.0, INT_MAX, FLAGS},
    { "2bias", "set bias for 3rd plane", OFFSET(biases[2]), AV_OPT_TYPE_FLOAT, {.dbl=0.0}, 0.0, INT_MAX, FLAGS},
    { "3bias", "set bias for 4th plane", OFFSET(biases[3]), AV_OPT_TYPE_FLOAT, {.dbl=0.0}, 0.0, INT_MAX, FLAGS},
    { NULL }
};

AVFILTER_DEFINE_CLASS(convolution_opencl);

static const AVFilterPad convolution_opencl_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &convolution_opencl_filter_frame,
        .config_props = &ff_opencl_filter_config_input,
    },
    { NULL }
};

static const AVFilterPad convolution_opencl_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_opencl_filter_config_output,
    },
    { NULL }
};

AVFilter ff_vf_convolution_opencl = {
    .name           = "convolution_opencl",
    .description    = NULL_IF_CONFIG_SMALL("Apply convolution mask to input video"),
    .priv_size      = sizeof(ConvolutionOpenCLContext),
    .priv_class     = &convolution_opencl_class,
    .init           = &ff_opencl_filter_init,
    .uninit         = &convolution_opencl_uninit,
    .query_formats  = &ff_opencl_filter_query_formats,
    .inputs         = convolution_opencl_inputs,
    .outputs        = convolution_opencl_outputs,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
