/*
 * H.26L/H.264/AVC/JVT/14496-10/... direct mb/block decoding
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
 * H.264 / AVC / MPEG-4 part10 direct mb/block decoding.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "avcodec.h"
#include "h264dec.h"
#include "h264_ps.h"
#include "mpegutils.h"
#include "rectangle.h"
#include "threadframe.h"

#include <assert.h>

static int get_scale_factor(const H264SliceContext *sl,
                            int poc, int poc1, int i)
{
    int poc0 = sl->ref_list[0][i].poc;
    int64_t pocdiff = poc1 - (int64_t)poc0;
    int td = av_clip_int8(pocdiff);

    if (pocdiff != (int)pocdiff)
        avpriv_request_sample(sl->h264->avctx, "pocdiff overflow");

    if (td == 0 || sl->ref_list[0][i].parent->long_ref) {
        return 256;
    } else {
        int64_t pocdiff0 = poc - (int64_t)poc0;
        int tb = av_clip_int8(pocdiff0);
        int tx = (16384 + (FFABS(td) >> 1)) / td;

        if (pocdiff0 != (int)pocdiff0)
            av_log(sl->h264->avctx, AV_LOG_DEBUG, "pocdiff0 overflow\n");

        return av_clip_intp2((tb * tx + 32) >> 6, 10);
    }
}

void ff_h264_direct_dist_scale_factor(const H264Context *const h,
                                      H264SliceContext *sl)
{
    const int poc  = FIELD_PICTURE(h) ? h->cur_pic_ptr->field_poc[h->picture_structure == PICT_BOTTOM_FIELD]
                                      : h->cur_pic_ptr->poc;
    const int poc1 = sl->ref_list[1][0].poc;
    int i, field;

    if (FRAME_MBAFF(h))
        for (field = 0; field < 2; field++) {
            const int poc  = h->cur_pic_ptr->field_poc[field];
            const int poc1 = sl->ref_list[1][0].parent->field_poc[field];
            for (i = 0; i < 2 * sl->ref_count[0]; i++)
                sl->dist_scale_factor_field[field][i ^ field] =
                    get_scale_factor(sl, poc, poc1, i + 16);
        }

    for (i = 0; i < sl->ref_count[0]; i++)
        sl->dist_scale_factor[i] = get_scale_factor(sl, poc, poc1, i);
}

static void fill_colmap(const H264Context *h, H264SliceContext *sl,
                        int map[2][16 + 32], int list,
                        int field, int colfield, int mbafi)
{
    const H264Picture *const ref1 = sl->ref_list[1][0].parent;
    int j, old_ref, rfield;
    int start  = mbafi ? 16                       : 0;
    int end    = mbafi ? 16 + 2 * sl->ref_count[0] : sl->ref_count[0];
    int interl = mbafi || h->picture_structure != PICT_FRAME;

    /* bogus; fills in for missing frames */
    memset(map[list], 0, sizeof(map[list]));

    for (rfield = 0; rfield < 2; rfield++) {
        for (old_ref = 0; old_ref < ref1->ref_count[colfield][list]; old_ref++) {
            int poc = ref1->ref_poc[colfield][list][old_ref];

            if (!interl)
                poc |= 3;
            // FIXME: store all MBAFF references so this is not needed
            else if (interl && (poc & 3) == 3)
                poc = (poc & ~3) + rfield + 1;

            for (j = start; j < end; j++) {
                if (4 * sl->ref_list[0][j].parent->frame_num +
                    (sl->ref_list[0][j].reference & 3) == poc) {
                    int cur_ref = mbafi ? (j - 16) ^ field : j;
                    if (ref1->mbaff)
                        map[list][2 * old_ref + (rfield ^ field) + 16] = cur_ref;
                    if (rfield == field || !interl)
                        map[list][old_ref] = cur_ref;
                    break;
                }
            }
        }
    }
}

