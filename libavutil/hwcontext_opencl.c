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

#include <string.h>

#include "config.h"

#include "avassert.h"
#include "avstring.h"
#include "common.h"
#include "hwcontext.h"
#include "hwcontext_internal.h"
#include "hwcontext_opencl.h"
#include "mem.h"
#include "pixdesc.h"


typedef struct OpenCLDeviceContext {
    // Default command queue to use for transfer/mapping operations on
    // the device.  If the user supplies one, this is a reference to it.
    // Otherwise, it is newly-created.
    cl_command_queue command_queue;

    // The platform the context exists on.  This is needed to query and
    // retrieve extension functions.
    cl_platform_id platform_id;

    // Platform/device-specific functions.
} OpenCLDeviceContext;

typedef struct OpenCLFramesContext {
    // Command queue used for transfer/mapping operations on this frames
    // context.  If the user supplies one, this is a reference to it.
    // Otherwise, it is a reference to the default command queue for the
    // device.
    cl_command_queue command_queue;
} OpenCLFramesContext;


static void opencl_error_callback(const char *errinfo,
                                  const void *private_info, size_t cb,
                                  void *user_data)
{
    AVHWDeviceContext *ctx = user_data;
    av_log(ctx, AV_LOG_ERROR, "OpenCL error: %s\n", errinfo);
}

static void opencl_device_free(AVHWDeviceContext *hwdev)
{
    AVOpenCLDeviceContext *hwctx = hwdev->hwctx;
    cl_int cle;

    cle = clReleaseContext(hwctx->context);
    if (cle != CL_SUCCESS) {
        av_log(hwdev, AV_LOG_ERROR, "Failed to release OpenCL "
               "context: %d.\n", cle);
    }
}

static struct {
    const char *key;
    cl_platform_info name;
} opencl_platform_params[] = {
    { "platform_profile",    CL_PLATFORM_PROFILE    },
    { "platform_version",    CL_PLATFORM_VERSION    },
    { "platform_name",       CL_PLATFORM_NAME       },
    { "platform_vendor",     CL_PLATFORM_VENDOR     },
    { "platform_extensions", CL_PLATFORM_EXTENSIONS },
};

static struct {
    const char *key;
    cl_device_info name;
} opencl_device_params[] = {
    { "device_name",         CL_DEVICE_NAME         },
    { "device_vendor",       CL_DEVICE_VENDOR       },
    { "driver_version",      CL_DRIVER_VERSION      },
    { "device_version",      CL_DEVICE_VERSION      },
    { "device_profile",      CL_DEVICE_PROFILE      },
    { "device_extensions",   CL_DEVICE_EXTENSIONS   },
};

static struct {
    const char *key;
    cl_device_type type;
} opencl_device_types[] = {
    { "cpu",         CL_DEVICE_TYPE_CPU         },
    { "gpu",         CL_DEVICE_TYPE_GPU         },
    { "accelerator", CL_DEVICE_TYPE_ACCELERATOR },
    { "custom",      CL_DEVICE_TYPE_CUSTOM      },
    { "default",     CL_DEVICE_TYPE_DEFAULT     },
    { "all",         CL_DEVICE_TYPE_ALL         },
};

static char *opencl_get_platform_string(cl_platform_id platform_id,
                                        cl_platform_info key)
{
    char *str;
    size_t size;
    cl_int cle;
    cle = clGetPlatformInfo(platform_id, key, 0, NULL, &size);
    if (cle != CL_SUCCESS)
        return NULL;
    str = av_malloc(size);
    if (!str)
        return NULL;
    cle = clGetPlatformInfo(platform_id, key, size, str, &size);
    if (cle != CL_SUCCESS) {
        av_free(str);
        return NULL;
    }
    av_assert0(strlen(str) + 1 == size);
    return str;
}

static char *opencl_get_device_string(cl_device_id device_id,
                                      cl_device_info key)
{
    char *str;
    size_t size;
    cl_int cle;
    cle = clGetDeviceInfo(device_id, key, 0, NULL, &size);
    if (cle != CL_SUCCESS)
        return NULL;
    str = av_malloc(size);
    if (!str)
        return NULL;
    cle = clGetDeviceInfo(device_id, key, size, str, &size);
    if (cle != CL_SUCCESS) {
        av_free(str);
        return NULL;
    }
    av_assert0(strlen(str) + 1== size);
    return str;
}

static int opencl_check_platform_extension(cl_platform_id platform_id,
                                           const char *name)
{
    char *str;
    int found = 0;
    str = opencl_get_platform_string(platform_id,
                                     CL_PLATFORM_EXTENSIONS);
    if (str && strstr(str, name))
        found = 1;
    av_free(str);
    return found;
}

static int opencl_check_device_extension(cl_device_id device_id,
                                         const char *name)
{
    char *str;
    int found = 0;
    str = opencl_get_device_string(device_id,
                                   CL_DEVICE_EXTENSIONS);
    if (str && strstr(str, name))
        found = 1;
    av_free(str);
    return found;
}

static av_unused int opencl_check_extension(AVHWDeviceContext *hwdev,
                                            const char *name)
{
    AVOpenCLDeviceContext *hwctx = hwdev->hwctx;
    OpenCLDeviceContext    *priv = hwdev->internal->priv;

    if (opencl_check_platform_extension(priv->platform_id, name)) {
        av_log(hwdev, AV_LOG_DEBUG,
               "%s found as platform extension.\n", name);
        return 1;
    }

    if (opencl_check_device_extension(hwctx->device_id, name)) {
        av_log(hwdev, AV_LOG_DEBUG,
               "%s found as device extension.\n", name);
        return 1;
    }

    return 0;
}

