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

#include "opencl.h"
#include "avstring.h"
#include "log.h"
#include "avassert.h"

#if HAVE_PTHREADS

#include <pthread.h>
static pthread_mutex_t atomic_opencl_lock = PTHREAD_MUTEX_INITIALIZER;

#define LOCK_OPENCL pthread_mutex_lock(&atomic_opencl_lock);
#define UNLOCK_OPENCL pthread_mutex_unlock(&atomic_opencl_lock);

#elif !HAVE_THREADS
#define LOCK_OPENCL
#define UNLOCK_OPENCL
#endif


#define MAX_KERNEL_NUM 500
#define MAX_KERNEL_CODE_NUM 200

typedef struct {
    int dev_idx;
    int platform_idx;
} UserSpecDevInfo;

typedef struct {
    int is_compiled;
    const char *kernel_string;
} KernelCode;

typedef struct {
    int init_count;
    UserSpecDevInfo usr_spec_dev_info;
    cl_platform_id platform_id;
    cl_device_type device_type;
    cl_context context;
    cl_device_id *device_ids;
    cl_device_id device_id;
    cl_command_queue command_queue;
    int program_count;
    cl_program programs[MAX_KERNEL_CODE_NUM];
    int kernel_code_count;
    KernelCode kernel_code[MAX_KERNEL_CODE_NUM];
    int kernel_count;
    /**
     * if set to 1, the OpenCL environment was created by the user and
     * passed as AVOpenCLExternalEnv when initing ,0:created by opencl wrapper.
     */
    int is_user_created;
} GPUEnv;

typedef struct {
    const AVClass *class;
    int log_offset;
    void *log_ctx;
} OpenclUtils;

static const AVClass openclutils_class = {
    .class_name                = "OPENCLUTILS",
    .item_name                 = av_default_item_name,
    .version                   = LIBAVUTIL_VERSION_INT,
    .log_level_offset_offset   = offsetof(OpenclUtils, log_offset),
    .parent_log_context_offset = offsetof(OpenclUtils, log_ctx),
};

static OpenclUtils openclutils = {&openclutils_class};

static GPUEnv gpu_env;

static const cl_device_type device_type[] = {CL_DEVICE_TYPE_GPU, CL_DEVICE_TYPE_CPU, CL_DEVICE_TYPE_DEFAULT};


typedef struct {
    int err_code;
    const char *err_str;
} OpenclErrorMsg;

