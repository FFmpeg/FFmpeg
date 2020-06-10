/*
 * Copyright (c) 2019 Guo Yejun
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

#ifndef AVFILTER_DNN_DNN_BACKEND_NATIVE_LAYERS_H
#define AVFILTER_DNN_DNN_BACKEND_NATIVE_LAYERS_H

#include <stdint.h>
#include "dnn_backend_native.h"

typedef int (*LAYER_EXEC_FUNC)(DnnOperand *operands, const int32_t *input_operand_indexes,
                               int32_t output_operand_index, const void *parameters);
typedef int (*LAYER_LOAD_FUNC)(Layer *layer, AVIOContext *model_file_context, int file_size, int operands_num);

typedef struct LayerFunc {
    LAYER_EXEC_FUNC pf_exec;
    LAYER_LOAD_FUNC pf_load;
}LayerFunc;

extern LayerFunc layer_funcs[DLT_COUNT];

#endif