static int opencl_enumerate_platforms(AVHWDeviceContext *hwdev,
                                      cl_uint *nb_platforms,
                                      cl_platform_id **platforms,
                                      void *context)
{
    cl_int cle;

    cle = clGetPlatformIDs(0, NULL, nb_platforms);
    if (cle != CL_SUCCESS) {
        av_log(hwdev, AV_LOG_ERROR, "Failed to get number of "
               "OpenCL platforms: %d.\n", cle);
        return AVERROR(ENODEV);
    }
    av_log(hwdev, AV_LOG_DEBUG, "%u OpenCL platforms found.\n",
           *nb_platforms);

    *platforms = av_malloc_array(*nb_platforms, sizeof(**platforms));
    if (!*platforms)
        return AVERROR(ENOMEM);

    cle = clGetPlatformIDs(*nb_platforms, *platforms, NULL);
    if (cle != CL_SUCCESS) {
        av_log(hwdev, AV_LOG_ERROR, "Failed to get list of OpenCL "
               "platforms: %d.\n", cle);
        av_freep(platforms);
        return AVERROR(ENODEV);
    }

    return 0;
}

static int opencl_filter_platform(AVHWDeviceContext *hwdev,
                                  cl_platform_id platform_id,
                                  const char *platform_name,
                                  void *context)
{
    AVDictionary *opts = context;
    const AVDictionaryEntry *param;
    char *str;
    int i, ret = 0;

    for (i = 0; i < FF_ARRAY_ELEMS(opencl_platform_params); i++) {
        param = av_dict_get(opts, opencl_platform_params[i].key,
                            NULL, 0);
        if (!param)
            continue;

        str = opencl_get_platform_string(platform_id,
                                         opencl_platform_params[i].name);
        if (!str) {
            av_log(hwdev, AV_LOG_ERROR, "Failed to query %s "
                   "of platform \"%s\".\n",
                   opencl_platform_params[i].key, platform_name);
            return AVERROR_UNKNOWN;
        }
        if (!av_stristr(str, param->value)) {
            av_log(hwdev, AV_LOG_DEBUG, "%s does not match (\"%s\").\n",
                   param->key, str);
            ret = 1;
        }
        av_free(str);
    }

    return ret;
}

static int opencl_enumerate_devices(AVHWDeviceContext *hwdev,
                                    cl_platform_id platform_id,
                                    const char *platform_name,
                                    cl_uint *nb_devices,
                                    cl_device_id **devices,
                                    void *context)
{
    cl_int cle;

    cle = clGetDeviceIDs(platform_id, CL_DEVICE_TYPE_ALL,
                         0, NULL, nb_devices);
    if (cle == CL_DEVICE_NOT_FOUND) {
        av_log(hwdev, AV_LOG_DEBUG, "No devices found "
               "on platform \"%s\".\n", platform_name);
        *nb_devices = 0;
        return 0;
    } else if (cle != CL_SUCCESS) {
        av_log(hwdev, AV_LOG_ERROR, "Failed to get number of devices "
               "on platform \"%s\": %d.\n", platform_name, cle);
        return AVERROR(ENODEV);
    }
    av_log(hwdev, AV_LOG_DEBUG, "%u OpenCL devices found on "
           "platform \"%s\".\n", *nb_devices, platform_name);

    *devices = av_malloc_array(*nb_devices, sizeof(**devices));
    if (!*devices)
        return AVERROR(ENOMEM);

    cle = clGetDeviceIDs(platform_id, CL_DEVICE_TYPE_ALL,
                         *nb_devices, *devices, NULL);
    if (cle != CL_SUCCESS) {
        av_log(hwdev, AV_LOG_ERROR, "Failed to get list of devices "
               "on platform \"%s\": %d.\n", platform_name, cle);
        av_freep(devices);
        return AVERROR(ENODEV);
    }

    return 0;
}

static int opencl_filter_device(AVHWDeviceContext *hwdev,
                                cl_device_id device_id,
                                const char *device_name,
                                void *context)
{
    AVDictionary *opts = context;
    const AVDictionaryEntry *param;
    char *str;
    int i, ret = 0;

    param = av_dict_get(opts, "device_type", NULL, 0);
    if (param) {
        cl_device_type match_type = 0, device_type;
        cl_int cle;

        for (i = 0; i < FF_ARRAY_ELEMS(opencl_device_types); i++) {
            if (!strcmp(opencl_device_types[i].key, param->value)) {
                match_type = opencl_device_types[i].type;
                break;
            }
        }
        if (!match_type) {
            av_log(hwdev, AV_LOG_ERROR, "Unknown device type %s.\n",
                   param->value);
            return AVERROR(EINVAL);
        }

        cle = clGetDeviceInfo(device_id, CL_DEVICE_TYPE,
                              sizeof(device_type), &device_type, NULL);
        if (cle != CL_SUCCESS) {
            av_log(hwdev, AV_LOG_ERROR, "Failed to query device type "
                   "of device \"%s\".\n", device_name);
            return AVERROR_UNKNOWN;
        }

        if (!(device_type & match_type)) {
            av_log(hwdev, AV_LOG_DEBUG, "device_type does not match.\n");
            return 1;
        }
    }

    for (i = 0; i < FF_ARRAY_ELEMS(opencl_device_params); i++) {
        param = av_dict_get(opts, opencl_device_params[i].key,
                            NULL, 0);
        if (!param)
            continue;

        str = opencl_get_device_string(device_id,
                                       opencl_device_params[i].name);
        if (!str) {
            av_log(hwdev, AV_LOG_ERROR, "Failed to query %s "
                   "of device \"%s\".\n",
                   opencl_device_params[i].key, device_name);
            return AVERROR_UNKNOWN;
        }
        if (!av_stristr(str, param->value)) {
            av_log(hwdev, AV_LOG_DEBUG, "%s does not match (\"%s\").\n",
                   param->key, str);
            ret = 1;
        }
        av_free(str);
    }

    return ret;
}