static const OpenclErrorMsg opencl_err_msg[] = {
    {CL_DEVICE_NOT_FOUND,                               "DEVICE NOT FOUND"},
    {CL_DEVICE_NOT_AVAILABLE,                           "DEVICE NOT AVAILABLE"},
    {CL_COMPILER_NOT_AVAILABLE,                         "COMPILER NOT AVAILABLE"},
    {CL_MEM_OBJECT_ALLOCATION_FAILURE,                  "MEM OBJECT ALLOCATION FAILURE"},
    {CL_OUT_OF_RESOURCES,                               "OUT OF RESOURCES"},
    {CL_OUT_OF_HOST_MEMORY,                             "OUT OF HOST MEMORY"},
    {CL_PROFILING_INFO_NOT_AVAILABLE,                   "PROFILING INFO NOT AVAILABLE"},
    {CL_MEM_COPY_OVERLAP,                               "MEM COPY OVERLAP"},
    {CL_IMAGE_FORMAT_MISMATCH,                          "IMAGE FORMAT MISMATCH"},
    {CL_IMAGE_FORMAT_NOT_SUPPORTED,                     "IMAGE FORMAT NOT_SUPPORTED"},
    {CL_BUILD_PROGRAM_FAILURE,                          "BUILD PROGRAM FAILURE"},
    {CL_MAP_FAILURE,                                    "MAP FAILURE"},
    {CL_MISALIGNED_SUB_BUFFER_OFFSET,                   "MISALIGNED SUB BUFFER OFFSET"},
    {CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST,      "EXEC STATUS ERROR FOR EVENTS IN WAIT LIST"},
    {CL_COMPILE_PROGRAM_FAILURE,                        "COMPILE PROGRAM FAILURE"},
    {CL_LINKER_NOT_AVAILABLE,                           "LINKER NOT AVAILABLE"},
    {CL_LINK_PROGRAM_FAILURE,                           "LINK PROGRAM FAILURE"},
    {CL_DEVICE_PARTITION_FAILED,                        "DEVICE PARTITION FAILED"},
    {CL_KERNEL_ARG_INFO_NOT_AVAILABLE,                  "KERNEL ARG INFO NOT AVAILABLE"},
    {CL_INVALID_VALUE,                                  "INVALID VALUE"},
    {CL_INVALID_DEVICE_TYPE,                            "INVALID DEVICE TYPE"},
    {CL_INVALID_PLATFORM,                               "INVALID PLATFORM"},
    {CL_INVALID_DEVICE,                                 "INVALID DEVICE"},
    {CL_INVALID_CONTEXT,                                "INVALID CONTEXT"},
    {CL_INVALID_QUEUE_PROPERTIES,                       "INVALID QUEUE PROPERTIES"},
    {CL_INVALID_COMMAND_QUEUE,                          "INVALID COMMAND QUEUE"},
    {CL_INVALID_HOST_PTR,                               "INVALID HOST PTR"},
    {CL_INVALID_MEM_OBJECT,                             "INVALID MEM OBJECT"},
    {CL_INVALID_IMAGE_FORMAT_DESCRIPTOR,                "INVALID IMAGE FORMAT DESCRIPTOR"},
    {CL_INVALID_IMAGE_SIZE,                             "INVALID IMAGE SIZE"},
    {CL_INVALID_SAMPLER,                                "INVALID SAMPLER"},
    {CL_INVALID_BINARY,                                 "INVALID BINARY"},
    {CL_INVALID_BUILD_OPTIONS,                          "INVALID BUILD OPTIONS"},
    {CL_INVALID_PROGRAM,                                "INVALID PROGRAM"},
    {CL_INVALID_PROGRAM_EXECUTABLE,                     "INVALID PROGRAM EXECUTABLE"},
    {CL_INVALID_KERNEL_NAME,                            "INVALID KERNEL NAME"},
    {CL_INVALID_KERNEL_DEFINITION,                      "INVALID KERNEL DEFINITION"},
    {CL_INVALID_KERNEL,                                 "INVALID KERNEL"},
    {CL_INVALID_ARG_INDEX,                              "INVALID ARG INDEX"},
    {CL_INVALID_ARG_VALUE,                              "INVALID ARG VALUE"},
    {CL_INVALID_ARG_SIZE,                               "INVALID ARG_SIZE"},
    {CL_INVALID_KERNEL_ARGS,                            "INVALID KERNEL ARGS"},
    {CL_INVALID_WORK_DIMENSION,                         "INVALID WORK DIMENSION"},
    {CL_INVALID_WORK_GROUP_SIZE,                        "INVALID WORK GROUP SIZE"},
    {CL_INVALID_WORK_ITEM_SIZE,                         "INVALID WORK ITEM SIZE"},
    {CL_INVALID_GLOBAL_OFFSET,                          "INVALID GLOBAL OFFSET"},
    {CL_INVALID_EVENT_WAIT_LIST,                        "INVALID EVENT WAIT LIST"},
    {CL_INVALID_EVENT,                                  "INVALID EVENT"},
    {CL_INVALID_OPERATION,                              "INVALID OPERATION"},
    {CL_INVALID_GL_OBJECT,                              "INVALID GL OBJECT"},
    {CL_INVALID_BUFFER_SIZE,                            "INVALID BUFFER SIZE"},
    {CL_INVALID_MIP_LEVEL,                              "INVALID MIP LEVEL"},
    {CL_INVALID_GLOBAL_WORK_SIZE,                       "INVALID GLOBAL WORK SIZE"},
    {CL_INVALID_PROPERTY,                               "INVALID PROPERTY"},
    {CL_INVALID_IMAGE_DESCRIPTOR,                       "INVALID IMAGE DESCRIPTOR"},
    {CL_INVALID_COMPILER_OPTIONS,                       "INVALID COMPILER OPTIONS"},
    {CL_INVALID_LINKER_OPTIONS,                         "INVALID LINKER OPTIONS"},
    {CL_INVALID_DEVICE_PARTITION_COUNT,                 "INVALID DEVICE PARTITION COUNT"},
};

