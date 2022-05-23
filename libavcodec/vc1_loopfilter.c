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
 * VC-1 and WMV3 loopfilter
 */

#include "avcodec.h"
#include "mpegvideo.h"
#include "vc1.h"
#include "vc1dsp.h"

static av_always_inline void vc1_h_overlap_filter(VC1Context *v, int16_t (*left_block)[64],
                                                  int16_t (*right_block)[64], int left_fieldtx,
                                                  int right_fieldtx, int block_num)
{
    switch (block_num) {
    case 0:
        v->vc1dsp.vc1_h_s_overlap(left_block[2],
                                  right_block[0],
                                  left_fieldtx ^ right_fieldtx ? 16 - 8 * left_fieldtx : 8,
                                  left_fieldtx ^ right_fieldtx ? 16 - 8 * right_fieldtx : 8,
                                  left_fieldtx || right_fieldtx ? 0 : 1);
        break;

    case 1:
        v->vc1dsp.vc1_h_s_overlap(right_block[0],
                                  right_block[2],
                                  8,
                                  8,
                                  right_fieldtx ? 0 : 1);
        break;

    case 2:
        v->vc1dsp.vc1_h_s_overlap(!left_fieldtx && right_fieldtx ? left_block[2] + 8 : left_block[3],
                                  left_fieldtx && !right_fieldtx ? right_block[0] + 8 : right_block[1],
                                  left_fieldtx ^ right_fieldtx ? 16 - 8 * left_fieldtx : 8,
                                  left_fieldtx ^ right_fieldtx ? 16 - 8 * right_fieldtx : 8,
                                  left_fieldtx || right_fieldtx ? 2 : 1);
        break;

    case 3:
        v->vc1dsp.vc1_h_s_overlap(right_block[1],
                                  right_block[3],
                                  8,
                                  8,
                                  right_fieldtx ? 2 : 1);
        break;

    case 4:
    case 5:
        v->vc1dsp.vc1_h_s_overlap(left_block[block_num], right_block[block_num], 8, 8, 1);
        break;
    }
}

static av_always_inline void vc1_v_overlap_filter(VC1Context *v, int16_t (*top_block)[64],
                                                  int16_t (*bottom_block)[64], int block_num)
{
    switch (block_num) {
    case 0:
        v->vc1dsp.vc1_v_s_overlap(top_block[1], bottom_block[0]);
        break;

    case 1:
        v->vc1dsp.vc1_v_s_overlap(top_block[3], bottom_block[2]);
        break;

    case 2:
        v->vc1dsp.vc1_v_s_overlap(bottom_block[0], bottom_block[1]);
        break;

    case 3:
        v->vc1dsp.vc1_v_s_overlap(bottom_block[2], bottom_block[3]);
        break;

    case 4:
    case 5:
        v->vc1dsp.vc1_v_s_overlap(top_block[block_num], bottom_block[block_num]);
        break;
    }
}

void ff_vc1_i_overlap_filter(VC1Context *v)
{
    MpegEncContext *s = &v->s;
    int16_t (*topleft_blk)[64], (*top_blk)[64], (*left_blk)[64], (*cur_blk)[64];
    int block_count = CONFIG_GRAY && (s->avctx->flags & AV_CODEC_FLAG_GRAY) ? 4 : 6;
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;
    int i;

    topleft_blk = v->block[v->topleft_blk_idx];
    top_blk = v->block[v->top_blk_idx];
    left_blk = v->block[v->left_blk_idx];
    cur_blk = v->block[v->cur_blk_idx];

    /* Within a MB, the horizontal overlap always runs before the vertical.
     * To accomplish that, we run the H on the left and internal vertical
     * borders of the currently decoded MB. Then, we wait for the next overlap
     * iteration to do H overlap on the right edge of this MB, before moving
     * over and running the V overlap on the top and internal horizontal
     * borders. Therefore, the H overlap trails by one MB col and the
     * V overlap trails by one MB row. This is reflected in the time at which
     * we run the put_pixels loop, i.e. delayed by one row and one column. */
    for (i = 0; i < block_count; i++) {
        if (s->mb_x == 0 && (i & 5) != 1)
            continue;

        if (v->pq >= 9 || (v->profile == PROFILE_ADVANCED &&
                           (v->condover == CONDOVER_ALL ||
                            (v->over_flags_plane[mb_pos] &&
                             ((i & 5) == 1 || v->over_flags_plane[mb_pos - 1])))))
            vc1_h_overlap_filter(v,
                                 s->mb_x ? left_blk : cur_blk, cur_blk,
                                 v->fcm == ILACE_FRAME && s->mb_x && v->fieldtx_plane[mb_pos - 1],
                                 v->fcm == ILACE_FRAME && v->fieldtx_plane[mb_pos],
                                 i);
    }

    if (v->fcm != ILACE_FRAME)
        for (i = 0; i < block_count; i++) {
            if (s->first_slice_line && !(i & 2))
                continue;

            if (s->mb_x &&
                (v->pq >= 9 || (v->profile == PROFILE_ADVANCED &&
                                (v->condover == CONDOVER_ALL ||
                                 (v->over_flags_plane[mb_pos - 1] &&
                                  ((i & 2) || v->over_flags_plane[mb_pos - 1 - s->mb_stride]))))))
                vc1_v_overlap_filter(v, s->first_slice_line ? left_blk : topleft_blk, left_blk, i);
            if (s->mb_x == s->mb_width - 1 &&
                (v->pq >= 9 || (v->profile == PROFILE_ADVANCED &&
                                (v->condover == CONDOVER_ALL ||
                                 (v->over_flags_plane[mb_pos] &&
                                  ((i & 2) || v->over_flags_plane[mb_pos - s->mb_stride]))))))
                vc1_v_overlap_filter(v, s->first_slice_line ? cur_blk : top_blk, cur_blk, i);
        }
}

void ff_vc1_p_overlap_filter(VC1Context *v)
{
    MpegEncContext *s = &v->s;
    int16_t (*topleft_blk)[64], (*top_blk)[64], (*left_blk)[64], (*cur_blk)[64];
    int block_count = CONFIG_GRAY && (s->avctx->flags & AV_CODEC_FLAG_GRAY) ? 4 : 6;
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;
    int i;

    topleft_blk = v->block[v->topleft_blk_idx];
    top_blk = v->block[v->top_blk_idx];
    left_blk = v->block[v->left_blk_idx];
    cur_blk = v->block[v->cur_blk_idx];

    for (i = 0; i < block_count; i++) {
        if (s->mb_x == 0 && (i & 5) != 1)
            continue;

        if (v->mb_type[0][s->block_index[i]] && v->mb_type[0][s->block_index[i] - 1])
            vc1_h_overlap_filter(v,
                                 s->mb_x ? left_blk : cur_blk, cur_blk,
                                 v->fcm == ILACE_FRAME && s->mb_x && v->fieldtx_plane[mb_pos - 1],
                                 v->fcm == ILACE_FRAME && v->fieldtx_plane[mb_pos],
                                 i);
    }

    if (v->fcm != ILACE_FRAME)
        for (i = 0; i < block_count; i++) {
            if (s->first_slice_line && !(i & 2))
                continue;

            if (s->mb_x && v->mb_type[0][s->block_index[i] - 2 + (i > 3)] &&
                v->mb_type[0][s->block_index[i] - s->block_wrap[i] - 2 + (i > 3)])
                vc1_v_overlap_filter(v, s->first_slice_line ? left_blk : topleft_blk, left_blk, i);
            if (s->mb_x == s->mb_width - 1)
                if (v->mb_type[0][s->block_index[i]] &&
                    v->mb_type[0][s->block_index[i] - s->block_wrap[i]])
                    vc1_v_overlap_filter(v, s->first_slice_line ? cur_blk : top_blk, cur_blk, i);
        }
}