void ff_h264_direct_ref_list_init(const H264Context *const h, H264SliceContext *sl)
{
    H264Ref *const ref1 = &sl->ref_list[1][0];
    H264Picture *const cur = h->cur_pic_ptr;
    int list, j, field;
    int sidx     = (h->picture_structure & 1) ^ 1;
    int ref1sidx = (ref1->reference      & 1) ^ 1;

    for (list = 0; list < sl->list_count; list++) {
        cur->ref_count[sidx][list] = sl->ref_count[list];
        for (j = 0; j < sl->ref_count[list]; j++)
            cur->ref_poc[sidx][list][j] = 4 * sl->ref_list[list][j].parent->frame_num +
                                          (sl->ref_list[list][j].reference & 3);
    }

    if (h->picture_structure == PICT_FRAME) {
        memcpy(cur->ref_count[1], cur->ref_count[0], sizeof(cur->ref_count[0]));
        memcpy(cur->ref_poc[1],   cur->ref_poc[0],   sizeof(cur->ref_poc[0]));
    }

    if (h->current_slice == 0) {
        cur->mbaff = FRAME_MBAFF(h);
    } else {
        av_assert0(cur->mbaff == FRAME_MBAFF(h));
    }

    sl->col_fieldoff = 0;

    if (sl->list_count != 2 || !sl->ref_count[1])
        return;

    if (h->picture_structure == PICT_FRAME) {
        int cur_poc  = h->cur_pic_ptr->poc;
        const int *col_poc = sl->ref_list[1][0].parent->field_poc;
        if (col_poc[0] == INT_MAX && col_poc[1] == INT_MAX) {
            av_log(h->avctx, AV_LOG_ERROR, "co located POCs unavailable\n");
            sl->col_parity = 1;
        } else
            sl->col_parity = (FFABS(col_poc[0] - (int64_t)cur_poc) >=
                              FFABS(col_poc[1] - (int64_t)cur_poc));
        ref1sidx =
        sidx     = sl->col_parity;
    // FL -> FL & differ parity
    } else if (!(h->picture_structure & sl->ref_list[1][0].reference) &&
               !sl->ref_list[1][0].parent->mbaff) {
        sl->col_fieldoff = 2 * sl->ref_list[1][0].reference - 3;
    }

    if (sl->slice_type_nos != AV_PICTURE_TYPE_B || sl->direct_spatial_mv_pred)
        return;

    for (list = 0; list < 2; list++) {
        fill_colmap(h, sl, sl->map_col_to_list0, list, sidx, ref1sidx, 0);
        if (FRAME_MBAFF(h))
            for (field = 0; field < 2; field++)
                fill_colmap(h, sl, sl->map_col_to_list0_field[field], list, field,
                            field, 1);
    }
}

static void await_reference_mb_row(const H264Context *const h, H264Ref *ref,
                                   int mb_y)
{
    int ref_field         = ref->reference - 1;
    int ref_field_picture = ref->parent->field_picture;
    int ref_height        = 16 * h->mb_height >> ref_field_picture;

    if (!HAVE_THREADS || !(h->avctx->active_thread_type & FF_THREAD_FRAME))
        return;

    /* FIXME: It can be safe to access mb stuff
     * even if pixels aren't deblocked yet. */

    ff_thread_await_progress(&ref->parent->tf,
                             FFMIN(16 * mb_y >> ref_field_picture,
                                   ref_height - 1),
                             ref_field_picture && ref_field);
}

