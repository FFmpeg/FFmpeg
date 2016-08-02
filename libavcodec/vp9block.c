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

#include "libavutil/avassert.h"

#include "avcodec.h"
#include "internal.h"
#include "videodsp.h"
#include "vp56.h"
#include "vp9.h"
#include "vp9data.h"

static const uint8_t bwh_tab[2][N_BS_SIZES][2] = {
    {
        { 16, 16 }, { 16, 8 }, { 8, 16 }, { 8, 8 }, { 8, 4 }, { 4, 8 },
        {  4,  4 }, {  4, 2 }, { 2,  4 }, { 2, 2 }, { 2, 1 }, { 1, 2 }, { 1, 1 },
    },  {
        {  8,  8 }, {  8, 4 }, { 4,  8 }, { 4, 4 }, { 4, 2 }, { 2, 4 },
        {  2,  2 }, {  2, 1 }, { 1,  2 }, { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 },
    }
};

// differential forward probability updates
static void decode_mode(VP9Context *s, VP9Block *const b)
{
    static const uint8_t left_ctx[N_BS_SIZES] = {
        0x0, 0x8, 0x0, 0x8, 0xc, 0x8, 0xc, 0xe, 0xc, 0xe, 0xf, 0xe, 0xf
    };
    static const uint8_t above_ctx[N_BS_SIZES] = {
        0x0, 0x0, 0x8, 0x8, 0x8, 0xc, 0xc, 0xc, 0xe, 0xe, 0xe, 0xf, 0xf
    };
    static const uint8_t max_tx_for_bl_bp[N_BS_SIZES] = {
        TX_32X32, TX_32X32, TX_32X32, TX_32X32, TX_16X16, TX_16X16,
        TX_16X16, TX_8X8,   TX_8X8,   TX_8X8,   TX_4X4,   TX_4X4,  TX_4X4
    };
    int row = b->row, col = b->col, row7 = b->row7;
    enum TxfmMode max_tx = max_tx_for_bl_bp[b->bs];
    int w4 = FFMIN(s->cols - col, bwh_tab[1][b->bs][0]);
    int h4 = FFMIN(s->rows - row, bwh_tab[1][b->bs][1]);
    int have_a = row > 0, have_l = col > s->tiling.tile_col_start;
    int y;

    if (!s->segmentation.enabled) {
        b->seg_id = 0;
    } else if (s->keyframe || s->intraonly) {
        b->seg_id = s->segmentation.update_map ?
                    vp8_rac_get_tree(&s->c, ff_vp9_segmentation_tree, s->prob.seg) : 0;
    } else if (!s->segmentation.update_map ||
               (s->segmentation.temporal &&
                vp56_rac_get_prob_branchy(&s->c,
                                          s->prob.segpred[s->above_segpred_ctx[col] +
                                                          s->left_segpred_ctx[row7]]))) {
        uint8_t *refsegmap = s->frames[LAST_FRAME].segmentation_map;
        int pred = MAX_SEGMENT - 1;
        int x;

        if (!s->last_uses_2pass)
            ff_thread_await_progress(&s->frames[LAST_FRAME].tf, row >> 3, 0);

        for (y = 0; y < h4; y++)
            for (x = 0; x < w4; x++)
                pred = FFMIN(pred,
                             refsegmap[(y + row) * 8 * s->sb_cols + x + col]);
        b->seg_id = pred;

        memset(&s->above_segpred_ctx[col], 1, w4);
        memset(&s->left_segpred_ctx[row7], 1, h4);
    } else {
        b->seg_id = vp8_rac_get_tree(&s->c, ff_vp9_segmentation_tree,
                                     s->prob.seg);

        memset(&s->above_segpred_ctx[col], 0, w4);
        memset(&s->left_segpred_ctx[row7], 0, h4);
    }
    if ((s->segmentation.enabled && s->segmentation.update_map) || s->keyframe) {
        uint8_t *segmap = s->frames[CUR_FRAME].segmentation_map;

        for (y = 0; y < h4; y++)
            memset(&segmap[(y + row) * 8 * s->sb_cols + col],
                   b->seg_id, w4);
    }

    b->skip = s->segmentation.enabled &&
              s->segmentation.feat[b->seg_id].skip_enabled;
    if (!b->skip) {
        int c = s->left_skip_ctx[row7] + s->above_skip_ctx[col];
        b->skip = vp56_rac_get_prob(&s->c, s->prob.p.skip[c]);
        s->counts.skip[c][b->skip]++;
    }

    if (s->keyframe || s->intraonly) {
        b->intra = 1;
    } else if (s->segmentation.feat[b->seg_id].ref_enabled) {
        b->intra = !s->segmentation.feat[b->seg_id].ref_val;
    } else {
        int c, bit;

        if (have_a && have_l) {
            c  = s->above_intra_ctx[col] + s->left_intra_ctx[row7];
            c += (c == 2);
        } else {
            c = have_a ? 2 * s->above_intra_ctx[col] :
                have_l ? 2 * s->left_intra_ctx[row7] : 0;
        }
        bit = vp56_rac_get_prob(&s->c, s->prob.p.intra[c]);
        s->counts.intra[c][bit]++;
        b->intra = !bit;
    }

    if ((b->intra || !b->skip) && s->txfmmode == TX_SWITCHABLE) {
        int c;
        if (have_a) {
            if (have_l) {
                c = (s->above_skip_ctx[col] ? max_tx :
                     s->above_txfm_ctx[col]) +
                    (s->left_skip_ctx[row7] ? max_tx :
                     s->left_txfm_ctx[row7]) > max_tx;
            } else {
                c = s->above_skip_ctx[col] ? 1 :
                    (s->above_txfm_ctx[col] * 2 > max_tx);
            }
        } else if (have_l) {
            c = s->left_skip_ctx[row7] ? 1 :
                (s->left_txfm_ctx[row7] * 2 > max_tx);
        } else {
            c = 1;
        }
        switch (max_tx) {
        case TX_32X32:
            b->tx = vp56_rac_get_prob(&s->c, s->prob.p.tx32p[c][0]);
            if (b->tx) {
                b->tx += vp56_rac_get_prob(&s->c, s->prob.p.tx32p[c][1]);
                if (b->tx == 2)
                    b->tx += vp56_rac_get_prob(&s->c, s->prob.p.tx32p[c][2]);
            }
            s->counts.tx32p[c][b->tx]++;
            break;
        case TX_16X16:
            b->tx = vp56_rac_get_prob(&s->c, s->prob.p.tx16p[c][0]);
            if (b->tx)
                b->tx += vp56_rac_get_prob(&s->c, s->prob.p.tx16p[c][1]);
            s->counts.tx16p[c][b->tx]++;
            break;
        case TX_8X8:
            b->tx = vp56_rac_get_prob(&s->c, s->prob.p.tx8p[c]);
            s->counts.tx8p[c][b->tx]++;
            break;
        case TX_4X4:
            b->tx = TX_4X4;
            break;
        }
    } else {
        b->tx = FFMIN(max_tx, s->txfmmode);
    }

    if (s->keyframe || s->intraonly) {
        uint8_t *a = &s->above_mode_ctx[col * 2];
        uint8_t *l = &s->left_mode_ctx[(row7) << 1];

        b->comp = 0;
        if (b->bs > BS_8x8) {
            // FIXME the memory storage intermediates here aren't really
            // necessary, they're just there to make the code slightly
            // simpler for now
            b->mode[0] =
            a[0]       = vp8_rac_get_tree(&s->c, ff_vp9_intramode_tree,
                                          ff_vp9_default_kf_ymode_probs[a[0]][l[0]]);
            if (b->bs != BS_8x4) {
                b->mode[1] = vp8_rac_get_tree(&s->c, ff_vp9_intramode_tree,
                                              ff_vp9_default_kf_ymode_probs[a[1]][b->mode[0]]);
                l[0]       =
                a[1]       = b->mode[1];
            } else {
                l[0]       =
                a[1]       =
                b->mode[1] = b->mode[0];
            }
            if (b->bs != BS_4x8) {
                b->mode[2] =
                a[0]       = vp8_rac_get_tree(&s->c, ff_vp9_intramode_tree,
                                              ff_vp9_default_kf_ymode_probs[a[0]][l[1]]);
                if (b->bs != BS_8x4) {
                    b->mode[3] = vp8_rac_get_tree(&s->c, ff_vp9_intramode_tree,
                                                  ff_vp9_default_kf_ymode_probs[a[1]][b->mode[2]]);
                    l[1]       =
                    a[1]       = b->mode[3];
                } else {
                    l[1]       =
                    a[1]       =
                    b->mode[3] = b->mode[2];
                }
            } else {
                b->mode[2] = b->mode[0];
                l[1]       =
                a[1]       =
                b->mode[3] = b->mode[1];
            }
        } else {
            b->mode[0] = vp8_rac_get_tree(&s->c, ff_vp9_intramode_tree,
                                          ff_vp9_default_kf_ymode_probs[*a][*l]);
            b->mode[3] =
            b->mode[2] =
            b->mode[1] = b->mode[0];
            // FIXME this can probably be optimized
            memset(a, b->mode[0], bwh_tab[0][b->bs][0]);
            memset(l, b->mode[0], bwh_tab[0][b->bs][1]);
        }
        b->uvmode = vp8_rac_get_tree(&s->c, ff_vp9_intramode_tree,
                                     ff_vp9_default_kf_uvmode_probs[b->mode[3]]);
    } else if (b->intra) {
        b->comp = 0;
        if (b->bs > BS_8x8) {
            b->mode[0] = vp8_rac_get_tree(&s->c, ff_vp9_intramode_tree,
                                          s->prob.p.y_mode[0]);
            s->counts.y_mode[0][b->mode[0]]++;
            if (b->bs != BS_8x4) {
                b->mode[1] = vp8_rac_get_tree(&s->c, ff_vp9_intramode_tree,
                                              s->prob.p.y_mode[0]);
                s->counts.y_mode[0][b->mode[1]]++;
            } else {
                b->mode[1] = b->mode[0];
            }
            if (b->bs != BS_4x8) {
                b->mode[2] = vp8_rac_get_tree(&s->c, ff_vp9_intramode_tree,
                                              s->prob.p.y_mode[0]);
                s->counts.y_mode[0][b->mode[2]]++;
                if (b->bs != BS_8x4) {
                    b->mode[3] = vp8_rac_get_tree(&s->c, ff_vp9_intramode_tree,
                                                  s->prob.p.y_mode[0]);
                    s->counts.y_mode[0][b->mode[3]]++;
                } else {
                    b->mode[3] = b->mode[2];
                }
            } else {
                b->mode[2] = b->mode[0];
                b->mode[3] = b->mode[1];
            }
        } else {
            static const uint8_t size_group[10] = {
                3, 3, 3, 3, 2, 2, 2, 1, 1, 1
            };
            int sz = size_group[b->bs];

            b->mode[0] = vp8_rac_get_tree(&s->c, ff_vp9_intramode_tree,
                                          s->prob.p.y_mode[sz]);
            b->mode[1] =
            b->mode[2] =
            b->mode[3] = b->mode[0];
            s->counts.y_mode[sz][b->mode[3]]++;
        }
        b->uvmode = vp8_rac_get_tree(&s->c, ff_vp9_intramode_tree,
                                     s->prob.p.uv_mode[b->mode[3]]);
        s->counts.uv_mode[b->mode[3]][b->uvmode]++;
    } else {
        static const uint8_t inter_mode_ctx_lut[14][14] = {
            { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5 },
            { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5 },
            { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5 },
            { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5 },
            { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5 },
            { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5 },
            { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5 },
            { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5 },
            { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5 },
            { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5 },
            { 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 2, 2, 1, 3 },
            { 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 2, 2, 1, 3 },
            { 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 1, 1, 0, 3 },
            { 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 3, 3, 3, 4 },
        };

        if (s->segmentation.feat[b->seg_id].ref_enabled) {
            av_assert2(s->segmentation.feat[b->seg_id].ref_val != 0);
            b->comp   = 0;
            b->ref[0] = s->segmentation.feat[b->seg_id].ref_val - 1;
        } else {
            // read comp_pred flag
            if (s->comppredmode != PRED_SWITCHABLE) {
                b->comp = s->comppredmode == PRED_COMPREF;
            } else {
                int c;

                // FIXME add intra as ref=0xff (or -1) to make these easier?
                if (have_a) {
                    if (have_l) {
                        if (s->above_comp_ctx[col] && s->left_comp_ctx[row7]) {
                            c = 4;
                        } else if (s->above_comp_ctx[col]) {
                            c = 2 + (s->left_intra_ctx[row7] ||
                                     s->left_ref_ctx[row7] == s->fixcompref);
                        } else if (s->left_comp_ctx[row7]) {
                            c = 2 + (s->above_intra_ctx[col] ||
                                     s->above_ref_ctx[col] == s->fixcompref);
                        } else {
                            c = (!s->above_intra_ctx[col] &&
                                 s->above_ref_ctx[col] == s->fixcompref) ^
                                (!s->left_intra_ctx[row7] &&
                                 s->left_ref_ctx[row & 7] == s->fixcompref);
                        }
                    } else {
                        c = s->above_comp_ctx[col] ? 3 :
                            (!s->above_intra_ctx[col] && s->above_ref_ctx[col] == s->fixcompref);
                    }
                } else if (have_l) {
                    c = s->left_comp_ctx[row7] ? 3 :
                        (!s->left_intra_ctx[row7] && s->left_ref_ctx[row7] == s->fixcompref);
                } else {
                    c = 1;
                }
                b->comp = vp56_rac_get_prob(&s->c, s->prob.p.comp[c]);
                s->counts.comp[c][b->comp]++;
            }

            // read actual references
            // FIXME probably cache a few variables here to prevent repetitive
            // memory accesses below
            if (b->comp) { /* two references */
                int fix_idx = s->signbias[s->fixcompref], var_idx = !fix_idx, c, bit;

                b->ref[fix_idx] = s->fixcompref;
                // FIXME can this codeblob be replaced by some sort of LUT?
                if (have_a) {
                    if (have_l) {
                        if (s->above_intra_ctx[col]) {
                            if (s->left_intra_ctx[row7]) {
                                c = 2;
                            } else {
                                c = 1 + 2 * (s->left_ref_ctx[row7] != s->varcompref[1]);
                            }
                        } else if (s->left_intra_ctx[row7]) {
                            c = 1 + 2 * (s->above_ref_ctx[col] != s->varcompref[1]);
                        } else {
                            int refl = s->left_ref_ctx[row7], refa = s->above_ref_ctx[col];

                            if (refl == refa && refa == s->varcompref[1]) {
                                c = 0;
                            } else if (!s->left_comp_ctx[row7] && !s->above_comp_ctx[col]) {
                                if ((refa == s->fixcompref && refl == s->varcompref[0]) ||
                                    (refl == s->fixcompref && refa == s->varcompref[0])) {
                                    c = 4;
                                } else {
                                    c = (refa == refl) ? 3 : 1;
                                }
                            } else if (!s->left_comp_ctx[row7]) {
                                if (refa == s->varcompref[1] && refl != s->varcompref[1]) {
                                    c = 1;
                                } else {
                                    c = (refl == s->varcompref[1] &&
                                         refa != s->varcompref[1]) ? 2 : 4;
                                }
                            } else if (!s->above_comp_ctx[col]) {
                                if (refl == s->varcompref[1] && refa != s->varcompref[1]) {
                                    c = 1;
                                } else {
                                    c = (refa == s->varcompref[1] &&
                                         refl != s->varcompref[1]) ? 2 : 4;
                                }
                            } else {
                                c = (refl == refa) ? 4 : 2;
                            }
                        }
                    } else {
                        if (s->above_intra_ctx[col]) {
                            c = 2;
                        } else if (s->above_comp_ctx[col]) {
                            c = 4 * (s->above_ref_ctx[col] != s->varcompref[1]);
                        } else {
                            c = 3 * (s->above_ref_ctx[col] != s->varcompref[1]);
                        }
                    }
                } else if (have_l) {
                    if (s->left_intra_ctx[row7]) {
                        c = 2;
                    } else if (s->left_comp_ctx[row7]) {
                        c = 4 * (s->left_ref_ctx[row7] != s->varcompref[1]);
                    } else {
                        c = 3 * (s->left_ref_ctx[row7] != s->varcompref[1]);
                    }
                } else {
                    c = 2;
                }
                bit = vp56_rac_get_prob(&s->c, s->prob.p.comp_ref[c]);
                b->ref[var_idx] = s->varcompref[bit];
                s->counts.comp_ref[c][bit]++;
            } else { /* single reference */
                int bit, c;

                if (have_a && !s->above_intra_ctx[col]) {
                    if (have_l && !s->left_intra_ctx[row7]) {
                        if (s->left_comp_ctx[row7]) {
                            if (s->above_comp_ctx[col]) {
                                c = 1 + (!s->fixcompref || !s->left_ref_ctx[row7] ||
                                         !s->above_ref_ctx[col]);
                            } else {
                                c = (3 * !s->above_ref_ctx[col]) +
                                    (!s->fixcompref || !s->left_ref_ctx[row7]);
                            }
                        } else if (s->above_comp_ctx[col]) {
                            c = (3 * !s->left_ref_ctx[row7]) +
                                (!s->fixcompref || !s->above_ref_ctx[col]);
                        } else {
                            c = 2 * !s->left_ref_ctx[row7] + 2 * !s->above_ref_ctx[col];
                        }
                    } else if (s->above_intra_ctx[col]) {
                        c = 2;
                    } else if (s->above_comp_ctx[col]) {
                        c = 1 + (!s->fixcompref || !s->above_ref_ctx[col]);
                    } else {
                        c = 4 * (!s->above_ref_ctx[col]);
                    }
                } else if (have_l && !s->left_intra_ctx[row7]) {
                    if (s->left_intra_ctx[row7]) {
                        c = 2;
                    } else if (s->left_comp_ctx[row7]) {
                        c = 1 + (!s->fixcompref || !s->left_ref_ctx[row7]);
                    } else {
                        c = 4 * (!s->left_ref_ctx[row7]);
                    }
                } else {
                    c = 2;
                }
                bit = vp56_rac_get_prob(&s->c, s->prob.p.single_ref[c][0]);
                s->counts.single_ref[c][0][bit]++;
                if (!bit) {
                    b->ref[0] = 0;
                } else {
                    // FIXME can this codeblob be replaced by some sort of LUT?
                    if (have_a) {
                        if (have_l) {
                            if (s->left_intra_ctx[row7]) {
                                if (s->above_intra_ctx[col]) {
                                    c = 2;
                                } else if (s->above_comp_ctx[col]) {
                                    c = 1 + 2 * (s->fixcompref == 1 ||
                                                 s->above_ref_ctx[col] == 1);
                                } else if (!s->above_ref_ctx[col]) {
                                    c = 3;
                                } else {
                                    c = 4 * (s->above_ref_ctx[col] == 1);
                                }
                            } else if (s->above_intra_ctx[col]) {
                                if (s->left_intra_ctx[row7]) {
                                    c = 2;
                                } else if (s->left_comp_ctx[row7]) {
                                    c = 1 + 2 * (s->fixcompref == 1 ||
                                                 s->left_ref_ctx[row7] == 1);
                                } else if (!s->left_ref_ctx[row7]) {
                                    c = 3;
                                } else {
                                    c = 4 * (s->left_ref_ctx[row7] == 1);
                                }
                            } else if (s->above_comp_ctx[col]) {
                                if (s->left_comp_ctx[row7]) {
                                    if (s->left_ref_ctx[row7] == s->above_ref_ctx[col]) {
                                        c = 3 * (s->fixcompref == 1 ||
                                                 s->left_ref_ctx[row7] == 1);
                                    } else {
                                        c = 2;
                                    }
                                } else if (!s->left_ref_ctx[row7]) {
                                    c = 1 + 2 * (s->fixcompref == 1 ||
                                                 s->above_ref_ctx[col] == 1);
                                } else {
                                    c = 3 * (s->left_ref_ctx[row7] == 1) +
                                        (s->fixcompref == 1 || s->above_ref_ctx[col] == 1);
                                }
                            } else if (s->left_comp_ctx[row7]) {
                                if (!s->above_ref_ctx[col]) {
                                    c = 1 + 2 * (s->fixcompref == 1 ||
                                                 s->left_ref_ctx[row7] == 1);
                                } else {
                                    c = 3 * (s->above_ref_ctx[col] == 1) +
                                        (s->fixcompref == 1 || s->left_ref_ctx[row7] == 1);
                                }
                            } else if (!s->above_ref_ctx[col]) {
                                if (!s->left_ref_ctx[row7]) {
                                    c = 3;
                                } else {
                                    c = 4 * (s->left_ref_ctx[row7] == 1);
                                }
                            } else if (!s->left_ref_ctx[row7]) {
                                c = 4 * (s->above_ref_ctx[col] == 1);
                            } else {
                                c = 2 * (s->left_ref_ctx[row7] == 1) +
                                    2 * (s->above_ref_ctx[col] == 1);
                            }
                        } else {
                            if (s->above_intra_ctx[col] ||
                                (!s->above_comp_ctx[col] && !s->above_ref_ctx[col])) {
                                c = 2;
                            } else if (s->above_comp_ctx[col]) {
                                c = 3 * (s->fixcompref == 1 || s->above_ref_ctx[col] == 1);
                            } else {
                                c = 4 * (s->above_ref_ctx[col] == 1);
                            }
                        }
                    } else if (have_l) {
                        if (s->left_intra_ctx[row7] ||
                            (!s->left_comp_ctx[row7] && !s->left_ref_ctx[row7])) {
                            c = 2;
                        } else if (s->left_comp_ctx[row7]) {
                            c = 3 * (s->fixcompref == 1 || s->left_ref_ctx[row7] == 1);
                        } else {
                            c = 4 * (s->left_ref_ctx[row7] == 1);
                        }
                    } else {
                        c = 2;
                    }
                    bit = vp56_rac_get_prob(&s->c, s->prob.p.single_ref[c][1]);
                    s->counts.single_ref[c][1][bit]++;
                    b->ref[0] = 1 + bit;
                }
            }
        }

        if (b->bs <= BS_8x8) {
            if (s->segmentation.feat[b->seg_id].skip_enabled) {
                b->mode[0] =
                b->mode[1] =
                b->mode[2] =
                b->mode[3] = ZEROMV;
            } else {
                static const uint8_t off[10] = {
                    3, 0, 0, 1, 0, 0, 0, 0, 0, 0
                };

                // FIXME this needs to use the LUT tables from find_ref_mvs
                // because not all are -1,0/0,-1
                int c = inter_mode_ctx_lut[s->above_mode_ctx[col + off[b->bs]]]
                                          [s->left_mode_ctx[row7 + off[b->bs]]];

                b->mode[0] = vp8_rac_get_tree(&s->c, ff_vp9_inter_mode_tree,
                                              s->prob.p.mv_mode[c]);
                b->mode[1] =
                b->mode[2] =
                b->mode[3] = b->mode[0];
                s->counts.mv_mode[c][b->mode[0] - 10]++;
            }
        }

        if (s->filtermode == FILTER_SWITCHABLE) {
            int c;

            if (have_a && s->above_mode_ctx[col] >= NEARESTMV) {
                if (have_l && s->left_mode_ctx[row7] >= NEARESTMV) {
                    c = s->above_filter_ctx[col] == s->left_filter_ctx[row7] ?
                        s->left_filter_ctx[row7] : 3;
                } else {
                    c = s->above_filter_ctx[col];
                }
            } else if (have_l && s->left_mode_ctx[row7] >= NEARESTMV) {
                c = s->left_filter_ctx[row7];
            } else {
                c = 3;
            }

            b->filter = vp8_rac_get_tree(&s->c, ff_vp9_filter_tree,
                                         s->prob.p.filter[c]);
            s->counts.filter[c][b->filter]++;
        } else {
            b->filter = s->filtermode;
        }

        if (b->bs > BS_8x8) {
            int c = inter_mode_ctx_lut[s->above_mode_ctx[col]][s->left_mode_ctx[row7]];

            b->mode[0] = vp8_rac_get_tree(&s->c, ff_vp9_inter_mode_tree,
                                          s->prob.p.mv_mode[c]);
            s->counts.mv_mode[c][b->mode[0] - 10]++;
            ff_vp9_fill_mv(s, b->mv[0], b->mode[0], 0);

            if (b->bs != BS_8x4) {
                b->mode[1] = vp8_rac_get_tree(&s->c, ff_vp9_inter_mode_tree,
                                              s->prob.p.mv_mode[c]);
                s->counts.mv_mode[c][b->mode[1] - 10]++;
                ff_vp9_fill_mv(s, b->mv[1], b->mode[1], 1);
            } else {
                b->mode[1] = b->mode[0];
                AV_COPY32(&b->mv[1][0], &b->mv[0][0]);
                AV_COPY32(&b->mv[1][1], &b->mv[0][1]);
            }

            if (b->bs != BS_4x8) {
                b->mode[2] = vp8_rac_get_tree(&s->c, ff_vp9_inter_mode_tree,
                                              s->prob.p.mv_mode[c]);
                s->counts.mv_mode[c][b->mode[2] - 10]++;
                ff_vp9_fill_mv(s, b->mv[2], b->mode[2], 2);

                if (b->bs != BS_8x4) {
                    b->mode[3] = vp8_rac_get_tree(&s->c, ff_vp9_inter_mode_tree,
                                                  s->prob.p.mv_mode[c]);
                    s->counts.mv_mode[c][b->mode[3] - 10]++;
                    ff_vp9_fill_mv(s, b->mv[3], b->mode[3], 3);
                } else {
                    b->mode[3] = b->mode[2];
                    AV_COPY32(&b->mv[3][0], &b->mv[2][0]);
                    AV_COPY32(&b->mv[3][1], &b->mv[2][1]);
                }
            } else {
                b->mode[2] = b->mode[0];
                AV_COPY32(&b->mv[2][0], &b->mv[0][0]);
                AV_COPY32(&b->mv[2][1], &b->mv[0][1]);
                b->mode[3] = b->mode[1];
                AV_COPY32(&b->mv[3][0], &b->mv[1][0]);
                AV_COPY32(&b->mv[3][1], &b->mv[1][1]);
            }
        } else {
            ff_vp9_fill_mv(s, b->mv[0], b->mode[0], -1);
            AV_COPY32(&b->mv[1][0], &b->mv[0][0]);
            AV_COPY32(&b->mv[2][0], &b->mv[0][0]);
            AV_COPY32(&b->mv[3][0], &b->mv[0][0]);
            AV_COPY32(&b->mv[1][1], &b->mv[0][1]);
            AV_COPY32(&b->mv[2][1], &b->mv[0][1]);
            AV_COPY32(&b->mv[3][1], &b->mv[0][1]);
        }
    }

    // FIXME this can probably be optimized
    memset(&s->above_skip_ctx[col], b->skip, w4);
    memset(&s->left_skip_ctx[row7], b->skip, h4);
    memset(&s->above_txfm_ctx[col], b->tx, w4);
    memset(&s->left_txfm_ctx[row7], b->tx, h4);
    memset(&s->above_partition_ctx[col], above_ctx[b->bs], w4);
    memset(&s->left_partition_ctx[row7], left_ctx[b->bs], h4);
    if (!s->keyframe && !s->intraonly) {
        memset(&s->above_intra_ctx[col], b->intra, w4);
        memset(&s->left_intra_ctx[row7], b->intra, h4);
        memset(&s->above_comp_ctx[col], b->comp, w4);
        memset(&s->left_comp_ctx[row7], b->comp, h4);
        memset(&s->above_mode_ctx[col], b->mode[3], w4);
        memset(&s->left_mode_ctx[row7], b->mode[3], h4);
        if (s->filtermode == FILTER_SWITCHABLE && !b->intra) {
            memset(&s->above_filter_ctx[col], b->filter, w4);
            memset(&s->left_filter_ctx[row7], b->filter, h4);
            b->filter = ff_vp9_filter_lut[b->filter];
        }
        if (b->bs > BS_8x8) {
            int mv0 = AV_RN32A(&b->mv[3][0]), mv1 = AV_RN32A(&b->mv[3][1]);

            AV_COPY32(&s->left_mv_ctx[row7 * 2 + 0][0], &b->mv[1][0]);
            AV_COPY32(&s->left_mv_ctx[row7 * 2 + 0][1], &b->mv[1][1]);
            AV_WN32A(&s->left_mv_ctx[row7 * 2 + 1][0], mv0);
            AV_WN32A(&s->left_mv_ctx[row7 * 2 + 1][1], mv1);
            AV_COPY32(&s->above_mv_ctx[col * 2 + 0][0], &b->mv[2][0]);
            AV_COPY32(&s->above_mv_ctx[col * 2 + 0][1], &b->mv[2][1]);
            AV_WN32A(&s->above_mv_ctx[col * 2 + 1][0], mv0);
            AV_WN32A(&s->above_mv_ctx[col * 2 + 1][1], mv1);
        } else {
            int n, mv0 = AV_RN32A(&b->mv[3][0]), mv1 = AV_RN32A(&b->mv[3][1]);

            for (n = 0; n < w4 * 2; n++) {
                AV_WN32A(&s->above_mv_ctx[col * 2 + n][0], mv0);
                AV_WN32A(&s->above_mv_ctx[col * 2 + n][1], mv1);
            }
            for (n = 0; n < h4 * 2; n++) {
                AV_WN32A(&s->left_mv_ctx[row7 * 2 + n][0], mv0);
                AV_WN32A(&s->left_mv_ctx[row7 * 2 + n][1], mv1);
            }
        }

        if (!b->intra) { // FIXME write 0xff or -1 if intra, so we can use this
                         // as a direct check in above branches
            int vref = b->ref[b->comp ? s->signbias[s->varcompref[0]] : 0];

            memset(&s->above_ref_ctx[col], vref, w4);
            memset(&s->left_ref_ctx[row7], vref, h4);
        }
    }

    // FIXME kinda ugly
    for (y = 0; y < h4; y++) {
        int x, o = (row + y) * s->sb_cols * 8 + col;
        VP9MVRefPair *mv = &s->frames[CUR_FRAME].mv[o];

        if (b->intra) {
            for (x = 0; x < w4; x++) {
                mv[x].ref[0] =
                mv[x].ref[1] = -1;
            }
        } else if (b->comp) {
            for (x = 0; x < w4; x++) {
                mv[x].ref[0] = b->ref[0];
                mv[x].ref[1] = b->ref[1];
                AV_COPY32(&mv[x].mv[0], &b->mv[3][0]);
                AV_COPY32(&mv[x].mv[1], &b->mv[3][1]);
            }
        } else {
            for (x = 0; x < w4; x++) {
                mv[x].ref[0] = b->ref[0];
                mv[x].ref[1] = -1;
                AV_COPY32(&mv[x].mv[0], &b->mv[3][0]);
            }
        }
    }
}

