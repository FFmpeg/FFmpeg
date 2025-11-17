/*
 * Copyright (c) 2025 Lynne <dev@lynne.ee>
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

#include "aacdec_tab.h"
#include "libavcodec/get_bits.h"
#include "libavutil/macros.h"

#include "aacdec_usac_mps212.h"

static int huff_dec_1D(GetBitContext *gb, const int16_t (*tab)[2])
{
    int idx = 0;
    do {
        /* Overreads are not possible here, the array forms a closed set */
        idx = tab[idx][get_bits1(gb)];
    } while (idx > 0);
    return idx;
}

static int huff_dec_2D(GetBitContext *gb, const int16_t (*tab)[2], int16_t ret[2])
{
    int idx = huff_dec_1D(gb, tab);
    if (!idx) { /* Escape */
        ret[0] = 0;
        ret[1] = 1;
        return 1;
    }

    idx = -(idx + 1);
    ret[0] = idx >> 4;
    ret[1] = idx & 0xf;
    return 0;
}

static int huff_data_1d(GetBitContext *gb, int16_t *data, int data_bands,
                        enum AACMPSDataType data_type, int diff_freq, int p0_flag)
{
    const int16_t (*hcod_first_band)[2];
    const int16_t (*hcod1D)[2];

    switch (data_type) {
    case MPS_CLD:
        hcod_first_band = ff_aac_hcod_firstband_CLD;
        hcod1D = ff_aac_hcod1D_CLD[diff_freq];
        break;
    case MPS_ICC:
        hcod_first_band = ff_aac_hcod_firstband_ICC;
        hcod1D = ff_aac_hcod1D_ICC;
        break;
    case MPS_IPD:
        hcod_first_band = ff_aac_hcod_firstband_IPD;
        hcod1D = ff_aac_hcod1D_IPD[diff_freq];
        if (data_bands == 1)
            hcod1D = ff_aac_hcod1D_IPD[!diff_freq];
        break;
    }

    if (p0_flag)
        data[0] = -(huff_dec_1D(gb, hcod_first_band) + 1);

    for (int off = diff_freq; off < data_bands; off++) {
        int16_t val = -(huff_dec_1D(gb, hcod1D) + 1);
        if (val && data_type != MPS_IPD)
            val = get_bits1(gb) ? -val : val;
        data[off] = val;
    }

    return 0;
}

static void symmetry_data(GetBitContext *gb, int16_t data[2],
                          uint8_t lav, enum AACMPSDataType data_type)
{
    int16_t sum = data[0] + data[1];
    int16_t diff = data[0] - data[1];

    if (sum > lav) {
        data[0] = -sum + (2*lav + 1);
        data[1] = -diff;
    } else {
        data[0] = sum;
        data[1] = diff;
    }

    if ((data_type != MPS_IPD) && (data[0] + data[1])) {
        int sym = get_bits1(gb) ? -1 : 1;
        data[0] *= sym;
        data[1] *= sym;
    }

    if (data[0] - data[1]) {
        if (get_bits1(gb))
            FFSWAP(int16_t, data[0], data[1]);
    }
}

/* NB: NOT a standard integer log2! */
static int mps_log2(int s) {
    if (s)
        s--;
    int v = 0;
    while (s) {
        s >>= 1;
        v++;
    }
    return v;
}

static void pcm_decode(GetBitContext *gb, int16_t *data0, int16_t *data1,
                       int16_t offset, int nb_pcm_data_bands,
                       int nb_quant_steps, int nb_levels)
{
    int max_group_len;
    switch (nb_levels) {
    case  3: max_group_len = 5; break;
    case  7: max_group_len = 6; break;
    case 11: max_group_len = 2; break;
    case 13: max_group_len = 4; break;
    case 19: max_group_len = 4; break;
    case 25: max_group_len = 3; break;
    case 51: max_group_len = 4; break;
    case  4: case  8: case 15: case 16: case 26: case 31:
        max_group_len = 1;
        break;
    default:
        return;
    };

    int pcm_chunk_size[7] = { 0 };

    int tmp = 1;
    for (int i = 1; i <= max_group_len; i++) {
        tmp *= nb_levels;
        pcm_chunk_size[i] = mps_log2(tmp);
    }

    for (int i = 0; i < nb_pcm_data_bands; i+= max_group_len) {
        int group_len = FFMIN(max_group_len, nb_pcm_data_bands - i);

        int pcm = get_bits(gb, pcm_chunk_size[group_len]);
        for (int j = 0; j < group_len; j++) {
            int idx = i + (group_len - 1) - j;
            int val = pcm % nb_levels;
            if (data0 && data1) {
                if (idx % 2)
                    data1[idx / 2] = val - offset;
                else
                    data0[idx / 2] = val - offset;
            } else if (!data1) {
                data0[idx] = val - offset;
            } else if (!data0) {
                data1[idx] = val - offset;
            }
            pcm = (pcm - val) / nb_levels;
        }
    }
}