static const char *opencl_errstr(cl_int status)
{
    int i;
    for (i = 0; i < sizeof(opencl_err_msg); i++) {
        if (opencl_err_msg[i].err_code == status)
            return opencl_err_msg[i].err_str;
    }
    return "unknown error";
}

AVOpenCLExternalEnv *av_opencl_alloc_external_env(void)
{
    AVOpenCLExternalEnv *ext = av_mallocz(sizeof(AVOpenCLExternalEnv));
    if (!ext) {
        av_log(&openclutils, AV_LOG_ERROR,
               "Could not malloc external opencl environment data space\n");
    }
    return ext;
}

void av_opencl_free_external_env(AVOpenCLExternalEnv **ext_opencl_env)
{
    av_freep(ext_opencl_env);
}

int av_opencl_register_kernel_code(const char *kernel_code)
{
    int i, ret = 0;
    LOCK_OPENCL;
    if (gpu_env.kernel_code_count >= MAX_KERNEL_CODE_NUM) {
        av_log(&openclutils, AV_LOG_ERROR,
               "Could not register kernel code, maximum number of registered kernel code %d already reached\n",
               MAX_KERNEL_CODE_NUM);
        ret = AVERROR(EINVAL);
        goto end;
    }
    for (i = 0; i < gpu_env.kernel_code_count; i++) {
        if (gpu_env.kernel_code[i].kernel_string == kernel_code) {
            av_log(&openclutils, AV_LOG_WARNING, "Same kernel code has been registered\n");
            goto end;
        }
    }
    gpu_env.kernel_code[gpu_env.kernel_code_count].kernel_string = kernel_code;
    gpu_env.kernel_code[gpu_env.kernel_code_count].is_compiled = 0;
    gpu_env.kernel_code_count++;
end:
    UNLOCK_OPENCL;
    return ret;
}

int av_opencl_create_kernel(AVOpenCLKernelEnv *env, const char *kernel_name)
{
    cl_int status;
    int i, ret = 0;
    LOCK_OPENCL;
    if (strlen(kernel_name) + 1 > AV_OPENCL_MAX_KERNEL_NAME_SIZE) {
        av_log(&openclutils, AV_LOG_ERROR, "Created kernel name %s is too long\n", kernel_name);
        ret = AVERROR(EINVAL);
        goto end;
    }
    if (!env->kernel) {
        if (gpu_env.kernel_count >= MAX_KERNEL_NUM) {
            av_log(&openclutils, AV_LOG_ERROR,
                   "Could not create kernel with name '%s', maximum number of kernels %d already reached\n",
                   kernel_name, MAX_KERNEL_NUM);
            ret = AVERROR(EINVAL);
            goto end;
        }
        if (gpu_env.program_count == 0) {
            av_log(&openclutils, AV_LOG_ERROR, "Program count of OpenCL is 0, can not create kernel\n");
            ret = AVERROR(EINVAL);
            goto end;
        }
        for (i = 0; i < gpu_env.program_count; i++) {
            env->kernel = clCreateKernel(gpu_env.programs[i], kernel_name, &status);
            if (status == CL_SUCCESS)
                break;
        }
        if (status != CL_SUCCESS) {
            av_log(&openclutils, AV_LOG_ERROR, "Could not create OpenCL kernel: %s\n", opencl_errstr(status));
            ret = AVERROR_EXTERNAL;
            goto end;
        }
        gpu_env.kernel_count++;
        env->command_queue = gpu_env.command_queue;
        av_strlcpy(env->kernel_name, kernel_name, sizeof(env->kernel_name));
    }
end:
    UNLOCK_OPENCL;
    return ret;
}