// FIXME remove tx argument, and merge cnt/eob arguments?
static int decode_block_coeffs(VP56RangeCoder *c, int16_t *coef, int n_coeffs,
                               enum TxfmMode tx, unsigned (*cnt)[6][3],
                               unsigned (*eob)[6][2], uint8_t(*p)[6][11],
                               int nnz, const int16_t *scan,
                               const int16_t(*nb)[2],
                               const int16_t *band_counts, const int16_t *qmul)
{
    int i = 0, band = 0, band_left = band_counts[band];
    uint8_t *tp = p[0][nnz];
    uint8_t cache[1024];

    do {
        int val, rc;

        val = vp56_rac_get_prob_branchy(c, tp[0]); // eob
        eob[band][nnz][val]++;
        if (!val)
            break;

skip_eob:
        if (!vp56_rac_get_prob_branchy(c, tp[1])) { // zero
            cnt[band][nnz][0]++;
            if (!--band_left)
                band_left = band_counts[++band];
            cache[scan[i]] = 0;
            nnz            = (1 + cache[nb[i][0]] + cache[nb[i][1]]) >> 1;
            tp             = p[band][nnz];
            if (++i == n_coeffs)
                break;  //invalid input; blocks should end with EOB
            goto skip_eob;
        }

        rc = scan[i];
        if (!vp56_rac_get_prob_branchy(c, tp[2])) { // one
            cnt[band][nnz][1]++;
            val       = 1;
            cache[rc] = 1;
        } else {
            // fill in p[3-10] (model fill) - only once per frame for each pos
            if (!tp[3])
                memcpy(&tp[3], ff_vp9_model_pareto8[tp[2]], 8);

            cnt[band][nnz][2]++;
            if (!vp56_rac_get_prob_branchy(c, tp[3])) { // 2, 3, 4
                if (!vp56_rac_get_prob_branchy(c, tp[4])) {
                    cache[rc] = val = 2;
                } else {
                    val       = 3 + vp56_rac_get_prob(c, tp[5]);
                    cache[rc] = 3;
                }
            } else if (!vp56_rac_get_prob_branchy(c, tp[6])) { // cat1/2
                cache[rc] = 4;
                if (!vp56_rac_get_prob_branchy(c, tp[7])) {
                    val  =  vp56_rac_get_prob(c, 159) + 5;
                } else {
                    val  = (vp56_rac_get_prob(c, 165) << 1) + 7;
                    val +=  vp56_rac_get_prob(c, 145);
                }
            } else { // cat 3-6
                cache[rc] = 5;
                if (!vp56_rac_get_prob_branchy(c, tp[8])) {
                    if (!vp56_rac_get_prob_branchy(c, tp[9])) {
                        val  = (vp56_rac_get_prob(c, 173) << 2) + 11;
                        val += (vp56_rac_get_prob(c, 148) << 1);
                        val +=  vp56_rac_get_prob(c, 140);
                    } else {
                        val  = (vp56_rac_get_prob(c, 176) << 3) + 19;
                        val += (vp56_rac_get_prob(c, 155) << 2);
                        val += (vp56_rac_get_prob(c, 140) << 1);
                        val +=  vp56_rac_get_prob(c, 135);
                    }
                } else if (!vp56_rac_get_prob_branchy(c, tp[10])) {
                    val  = (vp56_rac_get_prob(c, 180) << 4) + 35;
                    val += (vp56_rac_get_prob(c, 157) << 3);
                    val += (vp56_rac_get_prob(c, 141) << 2);
                    val += (vp56_rac_get_prob(c, 134) << 1);
                    val +=  vp56_rac_get_prob(c, 130);
                } else {
                    val  = (vp56_rac_get_prob(c, 254) << 13) + 67;
                    val += (vp56_rac_get_prob(c, 254) << 12);
                    val += (vp56_rac_get_prob(c, 254) << 11);
                    val += (vp56_rac_get_prob(c, 252) << 10);
                    val += (vp56_rac_get_prob(c, 249) << 9);
                    val += (vp56_rac_get_prob(c, 243) << 8);
                    val += (vp56_rac_get_prob(c, 230) << 7);
                    val += (vp56_rac_get_prob(c, 196) << 6);
                    val += (vp56_rac_get_prob(c, 177) << 5);
                    val += (vp56_rac_get_prob(c, 153) << 4);
                    val += (vp56_rac_get_prob(c, 140) << 3);
                    val += (vp56_rac_get_prob(c, 133) << 2);
                    val += (vp56_rac_get_prob(c, 130) << 1);
                    val +=  vp56_rac_get_prob(c, 129);
                }
            }
        }
        if (!--band_left)
            band_left = band_counts[++band];
        if (tx == TX_32X32) // FIXME slow
            coef[rc] = ((vp8_rac_get(c) ? -val : val) * qmul[!!i]) / 2;
        else
            coef[rc] = (vp8_rac_get(c) ? -val : val) * qmul[!!i];
        nnz = (1 + cache[nb[i][0]] + cache[nb[i][1]]) >> 1;
        tp  = p[band][nnz];
    } while (++i < n_coeffs);

    return i;
}