static void huff_data_2d(GetBitContext *gb, int16_t *part0_data[2], int16_t (*data)[2],
                         int data_bands, int stride, enum AACMPSDataType data_type,
                         int diff_freq, int freq_pair)
{
    int16_t lav_idx = huff_dec_1D(gb, ff_aac_hcod_lav_idx);
    uint8_t lav = ff_aac_lav_tab_XXX[data_type][-(lav_idx + 1)];

    const int16_t (*hcod1D)[2];
    const int16_t (*hcod2D)[2];
    switch (data_type) {
    case MPS_CLD:
        hcod1D = ff_aac_hcod_firstband_CLD;
        switch (lav) {
        case 3: hcod2D = ff_aac_hcod2D_CLD_03[freq_pair][diff_freq]; break;
        case 5: hcod2D = ff_aac_hcod2D_CLD_05[freq_pair][diff_freq]; break;
        case 7: hcod2D = ff_aac_hcod2D_CLD_07[freq_pair][diff_freq]; break;
        case 9: hcod2D = ff_aac_hcod2D_CLD_09[freq_pair][diff_freq]; break;
        }
        break;
    case MPS_ICC:
        hcod1D = ff_aac_hcod_firstband_ICC;
        switch (lav) {
        case 1: hcod2D = ff_aac_hcod2D_ICC_01[freq_pair][diff_freq]; break;
        case 3: hcod2D = ff_aac_hcod2D_ICC_03[freq_pair][diff_freq]; break;
        case 5: hcod2D = ff_aac_hcod2D_ICC_05[freq_pair][diff_freq]; break;
        case 7: hcod2D = ff_aac_hcod2D_ICC_07[freq_pair][diff_freq]; break;
        }
        break;
    case MPS_IPD:
        hcod1D = ff_aac_hcod_firstband_IPD;
        switch (lav) {
        case 1: hcod2D = ff_aac_hcod2D_IPD_01[freq_pair][diff_freq]; break;
        case 3: hcod2D = ff_aac_hcod2D_IPD_03[freq_pair][diff_freq]; break;
        case 5: hcod2D = ff_aac_hcod2D_IPD_05[freq_pair][diff_freq]; break;
        case 7: hcod2D = ff_aac_hcod2D_IPD_07[freq_pair][diff_freq]; break;
        }
        break;
    }

    if (part0_data[0])
        part0_data[0][0] = -(huff_dec_1D(gb, hcod1D) + 1);
    if (part0_data[1])
        part0_data[1][0] = -(huff_dec_1D(gb, hcod1D) + 1);

    int i = 0;
    int esc_cnt = 0;
    int16_t esc_data[2][28];
    int esc_idx[28];
    for (; i < data_bands; i += stride) {
        if (huff_dec_2D(gb, hcod2D, data[i]))
            esc_idx[esc_cnt++] = i; /* Escape */
        else
            symmetry_data(gb, data[i], lav, data_type);
    }

    if (esc_cnt) {
        pcm_decode(gb, esc_data[0], esc_data[1],
                   0, 2*esc_cnt, 0, (2*lav + 1));
        for (i = 0; i < esc_cnt; i++) {
            data[esc_idx[i]][0] = esc_data[0][i] - lav;
            data[esc_idx[i]][0] = esc_data[0][i] - lav;
        }
    }
}