void av_opencl_release_kernel(AVOpenCLKernelEnv *env)
{
    cl_int status;
    LOCK_OPENCL
    if (!env->kernel)
        goto end;
    status = clReleaseKernel(env->kernel);
    if (status != CL_SUCCESS) {
        av_log(&openclutils, AV_LOG_ERROR, "Could not release kernel: %s\n",
              opencl_errstr(status));
    }
    env->kernel = NULL;
    env->command_queue = NULL;
    env->kernel_name[0] = 0;
    gpu_env.kernel_count--;
end:
    UNLOCK_OPENCL
}

static int init_opencl_env(GPUEnv *gpu_env, AVOpenCLExternalEnv *ext_opencl_env)
{
    size_t device_length;
    cl_int status;
    cl_uint num_platforms, num_devices;
    cl_platform_id *platform_ids = NULL;
    cl_context_properties cps[3];
    char platform_name[100];
    int i, j, ret = 0;

    if (ext_opencl_env) {
        if (gpu_env->is_user_created)
            return 0;
        gpu_env->platform_id     = ext_opencl_env->platform_id;
        gpu_env->is_user_created = 1;
        gpu_env->command_queue   = ext_opencl_env->command_queue;
        gpu_env->context         = ext_opencl_env->context;
        gpu_env->device_ids      = ext_opencl_env->device_ids;
        gpu_env->device_id       = ext_opencl_env->device_id;
        gpu_env->device_type     = ext_opencl_env->device_type;
    } else {
        if (!gpu_env->is_user_created) {
            status = clGetPlatformIDs(0, NULL, &num_platforms);
            if (status != CL_SUCCESS) {
                av_log(&openclutils, AV_LOG_ERROR, "Could not get OpenCL platform ids: %s\n", opencl_errstr(status));
                return AVERROR_EXTERNAL;
            }
            if (gpu_env->usr_spec_dev_info.platform_idx >= 0) {
                if (num_platforms < gpu_env->usr_spec_dev_info.platform_idx + 1) {
                    av_log(&openclutils, AV_LOG_ERROR, "User set platform index not exist\n");
                    return AVERROR(EINVAL);
                }
            }
            if (num_platforms > 0) {
                platform_ids = av_mallocz(num_platforms * sizeof(cl_platform_id));
                if (!platform_ids) {
                    ret = AVERROR(ENOMEM);
                    goto end;
                }
                status = clGetPlatformIDs(num_platforms, platform_ids, NULL);
                if (status != CL_SUCCESS) {
                    av_log(&openclutils, AV_LOG_ERROR, "Could not get OpenCL platform ids: %s\n", opencl_errstr(status));
                    ret = AVERROR_EXTERNAL;
                    goto end;
                }
                i = 0;
                if (gpu_env->usr_spec_dev_info.platform_idx >= 0) {
                    i = gpu_env->usr_spec_dev_info.platform_idx;
                }
                while (i < num_platforms) {
                    status = clGetPlatformInfo(platform_ids[i], CL_PLATFORM_VENDOR,
                                               sizeof(platform_name), platform_name,
                                               NULL);

                    if (status != CL_SUCCESS) {
                        av_log(&openclutils, AV_LOG_ERROR, "Could not get OpenCL platform info: %s\n", opencl_errstr(status));
                        ret = AVERROR_EXTERNAL;
                        goto end;
                    }
                    gpu_env->platform_id = platform_ids[i];
                    for (j = 0; j < FF_ARRAY_ELEMS(device_type); j++) {
                        status = clGetDeviceIDs(gpu_env->platform_id, device_type[j], 0, NULL, &num_devices);
                        if (status == CL_SUCCESS)
                            break;
                    }
                    if (num_devices)
                       break;
                    if (gpu_env->usr_spec_dev_info.platform_idx >= 0) {
                        av_log(&openclutils, AV_LOG_ERROR, "Device number of user set platform is 0\n");
                        ret = AVERROR_EXTERNAL;
                        goto end;
                    }
                    if (i >= num_platforms - 1) {
                        if (status != CL_SUCCESS) {
                            av_log(&openclutils, AV_LOG_ERROR,
                                    "Could not get OpenCL device ids: %s\n", opencl_errstr(status));
                            ret = AVERROR(EINVAL);
                            goto end;
                        }
                    }
                    i++;
                }
            }
            if (!gpu_env->platform_id) {
                av_log(&openclutils, AV_LOG_ERROR, "Could not get OpenCL platforms\n");
                ret = AVERROR_EXTERNAL;
                goto end;
            }
            if (gpu_env->usr_spec_dev_info.dev_idx >= 0) {
                if (num_devices < gpu_env->usr_spec_dev_info.dev_idx + 1) {
                    av_log(&openclutils, AV_LOG_ERROR, "Could not get OpenCL device idx in the user set platform\n");
                    ret = AVERROR(EINVAL);
                    goto end;
                }
            }

            /*
             * Use available platform.
             */
            av_log(&openclutils, AV_LOG_VERBOSE, "Platform Name: %s\n", platform_name);
            cps[0] = CL_CONTEXT_PLATFORM;
            cps[1] = (cl_context_properties)gpu_env->platform_id;
            cps[2] = 0;

            /* Check for GPU. */
            for (i = 0; i < FF_ARRAY_ELEMS(device_type); i++) {
                gpu_env->device_type = device_type[i];
                gpu_env->context     = clCreateContextFromType(cps, gpu_env->device_type,
                                                               NULL, NULL, &status);
                if (status == CL_SUCCESS)
                    break;
            }
            if (!gpu_env->context) {
                av_log(&openclutils, AV_LOG_ERROR,
                       "Could not get OpenCL context from device type: %s\n", opencl_errstr(status));
                ret = AVERROR_EXTERNAL;
                goto end;
            }
            /* Detect OpenCL devices. */
            /* First, get the size of device list data */
            status = clGetContextInfo(gpu_env->context, CL_CONTEXT_DEVICES,
                                      0, NULL, &device_length);
            if (status != CL_SUCCESS) {
                av_log(&openclutils, AV_LOG_ERROR,
                       "Could not get OpenCL device length: %s\n", opencl_errstr(status));
                ret = AVERROR_EXTERNAL;
                goto end;
            }
            if (device_length == 0) {
                av_log(&openclutils, AV_LOG_ERROR, "Could not get OpenCL device length\n");
                ret = AVERROR_EXTERNAL;
                goto end;
            }
            /* Now allocate memory for device list based on the size we got earlier */
            gpu_env->device_ids = av_mallocz(device_length);
            if (!gpu_env->device_ids) {
                ret = AVERROR(ENOMEM);
                goto end;
            }
            /* Now, get the device list data */
            status = clGetContextInfo(gpu_env->context, CL_CONTEXT_DEVICES, device_length,
                                      gpu_env->device_ids, NULL);
            if (status != CL_SUCCESS) {
                av_log(&openclutils, AV_LOG_ERROR,
                       "Could not get OpenCL context info: %s\n", opencl_errstr(status));
                ret = AVERROR_EXTERNAL;
                goto end;
            }
            /* Create OpenCL command queue. */
            i = 0;
            if (gpu_env->usr_spec_dev_info.dev_idx >= 0) {
                i = gpu_env->usr_spec_dev_info.dev_idx;
            }
            gpu_env->command_queue = clCreateCommandQueue(gpu_env->context, gpu_env->device_ids[i],
                                                          0, &status);
            if (status != CL_SUCCESS) {
                av_log(&openclutils, AV_LOG_ERROR,
                       "Could not create OpenCL command queue: %s\n", opencl_errstr(status));
                ret = AVERROR_EXTERNAL;
                goto end;
            }
        }
    }
end:
    av_free(platform_ids);
    return ret;
}

