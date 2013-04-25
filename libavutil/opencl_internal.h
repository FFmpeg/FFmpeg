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

#define FF_OPENCL_PARAM_INFO(a) ((void*)(&(a))), (sizeof(a))

typedef struct {
    cl_kernel kernel;
    int param_num;
    void *ctx;
} FFOpenclParam;

int ff_opencl_set_parameter(FFOpenclParam *opencl_param, ...);
