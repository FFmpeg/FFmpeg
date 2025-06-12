/*
 * H.263 internal header
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
#ifndef AVCODEC_H263_H
#define AVCODEC_H263_H

#include "libavutil/rational.h"
#include "mpegvideo.h"

#define FF_ASPECT_EXTENDED 15

#define H263_GOB_HEIGHT(h) ((h) <= 400 ? 1 : (h) <= 800 ? 2 : 4)

static inline int ff_h263_round_chroma(int x)
{
    //FIXME static or not?
    static const uint8_t h263_chroma_roundtab[16] = {
    //  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
        0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1,
    };
    return h263_chroma_roundtab[x & 0xf] + (x >> 3);
}

av_const int ff_h263_aspect_to_info(AVRational aspect);
int16_t *ff_h263_pred_motion(MpegEncContext * s, int block, int dir,
                             int *px, int *py);
void ff_h263_init_rl_inter(void);
void ff_h263_update_motion_val(MpegEncContext * s);
void ff_h263_loop_filter(MpegEncContext * s);

static inline void ff_h263_clean_intra_table_entries(MpegEncContext *s, int xy)
{
    if (s->mbintra_table[xy]) {
        s->mbintra_table[xy] = 0;
        ff_clean_intra_table_entries(s);
    }
}

#endif /* AVCODEC_H263_H */
