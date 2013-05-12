/*
 * Copyright (C) 2012 Peng Gao <peng@multicorewareinc.com>
 * Copyright (C) 2012 Li   Cao <li@multicorewareinc.com>
 * Copyright (C) 2012 Wei  Gao <weigao@multicorewareinc.com>
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
 * OpenCL wrapper
 *
 * This interface is considered still experimental and its API and ABI may
 * change without prior notice.
 */

#ifndef LIBAVUTIL_OPENCL_H
#define LIBAVUTIL_OPENCL_H

#include "config.h"
#if HAVE_CL_CL_H
#include <CL/cl.h>
#else
#include <OpenCL/cl.h>
#endif
#include "dict.h"

#define AV_OPENCL_KERNEL( ... )# __VA_ARGS__

#define AV_OPENCL_MAX_KERNEL_NAME_SIZE 150

#define AV_OPENCL_MAX_DEVICE_NAME_SIZE 100

#define AV_OPENCL_MAX_PLATFORM_NAME_SIZE 100

typedef struct {
    int device_type;
    char device_name[AV_OPENCL_MAX_DEVICE_NAME_SIZE];
    cl_device_id device_id;
} AVOpenCLDeviceNode;

typedef struct {
    cl_platform_id platform_id;
    char platform_name[AV_OPENCL_MAX_PLATFORM_NAME_SIZE];
    int device_num;
    AVOpenCLDeviceNode **device_node;
} AVOpenCLPlatformNode;

typedef struct {
    int platform_num;
    AVOpenCLPlatformNode **platform_node;
} AVOpenCLDeviceList;

typedef struct {
    cl_command_queue command_queue;
    cl_kernel kernel;
    char kernel_name[AV_OPENCL_MAX_KERNEL_NAME_SIZE];
} AVOpenCLKernelEnv;

typedef struct {
    cl_platform_id platform_id;
    cl_device_type device_type;
    cl_context context;
    cl_device_id  device_id;
    cl_command_queue command_queue;
    char *platform_name;
} AVOpenCLExternalEnv;

/**
 * Get OpenCL device list.
 *
 * It must be freed with av_opencl_free_device_list().
 *
 * @param device_list pointer to OpenCL environment device list,
 *                    should be released by av_opencl_free_device_list()
 *
 * @return  >=0 on success, a negative error code in case of failure
 */
int av_opencl_get_device_list(AVOpenCLDeviceList **device_list);

/**
  * Free OpenCL device list.
  *
  * @param device_list pointer to OpenCL environment device list
  *                       created by av_opencl_get_device_list()
  */
void av_opencl_free_device_list(AVOpenCLDeviceList **device_list);

/**
 * Set option in the global OpenCL context.
 *
 * This options affect the operation performed by the next
 * av_opencl_init() operation.
 *
 * The currently accepted options are:
 * - build_options: set options to compile registered kernels code
 * - platform: set index of platform in device list
 * - device: set index of device in device list
 *
 * See reference "OpenCL Specification Version: 1.2 chapter 5.6.4".
 *
 * @param key                 option key
 * @param val                 option value
 * @return >=0 on success, a negative error code in case of failure
 * @see av_opencl_get_option()
 */
int av_opencl_set_option(const char *key, const char *val);

/**
 * Get option value from the global OpenCL context.
 *
 * @param key        option key
 * @param out_val  pointer to location where option value will be
 *                         written, must be freed with av_freep()
 * @return  >=0 on success, a negative error code in case of failure
 * @see av_opencl_set_option()
 */
int av_opencl_get_option(const char *key, uint8_t **out_val);

/**
 * Free option values of the global OpenCL context.
 *
 */
void av_opencl_free_option(void);

/**
 * Allocate OpenCL external environment.
 *
 * It must be freed with av_opencl_free_external_env().
 *
 * @return pointer to allocated OpenCL external environment
 */
AVOpenCLExternalEnv *av_opencl_alloc_external_env(void);

/**
 * Free OpenCL external environment.
 *
 * @param ext_opencl_env pointer to OpenCL external environment
 *                       created by av_opencl_alloc_external_env()
 */
void av_opencl_free_external_env(AVOpenCLExternalEnv **ext_opencl_env);

/**
 * Get OpenCL error string.
 *
 * @param status    OpenCL error code
 * @return OpenCL error string
 */
const char *av_opencl_errstr(cl_int status);

/**
 * Register kernel code.
 *
 *  The registered kernel code is stored in a global context, and compiled
 *  in the runtime environment when av_opencl_init() is called.
 *
 * @param kernel_code    kernel code to be compiled in the OpenCL runtime environment
 * @return  >=0 on success, a negative error code in case of failure
 */
