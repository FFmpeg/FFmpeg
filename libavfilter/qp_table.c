/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdint.h>

#include "libavutil/frame.h"
#include "libavutil/mem.h"
#include "libavutil/video_enc_params.h"

#include "qp_table.h"

int ff_qp_table_extract(AVFrame *frame, int8_t **table, int *table_w, int *table_h,
                        enum AVVideoEncParamsType *qscale_type)
{
    AVFrameSideData *sd;
    AVVideoEncParams *par;
    unsigned int mb_h = (frame->height + 15) / 16;
    unsigned int mb_w = (frame->width + 15) / 16;
    unsigned int nb_mb = mb_h * mb_w;
    unsigned int block_idx;

    *table = NULL;

    sd = av_frame_get_side_data(frame, AV_FRAME_DATA_VIDEO_ENC_PARAMS);
    if (!sd)
        return 0;
    par = (AVVideoEncParams *)sd->data;
    if ((par->type != AV_VIDEO_ENC_PARAMS_MPEG2 &&
         par->type != AV_VIDEO_ENC_PARAMS_H264) ||
        (par->nb_blocks != 0 && par->nb_blocks != nb_mb))
        return AVERROR(ENOSYS);

    *table = av_malloc(nb_mb);
    if (!*table)
        return AVERROR(ENOMEM);
    if (table_w)
        *table_w = mb_w;
    if (table_h)
        *table_h = mb_h;
    if (qscale_type)
        *qscale_type = par->type;

    if (par->nb_blocks == 0) {
        memset(*table, par->qp, nb_mb);
        return 0;
    }

    for (block_idx = 0; block_idx < nb_mb; block_idx++) {
        AVVideoBlockParams *b = av_video_enc_params_block(par, block_idx);
        (*table)[block_idx] = par->qp + b->delta_qp;
    }

    return 0;
}

