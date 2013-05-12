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

#include "opencl_allkernels.h"
#if CONFIG_OPENCL
#include "libavutil/opencl.h"
#include "deshake_opencl_kernel.h"
#include "unsharp_opencl_kernel.h"
#endif

#define OPENCL_REGISTER_KERNEL_CODE(X, x)                                              \
    {                                                                                  \
        if (CONFIG_##X##_FILTER) {                                                     \
            av_opencl_register_kernel_code(ff_kernel_##x##_opencl);                    \
        }                                                                              \
    }

void ff_opencl_register_filter_kernel_code_all(void)
{
 #if CONFIG_OPENCL
   OPENCL_REGISTER_KERNEL_CODE(DESHAKE,     deshake);
   OPENCL_REGISTER_KERNEL_CODE(UNSHARP,     unsharp);
 #endif
}
