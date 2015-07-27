/*
 * Vidvox Hap utility functions
 * Copyright (C) 2015 Tom Butterworth <bangnoise@gmail.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Hap utilities
 */
#include "hap.h"

int ff_hap_set_chunk_count(HapContext *ctx, int count, int first_in_frame)
{
    int ret = 0;
    if (first_in_frame == 1 && ctx->chunk_count != count) {
        int ret = av_reallocp_array(&ctx->chunks, count, sizeof(HapChunk));
        if (ret == 0)
            ret = av_reallocp_array(&ctx->chunk_results, count, sizeof(int));
        if (ret < 0) {
            ctx->chunk_count = 0;
        } else {
            ctx->chunk_count = count;
        }
    } else if (ctx->chunk_count != count) {
        /* If this is not the first chunk count calculated for a frame and a
         * different count has already been encountered, then reject the frame:
         * each table in the Decode Instructions Container must describe the
         * same number of chunks. */
        ret = AVERROR_INVALIDDATA;
    }
    return ret;
}

av_cold void ff_hap_free_context(HapContext *ctx)
{
    av_freep(&ctx->tex_buf);
    av_freep(&ctx->chunks);
    av_freep(&ctx->chunk_results);
}