static int decode_coeffs(AVCodecContext *avctx)
{
    VP9Context *s = avctx->priv_data;
    VP9Block *b = s->b;
    int row = b->row, col = b->col;
    uint8_t (*p)[6][11] = s->prob.coef[b->tx][0 /* y */][!b->intra];
    unsigned (*c)[6][3] = s->counts.coef[b->tx][0 /* y */][!b->intra];
    unsigned (*e)[6][2] = s->counts.eob[b->tx][0 /* y */][!b->intra];
    int w4 = bwh_tab[1][b->bs][0] << 1, h4 = bwh_tab[1][b->bs][1] << 1;
    int end_x = FFMIN(2 * (s->cols - col), w4);
    int end_y = FFMIN(2 * (s->rows - row), h4);
    int n, pl, x, y, step1d = 1 << b->tx, step = 1 << (b->tx * 2);
    int uvstep1d = 1 << b->uvtx, uvstep = 1 << (b->uvtx * 2), ret;
    int16_t (*qmul)[2] = s->segmentation.feat[b->seg_id].qmul;
    int tx = 4 * s->lossless + b->tx;
    const int16_t **yscans = ff_vp9_scans[tx];
    const int16_t (**ynbs)[2] = ff_vp9_scans_nb[tx];
    const int16_t *uvscan = ff_vp9_scans[b->uvtx][DCT_DCT];
    const int16_t (*uvnb)[2] = ff_vp9_scans_nb[b->uvtx][DCT_DCT];
    uint8_t *a = &s->above_y_nnz_ctx[col * 2];
    uint8_t *l = &s->left_y_nnz_ctx[(row & 7) << 1];
    static const int16_t band_counts[4][8] = {
        { 1, 2, 3, 4,  3,   16 - 13, 0 },
        { 1, 2, 3, 4, 11,   64 - 21, 0 },
        { 1, 2, 3, 4, 11,  256 - 21, 0 },
        { 1, 2, 3, 4, 11, 1024 - 21, 0 },
    };
    const int16_t *y_band_counts  = band_counts[b->tx];
    const int16_t *uv_band_counts = band_counts[b->uvtx];

    /* y tokens */
    if (b->tx > TX_4X4) { // FIXME slow
        for (y = 0; y < end_y; y += step1d)
            for (x = 1; x < step1d; x++)
                l[y] |= l[y + x];
        for (x = 0; x < end_x; x += step1d)
            for (y = 1; y < step1d; y++)
                a[x] |= a[x + y];
    }
    for (n = 0, y = 0; y < end_y; y += step1d) {
        for (x = 0; x < end_x; x += step1d, n += step) {
            enum TxfmType txtp = ff_vp9_intra_txfm_type[b->mode[b->tx == TX_4X4 &&
                                                                b->bs > BS_8x8 ?
                                                                n : 0]];
            int nnz = a[x] + l[y];
            if ((ret = decode_block_coeffs(&s->c, s->block + 16 * n, 16 * step,
                                           b->tx, c, e, p, nnz, yscans[txtp],
                                           ynbs[txtp], y_band_counts,
                                           qmul[0])) < 0)
                return ret;
            a[x] = l[y] = !!ret;
            if (b->tx > TX_8X8)
                AV_WN16A(&s->eob[n], ret);
            else
                s->eob[n] = ret;
        }
    }
    if (b->tx > TX_4X4) { // FIXME slow
        for (y = 0; y < end_y; y += step1d)
            memset(&l[y + 1], l[y], FFMIN(end_y - y - 1, step1d - 1));
        for (x = 0; x < end_x; x += step1d)
            memset(&a[x + 1], a[x], FFMIN(end_x - x - 1, step1d - 1));
    }

    p = s->prob.coef[b->uvtx][1 /* uv */][!b->intra];
    c = s->counts.coef[b->uvtx][1 /* uv */][!b->intra];
    e = s->counts.eob[b->uvtx][1 /* uv */][!b->intra];
    w4    >>= 1;
    h4    >>= 1;
    end_x >>= 1;
    end_y >>= 1;
    for (pl = 0; pl < 2; pl++) {
        a = &s->above_uv_nnz_ctx[pl][col];
        l = &s->left_uv_nnz_ctx[pl][row & 7];
        if (b->uvtx > TX_4X4) { // FIXME slow
            for (y = 0; y < end_y; y += uvstep1d)
                for (x = 1; x < uvstep1d; x++)
                    l[y] |= l[y + x];
            for (x = 0; x < end_x; x += uvstep1d)
                for (y = 1; y < uvstep1d; y++)
                    a[x] |= a[x + y];
        }
        for (n = 0, y = 0; y < end_y; y += uvstep1d) {
            for (x = 0; x < end_x; x += uvstep1d, n += uvstep) {
                int nnz = a[x] + l[y];
                if ((ret = decode_block_coeffs(&s->c, s->uvblock[pl] + 16 * n,
                                               16 * uvstep, b->uvtx, c, e, p,
                                               nnz, uvscan, uvnb,
                                               uv_band_counts, qmul[1])) < 0)
                    return ret;
                a[x] = l[y] = !!ret;
                if (b->uvtx > TX_8X8)
                    AV_WN16A(&s->uveob[pl][n], ret);
                else
                    s->uveob[pl][n] = ret;
            }
        }
        if (b->uvtx > TX_4X4) { // FIXME slow
            for (y = 0; y < end_y; y += uvstep1d)
                memset(&l[y + 1], l[y], FFMIN(end_y - y - 1, uvstep1d - 1));
            for (x = 0; x < end_x; x += uvstep1d)
                memset(&a[x + 1], a[x], FFMIN(end_x - x - 1, uvstep1d - 1));
        }
    }

    return 0;
}

