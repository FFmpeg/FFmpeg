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

#define MAX_DIAMETER 23

typedef struct UnsharpOpenCLContext {
    OpenCLFilterContext ocf;

    int              initialised;
    cl_kernel        kernel;
    cl_command_queue command_queue;

    float luma_size_x;
    float luma_size_y;
    float luma_amount;
    float chroma_size_x;
    float chroma_size_y;
    float chroma_amount;

    int global;

    int nb_planes;
    struct {
        float blur_x[MAX_DIAMETER];
        float blur_y[MAX_DIAMETER];

        cl_mem   matrix;
        cl_mem   coef_x;
        cl_mem   coef_y;

        cl_int   size_x;
        cl_int   size_y;
        cl_float amount;
        cl_float threshold;
    } plane[4];
} UnsharpOpenCLContext;


static int unsharp_opencl_init(AVFilterContext *avctx)
{
    UnsharpOpenCLContext *ctx = avctx->priv;
    cl_int cle;
    int err;

    err = ff_opencl_filter_load_program(avctx, &ff_source_unsharp_cl, 1);
    if (err < 0)
        goto fail;

    ctx->command_queue = clCreateCommandQueue(ctx->ocf.hwctx->context,
                                              ctx->ocf.hwctx->device_id,
                                              0, &cle);
    CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to create OpenCL "
                     "command queue %d.\n", cle);

    // Use global kernel if mask size will be too big for the local store..
    ctx->global = (ctx->luma_size_x   > 17.0f ||
                   ctx->luma_size_y   > 17.0f ||
                   ctx->chroma_size_x > 17.0f ||
                   ctx->chroma_size_y > 17.0f);

    ctx->kernel = clCreateKernel(ctx->ocf.program,
                                 ctx->global ? "unsharp_global"
                                             : "unsharp_local", &cle);
    CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to create kernel %d.\n", cle);

    ctx->initialised = 1;
    return 0;

fail:
    if (ctx->command_queue)
        clReleaseCommandQueue(ctx->command_queue);
    if (ctx->kernel)
        clReleaseKernel(ctx->kernel);
    return err;
}

static int unsharp_opencl_make_filter_params(AVFilterContext *avctx)
{
    UnsharpOpenCLContext *ctx = avctx->priv;
    const AVPixFmtDescriptor *desc;
    float *matrix;
    double val, sum;
    cl_int cle;
    cl_mem buffer;
    size_t matrix_bytes;
    float diam_x, diam_y, amount;
    int err, p, x, y, size_x, size_y;

    desc = av_pix_fmt_desc_get(ctx->ocf.output_format);

    ctx->nb_planes = 0;
    for (p = 0; p < desc->nb_components; p++)
        ctx->nb_planes = FFMAX(ctx->nb_planes, desc->comp[p].plane + 1);

    for (p = 0; p < ctx->nb_planes; p++) {
        if (p == 0 || (desc->flags & AV_PIX_FMT_FLAG_RGB)) {
            diam_x = ctx->luma_size_x;
            diam_y = ctx->luma_size_y;
            amount = ctx->luma_amount;
        } else {
            diam_x = ctx->chroma_size_x;
            diam_y = ctx->chroma_size_y;
            amount = ctx->chroma_amount;
        }
        size_x = (int)ceil(diam_x) | 1;
        size_y = (int)ceil(diam_y) | 1;
        matrix_bytes = size_x * size_y * sizeof(float);

        matrix = av_malloc(matrix_bytes);
        if (!matrix) {
            err = AVERROR(ENOMEM);
            goto fail;
        }

        sum = 0.0;
        for (x = 0; x < size_x; x++) {
            double dx = (double)(x - size_x / 2) / diam_x;
            sum += ctx->plane[p].blur_x[x] = exp(-16.0 * (dx * dx));
        }
        for (x = 0; x < size_x; x++)
            ctx->plane[p].blur_x[x] /= sum;

        sum = 0.0;
        for (y = 0; y < size_y; y++) {
            double dy = (double)(y - size_y / 2) / diam_y;
            sum += ctx->plane[p].blur_y[y] = exp(-16.0 * (dy * dy));
        }
        for (y = 0; y < size_y; y++)
            ctx->plane[p].blur_y[y] /= sum;

        for (y = 0; y < size_y; y++) {
            for (x = 0; x < size_x; x++) {
                val = ctx->plane[p].blur_x[x] * ctx->plane[p].blur_y[y];
                matrix[y * size_x + x] = val;
            }
        }

        if (ctx->global) {
            buffer = clCreateBuffer(ctx->ocf.hwctx->context,
                                    CL_MEM_READ_ONLY     |
                                    CL_MEM_COPY_HOST_PTR |
                                    CL_MEM_HOST_NO_ACCESS,
                                    matrix_bytes, matrix, &cle);
            CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to create matrix buffer: "
                             "%d.\n", cle);
            ctx->plane[p].matrix = buffer;
        } else {
            buffer = clCreateBuffer(ctx->ocf.hwctx->context,
                                    CL_MEM_READ_ONLY     |
                                    CL_MEM_COPY_HOST_PTR |
                                    CL_MEM_HOST_NO_ACCESS,
                                    sizeof(ctx->plane[p].blur_x),
                                    ctx->plane[p].blur_x, &cle);
            CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to create x-coef buffer: "
                             "%d.\n", cle);
            ctx->plane[p].coef_x = buffer;

            buffer = clCreateBuffer(ctx->ocf.hwctx->context,
                                    CL_MEM_READ_ONLY     |
                                    CL_MEM_COPY_HOST_PTR |
                                    CL_MEM_HOST_NO_ACCESS,
                                    sizeof(ctx->plane[p].blur_y),
                                    ctx->plane[p].blur_y, &cle);
            CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to create y-coef buffer: "
                             "%d.\n", cle);
            ctx->plane[p].coef_y = buffer;
        }

        av_freep(&matrix);

        ctx->plane[p].size_x = size_x;
        ctx->plane[p].size_y = size_y;
        ctx->plane[p].amount = amount;
    }

    err = 0;
