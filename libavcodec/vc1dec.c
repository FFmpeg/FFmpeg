/*
 * VC-1 and WMV3 decoder
 * Copyright (c) 2011 Mashiat Sarker Shakkhar
 * Copyright (c) 2006-2007 Konstantin Shishkov
 * Partly based on vc9.c (c) 2005 Anonymous, Alex Beregszaszi, Michael Niedermayer
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
 * VC-1 and WMV3 decoder
 */

#include "internal.h"
#include "dsputil.h"
#include "avcodec.h"
#include "mpegvideo.h"
#include "h263.h"
#include "vc1.h"
#include "vc1data.h"
#include "vc1acdata.h"
#include "msmpeg4data.h"
#include "unary.h"
#include "simple_idct.h"
#include "mathops.h"
#include "vdpau_internal.h"

#undef NDEBUG
#include <assert.h>

#define MB_INTRA_VLC_BITS 9
#define DC_VLC_BITS 9
#define AC_VLC_BITS 9


static const uint16_t vlc_offs[] = {
        0,   520,   552,   616,  1128,  1160,  1224,  1740,  1772,  1836,  1900,  2436,
     2986,  3050,  3610,  4154,  4218,  4746,  5326,  5390,  5902,  6554,  7658,  8342,
     9304,  9988, 10630, 11234, 12174, 13006, 13560, 14232, 14786, 15432, 16350, 17522,
    20372, 21818, 22330, 22394, 23166, 23678, 23742, 24820, 25332, 25396, 26460, 26980,
    27048, 27592, 27600, 27608, 27616, 27624, 28224, 28258, 28290, 28802, 28834, 28866,
    29378, 29412, 29444, 29960, 29994, 30026, 30538, 30572, 30604, 31120, 31154, 31186,
    31714, 31746, 31778, 32306, 32340, 32372
};

// offset tables for interlaced picture MVDATA decoding
static const int offset_table1[9] = {  0,  1,  2,  4,  8, 16, 32,  64, 128 };
static const int offset_table2[9] = {  0,  1,  3,  7, 15, 31, 63, 127, 255 };

/**
 * Init VC-1 specific tables and VC1Context members
 * @param v The VC1Context to initialize
 * @return Status
 */
int ff_vc1_init_common(VC1Context *v)
{
    static int done = 0;
    int i = 0;
    static VLC_TYPE vlc_table[32372][2];

    v->hrd_rate = v->hrd_buffer = NULL;

    /* VLC tables */
    if (!done) {
        INIT_VLC_STATIC(&ff_vc1_bfraction_vlc, VC1_BFRACTION_VLC_BITS, 23,
                        ff_vc1_bfraction_bits, 1, 1,
                        ff_vc1_bfraction_codes, 1, 1, 1 << VC1_BFRACTION_VLC_BITS);
        INIT_VLC_STATIC(&ff_vc1_norm2_vlc, VC1_NORM2_VLC_BITS, 4,
                        ff_vc1_norm2_bits, 1, 1,
                        ff_vc1_norm2_codes, 1, 1, 1 << VC1_NORM2_VLC_BITS);
        INIT_VLC_STATIC(&ff_vc1_norm6_vlc, VC1_NORM6_VLC_BITS, 64,
                        ff_vc1_norm6_bits, 1, 1,
                        ff_vc1_norm6_codes, 2, 2, 556);
        INIT_VLC_STATIC(&ff_vc1_imode_vlc, VC1_IMODE_VLC_BITS, 7,
                        ff_vc1_imode_bits, 1, 1,
                        ff_vc1_imode_codes, 1, 1, 1 << VC1_IMODE_VLC_BITS);
        for (i = 0; i < 3; i++) {
            ff_vc1_ttmb_vlc[i].table           = &vlc_table[vlc_offs[i * 3 + 0]];
            ff_vc1_ttmb_vlc[i].table_allocated = vlc_offs[i * 3 + 1] - vlc_offs[i * 3 + 0];
            init_vlc(&ff_vc1_ttmb_vlc[i], VC1_TTMB_VLC_BITS, 16,
                     ff_vc1_ttmb_bits[i], 1, 1,
                     ff_vc1_ttmb_codes[i], 2, 2, INIT_VLC_USE_NEW_STATIC);
            ff_vc1_ttblk_vlc[i].table           = &vlc_table[vlc_offs[i * 3 + 1]];
            ff_vc1_ttblk_vlc[i].table_allocated = vlc_offs[i * 3 + 2] - vlc_offs[i * 3 + 1];
            init_vlc(&ff_vc1_ttblk_vlc[i], VC1_TTBLK_VLC_BITS, 8,
                     ff_vc1_ttblk_bits[i], 1, 1,
                     ff_vc1_ttblk_codes[i], 1, 1, INIT_VLC_USE_NEW_STATIC);
            ff_vc1_subblkpat_vlc[i].table           = &vlc_table[vlc_offs[i * 3 + 2]];
            ff_vc1_subblkpat_vlc[i].table_allocated = vlc_offs[i * 3 + 3] - vlc_offs[i * 3 + 2];
            init_vlc(&ff_vc1_subblkpat_vlc[i], VC1_SUBBLKPAT_VLC_BITS, 15,
                     ff_vc1_subblkpat_bits[i], 1, 1,
                     ff_vc1_subblkpat_codes[i], 1, 1, INIT_VLC_USE_NEW_STATIC);
        }
        for (i = 0; i < 4; i++) {
            ff_vc1_4mv_block_pattern_vlc[i].table           = &vlc_table[vlc_offs[i * 3 + 9]];
            ff_vc1_4mv_block_pattern_vlc[i].table_allocated = vlc_offs[i * 3 + 10] - vlc_offs[i * 3 + 9];
            init_vlc(&ff_vc1_4mv_block_pattern_vlc[i], VC1_4MV_BLOCK_PATTERN_VLC_BITS, 16,
                     ff_vc1_4mv_block_pattern_bits[i], 1, 1,
                     ff_vc1_4mv_block_pattern_codes[i], 1, 1, INIT_VLC_USE_NEW_STATIC);
            ff_vc1_cbpcy_p_vlc[i].table           = &vlc_table[vlc_offs[i * 3 + 10]];
            ff_vc1_cbpcy_p_vlc[i].table_allocated = vlc_offs[i * 3 + 11] - vlc_offs[i * 3 + 10];
            init_vlc(&ff_vc1_cbpcy_p_vlc[i], VC1_CBPCY_P_VLC_BITS, 64,
                     ff_vc1_cbpcy_p_bits[i], 1, 1,
                     ff_vc1_cbpcy_p_codes[i], 2, 2, INIT_VLC_USE_NEW_STATIC);
            ff_vc1_mv_diff_vlc[i].table           = &vlc_table[vlc_offs[i * 3 + 11]];
            ff_vc1_mv_diff_vlc[i].table_allocated = vlc_offs[i * 3 + 12] - vlc_offs[i * 3 + 11];
            init_vlc(&ff_vc1_mv_diff_vlc[i], VC1_MV_DIFF_VLC_BITS, 73,
                     ff_vc1_mv_diff_bits[i], 1, 1,
                     ff_vc1_mv_diff_codes[i], 2, 2, INIT_VLC_USE_NEW_STATIC);
        }
        for (i = 0; i < 8; i++) {
            ff_vc1_ac_coeff_table[i].table           = &vlc_table[vlc_offs[i * 2 + 21]];
            ff_vc1_ac_coeff_table[i].table_allocated = vlc_offs[i * 2 + 22] - vlc_offs[i * 2 + 21];
            init_vlc(&ff_vc1_ac_coeff_table[i], AC_VLC_BITS, vc1_ac_sizes[i],
                     &vc1_ac_tables[i][0][1], 8, 4,
                     &vc1_ac_tables[i][0][0], 8, 4, INIT_VLC_USE_NEW_STATIC);
            /* initialize interlaced MVDATA tables (2-Ref) */
            ff_vc1_2ref_mvdata_vlc[i].table           = &vlc_table[vlc_offs[i * 2 + 22]];
            ff_vc1_2ref_mvdata_vlc[i].table_allocated = vlc_offs[i * 2 + 23] - vlc_offs[i * 2 + 22];
            init_vlc(&ff_vc1_2ref_mvdata_vlc[i], VC1_2REF_MVDATA_VLC_BITS, 126,
                     ff_vc1_2ref_mvdata_bits[i], 1, 1,
                     ff_vc1_2ref_mvdata_codes[i], 4, 4, INIT_VLC_USE_NEW_STATIC);
        }
        for (i = 0; i < 4; i++) {
            /* initialize 4MV MBMODE VLC tables for interlaced frame P picture */
            ff_vc1_intfr_4mv_mbmode_vlc[i].table           = &vlc_table[vlc_offs[i * 3 + 37]];
            ff_vc1_intfr_4mv_mbmode_vlc[i].table_allocated = vlc_offs[i * 3 + 38] - vlc_offs[i * 3 + 37];
            init_vlc(&ff_vc1_intfr_4mv_mbmode_vlc[i], VC1_INTFR_4MV_MBMODE_VLC_BITS, 15,
                     ff_vc1_intfr_4mv_mbmode_bits[i], 1, 1,
                     ff_vc1_intfr_4mv_mbmode_codes[i], 2, 2, INIT_VLC_USE_NEW_STATIC);
            /* initialize NON-4MV MBMODE VLC tables for the same */
            ff_vc1_intfr_non4mv_mbmode_vlc[i].table           = &vlc_table[vlc_offs[i * 3 + 38]];
            ff_vc1_intfr_non4mv_mbmode_vlc[i].table_allocated = vlc_offs[i * 3 + 39] - vlc_offs[i * 3 + 38];
            init_vlc(&ff_vc1_intfr_non4mv_mbmode_vlc[i], VC1_INTFR_NON4MV_MBMODE_VLC_BITS, 9,
                     ff_vc1_intfr_non4mv_mbmode_bits[i], 1, 1,
                     ff_vc1_intfr_non4mv_mbmode_codes[i], 1, 1, INIT_VLC_USE_NEW_STATIC);
            /* initialize interlaced MVDATA tables (1-Ref) */
            ff_vc1_1ref_mvdata_vlc[i].table           = &vlc_table[vlc_offs[i * 3 + 39]];
            ff_vc1_1ref_mvdata_vlc[i].table_allocated = vlc_offs[i * 3 + 40] - vlc_offs[i * 3 + 39];
            init_vlc(&ff_vc1_1ref_mvdata_vlc[i], VC1_1REF_MVDATA_VLC_BITS, 72,
                     ff_vc1_1ref_mvdata_bits[i], 1, 1,
                     ff_vc1_1ref_mvdata_codes[i], 4, 4, INIT_VLC_USE_NEW_STATIC);
        }
        for (i = 0; i < 4; i++) {
            /* Initialize 2MV Block pattern VLC tables */
            ff_vc1_2mv_block_pattern_vlc[i].table           = &vlc_table[vlc_offs[i + 49]];
            ff_vc1_2mv_block_pattern_vlc[i].table_allocated = vlc_offs[i + 50] - vlc_offs[i + 49];
            init_vlc(&ff_vc1_2mv_block_pattern_vlc[i], VC1_2MV_BLOCK_PATTERN_VLC_BITS, 4,
                     ff_vc1_2mv_block_pattern_bits[i], 1, 1,
                     ff_vc1_2mv_block_pattern_codes[i], 1, 1, INIT_VLC_USE_NEW_STATIC);
        }
        for (i = 0; i < 8; i++) {
            /* Initialize interlaced CBPCY VLC tables (Table 124 - Table 131) */
            ff_vc1_icbpcy_vlc[i].table           = &vlc_table[vlc_offs[i * 3 + 53]];
            ff_vc1_icbpcy_vlc[i].table_allocated = vlc_offs[i * 3 + 54] - vlc_offs[i * 3 + 53];
            init_vlc(&ff_vc1_icbpcy_vlc[i], VC1_ICBPCY_VLC_BITS, 63,
                     ff_vc1_icbpcy_p_bits[i], 1, 1,
                     ff_vc1_icbpcy_p_codes[i], 2, 2, INIT_VLC_USE_NEW_STATIC);
            /* Initialize interlaced field picture MBMODE VLC tables */
            ff_vc1_if_mmv_mbmode_vlc[i].table           = &vlc_table[vlc_offs[i * 3 + 54]];
            ff_vc1_if_mmv_mbmode_vlc[i].table_allocated = vlc_offs[i * 3 + 55] - vlc_offs[i * 3 + 54];
            init_vlc(&ff_vc1_if_mmv_mbmode_vlc[i], VC1_IF_MMV_MBMODE_VLC_BITS, 8,
                     ff_vc1_if_mmv_mbmode_bits[i], 1, 1,
                     ff_vc1_if_mmv_mbmode_codes[i], 1, 1, INIT_VLC_USE_NEW_STATIC);
            ff_vc1_if_1mv_mbmode_vlc[i].table           = &vlc_table[vlc_offs[i * 3 + 55]];
            ff_vc1_if_1mv_mbmode_vlc[i].table_allocated = vlc_offs[i * 3 + 56] - vlc_offs[i * 3 + 55];
            init_vlc(&ff_vc1_if_1mv_mbmode_vlc[i], VC1_IF_1MV_MBMODE_VLC_BITS, 6,
                     ff_vc1_if_1mv_mbmode_bits[i], 1, 1,
                     ff_vc1_if_1mv_mbmode_codes[i], 1, 1, INIT_VLC_USE_NEW_STATIC);
        }
        done = 1;
    }

    /* Other defaults */
    v->pq      = -1;
    v->mvrange = 0; /* 7.1.1.18, p80 */

    return 0;
}

/***********************************************************************/
/**
 * @name VC-1 Bitplane decoding
 * @see 8.7, p56
 * @{
 */

/**
 * Imode types
 * @{
 */
enum Imode {
    IMODE_RAW,
    IMODE_NORM2,
    IMODE_DIFF2,
    IMODE_NORM6,
    IMODE_DIFF6,
    IMODE_ROWSKIP,
    IMODE_COLSKIP
};
/** @} */ //imode defines


/** @} */ //Bitplane group

static void vc1_put_signed_blocks_clamped(VC1Context *v)
{
    MpegEncContext *s = &v->s;
    int topleft_mb_pos, top_mb_pos;
    int stride_y, fieldtx;
    int v_dist;

    /* The put pixels loop is always one MB row behind the decoding loop,
     * because we can only put pixels when overlap filtering is done, and
     * for filtering of the bottom edge of a MB, we need the next MB row
     * present as well.
     * Within the row, the put pixels loop is also one MB col behind the
     * decoding loop. The reason for this is again, because for filtering
     * of the right MB edge, we need the next MB present. */
    if (!s->first_slice_line) {
        if (s->mb_x) {
            topleft_mb_pos = (s->mb_y - 1) * s->mb_stride + s->mb_x - 1;
            fieldtx        = v->fieldtx_plane[topleft_mb_pos];
            stride_y       = s->linesize << fieldtx;
            v_dist         = (16 - fieldtx) >> (fieldtx == 0);
            s->dsp.put_signed_pixels_clamped(v->block[v->topleft_blk_idx][0],
                                             s->dest[0] - 16 * s->linesize - 16,
                                             stride_y);
            s->dsp.put_signed_pixels_clamped(v->block[v->topleft_blk_idx][1],
                                             s->dest[0] - 16 * s->linesize - 8,
                                             stride_y);
            s->dsp.put_signed_pixels_clamped(v->block[v->topleft_blk_idx][2],
                                             s->dest[0] - v_dist * s->linesize - 16,
                                             stride_y);
            s->dsp.put_signed_pixels_clamped(v->block[v->topleft_blk_idx][3],
                                             s->dest[0] - v_dist * s->linesize - 8,
                                             stride_y);
            s->dsp.put_signed_pixels_clamped(v->block[v->topleft_blk_idx][4],
                                             s->dest[1] - 8 * s->uvlinesize - 8,
                                             s->uvlinesize);
            s->dsp.put_signed_pixels_clamped(v->block[v->topleft_blk_idx][5],
                                             s->dest[2] - 8 * s->uvlinesize - 8,
                                             s->uvlinesize);
        }
        if (s->mb_x == s->mb_width - 1) {
            top_mb_pos = (s->mb_y - 1) * s->mb_stride + s->mb_x;
            fieldtx    = v->fieldtx_plane[top_mb_pos];
            stride_y   = s->linesize << fieldtx;
            v_dist     = fieldtx ? 15 : 8;
            s->dsp.put_signed_pixels_clamped(v->block[v->top_blk_idx][0],
                                             s->dest[0] - 16 * s->linesize,
                                             stride_y);
            s->dsp.put_signed_pixels_clamped(v->block[v->top_blk_idx][1],
                                             s->dest[0] - 16 * s->linesize + 8,
                                             stride_y);
            s->dsp.put_signed_pixels_clamped(v->block[v->top_blk_idx][2],
                                             s->dest[0] - v_dist * s->linesize,
                                             stride_y);
            s->dsp.put_signed_pixels_clamped(v->block[v->top_blk_idx][3],
                                             s->dest[0] - v_dist * s->linesize + 8,
                                             stride_y);
            s->dsp.put_signed_pixels_clamped(v->block[v->top_blk_idx][4],
                                             s->dest[1] - 8 * s->uvlinesize,
                                             s->uvlinesize);
            s->dsp.put_signed_pixels_clamped(v->block[v->top_blk_idx][5],
                                             s->dest[2] - 8 * s->uvlinesize,
                                             s->uvlinesize);
        }
    }

#define inc_blk_idx(idx) do { \
        idx++; \
        if (idx >= v->n_allocated_blks) \
            idx = 0; \
    } while (0)

    inc_blk_idx(v->topleft_blk_idx);
    inc_blk_idx(v->top_blk_idx);
    inc_blk_idx(v->left_blk_idx);
    inc_blk_idx(v->cur_blk_idx);
}

static void vc1_loop_filter_iblk(VC1Context *v, int pq)
{
    MpegEncContext *s = &v->s;
    int j;
    if (!s->first_slice_line) {
        v->vc1dsp.vc1_v_loop_filter16(s->dest[0], s->linesize, pq);
        if (s->mb_x)
            v->vc1dsp.vc1_h_loop_filter16(s->dest[0] - 16 * s->linesize, s->linesize, pq);
        v->vc1dsp.vc1_h_loop_filter16(s->dest[0] - 16 * s->linesize + 8, s->linesize, pq);
        for (j = 0; j < 2; j++) {
            v->vc1dsp.vc1_v_loop_filter8(s->dest[j + 1], s->uvlinesize, pq);
            if (s->mb_x)
                v->vc1dsp.vc1_h_loop_filter8(s->dest[j + 1] - 8 * s->uvlinesize, s->uvlinesize, pq);
        }
    }
    v->vc1dsp.vc1_v_loop_filter16(s->dest[0] + 8 * s->linesize, s->linesize, pq);

    if (s->mb_y == s->end_mb_y - 1) {
        if (s->mb_x) {
            v->vc1dsp.vc1_h_loop_filter16(s->dest[0], s->linesize, pq);
            v->vc1dsp.vc1_h_loop_filter8(s->dest[1], s->uvlinesize, pq);
            v->vc1dsp.vc1_h_loop_filter8(s->dest[2], s->uvlinesize, pq);
        }
        v->vc1dsp.vc1_h_loop_filter16(s->dest[0] + 8, s->linesize, pq);
    }
}

static void vc1_loop_filter_iblk_delayed(VC1Context *v, int pq)
{
    MpegEncContext *s = &v->s;
    int j;

    /* The loopfilter runs 1 row and 1 column behind the overlap filter, which
     * means it runs two rows/cols behind the decoding loop. */
    if (!s->first_slice_line) {
        if (s->mb_x) {
            if (s->mb_y >= s->start_mb_y + 2) {
                v->vc1dsp.vc1_v_loop_filter16(s->dest[0] - 16 * s->linesize - 16, s->linesize, pq);

                if (s->mb_x >= 2)
                    v->vc1dsp.vc1_h_loop_filter16(s->dest[0] - 32 * s->linesize - 16, s->linesize, pq);
                v->vc1dsp.vc1_h_loop_filter16(s->dest[0] - 32 * s->linesize - 8, s->linesize, pq);
                for (j = 0; j < 2; j++) {
                    v->vc1dsp.vc1_v_loop_filter8(s->dest[j + 1] - 8 * s->uvlinesize - 8, s->uvlinesize, pq);
                    if (s->mb_x >= 2) {
                        v->vc1dsp.vc1_h_loop_filter8(s->dest[j + 1] - 16 * s->uvlinesize - 8, s->uvlinesize, pq);
                    }
                }
            }
            v->vc1dsp.vc1_v_loop_filter16(s->dest[0] - 8 * s->linesize - 16, s->linesize, pq);
        }

        if (s->mb_x == s->mb_width - 1) {
            if (s->mb_y >= s->start_mb_y + 2) {
                v->vc1dsp.vc1_v_loop_filter16(s->dest[0] - 16 * s->linesize, s->linesize, pq);

                if (s->mb_x)
                    v->vc1dsp.vc1_h_loop_filter16(s->dest[0] - 32 * s->linesize, s->linesize, pq);
                v->vc1dsp.vc1_h_loop_filter16(s->dest[0] - 32 * s->linesize + 8, s->linesize, pq);
                for (j = 0; j < 2; j++) {
                    v->vc1dsp.vc1_v_loop_filter8(s->dest[j + 1] - 8 * s->uvlinesize, s->uvlinesize, pq);
                    if (s->mb_x >= 2) {
                        v->vc1dsp.vc1_h_loop_filter8(s->dest[j + 1] - 16 * s->uvlinesize, s->uvlinesize, pq);
                    }
                }
            }
            v->vc1dsp.vc1_v_loop_filter16(s->dest[0] - 8 * s->linesize, s->linesize, pq);
        }

        if (s->mb_y == s->end_mb_y) {
            if (s->mb_x) {
                if (s->mb_x >= 2)
                    v->vc1dsp.vc1_h_loop_filter16(s->dest[0] - 16 * s->linesize - 16, s->linesize, pq);
                v->vc1dsp.vc1_h_loop_filter16(s->dest[0] - 16 * s->linesize - 8, s->linesize, pq);
                if (s->mb_x >= 2) {
                    for (j = 0; j < 2; j++) {
                        v->vc1dsp.vc1_h_loop_filter8(s->dest[j + 1] - 8 * s->uvlinesize - 8, s->uvlinesize, pq);
                    }
                }
            }

            if (s->mb_x == s->mb_width - 1) {
                if (s->mb_x)
                    v->vc1dsp.vc1_h_loop_filter16(s->dest[0] - 16 * s->linesize, s->linesize, pq);
                v->vc1dsp.vc1_h_loop_filter16(s->dest[0] - 16 * s->linesize + 8, s->linesize, pq);
                if (s->mb_x) {
                    for (j = 0; j < 2; j++) {
                        v->vc1dsp.vc1_h_loop_filter8(s->dest[j + 1] - 8 * s->uvlinesize, s->uvlinesize, pq);
                    }
                }
            }
        }
    }
}

static void vc1_smooth_overlap_filter_iblk(VC1Context *v)
{
    MpegEncContext *s = &v->s;
    int mb_pos;

    if (v->condover == CONDOVER_NONE)
        return;

    mb_pos = s->mb_x + s->mb_y * s->mb_stride;

    /* Within a MB, the horizontal overlap always runs before the vertical.
     * To accomplish that, we run the H on left and internal borders of the
     * currently decoded MB. Then, we wait for the next overlap iteration
     * to do H overlap on the right edge of this MB, before moving over and
     * running the V overlap. Therefore, the V overlap makes us trail by one
     * MB col and the H overlap filter makes us trail by one MB row. This
     * is reflected in the time at which we run the put_pixels loop. */
    if (v->condover == CONDOVER_ALL || v->pq >= 9 || v->over_flags_plane[mb_pos]) {
        if (s->mb_x && (v->condover == CONDOVER_ALL || v->pq >= 9 ||
                        v->over_flags_plane[mb_pos - 1])) {
            v->vc1dsp.vc1_h_s_overlap(v->block[v->left_blk_idx][1],
                                      v->block[v->cur_blk_idx][0]);
            v->vc1dsp.vc1_h_s_overlap(v->block[v->left_blk_idx][3],
                                      v->block[v->cur_blk_idx][2]);
            if (!(s->flags & CODEC_FLAG_GRAY)) {
                v->vc1dsp.vc1_h_s_overlap(v->block[v->left_blk_idx][4],
                                          v->block[v->cur_blk_idx][4]);
                v->vc1dsp.vc1_h_s_overlap(v->block[v->left_blk_idx][5],
                                          v->block[v->cur_blk_idx][5]);
            }
        }
        v->vc1dsp.vc1_h_s_overlap(v->block[v->cur_blk_idx][0],
                                  v->block[v->cur_blk_idx][1]);
        v->vc1dsp.vc1_h_s_overlap(v->block[v->cur_blk_idx][2],
                                  v->block[v->cur_blk_idx][3]);

        if (s->mb_x == s->mb_width - 1) {
            if (!s->first_slice_line && (v->condover == CONDOVER_ALL || v->pq >= 9 ||
                                         v->over_flags_plane[mb_pos - s->mb_stride])) {
                v->vc1dsp.vc1_v_s_overlap(v->block[v->top_blk_idx][2],
                                          v->block[v->cur_blk_idx][0]);
                v->vc1dsp.vc1_v_s_overlap(v->block[v->top_blk_idx][3],
                                          v->block[v->cur_blk_idx][1]);
                if (!(s->flags & CODEC_FLAG_GRAY)) {
                    v->vc1dsp.vc1_v_s_overlap(v->block[v->top_blk_idx][4],
                                              v->block[v->cur_blk_idx][4]);
                    v->vc1dsp.vc1_v_s_overlap(v->block[v->top_blk_idx][5],
                                              v->block[v->cur_blk_idx][5]);
                }
            }
            v->vc1dsp.vc1_v_s_overlap(v->block[v->cur_blk_idx][0],
                                      v->block[v->cur_blk_idx][2]);
            v->vc1dsp.vc1_v_s_overlap(v->block[v->cur_blk_idx][1],
                                      v->block[v->cur_blk_idx][3]);
        }
    }
    if (s->mb_x && (v->condover == CONDOVER_ALL || v->over_flags_plane[mb_pos - 1])) {
        if (!s->first_slice_line && (v->condover == CONDOVER_ALL || v->pq >= 9 ||
                                     v->over_flags_plane[mb_pos - s->mb_stride - 1])) {
            v->vc1dsp.vc1_v_s_overlap(v->block[v->topleft_blk_idx][2],
                                      v->block[v->left_blk_idx][0]);
            v->vc1dsp.vc1_v_s_overlap(v->block[v->topleft_blk_idx][3],
                                      v->block[v->left_blk_idx][1]);
            if (!(s->flags & CODEC_FLAG_GRAY)) {
                v->vc1dsp.vc1_v_s_overlap(v->block[v->topleft_blk_idx][4],
                                          v->block[v->left_blk_idx][4]);
                v->vc1dsp.vc1_v_s_overlap(v->block[v->topleft_blk_idx][5],
                                          v->block[v->left_blk_idx][5]);
            }
        }
        v->vc1dsp.vc1_v_s_overlap(v->block[v->left_blk_idx][0],
                                  v->block[v->left_blk_idx][2]);
        v->vc1dsp.vc1_v_s_overlap(v->block[v->left_blk_idx][1],
                                  v->block[v->left_blk_idx][3]);
    }
}

/** Do motion compensation over 1 macroblock
 * Mostly adapted hpel_motion and qpel_motion from mpegvideo.c
 */
static void vc1_mc_1mv(VC1Context *v, int dir)
{
    MpegEncContext *s = &v->s;
    DSPContext *dsp   = &v->s.dsp;
    uint8_t *srcY, *srcU, *srcV;
    int dxy, mx, my, uvmx, uvmy, src_x, src_y, uvsrc_x, uvsrc_y;
    int off, off_uv;
    int v_edge_pos = s->v_edge_pos >> v->field_mode;

    if ((!v->field_mode ||
         (v->ref_field_type[dir] == 1 && v->cur_field_type == 1)) &&
        !v->s.last_picture.f.data[0])
        return;

    mx = s->mv[dir][0][0];
    my = s->mv[dir][0][1];

    // store motion vectors for further use in B frames
    if (s->pict_type == AV_PICTURE_TYPE_P) {
        s->current_picture.f.motion_val[1][s->block_index[0] + v->blocks_off][0] = mx;
        s->current_picture.f.motion_val[1][s->block_index[0] + v->blocks_off][1] = my;
    }

    uvmx = (mx + ((mx & 3) == 3)) >> 1;
    uvmy = (my + ((my & 3) == 3)) >> 1;
    v->luma_mv[s->mb_x][0] = uvmx;
    v->luma_mv[s->mb_x][1] = uvmy;

    if (v->field_mode &&
        v->cur_field_type != v->ref_field_type[dir]) {
        my   = my   - 2 + 4 * v->cur_field_type;
        uvmy = uvmy - 2 + 4 * v->cur_field_type;
    }

    // fastuvmc shall be ignored for interlaced frame picture
    if (v->fastuvmc && (v->fcm != ILACE_FRAME)) {
        uvmx = uvmx + ((uvmx < 0) ? (uvmx & 1) : -(uvmx & 1));
        uvmy = uvmy + ((uvmy < 0) ? (uvmy & 1) : -(uvmy & 1));
    }
    if (v->field_mode) { // interlaced field picture
        if (!dir) {
            if ((v->cur_field_type != v->ref_field_type[dir]) && v->second_field) {
                srcY = s->current_picture.f.data[0];
                srcU = s->current_picture.f.data[1];
                srcV = s->current_picture.f.data[2];
            } else {
                srcY = s->last_picture.f.data[0];
                srcU = s->last_picture.f.data[1];
                srcV = s->last_picture.f.data[2];
            }
        } else {
            srcY = s->next_picture.f.data[0];
            srcU = s->next_picture.f.data[1];
            srcV = s->next_picture.f.data[2];
        }
    } else {
        if (!dir) {
            srcY = s->last_picture.f.data[0];
            srcU = s->last_picture.f.data[1];
            srcV = s->last_picture.f.data[2];
        } else {
            srcY = s->next_picture.f.data[0];
            srcU = s->next_picture.f.data[1];
            srcV = s->next_picture.f.data[2];
        }
    }

    src_x   = s->mb_x * 16 + (mx   >> 2);
    src_y   = s->mb_y * 16 + (my   >> 2);
    uvsrc_x = s->mb_x *  8 + (uvmx >> 2);
    uvsrc_y = s->mb_y *  8 + (uvmy >> 2);

    if (v->profile != PROFILE_ADVANCED) {
        src_x   = av_clip(  src_x, -16, s->mb_width  * 16);
        src_y   = av_clip(  src_y, -16, s->mb_height * 16);
        uvsrc_x = av_clip(uvsrc_x,  -8, s->mb_width  *  8);
        uvsrc_y = av_clip(uvsrc_y,  -8, s->mb_height *  8);
    } else {
        src_x   = av_clip(  src_x, -17, s->avctx->coded_width);
        src_y   = av_clip(  src_y, -18, s->avctx->coded_height + 1);
        uvsrc_x = av_clip(uvsrc_x,  -8, s->avctx->coded_width  >> 1);
        uvsrc_y = av_clip(uvsrc_y,  -8, s->avctx->coded_height >> 1);
    }

    srcY += src_y   * s->linesize   + src_x;
    srcU += uvsrc_y * s->uvlinesize + uvsrc_x;
    srcV += uvsrc_y * s->uvlinesize + uvsrc_x;

    if (v->field_mode && v->ref_field_type[dir]) {
        srcY += s->current_picture_ptr->f.linesize[0];
        srcU += s->current_picture_ptr->f.linesize[1];
        srcV += s->current_picture_ptr->f.linesize[2];
    }

    /* for grayscale we should not try to read from unknown area */
    if (s->flags & CODEC_FLAG_GRAY) {
        srcU = s->edge_emu_buffer + 18 * s->linesize;
        srcV = s->edge_emu_buffer + 18 * s->linesize;
    }

    if (v->rangeredfrm || (v->mv_mode == MV_PMODE_INTENSITY_COMP)
        || s->h_edge_pos < 22 || v_edge_pos < 22
        || (unsigned)(src_x - s->mspel) > s->h_edge_pos - (mx&3) - 16 - s->mspel * 3
        || (unsigned)(src_y - s->mspel) > v_edge_pos    - (my&3) - 16 - s->mspel * 3) {
        uint8_t *uvbuf = s->edge_emu_buffer + 19 * s->linesize;

        srcY -= s->mspel * (1 + s->linesize);
        s->dsp.emulated_edge_mc(s->edge_emu_buffer, srcY, s->linesize,
                                17 + s->mspel * 2, 17 + s->mspel * 2,
                                src_x - s->mspel, src_y - s->mspel,
                                s->h_edge_pos, v_edge_pos);
        srcY = s->edge_emu_buffer;
        s->dsp.emulated_edge_mc(uvbuf     , srcU, s->uvlinesize, 8 + 1, 8 + 1,
                                uvsrc_x, uvsrc_y, s->h_edge_pos >> 1, v_edge_pos >> 1);
        s->dsp.emulated_edge_mc(uvbuf + 16, srcV, s->uvlinesize, 8 + 1, 8 + 1,
                                uvsrc_x, uvsrc_y, s->h_edge_pos >> 1, v_edge_pos >> 1);
        srcU = uvbuf;
        srcV = uvbuf + 16;
        /* if we deal with range reduction we need to scale source blocks */
        if (v->rangeredfrm) {
            int i, j;
            uint8_t *src, *src2;

            src = srcY;
            for (j = 0; j < 17 + s->mspel * 2; j++) {
                for (i = 0; i < 17 + s->mspel * 2; i++)
                    src[i] = ((src[i] - 128) >> 1) + 128;
                src += s->linesize;
            }
            src  = srcU;
            src2 = srcV;
            for (j = 0; j < 9; j++) {
                for (i = 0; i < 9; i++) {
                    src[i]  = ((src[i]  - 128) >> 1) + 128;
                    src2[i] = ((src2[i] - 128) >> 1) + 128;
                }
                src  += s->uvlinesize;
                src2 += s->uvlinesize;
            }
        }
        /* if we deal with intensity compensation we need to scale source blocks */
        if (v->mv_mode == MV_PMODE_INTENSITY_COMP) {
            int i, j;
            uint8_t *src, *src2;

            src = srcY;
            for (j = 0; j < 17 + s->mspel * 2; j++) {
                for (i = 0; i < 17 + s->mspel * 2; i++)
                    src[i] = v->luty[src[i]];
                src += s->linesize;
            }
            src  = srcU;
            src2 = srcV;
            for (j = 0; j < 9; j++) {
                for (i = 0; i < 9; i++) {
                    src[i]  = v->lutuv[src[i]];
                    src2[i] = v->lutuv[src2[i]];
                }
                src  += s->uvlinesize;
                src2 += s->uvlinesize;
            }
        }
        srcY += s->mspel * (1 + s->linesize);
    }

    if (v->field_mode && v->second_field) {
        off    = s->current_picture_ptr->f.linesize[0];
        off_uv = s->current_picture_ptr->f.linesize[1];
    } else {
        off    = 0;
        off_uv = 0;
    }
    if (s->mspel) {
        dxy = ((my & 3) << 2) | (mx & 3);
        v->vc1dsp.put_vc1_mspel_pixels_tab[dxy](s->dest[0] + off    , srcY    , s->linesize, v->rnd);
        v->vc1dsp.put_vc1_mspel_pixels_tab[dxy](s->dest[0] + off + 8, srcY + 8, s->linesize, v->rnd);
        srcY += s->linesize * 8;
        v->vc1dsp.put_vc1_mspel_pixels_tab[dxy](s->dest[0] + off + 8 * s->linesize    , srcY    , s->linesize, v->rnd);
        v->vc1dsp.put_vc1_mspel_pixels_tab[dxy](s->dest[0] + off + 8 * s->linesize + 8, srcY + 8, s->linesize, v->rnd);
    } else { // hpel mc - always used for luma
        dxy = (my & 2) | ((mx & 2) >> 1);
        if (!v->rnd)
            dsp->put_pixels_tab[0][dxy](s->dest[0] + off, srcY, s->linesize, 16);
        else
            dsp->put_no_rnd_pixels_tab[0][dxy](s->dest[0] + off, srcY, s->linesize, 16);
    }

    if (s->flags & CODEC_FLAG_GRAY) return;
    /* Chroma MC always uses qpel bilinear */
    uvmx = (uvmx & 3) << 1;
    uvmy = (uvmy & 3) << 1;
    if (!v->rnd) {
        dsp->put_h264_chroma_pixels_tab[0](s->dest[1] + off_uv, srcU, s->uvlinesize, 8, uvmx, uvmy);
        dsp->put_h264_chroma_pixels_tab[0](s->dest[2] + off_uv, srcV, s->uvlinesize, 8, uvmx, uvmy);
    } else {
        v->vc1dsp.put_no_rnd_vc1_chroma_pixels_tab[0](s->dest[1] + off_uv, srcU, s->uvlinesize, 8, uvmx, uvmy);
        v->vc1dsp.put_no_rnd_vc1_chroma_pixels_tab[0](s->dest[2] + off_uv, srcV, s->uvlinesize, 8, uvmx, uvmy);
    }
}

