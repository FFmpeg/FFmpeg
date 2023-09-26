/*
 * Functions common to fixed/float MPEG-4 Parametric Stereo decoding
 * Copyright (c) 2010 Alex Converse <alex.converse@gmail.com>
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

#include <stdint.h>
#include "libavutil/common.h"
#include "aacps.h"
#include "get_bits.h"
#include "aacpsdata.c"

static const int8_t num_env_tab[2][4] = {
    { 0, 1, 2, 4, },
    { 1, 2, 3, 4, },
};

static const int8_t nr_iidicc_par_tab[] = {
    10, 20, 34, 10, 20, 34,
};

static const int8_t nr_iidopd_par_tab[] = {
     5, 11, 17,  5, 11, 17,
};

enum {
    huff_iid_df1,
    huff_iid_dt1,
    huff_iid_df0,
    huff_iid_dt0,
    huff_icc_df,
    huff_icc_dt,
    huff_ipd_df,
    huff_ipd_dt,
    huff_opd_df,
    huff_opd_dt,
};

static const int huff_iid[] = {
    huff_iid_df0,
    huff_iid_df1,
    huff_iid_dt0,
    huff_iid_dt1,
};

static const VLCElem *vlc_ps[10];

#define READ_PAR_DATA(PAR, MASK, ERR_CONDITION, NB_BITS, MAX_DEPTH) \
/** \
 * Read Inter-channel Intensity Difference/Inter-Channel Coherence/ \
 * Inter-channel Phase Difference/Overall Phase Difference parameters from the \
 * bitstream. \
 * \
 * @param logctx a context for logging \
 * @param gb    pointer to the input bitstream \
 * @param ps    pointer to the Parametric Stereo context \
 * @param PAR   pointer to the parameter to be read \
 * @param e     envelope to decode \
 * @param dt    1: time delta-coded, 0: frequency delta-coded \
 */ \
