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
 * unsharp input video
 */

#include "unsharp_opencl.h"
#include "libavutil/common.h"
#include "libavutil/opencl_internal.h"

#define PLANE_NUM 3
#define ROUND_TO_16(a) (((((a) - 1)/16)+1)*16)

static inline void add_mask_counter(uint32_t *dst, uint32_t *counter1, uint32_t *counter2, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        dst[i] = counter1[i] + counter2[i];
    }
}

static int compute_mask(int step, uint32_t *mask)
{
    int i, z, ret = 0;
    int counter_size = sizeof(uint32_t) * (2 * step + 1);
    uint32_t *temp1_counter, *temp2_counter, **counter;
    temp1_counter = av_mallocz(counter_size);
    if (!temp1_counter) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    temp2_counter = av_mallocz(counter_size);
    if (!temp2_counter) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    counter = av_mallocz_array(2 * step + 1, sizeof(uint32_t *));
    if (!counter) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    for (i = 0; i < 2 * step + 1; i++) {
        counter[i] = av_mallocz(counter_size);
        if (!counter[i]) {
            ret = AVERROR(ENOMEM);
            goto end;
        }
    }
    for (i = 0; i < 2 * step + 1; i++) {
        memset(temp1_counter, 0, counter_size);
        temp1_counter[i] = 1;
        for (z = 0; z < step * 2; z += 2) {
            add_mask_counter(temp2_counter, counter[z], temp1_counter, step * 2);
            memcpy(counter[z], temp1_counter, counter_size);
            add_mask_counter(temp1_counter, counter[z + 1], temp2_counter, step * 2);
            memcpy(counter[z + 1], temp2_counter, counter_size);
        }
    }
    memcpy(mask, temp1_counter, counter_size);
end:
    av_freep(&temp1_counter);
    av_freep(&temp2_counter);
    for (i = 0; i < 2 * step + 1; i++) {
        av_freep(&counter[i]);
    }
    av_freep(&counter);
    return ret;
}

static int copy_separable_masks(cl_mem cl_mask_x, cl_mem cl_mask_y, int step_x, int step_y)
{
    int ret = 0;
    uint32_t *mask_x, *mask_y;
    size_t size_mask_x = sizeof(uint32_t) * (2 * step_x + 1);
    size_t size_mask_y = sizeof(uint32_t) * (2 * step_y + 1);
    mask_x = av_mallocz_array(2 * step_x + 1, sizeof(uint32_t));
    if (!mask_x) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    mask_y = av_mallocz_array(2 * step_y + 1, sizeof(uint32_t));
    if (!mask_y) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    ret = compute_mask(step_x, mask_x);
    if (ret < 0)
        goto end;
    ret = compute_mask(step_y, mask_y);
    if (ret < 0)
        goto end;

    ret = av_opencl_buffer_write(cl_mask_x, (uint8_t *)mask_x, size_mask_x);
    ret = av_opencl_buffer_write(cl_mask_y, (uint8_t *)mask_y, size_mask_y);
end:
    av_freep(&mask_x);
    av_freep(&mask_y);

    return ret;
}

static int generate_mask(AVFilterContext *ctx)
{
    cl_mem masks[4];
    cl_mem mask_matrix[2];
    int i, ret = 0, step_x[2], step_y[2];

    UnsharpContext *unsharp = ctx->priv;
    mask_matrix[0] = unsharp->opencl_ctx.cl_luma_mask;
    mask_matrix[1] = unsharp->opencl_ctx.cl_chroma_mask;
    masks[0] = unsharp->opencl_ctx.cl_luma_mask_x;
    masks[1] = unsharp->opencl_ctx.cl_luma_mask_y;
    masks[2] = unsharp->opencl_ctx.cl_chroma_mask_x;
    masks[3] = unsharp->opencl_ctx.cl_chroma_mask_y;
    step_x[0] = unsharp->luma.steps_x;
    step_x[1] = unsharp->chroma.steps_x;
    step_y[0] = unsharp->luma.steps_y;
    step_y[1] = unsharp->chroma.steps_y;

    /* use default kernel if any matrix dim larger than 8 due to limited local mem size */
    if (step_x[0]>8 || step_x[1]>8 || step_y[0]>8 || step_y[1]>8)
        unsharp->opencl_ctx.use_fast_kernels = 0;
    else
        unsharp->opencl_ctx.use_fast_kernels = 1;

    if (!masks[0] || !masks[1] || !masks[2] || !masks[3]) {
        av_log(ctx, AV_LOG_ERROR, "Luma mask and chroma mask should not be NULL\n");
        return AVERROR(EINVAL);
    }
    if (!mask_matrix[0] || !mask_matrix[1]) {
        av_log(ctx, AV_LOG_ERROR, "Luma mask and chroma mask should not be NULL\n");
        return AVERROR(EINVAL);
    }
    for (i = 0; i < 2; i++) {
        ret = copy_separable_masks(masks[2*i], masks[2*i+1], step_x[i], step_y[i]);
        if (ret < 0)
            return ret;
    }
    return ret;
}