static int huff_decode(GetBitContext *gb, int16_t *data[2],
                       enum AACMPSDataType data_type, int diff_freq[2],
                       int num_val, int *time_pair)
{
    int16_t pair_vec[28][2];
    int num_val_ch[2] = { num_val, num_val };
    int16_t *p0_data[2][2] = { 0 };
    int df_rest_flag[2] = { 0, 0 };

    /* Coding scheme */
    int dim = get_bits1(gb);
    if (dim) { /* 2D */
        *time_pair = 0;
        if (data[0] && data[1])
            *time_pair = get_bits1(gb);

        if (*time_pair) {
            if (diff_freq[0] || diff_freq[1]) {
                p0_data[0][0] = data[0];
                p0_data[0][1] = data[1];

                data[0] += 1;
                data[1] += 1;

                num_val_ch[0] -= 1;
            }

            int diff_mode = 1;
            if (!diff_freq[0] || !diff_freq[1])
                diff_mode = 0; // time

            huff_data_2d(gb, p0_data[0], pair_vec, num_val_ch[0], 1, data_type,
                         diff_mode, 0);

            for (int i = 0; i < num_val_ch[0]; i++) {
                data[0][i] = pair_vec[i][0];
                data[1][i] = pair_vec[i][1];
            }
        } else {
            if (data[0]) {
                if (diff_freq[0]) {
                    p0_data[0][0] = data[0];
                    p0_data[0][1] = NULL;

                    num_val_ch[0] -= 1;
                    data[0]++;
                }
                df_rest_flag[0] = num_val_ch[0] % 2;
                if (df_rest_flag[0])
                    num_val_ch[0] -= 1;
                if (num_val_ch[0] < 0)
                    return AVERROR(EINVAL);
            }

            if (data[1]) {
                if (diff_freq[1]) {
                    p0_data[1][0] = NULL;
                    p0_data[1][1] = data[1];

                    num_val_ch[1] -= 1;
                    data[1]++;
                }
                df_rest_flag[1] = num_val_ch[1] % 2;
                if (df_rest_flag[1])
                    num_val_ch[1] -= 1;
                if (num_val_ch[1] < 0)
                    return AVERROR(EINVAL);
            }

            if (data[0]) {
                huff_data_2d(gb, p0_data[0], pair_vec, num_val_ch[0], 2, data_type,
                             diff_freq[0], 1);
                if (df_rest_flag[0])
                    huff_data_1d(gb, data[0] + num_val_ch[0], 1,
                                 data_type, !diff_freq[0], 0);
            }
            if (data[1]) {
                huff_data_2d(gb, p0_data[1], pair_vec + 1, num_val_ch[1], 2, data_type,
                             diff_freq[1], 1);
                if (df_rest_flag[1])
                    huff_data_1d(gb, data[1] + num_val_ch[1], 1,
                                 data_type, !diff_freq[1], 0);
            }
        }
    } else { /* 1D */
        if (data[0])
            huff_data_1d(gb, data[0], num_val, data_type, diff_freq[0], diff_freq[0]);
        if (data[1])
            huff_data_1d(gb, data[1], num_val, data_type, diff_freq[1], diff_freq[1]);
    }

    return 0;
}

static void diff_freq_decode(const int16_t *diff, int16_t *out, int nb_val)
{
    int i = 0;
    out[0] = diff[0];
    for (i = 1; i < nb_val; i++)
        out[i] = out[i - 1] + diff[i];
}

static void diff_time_decode_backwards(const int16_t *prev, const int16_t *diff,
                                       int16_t *out, const int mixed_diff_type,
                                       const int nb_val)
{
    if (mixed_diff_type)
        out[0] = diff[0];
    for (int i = mixed_diff_type; i < nb_val; i++)
        out[i] = prev[i] + diff[i];
}

static void diff_time_decode_forwards(const int16_t *prev, const int16_t *diff,
                                      int16_t *out, const int mixed_diff_type,
                                      const int nb_val)
{
    if (mixed_diff_type)
        out[0] = diff[0];
    for (int i = mixed_diff_type; i < nb_val; i++)
        out[i] = prev[i] - diff[i];
}

static void attach_lsb(GetBitContext *gb, int16_t *data_msb,
                       int offset, int nb_lsb, int nb_val,
                       int16_t *data)
{
    for (int i = 0; i < nb_val; i++) {
        int msb = data_msb[i];
        if (nb_lsb > 0) {
            uint32_t lsb = get_bits(gb, nb_lsb);
            data[i] = ((msb << nb_lsb) | lsb) - offset;
        } else {
            data[i] = msb - offset;
        }
    }
}