static inline int median4(int a, int b, int c, int d)
{
    if (a < b) {
        if (c < d) return (FFMIN(b, d) + FFMAX(a, c)) / 2;
        else       return (FFMIN(b, c) + FFMAX(a, d)) / 2;
    } else {
        if (c < d) return (FFMIN(a, d) + FFMAX(b, c)) / 2;
        else       return (FFMIN(a, c) + FFMAX(b, d)) / 2;
    }
}

/** Do motion compensation for 4-MV macroblock - luminance block
 */
static void vc1_mc_4mv_luma(VC1Context *v, int n, int dir)
{
    MpegEncContext *s = &v->s;
    DSPContext *dsp = &v->s.dsp;
    uint8_t *srcY;
    int dxy, mx, my, src_x, src_y;
    int off;
    int fieldmv = (v->fcm == ILACE_FRAME) ? v->blk_mv_type[s->block_index[n]] : 0;
    int v_edge_pos = s->v_edge_pos >> v->field_mode;

    if ((!v->field_mode ||
         (v->ref_field_type[dir] == 1 && v->cur_field_type == 1)) &&
        !v->s.last_picture.f.data[0])
        return;

    mx = s->mv[dir][n][0];
    my = s->mv[dir][n][1];

    if (!dir) {
        if (v->field_mode) {
            if ((v->cur_field_type != v->ref_field_type[dir]) && v->second_field)
                srcY = s->current_picture.f.data[0];
            else
                srcY = s->last_picture.f.data[0];
        } else
            srcY = s->last_picture.f.data[0];
    } else
        srcY = s->next_picture.f.data[0];

    if (v->field_mode) {
        if (v->cur_field_type != v->ref_field_type[dir])
            my = my - 2 + 4 * v->cur_field_type;
    }

    if (s->pict_type == AV_PICTURE_TYPE_P && n == 3 && v->field_mode) {
        int same_count = 0, opp_count = 0, k;
        int chosen_mv[2][4][2], f;
        int tx, ty;
        for (k = 0; k < 4; k++) {
            f = v->mv_f[0][s->block_index[k] + v->blocks_off];
            chosen_mv[f][f ? opp_count : same_count][0] = s->mv[0][k][0];
            chosen_mv[f][f ? opp_count : same_count][1] = s->mv[0][k][1];
            opp_count  += f;
            same_count += 1 - f;
        }
        f = opp_count > same_count;
        switch (f ? opp_count : same_count) {
        case 4:
            tx = median4(chosen_mv[f][0][0], chosen_mv[f][1][0],
                         chosen_mv[f][2][0], chosen_mv[f][3][0]);
            ty = median4(chosen_mv[f][0][1], chosen_mv[f][1][1],
                         chosen_mv[f][2][1], chosen_mv[f][3][1]);
            break;
        case 3:
            tx = mid_pred(chosen_mv[f][0][0], chosen_mv[f][1][0], chosen_mv[f][2][0]);
            ty = mid_pred(chosen_mv[f][0][1], chosen_mv[f][1][1], chosen_mv[f][2][1]);
            break;
        case 2:
            tx = (chosen_mv[f][0][0] + chosen_mv[f][1][0]) / 2;
            ty = (chosen_mv[f][0][1] + chosen_mv[f][1][1]) / 2;
            break;
        }
        s->current_picture.f.motion_val[1][s->block_index[0] + v->blocks_off][0] = tx;
        s->current_picture.f.motion_val[1][s->block_index[0] + v->blocks_off][1] = ty;
        for (k = 0; k < 4; k++)
            v->mv_f[1][s->block_index[k] + v->blocks_off] = f;
    }

    if (v->fcm == ILACE_FRAME) {  // not sure if needed for other types of picture
        int qx, qy;
        int width  = s->avctx->coded_width;
        int height = s->avctx->coded_height >> 1;
        qx = (s->mb_x * 16) + (mx >> 2);
        qy = (s->mb_y *  8) + (my >> 3);

        if (qx < -17)
            mx -= 4 * (qx + 17);
        else if (qx > width)
            mx -= 4 * (qx - width);
        if (qy < -18)
            my -= 8 * (qy + 18);
        else if (qy > height + 1)
            my -= 8 * (qy - height - 1);
    }

    if ((v->fcm == ILACE_FRAME) && fieldmv)
        off = ((n > 1) ? s->linesize : 0) + (n & 1) * 8;
    else
        off = s->linesize * 4 * (n & 2) + (n & 1) * 8;
    if (v->field_mode && v->second_field)
        off += s->current_picture_ptr->f.linesize[0];

    src_x = s->mb_x * 16 + (n & 1) * 8 + (mx >> 2);
    if (!fieldmv)
        src_y = s->mb_y * 16 + (n & 2) * 4 + (my >> 2);
    else
        src_y = s->mb_y * 16 + ((n > 1) ? 1 : 0) + (my >> 2);

    if (v->profile != PROFILE_ADVANCED) {
        src_x = av_clip(src_x, -16, s->mb_width  * 16);
        src_y = av_clip(src_y, -16, s->mb_height * 16);
    } else {
        src_x = av_clip(src_x, -17, s->avctx->coded_width);
        if (v->fcm == ILACE_FRAME) {
            if (src_y & 1)
                src_y = av_clip(src_y, -17, s->avctx->coded_height + 1);
            else
                src_y = av_clip(src_y, -18, s->avctx->coded_height);
        } else {
            src_y = av_clip(src_y, -18, s->avctx->coded_height + 1);
        }
    }

    srcY += src_y * s->linesize + src_x;
    if (v->field_mode && v->ref_field_type[dir])
        srcY += s->current_picture_ptr->f.linesize[0];

    if (fieldmv && !(src_y & 1))
        v_edge_pos--;
    if (fieldmv && (src_y & 1) && src_y < 4)
        src_y--;
    if (v->rangeredfrm || (v->mv_mode == MV_PMODE_INTENSITY_COMP)
        || s->h_edge_pos < 13 || v_edge_pos < 23
        || (unsigned)(src_x - s->mspel) > s->h_edge_pos - (mx & 3) - 8 - s->mspel * 2
        || (unsigned)(src_y - (s->mspel << fieldmv)) > v_edge_pos - (my & 3) - ((8 + s->mspel * 2) << fieldmv)) {
        srcY -= s->mspel * (1 + (s->linesize << fieldmv));
        /* check emulate edge stride and offset */
        s->dsp.emulated_edge_mc(s->edge_emu_buffer, srcY, s->linesize,
                                9 + s->mspel * 2, (9 + s->mspel * 2) << fieldmv,
                                src_x - s->mspel, src_y - (s->mspel << fieldmv),
                                s->h_edge_pos, v_edge_pos);
        srcY = s->edge_emu_buffer;
        /* if we deal with range reduction we need to scale source blocks */
        if (v->rangeredfrm) {
            int i, j;
            uint8_t *src;

            src = srcY;
            for (j = 0; j < 9 + s->mspel * 2; j++) {
                for (i = 0; i < 9 + s->mspel * 2; i++)
                    src[i] = ((src[i] - 128) >> 1) + 128;
                src += s->linesize << fieldmv;
            }
        }
        /* if we deal with intensity compensation we need to scale source blocks */
        if (v->mv_mode == MV_PMODE_INTENSITY_COMP) {
            int i, j;
            uint8_t *src;

            src = srcY;
            for (j = 0; j < 9 + s->mspel * 2; j++) {
                for (i = 0; i < 9 + s->mspel * 2; i++)
                    src[i] = v->luty[src[i]];
                src += s->linesize << fieldmv;
            }
        }
        srcY += s->mspel * (1 + (s->linesize << fieldmv));
    }

    if (s->mspel) {
        dxy = ((my & 3) << 2) | (mx & 3);
        v->vc1dsp.put_vc1_mspel_pixels_tab[dxy](s->dest[0] + off, srcY, s->linesize << fieldmv, v->rnd);
    } else { // hpel mc - always used for luma
        dxy = (my & 2) | ((mx & 2) >> 1);
        if (!v->rnd)
            dsp->put_pixels_tab[1][dxy](s->dest[0] + off, srcY, s->linesize, 8);
        else
            dsp->put_no_rnd_pixels_tab[1][dxy](s->dest[0] + off, srcY, s->linesize, 8);
    }
}

static av_always_inline int get_chroma_mv(int *mvx, int *mvy, int *a, int flag, int *tx, int *ty)
{
    int idx, i;
    static const int count[16] = { 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};

    idx =  ((a[3] != flag) << 3)
         | ((a[2] != flag) << 2)
         | ((a[1] != flag) << 1)
         |  (a[0] != flag);
    if (!idx) {
        *tx = median4(mvx[0], mvx[1], mvx[2], mvx[3]);
        *ty = median4(mvy[0], mvy[1], mvy[2], mvy[3]);
        return 4;
    } else if (count[idx] == 1) {
        switch (idx) {
        case 0x1:
            *tx = mid_pred(mvx[1], mvx[2], mvx[3]);
            *ty = mid_pred(mvy[1], mvy[2], mvy[3]);
            return 3;
        case 0x2:
            *tx = mid_pred(mvx[0], mvx[2], mvx[3]);
            *ty = mid_pred(mvy[0], mvy[2], mvy[3]);
            return 3;
        case 0x4:
            *tx = mid_pred(mvx[0], mvx[1], mvx[3]);
            *ty = mid_pred(mvy[0], mvy[1], mvy[3]);
            return 3;
        case 0x8:
            *tx = mid_pred(mvx[0], mvx[1], mvx[2]);
            *ty = mid_pred(mvy[0], mvy[1], mvy[2]);
            return 3;
        }
    } else if (count[idx] == 2) {
        int t1 = 0, t2 = 0;
        for (i = 0; i < 3; i++)
            if (!a[i]) {
                t1 = i;
                break;
            }
        for (i = t1 + 1; i < 4; i++)
            if (!a[i]) {
                t2 = i;
                break;
            }
        *tx = (mvx[t1] + mvx[t2]) / 2;
        *ty = (mvy[t1] + mvy[t2]) / 2;
        return 2;
    } else {
        return 0;
    }
    return -1;
}

/** Do motion compensation for 4-MV macroblock - both chroma blocks
 */
static void vc1_mc_4mv_chroma(VC1Context *v, int dir)
{
    MpegEncContext *s = &v->s;
    DSPContext *dsp   = &v->s.dsp;
    uint8_t *srcU, *srcV;
    int uvmx, uvmy, uvsrc_x, uvsrc_y;
    int k, tx = 0, ty = 0;
    int mvx[4], mvy[4], intra[4], mv_f[4];
    int valid_count;
    int chroma_ref_type = v->cur_field_type, off = 0;
    int v_edge_pos = s->v_edge_pos >> v->field_mode;

    if (!v->field_mode && !v->s.last_picture.f.data[0])
        return;
    if (s->flags & CODEC_FLAG_GRAY)
        return;

    for (k = 0; k < 4; k++) {
        mvx[k] = s->mv[dir][k][0];
        mvy[k] = s->mv[dir][k][1];
        intra[k] = v->mb_type[0][s->block_index[k]];
        if (v->field_mode)
            mv_f[k] = v->mv_f[dir][s->block_index[k] + v->blocks_off];
    }

    /* calculate chroma MV vector from four luma MVs */
    if (!v->field_mode || (v->field_mode && !v->numref)) {
        valid_count = get_chroma_mv(mvx, mvy, intra, 0, &tx, &ty);
        if (!valid_count) {
            s->current_picture.f.motion_val[1][s->block_index[0] + v->blocks_off][0] = 0;
            s->current_picture.f.motion_val[1][s->block_index[0] + v->blocks_off][1] = 0;
            v->luma_mv[s->mb_x][0] = v->luma_mv[s->mb_x][1] = 0;
            return; //no need to do MC for intra blocks
        }
    } else {
        int dominant = 0;
        if (mv_f[0] + mv_f[1] + mv_f[2] + mv_f[3] > 2)
            dominant = 1;
        valid_count = get_chroma_mv(mvx, mvy, mv_f, dominant, &tx, &ty);
        if (dominant)
            chroma_ref_type = !v->cur_field_type;
    }
    if (v->field_mode && chroma_ref_type == 1 && v->cur_field_type == 1 && !v->s.last_picture.f.data[0])
        return;
    s->current_picture.f.motion_val[1][s->block_index[0] + v->blocks_off][0] = tx;
    s->current_picture.f.motion_val[1][s->block_index[0] + v->blocks_off][1] = ty;
    uvmx = (tx + ((tx & 3) == 3)) >> 1;
    uvmy = (ty + ((ty & 3) == 3)) >> 1;

    v->luma_mv[s->mb_x][0] = uvmx;
    v->luma_mv[s->mb_x][1] = uvmy;

    if (v->fastuvmc) {
        uvmx = uvmx + ((uvmx < 0) ? (uvmx & 1) : -(uvmx & 1));
        uvmy = uvmy + ((uvmy < 0) ? (uvmy & 1) : -(uvmy & 1));
    }
    // Field conversion bias
    if (v->cur_field_type != chroma_ref_type)
        uvmy += 2 - 4 * chroma_ref_type;

    uvsrc_x = s->mb_x * 8 + (uvmx >> 2);
    uvsrc_y = s->mb_y * 8 + (uvmy >> 2);

    if (v->profile != PROFILE_ADVANCED) {
        uvsrc_x = av_clip(uvsrc_x, -8, s->mb_width  * 8);
        uvsrc_y = av_clip(uvsrc_y, -8, s->mb_height * 8);
    } else {
        uvsrc_x = av_clip(uvsrc_x, -8, s->avctx->coded_width  >> 1);
        uvsrc_y = av_clip(uvsrc_y, -8, s->avctx->coded_height >> 1);
    }

    if (!dir) {
        if (v->field_mode) {
            if ((v->cur_field_type != chroma_ref_type) && v->cur_field_type) {
                srcU = s->current_picture.f.data[1] + uvsrc_y * s->uvlinesize + uvsrc_x;
                srcV = s->current_picture.f.data[2] + uvsrc_y * s->uvlinesize + uvsrc_x;
            } else {
                srcU = s->last_picture.f.data[1] + uvsrc_y * s->uvlinesize + uvsrc_x;
                srcV = s->last_picture.f.data[2] + uvsrc_y * s->uvlinesize + uvsrc_x;
            }
        } else {
            srcU = s->last_picture.f.data[1] + uvsrc_y * s->uvlinesize + uvsrc_x;
            srcV = s->last_picture.f.data[2] + uvsrc_y * s->uvlinesize + uvsrc_x;
        }
    } else {
        srcU = s->next_picture.f.data[1] + uvsrc_y * s->uvlinesize + uvsrc_x;
        srcV = s->next_picture.f.data[2] + uvsrc_y * s->uvlinesize + uvsrc_x;
    }

    if (v->field_mode) {
        if (chroma_ref_type) {
            srcU += s->current_picture_ptr->f.linesize[1];
            srcV += s->current_picture_ptr->f.linesize[2];
        }
        off = v->second_field ? s->current_picture_ptr->f.linesize[1] : 0;
    }

    if (v->rangeredfrm || (v->mv_mode == MV_PMODE_INTENSITY_COMP)
        || s->h_edge_pos < 18 || v_edge_pos < 18
        || (unsigned)uvsrc_x > (s->h_edge_pos >> 1) - 9
        || (unsigned)uvsrc_y > (v_edge_pos    >> 1) - 9) {
        s->dsp.emulated_edge_mc(s->edge_emu_buffer     , srcU, s->uvlinesize,
                                8 + 1, 8 + 1, uvsrc_x, uvsrc_y,
                                s->h_edge_pos >> 1, v_edge_pos >> 1);
        s->dsp.emulated_edge_mc(s->edge_emu_buffer + 16, srcV, s->uvlinesize,
                                8 + 1, 8 + 1, uvsrc_x, uvsrc_y,
                                s->h_edge_pos >> 1, v_edge_pos >> 1);
        srcU = s->edge_emu_buffer;
        srcV = s->edge_emu_buffer + 16;

        /* if we deal with range reduction we need to scale source blocks */
        if (v->rangeredfrm) {
            int i, j;
            uint8_t *src, *src2;

            src  = srcU;
            src2 = srcV;
            for (j = 0; j < 9; j++) {
                for (i = 0; i < 9; i++) {
                    src[i]  = ((src[i]  - 128) >> 1) + 128;
                    src2[i] = ((src2[i] - 128) >> 1) + 128;
                }
                src  += s->uvlinesize;
                src2 += s->uvlinesize;
            }
        }
        /* if we deal with intensity compensation we need to scale source blocks */
        if (v->mv_mode == MV_PMODE_INTENSITY_COMP) {
            int i, j;
            uint8_t *src, *src2;

            src  = srcU;
            src2 = srcV;
            for (j = 0; j < 9; j++) {
                for (i = 0; i < 9; i++) {
                    src[i]  = v->lutuv[src[i]];
                    src2[i] = v->lutuv[src2[i]];
                }
                src  += s->uvlinesize;
                src2 += s->uvlinesize;
            }
        }
    }

    /* Chroma MC always uses qpel bilinear */
    uvmx = (uvmx & 3) << 1;
    uvmy = (uvmy & 3) << 1;
    if (!v->rnd) {
        dsp->put_h264_chroma_pixels_tab[0](s->dest[1] + off, srcU, s->uvlinesize, 8, uvmx, uvmy);
        dsp->put_h264_chroma_pixels_tab[0](s->dest[2] + off, srcV, s->uvlinesize, 8, uvmx, uvmy);
    } else {
        v->vc1dsp.put_no_rnd_vc1_chroma_pixels_tab[0](s->dest[1] + off, srcU, s->uvlinesize, 8, uvmx, uvmy);
        v->vc1dsp.put_no_rnd_vc1_chroma_pixels_tab[0](s->dest[2] + off, srcV, s->uvlinesize, 8, uvmx, uvmy);
    }
}

/** Do motion compensation for 4-MV field chroma macroblock (both U and V)
 */
static void vc1_mc_4mv_chroma4(VC1Context *v)
{
    MpegEncContext *s = &v->s;
    DSPContext *dsp = &v->s.dsp;
    uint8_t *srcU, *srcV;
    int uvsrc_x, uvsrc_y;
    int uvmx_field[4], uvmy_field[4];
    int i, off, tx, ty;
    int fieldmv = v->blk_mv_type[s->block_index[0]];
    static const int s_rndtblfield[16] = { 0, 0, 1, 2, 4, 4, 5, 6, 2, 2, 3, 8, 6, 6, 7, 12 };
    int v_dist = fieldmv ? 1 : 4; // vertical offset for lower sub-blocks
    int v_edge_pos = s->v_edge_pos >> 1;

    if (!v->s.last_picture.f.data[0])
        return;
    if (s->flags & CODEC_FLAG_GRAY)
        return;

    for (i = 0; i < 4; i++) {
        tx = s->mv[0][i][0];
        uvmx_field[i] = (tx + ((tx & 3) == 3)) >> 1;
        ty = s->mv[0][i][1];
        if (fieldmv)
            uvmy_field[i] = (ty >> 4) * 8 + s_rndtblfield[ty & 0xF];
        else
            uvmy_field[i] = (ty + ((ty & 3) == 3)) >> 1;
    }

    for (i = 0; i < 4; i++) {
        off = (i & 1) * 4 + ((i & 2) ? v_dist * s->uvlinesize : 0);
        uvsrc_x = s->mb_x * 8 +  (i & 1) * 4           + (uvmx_field[i] >> 2);
        uvsrc_y = s->mb_y * 8 + ((i & 2) ? v_dist : 0) + (uvmy_field[i] >> 2);
        // FIXME: implement proper pull-back (see vc1cropmv.c, vc1CROPMV_ChromaPullBack())
        uvsrc_x = av_clip(uvsrc_x, -8, s->avctx->coded_width  >> 1);
        uvsrc_y = av_clip(uvsrc_y, -8, s->avctx->coded_height >> 1);
        srcU = s->last_picture.f.data[1] + uvsrc_y * s->uvlinesize + uvsrc_x;
        srcV = s->last_picture.f.data[2] + uvsrc_y * s->uvlinesize + uvsrc_x;
        uvmx_field[i] = (uvmx_field[i] & 3) << 1;
        uvmy_field[i] = (uvmy_field[i] & 3) << 1;

        if (fieldmv && !(uvsrc_y & 1))
            v_edge_pos--;
        if (fieldmv && (uvsrc_y & 1) && uvsrc_y < 2)
            uvsrc_y--;
        if ((v->mv_mode == MV_PMODE_INTENSITY_COMP)
            || s->h_edge_pos < 10 || v_edge_pos < (5 << fieldmv)
            || (unsigned)uvsrc_x > (s->h_edge_pos >> 1) - 5
            || (unsigned)uvsrc_y > v_edge_pos - (5 << fieldmv)) {
            s->dsp.emulated_edge_mc(s->edge_emu_buffer, srcU, s->uvlinesize,
                                    5, (5 << fieldmv), uvsrc_x, uvsrc_y,
                                    s->h_edge_pos >> 1, v_edge_pos);
            s->dsp.emulated_edge_mc(s->edge_emu_buffer + 16, srcV, s->uvlinesize,
                                    5, (5 << fieldmv), uvsrc_x, uvsrc_y,
                                    s->h_edge_pos >> 1, v_edge_pos);
            srcU = s->edge_emu_buffer;
            srcV = s->edge_emu_buffer + 16;

            /* if we deal with intensity compensation we need to scale source blocks */
            if (v->mv_mode == MV_PMODE_INTENSITY_COMP) {
                int i, j;
                uint8_t *src, *src2;

                src  = srcU;
                src2 = srcV;
                for (j = 0; j < 5; j++) {
                    for (i = 0; i < 5; i++) {
                        src[i]  = v->lutuv[src[i]];
                        src2[i] = v->lutuv[src2[i]];
                    }
                    src  += s->uvlinesize << 1;
                    src2 += s->uvlinesize << 1;
                }
            }
        }
        if (!v->rnd) {
            dsp->put_h264_chroma_pixels_tab[1](s->dest[1] + off, srcU, s->uvlinesize << fieldmv, 4, uvmx_field[i], uvmy_field[i]);
            dsp->put_h264_chroma_pixels_tab[1](s->dest[2] + off, srcV, s->uvlinesize << fieldmv, 4, uvmx_field[i], uvmy_field[i]);
        } else {
            v->vc1dsp.put_no_rnd_vc1_chroma_pixels_tab[1](s->dest[1] + off, srcU, s->uvlinesize << fieldmv, 4, uvmx_field[i], uvmy_field[i]);
            v->vc1dsp.put_no_rnd_vc1_chroma_pixels_tab[1](s->dest[2] + off, srcV, s->uvlinesize << fieldmv, 4, uvmx_field[i], uvmy_field[i]);
        }
    }
}

/***********************************************************************/
/**
 * @name VC-1 Block-level functions
 * @see 7.1.4, p91 and 8.1.1.7, p(1)04
 * @{
 */

/**
 * @def GET_MQUANT
 * @brief Get macroblock-level quantizer scale
 */
#define GET_MQUANT()                                           \
    if (v->dquantfrm) {                                        \
        int edges = 0;                                         \
        if (v->dqprofile == DQPROFILE_ALL_MBS) {               \
            if (v->dqbilevel) {                                \
                mquant = (get_bits1(gb)) ? v->altpq : v->pq;   \
            } else {                                           \
                mqdiff = get_bits(gb, 3);                      \
                if (mqdiff != 7)                               \
                    mquant = v->pq + mqdiff;                   \
                else                                           \
                    mquant = get_bits(gb, 5);                  \
            }                                                  \
        }                                                      \
        if (v->dqprofile == DQPROFILE_SINGLE_EDGE)             \
            edges = 1 << v->dqsbedge;                          \
        else if (v->dqprofile == DQPROFILE_DOUBLE_EDGES)       \
            edges = (3 << v->dqsbedge) % 15;                   \
        else if (v->dqprofile == DQPROFILE_FOUR_EDGES)         \
            edges = 15;                                        \
        if ((edges&1) && !s->mb_x)                             \
            mquant = v->altpq;                                 \
        if ((edges&2) && s->first_slice_line)                  \
            mquant = v->altpq;                                 \
        if ((edges&4) && s->mb_x == (s->mb_width - 1))         \
            mquant = v->altpq;                                 \
        if ((edges&8) && s->mb_y == (s->mb_height - 1))        \
            mquant = v->altpq;                                 \
    }

/**
 * @def GET_MVDATA(_dmv_x, _dmv_y)
 * @brief Get MV differentials
 * @see MVDATA decoding from 8.3.5.2, p(1)20
 * @param _dmv_x Horizontal differential for decoded MV
 * @param _dmv_y Vertical differential for decoded MV
 */
#define GET_MVDATA(_dmv_x, _dmv_y)                                      \
    index = 1 + get_vlc2(gb, ff_vc1_mv_diff_vlc[s->mv_table_index].table, \
                         VC1_MV_DIFF_VLC_BITS, 2);                      \
    if (index > 36) {                                                   \
        mb_has_coeffs = 1;                                              \
        index -= 37;                                                    \
    } else                                                              \
        mb_has_coeffs = 0;                                              \
    s->mb_intra = 0;                                                    \
    if (!index) {                                                       \
        _dmv_x = _dmv_y = 0;                                            \
    } else if (index == 35) {                                           \
        _dmv_x = get_bits(gb, v->k_x - 1 + s->quarter_sample);          \
        _dmv_y = get_bits(gb, v->k_y - 1 + s->quarter_sample);          \
    } else if (index == 36) {                                           \
        _dmv_x = 0;                                                     \
        _dmv_y = 0;                                                     \
        s->mb_intra = 1;                                                \
    } else {                                                            \
        index1 = index % 6;                                             \
        if (!s->quarter_sample && index1 == 5) val = 1;                 \
        else                                   val = 0;                 \
        if (size_table[index1] - val > 0)                               \
            val = get_bits(gb, size_table[index1] - val);               \
        else                                   val = 0;                 \
        sign = 0 - (val&1);                                             \
        _dmv_x = (sign ^ ((val>>1) + offset_table[index1])) - sign;     \
                                                                        \
        index1 = index / 6;                                             \
        if (!s->quarter_sample && index1 == 5) val = 1;                 \
        else                                   val = 0;                 \
        if (size_table[index1] - val > 0)                               \
            val = get_bits(gb, size_table[index1] - val);               \
        else                                   val = 0;                 \
        sign = 0 - (val & 1);                                           \
        _dmv_y = (sign ^ ((val >> 1) + offset_table[index1])) - sign;   \
    }

static av_always_inline void get_mvdata_interlaced(VC1Context *v, int *dmv_x,
                                                   int *dmv_y, int *pred_flag)
{
    int index, index1;
    int extend_x = 0, extend_y = 0;
    GetBitContext *gb = &v->s.gb;
    int bits, esc;
    int val, sign;
    const int* offs_tab;

    if (v->numref) {
        bits = VC1_2REF_MVDATA_VLC_BITS;
        esc  = 125;
    } else {
        bits = VC1_1REF_MVDATA_VLC_BITS;
        esc  = 71;
    }
    switch (v->dmvrange) {
    case 1:
        extend_x = 1;
        break;
    case 2:
        extend_y = 1;
        break;
    case 3:
        extend_x = extend_y = 1;
        break;
    }
    index = get_vlc2(gb, v->imv_vlc->table, bits, 3);
    if (index == esc) {
        *dmv_x = get_bits(gb, v->k_x);
        *dmv_y = get_bits(gb, v->k_y);
        if (v->numref) {
            *pred_flag = *dmv_y & 1;
            *dmv_y     = (*dmv_y + *pred_flag) >> 1;
        }
    }
    else {
        if (extend_x)
            offs_tab = offset_table2;
        else
            offs_tab = offset_table1;
        index1 = (index + 1) % 9;
        if (index1 != 0) {
            val    = get_bits(gb, index1 + extend_x);
            sign   = 0 -(val & 1);
            *dmv_x = (sign ^ ((val >> 1) + offs_tab[index1])) - sign;
        } else
            *dmv_x = 0;
        if (extend_y)
            offs_tab = offset_table2;
        else
            offs_tab = offset_table1;
        index1 = (index + 1) / 9;
        if (index1 > v->numref) {
            val    = get_bits(gb, (index1 + (extend_y << v->numref)) >> v->numref);
            sign   = 0 - (val & 1);
            *dmv_y = (sign ^ ((val >> 1) + offs_tab[index1 >> v->numref])) - sign;
        } else
            *dmv_y = 0;
        if (v->numref)
            *pred_flag = index1 & 1;
    }
}

static av_always_inline int scaleforsame_x(VC1Context *v, int n /* MV */, int dir)
{
    int scaledvalue, refdist;
    int scalesame1, scalesame2;
    int scalezone1_x, zone1offset_x;
    int table_index = dir ^ v->second_field;

    if (v->s.pict_type != AV_PICTURE_TYPE_B)
        refdist = v->refdist;
    else
        refdist = dir ? v->brfd : v->frfd;
    if (refdist > 3)
        refdist = 3;
    scalesame1    = vc1_field_mvpred_scales[table_index][1][refdist];
    scalesame2    = vc1_field_mvpred_scales[table_index][2][refdist];
    scalezone1_x  = vc1_field_mvpred_scales[table_index][3][refdist];
    zone1offset_x = vc1_field_mvpred_scales[table_index][5][refdist];

    if (FFABS(n) > 255)
        scaledvalue = n;
    else {
        if (FFABS(n) < scalezone1_x)
            scaledvalue = (n * scalesame1) >> 8;
        else {
            if (n < 0)
                scaledvalue = ((n * scalesame2) >> 8) - zone1offset_x;
            else
                scaledvalue = ((n * scalesame2) >> 8) + zone1offset_x;
        }
    }
    return av_clip(scaledvalue, -v->range_x, v->range_x - 1);
}

