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
#include "libavutil/mem.h"

extern const DNNModule ff_dnn_backend_openvino;
extern const DNNModule ff_dnn_backend_tf;

const DNNModule *ff_get_dnn_module(DNNBackendType backend_type, void *log_ctx)
{
    switch(backend_type){
    #if (CONFIG_LIBTENSORFLOW == 1)
    case DNN_TF:
        return &ff_dnn_backend_tf;
    #endif
    #if (CONFIG_LIBOPENVINO == 1)
    case DNN_OV:
        return &ff_dnn_backend_openvino;
    #endif
    default:
        av_log(log_ctx, AV_LOG_ERROR,
                "Module backend_type %d is not supported or enabled.\n",
                backend_type);
        return NULL;
    }
}
