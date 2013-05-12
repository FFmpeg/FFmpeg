/*
 * Copyright (C) 2013 Wei Gao <weigao@multicorewareinc.com>
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

#ifndef AVFILTER_UNSHARP_OPENCL_H
#define AVFILTER_UNSHARP_OPENCL_H

#include "unsharp.h"

int ff_opencl_unsharp_init(AVFilterContext *ctx);

void ff_opencl_unsharp_uninit(AVFilterContext *ctx);

int ff_opencl_unsharp_process_inout_buf(AVFilterContext *ctx, AVFrame *in, AVFrame *out);

int ff_opencl_apply_unsharp(AVFilterContext *ctx, AVFrame *in, AVFrame *out);

#endif /* AVFILTER_UNSHARP_OPENCL_H */
