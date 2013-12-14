/*
 * Copyright (C) 2013 Wei Gao <weigao@multicorewareinc.com>
 * Copyright (C) 2013 Lenny Wang
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
#include "libavutil/opencl_internal.h"

#define PLANE_NUM 3
#define ROUND_TO_16(a) ((((a - 1)/16)+1)*16)

int ff_opencl_transform(AVFilterContext *ctx,
                        int width, int height, int cw, int ch,
                        const float *matrix_y, const float *matrix_uv,
                        enum InterpolateMethod interpolate,
                        enum FillMethod fill, AVFrame *in, AVFrame *out)
{
    int ret = 0;
    cl_int status;
    DeshakeContext *deshake = ctx->priv;
    float4 packed_matrix_lu = {matrix_y[0], matrix_y[1], matrix_y[2], matrix_y[5]};
    float4 packed_matrix_ch = {matrix_uv[0], matrix_uv[1], matrix_uv[2], matrix_uv[5]};
    size_t global_worksize_lu[2] = {(size_t)ROUND_TO_16(width), (size_t)ROUND_TO_16(height)};
    size_t global_worksize_ch[2] = {(size_t)ROUND_TO_16(cw), (size_t)(2*ROUND_TO_16(ch))};
    size_t local_worksize[2] = {16, 16};
    FFOpenclParam param_lu = {0};
    FFOpenclParam param_ch = {0};
    param_lu.ctx = param_ch.ctx = ctx;
    param_lu.kernel = deshake->opencl_ctx.kernel_luma;
    param_ch.kernel = deshake->opencl_ctx.kernel_chroma;

    if ((unsigned int)interpolate > INTERPOLATE_BIQUADRATIC) {
        av_log(ctx, AV_LOG_ERROR, "Selected interpolate method is invalid\n");
        return AVERROR(EINVAL);
    }
    ret = ff_opencl_set_parameter(&param_lu,
                                  FF_OPENCL_PARAM_INFO(deshake->opencl_ctx.cl_inbuf),
                                  FF_OPENCL_PARAM_INFO(deshake->opencl_ctx.cl_outbuf),
                                  FF_OPENCL_PARAM_INFO(packed_matrix_lu),
                                  FF_OPENCL_PARAM_INFO(interpolate),
                                  FF_OPENCL_PARAM_INFO(fill),
                                  FF_OPENCL_PARAM_INFO(in->linesize[0]),
                                  FF_OPENCL_PARAM_INFO(out->linesize[0]),
                                  FF_OPENCL_PARAM_INFO(height),
                                  FF_OPENCL_PARAM_INFO(width),
                                  NULL);
    if (ret < 0)
        return ret;
    ret = ff_opencl_set_parameter(&param_ch,
                                  FF_OPENCL_PARAM_INFO(deshake->opencl_ctx.cl_inbuf),
                                  FF_OPENCL_PARAM_INFO(deshake->opencl_ctx.cl_outbuf),
                                  FF_OPENCL_PARAM_INFO(packed_matrix_ch),
                                  FF_OPENCL_PARAM_INFO(interpolate),
                                  FF_OPENCL_PARAM_INFO(fill),
                                  FF_OPENCL_PARAM_INFO(in->linesize[0]),
                                  FF_OPENCL_PARAM_INFO(out->linesize[0]),
                                  FF_OPENCL_PARAM_INFO(in->linesize[1]),
                                  FF_OPENCL_PARAM_INFO(out->linesize[1]),
                                  FF_OPENCL_PARAM_INFO(height),
                                  FF_OPENCL_PARAM_INFO(width),
                                  FF_OPENCL_PARAM_INFO(ch),
                                  FF_OPENCL_PARAM_INFO(cw),
                                  NULL);
    if (ret < 0)
        return ret;
    status = clEnqueueNDRangeKernel(deshake->opencl_ctx.command_queue,
                                    deshake->opencl_ctx.kernel_luma, 2, NULL,
                                    global_worksize_lu, local_worksize, 0, NULL, NULL);
    status |= clEnqueueNDRangeKernel(deshake->opencl_ctx.command_queue,
                                    deshake->opencl_ctx.kernel_chroma, 2, NULL,
                                    global_worksize_ch, local_worksize, 0, NULL, NULL);
    if (status != CL_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "OpenCL run kernel error occurred: %s\n", av_opencl_errstr(status));
        return AVERROR_EXTERNAL;
    }
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
    deshake->opencl_ctx.plane_num = PLANE_NUM;
    deshake->opencl_ctx.command_queue = av_opencl_get_command_queue();
    if (!deshake->opencl_ctx.command_queue) {
        av_log(ctx, AV_LOG_ERROR, "Unable to get OpenCL command queue in filter 'deshake'\n");
        return AVERROR(EINVAL);
    }
    deshake->opencl_ctx.program = av_opencl_compile("avfilter_transform", NULL);
    if (!deshake->opencl_ctx.program) {
        av_log(ctx, AV_LOG_ERROR, "OpenCL failed to compile program 'avfilter_transform'\n");
        return AVERROR(EINVAL);
    }
    if (!deshake->opencl_ctx.kernel_luma) {
        deshake->opencl_ctx.kernel_luma = clCreateKernel(deshake->opencl_ctx.program,
                                                         "avfilter_transform_luma", &ret);
        if (ret != CL_SUCCESS) {
            av_log(ctx, AV_LOG_ERROR, "OpenCL failed to create kernel 'avfilter_transform_luma'\n");
            return AVERROR(EINVAL);
        }
    }
    if (!deshake->opencl_ctx.kernel_chroma) {
        deshake->opencl_ctx.kernel_chroma = clCreateKernel(deshake->opencl_ctx.program,
                                                           "avfilter_transform_chroma", &ret);
        if (ret != CL_SUCCESS) {
            av_log(ctx, AV_LOG_ERROR, "OpenCL failed to create kernel 'avfilter_transform_chroma'\n");
            return AVERROR(EINVAL);
        }
    }
    return ret;
}

void ff_opencl_deshake_uninit(AVFilterContext *ctx)
{
    DeshakeContext *deshake = ctx->priv;
    av_opencl_buffer_release(&deshake->opencl_ctx.cl_inbuf);
    av_opencl_buffer_release(&deshake->opencl_ctx.cl_outbuf);
    clReleaseKernel(deshake->opencl_ctx.kernel_luma);
    clReleaseKernel(deshake->opencl_ctx.kernel_chroma);
    clReleaseProgram(deshake->opencl_ctx.program);
    deshake->opencl_ctx.command_queue = NULL;
    av_opencl_uninit();
}

int ff_opencl_deshake_process_inout_buf(AVFilterContext *ctx, AVFrame *in, AVFrame *out)
{
    int ret = 0;
    AVFilterLink *link = ctx->inputs[0];
    DeshakeContext *deshake = ctx->priv;
    const int hshift = av_pix_fmt_desc_get(link->format)->log2_chroma_h;
    int chroma_height = FF_CEIL_RSHIFT(link->h, hshift);

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