static av_always_inline int check_intra_mode(VP9Context *s, int mode,
                                             uint8_t **a,
                                             uint8_t *dst_edge,
                                             ptrdiff_t stride_edge,
                                             uint8_t *dst_inner,
                                             ptrdiff_t stride_inner,
                                             uint8_t *l, int col, int x, int w,
                                             int row, int y, enum TxfmMode tx,
                                             int p)
{
    int have_top   = row > 0 || y > 0;
    int have_left  = col > s->tiling.tile_col_start || x > 0;
    int have_right = x < w - 1;
    static const uint8_t mode_conv[10][2 /* have_left */][2 /* have_top */] = {
        [VERT_PRED]            = { { DC_127_PRED,          VERT_PRED            },
                                   { DC_127_PRED,          VERT_PRED            } },
        [HOR_PRED]             = { { DC_129_PRED,          DC_129_PRED          },
                                   { HOR_PRED,             HOR_PRED             } },
        [DC_PRED]              = { { DC_128_PRED,          TOP_DC_PRED          },
                                   { LEFT_DC_PRED,         DC_PRED              } },
        [DIAG_DOWN_LEFT_PRED]  = { { DC_127_PRED,          DIAG_DOWN_LEFT_PRED  },
                                   { DC_127_PRED,          DIAG_DOWN_LEFT_PRED  } },
        [DIAG_DOWN_RIGHT_PRED] = { { DIAG_DOWN_RIGHT_PRED, DIAG_DOWN_RIGHT_PRED },
                                   { DIAG_DOWN_RIGHT_PRED, DIAG_DOWN_RIGHT_PRED } },
        [VERT_RIGHT_PRED]      = { { VERT_RIGHT_PRED,      VERT_RIGHT_PRED      },
                                   { VERT_RIGHT_PRED,      VERT_RIGHT_PRED      } },
        [HOR_DOWN_PRED]        = { { HOR_DOWN_PRED,        HOR_DOWN_PRED        },
                                   { HOR_DOWN_PRED,        HOR_DOWN_PRED        } },
        [VERT_LEFT_PRED]       = { { DC_127_PRED,          VERT_LEFT_PRED       },
                                   { DC_127_PRED,          VERT_LEFT_PRED       } },
        [HOR_UP_PRED]          = { { DC_129_PRED,          DC_129_PRED          },
                                   { HOR_UP_PRED,          HOR_UP_PRED          } },
        [TM_VP8_PRED]          = { { DC_129_PRED,          VERT_PRED            },
                                   { HOR_PRED,             TM_VP8_PRED          } },
    };
    static const struct {
        uint8_t needs_left:1;
        uint8_t needs_top:1;
        uint8_t needs_topleft:1;
        uint8_t needs_topright:1;
    } edges[N_INTRA_PRED_MODES] = {
        [VERT_PRED]            = { .needs_top  = 1 },
        [HOR_PRED]             = { .needs_left = 1 },
        [DC_PRED]              = { .needs_top  = 1, .needs_left = 1 },
        [DIAG_DOWN_LEFT_PRED]  = { .needs_top  = 1, .needs_topright = 1 },
        [DIAG_DOWN_RIGHT_PRED] = { .needs_left = 1, .needs_top = 1,
                                   .needs_topleft = 1 },
        [VERT_RIGHT_PRED]      = { .needs_left = 1, .needs_top = 1,
                                   .needs_topleft = 1 },
        [HOR_DOWN_PRED]        = { .needs_left = 1, .needs_top = 1,
                                   .needs_topleft = 1 },
        [VERT_LEFT_PRED]       = { .needs_top  = 1, .needs_topright = 1 },
        [HOR_UP_PRED]          = { .needs_left = 1 },
        [TM_VP8_PRED]          = { .needs_left = 1, .needs_top = 1,
                                   .needs_topleft = 1 },
        [LEFT_DC_PRED]         = { .needs_left = 1 },
        [TOP_DC_PRED]          = { .needs_top  = 1 },
        [DC_128_PRED]          = { 0 },
        [DC_127_PRED]          = { 0 },
        [DC_129_PRED]          = { 0 }
    };

    av_assert2(mode >= 0 && mode < 10);
    mode = mode_conv[mode][have_left][have_top];
    if (edges[mode].needs_top) {
        uint8_t *top = NULL, *topleft = NULL;
        int n_px_need = 4 << tx, n_px_have = (((s->cols - col) << !p) - x) * 4;
        int n_px_need_tr = 0;

        if (tx == TX_4X4 && edges[mode].needs_topright && have_right)
            n_px_need_tr = 4;

        // if top of sb64-row, use s->intra_pred_data[] instead of
        // dst[-stride] for intra prediction (it contains pre- instead of
        // post-loopfilter data)
        if (have_top) {
            top = !(row & 7) && !y ?
                  s->intra_pred_data[p] + col * (8 >> !!p) + x * 4 :
                  y == 0 ? &dst_edge[-stride_edge] : &dst_inner[-stride_inner];
            if (have_left)
                topleft = !(row & 7) && !y ?
                          s->intra_pred_data[p] + col * (8 >> !!p) + x * 4 :
                          y == 0 || x == 0 ? &dst_edge[-stride_edge] :
                          &dst_inner[-stride_inner];
        }

        if (have_top &&
            (!edges[mode].needs_topleft || (have_left && top == topleft)) &&
            (tx != TX_4X4 || !edges[mode].needs_topright || have_right) &&
            n_px_need + n_px_need_tr <= n_px_have) {
            *a = top;
        } else {
            if (have_top) {
                if (n_px_need <= n_px_have) {
                    memcpy(*a, top, n_px_need);
                } else {
                    memcpy(*a, top, n_px_have);
                    memset(&(*a)[n_px_have], (*a)[n_px_have - 1],
                           n_px_need - n_px_have);
                }
            } else {
                memset(*a, 127, n_px_need);
            }
            if (edges[mode].needs_topleft) {
                if (have_left && have_top)
                    (*a)[-1] = topleft[-1];
                else
                    (*a)[-1] = have_top ? 129 : 127;
            }
            if (tx == TX_4X4 && edges[mode].needs_topright) {
                if (have_top && have_right &&
                    n_px_need + n_px_need_tr <= n_px_have) {
                    memcpy(&(*a)[4], &top[4], 4);
                } else {
                    memset(&(*a)[4], (*a)[3], 4);
                }
            }
        }
    }
    if (edges[mode].needs_left) {
        if (have_left) {
            int i;
            int n_px_need = 4 << tx;
            int n_px_have = (((s->rows - row) << !p) - y) * 4;
            uint8_t *dst     = x == 0 ? dst_edge : dst_inner;
            ptrdiff_t stride = x == 0 ? stride_edge : stride_inner;

            if (n_px_need <= n_px_have) {
                for (i = 0; i < n_px_need; i++)
                    l[i] = dst[i * stride - 1];
            } else {
                for (i = 0; i < n_px_have; i++)
                    l[i] = dst[i * stride - 1];
                memset(&l[i], l[i - 1], n_px_need - n_px_have);
            }
        } else {
            memset(l, 129, 4 << tx);
        }
    }

    return mode;
}