typedef struct OpenCLDeviceSelector {
    int platform_index;
    int device_index;
    void *context;
    int (*enumerate_platforms)(AVHWDeviceContext *hwdev,
                               cl_uint *nb_platforms,
                               cl_platform_id **platforms,
                               void *context);
    int (*filter_platform)    (AVHWDeviceContext *hwdev,
                               cl_platform_id platform_id,
                               const char *platform_name,
                               void *context);
    int (*enumerate_devices)  (AVHWDeviceContext *hwdev,
                               cl_platform_id platform_id,
                               const char *platform_name,
                               cl_uint *nb_devices,
                               cl_device_id **devices,
                               void *context);
    int (*filter_device)      (AVHWDeviceContext *hwdev,
                               cl_device_id device_id,
                               const char *device_name,
                               void *context);
} OpenCLDeviceSelector;

static int opencl_device_create_internal(AVHWDeviceContext *hwdev,
                                         const OpenCLDeviceSelector *selector,
                                         cl_context_properties *props)
{
    cl_uint      nb_platforms;
    cl_platform_id *platforms = NULL;
    cl_platform_id  platform_id;
    cl_uint      nb_devices;
    cl_device_id   *devices = NULL;
    AVOpenCLDeviceContext *hwctx = hwdev->hwctx;
    cl_int cle;
    cl_context_properties default_props[3];
    char *platform_name_src = NULL,
         *device_name_src   = NULL;
    int err, found, p, d;

    err = selector->enumerate_platforms(hwdev, &nb_platforms, &platforms,
                                        selector->context);
    if (err)
        return err;

    found = 0;
    for (p = 0; p < nb_platforms; p++) {
        const char *platform_name;

        if (selector->platform_index >= 0 &&
            selector->platform_index != p)
            continue;

        av_freep(&platform_name_src);
        platform_name_src = opencl_get_platform_string(platforms[p],
                                                           CL_PLATFORM_NAME);
        if (platform_name_src)
            platform_name = platform_name_src;
        else
            platform_name = "Unknown Platform";

        if (selector->filter_platform) {
            err = selector->filter_platform(hwdev, platforms[p],
                                            platform_name,
                                            selector->context);
            if (err < 0)
                goto fail;
            if (err > 0)
                continue;
        }

        err = opencl_enumerate_devices(hwdev, platforms[p], platform_name,
                                       &nb_devices, &devices,
                                       selector->context);
        if (err < 0)
            continue;

        for (d = 0; d < nb_devices; d++) {
            const char *device_name;

            if (selector->device_index >= 0 &&
                selector->device_index != d)
                continue;

            av_freep(&device_name_src);
            device_name_src = opencl_get_device_string(devices[d],
                                                           CL_DEVICE_NAME);
            if (device_name_src)
                device_name = device_name_src;
            else
                device_name = "Unknown Device";

            if (selector->filter_device) {
                err = selector->filter_device(hwdev, devices[d],
                                              device_name,
                                              selector->context);
                if (err < 0)
                    goto fail;
                if (err > 0)
                    continue;
            }

            av_log(hwdev, AV_LOG_VERBOSE, "%d.%d: %s / %s\n", p, d,
                   platform_name, device_name);

            ++found;
            platform_id      = platforms[p];
            hwctx->device_id = devices[d];
        }

        av_freep(&devices);
    }

    if (found == 0) {
        av_log(hwdev, AV_LOG_ERROR, "No matching devices found.\n");
        err = AVERROR(ENODEV);
        goto fail;
    }
    if (found > 1) {
        av_log(hwdev, AV_LOG_ERROR, "More than one matching device found.\n");
        err = AVERROR(ENODEV);
        goto fail;
    }

    if (!props) {
        props = default_props;
        default_props[0] = CL_CONTEXT_PLATFORM;
        default_props[1] = (intptr_t)platform_id;
        default_props[2] = 0;
    } else {
        if (props[0] == CL_CONTEXT_PLATFORM && props[1] == 0)
            props[1] = (intptr_t)platform_id;
    }

    hwctx->context = clCreateContext(props, 1, &hwctx->device_id,
                                     &opencl_error_callback, hwdev, &cle);
    if (!hwctx->context) {
        av_log(hwdev, AV_LOG_ERROR, "Failed to create OpenCL context: "
               "%d.\n", cle);
        err = AVERROR(ENODEV);
        goto fail;
    }

    hwdev->free = &opencl_device_free;

    err = 0;
fail:
    av_freep(&platform_name_src);
    av_freep(&device_name_src);
    av_freep(&platforms);
    av_freep(&devices);
    return err;
}

