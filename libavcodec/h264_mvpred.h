/*
 * H.26L/H.264/AVC/JVT/14496-10/... motion vector predicion
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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
 * H.264 / AVC / MPEG4 part10 motion vector predicion.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#ifndef AVCODEC_H264_MVPRED_H
#define AVCODEC_H264_MVPRED_H

#include "internal.h"
#include "avcodec.h"
#include "h264.h"
#include "libavutil/avassert.h"


static av_always_inline int fetch_diagonal_mv(H264Context *h, const int16_t **C,
                                              int i, int list, int part_width)
{
    const int topright_ref = h->ref_cache[list][i - 8 + part_width];

    /* there is no consistent mapping of mvs to neighboring locations that will
     * make mbaff happy, so we can't move all this logic to fill_caches */
    if (FRAME_MBAFF) {
#define SET_DIAG_MV(MV_OP, REF_OP, XY, Y4)                              \
        const int xy = XY, y4 = Y4;                                     \
        const int mb_type = mb_types[xy + (y4 >> 2) * h->mb_stride];    \
        if (!USES_LIST(mb_type, list))                                  \
            return LIST_NOT_USED;                                       \
        mv = h->cur_pic_ptr->f.motion_val[list][h->mb2b_xy[xy] + 3 + y4 * h->b_stride]; \
        h->mv_cache[list][scan8[0] - 2][0] = mv[0];                     \
        h->mv_cache[list][scan8[0] - 2][1] = mv[1] MV_OP;               \
        return h->cur_pic_ptr->f.ref_index[list][4 * xy + 1 + (y4 & ~1)] REF_OP;

        if (topright_ref == PART_NOT_AVAILABLE
            && i >= scan8[0] + 8 && (i & 7) == 4
            && h->ref_cache[list][scan8[0] - 1] != PART_NOT_AVAILABLE) {
            const uint32_t *mb_types = h->cur_pic_ptr->f.mb_type;
            const int16_t *mv;
            AV_ZERO32(h->mv_cache[list][scan8[0] - 2]);
            *C = h->mv_cache[list][scan8[0] - 2];

            if (!MB_FIELD && IS_INTERLACED(h->left_type[0])) {
                SET_DIAG_MV(* 2, >> 1, h->left_mb_xy[0] + h->mb_stride,
                            (h->mb_y & 1) * 2 + (i >> 5));
            }
            if (MB_FIELD && !IS_INTERLACED(h->left_type[0])) {
                // left shift will turn LIST_NOT_USED into PART_NOT_AVAILABLE, but that's OK.
                SET_DIAG_MV(/ 2, << 1, h->left_mb_xy[i >= 36], ((i >> 2)) & 3);
            }
        }
#undef SET_DIAG_MV
    }

    if (topright_ref != PART_NOT_AVAILABLE) {
        *C = h->mv_cache[list][i - 8 + part_width];
        return topright_ref;
    } else {
        tprintf(h->avctx, "topright MV not available\n");

        *C = h->mv_cache[list][i - 8 - 1];
        return h->ref_cache[list][i - 8 - 1];
    }
}

/**
 * Get the predicted MV.
 * @param n the block index
 * @param part_width the width of the partition (4, 8,16) -> (1, 2, 4)
 * @param mx the x component of the predicted motion vector
 * @param my the y component of the predicted motion vector
 */
static av_always_inline void pred_motion(H264Context *const h, int n,
                                         int part_width, int list, int ref,
                                         int *const mx, int *const my)
{
    const int index8       = scan8[n];
    const int top_ref      = h->ref_cache[list][index8 - 8];
    const int left_ref     = h->ref_cache[list][index8 - 1];
    const int16_t *const A = h->mv_cache[list][index8 - 1];
    const int16_t *const B = h->mv_cache[list][index8 - 8];
    const int16_t *C;
    int diagonal_ref, match_count;

    av_assert2(part_width == 1 || part_width == 2 || part_width == 4);

/* mv_cache
 * B . . A T T T T
 * U . . L . . , .
 * U . . L . . . .
 * U . . L . . , .
 * . . . L . . . .
 */

    diagonal_ref = fetch_diagonal_mv(h, &C, index8, list, part_width);
    match_count  = (diagonal_ref == ref) + (top_ref == ref) + (left_ref == ref);
    tprintf(h->avctx, "pred_motion match_count=%d\n", match_count);
    if (match_count > 1) { //most common
        *mx = mid_pred(A[0], B[0], C[0]);
        *my = mid_pred(A[1], B[1], C[1]);
    } else if (match_count == 1) {
        if (left_ref == ref) {
            *mx = A[0];
            *my = A[1];
        } else if (top_ref == ref) {
            *mx = B[0];
            *my = B[1];
        } else {
            *mx = C[0];
            *my = C[1];
        }
    } else {
        if (top_ref      == PART_NOT_AVAILABLE &&
            diagonal_ref == PART_NOT_AVAILABLE &&
            left_ref     != PART_NOT_AVAILABLE) {
            *mx = A[0];
            *my = A[1];
        } else {
            *mx = mid_pred(A[0], B[0], C[0]);
            *my = mid_pred(A[1], B[1], C[1]);
        }
    }

    tprintf(h->avctx,
            "pred_motion (%2d %2d %2d) (%2d %2d %2d) (%2d %2d %2d) -> (%2d %2d %2d) at %2d %2d %d list %d\n",
            top_ref, B[0], B[1], diagonal_ref, C[0], C[1], left_ref,
            A[0], A[1], ref, *mx, *my, h->mb_x, h->mb_y, n, list);
}

