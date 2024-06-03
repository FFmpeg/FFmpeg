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
#include "libavutil/thread.h"

#include "mpegvideo.h"
#include "mpeg12codecs.h"
#include "mpeg12data.h"
#include "mpeg12dec.h"
#include "mpegutils.h"
#include "rl.h"

static const uint8_t table_mb_ptype[7][2] = {
    { 3, 5 }, // 0x01 MB_INTRA
    { 1, 2 }, // 0x02 MB_PAT
    { 1, 3 }, // 0x08 MB_FOR
    { 1, 1 }, // 0x0A MB_FOR|MB_PAT
    { 1, 6 }, // 0x11 MB_QUANT|MB_INTRA
    { 1, 5 }, // 0x12 MB_QUANT|MB_PAT
    { 2, 5 }, // 0x1A MB_QUANT|MB_FOR|MB_PAT
};

static const uint8_t table_mb_btype[11][2] = {
    { 3, 5 }, // 0x01 MB_INTRA
    { 2, 3 }, // 0x04 MB_BACK
    { 3, 3 }, // 0x06 MB_BACK|MB_PAT
    { 2, 4 }, // 0x08 MB_FOR
    { 3, 4 }, // 0x0A MB_FOR|MB_PAT
    { 2, 2 }, // 0x0C MB_FOR|MB_BACK
    { 3, 2 }, // 0x0E MB_FOR|MB_BACK|MB_PAT
    { 1, 6 }, // 0x11 MB_QUANT|MB_INTRA
    { 2, 6 }, // 0x16 MB_QUANT|MB_BACK|MB_PAT
    { 3, 6 }, // 0x1A MB_QUANT|MB_FOR|MB_PAT
    { 2, 5 }, // 0x1E MB_QUANT|MB_FOR|MB_BACK|MB_PAT
};

static const int16_t ptype2mb_type[7] = {
                    MB_TYPE_INTRA,
                    MB_TYPE_FORWARD_MV | MB_TYPE_CBP | MB_TYPE_ZERO_MV | MB_TYPE_16x16,
                    MB_TYPE_FORWARD_MV,
                    MB_TYPE_FORWARD_MV | MB_TYPE_CBP,
    MB_TYPE_QUANT | MB_TYPE_INTRA,
    MB_TYPE_QUANT | MB_TYPE_FORWARD_MV | MB_TYPE_CBP | MB_TYPE_ZERO_MV | MB_TYPE_16x16,
    MB_TYPE_QUANT | MB_TYPE_FORWARD_MV | MB_TYPE_CBP,
};

static const int16_t btype2mb_type[11] = {
                    MB_TYPE_INTRA,
                    MB_TYPE_BACKWARD_MV,
                    MB_TYPE_BACKWARD_MV | MB_TYPE_CBP,
                    MB_TYPE_FORWARD_MV,
                    MB_TYPE_FORWARD_MV  | MB_TYPE_CBP,
                    MB_TYPE_BIDIR_MV,
                    MB_TYPE_BIDIR_MV    | MB_TYPE_CBP,
    MB_TYPE_QUANT | MB_TYPE_INTRA,
    MB_TYPE_QUANT | MB_TYPE_BACKWARD_MV | MB_TYPE_CBP,
    MB_TYPE_QUANT | MB_TYPE_FORWARD_MV  | MB_TYPE_CBP,
    MB_TYPE_QUANT | MB_TYPE_BIDIR_MV    | MB_TYPE_CBP,
};