static av_always_inline int scaleforsame_y(VC1Context *v, int i, int n /* MV */, int dir)
{
    int scaledvalue, refdist;
    int scalesame1, scalesame2;
    int scalezone1_y, zone1offset_y;
    int table_index = dir ^ v->second_field;

    if (v->s.pict_type != AV_PICTURE_TYPE_B)
        refdist = v->refdist;
    else
        refdist = dir ? v->brfd : v->frfd;
    if (refdist > 3)
        refdist = 3;
    scalesame1    = vc1_field_mvpred_scales[table_index][1][refdist];
    scalesame2    = vc1_field_mvpred_scales[table_index][2][refdist];
    scalezone1_y  = vc1_field_mvpred_scales[table_index][4][refdist];
    zone1offset_y = vc1_field_mvpred_scales[table_index][6][refdist];

    if (FFABS(n) > 63)
        scaledvalue = n;
    else {
        if (FFABS(n) < scalezone1_y)
            scaledvalue = (n * scalesame1) >> 8;
        else {
            if (n < 0)
                scaledvalue = ((n * scalesame2) >> 8) - zone1offset_y;
            else
                scaledvalue = ((n * scalesame2) >> 8) + zone1offset_y;
        }
    }

    if (v->cur_field_type && !v->ref_field_type[dir])
        return av_clip(scaledvalue, -v->range_y / 2 + 1, v->range_y / 2);
    else
        return av_clip(scaledvalue, -v->range_y / 2, v->range_y / 2 - 1);
}

static av_always_inline int scaleforopp_x(VC1Context *v, int n /* MV */)
{
    int scalezone1_x, zone1offset_x;
    int scaleopp1, scaleopp2, brfd;
    int scaledvalue;

    brfd = FFMIN(v->brfd, 3);
    scalezone1_x  = vc1_b_field_mvpred_scales[3][brfd];
    zone1offset_x = vc1_b_field_mvpred_scales[5][brfd];
    scaleopp1     = vc1_b_field_mvpred_scales[1][brfd];
    scaleopp2     = vc1_b_field_mvpred_scales[2][brfd];

    if (FFABS(n) > 255)
        scaledvalue = n;
    else {
        if (FFABS(n) < scalezone1_x)
            scaledvalue = (n * scaleopp1) >> 8;
        else {
            if (n < 0)
                scaledvalue = ((n * scaleopp2) >> 8) - zone1offset_x;
            else
                scaledvalue = ((n * scaleopp2) >> 8) + zone1offset_x;
        }
    }
    return av_clip(scaledvalue, -v->range_x, v->range_x - 1);
}

static av_always_inline int scaleforopp_y(VC1Context *v, int n /* MV */, int dir)
{
    int scalezone1_y, zone1offset_y;
    int scaleopp1, scaleopp2, brfd;
    int scaledvalue;

    brfd = FFMIN(v->brfd, 3);
    scalezone1_y  = vc1_b_field_mvpred_scales[4][brfd];
    zone1offset_y = vc1_b_field_mvpred_scales[6][brfd];
    scaleopp1     = vc1_b_field_mvpred_scales[1][brfd];
    scaleopp2     = vc1_b_field_mvpred_scales[2][brfd];

    if (FFABS(n) > 63)
        scaledvalue = n;
    else {
        if (FFABS(n) < scalezone1_y)
            scaledvalue = (n * scaleopp1) >> 8;
        else {
            if (n < 0)
                scaledvalue = ((n * scaleopp2) >> 8) - zone1offset_y;
            else
                scaledvalue = ((n * scaleopp2) >> 8) + zone1offset_y;
        }
    }
    if (v->cur_field_type && !v->ref_field_type[dir]) {
        return av_clip(scaledvalue, -v->range_y / 2 + 1, v->range_y / 2);
    } else {
        return av_clip(scaledvalue, -v->range_y / 2, v->range_y / 2 - 1);
    }
}

static av_always_inline int scaleforsame(VC1Context *v, int i, int n /* MV */,
                                         int dim, int dir)
{
    int brfd, scalesame;
    int hpel = 1 - v->s.quarter_sample;

    n >>= hpel;
    if (v->s.pict_type != AV_PICTURE_TYPE_B || v->second_field || !dir) {
        if (dim)
            n = scaleforsame_y(v, i, n, dir) << hpel;
        else
            n = scaleforsame_x(v, n, dir) << hpel;
        return n;
    }
    brfd      = FFMIN(v->brfd, 3);
    scalesame = vc1_b_field_mvpred_scales[0][brfd];

    n = (n * scalesame >> 8) << hpel;
    return n;
}

static av_always_inline int scaleforopp(VC1Context *v, int n /* MV */,
                                        int dim, int dir)
{
    int refdist, scaleopp;
    int hpel = 1 - v->s.quarter_sample;

    n >>= hpel;
    if (v->s.pict_type == AV_PICTURE_TYPE_B && !v->second_field && dir == 1) {
        if (dim)
            n = scaleforopp_y(v, n, dir) << hpel;
        else
            n = scaleforopp_x(v, n) << hpel;
        return n;
    }
    if (v->s.pict_type != AV_PICTURE_TYPE_B)
        refdist = FFMIN(v->refdist, 3);
    else
        refdist = dir ? v->brfd : v->frfd;
    scaleopp = vc1_field_mvpred_scales[dir ^ v->second_field][0][refdist];

    n = (n * scaleopp >> 8) << hpel;
    return n;
}

/** Predict and set motion vector
 */
static inline void vc1_pred_mv(VC1Context *v, int n, int dmv_x, int dmv_y,
                               int mv1, int r_x, int r_y, uint8_t* is_intra,
                               int pred_flag, int dir)
{
    MpegEncContext *s = &v->s;
    int xy, wrap, off = 0;
    int16_t *A, *B, *C;
    int px, py;
    int sum;
    int mixedmv_pic, num_samefield = 0, num_oppfield = 0;
    int opposit, a_f, b_f, c_f;
    int16_t field_predA[2];
    int16_t field_predB[2];
    int16_t field_predC[2];
    int a_valid, b_valid, c_valid;
    int hybridmv_thresh, y_bias = 0;

    if (v->mv_mode == MV_PMODE_MIXED_MV ||
        ((v->mv_mode == MV_PMODE_INTENSITY_COMP) && (v->mv_mode2 == MV_PMODE_MIXED_MV)))
        mixedmv_pic = 1;
    else
        mixedmv_pic = 0;
    /* scale MV difference to be quad-pel */
    dmv_x <<= 1 - s->quarter_sample;
    dmv_y <<= 1 - s->quarter_sample;

    wrap = s->b8_stride;
    xy   = s->block_index[n];

    if (s->mb_intra) {
        s->mv[0][n][0] = s->current_picture.f.motion_val[0][xy + v->blocks_off][0] = 0;
        s->mv[0][n][1] = s->current_picture.f.motion_val[0][xy + v->blocks_off][1] = 0;
        s->current_picture.f.motion_val[1][xy + v->blocks_off][0] = 0;
        s->current_picture.f.motion_val[1][xy + v->blocks_off][1] = 0;
        if (mv1) { /* duplicate motion data for 1-MV block */
            s->current_picture.f.motion_val[0][xy + 1 + v->blocks_off][0]        = 0;
            s->current_picture.f.motion_val[0][xy + 1 + v->blocks_off][1]        = 0;
            s->current_picture.f.motion_val[0][xy + wrap + v->blocks_off][0]     = 0;
            s->current_picture.f.motion_val[0][xy + wrap + v->blocks_off][1]     = 0;
            s->current_picture.f.motion_val[0][xy + wrap + 1 + v->blocks_off][0] = 0;
            s->current_picture.f.motion_val[0][xy + wrap + 1 + v->blocks_off][1] = 0;
            v->luma_mv[s->mb_x][0] = v->luma_mv[s->mb_x][1] = 0;
            s->current_picture.f.motion_val[1][xy + 1 + v->blocks_off][0]        = 0;
            s->current_picture.f.motion_val[1][xy + 1 + v->blocks_off][1]        = 0;
            s->current_picture.f.motion_val[1][xy + wrap][0]                     = 0;
            s->current_picture.f.motion_val[1][xy + wrap + v->blocks_off][1]     = 0;
            s->current_picture.f.motion_val[1][xy + wrap + 1 + v->blocks_off][0] = 0;
            s->current_picture.f.motion_val[1][xy + wrap + 1 + v->blocks_off][1] = 0;
        }
        return;
    }

    C = s->current_picture.f.motion_val[dir][xy -    1 + v->blocks_off];
    A = s->current_picture.f.motion_val[dir][xy - wrap + v->blocks_off];
    if (mv1) {
        if (v->field_mode && mixedmv_pic)
            off = (s->mb_x == (s->mb_width - 1)) ? -2 : 2;
        else
            off = (s->mb_x == (s->mb_width - 1)) ? -1 : 2;
    } else {
        //in 4-MV mode different blocks have different B predictor position
        switch (n) {
        case 0:
            off = (s->mb_x > 0) ? -1 : 1;
            break;
        case 1:
            off = (s->mb_x == (s->mb_width - 1)) ? -1 : 1;
            break;
        case 2:
            off = 1;
            break;
        case 3:
            off = -1;
        }
    }
    B = s->current_picture.f.motion_val[dir][xy - wrap + off + v->blocks_off];

    a_valid = !s->first_slice_line || (n == 2 || n == 3);
    b_valid = a_valid && (s->mb_width > 1);
    c_valid = s->mb_x || (n == 1 || n == 3);
    if (v->field_mode) {
        a_valid = a_valid && !is_intra[xy - wrap];
        b_valid = b_valid && !is_intra[xy - wrap + off];
        c_valid = c_valid && !is_intra[xy - 1];
    }

    if (a_valid) {
        a_f = v->mv_f[dir][xy - wrap + v->blocks_off];
        num_oppfield  += a_f;
        num_samefield += 1 - a_f;
        field_predA[0] = A[0];
        field_predA[1] = A[1];
    } else {
        field_predA[0] = field_predA[1] = 0;
        a_f = 0;
    }
    if (b_valid) {
        b_f = v->mv_f[dir][xy - wrap + off + v->blocks_off];
        num_oppfield  += b_f;
        num_samefield += 1 - b_f;
        field_predB[0] = B[0];
        field_predB[1] = B[1];
    } else {
        field_predB[0] = field_predB[1] = 0;
        b_f = 0;
    }
    if (c_valid) {
        c_f = v->mv_f[dir][xy - 1 + v->blocks_off];
        num_oppfield  += c_f;
        num_samefield += 1 - c_f;
        field_predC[0] = C[0];
        field_predC[1] = C[1];
    } else {
        field_predC[0] = field_predC[1] = 0;
        c_f = 0;
    }

    if (v->field_mode) {
        if (num_samefield <= num_oppfield)
            opposit = 1 - pred_flag;
        else
            opposit = pred_flag;
    } else
        opposit = 0;
    if (opposit) {
        if (a_valid && !a_f) {
            field_predA[0] = scaleforopp(v, field_predA[0], 0, dir);
            field_predA[1] = scaleforopp(v, field_predA[1], 1, dir);
        }
        if (b_valid && !b_f) {
            field_predB[0] = scaleforopp(v, field_predB[0], 0, dir);
            field_predB[1] = scaleforopp(v, field_predB[1], 1, dir);
        }
        if (c_valid && !c_f) {
            field_predC[0] = scaleforopp(v, field_predC[0], 0, dir);
            field_predC[1] = scaleforopp(v, field_predC[1], 1, dir);
        }
        v->mv_f[dir][xy + v->blocks_off] = 1;
        v->ref_field_type[dir] = !v->cur_field_type;
    } else {
        if (a_valid && a_f) {
            field_predA[0] = scaleforsame(v, n, field_predA[0], 0, dir);
            field_predA[1] = scaleforsame(v, n, field_predA[1], 1, dir);
        }
        if (b_valid && b_f) {
            field_predB[0] = scaleforsame(v, n, field_predB[0], 0, dir);
            field_predB[1] = scaleforsame(v, n, field_predB[1], 1, dir);
        }
        if (c_valid && c_f) {
            field_predC[0] = scaleforsame(v, n, field_predC[0], 0, dir);
            field_predC[1] = scaleforsame(v, n, field_predC[1], 1, dir);
        }
        v->mv_f[dir][xy + v->blocks_off] = 0;
        v->ref_field_type[dir] = v->cur_field_type;
    }

    if (a_valid) {
        px = field_predA[0];
        py = field_predA[1];
    } else if (c_valid) {
        px = field_predC[0];
        py = field_predC[1];
    } else if (b_valid) {
        px = field_predB[0];
        py = field_predB[1];
    } else {
        px = 0;
        py = 0;
    }

    if (num_samefield + num_oppfield > 1) {
        px = mid_pred(field_predA[0], field_predB[0], field_predC[0]);
        py = mid_pred(field_predA[1], field_predB[1], field_predC[1]);
    }

    /* Pullback MV as specified in 8.3.5.3.4 */
    if (!v->field_mode) {
        int qx, qy, X, Y;
        qx = (s->mb_x << 6) + ((n == 1 || n == 3) ? 32 : 0);
        qy = (s->mb_y << 6) + ((n == 2 || n == 3) ? 32 : 0);
        X  = (s->mb_width  << 6) - 4;
        Y  = (s->mb_height << 6) - 4;
        if (mv1) {
            if (qx + px < -60) px = -60 - qx;
            if (qy + py < -60) py = -60 - qy;
        } else {
            if (qx + px < -28) px = -28 - qx;
            if (qy + py < -28) py = -28 - qy;
        }
        if (qx + px > X) px = X - qx;
        if (qy + py > Y) py = Y - qy;
    }

    if (!v->field_mode || s->pict_type != AV_PICTURE_TYPE_B) {
        /* Calculate hybrid prediction as specified in 8.3.5.3.5 (also 10.3.5.4.3.5) */
        hybridmv_thresh = 32;
        if (a_valid && c_valid) {
            if (is_intra[xy - wrap])
                sum = FFABS(px) + FFABS(py);
            else
                sum = FFABS(px - field_predA[0]) + FFABS(py - field_predA[1]);
            if (sum > hybridmv_thresh) {
                if (get_bits1(&s->gb)) {     // read HYBRIDPRED bit
                    px = field_predA[0];
                    py = field_predA[1];
                } else {
                    px = field_predC[0];
                    py = field_predC[1];
                }
            } else {
                if (is_intra[xy - 1])
                    sum = FFABS(px) + FFABS(py);
                else
                    sum = FFABS(px - field_predC[0]) + FFABS(py - field_predC[1]);
                if (sum > hybridmv_thresh) {
                    if (get_bits1(&s->gb)) {
                        px = field_predA[0];
                        py = field_predA[1];
                    } else {
                        px = field_predC[0];
                        py = field_predC[1];
                    }
                }
            }
        }
    }

    if (v->field_mode && !s->quarter_sample) {
        r_x <<= 1;
        r_y <<= 1;
    }
    if (v->field_mode && v->numref)
        r_y >>= 1;
    if (v->field_mode && v->cur_field_type && v->ref_field_type[dir] == 0)
        y_bias = 1;
    /* store MV using signed modulus of MV range defined in 4.11 */
    s->mv[dir][n][0] = s->current_picture.f.motion_val[dir][xy + v->blocks_off][0] = ((px + dmv_x + r_x) & ((r_x << 1) - 1)) - r_x;
    s->mv[dir][n][1] = s->current_picture.f.motion_val[dir][xy + v->blocks_off][1] = ((py + dmv_y + r_y - y_bias) & ((r_y << 1) - 1)) - r_y + y_bias;
    if (mv1) { /* duplicate motion data for 1-MV block */
        s->current_picture.f.motion_val[dir][xy +    1 +     v->blocks_off][0] = s->current_picture.f.motion_val[dir][xy + v->blocks_off][0];
        s->current_picture.f.motion_val[dir][xy +    1 +     v->blocks_off][1] = s->current_picture.f.motion_val[dir][xy + v->blocks_off][1];
        s->current_picture.f.motion_val[dir][xy + wrap +     v->blocks_off][0] = s->current_picture.f.motion_val[dir][xy + v->blocks_off][0];
        s->current_picture.f.motion_val[dir][xy + wrap +     v->blocks_off][1] = s->current_picture.f.motion_val[dir][xy + v->blocks_off][1];
        s->current_picture.f.motion_val[dir][xy + wrap + 1 + v->blocks_off][0] = s->current_picture.f.motion_val[dir][xy + v->blocks_off][0];
        s->current_picture.f.motion_val[dir][xy + wrap + 1 + v->blocks_off][1] = s->current_picture.f.motion_val[dir][xy + v->blocks_off][1];
        v->mv_f[dir][xy +    1 + v->blocks_off] = v->mv_f[dir][xy +            v->blocks_off];
        v->mv_f[dir][xy + wrap + v->blocks_off] = v->mv_f[dir][xy + wrap + 1 + v->blocks_off] = v->mv_f[dir][xy + v->blocks_off];
    }
}

/** Predict and set motion vector for interlaced frame picture MBs
 */
static inline void vc1_pred_mv_intfr(VC1Context *v, int n, int dmv_x, int dmv_y,
                                     int mvn, int r_x, int r_y, uint8_t* is_intra)
{
    MpegEncContext *s = &v->s;
    int xy, wrap, off = 0;
    int A[2], B[2], C[2];
    int px, py;
    int a_valid = 0, b_valid = 0, c_valid = 0;
    int field_a, field_b, field_c; // 0: same, 1: opposit
    int total_valid, num_samefield, num_oppfield;
    int pos_c, pos_b, n_adj;

    wrap = s->b8_stride;
    xy = s->block_index[n];

    if (s->mb_intra) {
        s->mv[0][n][0] = s->current_picture.f.motion_val[0][xy][0] = 0;
        s->mv[0][n][1] = s->current_picture.f.motion_val[0][xy][1] = 0;
        s->current_picture.f.motion_val[1][xy][0] = 0;
        s->current_picture.f.motion_val[1][xy][1] = 0;
        if (mvn == 1) { /* duplicate motion data for 1-MV block */
            s->current_picture.f.motion_val[0][xy + 1][0]        = 0;
            s->current_picture.f.motion_val[0][xy + 1][1]        = 0;
            s->current_picture.f.motion_val[0][xy + wrap][0]     = 0;
            s->current_picture.f.motion_val[0][xy + wrap][1]     = 0;
            s->current_picture.f.motion_val[0][xy + wrap + 1][0] = 0;
            s->current_picture.f.motion_val[0][xy + wrap + 1][1] = 0;
            v->luma_mv[s->mb_x][0] = v->luma_mv[s->mb_x][1] = 0;
            s->current_picture.f.motion_val[1][xy + 1][0]        = 0;
            s->current_picture.f.motion_val[1][xy + 1][1]        = 0;
            s->current_picture.f.motion_val[1][xy + wrap][0]     = 0;
            s->current_picture.f.motion_val[1][xy + wrap][1]     = 0;
            s->current_picture.f.motion_val[1][xy + wrap + 1][0] = 0;
            s->current_picture.f.motion_val[1][xy + wrap + 1][1] = 0;
        }
        return;
    }

    off = ((n == 0) || (n == 1)) ? 1 : -1;
    /* predict A */
    if (s->mb_x || (n == 1) || (n == 3)) {
        if ((v->blk_mv_type[xy]) // current block (MB) has a field MV
            || (!v->blk_mv_type[xy] && !v->blk_mv_type[xy - 1])) { // or both have frame MV
            A[0] = s->current_picture.f.motion_val[0][xy - 1][0];
            A[1] = s->current_picture.f.motion_val[0][xy - 1][1];
            a_valid = 1;
        } else { // current block has frame mv and cand. has field MV (so average)
            A[0] = (s->current_picture.f.motion_val[0][xy - 1][0]
                    + s->current_picture.f.motion_val[0][xy - 1 + off * wrap][0] + 1) >> 1;
            A[1] = (s->current_picture.f.motion_val[0][xy - 1][1]
                    + s->current_picture.f.motion_val[0][xy - 1 + off * wrap][1] + 1) >> 1;
            a_valid = 1;
        }
        if (!(n & 1) && v->is_intra[s->mb_x - 1]) {
            a_valid = 0;
            A[0] = A[1] = 0;
        }
    } else
        A[0] = A[1] = 0;
    /* Predict B and C */
    B[0] = B[1] = C[0] = C[1] = 0;
    if (n == 0 || n == 1 || v->blk_mv_type[xy]) {
        if (!s->first_slice_line) {
            if (!v->is_intra[s->mb_x - s->mb_stride]) {
                b_valid = 1;
                n_adj   = n | 2;
                pos_b   = s->block_index[n_adj] - 2 * wrap;
                if (v->blk_mv_type[pos_b] && v->blk_mv_type[xy]) {
                    n_adj = (n & 2) | (n & 1);
                }
                B[0] = s->current_picture.f.motion_val[0][s->block_index[n_adj] - 2 * wrap][0];
                B[1] = s->current_picture.f.motion_val[0][s->block_index[n_adj] - 2 * wrap][1];
                if (v->blk_mv_type[pos_b] && !v->blk_mv_type[xy]) {
                    B[0] = (B[0] + s->current_picture.f.motion_val[0][s->block_index[n_adj ^ 2] - 2 * wrap][0] + 1) >> 1;
                    B[1] = (B[1] + s->current_picture.f.motion_val[0][s->block_index[n_adj ^ 2] - 2 * wrap][1] + 1) >> 1;
                }
            }
            if (s->mb_width > 1) {
                if (!v->is_intra[s->mb_x - s->mb_stride + 1]) {
                    c_valid = 1;
                    n_adj   = 2;
                    pos_c   = s->block_index[2] - 2 * wrap + 2;
                    if (v->blk_mv_type[pos_c] && v->blk_mv_type[xy]) {
                        n_adj = n & 2;
                    }
                    C[0] = s->current_picture.f.motion_val[0][s->block_index[n_adj] - 2 * wrap + 2][0];
                    C[1] = s->current_picture.f.motion_val[0][s->block_index[n_adj] - 2 * wrap + 2][1];
                    if (v->blk_mv_type[pos_c] && !v->blk_mv_type[xy]) {
                        C[0] = (1 + C[0] + (s->current_picture.f.motion_val[0][s->block_index[n_adj ^ 2] - 2 * wrap + 2][0])) >> 1;
                        C[1] = (1 + C[1] + (s->current_picture.f.motion_val[0][s->block_index[n_adj ^ 2] - 2 * wrap + 2][1])) >> 1;
                    }
                    if (s->mb_x == s->mb_width - 1) {
                        if (!v->is_intra[s->mb_x - s->mb_stride - 1]) {
                            c_valid = 1;
                            n_adj   = 3;
                            pos_c   = s->block_index[3] - 2 * wrap - 2;
                            if (v->blk_mv_type[pos_c] && v->blk_mv_type[xy]) {
                                n_adj = n | 1;
                            }
                            C[0] = s->current_picture.f.motion_val[0][s->block_index[n_adj] - 2 * wrap - 2][0];
                            C[1] = s->current_picture.f.motion_val[0][s->block_index[n_adj] - 2 * wrap - 2][1];
                            if (v->blk_mv_type[pos_c] && !v->blk_mv_type[xy]) {
                                C[0] = (1 + C[0] + s->current_picture.f.motion_val[0][s->block_index[1] - 2 * wrap - 2][0]) >> 1;
                                C[1] = (1 + C[1] + s->current_picture.f.motion_val[0][s->block_index[1] - 2 * wrap - 2][1]) >> 1;
                            }
                        } else
                            c_valid = 0;
                    }
                }
            }
        }
    } else {
        pos_b   = s->block_index[1];
        b_valid = 1;
        B[0]    = s->current_picture.f.motion_val[0][pos_b][0];
        B[1]    = s->current_picture.f.motion_val[0][pos_b][1];
        pos_c   = s->block_index[0];
        c_valid = 1;
        C[0]    = s->current_picture.f.motion_val[0][pos_c][0];
        C[1]    = s->current_picture.f.motion_val[0][pos_c][1];
    }

    total_valid = a_valid + b_valid + c_valid;
    // check if predictor A is out of bounds
    if (!s->mb_x && !(n == 1 || n == 3)) {
        A[0] = A[1] = 0;
    }
    // check if predictor B is out of bounds
    if ((s->first_slice_line && v->blk_mv_type[xy]) || (s->first_slice_line && !(n & 2))) {
        B[0] = B[1] = C[0] = C[1] = 0;
    }
    if (!v->blk_mv_type[xy]) {
        if (s->mb_width == 1) {
            px = B[0];
            py = B[1];
        } else {
            if (total_valid >= 2) {
                px = mid_pred(A[0], B[0], C[0]);
                py = mid_pred(A[1], B[1], C[1]);
            } else if (total_valid) {
                if (a_valid) { px = A[0]; py = A[1]; }
                if (b_valid) { px = B[0]; py = B[1]; }
                if (c_valid) { px = C[0]; py = C[1]; }
            } else
                px = py = 0;
        }
    } else {
        if (a_valid)
            field_a = (A[1] & 4) ? 1 : 0;
        else
            field_a = 0;
        if (b_valid)
            field_b = (B[1] & 4) ? 1 : 0;
        else
            field_b = 0;
        if (c_valid)
            field_c = (C[1] & 4) ? 1 : 0;
        else
            field_c = 0;

        num_oppfield  = field_a + field_b + field_c;
        num_samefield = total_valid - num_oppfield;
        if (total_valid == 3) {
            if ((num_samefield == 3) || (num_oppfield == 3)) {
                px = mid_pred(A[0], B[0], C[0]);
                py = mid_pred(A[1], B[1], C[1]);
            } else if (num_samefield >= num_oppfield) {
                /* take one MV from same field set depending on priority
                the check for B may not be necessary */
                px = !field_a ? A[0] : B[0];
                py = !field_a ? A[1] : B[1];
            } else {
                px =  field_a ? A[0] : B[0];
                py =  field_a ? A[1] : B[1];
            }
        } else if (total_valid == 2) {
            if (num_samefield >= num_oppfield) {
                if (!field_a && a_valid) {
                    px = A[0];
                    py = A[1];
                } else if (!field_b && b_valid) {
                    px = B[0];
                    py = B[1];
                } else if (c_valid) {
                    px = C[0];
                    py = C[1];
                } else px = py = 0;
            } else {
                if (field_a && a_valid) {
                    px = A[0];
                    py = A[1];
                } else if (field_b && b_valid) {
                    px = B[0];
                    py = B[1];
                } else if (c_valid) {
                    px = C[0];
                    py = C[1];
                }
            }
        } else if (total_valid == 1) {
            px = (a_valid) ? A[0] : ((b_valid) ? B[0] : C[0]);
            py = (a_valid) ? A[1] : ((b_valid) ? B[1] : C[1]);
        } else
            px = py = 0;
    }

    /* store MV using signed modulus of MV range defined in 4.11 */
    s->mv[0][n][0] = s->current_picture.f.motion_val[0][xy][0] = ((px + dmv_x + r_x) & ((r_x << 1) - 1)) - r_x;
    s->mv[0][n][1] = s->current_picture.f.motion_val[0][xy][1] = ((py + dmv_y + r_y) & ((r_y << 1) - 1)) - r_y;
    if (mvn == 1) { /* duplicate motion data for 1-MV block */
        s->current_picture.f.motion_val[0][xy +    1    ][0] = s->current_picture.f.motion_val[0][xy][0];
        s->current_picture.f.motion_val[0][xy +    1    ][1] = s->current_picture.f.motion_val[0][xy][1];
        s->current_picture.f.motion_val[0][xy + wrap    ][0] = s->current_picture.f.motion_val[0][xy][0];
        s->current_picture.f.motion_val[0][xy + wrap    ][1] = s->current_picture.f.motion_val[0][xy][1];
        s->current_picture.f.motion_val[0][xy + wrap + 1][0] = s->current_picture.f.motion_val[0][xy][0];
        s->current_picture.f.motion_val[0][xy + wrap + 1][1] = s->current_picture.f.motion_val[0][xy][1];
    } else if (mvn == 2) { /* duplicate motion data for 2-Field MV block */
        s->current_picture.f.motion_val[0][xy + 1][0] = s->current_picture.f.motion_val[0][xy][0];
        s->current_picture.f.motion_val[0][xy + 1][1] = s->current_picture.f.motion_val[0][xy][1];
        s->mv[0][n + 1][0] = s->mv[0][n][0];
        s->mv[0][n + 1][1] = s->mv[0][n][1];
    }
}

/** Motion compensation for direct or interpolated blocks in B-frames
 */
static void vc1_interp_mc(VC1Context *v)
{
    MpegEncContext *s = &v->s;
    DSPContext *dsp = &v->s.dsp;
    uint8_t *srcY, *srcU, *srcV;
    int dxy, mx, my, uvmx, uvmy, src_x, src_y, uvsrc_x, uvsrc_y;
    int off, off_uv;
    int v_edge_pos = s->v_edge_pos >> v->field_mode;

    if (!v->field_mode && !v->s.next_picture.f.data[0])
        return;

    mx   = s->mv[1][0][0];
    my   = s->mv[1][0][1];
    uvmx = (mx + ((mx & 3) == 3)) >> 1;
    uvmy = (my + ((my & 3) == 3)) >> 1;
    if (v->field_mode) {
        if (v->cur_field_type != v->ref_field_type[1])
            my   = my   - 2 + 4 * v->cur_field_type;
            uvmy = uvmy - 2 + 4 * v->cur_field_type;
    }
    if (v->fastuvmc) {
        uvmx = uvmx + ((uvmx < 0) ? -(uvmx & 1) : (uvmx & 1));
        uvmy = uvmy + ((uvmy < 0) ? -(uvmy & 1) : (uvmy & 1));
    }
    srcY = s->next_picture.f.data[0];
    srcU = s->next_picture.f.data[1];
    srcV = s->next_picture.f.data[2];

    src_x   = s->mb_x * 16 + (mx   >> 2);
    src_y   = s->mb_y * 16 + (my   >> 2);
    uvsrc_x = s->mb_x *  8 + (uvmx >> 2);
    uvsrc_y = s->mb_y *  8 + (uvmy >> 2);

    if (v->profile != PROFILE_ADVANCED) {
        src_x   = av_clip(  src_x, -16, s->mb_width  * 16);
        src_y   = av_clip(  src_y, -16, s->mb_height * 16);
        uvsrc_x = av_clip(uvsrc_x,  -8, s->mb_width  *  8);
        uvsrc_y = av_clip(uvsrc_y,  -8, s->mb_height *  8);
    } else {
        src_x   = av_clip(  src_x, -17, s->avctx->coded_width);
        src_y   = av_clip(  src_y, -18, s->avctx->coded_height + 1);
        uvsrc_x = av_clip(uvsrc_x,  -8, s->avctx->coded_width  >> 1);
        uvsrc_y = av_clip(uvsrc_y,  -8, s->avctx->coded_height >> 1);
    }

    srcY += src_y   * s->linesize   + src_x;
    srcU += uvsrc_y * s->uvlinesize + uvsrc_x;
    srcV += uvsrc_y * s->uvlinesize + uvsrc_x;

    if (v->field_mode && v->ref_field_type[1]) {
        srcY += s->current_picture_ptr->f.linesize[0];
        srcU += s->current_picture_ptr->f.linesize[1];
        srcV += s->current_picture_ptr->f.linesize[2];
    }

    /* for grayscale we should not try to read from unknown area */
    if (s->flags & CODEC_FLAG_GRAY) {
        srcU = s->edge_emu_buffer + 18 * s->linesize;
        srcV = s->edge_emu_buffer + 18 * s->linesize;
    }

    if (v->rangeredfrm || s->h_edge_pos < 22 || v_edge_pos < 22
        || (unsigned)(src_x - s->mspel) > s->h_edge_pos - (mx & 3) - 16 - s->mspel * 3
        || (unsigned)(src_y - s->mspel) > v_edge_pos    - (my & 3) - 16 - s->mspel * 3) {
        uint8_t *uvbuf = s->edge_emu_buffer + 19 * s->linesize;

        srcY -= s->mspel * (1 + s->linesize);
        s->dsp.emulated_edge_mc(s->edge_emu_buffer, srcY, s->linesize,
                                17 + s->mspel * 2, 17 + s->mspel * 2,
                                src_x - s->mspel, src_y - s->mspel,
                                s->h_edge_pos, v_edge_pos);
        srcY = s->edge_emu_buffer;
        s->dsp.emulated_edge_mc(uvbuf     , srcU, s->uvlinesize, 8 + 1, 8 + 1,
                                uvsrc_x, uvsrc_y, s->h_edge_pos >> 1, v_edge_pos >> 1);
        s->dsp.emulated_edge_mc(uvbuf + 16, srcV, s->uvlinesize, 8 + 1, 8 + 1,
                                uvsrc_x, uvsrc_y, s->h_edge_pos >> 1, v_edge_pos >> 1);
        srcU = uvbuf;
        srcV = uvbuf + 16;
        /* if we deal with range reduction we need to scale source blocks */
        if (v->rangeredfrm) {
            int i, j;
            uint8_t *src, *src2;

            src = srcY;
            for (j = 0; j < 17 + s->mspel * 2; j++) {
                for (i = 0; i < 17 + s->mspel * 2; i++)
                    src[i] = ((src[i] - 128) >> 1) + 128;
                src += s->linesize;
            }
            src = srcU;
            src2 = srcV;
            for (j = 0; j < 9; j++) {
                for (i = 0; i < 9; i++) {
                    src[i]  = ((src[i]  - 128) >> 1) + 128;
                    src2[i] = ((src2[i] - 128) >> 1) + 128;
                }
                src  += s->uvlinesize;
                src2 += s->uvlinesize;
            }
        }
        srcY += s->mspel * (1 + s->linesize);
    }

    if (v->field_mode && v->second_field) {
        off    = s->current_picture_ptr->f.linesize[0];
        off_uv = s->current_picture_ptr->f.linesize[1];
    } else {
        off    = 0;
        off_uv = 0;
    }

    if (s->mspel) {
        dxy = ((my & 3) << 2) | (mx & 3);
        v->vc1dsp.avg_vc1_mspel_pixels_tab[dxy](s->dest[0] + off    , srcY    , s->linesize, v->rnd);
        v->vc1dsp.avg_vc1_mspel_pixels_tab[dxy](s->dest[0] + off + 8, srcY + 8, s->linesize, v->rnd);
        srcY += s->linesize * 8;
        v->vc1dsp.avg_vc1_mspel_pixels_tab[dxy](s->dest[0] + off + 8 * s->linesize    , srcY    , s->linesize, v->rnd);
        v->vc1dsp.avg_vc1_mspel_pixels_tab[dxy](s->dest[0] + off + 8 * s->linesize + 8, srcY + 8, s->linesize, v->rnd);
    } else { // hpel mc
        dxy = (my & 2) | ((mx & 2) >> 1);

        if (!v->rnd)
            dsp->avg_pixels_tab[0][dxy](s->dest[0] + off, srcY, s->linesize, 16);
        else
            dsp->avg_no_rnd_pixels_tab[0][dxy](s->dest[0] + off, srcY, s->linesize, 16);
    }

    if (s->flags & CODEC_FLAG_GRAY) return;
    /* Chroma MC always uses qpel blilinear */
    uvmx = (uvmx & 3) << 1;
    uvmy = (uvmy & 3) << 1;
    if (!v->rnd) {
        dsp->avg_h264_chroma_pixels_tab[0](s->dest[1] + off_uv, srcU, s->uvlinesize, 8, uvmx, uvmy);
        dsp->avg_h264_chroma_pixels_tab[0](s->dest[2] + off_uv, srcV, s->uvlinesize, 8, uvmx, uvmy);
    } else {
        v->vc1dsp.avg_no_rnd_vc1_chroma_pixels_tab[0](s->dest[1] + off_uv, srcU, s->uvlinesize, 8, uvmx, uvmy);
        v->vc1dsp.avg_no_rnd_vc1_chroma_pixels_tab[0](s->dest[2] + off_uv, srcV, s->uvlinesize, 8, uvmx, uvmy);
    }
}

