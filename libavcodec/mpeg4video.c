/*
 * MPEG4 decoder / encoder common code.
 * Copyright (c) 2000,2001 Fabrice Bellard
 * Copyright (c) 2002-2010 Michael Niedermayer <michaelni@gmx.at>
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

#include "mpegvideo.h"
#include "mpeg4video.h"
#include "mpeg4data.h"

uint8_t ff_mpeg4_static_rl_table_store[3][2][2*MAX_RUN + MAX_LEVEL + 3];

int ff_mpeg4_get_video_packet_prefix_length(MpegEncContext *s){
    switch(s->pict_type){
        case FF_I_TYPE:
            return 16;
        case FF_P_TYPE:
        case FF_S_TYPE:
            return s->f_code+15;
        case FF_B_TYPE:
            return FFMAX3(s->f_code, s->b_code, 2) + 15;
        default:
            return -1;
    }
}

void ff_mpeg4_clean_buffers(MpegEncContext *s)
{
    int c_wrap, c_xy, l_wrap, l_xy;

    l_wrap= s->b8_stride;
    l_xy= (2*s->mb_y-1)*l_wrap + s->mb_x*2 - 1;
    c_wrap= s->mb_stride;
    c_xy= (s->mb_y-1)*c_wrap + s->mb_x - 1;

#if 0
    /* clean DC */
    memsetw(s->dc_val[0] + l_xy, 1024, l_wrap*2+1);
    memsetw(s->dc_val[1] + c_xy, 1024, c_wrap+1);
    memsetw(s->dc_val[2] + c_xy, 1024, c_wrap+1);
#endif

    /* clean AC */
    memset(s->ac_val[0] + l_xy, 0, (l_wrap*2+1)*16*sizeof(int16_t));
    memset(s->ac_val[1] + c_xy, 0, (c_wrap  +1)*16*sizeof(int16_t));
    memset(s->ac_val[2] + c_xy, 0, (c_wrap  +1)*16*sizeof(int16_t));

    /* clean MV */
    // we can't clear the MVs as they might be needed by a b frame
//    memset(s->motion_val + l_xy, 0, (l_wrap*2+1)*2*sizeof(int16_t));
//    memset(s->motion_val, 0, 2*sizeof(int16_t)*(2 + s->mb_width*2)*(2 + s->mb_height*2));
    s->last_mv[0][0][0]=
    s->last_mv[0][0][1]=
    s->last_mv[1][0][0]=
    s->last_mv[1][0][1]= 0;
}