static int ec_pair_dec(GetBitContext *gb,
                       int16_t set1[MPS_MAX_PARAM_BANDS],
                       int16_t set2[MPS_MAX_PARAM_BANDS], int16_t *last,
                       enum AACMPSDataType data_type, int start_band, int nb_bands,
                       int pair, int coarse,
                       int diff_time_back)
{
    int attach_lsb_flag = 0;
    int quant_levels = 0;
    int quant_offset = 0;

    switch (data_type) {
    case MPS_CLD:
        if (coarse) {
            attach_lsb_flag = 0;
            quant_levels = 15;
            quant_offset = 7;
      } else {
            attach_lsb_flag = 0;
            quant_levels = 31;
            quant_offset = 15;
        }
        break;
    case MPS_ICC:
        if (coarse) {
            attach_lsb_flag = 0;
            quant_levels = 4;
            quant_offset = 0;
        } else {
            attach_lsb_flag = 0;
            quant_levels = 8;
            quant_offset = 0;
        }
        break;
    case MPS_IPD:
        if (!coarse) {
            attach_lsb_flag = 1;
            quant_levels = 16;
            quant_offset = 0;
        } else {
            attach_lsb_flag = 0;
            quant_levels = 8;
            quant_offset = 0;
        }
        break;
    }

    int16_t last_msb[28] = { 0 };
    int16_t data_pair[2][28] = { 0 };
    int16_t data_diff[2][28] = { 0 };
    int16_t *p_data[2];

    int pcm_coding = get_bits1(gb);
    if (pcm_coding) { /* bsPcmCoding */
        int nb_pcm_vals;
        if (pair) {
            p_data[0] = data_pair[0];
            p_data[1] = data_pair[1];
            nb_pcm_vals = 2 * nb_bands;
        } else {
            p_data[0] = data_pair[0];
            p_data[1] = NULL;
            nb_pcm_vals = nb_bands;
        }

        int nb_quant_steps;
        switch (data_type) {
        case MPS_CLD: nb_quant_steps = coarse ? 15 : 31; break;
        case MPS_ICC: nb_quant_steps = coarse ?  4 :  8; break;
        case MPS_IPD: nb_quant_steps = coarse ?  8 : 16; break;
        }
        pcm_decode(gb, p_data[0], p_data[1], quant_offset, nb_pcm_vals,
                   nb_quant_steps, quant_levels);

        memcpy(&set1[start_band], data_pair[0], 2*nb_bands);
        if (pair)
            memcpy(&set2[start_band], data_pair[1], 2*nb_bands);

        return 0;
    }

    if (pair) {
        p_data[0] = data_pair[0];
        p_data[1] = data_pair[1];
    } else {
        p_data[0] = data_pair[0];
        p_data[1] = NULL;
    }

    int diff_freq[2] = { 1, 1 };
    int backwards = 1;

    if (pair || diff_time_back)
        diff_freq[0] = !get_bits1(gb);

    if (pair && (diff_freq[0] || diff_time_back))
        diff_freq[1] = !get_bits1(gb);

    int time_pair;
    huff_decode(gb, p_data, data_type, diff_freq,
                nb_bands, &time_pair);

    /* Differential decoding */
    if (!diff_freq[0] || !diff_freq[1]) {
        if (0 /* 1 if SAOC */) {
            backwards = 1;
        } else {
            if (pair) {
                if (!diff_freq[0] && !diff_time_back)
                    backwards = 0;
                else if (!diff_freq[1])
                    backwards = 1;
                else
                    backwards = !get_bits1(gb);
            } else {
                backwards = 1;
            }
        }
    }

    int mixed_time_pair = (diff_freq[0] != diff_freq[1]) && time_pair;

    if (backwards) {
        if (diff_freq[0]) {
            diff_freq_decode(data_diff[0], data_pair[0], nb_bands);
        } else {
            for (int i = 0; i < nb_bands; i++) {
                last_msb[i] = last[i + start_band] + quant_offset;
                if (attach_lsb_flag) {
                    last_msb[i] >>= 1;
                }
            }
            diff_time_decode_backwards(last_msb, data_diff[0], data_pair[0],
                                       mixed_time_pair, nb_bands);
        }

        if (diff_freq[1])
            diff_freq_decode(data_diff[1], data_pair[1], nb_bands);
        else
            diff_time_decode_backwards(data_pair[0], data_diff[1],
                                       data_pair[1], mixed_time_pair, nb_bands);
    } else {
        diff_freq_decode(data_diff[1], data_pair[1], nb_bands);

        if (diff_freq[0])
          diff_freq_decode(data_diff[0], data_pair[0], nb_bands);
        else
          diff_time_decode_forwards(data_pair[1], data_diff[0], data_pair[0],
                                    mixed_time_pair, nb_bands);
    }

    /* Decode LSBs */
    attach_lsb(gb, p_data[0], quant_offset, attach_lsb_flag,
               nb_bands, p_data[0]);
    if (pair)
        attach_lsb(gb, p_data[1], quant_offset, attach_lsb_flag,
                   nb_bands, p_data[1]);

    memcpy(&set1[start_band], data_pair[0], 2*nb_bands);
    if (pair)
        memcpy(&set2[start_band], data_pair[1], 2*nb_bands);

    return 0;
}