static int compile_kernel_file(GPUEnv *gpu_env, const char *build_options)
{
    cl_int status;
    char *temp, *source_str = NULL;
    size_t source_str_len = 0;
    int i, ret = 0;

    for (i = 0; i < gpu_env->kernel_code_count; i++) {
        if (!gpu_env->kernel_code[i].is_compiled)
            source_str_len += strlen(gpu_env->kernel_code[i].kernel_string);
    }
    if (!source_str_len) {
        return 0;
    }
    source_str = av_mallocz(source_str_len + 1);
    if (!source_str) {
        return AVERROR(ENOMEM);
    }
    temp = source_str;
    for (i = 0; i < gpu_env->kernel_code_count; i++) {
        if (!gpu_env->kernel_code[i].is_compiled) {
            memcpy(temp, gpu_env->kernel_code[i].kernel_string,
                        strlen(gpu_env->kernel_code[i].kernel_string));
            gpu_env->kernel_code[i].is_compiled = 1;
            temp += strlen(gpu_env->kernel_code[i].kernel_string);
        }
    }
    /* create a CL program using the kernel source */
    gpu_env->programs[gpu_env->program_count] = clCreateProgramWithSource(gpu_env->context,
                                                           1, (const char **)(&source_str),
                                                                   &source_str_len, &status);
    if(status != CL_SUCCESS) {
        av_log(&openclutils, AV_LOG_ERROR,
               "Could not create OpenCL program with source code: %s\n", opencl_errstr(status));
        ret = AVERROR_EXTERNAL;
        goto end;
    }
    if (!gpu_env->programs[gpu_env->program_count]) {
        av_log(&openclutils, AV_LOG_ERROR, "Created program is NULL\n");
        ret = AVERROR_EXTERNAL;
        goto end;
    }
    i = 0;
    if (gpu_env->usr_spec_dev_info.dev_idx >= 0)
        i = gpu_env->usr_spec_dev_info.dev_idx;
    /* create a cl program executable for all the devices specified */
    if (!gpu_env->is_user_created)
        status = clBuildProgram(gpu_env->programs[gpu_env->program_count], 1, &gpu_env->device_ids[i],
                                build_options, NULL, NULL);
    else
        status = clBuildProgram(gpu_env->programs[gpu_env->program_count], 1, &(gpu_env->device_id),
                                 build_options, NULL, NULL);

    if (status != CL_SUCCESS) {
        av_log(&openclutils, AV_LOG_ERROR,
               "Could not compile OpenCL kernel: %s\n", opencl_errstr(status));
        ret = AVERROR_EXTERNAL;
        goto end;
    }
    gpu_env->program_count++;
end:
    av_free(source_str);
    return ret;
}