static void intra_recon(AVCodecContext *avctx, ptrdiff_t y_off, ptrdiff_t uv_off)
{
    VP9Context *s = avctx->priv_data;
    VP9Block *b = s->b;
    AVFrame *f = s->frames[CUR_FRAME].tf.f;
    int row = b->row, col = b->col;
    int w4 = bwh_tab[1][b->bs][0] << 1, step1d = 1 << b->tx, n;
    int h4 = bwh_tab[1][b->bs][1] << 1, x, y, step = 1 << (b->tx * 2);
    int end_x = FFMIN(2 * (s->cols - col), w4);
    int end_y = FFMIN(2 * (s->rows - row), h4);
    int tx = 4 * s->lossless + b->tx, uvtx = b->uvtx + 4 * s->lossless;
    int uvstep1d = 1 << b->uvtx, p;
    uint8_t *dst = b->dst[0], *dst_r = f->data[0] + y_off;

    for (n = 0, y = 0; y < end_y; y += step1d) {
        uint8_t *ptr = dst, *ptr_r = dst_r;
        for (x = 0; x < end_x;
             x += step1d, ptr += 4 * step1d, ptr_r += 4 * step1d, n += step) {
            int mode = b->mode[b->bs > BS_8x8 && b->tx == TX_4X4 ?
                               y * 2 + x : 0];
            LOCAL_ALIGNED_16(uint8_t, a_buf, [48]);
            uint8_t *a = &a_buf[16], l[32];
            enum TxfmType txtp = ff_vp9_intra_txfm_type[mode];
            int eob = b->tx > TX_8X8 ? AV_RN16A(&s->eob[n]) : s->eob[n];

            mode = check_intra_mode(s, mode, &a, ptr_r,
                                    f->linesize[0],
                                    ptr, b->y_stride, l,
                                    col, x, w4, row, y, b->tx, 0);
            s->dsp.intra_pred[b->tx][mode](ptr, b->y_stride, l, a);
            if (eob)
                s->dsp.itxfm_add[tx][txtp](ptr, b->y_stride,
                                           s->block + 16 * n, eob);
        }
        dst_r += 4 * f->linesize[0] * step1d;
        dst   += 4 * b->y_stride * step1d;
    }

    // U/V
    h4    >>= 1;
    w4    >>= 1;
    end_x >>= 1;
    end_y >>= 1;
    step    = 1 << (b->uvtx * 2);
    for (p = 0; p < 2; p++) {
        dst   = b->dst[1 + p];
        dst_r = f->data[1 + p] + uv_off;
        for (n = 0, y = 0; y < end_y; y += uvstep1d) {
            uint8_t *ptr = dst, *ptr_r = dst_r;
            for (x = 0; x < end_x;
                 x += uvstep1d, ptr += 4 * uvstep1d,
                 ptr_r += 4 * uvstep1d, n += step) {
                int mode = b->uvmode;
                LOCAL_ALIGNED_16(uint8_t, a_buf, [48]);
                uint8_t *a = &a_buf[16], l[32];
                int eob    = b->uvtx > TX_8X8 ? AV_RN16A(&s->uveob[p][n])
                                              : s->uveob[p][n];

                mode = check_intra_mode(s, mode, &a, ptr_r,
                                        f->linesize[1],
                                        ptr, b->uv_stride, l,
                                        col, x, w4, row, y, b->uvtx, p + 1);
                s->dsp.intra_pred[b->uvtx][mode](ptr, b->uv_stride, l, a);
                if (eob)
                    s->dsp.itxfm_add[uvtx][DCT_DCT](ptr, b->uv_stride,
                                                    s->uvblock[p] + 16 * n,
                                                    eob);
            }
            dst_r += 4 * uvstep1d * f->linesize[1];
            dst   += 4 * uvstep1d * b->uv_stride;
        }
    }
}

