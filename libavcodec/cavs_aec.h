/*
 * Chinese AVS video (AVS1-P16, Broadcasting profile) AEC decoder.
 * Copyright (c) 2026 Rainbaby <r@bgr.jp>
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
 * Chinese AVS video (AVS1-P16, Broadcasting profile) AEC decoder
 */

#ifndef AVCODEC_CAVS_AEC_H
#define AVCODEC_CAVS_AEC_H

#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/error.h"
#include "libavutil/macros.h"

#include "get_bits.h"

/* Table 51's largest start index is 322, weighting_prediction, one model. */
#define CAVS_AEC_NUM_CTX 323

/** Table 51 start index + ctxIdxInc, or AVERROR_INVALIDDATA/AVERROR_BUG. */
static inline int ff_cavs_aec_ctx_idx(int start, int span, int inc)
{
    if (span <= 0 || start < 0 || start > CAVS_AEC_NUM_CTX - span)
        return AVERROR_BUG;
    if ((unsigned)inc >= (unsigned)span)
        return AVERROR_INVALIDDATA;

    return start + inc;
}

typedef struct CAVSAECModel {
    uint8_t  mps;      ///< mps:    more probable symbol, 1 bit, init 0
    uint8_t  cycno;    ///< cycno:  adaptation counter, 2 bits, init 0
    uint16_t lg_pmps;  ///< lgPmps: log domain P(MPS), 11 bits, init 1023
} CAVSAECModel;

typedef struct CAVSAECContext {
    GetBitContext *gb; ///< bitstream reader, shared with the slice parser
    int rs1;           ///< range exponent,  init 0    (rS1)
    int rt1;           ///< range mantissa,  init 0xFF (rT1)
    int vs;            ///< offset exponent            (valueS)
    int vt;            ///< offset mantissa, 9 bits inside a call (valueT)
    CAVSAECModel ctx[CAVS_AEC_NUM_CTX]; ///< context models, 8.4.2.1
} CAVSAECContext;

/* Constants of clauses 8.4.2 and 8.4.4.3. */
#define CAVS_AEC_LGPMPS_INIT    1023
#define CAVS_AEC_LGPMPS_MAX     1023
#define CAVS_AEC_LGPMPS_REFLECT 2047
#define CAVS_AEC_LGPMPS_BYPASS  1023
#define CAVS_AEC_LGPMPS_STUFF      4
#define CAVS_AEC_RT1_INIT       0xFF

/** Adapt one context model after a bin has been decoded, 8.4.4.3.5. */
static inline void cavs_aec_update_ctx(CAVSAECModel *m, int bin)
{
    static const uint8_t  cwr_tab[4]     = { 3, 3, 4, 5 };
    /* cwr is 3..5, the leading entries only pad the table so it indexes */
    static const uint16_t lps_inc_tab[6] = { 0, 0, 0, 197, 95, 46 };
    const int cwr = cwr_tab[m->cycno];

    if (bin != m->mps)
        m->cycno = FFMIN(m->cycno + 1, 3);
    else if (m->cycno == 0)
        m->cycno = 1;

    if (bin == m->mps) {
        m->lg_pmps -= (m->lg_pmps >> cwr) + (m->lg_pmps >> (cwr + 2));
    } else {
        m->lg_pmps += lps_inc_tab[cwr];
        if (m->lg_pmps > CAVS_AEC_LGPMPS_MAX) {
            m->lg_pmps = CAVS_AEC_LGPMPS_REFLECT - m->lg_pmps;
            m->mps    ^= 1;
        }
    }
}