av_cold void ff_init_2d_vlc_rl(const uint16_t table_vlc[][2], RL_VLC_ELEM rl_vlc[],
                               const int8_t table_run[], const uint8_t table_level[],
                               int n, unsigned static_size, int flags)
{
    int i;
    VLCElem table[680] = { 0 };
    VLC vlc = { .table = table, .table_allocated = static_size };
    av_assert0(static_size <= FF_ARRAY_ELEMS(table));
    vlc_init(&vlc, TEX_VLC_BITS, n + 2, &table_vlc[0][1], 4, 2, &table_vlc[0][0], 4, 2, VLC_INIT_USE_STATIC | flags);

    for (i = 0; i < vlc.table_size; i++) {
        int code = vlc.table[i].sym;
        int len  = vlc.table[i].len;
        int level, run;

        if (len == 0) { // illegal code
            run   = 65;
            level = MAX_LEVEL;
        } else if (len<0) { //more bits needed
            run   = 0;
            level = code;
        } else {
            if (code == n) { //esc
                run   = 65;
                level = 0;
            } else if (code == n + 1) { //eob
                run   = 0;
                level = 127;
            } else {
                run   = table_run  [code] + 1;
                level = table_level[code];
            }
        }
        rl_vlc[i].len   = len;
        rl_vlc[i].level = level;
        rl_vlc[i].run   = run;
    }
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

VLCElem ff_mv_vlc[266];

VLCElem ff_dc_lum_vlc[512];
VLCElem ff_dc_chroma_vlc[514];

VLCElem ff_mbincr_vlc[538];
VLCElem ff_mb_ptype_vlc[64];
VLCElem ff_mb_btype_vlc[64];
VLCElem ff_mb_pat_vlc[512];

RL_VLC_ELEM ff_mpeg1_rl_vlc[680];
RL_VLC_ELEM ff_mpeg2_rl_vlc[674];

static av_cold void mpeg12_init_vlcs(void)
{
    VLC_INIT_STATIC_TABLE(ff_dc_lum_vlc, DC_VLC_BITS, 12,
                          ff_mpeg12_vlc_dc_lum_bits, 1, 1,
                          ff_mpeg12_vlc_dc_lum_code, 2, 2, 0);
    VLC_INIT_STATIC_TABLE(ff_dc_chroma_vlc,  DC_VLC_BITS, 12,
                          ff_mpeg12_vlc_dc_chroma_bits, 1, 1,
                          ff_mpeg12_vlc_dc_chroma_code, 2, 2, 0);
    VLC_INIT_STATIC_TABLE(ff_mv_vlc, MV_VLC_BITS, 17,
                          &ff_mpeg12_mbMotionVectorTable[0][1], 2, 1,
                          &ff_mpeg12_mbMotionVectorTable[0][0], 2, 1, 0);
    VLC_INIT_STATIC_TABLE(ff_mbincr_vlc, MBINCR_VLC_BITS, 36,
                          &ff_mpeg12_mbAddrIncrTable[0][1], 2, 1,
                          &ff_mpeg12_mbAddrIncrTable[0][0], 2, 1, 0);
    VLC_INIT_STATIC_TABLE(ff_mb_pat_vlc, MB_PAT_VLC_BITS, 64,
                          &ff_mpeg12_mbPatTable[0][1], 2, 1,
                          &ff_mpeg12_mbPatTable[0][0], 2, 1, 0);

    VLC_INIT_STATIC_SPARSE_TABLE(ff_mb_ptype_vlc, MB_PTYPE_VLC_BITS, 7,
                                 &table_mb_ptype[0][1], 2, 1,
                                 &table_mb_ptype[0][0], 2, 1,
                                 ptype2mb_type, 2, 2, 0);
    VLC_INIT_STATIC_SPARSE_TABLE(ff_mb_btype_vlc, MB_BTYPE_VLC_BITS, 11,
                                 &table_mb_btype[0][1], 2, 1,
                                 &table_mb_btype[0][0], 2, 1,
                                 btype2mb_type, 2, 2, 0);

    ff_init_2d_vlc_rl(ff_mpeg1_vlc_table, ff_mpeg1_rl_vlc, ff_mpeg12_run,
                      ff_mpeg12_level, MPEG12_RL_NB_ELEMS,
                      FF_ARRAY_ELEMS(ff_mpeg1_rl_vlc), 0);
    ff_init_2d_vlc_rl(ff_mpeg2_vlc_table, ff_mpeg2_rl_vlc, ff_mpeg12_run,
                      ff_mpeg12_level, MPEG12_RL_NB_ELEMS,
                      FF_ARRAY_ELEMS(ff_mpeg2_rl_vlc), 0);
}

av_cold void ff_mpeg12_init_vlcs(void)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    ff_thread_once(&init_static_once, mpeg12_init_vlcs);
}

#define MAX_INDEX (64 - 1)

int ff_mpeg1_decode_block_intra(GetBitContext *gb,
                                const uint16_t *quant_matrix,
                                const uint8_t *scantable, int last_dc[3],
                                int16_t *block, int index, int qscale)
{
    int dc, diff, i = 0, component;

    /* DC coefficient */
    component = index <= 3 ? 0 : index - 4 + 1;

    diff = decode_dc(gb, component);

    dc  = last_dc[component];
    dc += diff;
    last_dc[component] = dc;

    block[0] = dc * quant_matrix[0];

    {
        OPEN_READER(re, gb);
        UPDATE_CACHE(re, gb);
        if (((int32_t)GET_CACHE(re, gb)) <= (int32_t)0xBFFFFFFF)
            goto end;

        /* now quantify & encode AC coefficients */
        while (1) {
            int level, run, j;

            GET_RL_VLC(level, run, re, gb, ff_mpeg1_rl_vlc,
                       TEX_VLC_BITS, 2, 0);

            if (level != 0) {
                i += run;
                if (i > MAX_INDEX)
                    break;

                j = scantable[i];
                level = (level * qscale * quant_matrix[j]) >> 4;
                level = (level - 1) | 1;
                level = (level ^ SHOW_SBITS(re, gb, 1)) -
                        SHOW_SBITS(re, gb, 1);
                SKIP_BITS(re, gb, 1);
            } else {
                /* escape */
                run = SHOW_UBITS(re, gb, 6) + 1;
                LAST_SKIP_BITS(re, gb, 6);
                UPDATE_CACHE(re, gb);
                level = SHOW_SBITS(re, gb, 8);
                SKIP_BITS(re, gb, 8);

                if (level == -128) {
                    level = SHOW_UBITS(re, gb, 8) - 256;
                    SKIP_BITS(re, gb, 8);
                } else if (level == 0) {
                    level = SHOW_UBITS(re, gb, 8);
                    SKIP_BITS(re, gb, 8);
                }

                i += run;
                if (i > MAX_INDEX)
                    break;

                j = scantable[i];
                if (level < 0) {
                    level = -level;
                    level = (level * qscale * quant_matrix[j]) >> 4;
                    level = (level - 1) | 1;
                    level = -level;
                } else {
                    level = (level * qscale * quant_matrix[j]) >> 4;
                    level = (level - 1) | 1;
                }
            }

            block[j] = level;
            if (((int32_t)GET_CACHE(re, gb)) <= (int32_t)0xBFFFFFFF)
               break;

            UPDATE_CACHE(re, gb);
        }
end:
        LAST_SKIP_BITS(re, gb, 2);
        CLOSE_READER(re, gb);
    }

    if (i > MAX_INDEX)
        i = AVERROR_INVALIDDATA;

    return i;
}
