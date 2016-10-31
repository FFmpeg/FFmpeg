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

void ff_vc1_loop_filter_iblk(VC1Context *v, int pq)
{
    MpegEncContext *s = &v->s;
    int j;
    if (!s->first_slice_line) {
        v->vc1dsp.vc1_v_loop_filter16(s->dest[0], s->linesize, pq);
        if (s->mb_x)
            v->vc1dsp.vc1_h_loop_filter16(s->dest[0] - 16 * s->linesize, s->linesize, pq);
        v->vc1dsp.vc1_h_loop_filter16(s->dest[0] - 16 * s->linesize + 8, s->linesize, pq);
        if (!CONFIG_GRAY || !(s->avctx->flags & AV_CODEC_FLAG_GRAY))
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
            if (!CONFIG_GRAY || !(s->avctx->flags & AV_CODEC_FLAG_GRAY)) {
            v->vc1dsp.vc1_h_loop_filter8(s->dest[1], s->uvlinesize, pq);
            v->vc1dsp.vc1_h_loop_filter8(s->dest[2], s->uvlinesize, pq);
            }
        }
        v->vc1dsp.vc1_h_loop_filter16(s->dest[0] + 8, s->linesize, pq);
    }
}

void ff_vc1_loop_filter_iblk_delayed(VC1Context *v, int pq)
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
                if (!CONFIG_GRAY || !(s->avctx->flags & AV_CODEC_FLAG_GRAY))
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
                if (!CONFIG_GRAY || !(s->avctx->flags & AV_CODEC_FLAG_GRAY))
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
                if (s->mb_x >= 2 && (!CONFIG_GRAY || !(s->avctx->flags & AV_CODEC_FLAG_GRAY))) {
                    for (j = 0; j < 2; j++) {
                        v->vc1dsp.vc1_h_loop_filter8(s->dest[j + 1] - 8 * s->uvlinesize - 8, s->uvlinesize, pq);
                    }
                }
            }

            if (s->mb_x == s->mb_width - 1) {
                if (s->mb_x)
                    v->vc1dsp.vc1_h_loop_filter16(s->dest[0] - 16 * s->linesize, s->linesize, pq);
                v->vc1dsp.vc1_h_loop_filter16(s->dest[0] - 16 * s->linesize + 8, s->linesize, pq);
                if (s->mb_x && (!CONFIG_GRAY || !(s->avctx->flags & AV_CODEC_FLAG_GRAY))) {
                    for (j = 0; j < 2; j++) {
                        v->vc1dsp.vc1_h_loop_filter8(s->dest[j + 1] - 8 * s->uvlinesize, s->uvlinesize, pq);
                    }
                }
            }
        }
    }
}

void ff_vc1_smooth_overlap_filter_iblk(VC1Context *v)
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
            if (!CONFIG_GRAY || !(s->avctx->flags & AV_CODEC_FLAG_GRAY)) {
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
                if (!CONFIG_GRAY || !(s->avctx->flags & AV_CODEC_FLAG_GRAY)) {
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
            if (!CONFIG_GRAY || !(s->avctx->flags & AV_CODEC_FLAG_GRAY)) {
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

static av_always_inline void vc1_apply_p_v_loop_filter(VC1Context *v, int block_num)
{
    MpegEncContext *s  = &v->s;
    int mb_cbp         = v->cbp[s->mb_x - s->mb_stride],
        block_cbp      = mb_cbp      >> (block_num * 4), bottom_cbp,
        mb_is_intra    = v->is_intra[s->mb_x - s->mb_stride],
        block_is_intra = mb_is_intra >> block_num, bottom_is_intra;
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
            bottom_is_intra = v->is_intra[s->mb_x] >> block_num;
            mv              = &v->luma_mv[s->mb_x - s->mb_stride];
            mv_stride       = s->mb_stride;
        } else {
            bottom_cbp      = (block_num < 2) ? (mb_cbp               >> ((block_num + 2) * 4))
                                              : (v->cbp[s->mb_x]      >> ((block_num - 2) * 4));
            bottom_is_intra = (block_num < 2) ? (mb_is_intra          >> (block_num + 2))
                                              : (v->is_intra[s->mb_x] >> (block_num - 2));
            mv_stride       = s->b8_stride;
            mv              = &s->current_picture.motion_val[0][s->block_index[block_num] - 2 * mv_stride];
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
        block_is_intra = mb_is_intra >> block_num, right_is_intra;
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
            right_is_intra = v->is_intra[s->mb_x - s->mb_stride] >> block_num;
            mv             = &v->luma_mv[s->mb_x - s->mb_stride - 1];
        } else {
            right_cbp      = (block_num & 1) ? (v->cbp[s->mb_x - s->mb_stride]      >> ((block_num - 1) * 4))
                                             : (mb_cbp                              >> ((block_num + 1) * 4));
            right_is_intra = (block_num & 1) ? (v->is_intra[s->mb_x - s->mb_stride] >> (block_num - 1))
                                             : (mb_is_intra                         >> (block_num + 1));
            mv             = &s->current_picture.motion_val[0][s->block_index[block_num] - s->b8_stride * 2 - 2];
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

void ff_vc1_apply_p_loop_filter(VC1Context *v)
{
    MpegEncContext *s = &v->s;
    int i;
    int block_count = CONFIG_GRAY && (s->avctx->flags & AV_CODEC_FLAG_GRAY) ? 4 : 6;

    for (i = 0; i < block_count; i++) {
        vc1_apply_p_v_loop_filter(v, i);
    }

    /* V always precedes H, therefore we run H one MB before V;
     * at the end of a row, we catch up to complete the row */
    if (s->mb_x) {
        for (i = 0; i < block_count; i++) {
            vc1_apply_p_h_loop_filter(v, i);
        }
        if (s->mb_x == s->mb_width - 1) {
            s->mb_x++;
            ff_update_block_index(s);
            for (i = 0; i < block_count; i++) {
                vc1_apply_p_h_loop_filter(v, i);
            }
        }
    }
}