#define LEFT_EDGE   (1 << 0)
#define RIGHT_EDGE  (1 << 1)
#define TOP_EDGE    (1 << 2)
#define BOTTOM_EDGE (1 << 3)

static av_always_inline void vc1_i_h_loop_filter(VC1Context *v, uint8_t *dest,
                                                 uint32_t flags, int block_num)
{
    MpegEncContext *s  = &v->s;
    int pq = v->pq;
    uint8_t *dst;

    if (block_num & 2)
        return;

    if (!(flags & LEFT_EDGE) || (block_num & 5) == 1) {
        if (block_num > 3)
            dst = dest;
        else
            dst = dest + (block_num & 2) * 4 * s->linesize + (block_num & 1) * 8;

        if (v->fcm == ILACE_FRAME)
            if (block_num > 3) {
                v->vc1dsp.vc1_h_loop_filter4(dst, 2 * s->uvlinesize, pq);
                v->vc1dsp.vc1_h_loop_filter4(dst + s->uvlinesize, 2 * s->uvlinesize, pq);
            } else {
                v->vc1dsp.vc1_h_loop_filter8(dst, 2 * s->linesize, pq);
                v->vc1dsp.vc1_h_loop_filter8(dst + s->linesize, 2 * s->linesize, pq);
            }
        else
            if (block_num > 3)
                v->vc1dsp.vc1_h_loop_filter8(dst, s->uvlinesize, pq);
            else
                v->vc1dsp.vc1_h_loop_filter16(dst, s->linesize, pq);
    }
}

static av_always_inline void vc1_i_v_loop_filter(VC1Context *v, uint8_t *dest,
                                                 uint32_t flags, uint8_t fieldtx,
                                                 int block_num)
{
    MpegEncContext *s  = &v->s;
    int pq = v->pq;
    uint8_t *dst;

    if ((block_num & 5) == 1)
        return;

    if (!(flags & TOP_EDGE) || block_num & 2) {
        if (block_num > 3)
            dst = dest;
        else
            dst = dest + (block_num & 2) * 4 * s->linesize + (block_num & 1) * 8;

        if (v->fcm == ILACE_FRAME) {
            if (block_num > 3) {
                v->vc1dsp.vc1_v_loop_filter8(dst, 2 * s->uvlinesize, pq);
                v->vc1dsp.vc1_v_loop_filter8(dst + s->uvlinesize, 2 * s->uvlinesize, pq);
            } else if (block_num < 2 || !fieldtx) {
                v->vc1dsp.vc1_v_loop_filter16(dst, 2 * s->linesize, pq);
                v->vc1dsp.vc1_v_loop_filter16(dst + s->linesize, 2 * s->linesize, pq);
            }
        } else
            if (block_num > 3)
                v->vc1dsp.vc1_v_loop_filter8(dst, s->uvlinesize, pq);
            else
                v->vc1dsp.vc1_v_loop_filter16(dst, s->linesize, pq);
    }
}

void ff_vc1_i_loop_filter(VC1Context *v)
{
    MpegEncContext *s = &v->s;
    int block_count = CONFIG_GRAY && (s->avctx->flags & AV_CODEC_FLAG_GRAY) ? 4 : 6;
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;
    uint8_t *dest, fieldtx;
    uint32_t flags = 0;
    int i;

    /* Within a MB, the vertical loop filter always runs before the horizontal.
     * To accomplish that, we run the V loop filter on top and internal
     * horizontal borders of the last overlap filtered MB. Then, we wait for
     * the loop filter iteration on the next row to do V loop filter on the
     * bottom edge of this MB, before moving over and running the H loop
     * filter on the left and internal vertical borders. Therefore, the loop
     * filter trails by one row and one column relative to the overlap filter
     * and two rows and two columns relative to the decoding loop. */
    if (!s->first_slice_line) {
        dest = s->dest[0] - 16 * s->linesize - 16;
        flags = s->mb_y == s->start_mb_y + 1 ? TOP_EDGE : 0;
        if (s->mb_x) {
            fieldtx = v->fieldtx_plane[mb_pos - s->mb_stride - 1];
            for (i = 0; i < block_count; i++)
                vc1_i_v_loop_filter(v, i > 3 ? s->dest[i - 3] - 8 * s->uvlinesize - 8 : dest, flags, fieldtx, i);
        }
        if (s->mb_x == v->end_mb_x - 1) {
            dest += 16;
            fieldtx = v->fieldtx_plane[mb_pos - s->mb_stride];
            for (i = 0; i < block_count; i++)
                vc1_i_v_loop_filter(v, i > 3 ? s->dest[i - 3] - 8 * s->uvlinesize : dest, flags, fieldtx, i);
        }
    }
    if (s->mb_y == s->end_mb_y - 1) {
        dest = s->dest[0] - 16;
        flags = s->first_slice_line ? TOP_EDGE | BOTTOM_EDGE : BOTTOM_EDGE;
        if (s->mb_x) {
            fieldtx = v->fieldtx_plane[mb_pos - 1];
            for (i = 0; i < block_count; i++)
                vc1_i_v_loop_filter(v, i > 3 ? s->dest[i - 3] - 8 : dest, flags, fieldtx, i);
        }
        if (s->mb_x == v->end_mb_x - 1) {
            dest += 16;
            fieldtx = v->fieldtx_plane[mb_pos];
            for (i = 0; i < block_count; i++)
                vc1_i_v_loop_filter(v, i > 3 ? s->dest[i - 3] : dest, flags, fieldtx, i);
        }
    }

    if (s->mb_y >= s->start_mb_y + 2) {
        dest = s->dest[0] - 32 * s->linesize - 16;
        if (s->mb_x) {
            flags = s->mb_x == 1 ? LEFT_EDGE : 0;
            for (i = 0; i < block_count; i++)
                vc1_i_h_loop_filter(v, i > 3 ? s->dest[i - 3] - 16 * s->uvlinesize - 8 : dest, flags, i);
        }
        if (s->mb_x == v->end_mb_x - 1) {
            dest += 16;
            flags = s->mb_x == 0 ? LEFT_EDGE | RIGHT_EDGE : RIGHT_EDGE;
            for (i = 0; i < block_count; i++)
                vc1_i_h_loop_filter(v, i > 3 ? s->dest[i - 3] - 16 * s->uvlinesize : dest, flags, i);
        }
    }
    if (s->mb_y == s->end_mb_y - 1) {
        if (s->mb_y >= s->start_mb_y + 1) {
            dest = s->dest[0] - 16 * s->linesize - 16;
            if (s->mb_x) {
                flags = s->mb_x == 1 ? LEFT_EDGE : 0;
                for (i = 0; i < block_count; i++)
                    vc1_i_h_loop_filter(v, i > 3 ? s->dest[i - 3] - 8 * s->uvlinesize - 8 : dest, flags, i);
            }
            if (s->mb_x == v->end_mb_x - 1) {
                flags = s->mb_x == 0 ? LEFT_EDGE | RIGHT_EDGE : RIGHT_EDGE;
                dest += 16;
                for (i = 0; i < block_count; i++)
                    vc1_i_h_loop_filter(v, i > 3 ? s->dest[i - 3] - 8 * s->uvlinesize : dest, flags, i);
            }
        }
        dest = s->dest[0] - 16;
        if (s->mb_x) {
            flags = s->mb_x == 1 ? LEFT_EDGE : 0;
            for (i = 0; i < block_count; i++)
                vc1_i_h_loop_filter(v, i > 3 ? s->dest[i - 3] - 8 : dest, flags, i);
        }
        if (s->mb_x == v->end_mb_x - 1) {
            dest += 16;
            flags = s->mb_x == 0 ? LEFT_EDGE | RIGHT_EDGE : RIGHT_EDGE;
            for (i = 0; i < block_count; i++)
                vc1_i_h_loop_filter(v, i > 3 ? s->dest[i - 3] : dest, flags, i);
        }
    }
}