static void pred_spatial_direct_motion(const H264Context *const h, H264SliceContext *sl,
                                       int *mb_type)
{
    int b8_stride = 2;
    int b4_stride = h->b_stride;
    int mb_xy = sl->mb_xy, mb_y = sl->mb_y;
    int mb_type_col[2];
    const int16_t (*l1mv0)[2], (*l1mv1)[2];
    const int8_t *l1ref0, *l1ref1;
    const int is_b8x8 = IS_8X8(*mb_type);
    unsigned int sub_mb_type = MB_TYPE_L0L1;
    int i8, i4;
    int ref[2];
    int mv[2];
    int list;

    assert(sl->ref_list[1][0].reference & 3);

    await_reference_mb_row(h, &sl->ref_list[1][0],
                           sl->mb_y + !!IS_INTERLACED(*mb_type));

#define MB_TYPE_16x16_OR_INTRA (MB_TYPE_16x16 | MB_TYPE_INTRA4x4 | \
                                MB_TYPE_INTRA16x16 | MB_TYPE_INTRA_PCM)

    /* ref = min(neighbors) */
    for (list = 0; list < 2; list++) {
        int left_ref     = sl->ref_cache[list][scan8[0] - 1];
        int top_ref      = sl->ref_cache[list][scan8[0] - 8];
        int refc         = sl->ref_cache[list][scan8[0] - 8 + 4];
        const int16_t *C = sl->mv_cache[list][scan8[0]  - 8 + 4];
        if (refc == PART_NOT_AVAILABLE) {
            refc = sl->ref_cache[list][scan8[0] - 8 - 1];
            C    = sl->mv_cache[list][scan8[0]  - 8 - 1];
        }
        ref[list] = FFMIN3((unsigned)left_ref,
                           (unsigned)top_ref,
                           (unsigned)refc);
        if (ref[list] >= 0) {
            /* This is just pred_motion() but with the cases removed that
             * cannot happen for direct blocks. */
            const int16_t *const A = sl->mv_cache[list][scan8[0] - 1];
            const int16_t *const B = sl->mv_cache[list][scan8[0] - 8];

            int match_count = (left_ref == ref[list]) +
                              (top_ref  == ref[list]) +
                              (refc     == ref[list]);

            if (match_count > 1) { // most common
                mv[list] = pack16to32(mid_pred(A[0], B[0], C[0]),
                                      mid_pred(A[1], B[1], C[1]));
            } else {
                assert(match_count == 1);
                if (left_ref == ref[list])
                    mv[list] = AV_RN32A(A);
                else if (top_ref == ref[list])
                    mv[list] = AV_RN32A(B);
                else
                    mv[list] = AV_RN32A(C);
            }
            av_assert2(ref[list] < (sl->ref_count[list] << !!FRAME_MBAFF(h)));
        } else {
            int mask = ~(MB_TYPE_L0 << (2 * list));
            mv[list]  = 0;
            ref[list] = -1;
            if (!is_b8x8)
                *mb_type &= mask;
            sub_mb_type &= mask;
        }
    }
    if (ref[0] < 0 && ref[1] < 0) {
        ref[0] = ref[1] = 0;
        if (!is_b8x8)
            *mb_type |= MB_TYPE_L0L1;
        sub_mb_type |= MB_TYPE_L0L1;
    }

    if (!(is_b8x8 | mv[0] | mv[1])) {
        fill_rectangle(&sl->ref_cache[0][scan8[0]], 4, 4, 8, (uint8_t)ref[0], 1);
        fill_rectangle(&sl->ref_cache[1][scan8[0]], 4, 4, 8, (uint8_t)ref[1], 1);
        fill_rectangle(&sl->mv_cache[0][scan8[0]], 4, 4, 8, 0, 4);
        fill_rectangle(&sl->mv_cache[1][scan8[0]], 4, 4, 8, 0, 4);
        *mb_type = (*mb_type & ~(MB_TYPE_8x8 | MB_TYPE_16x8 | MB_TYPE_8x16 |
                                 MB_TYPE_P1L0 | MB_TYPE_P1L1)) |
                   MB_TYPE_16x16 | MB_TYPE_DIRECT2;
        return;
    }

    if (IS_INTERLACED(sl->ref_list[1][0].parent->mb_type[mb_xy])) { // AFL/AFR/FR/FL -> AFL/FL
        if (!IS_INTERLACED(*mb_type)) {                    //     AFR/FR    -> AFL/FL
            mb_y  = (sl->mb_y & ~1) + sl->col_parity;
            mb_xy = sl->mb_x +
                    ((sl->mb_y & ~1) + sl->col_parity) * h->mb_stride;
            b8_stride = 0;
        } else {
            mb_y  += sl->col_fieldoff;
            mb_xy += h->mb_stride * sl->col_fieldoff; // non-zero for FL -> FL & differ parity
        }
        goto single_col;
    } else {                                             // AFL/AFR/FR/FL -> AFR/FR
        if (IS_INTERLACED(*mb_type)) {                   // AFL       /FL -> AFR/FR
            mb_y           =  sl->mb_y & ~1;
            mb_xy          = (sl->mb_y & ~1) * h->mb_stride + sl->mb_x;
            mb_type_col[0] = sl->ref_list[1][0].parent->mb_type[mb_xy];
            mb_type_col[1] = sl->ref_list[1][0].parent->mb_type[mb_xy + h->mb_stride];
            b8_stride      = 2 + 4 * h->mb_stride;
            b4_stride     *= 6;
            if (IS_INTERLACED(mb_type_col[0]) !=
                IS_INTERLACED(mb_type_col[1])) {
                mb_type_col[0] &= ~MB_TYPE_INTERLACED;
                mb_type_col[1] &= ~MB_TYPE_INTERLACED;
            }

            sub_mb_type |= MB_TYPE_16x16 | MB_TYPE_DIRECT2; /* B_SUB_8x8 */
            if ((mb_type_col[0] & MB_TYPE_16x16_OR_INTRA) &&
                (mb_type_col[1] & MB_TYPE_16x16_OR_INTRA) &&
                !is_b8x8) {
                *mb_type |= MB_TYPE_16x8 | MB_TYPE_DIRECT2;  /* B_16x8 */
            } else {
                *mb_type |= MB_TYPE_8x8;
            }
        } else {                                         //     AFR/FR    -> AFR/FR
single_col:
            mb_type_col[0] =
            mb_type_col[1] = sl->ref_list[1][0].parent->mb_type[mb_xy];

            sub_mb_type |= MB_TYPE_16x16 | MB_TYPE_DIRECT2; /* B_SUB_8x8 */
            if (!is_b8x8 && (mb_type_col[0] & MB_TYPE_16x16_OR_INTRA)) {
                *mb_type |= MB_TYPE_16x16 | MB_TYPE_DIRECT2; /* B_16x16 */
            } else if (!is_b8x8 &&
                       (mb_type_col[0] & (MB_TYPE_16x8 | MB_TYPE_8x16))) {
                *mb_type |= MB_TYPE_DIRECT2 |
                            (mb_type_col[0] & (MB_TYPE_16x8 | MB_TYPE_8x16));
            } else {
                if (!h->ps.sps->direct_8x8_inference_flag) {
                    /* FIXME: Save sub mb types from previous frames (or derive
                     * from MVs) so we know exactly what block size to use. */
                    sub_mb_type += (MB_TYPE_8x8 - MB_TYPE_16x16); /* B_SUB_4x4 */
                }
                *mb_type |= MB_TYPE_8x8;
            }
        }
    }

    await_reference_mb_row(h, &sl->ref_list[1][0], mb_y);

    l1mv0  = (void*)&sl->ref_list[1][0].parent->motion_val[0][h->mb2b_xy[mb_xy]];
    l1mv1  = (void*)&sl->ref_list[1][0].parent->motion_val[1][h->mb2b_xy[mb_xy]];
    l1ref0 = &sl->ref_list[1][0].parent->ref_index[0][4 * mb_xy];
    l1ref1 = &sl->ref_list[1][0].parent->ref_index[1][4 * mb_xy];
    if (!b8_stride) {
        if (sl->mb_y & 1) {
            l1ref0 += 2;
            l1ref1 += 2;
            l1mv0  += 2 * b4_stride;
            l1mv1  += 2 * b4_stride;
        }
    }

    if (IS_INTERLACED(*mb_type) != IS_INTERLACED(mb_type_col[0])) {
        int n = 0;
        for (i8 = 0; i8 < 4; i8++) {
            int x8  = i8 & 1;
            int y8  = i8 >> 1;
            int xy8 = x8     + y8 * b8_stride;
            int xy4 = x8 * 3 + y8 * b4_stride;
            int a, b;

            if (is_b8x8 && !IS_DIRECT(sl->sub_mb_type[i8]))
                continue;
            sl->sub_mb_type[i8] = sub_mb_type;

            fill_rectangle(&sl->ref_cache[0][scan8[i8 * 4]], 2, 2, 8,
                           (uint8_t)ref[0], 1);
            fill_rectangle(&sl->ref_cache[1][scan8[i8 * 4]], 2, 2, 8,
                           (uint8_t)ref[1], 1);
            if (!IS_INTRA(mb_type_col[y8]) && !sl->ref_list[1][0].parent->long_ref &&
                ((l1ref0[xy8] == 0 &&
                  FFABS(l1mv0[xy4][0]) <= 1 &&
                  FFABS(l1mv0[xy4][1]) <= 1) ||
                 (l1ref0[xy8] < 0 &&
                  l1ref1[xy8] == 0 &&
                  FFABS(l1mv1[xy4][0]) <= 1 &&
                  FFABS(l1mv1[xy4][1]) <= 1))) {
                a =
                b = 0;
                if (ref[0] > 0)
                    a = mv[0];
                if (ref[1] > 0)
                    b = mv[1];
                n++;
            } else {
                a = mv[0];
                b = mv[1];
            }
            fill_rectangle(&sl->mv_cache[0][scan8[i8 * 4]], 2, 2, 8, a, 4);
            fill_rectangle(&sl->mv_cache[1][scan8[i8 * 4]], 2, 2, 8, b, 4);
        }
        if (!is_b8x8 && !(n & 3))
            *mb_type = (*mb_type & ~(MB_TYPE_8x8 | MB_TYPE_16x8 | MB_TYPE_8x16 |
                                     MB_TYPE_P1L0 | MB_TYPE_P1L1)) |
                       MB_TYPE_16x16 | MB_TYPE_DIRECT2;
    } else if (IS_16X16(*mb_type)) {
        int a, b;

        fill_rectangle(&sl->ref_cache[0][scan8[0]], 4, 4, 8, (uint8_t)ref[0], 1);
        fill_rectangle(&sl->ref_cache[1][scan8[0]], 4, 4, 8, (uint8_t)ref[1], 1);
        if (!IS_INTRA(mb_type_col[0]) && !sl->ref_list[1][0].parent->long_ref &&
            ((l1ref0[0] == 0 &&
              FFABS(l1mv0[0][0]) <= 1 &&
              FFABS(l1mv0[0][1]) <= 1) ||
             (l1ref0[0] < 0 && !l1ref1[0] &&
              FFABS(l1mv1[0][0]) <= 1 &&
              FFABS(l1mv1[0][1]) <= 1 &&
              h->x264_build > 33U))) {
            a = b = 0;
            if (ref[0] > 0)
                a = mv[0];
            if (ref[1] > 0)
                b = mv[1];
        } else {
            a = mv[0];
            b = mv[1];
        }
        fill_rectangle(&sl->mv_cache[0][scan8[0]], 4, 4, 8, a, 4);
        fill_rectangle(&sl->mv_cache[1][scan8[0]], 4, 4, 8, b, 4);
    } else {
        int n = 0;
        for (i8 = 0; i8 < 4; i8++) {
            const int x8 = i8 & 1;
            const int y8 = i8 >> 1;

            if (is_b8x8 && !IS_DIRECT(sl->sub_mb_type[i8]))
                continue;
            sl->sub_mb_type[i8] = sub_mb_type;

            fill_rectangle(&sl->mv_cache[0][scan8[i8 * 4]], 2, 2, 8, mv[0], 4);
            fill_rectangle(&sl->mv_cache[1][scan8[i8 * 4]], 2, 2, 8, mv[1], 4);
            fill_rectangle(&sl->ref_cache[0][scan8[i8 * 4]], 2, 2, 8,
                           (uint8_t)ref[0], 1);
            fill_rectangle(&sl->ref_cache[1][scan8[i8 * 4]], 2, 2, 8,
                           (uint8_t)ref[1], 1);

            assert(b8_stride == 2);
            /* col_zero_flag */
            if (!IS_INTRA(mb_type_col[0]) && !sl->ref_list[1][0].parent->long_ref &&
                (l1ref0[i8] == 0 ||
                 (l1ref0[i8] < 0 &&
                  l1ref1[i8] == 0 &&
                  h->x264_build > 33U))) {
                const int16_t (*l1mv)[2] = l1ref0[i8] == 0 ? l1mv0 : l1mv1;
                if (IS_SUB_8X8(sub_mb_type)) {
                    const int16_t *mv_col = l1mv[x8 * 3 + y8 * 3 * b4_stride];
                    if (FFABS(mv_col[0]) <= 1 && FFABS(mv_col[1]) <= 1) {
                        if (ref[0] == 0)
                            fill_rectangle(&sl->mv_cache[0][scan8[i8 * 4]], 2, 2,
                                           8, 0, 4);
                        if (ref[1] == 0)
                            fill_rectangle(&sl->mv_cache[1][scan8[i8 * 4]], 2, 2,
                                           8, 0, 4);
                        n += 4;
                    }
                } else {
                    int m = 0;
                    for (i4 = 0; i4 < 4; i4++) {
                        const int16_t *mv_col = l1mv[x8 * 2 + (i4 & 1) +
                                                     (y8 * 2 + (i4 >> 1)) * b4_stride];
                        if (FFABS(mv_col[0]) <= 1 && FFABS(mv_col[1]) <= 1) {
                            if (ref[0] == 0)
                                AV_ZERO32(sl->mv_cache[0][scan8[i8 * 4 + i4]]);
                            if (ref[1] == 0)
                                AV_ZERO32(sl->mv_cache[1][scan8[i8 * 4 + i4]]);
                            m++;
                        }
                    }
                    if (!(m & 3))
                        sl->sub_mb_type[i8] += MB_TYPE_16x16 - MB_TYPE_8x8;
                    n += m;
                }
            }
        }
        if (!is_b8x8 && !(n & 15))
            *mb_type = (*mb_type & ~(MB_TYPE_8x8 | MB_TYPE_16x8 | MB_TYPE_8x16 |
                                     MB_TYPE_P1L0 | MB_TYPE_P1L1)) |
                       MB_TYPE_16x16 | MB_TYPE_DIRECT2;
    }
}