int av_opencl_init(AVDictionary *options, AVOpenCLExternalEnv *ext_opencl_env)
{
    int ret = 0;
    AVDictionaryEntry *opt_build_entry;
    AVDictionaryEntry *opt_platform_entry;
    AVDictionaryEntry *opt_device_entry;
    char *pos;
    LOCK_OPENCL
    if (!gpu_env.init_count) {
        opt_platform_entry = av_dict_get(options, "platform_idx", NULL, 0);
        opt_device_entry   = av_dict_get(options, "device_idx", NULL, 0);
        /* initialize devices, context, command_queue */
        gpu_env.usr_spec_dev_info.platform_idx = -1;
        gpu_env.usr_spec_dev_info.dev_idx = -1;
        if (opt_platform_entry) {
            gpu_env.usr_spec_dev_info.platform_idx = strtol(opt_platform_entry->value, &pos, 10);
            if (pos == opt_platform_entry->value) {
                av_log(&openclutils, AV_LOG_ERROR, "Platform index should be a number\n");
                ret = AVERROR(EINVAL);
                goto end;
            }
        }
        if (opt_device_entry) {
            gpu_env.usr_spec_dev_info.dev_idx = strtol(opt_device_entry->value, &pos, 10);
            if (pos == opt_platform_entry->value) {
                av_log(&openclutils, AV_LOG_ERROR, "Device index should be a number\n");
                ret = AVERROR(EINVAL);
                goto end;
            }
        }
        ret = init_opencl_env(&gpu_env, ext_opencl_env);
        if (ret < 0)
            goto end;
    }
    /*initialize program, kernel_name, kernel_count*/
    opt_build_entry = av_dict_get(options, "build_options", NULL, 0);
    if (opt_build_entry)
        ret = compile_kernel_file(&gpu_env, opt_build_entry->value);
    else
        ret = compile_kernel_file(&gpu_env, NULL);
    if (ret < 0)
        goto end;
    if (gpu_env.kernel_code_count <= 0) {
        av_log(&openclutils, AV_LOG_ERROR,
               "No kernel code is registered, compile kernel file failed\n");
        ret = AVERROR(EINVAL);
        goto end;
    }
    gpu_env.init_count++;

end:
    UNLOCK_OPENCL
    return ret;
}