static int opencl_device_create(AVHWDeviceContext *hwdev, const char *device,
                                AVDictionary *opts, int flags)
{
    OpenCLDeviceSelector selector = {
        .context = opts,
        .enumerate_platforms = &opencl_enumerate_platforms,
        .filter_platform     = &opencl_filter_platform,
        .enumerate_devices   = &opencl_enumerate_devices,
        .filter_device       = &opencl_filter_device,
    };

    if (device && device[0]) {
        // Match one or both indices for platform and device.
        int d = -1, p = -1, ret;
        if (device[0] == '.')
            ret = sscanf(device, ".%d", &d);
        else
            ret = sscanf(device, "%d.%d", &p, &d);
        if (ret < 1) {
            av_log(hwdev, AV_LOG_ERROR, "Invalid OpenCL platform/device "
                   "index specification \"%s\".\n", device);
            return AVERROR(EINVAL);
        }
        selector.platform_index = p;
        selector.device_index   = d;
    } else {
        selector.platform_index = -1;
        selector.device_index   = -1;
    }

    return opencl_device_create_internal(hwdev, &selector, NULL);
}

static int opencl_device_init(AVHWDeviceContext *hwdev)
{
    AVOpenCLDeviceContext *hwctx = hwdev->hwctx;
    OpenCLDeviceContext    *priv = hwdev->internal->priv;
    cl_int cle;

    if (hwctx->command_queue) {
        cle = clRetainCommandQueue(hwctx->command_queue);
        if (cle != CL_SUCCESS) {
            av_log(hwdev, AV_LOG_ERROR, "Failed to retain external "
                   "command queue: %d.\n", cle);
            return AVERROR(EIO);
        }
        priv->command_queue = hwctx->command_queue;
    } else {
        priv->command_queue = clCreateCommandQueue(hwctx->context,
                                                   hwctx->device_id,
                                                   0, &cle);
        if (!priv->command_queue) {
            av_log(hwdev, AV_LOG_ERROR, "Failed to create internal "
                   "command queue: %d.\n", cle);
            return AVERROR(EIO);
        }
    }

    cle = clGetDeviceInfo(hwctx->device_id, CL_DEVICE_PLATFORM,
                          sizeof(priv->platform_id), &priv->platform_id,
                          NULL);
    if (cle != CL_SUCCESS) {
        av_log(hwdev, AV_LOG_ERROR, "Failed to determine the OpenCL "
               "platform containing the device.\n");
        return AVERROR(EIO);
    }

    return 0;
}

static void opencl_device_uninit(AVHWDeviceContext *hwdev)
{
    OpenCLDeviceContext *priv = hwdev->internal->priv;
    cl_int cle;

    if (priv->command_queue) {
        cle = clReleaseCommandQueue(priv->command_queue);
        if (cle != CL_SUCCESS) {
            av_log(hwdev, AV_LOG_ERROR, "Failed to release internal "
                   "command queue reference: %d.\n", cle);
        }
    }
}

static int opencl_get_plane_format(enum AVPixelFormat pixfmt,
                                   int plane, int width, int height,
                                   cl_image_format *image_format,
                                   cl_image_desc *image_desc)
{
    const AVPixFmtDescriptor *desc;
    const AVComponentDescriptor *comp;
    int channels = 0, order = 0, depth = 0, step = 0;
    int wsub, hsub, alpha;
    int c;

    if (plane >= AV_NUM_DATA_POINTERS)
        return AVERROR(ENOENT);

    desc = av_pix_fmt_desc_get(pixfmt);

    // Only normal images are allowed.
    if (desc->flags & (AV_PIX_FMT_FLAG_BITSTREAM |
                       AV_PIX_FMT_FLAG_HWACCEL   |
                       AV_PIX_FMT_FLAG_PAL))
        return AVERROR(EINVAL);

    wsub = 1 << desc->log2_chroma_w;
    hsub = 1 << desc->log2_chroma_h;
    // Subsampled components must be exact.
    if (width & wsub - 1 || height & hsub - 1)
        return AVERROR(EINVAL);

    for (c = 0; c < desc->nb_components; c++) {
        comp = &desc->comp[c];
        if (comp->plane != plane)
            continue;
        // The step size must be a power of two.
        if (comp->step != 1 && comp->step != 2 &&
            comp->step != 4 && comp->step != 8)
            return AVERROR(EINVAL);
        // The bits in each component must be packed in the
        // most-significant-bits of the relevant bytes.
        if (comp->shift + comp->depth != 8 &&
            comp->shift + comp->depth != 16)
            return AVERROR(EINVAL);
        // The depth must not vary between components.
        if (depth && comp->depth != depth)
            return AVERROR(EINVAL);
        // If a single data element crosses multiple bytes then
        // it must match the native endianness.
        if (comp->depth > 8 &&
            HAVE_BIGENDIAN == !(desc->flags & AV_PIX_FMT_FLAG_BE))
            return AVERROR(EINVAL);
        // A single data element must not contain multiple samples
        // from the same component.
        if (step && comp->step != step)
            return AVERROR(EINVAL);
        order = order * 10 + c + 1;
        depth = comp->depth;
        step  = comp->step;
        alpha = (desc->flags & AV_PIX_FMT_FLAG_ALPHA &&
                 c == desc->nb_components - 1);
        ++channels;
    }
    if (channels == 0)
        return AVERROR(ENOENT);

    memset(image_format, 0, sizeof(*image_format));
    memset(image_desc,   0, sizeof(*image_desc));
    image_desc->image_type = CL_MEM_OBJECT_IMAGE2D;

    if (plane == 0 || alpha) {
        image_desc->image_width     = width;
        image_desc->image_height    = height;
        image_desc->image_row_pitch = step * width;
    } else {
        image_desc->image_width     = width  / wsub;
        image_desc->image_height    = height / hsub;
        image_desc->image_row_pitch = step * width / wsub;
    }

    if (depth <= 8) {
        image_format->image_channel_data_type = CL_UNORM_INT8;
    } else {
        if (depth <= 16)
            image_format->image_channel_data_type = CL_UNORM_INT16;
        else
            return AVERROR(EINVAL);
    }

#define CHANNEL_ORDER(order, type) \
    case order: image_format->image_channel_order = type; break;
    switch (order) {
        CHANNEL_ORDER(1,    CL_R);
        CHANNEL_ORDER(2,    CL_R);
        CHANNEL_ORDER(3,    CL_R);
        CHANNEL_ORDER(4,    CL_R);
        CHANNEL_ORDER(12,   CL_RG);
        CHANNEL_ORDER(23,   CL_RG);
        CHANNEL_ORDER(1234, CL_RGBA);
        CHANNEL_ORDER(3214, CL_BGRA);
        CHANNEL_ORDER(4123, CL_ARGB);
#ifdef CL_ABGR
        CHANNEL_ORDER(4321, CL_ABGR);
#endif
    default:
        return AVERROR(EINVAL);
    }
#undef CHANNEL_ORDER

    return 0;
}

