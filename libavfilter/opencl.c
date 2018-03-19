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

#include <stdio.h>
#include <string.h>

#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_opencl.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "formats.h"
#include "opencl.h"

int ff_opencl_filter_query_formats(AVFilterContext *avctx)
{
    const static enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_OPENCL,
        AV_PIX_FMT_NONE,
    };
    AVFilterFormats *formats;

    formats = ff_make_format_list(pix_fmts);
    if (!formats)
        return AVERROR(ENOMEM);

    return ff_set_common_formats(avctx, formats);
}

static int opencl_filter_set_device(AVFilterContext *avctx,
                                    AVBufferRef *device)
{
    OpenCLFilterContext *ctx = avctx->priv;

    av_buffer_unref(&ctx->device_ref);

    ctx->device_ref = av_buffer_ref(device);
    if (!ctx->device_ref)
        return AVERROR(ENOMEM);

    ctx->device = (AVHWDeviceContext*)ctx->device_ref->data;
    ctx->hwctx  = ctx->device->hwctx;

    return 0;
}

int ff_opencl_filter_config_input(AVFilterLink *inlink)
{
    AVFilterContext   *avctx = inlink->dst;
    OpenCLFilterContext *ctx = avctx->priv;
    AVHWFramesContext *input_frames;
    int err;

    if (!inlink->hw_frames_ctx) {
        av_log(avctx, AV_LOG_ERROR, "OpenCL filtering requires a "
               "hardware frames context on the input.\n");
        return AVERROR(EINVAL);
    }

    // Extract the device and default output format from the first input.
    if (avctx->inputs[0] != inlink)
        return 0;

    input_frames = (AVHWFramesContext*)inlink->hw_frames_ctx->data;
    if (input_frames->format != AV_PIX_FMT_OPENCL)
        return AVERROR(EINVAL);

    err = opencl_filter_set_device(avctx, input_frames->device_ref);
    if (err < 0)
        return err;

    // Default output parameters match input parameters.
    if (ctx->output_format == AV_PIX_FMT_NONE)
        ctx->output_format = input_frames->sw_format;
    if (!ctx->output_width)
        ctx->output_width  = inlink->w;
    if (!ctx->output_height)
        ctx->output_height = inlink->h;

    return 0;
}

int ff_opencl_filter_config_output(AVFilterLink *outlink)
{
    AVFilterContext   *avctx = outlink->src;
    OpenCLFilterContext *ctx = avctx->priv;
    AVBufferRef       *output_frames_ref = NULL;
    AVHWFramesContext *output_frames;
    int err;

    av_buffer_unref(&outlink->hw_frames_ctx);

    if (!ctx->device_ref) {
        if (!avctx->hw_device_ctx) {
            av_log(avctx, AV_LOG_ERROR, "OpenCL filtering requires an "
                   "OpenCL device.\n");
            return AVERROR(EINVAL);
        }

        err = opencl_filter_set_device(avctx, avctx->hw_device_ctx);
        if (err < 0)
            return err;
    }

    output_frames_ref = av_hwframe_ctx_alloc(ctx->device_ref);
    if (!output_frames_ref) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    output_frames = (AVHWFramesContext*)output_frames_ref->data;

    output_frames->format    = AV_PIX_FMT_OPENCL;
    output_frames->sw_format = ctx->output_format;
    output_frames->width     = ctx->output_width;
    output_frames->height    = ctx->output_height;

    err = av_hwframe_ctx_init(output_frames_ref);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to initialise output "
               "frames: %d.\n", err);
        goto fail;
    }

    outlink->hw_frames_ctx = output_frames_ref;
    outlink->w = ctx->output_width;
    outlink->h = ctx->output_height;

    return 0;
fail:
    av_buffer_unref(&output_frames_ref);
    return err;
}

int ff_opencl_filter_init(AVFilterContext *avctx)
{
    OpenCLFilterContext *ctx = avctx->priv;

    ctx->output_format = AV_PIX_FMT_NONE;

    return 0;
}

void ff_opencl_filter_uninit(AVFilterContext *avctx)
{
    OpenCLFilterContext *ctx = avctx->priv;
    cl_int cle;

    if (ctx->program) {
        cle = clReleaseProgram(ctx->program);
        if (cle != CL_SUCCESS)
            av_log(avctx, AV_LOG_ERROR, "Failed to release "
                   "program: %d.\n", cle);
    }

    av_buffer_unref(&ctx->device_ref);
}