/**
 * Get the directionally predicted 16x8 MV.
 * @param n the block index
 * @param mx the x component of the predicted motion vector
 * @param my the y component of the predicted motion vector
 */
static av_always_inline void pred_16x8_motion(H264Context *const h,
                                              int n, int list, int ref,
                                              int *const mx, int *const my)
{
    if (n == 0) {
        const int top_ref      = h->ref_cache[list][scan8[0] - 8];
        const int16_t *const B = h->mv_cache[list][scan8[0] - 8];

        tprintf(h->avctx, "pred_16x8: (%2d %2d %2d) at %2d %2d %d list %d\n",
                top_ref, B[0], B[1], h->mb_x, h->mb_y, n, list);

        if (top_ref == ref) {
            *mx = B[0];
            *my = B[1];
            return;
        }
    } else {
        const int left_ref     = h->ref_cache[list][scan8[8] - 1];
        const int16_t *const A = h->mv_cache[list][scan8[8] - 1];

        tprintf(h->avctx, "pred_16x8: (%2d %2d %2d) at %2d %2d %d list %d\n",
                left_ref, A[0], A[1], h->mb_x, h->mb_y, n, list);

        if (left_ref == ref) {
            *mx = A[0];
            *my = A[1];
            return;
        }
    }

    //RARE
    pred_motion(h, n, 4, list, ref, mx, my);
}

/**
 * Get the directionally predicted 8x16 MV.
 * @param n the block index
 * @param mx the x component of the predicted motion vector
 * @param my the y component of the predicted motion vector
 */
static av_always_inline void pred_8x16_motion(H264Context *const h,
                                              int n, int list, int ref,
                                              int *const mx, int *const my)
{
    if (n == 0) {
        const int left_ref     = h->ref_cache[list][scan8[0] - 1];
        const int16_t *const A = h->mv_cache[list][scan8[0] - 1];

        tprintf(h->avctx, "pred_8x16: (%2d %2d %2d) at %2d %2d %d list %d\n",
                left_ref, A[0], A[1], h->mb_x, h->mb_y, n, list);

        if (left_ref == ref) {
            *mx = A[0];
            *my = A[1];
            return;
        }
    } else {
        const int16_t *C;
        int diagonal_ref;

        diagonal_ref = fetch_diagonal_mv(h, &C, scan8[4], list, 2);

        tprintf(h->avctx, "pred_8x16: (%2d %2d %2d) at %2d %2d %d list %d\n",
                diagonal_ref, C[0], C[1], h->mb_x, h->mb_y, n, list);

        if (diagonal_ref == ref) {
            *mx = C[0];
            *my = C[1];
            return;
        }
    }

    //RARE
    pred_motion(h, n, 2, list, ref, mx, my);
}

#define FIX_MV_MBAFF(type, refn, mvn, idx)      \
    if (FRAME_MBAFF) {                          \
        if (MB_FIELD) {                         \
            if (!IS_INTERLACED(type)) {         \
                refn <<= 1;                     \
                AV_COPY32(mvbuf[idx], mvn);     \
                mvbuf[idx][1] /= 2;             \
                mvn = mvbuf[idx];               \
            }                                   \
        } else {                                \
            if (IS_INTERLACED(type)) {          \
                refn >>= 1;                     \
                AV_COPY32(mvbuf[idx], mvn);     \
                mvbuf[idx][1] <<= 1;            \
                mvn = mvbuf[idx];               \
            }                                   \
        }                                       \
    }

