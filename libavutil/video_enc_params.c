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

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "buffer.h"
#include "common.h"
#include "frame.h"
#include "mem.h"
#include "video_enc_params.h"

AVVideoEncParams *av_video_enc_params_alloc(enum AVVideoEncParamsType type,
                                            unsigned int nb_blocks, size_t *out_size)
{
    AVVideoEncParams *par;
    size_t size;

    size = sizeof(*par);
    if (nb_blocks > (SIZE_MAX - size) / sizeof(AVVideoBlockParams))
        return NULL;
    size += sizeof(AVVideoBlockParams) * nb_blocks;

    par = av_mallocz(size);
    if (!par)
        return NULL;

    par->type          = type;
    par->nb_blocks     = nb_blocks;
    par->block_size    = sizeof(AVVideoBlockParams);
    par->blocks_offset = sizeof(*par);

    if (out_size)
        *out_size = size;

    return par;
}

AVVideoEncParams*
av_video_enc_params_create_side_data(AVFrame *frame, enum AVVideoEncParamsType type,
                                     unsigned int nb_blocks)
{
    AVBufferRef      *buf;
    AVVideoEncParams *par;
    size_t size;

    par = av_video_enc_params_alloc(type, nb_blocks, &size);
    if (!par)
        return NULL;
    if (size > INT_MAX) {
        av_free(par);
        return NULL;
    }
    buf = av_buffer_create((uint8_t *)par, size, NULL, NULL, 0);
    if (!buf) {
        av_freep(&par);
        return NULL;
    }

    if (!av_frame_new_side_data_from_buf(frame, AV_FRAME_DATA_VIDEO_ENC_PARAMS, buf)) {
        av_buffer_unref(&buf);
        return NULL;
    }

    return par;
}
