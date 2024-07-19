/*
 * Copyright (c) 2024 Lynne <dev@lynne.ee>
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

#include "aacdec_lpd.h"
#include "aacdec_usac.h"
#include "libavcodec/unary.h"

const uint8_t ff_aac_lpd_mode_tab[32][4] = {
    { 0, 0, 0, 0 },
    { 1, 0, 0, 0 },
    { 0, 1, 0, 0 },
    { 1, 1, 0, 0 },
    { 0, 0, 1, 0 },
    { 1, 0, 1, 0 },
    { 0, 1, 1, 0 },
    { 1, 1, 1, 0 },
    { 0, 0, 0, 1 },
    { 1, 0, 0, 1 },
    { 0, 1, 0, 1 },
    { 1, 1, 0, 1 },
    { 0, 0, 1, 1 },
    { 1, 0, 1, 1 },
    { 0, 1, 1, 1 },
    { 1, 1, 1, 1 },
    { 2, 2, 0, 0 },
    { 2, 2, 1, 0 },
    { 2, 2, 0, 1 },
    { 2, 2, 1, 1 },
    { 0, 0, 2, 2 },
    { 1, 0, 2, 2 },
    { 0, 1, 2, 2 },
    { 1, 1, 2, 2 },
    { 2, 2, 2, 2 },
    { 3, 3, 3, 3 },
    /* Larger values are reserved, but permit them for resilience */
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
};

static void parse_qn(GetBitContext *gb, int *qn, int nk_mode, int no_qn)
{
    if (nk_mode == 1) {
        for (int k = 0; k < no_qn; k++) {
            qn[k] = get_unary(gb, 0, INT32_MAX); // TODO: find proper ranges
            if (qn[k])
                qn[k]++;
        }
        return;
    }

    for (int k = 0; k < no_qn; k++)
        qn[k] = get_bits(gb, 2) + 2;

    if (nk_mode == 2) {
        for (int k = 0; k < no_qn; k++) {
            if (qn[k] > 4) {
                qn[k] = get_unary(gb, 0, INT32_MAX);;
                if (qn[k])
                    qn[k] += 4;
            }
        }
        return;
    }

    for (int k = 0; k < no_qn; k++) {
        if (qn[k] > 4) {
            int qn_ext = get_unary(gb, 0, INT32_MAX);;
            switch (qn_ext) {
            case 0: qn[k] = 5; break;
            case 1: qn[k] = 6; break;
            case 2: qn[k] = 0; break;
            default: qn[k] = qn_ext + 4; break;
            }
        }
    }
}

static int parse_codebook_idx(GetBitContext *gb, uint32_t *kv,
                              int nk_mode, int no_qn)
{
    int n, nk;

    int qn[2];
    parse_qn(gb, qn, nk_mode, no_qn);

    for (int k = 0; k < no_qn; k++) {
        if (qn[k] > 4) {
            nk = (qn[k] - 3) / 2;
            n = qn[k] - nk*2;
        } else {
            nk = 0;
            n = qn[k];
        }
    }

    skip_bits(gb, 4*n);

    if (nk > 0)
        for (int i = 0; i < 8; i++)
            kv[i] = get_bits(gb, nk);

    return 0;
}

int ff_aac_parse_fac_data(AACUsacElemData *ce, GetBitContext *gb,
                          int use_gain, int len)
{
    int ret;
    if (use_gain)
        ce->fac.gain = get_bits(gb, 7);

    if (len/8 > 8)
        return AVERROR_PATCHWELCOME;

    for (int i = 0; i < len/8; i++) {
        ret = parse_codebook_idx(gb, ce->fac.kv[i], 1, 1);
        if (ret < 0)
            return ret;
    }

    return 0;
}

int ff_aac_ldp_parse_channel_stream(AACDecContext *ac, AACUSACConfig *usac,
                                    AACUsacElemData *ce, GetBitContext *gb)
{
    int k;
    const uint8_t *mod;
    int first_ldp_flag;

    ce->ldp.acelp_core_mode = get_bits(gb, 3);
    ce->ldp.lpd_mode = get_bits(gb, 5);

    ce->ldp.bpf_control_info = get_bits1(gb);
    ce->ldp.core_mode_last = get_bits1(gb);
    ce->ldp.fac_data_present = get_bits1(gb);

    mod = ff_aac_lpd_mode_tab[ce->ldp.lpd_mode];

    first_ldp_flag = !ce->ldp.core_mode_last;
    if (first_ldp_flag)
        ce->ldp.last_lpd_mode = -1; /* last_ldp_mode is a **STATEFUL** value */

    k = 0;
    while (k < 0) {
        if (!k) {
            if (ce->ldp.core_mode_last && ce->ldp.fac_data_present)
                ff_aac_parse_fac_data(ce, gb, 0, usac->core_frame_len/8);
        } else {
            if (!ce->ldp.last_lpd_mode && mod[k] > 0 ||
                ce->ldp.last_lpd_mode && !mod[k])
                ff_aac_parse_fac_data(ce, gb, 0, usac->core_frame_len/8);
        }
        if (!mod[k]) {
//            parse_acelp_coding();
            ce->ldp.last_lpd_mode = 0;
            k++;
        } else {
//            parse_tcx_coding();
            ce->ldp.last_lpd_mode = mod[k];
            k += (1 << (mod[k] - 1));
        }
    }

//    parse_lpc_data(first_lpd_flag);

    if (!ce->ldp.core_mode_last && ce->ldp.fac_data_present) {
        uint16_t len_8 = usac->core_frame_len / 8;
        uint16_t len_16 = usac->core_frame_len / 16;
        uint16_t fac_len = get_bits1(gb) /* short_fac_flag */ ? len_8 : len_16;
        int ret = ff_aac_parse_fac_data(ce, gb, 1, fac_len);
        if (ret < 0)
            return ret;
    }

    return 0;
}