static av_always_inline void vc1_p_h_loop_filter(VC1Context *v, uint8_t *dest, uint32_t *cbp,
                                                 uint8_t *is_intra, int16_t (*mv)[2], uint8_t *mv_f,
                                                 int *ttblk, uint32_t flags, int block_num)
{
    MpegEncContext *s  = &v->s;
    int pq = v->pq;
    uint32_t left_cbp = cbp[0] >> (block_num * 4), right_cbp;
    uint8_t left_is_intra, right_is_intra;
    int tt;
    int idx, linesize  = block_num > 3 ? s->uvlinesize : s->linesize;
    uint8_t *dst;

    if (block_num > 3)
        dst = dest;
    else
        dst = dest + (block_num & 2) * 4 * s->linesize + (block_num & 1) * 8;

    if (!(flags & RIGHT_EDGE) || !(block_num & 5)) {
        left_is_intra = is_intra[0] & (1 << block_num);

        if (block_num > 3) {
            right_is_intra = is_intra[1] & (1 << block_num);
            right_cbp = cbp[1] >> (block_num * 4);
        } else if (block_num & 1) {
            right_is_intra = is_intra[1] & (1 << block_num - 1);
            right_cbp = cbp[1] >> ((block_num - 1) * 4);
        } else {
            right_is_intra = is_intra[0] & (1 << block_num + 1);
            right_cbp = cbp[0] >> ((block_num + 1) * 4);
        }

        if (left_is_intra || right_is_intra ||
            mv[0][0] != mv[1][0] || mv[0][1] != mv[1][1] ||
            (v->fcm == ILACE_FIELD && mv_f[0] != mv_f[1]))
            v->vc1dsp.vc1_h_loop_filter8(dst + 8, linesize, pq);
        else {
            idx = (left_cbp | (right_cbp >> 1)) & 5;
            if (idx & 1)
                v->vc1dsp.vc1_h_loop_filter4(dst + 4 * linesize + 8, linesize, pq);
            if (idx & 4)
                v->vc1dsp.vc1_h_loop_filter4(dst + 8, linesize, pq);
        }
    }

    tt = ttblk[0] >> (block_num * 4) & 0xf;
    if (tt == TT_4X4 || tt == TT_4X8) {
        if (left_cbp & 3)
            v->vc1dsp.vc1_h_loop_filter4(dst + 4 * linesize + 4, linesize, pq);
        if (left_cbp & 12)
            v->vc1dsp.vc1_h_loop_filter4(dst + 4, linesize, pq);
    }
}

static av_always_inline void vc1_p_v_loop_filter(VC1Context *v, uint8_t *dest, uint32_t *cbp,
                                                 uint8_t *is_intra, int16_t (*mv)[2], uint8_t *mv_f,
                                                 int *ttblk, uint32_t flags, int block_num)
{
    MpegEncContext *s  = &v->s;
    int pq = v->pq;
    uint32_t top_cbp = cbp[0] >> (block_num * 4), bottom_cbp;
    uint8_t top_is_intra, bottom_is_intra;
    int tt;
    int idx, linesize  = block_num > 3 ? s->uvlinesize : s->linesize;
    uint8_t *dst;

    if (block_num > 3)
        dst = dest;
    else
        dst = dest + (block_num & 2) * 4 * s->linesize + (block_num & 1) * 8;

    if(!(flags & BOTTOM_EDGE) || block_num < 2) {
        top_is_intra = is_intra[0] & (1 << block_num);

        if (block_num > 3) {
            bottom_is_intra = is_intra[s->mb_stride] & (1 << block_num);
            bottom_cbp = cbp[s->mb_stride] >> (block_num * 4);
        } else if (block_num < 2) {
            bottom_is_intra = is_intra[0] & (1 << block_num + 2);
            bottom_cbp = cbp[0] >> ((block_num + 2) * 4);
        } else {
            bottom_is_intra = is_intra[s->mb_stride] & (1 << block_num - 2);
            bottom_cbp = cbp[s->mb_stride] >> ((block_num - 2) * 4);
        }

        if (top_is_intra || bottom_is_intra ||
            mv[0][0] != mv[block_num > 3 ? s->mb_stride : s->b8_stride][0] ||
            mv[0][1] != mv[block_num > 3 ? s->mb_stride : s->b8_stride][1] ||
            (v->fcm == ILACE_FIELD && mv_f[0] != mv_f[block_num > 3 ? s->mb_stride : s->b8_stride]))
            v->vc1dsp.vc1_v_loop_filter8(dst + 8 * linesize, linesize, pq);
        else {
            idx = (top_cbp | (bottom_cbp >> 2)) & 3;
            if (idx & 1)
                v->vc1dsp.vc1_v_loop_filter4(dst + 8 * linesize + 4, linesize, pq);
            if (idx & 2)
                v->vc1dsp.vc1_v_loop_filter4(dst + 8 * linesize, linesize, pq);
        }
    }

    tt = ttblk[0] >> (block_num * 4) & 0xf;
    if (tt == TT_4X4 || tt == TT_8X4) {
        if (top_cbp & 5)
            v->vc1dsp.vc1_v_loop_filter4(dst + 4 * linesize + 4, linesize, pq);
        if (top_cbp & 10)
            v->vc1dsp.vc1_v_loop_filter4(dst + 4 * linesize, linesize, pq);
    }
}