static int read_ ## PAR ## _data(void *logctx, GetBitContext *gb, PSCommonContext *ps, \
                        int8_t (*PAR)[PS_MAX_NR_IIDICC], int table_idx, int e, int dt) \
{ \
    int b, num = ps->nr_ ## PAR ## _par; \
    const VLCElem *vlc_table = vlc_ps[table_idx]; \
    if (dt) { \
        int e_prev = e ? e - 1 : ps->num_env_old - 1; \
        e_prev = FFMAX(e_prev, 0); \
        for (b = 0; b < num; b++) { \
            int val = PAR[e_prev][b] + get_vlc2(gb, vlc_table, NB_BITS, MAX_DEPTH); \
            if (MASK) val &= MASK; \
            PAR[e][b] = val; \
            if (ERR_CONDITION) \
                goto err; \
        } \
    } else { \
        int val = 0; \
        for (b = 0; b < num; b++) { \
            val += get_vlc2(gb, vlc_table, NB_BITS, MAX_DEPTH); \
            if (MASK) val &= MASK; \
            PAR[e][b] = val; \
            if (ERR_CONDITION) \
                goto err; \
        } \
    } \
    return 0; \
err: \
    av_log(logctx, AV_LOG_ERROR, "illegal "#PAR"\n"); \
    return AVERROR_INVALIDDATA; \
}

READ_PAR_DATA(iid,       0, FFABS(ps->iid_par[e][b]) > 7 + 8 * ps->iid_quant, 9, 3)
READ_PAR_DATA(icc,       0, ps->icc_par[e][b] > 7U, 9, 2)
READ_PAR_DATA(ipdopd, 0x07, 0, 5, 1)

static int ps_read_extension_data(GetBitContext *gb, PSCommonContext *ps,
                                  int ps_extension_id)
{
    int e;
    int count = get_bits_count(gb);

    if (ps_extension_id)
        return 0;

    ps->enable_ipdopd = get_bits1(gb);
    if (ps->enable_ipdopd) {
        for (e = 0; e < ps->num_env; e++) {
            int dt = get_bits1(gb);
            read_ipdopd_data(NULL, gb, ps, ps->ipd_par, dt ? huff_ipd_dt : huff_ipd_df, e, dt);
            dt = get_bits1(gb);
            read_ipdopd_data(NULL, gb, ps, ps->opd_par, dt ? huff_opd_dt : huff_opd_df, e, dt);
        }
    }
    skip_bits1(gb);      //reserved_ps
    return get_bits_count(gb) - count;
}

int ff_ps_read_data(void *logctx, GetBitContext *gb_host,
                    PSCommonContext *ps, int bits_left)
{
    int e;
    int bit_count_start = get_bits_count(gb_host);
    int header;
    int bits_consumed;
    GetBitContext gbc = *gb_host, *gb = &gbc;

    header = get_bits1(gb);
    if (header) {     //enable_ps_header
        ps->enable_iid = get_bits1(gb);
        if (ps->enable_iid) {
            int iid_mode = get_bits(gb, 3);
            if (iid_mode > 5) {
                av_log(logctx, AV_LOG_ERROR, "iid_mode %d is reserved.\n",
                       iid_mode);
                goto err;
            }
            ps->nr_iid_par    = nr_iidicc_par_tab[iid_mode];
            ps->iid_quant     = iid_mode > 2;
            ps->nr_ipdopd_par = nr_iidopd_par_tab[iid_mode];
        }
        ps->enable_icc = get_bits1(gb);
        if (ps->enable_icc) {
            ps->icc_mode = get_bits(gb, 3);
            if (ps->icc_mode > 5) {
                av_log(logctx, AV_LOG_ERROR, "icc_mode %d is reserved.\n",
                       ps->icc_mode);
                goto err;
            }
            ps->nr_icc_par = nr_iidicc_par_tab[ps->icc_mode];
        }
        ps->enable_ext = get_bits1(gb);
    }

    ps->frame_class = get_bits1(gb);
    ps->num_env_old = ps->num_env;
    ps->num_env     = num_env_tab[ps->frame_class][get_bits(gb, 2)];

    ps->border_position[0] = -1;
    if (ps->frame_class) {
        for (e = 1; e <= ps->num_env; e++) {
            ps->border_position[e] = get_bits(gb, 5);
            if (ps->border_position[e] < ps->border_position[e-1]) {
                av_log(logctx, AV_LOG_ERROR, "border_position non monotone.\n");
                goto err;
            }
        }
    } else
        for (e = 1; e <= ps->num_env; e++)
            ps->border_position[e] = (e * numQMFSlots >> ff_log2_tab[ps->num_env]) - 1;

    if (ps->enable_iid) {
        for (e = 0; e < ps->num_env; e++) {
            int dt = get_bits1(gb);
            if (read_iid_data(logctx, gb, ps, ps->iid_par, huff_iid[2*dt+ps->iid_quant], e, dt))
                goto err;
        }
    } else
        memset(ps->iid_par, 0, sizeof(ps->iid_par));

    if (ps->enable_icc)
        for (e = 0; e < ps->num_env; e++) {
            int dt = get_bits1(gb);
            if (read_icc_data(logctx, gb, ps, ps->icc_par, dt ? huff_icc_dt : huff_icc_df, e, dt))
                goto err;
        }
    else
        memset(ps->icc_par, 0, sizeof(ps->icc_par));

    if (ps->enable_ext) {
        int cnt = get_bits(gb, 4);
        if (cnt == 15) {
            cnt += get_bits(gb, 8);
        }
        cnt *= 8;
        while (cnt > 7) {
            int ps_extension_id = get_bits(gb, 2);
            cnt -= 2 + ps_read_extension_data(gb, ps, ps_extension_id);
        }
        if (cnt < 0) {
            av_log(logctx, AV_LOG_ERROR, "ps extension overflow %d\n", cnt);
            goto err;
        }
        skip_bits(gb, cnt);
    }

    ps->enable_ipdopd &= !PS_BASELINE;

    //Fix up envelopes
    if (!ps->num_env || ps->border_position[ps->num_env] < numQMFSlots - 1) {
        //Create a fake envelope
        int source = ps->num_env ? ps->num_env - 1 : ps->num_env_old - 1;
        int b;
        if (source >= 0 && source != ps->num_env) {
            if (ps->enable_iid) {
                memcpy(ps->iid_par+ps->num_env, ps->iid_par+source, sizeof(ps->iid_par[0]));
            }
            if (ps->enable_icc) {
                memcpy(ps->icc_par+ps->num_env, ps->icc_par+source, sizeof(ps->icc_par[0]));
            }
            if (ps->enable_ipdopd) {
                memcpy(ps->ipd_par+ps->num_env, ps->ipd_par+source, sizeof(ps->ipd_par[0]));
                memcpy(ps->opd_par+ps->num_env, ps->opd_par+source, sizeof(ps->opd_par[0]));
            }
        }
        if (ps->enable_iid){
            for (b = 0; b < ps->nr_iid_par; b++) {
                if (FFABS(ps->iid_par[ps->num_env][b]) > 7 + 8 * ps->iid_quant) {
                    av_log(logctx, AV_LOG_ERROR, "iid_par invalid\n");
                    goto err;
                }
            }
        }
        if (ps->enable_icc){
            for (b = 0; b < ps->nr_iid_par; b++) {
                if (ps->icc_par[ps->num_env][b] > 7U) {
                    av_log(logctx, AV_LOG_ERROR, "icc_par invalid\n");
                    goto err;
                }
            }
        }
        ps->num_env++;
        ps->border_position[ps->num_env] = numQMFSlots - 1;
    }


    ps->is34bands_old = ps->is34bands;
    if (!PS_BASELINE && (ps->enable_iid || ps->enable_icc))
        ps->is34bands = (ps->enable_iid && ps->nr_iid_par == 34) ||
                        (ps->enable_icc && ps->nr_icc_par == 34);

    //Baseline
    if (!ps->enable_ipdopd) {
        memset(ps->ipd_par, 0, sizeof(ps->ipd_par));
        memset(ps->opd_par, 0, sizeof(ps->opd_par));
    }

    if (header)
        ps->start = 1;

    bits_consumed = get_bits_count(gb) - bit_count_start;
    if (bits_consumed <= bits_left) {
        skip_bits_long(gb_host, bits_consumed);
        return bits_consumed;
    }
    av_log(logctx, AV_LOG_ERROR, "Expected to read %d PS bits actually read %d.\n", bits_left, bits_consumed);
err:
    ps->start = 0;
    skip_bits_long(gb_host, bits_left);
    memset(ps->iid_par, 0, sizeof(ps->iid_par));
    memset(ps->icc_par, 0, sizeof(ps->icc_par));
    memset(ps->ipd_par, 0, sizeof(ps->ipd_par));
    memset(ps->opd_par, 0, sizeof(ps->opd_par));
    return bits_left;
}

av_cold void ff_ps_init_common(void)
{
    static VLCElem vlc_buf[(1544 + 832 + 1024 + 1036) +
                           (544 + 544) + (32 + 32 + 32 + 32)];
    VLCInitState state = VLC_INIT_STATE(vlc_buf);
    const uint8_t (*tab)[2] = aacps_huff_tabs;

    for (int i = 0; i < FF_ARRAY_ELEMS(vlc_ps); i++) {
        vlc_ps[i] =
            ff_vlc_init_tables_from_lengths(&state, i <= 5 ? 9 : 5, huff_sizes[i],
                                            &tab[0][1], 2,
                                            &tab[0][0], 2, 1,
                                            huff_offset[i], 0);
        tab += huff_sizes[i];
    }
}
