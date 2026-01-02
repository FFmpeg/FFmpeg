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

#ifndef AVUTIL_ZLIB_UTILS_H
#define AVUTIL_ZLIB_UTILS_H

#include <stdint.h>
#include "log.h"
#include "error.h"
#include "mem.h"

#include <zlib.h>
#define CHUNK_SIZE 1024 * 64

static inline int ff_zlib_expand(void *ctx, uint8_t **out, size_t *out_len,
                                 const uint8_t *src, int src_len)
{
    int ret;

    z_stream stream = { 0 };
    if (inflateInit2(&stream, 32 + 15) != Z_OK) {
        av_log(ctx, AV_LOG_ERROR, "Error during zlib initialisation: %s\n",
               stream.msg);
        return AVERROR(ENOSYS);
    }

    uint64_t buf_size = CHUNK_SIZE * 4;
    uint8_t *buf = av_realloc(NULL, buf_size);
    if (!buf) {
        inflateEnd(&stream);
        return AVERROR(ENOMEM);
    }

    stream.next_in = src;
    stream.avail_in = src_len;

    do {
        stream.avail_out = buf_size - stream.total_out;
        stream.next_out = buf + stream.total_out;

        ret = inflate(&stream, Z_FINISH);
        if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
            av_log(ctx, AV_LOG_ERROR, "zlib inflate error(%d): %s\n",
                   ret, stream.msg);
            inflateEnd(&stream);
            av_free(buf);
            return AVERROR(EINVAL);
        }

        if (stream.avail_out == 0) {
            buf_size += CHUNK_SIZE;
            uint8_t *tmp = av_realloc(buf, buf_size);
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

    *out = buf;
    *out_len = stream.total_out;

    return 0;
}
#endif /* AVUTIL_ZLIB_UTILS_H */