void ff_vc1_p_loop_filter(VC1Context *v)
{
    MpegEncContext *s = &v->s;
    int block_count = CONFIG_GRAY && (s->avctx->flags & AV_CODEC_FLAG_GRAY) ? 4 : 6;
    uint8_t *dest;
    uint32_t *cbp;
    uint8_t *is_intra;
    int16_t (*uvmv)[2];
    int *ttblk;
    uint32_t flags;
    int i;

    /* Within a MB, the vertical loop filter always runs before the horizontal.
     * To accomplish that, we run the V loop filter on all applicable
     * horizontal borders of the MB above the last overlap filtered MB. Then,
     * we wait for the next loop filter iteration to do H loop filter on all
     * applicable vertical borders of this MB. Therefore, the loop filter
     * trails by one row and one column relative to the overlap filter and two
     * rows and two columns relative to the decoding loop. */
    if (s->mb_y >= s->start_mb_y + 2) {
        if (s->mb_x) {
            dest = s->dest[0] - 32 * s->linesize - 16;
            cbp = &v->cbp[s->mb_x - 2 * s->mb_stride - 1];
            is_intra = &v->is_intra[s->mb_x - 2 * s->mb_stride - 1];
            uvmv = &v->luma_mv[s->mb_x - 2 * s->mb_stride - 1];
            ttblk = &v->ttblk[s->mb_x - 2 * s->mb_stride - 1];
            flags = s->mb_y == s->start_mb_y + 2 ? TOP_EDGE : 0;
            for (i = 0; i < block_count; i++)
                vc1_p_v_loop_filter(v,
                                    i > 3 ? s->dest[i - 3] - 16 * s->uvlinesize - 8 : dest,
                                    cbp,
                                    is_intra,
                                    i > 3 ? uvmv :
                                            &s->current_picture.motion_val[0][s->block_index[i] - 4 * s->b8_stride - 2 + v->blocks_off],
                                    i > 3 ? &v->mv_f[0][s->block_index[i] - 2 * s->mb_stride - 1 + v->mb_off] :
                                            &v->mv_f[0][s->block_index[i] - 4 * s->b8_stride - 2 + v->blocks_off],
                                    ttblk,
                                    flags,
                                    i);
        }
        if (s->mb_x == s->mb_width - 1) {
            dest = s->dest[0] - 32 * s->linesize;
            cbp = &v->cbp[s->mb_x - 2 * s->mb_stride];
            is_intra = &v->is_intra[s->mb_x - 2 * s->mb_stride];
            uvmv = &v->luma_mv[s->mb_x - 2 * s->mb_stride];
            ttblk = &v->ttblk[s->mb_x - 2 * s->mb_stride];
            flags = s->mb_y == s->start_mb_y + 2 ? TOP_EDGE : 0;
            for (i = 0; i < block_count; i++)
                vc1_p_v_loop_filter(v,
                                    i > 3 ? s->dest[i - 3] - 16 * s->uvlinesize : dest,
                                    cbp,
                                    is_intra,
                                    i > 3 ? uvmv :
                                            &s->current_picture.motion_val[0][s->block_index[i] - 4 * s->b8_stride + v->blocks_off],
                                    i > 3 ? &v->mv_f[0][s->block_index[i] - 2 * s->mb_stride + v->mb_off] :
                                            &v->mv_f[0][s->block_index[i] - 4 * s->b8_stride + v->blocks_off],
                                    ttblk,
                                    flags,
                                    i);
        }
    }
    if (s->mb_y == s->end_mb_y - 1) {
        if (s->mb_x) {
            if (s->mb_y >= s->start_mb_y + 1) {
                dest = s->dest[0] - 16 * s->linesize - 16;
                cbp = &v->cbp[s->mb_x - s->mb_stride - 1];
                is_intra = &v->is_intra[s->mb_x - s->mb_stride - 1];
                uvmv = &v->luma_mv[s->mb_x - s->mb_stride - 1];
                ttblk = &v->ttblk[s->mb_x - s->mb_stride - 1];
                flags = s->mb_y == s->start_mb_y + 1 ? TOP_EDGE : 0;
                for (i = 0; i < block_count; i++)
                    vc1_p_v_loop_filter(v,
                                        i > 3 ? s->dest[i - 3] - 8 * s->uvlinesize - 8 : dest,
                                        cbp,
                                        is_intra,
                                        i > 3 ? uvmv :
                                                &s->current_picture.motion_val[0][s->block_index[i] - 2 * s->b8_stride - 2 + v->blocks_off],
                                        i > 3 ? &v->mv_f[0][s->block_index[i] - s->mb_stride - 1 + v->mb_off] :
                                                &v->mv_f[0][s->block_index[i] - 2 * s->b8_stride - 2 + v->blocks_off],
                                        ttblk,
                                        flags,
                                        i);
            }
            dest = s->dest[0] - 16;
            cbp = &v->cbp[s->mb_x - 1];
            is_intra = &v->is_intra[s->mb_x - 1];
            uvmv = &v->luma_mv[s->mb_x - 1];
            ttblk = &v->ttblk[s->mb_x - 1];
            flags = s->mb_y == s->start_mb_y ? TOP_EDGE | BOTTOM_EDGE : BOTTOM_EDGE;
            for (i = 0; i < block_count; i++)
                vc1_p_v_loop_filter(v,
                                    i > 3 ? s->dest[i - 3] - 8 : dest,
                                    cbp,
                                    is_intra,
                                    i > 3 ? uvmv :
                                            &s->current_picture.motion_val[0][s->block_index[i] - 2 + v->blocks_off],
                                    i > 3 ? &v->mv_f[0][s->block_index[i] - 1 + v->mb_off] :
                                            &v->mv_f[0][s->block_index[i] - 2 + v->blocks_off],
                                    ttblk,
                                    flags,
                                    i);
        }
        if (s->mb_x == s->mb_width - 1) {
            if (s->mb_y >= s->start_mb_y + 1) {
                dest = s->dest[0] - 16 * s->linesize;
                cbp = &v->cbp[s->mb_x - s->mb_stride];
                is_intra = &v->is_intra[s->mb_x - s->mb_stride];
                uvmv = &v->luma_mv[s->mb_x - s->mb_stride];
                ttblk = &v->ttblk[s->mb_x - s->mb_stride];
                flags = s->mb_y == s->start_mb_y + 1 ? TOP_EDGE : 0;
                for (i = 0; i < block_count; i++)
                    vc1_p_v_loop_filter(v,
                                        i > 3 ? s->dest[i - 3] - 8 * s->uvlinesize : dest,
                                        cbp,
                                        is_intra,
                                        i > 3 ? uvmv :
                                                &s->current_picture.motion_val[0][s->block_index[i] - 2 * s->b8_stride + v->blocks_off],
                                        i > 3 ? &v->mv_f[0][s->block_index[i] - s->mb_stride + v->mb_off] :
                                                &v->mv_f[0][s->block_index[i] - 2 * s->b8_stride + v->blocks_off],
                                        ttblk,
                                        flags,
                                        i);
            }
            dest = s->dest[0];
            cbp = &v->cbp[s->mb_x];
            is_intra = &v->is_intra[s->mb_x];
            uvmv = &v->luma_mv[s->mb_x];
            ttblk = &v->ttblk[s->mb_x];
            flags = s->mb_y == s->start_mb_y ? TOP_EDGE | BOTTOM_EDGE : BOTTOM_EDGE;
            for (i = 0; i < block_count; i++)
                vc1_p_v_loop_filter(v,
                                    i > 3 ? s->dest[i - 3] : dest,
                                    cbp,
                                    is_intra,
                                    i > 3 ? uvmv :
                                            &s->current_picture.motion_val[0][s->block_index[i] + v->blocks_off],
                                    i > 3 ? &v->mv_f[0][s->block_index[i] + v->mb_off] :
                                            &v->mv_f[0][s->block_index[i] + v->blocks_off],
                                    ttblk,
                                    flags,
                                    i);
        }
    }

    if (s->mb_y >= s->start_mb_y + 2) {
        if (s->mb_x >= 2) {
            dest = s->dest[0] - 32 * s->linesize - 32;
            cbp = &v->cbp[s->mb_x - 2 * s->mb_stride - 2];
            is_intra = &v->is_intra[s->mb_x - 2 * s->mb_stride - 2];
            uvmv = &v->luma_mv[s->mb_x - 2 * s->mb_stride - 2];
            ttblk = &v->ttblk[s->mb_x - 2 * s->mb_stride - 2];
            flags = s->mb_x == 2 ? LEFT_EDGE : 0;
            for (i = 0; i < block_count; i++)
                vc1_p_h_loop_filter(v,
                                    i > 3 ? s->dest[i - 3] - 16 * s->uvlinesize - 16 : dest,
                                    cbp,
                                    is_intra,
                                    i > 3 ? uvmv :
                                            &s->current_picture.motion_val[0][s->block_index[i] - 4 * s->b8_stride - 4 + v->blocks_off],
                                    i > 3 ? &v->mv_f[0][s->block_index[i] - 2 * s->mb_stride - 2 + v->mb_off] :
                                            &v->mv_f[0][s->block_index[i] - 4 * s->b8_stride - 4 + v->blocks_off],
                                    ttblk,
                                    flags,
                                    i);
        }
        if (s->mb_x == s->mb_width - 1) {
            if (s->mb_x >= 1) {
                dest = s->dest[0] - 32 * s->linesize - 16;
                cbp = &v->cbp[s->mb_x - 2 * s->mb_stride - 1];
                is_intra = &v->is_intra[s->mb_x - 2 * s->mb_stride - 1];
                uvmv = &v->luma_mv[s->mb_x - 2 * s->mb_stride - 1];
                ttblk = &v->ttblk[s->mb_x - 2 * s->mb_stride - 1];
                flags = s->mb_x == 1 ? LEFT_EDGE : 0;
                for (i = 0; i < block_count; i++)
                        vc1_p_h_loop_filter(v,
                                            i > 3 ? s->dest[i - 3] - 16 * s->uvlinesize - 8 : dest,
                                            cbp,
                                            is_intra,
                                            i > 3 ? uvmv :
                                                    &s->current_picture.motion_val[0][s->block_index[i] - 4 * s->b8_stride - 2 + v->blocks_off],
                                            i > 3 ? &v->mv_f[0][s->block_index[i] - 2 * s->mb_stride - 1 + v->mb_off] :
                                                    &v->mv_f[0][s->block_index[i] - 4 * s->b8_stride - 2 + v->blocks_off],
                                            ttblk,
                                            flags,
                                            i);
            }
            dest = s->dest[0] - 32 * s->linesize;
            cbp = &v->cbp[s->mb_x - 2 * s->mb_stride];
            is_intra = &v->is_intra[s->mb_x - 2 * s->mb_stride];
            uvmv = &v->luma_mv[s->mb_x - 2 * s->mb_stride];
            ttblk = &v->ttblk[s->mb_x - 2 * s->mb_stride];
            flags = s->mb_x ? RIGHT_EDGE : LEFT_EDGE | RIGHT_EDGE;
            for (i = 0; i < block_count; i++)
                vc1_p_h_loop_filter(v,
                                    i > 3 ? s->dest[i - 3] - 16 * s->uvlinesize : dest,
                                    cbp,
                                    is_intra,
                                    i > 3 ? uvmv :
                                            &s->current_picture.motion_val[0][s->block_index[i] - 4 * s->b8_stride + v->blocks_off],
                                    i > 3 ? &v->mv_f[0][s->block_index[i] - 2 * s->mb_stride + v->mb_off] :
                                            &v->mv_f[0][s->block_index[i] - 4 * s->b8_stride + v->blocks_off],
                                    ttblk,
                                    flags,
                                    i);
        }
    }
    if (s->mb_y == s->end_mb_y - 1) {
        if (s->mb_y >= s->start_mb_y + 1) {
            if (s->mb_x >= 2) {
                dest = s->dest[0] - 16 * s->linesize - 32;
                cbp = &v->cbp[s->mb_x - s->mb_stride - 2];
                is_intra = &v->is_intra[s->mb_x - s->mb_stride - 2];
                uvmv = &v->luma_mv[s->mb_x - s->mb_stride - 2];
                ttblk = &v->ttblk[s->mb_x - s->mb_stride - 2];
                flags = s->mb_x == 2 ? LEFT_EDGE : 0;
                for (i = 0; i < block_count; i++)
                    vc1_p_h_loop_filter(v,
                                        i > 3 ? s->dest[i - 3] - 8 * s->uvlinesize - 16 : dest,
                                        cbp,
                                        is_intra,
                                        i > 3 ? uvmv :
                                                &s->current_picture.motion_val[0][s->block_index[i] - 2 * s->b8_stride - 4 + v->blocks_off],
                                        i > 3 ? &v->mv_f[0][s->block_index[i] - s->mb_stride - 2 + v->mb_off] :
                                                &v->mv_f[0][s->block_index[i] - 2 * s->b8_stride - 4 + v->blocks_off],
                                        ttblk,
                                        flags,
                                        i);
            }
            if (s->mb_x == s->mb_width - 1) {
                if (s->mb_x >= 1) {
                    dest = s->dest[0] - 16 * s->linesize - 16;
                    cbp = &v->cbp[s->mb_x - s->mb_stride - 1];
                    is_intra = &v->is_intra[s->mb_x - s->mb_stride - 1];
                    uvmv = &v->luma_mv[s->mb_x - s->mb_stride - 1];
                    ttblk = &v->ttblk[s->mb_x - s->mb_stride - 1];
                    flags = s->mb_x == 1 ? LEFT_EDGE : 0;
                    for (i = 0; i < block_count; i++)
                            vc1_p_h_loop_filter(v,
                                                i > 3 ? s->dest[i - 3] - 8 * s->uvlinesize - 8 : dest,
                                                cbp,
                                                is_intra,
                                                i > 3 ? uvmv :
                                                        &s->current_picture.motion_val[0][s->block_index[i] - 2 * s->b8_stride - 2 + v->blocks_off],
                                                i > 3 ? &v->mv_f[0][s->block_index[i] - s->mb_stride - 1 + v->mb_off] :
                                                        &v->mv_f[0][s->block_index[i] - 2 * s->b8_stride - 2 + v->blocks_off],
                                                ttblk,
                                                flags,
                                                i);
                }
                dest = s->dest[0] - 16 * s->linesize;
                cbp = &v->cbp[s->mb_x - s->mb_stride];
                is_intra = &v->is_intra[s->mb_x - s->mb_stride];
                uvmv = &v->luma_mv[s->mb_x - s->mb_stride];
                ttblk = &v->ttblk[s->mb_x - s->mb_stride];
                flags = s->mb_x ? RIGHT_EDGE : LEFT_EDGE | RIGHT_EDGE;
                for (i = 0; i < block_count; i++)
                    vc1_p_h_loop_filter(v,
                                        i > 3 ? s->dest[i - 3] - 8 * s->uvlinesize : dest,
                                        cbp,
                                        is_intra,
                                        i > 3 ? uvmv :
                                                &s->current_picture.motion_val[0][s->block_index[i] - 2 * s->b8_stride + v->blocks_off],
                                        i > 3 ? &v->mv_f[0][s->block_index[i] - s->mb_stride + v->mb_off] :
                                                &v->mv_f[0][s->block_index[i] - 2 * s->b8_stride + v->blocks_off],
                                        ttblk,
                                        flags,
                                        i);
            }
        }
        if (s->mb_x >= 2) {
            dest = s->dest[0] - 32;
            cbp = &v->cbp[s->mb_x - 2];
            is_intra = &v->is_intra[s->mb_x - 2];
            uvmv = &v->luma_mv[s->mb_x - 2];
            ttblk = &v->ttblk[s->mb_x - 2];
            flags = s->mb_x == 2 ? LEFT_EDGE : 0;
            for (i = 0; i < block_count; i++)
                vc1_p_h_loop_filter(v,
                                    i > 3 ? s->dest[i - 3] - 16 : dest,
                                    cbp,
                                    is_intra,
                                    i > 3 ? uvmv :
                                            &s->current_picture.motion_val[0][s->block_index[i] - 4 + v->blocks_off],
                                    i > 3 ? &v->mv_f[0][s->block_index[i] - 2 + v->mb_off] :
                                            &v->mv_f[0][s->block_index[i] - 4 + v->blocks_off],
                                    ttblk,
                                    flags,
                                    i);
        }
        if (s->mb_x == s->mb_width - 1) {
            if (s->mb_x >= 1) {
                dest = s->dest[0] - 16;
                cbp = &v->cbp[s->mb_x - 1];
                is_intra = &v->is_intra[s->mb_x - 1];
                uvmv = &v->luma_mv[s->mb_x - 1];
                ttblk = &v->ttblk[s->mb_x - 1];
                flags = s->mb_x == 1 ? LEFT_EDGE : 0;
                for (i = 0; i < block_count; i++)
                    vc1_p_h_loop_filter(v,
                                        i > 3 ? s->dest[i - 3] - 8 : dest,
                                        cbp,
                                        is_intra,
                                        i > 3 ? uvmv :
                                                &s->current_picture.motion_val[0][s->block_index[i] - 2 + v->blocks_off],
                                        i > 3 ? &v->mv_f[0][s->block_index[i] - 1 + v->mb_off] :
                                                &v->mv_f[0][s->block_index[i] - 2 + v->blocks_off],
                                        ttblk,
                                        flags,
                                        i);
            }
            dest = s->dest[0];
            cbp = &v->cbp[s->mb_x];
            is_intra = &v->is_intra[s->mb_x];
            uvmv = &v->luma_mv[s->mb_x];
            ttblk = &v->ttblk[s->mb_x];
            flags = s->mb_x ? RIGHT_EDGE : LEFT_EDGE | RIGHT_EDGE;
            for (i = 0; i < block_count; i++)
                vc1_p_h_loop_filter(v,
                                    i > 3 ? s->dest[i - 3] : dest,
                                    cbp,
                                    is_intra,
                                    i > 3 ? uvmv :
                                            &s->current_picture.motion_val[0][s->block_index[i] + v->blocks_off],
                                    i > 3 ? &v->mv_f[0][s->block_index[i] + v->mb_off] :
                                            &v->mv_f[0][s->block_index[i] + v->blocks_off],
                                    ttblk,
                                    flags,
                                    i);
        }
    }
}