static void pred_temp_direct_motion(const H264Context *const h, H264SliceContext *sl,
                                    int *mb_type)
{
    int b8_stride = 2;
    int b4_stride = h->b_stride;
    int mb_xy = sl->mb_xy, mb_y = sl->mb_y;
    int mb_type_col[2];
    const int16_t (*l1mv0)[2], (*l1mv1)[2];
    const int8_t *l1ref0, *l1ref1;
    const int is_b8x8 = IS_8X8(*mb_type);
    unsigned int sub_mb_type;
    int i8, i4;

    assert(sl->ref_list[1][0].reference & 3);

    await_reference_mb_row(h, &sl->ref_list[1][0],
                           sl->mb_y + !!IS_INTERLACED(*mb_type));

    if (IS_INTERLACED(sl->ref_list[1][0].parent->mb_type[mb_xy])) { // AFL/AFR/FR/FL -> AFL/FL
        if (!IS_INTERLACED(*mb_type)) {                    //     AFR/FR    -> AFL/FL
            mb_y  = (sl->mb_y & ~1) + sl->col_parity;
            mb_xy = sl->mb_x +
                    ((sl->mb_y & ~1) + sl->col_parity) * h->mb_stride;
            b8_stride = 0;
        } else {
            mb_y  += sl->col_fieldoff;
            mb_xy += h->mb_stride * sl->col_fieldoff; // non-zero for FL -> FL & differ parity
        }
        goto single_col;
    } else {                                        // AFL/AFR/FR/FL -> AFR/FR
        if (IS_INTERLACED(*mb_type)) {              // AFL       /FL -> AFR/FR
            mb_y           = sl->mb_y & ~1;
            mb_xy          = sl->mb_x + (sl->mb_y & ~1) * h->mb_stride;
            mb_type_col[0] = sl->ref_list[1][0].parent->mb_type[mb_xy];
            mb_type_col[1] = sl->ref_list[1][0].parent->mb_type[mb_xy + h->mb_stride];
            b8_stride      = 2 + 4 * h->mb_stride;
            b4_stride     *= 6;
            if (IS_INTERLACED(mb_type_col[0]) !=
                IS_INTERLACED(mb_type_col[1])) {
                mb_type_col[0] &= ~MB_TYPE_INTERLACED;
                mb_type_col[1] &= ~MB_TYPE_INTERLACED;
            }

            sub_mb_type = MB_TYPE_16x16 | MB_TYPE_P0L0 | MB_TYPE_P0L1 |
                          MB_TYPE_DIRECT2;                  /* B_SUB_8x8 */

            if ((mb_type_col[0] & MB_TYPE_16x16_OR_INTRA) &&
                (mb_type_col[1] & MB_TYPE_16x16_OR_INTRA) &&
                !is_b8x8) {
                *mb_type |= MB_TYPE_16x8 | MB_TYPE_L0L1 |
                            MB_TYPE_DIRECT2;                /* B_16x8 */
            } else {
                *mb_type |= MB_TYPE_8x8 | MB_TYPE_L0L1;
            }
        } else {                                    //     AFR/FR    -> AFR/FR
single_col:
            mb_type_col[0]     =
                mb_type_col[1] = sl->ref_list[1][0].parent->mb_type[mb_xy];

            sub_mb_type = MB_TYPE_16x16 | MB_TYPE_P0L0 | MB_TYPE_P0L1 |
                          MB_TYPE_DIRECT2;                  /* B_SUB_8x8 */
            if (!is_b8x8 && (mb_type_col[0] & MB_TYPE_16x16_OR_INTRA)) {
                *mb_type |= MB_TYPE_16x16 | MB_TYPE_P0L0 | MB_TYPE_P0L1 |
                            MB_TYPE_DIRECT2;                /* B_16x16 */
            } else if (!is_b8x8 &&
                       (mb_type_col[0] & (MB_TYPE_16x8 | MB_TYPE_8x16))) {
                *mb_type |= MB_TYPE_L0L1 | MB_TYPE_DIRECT2 |
                            (mb_type_col[0] & (MB_TYPE_16x8 | MB_TYPE_8x16));
            } else {
                if (!h->ps.sps->direct_8x8_inference_flag) {
                    /* FIXME: save sub mb types from previous frames (or derive
                     * from MVs) so we know exactly what block size to use */
                    sub_mb_type = MB_TYPE_8x8 | MB_TYPE_P0L0 | MB_TYPE_P0L1 |
                                  MB_TYPE_DIRECT2;          /* B_SUB_4x4 */
                }
                *mb_type |= MB_TYPE_8x8 | MB_TYPE_L0L1;
            }
        }
    }

    await_reference_mb_row(h, &sl->ref_list[1][0], mb_y);

    l1mv0  = (void*)&sl->ref_list[1][0].parent->motion_val[0][h->mb2b_xy[mb_xy]];
    l1mv1  = (void*)&sl->ref_list[1][0].parent->motion_val[1][h->mb2b_xy[mb_xy]];
    l1ref0 = &sl->ref_list[1][0].parent->ref_index[0][4 * mb_xy];
    l1ref1 = &sl->ref_list[1][0].parent->ref_index[1][4 * mb_xy];
    if (!b8_stride) {
        if (sl->mb_y & 1) {
            l1ref0 += 2;
            l1ref1 += 2;
            l1mv0  += 2 * b4_stride;
            l1mv1  += 2 * b4_stride;
        }
    }

    {
        const int *map_col_to_list0[2] = { sl->map_col_to_list0[0],
                                           sl->map_col_to_list0[1] };
        const int *dist_scale_factor = sl->dist_scale_factor;
        int ref_offset;

        if (FRAME_MBAFF(h) && IS_INTERLACED(*mb_type)) {
            map_col_to_list0[0] = sl->map_col_to_list0_field[sl->mb_y & 1][0];
            map_col_to_list0[1] = sl->map_col_to_list0_field[sl->mb_y & 1][1];
            dist_scale_factor   = sl->dist_scale_factor_field[sl->mb_y & 1];
        }
        ref_offset = (sl->ref_list[1][0].parent->mbaff << 4) & (mb_type_col[0] >> 3);

        if (IS_INTERLACED(*mb_type) != IS_INTERLACED(mb_type_col[0])) {
            int y_shift = 2 * !IS_INTERLACED(*mb_type);
            assert(h->ps.sps->direct_8x8_inference_flag);

            for (i8 = 0; i8 < 4; i8++) {
                const int x8 = i8 & 1;
                const int y8 = i8 >> 1;
                int ref0, scale;
                const int16_t (*l1mv)[2] = l1mv0;

                if (is_b8x8 && !IS_DIRECT(sl->sub_mb_type[i8]))
                    continue;
                sl->sub_mb_type[i8] = sub_mb_type;

                fill_rectangle(&sl->ref_cache[1][scan8[i8 * 4]], 2, 2, 8, 0, 1);
                if (IS_INTRA(mb_type_col[y8])) {
                    fill_rectangle(&sl->ref_cache[0][scan8[i8 * 4]], 2, 2, 8, 0, 1);
                    fill_rectangle(&sl->mv_cache[0][scan8[i8 * 4]], 2, 2, 8, 0, 4);
                    fill_rectangle(&sl->mv_cache[1][scan8[i8 * 4]], 2, 2, 8, 0, 4);
                    continue;
                }

                ref0 = l1ref0[x8 + y8 * b8_stride];
                if (ref0 >= 0)
                    ref0 = map_col_to_list0[0][ref0 + ref_offset];
                else {
                    ref0 = map_col_to_list0[1][l1ref1[x8 + y8 * b8_stride] +
                                               ref_offset];
                    l1mv = l1mv1;
                }
                scale = dist_scale_factor[ref0];
                fill_rectangle(&sl->ref_cache[0][scan8[i8 * 4]], 2, 2, 8,
                               ref0, 1);

                {
                    const int16_t *mv_col = l1mv[x8 * 3 + y8 * b4_stride];
                    int my_col            = (mv_col[1] * (1 << y_shift)) / 2;
                    int mx                = (scale * mv_col[0] + 128) >> 8;
                    int my                = (scale * my_col    + 128) >> 8;
                    fill_rectangle(&sl->mv_cache[0][scan8[i8 * 4]], 2, 2, 8,
                                   pack16to32(mx, my), 4);
                    fill_rectangle(&sl->mv_cache[1][scan8[i8 * 4]], 2, 2, 8,
                                   pack16to32(mx - mv_col[0], my - my_col), 4);
                }
            }
            return;
        }

        /* one-to-one mv scaling */

        if (IS_16X16(*mb_type)) {
            int ref, mv0, mv1;

            fill_rectangle(&sl->ref_cache[1][scan8[0]], 4, 4, 8, 0, 1);
            if (IS_INTRA(mb_type_col[0])) {
                ref = mv0 = mv1 = 0;
            } else {
                const int ref0 = l1ref0[0] >= 0 ? map_col_to_list0[0][l1ref0[0] + ref_offset]
                                                : map_col_to_list0[1][l1ref1[0] + ref_offset];
                const int scale = dist_scale_factor[ref0];
                const int16_t *mv_col = l1ref0[0] >= 0 ? l1mv0[0] : l1mv1[0];
                int mv_l0[2];
                mv_l0[0] = (scale * mv_col[0] + 128) >> 8;
                mv_l0[1] = (scale * mv_col[1] + 128) >> 8;
                ref      = ref0;
                mv0      = pack16to32(mv_l0[0], mv_l0[1]);
                mv1      = pack16to32(mv_l0[0] - mv_col[0], mv_l0[1] - mv_col[1]);
            }
            fill_rectangle(&sl->ref_cache[0][scan8[0]], 4, 4, 8, ref, 1);
            fill_rectangle(&sl->mv_cache[0][scan8[0]], 4, 4, 8, mv0, 4);
            fill_rectangle(&sl->mv_cache[1][scan8[0]], 4, 4, 8, mv1, 4);
        } else {
            for (i8 = 0; i8 < 4; i8++) {
                const int x8 = i8 & 1;
                const int y8 = i8 >> 1;
                int ref0, scale;
                const int16_t (*l1mv)[2] = l1mv0;

                if (is_b8x8 && !IS_DIRECT(sl->sub_mb_type[i8]))
                    continue;
                sl->sub_mb_type[i8] = sub_mb_type;
                fill_rectangle(&sl->ref_cache[1][scan8[i8 * 4]], 2, 2, 8, 0, 1);
                if (IS_INTRA(mb_type_col[0])) {
                    fill_rectangle(&sl->ref_cache[0][scan8[i8 * 4]], 2, 2, 8, 0, 1);
                    fill_rectangle(&sl->mv_cache[0][scan8[i8 * 4]], 2, 2, 8, 0, 4);
                    fill_rectangle(&sl->mv_cache[1][scan8[i8 * 4]], 2, 2, 8, 0, 4);
                    continue;
                }

                assert(b8_stride == 2);
                ref0 = l1ref0[i8];
                if (ref0 >= 0)
                    ref0 = map_col_to_list0[0][ref0 + ref_offset];
                else {
                    ref0 = map_col_to_list0[1][l1ref1[i8] + ref_offset];
                    l1mv = l1mv1;
                }
                scale = dist_scale_factor[ref0];

                fill_rectangle(&sl->ref_cache[0][scan8[i8 * 4]], 2, 2, 8,
                               ref0, 1);
                if (IS_SUB_8X8(sub_mb_type)) {
                    const int16_t *mv_col = l1mv[x8 * 3 + y8 * 3 * b4_stride];
                    int mx                = (scale * mv_col[0] + 128) >> 8;
                    int my                = (scale * mv_col[1] + 128) >> 8;
                    fill_rectangle(&sl->mv_cache[0][scan8[i8 * 4]], 2, 2, 8,
                                   pack16to32(mx, my), 4);
                    fill_rectangle(&sl->mv_cache[1][scan8[i8 * 4]], 2, 2, 8,
                                   pack16to32(mx - mv_col[0], my - mv_col[1]), 4);
                } else {
                    for (i4 = 0; i4 < 4; i4++) {
                        const int16_t *mv_col = l1mv[x8 * 2 + (i4 & 1) +
                                                     (y8 * 2 + (i4 >> 1)) * b4_stride];
                        int16_t *mv_l0 = sl->mv_cache[0][scan8[i8 * 4 + i4]];
                        mv_l0[0] = (scale * mv_col[0] + 128) >> 8;
                        mv_l0[1] = (scale * mv_col[1] + 128) >> 8;
                        AV_WN32A(sl->mv_cache[1][scan8[i8 * 4 + i4]],
                                 pack16to32(mv_l0[0] - mv_col[0],
                                            mv_l0[1] - mv_col[1]));
                    }
                }
            }
        }
    }
}

void ff_h264_pred_direct_motion(const H264Context *const h, H264SliceContext *sl,
                                int *mb_type)
{
    if (sl->direct_spatial_mv_pred)
        pred_spatial_direct_motion(h, sl, mb_type);
    else
        pred_temp_direct_motion(h, sl, mb_type);
}