void av_opencl_uninit(void)
{
    cl_int status;
    int i;
    LOCK_OPENCL
    gpu_env.init_count--;
    if (gpu_env.is_user_created)
        goto end;
    if (gpu_env.init_count > 0 || gpu_env.kernel_count > 0)
        goto end;
    for (i = 0; i < gpu_env.program_count; i++) {
        if (gpu_env.programs[i]) {
            status = clReleaseProgram(gpu_env.programs[i]);
            if (status != CL_SUCCESS) {
                av_log(&openclutils, AV_LOG_ERROR,
                       "Could not release OpenCL program: %s\n", opencl_errstr(status));
            }
            gpu_env.programs[i] = NULL;
        }
    }
    if (gpu_env.command_queue) {
        status = clReleaseCommandQueue(gpu_env.command_queue);
        if (status != CL_SUCCESS) {
            av_log(&openclutils, AV_LOG_ERROR,
                   "Could not release OpenCL command queue: %s\n", opencl_errstr(status));
        }
        gpu_env.command_queue = NULL;
    }
    if (gpu_env.context) {
        status = clReleaseContext(gpu_env.context);
        if (status != CL_SUCCESS) {
            av_log(&openclutils, AV_LOG_ERROR,
                   "Could not release OpenCL context: %s\n", opencl_errstr(status));
        }
        gpu_env.context = NULL;
    }
    av_freep(&(gpu_env.device_ids));
end:
    UNLOCK_OPENCL
}

int av_opencl_buffer_create(cl_mem *cl_buf, size_t cl_buf_size, int flags, void *host_ptr)
{
    cl_int status;
    *cl_buf = clCreateBuffer(gpu_env.context, flags, cl_buf_size, host_ptr, &status);
    if (status != CL_SUCCESS) {
        av_log(&openclutils, AV_LOG_ERROR, "Could not create OpenCL buffer: %s\n", opencl_errstr(status));
        return AVERROR_EXTERNAL;
    }
    return 0;
}

void av_opencl_buffer_release(cl_mem *cl_buf)
{
    cl_int status = 0;
    if (!cl_buf)
        return;
    status = clReleaseMemObject(*cl_buf);
    if (status != CL_SUCCESS) {
        av_log(&openclutils, AV_LOG_ERROR,
               "Could not release OpenCL buffer: %s\n", opencl_errstr(status));
    }
    memset(cl_buf, 0, sizeof(*cl_buf));
}

int av_opencl_buffer_write(cl_mem dst_cl_buf, uint8_t *src_buf, size_t buf_size)
{
    cl_int status;
    void *mapped = clEnqueueMapBuffer(gpu_env.command_queue, dst_cl_buf,
                                      CL_TRUE,CL_MAP_WRITE, 0, sizeof(uint8_t) * buf_size,
                                      0, NULL, NULL, &status);

    if (status != CL_SUCCESS) {
        av_log(&openclutils, AV_LOG_ERROR,
               "Could not map OpenCL buffer: %s\n", opencl_errstr(status));
        return AVERROR_EXTERNAL;
    }
    memcpy(mapped, src_buf, buf_size);

    status = clEnqueueUnmapMemObject(gpu_env.command_queue, dst_cl_buf, mapped, 0, NULL, NULL);
    if (status != CL_SUCCESS) {
        av_log(&openclutils, AV_LOG_ERROR,
               "Could not unmap OpenCL buffer: %s\n", opencl_errstr(status));
        return AVERROR_EXTERNAL;
    }
    return 0;
}