static av_always_inline void vc1_p_h_intfr_loop_filter(VC1Context *v, uint8_t *dest, int *ttblk,
                                                       uint32_t flags, uint8_t fieldtx, int block_num)
{
    MpegEncContext *s  = &v->s;
    int pq = v->pq;
    int tt;
    int linesize  = block_num > 3 ? s->uvlinesize : s->linesize;
    uint8_t *dst;

    if (block_num > 3)
        dst = dest;
    else
        dst = dest + (block_num & 2) * 4 * s->linesize + (block_num & 1) * 8;

    tt = ttblk[0] >> (block_num * 4) & 0xf;
    if (block_num < 4) {
        if (fieldtx) {
            if (block_num < 2) {
                if (tt == TT_4X4 || tt == TT_4X8)
                    v->vc1dsp.vc1_h_loop_filter8(dst + 4, 2 * linesize, pq);
                if (!(flags & RIGHT_EDGE) || block_num == 0)
                    v->vc1dsp.vc1_h_loop_filter8(dst + 8, 2 * linesize, pq);
            } else {
                if (tt == TT_4X4 || tt == TT_4X8)
                    v->vc1dsp.vc1_h_loop_filter8(dst - 7 * linesize + 4, 2 * linesize, pq);
                if (!(flags & RIGHT_EDGE) || block_num == 2)
                    v->vc1dsp.vc1_h_loop_filter8(dst - 7 * linesize + 8, 2 * linesize, pq);
            }
        } else {
            if(tt == TT_4X4 || tt == TT_4X8) {
                v->vc1dsp.vc1_h_loop_filter4(dst + 4, 2 * linesize, pq);
                v->vc1dsp.vc1_h_loop_filter4(dst + linesize + 4, 2 * linesize, pq);
            }
            if (!(flags & RIGHT_EDGE) || !(block_num & 5)) {
                v->vc1dsp.vc1_h_loop_filter4(dst + 8, 2 * linesize, pq);
                v->vc1dsp.vc1_h_loop_filter4(dst + linesize + 8, 2 * linesize, pq);
            }
        }
    } else {
        if (tt == TT_4X4 || tt == TT_4X8) {
            v->vc1dsp.vc1_h_loop_filter4(dst + 4, 2 * linesize, pq);
            v->vc1dsp.vc1_h_loop_filter4(dst + linesize + 4, 2 * linesize, pq);
        }
        if (!(flags & RIGHT_EDGE)) {
            v->vc1dsp.vc1_h_loop_filter4(dst + 8, 2 * linesize, pq);
            v->vc1dsp.vc1_h_loop_filter4(dst + linesize + 8, 2 * linesize, pq);
        }
    }
}

