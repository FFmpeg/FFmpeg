/*
 * Copyright (C) 2013 Wei Gao <weigao@multicorewareinc.com>
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
 * transform input video
 */

#include "libavutil/common.h"
#include "libavutil/dict.h"
#include "libavutil/pixdesc.h"
#include "deshake_opencl.h"

#define MATRIX_SIZE 6
#define PLANE_NUM 3

#define TRANSFORM_OPENCL_CHECK(method, ...)                                                                  \
    status = method(__VA_ARGS__);                                                                            \
    if (status != CL_SUCCESS) {                                                                              \
        av_log(ctx, AV_LOG_ERROR, "error %s %d\n", # method, status);                                        \
        return AVERROR_EXTERNAL;                                                                             \
    }

#define TRANSFORM_OPENCL_SET_KERNEL_ARG(arg_ptr)                                                             \
    status = clSetKernelArg((kernel),(arg_no++),(sizeof(arg_ptr)),(void*)(&(arg_ptr)));                      \
    if (status != CL_SUCCESS) {                                                                              \
        av_log(ctx, AV_LOG_ERROR, "cannot set kernel argument: %d\n", status );                              \
        return AVERROR_EXTERNAL;                                                                             \
    }

int ff_opencl_transform(AVFilterContext *ctx,
                        int width, int height, int cw, int ch,
                        const float *matrix_y, const float *matrix_uv,
                        enum InterpolateMethod interpolate,
                        enum FillMethod fill, AVFrame *in, AVFrame *out)
{
    int arg_no, ret = 0;
    const size_t global_work_size = width * height + 2 * ch * cw;
    cl_kernel kernel;
    cl_int status;
    DeshakeContext *deshake = ctx->priv;
    ret = av_opencl_buffer_write(deshake->opencl_ctx.cl_matrix_y, (uint8_t *)matrix_y, deshake->opencl_ctx.matrix_size * sizeof(cl_float));
    if (ret < 0)
        return ret;
    ret = av_opencl_buffer_write(deshake->opencl_ctx.cl_matrix_uv, (uint8_t *)matrix_uv, deshake->opencl_ctx.matrix_size * sizeof(cl_float));
    if (ret < 0)
        return ret;
    kernel = deshake->opencl_ctx.kernel_env.kernel;
    arg_no = 0;

    if ((unsigned int)interpolate > INTERPOLATE_BIQUADRATIC) {
        av_log(ctx, AV_LOG_ERROR, "Selected interpolate method is invalid\n");
        return AVERROR(EINVAL);
    }
    TRANSFORM_OPENCL_SET_KERNEL_ARG(deshake->opencl_ctx.cl_inbuf);
    TRANSFORM_OPENCL_SET_KERNEL_ARG(deshake->opencl_ctx.cl_outbuf);
    TRANSFORM_OPENCL_SET_KERNEL_ARG(deshake->opencl_ctx.cl_matrix_y);
    TRANSFORM_OPENCL_SET_KERNEL_ARG(deshake->opencl_ctx.cl_matrix_uv);
    TRANSFORM_OPENCL_SET_KERNEL_ARG(interpolate);
    TRANSFORM_OPENCL_SET_KERNEL_ARG(fill);
    TRANSFORM_OPENCL_SET_KERNEL_ARG(in->linesize[0]);
    TRANSFORM_OPENCL_SET_KERNEL_ARG(out->linesize[0]);
    TRANSFORM_OPENCL_SET_KERNEL_ARG(in->linesize[1]);
    TRANSFORM_OPENCL_SET_KERNEL_ARG(out->linesize[1]);
    TRANSFORM_OPENCL_SET_KERNEL_ARG(height);
    TRANSFORM_OPENCL_SET_KERNEL_ARG(width);
    TRANSFORM_OPENCL_SET_KERNEL_ARG(ch);
    TRANSFORM_OPENCL_SET_KERNEL_ARG(cw);
    TRANSFORM_OPENCL_CHECK(clEnqueueNDRangeKernel, deshake->opencl_ctx.kernel_env.command_queue, deshake->opencl_ctx.kernel_env.kernel, 1, NULL,
                           &global_work_size, NULL, 0, NULL, NULL);
    clFinish(deshake->opencl_ctx.kernel_env.command_queue);
    ret = av_opencl_buffer_read_image(out->data, deshake->opencl_ctx.out_plane_size,
                                      deshake->opencl_ctx.plane_num, deshake->opencl_ctx.cl_outbuf,
                                      deshake->opencl_ctx.cl_outbuf_size);
    if (ret < 0)
        return ret;
    return ret;
}

int ff_opencl_deshake_init(AVFilterContext *ctx)
{
    int ret = 0;
    DeshakeContext *deshake = ctx->priv;
    ret = av_opencl_init(NULL);
    if (ret < 0)
        return ret;
    deshake->opencl_ctx.matrix_size = MATRIX_SIZE;
    deshake->opencl_ctx.plane_num   = PLANE_NUM;
    ret = av_opencl_buffer_create(&deshake->opencl_ctx.cl_matrix_y,
        deshake->opencl_ctx.matrix_size*sizeof(cl_float), CL_MEM_READ_ONLY, NULL);
    if (ret < 0)
        return ret;
    ret = av_opencl_buffer_create(&deshake->opencl_ctx.cl_matrix_uv,
        deshake->opencl_ctx.matrix_size*sizeof(cl_float), CL_MEM_READ_ONLY, NULL);
    if (ret < 0)
        return ret;
    if (!deshake->opencl_ctx.kernel_env.kernel) {
        ret =  av_opencl_create_kernel(&deshake->opencl_ctx.kernel_env, "avfilter_transform");
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR, "OpenCL failed to create kernel for name 'avfilter_transform'\n");
            return ret;
        }
    }
    return ret;
}