static av_always_inline void pred_pskip_motion(H264Context *const h)
{
    DECLARE_ALIGNED(4, static const int16_t, zeromv)[2] = { 0 };
    DECLARE_ALIGNED(4, int16_t, mvbuf)[3][2];
    int8_t *ref     = h->cur_pic.f.ref_index[0];
    int16_t(*mv)[2] = h->cur_pic.f.motion_val[0];
    int top_ref, left_ref, diagonal_ref, match_count, mx, my;
    const int16_t *A, *B, *C;
    int b_stride = h->b_stride;

    fill_rectangle(&h->ref_cache[0][scan8[0]], 4, 4, 8, 0, 1);

    /* To avoid doing an entire fill_decode_caches, we inline the relevant
     * parts here.
     * FIXME: this is a partial duplicate of the logic in fill_decode_caches,
     * but it's faster this way.  Is there a way to avoid this duplication?
     */
    if (USES_LIST(h->left_type[LTOP], 0)) {
        left_ref = ref[4 * h->left_mb_xy[LTOP] + 1 + (h->left_block[0] & ~1)];
        A = mv[h->mb2b_xy[h->left_mb_xy[LTOP]] + 3 + b_stride * h->left_block[0]];
        FIX_MV_MBAFF(h->left_type[LTOP], left_ref, A, 0);
        if (!(left_ref | AV_RN32A(A)))
            goto zeromv;
    } else if (h->left_type[LTOP]) {
        left_ref = LIST_NOT_USED;
        A        = zeromv;
    } else {
        goto zeromv;
    }

    if (USES_LIST(h->top_type, 0)) {
        top_ref = ref[4 * h->top_mb_xy + 2];
        B       = mv[h->mb2b_xy[h->top_mb_xy] + 3 * b_stride];
        FIX_MV_MBAFF(h->top_type, top_ref, B, 1);
        if (!(top_ref | AV_RN32A(B)))
            goto zeromv;
    } else if (h->top_type) {
        top_ref = LIST_NOT_USED;
        B       = zeromv;
    } else {
        goto zeromv;
    }

    tprintf(h->avctx, "pred_pskip: (%d) (%d) at %2d %2d\n",
            top_ref, left_ref, h->mb_x, h->mb_y);

    if (USES_LIST(h->topright_type, 0)) {
        diagonal_ref = ref[4 * h->topright_mb_xy + 2];
        C = mv[h->mb2b_xy[h->topright_mb_xy] + 3 * b_stride];
        FIX_MV_MBAFF(h->topright_type, diagonal_ref, C, 2);
    } else if (h->topright_type) {
        diagonal_ref = LIST_NOT_USED;
        C = zeromv;
    } else {
        if (USES_LIST(h->topleft_type, 0)) {
            diagonal_ref = ref[4 * h->topleft_mb_xy + 1 +
                               (h->topleft_partition & 2)];
            C = mv[h->mb2b_xy[h->topleft_mb_xy] + 3 + b_stride +
                   (h->topleft_partition & 2 * b_stride)];
            FIX_MV_MBAFF(h->topleft_type, diagonal_ref, C, 2);
        } else if (h->topleft_type) {
            diagonal_ref = LIST_NOT_USED;
            C            = zeromv;
        } else {
            diagonal_ref = PART_NOT_AVAILABLE;
            C            = zeromv;
        }
    }

    match_count = !diagonal_ref + !top_ref + !left_ref;
    tprintf(h->avctx, "pred_pskip_motion match_count=%d\n", match_count);
    if (match_count > 1) {
        mx = mid_pred(A[0], B[0], C[0]);
        my = mid_pred(A[1], B[1], C[1]);
    } else if (match_count == 1) {
        if (!left_ref) {
            mx = A[0];
            my = A[1];
        } else if (!top_ref) {
            mx = B[0];
            my = B[1];
        } else {
            mx = C[0];
            my = C[1];
        }
    } else {
        mx = mid_pred(A[0], B[0], C[0]);
        my = mid_pred(A[1], B[1], C[1]);
    }

    fill_rectangle(h->mv_cache[0][scan8[0]], 4, 4, 8, pack16to32(mx, my), 4);
    return;

zeromv:
    fill_rectangle(h->mv_cache[0][scan8[0]], 4, 4, 8, 0, 4);
    return;
}

