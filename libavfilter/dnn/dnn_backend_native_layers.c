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

LayerFunc layer_funcs[DLT_COUNT] = {
    {NULL, NULL},
    {dnn_execute_layer_conv2d,      dnn_load_layer_conv2d},
    {dnn_execute_layer_depth2space, dnn_load_layer_depth2space},
    {dnn_execute_layer_pad,         dnn_load_layer_pad},
    {dnn_execute_layer_maximum,     dnn_load_layer_maximum},
    {dnn_execute_layer_math_binary, dnn_load_layer_math_binary},
    {dnn_execute_layer_math_unary,  dnn_load_layer_math_unary},
};
