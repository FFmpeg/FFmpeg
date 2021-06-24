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

#if CONFIG_PTX_COMPRESSION
#include <zlib.h>
#define CHUNK_SIZE 1024 * 64
#endif

#include "load_helper.h"

#define CHECK_CU(x) FF_CUDA_CHECK_DL(avctx, cu, x)

int ff_cuda_load_module(void *avctx, AVCUDADeviceContext *hwctx, CUmodule *cu_module,
                        const unsigned char *data, const unsigned int length)
{
    CudaFunctions *cu = hwctx->internal->cuda_dl;

#if CONFIG_PTX_COMPRESSION
    z_stream stream = { 0 };
    uint8_t *buf, *tmp;
    uint64_t buf_size;
    int ret;

    if (inflateInit2(&stream, 32 + 15) != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Error during zlib initialisation: %s\n", stream.msg);
        return AVERROR(ENOSYS);
    }

    buf_size = CHUNK_SIZE * 4;
    buf = av_realloc(NULL, buf_size);
    if (!buf) {
        inflateEnd(&stream);
        return AVERROR(ENOMEM);
    }

    stream.next_in = data;
    stream.avail_in = length;

    do {
        stream.avail_out = buf_size - stream.total_out;
        stream.next_out = buf + stream.total_out;

        ret = inflate(&stream, Z_FINISH);
        if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
            av_log(avctx, AV_LOG_ERROR, "zlib inflate error(%d): %s\n", ret, stream.msg);
            inflateEnd(&stream);
            av_free(buf);
            return AVERROR(EINVAL);
        }

        if (stream.avail_out == 0) {
            buf_size += CHUNK_SIZE;
            tmp = av_realloc(buf, buf_size);
            if (!tmp) {
                inflateEnd(&stream);
                av_free(buf);
                return AVERROR(ENOMEM);
            }
            buf = tmp;
        }
    } while (ret != Z_STREAM_END);

    // NULL-terminate string
    // there is guaranteed to be space for this, due to condition in loop
    buf[stream.total_out] = 0;

    inflateEnd(&stream);

    ret = CHECK_CU(cu->cuModuleLoadData(cu_module, buf));
    av_free(buf);
    return ret;
#else
    return CHECK_CU(cu->cuModuleLoadData(cu_module, data));
#endif
}