static av_always_inline void vc1_p_v_intfr_loop_filter(VC1Context *v, uint8_t *dest, int *ttblk,
                                                       uint32_t flags, uint8_t fieldtx, int block_num)
{
    MpegEncContext *s  = &v->s;
    int pq = v->pq;
    int tt;
    int linesize  = block_num > 3 ? s->uvlinesize : s->linesize;
    uint8_t *dst;

    if (block_num > 3)
        dst = dest;
    else
        dst = dest + (block_num & 2) * 4 * s->linesize + (block_num & 1) * 8;

    tt = ttblk[0] >> (block_num * 4) & 0xf;
    if (block_num < 4) {
        if (fieldtx) {
            if (block_num < 2) {
                if (tt == TT_4X4 || tt == TT_8X4)
                    v->vc1dsp.vc1_v_loop_filter8(dst + 8 * linesize, 2 * linesize, pq);
                if (!(flags & BOTTOM_EDGE))
                    v->vc1dsp.vc1_v_loop_filter8(dst + 16 * linesize, 2 * linesize, pq);
            } else {
                if (tt == TT_4X4 || tt == TT_8X4)
                    v->vc1dsp.vc1_v_loop_filter8(dst + linesize, 2 * linesize, pq);
                if (!(flags & BOTTOM_EDGE))
                    v->vc1dsp.vc1_v_loop_filter8(dst + 9 * linesize, 2 * linesize, pq);
            }
        } else {
            if (block_num < 2) {
                if (!(flags & TOP_EDGE) && (tt == TT_4X4 || tt == TT_8X4)) {
                    v->vc1dsp.vc1_v_loop_filter8(dst + 4 * linesize, 2 * linesize, pq);
                    v->vc1dsp.vc1_v_loop_filter8(dst + 5 * linesize, 2 * linesize, pq);
                }
                v->vc1dsp.vc1_v_loop_filter8(dst + 8 * linesize, 2 * linesize, pq);
                v->vc1dsp.vc1_v_loop_filter8(dst + 9 * linesize, 2 * linesize, pq);
            } else if (!(flags & BOTTOM_EDGE)) {
                if (tt == TT_4X4 || tt == TT_8X4) {
                    v->vc1dsp.vc1_v_loop_filter8(dst + 4 * linesize, 2 * linesize, pq);
                    v->vc1dsp.vc1_v_loop_filter8(dst + 5 * linesize, 2 * linesize, pq);
                }
                v->vc1dsp.vc1_v_loop_filter8(dst + 8 * linesize, 2 * linesize, pq);
                v->vc1dsp.vc1_v_loop_filter8(dst + 9 * linesize, 2 * linesize, pq);
            }
        }
    } else {
        if (!(flags & BOTTOM_EDGE)) {
            if (!(flags & TOP_EDGE) && (tt == TT_4X4 || tt == TT_8X4)) {
                v->vc1dsp.vc1_v_loop_filter8(dst + 4 * linesize, 2 * linesize, pq);
                v->vc1dsp.vc1_v_loop_filter8(dst + 5 * linesize, 2 * linesize, pq);
            }
                v->vc1dsp.vc1_v_loop_filter8(dst + 8 * linesize, 2 * linesize, pq);
                v->vc1dsp.vc1_v_loop_filter8(dst + 9 * linesize, 2 * linesize, pq);
        }
    }
}