static av_always_inline void mc_luma_dir(VP9Context *s, vp9_mc_func(*mc)[2],
                                         uint8_t *dst, ptrdiff_t dst_stride,
                                         const uint8_t *ref,
                                         ptrdiff_t ref_stride,
                                         ThreadFrame *ref_frame,
                                         ptrdiff_t y, ptrdiff_t x,
                                         const VP56mv *mv,
                                         int bw, int bh, int w, int h)
{
    int mx = mv->x, my = mv->y;
    int th;

    y   += my >> 3;
    x   += mx >> 3;
    ref += y * ref_stride + x;
    mx  &= 7;
    my  &= 7;

    // we use +7 because the last 7 pixels of each sbrow can be changed in
    // the longest loopfilter of the next sbrow
    th = (y + bh + 4 * !!my + 7) >> 6;
    ff_thread_await_progress(ref_frame, FFMAX(th, 0), 0);

    // FIXME bilinear filter only needs 0/1 pixels, not 3/4
    if (x < !!mx * 3 || y < !!my * 3 ||
        x + !!mx * 4 > w - bw || y + !!my * 4 > h - bh) {
        s->vdsp.emulated_edge_mc(s->edge_emu_buffer,
                                 ref - !!my * 3 * ref_stride - !!mx * 3,
                                 80,
                                 ref_stride,
                                 bw + !!mx * 7, bh + !!my * 7,
                                 x - !!mx * 3, y - !!my * 3, w, h);
        ref        = s->edge_emu_buffer + !!my * 3 * 80 + !!mx * 3;
        ref_stride = 80;
    }
    mc[!!mx][!!my](dst, ref, dst_stride, ref_stride, bh, mx << 1, my << 1);
}

static av_always_inline void mc_chroma_dir(VP9Context *s, vp9_mc_func(*mc)[2],
                                           uint8_t *dst_u, uint8_t *dst_v,
                                           ptrdiff_t dst_stride,
                                           const uint8_t *ref_u,
                                           ptrdiff_t src_stride_u,
                                           const uint8_t *ref_v,
                                           ptrdiff_t src_stride_v,
                                           ThreadFrame *ref_frame,
                                           ptrdiff_t y, ptrdiff_t x,
                                           const VP56mv *mv,
                                           int bw, int bh, int w, int h)
{
    int mx = mv->x, my = mv->y;
    int th;

    y     += my >> 4;
    x     += mx >> 4;
    ref_u += y * src_stride_u + x;
    ref_v += y * src_stride_v + x;
    mx    &= 15;
    my    &= 15;

    // we use +7 because the last 7 pixels of each sbrow can be changed in
    // the longest loopfilter of the next sbrow
    th = (y + bh + 4 * !!my + 7) >> 5;
    ff_thread_await_progress(ref_frame, FFMAX(th, 0), 0);

    // FIXME bilinear filter only needs 0/1 pixels, not 3/4
    if (x < !!mx * 3 || y < !!my * 3 ||
        x + !!mx * 4 > w - bw || y + !!my * 4 > h - bh) {
        s->vdsp.emulated_edge_mc(s->edge_emu_buffer,
                                 ref_u - !!my * 3 * src_stride_u - !!mx * 3,
                                 80,
                                 src_stride_u,
                                 bw + !!mx * 7, bh + !!my * 7,
                                 x - !!mx * 3, y - !!my * 3, w, h);
        ref_u = s->edge_emu_buffer + !!my * 3 * 80 + !!mx * 3;
        mc[!!mx][!!my](dst_u, ref_u, dst_stride, 80, bh, mx, my);

        s->vdsp.emulated_edge_mc(s->edge_emu_buffer,
                                 ref_v - !!my * 3 * src_stride_v - !!mx * 3,
                                 80,
                                 src_stride_v,
                                 bw + !!mx * 7, bh + !!my * 7,
                                 x - !!mx * 3, y - !!my * 3, w, h);
        ref_v = s->edge_emu_buffer + !!my * 3 * 80 + !!mx * 3;
        mc[!!mx][!!my](dst_v, ref_v, dst_stride, 80, bh, mx, my);
    } else {
        mc[!!mx][!!my](dst_u, ref_u, dst_stride, src_stride_u, bh, mx, my);
        mc[!!mx][!!my](dst_v, ref_v, dst_stride, src_stride_v, bh, mx, my);
    }
}