static void fill_decode_neighbors(H264Context *h, int mb_type)
{
    const int mb_xy = h->mb_xy;
    int topleft_xy, top_xy, topright_xy, left_xy[LEFT_MBS];
    static const uint8_t left_block_options[4][32] = {
        { 0, 1, 2, 3, 7, 10, 8, 11, 3 + 0 * 4, 3 + 1 * 4, 3 + 2 * 4, 3 + 3 * 4, 1 + 4 * 4, 1 + 8 * 4, 1 + 5 * 4, 1 + 9 * 4 },
        { 2, 2, 3, 3, 8, 11, 8, 11, 3 + 2 * 4, 3 + 2 * 4, 3 + 3 * 4, 3 + 3 * 4, 1 + 5 * 4, 1 + 9 * 4, 1 + 5 * 4, 1 + 9 * 4 },
        { 0, 0, 1, 1, 7, 10, 7, 10, 3 + 0 * 4, 3 + 0 * 4, 3 + 1 * 4, 3 + 1 * 4, 1 + 4 * 4, 1 + 8 * 4, 1 + 4 * 4, 1 + 8 * 4 },
        { 0, 2, 0, 2, 7, 10, 7, 10, 3 + 0 * 4, 3 + 2 * 4, 3 + 0 * 4, 3 + 2 * 4, 1 + 4 * 4, 1 + 8 * 4, 1 + 4 * 4, 1 + 8 * 4 }
    };

    h->topleft_partition = -1;

    top_xy = mb_xy - (h->mb_stride << MB_FIELD);

    /* Wow, what a mess, why didn't they simplify the interlacing & intra
     * stuff, I can't imagine that these complex rules are worth it. */

    topleft_xy    = top_xy - 1;
    topright_xy   = top_xy + 1;
    left_xy[LBOT] = left_xy[LTOP] = mb_xy - 1;
    h->left_block = left_block_options[0];
    if (FRAME_MBAFF) {
        const int left_mb_field_flag = IS_INTERLACED(h->cur_pic.f.mb_type[mb_xy - 1]);
        const int curr_mb_field_flag = IS_INTERLACED(mb_type);
        if (h->mb_y & 1) {
            if (left_mb_field_flag != curr_mb_field_flag) {
                left_xy[LBOT] = left_xy[LTOP] = mb_xy - h->mb_stride - 1;
                if (curr_mb_field_flag) {
                    left_xy[LBOT] += h->mb_stride;
                    h->left_block  = left_block_options[3];
                } else {
                    topleft_xy += h->mb_stride;
                    /* take top left mv from the middle of the mb, as opposed
                     * to all other modes which use the bottom right partition */
                    h->topleft_partition = 0;
                    h->left_block        = left_block_options[1];
                }
            }
        } else {
            if (curr_mb_field_flag) {
                topleft_xy  += h->mb_stride & (((h->cur_pic.f.mb_type[top_xy - 1] >> 7) & 1) - 1);
                topright_xy += h->mb_stride & (((h->cur_pic.f.mb_type[top_xy + 1] >> 7) & 1) - 1);
                top_xy      += h->mb_stride & (((h->cur_pic.f.mb_type[top_xy]     >> 7) & 1) - 1);
            }
            if (left_mb_field_flag != curr_mb_field_flag) {
                if (curr_mb_field_flag) {
                    left_xy[LBOT] += h->mb_stride;
                    h->left_block  = left_block_options[3];
                } else {
                    h->left_block = left_block_options[2];
                }
            }
        }
    }

    h->topleft_mb_xy    = topleft_xy;
    h->top_mb_xy        = top_xy;
    h->topright_mb_xy   = topright_xy;
    h->left_mb_xy[LTOP] = left_xy[LTOP];
    h->left_mb_xy[LBOT] = left_xy[LBOT];
    //FIXME do we need all in the context?

    h->topleft_type    = h->cur_pic.f.mb_type[topleft_xy];
    h->top_type        = h->cur_pic.f.mb_type[top_xy];
    h->topright_type   = h->cur_pic.f.mb_type[topright_xy];
    h->left_type[LTOP] = h->cur_pic.f.mb_type[left_xy[LTOP]];
    h->left_type[LBOT] = h->cur_pic.f.mb_type[left_xy[LBOT]];

    if (FMO) {
        if (h->slice_table[topleft_xy] != h->slice_num)
            h->topleft_type = 0;
        if (h->slice_table[top_xy] != h->slice_num)
            h->top_type = 0;
        if (h->slice_table[left_xy[LTOP]] != h->slice_num)
            h->left_type[LTOP] = h->left_type[LBOT] = 0;
    } else {
        if (h->slice_table[topleft_xy] != h->slice_num) {
            h->topleft_type = 0;
            if (h->slice_table[top_xy] != h->slice_num)
                h->top_type = 0;
            if (h->slice_table[left_xy[LTOP]] != h->slice_num)
                h->left_type[LTOP] = h->left_type[LBOT] = 0;
        }
    }
    if (h->slice_table[topright_xy] != h->slice_num)
        h->topright_type = 0;
}

