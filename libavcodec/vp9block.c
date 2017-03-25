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

static const uint8_t bwh_tab[2][N_BS_SIZES][2] = {
    {
        { 16, 16 }, { 16, 8 }, { 8, 16 }, { 8, 8 }, { 8, 4 }, { 4, 8 },
        { 4, 4 }, { 4, 2 }, { 2, 4 }, { 2, 2 }, { 2, 1 }, { 1, 2 }, { 1, 1 },
    }, {
        { 8, 8 }, { 8, 4 }, { 4, 8 }, { 4, 4 }, { 4, 2 }, { 2, 4 },
        { 2, 2 }, { 2, 1 }, { 1, 2 }, { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 },
    }
};

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

static void decode_mode(AVCodecContext *ctx)
{
    static const uint8_t left_ctx[N_BS_SIZES] = {
        0x0, 0x8, 0x0, 0x8, 0xc, 0x8, 0xc, 0xe, 0xc, 0xe, 0xf, 0xe, 0xf
    };
    static const uint8_t above_ctx[N_BS_SIZES] = {
        0x0, 0x0, 0x8, 0x8, 0x8, 0xc, 0xc, 0xc, 0xe, 0xe, 0xe, 0xf, 0xf
    };
    static const uint8_t max_tx_for_bl_bp[N_BS_SIZES] = {
        TX_32X32, TX_32X32, TX_32X32, TX_32X32, TX_16X16, TX_16X16,
        TX_16X16, TX_8X8, TX_8X8, TX_8X8, TX_4X4, TX_4X4, TX_4X4
    };
    VP9Context *s = ctx->priv_data;
    VP9Block *b = s->b;
    int row = s->row, col = s->col, row7 = s->row7;
    enum TxfmMode max_tx = max_tx_for_bl_bp[b->bs];
    int bw4 = bwh_tab[1][b->bs][0], w4 = FFMIN(s->cols - col, bw4);
    int bh4 = bwh_tab[1][b->bs][1], h4 = FFMIN(s->rows - row, bh4), y;
    int have_a = row > 0, have_l = col > s->tile_col_start;
    int vref, filter_id;

    if (!s->s.h.segmentation.enabled) {
        b->seg_id = 0;
    } else if (s->s.h.keyframe || s->s.h.intraonly) {
        b->seg_id = !s->s.h.segmentation.update_map ? 0 :
                    vp8_rac_get_tree(&s->c, ff_vp9_segmentation_tree, s->s.h.segmentation.prob);
    } else if (!s->s.h.segmentation.update_map ||
               (s->s.h.segmentation.temporal &&
                vp56_rac_get_prob_branchy(&s->c,
                    s->s.h.segmentation.pred_prob[s->above_segpred_ctx[col] +
                                    s->left_segpred_ctx[row7]]))) {
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
        memset(&s->left_segpred_ctx[row7], 1, h4);
    } else {
        b->seg_id = vp8_rac_get_tree(&s->c, ff_vp9_segmentation_tree,
                                     s->s.h.segmentation.prob);

        memset(&s->above_segpred_ctx[col], 0, w4);
        memset(&s->left_segpred_ctx[row7], 0, h4);
    }
    if (s->s.h.segmentation.enabled &&
        (s->s.h.segmentation.update_map || s->s.h.keyframe || s->s.h.intraonly)) {
        setctx_2d(&s->s.frames[CUR_FRAME].segmentation_map[row * 8 * s->sb_cols + col],
                  bw4, bh4, 8 * s->sb_cols, b->seg_id);
    }

    b->skip = s->s.h.segmentation.enabled &&
        s->s.h.segmentation.feat[b->seg_id].skip_enabled;
    if (!b->skip) {
        int c = s->left_skip_ctx[row7] + s->above_skip_ctx[col];
        b->skip = vp56_rac_get_prob(&s->c, s->prob.p.skip[c]);
        s->counts.skip[c][b->skip]++;
    }

    if (s->s.h.keyframe || s->s.h.intraonly) {
        b->intra = 1;
    } else if (s->s.h.segmentation.enabled && s->s.h.segmentation.feat[b->seg_id].ref_enabled) {
        b->intra = !s->s.h.segmentation.feat[b->seg_id].ref_val;
    } else {
        int c, bit;

        if (have_a && have_l) {
            c = s->above_intra_ctx[col] + s->left_intra_ctx[row7];
            c += (c == 2);
        } else {
            c = have_a ? 2 * s->above_intra_ctx[col] :
                have_l ? 2 * s->left_intra_ctx[row7] : 0;
        }
        bit = vp56_rac_get_prob(&s->c, s->prob.p.intra[c]);
        s->counts.intra[c][bit]++;
        b->intra = !bit;
    }

    if ((b->intra || !b->skip) && s->s.h.txfmmode == TX_SWITCHABLE) {
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
        b->tx = FFMIN(max_tx, s->s.h.txfmmode);
    }

    if (s->s.h.keyframe || s->s.h.intraonly) {
        uint8_t *a = &s->above_mode_ctx[col * 2];
        uint8_t *l = &s->left_mode_ctx[(row7) << 1];

        b->comp = 0;
        if (b->bs > BS_8x8) {
            // FIXME the memory storage intermediates here aren't really
            // necessary, they're just there to make the code slightly
            // simpler for now
            b->mode[0] = a[0] = vp8_rac_get_tree(&s->c, ff_vp9_intramode_tree,
                                    ff_vp9_default_kf_ymode_probs[a[0]][l[0]]);
            if (b->bs != BS_8x4) {
                b->mode[1] = vp8_rac_get_tree(&s->c, ff_vp9_intramode_tree,
                                 ff_vp9_default_kf_ymode_probs[a[1]][b->mode[0]]);
                l[0] = a[1] = b->mode[1];
            } else {
                l[0] = a[1] = b->mode[1] = b->mode[0];
            }
            if (b->bs != BS_4x8) {
                b->mode[2] = a[0] = vp8_rac_get_tree(&s->c, ff_vp9_intramode_tree,
                                        ff_vp9_default_kf_ymode_probs[a[0]][l[1]]);
                if (b->bs != BS_8x4) {
                    b->mode[3] = vp8_rac_get_tree(&s->c, ff_vp9_intramode_tree,
                                  ff_vp9_default_kf_ymode_probs[a[1]][b->mode[2]]);
                    l[1] = a[1] = b->mode[3];
                } else {
                    l[1] = a[1] = b->mode[3] = b->mode[2];
                }
            } else {
                b->mode[2] = b->mode[0];
                l[1] = a[1] = b->mode[3] = b->mode[1];
            }
        } else {
            b->mode[0] = vp8_rac_get_tree(&s->c, ff_vp9_intramode_tree,
                                          ff_vp9_default_kf_ymode_probs[*a][*l]);
            b->mode[3] = b->mode[2] = b->mode[1] = b->mode[0];
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
            b->mode[1] = b->mode[2] = b->mode[3] = b->mode[0];
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
                        if (s->above_comp_ctx[col] && s->left_comp_ctx[row7]) {
                            c = 4;
                        } else if (s->above_comp_ctx[col]) {
                            c = 2 + (s->left_intra_ctx[row7] ||
                                     s->left_ref_ctx[row7] == s->s.h.fixcompref);
                        } else if (s->left_comp_ctx[row7]) {
                            c = 2 + (s->above_intra_ctx[col] ||
                                     s->above_ref_ctx[col] == s->s.h.fixcompref);
                        } else {
                            c = (!s->above_intra_ctx[col] &&
                                 s->above_ref_ctx[col] == s->s.h.fixcompref) ^
                            (!s->left_intra_ctx[row7] &&
                             s->left_ref_ctx[row & 7] == s->s.h.fixcompref);
                        }
                    } else {
                        c = s->above_comp_ctx[col] ? 3 :
                        (!s->above_intra_ctx[col] && s->above_ref_ctx[col] == s->s.h.fixcompref);
                    }
                } else if (have_l) {
                    c = s->left_comp_ctx[row7] ? 3 :
                    (!s->left_intra_ctx[row7] && s->left_ref_ctx[row7] == s->s.h.fixcompref);
                } else {
                    c = 1;
                }
                b->comp = vp56_rac_get_prob(&s->c, s->prob.p.comp[c]);
                s->counts.comp[c][b->comp]++;
            }

            // read actual references
            // FIXME probably cache a few variables here to prevent repetitive
            // memory accesses below
            if (b->comp) /* two references */ {
                int fix_idx = s->s.h.signbias[s->s.h.fixcompref], var_idx = !fix_idx, c, bit;

                b->ref[fix_idx] = s->s.h.fixcompref;
                // FIXME can this codeblob be replaced by some sort of LUT?
                if (have_a) {
                    if (have_l) {
                        if (s->above_intra_ctx[col]) {
                            if (s->left_intra_ctx[row7]) {
                                c = 2;
                            } else {
                                c = 1 + 2 * (s->left_ref_ctx[row7] != s->s.h.varcompref[1]);
                            }
                        } else if (s->left_intra_ctx[row7]) {
                            c = 1 + 2 * (s->above_ref_ctx[col] != s->s.h.varcompref[1]);
                        } else {
                            int refl = s->left_ref_ctx[row7], refa = s->above_ref_ctx[col];

                            if (refl == refa && refa == s->s.h.varcompref[1]) {
                                c = 0;
                            } else if (!s->left_comp_ctx[row7] && !s->above_comp_ctx[col]) {
                                if ((refa == s->s.h.fixcompref && refl == s->s.h.varcompref[0]) ||
                                    (refl == s->s.h.fixcompref && refa == s->s.h.varcompref[0])) {
                                    c = 4;
                                } else {
                                    c = (refa == refl) ? 3 : 1;
                                }
                            } else if (!s->left_comp_ctx[row7]) {
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
                    if (s->left_intra_ctx[row7]) {
                        c = 2;
                    } else if (s->left_comp_ctx[row7]) {
                        c = 4 * (s->left_ref_ctx[row7] != s->s.h.varcompref[1]);
                    } else {
                        c = 3 * (s->left_ref_ctx[row7] != s->s.h.varcompref[1]);
                    }
                } else {
                    c = 2;
                }
                bit = vp56_rac_get_prob(&s->c, s->prob.p.comp_ref[c]);
                b->ref[var_idx] = s->s.h.varcompref[bit];
                s->counts.comp_ref[c][bit]++;
            } else /* single reference */ {
                int bit, c;

                if (have_a && !s->above_intra_ctx[col]) {
                    if (have_l && !s->left_intra_ctx[row7]) {
                        if (s->left_comp_ctx[row7]) {
                            if (s->above_comp_ctx[col]) {
                                c = 1 + (!s->s.h.fixcompref || !s->left_ref_ctx[row7] ||
                                         !s->above_ref_ctx[col]);
                            } else {
                                c = (3 * !s->above_ref_ctx[col]) +
                                    (!s->s.h.fixcompref || !s->left_ref_ctx[row7]);
                            }
                        } else if (s->above_comp_ctx[col]) {
                            c = (3 * !s->left_ref_ctx[row7]) +
                                (!s->s.h.fixcompref || !s->above_ref_ctx[col]);
                        } else {
                            c = 2 * !s->left_ref_ctx[row7] + 2 * !s->above_ref_ctx[col];
                        }
                    } else if (s->above_intra_ctx[col]) {
                        c = 2;
                    } else if (s->above_comp_ctx[col]) {
                        c = 1 + (!s->s.h.fixcompref || !s->above_ref_ctx[col]);
                    } else {
                        c = 4 * (!s->above_ref_ctx[col]);
                    }
                } else if (have_l && !s->left_intra_ctx[row7]) {
                    if (s->left_intra_ctx[row7]) {
                        c = 2;
                    } else if (s->left_comp_ctx[row7]) {
                        c = 1 + (!s->s.h.fixcompref || !s->left_ref_ctx[row7]);
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
                                    c = 1 + 2 * (s->s.h.fixcompref == 1 ||
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
                                    c = 1 + 2 * (s->s.h.fixcompref == 1 ||
                                                 s->left_ref_ctx[row7] == 1);
                                } else if (!s->left_ref_ctx[row7]) {
                                    c = 3;
                                } else {
                                    c = 4 * (s->left_ref_ctx[row7] == 1);
                                }
                            } else if (s->above_comp_ctx[col]) {
                                if (s->left_comp_ctx[row7]) {
                                    if (s->left_ref_ctx[row7] == s->above_ref_ctx[col]) {
                                        c = 3 * (s->s.h.fixcompref == 1 ||
                                                 s->left_ref_ctx[row7] == 1);
                                    } else {
                                        c = 2;
                                    }
                                } else if (!s->left_ref_ctx[row7]) {
                                    c = 1 + 2 * (s->s.h.fixcompref == 1 ||
                                                 s->above_ref_ctx[col] == 1);
                                } else {
                                    c = 3 * (s->left_ref_ctx[row7] == 1) +
                                    (s->s.h.fixcompref == 1 || s->above_ref_ctx[col] == 1);
                                }
                            } else if (s->left_comp_ctx[row7]) {
                                if (!s->above_ref_ctx[col]) {
                                    c = 1 + 2 * (s->s.h.fixcompref == 1 ||
                                                 s->left_ref_ctx[row7] == 1);
                                } else {
                                    c = 3 * (s->above_ref_ctx[col] == 1) +
                                    (s->s.h.fixcompref == 1 || s->left_ref_ctx[row7] == 1);
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
                                c = 3 * (s->s.h.fixcompref == 1 || s->above_ref_ctx[col] == 1);
                            } else {
                                c = 4 * (s->above_ref_ctx[col] == 1);
                            }
                        }
                    } else if (have_l) {
                        if (s->left_intra_ctx[row7] ||
                            (!s->left_comp_ctx[row7] && !s->left_ref_ctx[row7])) {
                            c = 2;
                        } else if (s->left_comp_ctx[row7]) {
                            c = 3 * (s->s.h.fixcompref == 1 || s->left_ref_ctx[row7] == 1);
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
            if (s->s.h.segmentation.enabled && s->s.h.segmentation.feat[b->seg_id].skip_enabled) {
                b->mode[0] = b->mode[1] = b->mode[2] = b->mode[3] = ZEROMV;
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
                b->mode[1] = b->mode[2] = b->mode[3] = b->mode[0];
                s->counts.mv_mode[c][b->mode[0] - 10]++;
            }
        }

        if (s->s.h.filtermode == FILTER_SWITCHABLE) {
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

            filter_id = vp8_rac_get_tree(&s->c, ff_vp9_filter_tree,
                                         s->prob.p.filter[c]);
            s->counts.filter[c][filter_id]++;
            b->filter = ff_vp9_filter_lut[filter_id];
        } else {
            b->filter = s->s.h.filtermode;
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

    switch (bwh_tab[1][b->bs][0]) {
#define SET_CTXS(dir, off, n) \
    do { \
        SPLAT_CTX(s->dir##_skip_ctx[off],      b->skip,          n); \
        SPLAT_CTX(s->dir##_txfm_ctx[off],      b->tx,            n); \
        SPLAT_CTX(s->dir##_partition_ctx[off], dir##_ctx[b->bs], n); \
        if (!s->s.h.keyframe && !s->s.h.intraonly) { \
            SPLAT_CTX(s->dir##_intra_ctx[off], b->intra,   n); \
            SPLAT_CTX(s->dir##_comp_ctx[off],  b->comp,    n); \
            SPLAT_CTX(s->dir##_mode_ctx[off],  b->mode[3], n); \
            if (!b->intra) { \
                SPLAT_CTX(s->dir##_ref_ctx[off], vref, n); \
                if (s->s.h.filtermode == FILTER_SWITCHABLE) { \
                    SPLAT_CTX(s->dir##_filter_ctx[off], filter_id, n); \
                } \
            } \
        } \
    } while (0)
    case 1: SET_CTXS(above, col, 1); break;
    case 2: SET_CTXS(above, col, 2); break;
    case 4: SET_CTXS(above, col, 4); break;
    case 8: SET_CTXS(above, col, 8); break;
    }
    switch (bwh_tab[1][b->bs][1]) {
    case 1: SET_CTXS(left, row7, 1); break;
    case 2: SET_CTXS(left, row7, 2); break;
    case 4: SET_CTXS(left, row7, 4); break;
    case 8: SET_CTXS(left, row7, 8); break;
    }
#undef SPLAT_CTX
#undef SET_CTXS

    if (!s->s.h.keyframe && !s->s.h.intraonly) {
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
    }

    // FIXME kinda ugly
    for (y = 0; y < h4; y++) {
        int x, o = (row + y) * s->sb_cols * 8 + col;
        struct VP9mvrefPair *mv = &s->s.frames[CUR_FRAME].mv[o];

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
            nnz = (1 + cache[nb[i][0]] + cache[nb[i][1]]) >> 1;
            tp = p[band][nnz];
            if (++i == n_coeffs)
                break; //invalid input; blocks should end with EOB
            goto skip_eob;
        }

        rc = scan[i];
        if (!vp56_rac_get_prob_branchy(c, tp[2])) { // one
            cnt[band][nnz][1]++;
            val = 1;
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
                    val = 3 + vp56_rac_get_prob(c, tp[5]);
                    cache[rc] = 3;
                }
            } else if (!vp56_rac_get_prob_branchy(c, tp[6])) { // cat1/2
                cache[rc] = 4;
                if (!vp56_rac_get_prob_branchy(c, tp[7])) {
                    val = 5 + vp56_rac_get_prob(c, 159);
                } else {
                    val  = 7 + (vp56_rac_get_prob(c, 165) << 1);
                    val +=      vp56_rac_get_prob(c, 145);
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
                    val  = 35 + (vp56_rac_get_prob(c, 180) << 4);
                    val +=      (vp56_rac_get_prob(c, 157) << 3);
                    val +=      (vp56_rac_get_prob(c, 141) << 2);
                    val +=      (vp56_rac_get_prob(c, 134) << 1);
                    val +=       vp56_rac_get_prob(c, 130);
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
                    val +=      (vp56_rac_get_prob(c, 254) << 13);
                    val +=      (vp56_rac_get_prob(c, 254) << 12);
                    val +=      (vp56_rac_get_prob(c, 254) << 11);
                    val +=      (vp56_rac_get_prob(c, 252) << 10);
                    val +=      (vp56_rac_get_prob(c, 249) << 9);
                    val +=      (vp56_rac_get_prob(c, 243) << 8);
                    val +=      (vp56_rac_get_prob(c, 230) << 7);
                    val +=      (vp56_rac_get_prob(c, 196) << 6);
                    val +=      (vp56_rac_get_prob(c, 177) << 5);
                    val +=      (vp56_rac_get_prob(c, 153) << 4);
                    val +=      (vp56_rac_get_prob(c, 140) << 3);
                    val +=      (vp56_rac_get_prob(c, 133) << 2);
                    val +=      (vp56_rac_get_prob(c, 130) << 1);
                    val +=       vp56_rac_get_prob(c, 129);
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
            STORE_COEF(coef, rc, ((vp8_rac_get(c) ? -val : val) * qmul[!!i]) / 2);
        else
            STORE_COEF(coef, rc, (vp8_rac_get(c) ? -val : val) * qmul[!!i]);
        nnz = (1 + cache[nb[i][0]] + cache[nb[i][1]]) >> 1;
        tp = p[band][nnz];
    } while (++i < n_coeffs);

    return i;
}

static int decode_coeffs_b_8bpp(VP9Context *s, int16_t *coef, int n_coeffs,
                                unsigned (*cnt)[6][3], unsigned (*eob)[6][2],
                                uint8_t (*p)[6][11], int nnz, const int16_t *scan,
                                const int16_t (*nb)[2], const int16_t *band_counts,
                                const int16_t *qmul)
{
    return decode_coeffs_b_generic(&s->c, coef, n_coeffs, 0, 1, 8, cnt, eob, p,
                                   nnz, scan, nb, band_counts, qmul);
}

static int decode_coeffs_b32_8bpp(VP9Context *s, int16_t *coef, int n_coeffs,
                                  unsigned (*cnt)[6][3], unsigned (*eob)[6][2],
                                  uint8_t (*p)[6][11], int nnz, const int16_t *scan,
                                  const int16_t (*nb)[2], const int16_t *band_counts,
                                  const int16_t *qmul)
{
    return decode_coeffs_b_generic(&s->c, coef, n_coeffs, 1, 1, 8, cnt, eob, p,
                                   nnz, scan, nb, band_counts, qmul);
}

static int decode_coeffs_b_16bpp(VP9Context *s, int16_t *coef, int n_coeffs,
                                 unsigned (*cnt)[6][3], unsigned (*eob)[6][2],
                                 uint8_t (*p)[6][11], int nnz, const int16_t *scan,
                                 const int16_t (*nb)[2], const int16_t *band_counts,
                                 const int16_t *qmul)
{
    return decode_coeffs_b_generic(&s->c, coef, n_coeffs, 0, 0, s->s.h.bpp, cnt, eob, p,
                                   nnz, scan, nb, band_counts, qmul);
}

static int decode_coeffs_b32_16bpp(VP9Context *s, int16_t *coef, int n_coeffs,
                                   unsigned (*cnt)[6][3], unsigned (*eob)[6][2],
                                   uint8_t (*p)[6][11], int nnz, const int16_t *scan,
                                   const int16_t (*nb)[2], const int16_t *band_counts,
                                   const int16_t *qmul)
{
    return decode_coeffs_b_generic(&s->c, coef, n_coeffs, 1, 0, s->s.h.bpp, cnt, eob, p,
                                   nnz, scan, nb, band_counts, qmul);
}

static av_always_inline int decode_coeffs(AVCodecContext *ctx, int is8bitsperpixel)
{
    VP9Context *s = ctx->priv_data;
    VP9Block *b = s->b;
    int row = s->row, col = s->col;
    uint8_t (*p)[6][11] = s->prob.coef[b->tx][0 /* y */][!b->intra];
    unsigned (*c)[6][3] = s->counts.coef[b->tx][0 /* y */][!b->intra];
    unsigned (*e)[6][2] = s->counts.eob[b->tx][0 /* y */][!b->intra];
    int w4 = bwh_tab[1][b->bs][0] << 1, h4 = bwh_tab[1][b->bs][1] << 1;
    int end_x = FFMIN(2 * (s->cols - col), w4);
    int end_y = FFMIN(2 * (s->rows - row), h4);
    int n, pl, x, y, res;
    int16_t (*qmul)[2] = s->s.h.segmentation.feat[b->seg_id].qmul;
    int tx = 4 * s->s.h.lossless + b->tx;
    const int16_t * const *yscans = ff_vp9_scans[tx];
    const int16_t (* const *ynbs)[2] = ff_vp9_scans_nb[tx];
    const int16_t *uvscan = ff_vp9_scans[b->uvtx][DCT_DCT];
    const int16_t (*uvnb)[2] = ff_vp9_scans_nb[b->uvtx][DCT_DCT];
    uint8_t *a = &s->above_y_nnz_ctx[col * 2];
    uint8_t *l = &s->left_y_nnz_ctx[(row & 7) << 1];
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
            res = (is8bitsperpixel ? decode_coeffs_b##v##_8bpp : decode_coeffs_b##v##_16bpp) \
                                    (s, s->block + 16 * n * bytesperpixel, 16 * step * step, \
                                     c, e, p, a[x] + l[y], yscans[txtp], \
                                     ynbs[txtp], y_band_counts, qmul[0]); \
            a[x] = l[y] = !!res; \
            total_coeff |= !!res; \
            if (step >= 4) { \
                AV_WN16A(&s->eob[n], res); \
            } else { \
                s->eob[n] = res; \
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
            res = (is8bitsperpixel ? decode_coeffs_b##v##_8bpp : decode_coeffs_b##v##_16bpp) \
                                    (s, s->uvblock[pl] + 16 * n * bytesperpixel, \
                                     16 * step * step, c, e, p, a[x] + l[y], \
                                     uvscan, uvnb, uv_band_counts, qmul[1]); \
            a[x] = l[y] = !!res; \
            total_coeff |= !!res; \
            if (step >= 4) { \
                AV_WN16A(&s->uveob[pl][n], res); \
            } else { \
                s->uveob[pl][n] = res; \
            } \
        } \
    }

    p = s->prob.coef[b->uvtx][1 /* uv */][!b->intra];
    c = s->counts.coef[b->uvtx][1 /* uv */][!b->intra];
    e = s->counts.eob[b->uvtx][1 /* uv */][!b->intra];
    w4 >>= s->ss_h;
    end_x >>= s->ss_h;
    h4 >>= s->ss_v;
    end_y >>= s->ss_v;
    for (pl = 0; pl < 2; pl++) {
        a = &s->above_uv_nnz_ctx[pl][col << !s->ss_h];
        l = &s->left_uv_nnz_ctx[pl][(row & 7) << !s->ss_v];
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

static int decode_coeffs_8bpp(AVCodecContext *ctx)
{
    return decode_coeffs(ctx, 1);
}

static int decode_coeffs_16bpp(AVCodecContext *ctx)
{
    return decode_coeffs(ctx, 0);
}

static av_always_inline int check_intra_mode(VP9Context *s, int mode, uint8_t **a,
                                             uint8_t *dst_edge, ptrdiff_t stride_edge,
                                             uint8_t *dst_inner, ptrdiff_t stride_inner,
                                             uint8_t *l, int col, int x, int w,
                                             int row, int y, enum TxfmMode tx,
                                             int p, int ss_h, int ss_v, int bytesperpixel)
{
    int have_top = row > 0 || y > 0;
    int have_left = col > s->tile_col_start || x > 0;
    int have_right = x < w - 1;
    int bpp = s->s.h.bpp;
    static const uint8_t mode_conv[10][2 /* have_left */][2 /* have_top */] = {
        [VERT_PRED]            = { { DC_127_PRED,          VERT_PRED },
                                   { DC_127_PRED,          VERT_PRED } },
        [HOR_PRED]             = { { DC_129_PRED,          DC_129_PRED },
                                   { HOR_PRED,             HOR_PRED } },
        [DC_PRED]              = { { DC_128_PRED,          TOP_DC_PRED },
                                   { LEFT_DC_PRED,         DC_PRED } },
        [DIAG_DOWN_LEFT_PRED]  = { { DC_127_PRED,          DIAG_DOWN_LEFT_PRED },
                                   { DC_127_PRED,          DIAG_DOWN_LEFT_PRED } },
        [DIAG_DOWN_RIGHT_PRED] = { { DIAG_DOWN_RIGHT_PRED, DIAG_DOWN_RIGHT_PRED },
                                   { DIAG_DOWN_RIGHT_PRED, DIAG_DOWN_RIGHT_PRED } },
        [VERT_RIGHT_PRED]      = { { VERT_RIGHT_PRED,      VERT_RIGHT_PRED },
                                   { VERT_RIGHT_PRED,      VERT_RIGHT_PRED } },
        [HOR_DOWN_PRED]        = { { HOR_DOWN_PRED,        HOR_DOWN_PRED },
                                   { HOR_DOWN_PRED,        HOR_DOWN_PRED } },
        [VERT_LEFT_PRED]       = { { DC_127_PRED,          VERT_LEFT_PRED },
                                   { DC_127_PRED,          VERT_LEFT_PRED } },
        [HOR_UP_PRED]          = { { DC_129_PRED,          DC_129_PRED },
                                   { HOR_UP_PRED,          HOR_UP_PRED } },
        [TM_VP8_PRED]          = { { DC_129_PRED,          VERT_PRED },
                                   { HOR_PRED,             TM_VP8_PRED } },
    };
    static const struct {
        uint8_t needs_left:1;
        uint8_t needs_top:1;
        uint8_t needs_topleft:1;
        uint8_t needs_topright:1;
        uint8_t invert_left:1;
    } edges[N_INTRA_PRED_MODES] = {
        [VERT_PRED]            = { .needs_top  = 1 },
        [HOR_PRED]             = { .needs_left = 1 },
        [DC_PRED]              = { .needs_top  = 1, .needs_left = 1 },
        [DIAG_DOWN_LEFT_PRED]  = { .needs_top  = 1, .needs_topright = 1 },
        [DIAG_DOWN_RIGHT_PRED] = { .needs_left = 1, .needs_top = 1, .needs_topleft = 1 },
        [VERT_RIGHT_PRED]      = { .needs_left = 1, .needs_top = 1, .needs_topleft = 1 },
        [HOR_DOWN_PRED]        = { .needs_left = 1, .needs_top = 1, .needs_topleft = 1 },
        [VERT_LEFT_PRED]       = { .needs_top  = 1, .needs_topright = 1 },
        [HOR_UP_PRED]          = { .needs_left = 1, .invert_left = 1 },
        [TM_VP8_PRED]          = { .needs_left = 1, .needs_top = 1, .needs_topleft = 1 },
        [LEFT_DC_PRED]         = { .needs_left = 1 },
        [TOP_DC_PRED]          = { .needs_top  = 1 },
        [DC_128_PRED]          = { 0 },
        [DC_127_PRED]          = { 0 },
        [DC_129_PRED]          = { 0 }
    };

    av_assert2(mode >= 0 && mode < 10);
    mode = mode_conv[mode][have_left][have_top];
    if (edges[mode].needs_top) {
        uint8_t *top, *topleft;
        int n_px_need = 4 << tx, n_px_have = (((s->cols - col) << !ss_h) - x) * 4;
        int n_px_need_tr = 0;

        if (tx == TX_4X4 && edges[mode].needs_topright && have_right)
            n_px_need_tr = 4;

        // if top of sb64-row, use s->intra_pred_data[] instead of
        // dst[-stride] for intra prediction (it contains pre- instead of
        // post-loopfilter data)
        if (have_top) {
            top = !(row & 7) && !y ?
                s->intra_pred_data[p] + (col * (8 >> ss_h) + x * 4) * bytesperpixel :
                y == 0 ? &dst_edge[-stride_edge] : &dst_inner[-stride_inner];
            if (have_left)
                topleft = !(row & 7) && !y ?
                    s->intra_pred_data[p] + (col * (8 >> ss_h) + x * 4) * bytesperpixel :
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
                    memcpy(*a, top, n_px_need * bytesperpixel);
                } else {
#define memset_bpp(c, i1, v, i2, num) do { \
    if (bytesperpixel == 1) { \
        memset(&(c)[(i1)], (v)[(i2)], (num)); \
    } else { \
        int n, val = AV_RN16A(&(v)[(i2) * 2]); \
        for (n = 0; n < (num); n++) { \
            AV_WN16A(&(c)[((i1) + n) * 2], val); \
        } \
    } \
} while (0)
                    memcpy(*a, top, n_px_have * bytesperpixel);
                    memset_bpp(*a, n_px_have, (*a), n_px_have - 1, n_px_need - n_px_have);
                }
            } else {
#define memset_val(c, val, num) do { \
    if (bytesperpixel == 1) { \
        memset((c), (val), (num)); \
    } else { \
        int n; \
        for (n = 0; n < (num); n++) { \
            AV_WN16A(&(c)[n * 2], (val)); \
        } \
    } \
} while (0)
                memset_val(*a, (128 << (bpp - 8)) - 1, n_px_need);
            }
            if (edges[mode].needs_topleft) {
                if (have_left && have_top) {
#define assign_bpp(c, i1, v, i2) do { \
    if (bytesperpixel == 1) { \
        (c)[(i1)] = (v)[(i2)]; \
    } else { \
        AV_COPY16(&(c)[(i1) * 2], &(v)[(i2) * 2]); \
    } \
} while (0)
                    assign_bpp(*a, -1, topleft, -1);
                } else {
#define assign_val(c, i, v) do { \
    if (bytesperpixel == 1) { \
        (c)[(i)] = (v); \
    } else { \
        AV_WN16A(&(c)[(i) * 2], (v)); \
    } \
} while (0)
                    assign_val((*a), -1, (128 << (bpp - 8)) + (have_top ? +1 : -1));
                }
            }
            if (tx == TX_4X4 && edges[mode].needs_topright) {
                if (have_top && have_right &&
                    n_px_need + n_px_need_tr <= n_px_have) {
                    memcpy(&(*a)[4 * bytesperpixel], &top[4 * bytesperpixel], 4 * bytesperpixel);
                } else {
                    memset_bpp(*a, 4, *a, 3, 4);
                }
            }
        }
    }
    if (edges[mode].needs_left) {
        if (have_left) {
            int n_px_need = 4 << tx, i, n_px_have = (((s->rows - row) << !ss_v) - y) * 4;
            uint8_t *dst = x == 0 ? dst_edge : dst_inner;
            ptrdiff_t stride = x == 0 ? stride_edge : stride_inner;

            if (edges[mode].invert_left) {
                if (n_px_need <= n_px_have) {
                    for (i = 0; i < n_px_need; i++)
                        assign_bpp(l, i, &dst[i * stride], -1);
                } else {
                    for (i = 0; i < n_px_have; i++)
                        assign_bpp(l, i, &dst[i * stride], -1);
                    memset_bpp(l, n_px_have, l, n_px_have - 1, n_px_need - n_px_have);
                }
            } else {
                if (n_px_need <= n_px_have) {
                    for (i = 0; i < n_px_need; i++)
                        assign_bpp(l, n_px_need - 1 - i, &dst[i * stride], -1);
                } else {
                    for (i = 0; i < n_px_have; i++)
                        assign_bpp(l, n_px_need - 1 - i, &dst[i * stride], -1);
                    memset_bpp(l, 0, l, n_px_need - n_px_have, n_px_need - n_px_have);
                }
            }
        } else {
            memset_val(l, (128 << (bpp - 8)) + 1, 4 << tx);
        }
    }

    return mode;
}

static av_always_inline void intra_recon(AVCodecContext *ctx, ptrdiff_t y_off,
                                         ptrdiff_t uv_off, int bytesperpixel)
{
    VP9Context *s = ctx->priv_data;
    VP9Block *b = s->b;
    int row = s->row, col = s->col;
    int w4 = bwh_tab[1][b->bs][0] << 1, step1d = 1 << b->tx, n;
    int h4 = bwh_tab[1][b->bs][1] << 1, x, y, step = 1 << (b->tx * 2);
    int end_x = FFMIN(2 * (s->cols - col), w4);
    int end_y = FFMIN(2 * (s->rows - row), h4);
    int tx = 4 * s->s.h.lossless + b->tx, uvtx = b->uvtx + 4 * s->s.h.lossless;
    int uvstep1d = 1 << b->uvtx, p;
    uint8_t *dst = s->dst[0], *dst_r = s->s.frames[CUR_FRAME].tf.f->data[0] + y_off;
    LOCAL_ALIGNED_32(uint8_t, a_buf, [96]);
    LOCAL_ALIGNED_32(uint8_t, l, [64]);

    for (n = 0, y = 0; y < end_y; y += step1d) {
        uint8_t *ptr = dst, *ptr_r = dst_r;
        for (x = 0; x < end_x; x += step1d, ptr += 4 * step1d * bytesperpixel,
                               ptr_r += 4 * step1d * bytesperpixel, n += step) {
            int mode = b->mode[b->bs > BS_8x8 && b->tx == TX_4X4 ?
                               y * 2 + x : 0];
            uint8_t *a = &a_buf[32];
            enum TxfmType txtp = ff_vp9_intra_txfm_type[mode];
            int eob = b->skip ? 0 : b->tx > TX_8X8 ? AV_RN16A(&s->eob[n]) : s->eob[n];

            mode = check_intra_mode(s, mode, &a, ptr_r,
                                    s->s.frames[CUR_FRAME].tf.f->linesize[0],
                                    ptr, s->y_stride, l,
                                    col, x, w4, row, y, b->tx, 0, 0, 0, bytesperpixel);
            s->dsp.intra_pred[b->tx][mode](ptr, s->y_stride, l, a);
            if (eob)
                s->dsp.itxfm_add[tx][txtp](ptr, s->y_stride,
                                           s->block + 16 * n * bytesperpixel, eob);
        }
        dst_r += 4 * step1d * s->s.frames[CUR_FRAME].tf.f->linesize[0];
        dst   += 4 * step1d * s->y_stride;
    }

    // U/V
    w4 >>= s->ss_h;
    end_x >>= s->ss_h;
    end_y >>= s->ss_v;
    step = 1 << (b->uvtx * 2);
    for (p = 0; p < 2; p++) {
        dst   = s->dst[1 + p];
        dst_r = s->s.frames[CUR_FRAME].tf.f->data[1 + p] + uv_off;
        for (n = 0, y = 0; y < end_y; y += uvstep1d) {
            uint8_t *ptr = dst, *ptr_r = dst_r;
            for (x = 0; x < end_x; x += uvstep1d, ptr += 4 * uvstep1d * bytesperpixel,
                                   ptr_r += 4 * uvstep1d * bytesperpixel, n += step) {
                int mode = b->uvmode;
                uint8_t *a = &a_buf[32];
                int eob = b->skip ? 0 : b->uvtx > TX_8X8 ? AV_RN16A(&s->uveob[p][n]) : s->uveob[p][n];

                mode = check_intra_mode(s, mode, &a, ptr_r,
                                        s->s.frames[CUR_FRAME].tf.f->linesize[1],
                                        ptr, s->uv_stride, l, col, x, w4, row, y,
                                        b->uvtx, p + 1, s->ss_h, s->ss_v, bytesperpixel);
                s->dsp.intra_pred[b->uvtx][mode](ptr, s->uv_stride, l, a);
                if (eob)
                    s->dsp.itxfm_add[uvtx][DCT_DCT](ptr, s->uv_stride,
                                                    s->uvblock[p] + 16 * n * bytesperpixel, eob);
            }
            dst_r += 4 * uvstep1d * s->s.frames[CUR_FRAME].tf.f->linesize[1];
            dst   += 4 * uvstep1d * s->uv_stride;
        }
    }
}

static void intra_recon_8bpp(AVCodecContext *ctx, ptrdiff_t y_off, ptrdiff_t uv_off)
{
    intra_recon(ctx, y_off, uv_off, 1);
}

static void intra_recon_16bpp(AVCodecContext *ctx, ptrdiff_t y_off, ptrdiff_t uv_off)
{
    intra_recon(ctx, y_off, uv_off, 2);
}

static av_always_inline void mc_luma_unscaled(VP9Context *s, vp9_mc_func (*mc)[2],
                                              uint8_t *dst, ptrdiff_t dst_stride,
                                              const uint8_t *ref, ptrdiff_t ref_stride,
                                              ThreadFrame *ref_frame,
                                              ptrdiff_t y, ptrdiff_t x, const VP56mv *mv,
                                              int bw, int bh, int w, int h, int bytesperpixel)
{
    int mx = mv->x, my = mv->y, th;

    y += my >> 3;
    x += mx >> 3;
    ref += y * ref_stride + x * bytesperpixel;
    mx &= 7;
    my &= 7;
    // FIXME bilinear filter only needs 0/1 pixels, not 3/4
    // we use +7 because the last 7 pixels of each sbrow can be changed in
    // the longest loopfilter of the next sbrow
    th = (y + bh + 4 * !!my + 7) >> 6;
    ff_thread_await_progress(ref_frame, FFMAX(th, 0), 0);
    // The arm/aarch64 _hv filters read one more row than what actually is
    // needed, so switch to emulated edge one pixel sooner vertically
    // (!!my * 5) than horizontally (!!mx * 4).
    if (x < !!mx * 3 || y < !!my * 3 ||
        x + !!mx * 4 > w - bw || y + !!my * 5 > h - bh) {
        s->vdsp.emulated_edge_mc(s->edge_emu_buffer,
                                 ref - !!my * 3 * ref_stride - !!mx * 3 * bytesperpixel,
                                 160, ref_stride,
                                 bw + !!mx * 7, bh + !!my * 7,
                                 x - !!mx * 3, y - !!my * 3, w, h);
        ref = s->edge_emu_buffer + !!my * 3 * 160 + !!mx * 3 * bytesperpixel;
        ref_stride = 160;
    }
    mc[!!mx][!!my](dst, dst_stride, ref, ref_stride, bh, mx << 1, my << 1);
}

static av_always_inline void mc_chroma_unscaled(VP9Context *s, vp9_mc_func (*mc)[2],
                                                uint8_t *dst_u, uint8_t *dst_v,
                                                ptrdiff_t dst_stride,
                                                const uint8_t *ref_u, ptrdiff_t src_stride_u,
                                                const uint8_t *ref_v, ptrdiff_t src_stride_v,
                                                ThreadFrame *ref_frame,
                                                ptrdiff_t y, ptrdiff_t x, const VP56mv *mv,
                                                int bw, int bh, int w, int h, int bytesperpixel)
{
    int mx = mv->x * (1 << !s->ss_h), my = mv->y * (1 << !s->ss_v), th;

    y += my >> 4;
    x += mx >> 4;
    ref_u += y * src_stride_u + x * bytesperpixel;
    ref_v += y * src_stride_v + x * bytesperpixel;
    mx &= 15;
    my &= 15;
    // FIXME bilinear filter only needs 0/1 pixels, not 3/4
    // we use +7 because the last 7 pixels of each sbrow can be changed in
    // the longest loopfilter of the next sbrow
    th = (y + bh + 4 * !!my + 7) >> (6 - s->ss_v);
    ff_thread_await_progress(ref_frame, FFMAX(th, 0), 0);
    // The arm/aarch64 _hv filters read one more row than what actually is
    // needed, so switch to emulated edge one pixel sooner vertically
    // (!!my * 5) than horizontally (!!mx * 4).
    if (x < !!mx * 3 || y < !!my * 3 ||
        x + !!mx * 4 > w - bw || y + !!my * 5 > h - bh) {
        s->vdsp.emulated_edge_mc(s->edge_emu_buffer,
                                 ref_u - !!my * 3 * src_stride_u - !!mx * 3 * bytesperpixel,
                                 160, src_stride_u,
                                 bw + !!mx * 7, bh + !!my * 7,
                                 x - !!mx * 3, y - !!my * 3, w, h);
        ref_u = s->edge_emu_buffer + !!my * 3 * 160 + !!mx * 3 * bytesperpixel;
        mc[!!mx][!!my](dst_u, dst_stride, ref_u, 160, bh, mx, my);

        s->vdsp.emulated_edge_mc(s->edge_emu_buffer,
                                 ref_v - !!my * 3 * src_stride_v - !!mx * 3 * bytesperpixel,
                                 160, src_stride_v,
                                 bw + !!mx * 7, bh + !!my * 7,
                                 x - !!mx * 3, y - !!my * 3, w, h);
        ref_v = s->edge_emu_buffer + !!my * 3 * 160 + !!mx * 3 * bytesperpixel;
        mc[!!mx][!!my](dst_v, dst_stride, ref_v, 160, bh, mx, my);
    } else {
        mc[!!mx][!!my](dst_u, dst_stride, ref_u, src_stride_u, bh, mx, my);
        mc[!!mx][!!my](dst_v, dst_stride, ref_v, src_stride_v, bh, mx, my);
    }
}

#define mc_luma_dir(s, mc, dst, dst_ls, src, src_ls, tref, row, col, mv, \
                    px, py, pw, ph, bw, bh, w, h, i) \
    mc_luma_unscaled(s, s->dsp.mc, dst, dst_ls, src, src_ls, tref, row, col, \
                     mv, bw, bh, w, h, bytesperpixel)
#define mc_chroma_dir(s, mc, dstu, dstv, dst_ls, srcu, srcu_ls, srcv, srcv_ls, tref, \
                      row, col, mv, px, py, pw, ph, bw, bh, w, h, i) \
    mc_chroma_unscaled(s, s->dsp.mc, dstu, dstv, dst_ls, srcu, srcu_ls, srcv, srcv_ls, tref, \
                       row, col, mv, bw, bh, w, h, bytesperpixel)
#define SCALED 0
#define FN(x) x##_8bpp
#define BYTES_PER_PIXEL 1
#include "vp9_mc_template.c"
#undef FN
#undef BYTES_PER_PIXEL
#define FN(x) x##_16bpp
#define BYTES_PER_PIXEL 2
#include "vp9_mc_template.c"
#undef mc_luma_dir
#undef mc_chroma_dir
#undef FN
#undef BYTES_PER_PIXEL
#undef SCALED

static av_always_inline void mc_luma_scaled(VP9Context *s, vp9_scaled_mc_func smc,
                                            vp9_mc_func (*mc)[2],
                                            uint8_t *dst, ptrdiff_t dst_stride,
                                            const uint8_t *ref, ptrdiff_t ref_stride,
                                            ThreadFrame *ref_frame,
                                            ptrdiff_t y, ptrdiff_t x, const VP56mv *in_mv,
                                            int px, int py, int pw, int ph,
                                            int bw, int bh, int w, int h, int bytesperpixel,
                                            const uint16_t *scale, const uint8_t *step)
{
    if (s->s.frames[CUR_FRAME].tf.f->width == ref_frame->f->width &&
        s->s.frames[CUR_FRAME].tf.f->height == ref_frame->f->height) {
        mc_luma_unscaled(s, mc, dst, dst_stride, ref, ref_stride, ref_frame,
                         y, x, in_mv, bw, bh, w, h, bytesperpixel);
    } else {
#define scale_mv(n, dim) (((int64_t)(n) * scale[dim]) >> 14)
    int mx, my;
    int refbw_m1, refbh_m1;
    int th;
    VP56mv mv;

    mv.x = av_clip(in_mv->x, -(x + pw - px + 4) * 8, (s->cols * 8 - x + px + 3) * 8);
    mv.y = av_clip(in_mv->y, -(y + ph - py + 4) * 8, (s->rows * 8 - y + py + 3) * 8);
    // BUG libvpx seems to scale the two components separately. This introduces
    // rounding errors but we have to reproduce them to be exactly compatible
    // with the output from libvpx...
    mx = scale_mv(mv.x * 2, 0) + scale_mv(x * 16, 0);
    my = scale_mv(mv.y * 2, 1) + scale_mv(y * 16, 1);

    y = my >> 4;
    x = mx >> 4;
    ref += y * ref_stride + x * bytesperpixel;
    mx &= 15;
    my &= 15;
    refbw_m1 = ((bw - 1) * step[0] + mx) >> 4;
    refbh_m1 = ((bh - 1) * step[1] + my) >> 4;
    // FIXME bilinear filter only needs 0/1 pixels, not 3/4
    // we use +7 because the last 7 pixels of each sbrow can be changed in
    // the longest loopfilter of the next sbrow
    th = (y + refbh_m1 + 4 + 7) >> 6;
    ff_thread_await_progress(ref_frame, FFMAX(th, 0), 0);
    // The arm/aarch64 _hv filters read one more row than what actually is
    // needed, so switch to emulated edge one pixel sooner vertically
    // (y + 5 >= h - refbh_m1) than horizontally (x + 4 >= w - refbw_m1).
    if (x < 3 || y < 3 || x + 4 >= w - refbw_m1 || y + 5 >= h - refbh_m1) {
        s->vdsp.emulated_edge_mc(s->edge_emu_buffer,
                                 ref - 3 * ref_stride - 3 * bytesperpixel,
                                 288, ref_stride,
                                 refbw_m1 + 8, refbh_m1 + 8,
                                 x - 3, y - 3, w, h);
        ref = s->edge_emu_buffer + 3 * 288 + 3 * bytesperpixel;
        ref_stride = 288;
    }
    smc(dst, dst_stride, ref, ref_stride, bh, mx, my, step[0], step[1]);
    }
}

static av_always_inline void mc_chroma_scaled(VP9Context *s, vp9_scaled_mc_func smc,
                                              vp9_mc_func (*mc)[2],
                                              uint8_t *dst_u, uint8_t *dst_v,
                                              ptrdiff_t dst_stride,
                                              const uint8_t *ref_u, ptrdiff_t src_stride_u,
                                              const uint8_t *ref_v, ptrdiff_t src_stride_v,
                                              ThreadFrame *ref_frame,
                                              ptrdiff_t y, ptrdiff_t x, const VP56mv *in_mv,
                                              int px, int py, int pw, int ph,
                                              int bw, int bh, int w, int h, int bytesperpixel,
                                              const uint16_t *scale, const uint8_t *step)
{
    if (s->s.frames[CUR_FRAME].tf.f->width == ref_frame->f->width &&
        s->s.frames[CUR_FRAME].tf.f->height == ref_frame->f->height) {
        mc_chroma_unscaled(s, mc, dst_u, dst_v, dst_stride, ref_u, src_stride_u,
                           ref_v, src_stride_v, ref_frame,
                           y, x, in_mv, bw, bh, w, h, bytesperpixel);
    } else {
    int mx, my;
    int refbw_m1, refbh_m1;
    int th;
    VP56mv mv;

    if (s->ss_h) {
        // BUG https://code.google.com/p/webm/issues/detail?id=820
        mv.x = av_clip(in_mv->x, -(x + pw - px + 4) * 16, (s->cols * 4 - x + px + 3) * 16);
        mx = scale_mv(mv.x, 0) + (scale_mv(x * 16, 0) & ~15) + (scale_mv(x * 32, 0) & 15);
    } else {
        mv.x = av_clip(in_mv->x, -(x + pw - px + 4) * 8, (s->cols * 8 - x + px + 3) * 8);
        mx = scale_mv(mv.x * 2, 0) + scale_mv(x * 16, 0);
    }
    if (s->ss_v) {
        // BUG https://code.google.com/p/webm/issues/detail?id=820
        mv.y = av_clip(in_mv->y, -(y + ph - py + 4) * 16, (s->rows * 4 - y + py + 3) * 16);
        my = scale_mv(mv.y, 1) + (scale_mv(y * 16, 1) & ~15) + (scale_mv(y * 32, 1) & 15);
    } else {
        mv.y = av_clip(in_mv->y, -(y + ph - py + 4) * 8, (s->rows * 8 - y + py + 3) * 8);
        my = scale_mv(mv.y * 2, 1) + scale_mv(y * 16, 1);
    }
#undef scale_mv
    y = my >> 4;
    x = mx >> 4;
    ref_u += y * src_stride_u + x * bytesperpixel;
    ref_v += y * src_stride_v + x * bytesperpixel;
    mx &= 15;
    my &= 15;
    refbw_m1 = ((bw - 1) * step[0] + mx) >> 4;
    refbh_m1 = ((bh - 1) * step[1] + my) >> 4;
    // FIXME bilinear filter only needs 0/1 pixels, not 3/4
    // we use +7 because the last 7 pixels of each sbrow can be changed in
    // the longest loopfilter of the next sbrow
    th = (y + refbh_m1 + 4 + 7) >> (6 - s->ss_v);
    ff_thread_await_progress(ref_frame, FFMAX(th, 0), 0);
    // The arm/aarch64 _hv filters read one more row than what actually is
    // needed, so switch to emulated edge one pixel sooner vertically
    // (y + 5 >= h - refbh_m1) than horizontally (x + 4 >= w - refbw_m1).
    if (x < 3 || y < 3 || x + 4 >= w - refbw_m1 || y + 5 >= h - refbh_m1) {
        s->vdsp.emulated_edge_mc(s->edge_emu_buffer,
                                 ref_u - 3 * src_stride_u - 3 * bytesperpixel,
                                 288, src_stride_u,
                                 refbw_m1 + 8, refbh_m1 + 8,
                                 x - 3, y - 3, w, h);
        ref_u = s->edge_emu_buffer + 3 * 288 + 3 * bytesperpixel;
        smc(dst_u, dst_stride, ref_u, 288, bh, mx, my, step[0], step[1]);

        s->vdsp.emulated_edge_mc(s->edge_emu_buffer,
                                 ref_v - 3 * src_stride_v - 3 * bytesperpixel,
                                 288, src_stride_v,
                                 refbw_m1 + 8, refbh_m1 + 8,
                                 x - 3, y - 3, w, h);
        ref_v = s->edge_emu_buffer + 3 * 288 + 3 * bytesperpixel;
        smc(dst_v, dst_stride, ref_v, 288, bh, mx, my, step[0], step[1]);
    } else {
        smc(dst_u, dst_stride, ref_u, src_stride_u, bh, mx, my, step[0], step[1]);
        smc(dst_v, dst_stride, ref_v, src_stride_v, bh, mx, my, step[0], step[1]);
    }
    }
}

#define mc_luma_dir(s, mc, dst, dst_ls, src, src_ls, tref, row, col, mv, \
                    px, py, pw, ph, bw, bh, w, h, i) \
    mc_luma_scaled(s, s->dsp.s##mc, s->dsp.mc, dst, dst_ls, src, src_ls, tref, row, col, \
                   mv, px, py, pw, ph, bw, bh, w, h, bytesperpixel, \
                   s->mvscale[b->ref[i]], s->mvstep[b->ref[i]])
#define mc_chroma_dir(s, mc, dstu, dstv, dst_ls, srcu, srcu_ls, srcv, srcv_ls, tref, \
                      row, col, mv, px, py, pw, ph, bw, bh, w, h, i) \
    mc_chroma_scaled(s, s->dsp.s##mc, s->dsp.mc, dstu, dstv, dst_ls, srcu, srcu_ls, srcv, srcv_ls, tref, \
                     row, col, mv, px, py, pw, ph, bw, bh, w, h, bytesperpixel, \
                     s->mvscale[b->ref[i]], s->mvstep[b->ref[i]])
#define SCALED 1
#define FN(x) x##_scaled_8bpp
#define BYTES_PER_PIXEL 1
#include "vp9_mc_template.c"
#undef FN
#undef BYTES_PER_PIXEL
#define FN(x) x##_scaled_16bpp
#define BYTES_PER_PIXEL 2
#include "vp9_mc_template.c"
#undef mc_luma_dir
#undef mc_chroma_dir
#undef FN
#undef BYTES_PER_PIXEL
#undef SCALED

static av_always_inline void inter_recon(AVCodecContext *ctx, int bytesperpixel)
{
    VP9Context *s = ctx->priv_data;
    VP9Block *b = s->b;
    int row = s->row, col = s->col;

    if (s->mvscale[b->ref[0]][0] || (b->comp && s->mvscale[b->ref[1]][0])) {
        if (bytesperpixel == 1) {
            inter_pred_scaled_8bpp(ctx);
        } else {
            inter_pred_scaled_16bpp(ctx);
        }
    } else {
        if (bytesperpixel == 1) {
            inter_pred_8bpp(ctx);
        } else {
            inter_pred_16bpp(ctx);
        }
    }
    if (!b->skip) {
        /* mostly copied intra_recon() */

        int w4 = bwh_tab[1][b->bs][0] << 1, step1d = 1 << b->tx, n;
        int h4 = bwh_tab[1][b->bs][1] << 1, x, y, step = 1 << (b->tx * 2);
        int end_x = FFMIN(2 * (s->cols - col), w4);
        int end_y = FFMIN(2 * (s->rows - row), h4);
        int tx = 4 * s->s.h.lossless + b->tx, uvtx = b->uvtx + 4 * s->s.h.lossless;
        int uvstep1d = 1 << b->uvtx, p;
        uint8_t *dst = s->dst[0];

        // y itxfm add
        for (n = 0, y = 0; y < end_y; y += step1d) {
            uint8_t *ptr = dst;
            for (x = 0; x < end_x; x += step1d,
                 ptr += 4 * step1d * bytesperpixel, n += step) {
                int eob = b->tx > TX_8X8 ? AV_RN16A(&s->eob[n]) : s->eob[n];

                if (eob)
                    s->dsp.itxfm_add[tx][DCT_DCT](ptr, s->y_stride,
                                                  s->block + 16 * n * bytesperpixel, eob);
            }
            dst += 4 * s->y_stride * step1d;
        }

        // uv itxfm add
        end_x >>= s->ss_h;
        end_y >>= s->ss_v;
        step = 1 << (b->uvtx * 2);
        for (p = 0; p < 2; p++) {
            dst = s->dst[p + 1];
            for (n = 0, y = 0; y < end_y; y += uvstep1d) {
                uint8_t *ptr = dst;
                for (x = 0; x < end_x; x += uvstep1d,
                     ptr += 4 * uvstep1d * bytesperpixel, n += step) {
                    int eob = b->uvtx > TX_8X8 ? AV_RN16A(&s->uveob[p][n]) : s->uveob[p][n];

                    if (eob)
                        s->dsp.itxfm_add[uvtx][DCT_DCT](ptr, s->uv_stride,
                                                        s->uvblock[p] + 16 * n * bytesperpixel, eob);
                }
                dst += 4 * uvstep1d * s->uv_stride;
            }
        }
    }
}

static void inter_recon_8bpp(AVCodecContext *ctx)
{
    inter_recon(ctx, 1);
}

static void inter_recon_16bpp(AVCodecContext *ctx)
{
    inter_recon(ctx, 2);
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
            static const unsigned masks[4] = { 0xff, 0x55, 0x11, 0x01 };
            int l2 = tx + ss_h - 1, step1d;
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

void ff_vp9_decode_block(AVCodecContext *ctx, int row, int col,
                         struct VP9Filter *lflvl, ptrdiff_t yoff, ptrdiff_t uvoff,
                         enum BlockLevel bl, enum BlockPartition bp)
{
    VP9Context *s = ctx->priv_data;
    VP9Block *b = s->b;
    enum BlockSize bs = bl * 3 + bp;
    int bytesperpixel = s->bytesperpixel;
    int w4 = bwh_tab[1][bs][0], h4 = bwh_tab[1][bs][1], lvl;
    int emu[2];
    AVFrame *f = s->s.frames[CUR_FRAME].tf.f;

    s->row = row;
    s->row7 = row & 7;
    s->col = col;
    s->col7 = col & 7;
    s->min_mv.x = -(128 + col * 64);
    s->min_mv.y = -(128 + row * 64);
    s->max_mv.x = 128 + (s->cols - col - w4) * 64;
    s->max_mv.y = 128 + (s->rows - row - h4) * 64;
    if (s->pass < 2) {
        b->bs = bs;
        b->bl = bl;
        b->bp = bp;
        decode_mode(ctx);
        b->uvtx = b->tx - ((s->ss_h && w4 * 2 == (1 << b->tx)) ||
                           (s->ss_v && h4 * 2 == (1 << b->tx)));

        if (!b->skip) {
            int has_coeffs;

            if (bytesperpixel == 1) {
                has_coeffs = decode_coeffs_8bpp(ctx);
            } else {
                has_coeffs = decode_coeffs_16bpp(ctx);
            }
            if (!has_coeffs && b->bs <= BS_8x8 && !b->intra) {
                b->skip = 1;
                memset(&s->above_skip_ctx[col], 1, w4);
                memset(&s->left_skip_ctx[s->row7], 1, h4);
            }
        } else {
            int row7 = s->row7;

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
        SPLAT_ZERO_CTX(s->dir##_y_##var[off * 2], n * 2); \
        if (s->ss_##dir2) { \
            SPLAT_ZERO_CTX(s->dir##_uv_##var[0][off], n); \
            SPLAT_ZERO_CTX(s->dir##_uv_##var[1][off], n); \
        } else { \
            SPLAT_ZERO_CTX(s->dir##_uv_##var[0][off * 2], n * 2); \
            SPLAT_ZERO_CTX(s->dir##_uv_##var[1][off * 2], n * 2); \
        } \
    } while (0)

            switch (w4) {
            case 1: SPLAT_ZERO_YUV(above, nnz_ctx, col, 1, h); break;
            case 2: SPLAT_ZERO_YUV(above, nnz_ctx, col, 2, h); break;
            case 4: SPLAT_ZERO_YUV(above, nnz_ctx, col, 4, h); break;
            case 8: SPLAT_ZERO_YUV(above, nnz_ctx, col, 8, h); break;
            }
            switch (h4) {
            case 1: SPLAT_ZERO_YUV(left, nnz_ctx, row7, 1, v); break;
            case 2: SPLAT_ZERO_YUV(left, nnz_ctx, row7, 2, v); break;
            case 4: SPLAT_ZERO_YUV(left, nnz_ctx, row7, 4, v); break;
            case 8: SPLAT_ZERO_YUV(left, nnz_ctx, row7, 8, v); break;
            }
        }

        if (s->pass == 1) {
            s->b++;
            s->block += w4 * h4 * 64 * bytesperpixel;
            s->uvblock[0] += w4 * h4 * 64 * bytesperpixel >> (s->ss_h + s->ss_v);
            s->uvblock[1] += w4 * h4 * 64 * bytesperpixel >> (s->ss_h + s->ss_v);
            s->eob += 4 * w4 * h4;
            s->uveob[0] += 4 * w4 * h4 >> (s->ss_h + s->ss_v);
            s->uveob[1] += 4 * w4 * h4 >> (s->ss_h + s->ss_v);

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
        s->dst[0] = s->tmp_y;
        s->y_stride = 128;
    } else {
        s->dst[0] = f->data[0] + yoff;
        s->y_stride = f->linesize[0];
    }
    if (emu[1]) {
        s->dst[1] = s->tmp_uv[0];
        s->dst[2] = s->tmp_uv[1];
        s->uv_stride = 128;
    } else {
        s->dst[1] = f->data[1] + uvoff;
        s->dst[2] = f->data[2] + uvoff;
        s->uv_stride = f->linesize[1];
    }
    if (b->intra) {
        if (s->s.h.bpp > 8) {
            intra_recon_16bpp(ctx, yoff, uvoff);
        } else {
            intra_recon_8bpp(ctx, yoff, uvoff);
        }
    } else {
        if (s->s.h.bpp > 8) {
            inter_recon_16bpp(ctx);
        } else {
            inter_recon_8bpp(ctx);
        }
    }
    if (emu[0]) {
        int w = FFMIN(s->cols - col, w4) * 8, h = FFMIN(s->rows - row, h4) * 8, n, o = 0;

        for (n = 0; o < w; n++) {
            int bw = 64 >> n;

            av_assert2(n <= 4);
            if (w & bw) {
                s->dsp.mc[n][0][0][0][0](f->data[0] + yoff + o * bytesperpixel, f->linesize[0],
                                         s->tmp_y + o * bytesperpixel, 128, h, 0, 0);
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
                                         s->tmp_uv[0] + o * bytesperpixel, 128, h, 0, 0);
                s->dsp.mc[n][0][0][0][0](f->data[2] + uvoff + o * bytesperpixel, f->linesize[2],
                                         s->tmp_uv[1] + o * bytesperpixel, 128, h, 0, 0);
                o += bw;
            }
        }
    }

    // pick filter level and find edges to apply filter to
    if (s->s.h.filter.level &&
        (lvl = s->s.h.segmentation.feat[b->seg_id].lflvl[b->intra ? 0 : b->ref[0] + 1]
                                                      [b->mode[3] != ZEROMV]) > 0) {
        int x_end = FFMIN(s->cols - col, w4), y_end = FFMIN(s->rows - row, h4);
        int skip_inter = !b->intra && b->skip, col7 = s->col7, row7 = s->row7;

        setctx_2d(&lflvl->level[row7 * 8 + col7], w4, h4, 8, lvl);
        mask_edges(lflvl->mask[0], 0, 0, row7, col7, x_end, y_end, 0, 0, b->tx, skip_inter);
        if (s->ss_h || s->ss_v)
            mask_edges(lflvl->mask[1], s->ss_h, s->ss_v, row7, col7, x_end, y_end,
                       s->cols & 1 && col + w4 >= s->cols ? s->cols & 7 : 0,
                       s->rows & 1 && row + h4 >= s->rows ? s->rows & 7 : 0,
                       b->uvtx, skip_inter);

        if (!s->filter_lut.lim_lut[lvl]) {
            int sharp = s->s.h.filter.sharpness;
            int limit = lvl;

            if (sharp > 0) {
                limit >>= (sharp + 3) >> 2;
                limit = FFMIN(limit, 9 - sharp);
            }
            limit = FFMAX(limit, 1);

            s->filter_lut.lim_lut[lvl] = limit;
            s->filter_lut.mblim_lut[lvl] = 2 * (lvl + 2) + limit;
        }
    }

    if (s->pass == 2) {
        s->b++;
        s->block += w4 * h4 * 64 * bytesperpixel;
        s->uvblock[0] += w4 * h4 * 64 * bytesperpixel >> (s->ss_v + s->ss_h);
        s->uvblock[1] += w4 * h4 * 64 * bytesperpixel >> (s->ss_v + s->ss_h);
        s->eob += 4 * w4 * h4;
        s->uveob[0] += 4 * w4 * h4 >> (s->ss_v + s->ss_h);
        s->uveob[1] += 4 * w4 * h4 >> (s->ss_v + s->ss_h);
    }
}