static void coarse_to_fine(int16_t *data, enum AACMPSDataType data_type,
                           int start_band, int end_band)
{
    for (int i = start_band; i < end_band; i++)
        data[i] <<= 1;
    if (data_type == MPS_CLD) {
        for (int i = start_band; i < end_band; i++) {
            if (data[i] == -14)
                data[i] = -15;
            else if (data[i] == 14)
                data[i] = 15;
        }
    }
}

static void fine_to_coarse(int16_t *data, enum AACMPSDataType data_type,
                           int start_band, int end_band)
{
    for (int i = start_band; i < end_band; i++) {
        if (data_type == MPS_CLD)
            data[i] /= 2;
        else
            data[i] >>= 1;
    }
}

static int get_freq_strides(int16_t *freq_strides, int band_stride,
                            int start_band, int end_band)
{
    int data_bands = (end_band - start_band - 1) / band_stride + 1;

    freq_strides[0] = start_band;
    for (int i = 1; i <= data_bands; i++)
        freq_strides[i] = freq_strides[i - 1] + band_stride;

    int offs = 0;
    while (freq_strides[data_bands] > end_band) {
        if (offs < data_bands)
            offs++;
        for (int i = offs; i <= data_bands; i++) {
            freq_strides[i]--;
        }
    }

    return data_bands;
}

static const int stride_table[4] = { 1, 2, 5, 28 };

int ff_aac_ec_data_dec(GetBitContext *gb, AACMPSLosslessData *ld,
                       enum AACMPSDataType data_type,
                       int default_val,
                       int start_band, int end_band, int frame_indep_flag,
                       int indep_flag, int nb_param_sets)
{
    for (int i = 0; i < nb_param_sets; i++) {
        ld->data_mode[i] = get_bits(gb, 2);
        /* Error checking */
        if ((indep_flag && !i && (ld->data_mode[i] == 1 || ld->data_mode[i] == 2)) ||
            ((i == (nb_param_sets - 1) && (ld->data_mode[i] == 2)))) {
            return AVERROR(EINVAL);
        }
    }

    int set_idx = 0;
    int data_pair = 0;
    bool old_coarse = ld->quant_coarse_prev;

    for (int i = 0; i < nb_param_sets; i++) {
        if (!ld->data_mode[i]) {
            for (int j = start_band; j < end_band; j++)
                ld->last_data[j] = default_val;
            old_coarse = 0;
        }

        if (ld->data_mode[i] != 3) {
            continue;
        } else if (data_pair) {
            data_pair = 0;
            continue;
        }

        data_pair = get_bits1(gb);
        ld->coarse_quant[set_idx] = get_bits1(gb);
        ld->freq_res[set_idx] = get_bits(gb, 2);

        if (ld->coarse_quant[set_idx] != old_coarse) {
            if (old_coarse)
                coarse_to_fine(ld->last_data, data_type, start_band, end_band);
            else
                fine_to_coarse(ld->last_data, data_type, start_band, end_band);
        }

        int data_bands = get_freq_strides(ld->freq_res,
                                          stride_table[ld->freq_res[set_idx]],
                                          start_band, end_band);

        if (set_idx + data_pair > MPS_MAX_PARAM_SETS)
            return AVERROR(EINVAL);

        for (int j = 0; j < data_bands; j++)
            ld->last_data[start_band + j] = ld->last_data[ld->freq_res[j]];

        int err = ec_pair_dec(gb,
                              ld->data[set_idx + 0], ld->data[set_idx + 1],
                              ld->last_data, data_type, start_band, end_band - start_band,
                              data_pair, ld->coarse_quant[set_idx],
                              !(indep_flag && (i == 0)) || (set_idx > 0));
        if (err < 0)
            return err;

        if (data_type == MPS_IPD) {
            const int mask = ld->coarse_quant[set_idx] ? 0x7 : 0xF;
            for (int j = 0; j < data_bands; j++)
                for (int k = ld->freq_res[j + 0]; k < ld->freq_res[j + 1]; k++)
                    ld->last_data[k] = ld->data[set_idx + data_pair][start_band + j] & mask;
        } else {
            for (int j = 0; j < data_bands; j++)
                for (int k = ld->freq_res[j + 0]; k < ld->freq_res[j + 1]; k++)
                    ld->last_data[k] = ld->data[set_idx + data_pair][start_band + j];
        }

        old_coarse = ld->coarse_quant[set_idx];
        if (data_pair) {
            ld->coarse_quant[set_idx + 1] = ld->coarse_quant[set_idx];
            ld->freq_res[set_idx + 1] = ld->freq_res[set_idx];
        }
        set_idx += data_pair + 1;
    }

    ld->quant_coarse_prev = old_coarse;

    return 0;
}