int ff_opencl_apply_unsharp(AVFilterContext *ctx, AVFrame *in, AVFrame *out)
{
    int ret;
    AVFilterLink *link = ctx->inputs[0];
    UnsharpContext *unsharp = ctx->priv;
    cl_int status;
    FFOpenclParam kernel1 = {0};
    FFOpenclParam kernel2 = {0};
    int width = link->w;
    int height = link->h;
    int cw = FF_CEIL_RSHIFT(link->w, unsharp->hsub);
    int ch = FF_CEIL_RSHIFT(link->h, unsharp->vsub);
    size_t globalWorkSize1d = width * height + 2 * ch * cw;
    size_t globalWorkSize2dLuma[2];
    size_t globalWorkSize2dChroma[2];
    size_t localWorkSize2d[2] = {16, 16};

    if (unsharp->opencl_ctx.use_fast_kernels) {
        globalWorkSize2dLuma[0] = (size_t)ROUND_TO_16(width);
        globalWorkSize2dLuma[1] = (size_t)ROUND_TO_16(height);
        globalWorkSize2dChroma[0] = (size_t)ROUND_TO_16(cw);
        globalWorkSize2dChroma[1] = (size_t)(2*ROUND_TO_16(ch));

        kernel1.ctx = ctx;
        kernel1.kernel = unsharp->opencl_ctx.kernel_luma;
        ret = avpriv_opencl_set_parameter(&kernel1,
                                      FF_OPENCL_PARAM_INFO(unsharp->opencl_ctx.cl_inbuf),
                                      FF_OPENCL_PARAM_INFO(unsharp->opencl_ctx.cl_outbuf),
                                      FF_OPENCL_PARAM_INFO(unsharp->opencl_ctx.cl_luma_mask_x),
                                      FF_OPENCL_PARAM_INFO(unsharp->opencl_ctx.cl_luma_mask_y),
                                      FF_OPENCL_PARAM_INFO(unsharp->luma.amount),
                                      FF_OPENCL_PARAM_INFO(unsharp->luma.scalebits),
                                      FF_OPENCL_PARAM_INFO(unsharp->luma.halfscale),
                                      FF_OPENCL_PARAM_INFO(in->linesize[0]),
                                      FF_OPENCL_PARAM_INFO(out->linesize[0]),
                                      FF_OPENCL_PARAM_INFO(width),
                                      FF_OPENCL_PARAM_INFO(height),
                                      NULL);
        if (ret < 0)
            return ret;

        kernel2.ctx = ctx;
        kernel2.kernel = unsharp->opencl_ctx.kernel_chroma;
        ret = avpriv_opencl_set_parameter(&kernel2,
                                      FF_OPENCL_PARAM_INFO(unsharp->opencl_ctx.cl_inbuf),
                                      FF_OPENCL_PARAM_INFO(unsharp->opencl_ctx.cl_outbuf),
                                      FF_OPENCL_PARAM_INFO(unsharp->opencl_ctx.cl_chroma_mask_x),
                                      FF_OPENCL_PARAM_INFO(unsharp->opencl_ctx.cl_chroma_mask_y),
                                      FF_OPENCL_PARAM_INFO(unsharp->chroma.amount),
                                      FF_OPENCL_PARAM_INFO(unsharp->chroma.scalebits),
                                      FF_OPENCL_PARAM_INFO(unsharp->chroma.halfscale),
                                      FF_OPENCL_PARAM_INFO(in->linesize[0]),
                                      FF_OPENCL_PARAM_INFO(in->linesize[1]),
                                      FF_OPENCL_PARAM_INFO(out->linesize[0]),
                                      FF_OPENCL_PARAM_INFO(out->linesize[1]),
                                      FF_OPENCL_PARAM_INFO(link->w),
                                      FF_OPENCL_PARAM_INFO(link->h),
                                      FF_OPENCL_PARAM_INFO(cw),
                                      FF_OPENCL_PARAM_INFO(ch),
                                      NULL);
        if (ret < 0)
            return ret;
        status = clEnqueueNDRangeKernel(unsharp->opencl_ctx.command_queue,
                                        unsharp->opencl_ctx.kernel_luma, 2, NULL,
                                        globalWorkSize2dLuma, localWorkSize2d, 0, NULL, NULL);
        status |=clEnqueueNDRangeKernel(unsharp->opencl_ctx.command_queue,
                                        unsharp->opencl_ctx.kernel_chroma, 2, NULL,
                                        globalWorkSize2dChroma, localWorkSize2d, 0, NULL, NULL);
        if (status != CL_SUCCESS) {
            av_log(ctx, AV_LOG_ERROR, "OpenCL run kernel error occurred: %s\n", av_opencl_errstr(status));
            return AVERROR_EXTERNAL;
        }
    } else {    /* use default kernel */
        kernel1.ctx = ctx;
        kernel1.kernel = unsharp->opencl_ctx.kernel_default;

        ret = avpriv_opencl_set_parameter(&kernel1,
                                      FF_OPENCL_PARAM_INFO(unsharp->opencl_ctx.cl_inbuf),
                                      FF_OPENCL_PARAM_INFO(unsharp->opencl_ctx.cl_outbuf),
                                      FF_OPENCL_PARAM_INFO(unsharp->opencl_ctx.cl_luma_mask),
                                      FF_OPENCL_PARAM_INFO(unsharp->opencl_ctx.cl_chroma_mask),
                                      FF_OPENCL_PARAM_INFO(unsharp->luma.amount),
                                      FF_OPENCL_PARAM_INFO(unsharp->chroma.amount),
                                      FF_OPENCL_PARAM_INFO(unsharp->luma.steps_x),
                                      FF_OPENCL_PARAM_INFO(unsharp->luma.steps_y),
                                      FF_OPENCL_PARAM_INFO(unsharp->chroma.steps_x),
                                      FF_OPENCL_PARAM_INFO(unsharp->chroma.steps_y),
                                      FF_OPENCL_PARAM_INFO(unsharp->luma.scalebits),
                                      FF_OPENCL_PARAM_INFO(unsharp->chroma.scalebits),
                                      FF_OPENCL_PARAM_INFO(unsharp->luma.halfscale),
                                      FF_OPENCL_PARAM_INFO(unsharp->chroma.halfscale),
                                      FF_OPENCL_PARAM_INFO(in->linesize[0]),
                                      FF_OPENCL_PARAM_INFO(in->linesize[1]),
                                      FF_OPENCL_PARAM_INFO(out->linesize[0]),
                                      FF_OPENCL_PARAM_INFO(out->linesize[1]),
                                      FF_OPENCL_PARAM_INFO(link->h),
                                      FF_OPENCL_PARAM_INFO(link->w),
                                      FF_OPENCL_PARAM_INFO(ch),
                                      FF_OPENCL_PARAM_INFO(cw),
                                      NULL);
        if (ret < 0)
            return ret;
        status = clEnqueueNDRangeKernel(unsharp->opencl_ctx.command_queue,
                                        unsharp->opencl_ctx.kernel_default, 1, NULL,
                                        &globalWorkSize1d, NULL, 0, NULL, NULL);
        if (status != CL_SUCCESS) {
            av_log(ctx, AV_LOG_ERROR, "OpenCL run kernel error occurred: %s\n", av_opencl_errstr(status));
            return AVERROR_EXTERNAL;
        }
    }
    //blocking map is suffficient, no need for clFinish
    //clFinish(unsharp->opencl_ctx.command_queue);

    return av_opencl_buffer_read_image(out->data, unsharp->opencl_ctx.out_plane_size,
                                       unsharp->opencl_ctx.plane_num, unsharp->opencl_ctx.cl_outbuf,
                                       unsharp->opencl_ctx.cl_outbuf_size);
}