int av_opencl_register_kernel_code(const char *kernel_code);

/**
 * Initialize the run time OpenCL environment and compile the kernel
 * code registered with av_opencl_register_kernel_code().
 *
 * @param ext_opencl_env external OpenCL environment, created by an
 *                       application program, ignored if set to NULL
 * @return >=0 on success, a negative error code in case of failure
 */
 int av_opencl_init(AVOpenCLExternalEnv *ext_opencl_env);

/**
 * Create kernel object in the specified kernel environment.
 *
 * @param env              pointer to kernel environment which is filled with
 *                         the environment used to run the kernel
 * @param kernel_name      kernel function name
 * @return >=0 on success, a negative error code in case of failure
 */
int av_opencl_create_kernel(AVOpenCLKernelEnv *env, const char *kernel_name);

/**
 * Create OpenCL buffer.
 *
 * The buffer is used to save the data used or created by an OpenCL
 * kernel.
 * The created buffer must be released with av_opencl_buffer_release().
 *
 * See clCreateBuffer() function reference for more information about
 * the parameters.
 *
 * @param cl_buf       pointer to OpenCL buffer
 * @param cl_buf_size  size in bytes of the OpenCL buffer to create
 * @param flags        flags used to control buffer attributes
 * @param host_ptr     host pointer of the OpenCL buffer
 * @return >=0 on success, a negative error code in case of failure
 */
int av_opencl_buffer_create(cl_mem *cl_buf, size_t cl_buf_size, int flags, void *host_ptr);

/**
 * Write OpenCL buffer with data from src_buf.
 *
 * @param dst_cl_buf        pointer to OpenCL destination buffer
 * @param src_buf           pointer to source buffer
 * @param buf_size          size in bytes of the source and destination buffers
 * @return >=0 on success, a negative error code in case of failure
 */
int av_opencl_buffer_write(cl_mem dst_cl_buf, uint8_t *src_buf, size_t buf_size);

/**
 * Read data from OpenCL buffer to memory buffer.
 *
 * @param dst_buf           pointer to destination buffer (CPU memory)
 * @param src_cl_buf        pointer to source OpenCL buffer
 * @param buf_size          size in bytes of the source and destination buffers
 * @return >=0 on success, a negative error code in case of failure
 */
int av_opencl_buffer_read(uint8_t *dst_buf, cl_mem src_cl_buf, size_t buf_size);

/**
 * Write image data from memory to OpenCL buffer.
 *
 * The source must be an array of pointers to image plane buffers.
 *
 * @param dst_cl_buf         pointer to destination OpenCL buffer
 * @param dst_cl_buf_size    size in bytes of OpenCL buffer
 * @param dst_cl_buf_offset  the offset of the OpenCL buffer start position
 * @param src_data           array of pointers to source plane buffers
 * @param src_plane_sizes    array of sizes in bytes of the source plane buffers
 * @param src_plane_num      number of source image planes
 * @return >=0 on success, a negative error code in case of failure
 */
int av_opencl_buffer_write_image(cl_mem dst_cl_buf, size_t cl_buffer_size, int dst_cl_offset,
                                 uint8_t **src_data, int *plane_size, int plane_num);

/**
 * Read image data from OpenCL buffer.
 *
 * @param dst_data           array of pointers to destination plane buffers
 * @param dst_plane_sizes    array of pointers to destination plane buffers
 * @param dst_plane_num      number of destination image planes
 * @param src_cl_buf         pointer to source OpenCL buffer
 * @param src_cl_buf_size    size in bytes of OpenCL buffer
 * @return >=0 on success, a negative error code in case of failure
 */
int av_opencl_buffer_read_image(uint8_t **dst_data, int *plane_size, int plane_num,
                                cl_mem src_cl_buf, size_t cl_buffer_size);

/**
 * Release OpenCL buffer.
 *
 * @param cl_buf pointer to OpenCL buffer to release, which was
 *               previously filled with av_opencl_buffer_create()
 */
void av_opencl_buffer_release(cl_mem *cl_buf);

/**
 * Release kernel object.
 *
 * @param env kernel environment where the kernel object was created
 *            with av_opencl_create_kernel()
 */
void av_opencl_release_kernel(AVOpenCLKernelEnv *env);

/**
 * Release OpenCL environment.
 *
 * The OpenCL environment is effectively released only if all the created
 * kernels had been released with av_opencl_release_kernel().
 */
void av_opencl_uninit(void);

#endif /* LIBAVUTIL_OPENCL_H */