int ff_aac_huff_dec_reshape(GetBitContext *gb, int16_t *out_data,
                            int nb_val)
{
    int val, len;
    int val_received = 0;
    int16_t rl_data[2] = { 0 };

    while (val_received < nb_val) {
        huff_dec_2D(gb, ff_aac_hcod2D_reshape, rl_data);
        val = rl_data[0];
        len = rl_data[1] + 1;
        if (val_received + len > nb_val)
            return AVERROR(EINVAL);
        for (int i = val_received; i < val_received + len; i++)
            out_data[i] = val;
        val_received += len;
    }

    return 0;
}

static void create_mapping(int map[MPS_MAX_PARAM_BANDS + 1],
                           int start_band, int stop_band, int stride)
{
    int diff[MPS_MAX_PARAM_BANDS + 1];
    int src_bands = stop_band - start_band;
    int dst_bands = (src_bands - 1) / stride + 1;

    if (dst_bands < 1)
        dst_bands = 1;

    int bands_achived = dst_bands * stride;
    int bands_diff = src_bands - bands_achived;
    for (int i = 0; i < dst_bands; i++)
        diff[i] = stride;

    int incr, k;
    if (bands_diff > 0) {
        incr = -1;
        k = dst_bands - 1;
    } else {
        incr = 1;
        k = 0;
    }

    while (bands_diff != 0) {
        diff[k] = diff[k] - incr;
        k = k + incr;
        bands_diff = bands_diff + incr;
        if (k >= dst_bands) {
            if (bands_diff > 0) {
                k = dst_bands - 1;
            } else if (bands_diff < 0) {
                k = 0;
            }
        }
    }

    map[0] = start_band;
    for (int i = 0; i < dst_bands; i++)
        map[i + 1] = map[i] + diff[i];
}

static void map_freq(int16_t *dst, const int16_t *src,
                     int *map, int nb_bands)
{
    for (int i = 0; i < nb_bands; i++) {
        int value = src[i + map[0]];
        int start_band = map[i];
        int stop_band = map[i + 1];
        for (int j = start_band; j < stop_band; j++) {
            dst[j] = value;
        }
    }
}

static int deq_idx(int value, enum AACMPSDataType data_type)
{
    int idx = -1;

    switch (data_type) {
    case MPS_CLD:
        if (((value + 15) >= 0) && ((value + 15) < 31))
            idx = (value + 15);
        break;
    case MPS_ICC:
        if ((value >= 0) && (value < 8))
        idx = value;
        break;
    case MPS_IPD:
        /* (+/-)15 * MAX_PARAMETER_BANDS for differential coding in frequency
         * domain (according to rbl) */
      if ((value >= -420) && (value <= 420))
          idx = (value & 0xf);
        break;
    }

    return idx;
}