static int inter_recon(AVCodecContext *avctx)
{
    static const uint8_t bwlog_tab[2][N_BS_SIZES] = {
        { 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4 },
        { 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 4, 4 },
    };
    VP9Context *s = avctx->priv_data;
    VP9Block *b = s->b;
    int row = b->row, col = b->col;

    ThreadFrame *tref1 = &s->refs[s->refidx[b->ref[0]]];
    ThreadFrame *tref2 = b->comp ? &s->refs[s->refidx[b->ref[1]]] : NULL;
    AVFrame      *ref1 = tref1->f;
    AVFrame      *ref2 = tref2 ? tref2->f : NULL;

    int w = avctx->width, h = avctx->height;
    ptrdiff_t ls_y = b->y_stride, ls_uv = b->uv_stride;

    if (!ref1->data[0] || (b->comp && !ref2->data[0]))
        return AVERROR_INVALIDDATA;

    // y inter pred
    if (b->bs > BS_8x8) {
        if (b->bs == BS_8x4) {
            mc_luma_dir(s, s->dsp.mc[3][b->filter][0], b->dst[0], ls_y,
                        ref1->data[0], ref1->linesize[0], tref1,
                        row << 3, col << 3, &b->mv[0][0], 8, 4, w, h);
            mc_luma_dir(s, s->dsp.mc[3][b->filter][0],
                        b->dst[0] + 4 * ls_y, ls_y,
                        ref1->data[0], ref1->linesize[0], tref1,
                        (row << 3) + 4, col << 3, &b->mv[2][0], 8, 4, w, h);

            if (b->comp) {
                mc_luma_dir(s, s->dsp.mc[3][b->filter][1], b->dst[0], ls_y,
                            ref2->data[0], ref2->linesize[0], tref2,
                            row << 3, col << 3, &b->mv[0][1], 8, 4, w, h);
                mc_luma_dir(s, s->dsp.mc[3][b->filter][1],
                            b->dst[0] + 4 * ls_y, ls_y,
                            ref2->data[0], ref2->linesize[0], tref2,
                            (row << 3) + 4, col << 3, &b->mv[2][1], 8, 4, w, h);
            }
        } else if (b->bs == BS_4x8) {
            mc_luma_dir(s, s->dsp.mc[4][b->filter][0], b->dst[0], ls_y,
                        ref1->data[0], ref1->linesize[0], tref1,
                        row << 3, col << 3, &b->mv[0][0], 4, 8, w, h);
            mc_luma_dir(s, s->dsp.mc[4][b->filter][0], b->dst[0] + 4, ls_y,
                        ref1->data[0], ref1->linesize[0], tref1,
                        row << 3, (col << 3) + 4, &b->mv[1][0], 4, 8, w, h);

            if (b->comp) {
                mc_luma_dir(s, s->dsp.mc[4][b->filter][1], b->dst[0], ls_y,
                            ref2->data[0], ref2->linesize[0], tref2,
                            row << 3, col << 3, &b->mv[0][1], 4, 8, w, h);
                mc_luma_dir(s, s->dsp.mc[4][b->filter][1], b->dst[0] + 4, ls_y,
                            ref2->data[0], ref2->linesize[0], tref2,
                            row << 3, (col << 3) + 4, &b->mv[1][1], 4, 8, w, h);
            }
        } else {
            av_assert2(b->bs == BS_4x4);

            // FIXME if two horizontally adjacent blocks have the same MV,
            // do a w8 instead of a w4 call
            mc_luma_dir(s, s->dsp.mc[4][b->filter][0], b->dst[0], ls_y,
                        ref1->data[0], ref1->linesize[0], tref1,
                        row << 3, col << 3, &b->mv[0][0], 4, 4, w, h);
            mc_luma_dir(s, s->dsp.mc[4][b->filter][0], b->dst[0] + 4, ls_y,
                        ref1->data[0], ref1->linesize[0], tref1,
                        row << 3, (col << 3) + 4, &b->mv[1][0], 4, 4, w, h);
            mc_luma_dir(s, s->dsp.mc[4][b->filter][0],
                        b->dst[0] + 4 * ls_y, ls_y,
                        ref1->data[0], ref1->linesize[0], tref1,
                        (row << 3) + 4, col << 3, &b->mv[2][0], 4, 4, w, h);
            mc_luma_dir(s, s->dsp.mc[4][b->filter][0],
                        b->dst[0] + 4 * ls_y + 4, ls_y,
                        ref1->data[0], ref1->linesize[0], tref1,
                        (row << 3) + 4, (col << 3) + 4, &b->mv[3][0], 4, 4, w, h);

            if (b->comp) {
                mc_luma_dir(s, s->dsp.mc[4][b->filter][1], b->dst[0], ls_y,
                            ref2->data[0], ref2->linesize[0], tref2,
                            row << 3, col << 3, &b->mv[0][1], 4, 4, w, h);
                mc_luma_dir(s, s->dsp.mc[4][b->filter][1], b->dst[0] + 4, ls_y,
                            ref2->data[0], ref2->linesize[0], tref2,
                            row << 3, (col << 3) + 4, &b->mv[1][1], 4, 4, w, h);
                mc_luma_dir(s, s->dsp.mc[4][b->filter][1],
                            b->dst[0] + 4 * ls_y, ls_y,
                            ref2->data[0], ref2->linesize[0], tref2,
                            (row << 3) + 4, col << 3, &b->mv[2][1], 4, 4, w, h);
                mc_luma_dir(s, s->dsp.mc[4][b->filter][1],
                            b->dst[0] + 4 * ls_y + 4, ls_y,
                            ref2->data[0], ref2->linesize[0], tref2,
                            (row << 3) + 4, (col << 3) + 4, &b->mv[3][1], 4, 4, w, h);
            }
        }
    } else {
        int bwl = bwlog_tab[0][b->bs];
        int bw  = bwh_tab[0][b->bs][0] * 4;
        int bh  = bwh_tab[0][b->bs][1] * 4;

        mc_luma_dir(s, s->dsp.mc[bwl][b->filter][0], b->dst[0], ls_y,
                    ref1->data[0], ref1->linesize[0], tref1,
                    row << 3, col << 3, &b->mv[0][0], bw, bh, w, h);

        if (b->comp)
            mc_luma_dir(s, s->dsp.mc[bwl][b->filter][1], b->dst[0], ls_y,
                        ref2->data[0], ref2->linesize[0], tref2,
                        row << 3, col << 3, &b->mv[0][1], bw, bh, w, h);
    }

    // uv inter pred
    {
        int bwl = bwlog_tab[1][b->bs];
        int bw  = bwh_tab[1][b->bs][0] * 4, bh = bwh_tab[1][b->bs][1] * 4;
        VP56mv mvuv;

        w = (w + 1) >> 1;
        h = (h + 1) >> 1;
        if (b->bs > BS_8x8) {
            mvuv.x = ROUNDED_DIV(b->mv[0][0].x + b->mv[1][0].x +
                                 b->mv[2][0].x + b->mv[3][0].x, 4);
            mvuv.y = ROUNDED_DIV(b->mv[0][0].y + b->mv[1][0].y +
                                 b->mv[2][0].y + b->mv[3][0].y, 4);
        } else {
            mvuv = b->mv[0][0];
        }

        mc_chroma_dir(s, s->dsp.mc[bwl][b->filter][0],
                      b->dst[1], b->dst[2], ls_uv,
                      ref1->data[1], ref1->linesize[1],
                      ref1->data[2], ref1->linesize[2], tref1,
                      row << 2, col << 2, &mvuv, bw, bh, w, h);

        if (b->comp) {
            if (b->bs > BS_8x8) {
                mvuv.x = ROUNDED_DIV(b->mv[0][1].x + b->mv[1][1].x +
                                     b->mv[2][1].x + b->mv[3][1].x, 4);
                mvuv.y = ROUNDED_DIV(b->mv[0][1].y + b->mv[1][1].y +
                                     b->mv[2][1].y + b->mv[3][1].y, 4);
            } else {
                mvuv = b->mv[0][1];
            }
            mc_chroma_dir(s, s->dsp.mc[bwl][b->filter][1],
                          b->dst[1], b->dst[2], ls_uv,
                          ref2->data[1], ref2->linesize[1],
                          ref2->data[2], ref2->linesize[2], tref2,
                          row << 2, col << 2, &mvuv, bw, bh, w, h);
        }
    }

    if (!b->skip) {
        /* mostly copied intra_reconn() */

        int w4 = bwh_tab[1][b->bs][0] << 1, step1d = 1 << b->tx, n;
        int h4 = bwh_tab[1][b->bs][1] << 1, x, y, step = 1 << (b->tx * 2);
        int end_x = FFMIN(2 * (s->cols - col), w4);
        int end_y = FFMIN(2 * (s->rows - row), h4);
        int tx = 4 * s->lossless + b->tx, uvtx = b->uvtx + 4 * s->lossless;
        int uvstep1d = 1 << b->uvtx, p;
        uint8_t *dst = b->dst[0];

        // y itxfm add
        for (n = 0, y = 0; y < end_y; y += step1d) {
            uint8_t *ptr = dst;
            for (x = 0; x < end_x; x += step1d, ptr += 4 * step1d, n += step) {
                int eob = b->tx > TX_8X8 ? AV_RN16A(&s->eob[n]) : s->eob[n];

                if (eob)
                    s->dsp.itxfm_add[tx][DCT_DCT](ptr, b->y_stride,
                                                  s->block + 16 * n, eob);
            }
            dst += 4 * b->y_stride * step1d;
        }

        // uv itxfm add
        h4    >>= 1;
        w4    >>= 1;
        end_x >>= 1;
        end_y >>= 1;
        step    = 1 << (b->uvtx * 2);
        for (p = 0; p < 2; p++) {
            dst = b->dst[p + 1];
            for (n = 0, y = 0; y < end_y; y += uvstep1d) {
                uint8_t *ptr = dst;
                for (x = 0; x < end_x; x += uvstep1d, ptr += 4 * uvstep1d, n += step) {
                    int eob = b->uvtx > TX_8X8 ? AV_RN16A(&s->uveob[p][n])
                                               : s->uveob[p][n];
                    if (eob)
                        s->dsp.itxfm_add[uvtx][DCT_DCT](ptr, b->uv_stride,
                                                        s->uvblock[p] + 16 * n, eob);
                }
                dst += 4 * uvstep1d * b->uv_stride;
            }
        }
    }
    return 0;
}

static av_always_inline void mask_edges(VP9Filter *lflvl, int is_uv,
                                        int row_and_7, int col_and_7,
                                        int w, int h, int col_end, int row_end,
                                        enum TxfmMode tx, int skip_inter)
{
    // FIXME I'm pretty sure all loops can be replaced by a single LUT if
    // we make VP9Filter.mask uint64_t (i.e. row/col all single variable)
    // and make the LUT 5-indexed (bl, bp, is_uv, tx and row/col), and then
    // use row_and_7/col_and_7 as shifts (1*col_and_7+8*row_and_7)

    // the intended behaviour of the vp9 loopfilter is to work on 8-pixel
    // edges. This means that for UV, we work on two subsampled blocks at
    // a time, and we only use the topleft block's mode information to set
    // things like block strength. Thus, for any block size smaller than
    // 16x16, ignore the odd portion of the block.
    if (tx == TX_4X4 && is_uv) {
        if (h == 1) {
            if (row_and_7 & 1)
                return;
            if (!row_end)
                h += 1;
        }
        if (w == 1) {
            if (col_and_7 & 1)
                return;
            if (!col_end)
                w += 1;
        }
    }