fail:
    av_freep(&matrix);
    return err;
}

static int unsharp_opencl_filter_frame(AVFilterLink *inlink, AVFrame *input)
{
    AVFilterContext    *avctx = inlink->dst;
    AVFilterLink     *outlink = avctx->outputs[0];
    UnsharpOpenCLContext *ctx = avctx->priv;
    AVFrame *output = NULL;
    cl_int cle;
    size_t global_work[2];
    size_t local_work[2];
    cl_mem src, dst;
    int err, p;

    av_log(ctx, AV_LOG_DEBUG, "Filter input: %s, %ux%u (%"PRId64").\n",
           av_get_pix_fmt_name(input->format),
           input->width, input->height, input->pts);

    if (!input->hw_frames_ctx)
        return AVERROR(EINVAL);

    if (!ctx->initialised) {
        err = unsharp_opencl_init(avctx);
        if (err < 0)
            goto fail;

        err = unsharp_opencl_make_filter_params(avctx);
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

        CL_SET_KERNEL_ARG(ctx->kernel, 0, cl_mem,   &dst);
        CL_SET_KERNEL_ARG(ctx->kernel, 1, cl_mem,   &src);
        CL_SET_KERNEL_ARG(ctx->kernel, 2, cl_int,   &ctx->plane[p].size_x);
        CL_SET_KERNEL_ARG(ctx->kernel, 3, cl_int,   &ctx->plane[p].size_y);
        CL_SET_KERNEL_ARG(ctx->kernel, 4, cl_float, &ctx->plane[p].amount);

        if (ctx->global) {
            CL_SET_KERNEL_ARG(ctx->kernel, 5, cl_mem, &ctx->plane[p].matrix);
        } else {
            CL_SET_KERNEL_ARG(ctx->kernel, 5, cl_mem, &ctx->plane[p].coef_x);
            CL_SET_KERNEL_ARG(ctx->kernel, 6, cl_mem, &ctx->plane[p].coef_y);
        }

        err = ff_opencl_filter_work_size_from_image(avctx, global_work, output, p,
                                                    ctx->global ? 0 : 16);
        if (err < 0)
            goto fail;

        local_work[0]  = 16;
        local_work[1]  = 16;

        av_log(avctx, AV_LOG_DEBUG, "Run kernel on plane %d "
               "(%"SIZE_SPECIFIER"x%"SIZE_SPECIFIER").\n",
               p, global_work[0], global_work[1]);

        cle = clEnqueueNDRangeKernel(ctx->command_queue, ctx->kernel, 2, NULL,
                                     global_work, ctx->global ? NULL : local_work,
                                     0, NULL, NULL);
        CL_FAIL_ON_ERROR(AVERROR(EIO), "Failed to enqueue kernel: %d.\n", cle);
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

static av_cold void unsharp_opencl_uninit(AVFilterContext *avctx)
{
    UnsharpOpenCLContext *ctx = avctx->priv;
    cl_int cle;
    int i;

    for (i = 0; i < ctx->nb_planes; i++) {
        if (ctx->plane[i].matrix)
            clReleaseMemObject(ctx->plane[i].matrix);
        if (ctx->plane[i].coef_x)
            clReleaseMemObject(ctx->plane[i].coef_x);
        if (ctx->plane[i].coef_y)
            clReleaseMemObject(ctx->plane[i].coef_y);
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

#define OFFSET(x) offsetof(UnsharpOpenCLContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption unsharp_opencl_options[] = {
    { "luma_msize_x",     "Set luma mask horizontal diameter (pixels)",
      OFFSET(luma_size_x),     AV_OPT_TYPE_FLOAT,
      { .dbl = 5.0 },   1, MAX_DIAMETER, FLAGS },
    { "lx",               "Set luma mask horizontal diameter (pixels)",
      OFFSET(luma_size_x),     AV_OPT_TYPE_FLOAT,
      { .dbl = 5.0 },   1, MAX_DIAMETER, FLAGS },
    { "luma_msize_y",     "Set luma mask vertical diameter (pixels)",
      OFFSET(luma_size_y),     AV_OPT_TYPE_FLOAT,
      { .dbl = 5.0 },   1, MAX_DIAMETER, FLAGS },
    { "ly",               "Set luma mask vertical diameter (pixels)",
      OFFSET(luma_size_y),     AV_OPT_TYPE_FLOAT,
      { .dbl = 5.0 },   1, MAX_DIAMETER, FLAGS },
    { "luma_amount",      "Set luma amount (multiplier)",
      OFFSET(luma_amount),     AV_OPT_TYPE_FLOAT,
      { .dbl = 1.0 }, -10, 10, FLAGS },
    { "la",               "Set luma amount (multiplier)",
      OFFSET(luma_amount),     AV_OPT_TYPE_FLOAT,
      { .dbl = 1.0 }, -10, 10, FLAGS },

    { "chroma_msize_x",   "Set chroma mask horizontal diameter (pixels after subsampling)",
      OFFSET(chroma_size_x),   AV_OPT_TYPE_FLOAT,
      { .dbl = 5.0 },   1, MAX_DIAMETER, FLAGS },
    { "cx",               "Set chroma mask horizontal diameter (pixels after subsampling)",
      OFFSET(chroma_size_x),   AV_OPT_TYPE_FLOAT,
      { .dbl = 5.0 },   1, MAX_DIAMETER, FLAGS },
    { "chroma_msize_y",   "Set chroma mask vertical diameter (pixels after subsampling)",
      OFFSET(chroma_size_y),   AV_OPT_TYPE_FLOAT,
      { .dbl = 5.0 },   1, MAX_DIAMETER, FLAGS },
    { "cy",               "Set chroma mask vertical diameter (pixels after subsampling)",
      OFFSET(chroma_size_y),   AV_OPT_TYPE_FLOAT,
      { .dbl = 5.0 },   1, MAX_DIAMETER, FLAGS },
    { "chroma_amount",    "Set chroma amount (multiplier)",
      OFFSET(chroma_amount),   AV_OPT_TYPE_FLOAT,
      { .dbl = 0.0 }, -10, 10, FLAGS },
    { "ca",               "Set chroma amount (multiplier)",
      OFFSET(chroma_amount),   AV_OPT_TYPE_FLOAT,
      { .dbl = 0.0 }, -10, 10, FLAGS },

    { NULL }
};

AVFILTER_DEFINE_CLASS(unsharp_opencl);

static const AVFilterPad unsharp_opencl_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &unsharp_opencl_filter_frame,
        .config_props = &ff_opencl_filter_config_input,
    },
};

static const AVFilterPad unsharp_opencl_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_opencl_filter_config_output,
    },
};

const AVFilter ff_vf_unsharp_opencl = {
    .name           = "unsharp_opencl",
    .description    = NULL_IF_CONFIG_SMALL("Apply unsharp mask to input video"),
    .priv_size      = sizeof(UnsharpOpenCLContext),
    .priv_class     = &unsharp_opencl_class,
    .init           = &ff_opencl_filter_init,
    .uninit         = &unsharp_opencl_uninit,
    FILTER_INPUTS(unsharp_opencl_inputs),
    FILTER_OUTPUTS(unsharp_opencl_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_OPENCL),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
    .flags          = AVFILTER_FLAG_HWDEVICE,
};