static av_always_inline int scale_mv(int value, int bfrac, int inv, int qs)
{
    int n = bfrac;

#if B_FRACTION_DEN==256
    if (inv)
        n -= 256;
    if (!qs)
        return 2 * ((value * n + 255) >> 9);
    return (value * n + 128) >> 8;
#else
    if (inv)
        n -= B_FRACTION_DEN;
    if (!qs)
        return 2 * ((value * n + B_FRACTION_DEN - 1) / (2 * B_FRACTION_DEN));
    return (value * n + B_FRACTION_DEN/2) / B_FRACTION_DEN;
#endif
}

static av_always_inline int scale_mv_intfi(int value, int bfrac, int inv,
                                           int qs, int qs_last)
{
    int n = bfrac;

    if (inv)
        n -= 256;
    n <<= !qs_last;
    if (!qs)
        return (value * n + 255) >> 9;
    else
        return (value * n + 128) >> 8;
}

/** Reconstruct motion vector for B-frame and do motion compensation
 */
static inline void vc1_b_mc(VC1Context *v, int dmv_x[2], int dmv_y[2],
                            int direct, int mode)
{
    if (v->use_ic) {
        v->mv_mode2 = v->mv_mode;
        v->mv_mode  = MV_PMODE_INTENSITY_COMP;
    }
    if (direct) {
        vc1_mc_1mv(v, 0);
        vc1_interp_mc(v);
        if (v->use_ic)
            v->mv_mode = v->mv_mode2;
        return;
    }
    if (mode == BMV_TYPE_INTERPOLATED) {
        vc1_mc_1mv(v, 0);
        vc1_interp_mc(v);
        if (v->use_ic)
            v->mv_mode = v->mv_mode2;
        return;
    }

    if (v->use_ic && (mode == BMV_TYPE_BACKWARD))
        v->mv_mode = v->mv_mode2;
    vc1_mc_1mv(v, (mode == BMV_TYPE_BACKWARD));
    if (v->use_ic)
        v->mv_mode = v->mv_mode2;
}

static inline void vc1_pred_b_mv(VC1Context *v, int dmv_x[2], int dmv_y[2],
                                 int direct, int mvtype)
{
    MpegEncContext *s = &v->s;
    int xy, wrap, off = 0;
    int16_t *A, *B, *C;
    int px, py;
    int sum;
    int r_x, r_y;
    const uint8_t *is_intra = v->mb_type[0];

    r_x = v->range_x;
    r_y = v->range_y;
    /* scale MV difference to be quad-pel */
    dmv_x[0] <<= 1 - s->quarter_sample;
    dmv_y[0] <<= 1 - s->quarter_sample;
    dmv_x[1] <<= 1 - s->quarter_sample;
    dmv_y[1] <<= 1 - s->quarter_sample;

    wrap = s->b8_stride;
    xy = s->block_index[0];

    if (s->mb_intra) {
        s->current_picture.f.motion_val[0][xy + v->blocks_off][0] =
        s->current_picture.f.motion_val[0][xy + v->blocks_off][1] =
        s->current_picture.f.motion_val[1][xy + v->blocks_off][0] =
        s->current_picture.f.motion_val[1][xy + v->blocks_off][1] = 0;
        return;
    }
    if (!v->field_mode) {
        s->mv[0][0][0] = scale_mv(s->next_picture.f.motion_val[1][xy][0], v->bfraction, 0, s->quarter_sample);
        s->mv[0][0][1] = scale_mv(s->next_picture.f.motion_val[1][xy][1], v->bfraction, 0, s->quarter_sample);
        s->mv[1][0][0] = scale_mv(s->next_picture.f.motion_val[1][xy][0], v->bfraction, 1, s->quarter_sample);
        s->mv[1][0][1] = scale_mv(s->next_picture.f.motion_val[1][xy][1], v->bfraction, 1, s->quarter_sample);

        /* Pullback predicted motion vectors as specified in 8.4.5.4 */
        s->mv[0][0][0] = av_clip(s->mv[0][0][0], -60 - (s->mb_x << 6), (s->mb_width  << 6) - 4 - (s->mb_x << 6));
        s->mv[0][0][1] = av_clip(s->mv[0][0][1], -60 - (s->mb_y << 6), (s->mb_height << 6) - 4 - (s->mb_y << 6));
        s->mv[1][0][0] = av_clip(s->mv[1][0][0], -60 - (s->mb_x << 6), (s->mb_width  << 6) - 4 - (s->mb_x << 6));
        s->mv[1][0][1] = av_clip(s->mv[1][0][1], -60 - (s->mb_y << 6), (s->mb_height << 6) - 4 - (s->mb_y << 6));
    }
    if (direct) {
        s->current_picture.f.motion_val[0][xy + v->blocks_off][0] = s->mv[0][0][0];
        s->current_picture.f.motion_val[0][xy + v->blocks_off][1] = s->mv[0][0][1];
        s->current_picture.f.motion_val[1][xy + v->blocks_off][0] = s->mv[1][0][0];
        s->current_picture.f.motion_val[1][xy + v->blocks_off][1] = s->mv[1][0][1];
        return;
    }

    if ((mvtype == BMV_TYPE_FORWARD) || (mvtype == BMV_TYPE_INTERPOLATED)) {
        C   = s->current_picture.f.motion_val[0][xy - 2];
        A   = s->current_picture.f.motion_val[0][xy - wrap * 2];
        off = (s->mb_x == (s->mb_width - 1)) ? -2 : 2;
        B   = s->current_picture.f.motion_val[0][xy - wrap * 2 + off];

        if (!s->mb_x) C[0] = C[1] = 0;
        if (!s->first_slice_line) { // predictor A is not out of bounds
            if (s->mb_width == 1) {
                px = A[0];
                py = A[1];
            } else {
                px = mid_pred(A[0], B[0], C[0]);
                py = mid_pred(A[1], B[1], C[1]);
            }
        } else if (s->mb_x) { // predictor C is not out of bounds
            px = C[0];
            py = C[1];
        } else {
            px = py = 0;
        }
        /* Pullback MV as specified in 8.3.5.3.4 */
        {
            int qx, qy, X, Y;
            if (v->profile < PROFILE_ADVANCED) {
                qx = (s->mb_x << 5);
                qy = (s->mb_y << 5);
                X  = (s->mb_width  << 5) - 4;
                Y  = (s->mb_height << 5) - 4;
                if (qx + px < -28) px = -28 - qx;
                if (qy + py < -28) py = -28 - qy;
                if (qx + px > X) px = X - qx;
                if (qy + py > Y) py = Y - qy;
            } else {
                qx = (s->mb_x << 6);
                qy = (s->mb_y << 6);
                X  = (s->mb_width  << 6) - 4;
                Y  = (s->mb_height << 6) - 4;
                if (qx + px < -60) px = -60 - qx;
                if (qy + py < -60) py = -60 - qy;
                if (qx + px > X) px = X - qx;
                if (qy + py > Y) py = Y - qy;
            }
        }
        /* Calculate hybrid prediction as specified in 8.3.5.3.5 */
        if (0 && !s->first_slice_line && s->mb_x) {
            if (is_intra[xy - wrap])
                sum = FFABS(px) + FFABS(py);
            else
                sum = FFABS(px - A[0]) + FFABS(py - A[1]);
            if (sum > 32) {
                if (get_bits1(&s->gb)) {
                    px = A[0];
                    py = A[1];
                } else {
                    px = C[0];
                    py = C[1];
                }
            } else {
                if (is_intra[xy - 2])
                    sum = FFABS(px) + FFABS(py);
                else
                    sum = FFABS(px - C[0]) + FFABS(py - C[1]);
                if (sum > 32) {
                    if (get_bits1(&s->gb)) {
                        px = A[0];
                        py = A[1];
                    } else {
                        px = C[0];
                        py = C[1];
                    }
                }
            }
        }
        /* store MV using signed modulus of MV range defined in 4.11 */
        s->mv[0][0][0] = ((px + dmv_x[0] + r_x) & ((r_x << 1) - 1)) - r_x;
        s->mv[0][0][1] = ((py + dmv_y[0] + r_y) & ((r_y << 1) - 1)) - r_y;
    }
    if ((mvtype == BMV_TYPE_BACKWARD) || (mvtype == BMV_TYPE_INTERPOLATED)) {
        C   = s->current_picture.f.motion_val[1][xy - 2];
        A   = s->current_picture.f.motion_val[1][xy - wrap * 2];
        off = (s->mb_x == (s->mb_width - 1)) ? -2 : 2;
        B   = s->current_picture.f.motion_val[1][xy - wrap * 2 + off];

        if (!s->mb_x)
            C[0] = C[1] = 0;
        if (!s->first_slice_line) { // predictor A is not out of bounds
            if (s->mb_width == 1) {
                px = A[0];
                py = A[1];
            } else {
                px = mid_pred(A[0], B[0], C[0]);
                py = mid_pred(A[1], B[1], C[1]);
            }
        } else if (s->mb_x) { // predictor C is not out of bounds
            px = C[0];
            py = C[1];
        } else {
            px = py = 0;
        }
        /* Pullback MV as specified in 8.3.5.3.4 */
        {
            int qx, qy, X, Y;
            if (v->profile < PROFILE_ADVANCED) {
                qx = (s->mb_x << 5);
                qy = (s->mb_y << 5);
                X  = (s->mb_width  << 5) - 4;
                Y  = (s->mb_height << 5) - 4;
                if (qx + px < -28) px = -28 - qx;
                if (qy + py < -28) py = -28 - qy;
                if (qx + px > X) px = X - qx;
                if (qy + py > Y) py = Y - qy;
            } else {
                qx = (s->mb_x << 6);
                qy = (s->mb_y << 6);
                X  = (s->mb_width  << 6) - 4;
                Y  = (s->mb_height << 6) - 4;
                if (qx + px < -60) px = -60 - qx;
                if (qy + py < -60) py = -60 - qy;
                if (qx + px > X) px = X - qx;
                if (qy + py > Y) py = Y - qy;
            }
        }
        /* Calculate hybrid prediction as specified in 8.3.5.3.5 */
        if (0 && !s->first_slice_line && s->mb_x) {
            if (is_intra[xy - wrap])
                sum = FFABS(px) + FFABS(py);
            else
                sum = FFABS(px - A[0]) + FFABS(py - A[1]);
            if (sum > 32) {
                if (get_bits1(&s->gb)) {
                    px = A[0];
                    py = A[1];
                } else {
                    px = C[0];
                    py = C[1];
                }
            } else {
                if (is_intra[xy - 2])
                    sum = FFABS(px) + FFABS(py);
                else
                    sum = FFABS(px - C[0]) + FFABS(py - C[1]);
                if (sum > 32) {
                    if (get_bits1(&s->gb)) {
                        px = A[0];
                        py = A[1];
                    } else {
                        px = C[0];
                        py = C[1];
                    }
                }
            }
        }
        /* store MV using signed modulus of MV range defined in 4.11 */

        s->mv[1][0][0] = ((px + dmv_x[1] + r_x) & ((r_x << 1) - 1)) - r_x;
        s->mv[1][0][1] = ((py + dmv_y[1] + r_y) & ((r_y << 1) - 1)) - r_y;
    }
    s->current_picture.f.motion_val[0][xy][0] = s->mv[0][0][0];
    s->current_picture.f.motion_val[0][xy][1] = s->mv[0][0][1];
    s->current_picture.f.motion_val[1][xy][0] = s->mv[1][0][0];
    s->current_picture.f.motion_val[1][xy][1] = s->mv[1][0][1];
}

static inline void vc1_pred_b_mv_intfi(VC1Context *v, int n, int *dmv_x, int *dmv_y, int mv1, int *pred_flag)
{
    int dir = (v->bmvtype == BMV_TYPE_BACKWARD) ? 1 : 0;
    MpegEncContext *s = &v->s;
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;

    if (v->bmvtype == BMV_TYPE_DIRECT) {
        int total_opp, k, f;
        if (s->next_picture.f.mb_type[mb_pos + v->mb_off] != MB_TYPE_INTRA) {
            s->mv[0][0][0] = scale_mv_intfi(s->next_picture.f.motion_val[1][s->block_index[0] + v->blocks_off][0],
                                            v->bfraction, 0, s->quarter_sample, v->qs_last);
            s->mv[0][0][1] = scale_mv_intfi(s->next_picture.f.motion_val[1][s->block_index[0] + v->blocks_off][1],
                                            v->bfraction, 0, s->quarter_sample, v->qs_last);
            s->mv[1][0][0] = scale_mv_intfi(s->next_picture.f.motion_val[1][s->block_index[0] + v->blocks_off][0],
                                            v->bfraction, 1, s->quarter_sample, v->qs_last);
            s->mv[1][0][1] = scale_mv_intfi(s->next_picture.f.motion_val[1][s->block_index[0] + v->blocks_off][1],
                                            v->bfraction, 1, s->quarter_sample, v->qs_last);

            total_opp = v->mv_f_next[0][s->block_index[0] + v->blocks_off]
                      + v->mv_f_next[0][s->block_index[1] + v->blocks_off]
                      + v->mv_f_next[0][s->block_index[2] + v->blocks_off]
                      + v->mv_f_next[0][s->block_index[3] + v->blocks_off];
            f = (total_opp > 2) ? 1 : 0;
        } else {
            s->mv[0][0][0] = s->mv[0][0][1] = 0;
            s->mv[1][0][0] = s->mv[1][0][1] = 0;
            f = 0;
        }
        v->ref_field_type[0] = v->ref_field_type[1] = v->cur_field_type ^ f;
        for (k = 0; k < 4; k++) {
            s->current_picture.f.motion_val[0][s->block_index[k] + v->blocks_off][0] = s->mv[0][0][0];
            s->current_picture.f.motion_val[0][s->block_index[k] + v->blocks_off][1] = s->mv[0][0][1];
            s->current_picture.f.motion_val[1][s->block_index[k] + v->blocks_off][0] = s->mv[1][0][0];
            s->current_picture.f.motion_val[1][s->block_index[k] + v->blocks_off][1] = s->mv[1][0][1];
            v->mv_f[0][s->block_index[k] + v->blocks_off] = f;
            v->mv_f[1][s->block_index[k] + v->blocks_off] = f;
        }
        return;
    }
    if (v->bmvtype == BMV_TYPE_INTERPOLATED) {
        vc1_pred_mv(v, 0, dmv_x[0], dmv_y[0],   1, v->range_x, v->range_y, v->mb_type[0], pred_flag[0], 0);
        vc1_pred_mv(v, 0, dmv_x[1], dmv_y[1],   1, v->range_x, v->range_y, v->mb_type[0], pred_flag[1], 1);
        return;
    }
    if (dir) { // backward
        vc1_pred_mv(v, n, dmv_x[1], dmv_y[1], mv1, v->range_x, v->range_y, v->mb_type[0], pred_flag[1], 1);
        if (n == 3 || mv1) {
            vc1_pred_mv(v, 0, dmv_x[0], dmv_y[0],   1, v->range_x, v->range_y, v->mb_type[0], 0, 0);
        }
    } else { // forward
        vc1_pred_mv(v, n, dmv_x[0], dmv_y[0], mv1, v->range_x, v->range_y, v->mb_type[0], pred_flag[0], 0);
        if (n == 3 || mv1) {
            vc1_pred_mv(v, 0, dmv_x[1], dmv_y[1],   1, v->range_x, v->range_y, v->mb_type[0], 0, 1);
        }
    }
}

/** Get predicted DC value for I-frames only
 * prediction dir: left=0, top=1
 * @param s MpegEncContext
 * @param overlap flag indicating that overlap filtering is used
 * @param pq integer part of picture quantizer
 * @param[in] n block index in the current MB
 * @param dc_val_ptr Pointer to DC predictor
 * @param dir_ptr Prediction direction for use in AC prediction
 */
static inline int vc1_i_pred_dc(MpegEncContext *s, int overlap, int pq, int n,
                                int16_t **dc_val_ptr, int *dir_ptr)
{
    int a, b, c, wrap, pred, scale;
    int16_t *dc_val;
    static const uint16_t dcpred[32] = {
        -1, 1024,  512,  341,  256,  205,  171,  146,  128,
             114,  102,   93,   85,   79,   73,   68,   64,
              60,   57,   54,   51,   49,   47,   45,   43,
              41,   39,   38,   37,   35,   34,   33
    };

    /* find prediction - wmv3_dc_scale always used here in fact */
    if (n < 4) scale = s->y_dc_scale;
    else       scale = s->c_dc_scale;

    wrap   = s->block_wrap[n];
    dc_val = s->dc_val[0] + s->block_index[n];

    /* B A
     * C X
     */
    c = dc_val[ - 1];
    b = dc_val[ - 1 - wrap];
    a = dc_val[ - wrap];

    if (pq < 9 || !overlap) {
        /* Set outer values */
        if (s->first_slice_line && (n != 2 && n != 3))
            b = a = dcpred[scale];
        if (s->mb_x == 0 && (n != 1 && n != 3))
            b = c = dcpred[scale];
    } else {
        /* Set outer values */
        if (s->first_slice_line && (n != 2 && n != 3))
            b = a = 0;
        if (s->mb_x == 0 && (n != 1 && n != 3))
            b = c = 0;
    }

    if (abs(a - b) <= abs(b - c)) {
        pred     = c;
        *dir_ptr = 1; // left
    } else {
        pred     = a;
        *dir_ptr = 0; // top
    }

    /* update predictor */
    *dc_val_ptr = &dc_val[0];
    return pred;
}


/** Get predicted DC value
 * prediction dir: left=0, top=1
 * @param s MpegEncContext
 * @param overlap flag indicating that overlap filtering is used
 * @param pq integer part of picture quantizer
 * @param[in] n block index in the current MB
 * @param a_avail flag indicating top block availability
 * @param c_avail flag indicating left block availability
 * @param dc_val_ptr Pointer to DC predictor
 * @param dir_ptr Prediction direction for use in AC prediction
 */
static inline int vc1_pred_dc(MpegEncContext *s, int overlap, int pq, int n,
                              int a_avail, int c_avail,
                              int16_t **dc_val_ptr, int *dir_ptr)
{
    int a, b, c, wrap, pred;
    int16_t *dc_val;
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;
    int q1, q2 = 0;
    int dqscale_index;

    wrap = s->block_wrap[n];
    dc_val = s->dc_val[0] + s->block_index[n];

    /* B A
     * C X
     */
    c = dc_val[ - 1];
    b = dc_val[ - 1 - wrap];
    a = dc_val[ - wrap];
    /* scale predictors if needed */
    q1 = s->current_picture.f.qscale_table[mb_pos];
    dqscale_index = s->y_dc_scale_table[q1] - 1;
    if (dqscale_index < 0)
        return 0;
    if (c_avail && (n != 1 && n != 3)) {
        q2 = s->current_picture.f.qscale_table[mb_pos - 1];
        if (q2 && q2 != q1)
            c = (c * s->y_dc_scale_table[q2] * ff_vc1_dqscale[dqscale_index] + 0x20000) >> 18;
    }
    if (a_avail && (n != 2 && n != 3)) {
        q2 = s->current_picture.f.qscale_table[mb_pos - s->mb_stride];
        if (q2 && q2 != q1)
            a = (a * s->y_dc_scale_table[q2] * ff_vc1_dqscale[dqscale_index] + 0x20000) >> 18;
    }
    if (a_avail && c_avail && (n != 3)) {
        int off = mb_pos;
        if (n != 1)
            off--;
        if (n != 2)
            off -= s->mb_stride;
        q2 = s->current_picture.f.qscale_table[off];
        if (q2 && q2 != q1)
            b = (b * s->y_dc_scale_table[q2] * ff_vc1_dqscale[dqscale_index] + 0x20000) >> 18;
    }

    if (a_avail && c_avail) {
        if (abs(a - b) <= abs(b - c)) {
            pred     = c;
            *dir_ptr = 1; // left
        } else {
            pred     = a;
            *dir_ptr = 0; // top
        }
    } else if (a_avail) {
        pred     = a;
        *dir_ptr = 0; // top
    } else if (c_avail) {
        pred     = c;
        *dir_ptr = 1; // left
    } else {
        pred     = 0;
        *dir_ptr = 1; // left
    }

    /* update predictor */
    *dc_val_ptr = &dc_val[0];
    return pred;
}

/** @} */ // Block group

/**
 * @name VC1 Macroblock-level functions in Simple/Main Profiles
 * @see 7.1.4, p91 and 8.1.1.7, p(1)04
 * @{
 */

static inline int vc1_coded_block_pred(MpegEncContext * s, int n,
                                       uint8_t **coded_block_ptr)
{
    int xy, wrap, pred, a, b, c;

    xy   = s->block_index[n];
    wrap = s->b8_stride;

    /* B C
     * A X
     */
    a = s->coded_block[xy - 1       ];
    b = s->coded_block[xy - 1 - wrap];
    c = s->coded_block[xy     - wrap];

    if (b == c) {
        pred = a;
    } else {
        pred = c;
    }

    /* store value */
    *coded_block_ptr = &s->coded_block[xy];

    return pred;
}

/**
 * Decode one AC coefficient
 * @param v The VC1 context
 * @param last Last coefficient
 * @param skip How much zero coefficients to skip
 * @param value Decoded AC coefficient value
 * @param codingset set of VLC to decode data
 * @see 8.1.3.4
 */
static void vc1_decode_ac_coeff(VC1Context *v, int *last, int *skip,
                                int *value, int codingset)
{
    GetBitContext *gb = &v->s.gb;
    int index, escape, run = 0, level = 0, lst = 0;

    index = get_vlc2(gb, ff_vc1_ac_coeff_table[codingset].table, AC_VLC_BITS, 3);
    if (index != vc1_ac_sizes[codingset] - 1) {
        run   = vc1_index_decode_table[codingset][index][0];
        level = vc1_index_decode_table[codingset][index][1];
        lst   = index >= vc1_last_decode_table[codingset] || get_bits_left(gb) < 0;
        if (get_bits1(gb))
            level = -level;
    } else {
        escape = decode210(gb);
        if (escape != 2) {
            index = get_vlc2(gb, ff_vc1_ac_coeff_table[codingset].table, AC_VLC_BITS, 3);
            run   = vc1_index_decode_table[codingset][index][0];
            level = vc1_index_decode_table[codingset][index][1];
            lst   = index >= vc1_last_decode_table[codingset];
            if (escape == 0) {
                if (lst)
                    level += vc1_last_delta_level_table[codingset][run];
                else
                    level += vc1_delta_level_table[codingset][run];
            } else {
                if (lst)
                    run += vc1_last_delta_run_table[codingset][level] + 1;
                else
                    run += vc1_delta_run_table[codingset][level] + 1;
            }
            if (get_bits1(gb))
                level = -level;
        } else {
            int sign;
            lst = get_bits1(gb);
            if (v->s.esc3_level_length == 0) {
                if (v->pq < 8 || v->dquantfrm) { // table 59
                    v->s.esc3_level_length = get_bits(gb, 3);
                    if (!v->s.esc3_level_length)
                        v->s.esc3_level_length = get_bits(gb, 2) + 8;
                } else { // table 60
                    v->s.esc3_level_length = get_unary(gb, 1, 6) + 2;
                }
                v->s.esc3_run_length = 3 + get_bits(gb, 2);
            }
            run   = get_bits(gb, v->s.esc3_run_length);
            sign  = get_bits1(gb);
            level = get_bits(gb, v->s.esc3_level_length);
            if (sign)
                level = -level;
        }
    }

    *last  = lst;
    *skip  = run;
    *value = level;
}

/** Decode intra block in intra frames - should be faster than decode_intra_block
 * @param v VC1Context
 * @param block block to decode
 * @param[in] n subblock index
 * @param coded are AC coeffs present or not
 * @param codingset set of VLC to decode data
 */
static int vc1_decode_i_block(VC1Context *v, DCTELEM block[64], int n,
                              int coded, int codingset)
{
    GetBitContext *gb = &v->s.gb;
    MpegEncContext *s = &v->s;
    int dc_pred_dir = 0; /* Direction of the DC prediction used */
    int i;
    int16_t *dc_val;
    int16_t *ac_val, *ac_val2;
    int dcdiff;

    /* Get DC differential */
    if (n < 4) {
        dcdiff = get_vlc2(&s->gb, ff_msmp4_dc_luma_vlc[s->dc_table_index].table, DC_VLC_BITS, 3);
    } else {
        dcdiff = get_vlc2(&s->gb, ff_msmp4_dc_chroma_vlc[s->dc_table_index].table, DC_VLC_BITS, 3);
    }
    if (dcdiff < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Illegal DC VLC\n");
        return -1;
    }
    if (dcdiff) {
        if (dcdiff == 119 /* ESC index value */) {
            /* TODO: Optimize */
            if (v->pq == 1)      dcdiff = get_bits(gb, 10);
            else if (v->pq == 2) dcdiff = get_bits(gb, 9);
            else                 dcdiff = get_bits(gb, 8);
        } else {
            if (v->pq == 1)
                dcdiff = (dcdiff << 2) + get_bits(gb, 2) - 3;
            else if (v->pq == 2)
                dcdiff = (dcdiff << 1) + get_bits1(gb)   - 1;
        }
        if (get_bits1(gb))
            dcdiff = -dcdiff;
    }

    /* Prediction */
    dcdiff += vc1_i_pred_dc(&v->s, v->overlap, v->pq, n, &dc_val, &dc_pred_dir);
    *dc_val = dcdiff;

    /* Store the quantized DC coeff, used for prediction */
    if (n < 4) {
        block[0] = dcdiff * s->y_dc_scale;
    } else {
        block[0] = dcdiff * s->c_dc_scale;
    }
    /* Skip ? */
    if (!coded) {
        goto not_coded;
    }

    // AC Decoding
    i = 1;

    {
        int last = 0, skip, value;
        const uint8_t *zz_table;
        int scale;
        int k;

        scale = v->pq * 2 + v->halfpq;

        if (v->s.ac_pred) {
            if (!dc_pred_dir)
                zz_table = v->zz_8x8[2];
            else
                zz_table = v->zz_8x8[3];
        } else
            zz_table = v->zz_8x8[1];

        ac_val  = s->ac_val[0][0] + s->block_index[n] * 16;
        ac_val2 = ac_val;
        if (dc_pred_dir) // left
            ac_val -= 16;
        else // top
            ac_val -= 16 * s->block_wrap[n];

        while (!last) {
            vc1_decode_ac_coeff(v, &last, &skip, &value, codingset);
            i += skip;
            if (i > 63)
                break;
            block[zz_table[i++]] = value;
        }

        /* apply AC prediction if needed */
        if (s->ac_pred) {
            if (dc_pred_dir) { // left
                for (k = 1; k < 8; k++)
                    block[k << v->left_blk_sh] += ac_val[k];
            } else { // top
                for (k = 1; k < 8; k++)
                    block[k << v->top_blk_sh] += ac_val[k + 8];
            }
        }
        /* save AC coeffs for further prediction */
        for (k = 1; k < 8; k++) {
            ac_val2[k]     = block[k << v->left_blk_sh];
            ac_val2[k + 8] = block[k << v->top_blk_sh];
        }

        /* scale AC coeffs */
        for (k = 1; k < 64; k++)
            if (block[k]) {
                block[k] *= scale;
                if (!v->pquantizer)
                    block[k] += (block[k] < 0) ? -v->pq : v->pq;
            }

        if (s->ac_pred) i = 63;
    }

not_coded:
    if (!coded) {
        int k, scale;
        ac_val  = s->ac_val[0][0] + s->block_index[n] * 16;
        ac_val2 = ac_val;

        i = 0;
        scale = v->pq * 2 + v->halfpq;
        memset(ac_val2, 0, 16 * 2);
        if (dc_pred_dir) { // left
            ac_val -= 16;
            if (s->ac_pred)
                memcpy(ac_val2, ac_val, 8 * 2);
        } else { // top
            ac_val -= 16 * s->block_wrap[n];
            if (s->ac_pred)
                memcpy(ac_val2 + 8, ac_val + 8, 8 * 2);
        }

        /* apply AC prediction if needed */
        if (s->ac_pred) {
            if (dc_pred_dir) { //left
                for (k = 1; k < 8; k++) {
                    block[k << v->left_blk_sh] = ac_val[k] * scale;
                    if (!v->pquantizer && block[k << v->left_blk_sh])
                        block[k << v->left_blk_sh] += (block[k << v->left_blk_sh] < 0) ? -v->pq : v->pq;
                }
            } else { // top
                for (k = 1; k < 8; k++) {
                    block[k << v->top_blk_sh] = ac_val[k + 8] * scale;
                    if (!v->pquantizer && block[k << v->top_blk_sh])
                        block[k << v->top_blk_sh] += (block[k << v->top_blk_sh] < 0) ? -v->pq : v->pq;
                }
            }
            i = 63;
        }
    }
    s->block_last_index[n] = i;

    return 0;
}

/** Decode intra block in intra frames - should be faster than decode_intra_block
 * @param v VC1Context
 * @param block block to decode
 * @param[in] n subblock number
 * @param coded are AC coeffs present or not
 * @param codingset set of VLC to decode data
 * @param mquant quantizer value for this macroblock
 */