static int opencl_frames_get_constraints(AVHWDeviceContext *hwdev,
                                         const void *hwconfig,
                                         AVHWFramesConstraints *constraints)
{
    AVOpenCLDeviceContext *hwctx = hwdev->hwctx;
    cl_uint nb_image_formats;
    cl_image_format *image_formats = NULL;
    cl_int cle;
    enum AVPixelFormat pix_fmt;
    int err, pix_fmts_found;
    size_t max_width, max_height;

    cle = clGetDeviceInfo(hwctx->device_id, CL_DEVICE_IMAGE2D_MAX_WIDTH,
                          sizeof(max_width), &max_width, NULL);
    if (cle != CL_SUCCESS) {
        av_log(hwdev, AV_LOG_ERROR, "Failed to query maximum "
               "supported image width: %d.\n", cle);
    } else {
        constraints->max_width = max_width;
    }
    cle = clGetDeviceInfo(hwctx->device_id, CL_DEVICE_IMAGE2D_MAX_HEIGHT,
                          sizeof(max_height), &max_height, NULL);
    if (cle != CL_SUCCESS) {
        av_log(hwdev, AV_LOG_ERROR, "Failed to query maximum "
               "supported image height: %d.\n", cle);
    } else {
        constraints->max_height = max_height;
    }
    av_log(hwdev, AV_LOG_DEBUG, "Maximum supported image size %dx%d.\n",
           constraints->max_width, constraints->max_height);

    cle = clGetSupportedImageFormats(hwctx->context,
                                     CL_MEM_READ_WRITE,
                                     CL_MEM_OBJECT_IMAGE2D,
                                     0, NULL, &nb_image_formats);
    if (cle != CL_SUCCESS) {
        av_log(hwdev, AV_LOG_ERROR, "Failed to query supported "
               "image formats: %d.\n", cle);
        err = AVERROR(ENOSYS);
        goto fail;
    }
    if (nb_image_formats == 0) {
        av_log(hwdev, AV_LOG_ERROR, "No image support in OpenCL "
               "driver (zero supported image formats).\n");
        err = AVERROR(ENOSYS);
        goto fail;
    }

    image_formats =
        av_malloc_array(nb_image_formats, sizeof(*image_formats));
    if (!image_formats) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    cle = clGetSupportedImageFormats(hwctx->context,
                                     CL_MEM_READ_WRITE,
                                     CL_MEM_OBJECT_IMAGE2D,
                                     nb_image_formats,
                                     image_formats, NULL);
    if (cle != CL_SUCCESS) {
        av_log(hwdev, AV_LOG_ERROR, "Failed to query supported "
               "image formats: %d.\n", cle);
        err = AVERROR(ENOSYS);
        goto fail;
    }

    pix_fmts_found = 0;
    for (pix_fmt = 0; pix_fmt < AV_PIX_FMT_NB; pix_fmt++) {
        cl_image_format image_format;
        cl_image_desc   image_desc;
        int plane, i;

        for (plane = 0;; plane++) {
            err = opencl_get_plane_format(pix_fmt, plane, 0, 0,
                                          &image_format,
                                          &image_desc);
            if (err < 0)
                break;

            for (i = 0; i < nb_image_formats; i++) {
                if (image_formats[i].image_channel_order ==
                    image_format.image_channel_order &&
                    image_formats[i].image_channel_data_type ==
                    image_format.image_channel_data_type)
                    break;
            }
            if (i == nb_image_formats) {
                err = AVERROR(EINVAL);
                break;
            }
        }
        if (err != AVERROR(ENOENT))
            continue;

        av_log(hwdev, AV_LOG_DEBUG, "Format %s supported.\n",
               av_get_pix_fmt_name(pix_fmt));

        err = av_reallocp_array(&constraints->valid_sw_formats,
                                pix_fmts_found + 2,
                                sizeof(*constraints->valid_sw_formats));
        if (err < 0)
            goto fail;
        constraints->valid_sw_formats[pix_fmts_found] = pix_fmt;
        constraints->valid_sw_formats[pix_fmts_found + 1] =
            AV_PIX_FMT_NONE;
        ++pix_fmts_found;
    }

    av_freep(&image_formats);

    constraints->valid_hw_formats =
        av_malloc_array(2, sizeof(*constraints->valid_hw_formats));
    if (!constraints->valid_hw_formats) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    constraints->valid_hw_formats[0] = AV_PIX_FMT_OPENCL;
    constraints->valid_hw_formats[1] = AV_PIX_FMT_NONE;

    return 0;

fail:
    av_freep(&image_formats);
    return err;
}

