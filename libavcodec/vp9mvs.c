/*
 * VP9 compatible video decoder
 *
 * Copyright (C) 2013 Ronald S. Bultje <rsbultje gmail com>
 * Copyright (C) 2013 Clément Bœsch <u pkh me>
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

#include "internal.h"
#include "vp56.h"
#include "vp9.h"
#include "vp9data.h"

static av_always_inline void clamp_mv(VP56mv *dst, const VP56mv *src,
                                      VP9Context *s)
{
    dst->x = av_clip(src->x, s->min_mv.x, s->max_mv.x);
    dst->y = av_clip(src->y, s->min_mv.y, s->max_mv.y);
}

static void find_ref_mvs(VP9Context *s,
                         VP56mv *pmv, int ref, int z, int idx, int sb)
{
    static const int8_t mv_ref_blk_off[N_BS_SIZES][8][2] = {
        [BS_64x64] = { {  3, -1 }, { -1,  3 }, {  4, -1 }, { -1,  4 },
                       { -1, -1 }, {  0, -1 }, { -1,  0 }, {  6, -1 } },
        [BS_64x32] = { {  0, -1 }, { -1,  0 }, {  4, -1 }, { -1,  2 },
                       { -1, -1 }, {  0, -3 }, { -3,  0 }, {  2, -1 } },
        [BS_32x64] = { { -1,  0 }, {  0, -1 }, { -1,  4 }, {  2, -1 },
                       { -1, -1 }, { -3,  0 }, {  0, -3 }, { -1,  2 } },
        [BS_32x32] = { {  1, -1 }, { -1,  1 }, {  2, -1 }, { -1,  2 },
                       { -1, -1 }, {  0, -3 }, { -3,  0 }, { -3, -3 } },
        [BS_32x16] = { {  0, -1 }, { -1,  0 }, {  2, -1 }, { -1, -1 },
                       { -1,  1 }, {  0, -3 }, { -3,  0 }, { -3, -3 } },
        [BS_16x32] = { { -1,  0 }, {  0, -1 }, { -1,  2 }, { -1, -1 },
                       {  1, -1 }, { -3,  0 }, {  0, -3 }, { -3, -3 } },
        [BS_16x16] = { {  0, -1 }, { -1,  0 }, {  1, -1 }, { -1,  1 },
                       { -1, -1 }, {  0, -3 }, { -3,  0 }, { -3, -3 } },
        [BS_16x8]  = { {  0, -1 }, { -1,  0 }, {  1, -1 }, { -1, -1 },
                       {  0, -2 }, { -2,  0 }, { -2, -1 }, { -1, -2 } },
        [BS_8x16]  = { { -1,  0 }, {  0, -1 }, { -1,  1 }, { -1, -1 },
                       { -2,  0 }, {  0, -2 }, { -1, -2 }, { -2, -1 } },
        [BS_8x8]   = { {  0, -1 }, { -1,  0 }, { -1, -1 }, {  0, -2 },
                       { -2,  0 }, { -1, -2 }, { -2, -1 }, { -2, -2 } },
        [BS_8x4]   = { {  0, -1 }, { -1,  0 }, { -1, -1 }, {  0, -2 },
                       { -2,  0 }, { -1, -2 }, { -2, -1 }, { -2, -2 } },
        [BS_4x8]   = { {  0, -1 }, { -1,  0 }, { -1, -1 }, {  0, -2 },
                       { -2,  0 }, { -1, -2 }, { -2, -1 }, { -2, -2 } },
        [BS_4x4]   = { {  0, -1 }, { -1,  0 }, { -1, -1 }, {  0, -2 },
                       { -2,  0 }, { -1, -2 }, { -2, -1 }, { -2, -2 } },
    };
    VP9Block *b = s->b;
    int row = b->row, col = b->col, row7 = b->row7;
    const int8_t (*p)[2] = mv_ref_blk_off[b->bs];
#define INVALID_MV 0x80008000U
    uint32_t mem = INVALID_MV;
    int i;

#define RETURN_DIRECT_MV(mv)                    \
    do {                                        \
        uint32_t m = AV_RN32A(&mv);             \
        if (!idx) {                             \
            AV_WN32A(pmv, m);                   \
            return;                             \
        } else if (mem == INVALID_MV) {         \
            mem = m;                            \
        } else if (m != mem) {                  \
            AV_WN32A(pmv, m);                   \
            return;                             \
        }                                       \
    } while (0)

    if (sb >= 0) {
        if (sb == 2 || sb == 1) {
            RETURN_DIRECT_MV(b->mv[0][z]);
        } else if (sb == 3) {
            RETURN_DIRECT_MV(b->mv[2][z]);
            RETURN_DIRECT_MV(b->mv[1][z]);
            RETURN_DIRECT_MV(b->mv[0][z]);
        }

#define RETURN_MV(mv)                           \
    do {                                        \
        if (sb > 0) {                           \
            VP56mv tmp;                         \
            uint32_t m;                         \
            clamp_mv(&tmp, &mv, s);             \
            m = AV_RN32A(&tmp);                 \
            if (!idx) {                         \
                AV_WN32A(pmv, m);               \
                return;                         \
            } else if (mem == INVALID_MV) {     \
                mem = m;                        \
            } else if (m != mem) {              \
                AV_WN32A(pmv, m);               \
                return;                         \
            }                                   \
        } else {                                \
            uint32_t m = AV_RN32A(&mv);         \
            if (!idx) {                         \
                clamp_mv(pmv, &mv, s);          \
                return;                         \
            } else if (mem == INVALID_MV) {     \
                mem = m;                        \
            } else if (m != mem) {              \
                clamp_mv(pmv, &mv, s);          \
                return;                         \
            }                                   \
        }                                       \
    } while (0)

        if (row > 0) {
            VP9MVRefPair *mv = &s->frames[CUR_FRAME].mv[(row - 1) * s->sb_cols * 8 + col];

            if (mv->ref[0] == ref)
                RETURN_MV(s->above_mv_ctx[2 * col + (sb & 1)][0]);
            else if (mv->ref[1] == ref)
                RETURN_MV(s->above_mv_ctx[2 * col + (sb & 1)][1]);
        }
        if (col > s->tiling.tile_col_start) {
            VP9MVRefPair *mv = &s->frames[CUR_FRAME].mv[row * s->sb_cols * 8 + col - 1];

            if (mv->ref[0] == ref)
                RETURN_MV(s->left_mv_ctx[2 * row7 + (sb >> 1)][0]);
            else if (mv->ref[1] == ref)
                RETURN_MV(s->left_mv_ctx[2 * row7 + (sb >> 1)][1]);
        }
        i = 2;
    } else {
        i = 0;
    }

    // previously coded MVs in the neighborhood, using same reference frame
    for (; i < 8; i++) {
        int c = p[i][0] + col, r = p[i][1] + row;

        if (c >= s->tiling.tile_col_start && c < s->cols &&
            r >= 0 && r < s->rows) {
            VP9MVRefPair *mv = &s->frames[CUR_FRAME].mv[r * s->sb_cols * 8 + c];

            if (mv->ref[0] == ref)
                RETURN_MV(mv->mv[0]);
            else if (mv->ref[1] == ref)
                RETURN_MV(mv->mv[1]);
        }
    }

    // MV at this position in previous frame, using same reference frame
    if (s->use_last_frame_mvs) {
        VP9MVRefPair *mv = &s->frames[LAST_FRAME].mv[row * s->sb_cols * 8 + col];

        if (!s->last_uses_2pass)
            ff_thread_await_progress(&s->frames[LAST_FRAME].tf, row >> 3, 0);

        if (mv->ref[0] == ref)
            RETURN_MV(mv->mv[0]);
        else if (mv->ref[1] == ref)
            RETURN_MV(mv->mv[1]);
    }

#define RETURN_SCALE_MV(mv, scale)              \
    do {                                        \
        if (scale) {                            \
            VP56mv mv_temp = { -mv.x, -mv.y };  \
            RETURN_MV(mv_temp);                 \
        } else {                                \
            RETURN_MV(mv);                      \
        }                                       \
    } while (0)

    // previously coded MVs in the neighborhood, using different reference frame
    for (i = 0; i < 8; i++) {
        int c = p[i][0] + col, r = p[i][1] + row;

        if (c >= s->tiling.tile_col_start && c < s->cols &&
            r >= 0 && r < s->rows) {
            VP9MVRefPair *mv = &s->frames[CUR_FRAME].mv[r * s->sb_cols * 8 + c];

            if (mv->ref[0] != ref && mv->ref[0] >= 0)
                RETURN_SCALE_MV(mv->mv[0],
                                s->signbias[mv->ref[0]] != s->signbias[ref]);
            if (mv->ref[1] != ref && mv->ref[1] >= 0 &&
                // BUG - libvpx has this condition regardless of whether
                // we used the first ref MV and pre-scaling
                AV_RN32A(&mv->mv[0]) != AV_RN32A(&mv->mv[1])) {
                RETURN_SCALE_MV(mv->mv[1],
                                s->signbias[mv->ref[1]] != s->signbias[ref]);
            }
        }
    }

    // MV at this position in previous frame, using different reference frame
    if (s->use_last_frame_mvs) {
        VP9MVRefPair *mv = &s->frames[LAST_FRAME].mv[row * s->sb_cols * 8 + col];

        // no need to await_progress, because we already did that above
        if (mv->ref[0] != ref && mv->ref[0] >= 0)
            RETURN_SCALE_MV(mv->mv[0],
                            s->signbias[mv->ref[0]] != s->signbias[ref]);
        if (mv->ref[1] != ref && mv->ref[1] >= 0 &&
            // BUG - libvpx has this condition regardless of whether
            // we used the first ref MV and pre-scaling
            AV_RN32A(&mv->mv[0]) != AV_RN32A(&mv->mv[1])) {
            RETURN_SCALE_MV(mv->mv[1],
                            s->signbias[mv->ref[1]] != s->signbias[ref]);
        }
    }

    AV_ZERO32(pmv);
#undef INVALID_MV
#undef RETURN_MV
#undef RETURN_SCALE_MV
}

static av_always_inline int read_mv_component(VP9Context *s, int idx, int hp)
{
    int bit, sign = vp56_rac_get_prob(&s->c, s->prob.p.mv_comp[idx].sign);
    int n, c = vp8_rac_get_tree(&s->c, ff_vp9_mv_class_tree,
                                s->prob.p.mv_comp[idx].classes);

    s->counts.mv_comp[idx].sign[sign]++;
    s->counts.mv_comp[idx].classes[c]++;
    if (c) {
        int m;

        for (n = 0, m = 0; m < c; m++) {
            bit = vp56_rac_get_prob(&s->c, s->prob.p.mv_comp[idx].bits[m]);
            n  |= bit << m;
            s->counts.mv_comp[idx].bits[m][bit]++;
        }
        n <<= 3;
        bit = vp8_rac_get_tree(&s->c, ff_vp9_mv_fp_tree,
                               s->prob.p.mv_comp[idx].fp);
        n  |= bit << 1;
        s->counts.mv_comp[idx].fp[bit]++;
        if (hp) {
            bit = vp56_rac_get_prob(&s->c, s->prob.p.mv_comp[idx].hp);
            s->counts.mv_comp[idx].hp[bit]++;
            n |= bit;
        } else {
            n |= 1;
            // bug in libvpx - we count for bw entropy purposes even if the
            // bit wasn't coded
            s->counts.mv_comp[idx].hp[1]++;
        }
        n += 8 << c;
    } else {
        n = vp56_rac_get_prob(&s->c, s->prob.p.mv_comp[idx].class0);
        s->counts.mv_comp[idx].class0[n]++;
        bit = vp8_rac_get_tree(&s->c, ff_vp9_mv_fp_tree,
                               s->prob.p.mv_comp[idx].class0_fp[n]);
        s->counts.mv_comp[idx].class0_fp[n][bit]++;
        n = (n << 3) | (bit << 1);
        if (hp) {
            bit = vp56_rac_get_prob(&s->c, s->prob.p.mv_comp[idx].class0_hp);
            s->counts.mv_comp[idx].class0_hp[bit]++;
            n |= bit;
        } else {
            n |= 1;
            // bug in libvpx - we count for bw entropy purposes even if the
            // bit wasn't coded
            s->counts.mv_comp[idx].class0_hp[1]++;
        }
    }

    return sign ? -(n + 1) : (n + 1);
}

void ff_vp9_fill_mv(VP9Context *s, VP56mv *mv, int mode, int sb)
{
    VP9Block *b = s->b;

    if (mode == ZEROMV) {
        memset(mv, 0, sizeof(*mv) * 2);
    } else {
        int hp;

        // FIXME cache this value and reuse for other subblocks
        find_ref_mvs(s, &mv[0], b->ref[0], 0, mode == NEARMV,
                     mode == NEWMV ? -1 : sb);
        // FIXME maybe move this code into find_ref_mvs()
        if ((mode == NEWMV || sb == -1) &&
            !(hp = s->highprecisionmvs &&
              abs(mv[0].x) < 64 && abs(mv[0].y) < 64)) {
            if (mv[0].y & 1) {
                if (mv[0].y < 0)
                    mv[0].y++;
                else
                    mv[0].y--;
            }
            if (mv[0].x & 1) {
                if (mv[0].x < 0)
                    mv[0].x++;
                else
                    mv[0].x--;
            }
        }
        if (mode == NEWMV) {
            enum MVJoint j = vp8_rac_get_tree(&s->c, ff_vp9_mv_joint_tree,
                                              s->prob.p.mv_joint);

            s->counts.mv_joint[j]++;
            if (j >= MV_JOINT_V)
                mv[0].y += read_mv_component(s, 0, hp);
            if (j & 1)
                mv[0].x += read_mv_component(s, 1, hp);
        }

        if (b->comp) {
            // FIXME cache this value and reuse for other subblocks
            find_ref_mvs(s, &mv[1], b->ref[1], 1, mode == NEARMV,
                         mode == NEWMV ? -1 : sb);
            if ((mode == NEWMV || sb == -1) &&
                !(hp = s->highprecisionmvs &&
                  abs(mv[1].x) < 64 && abs(mv[1].y) < 64)) {
                if (mv[1].y & 1) {
                    if (mv[1].y < 0)
                        mv[1].y++;
                    else
                        mv[1].y--;
                }
                if (mv[1].x & 1) {
                    if (mv[1].x < 0)
                        mv[1].x++;
                    else
                        mv[1].x--;
                }
            }
            if (mode == NEWMV) {
                enum MVJoint j = vp8_rac_get_tree(&s->c, ff_vp9_mv_joint_tree,
                                                  s->prob.p.mv_joint);

                s->counts.mv_joint[j]++;
                if (j >= MV_JOINT_V)
                    mv[1].y += read_mv_component(s, 0, hp);
                if (j & 1)
                    mv[1].x += read_mv_component(s, 1, hp);
            }
        }
    }
}
