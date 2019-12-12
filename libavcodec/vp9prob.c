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

#include "vp56.h"
#include "vp9.h"
#include "vp9data.h"
#include "vp9dec.h"

static av_always_inline void adapt_prob(uint8_t *p, unsigned ct0, unsigned ct1,
                                        int max_count, int update_factor)
{
    unsigned ct = ct0 + ct1, p2, p1;

    if (!ct)
        return;

    update_factor = FASTDIV(update_factor * FFMIN(ct, max_count), max_count);
    p1 = *p;
    p2 = ((((int64_t) ct0) << 8) + (ct >> 1)) / ct;
    p2 = av_clip(p2, 1, 255);

    // (p1 * (256 - update_factor) + p2 * update_factor + 128) >> 8
    *p = p1 + (((p2 - p1) * update_factor + 128) >> 8);
}

void ff_vp9_adapt_probs(VP9Context *s)
{
    int i, j, k, l, m;
    ProbContext *p = &s->prob_ctx[s->s.h.framectxid].p;
    int uf = (s->s.h.keyframe || s->s.h.intraonly || !s->last_keyframe) ? 112 : 128;

    // coefficients
    for (i = 0; i < 4; i++)
        for (j = 0; j < 2; j++)
            for (k = 0; k < 2; k++)
                for (l = 0; l < 6; l++)
                    for (m = 0; m < 6; m++) {
                        uint8_t *pp = s->prob_ctx[s->s.h.framectxid].coef[i][j][k][l][m];
                        unsigned *e = s->td[0].counts.eob[i][j][k][l][m];
                        unsigned *c = s->td[0].counts.coef[i][j][k][l][m];

                        if (l == 0 && m >= 3) // dc only has 3 pt
                            break;

                        adapt_prob(&pp[0], e[0], e[1], 24, uf);
                        adapt_prob(&pp[1], c[0], c[1] + c[2], 24, uf);
                        adapt_prob(&pp[2], c[1], c[2], 24, uf);
                    }

    if (s->s.h.keyframe || s->s.h.intraonly) {
        memcpy(p->skip,  s->prob.p.skip,  sizeof(p->skip));
        memcpy(p->tx32p, s->prob.p.tx32p, sizeof(p->tx32p));
        memcpy(p->tx16p, s->prob.p.tx16p, sizeof(p->tx16p));
        memcpy(p->tx8p,  s->prob.p.tx8p,  sizeof(p->tx8p));
        return;
    }

    // skip flag
    for (i = 0; i < 3; i++)
        adapt_prob(&p->skip[i], s->td[0].counts.skip[i][0],
                   s->td[0].counts.skip[i][1], 20, 128);

    // intra/inter flag
    for (i = 0; i < 4; i++)
        adapt_prob(&p->intra[i], s->td[0].counts.intra[i][0],
                   s->td[0].counts.intra[i][1], 20, 128);

    // comppred flag
    if (s->s.h.comppredmode == PRED_SWITCHABLE) {
        for (i = 0; i < 5; i++)
            adapt_prob(&p->comp[i], s->td[0].counts.comp[i][0],
                       s->td[0].counts.comp[i][1], 20, 128);
    }

    // reference frames
    if (s->s.h.comppredmode != PRED_SINGLEREF) {
        for (i = 0; i < 5; i++)
            adapt_prob(&p->comp_ref[i], s->td[0].counts.comp_ref[i][0],
                       s->td[0].counts.comp_ref[i][1], 20, 128);
    }

    if (s->s.h.comppredmode != PRED_COMPREF) {
        for (i = 0; i < 5; i++) {
            uint8_t *pp = p->single_ref[i];
            unsigned (*c)[2] = s->td[0].counts.single_ref[i];

            adapt_prob(&pp[0], c[0][0], c[0][1], 20, 128);
            adapt_prob(&pp[1], c[1][0], c[1][1], 20, 128);
        }
    }

    // block partitioning
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++) {
            uint8_t *pp = p->partition[i][j];
            unsigned *c = s->td[0].counts.partition[i][j];

            adapt_prob(&pp[0], c[0], c[1] + c[2] + c[3], 20, 128);
            adapt_prob(&pp[1], c[1], c[2] + c[3], 20, 128);
            adapt_prob(&pp[2], c[2], c[3], 20, 128);
        }

    // tx size
    if (s->s.h.txfmmode == TX_SWITCHABLE) {
        for (i = 0; i < 2; i++) {
            unsigned *c16 = s->td[0].counts.tx16p[i], *c32 = s->td[0].counts.tx32p[i];

            adapt_prob(&p->tx8p[i], s->td[0].counts.tx8p[i][0],
                       s->td[0].counts.tx8p[i][1], 20, 128);
            adapt_prob(&p->tx16p[i][0], c16[0], c16[1] + c16[2], 20, 128);
            adapt_prob(&p->tx16p[i][1], c16[1], c16[2], 20, 128);
            adapt_prob(&p->tx32p[i][0], c32[0], c32[1] + c32[2] + c32[3], 20, 128);
            adapt_prob(&p->tx32p[i][1], c32[1], c32[2] + c32[3], 20, 128);
            adapt_prob(&p->tx32p[i][2], c32[2], c32[3], 20, 128);
        }
    }

    // interpolation filter
    if (s->s.h.filtermode == FILTER_SWITCHABLE) {
        for (i = 0; i < 4; i++) {
            uint8_t *pp = p->filter[i];
            unsigned *c = s->td[0].counts.filter[i];

            adapt_prob(&pp[0], c[0], c[1] + c[2], 20, 128);
            adapt_prob(&pp[1], c[1], c[2], 20, 128);
        }
    }

    // inter modes
    for (i = 0; i < 7; i++) {
        uint8_t *pp = p->mv_mode[i];
        unsigned *c = s->td[0].counts.mv_mode[i];

        adapt_prob(&pp[0], c[2], c[1] + c[0] + c[3], 20, 128);
        adapt_prob(&pp[1], c[0], c[1] + c[3], 20, 128);
        adapt_prob(&pp[2], c[1], c[3], 20, 128);
    }

    // mv joints
    {
        uint8_t *pp = p->mv_joint;
        unsigned *c = s->td[0].counts.mv_joint;

        adapt_prob(&pp[0], c[0], c[1] + c[2] + c[3], 20, 128);
        adapt_prob(&pp[1], c[1], c[2] + c[3], 20, 128);
        adapt_prob(&pp[2], c[2], c[3], 20, 128);
    }

    // mv components
    for (i = 0; i < 2; i++) {
        uint8_t *pp;
        unsigned *c, (*c2)[2], sum;

        adapt_prob(&p->mv_comp[i].sign, s->td[0].counts.mv_comp[i].sign[0],
                   s->td[0].counts.mv_comp[i].sign[1], 20, 128);

        pp  = p->mv_comp[i].classes;
        c   = s->td[0].counts.mv_comp[i].classes;
        sum = c[1] + c[2] + c[3] + c[4] + c[5] +
              c[6] + c[7] + c[8] + c[9] + c[10];
        adapt_prob(&pp[0], c[0], sum, 20, 128);
        sum -= c[1];
        adapt_prob(&pp[1], c[1], sum, 20, 128);
        sum -= c[2] + c[3];
        adapt_prob(&pp[2], c[2] + c[3], sum, 20, 128);
        adapt_prob(&pp[3], c[2], c[3], 20, 128);
        sum -= c[4] + c[5];
        adapt_prob(&pp[4], c[4] + c[5], sum, 20, 128);
        adapt_prob(&pp[5], c[4], c[5], 20, 128);
        sum -= c[6];
        adapt_prob(&pp[6], c[6], sum, 20, 128);
        adapt_prob(&pp[7], c[7] + c[8], c[9] + c[10], 20, 128);
        adapt_prob(&pp[8], c[7], c[8], 20, 128);
        adapt_prob(&pp[9], c[9], c[10], 20, 128);

        adapt_prob(&p->mv_comp[i].class0, s->td[0].counts.mv_comp[i].class0[0],
                   s->td[0].counts.mv_comp[i].class0[1], 20, 128);
        pp = p->mv_comp[i].bits;
        c2 = s->td[0].counts.mv_comp[i].bits;
        for (j = 0; j < 10; j++)
            adapt_prob(&pp[j], c2[j][0], c2[j][1], 20, 128);

        for (j = 0; j < 2; j++) {
            pp = p->mv_comp[i].class0_fp[j];
            c  = s->td[0].counts.mv_comp[i].class0_fp[j];
            adapt_prob(&pp[0], c[0], c[1] + c[2] + c[3], 20, 128);
            adapt_prob(&pp[1], c[1], c[2] + c[3], 20, 128);
            adapt_prob(&pp[2], c[2], c[3], 20, 128);
        }
        pp = p->mv_comp[i].fp;
        c  = s->td[0].counts.mv_comp[i].fp;
        adapt_prob(&pp[0], c[0], c[1] + c[2] + c[3], 20, 128);
        adapt_prob(&pp[1], c[1], c[2] + c[3], 20, 128);
        adapt_prob(&pp[2], c[2], c[3], 20, 128);

        if (s->s.h.highprecisionmvs) {
            adapt_prob(&p->mv_comp[i].class0_hp,
                       s->td[0].counts.mv_comp[i].class0_hp[0],
                       s->td[0].counts.mv_comp[i].class0_hp[1], 20, 128);
            adapt_prob(&p->mv_comp[i].hp, s->td[0].counts.mv_comp[i].hp[0],
                       s->td[0].counts.mv_comp[i].hp[1], 20, 128);
        }
    }

    // y intra modes
    for (i = 0; i < 4; i++) {
        uint8_t *pp = p->y_mode[i];
        unsigned *c = s->td[0].counts.y_mode[i], sum, s2;

        sum = c[0] + c[1] + c[3] + c[4] + c[5] + c[6] + c[7] + c[8] + c[9];
        adapt_prob(&pp[0], c[DC_PRED], sum, 20, 128);
        sum -= c[TM_VP8_PRED];
        adapt_prob(&pp[1], c[TM_VP8_PRED], sum, 20, 128);
        sum -= c[VERT_PRED];
        adapt_prob(&pp[2], c[VERT_PRED], sum, 20, 128);
        s2   = c[HOR_PRED] + c[DIAG_DOWN_RIGHT_PRED] + c[VERT_RIGHT_PRED];
        sum -= s2;
        adapt_prob(&pp[3], s2, sum, 20, 128);
        s2 -= c[HOR_PRED];
        adapt_prob(&pp[4], c[HOR_PRED], s2, 20, 128);
        adapt_prob(&pp[5], c[DIAG_DOWN_RIGHT_PRED], c[VERT_RIGHT_PRED],
                   20, 128);
        sum -= c[DIAG_DOWN_LEFT_PRED];
        adapt_prob(&pp[6], c[DIAG_DOWN_LEFT_PRED], sum, 20, 128);
        sum -= c[VERT_LEFT_PRED];
        adapt_prob(&pp[7], c[VERT_LEFT_PRED], sum, 20, 128);
        adapt_prob(&pp[8], c[HOR_DOWN_PRED], c[HOR_UP_PRED], 20, 128);
    }

    // uv intra modes
    for (i = 0; i < 10; i++) {
        uint8_t *pp = p->uv_mode[i];
        unsigned *c = s->td[0].counts.uv_mode[i], sum, s2;

        sum = c[0] + c[1] + c[3] + c[4] + c[5] + c[6] + c[7] + c[8] + c[9];
        adapt_prob(&pp[0], c[DC_PRED], sum, 20, 128);
        sum -= c[TM_VP8_PRED];
        adapt_prob(&pp[1], c[TM_VP8_PRED], sum, 20, 128);
        sum -= c[VERT_PRED];
        adapt_prob(&pp[2], c[VERT_PRED], sum, 20, 128);
        s2   = c[HOR_PRED] + c[DIAG_DOWN_RIGHT_PRED] + c[VERT_RIGHT_PRED];
        sum -= s2;
        adapt_prob(&pp[3], s2, sum, 20, 128);
        s2 -= c[HOR_PRED];
        adapt_prob(&pp[4], c[HOR_PRED], s2, 20, 128);
        adapt_prob(&pp[5], c[DIAG_DOWN_RIGHT_PRED], c[VERT_RIGHT_PRED],
                   20, 128);
        sum -= c[DIAG_DOWN_LEFT_PRED];
        adapt_prob(&pp[6], c[DIAG_DOWN_LEFT_PRED], sum, 20, 128);
        sum -= c[VERT_LEFT_PRED];
        adapt_prob(&pp[7], c[VERT_LEFT_PRED], sum, 20, 128);
        adapt_prob(&pp[8], c[HOR_DOWN_PRED], c[HOR_UP_PRED], 20, 128);
    }
}
