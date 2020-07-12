/*
 * VP9 compatible video decoder
 *
 * Copyright (C) 2013 Ronald S. Bultje <rsbultje gmail com>
 * Copyright (C) 2013 Clément Bœsch <u pkh me>
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

#include "libavutil/avassert.h"

#include "avcodec.h"
#include "internal.h"
#include "videodsp.h"
#include "vp56.h"
#include "vp9.h"
#include "vp9data.h"
#include "vp9dec.h"

static av_always_inline void setctx_2d(uint8_t *ptr, int w, int h,
                                       ptrdiff_t stride, int v)
{
    switch (w) {
    case 1:
        do {
            *ptr = v;
            ptr += stride;
        } while (--h);
        break;
    case 2: {
        int v16 = v * 0x0101;
        do {
            AV_WN16A(ptr, v16);
            ptr += stride;
        } while (--h);
        break;
    }
    case 4: {
        uint32_t v32 = v * 0x01010101;
        do {
            AV_WN32A(ptr, v32);
            ptr += stride;
        } while (--h);
        break;
    }
    case 8: {
#if HAVE_FAST_64BIT
        uint64_t v64 = v * 0x0101010101010101ULL;
        do {
            AV_WN64A(ptr, v64);
            ptr += stride;
        } while (--h);
#else
        uint32_t v32 = v * 0x01010101;
        do {
            AV_WN32A(ptr,     v32);
            AV_WN32A(ptr + 4, v32);
            ptr += stride;
        } while (--h);
#endif
        break;
    }
    }
}

static void decode_mode(VP9TileData *td)
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
    VP9Context *s = td->s;
    VP9Block *b = td->b;
    int row = td->row, col = td->col, row7 = td->row7;
    enum TxfmMode max_tx = max_tx_for_bl_bp[b->bs];
    int bw4 = ff_vp9_bwh_tab[1][b->bs][0], w4 = FFMIN(s->cols - col, bw4);
    int bh4 = ff_vp9_bwh_tab[1][b->bs][1], h4 = FFMIN(s->rows - row, bh4), y;
    int have_a = row > 0, have_l = col > td->tile_col_start;
    int vref, filter_id;

    if (!s->s.h.segmentation.enabled) {
        b->seg_id = 0;
    } else if (s->s.h.keyframe || s->s.h.intraonly) {
        b->seg_id = !s->s.h.segmentation.update_map ? 0 :
                    vp8_rac_get_tree(td->c, ff_vp9_segmentation_tree, s->s.h.segmentation.prob);
    } else if (!s->s.h.segmentation.update_map ||
               (s->s.h.segmentation.temporal &&
                vp56_rac_get_prob_branchy(td->c,
                    s->s.h.segmentation.pred_prob[s->above_segpred_ctx[col] +
                                    td->left_segpred_ctx[row7]]))) {
        if (!s->s.h.errorres && s->s.frames[REF_FRAME_SEGMAP].segmentation_map) {
            int pred = 8, x;
            uint8_t *refsegmap = s->s.frames[REF_FRAME_SEGMAP].segmentation_map;

            if (!s->s.frames[REF_FRAME_SEGMAP].uses_2pass)
                ff_thread_await_progress(&s->s.frames[REF_FRAME_SEGMAP].tf, row >> 3, 0);
            for (y = 0; y < h4; y++) {
                int idx_base = (y + row) * 8 * s->sb_cols + col;
                for (x = 0; x < w4; x++)
                    pred = FFMIN(pred, refsegmap[idx_base + x]);
            }
            av_assert1(pred < 8);
            b->seg_id = pred;
        } else {
            b->seg_id = 0;
        }

        memset(&s->above_segpred_ctx[col], 1, w4);
        memset(&td->left_segpred_ctx[row7], 1, h4);
    } else {
        b->seg_id = vp8_rac_get_tree(td->c, ff_vp9_segmentation_tree,
                                     s->s.h.segmentation.prob);

        memset(&s->above_segpred_ctx[col], 0, w4);
        memset(&td->left_segpred_ctx[row7], 0, h4);
    }
    if (s->s.h.segmentation.enabled &&
        (s->s.h.segmentation.update_map || s->s.h.keyframe || s->s.h.intraonly)) {
        setctx_2d(&s->s.frames[CUR_FRAME].segmentation_map[row * 8 * s->sb_cols + col],
                  bw4, bh4, 8 * s->sb_cols, b->seg_id);
    }

    b->skip = s->s.h.segmentation.enabled &&
        s->s.h.segmentation.feat[b->seg_id].skip_enabled;
    if (!b->skip) {
        int c = td->left_skip_ctx[row7] + s->above_skip_ctx[col];
        b->skip = vp56_rac_get_prob(td->c, s->prob.p.skip[c]);
        td->counts.skip[c][b->skip]++;
    }

    if (s->s.h.keyframe || s->s.h.intraonly) {
        b->intra = 1;
    } else if (s->s.h.segmentation.enabled && s->s.h.segmentation.feat[b->seg_id].ref_enabled) {
        b->intra = !s->s.h.segmentation.feat[b->seg_id].ref_val;
    } else {
        int c, bit;

        if (have_a && have_l) {
            c = s->above_intra_ctx[col] + td->left_intra_ctx[row7];
            c += (c == 2);
        } else {
            c = have_a ? 2 * s->above_intra_ctx[col] :
                have_l ? 2 * td->left_intra_ctx[row7] : 0;
        }
        bit = vp56_rac_get_prob(td->c, s->prob.p.intra[c]);
        td->counts.intra[c][bit]++;
        b->intra = !bit;
    }

    if ((b->intra || !b->skip) && s->s.h.txfmmode == TX_SWITCHABLE) {
        int c;
        if (have_a) {
            if (have_l) {
                c = (s->above_skip_ctx[col] ? max_tx :
                     s->above_txfm_ctx[col]) +
                    (td->left_skip_ctx[row7] ? max_tx :
                     td->left_txfm_ctx[row7]) > max_tx;
            } else {
                c = s->above_skip_ctx[col] ? 1 :
                    (s->above_txfm_ctx[col] * 2 > max_tx);
            }
        } else if (have_l) {
            c = td->left_skip_ctx[row7] ? 1 :
                (td->left_txfm_ctx[row7] * 2 > max_tx);
        } else {
            c = 1;
        }
        switch (max_tx) {
        case TX_32X32:
            b->tx = vp56_rac_get_prob(td->c, s->prob.p.tx32p[c][0]);
            if (b->tx) {
                b->tx += vp56_rac_get_prob(td->c, s->prob.p.tx32p[c][1]);
                if (b->tx == 2)
                    b->tx += vp56_rac_get_prob(td->c, s->prob.p.tx32p[c][2]);
            }
            td->counts.tx32p[c][b->tx]++;
            break;
        case TX_16X16:
            b->tx = vp56_rac_get_prob(td->c, s->prob.p.tx16p[c][0]);
            if (b->tx)
                b->tx += vp56_rac_get_prob(td->c, s->prob.p.tx16p[c][1]);
            td->counts.tx16p[c][b->tx]++;
            break;
        case TX_8X8:
            b->tx = vp56_rac_get_prob(td->c, s->prob.p.tx8p[c]);
            td->counts.tx8p[c][b->tx]++;
            break;
        case TX_4X4:
            b->tx = TX_4X4;
            break;
        }
    } else {
        b->tx = FFMIN(max_tx, s->s.h.txfmmode);
    }

    if (s->s.h.keyframe || s->s.h.intraonly) {
        uint8_t *a = &s->above_mode_ctx[col * 2];
        uint8_t *l = &td->left_mode_ctx[(row7) << 1];

        b->comp = 0;
        if (b->bs > BS_8x8) {
            // FIXME the memory storage intermediates here aren't really
            // necessary, they're just there to make the code slightly
            // simpler for now
            b->mode[0] =
            a[0]       = vp8_rac_get_tree(td->c, ff_vp9_intramode_tree,
                                          ff_vp9_default_kf_ymode_probs[a[0]][l[0]]);
            if (b->bs != BS_8x4) {
                b->mode[1] = vp8_rac_get_tree(td->c, ff_vp9_intramode_tree,
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
                a[0]       = vp8_rac_get_tree(td->c, ff_vp9_intramode_tree,
                                              ff_vp9_default_kf_ymode_probs[a[0]][l[1]]);
                if (b->bs != BS_8x4) {
                    b->mode[3] = vp8_rac_get_tree(td->c, ff_vp9_intramode_tree,
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
            b->mode[0] = vp8_rac_get_tree(td->c, ff_vp9_intramode_tree,
                                          ff_vp9_default_kf_ymode_probs[*a][*l]);
            b->mode[3] =
            b->mode[2] =
            b->mode[1] = b->mode[0];
            // FIXME this can probably be optimized
            memset(a, b->mode[0], ff_vp9_bwh_tab[0][b->bs][0]);
            memset(l, b->mode[0], ff_vp9_bwh_tab[0][b->bs][1]);
        }
        b->uvmode = vp8_rac_get_tree(td->c, ff_vp9_intramode_tree,
                                     ff_vp9_default_kf_uvmode_probs[b->mode[3]]);
    } else if (b->intra) {
        b->comp = 0;
        if (b->bs > BS_8x8) {
            b->mode[0] = vp8_rac_get_tree(td->c, ff_vp9_intramode_tree,
                                          s->prob.p.y_mode[0]);
            td->counts.y_mode[0][b->mode[0]]++;
            if (b->bs != BS_8x4) {
                b->mode[1] = vp8_rac_get_tree(td->c, ff_vp9_intramode_tree,
                                              s->prob.p.y_mode[0]);
                td->counts.y_mode[0][b->mode[1]]++;
            } else {
                b->mode[1] = b->mode[0];
            }
            if (b->bs != BS_4x8) {
                b->mode[2] = vp8_rac_get_tree(td->c, ff_vp9_intramode_tree,
                                              s->prob.p.y_mode[0]);
                td->counts.y_mode[0][b->mode[2]]++;
                if (b->bs != BS_8x4) {
                    b->mode[3] = vp8_rac_get_tree(td->c, ff_vp9_intramode_tree,
                                                  s->prob.p.y_mode[0]);
                    td->counts.y_mode[0][b->mode[3]]++;
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

            b->mode[0] = vp8_rac_get_tree(td->c, ff_vp9_intramode_tree,
                                          s->prob.p.y_mode[sz]);
            b->mode[1] =
            b->mode[2] =
            b->mode[3] = b->mode[0];
            td->counts.y_mode[sz][b->mode[3]]++;
        }
        b->uvmode = vp8_rac_get_tree(td->c, ff_vp9_intramode_tree,
                                     s->prob.p.uv_mode[b->mode[3]]);
        td->counts.uv_mode[b->mode[3]][b->uvmode]++;
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

        if (s->s.h.segmentation.enabled && s->s.h.segmentation.feat[b->seg_id].ref_enabled) {
            av_assert2(s->s.h.segmentation.feat[b->seg_id].ref_val != 0);
            b->comp = 0;
            b->ref[0] = s->s.h.segmentation.feat[b->seg_id].ref_val - 1;
        } else {
            // read comp_pred flag
            if (s->s.h.comppredmode != PRED_SWITCHABLE) {
                b->comp = s->s.h.comppredmode == PRED_COMPREF;
            } else {
                int c;

                // FIXME add intra as ref=0xff (or -1) to make these easier?
                if (have_a) {
                    if (have_l) {
                        if (s->above_comp_ctx[col] && td->left_comp_ctx[row7]) {
                            c = 4;
                        } else if (s->above_comp_ctx[col]) {
                            c = 2 + (td->left_intra_ctx[row7] ||
                                     td->left_ref_ctx[row7] == s->s.h.fixcompref);
                        } else if (td->left_comp_ctx[row7]) {
                            c = 2 + (s->above_intra_ctx[col] ||
                                     s->above_ref_ctx[col] == s->s.h.fixcompref);
                        } else {
                            c = (!s->above_intra_ctx[col] &&
                                 s->above_ref_ctx[col] == s->s.h.fixcompref) ^
                                (!td->left_intra_ctx[row7] &&
                                 td->left_ref_ctx[row & 7] == s->s.h.fixcompref);
                        }
                    } else {
                        c = s->above_comp_ctx[col] ? 3 :
                        (!s->above_intra_ctx[col] && s->above_ref_ctx[col] == s->s.h.fixcompref);
                    }
                } else if (have_l) {
                    c = td->left_comp_ctx[row7] ? 3 :
                    (!td->left_intra_ctx[row7] && td->left_ref_ctx[row7] == s->s.h.fixcompref);
                } else {
                    c = 1;
                }
                b->comp = vp56_rac_get_prob(td->c, s->prob.p.comp[c]);
                td->counts.comp[c][b->comp]++;
            }

            // read actual references
            // FIXME probably cache a few variables here to prevent repetitive
            // memory accesses below
            if (b->comp) { /* two references */
                int fix_idx = s->s.h.signbias[s->s.h.fixcompref], var_idx = !fix_idx, c, bit;

                b->ref[fix_idx] = s->s.h.fixcompref;
                // FIXME can this codeblob be replaced by some sort of LUT?
                if (have_a) {
                    if (have_l) {
                        if (s->above_intra_ctx[col]) {
                            if (td->left_intra_ctx[row7]) {
                                c = 2;
                            } else {
                                c = 1 + 2 * (td->left_ref_ctx[row7] != s->s.h.varcompref[1]);
                            }
                        } else if (td->left_intra_ctx[row7]) {
                            c = 1 + 2 * (s->above_ref_ctx[col] != s->s.h.varcompref[1]);
                        } else {
                            int refl = td->left_ref_ctx[row7], refa = s->above_ref_ctx[col];

                            if (refl == refa && refa == s->s.h.varcompref[1]) {
                                c = 0;
                            } else if (!td->left_comp_ctx[row7] && !s->above_comp_ctx[col]) {
                                if ((refa == s->s.h.fixcompref && refl == s->s.h.varcompref[0]) ||
                                    (refl == s->s.h.fixcompref && refa == s->s.h.varcompref[0])) {
                                    c = 4;
                                } else {
                                    c = (refa == refl) ? 3 : 1;
                                }
                            } else if (!td->left_comp_ctx[row7]) {
                                if (refa == s->s.h.varcompref[1] && refl != s->s.h.varcompref[1]) {
                                    c = 1;
                                } else {
                                    c = (refl == s->s.h.varcompref[1] &&
                                         refa != s->s.h.varcompref[1]) ? 2 : 4;
                                }
                            } else if (!s->above_comp_ctx[col]) {
                                if (refl == s->s.h.varcompref[1] && refa != s->s.h.varcompref[1]) {
                                    c = 1;
                                } else {
                                    c = (refa == s->s.h.varcompref[1] &&
                                         refl != s->s.h.varcompref[1]) ? 2 : 4;
                                }
                            } else {
                                c = (refl == refa) ? 4 : 2;
                            }
                        }
                    } else {
                        if (s->above_intra_ctx[col]) {
                            c = 2;
                        } else if (s->above_comp_ctx[col]) {
                            c = 4 * (s->above_ref_ctx[col] != s->s.h.varcompref[1]);
                        } else {
                            c = 3 * (s->above_ref_ctx[col] != s->s.h.varcompref[1]);
                        }
                    }
                } else if (have_l) {
                    if (td->left_intra_ctx[row7]) {
                        c = 2;
                    } else if (td->left_comp_ctx[row7]) {
                        c = 4 * (td->left_ref_ctx[row7] != s->s.h.varcompref[1]);
                    } else {
                        c = 3 * (td->left_ref_ctx[row7] != s->s.h.varcompref[1]);
                    }
                } else {
                    c = 2;
                }
                bit = vp56_rac_get_prob(td->c, s->prob.p.comp_ref[c]);
                b->ref[var_idx] = s->s.h.varcompref[bit];
                td->counts.comp_ref[c][bit]++;
            } else /* single reference */ {
                int bit, c;

                if (have_a && !s->above_intra_ctx[col]) {
                    if (have_l && !td->left_intra_ctx[row7]) {
                        if (td->left_comp_ctx[row7]) {
                            if (s->above_comp_ctx[col]) {
                                c = 1 + (!s->s.h.fixcompref || !td->left_ref_ctx[row7] ||
                                         !s->above_ref_ctx[col]);
                            } else {
                                c = (3 * !s->above_ref_ctx[col]) +
                                    (!s->s.h.fixcompref || !td->left_ref_ctx[row7]);
                            }
                        } else if (s->above_comp_ctx[col]) {
                            c = (3 * !td->left_ref_ctx[row7]) +
                                (!s->s.h.fixcompref || !s->above_ref_ctx[col]);
                        } else {
                            c = 2 * !td->left_ref_ctx[row7] + 2 * !s->above_ref_ctx[col];
                        }
                    } else if (s->above_intra_ctx[col]) {
                        c = 2;
                    } else if (s->above_comp_ctx[col]) {
                        c = 1 + (!s->s.h.fixcompref || !s->above_ref_ctx[col]);
                    } else {
                        c = 4 * (!s->above_ref_ctx[col]);
                    }
                } else if (have_l && !td->left_intra_ctx[row7]) {
                    if (td->left_intra_ctx[row7]) {
                        c = 2;
                    } else if (td->left_comp_ctx[row7]) {
                        c = 1 + (!s->s.h.fixcompref || !td->left_ref_ctx[row7]);
                    } else {
                        c = 4 * (!td->left_ref_ctx[row7]);
                    }
                } else {
                    c = 2;
                }
                bit = vp56_rac_get_prob(td->c, s->prob.p.single_ref[c][0]);
                td->counts.single_ref[c][0][bit]++;
                if (!bit) {
                    b->ref[0] = 0;
                } else {
                    // FIXME can this codeblob be replaced by some sort of LUT?
                    if (have_a) {
                        if (have_l) {
                            if (td->left_intra_ctx[row7]) {
                                if (s->above_intra_ctx[col]) {
                                    c = 2;
                                } else if (s->above_comp_ctx[col]) {
                                    c = 1 + 2 * (s->s.h.fixcompref == 1 ||
                                                 s->above_ref_ctx[col] == 1);
                                } else if (!s->above_ref_ctx[col]) {
                                    c = 3;
                                } else {
                                    c = 4 * (s->above_ref_ctx[col] == 1);
                                }
                            } else if (s->above_intra_ctx[col]) {
                                if (td->left_intra_ctx[row7]) {
                                    c = 2;
                                } else if (td->left_comp_ctx[row7]) {
                                    c = 1 + 2 * (s->s.h.fixcompref == 1 ||
                                                 td->left_ref_ctx[row7] == 1);
                                } else if (!td->left_ref_ctx[row7]) {
                                    c = 3;
                                } else {
                                    c = 4 * (td->left_ref_ctx[row7] == 1);
                                }
                            } else if (s->above_comp_ctx[col]) {
                                if (td->left_comp_ctx[row7]) {
                                    if (td->left_ref_ctx[row7] == s->above_ref_ctx[col]) {
                                        c = 3 * (s->s.h.fixcompref == 1 ||
                                                 td->left_ref_ctx[row7] == 1);
                                    } else {
                                        c = 2;
                                    }
                                } else if (!td->left_ref_ctx[row7]) {
                                    c = 1 + 2 * (s->s.h.fixcompref == 1 ||
                                                 s->above_ref_ctx[col] == 1);
                                } else {
                                    c = 3 * (td->left_ref_ctx[row7] == 1) +
                                    (s->s.h.fixcompref == 1 || s->above_ref_ctx[col] == 1);
                                }
                            } else if (td->left_comp_ctx[row7]) {
                                if (!s->above_ref_ctx[col]) {
                                    c = 1 + 2 * (s->s.h.fixcompref == 1 ||
                                                 td->left_ref_ctx[row7] == 1);
                                } else {
                                    c = 3 * (s->above_ref_ctx[col] == 1) +
                                    (s->s.h.fixcompref == 1 || td->left_ref_ctx[row7] == 1);
                                }
                            } else if (!s->above_ref_ctx[col]) {
                                if (!td->left_ref_ctx[row7]) {
                                    c = 3;
                                } else {
                                    c = 4 * (td->left_ref_ctx[row7] == 1);
                                }
                            } else if (!td->left_ref_ctx[row7]) {
                                c = 4 * (s->above_ref_ctx[col] == 1);
                            } else {
                                c = 2 * (td->left_ref_ctx[row7] == 1) +
                                    2 * (s->above_ref_ctx[col] == 1);
                            }
                        } else {
                            if (s->above_intra_ctx[col] ||
                                (!s->above_comp_ctx[col] && !s->above_ref_ctx[col])) {
                                c = 2;
                            } else if (s->above_comp_ctx[col]) {
                                c = 3 * (s->s.h.fixcompref == 1 || s->above_ref_ctx[col] == 1);
                            } else {
                                c = 4 * (s->above_ref_ctx[col] == 1);
                            }
                        }
                    } else if (have_l) {
                        if (td->left_intra_ctx[row7] ||
                            (!td->left_comp_ctx[row7] && !td->left_ref_ctx[row7])) {
                            c = 2;
                        } else if (td->left_comp_ctx[row7]) {
                            c = 3 * (s->s.h.fixcompref == 1 || td->left_ref_ctx[row7] == 1);
                        } else {
                            c = 4 * (td->left_ref_ctx[row7] == 1);
                        }
                    } else {
                        c = 2;
                    }
                    bit = vp56_rac_get_prob(td->c, s->prob.p.single_ref[c][1]);
                    td->counts.single_ref[c][1][bit]++;
                    b->ref[0] = 1 + bit;
                }
            }
        }

        if (b->bs <= BS_8x8) {
            if (s->s.h.segmentation.enabled && s->s.h.segmentation.feat[b->seg_id].skip_enabled) {
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
                                          [td->left_mode_ctx[row7 + off[b->bs]]];

                b->mode[0] = vp8_rac_get_tree(td->c, ff_vp9_inter_mode_tree,
                                              s->prob.p.mv_mode[c]);
                b->mode[1] =
                b->mode[2] =
                b->mode[3] = b->mode[0];
                td->counts.mv_mode[c][b->mode[0] - 10]++;
            }
        }

        if (s->s.h.filtermode == FILTER_SWITCHABLE) {
            int c;

            if (have_a && s->above_mode_ctx[col] >= NEARESTMV) {
                if (have_l && td->left_mode_ctx[row7] >= NEARESTMV) {
                    c = s->above_filter_ctx[col] == td->left_filter_ctx[row7] ?
                        td->left_filter_ctx[row7] : 3;
                } else {
                    c = s->above_filter_ctx[col];
                }
            } else if (have_l && td->left_mode_ctx[row7] >= NEARESTMV) {
                c = td->left_filter_ctx[row7];
            } else {
                c = 3;
            }

            filter_id = vp8_rac_get_tree(td->c, ff_vp9_filter_tree,
                                         s->prob.p.filter[c]);
            td->counts.filter[c][filter_id]++;
            b->filter = ff_vp9_filter_lut[filter_id];
        } else {
            b->filter = s->s.h.filtermode;
        }

        if (b->bs > BS_8x8) {
            int c = inter_mode_ctx_lut[s->above_mode_ctx[col]][td->left_mode_ctx[row7]];

            b->mode[0] = vp8_rac_get_tree(td->c, ff_vp9_inter_mode_tree,
                                          s->prob.p.mv_mode[c]);
            td->counts.mv_mode[c][b->mode[0] - 10]++;
            ff_vp9_fill_mv(td, b->mv[0], b->mode[0], 0);

            if (b->bs != BS_8x4) {
                b->mode[1] = vp8_rac_get_tree(td->c, ff_vp9_inter_mode_tree,
                                              s->prob.p.mv_mode[c]);
                td->counts.mv_mode[c][b->mode[1] - 10]++;
                ff_vp9_fill_mv(td, b->mv[1], b->mode[1], 1);
            } else {
                b->mode[1] = b->mode[0];
                AV_COPY32(&b->mv[1][0], &b->mv[0][0]);
                AV_COPY32(&b->mv[1][1], &b->mv[0][1]);
            }

            if (b->bs != BS_4x8) {
                b->mode[2] = vp8_rac_get_tree(td->c, ff_vp9_inter_mode_tree,
                                              s->prob.p.mv_mode[c]);
                td->counts.mv_mode[c][b->mode[2] - 10]++;
                ff_vp9_fill_mv(td, b->mv[2], b->mode[2], 2);

                if (b->bs != BS_8x4) {
                    b->mode[3] = vp8_rac_get_tree(td->c, ff_vp9_inter_mode_tree,
                                                  s->prob.p.mv_mode[c]);
                    td->counts.mv_mode[c][b->mode[3] - 10]++;
                    ff_vp9_fill_mv(td, b->mv[3], b->mode[3], 3);
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
            ff_vp9_fill_mv(td, b->mv[0], b->mode[0], -1);
            AV_COPY32(&b->mv[1][0], &b->mv[0][0]);
            AV_COPY32(&b->mv[2][0], &b->mv[0][0]);
            AV_COPY32(&b->mv[3][0], &b->mv[0][0]);
            AV_COPY32(&b->mv[1][1], &b->mv[0][1]);
            AV_COPY32(&b->mv[2][1], &b->mv[0][1]);
            AV_COPY32(&b->mv[3][1], &b->mv[0][1]);
        }

        vref = b->ref[b->comp ? s->s.h.signbias[s->s.h.varcompref[0]] : 0];
    }

#if HAVE_FAST_64BIT
#define SPLAT_CTX(var, val, n) \
    switch (n) { \
    case 1:  var = val;                                    break; \
    case 2:  AV_WN16A(&var, val *             0x0101);     break; \
    case 4:  AV_WN32A(&var, val *         0x01010101);     break; \
    case 8:  AV_WN64A(&var, val * 0x0101010101010101ULL);  break; \
    case 16: { \
        uint64_t v64 = val * 0x0101010101010101ULL; \
        AV_WN64A(              &var,     v64); \
        AV_WN64A(&((uint8_t *) &var)[8], v64); \
        break; \
    } \
    }
#else
#define SPLAT_CTX(var, val, n) \
    switch (n) { \
    case 1:  var = val;                         break; \
    case 2:  AV_WN16A(&var, val *     0x0101);  break; \
    case 4:  AV_WN32A(&var, val * 0x01010101);  break; \
    case 8: { \
        uint32_t v32 = val * 0x01010101; \
        AV_WN32A(              &var,     v32); \
        AV_WN32A(&((uint8_t *) &var)[4], v32); \
        break; \
    } \
    case 16: { \
        uint32_t v32 = val * 0x01010101; \
        AV_WN32A(              &var,      v32); \
        AV_WN32A(&((uint8_t *) &var)[4],  v32); \
        AV_WN32A(&((uint8_t *) &var)[8],  v32); \
        AV_WN32A(&((uint8_t *) &var)[12], v32); \
        break; \
    } \
    }
#endif

    switch (ff_vp9_bwh_tab[1][b->bs][0]) {
#define SET_CTXS(perf, dir, off, n) \
    do { \
        SPLAT_CTX(perf->dir##_skip_ctx[off],      b->skip,          n); \
        SPLAT_CTX(perf->dir##_txfm_ctx[off],      b->tx,            n); \
        SPLAT_CTX(perf->dir##_partition_ctx[off], dir##_ctx[b->bs], n); \
        if (!s->s.h.keyframe && !s->s.h.intraonly) { \
            SPLAT_CTX(perf->dir##_intra_ctx[off], b->intra,   n); \
            SPLAT_CTX(perf->dir##_comp_ctx[off],  b->comp,    n); \
            SPLAT_CTX(perf->dir##_mode_ctx[off],  b->mode[3], n); \
            if (!b->intra) { \
                SPLAT_CTX(perf->dir##_ref_ctx[off], vref, n); \
                if (s->s.h.filtermode == FILTER_SWITCHABLE) { \
                    SPLAT_CTX(perf->dir##_filter_ctx[off], filter_id, n); \
                } \
            } \
        } \
    } while (0)
    case 1: SET_CTXS(s, above, col, 1); break;
    case 2: SET_CTXS(s, above, col, 2); break;
    case 4: SET_CTXS(s, above, col, 4); break;
    case 8: SET_CTXS(s, above, col, 8); break;
    }
    switch (ff_vp9_bwh_tab[1][b->bs][1]) {
    case 1: SET_CTXS(td, left, row7, 1); break;
    case 2: SET_CTXS(td, left, row7, 2); break;
    case 4: SET_CTXS(td, left, row7, 4); break;
    case 8: SET_CTXS(td, left, row7, 8); break;
    }
#undef SPLAT_CTX
#undef SET_CTXS

    if (!s->s.h.keyframe && !s->s.h.intraonly) {
        if (b->bs > BS_8x8) {
            int mv0 = AV_RN32A(&b->mv[3][0]), mv1 = AV_RN32A(&b->mv[3][1]);

            AV_COPY32(&td->left_mv_ctx[row7 * 2 + 0][0], &b->mv[1][0]);
            AV_COPY32(&td->left_mv_ctx[row7 * 2 + 0][1], &b->mv[1][1]);
            AV_WN32A(&td->left_mv_ctx[row7 * 2 + 1][0], mv0);
            AV_WN32A(&td->left_mv_ctx[row7 * 2 + 1][1], mv1);
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
                AV_WN32A(&td->left_mv_ctx[row7 * 2 + n][0], mv0);
                AV_WN32A(&td->left_mv_ctx[row7 * 2 + n][1], mv1);
            }
        }
    }

    // FIXME kinda ugly
    for (y = 0; y < h4; y++) {
        int x, o = (row + y) * s->sb_cols * 8 + col;
        VP9mvrefPair *mv = &s->s.frames[CUR_FRAME].mv[o];

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

// FIXME merge cnt/eob arguments?
static av_always_inline int
decode_coeffs_b_generic(VP56RangeCoder *c, int16_t *coef, int n_coeffs,
                        int is_tx32x32, int is8bitsperpixel, int bpp, unsigned (*cnt)[6][3],
                        unsigned (*eob)[6][2], uint8_t (*p)[6][11],
                        int nnz, const int16_t *scan, const int16_t (*nb)[2],
                        const int16_t *band_counts, int16_t *qmul)
{
    int i = 0, band = 0, band_left = band_counts[band];
    const uint8_t *tp = p[0][nnz];
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
                        val  = 11 + (vp56_rac_get_prob(c, 173) << 2);
                        val +=      (vp56_rac_get_prob(c, 148) << 1);
                        val +=       vp56_rac_get_prob(c, 140);
                    } else {
                        val  = 19 + (vp56_rac_get_prob(c, 176) << 3);
                        val +=      (vp56_rac_get_prob(c, 155) << 2);
                        val +=      (vp56_rac_get_prob(c, 140) << 1);
                        val +=       vp56_rac_get_prob(c, 135);
                    }
                } else if (!vp56_rac_get_prob_branchy(c, tp[10])) {
                    val  = (vp56_rac_get_prob(c, 180) << 4) + 35;
                    val += (vp56_rac_get_prob(c, 157) << 3);
                    val += (vp56_rac_get_prob(c, 141) << 2);
                    val += (vp56_rac_get_prob(c, 134) << 1);
                    val +=  vp56_rac_get_prob(c, 130);
                } else {
                    val = 67;
                    if (!is8bitsperpixel) {
                        if (bpp == 12) {
                            val += vp56_rac_get_prob(c, 255) << 17;
                            val += vp56_rac_get_prob(c, 255) << 16;
                        }
                        val +=  (vp56_rac_get_prob(c, 255) << 15);
                        val +=  (vp56_rac_get_prob(c, 255) << 14);
                    }
                    val += (vp56_rac_get_prob(c, 254) << 13);
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
#define STORE_COEF(c, i, v) do { \
    if (is8bitsperpixel) { \
        c[i] = v; \
    } else { \
        AV_WN32A(&c[i * 2], v); \
    } \
} while (0)
        if (!--band_left)
            band_left = band_counts[++band];
        if (is_tx32x32)
            STORE_COEF(coef, rc, (int)((vp8_rac_get(c) ? -val : val) * (unsigned)qmul[!!i]) / 2);
        else
            STORE_COEF(coef, rc, (vp8_rac_get(c) ? -val : val) * (unsigned)qmul[!!i]);
        nnz = (1 + cache[nb[i][0]] + cache[nb[i][1]]) >> 1;
        tp = p[band][nnz];
    } while (++i < n_coeffs);

    return i;
}

static int decode_coeffs_b_8bpp(VP9TileData *td, int16_t *coef, int n_coeffs,
                                unsigned (*cnt)[6][3], unsigned (*eob)[6][2],
                                uint8_t (*p)[6][11], int nnz, const int16_t *scan,
                                const int16_t (*nb)[2], const int16_t *band_counts,
                                int16_t *qmul)
{
    return decode_coeffs_b_generic(td->c, coef, n_coeffs, 0, 1, 8, cnt, eob, p,
                                   nnz, scan, nb, band_counts, qmul);
}

static int decode_coeffs_b32_8bpp(VP9TileData *td, int16_t *coef, int n_coeffs,
                                  unsigned (*cnt)[6][3], unsigned (*eob)[6][2],
                                  uint8_t (*p)[6][11], int nnz, const int16_t *scan,
                                  const int16_t (*nb)[2], const int16_t *band_counts,
                                  int16_t *qmul)
{
    return decode_coeffs_b_generic(td->c, coef, n_coeffs, 1, 1, 8, cnt, eob, p,
                                   nnz, scan, nb, band_counts, qmul);
}

static int decode_coeffs_b_16bpp(VP9TileData *td, int16_t *coef, int n_coeffs,
                                 unsigned (*cnt)[6][3], unsigned (*eob)[6][2],
                                 uint8_t (*p)[6][11], int nnz, const int16_t *scan,
                                 const int16_t (*nb)[2], const int16_t *band_counts,
                                 int16_t *qmul)
{
    return decode_coeffs_b_generic(td->c, coef, n_coeffs, 0, 0, td->s->s.h.bpp, cnt, eob, p,
                                   nnz, scan, nb, band_counts, qmul);
}

static int decode_coeffs_b32_16bpp(VP9TileData *td, int16_t *coef, int n_coeffs,
                                   unsigned (*cnt)[6][3], unsigned (*eob)[6][2],
                                   uint8_t (*p)[6][11], int nnz, const int16_t *scan,
                                   const int16_t (*nb)[2], const int16_t *band_counts,
                                   int16_t *qmul)
{
    return decode_coeffs_b_generic(td->c, coef, n_coeffs, 1, 0, td->s->s.h.bpp, cnt, eob, p,
                                   nnz, scan, nb, band_counts, qmul);
}

static av_always_inline int decode_coeffs(VP9TileData *td, int is8bitsperpixel)
{
    VP9Context *s = td->s;
    VP9Block *b = td->b;
    int row = td->row, col = td->col;
    uint8_t (*p)[6][11] = s->prob.coef[b->tx][0 /* y */][!b->intra];
    unsigned (*c)[6][3] = td->counts.coef[b->tx][0 /* y */][!b->intra];
    unsigned (*e)[6][2] = td->counts.eob[b->tx][0 /* y */][!b->intra];
    int w4 = ff_vp9_bwh_tab[1][b->bs][0] << 1, h4 = ff_vp9_bwh_tab[1][b->bs][1] << 1;
    int end_x = FFMIN(2 * (s->cols - col), w4);
    int end_y = FFMIN(2 * (s->rows - row), h4);
    int n, pl, x, y, ret;
    int16_t (*qmul)[2] = s->s.h.segmentation.feat[b->seg_id].qmul;
    int tx = 4 * s->s.h.lossless + b->tx;
    const int16_t * const *yscans = ff_vp9_scans[tx];
    const int16_t (* const * ynbs)[2] = ff_vp9_scans_nb[tx];
    const int16_t *uvscan = ff_vp9_scans[b->uvtx][DCT_DCT];
    const int16_t (*uvnb)[2] = ff_vp9_scans_nb[b->uvtx][DCT_DCT];
    uint8_t *a = &s->above_y_nnz_ctx[col * 2];
    uint8_t *l = &td->left_y_nnz_ctx[(row & 7) << 1];
    static const int16_t band_counts[4][8] = {
        { 1, 2, 3, 4,  3,   16 - 13 },
        { 1, 2, 3, 4, 11,   64 - 21 },
        { 1, 2, 3, 4, 11,  256 - 21 },
        { 1, 2, 3, 4, 11, 1024 - 21 },
    };
    const int16_t *y_band_counts = band_counts[b->tx];
    const int16_t *uv_band_counts = band_counts[b->uvtx];
    int bytesperpixel = is8bitsperpixel ? 1 : 2;
    int total_coeff = 0;

#define MERGE(la, end, step, rd) \
    for (n = 0; n < end; n += step) \
        la[n] = !!rd(&la[n])
#define MERGE_CTX(step, rd) \
    do { \
        MERGE(l, end_y, step, rd); \
        MERGE(a, end_x, step, rd); \
    } while (0)

#define DECODE_Y_COEF_LOOP(step, mode_index, v) \
    for (n = 0, y = 0; y < end_y; y += step) { \
        for (x = 0; x < end_x; x += step, n += step * step) { \
            enum TxfmType txtp = ff_vp9_intra_txfm_type[b->mode[mode_index]]; \
            ret = (is8bitsperpixel ? decode_coeffs_b##v##_8bpp : decode_coeffs_b##v##_16bpp) \
                                    (td, td->block + 16 * n * bytesperpixel, 16 * step * step, \
                                     c, e, p, a[x] + l[y], yscans[txtp], \
                                     ynbs[txtp], y_band_counts, qmul[0]); \
            a[x] = l[y] = !!ret; \
            total_coeff |= !!ret; \
            if (step >= 4) { \
                AV_WN16A(&td->eob[n], ret); \
            } else { \
                td->eob[n] = ret; \
            } \
        } \
    }

#define SPLAT(la, end, step, cond) \
    if (step == 2) { \
        for (n = 1; n < end; n += step) \
            la[n] = la[n - 1]; \
    } else if (step == 4) { \
        if (cond) { \
            for (n = 0; n < end; n += step) \
                AV_WN32A(&la[n], la[n] * 0x01010101); \
        } else { \
            for (n = 0; n < end; n += step) \
                memset(&la[n + 1], la[n], FFMIN(end - n - 1, 3)); \
        } \
    } else /* step == 8 */ { \
        if (cond) { \
            if (HAVE_FAST_64BIT) { \
                for (n = 0; n < end; n += step) \
                    AV_WN64A(&la[n], la[n] * 0x0101010101010101ULL); \
            } else { \
                for (n = 0; n < end; n += step) { \
                    uint32_t v32 = la[n] * 0x01010101; \
                    AV_WN32A(&la[n],     v32); \
                    AV_WN32A(&la[n + 4], v32); \
                } \
            } \
        } else { \
            for (n = 0; n < end; n += step) \
                memset(&la[n + 1], la[n], FFMIN(end - n - 1, 7)); \
        } \
    }
#define SPLAT_CTX(step) \
    do { \
        SPLAT(a, end_x, step, end_x == w4); \
        SPLAT(l, end_y, step, end_y == h4); \
    } while (0)

    /* y tokens */
    switch (b->tx) {
    case TX_4X4:
        DECODE_Y_COEF_LOOP(1, b->bs > BS_8x8 ? n : 0,);
        break;
    case TX_8X8:
        MERGE_CTX(2, AV_RN16A);
        DECODE_Y_COEF_LOOP(2, 0,);
        SPLAT_CTX(2);
        break;
    case TX_16X16:
        MERGE_CTX(4, AV_RN32A);
        DECODE_Y_COEF_LOOP(4, 0,);
        SPLAT_CTX(4);
        break;
    case TX_32X32:
        MERGE_CTX(8, AV_RN64A);
        DECODE_Y_COEF_LOOP(8, 0, 32);
        SPLAT_CTX(8);
        break;
    }

#define DECODE_UV_COEF_LOOP(step, v) \
    for (n = 0, y = 0; y < end_y; y += step) { \
        for (x = 0; x < end_x; x += step, n += step * step) { \
            ret = (is8bitsperpixel ? decode_coeffs_b##v##_8bpp : decode_coeffs_b##v##_16bpp) \
                                    (td, td->uvblock[pl] + 16 * n * bytesperpixel, \
                                     16 * step * step, c, e, p, a[x] + l[y], \
                                     uvscan, uvnb, uv_band_counts, qmul[1]); \
            a[x] = l[y] = !!ret; \
            total_coeff |= !!ret; \
            if (step >= 4) { \
                AV_WN16A(&td->uveob[pl][n], ret); \
            } else { \
                td->uveob[pl][n] = ret; \
            } \
        } \
    }

    p = s->prob.coef[b->uvtx][1 /* uv */][!b->intra];
    c = td->counts.coef[b->uvtx][1 /* uv */][!b->intra];
    e = td->counts.eob[b->uvtx][1 /* uv */][!b->intra];
    w4 >>= s->ss_h;
    end_x >>= s->ss_h;
    h4 >>= s->ss_v;
    end_y >>= s->ss_v;
    for (pl = 0; pl < 2; pl++) {
        a = &s->above_uv_nnz_ctx[pl][col << !s->ss_h];
        l = &td->left_uv_nnz_ctx[pl][(row & 7) << !s->ss_v];
        switch (b->uvtx) {
        case TX_4X4:
            DECODE_UV_COEF_LOOP(1,);
            break;
        case TX_8X8:
            MERGE_CTX(2, AV_RN16A);
            DECODE_UV_COEF_LOOP(2,);
            SPLAT_CTX(2);
            break;
        case TX_16X16:
            MERGE_CTX(4, AV_RN32A);
            DECODE_UV_COEF_LOOP(4,);
            SPLAT_CTX(4);
            break;
        case TX_32X32:
            MERGE_CTX(8, AV_RN64A);
            DECODE_UV_COEF_LOOP(8, 32);
            SPLAT_CTX(8);
            break;
        }
    }

    return total_coeff;
}

static int decode_coeffs_8bpp(VP9TileData *td)
{
    return decode_coeffs(td, 1);
}

static int decode_coeffs_16bpp(VP9TileData *td)
{
    return decode_coeffs(td, 0);
}

static av_always_inline void mask_edges(uint8_t (*mask)[8][4], int ss_h, int ss_v,
                                        int row_and_7, int col_and_7,
                                        int w, int h, int col_end, int row_end,
                                        enum TxfmMode tx, int skip_inter)
{
    static const unsigned wide_filter_col_mask[2] = { 0x11, 0x01 };
    static const unsigned wide_filter_row_mask[2] = { 0x03, 0x07 };

    // FIXME I'm pretty sure all loops can be replaced by a single LUT if
    // we make VP9Filter.mask uint64_t (i.e. row/col all single variable)
    // and make the LUT 5-indexed (bl, bp, is_uv, tx and row/col), and then
    // use row_and_7/col_and_7 as shifts (1*col_and_7+8*row_and_7)

    // the intended behaviour of the vp9 loopfilter is to work on 8-pixel
    // edges. This means that for UV, we work on two subsampled blocks at
    // a time, and we only use the topleft block's mode information to set
    // things like block strength. Thus, for any block size smaller than
    // 16x16, ignore the odd portion of the block.
    if (tx == TX_4X4 && (ss_v | ss_h)) {
        if (h == ss_v) {
            if (row_and_7 & 1)
                return;
            if (!row_end)
                h += 1;
        }
        if (w == ss_h) {
            if (col_and_7 & 1)
                return;
            if (!col_end)
                w += 1;
        }
    }

    if (tx == TX_4X4 && !skip_inter) {
        int t = 1 << col_and_7, m_col = (t << w) - t, y;
        // on 32-px edges, use the 8-px wide loopfilter; else, use 4-px wide
        int m_row_8 = m_col & wide_filter_col_mask[ss_h], m_row_4 = m_col - m_row_8;

        for (y = row_and_7; y < h + row_and_7; y++) {
            int col_mask_id = 2 - !(y & wide_filter_row_mask[ss_v]);

            mask[0][y][1] |= m_row_8;
            mask[0][y][2] |= m_row_4;
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
            if ((ss_h & ss_v) && (col_end & 1) && (y & 1)) {
                mask[1][y][col_mask_id] |= (t << (w - 1)) - t;
            } else {
                mask[1][y][col_mask_id] |= m_col;
            }
            if (!ss_h)
                mask[0][y][3] |= m_col;
            if (!ss_v) {
                if (ss_h && (col_end & 1))
                    mask[1][y][3] |= (t << (w - 1)) - t;
                else
                    mask[1][y][3] |= m_col;
            }
        }
    } else {
        int y, t = 1 << col_and_7, m_col = (t << w) - t;

        if (!skip_inter) {
            int mask_id = (tx == TX_8X8);
            int l2 = tx + ss_h - 1, step1d;
            static const unsigned masks[4] = { 0xff, 0x55, 0x11, 0x01 };
            int m_row = m_col & masks[l2];

            // at odd UV col/row edges tx16/tx32 loopfilter edges, force
            // 8wd loopfilter to prevent going off the visible edge.
            if (ss_h && tx > TX_8X8 && (w ^ (w - 1)) == 1) {
                int m_row_16 = ((t << (w - 1)) - t) & masks[l2];
                int m_row_8 = m_row - m_row_16;

                for (y = row_and_7; y < h + row_and_7; y++) {
                    mask[0][y][0] |= m_row_16;
                    mask[0][y][1] |= m_row_8;
                }
            } else {
                for (y = row_and_7; y < h + row_and_7; y++)
                    mask[0][y][mask_id] |= m_row;
            }

            l2 = tx + ss_v - 1;
            step1d = 1 << l2;
            if (ss_v && tx > TX_8X8 && (h ^ (h - 1)) == 1) {
                for (y = row_and_7; y < h + row_and_7 - 1; y += step1d)
                    mask[1][y][0] |= m_col;
                if (y - row_and_7 == h - 1)
                    mask[1][y][1] |= m_col;
            } else {
                for (y = row_and_7; y < h + row_and_7; y += step1d)
                    mask[1][y][mask_id] |= m_col;
            }
        } else if (tx != TX_4X4) {
            int mask_id;

            mask_id = (tx == TX_8X8) || (h == ss_v);
            mask[1][row_and_7][mask_id] |= m_col;
            mask_id = (tx == TX_8X8) || (w == ss_h);
            for (y = row_and_7; y < h + row_and_7; y++)
                mask[0][y][mask_id] |= t;
        } else {
            int t8 = t & wide_filter_col_mask[ss_h], t4 = t - t8;

            for (y = row_and_7; y < h + row_and_7; y++) {
                mask[0][y][2] |= t4;
                mask[0][y][1] |= t8;
            }
            mask[1][row_and_7][2 - !(row_and_7 & wide_filter_row_mask[ss_v])] |= m_col;
        }
    }
}

void ff_vp9_decode_block(VP9TileData *td, int row, int col,
                         VP9Filter *lflvl, ptrdiff_t yoff, ptrdiff_t uvoff,
                         enum BlockLevel bl, enum BlockPartition bp)
{
    VP9Context *s = td->s;
    VP9Block *b = td->b;
    enum BlockSize bs = bl * 3 + bp;
    int bytesperpixel = s->bytesperpixel;
    int w4 = ff_vp9_bwh_tab[1][bs][0], h4 = ff_vp9_bwh_tab[1][bs][1], lvl;
    int emu[2];
    AVFrame *f = s->s.frames[CUR_FRAME].tf.f;

    td->row = row;
    td->row7 = row & 7;
    td->col = col;
    td->col7 = col & 7;

    td->min_mv.x = -(128 + col * 64);
    td->min_mv.y = -(128 + row * 64);
    td->max_mv.x = 128 + (s->cols - col - w4) * 64;
    td->max_mv.y = 128 + (s->rows - row - h4) * 64;

    if (s->pass < 2) {
        b->bs = bs;
        b->bl = bl;
        b->bp = bp;
        decode_mode(td);
        b->uvtx = b->tx - ((s->ss_h && w4 * 2 == (1 << b->tx)) ||
                           (s->ss_v && h4 * 2 == (1 << b->tx)));

        if (td->block_structure) {
            td->block_structure[td->nb_block_structure].row = row;
            td->block_structure[td->nb_block_structure].col = col;
            td->block_structure[td->nb_block_structure].block_size_idx_x = av_log2(w4);
            td->block_structure[td->nb_block_structure].block_size_idx_y = av_log2(h4);
            td->nb_block_structure++;
        }

        if (!b->skip) {
            int has_coeffs;

            if (bytesperpixel == 1) {
                has_coeffs = decode_coeffs_8bpp(td);
            } else {
                has_coeffs = decode_coeffs_16bpp(td);
            }
            if (!has_coeffs && b->bs <= BS_8x8 && !b->intra) {
                b->skip = 1;
                memset(&s->above_skip_ctx[col], 1, w4);
                memset(&td->left_skip_ctx[td->row7], 1, h4);
            }
        } else {
            int row7 = td->row7;

#define SPLAT_ZERO_CTX(v, n) \
    switch (n) { \
    case 1:  v = 0;          break; \
    case 2:  AV_ZERO16(&v);  break; \
    case 4:  AV_ZERO32(&v);  break; \
    case 8:  AV_ZERO64(&v);  break; \
    case 16: AV_ZERO128(&v); break; \
    }
#define SPLAT_ZERO_YUV(dir, var, off, n, dir2) \
    do { \
        SPLAT_ZERO_CTX(dir##_y_##var[off * 2], n * 2); \
        if (s->ss_##dir2) { \
            SPLAT_ZERO_CTX(dir##_uv_##var[0][off], n); \
            SPLAT_ZERO_CTX(dir##_uv_##var[1][off], n); \
        } else { \
            SPLAT_ZERO_CTX(dir##_uv_##var[0][off * 2], n * 2); \
            SPLAT_ZERO_CTX(dir##_uv_##var[1][off * 2], n * 2); \
        } \
    } while (0)

            switch (w4) {
            case 1: SPLAT_ZERO_YUV(s->above, nnz_ctx, col, 1, h); break;
            case 2: SPLAT_ZERO_YUV(s->above, nnz_ctx, col, 2, h); break;
            case 4: SPLAT_ZERO_YUV(s->above, nnz_ctx, col, 4, h); break;
            case 8: SPLAT_ZERO_YUV(s->above, nnz_ctx, col, 8, h); break;
            }
            switch (h4) {
            case 1: SPLAT_ZERO_YUV(td->left, nnz_ctx, row7, 1, v); break;
            case 2: SPLAT_ZERO_YUV(td->left, nnz_ctx, row7, 2, v); break;
            case 4: SPLAT_ZERO_YUV(td->left, nnz_ctx, row7, 4, v); break;
            case 8: SPLAT_ZERO_YUV(td->left, nnz_ctx, row7, 8, v); break;
            }
        }

        if (s->pass == 1) {
            s->td[0].b++;
            s->td[0].block += w4 * h4 * 64 * bytesperpixel;
            s->td[0].uvblock[0] += w4 * h4 * 64 * bytesperpixel >> (s->ss_h + s->ss_v);
            s->td[0].uvblock[1] += w4 * h4 * 64 * bytesperpixel >> (s->ss_h + s->ss_v);
            s->td[0].eob += 4 * w4 * h4;
            s->td[0].uveob[0] += 4 * w4 * h4 >> (s->ss_h + s->ss_v);
            s->td[0].uveob[1] += 4 * w4 * h4 >> (s->ss_h + s->ss_v);

            return;
        }
    }

    // emulated overhangs if the stride of the target buffer can't hold. This
    // makes it possible to support emu-edge and so on even if we have large block
    // overhangs
    emu[0] = (col + w4) * 8 * bytesperpixel > f->linesize[0] ||
             (row + h4) > s->rows;
    emu[1] = ((col + w4) * 8 >> s->ss_h) * bytesperpixel > f->linesize[1] ||
             (row + h4) > s->rows;
    if (emu[0]) {
        td->dst[0] = td->tmp_y;
        td->y_stride = 128;
    } else {
        td->dst[0] = f->data[0] + yoff;
        td->y_stride = f->linesize[0];
    }
    if (emu[1]) {
        td->dst[1] = td->tmp_uv[0];
        td->dst[2] = td->tmp_uv[1];
        td->uv_stride = 128;
    } else {
        td->dst[1] = f->data[1] + uvoff;
        td->dst[2] = f->data[2] + uvoff;
        td->uv_stride = f->linesize[1];
    }
    if (b->intra) {
        if (s->s.h.bpp > 8) {
            ff_vp9_intra_recon_16bpp(td, yoff, uvoff);
        } else {
            ff_vp9_intra_recon_8bpp(td, yoff, uvoff);
        }
    } else {
        if (s->s.h.bpp > 8) {
            ff_vp9_inter_recon_16bpp(td);
        } else {
            ff_vp9_inter_recon_8bpp(td);
        }
    }
    if (emu[0]) {
        int w = FFMIN(s->cols - col, w4) * 8, h = FFMIN(s->rows - row, h4) * 8, n, o = 0;

        for (n = 0; o < w; n++) {
            int bw = 64 >> n;

            av_assert2(n <= 4);
            if (w & bw) {
                s->dsp.mc[n][0][0][0][0](f->data[0] + yoff + o * bytesperpixel, f->linesize[0],
                                         td->tmp_y + o * bytesperpixel, 128, h, 0, 0);
                o += bw;
            }
        }
    }
    if (emu[1]) {
        int w = FFMIN(s->cols - col, w4) * 8 >> s->ss_h;
        int h = FFMIN(s->rows - row, h4) * 8 >> s->ss_v, n, o = 0;

        for (n = s->ss_h; o < w; n++) {
            int bw = 64 >> n;

            av_assert2(n <= 4);
            if (w & bw) {
                s->dsp.mc[n][0][0][0][0](f->data[1] + uvoff + o * bytesperpixel, f->linesize[1],
                                         td->tmp_uv[0] + o * bytesperpixel, 128, h, 0, 0);
                s->dsp.mc[n][0][0][0][0](f->data[2] + uvoff + o * bytesperpixel, f->linesize[2],
                                         td->tmp_uv[1] + o * bytesperpixel, 128, h, 0, 0);
                o += bw;
            }
        }
    }

    // pick filter level and find edges to apply filter to
    if (s->s.h.filter.level &&
        (lvl = s->s.h.segmentation.feat[b->seg_id].lflvl[b->intra ? 0 : b->ref[0] + 1]
                                                      [b->mode[3] != ZEROMV]) > 0) {
        int x_end = FFMIN(s->cols - col, w4), y_end = FFMIN(s->rows - row, h4);
        int skip_inter = !b->intra && b->skip, col7 = td->col7, row7 = td->row7;

        setctx_2d(&lflvl->level[row7 * 8 + col7], w4, h4, 8, lvl);
        mask_edges(lflvl->mask[0], 0, 0, row7, col7, x_end, y_end, 0, 0, b->tx, skip_inter);
        if (s->ss_h || s->ss_v)
            mask_edges(lflvl->mask[1], s->ss_h, s->ss_v, row7, col7, x_end, y_end,
                       s->cols & 1 && col + w4 >= s->cols ? s->cols & 7 : 0,
                       s->rows & 1 && row + h4 >= s->rows ? s->rows & 7 : 0,
                       b->uvtx, skip_inter);
    }

    if (s->pass == 2) {
        s->td[0].b++;
        s->td[0].block += w4 * h4 * 64 * bytesperpixel;
        s->td[0].uvblock[0] += w4 * h4 * 64 * bytesperpixel >> (s->ss_v + s->ss_h);
        s->td[0].uvblock[1] += w4 * h4 * 64 * bytesperpixel >> (s->ss_v + s->ss_h);
        s->td[0].eob += 4 * w4 * h4;
        s->td[0].uveob[0] += 4 * w4 * h4 >> (s->ss_v + s->ss_h);
        s->td[0].uveob[1] += 4 * w4 * h4 >> (s->ss_v + s->ss_h);
    }
}