static void opencl_pool_free(void *opaque, uint8_t *data)
{
    AVHWFramesContext       *hwfc = opaque;
    AVOpenCLFrameDescriptor *desc = (AVOpenCLFrameDescriptor*)data;
    cl_int cle;
    int p;

    for (p = 0; p < desc->nb_planes; p++) {
        cle = clReleaseMemObject(desc->planes[p]);
        if (cle != CL_SUCCESS) {
            av_log(hwfc, AV_LOG_ERROR, "Failed to release plane %d: "
                   "%d.\n", p, cle);
        }
    }

    av_free(desc);
}

static AVBufferRef *opencl_pool_alloc(void *opaque, int size)
{
    AVHWFramesContext      *hwfc = opaque;
    AVOpenCLDeviceContext *hwctx = hwfc->device_ctx->hwctx;
    AVOpenCLFrameDescriptor *desc;
    cl_int cle;
    cl_mem image;
    cl_image_format image_format;
    cl_image_desc   image_desc;
    int err, p;
    AVBufferRef *ref;

    desc = av_mallocz(sizeof(*desc));
    if (!desc)
        return NULL;

    for (p = 0;; p++) {
        err = opencl_get_plane_format(hwfc->sw_format, p,
                                      hwfc->width, hwfc->height,
                                      &image_format, &image_desc);
        if (err == AVERROR(ENOENT))
            break;
        if (err < 0)
            goto fail;

        // For generic image objects, the pitch is determined by the
        // implementation.
        image_desc.image_row_pitch = 0;

        image = clCreateImage(hwctx->context, CL_MEM_READ_WRITE,
                              &image_format, &image_desc, NULL, &cle);
        if (!image) {
            av_log(hwfc, AV_LOG_ERROR, "Failed to create image for "
                   "plane %d: %d.\n", p, cle);
            goto fail;
        }

        desc->planes[p] = image;
    }

    desc->nb_planes = p;

    ref = av_buffer_create((uint8_t*)desc, sizeof(*desc),
                           &opencl_pool_free, hwfc, 0);
    if (!ref)
        goto fail;

    return ref;

fail:
    for (p = 0; desc->planes[p]; p++)
        clReleaseMemObject(desc->planes[p]);
    av_free(desc);
    return NULL;
}

static int opencl_frames_init_command_queue(AVHWFramesContext *hwfc)
{
    AVOpenCLFramesContext *hwctx = hwfc->hwctx;
    OpenCLDeviceContext *devpriv = hwfc->device_ctx->internal->priv;
    OpenCLFramesContext    *priv = hwfc->internal->priv;
    cl_int cle;

    priv->command_queue = hwctx->command_queue ? hwctx->command_queue
                                               : devpriv->command_queue;
    cle = clRetainCommandQueue(priv->command_queue);
    if (cle != CL_SUCCESS) {
        av_log(hwfc, AV_LOG_ERROR, "Failed to retain frame "
               "command queue: %d.\n", cle);
        return AVERROR(EIO);
    }

    return 0;
}

static int opencl_frames_init(AVHWFramesContext *hwfc)
{
    if (!hwfc->pool) {
        hwfc->internal->pool_internal =
            av_buffer_pool_init2(sizeof(cl_mem), hwfc,
                                 &opencl_pool_alloc, NULL);
        if (!hwfc->internal->pool_internal)
            return AVERROR(ENOMEM);
    }

    return opencl_frames_init_command_queue(hwfc);
}

static void opencl_frames_uninit(AVHWFramesContext *hwfc)
{
    OpenCLFramesContext *priv = hwfc->internal->priv;
    cl_int cle;

    cle = clReleaseCommandQueue(priv->command_queue);
    if (cle != CL_SUCCESS) {
        av_log(hwfc, AV_LOG_ERROR, "Failed to release frame "
               "command queue: %d.\n", cle);
    }
}

static int opencl_get_buffer(AVHWFramesContext *hwfc, AVFrame *frame)
{
    AVOpenCLFrameDescriptor *desc;
    int p;

    frame->buf[0] = av_buffer_pool_get(hwfc->pool);
    if (!frame->buf[0])
        return AVERROR(ENOMEM);

    desc = (AVOpenCLFrameDescriptor*)frame->buf[0]->data;

    for (p = 0; p < desc->nb_planes; p++)
        frame->data[p] = (uint8_t*)desc->planes[p];

    frame->format  = AV_PIX_FMT_OPENCL;
    frame->width   = hwfc->width;
    frame->height  = hwfc->height;

    return 0;
}

