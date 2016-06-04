/*
 * SVQ1 encoder
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

#ifndef AVCODEC_SVQ1ENC_H
#define AVCODEC_SVQ1ENC_H

#include <stdint.h>

#include "libavutil/frame.h"

#include "avcodec.h"
#include "hpeldsp.h"
#include "me_cmp.h"
#include "mpegvideo.h"
#include "put_bits.h"

typedef struct SVQ1EncContext {
    /* FIXME: Needed for motion estimation, should not be used for anything
     * else, the idea is to make the motion estimation eventually independent
     * of MpegEncContext, so this will be removed then. */
    MpegEncContext m;
    AVCodecContext *avctx;
    MECmpContext mecc;
    HpelDSPContext hdsp;
    AVFrame *current_picture;
    AVFrame *last_picture;
    PutBitContext pb;

    /* Some compression statistics */
    enum AVPictureType pict_type;
    int quality;

    /* why ooh why this sick breadth first order,
     * everything is slower and more complex */
    PutBitContext reorder_pb[6];

    int frame_width;
    int frame_height;

    /* Y plane block dimensions */
    int y_block_width;
    int y_block_height;

    /* U & V plane (C planes) block dimensions */
    int c_block_width;
    int c_block_height;

    uint16_t *mb_type;
    uint32_t *dummy;
    int16_t (*motion_val8[3])[2];
    int16_t (*motion_val16[3])[2];

    int64_t rd_total;

    uint8_t *scratchbuf;

    int motion_est;

    int (*ssd_int8_vs_int16)(const int8_t *pix1, const int16_t *pix2,
                             int size);
} SVQ1EncContext;

void ff_svq1enc_init_ppc(SVQ1EncContext *c);
void ff_svq1enc_init_x86(SVQ1EncContext *c);

#endif /* AVCODEC_SVQ1ENC_H */
