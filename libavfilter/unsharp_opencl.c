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
 * unsharp input video
 */

#include "unsharp_opencl.h"
#include "libavutil/opencl_internal.h"

#define PLANE_NUM 3

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
    counter = av_mallocz(sizeof(uint32_t *) * (2 * step + 1));
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

static int compute_mask_matrix(cl_mem cl_mask_matrix, int step_x, int step_y)
{
    int i, j, ret = 0;
    uint32_t *mask_matrix, *mask_x, *mask_y;
    size_t size_matrix = sizeof(uint32_t) * (2 * step_x + 1) * (2 * step_y + 1);
    mask_x = av_mallocz(sizeof(uint32_t) * (2 * step_x + 1));
    if (!mask_x) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    mask_y = av_mallocz(sizeof(uint32_t) * (2 * step_y + 1));
    if (!mask_y) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    mask_matrix = av_mallocz(size_matrix);
    if (!mask_matrix) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    ret = compute_mask(step_x, mask_x);
    if (ret < 0)
        goto end;
    ret = compute_mask(step_y, mask_y);
    if (ret < 0)
        goto end;
    for (j = 0; j < 2 * step_y + 1; j++) {
        for (i = 0; i < 2 * step_x + 1; i++) {
            mask_matrix[i + j * (2 * step_x + 1)] = mask_y[j] * mask_x[i];
        }
    }
    ret = av_opencl_buffer_write(cl_mask_matrix, (uint8_t *)mask_matrix, size_matrix);
end:
    av_freep(&mask_x);
    av_freep(&mask_y);
    av_freep(&mask_matrix);
    return ret;
}

static int generate_mask(AVFilterContext *ctx)
{
    UnsharpContext *unsharp = ctx->priv;
    int i, ret = 0, step_x[2], step_y[2];
    cl_mem mask_matrix[2];
    mask_matrix[0] = unsharp->opencl_ctx.cl_luma_mask;
    mask_matrix[1] = unsharp->opencl_ctx.cl_chroma_mask;
    step_x[0] = unsharp->luma.steps_x;
    step_x[1] = unsharp->chroma.steps_x;
    step_y[0] = unsharp->luma.steps_y;
    step_y[1] = unsharp->chroma.steps_y;
    if (!mask_matrix[0] || !mask_matrix[1]) {
        av_log(ctx, AV_LOG_ERROR, "Luma mask and chroma mask should not be NULL\n");
        return AVERROR(EINVAL);
    }
    for (i = 0; i < 2; i++) {
        ret = compute_mask_matrix(mask_matrix[i], step_x[i], step_y[i]);
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
    int cw = SHIFTUP(link->w, unsharp->hsub);
    int ch = SHIFTUP(link->h, unsharp->vsub);
    const size_t global_work_size = link->w * link->h + 2 * ch * cw;
    FFOpenclParam opencl_param = {0};

    opencl_param.ctx = ctx;
    opencl_param.kernel = unsharp->opencl_ctx.kernel_env.kernel;
    ret = ff_opencl_set_parameter(&opencl_param,
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
    status = clEnqueueNDRangeKernel(unsharp->opencl_ctx.kernel_env.command_queue,
                                    unsharp->opencl_ctx.kernel_env.kernel, 1, NULL,
                                    &global_work_size, NULL, 0, NULL, NULL);
    if (status != CL_SUCCESS) {
        av_log(ctx, AV_LOG_ERROR, "OpenCL run kernel error occurred: %s\n", av_opencl_errstr(status));
        return AVERROR_EXTERNAL;
    }
    clFinish(unsharp->opencl_ctx.kernel_env.command_queue);
    return av_opencl_buffer_read_image(out->data, unsharp->opencl_ctx.out_plane_size,
                                       unsharp->opencl_ctx.plane_num, unsharp->opencl_ctx.cl_outbuf,
                                       unsharp->opencl_ctx.cl_outbuf_size);
}

int ff_opencl_unsharp_init(AVFilterContext *ctx)
{
    int ret = 0;
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
    if (ret < 0)
        return ret;
    ret = generate_mask(ctx);
    if (ret < 0)
        return ret;
    unsharp->opencl_ctx.plane_num = PLANE_NUM;
    if (!unsharp->opencl_ctx.kernel_env.kernel) {
        ret = av_opencl_create_kernel(&unsharp->opencl_ctx.kernel_env, "unsharp");
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR, "OpenCL failed to create kernel with name 'unsharp'\n");
            return ret;
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
    av_opencl_release_kernel(&unsharp->opencl_ctx.kernel_env);
    av_opencl_uninit();
}

int ff_opencl_unsharp_process_inout_buf(AVFilterContext *ctx, AVFrame *in, AVFrame *out)
{
    int ret = 0;
    AVFilterLink *link = ctx->inputs[0];
    UnsharpContext *unsharp = ctx->priv;
    int ch = SHIFTUP(link->h, unsharp->vsub);

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