    if (tx == TX_4X4 && !skip_inter) {
        int t = 1 << col_and_7, m_col = (t << w) - t, y;
        int m_col_odd = (t << (w - 1)) - t;

        // on 32-px edges, use the 8-px wide loopfilter; else, use 4-px wide
        if (is_uv) {
            int m_row_8 = m_col & 0x01, m_row_4 = m_col - m_row_8;

            for (y = row_and_7; y < h + row_and_7; y++) {
                int col_mask_id = 2 - !(y & 7);

                lflvl->mask[is_uv][0][y][1] |= m_row_8;
                lflvl->mask[is_uv][0][y][2] |= m_row_4;
                // for odd lines, if the odd col is not being filtered,
                // skip odd row also:
                // .---. <-- a
                // |   |
                // |___| <-- b
                // ^   ^
                // c   d
                //
                // if a/c are even row/col and b/d are odd, and d is skipped,
                // e.g. right edge of size-66x66.webm, then skip b also (bug)
                if ((col_end & 1) && (y & 1)) {
                    lflvl->mask[is_uv][1][y][col_mask_id] |= m_col_odd;
                } else {
                    lflvl->mask[is_uv][1][y][col_mask_id] |= m_col;
                }
            }
        } else {
            int m_row_8 = m_col & 0x11, m_row_4 = m_col - m_row_8;

            for (y = row_and_7; y < h + row_and_7; y++) {
                int col_mask_id = 2 - !(y & 3);

                lflvl->mask[is_uv][0][y][1]           |= m_row_8; // row edge
                lflvl->mask[is_uv][0][y][2]           |= m_row_4;
                lflvl->mask[is_uv][1][y][col_mask_id] |= m_col; // col edge
                lflvl->mask[is_uv][0][y][3]           |= m_col;
                lflvl->mask[is_uv][1][y][3]           |= m_col;
            }
        }
    } else {
        int y, t = 1 << col_and_7, m_col = (t << w) - t;

        if (!skip_inter) {
            int mask_id = (tx == TX_8X8);
            int l2 = tx + is_uv - 1, step1d = 1 << l2;
            static const unsigned masks[4] = { 0xff, 0x55, 0x11, 0x01 };
            int m_row = m_col & masks[l2];

            // at odd UV col/row edges tx16/tx32 loopfilter edges, force
            // 8wd loopfilter to prevent going off the visible edge.
            if (is_uv && tx > TX_8X8 && (w ^ (w - 1)) == 1) {
                int m_row_16 = ((t << (w - 1)) - t) & masks[l2];
                int m_row_8  = m_row - m_row_16;

                for (y = row_and_7; y < h + row_and_7; y++) {
                    lflvl->mask[is_uv][0][y][0] |= m_row_16;
                    lflvl->mask[is_uv][0][y][1] |= m_row_8;
                }
            } else {
                for (y = row_and_7; y < h + row_and_7; y++)
                    lflvl->mask[is_uv][0][y][mask_id] |= m_row;
            }

            if (is_uv && tx > TX_8X8 && (h ^ (h - 1)) == 1) {
                for (y = row_and_7; y < h + row_and_7 - 1; y += step1d)
                    lflvl->mask[is_uv][1][y][0] |= m_col;
                if (y - row_and_7 == h - 1)
                    lflvl->mask[is_uv][1][y][1] |= m_col;
            } else {
                for (y = row_and_7; y < h + row_and_7; y += step1d)
                    lflvl->mask[is_uv][1][y][mask_id] |= m_col;
            }
        } else if (tx != TX_4X4) {
            int mask_id;

            mask_id = (tx == TX_8X8) || (is_uv && h == 1);
            lflvl->mask[is_uv][1][row_and_7][mask_id] |= m_col;
            mask_id = (tx == TX_8X8) || (is_uv && w == 1);
            for (y = row_and_7; y < h + row_and_7; y++)
                lflvl->mask[is_uv][0][y][mask_id] |= t;
        } else if (is_uv) {
            int t8 = t & 0x01, t4 = t - t8;

            for (y = row_and_7; y < h + row_and_7; y++) {
                lflvl->mask[is_uv][0][y][2] |= t4;
                lflvl->mask[is_uv][0][y][1] |= t8;
            }
            lflvl->mask[is_uv][1][row_and_7][2 - !(row_and_7 & 7)] |= m_col;
        } else {
            int t8 = t & 0x11, t4 = t - t8;

            for (y = row_and_7; y < h + row_and_7; y++) {
                lflvl->mask[is_uv][0][y][2] |= t4;
                lflvl->mask[is_uv][0][y][1] |= t8;
            }
            lflvl->mask[is_uv][1][row_and_7][2 - !(row_and_7 & 3)] |= m_col;
        }
    }
}

int ff_vp9_decode_block(AVCodecContext *avctx, int row, int col,
                        VP9Filter *lflvl, ptrdiff_t yoff, ptrdiff_t uvoff,
                        enum BlockLevel bl, enum BlockPartition bp)
{
    VP9Context *s = avctx->priv_data;
    VP9Block *b = s->b;
    AVFrame *f = s->frames[CUR_FRAME].tf.f;
    enum BlockSize bs = bl * 3 + bp;
    int ret, y, w4 = bwh_tab[1][bs][0], h4 = bwh_tab[1][bs][1], lvl;
    int emu[2];

    b->row  = row;
    b->row7 = row & 7;
    b->col  = col;
    b->col7 = col & 7;

    s->min_mv.x = -(128 + col * 64);
    s->min_mv.y = -(128 + row * 64);
    s->max_mv.x = 128 + (s->cols - col - w4) * 64;
    s->max_mv.y = 128 + (s->rows - row - h4) * 64;

    if (s->pass < 2) {
        b->bs = bs;
        b->bl = bl;
        b->bp = bp;
        decode_mode(s, b);
        b->uvtx = b->tx - (w4 * 2 == (1 << b->tx) || h4 * 2 == (1 << b->tx));

        if (!b->skip) {
            if ((ret = decode_coeffs(avctx)) < 0)
                return ret;
        } else {
            int pl;

            memset(&s->above_y_nnz_ctx[col * 2], 0, w4 * 2);
            memset(&s->left_y_nnz_ctx[(row & 7) << 1], 0, h4 * 2);
            for (pl = 0; pl < 2; pl++) {
                memset(&s->above_uv_nnz_ctx[pl][col], 0, w4);
                memset(&s->left_uv_nnz_ctx[pl][row & 7], 0, h4);
            }
        }

        if (s->pass == 1) {
            s->b++;
            s->block      += w4 * h4 * 64;
            s->uvblock[0] += w4 * h4 * 16;
            s->uvblock[1] += w4 * h4 * 16;
            s->eob        += w4 * h4 * 4;
            s->uveob[0]   += w4 * h4;
            s->uveob[1]   += w4 * h4;

            return 0;
        }
    }

    /* Emulated overhangs if the stride of the target buffer can't hold.
     * This allows to support emu-edge and so on even if we have large
     * block overhangs. */
    emu[0] = (col + w4) * 8 > f->linesize[0] ||
             (row + h4) > s->rows;
    emu[1] = (col + w4) * 4 > f->linesize[1] ||
             (row + h4) > s->rows;
    if (emu[0]) {
        b->dst[0]   = s->tmp_y;
        b->y_stride = 64;
    } else {
        b->dst[0]   = f->data[0] + yoff;
        b->y_stride = f->linesize[0];
    }
    if (emu[1]) {
        b->dst[1]    = s->tmp_uv[0];
        b->dst[2]    = s->tmp_uv[1];
        b->uv_stride = 32;
    } else {
        b->dst[1]    = f->data[1] + uvoff;
        b->dst[2]    = f->data[2] + uvoff;
        b->uv_stride = f->linesize[1];
    }
    if (b->intra) {
        intra_recon(avctx, yoff, uvoff);
    } else {
        if ((ret = inter_recon(avctx)) < 0)
            return ret;
    }
    if (emu[0]) {
        int w = FFMIN(s->cols - col, w4) * 8;
        int h = FFMIN(s->rows - row, h4) * 8;
        int n, o = 0;

        for (n = 0; o < w; n++) {
            int bw = 64 >> n;

            av_assert2(n <= 4);
            if (w & bw) {
                s->dsp.mc[n][0][0][0][0](f->data[0] + yoff + o,
                                         s->tmp_y + o,
                                         f->linesize[0],
                                         64, h, 0, 0);
                o += bw;
            }
        }
    }
    if (emu[1]) {
        int w = FFMIN(s->cols - col, w4) * 4;
        int h = FFMIN(s->rows - row, h4) * 4;
        int n, o = 0;

        for (n = 1; o < w; n++) {
            int bw = 64 >> n;

            av_assert2(n <= 4);
            if (w & bw) {
                s->dsp.mc[n][0][0][0][0](f->data[1] + uvoff + o,
                                         s->tmp_uv[0] + o,
                                         f->linesize[1],
                                         32, h, 0, 0);
                s->dsp.mc[n][0][0][0][0](f->data[2] + uvoff + o,
                                         s->tmp_uv[1] + o,
                                         f->linesize[2],
                                         32, h, 0, 0);
                o += bw;
            }
        }
    }

    // pick filter level and find edges to apply filter to
    if (s->filter.level &&
        (lvl = s->segmentation.feat[b->seg_id].lflvl[b->intra ? 0 : b->ref[0] + 1]
                                                    [b->mode[3] != ZEROMV]) > 0) {
        int x_end = FFMIN(s->cols - col, w4);
        int y_end = FFMIN(s->rows - row, h4);
        int skip_inter = !b->intra && b->skip;

        for (y = 0; y < h4; y++)
            memset(&lflvl->level[((row & 7) + y) * 8 + (col & 7)], lvl, w4);
        mask_edges(lflvl, 0, row & 7, col & 7, x_end, y_end, 0, 0, b->tx, skip_inter);
        mask_edges(lflvl, 1, row & 7, col & 7, x_end, y_end,
                   s->cols & 1 && col + w4 >= s->cols ? s->cols & 7 : 0,
                   s->rows & 1 && row + h4 >= s->rows ? s->rows & 7 : 0,
                   b->uvtx, skip_inter);

        if (!s->filter.lim_lut[lvl]) {
            int sharp = s->filter.sharpness;
            int limit = lvl;

            if (sharp > 0) {
                limit >>= (sharp + 3) >> 2;
                limit   = FFMIN(limit, 9 - sharp);
            }
            limit = FFMAX(limit, 1);

            s->filter.lim_lut[lvl]   = limit;
            s->filter.mblim_lut[lvl] = 2 * (lvl + 2) + limit;
        }
    }

    if (s->pass == 2) {
        s->b++;
        s->block      += w4 * h4 * 64;
        s->uvblock[0] += w4 * h4 * 16;
        s->uvblock[1] += w4 * h4 * 16;
        s->eob        += w4 * h4 * 4;
        s->uveob[0]   += w4 * h4;
        s->uveob[1]   += w4 * h4;
    }

    return 0;
}
