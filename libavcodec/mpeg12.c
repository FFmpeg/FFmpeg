/*
 * MPEG-1/2 decoder
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file
 * MPEG-1/2 decoder
 */

#define UNCHECKED_BITSTREAM_READER 1

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/timecode.h"

#include "internal.h"
#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"
#include "error_resilience.h"
#include "mpeg12.h"
#include "mpeg12data.h"
#include "mpeg12decdata.h"
#include "bytestream.h"
#include "vdpau_internal.h"
#include "xvmc_internal.h"
#include "thread.h"

uint8_t ff_mpeg12_static_rl_table_store[2][2][2*MAX_RUN + MAX_LEVEL + 3];

#define INIT_2D_VLC_RL(rl, static_size)\
{\
    static RL_VLC_ELEM rl_vlc_table[static_size];\
    INIT_VLC_STATIC(&rl.vlc, TEX_VLC_BITS, rl.n + 2,\
                    &rl.table_vlc[0][1], 4, 2,\
                    &rl.table_vlc[0][0], 4, 2, static_size);\
\
    rl.rl_vlc[0] = rl_vlc_table;\
    init_2d_vlc_rl(&rl);\
}

static av_cold void init_2d_vlc_rl(RLTable *rl)
{
    int i;

    for (i = 0; i < rl->vlc.table_size; i++) {
        int code = rl->vlc.table[i][0];
        int len  = rl->vlc.table[i][1];
        int level, run;

        if (len == 0) { // illegal code
            run   = 65;
            level = MAX_LEVEL;
        } else if (len<0) { //more bits needed
            run   = 0;
            level = code;
        } else {
            if (code == rl->n) { //esc
                run   = 65;
                level = 0;
            } else if (code == rl->n+1) { //eob
                run   = 0;
                level = 127;
            } else {
                run   = rl->table_run  [code] + 1;
                level = rl->table_level[code];
            }
        }
        rl->rl_vlc[0][i].len   = len;
        rl->rl_vlc[0][i].level = level;
        rl->rl_vlc[0][i].run   = run;
    }
}

av_cold void ff_mpeg12_common_init(MpegEncContext *s)
{

    s->y_dc_scale_table =
    s->c_dc_scale_table = ff_mpeg2_dc_scale_table[s->intra_dc_precision];

}

void ff_mpeg1_clean_buffers(MpegEncContext *s)
{
    s->last_dc[0] = 1 << (7 + s->intra_dc_precision);
    s->last_dc[1] = s->last_dc[0];
    s->last_dc[2] = s->last_dc[0];
    memset(s->last_mv, 0, sizeof(s->last_mv));
}


/******************************************/
/* decoding */

VLC ff_mv_vlc;

VLC ff_dc_lum_vlc;
VLC ff_dc_chroma_vlc;

VLC ff_mbincr_vlc;
VLC ff_mb_ptype_vlc;
VLC ff_mb_btype_vlc;
VLC ff_mb_pat_vlc;

av_cold void ff_mpeg12_init_vlcs(void)
{
    static int done = 0;

    if (!done) {
        done = 1;

        INIT_VLC_STATIC(&ff_dc_lum_vlc, DC_VLC_BITS, 12,
                        ff_mpeg12_vlc_dc_lum_bits, 1, 1,
                        ff_mpeg12_vlc_dc_lum_code, 2, 2, 512);
        INIT_VLC_STATIC(&ff_dc_chroma_vlc,  DC_VLC_BITS, 12,
                        ff_mpeg12_vlc_dc_chroma_bits, 1, 1,
                        ff_mpeg12_vlc_dc_chroma_code, 2, 2, 514);
        INIT_VLC_STATIC(&ff_mv_vlc, MV_VLC_BITS, 17,
                        &ff_mpeg12_mbMotionVectorTable[0][1], 2, 1,
                        &ff_mpeg12_mbMotionVectorTable[0][0], 2, 1, 518);
        INIT_VLC_STATIC(&ff_mbincr_vlc, MBINCR_VLC_BITS, 36,
                        &ff_mpeg12_mbAddrIncrTable[0][1], 2, 1,
                        &ff_mpeg12_mbAddrIncrTable[0][0], 2, 1, 538);
        INIT_VLC_STATIC(&ff_mb_pat_vlc, MB_PAT_VLC_BITS, 64,
                        &ff_mpeg12_mbPatTable[0][1], 2, 1,
                        &ff_mpeg12_mbPatTable[0][0], 2, 1, 512);

        INIT_VLC_STATIC(&ff_mb_ptype_vlc, MB_PTYPE_VLC_BITS, 7,
                        &table_mb_ptype[0][1], 2, 1,
                        &table_mb_ptype[0][0], 2, 1, 64);
        INIT_VLC_STATIC(&ff_mb_btype_vlc, MB_BTYPE_VLC_BITS, 11,
                        &table_mb_btype[0][1], 2, 1,
                        &table_mb_btype[0][0], 2, 1, 64);
        ff_init_rl(&ff_rl_mpeg1, ff_mpeg12_static_rl_table_store[0]);
        ff_init_rl(&ff_rl_mpeg2, ff_mpeg12_static_rl_table_store[1]);

        INIT_2D_VLC_RL(ff_rl_mpeg1, 680);
        INIT_2D_VLC_RL(ff_rl_mpeg2, 674);
    }
}

/**
 * Find the end of the current frame in the bitstream.
 * @return the position of the first byte of the next frame, or -1
 */
int ff_mpeg1_find_frame_end(ParseContext *pc, const uint8_t *buf, int buf_size, AVCodecParserContext *s)
{
    int i;
    uint32_t state = pc->state;

    /* EOF considered as end of frame */
    if (buf_size == 0)
        return 0;

/*
 0  frame start         -> 1/4
 1  first_SEQEXT        -> 0/2
 2  first field start   -> 3/0
 3  second_SEQEXT       -> 2/0
 4  searching end
*/

    for (i = 0; i < buf_size; i++) {
        av_assert1(pc->frame_start_found >= 0 && pc->frame_start_found <= 4);
        if (pc->frame_start_found & 1) {
            if (state == EXT_START_CODE && (buf[i] & 0xF0) != 0x80)
                pc->frame_start_found--;
            else if (state == EXT_START_CODE + 2) {
                if ((buf[i] & 3) == 3)
                    pc->frame_start_found = 0;
                else
                    pc->frame_start_found = (pc->frame_start_found + 1) & 3;
            }
            state++;
        } else {
            i = avpriv_find_start_code(buf + i, buf + buf_size, &state) - buf - 1;
            if (pc->frame_start_found == 0 && state >= SLICE_MIN_START_CODE && state <= SLICE_MAX_START_CODE) {
                i++;
                pc->frame_start_found = 4;
            }
            if (state == SEQ_END_CODE) {
                pc->frame_start_found = 0;
                pc->state=-1;
                return i+1;
            }
            if (pc->frame_start_found == 2 && state == SEQ_START_CODE)
                pc->frame_start_found = 0;
            if (pc->frame_start_found  < 4 && state == EXT_START_CODE)
                pc->frame_start_found++;
            if (pc->frame_start_found == 4 && (state & 0xFFFFFF00) == 0x100) {
                if (state < SLICE_MIN_START_CODE || state > SLICE_MAX_START_CODE) {
                    pc->frame_start_found = 0;
                    pc->state             = -1;
                    return i - 3;
                }
            }
            if (pc->frame_start_found == 0 && s && state == PICTURE_START_CODE) {
                ff_fetch_timestamp(s, i - 3, 1);
            }
        }
    }
    pc->state = state;
    return END_NOT_FOUND;
}

