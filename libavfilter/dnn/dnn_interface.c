/*
 * Copyright (c) 2018 Sergey Lavrushkin
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
 * Implements DNN module initialization with specified backend.
 */

#include "../dnn_interface.h"
#include "libavutil/avassert.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "libavfilter/filters.h"

extern const DNNModule ff_dnn_backend_openvino;
extern const DNNModule ff_dnn_backend_tf;
extern const DNNModule ff_dnn_backend_torch;

#define OFFSET(x) offsetof(DnnContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM
static const AVOption dnn_base_options[] = {
        {"model", "path to model file",
                OFFSET(model_filename), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS},
        {"input", "input name of the model",
                OFFSET(model_inputname), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS},
        {"output", "output name of the model",
                OFFSET(model_outputnames_string), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS},
        {"backend_configs", "backend configs (deprecated)",
                OFFSET(backend_options), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS | AV_OPT_FLAG_DEPRECATED},
        {"options", "backend configs (deprecated)",
                OFFSET(backend_options), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS | AV_OPT_FLAG_DEPRECATED},
        {"nireq", "number of request",
                OFFSET(nireq), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, FLAGS},
        {"async", "use DNN async inference",
                OFFSET(async), AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, FLAGS},
        {"device", "device to run model",
                OFFSET(device), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS},
        {NULL}
};

AVFILTER_DEFINE_CLASS(dnn_base);

typedef struct DnnBackendInfo {
    const size_t offset;
    union {
        const AVClass *class;
        const DNNModule *module;
    };
} DnnBackendInfo;

static const DnnBackendInfo dnn_backend_info_list[] = {
        {0, .class = &dnn_base_class},
        // Must keep the same order as in DNNOptions, so offset value in incremental order
#if CONFIG_LIBTENSORFLOW
        {offsetof(DnnContext, tf_option), .module = &ff_dnn_backend_tf},
#endif
#if CONFIG_LIBOPENVINO
        {offsetof(DnnContext, ov_option), .module = &ff_dnn_backend_openvino},
#endif
#if CONFIG_LIBTORCH
        {offsetof(DnnContext, torch_option), .module = &ff_dnn_backend_torch},
#endif
};

const DNNModule *ff_get_dnn_module(DNNBackendType backend_type, void *log_ctx)
{
    for (int i = 1; i < FF_ARRAY_ELEMS(dnn_backend_info_list); i++) {
        if (dnn_backend_info_list[i].module->type == backend_type)
            return dnn_backend_info_list[i].module;
    }

    av_log(log_ctx, AV_LOG_ERROR,
            "Module backend_type %d is not supported or enabled.\n",
            backend_type);
    return NULL;
}

void ff_dnn_init_child_class(DnnContext *ctx)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(dnn_backend_info_list); i++) {
        const AVClass **ptr = (const AVClass **) ((char *) ctx + dnn_backend_info_list[i].offset);
        *ptr = dnn_backend_info_list[i].class;
    }
}

void *ff_dnn_child_next(DnnContext *obj, void *prev) {
    size_t pre_offset;

    if (!prev) {
        av_assert0(obj->clazz);
        return obj;
    }

    pre_offset = (char *)prev - (char *)obj;
    for (int i = 0; i < FF_ARRAY_ELEMS(dnn_backend_info_list) - 1; i++) {
        if (dnn_backend_info_list[i].offset == pre_offset) {
            const AVClass **ptr = (const AVClass **) ((char *) obj + dnn_backend_info_list[i + 1].offset);
            av_assert0(*ptr);
            return ptr;
        }
    }

    return NULL;
}

const AVClass *ff_dnn_child_class_iterate_with_mask(void **iter, uint32_t backend_mask)
{
    for (uintptr_t i = (uintptr_t)*iter; i < FF_ARRAY_ELEMS(dnn_backend_info_list); i++) {
        if (i > 0) {
            const DNNModule *module = dnn_backend_info_list[i].module;

            if (!(module->type & backend_mask))
                continue;
        }

        *iter = (void *)(i + 1);
        return dnn_backend_info_list[i].class;
    }

    return NULL;
}