int ff_opencl_unsharp_init(AVFilterContext *ctx)
{
    int ret = 0;
    char build_opts[96];
    UnsharpContext *unsharp = ctx->priv;
    ret = av_opencl_init(NULL);
    if (ret < 0)
        return ret;
    ret = av_opencl_buffer_create(&unsharp->opencl_ctx.cl_luma_mask,
                                  sizeof(uint32_t) * (2 * unsharp->luma.steps_x + 1) * (2 * unsharp->luma.steps_y + 1),
                                  CL_MEM_READ_ONLY, NULL);
    if (ret < 0)
        return ret;
    ret = av_opencl_buffer_create(&unsharp->opencl_ctx.cl_chroma_mask,
                                  sizeof(uint32_t) * (2 * unsharp->chroma.steps_x + 1) * (2 * unsharp->chroma.steps_y + 1),
                                  CL_MEM_READ_ONLY, NULL);
    // separable filters
    if (ret < 0)
        return ret;
    ret = av_opencl_buffer_create(&unsharp->opencl_ctx.cl_luma_mask_x,
                                  sizeof(uint32_t) * (2 * unsharp->luma.steps_x + 1),
                                  CL_MEM_READ_ONLY, NULL);
    if (ret < 0)
        return ret;
    ret = av_opencl_buffer_create(&unsharp->opencl_ctx.cl_luma_mask_y,
                                  sizeof(uint32_t) * (2 * unsharp->luma.steps_y + 1),
                                  CL_MEM_READ_ONLY, NULL);
    if (ret < 0)
        return ret;
    ret = av_opencl_buffer_create(&unsharp->opencl_ctx.cl_chroma_mask_x,
                                  sizeof(uint32_t) * (2 * unsharp->chroma.steps_x + 1),
                                  CL_MEM_READ_ONLY, NULL);
    if (ret < 0)
        return ret;
    ret = av_opencl_buffer_create(&unsharp->opencl_ctx.cl_chroma_mask_y,
                                  sizeof(uint32_t) * (2 * unsharp->chroma.steps_y + 1),
                                  CL_MEM_READ_ONLY, NULL);
    if (ret < 0)
        return ret;
    ret = generate_mask(ctx);
    if (ret < 0)
        return ret;
    unsharp->opencl_ctx.plane_num = PLANE_NUM;
    unsharp->opencl_ctx.command_queue = av_opencl_get_command_queue();
    if (!unsharp->opencl_ctx.command_queue) {
        av_log(ctx, AV_LOG_ERROR, "Unable to get OpenCL command queue in filter 'unsharp'\n");
        return AVERROR(EINVAL);
    }
    snprintf(build_opts, 96, "-D LU_RADIUS_X=%d -D LU_RADIUS_Y=%d -D CH_RADIUS_X=%d -D CH_RADIUS_Y=%d",
            2*unsharp->luma.steps_x+1, 2*unsharp->luma.steps_y+1, 2*unsharp->chroma.steps_x+1, 2*unsharp->chroma.steps_y+1);
    unsharp->opencl_ctx.program = av_opencl_compile("unsharp", build_opts);
    if (!unsharp->opencl_ctx.program) {
        av_log(ctx, AV_LOG_ERROR, "OpenCL failed to compile program 'unsharp'\n");
        return AVERROR(EINVAL);
    }
    if (unsharp->opencl_ctx.use_fast_kernels) {
        if (!unsharp->opencl_ctx.kernel_luma) {
            unsharp->opencl_ctx.kernel_luma = clCreateKernel(unsharp->opencl_ctx.program, "unsharp_luma", &ret);
            if (ret != CL_SUCCESS) {
                av_log(ctx, AV_LOG_ERROR, "OpenCL failed to create kernel 'unsharp_luma'\n");
                return ret;
            }
        }
        if (!unsharp->opencl_ctx.kernel_chroma) {
            unsharp->opencl_ctx.kernel_chroma = clCreateKernel(unsharp->opencl_ctx.program, "unsharp_chroma", &ret);
            if (ret < 0) {
                av_log(ctx, AV_LOG_ERROR, "OpenCL failed to create kernel 'unsharp_chroma'\n");
                return ret;
            }
        }
    }
    else {
        if (!unsharp->opencl_ctx.kernel_default) {
            unsharp->opencl_ctx.kernel_default = clCreateKernel(unsharp->opencl_ctx.program, "unsharp_default", &ret);
            if (ret < 0) {
                av_log(ctx, AV_LOG_ERROR, "OpenCL failed to create kernel 'unsharp_default'\n");
                return ret;
            }
        }
    }
    return ret;
}