void ff_vc1_p_intfr_loop_filter(VC1Context *v)
{
    MpegEncContext *s = &v->s;
    int block_count = CONFIG_GRAY && (s->avctx->flags & AV_CODEC_FLAG_GRAY) ? 4 : 6;
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;
    uint8_t *dest;
    int *ttblk;
    uint32_t flags;
    uint8_t fieldtx;
    int i;

    /* Within a MB, the vertical loop filter always runs before the horizontal.
     * To accomplish that, we run the V loop filter on all applicable
     * horizontal borders of the MB above the last overlap filtered MB. Then,
     * we wait for the loop filter iteration on the next row and next column to
     * do H loop filter on all applicable vertical borders of this MB.
     * Therefore, the loop filter trails by two rows and one column relative to
     * the overlap filter and two rows and two columns relative to the decoding
     * loop. */
    if (s->mb_x) {
        if (s->mb_y >= s->start_mb_y + 1) {
            dest = s->dest[0] - 16 * s->linesize - 16;
            ttblk = &v->ttblk[s->mb_x - s->mb_stride - 1];
            flags = s->mb_y == s->start_mb_y + 1 ? TOP_EDGE : 0;
            fieldtx = v->fieldtx_plane[mb_pos - s->mb_stride - 1];
            for (i = 0; i < block_count; i++)
                vc1_p_v_intfr_loop_filter(v,
                                          i > 3 ? s->dest[i - 3] - 8 * s->uvlinesize - 8 : dest,
                                          ttblk,
                                          flags,
                                          fieldtx,
                                          i);
        }
    }
    if (s->mb_x == s->mb_width - 1) {
        if (s->mb_y >= s->start_mb_y + 1) {
            dest = s->dest[0] - 16 * s->linesize;
            ttblk = &v->ttblk[s->mb_x - s->mb_stride];
            flags = s->mb_y == s->start_mb_y + 1 ? TOP_EDGE : 0;
            fieldtx = v->fieldtx_plane[mb_pos - s->mb_stride];
            for (i = 0; i < block_count; i++)
                vc1_p_v_intfr_loop_filter(v,
                                          i > 3 ? s->dest[i - 3] - 8 * s->uvlinesize : dest,
                                          ttblk,
                                          flags,
                                          fieldtx,
                                          i);
        }
    }
    if (s->mb_y == s->end_mb_y - 1) {
        if (s->mb_x) {
            dest = s->dest[0] - 16;
            ttblk = &v->ttblk[s->mb_x - 1];
            flags = s->mb_y == s->start_mb_y ? TOP_EDGE | BOTTOM_EDGE : BOTTOM_EDGE;
            fieldtx = v->fieldtx_plane[mb_pos - 1];
            for (i = 0; i < block_count; i++)
                vc1_p_v_intfr_loop_filter(v,
                                          i > 3 ? s->dest[i - 3] - 8 : dest,
                                          ttblk,
                                          flags,
                                          fieldtx,
                                          i);
        }
        if (s->mb_x == s->mb_width - 1) {
            dest = s->dest[0];
            ttblk = &v->ttblk[s->mb_x];
            flags = s->mb_y == s->start_mb_y ? TOP_EDGE | BOTTOM_EDGE : BOTTOM_EDGE;
            fieldtx = v->fieldtx_plane[mb_pos];
            for (i = 0; i < block_count; i++)
                vc1_p_v_intfr_loop_filter(v,
                                          i > 3 ? s->dest[i - 3] : dest,
                                          ttblk,
                                          flags,
                                          fieldtx,
                                          i);
        }
    }

    if (s->mb_y >= s->start_mb_y + 2) {
        if (s->mb_x >= 2) {
            dest = s->dest[0] - 32 * s->linesize - 32;
            ttblk = &v->ttblk[s->mb_x - 2 * s->mb_stride - 2];
            flags = s->mb_x == 2 ? LEFT_EDGE : 0;
            fieldtx = v->fieldtx_plane[mb_pos - 2 * s->mb_stride - 2];
            for (i = 0; i < block_count; i++)
                vc1_p_h_intfr_loop_filter(v,
                                          i > 3 ? s->dest[i - 3] - 16 * s->uvlinesize - 16 : dest,
                                          ttblk,
                                          flags,
                                          fieldtx,
                                          i);
        }
        if (s->mb_x == s->mb_width - 1) {
            if (s->mb_x >= 1) {
                dest = s->dest[0] - 32 * s->linesize - 16;
                ttblk = &v->ttblk[s->mb_x - 2 * s->mb_stride - 1];
                flags = s->mb_x == 1 ? LEFT_EDGE : 0;
                fieldtx = v->fieldtx_plane[mb_pos - 2 * s->mb_stride - 1];
                for (i = 0; i < block_count; i++)
                    vc1_p_h_intfr_loop_filter(v,
                                              i > 3 ? s->dest[i - 3] - 16 * s->uvlinesize - 8 : dest,
                                              ttblk,
                                              flags,
                                              fieldtx,
                                              i);
            }
            dest = s->dest[0] - 32 * s->linesize;
            ttblk = &v->ttblk[s->mb_x - 2 * s->mb_stride];
            flags = s->mb_x ? RIGHT_EDGE : LEFT_EDGE | RIGHT_EDGE;
            fieldtx = v->fieldtx_plane[mb_pos - 2 * s->mb_stride];
            for (i = 0; i < block_count; i++)
                vc1_p_h_intfr_loop_filter(v,
                                          i > 3 ? s->dest[i - 3] - 16 * s->uvlinesize : dest,
                                          ttblk,
                                          flags,
                                          fieldtx,
                                          i);
        }
    }
    if (s->mb_y == s->end_mb_y - 1) {
        if (s->mb_y >= s->start_mb_y + 1) {
            if (s->mb_x >= 2) {
                dest = s->dest[0] - 16 * s->linesize - 32;
                ttblk = &v->ttblk[s->mb_x - s->mb_stride - 2];
                flags = s->mb_x == 2 ? LEFT_EDGE : 0;
                fieldtx = v->fieldtx_plane[mb_pos - s->mb_stride - 2];
                for (i = 0; i < block_count; i++)
                    vc1_p_h_intfr_loop_filter(v,
                                              i > 3 ? s->dest[i - 3] - 8 * s->uvlinesize - 16 : dest,
                                              ttblk,
                                              flags,
                                              fieldtx,
                                              i);
            }
            if (s->mb_x == s->mb_width - 1) {
                if (s->mb_x >= 1) {
                    dest = s->dest[0] - 16 * s->linesize - 16;
                    ttblk = &v->ttblk[s->mb_x - s->mb_stride - 1];
                    flags = s->mb_x == 1 ? LEFT_EDGE : 0;
                    fieldtx = v->fieldtx_plane[mb_pos - s->mb_stride - 1];
                    for (i = 0; i < block_count; i++)
                        vc1_p_h_intfr_loop_filter(v,
                                                  i > 3 ? s->dest[i - 3] - 8 * s->uvlinesize - 8 : dest,
                                                  ttblk,
                                                  flags,
                                                  fieldtx,
                                                  i);
                }
                dest = s->dest[0] - 16 * s->linesize;
                ttblk = &v->ttblk[s->mb_x - s->mb_stride];
                flags = s->mb_x ? RIGHT_EDGE : LEFT_EDGE | RIGHT_EDGE;
                fieldtx = v->fieldtx_plane[mb_pos - s->mb_stride];
                for (i = 0; i < block_count; i++)
                    vc1_p_h_intfr_loop_filter(v,
                                              i > 3 ? s->dest[i - 3] - 8 * s->uvlinesize : dest,
                                              ttblk,
                                              flags,
                                              fieldtx,
                                              i);
            }
        }
        if (s->mb_x >= 2) {
            dest = s->dest[0] - 32;
            ttblk = &v->ttblk[s->mb_x - 2];
            flags = s->mb_x == 2 ? LEFT_EDGE : 0;
            fieldtx = v->fieldtx_plane[mb_pos - 2];
            for (i = 0; i < block_count; i++)
                vc1_p_h_intfr_loop_filter(v,
                                          i > 3 ? s->dest[i - 3] - 16 : dest,
                                          ttblk,
                                          flags,
                                          fieldtx,
                                          i);
        }
        if (s->mb_x == s->mb_width - 1) {
            if (s->mb_x >= 1) {
                dest = s->dest[0] - 16;
                ttblk = &v->ttblk[s->mb_x - 1];
                flags = s->mb_x == 1 ? LEFT_EDGE : 0;
                fieldtx = v->fieldtx_plane[mb_pos - 1];
                for (i = 0; i < block_count; i++)
                    vc1_p_h_intfr_loop_filter(v,
                                              i > 3 ? s->dest[i - 3] - 8 : dest,
                                              ttblk,
                                              flags,
                                              fieldtx,
                                              i);
            }
            dest = s->dest[0];
            ttblk = &v->ttblk[s->mb_x];
            flags = s->mb_x ? RIGHT_EDGE : LEFT_EDGE | RIGHT_EDGE;
            fieldtx = v->fieldtx_plane[mb_pos];
            for (i = 0; i < block_count; i++)
                vc1_p_h_intfr_loop_filter(v,
                                          i > 3 ? s->dest[i - 3] : dest,
                                          ttblk,
                                          flags,
                                          fieldtx,
                                          i);
        }
    }
}

