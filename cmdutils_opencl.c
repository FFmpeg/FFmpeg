/*
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

#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavutil/log.h"
#include "libavutil/opencl.h"
#include "cmdutils.h"

typedef struct {
    int platform_idx;
    int device_idx;
    char device_name[64];
    int64_t runtime;
} OpenCLDeviceBenchmark;

const char *ocl_bench_source = AV_OPENCL_KERNEL(
inline unsigned char clip_uint8(int a)
{
    if (a & (~0xFF))
        return (-a)>>31;
    else
        return a;
}

kernel void unsharp_bench(
                    global unsigned char *src,
                    global unsigned char *dst,
                    global int *mask,
                    int width,
                    int height)
{
    int i, j, local_idx, lc_idx, sum = 0;
    int2 thread_idx, block_idx, global_idx, lm_idx;
    thread_idx.x = get_local_id(0);
    thread_idx.y = get_local_id(1);
    block_idx.x = get_group_id(0);
    block_idx.y = get_group_id(1);
    global_idx.x = get_global_id(0);
    global_idx.y = get_global_id(1);
    local uchar data[32][32];
    local int lc[128];

    for (i = 0; i <= 1; i++) {
        lm_idx.y = -8 + (block_idx.y + i) * 16 + thread_idx.y;
        lm_idx.y = lm_idx.y < 0 ? 0 : lm_idx.y;
        lm_idx.y = lm_idx.y >= height ? height - 1: lm_idx.y;
        for (j = 0; j <= 1; j++) {
            lm_idx.x = -8 + (block_idx.x + j) * 16 + thread_idx.x;
            lm_idx.x = lm_idx.x < 0 ? 0 : lm_idx.x;
            lm_idx.x = lm_idx.x >= width ? width - 1: lm_idx.x;
            data[i*16 + thread_idx.y][j*16 + thread_idx.x] = src[lm_idx.y*width + lm_idx.x];
        }
    }
    local_idx = thread_idx.y*16 + thread_idx.x;
    if (local_idx < 128)
        lc[local_idx] = mask[local_idx];
    barrier(CLK_LOCAL_MEM_FENCE);

    \n#pragma unroll\n
    for (i = -4; i <= 4; i++) {
        lm_idx.y = 8 + i + thread_idx.y;
        \n#pragma unroll\n
        for (j = -4; j <= 4; j++) {
            lm_idx.x = 8 + j + thread_idx.x;
            lc_idx = (i + 4)*8 + j + 4;
            sum += (int)data[lm_idx.y][lm_idx.x] * lc[lc_idx];
        }
    }
    int temp = (int)data[thread_idx.y + 8][thread_idx.x + 8];
    int res = temp + (((temp - (int)((sum + 1<<15) >> 16))) >> 16);
    if (global_idx.x < width && global_idx.y < height)
        dst[global_idx.x + global_idx.y*width] = clip_uint8(res);
}
);

#define OCLCHECK(method, ... )                                                 \
do {                                                                           \
    status = method(__VA_ARGS__);                                              \
    if (status != CL_SUCCESS) {                                                \
        av_log(NULL, AV_LOG_ERROR, # method " error '%s'\n",                   \
               av_opencl_errstr(status));                                      \
        ret = AVERROR_EXTERNAL;                                                \
        goto end;                                                              \
    }                                                                          \
} while (0)

#define CREATEBUF(out, flags, size)                                            \
do {                                                                           \
    out = clCreateBuffer(ext_opencl_env->context, flags, size, NULL, &status); \
    if (status != CL_SUCCESS) {                                                \
        av_log(NULL, AV_LOG_ERROR, "Could not create OpenCL buffer\n");        \
        ret = AVERROR_EXTERNAL;                                                \
        goto end;                                                              \
    }                                                                          \
} while (0)

static void fill_rand_int(int *data, int n)
{
    int i;
    srand(av_gettime());
    for (i = 0; i < n; i++)
        data[i] = rand();
}

#define OPENCL_NB_ITER 5
static int64_t run_opencl_bench(AVOpenCLExternalEnv *ext_opencl_env)
{
    int i, arg = 0, width = 1920, height = 1088;
    int64_t start, ret = 0;
    cl_int status;
    size_t kernel_len;
    char *inbuf;
    int *mask;
    int buf_size = width * height * sizeof(char);
    int mask_size = sizeof(uint32_t) * 128;

    cl_mem cl_mask, cl_inbuf, cl_outbuf;
    cl_kernel kernel = NULL;
    cl_program program = NULL;
    size_t local_work_size_2d[2] = {16, 16};
    size_t global_work_size_2d[2] = {(size_t)width, (size_t)height};

    if (!(inbuf = av_malloc(buf_size)) || !(mask = av_malloc(mask_size))) {
        av_log(NULL, AV_LOG_ERROR, "Out of memory\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }
    fill_rand_int((int*)inbuf, buf_size/4);
    fill_rand_int(mask, mask_size/4);

    CREATEBUF(cl_mask, CL_MEM_READ_ONLY, mask_size);
    CREATEBUF(cl_inbuf, CL_MEM_READ_ONLY, buf_size);
    CREATEBUF(cl_outbuf, CL_MEM_READ_WRITE, buf_size);

    kernel_len = strlen(ocl_bench_source);
    program = clCreateProgramWithSource(ext_opencl_env->context, 1, &ocl_bench_source,
                                        &kernel_len, &status);
    if (status != CL_SUCCESS || !program) {
        av_log(NULL, AV_LOG_ERROR, "OpenCL unable to create benchmark program\n");
        ret = AVERROR_EXTERNAL;
        goto end;
    }
    status = clBuildProgram(program, 1, &(ext_opencl_env->device_id), NULL, NULL, NULL);
    if (status != CL_SUCCESS) {
        av_log(NULL, AV_LOG_ERROR, "OpenCL unable to build benchmark program\n");
        ret = AVERROR_EXTERNAL;
        goto end;
    }
    kernel = clCreateKernel(program, "unsharp_bench", &status);
    if (status != CL_SUCCESS) {
        av_log(NULL, AV_LOG_ERROR, "OpenCL unable to create benchmark kernel\n");
        ret = AVERROR_EXTERNAL;
        goto end;
    }

    OCLCHECK(clEnqueueWriteBuffer, ext_opencl_env->command_queue, cl_inbuf, CL_TRUE, 0,
             buf_size, inbuf, 0, NULL, NULL);
    OCLCHECK(clEnqueueWriteBuffer, ext_opencl_env->command_queue, cl_mask, CL_TRUE, 0,
             mask_size, mask, 0, NULL, NULL);
    OCLCHECK(clSetKernelArg, kernel, arg++, sizeof(cl_mem), &cl_inbuf);
    OCLCHECK(clSetKernelArg, kernel, arg++, sizeof(cl_mem), &cl_outbuf);
    OCLCHECK(clSetKernelArg, kernel, arg++, sizeof(cl_mem), &cl_mask);
    OCLCHECK(clSetKernelArg, kernel, arg++, sizeof(cl_int), &width);
    OCLCHECK(clSetKernelArg, kernel, arg++, sizeof(cl_int), &height);

    start = av_gettime_relative();
    for (i = 0; i < OPENCL_NB_ITER; i++)
        OCLCHECK(clEnqueueNDRangeKernel, ext_opencl_env->command_queue, kernel, 2, NULL,
                 global_work_size_2d, local_work_size_2d, 0, NULL, NULL);
    clFinish(ext_opencl_env->command_queue);
    ret = (av_gettime_relative() - start)/OPENCL_NB_ITER;
end:
    if (kernel)
        clReleaseKernel(kernel);
    if (program)
        clReleaseProgram(program);
    if (cl_inbuf)
        clReleaseMemObject(cl_inbuf);
    if (cl_outbuf)
        clReleaseMemObject(cl_outbuf);
    if (cl_mask)
        clReleaseMemObject(cl_mask);
    av_free(inbuf);
    av_free(mask);
    return ret;
}

static int compare_ocl_device_desc(const void *a, const void *b)
{
    return ((OpenCLDeviceBenchmark*)a)->runtime - ((OpenCLDeviceBenchmark*)b)->runtime;
}

int opt_opencl_bench(void *optctx, const char *opt, const char *arg)
{
    int i, j, nb_devices = 0, count = 0;
    int64_t score = 0;
    AVOpenCLDeviceList *device_list;
    AVOpenCLDeviceNode *device_node = NULL;
    OpenCLDeviceBenchmark *devices = NULL;
    cl_platform_id platform;

    av_opencl_get_device_list(&device_list);
    for (i = 0; i < device_list->platform_num; i++)
        nb_devices += device_list->platform_node[i]->device_num;
    if (!nb_devices) {
        av_log(NULL, AV_LOG_ERROR, "No OpenCL device detected!\n");
        return AVERROR(EINVAL);
    }
    if (!(devices = av_malloc_array(nb_devices, sizeof(OpenCLDeviceBenchmark)))) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate buffer\n");
        return AVERROR(ENOMEM);
    }

    for (i = 0; i < device_list->platform_num; i++) {
        for (j = 0; j < device_list->platform_node[i]->device_num; j++) {
            device_node = device_list->platform_node[i]->device_node[j];
            platform = device_list->platform_node[i]->platform_id;
            score = av_opencl_benchmark(device_node, platform, run_opencl_bench);
            if (score > 0) {
                devices[count].platform_idx = i;
                devices[count].device_idx = j;
                devices[count].runtime = score;
                strcpy(devices[count].device_name, device_node->device_name);
                count++;
            }
        }
    }
    qsort(devices, count, sizeof(OpenCLDeviceBenchmark), compare_ocl_device_desc);
    fprintf(stderr, "platform_idx\tdevice_idx\tdevice_name\truntime\n");
    for (i = 0; i < count; i++)
        fprintf(stdout, "%d\t%d\t%s\t%"PRId64"\n",
                devices[i].platform_idx, devices[i].device_idx,
                devices[i].device_name, devices[i].runtime);

    av_opencl_free_device_list(&device_list);
    av_free(devices);
    return 0;
}

int opt_opencl(void *optctx, const char *opt, const char *arg)
{
    char *key, *value;
    const char *opts = arg;
    int ret = 0;
    while (*opts) {
        ret = av_opt_get_key_value(&opts, "=", ":", 0, &key, &value);
        if (ret < 0)
            return ret;
        ret = av_opencl_set_option(key, value);
        if (ret < 0)
            return ret;
        if (*opts)
            opts++;
    }
    return ret;
}
