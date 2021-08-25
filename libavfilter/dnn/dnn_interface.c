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
#include "dnn_backend_native.h"
#include "dnn_backend_tf.h"
#include "dnn_backend_openvino.h"
#include "libavutil/mem.h"

DNNModule *ff_get_dnn_module(DNNBackendType backend_type)
{
    DNNModule *dnn_module;

    dnn_module = av_mallocz(sizeof(DNNModule));
    if(!dnn_module){
        return NULL;
    }

    switch(backend_type){
    case DNN_NATIVE:
        dnn_module->load_model = &ff_dnn_load_model_native;
        dnn_module->execute_model = &ff_dnn_execute_model_native;
        dnn_module->get_result = &ff_dnn_get_result_native;
        dnn_module->flush = &ff_dnn_flush_native;
        dnn_module->free_model = &ff_dnn_free_model_native;
        break;
    case DNN_TF:
    #if (CONFIG_LIBTENSORFLOW == 1)
        dnn_module->load_model = &ff_dnn_load_model_tf;
        dnn_module->execute_model = &ff_dnn_execute_model_tf;
        dnn_module->get_result = &ff_dnn_get_result_tf;
        dnn_module->flush = &ff_dnn_flush_tf;
        dnn_module->free_model = &ff_dnn_free_model_tf;
    #else
        av_freep(&dnn_module);
        return NULL;
    #endif
        break;
    case DNN_OV:
    #if (CONFIG_LIBOPENVINO == 1)
        dnn_module->load_model = &ff_dnn_load_model_ov;
        dnn_module->execute_model = &ff_dnn_execute_model_ov;
        dnn_module->get_result = &ff_dnn_get_result_ov;
        dnn_module->flush = &ff_dnn_flush_ov;
        dnn_module->free_model = &ff_dnn_free_model_ov;
    #else
        av_freep(&dnn_module);
        return NULL;
    #endif
        break;
    default:
        av_log(NULL, AV_LOG_ERROR, "Module backend_type is not native or tensorflow\n");
        av_freep(&dnn_module);
        return NULL;
    }

    return dnn_module;
}