/** Decode one bin (8.4.4.3.2); the caller supplies predMps and lgPmps. */
static av_always_inline int cavs_aec_decode_bin(CAVSAECContext *aec,
                                                int pred_mps, int lg_pmps)
{
    GetBitContext *gb = aec->gb;
    const int lps_range = lg_pmps >> 2;
    int rs2, rt2, s_flag, bin;

    av_assert2(lps_range > 0);

    if (aec->rt1 >= lps_range) {
        rs2    = aec->rs1;
        rt2    = aec->rt1 - lps_range;
        s_flag = 0;
    } else {
        rs2    = aec->rs1 + 1;
        rt2    = 256 + aec->rt1 - lps_range;
        s_flag = 1;
    }

    if (rs2 > aec->vs || (rs2 == aec->vs && aec->vt >= rt2)) {
        int t_rlps = s_flag ? aec->rt1 + lps_range : lps_range;

        bin = !pred_mps;
        if (rs2 == aec->vs)
            aec->vt -= rt2;
        else
            aec->vt = 256 + ((aec->vt << 1) | get_bits1(gb)) - rt2;
        while (t_rlps < 0x100) {
            t_rlps       = t_rlps << 1;
            aec->vt      = (aec->vt << 1) | get_bits1(gb);
        }
        aec->rt1 = t_rlps & 0xFF;
    } else {
        bin      = pred_mps;
        aec->rs1 = rs2;
        aec->rt1 = rt2;
    }

    if (bin != pred_mps) {
        aec->rs1 = 0;
        aec->vs  = 0;
        /* the loop ends with the buffer, so truncated input terminates */
        while (aec->vt < 0x100 && get_bits_left(gb) > 0) {
            aec->vs++;
            aec->vt = (aec->vt << 1) | get_bits1(gb);
        }
        aec->vt &= 0xFF;
    }

    return bin;
}

/** Reset all contexts and start the decoder on byte aligned @p gb (8.4.1). */
void ff_cavs_aec_init(CAVSAECContext *aec, GetBitContext *gb);

/** Decode a bin with one context model (8.4.4.3.2). */
static inline int ff_cavs_aec_decode_decision(CAVSAECContext *aec, int ctx_idx)
{
    CAVSAECModel *m;
    int bin;

    av_assert2(ctx_idx >= 0 && ctx_idx < CAVS_AEC_NUM_CTX);

    m   = &aec->ctx[ctx_idx];
    bin = cavs_aec_decode_bin(aec, m->mps, m->lg_pmps);
    cavs_aec_update_ctx(m, bin);

    return bin;
}

/** Decode a bin with two weighted context models (8.4.4.3.2). */
static inline int ff_cavs_aec_decode_decision_w(CAVSAECContext *aec,
                                                int ctx_idx, int ctx_idx_w)
{
    CAVSAECModel *m1, *m2;
    int pred_mps, lg_pmps, bin;

    av_assert2(ctx_idx   >= 0 && ctx_idx   < CAVS_AEC_NUM_CTX);
    av_assert2(ctx_idx_w >= 0 && ctx_idx_w < CAVS_AEC_NUM_CTX);

    m1 = &aec->ctx[ctx_idx];
    m2 = &aec->ctx[ctx_idx_w];

    if (m1->mps == m2->mps) {
        pred_mps = m1->mps;
        lg_pmps  = (m1->lg_pmps + m2->lg_pmps) / 2;
    } else if (m1->lg_pmps < m2->lg_pmps) {
        pred_mps = m1->mps;
        lg_pmps  = CAVS_AEC_LGPMPS_MAX - ((m2->lg_pmps - m1->lg_pmps) >> 1);
    } else {
        pred_mps = m2->mps;
        lg_pmps  = CAVS_AEC_LGPMPS_MAX - ((m1->lg_pmps - m2->lg_pmps) >> 1);
    }

    bin = cavs_aec_decode_bin(aec, pred_mps, lg_pmps);
    cavs_aec_update_ctx(m1, bin);
    cavs_aec_update_ctx(m2, bin);

    return bin;
}

/** Decode a bypass bin, fixed lgPmps (8.4.4.3.3). */
static inline int ff_cavs_aec_decode_bypass(CAVSAECContext *aec)
{
    return cavs_aec_decode_bin(aec, 0, CAVS_AEC_LGPMPS_BYPASS);
}

/** Decode aec_mb_stuffing_bit, fixed lgPmps (8.4.4.3.4). */
static inline int ff_cavs_aec_decode_stuffing_bit(CAVSAECContext *aec)
{
    return cavs_aec_decode_bin(aec, 0, CAVS_AEC_LGPMPS_STUFF);
}

#endif /* AVCODEC_CAVS_AEC_H */