static int vc1_decode_i_block_adv(VC1Context *v, DCTELEM block[64], int n,
                                  int coded, int codingset, int mquant)
{
    GetBitContext *gb = &v->s.gb;
    MpegEncContext *s = &v->s;
    int dc_pred_dir = 0; /* Direction of the DC prediction used */
    int i;
    int16_t *dc_val;
    int16_t *ac_val, *ac_val2;
    int dcdiff;
    int a_avail = v->a_avail, c_avail = v->c_avail;
    int use_pred = s->ac_pred;
    int scale;
    int q1, q2 = 0;
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;

    /* Get DC differential */
    if (n < 4) {
        dcdiff = get_vlc2(&s->gb, ff_msmp4_dc_luma_vlc[s->dc_table_index].table, DC_VLC_BITS, 3);
    } else {
        dcdiff = get_vlc2(&s->gb, ff_msmp4_dc_chroma_vlc[s->dc_table_index].table, DC_VLC_BITS, 3);
    }
    if (dcdiff < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Illegal DC VLC\n");
        return -1;
    }
    if (dcdiff) {
        if (dcdiff == 119 /* ESC index value */) {
            /* TODO: Optimize */
            if (mquant == 1)      dcdiff = get_bits(gb, 10);
            else if (mquant == 2) dcdiff = get_bits(gb, 9);
            else                  dcdiff = get_bits(gb, 8);
        } else {
            if (mquant == 1)
                dcdiff = (dcdiff << 2) + get_bits(gb, 2) - 3;
            else if (mquant == 2)
                dcdiff = (dcdiff << 1) + get_bits1(gb)   - 1;
        }
        if (get_bits1(gb))
            dcdiff = -dcdiff;
    }

    /* Prediction */
    dcdiff += vc1_pred_dc(&v->s, v->overlap, mquant, n, v->a_avail, v->c_avail, &dc_val, &dc_pred_dir);
    *dc_val = dcdiff;

    /* Store the quantized DC coeff, used for prediction */
    if (n < 4) {
        block[0] = dcdiff * s->y_dc_scale;
    } else {
        block[0] = dcdiff * s->c_dc_scale;
    }

    //AC Decoding
    i = 1;

    /* check if AC is needed at all */
    if (!a_avail && !c_avail)
        use_pred = 0;
    ac_val  = s->ac_val[0][0] + s->block_index[n] * 16;
    ac_val2 = ac_val;

    scale = mquant * 2 + ((mquant == v->pq) ? v->halfpq : 0);

    if (dc_pred_dir) // left
        ac_val -= 16;
    else // top
        ac_val -= 16 * s->block_wrap[n];

    q1 = s->current_picture.f.qscale_table[mb_pos];
    if ( dc_pred_dir && c_avail && mb_pos)
        q2 = s->current_picture.f.qscale_table[mb_pos - 1];
    if (!dc_pred_dir && a_avail && mb_pos >= s->mb_stride)
        q2 = s->current_picture.f.qscale_table[mb_pos - s->mb_stride];
    if ( dc_pred_dir && n == 1)
        q2 = q1;
    if (!dc_pred_dir && n == 2)
        q2 = q1;
    if (n == 3)
        q2 = q1;

    if (coded) {
        int last = 0, skip, value;
        const uint8_t *zz_table;
        int k;

        if (v->s.ac_pred) {
            if (!use_pred && v->fcm == ILACE_FRAME) {
                zz_table = v->zzi_8x8;
            } else {
                if (!dc_pred_dir) // top
                    zz_table = v->zz_8x8[2];
                else // left
                    zz_table = v->zz_8x8[3];
            }
        } else {
            if (v->fcm != ILACE_FRAME)
                zz_table = v->zz_8x8[1];
            else
                zz_table = v->zzi_8x8;
        }

        while (!last) {
            vc1_decode_ac_coeff(v, &last, &skip, &value, codingset);
            i += skip;
            if (i > 63)
                break;
            block[zz_table[i++]] = value;
        }

        /* apply AC prediction if needed */
        if (use_pred) {
            /* scale predictors if needed*/
            if (q2 && q1 != q2) {
                q1 = q1 * 2 + ((q1 == v->pq) ? v->halfpq : 0) - 1;
                q2 = q2 * 2 + ((q2 == v->pq) ? v->halfpq : 0) - 1;

                if (q1 < 1)
                    return AVERROR_INVALIDDATA;
                if (dc_pred_dir) { // left
                    for (k = 1; k < 8; k++)
                        block[k << v->left_blk_sh] += (ac_val[k] * q2 * ff_vc1_dqscale[q1 - 1] + 0x20000) >> 18;
                } else { // top
                    for (k = 1; k < 8; k++)
                        block[k << v->top_blk_sh] += (ac_val[k + 8] * q2 * ff_vc1_dqscale[q1 - 1] + 0x20000) >> 18;
                }
            } else {
                if (dc_pred_dir) { //left
                    for (k = 1; k < 8; k++)
                        block[k << v->left_blk_sh] += ac_val[k];
                } else { //top
                    for (k = 1; k < 8; k++)
                        block[k << v->top_blk_sh] += ac_val[k + 8];
                }
            }
        }
        /* save AC coeffs for further prediction */
        for (k = 1; k < 8; k++) {
            ac_val2[k    ] = block[k << v->left_blk_sh];
            ac_val2[k + 8] = block[k << v->top_blk_sh];
        }

        /* scale AC coeffs */
        for (k = 1; k < 64; k++)
            if (block[k]) {
                block[k] *= scale;
                if (!v->pquantizer)
                    block[k] += (block[k] < 0) ? -mquant : mquant;
            }

        if (use_pred) i = 63;
    } else { // no AC coeffs
        int k;

        memset(ac_val2, 0, 16 * 2);
        if (dc_pred_dir) { // left
            if (use_pred) {
                memcpy(ac_val2, ac_val, 8 * 2);
                if (q2 && q1 != q2) {
                    q1 = q1 * 2 + ((q1 == v->pq) ? v->halfpq : 0) - 1;
                    q2 = q2 * 2 + ((q2 == v->pq) ? v->halfpq : 0) - 1;
                    if (q1 < 1)
                        return AVERROR_INVALIDDATA;
                    for (k = 1; k < 8; k++)
                        ac_val2[k] = (ac_val2[k] * q2 * ff_vc1_dqscale[q1 - 1] + 0x20000) >> 18;
                }
            }
        } else { // top
            if (use_pred) {
                memcpy(ac_val2 + 8, ac_val + 8, 8 * 2);
                if (q2 && q1 != q2) {
                    q1 = q1 * 2 + ((q1 == v->pq) ? v->halfpq : 0) - 1;
                    q2 = q2 * 2 + ((q2 == v->pq) ? v->halfpq : 0) - 1;
                    if (q1 < 1)
                        return AVERROR_INVALIDDATA;
                    for (k = 1; k < 8; k++)
                        ac_val2[k + 8] = (ac_val2[k + 8] * q2 * ff_vc1_dqscale[q1 - 1] + 0x20000) >> 18;
                }
            }
        }

        /* apply AC prediction if needed */
        if (use_pred) {
            if (dc_pred_dir) { // left
                for (k = 1; k < 8; k++) {
                    block[k << v->left_blk_sh] = ac_val2[k] * scale;
                    if (!v->pquantizer && block[k << v->left_blk_sh])
                        block[k << v->left_blk_sh] += (block[k << v->left_blk_sh] < 0) ? -mquant : mquant;
                }
            } else { // top
                for (k = 1; k < 8; k++) {
                    block[k << v->top_blk_sh] = ac_val2[k + 8] * scale;
                    if (!v->pquantizer && block[k << v->top_blk_sh])
                        block[k << v->top_blk_sh] += (block[k << v->top_blk_sh] < 0) ? -mquant : mquant;
                }
            }
            i = 63;
        }
    }
    s->block_last_index[n] = i;

    return 0;
}

/** Decode intra block in inter frames - more generic version than vc1_decode_i_block
 * @param v VC1Context
 * @param block block to decode
 * @param[in] n subblock index
 * @param coded are AC coeffs present or not
 * @param mquant block quantizer
 * @param codingset set of VLC to decode data
 */
static int vc1_decode_intra_block(VC1Context *v, DCTELEM block[64], int n,
                                  int coded, int mquant, int codingset)
{
    GetBitContext *gb = &v->s.gb;
    MpegEncContext *s = &v->s;
    int dc_pred_dir = 0; /* Direction of the DC prediction used */
    int i;
    int16_t *dc_val;
    int16_t *ac_val, *ac_val2;
    int dcdiff;
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;
    int a_avail = v->a_avail, c_avail = v->c_avail;
    int use_pred = s->ac_pred;
    int scale;
    int q1, q2 = 0;

    s->dsp.clear_block(block);

    /* XXX: Guard against dumb values of mquant */
    mquant = (mquant < 1) ? 0 : ((mquant > 31) ? 31 : mquant);

    /* Set DC scale - y and c use the same */
    s->y_dc_scale = s->y_dc_scale_table[mquant];
    s->c_dc_scale = s->c_dc_scale_table[mquant];

    /* Get DC differential */
    if (n < 4) {
        dcdiff = get_vlc2(&s->gb, ff_msmp4_dc_luma_vlc[s->dc_table_index].table, DC_VLC_BITS, 3);
    } else {
        dcdiff = get_vlc2(&s->gb, ff_msmp4_dc_chroma_vlc[s->dc_table_index].table, DC_VLC_BITS, 3);
    }
    if (dcdiff < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Illegal DC VLC\n");
        return -1;
    }
    if (dcdiff) {
        if (dcdiff == 119 /* ESC index value */) {
            /* TODO: Optimize */
            if (mquant == 1)      dcdiff = get_bits(gb, 10);
            else if (mquant == 2) dcdiff = get_bits(gb, 9);
            else                  dcdiff = get_bits(gb, 8);
        } else {
            if (mquant == 1)
                dcdiff = (dcdiff << 2) + get_bits(gb, 2) - 3;
            else if (mquant == 2)
                dcdiff = (dcdiff << 1) + get_bits1(gb)   - 1;
        }
        if (get_bits1(gb))
            dcdiff = -dcdiff;
    }

    /* Prediction */
    dcdiff += vc1_pred_dc(&v->s, v->overlap, mquant, n, a_avail, c_avail, &dc_val, &dc_pred_dir);
    *dc_val = dcdiff;

    /* Store the quantized DC coeff, used for prediction */

    if (n < 4) {
        block[0] = dcdiff * s->y_dc_scale;
    } else {
        block[0] = dcdiff * s->c_dc_scale;
    }

    //AC Decoding
    i = 1;

    /* check if AC is needed at all and adjust direction if needed */
    if (!a_avail) dc_pred_dir = 1;
    if (!c_avail) dc_pred_dir = 0;
    if (!a_avail && !c_avail) use_pred = 0;
    ac_val = s->ac_val[0][0] + s->block_index[n] * 16;
    ac_val2 = ac_val;

    scale = mquant * 2 + v->halfpq;

    if (dc_pred_dir) //left
        ac_val -= 16;
    else //top
        ac_val -= 16 * s->block_wrap[n];

    q1 = s->current_picture.f.qscale_table[mb_pos];
    if (dc_pred_dir && c_avail && mb_pos)
        q2 = s->current_picture.f.qscale_table[mb_pos - 1];
    if (!dc_pred_dir && a_avail && mb_pos >= s->mb_stride)
        q2 = s->current_picture.f.qscale_table[mb_pos - s->mb_stride];
    if ( dc_pred_dir && n == 1)
        q2 = q1;
    if (!dc_pred_dir && n == 2)
        q2 = q1;
    if (n == 3) q2 = q1;

    if (coded) {
        int last = 0, skip, value;
        int k;

        while (!last) {
            vc1_decode_ac_coeff(v, &last, &skip, &value, codingset);
            i += skip;
            if (i > 63)
                break;
            if (v->fcm == PROGRESSIVE)
                block[v->zz_8x8[0][i++]] = value;
            else {
                if (use_pred && (v->fcm == ILACE_FRAME)) {
                    if (!dc_pred_dir) // top
                        block[v->zz_8x8[2][i++]] = value;
                    else // left
                        block[v->zz_8x8[3][i++]] = value;
                } else {
                    block[v->zzi_8x8[i++]] = value;
                }
            }
        }

        /* apply AC prediction if needed */
        if (use_pred) {
            /* scale predictors if needed*/
            if (q2 && q1 != q2) {
                q1 = q1 * 2 + ((q1 == v->pq) ? v->halfpq : 0) - 1;
                q2 = q2 * 2 + ((q2 == v->pq) ? v->halfpq : 0) - 1;

                if (q1 < 1)
                    return AVERROR_INVALIDDATA;
                if (dc_pred_dir) { // left
                    for (k = 1; k < 8; k++)
                        block[k << v->left_blk_sh] += (ac_val[k] * q2 * ff_vc1_dqscale[q1 - 1] + 0x20000) >> 18;
                } else { //top
                    for (k = 1; k < 8; k++)
                        block[k << v->top_blk_sh] += (ac_val[k + 8] * q2 * ff_vc1_dqscale[q1 - 1] + 0x20000) >> 18;
                }
            } else {
                if (dc_pred_dir) { // left
                    for (k = 1; k < 8; k++)
                        block[k << v->left_blk_sh] += ac_val[k];
                } else { // top
                    for (k = 1; k < 8; k++)
                        block[k << v->top_blk_sh] += ac_val[k + 8];
                }
            }
        }
        /* save AC coeffs for further prediction */
        for (k = 1; k < 8; k++) {
            ac_val2[k    ] = block[k << v->left_blk_sh];
            ac_val2[k + 8] = block[k << v->top_blk_sh];
        }

        /* scale AC coeffs */
        for (k = 1; k < 64; k++)
            if (block[k]) {
                block[k] *= scale;
                if (!v->pquantizer)
                    block[k] += (block[k] < 0) ? -mquant : mquant;
            }

        if (use_pred) i = 63;
    } else { // no AC coeffs
        int k;

        memset(ac_val2, 0, 16 * 2);
        if (dc_pred_dir) { // left
            if (use_pred) {
                memcpy(ac_val2, ac_val, 8 * 2);
                if (q2 && q1 != q2) {
                    q1 = q1 * 2 + ((q1 == v->pq) ? v->halfpq : 0) - 1;
                    q2 = q2 * 2 + ((q2 == v->pq) ? v->halfpq : 0) - 1;
                    if (q1 < 1)
                        return AVERROR_INVALIDDATA;
                    for (k = 1; k < 8; k++)
                        ac_val2[k] = (ac_val2[k] * q2 * ff_vc1_dqscale[q1 - 1] + 0x20000) >> 18;
                }
            }
        } else { // top
            if (use_pred) {
                memcpy(ac_val2 + 8, ac_val + 8, 8 * 2);
                if (q2 && q1 != q2) {
                    q1 = q1 * 2 + ((q1 == v->pq) ? v->halfpq : 0) - 1;
                    q2 = q2 * 2 + ((q2 == v->pq) ? v->halfpq : 0) - 1;
                    if (q1 < 1)
                        return AVERROR_INVALIDDATA;
                    for (k = 1; k < 8; k++)
                        ac_val2[k + 8] = (ac_val2[k + 8] * q2 * ff_vc1_dqscale[q1 - 1] + 0x20000) >> 18;
                }
            }
        }

        /* apply AC prediction if needed */
        if (use_pred) {
            if (dc_pred_dir) { // left
                for (k = 1; k < 8; k++) {
                    block[k << v->left_blk_sh] = ac_val2[k] * scale;
                    if (!v->pquantizer && block[k << v->left_blk_sh])
                        block[k << v->left_blk_sh] += (block[k << v->left_blk_sh] < 0) ? -mquant : mquant;
                }
            } else { // top
                for (k = 1; k < 8; k++) {
                    block[k << v->top_blk_sh] = ac_val2[k + 8] * scale;
                    if (!v->pquantizer && block[k << v->top_blk_sh])
                        block[k << v->top_blk_sh] += (block[k << v->top_blk_sh] < 0) ? -mquant : mquant;
                }
            }
            i = 63;
        }
    }
    s->block_last_index[n] = i;

    return 0;
}

/** Decode P block
 */
static int vc1_decode_p_block(VC1Context *v, DCTELEM block[64], int n,
                              int mquant, int ttmb, int first_block,
                              uint8_t *dst, int linesize, int skip_block,
                              int *ttmb_out)
{
    MpegEncContext *s = &v->s;
    GetBitContext *gb = &s->gb;
    int i, j;
    int subblkpat = 0;
    int scale, off, idx, last, skip, value;
    int ttblk = ttmb & 7;
    int pat = 0;

    s->dsp.clear_block(block);

    if (ttmb == -1) {
        ttblk = ff_vc1_ttblk_to_tt[v->tt_index][get_vlc2(gb, ff_vc1_ttblk_vlc[v->tt_index].table, VC1_TTBLK_VLC_BITS, 1)];
    }
    if (ttblk == TT_4X4) {
        subblkpat = ~(get_vlc2(gb, ff_vc1_subblkpat_vlc[v->tt_index].table, VC1_SUBBLKPAT_VLC_BITS, 1) + 1);
    }
    if ((ttblk != TT_8X8 && ttblk != TT_4X4)
        && ((v->ttmbf || (ttmb != -1 && (ttmb & 8) && !first_block))
            || (!v->res_rtm_flag && !first_block))) {
        subblkpat = decode012(gb);
        if (subblkpat)
            subblkpat ^= 3; // swap decoded pattern bits
        if (ttblk == TT_8X4_TOP || ttblk == TT_8X4_BOTTOM)
            ttblk = TT_8X4;
        if (ttblk == TT_4X8_RIGHT || ttblk == TT_4X8_LEFT)
            ttblk = TT_4X8;
    }
    scale = 2 * mquant + ((v->pq == mquant) ? v->halfpq : 0);

    // convert transforms like 8X4_TOP to generic TT and SUBBLKPAT
    if (ttblk == TT_8X4_TOP || ttblk == TT_8X4_BOTTOM) {
        subblkpat = 2 - (ttblk == TT_8X4_TOP);
        ttblk     = TT_8X4;
    }
    if (ttblk == TT_4X8_RIGHT || ttblk == TT_4X8_LEFT) {
        subblkpat = 2 - (ttblk == TT_4X8_LEFT);
        ttblk     = TT_4X8;
    }
    switch (ttblk) {
    case TT_8X8:
        pat  = 0xF;
        i    = 0;
        last = 0;
        while (!last) {
            vc1_decode_ac_coeff(v, &last, &skip, &value, v->codingset2);
            i += skip;
            if (i > 63)
                break;
            if (!v->fcm)
                idx = v->zz_8x8[0][i++];
            else
                idx = v->zzi_8x8[i++];
            block[idx] = value * scale;
            if (!v->pquantizer)
                block[idx] += (block[idx] < 0) ? -mquant : mquant;
        }
        if (!skip_block) {
            if (i == 1)
                v->vc1dsp.vc1_inv_trans_8x8_dc(dst, linesize, block);
            else {
                v->vc1dsp.vc1_inv_trans_8x8(block);
                s->dsp.add_pixels_clamped(block, dst, linesize);
            }
        }
        break;
    case TT_4X4:
        pat = ~subblkpat & 0xF;
        for (j = 0; j < 4; j++) {
            last = subblkpat & (1 << (3 - j));
            i    = 0;
            off  = (j & 1) * 4 + (j & 2) * 16;
            while (!last) {
                vc1_decode_ac_coeff(v, &last, &skip, &value, v->codingset2);
                i += skip;
                if (i > 15)
                    break;
                if (!v->fcm)
                    idx = ff_vc1_simple_progressive_4x4_zz[i++];
                else
                    idx = ff_vc1_adv_interlaced_4x4_zz[i++];
                block[idx + off] = value * scale;
                if (!v->pquantizer)
                    block[idx + off] += (block[idx + off] < 0) ? -mquant : mquant;
            }
            if (!(subblkpat & (1 << (3 - j))) && !skip_block) {
                if (i == 1)
                    v->vc1dsp.vc1_inv_trans_4x4_dc(dst + (j & 1) * 4 + (j & 2) * 2 * linesize, linesize, block + off);
                else
                    v->vc1dsp.vc1_inv_trans_4x4(dst + (j & 1) * 4 + (j & 2) *  2 * linesize, linesize, block + off);
            }
        }
        break;
    case TT_8X4:
        pat = ~((subblkpat & 2) * 6 + (subblkpat & 1) * 3) & 0xF;
        for (j = 0; j < 2; j++) {
            last = subblkpat & (1 << (1 - j));
            i    = 0;
            off  = j * 32;
            while (!last) {
                vc1_decode_ac_coeff(v, &last, &skip, &value, v->codingset2);
                i += skip;
                if (i > 31)
                    break;
                if (!v->fcm)
                    idx = v->zz_8x4[i++] + off;
                else
                    idx = ff_vc1_adv_interlaced_8x4_zz[i++] + off;
                block[idx] = value * scale;
                if (!v->pquantizer)
                    block[idx] += (block[idx] < 0) ? -mquant : mquant;
            }
            if (!(subblkpat & (1 << (1 - j))) && !skip_block) {
                if (i == 1)
                    v->vc1dsp.vc1_inv_trans_8x4_dc(dst + j * 4 * linesize, linesize, block + off);
                else
                    v->vc1dsp.vc1_inv_trans_8x4(dst + j * 4 * linesize, linesize, block + off);
            }
        }
        break;
    case TT_4X8:
        pat = ~(subblkpat * 5) & 0xF;
        for (j = 0; j < 2; j++) {
            last = subblkpat & (1 << (1 - j));
            i    = 0;
            off  = j * 4;
            while (!last) {
                vc1_decode_ac_coeff(v, &last, &skip, &value, v->codingset2);
                i += skip;
                if (i > 31)
                    break;
                if (!v->fcm)
                    idx = v->zz_4x8[i++] + off;
                else
                    idx = ff_vc1_adv_interlaced_4x8_zz[i++] + off;
                block[idx] = value * scale;
                if (!v->pquantizer)
                    block[idx] += (block[idx] < 0) ? -mquant : mquant;
            }
            if (!(subblkpat & (1 << (1 - j))) && !skip_block) {
                if (i == 1)
                    v->vc1dsp.vc1_inv_trans_4x8_dc(dst + j * 4, linesize, block + off);
                else
                    v->vc1dsp.vc1_inv_trans_4x8(dst + j*4, linesize, block + off);
            }
        }
        break;
    }
    if (ttmb_out)
        *ttmb_out |= ttblk << (n * 4);
    return pat;
}

/** @} */ // Macroblock group

static const int size_table  [6] = { 0, 2, 3, 4,  5,  8 };
static const int offset_table[6] = { 0, 1, 3, 7, 15, 31 };

static av_always_inline void vc1_apply_p_v_loop_filter(VC1Context *v, int block_num)
{
    MpegEncContext *s  = &v->s;
    int mb_cbp         = v->cbp[s->mb_x - s->mb_stride],
        block_cbp      = mb_cbp      >> (block_num * 4), bottom_cbp,
        mb_is_intra    = v->is_intra[s->mb_x - s->mb_stride],
        block_is_intra = mb_is_intra >> (block_num * 4), bottom_is_intra;
    int idx, linesize  = block_num > 3 ? s->uvlinesize : s->linesize, ttblk;
    uint8_t *dst;

    if (block_num > 3) {
        dst      = s->dest[block_num - 3];
    } else {
        dst      = s->dest[0] + (block_num & 1) * 8 + ((block_num & 2) * 4 - 8) * linesize;
    }
    if (s->mb_y != s->end_mb_y || block_num < 2) {
        int16_t (*mv)[2];
        int mv_stride;

        if (block_num > 3) {
            bottom_cbp      = v->cbp[s->mb_x]      >> (block_num * 4);
            bottom_is_intra = v->is_intra[s->mb_x] >> (block_num * 4);
            mv              = &v->luma_mv[s->mb_x - s->mb_stride];
            mv_stride       = s->mb_stride;
        } else {
            bottom_cbp      = (block_num < 2) ? (mb_cbp               >> ((block_num + 2) * 4))
                                              : (v->cbp[s->mb_x]      >> ((block_num - 2) * 4));
            bottom_is_intra = (block_num < 2) ? (mb_is_intra          >> ((block_num + 2) * 4))
                                              : (v->is_intra[s->mb_x] >> ((block_num - 2) * 4));
            mv_stride       = s->b8_stride;
            mv              = &s->current_picture.f.motion_val[0][s->block_index[block_num] - 2 * mv_stride];
        }

        if (bottom_is_intra & 1 || block_is_intra & 1 ||
            mv[0][0] != mv[mv_stride][0] || mv[0][1] != mv[mv_stride][1]) {
            v->vc1dsp.vc1_v_loop_filter8(dst, linesize, v->pq);
        } else {
            idx = ((bottom_cbp >> 2) | block_cbp) & 3;
            if (idx == 3) {
                v->vc1dsp.vc1_v_loop_filter8(dst, linesize, v->pq);
            } else if (idx) {
                if (idx == 1)
                    v->vc1dsp.vc1_v_loop_filter4(dst + 4, linesize, v->pq);
                else
                    v->vc1dsp.vc1_v_loop_filter4(dst,     linesize, v->pq);
            }
        }
    }

    dst -= 4 * linesize;
    ttblk = (v->ttblk[s->mb_x - s->mb_stride] >> (block_num * 4)) & 0xF;
    if (ttblk == TT_4X4 || ttblk == TT_8X4) {
        idx = (block_cbp | (block_cbp >> 2)) & 3;
        if (idx == 3) {
            v->vc1dsp.vc1_v_loop_filter8(dst, linesize, v->pq);
        } else if (idx) {
            if (idx == 1)
                v->vc1dsp.vc1_v_loop_filter4(dst + 4, linesize, v->pq);
            else
                v->vc1dsp.vc1_v_loop_filter4(dst,     linesize, v->pq);
        }
    }
}

static av_always_inline void vc1_apply_p_h_loop_filter(VC1Context *v, int block_num)
{
    MpegEncContext *s  = &v->s;
    int mb_cbp         = v->cbp[s->mb_x - 1 - s->mb_stride],
        block_cbp      = mb_cbp      >> (block_num * 4), right_cbp,
        mb_is_intra    = v->is_intra[s->mb_x - 1 - s->mb_stride],
        block_is_intra = mb_is_intra >> (block_num * 4), right_is_intra;
    int idx, linesize  = block_num > 3 ? s->uvlinesize : s->linesize, ttblk;
    uint8_t *dst;

    if (block_num > 3) {
        dst = s->dest[block_num - 3] - 8 * linesize;
    } else {
        dst = s->dest[0] + (block_num & 1) * 8 + ((block_num & 2) * 4 - 16) * linesize - 8;
    }

    if (s->mb_x != s->mb_width || !(block_num & 5)) {
        int16_t (*mv)[2];

        if (block_num > 3) {
            right_cbp      = v->cbp[s->mb_x - s->mb_stride] >> (block_num * 4);
            right_is_intra = v->is_intra[s->mb_x - s->mb_stride] >> (block_num * 4);
            mv             = &v->luma_mv[s->mb_x - s->mb_stride - 1];
        } else {
            right_cbp      = (block_num & 1) ? (v->cbp[s->mb_x - s->mb_stride]      >> ((block_num - 1) * 4))
                                             : (mb_cbp                              >> ((block_num + 1) * 4));
            right_is_intra = (block_num & 1) ? (v->is_intra[s->mb_x - s->mb_stride] >> ((block_num - 1) * 4))
                                             : (mb_is_intra                         >> ((block_num + 1) * 4));
            mv             = &s->current_picture.f.motion_val[0][s->block_index[block_num] - s->b8_stride * 2 - 2];
        }
        if (block_is_intra & 1 || right_is_intra & 1 || mv[0][0] != mv[1][0] || mv[0][1] != mv[1][1]) {
            v->vc1dsp.vc1_h_loop_filter8(dst, linesize, v->pq);
        } else {
            idx = ((right_cbp >> 1) | block_cbp) & 5; // FIXME check
            if (idx == 5) {
                v->vc1dsp.vc1_h_loop_filter8(dst, linesize, v->pq);
            } else if (idx) {
                if (idx == 1)
                    v->vc1dsp.vc1_h_loop_filter4(dst + 4 * linesize, linesize, v->pq);
                else
                    v->vc1dsp.vc1_h_loop_filter4(dst,                linesize, v->pq);
            }
        }
    }

    dst -= 4;
    ttblk = (v->ttblk[s->mb_x - s->mb_stride - 1] >> (block_num * 4)) & 0xf;
    if (ttblk == TT_4X4 || ttblk == TT_4X8) {
        idx = (block_cbp | (block_cbp >> 1)) & 5;
        if (idx == 5) {
            v->vc1dsp.vc1_h_loop_filter8(dst, linesize, v->pq);
        } else if (idx) {
            if (idx == 1)
                v->vc1dsp.vc1_h_loop_filter4(dst + linesize * 4, linesize, v->pq);
            else
                v->vc1dsp.vc1_h_loop_filter4(dst,                linesize, v->pq);
        }
    }
}

static void vc1_apply_p_loop_filter(VC1Context *v)
{
    MpegEncContext *s = &v->s;
    int i;

    for (i = 0; i < 6; i++) {
        vc1_apply_p_v_loop_filter(v, i);
    }

    /* V always precedes H, therefore we run H one MB before V;
     * at the end of a row, we catch up to complete the row */
    if (s->mb_x) {
        for (i = 0; i < 6; i++) {
            vc1_apply_p_h_loop_filter(v, i);
        }
        if (s->mb_x == s->mb_width - 1) {
            s->mb_x++;
            ff_update_block_index(s);
            for (i = 0; i < 6; i++) {
                vc1_apply_p_h_loop_filter(v, i);
            }
        }
    }
}

/** Decode one P-frame MB
 */
static int vc1_decode_p_mb(VC1Context *v)
{
    MpegEncContext *s = &v->s;
    GetBitContext *gb = &s->gb;
    int i, j;
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;
    int cbp; /* cbp decoding stuff */
    int mqdiff, mquant; /* MB quantization */
    int ttmb = v->ttfrm; /* MB Transform type */

    int mb_has_coeffs = 1; /* last_flag */
    int dmv_x, dmv_y; /* Differential MV components */
    int index, index1; /* LUT indexes */
    int val, sign; /* temp values */
    int first_block = 1;
    int dst_idx, off;
    int skipped, fourmv;
    int block_cbp = 0, pat, block_tt = 0, block_intra = 0;

    mquant = v->pq; /* lossy initialization */

    if (v->mv_type_is_raw)
        fourmv = get_bits1(gb);
    else
        fourmv = v->mv_type_mb_plane[mb_pos];
    if (v->skip_is_raw)
        skipped = get_bits1(gb);
    else
        skipped = v->s.mbskip_table[mb_pos];

    if (!fourmv) { /* 1MV mode */
        if (!skipped) {
            GET_MVDATA(dmv_x, dmv_y);

            if (s->mb_intra) {
                s->current_picture.f.motion_val[1][s->block_index[0]][0] = 0;
                s->current_picture.f.motion_val[1][s->block_index[0]][1] = 0;
            }
            s->current_picture.f.mb_type[mb_pos] = s->mb_intra ? MB_TYPE_INTRA : MB_TYPE_16x16;
            vc1_pred_mv(v, 0, dmv_x, dmv_y, 1, v->range_x, v->range_y, v->mb_type[0], 0, 0);

            /* FIXME Set DC val for inter block ? */
            if (s->mb_intra && !mb_has_coeffs) {
                GET_MQUANT();
                s->ac_pred = get_bits1(gb);
                cbp        = 0;
            } else if (mb_has_coeffs) {
                if (s->mb_intra)
                    s->ac_pred = get_bits1(gb);
                cbp = get_vlc2(&v->s.gb, v->cbpcy_vlc->table, VC1_CBPCY_P_VLC_BITS, 2);
                GET_MQUANT();
            } else {
                mquant = v->pq;
                cbp    = 0;
            }
            s->current_picture.f.qscale_table[mb_pos] = mquant;

            if (!v->ttmbf && !s->mb_intra && mb_has_coeffs)
                ttmb = get_vlc2(gb, ff_vc1_ttmb_vlc[v->tt_index].table,
                                VC1_TTMB_VLC_BITS, 2);
            if (!s->mb_intra) vc1_mc_1mv(v, 0);
            dst_idx = 0;
            for (i = 0; i < 6; i++) {
                s->dc_val[0][s->block_index[i]] = 0;
                dst_idx += i >> 2;
                val = ((cbp >> (5 - i)) & 1);
                off = (i & 4) ? 0 : ((i & 1) * 8 + (i & 2) * 4 * s->linesize);
                v->mb_type[0][s->block_index[i]] = s->mb_intra;
                if (s->mb_intra) {
                    /* check if prediction blocks A and C are available */
                    v->a_avail = v->c_avail = 0;
                    if (i == 2 || i == 3 || !s->first_slice_line)
                        v->a_avail = v->mb_type[0][s->block_index[i] - s->block_wrap[i]];
                    if (i == 1 || i == 3 || s->mb_x)
                        v->c_avail = v->mb_type[0][s->block_index[i] - 1];

                    vc1_decode_intra_block(v, s->block[i], i, val, mquant,
                                           (i & 4) ? v->codingset2 : v->codingset);
                    if ((i>3) && (s->flags & CODEC_FLAG_GRAY))
                        continue;
                    v->vc1dsp.vc1_inv_trans_8x8(s->block[i]);
                    if (v->rangeredfrm)
                        for (j = 0; j < 64; j++)
                            s->block[i][j] <<= 1;
                    s->dsp.put_signed_pixels_clamped(s->block[i], s->dest[dst_idx] + off, i & 4 ? s->uvlinesize : s->linesize);
                    if (v->pq >= 9 && v->overlap) {
                        if (v->c_avail)
                            v->vc1dsp.vc1_h_overlap(s->dest[dst_idx] + off, i & 4 ? s->uvlinesize : s->linesize);
                        if (v->a_avail)
                            v->vc1dsp.vc1_v_overlap(s->dest[dst_idx] + off, i & 4 ? s->uvlinesize : s->linesize);
                    }
                    block_cbp   |= 0xF << (i << 2);
                    block_intra |= 1 << i;
                } else if (val) {
                    pat = vc1_decode_p_block(v, s->block[i], i, mquant, ttmb, first_block,
                                             s->dest[dst_idx] + off, (i & 4) ? s->uvlinesize : s->linesize,
                                             (i & 4) && (s->flags & CODEC_FLAG_GRAY), &block_tt);
                    block_cbp |= pat << (i << 2);
                    if (!v->ttmbf && ttmb < 8)
                        ttmb = -1;
                    first_block = 0;
                }
            }
        } else { // skipped
            s->mb_intra = 0;
            for (i = 0; i < 6; i++) {
                v->mb_type[0][s->block_index[i]] = 0;
                s->dc_val[0][s->block_index[i]]  = 0;
            }
            s->current_picture.f.mb_type[mb_pos]      = MB_TYPE_SKIP;
            s->current_picture.f.qscale_table[mb_pos] = 0;
            vc1_pred_mv(v, 0, 0, 0, 1, v->range_x, v->range_y, v->mb_type[0], 0, 0);
            vc1_mc_1mv(v, 0);
        }
    } else { // 4MV mode
        if (!skipped /* unskipped MB */) {
            int intra_count = 0, coded_inter = 0;
            int is_intra[6], is_coded[6];
            /* Get CBPCY */
            cbp = get_vlc2(&v->s.gb, v->cbpcy_vlc->table, VC1_CBPCY_P_VLC_BITS, 2);
            for (i = 0; i < 6; i++) {
                val = ((cbp >> (5 - i)) & 1);
                s->dc_val[0][s->block_index[i]] = 0;
                s->mb_intra                     = 0;
                if (i < 4) {
                    dmv_x = dmv_y = 0;
                    s->mb_intra   = 0;
                    mb_has_coeffs = 0;
                    if (val) {
                        GET_MVDATA(dmv_x, dmv_y);
                    }
                    vc1_pred_mv(v, i, dmv_x, dmv_y, 0, v->range_x, v->range_y, v->mb_type[0], 0, 0);
                    if (!s->mb_intra)
                        vc1_mc_4mv_luma(v, i, 0);
                    intra_count += s->mb_intra;
                    is_intra[i]  = s->mb_intra;
                    is_coded[i]  = mb_has_coeffs;
                }
                if (i & 4) {
                    is_intra[i] = (intra_count >= 3);
                    is_coded[i] = val;
                }
                if (i == 4)
                    vc1_mc_4mv_chroma(v, 0);
                v->mb_type[0][s->block_index[i]] = is_intra[i];
                if (!coded_inter)
                    coded_inter = !is_intra[i] & is_coded[i];
            }
            // if there are no coded blocks then don't do anything more
            dst_idx = 0;
            if (!intra_count && !coded_inter)
                goto end;
            GET_MQUANT();
            s->current_picture.f.qscale_table[mb_pos] = mquant;
            /* test if block is intra and has pred */
            {
                int intrapred = 0;
                for (i = 0; i < 6; i++)
                    if (is_intra[i]) {
                        if (((!s->first_slice_line || (i == 2 || i == 3)) && v->mb_type[0][s->block_index[i] - s->block_wrap[i]])
                            || ((s->mb_x || (i == 1 || i == 3)) && v->mb_type[0][s->block_index[i] - 1])) {
                            intrapred = 1;
                            break;
                        }
                    }
                if (intrapred)
                    s->ac_pred = get_bits1(gb);
                else
                    s->ac_pred = 0;
            }
            if (!v->ttmbf && coded_inter)
                ttmb = get_vlc2(gb, ff_vc1_ttmb_vlc[v->tt_index].table, VC1_TTMB_VLC_BITS, 2);
            for (i = 0; i < 6; i++) {
                dst_idx    += i >> 2;
                off         = (i & 4) ? 0 : ((i & 1) * 8 + (i & 2) * 4 * s->linesize);
                s->mb_intra = is_intra[i];
                if (is_intra[i]) {
                    /* check if prediction blocks A and C are available */
                    v->a_avail = v->c_avail = 0;
                    if (i == 2 || i == 3 || !s->first_slice_line)
                        v->a_avail = v->mb_type[0][s->block_index[i] - s->block_wrap[i]];
                    if (i == 1 || i == 3 || s->mb_x)
                        v->c_avail = v->mb_type[0][s->block_index[i] - 1];

                    vc1_decode_intra_block(v, s->block[i], i, is_coded[i], mquant,
                                           (i & 4) ? v->codingset2 : v->codingset);
                    if ((i>3) && (s->flags & CODEC_FLAG_GRAY))
                        continue;
                    v->vc1dsp.vc1_inv_trans_8x8(s->block[i]);
                    if (v->rangeredfrm)
                        for (j = 0; j < 64; j++)
                            s->block[i][j] <<= 1;
                    s->dsp.put_signed_pixels_clamped(s->block[i], s->dest[dst_idx] + off,
                                                     (i & 4) ? s->uvlinesize : s->linesize);
                    if (v->pq >= 9 && v->overlap) {
                        if (v->c_avail)
                            v->vc1dsp.vc1_h_overlap(s->dest[dst_idx] + off, i & 4 ? s->uvlinesize : s->linesize);
                        if (v->a_avail)
                            v->vc1dsp.vc1_v_overlap(s->dest[dst_idx] + off, i & 4 ? s->uvlinesize : s->linesize);
                    }
                    block_cbp   |= 0xF << (i << 2);
                    block_intra |= 1 << i;
                } else if (is_coded[i]) {
                    pat = vc1_decode_p_block(v, s->block[i], i, mquant, ttmb,
                                             first_block, s->dest[dst_idx] + off,
                                             (i & 4) ? s->uvlinesize : s->linesize,
                                             (i & 4) && (s->flags & CODEC_FLAG_GRAY),
                                             &block_tt);
                    block_cbp |= pat << (i << 2);
                    if (!v->ttmbf && ttmb < 8)
                        ttmb = -1;
                    first_block = 0;
                }
            }
        } else { // skipped MB
            s->mb_intra                               = 0;
            s->current_picture.f.qscale_table[mb_pos] = 0;
            for (i = 0; i < 6; i++) {
                v->mb_type[0][s->block_index[i]] = 0;
                s->dc_val[0][s->block_index[i]]  = 0;
            }
            for (i = 0; i < 4; i++) {
                vc1_pred_mv(v, i, 0, 0, 0, v->range_x, v->range_y, v->mb_type[0], 0, 0);
                vc1_mc_4mv_luma(v, i, 0);
            }
            vc1_mc_4mv_chroma(v, 0);
            s->current_picture.f.qscale_table[mb_pos] = 0;
        }
    }
end:
    v->cbp[s->mb_x]      = block_cbp;
    v->ttblk[s->mb_x]    = block_tt;
    v->is_intra[s->mb_x] = block_intra;

    return 0;
}