void ff_opencl_deshake_uninit(AVFilterContext *ctx)
{
    DeshakeContext *deshake = ctx->priv;
    av_opencl_buffer_release(&deshake->opencl_ctx.cl_inbuf);
    av_opencl_buffer_release(&deshake->opencl_ctx.cl_outbuf);
    av_opencl_buffer_release(&deshake->opencl_ctx.cl_matrix_y);
    av_opencl_buffer_release(&deshake->opencl_ctx.cl_matrix_uv);
    av_opencl_release_kernel(&deshake->opencl_ctx.kernel_env);
    av_opencl_uninit();
}


int ff_opencl_deshake_process_inout_buf(AVFilterContext *ctx, AVFrame *in, AVFrame *out)
{
    int ret = 0;
    AVFilterLink *link = ctx->inputs[0];
    DeshakeContext *deshake = ctx->priv;
    int chroma_height = -((-link->h) >> av_pix_fmt_desc_get(link->format)->log2_chroma_h);

    if ((!deshake->opencl_ctx.cl_inbuf) || (!deshake->opencl_ctx.cl_outbuf)) {
        deshake->opencl_ctx.in_plane_size[0]  = (in->linesize[0] * in->height);
        deshake->opencl_ctx.in_plane_size[1]  = (in->linesize[1] * chroma_height);
        deshake->opencl_ctx.in_plane_size[2]  = (in->linesize[2] * chroma_height);
        deshake->opencl_ctx.out_plane_size[0] = (out->linesize[0] * out->height);
        deshake->opencl_ctx.out_plane_size[1] = (out->linesize[1] * chroma_height);
        deshake->opencl_ctx.out_plane_size[2] = (out->linesize[2] * chroma_height);
        deshake->opencl_ctx.cl_inbuf_size  = deshake->opencl_ctx.in_plane_size[0] +
                                             deshake->opencl_ctx.in_plane_size[1] +
                                             deshake->opencl_ctx.in_plane_size[2];
        deshake->opencl_ctx.cl_outbuf_size = deshake->opencl_ctx.out_plane_size[0] +
                                             deshake->opencl_ctx.out_plane_size[1] +
                                             deshake->opencl_ctx.out_plane_size[2];
        if (!deshake->opencl_ctx.cl_inbuf) {
            ret = av_opencl_buffer_create(&deshake->opencl_ctx.cl_inbuf,
                                            deshake->opencl_ctx.cl_inbuf_size,
                                            CL_MEM_READ_ONLY, NULL);
            if (ret < 0)
                return ret;
        }
        if (!deshake->opencl_ctx.cl_outbuf) {
            ret = av_opencl_buffer_create(&deshake->opencl_ctx.cl_outbuf,
                                            deshake->opencl_ctx.cl_outbuf_size,
                                            CL_MEM_READ_WRITE, NULL);
            if (ret < 0)
                return ret;
        }
    }
    ret = av_opencl_buffer_write_image(deshake->opencl_ctx.cl_inbuf,
                                 deshake->opencl_ctx.cl_inbuf_size,
                                 0, in->data,deshake->opencl_ctx.in_plane_size,
                                 deshake->opencl_ctx.plane_num);
    if(ret < 0)
        return ret;
    return ret;
}