static int opencl_transfer_get_formats(AVHWFramesContext *hwfc,
                                       enum AVHWFrameTransferDirection dir,
                                       enum AVPixelFormat **formats)
{
    enum AVPixelFormat *fmts;

    fmts = av_malloc_array(2, sizeof(*fmts));
    if (!fmts)
        return AVERROR(ENOMEM);

    fmts[0] = hwfc->sw_format;
    fmts[1] = AV_PIX_FMT_NONE;

    *formats = fmts;
    return 0;
}

static int opencl_wait_events(AVHWFramesContext *hwfc,
                              cl_event *events, int nb_events)
{
    cl_int cle;
    int i;

    cle = clWaitForEvents(nb_events, events);
    if (cle != CL_SUCCESS) {
        av_log(hwfc, AV_LOG_ERROR, "Failed to wait for event "
               "completion: %d.\n", cle);
        return AVERROR(EIO);
    }

    for (i = 0; i < nb_events; i++) {
        cle = clReleaseEvent(events[i]);
        if (cle != CL_SUCCESS) {
            av_log(hwfc, AV_LOG_ERROR, "Failed to release "
                   "event: %d.\n", cle);
        }
    }

    return 0;
}

static int opencl_transfer_data_from(AVHWFramesContext *hwfc,
                                     AVFrame *dst, const AVFrame *src)
{
    OpenCLFramesContext *priv = hwfc->internal->priv;
    cl_image_format image_format;
    cl_image_desc image_desc;
    cl_int cle;
    size_t origin[3] = { 0, 0, 0 };
    size_t region[3];
    cl_event events[AV_NUM_DATA_POINTERS];
    int err, p;

    if (dst->format != hwfc->sw_format)
        return AVERROR(EINVAL);

    for (p = 0;; p++) {
        err = opencl_get_plane_format(hwfc->sw_format, p,
                                      src->width, src->height,
                                      &image_format, &image_desc);
        if (err < 0) {
            if (err == AVERROR(ENOENT))
                err = 0;
            break;
        }

        if (!dst->data[p]) {
            av_log(hwfc, AV_LOG_ERROR, "Plane %d missing on "
                   "destination frame for transfer.\n", p);
            err = AVERROR(EINVAL);
            break;
        }

        region[0] = image_desc.image_width;
        region[1] = image_desc.image_height;
        region[2] = 1;

        cle = clEnqueueReadImage(priv->command_queue,
                                 (cl_mem)src->data[p],
                                 CL_FALSE, origin, region,
                                 dst->linesize[p], 0,
                                 dst->data[p],
                                 0, NULL, &events[p]);
        if (cle != CL_SUCCESS) {
            av_log(hwfc, AV_LOG_ERROR, "Failed to enqueue read of "
                   "OpenCL image plane %d: %d.\n", p, cle);
            err = AVERROR(EIO);
            break;
        }
    }

    opencl_wait_events(hwfc, events, p);

    return err;
}

static int opencl_transfer_data_to(AVHWFramesContext *hwfc,
                                   AVFrame *dst, const AVFrame *src)
{
    OpenCLFramesContext *priv = hwfc->internal->priv;
    cl_image_format image_format;
    cl_image_desc image_desc;
    cl_int cle;
    size_t origin[3] = { 0, 0, 0 };
    size_t region[3];
    cl_event events[AV_NUM_DATA_POINTERS];
    int err, p;

    if (src->format != hwfc->sw_format)
        return AVERROR(EINVAL);

    for (p = 0;; p++) {
        err = opencl_get_plane_format(hwfc->sw_format, p,
                                      src->width, src->height,
                                      &image_format, &image_desc);
        if (err < 0) {
            if (err == AVERROR(ENOENT))
                err = 0;
            break;
        }

        if (!src->data[p]) {
            av_log(hwfc, AV_LOG_ERROR, "Plane %d missing on "
                   "source frame for transfer.\n", p);
            err = AVERROR(EINVAL);
            break;
        }

        region[0] = image_desc.image_width;
        region[1] = image_desc.image_height;
        region[2] = 1;

        cle = clEnqueueWriteImage(priv->command_queue,
                                  (cl_mem)dst->data[p],
                                  CL_FALSE, origin, region,
                                  src->linesize[p], 0,
                                  src->data[p],
                                  0, NULL, &events[p]);
        if (cle != CL_SUCCESS) {
            av_log(hwfc, AV_LOG_ERROR, "Failed to enqueue write of "
                   "OpenCL image plane %d: %d.\n", p, cle);
            err = AVERROR(EIO);
            break;
        }
    }

    opencl_wait_events(hwfc, events, p);

    return err;
}

typedef struct OpenCLMapping {
    // The mapped addresses for each plane.
    // The destination frame is not available when we unmap, so these
    // need to be stored separately.
    void *address[AV_NUM_DATA_POINTERS];
} OpenCLMapping;