void ff_opencl_unsharp_uninit(AVFilterContext *ctx)
{
    UnsharpContext *unsharp = ctx->priv;
    av_opencl_buffer_release(&unsharp->opencl_ctx.cl_inbuf);
    av_opencl_buffer_release(&unsharp->opencl_ctx.cl_outbuf);
    av_opencl_buffer_release(&unsharp->opencl_ctx.cl_luma_mask);
    av_opencl_buffer_release(&unsharp->opencl_ctx.cl_chroma_mask);
    av_opencl_buffer_release(&unsharp->opencl_ctx.cl_luma_mask_x);
    av_opencl_buffer_release(&unsharp->opencl_ctx.cl_chroma_mask_x);
    av_opencl_buffer_release(&unsharp->opencl_ctx.cl_luma_mask_y);
    av_opencl_buffer_release(&unsharp->opencl_ctx.cl_chroma_mask_y);
    clReleaseKernel(unsharp->opencl_ctx.kernel_default);
    clReleaseKernel(unsharp->opencl_ctx.kernel_luma);
    clReleaseKernel(unsharp->opencl_ctx.kernel_chroma);
    clReleaseProgram(unsharp->opencl_ctx.program);
    unsharp->opencl_ctx.command_queue = NULL;
    av_opencl_uninit();
}