/* Decode one macroblock in an interlaced frame p picture */

static int vc1_decode_p_mb_intfr(VC1Context *v)
{
    MpegEncContext *s = &v->s;
    GetBitContext *gb = &s->gb;
    int i;
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;
    int cbp = 0; /* cbp decoding stuff */
    int mqdiff, mquant; /* MB quantization */
    int ttmb = v->ttfrm; /* MB Transform type */

    int mb_has_coeffs = 1; /* last_flag */
    int dmv_x, dmv_y; /* Differential MV components */
    int val; /* temp value */
    int first_block = 1;
    int dst_idx, off;
    int skipped, fourmv = 0, twomv = 0;
    int block_cbp = 0, pat, block_tt = 0;
    int idx_mbmode = 0, mvbp;
    int stride_y, fieldtx;

    mquant = v->pq; /* Loosy initialization */

    if (v->skip_is_raw)
        skipped = get_bits1(gb);
    else
        skipped = v->s.mbskip_table[mb_pos];
    if (!skipped) {
        if (v->fourmvswitch)
            idx_mbmode = get_vlc2(gb, v->mbmode_vlc->table, VC1_INTFR_4MV_MBMODE_VLC_BITS, 2); // try getting this done
        else
            idx_mbmode = get_vlc2(gb, v->mbmode_vlc->table, VC1_INTFR_NON4MV_MBMODE_VLC_BITS, 2); // in a single line
        switch (ff_vc1_mbmode_intfrp[v->fourmvswitch][idx_mbmode][0]) {
        /* store the motion vector type in a flag (useful later) */
        case MV_PMODE_INTFR_4MV:
            fourmv = 1;
            v->blk_mv_type[s->block_index[0]] = 0;
            v->blk_mv_type[s->block_index[1]] = 0;
            v->blk_mv_type[s->block_index[2]] = 0;
            v->blk_mv_type[s->block_index[3]] = 0;
            break;
        case MV_PMODE_INTFR_4MV_FIELD:
            fourmv = 1;
            v->blk_mv_type[s->block_index[0]] = 1;
            v->blk_mv_type[s->block_index[1]] = 1;
            v->blk_mv_type[s->block_index[2]] = 1;
            v->blk_mv_type[s->block_index[3]] = 1;
            break;
        case MV_PMODE_INTFR_2MV_FIELD:
            twomv = 1;
            v->blk_mv_type[s->block_index[0]] = 1;
            v->blk_mv_type[s->block_index[1]] = 1;
            v->blk_mv_type[s->block_index[2]] = 1;
            v->blk_mv_type[s->block_index[3]] = 1;
            break;
        case MV_PMODE_INTFR_1MV:
            v->blk_mv_type[s->block_index[0]] = 0;
            v->blk_mv_type[s->block_index[1]] = 0;
            v->blk_mv_type[s->block_index[2]] = 0;
            v->blk_mv_type[s->block_index[3]] = 0;
            break;
        }
        if (ff_vc1_mbmode_intfrp[v->fourmvswitch][idx_mbmode][0] == MV_PMODE_INTFR_INTRA) { // intra MB
            s->current_picture.f.motion_val[1][s->block_index[0]][0] = 0;
            s->current_picture.f.motion_val[1][s->block_index[0]][1] = 0;
            s->current_picture.f.mb_type[mb_pos]                     = MB_TYPE_INTRA;
            s->mb_intra = v->is_intra[s->mb_x] = 1;
            for (i = 0; i < 6; i++)
                v->mb_type[0][s->block_index[i]] = 1;
            fieldtx = v->fieldtx_plane[mb_pos] = get_bits1(gb);
            mb_has_coeffs = get_bits1(gb);
            if (mb_has_coeffs)
                cbp = 1 + get_vlc2(&v->s.gb, v->cbpcy_vlc->table, VC1_CBPCY_P_VLC_BITS, 2);
            v->s.ac_pred = v->acpred_plane[mb_pos] = get_bits1(gb);
            GET_MQUANT();
            s->current_picture.f.qscale_table[mb_pos] = mquant;
            /* Set DC scale - y and c use the same (not sure if necessary here) */
            s->y_dc_scale = s->y_dc_scale_table[mquant];
            s->c_dc_scale = s->c_dc_scale_table[mquant];
            dst_idx = 0;
            for (i = 0; i < 6; i++) {
                s->dc_val[0][s->block_index[i]] = 0;
                dst_idx += i >> 2;
                val = ((cbp >> (5 - i)) & 1);
                v->mb_type[0][s->block_index[i]] = s->mb_intra;
                v->a_avail = v->c_avail = 0;
                if (i == 2 || i == 3 || !s->first_slice_line)
                    v->a_avail = v->mb_type[0][s->block_index[i] - s->block_wrap[i]];
                if (i == 1 || i == 3 || s->mb_x)
                    v->c_avail = v->mb_type[0][s->block_index[i] - 1];

                vc1_decode_intra_block(v, s->block[i], i, val, mquant,
                                       (i & 4) ? v->codingset2 : v->codingset);
                if ((i>3) && (s->flags & CODEC_FLAG_GRAY)) continue;
                v->vc1dsp.vc1_inv_trans_8x8(s->block[i]);
                if (i < 4) {
                    stride_y = s->linesize << fieldtx;
                    off = (fieldtx) ? ((i & 1) * 8) + ((i & 2) >> 1) * s->linesize : (i & 1) * 8 + 4 * (i & 2) * s->linesize;
                } else {
                    stride_y = s->uvlinesize;
                    off = 0;
                }
                s->dsp.put_signed_pixels_clamped(s->block[i], s->dest[dst_idx] + off, stride_y);
                //TODO: loop filter
            }

        } else { // inter MB
            mb_has_coeffs = ff_vc1_mbmode_intfrp[v->fourmvswitch][idx_mbmode][3];
            if (mb_has_coeffs)
                cbp = 1 + get_vlc2(&v->s.gb, v->cbpcy_vlc->table, VC1_CBPCY_P_VLC_BITS, 2);
            if (ff_vc1_mbmode_intfrp[v->fourmvswitch][idx_mbmode][0] == MV_PMODE_INTFR_2MV_FIELD) {
                v->twomvbp = get_vlc2(gb, v->twomvbp_vlc->table, VC1_2MV_BLOCK_PATTERN_VLC_BITS, 1);
            } else {
                if ((ff_vc1_mbmode_intfrp[v->fourmvswitch][idx_mbmode][0] == MV_PMODE_INTFR_4MV)
                    || (ff_vc1_mbmode_intfrp[v->fourmvswitch][idx_mbmode][0] == MV_PMODE_INTFR_4MV_FIELD)) {
                    v->fourmvbp = get_vlc2(gb, v->fourmvbp_vlc->table, VC1_4MV_BLOCK_PATTERN_VLC_BITS, 1);
                }
            }
            s->mb_intra = v->is_intra[s->mb_x] = 0;
            for (i = 0; i < 6; i++)
                v->mb_type[0][s->block_index[i]] = 0;
            fieldtx = v->fieldtx_plane[mb_pos] = ff_vc1_mbmode_intfrp[v->fourmvswitch][idx_mbmode][1];
            /* for all motion vector read MVDATA and motion compensate each block */
            dst_idx = 0;
            if (fourmv) {
                mvbp = v->fourmvbp;
                for (i = 0; i < 6; i++) {
                    if (i < 4) {
                        dmv_x = dmv_y = 0;
                        val   = ((mvbp >> (3 - i)) & 1);
                        if (val) {
                            get_mvdata_interlaced(v, &dmv_x, &dmv_y, 0);
                        }
                        vc1_pred_mv_intfr(v, i, dmv_x, dmv_y, 0, v->range_x, v->range_y, v->mb_type[0]);
                        vc1_mc_4mv_luma(v, i, 0);
                    } else if (i == 4) {
                        vc1_mc_4mv_chroma4(v);
                    }
                }
            } else if (twomv) {
                mvbp  = v->twomvbp;
                dmv_x = dmv_y = 0;
                if (mvbp & 2) {
                    get_mvdata_interlaced(v, &dmv_x, &dmv_y, 0);
                }
                vc1_pred_mv_intfr(v, 0, dmv_x, dmv_y, 2, v->range_x, v->range_y, v->mb_type[0]);
                vc1_mc_4mv_luma(v, 0, 0);
                vc1_mc_4mv_luma(v, 1, 0);
                dmv_x = dmv_y = 0;
                if (mvbp & 1) {
                    get_mvdata_interlaced(v, &dmv_x, &dmv_y, 0);
                }
                vc1_pred_mv_intfr(v, 2, dmv_x, dmv_y, 2, v->range_x, v->range_y, v->mb_type[0]);
                vc1_mc_4mv_luma(v, 2, 0);
                vc1_mc_4mv_luma(v, 3, 0);
                vc1_mc_4mv_chroma4(v);
            } else {
                mvbp = ff_vc1_mbmode_intfrp[v->fourmvswitch][idx_mbmode][2];
                if (mvbp) {
                    get_mvdata_interlaced(v, &dmv_x, &dmv_y, 0);
                }
                vc1_pred_mv_intfr(v, 0, dmv_x, dmv_y, 1, v->range_x, v->range_y, v->mb_type[0]);
                vc1_mc_1mv(v, 0);
            }
            if (cbp)
                GET_MQUANT();  // p. 227
            s->current_picture.f.qscale_table[mb_pos] = mquant;
            if (!v->ttmbf && cbp)
                ttmb = get_vlc2(gb, ff_vc1_ttmb_vlc[v->tt_index].table, VC1_TTMB_VLC_BITS, 2);
            for (i = 0; i < 6; i++) {
                s->dc_val[0][s->block_index[i]] = 0;
                dst_idx += i >> 2;
                val = ((cbp >> (5 - i)) & 1);
                if (!fieldtx)
                    off = (i & 4) ? 0 : ((i & 1) * 8 + (i & 2) * 4 * s->linesize);
                else
                    off = (i & 4) ? 0 : ((i & 1) * 8 + ((i > 1) * s->linesize));
                if (val) {
                    pat = vc1_decode_p_block(v, s->block[i], i, mquant, ttmb,
                                             first_block, s->dest[dst_idx] + off,
                                             (i & 4) ? s->uvlinesize : (s->linesize << fieldtx),
                                             (i & 4) && (s->flags & CODEC_FLAG_GRAY), &block_tt);
                    block_cbp |= pat << (i << 2);
                    if (!v->ttmbf && ttmb < 8)
                        ttmb = -1;
                    first_block = 0;
                }
            }
        }
    } else { // skipped
        s->mb_intra = v->is_intra[s->mb_x] = 0;
        for (i = 0; i < 6; i++) {
            v->mb_type[0][s->block_index[i]] = 0;
            s->dc_val[0][s->block_index[i]] = 0;
        }
        s->current_picture.f.mb_type[mb_pos]      = MB_TYPE_SKIP;
        s->current_picture.f.qscale_table[mb_pos] = 0;
        v->blk_mv_type[s->block_index[0]] = 0;
        v->blk_mv_type[s->block_index[1]] = 0;
        v->blk_mv_type[s->block_index[2]] = 0;
        v->blk_mv_type[s->block_index[3]] = 0;
        vc1_pred_mv_intfr(v, 0, 0, 0, 1, v->range_x, v->range_y, v->mb_type[0]);
        vc1_mc_1mv(v, 0);
    }
    if (s->mb_x == s->mb_width - 1)
        memmove(v->is_intra_base, v->is_intra, sizeof(v->is_intra_base[0])*s->mb_stride);
    return 0;
}

static int vc1_decode_p_mb_intfi(VC1Context *v)
{
    MpegEncContext *s = &v->s;
    GetBitContext *gb = &s->gb;
    int i;
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;
    int cbp = 0; /* cbp decoding stuff */
    int mqdiff, mquant; /* MB quantization */
    int ttmb = v->ttfrm; /* MB Transform type */

    int mb_has_coeffs = 1; /* last_flag */
    int dmv_x, dmv_y; /* Differential MV components */
    int val; /* temp values */
    int first_block = 1;
    int dst_idx, off;
    int pred_flag;
    int block_cbp = 0, pat, block_tt = 0;
    int idx_mbmode = 0;

    mquant = v->pq; /* Loosy initialization */

    idx_mbmode = get_vlc2(gb, v->mbmode_vlc->table, VC1_IF_MBMODE_VLC_BITS, 2);
    if (idx_mbmode <= 1) { // intra MB
        s->mb_intra = v->is_intra[s->mb_x] = 1;
        s->current_picture.f.motion_val[1][s->block_index[0] + v->blocks_off][0] = 0;
        s->current_picture.f.motion_val[1][s->block_index[0] + v->blocks_off][1] = 0;
        s->current_picture.f.mb_type[mb_pos + v->mb_off] = MB_TYPE_INTRA;
        GET_MQUANT();
        s->current_picture.f.qscale_table[mb_pos] = mquant;
        /* Set DC scale - y and c use the same (not sure if necessary here) */
        s->y_dc_scale = s->y_dc_scale_table[mquant];
        s->c_dc_scale = s->c_dc_scale_table[mquant];
        v->s.ac_pred  = v->acpred_plane[mb_pos] = get_bits1(gb);
        mb_has_coeffs = idx_mbmode & 1;
        if (mb_has_coeffs)
            cbp = 1 + get_vlc2(&v->s.gb, v->cbpcy_vlc->table, VC1_ICBPCY_VLC_BITS, 2);
        dst_idx = 0;
        for (i = 0; i < 6; i++) {
            s->dc_val[0][s->block_index[i]]  = 0;
            v->mb_type[0][s->block_index[i]] = 1;
            dst_idx += i >> 2;
            val = ((cbp >> (5 - i)) & 1);
            v->a_avail = v->c_avail = 0;
            if (i == 2 || i == 3 || !s->first_slice_line)
                v->a_avail = v->mb_type[0][s->block_index[i] - s->block_wrap[i]];
            if (i == 1 || i == 3 || s->mb_x)
                v->c_avail = v->mb_type[0][s->block_index[i] - 1];

            vc1_decode_intra_block(v, s->block[i], i, val, mquant,
                                   (i & 4) ? v->codingset2 : v->codingset);
            if ((i>3) && (s->flags & CODEC_FLAG_GRAY))
                continue;
            v->vc1dsp.vc1_inv_trans_8x8(s->block[i]);
            off  = (i & 4) ? 0 : ((i & 1) * 8 + (i & 2) * 4 * s->linesize);
            off += v->second_field ? ((i & 4) ? s->current_picture_ptr->f.linesize[1] : s->current_picture_ptr->f.linesize[0]) : 0;
            s->dsp.put_signed_pixels_clamped(s->block[i], s->dest[dst_idx] + off, (i & 4) ? s->uvlinesize : s->linesize);
            // TODO: loop filter
        }
    } else {
        s->mb_intra = v->is_intra[s->mb_x] = 0;
        s->current_picture.f.mb_type[mb_pos + v->mb_off] = MB_TYPE_16x16;
        for (i = 0; i < 6; i++) v->mb_type[0][s->block_index[i]] = 0;
        if (idx_mbmode <= 5) { // 1-MV
            dmv_x = dmv_y = 0;
            if (idx_mbmode & 1) {
                get_mvdata_interlaced(v, &dmv_x, &dmv_y, &pred_flag);
            }
            vc1_pred_mv(v, 0, dmv_x, dmv_y, 1, v->range_x, v->range_y, v->mb_type[0], pred_flag, 0);
            vc1_mc_1mv(v, 0);
            mb_has_coeffs = !(idx_mbmode & 2);
        } else { // 4-MV
            v->fourmvbp = get_vlc2(gb, v->fourmvbp_vlc->table, VC1_4MV_BLOCK_PATTERN_VLC_BITS, 1);
            for (i = 0; i < 6; i++) {
                if (i < 4) {
                    dmv_x = dmv_y = pred_flag = 0;
                    val   = ((v->fourmvbp >> (3 - i)) & 1);
                    if (val) {
                        get_mvdata_interlaced(v, &dmv_x, &dmv_y, &pred_flag);
                    }
                    vc1_pred_mv(v, i, dmv_x, dmv_y, 0, v->range_x, v->range_y, v->mb_type[0], pred_flag, 0);
                    vc1_mc_4mv_luma(v, i, 0);
                } else if (i == 4)
                    vc1_mc_4mv_chroma(v, 0);
            }
            mb_has_coeffs = idx_mbmode & 1;
        }
        if (mb_has_coeffs)
            cbp = 1 + get_vlc2(&v->s.gb, v->cbpcy_vlc->table, VC1_CBPCY_P_VLC_BITS, 2);
        if (cbp) {
            GET_MQUANT();
        }
        s->current_picture.f.qscale_table[mb_pos] = mquant;
        if (!v->ttmbf && cbp) {
            ttmb = get_vlc2(gb, ff_vc1_ttmb_vlc[v->tt_index].table, VC1_TTMB_VLC_BITS, 2);
        }
        dst_idx = 0;
        for (i = 0; i < 6; i++) {
            s->dc_val[0][s->block_index[i]] = 0;
            dst_idx += i >> 2;
            val = ((cbp >> (5 - i)) & 1);
            off = (i & 4) ? 0 : (i & 1) * 8 + (i & 2) * 4 * s->linesize;
            if (v->second_field)
                off += (i & 4) ? s->current_picture_ptr->f.linesize[1] : s->current_picture_ptr->f.linesize[0];
            if (val) {
                pat = vc1_decode_p_block(v, s->block[i], i, mquant, ttmb,
                                         first_block, s->dest[dst_idx] + off,
                                         (i & 4) ? s->uvlinesize : s->linesize,
                                         (i & 4) && (s->flags & CODEC_FLAG_GRAY),
                                         &block_tt);
                block_cbp |= pat << (i << 2);
                if (!v->ttmbf && ttmb < 8) ttmb = -1;
                first_block = 0;
            }
        }
    }
    if (s->mb_x == s->mb_width - 1)
        memmove(v->is_intra_base, v->is_intra, sizeof(v->is_intra_base[0]) * s->mb_stride);
    return 0;
}

/** Decode one B-frame MB (in Main profile)
 */
static void vc1_decode_b_mb(VC1Context *v)
{
    MpegEncContext *s = &v->s;
    GetBitContext *gb = &s->gb;
    int i, j;
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;
    int cbp = 0; /* cbp decoding stuff */
    int mqdiff, mquant; /* MB quantization */
    int ttmb = v->ttfrm; /* MB Transform type */
    int mb_has_coeffs = 0; /* last_flag */
    int index, index1; /* LUT indexes */
    int val, sign; /* temp values */
    int first_block = 1;
    int dst_idx, off;
    int skipped, direct;
    int dmv_x[2], dmv_y[2];
    int bmvtype = BMV_TYPE_BACKWARD;

    mquant      = v->pq; /* lossy initialization */
    s->mb_intra = 0;

    if (v->dmb_is_raw)
        direct = get_bits1(gb);
    else
        direct = v->direct_mb_plane[mb_pos];
    if (v->skip_is_raw)
        skipped = get_bits1(gb);
    else
        skipped = v->s.mbskip_table[mb_pos];

    dmv_x[0] = dmv_x[1] = dmv_y[0] = dmv_y[1] = 0;
    for (i = 0; i < 6; i++) {
        v->mb_type[0][s->block_index[i]] = 0;
        s->dc_val[0][s->block_index[i]]  = 0;
    }
    s->current_picture.f.qscale_table[mb_pos] = 0;

    if (!direct) {
        if (!skipped) {
            GET_MVDATA(dmv_x[0], dmv_y[0]);
            dmv_x[1] = dmv_x[0];
            dmv_y[1] = dmv_y[0];
        }
        if (skipped || !s->mb_intra) {
            bmvtype = decode012(gb);
            switch (bmvtype) {
            case 0:
                bmvtype = (v->bfraction >= (B_FRACTION_DEN/2)) ? BMV_TYPE_BACKWARD : BMV_TYPE_FORWARD;
                break;
            case 1:
                bmvtype = (v->bfraction >= (B_FRACTION_DEN/2)) ? BMV_TYPE_FORWARD : BMV_TYPE_BACKWARD;
                break;
            case 2:
                bmvtype  = BMV_TYPE_INTERPOLATED;
                dmv_x[0] = dmv_y[0] = 0;
            }
        }
    }
    for (i = 0; i < 6; i++)
        v->mb_type[0][s->block_index[i]] = s->mb_intra;

    if (skipped) {
        if (direct)
            bmvtype = BMV_TYPE_INTERPOLATED;
        vc1_pred_b_mv(v, dmv_x, dmv_y, direct, bmvtype);
        vc1_b_mc(v, dmv_x, dmv_y, direct, bmvtype);
        return;
    }
    if (direct) {
        cbp = get_vlc2(&v->s.gb, v->cbpcy_vlc->table, VC1_CBPCY_P_VLC_BITS, 2);
        GET_MQUANT();
        s->mb_intra = 0;
        s->current_picture.f.qscale_table[mb_pos] = mquant;
        if (!v->ttmbf)
            ttmb = get_vlc2(gb, ff_vc1_ttmb_vlc[v->tt_index].table, VC1_TTMB_VLC_BITS, 2);
        dmv_x[0] = dmv_y[0] = dmv_x[1] = dmv_y[1] = 0;
        vc1_pred_b_mv(v, dmv_x, dmv_y, direct, bmvtype);
        vc1_b_mc(v, dmv_x, dmv_y, direct, bmvtype);
    } else {
        if (!mb_has_coeffs && !s->mb_intra) {
            /* no coded blocks - effectively skipped */
            vc1_pred_b_mv(v, dmv_x, dmv_y, direct, bmvtype);
            vc1_b_mc(v, dmv_x, dmv_y, direct, bmvtype);
            return;
        }
        if (s->mb_intra && !mb_has_coeffs) {
            GET_MQUANT();
            s->current_picture.f.qscale_table[mb_pos] = mquant;
            s->ac_pred = get_bits1(gb);
            cbp = 0;
            vc1_pred_b_mv(v, dmv_x, dmv_y, direct, bmvtype);
        } else {
            if (bmvtype == BMV_TYPE_INTERPOLATED) {
                GET_MVDATA(dmv_x[0], dmv_y[0]);
                if (!mb_has_coeffs) {
                    /* interpolated skipped block */
                    vc1_pred_b_mv(v, dmv_x, dmv_y, direct, bmvtype);
                    vc1_b_mc(v, dmv_x, dmv_y, direct, bmvtype);
                    return;
                }
            }
            vc1_pred_b_mv(v, dmv_x, dmv_y, direct, bmvtype);
            if (!s->mb_intra) {
                vc1_b_mc(v, dmv_x, dmv_y, direct, bmvtype);
            }
            if (s->mb_intra)
                s->ac_pred = get_bits1(gb);
            cbp = get_vlc2(&v->s.gb, v->cbpcy_vlc->table, VC1_CBPCY_P_VLC_BITS, 2);
            GET_MQUANT();
            s->current_picture.f.qscale_table[mb_pos] = mquant;
            if (!v->ttmbf && !s->mb_intra && mb_has_coeffs)
                ttmb = get_vlc2(gb, ff_vc1_ttmb_vlc[v->tt_index].table, VC1_TTMB_VLC_BITS, 2);
        }
    }
    dst_idx = 0;
    for (i = 0; i < 6; i++) {
        s->dc_val[0][s->block_index[i]] = 0;
        dst_idx += i >> 2;
        val = ((cbp >> (5 - i)) & 1);
        off = (i & 4) ? 0 : ((i & 1) * 8 + (i & 2) * 4 * s->linesize);
        v->mb_type[0][s->block_index[i]] = s->mb_intra;
        if (s->mb_intra) {
            /* check if prediction blocks A and C are available */
            v->a_avail = v->c_avail = 0;
            if (i == 2 || i == 3 || !s->first_slice_line)
                v->a_avail = v->mb_type[0][s->block_index[i] - s->block_wrap[i]];
            if (i == 1 || i == 3 || s->mb_x)
                v->c_avail = v->mb_type[0][s->block_index[i] - 1];

            vc1_decode_intra_block(v, s->block[i], i, val, mquant,
                                   (i & 4) ? v->codingset2 : v->codingset);
            if ((i>3) && (s->flags & CODEC_FLAG_GRAY))
                continue;
            v->vc1dsp.vc1_inv_trans_8x8(s->block[i]);
            if (v->rangeredfrm)
                for (j = 0; j < 64; j++)
                    s->block[i][j] <<= 1;
            s->dsp.put_signed_pixels_clamped(s->block[i], s->dest[dst_idx] + off, i & 4 ? s->uvlinesize : s->linesize);
        } else if (val) {
            vc1_decode_p_block(v, s->block[i], i, mquant, ttmb,
                               first_block, s->dest[dst_idx] + off,
                               (i & 4) ? s->uvlinesize : s->linesize,
                               (i & 4) && (s->flags & CODEC_FLAG_GRAY), NULL);
            if (!v->ttmbf && ttmb < 8)
                ttmb = -1;
            first_block = 0;
        }
    }
}

/** Decode one B-frame MB (in interlaced field B picture)
 */