int av_opencl_buffer_read(uint8_t *dst_buf, cl_mem src_cl_buf, size_t buf_size)
{
    cl_int status;
    void *mapped = clEnqueueMapBuffer(gpu_env.command_queue, src_cl_buf,
                                      CL_TRUE,CL_MAP_READ, 0, buf_size,
                                      0, NULL, NULL, &status);

    if (status != CL_SUCCESS) {
        av_log(&openclutils, AV_LOG_ERROR,
               "Could not map OpenCL buffer: %s\n", opencl_errstr(status));
        return AVERROR_EXTERNAL;
    }
    memcpy(dst_buf, mapped, buf_size);

    status = clEnqueueUnmapMemObject(gpu_env.command_queue, src_cl_buf, mapped, 0, NULL, NULL);
    if (status != CL_SUCCESS) {
        av_log(&openclutils, AV_LOG_ERROR,
               "Could not unmap OpenCL buffer: %s\n", opencl_errstr(status));
        return AVERROR_EXTERNAL;
    }
    return 0;
}

int av_opencl_buffer_write_image(cl_mem dst_cl_buf, size_t cl_buffer_size, int dst_cl_offset,
                                 uint8_t **src_data, int *plane_size, int plane_num)
{
    int i, buffer_size = 0;
    uint8_t *temp;
    cl_int status;
    void *mapped;
    if ((unsigned int)plane_num > 8) {
        return AVERROR(EINVAL);
    }
    for (i = 0;i < plane_num;i++) {
        buffer_size += plane_size[i];
    }
    if (buffer_size > cl_buffer_size) {
        av_log(&openclutils, AV_LOG_ERROR,
               "Cannot write image to OpenCL buffer: buffer too small\n");
        return AVERROR(EINVAL);
    }
    mapped = clEnqueueMapBuffer(gpu_env.command_queue, dst_cl_buf,
                                CL_TRUE,CL_MAP_WRITE, 0, buffer_size + dst_cl_offset,
                                0, NULL, NULL, &status);
    if (status != CL_SUCCESS) {
        av_log(&openclutils, AV_LOG_ERROR,
               "Could not map OpenCL buffer: %s\n", opencl_errstr(status));
        return AVERROR_EXTERNAL;
    }
    temp = mapped;
    temp += dst_cl_offset;
    for (i = 0; i < plane_num; i++) {
        memcpy(temp, src_data[i], plane_size[i]);
        temp += plane_size[i];
    }
    status = clEnqueueUnmapMemObject(gpu_env.command_queue, dst_cl_buf, mapped, 0, NULL, NULL);
    if (status != CL_SUCCESS) {
        av_log(&openclutils, AV_LOG_ERROR,
               "Could not unmap OpenCL buffer: %s\n", opencl_errstr(status));
        return AVERROR_EXTERNAL;
    }
    return 0;
}

int av_opencl_buffer_read_image(uint8_t **dst_data, int *plane_size, int plane_num,
                                cl_mem src_cl_buf, size_t cl_buffer_size)
{
    int i,buffer_size = 0,ret = 0;
    uint8_t *temp;
    void *mapped;
    cl_int status;
    if ((unsigned int)plane_num > 8) {
        return AVERROR(EINVAL);
    }
    for (i = 0; i < plane_num; i++) {
        buffer_size += plane_size[i];
    }
    if (buffer_size > cl_buffer_size) {
        av_log(&openclutils, AV_LOG_ERROR,
               "Cannot write image to CPU buffer: OpenCL buffer too small\n");
        return AVERROR(EINVAL);
    }
    mapped = clEnqueueMapBuffer(gpu_env.command_queue, src_cl_buf,
                                CL_TRUE,CL_MAP_READ, 0, buffer_size,
                                0, NULL, NULL, &status);

    if (status != CL_SUCCESS) {
        av_log(&openclutils, AV_LOG_ERROR,
               "Could not map OpenCL buffer: %s\n", opencl_errstr(status));
        return AVERROR_EXTERNAL;
    }
    temp = mapped;
    if (ret >= 0) {
        for (i = 0; i < plane_num; i++) {
            memcpy(dst_data[i], temp, plane_size[i]);
            temp += plane_size[i];
        }
    }
    status = clEnqueueUnmapMemObject(gpu_env.command_queue, src_cl_buf, mapped, 0, NULL, NULL);
    if (status != CL_SUCCESS) {
        av_log(&openclutils, AV_LOG_ERROR,
               "Could not unmap OpenCL buffer: %s\n", opencl_errstr(status));
        return AVERROR_EXTERNAL;
    }
    return 0;
}