static void fill_decode_caches(H264Context *h, int mb_type)
{
    int topleft_xy, top_xy, topright_xy, left_xy[LEFT_MBS];
    int topleft_type, top_type, topright_type, left_type[LEFT_MBS];
    const uint8_t *left_block = h->left_block;
    int i;
    uint8_t *nnz;
    uint8_t *nnz_cache;

    topleft_xy      = h->topleft_mb_xy;
    top_xy          = h->top_mb_xy;
    topright_xy     = h->topright_mb_xy;
    left_xy[LTOP]   = h->left_mb_xy[LTOP];
    left_xy[LBOT]   = h->left_mb_xy[LBOT];
    topleft_type    = h->topleft_type;
    top_type        = h->top_type;
    topright_type   = h->topright_type;
    left_type[LTOP] = h->left_type[LTOP];
    left_type[LBOT] = h->left_type[LBOT];

    if (!IS_SKIP(mb_type)) {
        if (IS_INTRA(mb_type)) {
            int type_mask = h->pps.constrained_intra_pred ? IS_INTRA(-1) : -1;
            h->topleft_samples_available      =
                h->top_samples_available      =
                    h->left_samples_available = 0xFFFF;
            h->topright_samples_available     = 0xEEEA;

            if (!(top_type & type_mask)) {
                h->topleft_samples_available  = 0xB3FF;
                h->top_samples_available      = 0x33FF;
                h->topright_samples_available = 0x26EA;
            }
            if (IS_INTERLACED(mb_type) != IS_INTERLACED(left_type[LTOP])) {
                if (IS_INTERLACED(mb_type)) {
                    if (!(left_type[LTOP] & type_mask)) {
                        h->topleft_samples_available &= 0xDFFF;
                        h->left_samples_available    &= 0x5FFF;
                    }
                    if (!(left_type[LBOT] & type_mask)) {
                        h->topleft_samples_available &= 0xFF5F;
                        h->left_samples_available    &= 0xFF5F;
                    }
                } else {
                    int left_typei = h->cur_pic.f.mb_type[left_xy[LTOP] + h->mb_stride];

                    av_assert2(left_xy[LTOP] == left_xy[LBOT]);
                    if (!((left_typei & type_mask) && (left_type[LTOP] & type_mask))) {
                        h->topleft_samples_available &= 0xDF5F;
                        h->left_samples_available    &= 0x5F5F;
                    }
                }
            } else {
                if (!(left_type[LTOP] & type_mask)) {
                    h->topleft_samples_available &= 0xDF5F;
                    h->left_samples_available    &= 0x5F5F;
                }
            }

            if (!(topleft_type & type_mask))
                h->topleft_samples_available &= 0x7FFF;

            if (!(topright_type & type_mask))
                h->topright_samples_available &= 0xFBFF;

            if (IS_INTRA4x4(mb_type)) {
                if (IS_INTRA4x4(top_type)) {
                    AV_COPY32(h->intra4x4_pred_mode_cache + 4 + 8 * 0, h->intra4x4_pred_mode + h->mb2br_xy[top_xy]);
                } else {
                    h->intra4x4_pred_mode_cache[4 + 8 * 0] =
                    h->intra4x4_pred_mode_cache[5 + 8 * 0] =
                    h->intra4x4_pred_mode_cache[6 + 8 * 0] =
                    h->intra4x4_pred_mode_cache[7 + 8 * 0] = 2 - 3 * !(top_type & type_mask);
                }
                for (i = 0; i < 2; i++) {
                    if (IS_INTRA4x4(left_type[LEFT(i)])) {
                        int8_t *mode = h->intra4x4_pred_mode + h->mb2br_xy[left_xy[LEFT(i)]];
                        h->intra4x4_pred_mode_cache[3 + 8 * 1 + 2 * 8 * i] = mode[6 - left_block[0 + 2 * i]];
                        h->intra4x4_pred_mode_cache[3 + 8 * 2 + 2 * 8 * i] = mode[6 - left_block[1 + 2 * i]];
                    } else {
                        h->intra4x4_pred_mode_cache[3 + 8 * 1 + 2 * 8 * i] =
                        h->intra4x4_pred_mode_cache[3 + 8 * 2 + 2 * 8 * i] = 2 - 3 * !(left_type[LEFT(i)] & type_mask);
                    }
                }
            }
        }

        /*
         * 0 . T T. T T T T
         * 1 L . .L . . . .
         * 2 L . .L . . . .
         * 3 . T TL . . . .
         * 4 L . .L . . . .
         * 5 L . .. . . . .
         */
        /* FIXME: constraint_intra_pred & partitioning & nnz
         * (let us hope this is just a typo in the spec) */
        nnz_cache = h->non_zero_count_cache;
        if (top_type) {
            nnz = h->non_zero_count[top_xy];
            AV_COPY32(&nnz_cache[4 + 8 * 0], &nnz[4 * 3]);
            if (!h->chroma_y_shift) {
                AV_COPY32(&nnz_cache[4 + 8 *  5], &nnz[4 *  7]);
                AV_COPY32(&nnz_cache[4 + 8 * 10], &nnz[4 * 11]);
            } else {
                AV_COPY32(&nnz_cache[4 + 8 *  5], &nnz[4 * 5]);
                AV_COPY32(&nnz_cache[4 + 8 * 10], &nnz[4 * 9]);
            }
        } else {
            uint32_t top_empty = CABAC && !IS_INTRA(mb_type) ? 0 : 0x40404040;
            AV_WN32A(&nnz_cache[4 + 8 *  0], top_empty);
            AV_WN32A(&nnz_cache[4 + 8 *  5], top_empty);
            AV_WN32A(&nnz_cache[4 + 8 * 10], top_empty);
        }

        for (i = 0; i < 2; i++) {
            if (left_type[LEFT(i)]) {
                nnz = h->non_zero_count[left_xy[LEFT(i)]];
                nnz_cache[3 + 8 * 1 + 2 * 8 * i] = nnz[left_block[8 + 0 + 2 * i]];
                nnz_cache[3 + 8 * 2 + 2 * 8 * i] = nnz[left_block[8 + 1 + 2 * i]];
                if (CHROMA444) {
                    nnz_cache[3 + 8 *  6 + 2 * 8 * i] = nnz[left_block[8 + 0 + 2 * i] + 4 * 4];
                    nnz_cache[3 + 8 *  7 + 2 * 8 * i] = nnz[left_block[8 + 1 + 2 * i] + 4 * 4];
                    nnz_cache[3 + 8 * 11 + 2 * 8 * i] = nnz[left_block[8 + 0 + 2 * i] + 8 * 4];
                    nnz_cache[3 + 8 * 12 + 2 * 8 * i] = nnz[left_block[8 + 1 + 2 * i] + 8 * 4];
                } else if (CHROMA422) {
                    nnz_cache[3 + 8 *  6 + 2 * 8 * i] = nnz[left_block[8 + 0 + 2 * i] - 2 + 4 * 4];
                    nnz_cache[3 + 8 *  7 + 2 * 8 * i] = nnz[left_block[8 + 1 + 2 * i] - 2 + 4 * 4];
                    nnz_cache[3 + 8 * 11 + 2 * 8 * i] = nnz[left_block[8 + 0 + 2 * i] - 2 + 8 * 4];
                    nnz_cache[3 + 8 * 12 + 2 * 8 * i] = nnz[left_block[8 + 1 + 2 * i] - 2 + 8 * 4];
                } else {
                    nnz_cache[3 + 8 *  6 + 8 * i] = nnz[left_block[8 + 4 + 2 * i]];
                    nnz_cache[3 + 8 * 11 + 8 * i] = nnz[left_block[8 + 5 + 2 * i]];
                }
            } else {
                nnz_cache[3 + 8 *  1 + 2 * 8 * i] =
                nnz_cache[3 + 8 *  2 + 2 * 8 * i] =
                nnz_cache[3 + 8 *  6 + 2 * 8 * i] =
                nnz_cache[3 + 8 *  7 + 2 * 8 * i] =
                nnz_cache[3 + 8 * 11 + 2 * 8 * i] =
                nnz_cache[3 + 8 * 12 + 2 * 8 * i] = CABAC && !IS_INTRA(mb_type) ? 0 : 64;
            }
        }

        if (CABAC) {
            // top_cbp
            if (top_type)
                h->top_cbp = h->cbp_table[top_xy];
            else
                h->top_cbp = IS_INTRA(mb_type) ? 0x7CF : 0x00F;
            // left_cbp
            if (left_type[LTOP]) {
                h->left_cbp =   (h->cbp_table[left_xy[LTOP]] & 0x7F0) |
                               ((h->cbp_table[left_xy[LTOP]] >> (left_block[0] & (~1))) & 2) |
                              (((h->cbp_table[left_xy[LBOT]] >> (left_block[2] & (~1))) & 2) << 2);
            } else {
                h->left_cbp = IS_INTRA(mb_type) ? 0x7CF : 0x00F;
            }
        }
    }

    if (IS_INTER(mb_type) || (IS_DIRECT(mb_type) && h->direct_spatial_mv_pred)) {
        int list;
        int b_stride = h->b_stride;
        for (list = 0; list < h->list_count; list++) {
            int8_t *ref_cache = &h->ref_cache[list][scan8[0]];
            int8_t *ref       = h->cur_pic.f.ref_index[list];
            int16_t(*mv_cache)[2] = &h->mv_cache[list][scan8[0]];
            int16_t(*mv)[2]       = h->cur_pic.f.motion_val[list];
            if (!USES_LIST(mb_type, list))
                continue;
            av_assert2(!(IS_DIRECT(mb_type) && !h->direct_spatial_mv_pred));

            if (USES_LIST(top_type, list)) {
                const int b_xy = h->mb2b_xy[top_xy] + 3 * b_stride;
                AV_COPY128(mv_cache[0 - 1 * 8], mv[b_xy + 0]);
                ref_cache[0 - 1 * 8] =
                ref_cache[1 - 1 * 8] = ref[4 * top_xy + 2];
                ref_cache[2 - 1 * 8] =
                ref_cache[3 - 1 * 8] = ref[4 * top_xy + 3];
            } else {
                AV_ZERO128(mv_cache[0 - 1 * 8]);
                AV_WN32A(&ref_cache[0 - 1 * 8],
                         ((top_type ? LIST_NOT_USED : PART_NOT_AVAILABLE) & 0xFF) * 0x01010101u);
            }

            if (mb_type & (MB_TYPE_16x8 | MB_TYPE_8x8)) {
                for (i = 0; i < 2; i++) {
                    int cache_idx = -1 + i * 2 * 8;
                    if (USES_LIST(left_type[LEFT(i)], list)) {
                        const int b_xy  = h->mb2b_xy[left_xy[LEFT(i)]] + 3;
                        const int b8_xy = 4 * left_xy[LEFT(i)] + 1;
                        AV_COPY32(mv_cache[cache_idx],
                                  mv[b_xy + b_stride * left_block[0 + i * 2]]);
                        AV_COPY32(mv_cache[cache_idx + 8],
                                  mv[b_xy + b_stride * left_block[1 + i * 2]]);
                        ref_cache[cache_idx]     = ref[b8_xy + (left_block[0 + i * 2] & ~1)];
                        ref_cache[cache_idx + 8] = ref[b8_xy + (left_block[1 + i * 2] & ~1)];
                    } else {
                        AV_ZERO32(mv_cache[cache_idx]);
                        AV_ZERO32(mv_cache[cache_idx + 8]);
                        ref_cache[cache_idx]     =
                        ref_cache[cache_idx + 8] = (left_type[LEFT(i)]) ? LIST_NOT_USED
                                                                        : PART_NOT_AVAILABLE;
                    }
                }
            } else {
                if (USES_LIST(left_type[LTOP], list)) {
                    const int b_xy  = h->mb2b_xy[left_xy[LTOP]] + 3;
                    const int b8_xy = 4 * left_xy[LTOP] + 1;
                    AV_COPY32(mv_cache[-1], mv[b_xy + b_stride * left_block[0]]);
                    ref_cache[-1] = ref[b8_xy + (left_block[0] & ~1)];
                } else {
                    AV_ZERO32(mv_cache[-1]);
                    ref_cache[-1] = left_type[LTOP] ? LIST_NOT_USED
                                                    : PART_NOT_AVAILABLE;
                }
            }

            if (USES_LIST(topright_type, list)) {
                const int b_xy = h->mb2b_xy[topright_xy] + 3 * b_stride;
                AV_COPY32(mv_cache[4 - 1 * 8], mv[b_xy]);
                ref_cache[4 - 1 * 8] = ref[4 * topright_xy + 2];
            } else {
                AV_ZERO32(mv_cache[4 - 1 * 8]);
                ref_cache[4 - 1 * 8] = topright_type ? LIST_NOT_USED
                                                     : PART_NOT_AVAILABLE;
            }
            if(ref_cache[2 - 1*8] < 0 || ref_cache[4 - 1*8] < 0){
                if (USES_LIST(topleft_type, list)) {
                    const int b_xy  = h->mb2b_xy[topleft_xy] + 3 + b_stride +
                                      (h->topleft_partition & 2 * b_stride);
                    const int b8_xy = 4 * topleft_xy + 1 + (h->topleft_partition & 2);
                    AV_COPY32(mv_cache[-1 - 1 * 8], mv[b_xy]);
                    ref_cache[-1 - 1 * 8] = ref[b8_xy];
                } else {
                    AV_ZERO32(mv_cache[-1 - 1 * 8]);
                    ref_cache[-1 - 1 * 8] = topleft_type ? LIST_NOT_USED
                                                         : PART_NOT_AVAILABLE;
                }
            }

            if ((mb_type & (MB_TYPE_SKIP | MB_TYPE_DIRECT2)) && !FRAME_MBAFF)
                continue;

            if (!(mb_type & (MB_TYPE_SKIP | MB_TYPE_DIRECT2))) {
                uint8_t(*mvd_cache)[2]   = &h->mvd_cache[list][scan8[0]];
                uint8_t(*mvd)[2]         = h->mvd_table[list];
                ref_cache[2 + 8 * 0] =
                ref_cache[2 + 8 * 2] = PART_NOT_AVAILABLE;
                AV_ZERO32(mv_cache[2 + 8 * 0]);
                AV_ZERO32(mv_cache[2 + 8 * 2]);

                if (CABAC) {
                    if (USES_LIST(top_type, list)) {
                        const int b_xy = h->mb2br_xy[top_xy];
                        AV_COPY64(mvd_cache[0 - 1 * 8], mvd[b_xy + 0]);
                    } else {
                        AV_ZERO64(mvd_cache[0 - 1 * 8]);
                    }
                    if (USES_LIST(left_type[LTOP], list)) {
                        const int b_xy = h->mb2br_xy[left_xy[LTOP]] + 6;
                        AV_COPY16(mvd_cache[-1 + 0 * 8], mvd[b_xy - left_block[0]]);
                        AV_COPY16(mvd_cache[-1 + 1 * 8], mvd[b_xy - left_block[1]]);
                    } else {
                        AV_ZERO16(mvd_cache[-1 + 0 * 8]);
                        AV_ZERO16(mvd_cache[-1 + 1 * 8]);
                    }
                    if (USES_LIST(left_type[LBOT], list)) {
                        const int b_xy = h->mb2br_xy[left_xy[LBOT]] + 6;
                        AV_COPY16(mvd_cache[-1 + 2 * 8], mvd[b_xy - left_block[2]]);
                        AV_COPY16(mvd_cache[-1 + 3 * 8], mvd[b_xy - left_block[3]]);
                    } else {
                        AV_ZERO16(mvd_cache[-1 + 2 * 8]);
                        AV_ZERO16(mvd_cache[-1 + 3 * 8]);
                    }
                    AV_ZERO16(mvd_cache[2 + 8 * 0]);
                    AV_ZERO16(mvd_cache[2 + 8 * 2]);
                    if (h->slice_type_nos == AV_PICTURE_TYPE_B) {
                        uint8_t *direct_cache = &h->direct_cache[scan8[0]];
                        uint8_t *direct_table = h->direct_table;
                        fill_rectangle(direct_cache, 4, 4, 8, MB_TYPE_16x16 >> 1, 1);

                        if (IS_DIRECT(top_type)) {
                            AV_WN32A(&direct_cache[-1 * 8],
                                     0x01010101u * (MB_TYPE_DIRECT2 >> 1));
                        } else if (IS_8X8(top_type)) {
                            int b8_xy = 4 * top_xy;
                            direct_cache[0 - 1 * 8] = direct_table[b8_xy + 2];
                            direct_cache[2 - 1 * 8] = direct_table[b8_xy + 3];
                        } else {
                            AV_WN32A(&direct_cache[-1 * 8],
                                     0x01010101 * (MB_TYPE_16x16 >> 1));
                        }

                        if (IS_DIRECT(left_type[LTOP]))
                            direct_cache[-1 + 0 * 8] = MB_TYPE_DIRECT2 >> 1;
                        else if (IS_8X8(left_type[LTOP]))
                            direct_cache[-1 + 0 * 8] = direct_table[4 * left_xy[LTOP] + 1 + (left_block[0] & ~1)];
                        else
                            direct_cache[-1 + 0 * 8] = MB_TYPE_16x16 >> 1;

                        if (IS_DIRECT(left_type[LBOT]))
                            direct_cache[-1 + 2 * 8] = MB_TYPE_DIRECT2 >> 1;
                        else if (IS_8X8(left_type[LBOT]))
                            direct_cache[-1 + 2 * 8] = direct_table[4 * left_xy[LBOT] + 1 + (left_block[2] & ~1)];
                        else
                            direct_cache[-1 + 2 * 8] = MB_TYPE_16x16 >> 1;
                    }
                }
            }

#define MAP_MVS                                                         \
    MAP_F2F(scan8[0] - 1 - 1 * 8, topleft_type)                         \
    MAP_F2F(scan8[0] + 0 - 1 * 8, top_type)                             \
    MAP_F2F(scan8[0] + 1 - 1 * 8, top_type)                             \
    MAP_F2F(scan8[0] + 2 - 1 * 8, top_type)                             \
    MAP_F2F(scan8[0] + 3 - 1 * 8, top_type)                             \
    MAP_F2F(scan8[0] + 4 - 1 * 8, topright_type)                        \
    MAP_F2F(scan8[0] - 1 + 0 * 8, left_type[LTOP])                      \
    MAP_F2F(scan8[0] - 1 + 1 * 8, left_type[LTOP])                      \
    MAP_F2F(scan8[0] - 1 + 2 * 8, left_type[LBOT])                      \
    MAP_F2F(scan8[0] - 1 + 3 * 8, left_type[LBOT])

            if (FRAME_MBAFF) {
                if (MB_FIELD) {

#define MAP_F2F(idx, mb_type)                                           \
    if (!IS_INTERLACED(mb_type) && h->ref_cache[list][idx] >= 0) {      \
        h->ref_cache[list][idx]    <<= 1;                               \
        h->mv_cache[list][idx][1]   /= 2;                               \
        h->mvd_cache[list][idx][1] >>= 1;                               \
    }

                    MAP_MVS
                } else {

#undef MAP_F2F
#define MAP_F2F(idx, mb_type)                                           \
    if (IS_INTERLACED(mb_type) && h->ref_cache[list][idx] >= 0) {       \
        h->ref_cache[list][idx]    >>= 1;                               \
        h->mv_cache[list][idx][1]  <<= 1;                               \
        h->mvd_cache[list][idx][1] <<= 1;                               \
    }

                    MAP_MVS
#undef MAP_F2F
                }
            }
        }
    }

    h->neighbor_transform_size = !!IS_8x8DCT(top_type) + !!IS_8x8DCT(left_type[LTOP]);
}