static void vc1_decode_b_mb_intfi(VC1Context *v)
{
    MpegEncContext *s = &v->s;
    GetBitContext *gb = &s->gb;
    int i, j;
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;
    int cbp = 0; /* cbp decoding stuff */
    int mqdiff, mquant; /* MB quantization */
    int ttmb = v->ttfrm; /* MB Transform type */
    int mb_has_coeffs = 0; /* last_flag */
    int val; /* temp value */
    int first_block = 1;
    int dst_idx, off;
    int fwd;
    int dmv_x[2], dmv_y[2], pred_flag[2];
    int bmvtype = BMV_TYPE_BACKWARD;
    int idx_mbmode, interpmvp;

    mquant      = v->pq; /* Loosy initialization */
    s->mb_intra = 0;

    idx_mbmode = get_vlc2(gb, v->mbmode_vlc->table, VC1_IF_MBMODE_VLC_BITS, 2);
    if (idx_mbmode <= 1) { // intra MB
        s->mb_intra = v->is_intra[s->mb_x] = 1;
        s->current_picture.f.motion_val[1][s->block_index[0]][0] = 0;
        s->current_picture.f.motion_val[1][s->block_index[0]][1] = 0;
        s->current_picture.f.mb_type[mb_pos + v->mb_off]         = MB_TYPE_INTRA;
        GET_MQUANT();
        s->current_picture.f.qscale_table[mb_pos] = mquant;
        /* Set DC scale - y and c use the same (not sure if necessary here) */
        s->y_dc_scale = s->y_dc_scale_table[mquant];
        s->c_dc_scale = s->c_dc_scale_table[mquant];
        v->s.ac_pred  = v->acpred_plane[mb_pos] = get_bits1(gb);
        mb_has_coeffs = idx_mbmode & 1;
        if (mb_has_coeffs)
            cbp = 1 + get_vlc2(&v->s.gb, v->cbpcy_vlc->table, VC1_ICBPCY_VLC_BITS, 2);
        dst_idx = 0;
        for (i = 0; i < 6; i++) {
            s->dc_val[0][s->block_index[i]] = 0;
            dst_idx += i >> 2;
            val = ((cbp >> (5 - i)) & 1);
            v->mb_type[0][s->block_index[i]] = s->mb_intra;
            v->a_avail                       = v->c_avail = 0;
            if (i == 2 || i == 3 || !s->first_slice_line)
                v->a_avail = v->mb_type[0][s->block_index[i] - s->block_wrap[i]];
            if (i == 1 || i == 3 || s->mb_x)
                v->c_avail = v->mb_type[0][s->block_index[i] - 1];

            vc1_decode_intra_block(v, s->block[i], i, val, mquant,
                                   (i & 4) ? v->codingset2 : v->codingset);
            if ((i>3) && (s->flags & CODEC_FLAG_GRAY))
                continue;
            v->vc1dsp.vc1_inv_trans_8x8(s->block[i]);
            if (v->rangeredfrm)
                for (j = 0; j < 64; j++)
                    s->block[i][j] <<= 1;
            off  = (i & 4) ? 0 : ((i & 1) * 8 + (i & 2) * 4 * s->linesize);
            off += v->second_field ? ((i & 4) ? s->current_picture_ptr->f.linesize[1] : s->current_picture_ptr->f.linesize[0]) : 0;
            s->dsp.put_signed_pixels_clamped(s->block[i], s->dest[dst_idx] + off, (i & 4) ? s->uvlinesize : s->linesize);
            // TODO: yet to perform loop filter
        }
    } else {
        s->mb_intra = v->is_intra[s->mb_x] = 0;
        s->current_picture.f.mb_type[mb_pos + v->mb_off] = MB_TYPE_16x16;
        for (i = 0; i < 6; i++) v->mb_type[0][s->block_index[i]] = 0;
        if (v->fmb_is_raw)
            fwd = v->forward_mb_plane[mb_pos] = get_bits1(gb);
        else
            fwd = v->forward_mb_plane[mb_pos];
        if (idx_mbmode <= 5) { // 1-MV
            dmv_x[0]     = dmv_x[1] = dmv_y[0] = dmv_y[1] = 0;
            pred_flag[0] = pred_flag[1] = 0;
            if (fwd)
                bmvtype = BMV_TYPE_FORWARD;
            else {
                bmvtype = decode012(gb);
                switch (bmvtype) {
                case 0:
                    bmvtype = BMV_TYPE_BACKWARD;
                    break;
                case 1:
                    bmvtype = BMV_TYPE_DIRECT;
                    break;
                case 2:
                    bmvtype   = BMV_TYPE_INTERPOLATED;
                    interpmvp = get_bits1(gb);
                }
            }
            v->bmvtype = bmvtype;
            if (bmvtype != BMV_TYPE_DIRECT && idx_mbmode & 1) {
                get_mvdata_interlaced(v, &dmv_x[bmvtype == BMV_TYPE_BACKWARD], &dmv_y[bmvtype == BMV_TYPE_BACKWARD], &pred_flag[bmvtype == BMV_TYPE_BACKWARD]);
            }
            if (bmvtype == BMV_TYPE_INTERPOLATED && interpmvp) {
                get_mvdata_interlaced(v, &dmv_x[1], &dmv_y[1], &pred_flag[1]);
            }
            if (bmvtype == BMV_TYPE_DIRECT) {
                dmv_x[0] = dmv_y[0] = pred_flag[0] = 0;
                dmv_x[1] = dmv_y[1] = pred_flag[0] = 0;
            }
            vc1_pred_b_mv_intfi(v, 0, dmv_x, dmv_y, 1, pred_flag);
            vc1_b_mc(v, dmv_x, dmv_y, (bmvtype == BMV_TYPE_DIRECT), bmvtype);
            mb_has_coeffs = !(idx_mbmode & 2);
        } else { // 4-MV
            if (fwd)
                bmvtype = BMV_TYPE_FORWARD;
            v->bmvtype  = bmvtype;
            v->fourmvbp = get_vlc2(gb, v->fourmvbp_vlc->table, VC1_4MV_BLOCK_PATTERN_VLC_BITS, 1);
            for (i = 0; i < 6; i++) {
                if (i < 4) {
                    dmv_x[0] = dmv_y[0] = pred_flag[0] = 0;
                    dmv_x[1] = dmv_y[1] = pred_flag[1] = 0;
                    val = ((v->fourmvbp >> (3 - i)) & 1);
                    if (val) {
                        get_mvdata_interlaced(v, &dmv_x[bmvtype == BMV_TYPE_BACKWARD],
                                                 &dmv_y[bmvtype == BMV_TYPE_BACKWARD],
                                             &pred_flag[bmvtype == BMV_TYPE_BACKWARD]);
                    }
                    vc1_pred_b_mv_intfi(v, i, dmv_x, dmv_y, 0, pred_flag);
                    vc1_mc_4mv_luma(v, i, bmvtype == BMV_TYPE_BACKWARD);
                } else if (i == 4)
                    vc1_mc_4mv_chroma(v, bmvtype == BMV_TYPE_BACKWARD);
            }
            mb_has_coeffs = idx_mbmode & 1;
        }
        if (mb_has_coeffs)
            cbp = 1 + get_vlc2(&v->s.gb, v->cbpcy_vlc->table, VC1_CBPCY_P_VLC_BITS, 2);
        if (cbp) {
            GET_MQUANT();
        }
        s->current_picture.f.qscale_table[mb_pos] = mquant;
        if (!v->ttmbf && cbp) {
            ttmb = get_vlc2(gb, ff_vc1_ttmb_vlc[v->tt_index].table, VC1_TTMB_VLC_BITS, 2);
        }
        dst_idx = 0;
        for (i = 0; i < 6; i++) {
            s->dc_val[0][s->block_index[i]] = 0;
            dst_idx += i >> 2;
            val = ((cbp >> (5 - i)) & 1);
            off = (i & 4) ? 0 : (i & 1) * 8 + (i & 2) * 4 * s->linesize;
            if (v->second_field)
                off += (i & 4) ? s->current_picture_ptr->f.linesize[1] : s->current_picture_ptr->f.linesize[0];
            if (val) {
                vc1_decode_p_block(v, s->block[i], i, mquant, ttmb,
                                   first_block, s->dest[dst_idx] + off,
                                   (i & 4) ? s->uvlinesize : s->linesize,
                                   (i & 4) && (s->flags & CODEC_FLAG_GRAY), NULL);
                if (!v->ttmbf && ttmb < 8)
                    ttmb = -1;
                first_block = 0;
            }
        }
    }
}

/** Decode blocks of I-frame
 */
static void vc1_decode_i_blocks(VC1Context *v)
{
    int k, j;
    MpegEncContext *s = &v->s;
    int cbp, val;
    uint8_t *coded_val;
    int mb_pos;

    /* select codingmode used for VLC tables selection */
    switch (v->y_ac_table_index) {
    case 0:
        v->codingset = (v->pqindex <= 8) ? CS_HIGH_RATE_INTRA : CS_LOW_MOT_INTRA;
        break;
    case 1:
        v->codingset = CS_HIGH_MOT_INTRA;
        break;
    case 2:
        v->codingset = CS_MID_RATE_INTRA;
        break;
    }

    switch (v->c_ac_table_index) {
    case 0:
        v->codingset2 = (v->pqindex <= 8) ? CS_HIGH_RATE_INTER : CS_LOW_MOT_INTER;
        break;
    case 1:
        v->codingset2 = CS_HIGH_MOT_INTER;
        break;
    case 2:
        v->codingset2 = CS_MID_RATE_INTER;
        break;
    }

    /* Set DC scale - y and c use the same */
    s->y_dc_scale = s->y_dc_scale_table[v->pq];
    s->c_dc_scale = s->c_dc_scale_table[v->pq];

    //do frame decode
    s->mb_x = s->mb_y = 0;
    s->mb_intra         = 1;
    s->first_slice_line = 1;
    for (s->mb_y = 0; s->mb_y < s->mb_height; s->mb_y++) {
        s->mb_x = 0;
        ff_init_block_index(s);
        for (; s->mb_x < s->mb_width; s->mb_x++) {
            uint8_t *dst[6];
            ff_update_block_index(s);
            dst[0] = s->dest[0];
            dst[1] = dst[0] + 8;
            dst[2] = s->dest[0] + s->linesize * 8;
            dst[3] = dst[2] + 8;
            dst[4] = s->dest[1];
            dst[5] = s->dest[2];
            s->dsp.clear_blocks(s->block[0]);
            mb_pos = s->mb_x + s->mb_y * s->mb_width;
            s->current_picture.f.mb_type[mb_pos]                     = MB_TYPE_INTRA;
            s->current_picture.f.qscale_table[mb_pos]                = v->pq;
            s->current_picture.f.motion_val[1][s->block_index[0]][0] = 0;
            s->current_picture.f.motion_val[1][s->block_index[0]][1] = 0;

            // do actual MB decoding and displaying
            cbp = get_vlc2(&v->s.gb, ff_msmp4_mb_i_vlc.table, MB_INTRA_VLC_BITS, 2);
            v->s.ac_pred = get_bits1(&v->s.gb);

            for (k = 0; k < 6; k++) {
                val = ((cbp >> (5 - k)) & 1);

                if (k < 4) {
                    int pred   = vc1_coded_block_pred(&v->s, k, &coded_val);
                    val        = val ^ pred;
                    *coded_val = val;
                }
                cbp |= val << (5 - k);

                vc1_decode_i_block(v, s->block[k], k, val, (k < 4) ? v->codingset : v->codingset2);

                if (k > 3 && (s->flags & CODEC_FLAG_GRAY))
                    continue;
                v->vc1dsp.vc1_inv_trans_8x8(s->block[k]);
                if (v->pq >= 9 && v->overlap) {
                    if (v->rangeredfrm)
                        for (j = 0; j < 64; j++)
                            s->block[k][j] <<= 1;
                    s->dsp.put_signed_pixels_clamped(s->block[k], dst[k], k & 4 ? s->uvlinesize : s->linesize);
                } else {
                    if (v->rangeredfrm)
                        for (j = 0; j < 64; j++)
                            s->block[k][j] = (s->block[k][j] - 64) << 1;
                    s->dsp.put_pixels_clamped(s->block[k], dst[k], k & 4 ? s->uvlinesize : s->linesize);
                }
            }

            if (v->pq >= 9 && v->overlap) {
                if (s->mb_x) {
                    v->vc1dsp.vc1_h_overlap(s->dest[0], s->linesize);
                    v->vc1dsp.vc1_h_overlap(s->dest[0] + 8 * s->linesize, s->linesize);
                    if (!(s->flags & CODEC_FLAG_GRAY)) {
                        v->vc1dsp.vc1_h_overlap(s->dest[1], s->uvlinesize);
                        v->vc1dsp.vc1_h_overlap(s->dest[2], s->uvlinesize);
                    }
                }
                v->vc1dsp.vc1_h_overlap(s->dest[0] + 8, s->linesize);
                v->vc1dsp.vc1_h_overlap(s->dest[0] + 8 * s->linesize + 8, s->linesize);
                if (!s->first_slice_line) {
                    v->vc1dsp.vc1_v_overlap(s->dest[0], s->linesize);
                    v->vc1dsp.vc1_v_overlap(s->dest[0] + 8, s->linesize);
                    if (!(s->flags & CODEC_FLAG_GRAY)) {
                        v->vc1dsp.vc1_v_overlap(s->dest[1], s->uvlinesize);
                        v->vc1dsp.vc1_v_overlap(s->dest[2], s->uvlinesize);
                    }
                }
                v->vc1dsp.vc1_v_overlap(s->dest[0] + 8 * s->linesize, s->linesize);
                v->vc1dsp.vc1_v_overlap(s->dest[0] + 8 * s->linesize + 8, s->linesize);
            }
            if (v->s.loop_filter) vc1_loop_filter_iblk(v, v->pq);

            if (get_bits_count(&s->gb) > v->bits) {
                ff_er_add_slice(s, 0, 0, s->mb_x, s->mb_y, ER_MB_ERROR);
                av_log(s->avctx, AV_LOG_ERROR, "Bits overconsumption: %i > %i\n",
                       get_bits_count(&s->gb), v->bits);
                return;
            }
        }
        if (!v->s.loop_filter)
            ff_draw_horiz_band(s, s->mb_y * 16, 16);
        else if (s->mb_y)
            ff_draw_horiz_band(s, (s->mb_y - 1) * 16, 16);

        s->first_slice_line = 0;
    }
    if (v->s.loop_filter)
        ff_draw_horiz_band(s, (s->mb_height - 1) * 16, 16);
    ff_er_add_slice(s, 0, 0, s->mb_width - 1, s->mb_height - 1, ER_MB_END);
}

/** Decode blocks of I-frame for advanced profile
 */
static void vc1_decode_i_blocks_adv(VC1Context *v)
{
    int k;
    MpegEncContext *s = &v->s;
    int cbp, val;
    uint8_t *coded_val;
    int mb_pos;
    int mquant = v->pq;
    int mqdiff;
    GetBitContext *gb = &s->gb;

    /* select codingmode used for VLC tables selection */
    switch (v->y_ac_table_index) {
    case 0:
        v->codingset = (v->pqindex <= 8) ? CS_HIGH_RATE_INTRA : CS_LOW_MOT_INTRA;
        break;
    case 1:
        v->codingset = CS_HIGH_MOT_INTRA;
        break;
    case 2:
        v->codingset = CS_MID_RATE_INTRA;
        break;
    }

    switch (v->c_ac_table_index) {
    case 0:
        v->codingset2 = (v->pqindex <= 8) ? CS_HIGH_RATE_INTER : CS_LOW_MOT_INTER;
        break;
    case 1:
        v->codingset2 = CS_HIGH_MOT_INTER;
        break;
    case 2:
        v->codingset2 = CS_MID_RATE_INTER;
        break;
    }

    // do frame decode
    s->mb_x             = s->mb_y = 0;
    s->mb_intra         = 1;
    s->first_slice_line = 1;
    s->mb_y             = s->start_mb_y;
    if (s->start_mb_y) {
        s->mb_x = 0;
        ff_init_block_index(s);
        memset(&s->coded_block[s->block_index[0] - s->b8_stride], 0,
               (1 + s->b8_stride) * sizeof(*s->coded_block));
    }
    for (; s->mb_y < s->end_mb_y; s->mb_y++) {
        s->mb_x = 0;
        ff_init_block_index(s);
        for (;s->mb_x < s->mb_width; s->mb_x++) {
            DCTELEM (*block)[64] = v->block[v->cur_blk_idx];
            ff_update_block_index(s);
            s->dsp.clear_blocks(block[0]);
            mb_pos = s->mb_x + s->mb_y * s->mb_stride;
            s->current_picture.f.mb_type[mb_pos + v->mb_off]                         = MB_TYPE_INTRA;
            s->current_picture.f.motion_val[1][s->block_index[0] + v->blocks_off][0] = 0;
            s->current_picture.f.motion_val[1][s->block_index[0] + v->blocks_off][1] = 0;

            // do actual MB decoding and displaying
            if (v->fieldtx_is_raw)
                v->fieldtx_plane[mb_pos] = get_bits1(&v->s.gb);
            cbp = get_vlc2(&v->s.gb, ff_msmp4_mb_i_vlc.table, MB_INTRA_VLC_BITS, 2);
            if ( v->acpred_is_raw)
                v->s.ac_pred = get_bits1(&v->s.gb);
            else
                v->s.ac_pred = v->acpred_plane[mb_pos];

            if (v->condover == CONDOVER_SELECT && v->overflg_is_raw)
                v->over_flags_plane[mb_pos] = get_bits1(&v->s.gb);

            GET_MQUANT();

            s->current_picture.f.qscale_table[mb_pos] = mquant;
            /* Set DC scale - y and c use the same */
            s->y_dc_scale = s->y_dc_scale_table[mquant];
            s->c_dc_scale = s->c_dc_scale_table[mquant];

            for (k = 0; k < 6; k++) {
                val = ((cbp >> (5 - k)) & 1);

                if (k < 4) {
                    int pred   = vc1_coded_block_pred(&v->s, k, &coded_val);
                    val        = val ^ pred;
                    *coded_val = val;
                }
                cbp |= val << (5 - k);

                v->a_avail = !s->first_slice_line || (k == 2 || k == 3);
                v->c_avail = !!s->mb_x || (k == 1 || k == 3);

                vc1_decode_i_block_adv(v, block[k], k, val,
                                       (k < 4) ? v->codingset : v->codingset2, mquant);

                if (k > 3 && (s->flags & CODEC_FLAG_GRAY))
                    continue;
                v->vc1dsp.vc1_inv_trans_8x8(block[k]);
            }

            vc1_smooth_overlap_filter_iblk(v);
            vc1_put_signed_blocks_clamped(v);
            if (v->s.loop_filter) vc1_loop_filter_iblk_delayed(v, v->pq);

            if (get_bits_count(&s->gb) > v->bits) {
                // TODO: may need modification to handle slice coding
                ff_er_add_slice(s, 0, s->start_mb_y, s->mb_x, s->mb_y, ER_MB_ERROR);
                av_log(s->avctx, AV_LOG_ERROR, "Bits overconsumption: %i > %i\n",
                       get_bits_count(&s->gb), v->bits);
                return;
            }
        }
        if (!v->s.loop_filter)
            ff_draw_horiz_band(s, s->mb_y * 16, 16);
        else if (s->mb_y)
            ff_draw_horiz_band(s, (s->mb_y-1) * 16, 16);
        s->first_slice_line = 0;
    }

    /* raw bottom MB row */
    s->mb_x = 0;
    ff_init_block_index(s);
    for (;s->mb_x < s->mb_width; s->mb_x++) {
        ff_update_block_index(s);
        vc1_put_signed_blocks_clamped(v);
        if (v->s.loop_filter)
            vc1_loop_filter_iblk_delayed(v, v->pq);
    }
    if (v->s.loop_filter)
        ff_draw_horiz_band(s, (s->end_mb_y-1)*16, 16);
    ff_er_add_slice(s, 0, s->start_mb_y << v->field_mode, s->mb_width - 1,
                    (s->end_mb_y << v->field_mode) - 1, ER_MB_END);
}

static void vc1_decode_p_blocks(VC1Context *v)
{
    MpegEncContext *s = &v->s;
    int apply_loop_filter;

    /* select codingmode used for VLC tables selection */
    switch (v->c_ac_table_index) {
    case 0:
        v->codingset = (v->pqindex <= 8) ? CS_HIGH_RATE_INTRA : CS_LOW_MOT_INTRA;
        break;
    case 1:
        v->codingset = CS_HIGH_MOT_INTRA;
        break;
    case 2:
        v->codingset = CS_MID_RATE_INTRA;
        break;
    }

    switch (v->c_ac_table_index) {
    case 0:
        v->codingset2 = (v->pqindex <= 8) ? CS_HIGH_RATE_INTER : CS_LOW_MOT_INTER;
        break;
    case 1:
        v->codingset2 = CS_HIGH_MOT_INTER;
        break;
    case 2:
        v->codingset2 = CS_MID_RATE_INTER;
        break;
    }

    apply_loop_filter   = s->loop_filter && !(s->avctx->skip_loop_filter >= AVDISCARD_NONKEY);
    s->first_slice_line = 1;
    memset(v->cbp_base, 0, sizeof(v->cbp_base[0])*2*s->mb_stride);
    for (s->mb_y = s->start_mb_y; s->mb_y < s->end_mb_y; s->mb_y++) {
        s->mb_x = 0;
        ff_init_block_index(s);
        for (; s->mb_x < s->mb_width; s->mb_x++) {
            ff_update_block_index(s);

            if (v->fcm == ILACE_FIELD)
                vc1_decode_p_mb_intfi(v);
            else if (v->fcm == ILACE_FRAME)
                vc1_decode_p_mb_intfr(v);
            else vc1_decode_p_mb(v);
            if (s->mb_y != s->start_mb_y && apply_loop_filter && v->fcm == PROGRESSIVE)
                vc1_apply_p_loop_filter(v);
            if (get_bits_count(&s->gb) > v->bits || get_bits_count(&s->gb) < 0) {
                // TODO: may need modification to handle slice coding
                ff_er_add_slice(s, 0, s->start_mb_y, s->mb_x, s->mb_y, ER_MB_ERROR);
                av_log(s->avctx, AV_LOG_ERROR, "Bits overconsumption: %i > %i at %ix%i\n",
                       get_bits_count(&s->gb), v->bits, s->mb_x, s->mb_y);
                return;
            }
        }
        memmove(v->cbp_base,      v->cbp,      sizeof(v->cbp_base[0])      * s->mb_stride);
        memmove(v->ttblk_base,    v->ttblk,    sizeof(v->ttblk_base[0])    * s->mb_stride);
        memmove(v->is_intra_base, v->is_intra, sizeof(v->is_intra_base[0]) * s->mb_stride);
        memmove(v->luma_mv_base,  v->luma_mv,  sizeof(v->luma_mv_base[0])  * s->mb_stride);
        if (s->mb_y != s->start_mb_y) ff_draw_horiz_band(s, (s->mb_y - 1) * 16, 16);
        s->first_slice_line = 0;
    }
    if (apply_loop_filter) {
        s->mb_x = 0;
        ff_init_block_index(s);
        for (; s->mb_x < s->mb_width; s->mb_x++) {
            ff_update_block_index(s);
            vc1_apply_p_loop_filter(v);
        }
    }
    if (s->end_mb_y >= s->start_mb_y)
        ff_draw_horiz_band(s, (s->end_mb_y - 1) * 16, 16);
    ff_er_add_slice(s, 0, s->start_mb_y << v->field_mode, s->mb_width - 1,
                    (s->end_mb_y << v->field_mode) - 1, ER_MB_END);
}

static void vc1_decode_b_blocks(VC1Context *v)
{
    MpegEncContext *s = &v->s;

    /* select codingmode used for VLC tables selection */
    switch (v->c_ac_table_index) {
    case 0:
        v->codingset = (v->pqindex <= 8) ? CS_HIGH_RATE_INTRA : CS_LOW_MOT_INTRA;
        break;
    case 1:
        v->codingset = CS_HIGH_MOT_INTRA;
        break;
    case 2:
        v->codingset = CS_MID_RATE_INTRA;
        break;
    }

    switch (v->c_ac_table_index) {
    case 0:
        v->codingset2 = (v->pqindex <= 8) ? CS_HIGH_RATE_INTER : CS_LOW_MOT_INTER;
        break;
    case 1:
        v->codingset2 = CS_HIGH_MOT_INTER;
        break;
    case 2:
        v->codingset2 = CS_MID_RATE_INTER;
        break;
    }

    s->first_slice_line = 1;
    for (s->mb_y = s->start_mb_y; s->mb_y < s->end_mb_y; s->mb_y++) {
        s->mb_x = 0;
        ff_init_block_index(s);
        for (; s->mb_x < s->mb_width; s->mb_x++) {
            ff_update_block_index(s);

            if (v->fcm == ILACE_FIELD)
                vc1_decode_b_mb_intfi(v);
            else
                vc1_decode_b_mb(v);
            if (get_bits_count(&s->gb) > v->bits || get_bits_count(&s->gb) < 0) {
                // TODO: may need modification to handle slice coding
                ff_er_add_slice(s, 0, s->start_mb_y, s->mb_x, s->mb_y, ER_MB_ERROR);
                av_log(s->avctx, AV_LOG_ERROR, "Bits overconsumption: %i > %i at %ix%i\n",
                       get_bits_count(&s->gb), v->bits, s->mb_x, s->mb_y);
                return;
            }
            if (v->s.loop_filter) vc1_loop_filter_iblk(v, v->pq);
        }
        if (!v->s.loop_filter)
            ff_draw_horiz_band(s, s->mb_y * 16, 16);
        else if (s->mb_y)
            ff_draw_horiz_band(s, (s->mb_y - 1) * 16, 16);
        s->first_slice_line = 0;
    }
    if (v->s.loop_filter)
        ff_draw_horiz_band(s, (s->end_mb_y - 1) * 16, 16);
    ff_er_add_slice(s, 0, s->start_mb_y << v->field_mode, s->mb_width - 1,
                    (s->end_mb_y << v->field_mode) - 1, ER_MB_END);
}

static void vc1_decode_skip_blocks(VC1Context *v)
{
    MpegEncContext *s = &v->s;

    ff_er_add_slice(s, 0, s->start_mb_y, s->mb_width - 1, s->end_mb_y - 1, ER_MB_END);
    s->first_slice_line = 1;
    for (s->mb_y = s->start_mb_y; s->mb_y < s->end_mb_y; s->mb_y++) {
        s->mb_x = 0;
        ff_init_block_index(s);
        ff_update_block_index(s);
        memcpy(s->dest[0], s->last_picture.f.data[0] + s->mb_y * 16 * s->linesize,   s->linesize   * 16);
        memcpy(s->dest[1], s->last_picture.f.data[1] + s->mb_y *  8 * s->uvlinesize, s->uvlinesize *  8);
        memcpy(s->dest[2], s->last_picture.f.data[2] + s->mb_y *  8 * s->uvlinesize, s->uvlinesize *  8);
        ff_draw_horiz_band(s, s->mb_y * 16, 16);
        s->first_slice_line = 0;
    }
    s->pict_type = AV_PICTURE_TYPE_P;
}

static void vc1_decode_blocks(VC1Context *v)
{

    v->s.esc3_level_length = 0;
    if (v->x8_type) {
        ff_intrax8_decode_picture(&v->x8, 2*v->pq + v->halfpq, v->pq * !v->pquantizer);
    } else {
        v->cur_blk_idx     =  0;
        v->left_blk_idx    = -1;
        v->topleft_blk_idx =  1;
        v->top_blk_idx     =  2;
        switch (v->s.pict_type) {
        case AV_PICTURE_TYPE_I:
            if (v->profile == PROFILE_ADVANCED)
                vc1_decode_i_blocks_adv(v);
            else
                vc1_decode_i_blocks(v);
            break;
        case AV_PICTURE_TYPE_P:
            if (v->p_frame_skipped)
                vc1_decode_skip_blocks(v);
            else
                vc1_decode_p_blocks(v);
            break;
        case AV_PICTURE_TYPE_B:
            if (v->bi_type) {
                if (v->profile == PROFILE_ADVANCED)
                    vc1_decode_i_blocks_adv(v);
                else
                    vc1_decode_i_blocks(v);
            } else
                vc1_decode_b_blocks(v);
            break;
        }
    }
}

#if CONFIG_WMV3IMAGE_DECODER || CONFIG_VC1IMAGE_DECODER

typedef struct {
    /**
     * Transform coefficients for both sprites in 16.16 fixed point format,
     * in the order they appear in the bitstream:
     *  x scale
     *  rotation 1 (unused)
     *  x offset
     *  rotation 2 (unused)
     *  y scale
     *  y offset
     *  alpha
     */
    int coefs[2][7];

    int effect_type, effect_flag;
    int effect_pcount1, effect_pcount2;   ///< amount of effect parameters stored in effect_params
    int effect_params1[15], effect_params2[10]; ///< effect parameters in 16.16 fixed point format
} SpriteData;

static inline int get_fp_val(GetBitContext* gb)
{
    return (get_bits_long(gb, 30) - (1 << 29)) << 1;
}

static void vc1_sprite_parse_transform(GetBitContext* gb, int c[7])
{
    c[1] = c[3] = 0;

    switch (get_bits(gb, 2)) {
    case 0:
        c[0] = 1 << 16;
        c[2] = get_fp_val(gb);
        c[4] = 1 << 16;
        break;
    case 1:
        c[0] = c[4] = get_fp_val(gb);
        c[2] = get_fp_val(gb);
        break;
    case 2:
        c[0] = get_fp_val(gb);
        c[2] = get_fp_val(gb);
        c[4] = get_fp_val(gb);
        break;
    case 3:
        c[0] = get_fp_val(gb);
        c[1] = get_fp_val(gb);
        c[2] = get_fp_val(gb);
        c[3] = get_fp_val(gb);
        c[4] = get_fp_val(gb);
        break;
    }
    c[5] = get_fp_val(gb);
    if (get_bits1(gb))
        c[6] = get_fp_val(gb);
    else
        c[6] = 1 << 16;
}

static void vc1_parse_sprites(VC1Context *v, GetBitContext* gb, SpriteData* sd)
{
    AVCodecContext *avctx = v->s.avctx;
    int sprite, i;

    for (sprite = 0; sprite <= v->two_sprites; sprite++) {
        vc1_sprite_parse_transform(gb, sd->coefs[sprite]);
        if (sd->coefs[sprite][1] || sd->coefs[sprite][3])
            av_log_ask_for_sample(avctx, "Rotation coefficients are not zero");
        av_log(avctx, AV_LOG_DEBUG, sprite ? "S2:" : "S1:");
        for (i = 0; i < 7; i++)
            av_log(avctx, AV_LOG_DEBUG, " %d.%.3d",
                   sd->coefs[sprite][i] / (1<<16),
                   (abs(sd->coefs[sprite][i]) & 0xFFFF) * 1000 / (1 << 16));
        av_log(avctx, AV_LOG_DEBUG, "\n");
    }

    skip_bits(gb, 2);
    if (sd->effect_type = get_bits_long(gb, 30)) {
        switch (sd->effect_pcount1 = get_bits(gb, 4)) {
        case 7:
            vc1_sprite_parse_transform(gb, sd->effect_params1);
            break;
        case 14:
            vc1_sprite_parse_transform(gb, sd->effect_params1);
            vc1_sprite_parse_transform(gb, sd->effect_params1 + 7);
            break;
        default:
            for (i = 0; i < sd->effect_pcount1; i++)
                sd->effect_params1[i] = get_fp_val(gb);
        }
        if (sd->effect_type != 13 || sd->effect_params1[0] != sd->coefs[0][6]) {
            // effect 13 is simple alpha blending and matches the opacity above
            av_log(avctx, AV_LOG_DEBUG, "Effect: %d; params: ", sd->effect_type);
            for (i = 0; i < sd->effect_pcount1; i++)
                av_log(avctx, AV_LOG_DEBUG, " %d.%.2d",
                       sd->effect_params1[i] / (1 << 16),
                       (abs(sd->effect_params1[i]) & 0xFFFF) * 1000 / (1 << 16));
            av_log(avctx, AV_LOG_DEBUG, "\n");
        }

        sd->effect_pcount2 = get_bits(gb, 16);
        if (sd->effect_pcount2 > 10) {
            av_log(avctx, AV_LOG_ERROR, "Too many effect parameters\n");
            return;
        } else if (sd->effect_pcount2) {
            i = -1;
            av_log(avctx, AV_LOG_DEBUG, "Effect params 2: ");
            while (++i < sd->effect_pcount2) {
                sd->effect_params2[i] = get_fp_val(gb);
                av_log(avctx, AV_LOG_DEBUG, " %d.%.2d",
                       sd->effect_params2[i] / (1 << 16),
                       (abs(sd->effect_params2[i]) & 0xFFFF) * 1000 / (1 << 16));
            }
            av_log(avctx, AV_LOG_DEBUG, "\n");
        }
    }
    if (sd->effect_flag = get_bits1(gb))
        av_log(avctx, AV_LOG_DEBUG, "Effect flag set\n");

    if (get_bits_count(gb) >= gb->size_in_bits +
       (avctx->codec_id == CODEC_ID_WMV3IMAGE ? 64 : 0))
        av_log(avctx, AV_LOG_ERROR, "Buffer overrun\n");
    if (get_bits_count(gb) < gb->size_in_bits - 8)
        av_log(avctx, AV_LOG_WARNING, "Buffer not fully read\n");
}

static void vc1_draw_sprites(VC1Context *v, SpriteData* sd)
{
    int i, plane, row, sprite;
    int sr_cache[2][2] = { { -1, -1 }, { -1, -1 } };
    uint8_t* src_h[2][2];
    int xoff[2], xadv[2], yoff[2], yadv[2], alpha;
    int ysub[2];
    MpegEncContext *s = &v->s;

    for (i = 0; i < 2; i++) {
        xoff[i] = av_clip(sd->coefs[i][2], 0, v->sprite_width-1 << 16);
        xadv[i] = sd->coefs[i][0];
        if (xadv[i] != 1<<16 || (v->sprite_width << 16) - (v->output_width << 16) - xoff[i])
            xadv[i] = av_clip(xadv[i], 0, ((v->sprite_width<<16) - xoff[i] - 1) / v->output_width);

        yoff[i] = av_clip(sd->coefs[i][5], 0, v->sprite_height-1 << 16);
        yadv[i] = av_clip(sd->coefs[i][4], 0, ((v->sprite_height << 16) - yoff[i]) / v->output_height);
    }
    alpha = av_clip(sd->coefs[1][6], 0, (1<<16) - 1);

    for (plane = 0; plane < (s->flags&CODEC_FLAG_GRAY ? 1 : 3); plane++) {
        int width = v->output_width>>!!plane;

        for (row = 0; row < v->output_height>>!!plane; row++) {
            uint8_t *dst = v->sprite_output_frame.data[plane] +
                           v->sprite_output_frame.linesize[plane] * row;

            for (sprite = 0; sprite <= v->two_sprites; sprite++) {
                uint8_t *iplane = s->current_picture.f.data[plane];
                int      iline  = s->current_picture.f.linesize[plane];
                int      ycoord = yoff[sprite] + yadv[sprite] * row;
                int      yline  = ycoord >> 16;
                ysub[sprite] = ycoord & 0xFFFF;
                if (sprite) {
                    iplane = s->last_picture.f.data[plane];
                    iline  = s->last_picture.f.linesize[plane];
                }
                if (!(xoff[sprite] & 0xFFFF) && xadv[sprite] == 1 << 16) {
                        src_h[sprite][0] = iplane + (xoff[sprite] >> 16) +  yline      * iline;
                    if (ysub[sprite])
                        src_h[sprite][1] = iplane + (xoff[sprite] >> 16) + (yline + 1) * iline;
                } else {
                    if (sr_cache[sprite][0] != yline) {
                        if (sr_cache[sprite][1] == yline) {
                            FFSWAP(uint8_t*, v->sr_rows[sprite][0], v->sr_rows[sprite][1]);
                            FFSWAP(int,        sr_cache[sprite][0],   sr_cache[sprite][1]);
                        } else {
                            v->vc1dsp.sprite_h(v->sr_rows[sprite][0], iplane + yline * iline, xoff[sprite], xadv[sprite], width);
                            sr_cache[sprite][0] = yline;
                        }
                    }
                    if (ysub[sprite] && sr_cache[sprite][1] != yline + 1) {
                        v->vc1dsp.sprite_h(v->sr_rows[sprite][1], iplane + (yline + 1) * iline, xoff[sprite], xadv[sprite], width);
                        sr_cache[sprite][1] = yline + 1;
                    }
                    src_h[sprite][0] = v->sr_rows[sprite][0];
                    src_h[sprite][1] = v->sr_rows[sprite][1];
                }
            }

            if (!v->two_sprites) {
                if (ysub[0]) {
                    v->vc1dsp.sprite_v_single(dst, src_h[0][0], src_h[0][1], ysub[0], width);
                } else {
                    memcpy(dst, src_h[0][0], width);
                }
            } else {
                if (ysub[0] && ysub[1]) {
                    v->vc1dsp.sprite_v_double_twoscale(dst, src_h[0][0], src_h[0][1], ysub[0],
                                                       src_h[1][0], src_h[1][1], ysub[1], alpha, width);
                } else if (ysub[0]) {
                    v->vc1dsp.sprite_v_double_onescale(dst, src_h[0][0], src_h[0][1], ysub[0],
                                                       src_h[1][0], alpha, width);
                } else if (ysub[1]) {
                    v->vc1dsp.sprite_v_double_onescale(dst, src_h[1][0], src_h[1][1], ysub[1],
                                                       src_h[0][0], (1<<16)-1-alpha, width);
                } else {
                    v->vc1dsp.sprite_v_double_noscale(dst, src_h[0][0], src_h[1][0], alpha, width);
                }
            }
        }

        if (!plane) {
            for (i = 0; i < 2; i++) {
                xoff[i] >>= 1;
                yoff[i] >>= 1;
            }
        }

    }
}