static void opencl_unmap_frame(AVHWFramesContext *hwfc,
                               HWMapDescriptor *hwmap)
{
    OpenCLFramesContext *priv = hwfc->internal->priv;
    OpenCLMapping *map = hwmap->priv;
    cl_event events[AV_NUM_DATA_POINTERS];
    int p, e;
    cl_int cle;

    for (p = e = 0; p < FF_ARRAY_ELEMS(map->address); p++) {
        if (!map->address[p])
            break;

        cle = clEnqueueUnmapMemObject(priv->command_queue,
                                      (cl_mem)hwmap->source->data[p],
                                      map->address[p],
                                      0, NULL, &events[e]);
        if (cle != CL_SUCCESS) {
            av_log(hwfc, AV_LOG_ERROR, "Failed to unmap OpenCL "
                   "image plane %d: %d.\n", p, cle);
        }
        ++e;
    }

    opencl_wait_events(hwfc, events, e);

    av_free(map);
}

static int opencl_map_frame(AVHWFramesContext *hwfc, AVFrame *dst,
                            const AVFrame *src, int flags)
{
    OpenCLFramesContext *priv = hwfc->internal->priv;
    cl_map_flags map_flags;
    cl_image_format image_format;
    cl_image_desc image_desc;
    cl_int cle;
    OpenCLMapping *map;
    size_t origin[3] = { 0, 0, 0 };
    size_t region[3];
    size_t row_pitch;
    cl_event events[AV_NUM_DATA_POINTERS];
    int err, p;

    av_assert0(hwfc->sw_format == dst->format);

    if (flags & AV_HWFRAME_MAP_OVERWRITE &&
        !(flags & AV_HWFRAME_MAP_READ)) {
        // This is mutually exclusive with the read/write flags, so
        // there is no way to map with read here.
        map_flags = CL_MAP_WRITE_INVALIDATE_REGION;
    } else {
        map_flags = 0;
        if (flags & AV_HWFRAME_MAP_READ)
            map_flags |= CL_MAP_READ;
        if (flags & AV_HWFRAME_MAP_WRITE)
            map_flags |= CL_MAP_WRITE;
    }

    map = av_mallocz(sizeof(*map));
    if (!map)
        return AVERROR(ENOMEM);

    for (p = 0;; p++) {
        err = opencl_get_plane_format(hwfc->sw_format, p,
                                      src->width, src->height,
                                      &image_format, &image_desc);
        if (err == AVERROR(ENOENT))
            break;
        if (err < 0)
            goto fail;

        region[0] = image_desc.image_width;
        region[1] = image_desc.image_height;
        region[2] = 1;

        map->address[p] =
            clEnqueueMapImage(priv->command_queue,
                              (cl_mem)src->data[p],
                              CL_FALSE, map_flags, origin, region,
                              &row_pitch, NULL, 0, NULL,
                              &events[p], &cle);
        if (!map->address[p]) {
            av_log(hwfc, AV_LOG_ERROR, "Failed to map OpenCL "
                   "image plane %d: %d.\n", p, cle);
            err = AVERROR(EIO);
            goto fail;
        }

        dst->data[p] = map->address[p];

        av_log(hwfc, AV_LOG_DEBUG, "Map plane %d (%p -> %p).\n",
               p, src->data[p], dst->data[p]);
    }

    err = opencl_wait_events(hwfc, events, p);
    if (err < 0)
        goto fail;

    err = ff_hwframe_map_create(src->hw_frames_ctx, dst, src,
                                &opencl_unmap_frame, map);
    if (err < 0)
        goto fail;

    dst->width  = src->width;
    dst->height = src->height;

    return 0;

fail:
    for (p = 0; p < AV_NUM_DATA_POINTERS; p++) {
        if (!map->address[p])
            break;
        clEnqueueUnmapMemObject(priv->command_queue,
                                (cl_mem)src->data[p],
                                map->address[p],
                                0, NULL, &events[p]);
    }
    if (p > 0)
        opencl_wait_events(hwfc, events, p);
    av_freep(&map);
    return err;
}

static int opencl_map_from(AVHWFramesContext *hwfc, AVFrame *dst,
                           const AVFrame *src, int flags)
{
    av_assert0(src->format == AV_PIX_FMT_OPENCL);
    if (hwfc->sw_format != dst->format)
        return AVERROR(ENOSYS);
    return opencl_map_frame(hwfc, dst, src, flags);
}

const HWContextType ff_hwcontext_type_opencl = {
    .type                   = AV_HWDEVICE_TYPE_OPENCL,
    .name                   = "OpenCL",

    .device_hwctx_size      = sizeof(AVOpenCLDeviceContext),
    .device_priv_size       = sizeof(OpenCLDeviceContext),
    .frames_hwctx_size      = sizeof(AVOpenCLFramesContext),
    .frames_priv_size       = sizeof(OpenCLFramesContext),

    .device_create          = &opencl_device_create,
    .device_init            = &opencl_device_init,
    .device_uninit          = &opencl_device_uninit,

    .frames_get_constraints = &opencl_frames_get_constraints,
    .frames_init            = &opencl_frames_init,
    .frames_uninit          = &opencl_frames_uninit,
    .frames_get_buffer      = &opencl_get_buffer,

    .transfer_get_formats   = &opencl_transfer_get_formats,
    .transfer_data_to       = &opencl_transfer_data_to,
    .transfer_data_from     = &opencl_transfer_data_from,

    .map_from               = &opencl_map_from,

    .pix_fmts = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_OPENCL,
        AV_PIX_FMT_NONE
    },
};