/**
 * decodes a P_SKIP or B_SKIP macroblock
 */
static void av_unused decode_mb_skip(H264Context *h)
{
    const int mb_xy = h->mb_xy;
    int mb_type     = 0;

    memset(h->non_zero_count[mb_xy], 0, 48);

    if (MB_FIELD)
        mb_type |= MB_TYPE_INTERLACED;

    if (h->slice_type_nos == AV_PICTURE_TYPE_B) {
        // just for fill_caches. pred_direct_motion will set the real mb_type
        mb_type |= MB_TYPE_L0L1 | MB_TYPE_DIRECT2 | MB_TYPE_SKIP;
        if (h->direct_spatial_mv_pred) {
            fill_decode_neighbors(h, mb_type);
            fill_decode_caches(h, mb_type); //FIXME check what is needed and what not ...
        }
        ff_h264_pred_direct_motion(h, &mb_type);
        mb_type |= MB_TYPE_SKIP;
    } else {
        mb_type |= MB_TYPE_16x16 | MB_TYPE_P0L0 | MB_TYPE_P1L0 | MB_TYPE_SKIP;

        fill_decode_neighbors(h, mb_type);
        pred_pskip_motion(h);
    }

    write_back_motion(h, mb_type);
    h->cur_pic.f.mb_type[mb_xy]      = mb_type;
    h->cur_pic.f.qscale_table[mb_xy] = h->qscale;
    h->slice_table[mb_xy]            = h->slice_num;
    h->prev_mb_skipped               = 1;
}

#endif /* AVCODEC_H264_MVPRED_H */
