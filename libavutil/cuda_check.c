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

#include "compat/cuda/dynlink_loader.h"
#include "libavutil/cuda_check.h"

int ff_cuda_check(void *avctx,
                  void *cuGetErrorName_fn,
                  void *cuGetErrorString_fn,
                  CUresult err, const char *func)
{
    const char *err_name;
    const char *err_string;

    av_log(avctx, AV_LOG_TRACE, "Calling %s\n", func);

    if (err == CUDA_SUCCESS)
        return 0;

    ((tcuGetErrorName *)cuGetErrorName_fn)(err, &err_name);
    ((tcuGetErrorString *)cuGetErrorString_fn)(err, &err_string);

    av_log(avctx, AV_LOG_ERROR, "%s failed", func);
    if (err_name && err_string)
        av_log(avctx, AV_LOG_ERROR, " -> %s: %s", err_name, err_string);
    av_log(avctx, AV_LOG_ERROR, "\n");

    return AVERROR_EXTERNAL;
}