static int vc1_decode_sprites(VC1Context *v, GetBitContext* gb)
{
    MpegEncContext *s     = &v->s;
    AVCodecContext *avctx = s->avctx;
    SpriteData sd;

    vc1_parse_sprites(v, gb, &sd);

    if (!s->current_picture.f.data[0]) {
        av_log(avctx, AV_LOG_ERROR, "Got no sprites\n");
        return -1;
    }

    if (v->two_sprites && (!s->last_picture_ptr || !s->last_picture.f.data[0])) {
        av_log(avctx, AV_LOG_WARNING, "Need two sprites, only got one\n");
        v->two_sprites = 0;
    }

    if (v->sprite_output_frame.data[0])
        avctx->release_buffer(avctx, &v->sprite_output_frame);

    v->sprite_output_frame.buffer_hints = FF_BUFFER_HINTS_VALID;
    v->sprite_output_frame.reference = 0;
    if (avctx->get_buffer(avctx, &v->sprite_output_frame) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    vc1_draw_sprites(v, &sd);

    return 0;
}

static void vc1_sprite_flush(AVCodecContext *avctx)
{
    VC1Context *v     = avctx->priv_data;
    MpegEncContext *s = &v->s;
    AVFrame *f = &s->current_picture.f;
    int plane, i;

    /* Windows Media Image codecs have a convergence interval of two keyframes.
       Since we can't enforce it, clear to black the missing sprite. This is
       wrong but it looks better than doing nothing. */

    if (f->data[0])
        for (plane = 0; plane < (s->flags&CODEC_FLAG_GRAY ? 1 : 3); plane++)
            for (i = 0; i < v->sprite_height>>!!plane; i++)
                memset(f->data[plane] + i * f->linesize[plane],
                       plane ? 128 : 0, f->linesize[plane]);
}

#endif

static av_cold int vc1_decode_init_alloc_tables(VC1Context *v)
{
    MpegEncContext *s = &v->s;
    int i;

    /* Allocate mb bitplanes */
    v->mv_type_mb_plane = av_malloc (s->mb_stride * s->mb_height);
    v->direct_mb_plane  = av_malloc (s->mb_stride * s->mb_height);
    v->forward_mb_plane = av_malloc (s->mb_stride * s->mb_height);
    v->fieldtx_plane    = av_mallocz(s->mb_stride * s->mb_height);
    v->acpred_plane     = av_malloc (s->mb_stride * s->mb_height);
    v->over_flags_plane = av_malloc (s->mb_stride * s->mb_height);

    v->n_allocated_blks = s->mb_width + 2;
    v->block            = av_malloc(sizeof(*v->block) * v->n_allocated_blks);
    v->cbp_base         = av_malloc(sizeof(v->cbp_base[0]) * 2 * s->mb_stride);
    v->cbp              = v->cbp_base + s->mb_stride;
    v->ttblk_base       = av_malloc(sizeof(v->ttblk_base[0]) * 2 * s->mb_stride);
    v->ttblk            = v->ttblk_base + s->mb_stride;
    v->is_intra_base    = av_mallocz(sizeof(v->is_intra_base[0]) * 2 * s->mb_stride);
    v->is_intra         = v->is_intra_base + s->mb_stride;
    v->luma_mv_base     = av_malloc(sizeof(v->luma_mv_base[0]) * 2 * s->mb_stride);
    v->luma_mv          = v->luma_mv_base + s->mb_stride;

    /* allocate block type info in that way so it could be used with s->block_index[] */
    v->mb_type_base = av_malloc(s->b8_stride * (s->mb_height * 2 + 1) + s->mb_stride * (s->mb_height + 1) * 2);
    v->mb_type[0]   = v->mb_type_base + s->b8_stride + 1;
    v->mb_type[1]   = v->mb_type_base + s->b8_stride * (s->mb_height * 2 + 1) + s->mb_stride + 1;
    v->mb_type[2]   = v->mb_type[1] + s->mb_stride * (s->mb_height + 1);

    /* allocate memory to store block level MV info */
    v->blk_mv_type_base = av_mallocz(     s->b8_stride * (s->mb_height * 2 + 1) + s->mb_stride * (s->mb_height + 1) * 2);
    v->blk_mv_type      = v->blk_mv_type_base + s->b8_stride + 1;
    v->mv_f_base        = av_mallocz(2 * (s->b8_stride * (s->mb_height * 2 + 1) + s->mb_stride * (s->mb_height + 1) * 2));
    v->mv_f[0]          = v->mv_f_base + s->b8_stride + 1;
    v->mv_f[1]          = v->mv_f[0] + (s->b8_stride * (s->mb_height * 2 + 1) + s->mb_stride * (s->mb_height + 1) * 2);
    v->mv_f_last_base   = av_mallocz(2 * (s->b8_stride * (s->mb_height * 2 + 1) + s->mb_stride * (s->mb_height + 1) * 2));
    v->mv_f_last[0]     = v->mv_f_last_base + s->b8_stride + 1;
    v->mv_f_last[1]     = v->mv_f_last[0] + (s->b8_stride * (s->mb_height * 2 + 1) + s->mb_stride * (s->mb_height + 1) * 2);
    v->mv_f_next_base   = av_mallocz(2 * (s->b8_stride * (s->mb_height * 2 + 1) + s->mb_stride * (s->mb_height + 1) * 2));
    v->mv_f_next[0]     = v->mv_f_next_base + s->b8_stride + 1;
    v->mv_f_next[1]     = v->mv_f_next[0] + (s->b8_stride * (s->mb_height * 2 + 1) + s->mb_stride * (s->mb_height + 1) * 2);

    /* Init coded blocks info */
    if (v->profile == PROFILE_ADVANCED) {
//        if (alloc_bitplane(&v->over_flags_plane, s->mb_width, s->mb_height) < 0)
//            return -1;
//        if (alloc_bitplane(&v->ac_pred_plane, s->mb_width, s->mb_height) < 0)
//            return -1;
    }

    ff_intrax8_common_init(&v->x8,s);

    if (s->avctx->codec_id == CODEC_ID_WMV3IMAGE || s->avctx->codec_id == CODEC_ID_VC1IMAGE) {
        for (i = 0; i < 4; i++)
            if (!(v->sr_rows[i >> 1][i & 1] = av_malloc(v->output_width))) return -1;
    }

    if (!v->mv_type_mb_plane || !v->direct_mb_plane || !v->acpred_plane || !v->over_flags_plane ||
        !v->block || !v->cbp_base || !v->ttblk_base || !v->is_intra_base || !v->luma_mv_base ||
        !v->mb_type_base)
            return -1;

    return 0;
}

/** Initialize a VC1/WMV3 decoder
 * @todo TODO: Handle VC-1 IDUs (Transport level?)
 * @todo TODO: Decypher remaining bits in extra_data
 */
static av_cold int vc1_decode_init(AVCodecContext *avctx)
{
    VC1Context *v = avctx->priv_data;
    MpegEncContext *s = &v->s;
    GetBitContext gb;
    int i;

    /* save the container output size for WMImage */
    v->output_width  = avctx->width;
    v->output_height = avctx->height;

    if (!avctx->extradata_size || !avctx->extradata)
        return -1;
    if (!(avctx->flags & CODEC_FLAG_GRAY))
        avctx->pix_fmt = avctx->get_format(avctx, avctx->codec->pix_fmts);
    else
        avctx->pix_fmt = PIX_FMT_GRAY8;
    avctx->hwaccel = ff_find_hwaccel(avctx->codec->id, avctx->pix_fmt);
    v->s.avctx = avctx;
    avctx->flags |= CODEC_FLAG_EMU_EDGE;
    v->s.flags   |= CODEC_FLAG_EMU_EDGE;

    if (avctx->idct_algo == FF_IDCT_AUTO) {
        avctx->idct_algo = FF_IDCT_WMV2;
    }

    if (ff_vc1_init_common(v) < 0)
        return -1;
    ff_vc1dsp_init(&v->vc1dsp);

    if (avctx->codec_id == CODEC_ID_WMV3 || avctx->codec_id == CODEC_ID_WMV3IMAGE) {
        int count = 0;

        // looks like WMV3 has a sequence header stored in the extradata
        // advanced sequence header may be before the first frame
        // the last byte of the extradata is a version number, 1 for the
        // samples we can decode

        init_get_bits(&gb, avctx->extradata, avctx->extradata_size*8);

        if (vc1_decode_sequence_header(avctx, v, &gb) < 0)
          return -1;

        count = avctx->extradata_size*8 - get_bits_count(&gb);
        if (count > 0) {
            av_log(avctx, AV_LOG_INFO, "Extra data: %i bits left, value: %X\n",
                   count, get_bits(&gb, count));
        } else if (count < 0) {
            av_log(avctx, AV_LOG_INFO, "Read %i bits in overflow\n", -count);
        }
    } else { // VC1/WVC1/WVP2
        const uint8_t *start = avctx->extradata;
        uint8_t *end = avctx->extradata + avctx->extradata_size;
        const uint8_t *next;
        int size, buf2_size;
        uint8_t *buf2 = NULL;
        int seq_initialized = 0, ep_initialized = 0;

        if (avctx->extradata_size < 16) {
            av_log(avctx, AV_LOG_ERROR, "Extradata size too small: %i\n", avctx->extradata_size);
            return -1;
        }

        buf2  = av_mallocz(avctx->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
        start = find_next_marker(start, end); // in WVC1 extradata first byte is its size, but can be 0 in mkv
        next  = start;
        for (; next < end; start = next) {
            next = find_next_marker(start + 4, end);
            size = next - start - 4;
            if (size <= 0)
                continue;
            buf2_size = vc1_unescape_buffer(start + 4, size, buf2);
            init_get_bits(&gb, buf2, buf2_size * 8);
            switch (AV_RB32(start)) {
            case VC1_CODE_SEQHDR:
                if (vc1_decode_sequence_header(avctx, v, &gb) < 0) {
                    av_free(buf2);
                    return -1;
                }
                seq_initialized = 1;
                break;
            case VC1_CODE_ENTRYPOINT:
                if (vc1_decode_entry_point(avctx, v, &gb) < 0) {
                    av_free(buf2);
                    return -1;
                }
                ep_initialized = 1;
                break;
            }
        }
        av_free(buf2);
        if (!seq_initialized || !ep_initialized) {
            av_log(avctx, AV_LOG_ERROR, "Incomplete extradata\n");
            return -1;
        }
        v->res_sprite = (avctx->codec_tag == MKTAG('W','V','P','2'));
    }

    avctx->profile = v->profile;
    if (v->profile == PROFILE_ADVANCED)
        avctx->level = v->level;

    avctx->has_b_frames = !!avctx->max_b_frames;

    s->mb_width  = (avctx->coded_width  + 15) >> 4;
    s->mb_height = (avctx->coded_height + 15) >> 4;

    if (v->profile == PROFILE_ADVANCED || v->res_fasttx) {
        for (i = 0; i < 64; i++) {
#define transpose(x) ((x >> 3) | ((x & 7) << 3))
            v->zz_8x8[0][i] = transpose(wmv1_scantable[0][i]);
            v->zz_8x8[1][i] = transpose(wmv1_scantable[1][i]);
            v->zz_8x8[2][i] = transpose(wmv1_scantable[2][i]);
            v->zz_8x8[3][i] = transpose(wmv1_scantable[3][i]);
            v->zzi_8x8[i] = transpose(ff_vc1_adv_interlaced_8x8_zz[i]);
        }
        v->left_blk_sh = 0;
        v->top_blk_sh  = 3;
    } else {
        memcpy(v->zz_8x8, wmv1_scantable, 4*64);
        v->left_blk_sh = 3;
        v->top_blk_sh  = 0;
    }

    if (avctx->codec_id == CODEC_ID_WMV3IMAGE || avctx->codec_id == CODEC_ID_VC1IMAGE) {
        v->sprite_width  = avctx->coded_width;
        v->sprite_height = avctx->coded_height;

        avctx->coded_width  = avctx->width  = v->output_width;
        avctx->coded_height = avctx->height = v->output_height;

        // prevent 16.16 overflows
        if (v->sprite_width  > 1 << 14 ||
            v->sprite_height > 1 << 14 ||
            v->output_width  > 1 << 14 ||
            v->output_height > 1 << 14) return -1;
    }
    return 0;
}

/** Close a VC1/WMV3 decoder
 * @warning Initial try at using MpegEncContext stuff
 */
static av_cold int vc1_decode_end(AVCodecContext *avctx)
{
    VC1Context *v = avctx->priv_data;
    int i;

    if ((avctx->codec_id == CODEC_ID_WMV3IMAGE || avctx->codec_id == CODEC_ID_VC1IMAGE)
        && v->sprite_output_frame.data[0])
        avctx->release_buffer(avctx, &v->sprite_output_frame);
    for (i = 0; i < 4; i++)
        av_freep(&v->sr_rows[i >> 1][i & 1]);
    av_freep(&v->hrd_rate);
    av_freep(&v->hrd_buffer);
    MPV_common_end(&v->s);
    av_freep(&v->mv_type_mb_plane);
    av_freep(&v->direct_mb_plane);
    av_freep(&v->forward_mb_plane);
    av_freep(&v->fieldtx_plane);
    av_freep(&v->acpred_plane);
    av_freep(&v->over_flags_plane);
    av_freep(&v->mb_type_base);
    av_freep(&v->blk_mv_type_base);
    av_freep(&v->mv_f_base);
    av_freep(&v->mv_f_last_base);
    av_freep(&v->mv_f_next_base);
    av_freep(&v->block);
    av_freep(&v->cbp_base);
    av_freep(&v->ttblk_base);
    av_freep(&v->is_intra_base); // FIXME use v->mb_type[]
    av_freep(&v->luma_mv_base);
    ff_intrax8_common_end(&v->x8);
    return 0;
}


/** Decode a VC1/WMV3 frame
 * @todo TODO: Handle VC-1 IDUs (Transport level?)
 */
static int vc1_decode_frame(AVCodecContext *avctx, void *data,
                            int *data_size, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size, n_slices = 0, i;
    VC1Context *v = avctx->priv_data;
    MpegEncContext *s = &v->s;
    AVFrame *pict = data;
    uint8_t *buf2 = NULL;
    const uint8_t *buf_start = buf;
    int mb_height, n_slices1=-1;
    struct {
        uint8_t *buf;
        GetBitContext gb;
        int mby_start;
    } *slices = NULL, *tmp;

    if(s->flags & CODEC_FLAG_LOW_DELAY)
        s->low_delay = 1;

    /* no supplementary picture */
    if (buf_size == 0 || (buf_size == 4 && AV_RB32(buf) == VC1_CODE_ENDOFSEQ)) {
        /* special case for last picture */
        if (s->low_delay == 0 && s->next_picture_ptr) {
            *pict = *(AVFrame*)s->next_picture_ptr;
            s->next_picture_ptr = NULL;

            *data_size = sizeof(AVFrame);
        }

        return 0;
    }

    if (s->avctx->codec->capabilities&CODEC_CAP_HWACCEL_VDPAU) {
        if (v->profile < PROFILE_ADVANCED)
            avctx->pix_fmt = PIX_FMT_VDPAU_WMV3;
        else
            avctx->pix_fmt = PIX_FMT_VDPAU_VC1;
    }

    //for advanced profile we may need to parse and unescape data
    if (avctx->codec_id == CODEC_ID_VC1 || avctx->codec_id == CODEC_ID_VC1IMAGE) {
        int buf_size2 = 0;
        buf2 = av_mallocz(buf_size + FF_INPUT_BUFFER_PADDING_SIZE);

        if (IS_MARKER(AV_RB32(buf))) { /* frame starts with marker and needs to be parsed */
            const uint8_t *start, *end, *next;
            int size;

            next = buf;
            for (start = buf, end = buf + buf_size; next < end; start = next) {
                next = find_next_marker(start + 4, end);
                size = next - start - 4;
                if (size <= 0) continue;
                switch (AV_RB32(start)) {
                case VC1_CODE_FRAME:
                    if (avctx->hwaccel ||
                        s->avctx->codec->capabilities&CODEC_CAP_HWACCEL_VDPAU)
                        buf_start = start;
                    buf_size2 = vc1_unescape_buffer(start + 4, size, buf2);
                    break;
                case VC1_CODE_FIELD: {
                    int buf_size3;
                    slices = av_realloc(slices, sizeof(*slices) * (n_slices+1));
                    if (!slices)
                        goto err;
                    slices[n_slices].buf = av_mallocz(buf_size + FF_INPUT_BUFFER_PADDING_SIZE);
                    if (!slices[n_slices].buf)
                        goto err;
                    buf_size3 = vc1_unescape_buffer(start + 4, size,
                                                    slices[n_slices].buf);
                    init_get_bits(&slices[n_slices].gb, slices[n_slices].buf,
                                  buf_size3 << 3);
                    /* assuming that the field marker is at the exact middle,
                       hope it's correct */
                    slices[n_slices].mby_start = s->mb_height >> 1;
                    n_slices1 = n_slices - 1; // index of the last slice of the first field
                    n_slices++;
                    break;
                }
                case VC1_CODE_ENTRYPOINT: /* it should be before frame data */
                    buf_size2 = vc1_unescape_buffer(start + 4, size, buf2);
                    init_get_bits(&s->gb, buf2, buf_size2 * 8);
                    vc1_decode_entry_point(avctx, v, &s->gb);
                    break;
                case VC1_CODE_SLICE: {
                    int buf_size3;
                    slices = av_realloc(slices, sizeof(*slices) * (n_slices+1));
                    if (!slices)
                        goto err;
                    slices[n_slices].buf = av_mallocz(buf_size + FF_INPUT_BUFFER_PADDING_SIZE);
                    if (!slices[n_slices].buf)
                        goto err;
                    buf_size3 = vc1_unescape_buffer(start + 4, size,
                                                    slices[n_slices].buf);
                    init_get_bits(&slices[n_slices].gb, slices[n_slices].buf,
                                  buf_size3 << 3);
                    slices[n_slices].mby_start = get_bits(&slices[n_slices].gb, 9);
                    n_slices++;
                    break;
                }
                }
            }
        } else if (v->interlace && ((buf[0] & 0xC0) == 0xC0)) { /* WVC1 interlaced stores both fields divided by marker */
            const uint8_t *divider;
            int buf_size3;

            divider = find_next_marker(buf, buf + buf_size);
            if ((divider == (buf + buf_size)) || AV_RB32(divider) != VC1_CODE_FIELD) {
                av_log(avctx, AV_LOG_ERROR, "Error in WVC1 interlaced frame\n");
                goto err;
            } else { // found field marker, unescape second field
                tmp = av_realloc(slices, sizeof(*slices) * (n_slices+1));
                if (!tmp)
                    goto err;
                slices = tmp;
                slices[n_slices].buf = av_mallocz(buf_size + FF_INPUT_BUFFER_PADDING_SIZE);
                if (!slices[n_slices].buf)
                    goto err;
                buf_size3 = vc1_unescape_buffer(divider + 4, buf + buf_size - divider - 4, slices[n_slices].buf);
                init_get_bits(&slices[n_slices].gb, slices[n_slices].buf,
                              buf_size3 << 3);
                slices[n_slices].mby_start = s->mb_height >> 1;
                n_slices1 = n_slices - 1;
                n_slices++;
            }
            buf_size2 = vc1_unescape_buffer(buf, divider - buf, buf2);
        } else {
            buf_size2 = vc1_unescape_buffer(buf, buf_size, buf2);
        }
        init_get_bits(&s->gb, buf2, buf_size2*8);
    } else
        init_get_bits(&s->gb, buf, buf_size*8);

    if (v->res_sprite) {
        v->new_sprite  = !get_bits1(&s->gb);
        v->two_sprites =  get_bits1(&s->gb);
        /* res_sprite means a Windows Media Image stream, CODEC_ID_*IMAGE means
           we're using the sprite compositor. These are intentionally kept separate
           so you can get the raw sprites by using the wmv3 decoder for WMVP or
           the vc1 one for WVP2 */
        if (avctx->codec_id == CODEC_ID_WMV3IMAGE || avctx->codec_id == CODEC_ID_VC1IMAGE) {
            if (v->new_sprite) {
                // switch AVCodecContext parameters to those of the sprites
                avctx->width  = avctx->coded_width  = v->sprite_width;
                avctx->height = avctx->coded_height = v->sprite_height;
            } else {
                goto image;
            }
        }
    }

    if (s->context_initialized &&
        (s->width  != avctx->coded_width ||
         s->height != avctx->coded_height)) {
        vc1_decode_end(avctx);
    }

    if (!s->context_initialized) {
        if (ff_msmpeg4_decode_init(avctx) < 0 || vc1_decode_init_alloc_tables(v) < 0)
            return -1;

        s->low_delay = !avctx->has_b_frames || v->res_sprite;

        if (v->profile == PROFILE_ADVANCED) {
            s->h_edge_pos = avctx->coded_width;
            s->v_edge_pos = avctx->coded_height;
        }
    }

    /* We need to set current_picture_ptr before reading the header,
     * otherwise we cannot store anything in there. */
    if (s->current_picture_ptr == NULL || s->current_picture_ptr->f.data[0]) {
        int i = ff_find_unused_picture(s, 0);
        if (i < 0)
            goto err;
        s->current_picture_ptr = &s->picture[i];
    }

    // do parse frame header
    v->pic_header_flag = 0;
    if (v->profile < PROFILE_ADVANCED) {
        if (vc1_parse_frame_header(v, &s->gb) == -1) {
            goto err;
        }
    } else {
        if (vc1_parse_frame_header_adv(v, &s->gb) == -1) {
            goto err;
        }
    }

    if ((avctx->codec_id == CODEC_ID_WMV3IMAGE || avctx->codec_id == CODEC_ID_VC1IMAGE)
        && s->pict_type != AV_PICTURE_TYPE_I) {
        av_log(v->s.avctx, AV_LOG_ERROR, "Sprite decoder: expected I-frame\n");
        goto err;
    }

    // process pulldown flags
    s->current_picture_ptr->f.repeat_pict = 0;
    // Pulldown flags are only valid when 'broadcast' has been set.
    // So ticks_per_frame will be 2
    if (v->rff) {
        // repeat field
        s->current_picture_ptr->f.repeat_pict = 1;
    } else if (v->rptfrm) {
        // repeat frames
        s->current_picture_ptr->f.repeat_pict = v->rptfrm * 2;
    }

    // for skipping the frame
    s->current_picture.f.pict_type = s->pict_type;
    s->current_picture.f.key_frame = s->pict_type == AV_PICTURE_TYPE_I;

    /* skip B-frames if we don't have reference frames */
    if (s->last_picture_ptr == NULL && (s->pict_type == AV_PICTURE_TYPE_B || s->dropable)) {
        goto err;
    }
    if ((avctx->skip_frame >= AVDISCARD_NONREF && s->pict_type == AV_PICTURE_TYPE_B) ||
        (avctx->skip_frame >= AVDISCARD_NONKEY && s->pict_type != AV_PICTURE_TYPE_I) ||
         avctx->skip_frame >= AVDISCARD_ALL) {
        goto end;
    }

    if (s->next_p_frame_damaged) {
        if (s->pict_type == AV_PICTURE_TYPE_B)
            goto end;
        else
            s->next_p_frame_damaged = 0;
    }

    if (MPV_frame_start(s, avctx) < 0) {
        goto err;
    }

    s->me.qpel_put = s->dsp.put_qpel_pixels_tab;
    s->me.qpel_avg = s->dsp.avg_qpel_pixels_tab;

    if ((CONFIG_VC1_VDPAU_DECODER)
        &&s->avctx->codec->capabilities&CODEC_CAP_HWACCEL_VDPAU)
        ff_vdpau_vc1_decode_picture(s, buf_start, (buf + buf_size) - buf_start);
    else if (avctx->hwaccel) {
        if (avctx->hwaccel->start_frame(avctx, buf, buf_size) < 0)
            goto err;
        if (avctx->hwaccel->decode_slice(avctx, buf_start, (buf + buf_size) - buf_start) < 0)
            goto err;
        if (avctx->hwaccel->end_frame(avctx) < 0)
            goto err;
    } else {
        ff_er_frame_start(s);

        v->bits = buf_size * 8;
        if (v->field_mode) {
            uint8_t *tmp[2];
            s->current_picture.f.linesize[0] <<= 1;
            s->current_picture.f.linesize[1] <<= 1;
            s->current_picture.f.linesize[2] <<= 1;
            s->linesize                      <<= 1;
            s->uvlinesize                    <<= 1;
            tmp[0]          = v->mv_f_last[0];
            tmp[1]          = v->mv_f_last[1];
            v->mv_f_last[0] = v->mv_f_next[0];
            v->mv_f_last[1] = v->mv_f_next[1];
            v->mv_f_next[0] = v->mv_f[0];
            v->mv_f_next[1] = v->mv_f[1];
            v->mv_f[0] = tmp[0];
            v->mv_f[1] = tmp[1];
        }
        mb_height = s->mb_height >> v->field_mode;
        for (i = 0; i <= n_slices; i++) {
            if (i > 0 &&  slices[i - 1].mby_start >= mb_height) {
                v->second_field = 1;
                v->blocks_off   = s->mb_width  * s->mb_height << 1;
                v->mb_off       = s->mb_stride * s->mb_height >> 1;
            } else {
                v->second_field = 0;
                v->blocks_off   = 0;
                v->mb_off       = 0;
            }
            if (i) {
                v->pic_header_flag = 0;
                if (v->field_mode && i == n_slices1 + 2)
                    vc1_parse_frame_header_adv(v, &s->gb);
                else if (get_bits1(&s->gb)) {
                    v->pic_header_flag = 1;
                    vc1_parse_frame_header_adv(v, &s->gb);
                }
            }
            s->start_mb_y = (i == 0) ? 0 : FFMAX(0, slices[i-1].mby_start % mb_height);
            if (!v->field_mode || v->second_field)
                s->end_mb_y = (i == n_slices     ) ? mb_height : FFMIN(mb_height, slices[i].mby_start % mb_height);
            else
                s->end_mb_y = (i <= n_slices1 + 1) ? mb_height : FFMIN(mb_height, slices[i].mby_start % mb_height);
            vc1_decode_blocks(v);
            if (i != n_slices)
                s->gb = slices[i].gb;
        }
        if (v->field_mode) {
            v->second_field = 0;
            if (s->pict_type == AV_PICTURE_TYPE_B) {
                memcpy(v->mv_f_base, v->mv_f_next_base,
                       2 * (s->b8_stride * (s->mb_height * 2 + 1) + s->mb_stride * (s->mb_height + 1) * 2));
            }
            s->current_picture.f.linesize[0] >>= 1;
            s->current_picture.f.linesize[1] >>= 1;
            s->current_picture.f.linesize[2] >>= 1;
            s->linesize                      >>= 1;
            s->uvlinesize                    >>= 1;
        }
//av_log(s->avctx, AV_LOG_INFO, "Consumed %i/%i bits\n", get_bits_count(&s->gb), s->gb.size_in_bits);
//  if (get_bits_count(&s->gb) > buf_size * 8)
//      return -1;
        if(s->error_occurred && s->pict_type == AV_PICTURE_TYPE_B)
            goto err;
        ff_er_frame_end(s);
    }

    MPV_frame_end(s);

    if (avctx->codec_id == CODEC_ID_WMV3IMAGE || avctx->codec_id == CODEC_ID_VC1IMAGE) {
image:
        avctx->width  = avctx->coded_width  = v->output_width;
        avctx->height = avctx->coded_height = v->output_height;
        if (avctx->skip_frame >= AVDISCARD_NONREF)
            goto end;
#if CONFIG_WMV3IMAGE_DECODER || CONFIG_VC1IMAGE_DECODER
        if (vc1_decode_sprites(v, &s->gb))
            goto err;
#endif
        *pict      = v->sprite_output_frame;
        *data_size = sizeof(AVFrame);
    } else {
        if (s->pict_type == AV_PICTURE_TYPE_B || s->low_delay) {
            *pict = *(AVFrame*)s->current_picture_ptr;
        } else if (s->last_picture_ptr != NULL) {
            *pict = *(AVFrame*)s->last_picture_ptr;
        }
        if (s->last_picture_ptr || s->low_delay) {
            *data_size = sizeof(AVFrame);
            ff_print_debug_info(s, pict);
        }
    }

end:
    av_free(buf2);
    for (i = 0; i < n_slices; i++)
        av_free(slices[i].buf);
    av_free(slices);
    return buf_size;

err:
    av_free(buf2);
    for (i = 0; i < n_slices; i++)
        av_free(slices[i].buf);
    av_free(slices);
    return -1;
}


static const AVProfile profiles[] = {
    { FF_PROFILE_VC1_SIMPLE,   "Simple"   },
    { FF_PROFILE_VC1_MAIN,     "Main"     },
    { FF_PROFILE_VC1_COMPLEX,  "Complex"  },
    { FF_PROFILE_VC1_ADVANCED, "Advanced" },
    { FF_PROFILE_UNKNOWN },
};

AVCodec ff_vc1_decoder = {
    .name           = "vc1",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_VC1,
    .priv_data_size = sizeof(VC1Context),
    .init           = vc1_decode_init,
    .close          = vc1_decode_end,
    .decode         = vc1_decode_frame,
    .capabilities   = CODEC_CAP_DR1 | CODEC_CAP_DELAY,
    .long_name      = NULL_IF_CONFIG_SMALL("SMPTE VC-1"),
    .pix_fmts       = ff_hwaccel_pixfmt_list_420,
    .profiles       = NULL_IF_CONFIG_SMALL(profiles)
};

#if CONFIG_WMV3_DECODER
AVCodec ff_wmv3_decoder = {
    .name           = "wmv3",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_WMV3,
    .priv_data_size = sizeof(VC1Context),
    .init           = vc1_decode_init,
    .close          = vc1_decode_end,
    .decode         = vc1_decode_frame,
    .capabilities   = CODEC_CAP_DR1 | CODEC_CAP_DELAY,
    .long_name      = NULL_IF_CONFIG_SMALL("Windows Media Video 9"),
    .pix_fmts       = ff_hwaccel_pixfmt_list_420,
    .profiles       = NULL_IF_CONFIG_SMALL(profiles)
};
#endif

#if CONFIG_WMV3_VDPAU_DECODER
AVCodec ff_wmv3_vdpau_decoder = {
    .name           = "wmv3_vdpau",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_WMV3,
    .priv_data_size = sizeof(VC1Context),
    .init           = vc1_decode_init,
    .close          = vc1_decode_end,
    .decode         = vc1_decode_frame,
    .capabilities   = CODEC_CAP_DR1 | CODEC_CAP_DELAY | CODEC_CAP_HWACCEL_VDPAU,
    .long_name      = NULL_IF_CONFIG_SMALL("Windows Media Video 9 VDPAU"),
    .pix_fmts       = (const enum PixelFormat[]){PIX_FMT_VDPAU_WMV3, PIX_FMT_NONE},
    .profiles       = NULL_IF_CONFIG_SMALL(profiles)
};
#endif

#if CONFIG_VC1_VDPAU_DECODER
AVCodec ff_vc1_vdpau_decoder = {
    .name           = "vc1_vdpau",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_VC1,
    .priv_data_size = sizeof(VC1Context),
    .init           = vc1_decode_init,
    .close          = vc1_decode_end,
    .decode         = vc1_decode_frame,
    .capabilities   = CODEC_CAP_DR1 | CODEC_CAP_DELAY | CODEC_CAP_HWACCEL_VDPAU,
    .long_name      = NULL_IF_CONFIG_SMALL("SMPTE VC-1 VDPAU"),
    .pix_fmts       = (const enum PixelFormat[]){PIX_FMT_VDPAU_VC1, PIX_FMT_NONE},
    .profiles       = NULL_IF_CONFIG_SMALL(profiles)
};
#endif

#if CONFIG_WMV3IMAGE_DECODER
AVCodec ff_wmv3image_decoder = {
    .name           = "wmv3image",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_WMV3IMAGE,
    .priv_data_size = sizeof(VC1Context),
    .init           = vc1_decode_init,
    .close          = vc1_decode_end,
    .decode         = vc1_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .flush          = vc1_sprite_flush,
    .long_name      = NULL_IF_CONFIG_SMALL("Windows Media Video 9 Image"),
    .pix_fmts       = ff_pixfmt_list_420
};
#endif

#if CONFIG_VC1IMAGE_DECODER
AVCodec ff_vc1image_decoder = {
    .name           = "vc1image",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_VC1IMAGE,
    .priv_data_size = sizeof(VC1Context),
    .init           = vc1_decode_init,
    .close          = vc1_decode_end,
    .decode         = vc1_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .flush          = vc1_sprite_flush,
    .long_name      = NULL_IF_CONFIG_SMALL("Windows Media Video 9 Image v2"),
    .pix_fmts       = ff_pixfmt_list_420
};
#endif