int ff_opencl_unsharp_process_inout_buf(AVFilterContext *ctx, AVFrame *in, AVFrame *out)
{
    int ret = 0;
    AVFilterLink *link = ctx->inputs[0];
    UnsharpContext *unsharp = ctx->priv;
    int ch = FF_CEIL_RSHIFT(link->h, unsharp->vsub);

    if ((!unsharp->opencl_ctx.cl_inbuf) || (!unsharp->opencl_ctx.cl_outbuf)) {
        unsharp->opencl_ctx.in_plane_size[0]  = (in->linesize[0] * in->height);
        unsharp->opencl_ctx.in_plane_size[1]  = (in->linesize[1] * ch);
        unsharp->opencl_ctx.in_plane_size[2]  = (in->linesize[2] * ch);
        unsharp->opencl_ctx.out_plane_size[0] = (out->linesize[0] * out->height);
        unsharp->opencl_ctx.out_plane_size[1] = (out->linesize[1] * ch);
        unsharp->opencl_ctx.out_plane_size[2] = (out->linesize[2] * ch);
        unsharp->opencl_ctx.cl_inbuf_size  = unsharp->opencl_ctx.in_plane_size[0] +
                                             unsharp->opencl_ctx.in_plane_size[1] +
                                             unsharp->opencl_ctx.in_plane_size[2];
        unsharp->opencl_ctx.cl_outbuf_size = unsharp->opencl_ctx.out_plane_size[0] +
                                             unsharp->opencl_ctx.out_plane_size[1] +
                                             unsharp->opencl_ctx.out_plane_size[2];
        if (!unsharp->opencl_ctx.cl_inbuf) {
            ret = av_opencl_buffer_create(&unsharp->opencl_ctx.cl_inbuf,
                                          unsharp->opencl_ctx.cl_inbuf_size,
                                          CL_MEM_READ_ONLY, NULL);
            if (ret < 0)
                return ret;
        }
        if (!unsharp->opencl_ctx.cl_outbuf) {
            ret = av_opencl_buffer_create(&unsharp->opencl_ctx.cl_outbuf,
                                          unsharp->opencl_ctx.cl_outbuf_size,
                                          CL_MEM_READ_WRITE, NULL);
            if (ret < 0)
                return ret;
        }
    }
    return av_opencl_buffer_write_image(unsharp->opencl_ctx.cl_inbuf,
                                        unsharp->opencl_ctx.cl_inbuf_size,
                                        0, in->data, unsharp->opencl_ctx.in_plane_size,
                                        unsharp->opencl_ctx.plane_num);
}