static av_always_inline void vc1_b_h_intfi_loop_filter(VC1Context *v, uint8_t *dest, uint32_t *cbp,
                                                       int *ttblk, uint32_t flags, int block_num)
{
    MpegEncContext *s  = &v->s;
    int pq = v->pq;
    uint8_t *dst;
    uint32_t block_cbp = cbp[0] >> (block_num * 4);
    int tt;
    int idx, linesize  = block_num > 3 ? s->uvlinesize : s->linesize;

    if (block_num > 3)
        dst = dest;
    else
        dst = dest + (block_num & 2) * 4 * s->linesize + (block_num & 1) * 8;

    if (!(flags & RIGHT_EDGE) || !(block_num & 5)) {
        v->vc1dsp.vc1_h_loop_filter8(dst + 8, linesize, pq);
    }

    tt = ttblk[0] >> (block_num * 4) & 0xf;
    if (tt == TT_4X4 || tt == TT_4X8) {
        idx = (block_cbp | (block_cbp >> 1)) & 5;
        if (idx & 1)
            v->vc1dsp.vc1_h_loop_filter4(dst + 4 * linesize + 4, linesize, pq);
        if (idx & 4)
            v->vc1dsp.vc1_h_loop_filter4(dst + 4, linesize, pq);
    }
}

static av_always_inline void vc1_b_v_intfi_loop_filter(VC1Context *v, uint8_t *dest, uint32_t *cbp,
                                                       int *ttblk, uint32_t flags, int block_num)
{
    MpegEncContext *s  = &v->s;
    int pq = v->pq;
    uint8_t *dst;
    uint32_t block_cbp = cbp[0] >> (block_num * 4);
    int tt;
    int idx, linesize  = block_num > 3 ? s->uvlinesize : s->linesize;

    if (block_num > 3)
        dst = dest;
    else
        dst = dest + (block_num & 2) * 4 * s->linesize + (block_num & 1) * 8;

    if(!(flags & BOTTOM_EDGE) || block_num < 2)
        v->vc1dsp.vc1_v_loop_filter8(dst + 8 * linesize, linesize, pq);

    tt = ttblk[0] >> (block_num * 4) & 0xf;
    if (tt == TT_4X4 || tt == TT_8X4) {
        idx = (block_cbp | (block_cbp >> 2)) & 3;
        if (idx & 1)
            v->vc1dsp.vc1_v_loop_filter4(dst + 4 * linesize + 4, linesize, pq);
        if (idx & 2)
            v->vc1dsp.vc1_v_loop_filter4(dst + 4 * linesize, linesize, pq);
    }
}

void ff_vc1_b_intfi_loop_filter(VC1Context *v)
{
    MpegEncContext *s = &v->s;
    int block_count = CONFIG_GRAY && (s->avctx->flags & AV_CODEC_FLAG_GRAY) ? 4 : 6;
    uint8_t *dest;
    uint32_t *cbp;
    int *ttblk;
    uint32_t flags = 0;
    int i;

    /* Within a MB, the vertical loop filter always runs before the horizontal.
     * To accomplish that, we run the V loop filter on all applicable
     * horizontal borders of the MB above the currently decoded MB. Then,
     * we wait for the next loop filter iteration to do H loop filter on all
     * applicable vertical borders of this MB. Therefore, the loop filter
     * trails by one row and one column relative to the decoding loop. */
    if (!s->first_slice_line) {
        dest = s->dest[0] - 16 * s->linesize;
        cbp = &v->cbp[s->mb_x - s->mb_stride];
        ttblk = &v->ttblk[s->mb_x - s->mb_stride];
        flags = s->mb_y == s->start_mb_y + 1 ? TOP_EDGE : 0;
        for (i = 0; i < block_count; i++)
            vc1_b_v_intfi_loop_filter(v, i > 3 ? s->dest[i - 3] - 8 * s->uvlinesize : dest, cbp, ttblk, flags, i);
    }
    if (s->mb_y == s->end_mb_y - 1) {
        dest = s->dest[0];
        cbp = &v->cbp[s->mb_x];
        ttblk = &v->ttblk[s->mb_x];
        flags = s->first_slice_line ? TOP_EDGE | BOTTOM_EDGE : BOTTOM_EDGE;
        for (i = 0; i < block_count; i++)
            vc1_b_v_intfi_loop_filter(v, i > 3 ? s->dest[i - 3] : dest, cbp, ttblk, flags, i);
    }

    if (!s->first_slice_line) {
        dest = s->dest[0] - 16 * s->linesize - 16;
        cbp = &v->cbp[s->mb_x - s->mb_stride - 1];
        ttblk = &v->ttblk[s->mb_x - s->mb_stride - 1];
        if (s->mb_x) {
            flags = s->mb_x == 1 ? LEFT_EDGE : 0;
            for (i = 0; i < block_count; i++)
                vc1_b_h_intfi_loop_filter(v, i > 3 ? s->dest[i - 3] - 8 * s->uvlinesize - 8 : dest, cbp, ttblk, flags, i);
        }
        if (s->mb_x == s->mb_width - 1) {
            dest += 16;
            cbp++;
            ttblk++;
            flags = s->mb_x == 0 ? LEFT_EDGE | RIGHT_EDGE : RIGHT_EDGE;
            for (i = 0; i < block_count; i++)
                vc1_b_h_intfi_loop_filter(v, i > 3 ? s->dest[i - 3] - 8 * s->uvlinesize : dest, cbp, ttblk, flags, i);
        }
    }
    if (s->mb_y == s->end_mb_y - 1) {
        dest = s->dest[0] - 16;
        cbp = &v->cbp[s->mb_x - 1];
        ttblk = &v->ttblk[s->mb_x - 1];
        if (s->mb_x) {
            flags = s->mb_x == 1 ? LEFT_EDGE : 0;
            for (i = 0; i < block_count; i++)
                vc1_b_h_intfi_loop_filter(v, i > 3 ? s->dest[i - 3] - 8 : dest, cbp, ttblk, flags, i);
        }
        if (s->mb_x == s->mb_width - 1) {
            dest += 16;
            cbp++;
            ttblk++;
            flags = s->mb_x == 0 ? LEFT_EDGE | RIGHT_EDGE : RIGHT_EDGE;
            for (i = 0; i < block_count; i++)
                vc1_b_h_intfi_loop_filter(v, i > 3 ? s->dest[i - 3] : dest, cbp, ttblk, flags, i);
        }
    }
}
