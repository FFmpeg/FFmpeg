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

#include <stdint.h>
#include <stdio.h>

#include "libavutil/frame.h"
#include "libavutil/mem.h"
#include "libavutil/video_enc_params.h"

int main(void)
{
    AVVideoEncParams *par;
    AVVideoBlockParams *block;
    AVFrame *frame;
    size_t size;

    static const struct {
        enum AVVideoEncParamsType type;
        const char *name;
    } types[] = {
        { AV_VIDEO_ENC_PARAMS_VP9,   "VP9"   },
        { AV_VIDEO_ENC_PARAMS_H264,  "H264"  },
        { AV_VIDEO_ENC_PARAMS_MPEG2, "MPEG2" },
    };

    /* av_video_enc_params_alloc - each type with blocks */
    printf("Testing av_video_enc_params_alloc()\n");
    for (int i = 0; i < 3; i++) {
        par = av_video_enc_params_alloc(types[i].type, 4, &size);
        if (par) {
            printf("%s: OK, type=%d, nb_blocks=%u, size>0=%s\n",
                   types[i].name, par->type, par->nb_blocks,
                   size > 0 ? "yes" : "no");
            av_free(par);
        } else {
            printf("%s: FAIL\n", types[i].name);
        }
    }

    /* zero blocks */
    par = av_video_enc_params_alloc(AV_VIDEO_ENC_PARAMS_VP9, 0, &size);
    if (par) {
        printf("zero blocks: OK, nb_blocks=%u\n", par->nb_blocks);
        av_free(par);
    } else {
        printf("zero blocks: FAIL\n");
    }

    /* alloc without size */
    par = av_video_enc_params_alloc(AV_VIDEO_ENC_PARAMS_H264, 1, NULL);
    printf("alloc (no size): %s\n", par ? "OK" : "FAIL");
    av_free(par);

    /* av_video_enc_params_block - write and read back */
    printf("\nTesting av_video_enc_params_block()\n");
    par = av_video_enc_params_alloc(AV_VIDEO_ENC_PARAMS_H264, 3, NULL);
    if (par) {
        par->qp = 26;
        par->delta_qp[0][0] = -2;
        par->delta_qp[0][1] = -1;
        printf("frame qp=%d, delta_qp[0]={%d,%d}\n",
               par->qp, par->delta_qp[0][0], par->delta_qp[0][1]);

        for (int i = 0; i < 3; i++) {
            block = av_video_enc_params_block(par, i);
            if ((uint8_t *)block != (uint8_t *)par + par->blocks_offset +
                                    (size_t)i * par->block_size)
                printf("block %d: pointer inconsistent with blocks_offset/block_size\n", i);
            block->src_x = i * 16;
            block->src_y = i * 16;
            block->w = 16;
            block->h = 16;
            block->delta_qp = i - 1;
        }
        for (int i = 0; i < 3; i++) {
            block = av_video_enc_params_block(par, i);
            printf("block %d: src=(%d,%d) size=%dx%d delta_qp=%d\n",
                   i, block->src_x, block->src_y,
                   block->w, block->h, block->delta_qp);
        }
        av_free(par);
    }

    /* av_video_enc_params_create_side_data */
    printf("\nTesting av_video_enc_params_create_side_data()\n");
    frame = av_frame_alloc();
    if (frame) {
        par = av_video_enc_params_create_side_data(frame,
                  AV_VIDEO_ENC_PARAMS_VP9, 2);
        if (par) {
            printf("side_data: OK, type=%d, nb_blocks=%u\n",
                   par->type, par->nb_blocks);
            block = av_video_enc_params_block(par, 0);
            block->delta_qp = 5;
            block = av_video_enc_params_block(par, 0);
            printf("side_data block 0: delta_qp=%d\n", block->delta_qp);
        } else {
            printf("side_data: FAIL\n");
        }
        av_frame_free(&frame);
    }

    /* NONE type */
    printf("\nTesting NONE type\n");
    par = av_video_enc_params_alloc(AV_VIDEO_ENC_PARAMS_NONE, 0, &size);
    if (par) {
        printf("NONE: OK, type=%d\n", par->type);
        av_free(par);
    } else {
        printf("NONE: FAIL\n");
    }

    return 0;
}
