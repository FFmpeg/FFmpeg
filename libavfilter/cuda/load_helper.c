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

#include "config.h"

#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda_internal.h"
#include "libavutil/cuda_check.h"
#include "libavutil/mem.h"

#if CONFIG_SHADER_COMPRESSION
#include "libavutil/zlib_utils.h"
#endif

#include "load_helper.h"

#define CHECK_CU(x) FF_CUDA_CHECK_DL(avctx, cu, x)

int ff_cuda_load_module(void *avctx, AVCUDADeviceContext *hwctx, CUmodule *cu_module,
                        const unsigned char *data, const unsigned int length)
{
    CudaFunctions *cu = hwctx->internal->cuda_dl;

#if CONFIG_SHADER_COMPRESSION
    uint8_t *out;
    size_t out_len;
    int ret = ff_zlib_expand(avctx, &out, &out_len,
                             data, length);
    if (ret < 0)
        return ret;

    ret = CHECK_CU(cu->cuModuleLoadData(cu_module, out));
    av_free(out);
    return ret;
#else
    return CHECK_CU(cu->cuModuleLoadData(cu_module, data));
#endif
}