int ff_opencl_filter_load_program(AVFilterContext *avctx,
                                  const char **program_source_array,
                                  int nb_strings)
{
    OpenCLFilterContext *ctx = avctx->priv;
    cl_int cle;

    ctx->program = clCreateProgramWithSource(ctx->hwctx->context, nb_strings,
                                             program_source_array,
                                             NULL, &cle);
    if (!ctx->program) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create program: %d.\n", cle);
        return AVERROR(EIO);
    }

    cle = clBuildProgram(ctx->program, 1, &ctx->hwctx->device_id,
                         NULL, NULL, NULL);
    if (cle != CL_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to build program: %d.\n", cle);

        if (cle == CL_BUILD_PROGRAM_FAILURE) {
            char *log;
            size_t log_length;

            clGetProgramBuildInfo(ctx->program, ctx->hwctx->device_id,
                                  CL_PROGRAM_BUILD_LOG, 0, NULL, &log_length);

            log = av_malloc(log_length);
            if (log) {
                cle = clGetProgramBuildInfo(ctx->program,
                                            ctx->hwctx->device_id,
                                            CL_PROGRAM_BUILD_LOG,
                                            log_length, log, NULL);
                if (cle == CL_SUCCESS)
                    av_log(avctx, AV_LOG_ERROR, "Build log:\n%s\n", log);
            }

            av_free(log);
        }

        clReleaseProgram(ctx->program);
        ctx->program = NULL;
        return AVERROR(EIO);
    }

    return 0;
}

int ff_opencl_filter_load_program_from_file(AVFilterContext *avctx,
                                            const char *filename)
{
    FILE *file;
    char *src = NULL;
    size_t pos, len, rb;
    const char *src_const;
    int err;

    file = fopen(filename, "r");
    if (!file) {
        av_log(avctx, AV_LOG_ERROR, "Unable to open program "
               "source file \"%s\".\n", filename);
        return AVERROR(ENOENT);
    }

    len = 1 << 16;
    pos = 0;

    err = av_reallocp(&src, len);
    if (err < 0)
        goto fail;

    err = snprintf(src, len, "#line 1 \"%s\"\n", filename);
    if (err < 0) {
        err = AVERROR(errno);
        goto fail;
    }
    if (err > len / 2) {
        err = AVERROR(EINVAL);
        goto fail;
    }
    pos = err;

    while (1) {
        rb = fread(src + pos, 1, len - pos - 1, file);
        if (rb == 0 && ferror(file)) {
            err = AVERROR(EIO);
            goto fail;
        }
        pos += rb;
        if (pos < len)
            break;
        len <<= 1;
        err = av_reallocp(&src, len);
        if (err < 0)
            goto fail;
    }
    src[pos] = 0;

    src_const = src;

    err = ff_opencl_filter_load_program(avctx, &src_const, 1);
fail:
    fclose(file);
    av_freep(&src);
    return err;
}

int ff_opencl_filter_work_size_from_image(AVFilterContext *avctx,
                                          size_t *work_size,
                                          AVFrame *frame, int plane,
                                          int block_alignment)
{
    cl_mem image;
    cl_mem_object_type type;
    size_t width, height;
    cl_int cle;

    if (frame->format != AV_PIX_FMT_OPENCL) {
        av_log(avctx, AV_LOG_ERROR, "Invalid frame format %s, "
               "opencl required.\n", av_get_pix_fmt_name(frame->format));
        return AVERROR(EINVAL);
    }

    image = (cl_mem)frame->data[plane];
    if (!image) {
        av_log(avctx, AV_LOG_ERROR, "Plane %d required but not set.\n",
               plane);
        return AVERROR(EINVAL);
    }

    cle = clGetMemObjectInfo(image, CL_MEM_TYPE, sizeof(type),
                             &type, NULL);
    if (cle != CL_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to query object type of "
               "plane %d: %d.\n", plane, cle);
        return AVERROR_UNKNOWN;
    }
    if (type != CL_MEM_OBJECT_IMAGE2D) {
        av_log(avctx, AV_LOG_ERROR, "Plane %d is not a 2D image.\n",
               plane);
        return AVERROR(EINVAL);
    }

    cle = clGetImageInfo(image, CL_IMAGE_WIDTH,  sizeof(size_t),
                         &width, NULL);
    if (cle != CL_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to query plane %d width: %d.\n",
               plane, cle);
        return AVERROR_UNKNOWN;
    }

    cle = clGetImageInfo(image, CL_IMAGE_HEIGHT, sizeof(size_t),
                         &height, NULL);
    if (cle != CL_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to query plane %d height: %d.\n",
               plane, cle);
        return AVERROR_UNKNOWN;
    }

    if (block_alignment) {
        width  = FFALIGN(width,  block_alignment);
        height = FFALIGN(height, block_alignment);
    }

    work_size[0] = width;
    work_size[1] = height;

    return 0;
}
