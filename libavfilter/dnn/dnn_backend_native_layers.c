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

#include <string.h>
#include "dnn_backend_native_layers.h"
#include "dnn_backend_native_layer_pad.h"
#include "dnn_backend_native_layer_conv2d.h"
#include "dnn_backend_native_layer_depth2space.h"
#include "dnn_backend_native_layer_maximum.h"
#include "dnn_backend_native_layer_mathbinary.h"
#include "dnn_backend_native_layer_mathunary.h"
#include "dnn_backend_native_layer_avgpool.h"
#include "dnn_backend_native_layer_dense.h"

const LayerFunc ff_layer_funcs[DLT_COUNT] = {
    {NULL, NULL},
    {ff_dnn_execute_layer_conv2d,      ff_dnn_load_layer_conv2d},
    {ff_dnn_execute_layer_depth2space, ff_dnn_load_layer_depth2space},
    {ff_dnn_execute_layer_pad,         ff_dnn_load_layer_pad},
    {ff_dnn_execute_layer_maximum,     ff_dnn_load_layer_maximum},
    {ff_dnn_execute_layer_math_binary, ff_dnn_load_layer_math_binary},
    {ff_dnn_execute_layer_math_unary,  ff_dnn_load_layer_math_unary},
    {ff_dnn_execute_layer_avg_pool,    ff_dnn_load_layer_avg_pool},
    {ff_dnn_execute_layer_dense,       ff_dnn_load_layer_dense},
};