int ff_aac_map_index_data(AACMPSLosslessData *ld,
                          enum AACMPSDataType data_type,
                          int dst_idx[MPS_MAX_PARAM_SETS][MPS_MAX_PARAM_BANDS],
                          int default_value, int start_band, int stop_band,
                          int nb_param_sets, const int *param_set_idx,
                          int extend_frame)
{
    if (nb_param_sets > MPS_MAX_PARAM_SETS)
        return AVERROR(EINVAL);

    int data_mode_3_idx[MPS_MAX_PARAM_SETS];
    int nb_data_mode_3 = 0;
    for (int i = 0; i < nb_param_sets; i++) {
        if (ld->data_mode[i] == 3) {
            data_mode_3_idx[nb_data_mode_3] = i;
            nb_data_mode_3++;
        }
    }

    int set_idx = 0;

    /* Prepare data */
    int interpolate[MPS_MAX_PARAM_SETS] = { 0 };
    int16_t tmp_idx_data[MPS_MAX_PARAM_SETS][MPS_MAX_PARAM_BANDS];
    for (int i = 0; i < nb_param_sets; i++) {
        if (ld->data_mode[i] == 0) {
            ld->coarse_quant_no[i] = 0;
            for (int band = start_band; band < stop_band; band++)
                tmp_idx_data[i][band] = default_value;
            for (int band = start_band; band < stop_band; band++)
                ld->last_data[band] = tmp_idx_data[i][band];
            ld->quant_coarse_prev = 0;
        }

        if (ld->data_mode[i] == 1) {
            for (int band = start_band; band < stop_band; band++)
                tmp_idx_data[i][band] = ld->last_data[band];
            ld->coarse_quant_no[i] = ld->quant_coarse_prev;
        }

        if (ld->data_mode[i] == 2) {
            for (int band = start_band; band < stop_band; band++)
                tmp_idx_data[i][band] = ld->last_data[band];
            ld->coarse_quant_no[i] = ld->quant_coarse_prev;
            interpolate[i] = 1;
        } else {
            interpolate[i] = 0;
        }

        if (ld->data_mode[i] == 3) {
            int stride;

            int parmSlot = data_mode_3_idx[set_idx];
            stride = stride_table[ld->freq_res[set_idx]];
            int dataBands = (stop_band - start_band - 1) / stride + 1;

            int tmp[MPS_MAX_PARAM_BANDS + 1];
            create_mapping(tmp, start_band, stop_band, stride);
            map_freq(tmp_idx_data[parmSlot], ld->data[set_idx],
                     tmp, dataBands);

            for (int band = start_band; band < stop_band; band++)
                ld->last_data[band] = tmp_idx_data[parmSlot][band];

            ld->quant_coarse_prev = ld->coarse_quant[set_idx];
            ld->coarse_quant_no[i] = ld->coarse_quant[set_idx];

            set_idx++;
        }
    }

    /* Map all coarse data to fine */
    for (int i = 0; i < nb_param_sets; i++) {
        if (ld->coarse_quant_no[i] == 1) {
            coarse_to_fine(tmp_idx_data[i], data_type, start_band,
                           stop_band - start_band);
            ld->coarse_quant_no[i] = 0;
        }
    }

    /* Interpolate */
    int i1 = 0;
    for (int i = 0; i < nb_param_sets; i++) {
        if (interpolate[i] != 1) {
            i1 = i;
        } else {
            int xi, i2, x1, x2;

            for (i2 = i; i2 < nb_param_sets; i2++)
                if (interpolate[i2] != 1)
                    break;
            if (i2 >= nb_param_sets)
                return AVERROR(EINVAL);

            x1 = param_set_idx[i1];
            xi = param_set_idx[i];
            x2 = param_set_idx[i2];

            for (int band = start_band; band < stop_band; band++) {
                int yi, y1, y2;
                y1 = tmp_idx_data[i1][band];
                y2 = tmp_idx_data[i2][band];
                if (x1 != x2) {
                    yi = y1 + (xi - x1) * (y2 - y1) / (x2 - x1);
                } else {
                    yi = y1 /*+ (xi-x1)*(y2-y1)/1e-12*/;
                }
                tmp_idx_data[i][band] = yi;
            }
        }
    }

    /* Dequantize data and apply factorCLD if necessary */
    for (int ps = 0; ps < nb_param_sets; ps++) {
        /* Dequantize data */
        for (int band = start_band; band < stop_band; band++) {
            dst_idx[ps][band] = deq_idx(tmp_idx_data[ps][band],
                                        data_type);
            if (dst_idx[ps][band] == -1)
                dst_idx[ps][band] = default_value;
        }
    }

    if (extend_frame) {
        if (data_type == MPS_IPD)
            ld->coarse_quant[nb_param_sets] = ld->coarse_quant[nb_param_sets - 1];
        for (int band = start_band; band < stop_band; band++)
            dst_idx[nb_param_sets][band] = dst_idx[nb_param_sets - 1][band];
    }

    return 0;
}
