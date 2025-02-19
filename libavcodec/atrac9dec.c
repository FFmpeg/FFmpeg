/*
 * ATRAC9 decoder
 * Copyright (c) 2018 Rostislav Pehlivanov <atomnuker@gmail.com>
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

#include "libavutil/channel_layout.h"
#include "libavutil/mem.h"
#include "libavutil/thread.h"

#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"
#include "atrac9tab.h"
#include "libavutil/tx.h"
#include "libavutil/lfg.h"
#include "libavutil/float_dsp.h"
#include "libavutil/mem_internal.h"

#define ATRAC9_SF_VLC_BITS 8
#define ATRAC9_COEFF_VLC_BITS 9

typedef struct ATRAC9ChannelData {
    int band_ext;
    int q_unit_cnt;
    int band_ext_data[4];
    int32_t scalefactors[31];
    int32_t scalefactors_prev[31];

    int precision_coarse[30];
    int precision_fine[30];
    int precision_mask[30];

    int codebookset[30];

    int32_t q_coeffs_coarse[256];
    int32_t q_coeffs_fine[256];

    DECLARE_ALIGNED(32, float, coeffs  )[256];
    DECLARE_ALIGNED(32, float, prev_win)[128];
} ATRAC9ChannelData;

typedef struct ATRAC9BlockData {
    ATRAC9ChannelData channel[2];

    /* Base */
    int band_count;
    int q_unit_cnt;
    int q_unit_cnt_prev;

    /* Stereo block only */
    int stereo_q_unit;

    /* Band extension only */
    int has_band_ext;
    int has_band_ext_data;
    int band_ext_q_unit;

    /* Gradient */
    int grad_mode;
    int grad_boundary;
    int gradient[31];

    /* Stereo */
    int cpe_base_channel;
    int is_signs[30];

    int reuseable;

} ATRAC9BlockData;

typedef struct ATRAC9Context {
    AVCodecContext *avctx;
    AVFloatDSPContext *fdsp;
    AVTXContext *tx;
    av_tx_fn tx_fn;
    ATRAC9BlockData block[5];
    AVLFG lfg;

    /* Set on init */
    int frame_log2;
    int avg_frame_size;
    int frame_count;
    int samplerate_idx;
    const ATRAC9BlockConfig *block_config;

    /* Generated on init */
    uint8_t alloc_curve[48][48];
    DECLARE_ALIGNED(32, float, imdct_win)[256];

    DECLARE_ALIGNED(32, float, temp)[2048];
} ATRAC9Context;

static const VLCElem *sf_vlc[2][8];       /* Signed/unsigned, length */
static const VLCElem *coeff_vlc[2][8][4]; /* Cookbook, precision, cookbook index */

static inline int parse_gradient(ATRAC9Context *s, ATRAC9BlockData *b,
                                 GetBitContext *gb)
{
    int grad_range[2];
    int grad_value[2];
    int values, sign, base;
    uint8_t *curve;
    float scale;

    b->grad_mode = get_bits(gb, 2);
    if (b->grad_mode) {
        grad_range[0] = get_bits(gb, 5);
        grad_range[1] = 31;
        grad_value[0] = get_bits(gb, 5);
        grad_value[1] = 31;
    } else {
        grad_range[0] = get_bits(gb, 6);
        grad_range[1] = get_bits(gb, 6) + 1;
        grad_value[0] = get_bits(gb, 5);
        grad_value[1] = get_bits(gb, 5);
    }
    b->grad_boundary = get_bits(gb, 4);

    if (grad_range[0] >= grad_range[1] || grad_range[1] > 31)
        return AVERROR_INVALIDDATA;

    if (b->grad_boundary > b->q_unit_cnt)
        return AVERROR_INVALIDDATA;

    values    = grad_value[1] - grad_value[0];
    sign      = 1 - 2*(values < 0);
    base      = grad_value[0] + sign;
    scale     = (FFABS(values) - 1) / 31.0f;
    curve     = s->alloc_curve[grad_range[1] - grad_range[0] - 1];

    for (int i = 0; i <= b->q_unit_cnt; i++)
        b->gradient[i] = grad_value[i >= grad_range[0]];

    for (int i = grad_range[0]; i < grad_range[1]; i++)
        b->gradient[i] = base + sign*((int)(scale*curve[i - grad_range[0]]));

    return 0;
}

static inline void calc_precision(ATRAC9Context *s, ATRAC9BlockData *b,
                                  ATRAC9ChannelData *c)
{
    memset(c->precision_mask, 0, sizeof(c->precision_mask));
    for (int i = 1; i < b->q_unit_cnt; i++) {
        const int delta = FFABS(c->scalefactors[i] - c->scalefactors[i - 1]) - 1;
        if (delta > 0) {
            const int neg = c->scalefactors[i - 1] > c->scalefactors[i];
            c->precision_mask[i - neg] += FFMIN(delta, 5);
        }
    }

    if (b->grad_mode) {
        for (int i = 0; i < b->q_unit_cnt; i++) {
            c->precision_coarse[i] = c->scalefactors[i];
            c->precision_coarse[i] += c->precision_mask[i] - b->gradient[i];
            if (c->precision_coarse[i] < 0)
                continue;
            switch (b->grad_mode) {
            case 1:
                c->precision_coarse[i] >>= 1;
                break;
            case 2:
                c->precision_coarse[i] = (3 * c->precision_coarse[i]) >> 3;
                break;
            case 3:
                c->precision_coarse[i] >>= 2;
                break;
            }
        }
    } else {
        for (int i = 0; i < b->q_unit_cnt; i++)
            c->precision_coarse[i] = c->scalefactors[i] - b->gradient[i];
    }


    for (int i = 0; i < b->q_unit_cnt; i++)
        c->precision_coarse[i] = FFMAX(c->precision_coarse[i], 1);

    for (int i = 0; i < b->grad_boundary; i++)
        c->precision_coarse[i]++;

    for (int i = 0; i < b->q_unit_cnt; i++) {
        c->precision_fine[i] = 0;
        if (c->precision_coarse[i] > 15) {
            c->precision_fine[i] = FFMIN(c->precision_coarse[i], 30) - 15;
            c->precision_coarse[i] = 15;
        }
    }
}

static inline int parse_band_ext(ATRAC9Context *s, ATRAC9BlockData *b,
                                 GetBitContext *gb, int stereo)
{
    int ext_band = 0;

    if (b->has_band_ext) {
        if (b->q_unit_cnt < 13 || b->q_unit_cnt > 20)
            return AVERROR_INVALIDDATA;
        ext_band = at9_tab_band_ext_group[b->q_unit_cnt - 13][2];
        if (stereo) {
            b->channel[1].band_ext = get_bits(gb, 2);
            b->channel[1].band_ext = ext_band > 2 ? b->channel[1].band_ext : 4;
        } else {
            skip_bits1(gb);
        }
    }

    b->has_band_ext_data = get_bits1(gb);
    if (!b->has_band_ext_data)
        return 0;

    if (!b->has_band_ext) {
        skip_bits(gb, 2);
        skip_bits_long(gb, get_bits(gb, 5));
        return 0;
    }

    b->channel[0].band_ext = get_bits(gb, 2);
    b->channel[0].band_ext = ext_band > 2 ? b->channel[0].band_ext : 4;

    if (!get_bits(gb, 5)) {
        for (int i = 0; i <= stereo; i++) {
            ATRAC9ChannelData *c = &b->channel[i];
            const int count = at9_tab_band_ext_cnt[c->band_ext][ext_band];
            for (int j = 0; j < count; j++) {
                int len = at9_tab_band_ext_lengths[c->band_ext][ext_band][j];
                c->band_ext_data[j] = av_clip_uintp2_c(c->band_ext_data[j], len);
            }
        }

        return 0;
    }

    for (int i = 0; i <= stereo; i++) {
        ATRAC9ChannelData *c = &b->channel[i];
        const int count = at9_tab_band_ext_cnt[c->band_ext][ext_band];
        for (int j = 0; j < count; j++) {
            int len = at9_tab_band_ext_lengths[c->band_ext][ext_band][j];
            c->band_ext_data[j] = get_bits(gb, len);
        }
    }

    return 0;
}

static inline int read_scalefactors(ATRAC9Context *s, ATRAC9BlockData *b,
                                    ATRAC9ChannelData *c, GetBitContext *gb,
                                    int channel_idx, int first_in_pkt)
{
    static const uint8_t mode_map[2][4] = { { 0, 1, 2, 3 }, { 0, 2, 3, 4 } };
    const int mode = mode_map[channel_idx][get_bits(gb, 2)];

    memset(c->scalefactors, 0, sizeof(c->scalefactors));

    if (first_in_pkt && (mode == 4 || ((mode == 3) && !channel_idx))) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid scalefactor coding mode!\n");
        return AVERROR_INVALIDDATA;
    }

    switch (mode) {
    case 0: { /* VLC delta offset */
        const uint8_t *sf_weights = at9_tab_sf_weights[get_bits(gb, 3)];
        const int base = get_bits(gb, 5);
        const int len = get_bits(gb, 2) + 3;
        const VLCElem *tab = sf_vlc[0][len];

        c->scalefactors[0] = get_bits(gb, len);

        for (int i = 1; i < b->band_ext_q_unit; i++) {
            int val = c->scalefactors[i - 1] + get_vlc2(gb, tab,
                                                        ATRAC9_SF_VLC_BITS, 1);
            c->scalefactors[i] = val & ((1 << len) - 1);
        }

        for (int i = 0; i < b->band_ext_q_unit; i++)
            c->scalefactors[i] += base - sf_weights[i];

        break;
    }
    case 1: { /* CLC offset */
        const int len = get_bits(gb, 2) + 2;
        const int base = len < 5 ? get_bits(gb, 5) : 0;
        for (int i = 0; i < b->band_ext_q_unit; i++)
            c->scalefactors[i] = base + get_bits(gb, len);
        break;
    }
    case 2:
    case 4: { /* VLC dist to baseline */
        const int *baseline = mode == 4 ? c->scalefactors_prev :
                              channel_idx ? b->channel[0].scalefactors :
                              c->scalefactors_prev;
        const int baseline_len = mode == 4 ? b->q_unit_cnt_prev :
                                 channel_idx ? b->band_ext_q_unit :
                                 b->q_unit_cnt_prev;

        const int len = get_bits(gb, 2) + 2;
        const int unit_cnt = FFMIN(b->band_ext_q_unit, baseline_len);
        const VLCElem *tab = sf_vlc[1][len];

        for (int i = 0; i < unit_cnt; i++) {
            int dist = get_vlc2(gb, tab, ATRAC9_SF_VLC_BITS, 1);
            c->scalefactors[i] = baseline[i] + dist;
        }

        for (int i = unit_cnt; i < b->band_ext_q_unit; i++)
            c->scalefactors[i] = get_bits(gb, 5);

        break;
    }
    case 3: { /* VLC offset with baseline */
        const int *baseline = channel_idx ? b->channel[0].scalefactors :
                              c->scalefactors_prev;
        const int baseline_len = channel_idx ? b->band_ext_q_unit :
                                 b->q_unit_cnt_prev;

        const int base = get_bits(gb, 5) - (1 << (5 - 1));
        const int len = get_bits(gb, 2) + 1;
        const int unit_cnt = FFMIN(b->band_ext_q_unit, baseline_len);
        const VLCElem *tab = sf_vlc[0][len];

        c->scalefactors[0] = get_bits(gb, len);

        for (int i = 1; i < unit_cnt; i++) {
            int val = c->scalefactors[i - 1] + get_vlc2(gb, tab,
                                                        ATRAC9_SF_VLC_BITS, 1);
            c->scalefactors[i] = val & ((1 << len) - 1);
        }

        for (int i = 0; i < unit_cnt; i++)
            c->scalefactors[i] += base + baseline[i];

        for (int i = unit_cnt; i < b->band_ext_q_unit; i++)
            c->scalefactors[i] = get_bits(gb, 5);
        break;
    }
    }

    for (int i = 0; i < b->band_ext_q_unit; i++)
        if (c->scalefactors[i] < 0 || c->scalefactors[i] > 31)
            return AVERROR_INVALIDDATA;

    memcpy(c->scalefactors_prev, c->scalefactors, sizeof(c->scalefactors));

    return 0;
}

static inline void calc_codebook_idx(ATRAC9Context *s, ATRAC9BlockData *b,
                                     ATRAC9ChannelData *c)
{
    int avg = 0;
    const int last_sf = c->scalefactors[c->q_unit_cnt];

    memset(c->codebookset, 0, sizeof(c->codebookset));

    if (c->q_unit_cnt <= 1)
        return;
    if (s->samplerate_idx > 7)
        return;

    c->scalefactors[c->q_unit_cnt] = c->scalefactors[c->q_unit_cnt - 1];

    if (c->q_unit_cnt > 12) {
        for (int i = 0; i < 12; i++)
            avg += c->scalefactors[i];
        avg = (avg + 6) / 12;
    }

    for (int i = 8; i < c->q_unit_cnt; i++) {
        const int prev = c->scalefactors[i - 1];
        const int cur  = c->scalefactors[i    ];
        const int next = c->scalefactors[i + 1];
        const int min  = FFMIN(prev, next);
        if ((cur - min >= 3 || 2*cur - prev - next >= 3))
            c->codebookset[i] = 1;
    }


    for (int i = 12; i < c->q_unit_cnt; i++) {
        const int cur = c->scalefactors[i];
        const int cnd = at9_q_unit_to_coeff_cnt[i] == 16;
        const int min = FFMIN(c->scalefactors[i + 1], c->scalefactors[i - 1]);
        if (c->codebookset[i])
            continue;

        c->codebookset[i] = (((cur - min) >= 2) && (cur >= (avg - cnd)));
    }

    c->scalefactors[c->q_unit_cnt] = last_sf;
}

static inline void read_coeffs_coarse(ATRAC9Context *s, ATRAC9BlockData *b,
                                      ATRAC9ChannelData *c, GetBitContext *gb)
{
    const int max_prec = s->samplerate_idx > 7 ? 1 : 7;

    memset(c->q_coeffs_coarse, 0, sizeof(c->q_coeffs_coarse));

    for (int i = 0; i < c->q_unit_cnt; i++) {
        int *coeffs = &c->q_coeffs_coarse[at9_q_unit_to_coeff_idx[i]];
        const int bands = at9_q_unit_to_coeff_cnt[i];
        const int prec = c->precision_coarse[i] + 1;

        if (prec <= max_prec) {
            const int cb = c->codebookset[i];
            const int cbi = at9_q_unit_to_codebookidx[i];
            const VLCElem *tab = coeff_vlc[cb][prec][cbi];
            const HuffmanCodebook *huff = &at9_huffman_coeffs[cb][prec][cbi];
            const int groups = bands >> huff->value_cnt_pow;

            for (int j = 0; j < groups; j++) {
                uint16_t val = get_vlc2(gb, tab, ATRAC9_COEFF_VLC_BITS, 2);

                for (int k = 0; k < huff->value_cnt; k++) {
                    coeffs[k] = sign_extend(val, huff->value_bits);
                    val >>= huff->value_bits;
                }

                coeffs += huff->value_cnt;
            }
        } else {
            for (int j = 0; j < bands; j++)
                coeffs[j] = sign_extend(get_bits(gb, prec), prec);
        }
    }
}

static inline void read_coeffs_fine(ATRAC9Context *s, ATRAC9BlockData *b,
                                    ATRAC9ChannelData *c, GetBitContext *gb)
{
    memset(c->q_coeffs_fine, 0, sizeof(c->q_coeffs_fine));

    for (int i = 0; i < c->q_unit_cnt; i++) {
        const int start = at9_q_unit_to_coeff_idx[i + 0];
        const int end   = at9_q_unit_to_coeff_idx[i + 1];
        const int len   = c->precision_fine[i] + 1;

        if (c->precision_fine[i] <= 0)
            continue;

        for (int j = start; j < end; j++)
            c->q_coeffs_fine[j] = sign_extend(get_bits(gb, len), len);
    }
}

static inline void dequantize(ATRAC9Context *s, ATRAC9BlockData *b,
                              ATRAC9ChannelData *c)
{
    memset(c->coeffs, 0, sizeof(c->coeffs));

    for (int i = 0; i < c->q_unit_cnt; i++) {
        const int start = at9_q_unit_to_coeff_idx[i + 0];
        const int end   = at9_q_unit_to_coeff_idx[i + 1];

        const float coarse_c = at9_quant_step_coarse[c->precision_coarse[i]];
        const float fine_c   = at9_quant_step_fine[c->precision_fine[i]];

        for (int j = start; j < end; j++) {
            const float vc = c->q_coeffs_coarse[j] * coarse_c;
            const float vf = c->q_coeffs_fine[j]   * fine_c;
            c->coeffs[j] = vc + vf;
        }
    }
}

static inline void apply_intensity_stereo(ATRAC9Context *s, ATRAC9BlockData *b,
                                          const int stereo)
{
    float *src = b->channel[ b->cpe_base_channel].coeffs;
    float *dst = b->channel[!b->cpe_base_channel].coeffs;

    if (!stereo)
        return;

    if (b->q_unit_cnt <= b->stereo_q_unit)
        return;

    for (int i = b->stereo_q_unit; i < b->q_unit_cnt; i++) {
        const int sign  = b->is_signs[i];
        const int start = at9_q_unit_to_coeff_idx[i + 0];
        const int end   = at9_q_unit_to_coeff_idx[i + 1];
        for (int j = start; j < end; j++)
            dst[j] = sign*src[j];
    }
}

static inline void apply_scalefactors(ATRAC9Context *s, ATRAC9BlockData *b,
                                      const int stereo)
{
    for (int i = 0; i <= stereo; i++) {
        float *coeffs = b->channel[i].coeffs;
        for (int j = 0; j < b->q_unit_cnt; j++) {
            const int start = at9_q_unit_to_coeff_idx[j + 0];
            const int end   = at9_q_unit_to_coeff_idx[j + 1];
            const int scalefactor = b->channel[i].scalefactors[j];
            const float scale = at9_scalefactor_c[scalefactor];
            for (int k = start; k < end; k++)
                coeffs[k] *= scale;
        }
    }
}

static inline void fill_with_noise(ATRAC9Context *s, ATRAC9ChannelData *c,
                                   int start, int count)
{
    float maxval = 0.0f;
    for (int i = 0; i < count; i += 2) {
        double tmp[2];
        av_bmg_get(&s->lfg, tmp);
        c->coeffs[start + i + 0] = tmp[0];
        c->coeffs[start + i + 1] = tmp[1];
        maxval = FFMAX(FFMAX(FFABS(tmp[0]), FFABS(tmp[1])), maxval);
    }
    /* Normalize */
    for (int i = 0; i < count; i++)
        c->coeffs[start + i] /= maxval;
}

static inline void scale_band_ext_coeffs(ATRAC9ChannelData *c, float sf[6],
                                         const int s_unit, const int e_unit)
{
    for (int i = s_unit; i < e_unit; i++) {
        const int start = at9_q_unit_to_coeff_idx[i + 0];
        const int end   = at9_q_unit_to_coeff_idx[i + 1];
        for (int j = start; j < end; j++)
            c->coeffs[j] *= sf[i - s_unit];
    }
}

static inline void apply_band_extension(ATRAC9Context *s, ATRAC9BlockData *b,
                                       const int stereo)
{
    const int g_units[4] = { /* A, B, C, total units */
        b->q_unit_cnt,
        at9_tab_band_ext_group[b->q_unit_cnt - 13][0],
        at9_tab_band_ext_group[b->q_unit_cnt - 13][1],
        FFMAX(g_units[2], 22),
    };

    const int g_bins[4] = { /* A, B, C, total bins */
        at9_q_unit_to_coeff_idx[g_units[0]],
        at9_q_unit_to_coeff_idx[g_units[1]],
        at9_q_unit_to_coeff_idx[g_units[2]],
        at9_q_unit_to_coeff_idx[g_units[3]],
    };

    for (int ch = 0; ch <= stereo; ch++) {
        ATRAC9ChannelData *c = &b->channel[ch];

        /* Mirror the spectrum */
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < (g_bins[i + 1] - g_bins[i + 0]); j++)
                c->coeffs[g_bins[i] + j] = c->coeffs[g_bins[i] - j - 1];

        switch (c->band_ext) {
        case 0: {
            float sf[6] = { 0.0f };
            const int l = g_units[3] - g_units[0] - 1;
            const int n_start = at9_q_unit_to_coeff_idx[g_units[3] - 1];
            const int n_cnt   = at9_q_unit_to_coeff_cnt[g_units[3] - 1];
            switch (at9_tab_band_ext_group[b->q_unit_cnt - 13][2]) {
            case 3:
                sf[0] = at9_band_ext_scales_m0[0][0][c->band_ext_data[0]];
                sf[1] = at9_band_ext_scales_m0[0][1][c->band_ext_data[0]];
                sf[2] = at9_band_ext_scales_m0[0][2][c->band_ext_data[1]];
                sf[3] = at9_band_ext_scales_m0[0][3][c->band_ext_data[2]];
                sf[4] = at9_band_ext_scales_m0[0][4][c->band_ext_data[3]];
                break;
            case 4:
                sf[0] = at9_band_ext_scales_m0[1][0][c->band_ext_data[0]];
                sf[1] = at9_band_ext_scales_m0[1][1][c->band_ext_data[0]];
                sf[2] = at9_band_ext_scales_m0[1][2][c->band_ext_data[1]];
                sf[3] = at9_band_ext_scales_m0[1][3][c->band_ext_data[2]];
                sf[4] = at9_band_ext_scales_m0[1][4][c->band_ext_data[3]];
                break;
            case 5:
                sf[0] = at9_band_ext_scales_m0[2][0][c->band_ext_data[0]];
                sf[1] = at9_band_ext_scales_m0[2][1][c->band_ext_data[1]];
                sf[2] = at9_band_ext_scales_m0[2][2][c->band_ext_data[1]];
                break;
            }

            sf[l] = at9_scalefactor_c[c->scalefactors[g_units[0]]];

            fill_with_noise(s, c, n_start, n_cnt);
            scale_band_ext_coeffs(c, sf, g_units[0], g_units[3]);
            break;
        }
        case 1: {
            float sf[6];
            for (int i = g_units[0]; i < g_units[3]; i++)
                sf[i - g_units[0]] = at9_scalefactor_c[c->scalefactors[i]];

            fill_with_noise(s, c, g_bins[0], g_bins[3] - g_bins[0]);
            scale_band_ext_coeffs(c, sf, g_units[0], g_units[3]);
            break;
        }
        case 2: {
            const float g_sf[2] = {
                at9_band_ext_scales_m2[c->band_ext_data[0]],
                at9_band_ext_scales_m2[c->band_ext_data[1]],
            };

            for (int i = 0; i < 2; i++)
                for (int j = g_bins[i + 0]; j < g_bins[i + 1]; j++)
                    c->coeffs[j] *= g_sf[i];
            break;
        }
        case 3: {
            float scale = at9_band_ext_scales_m3[c->band_ext_data[0]][0];
            float rate  = at9_band_ext_scales_m3[c->band_ext_data[1]][1];
            rate = pow(2, rate);
            for (int i = g_bins[0]; i < g_bins[3]; i++) {
                scale *= rate;
                c->coeffs[i] *= scale;
            }
            break;
        }
        case 4: {
            const float m = at9_band_ext_scales_m4[c->band_ext_data[0]];
            const float g_sf[3] = { 0.7079468f*m, 0.5011902f*m, 0.3548279f*m };

            for (int i = 0; i < 3; i++)
                for (int j = g_bins[i + 0]; j < g_bins[i + 1]; j++)
                    c->coeffs[j] *= g_sf[i];
            break;
        }
        }
    }
}

static int atrac9_decode_block(ATRAC9Context *s, GetBitContext *gb,
                               ATRAC9BlockData *b, AVFrame *frame,
                               int frame_idx, int block_idx)
{
    const int first_in_pkt = !get_bits1(gb);
    const int reuse_params =  get_bits1(gb);
    const int stereo = s->block_config->type[block_idx] == ATRAC9_BLOCK_TYPE_CPE;

    if (s->block_config->type[block_idx] == ATRAC9_BLOCK_TYPE_LFE) {
        ATRAC9ChannelData *c = &b->channel[0];
        const int precision = reuse_params ? 8 : 4;
        c->q_unit_cnt = b->q_unit_cnt = 2;

        memset(c->scalefactors, 0, sizeof(c->scalefactors));
        memset(c->q_coeffs_fine, 0, sizeof(c->q_coeffs_fine));
        memset(c->q_coeffs_coarse, 0, sizeof(c->q_coeffs_coarse));

        for (int i = 0; i < b->q_unit_cnt; i++) {
            c->scalefactors[i] = get_bits(gb, 5);
            c->precision_coarse[i] = precision;
            c->precision_fine[i] = 0;
        }

        for (int i = 0; i < c->q_unit_cnt; i++) {
            const int start = at9_q_unit_to_coeff_idx[i + 0];
            const int end   = at9_q_unit_to_coeff_idx[i + 1];
            for (int j = start; j < end; j++)
                c->q_coeffs_coarse[j] = get_bits(gb, c->precision_coarse[i] + 1);
        }

        dequantize        (s, b, c);
        apply_scalefactors(s, b, 0);

        goto imdct;
    }

    if (first_in_pkt && reuse_params) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid block flags!\n");
        return AVERROR_INVALIDDATA;
    }

    /* Band parameters */
    if (!reuse_params) {
        int stereo_band, ext_band;
        const int min_band_count = s->samplerate_idx > 7 ? 1 : 3;
        b->reuseable = 0;
        b->band_count = get_bits(gb, 4) + min_band_count;
        b->q_unit_cnt = at9_tab_band_q_unit_map[b->band_count];

        b->band_ext_q_unit = b->stereo_q_unit = b->q_unit_cnt;

        if (b->band_count > at9_tab_sri_max_bands[s->samplerate_idx]) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid band count %i!\n",
                   b->band_count);
            return AVERROR_INVALIDDATA;
        }

        if (stereo) {
            stereo_band = get_bits(gb, 4) + min_band_count;
            if (stereo_band > b->band_count) {
                av_log(s->avctx, AV_LOG_ERROR, "Invalid stereo band %i!\n",
                       stereo_band);
                return AVERROR_INVALIDDATA;
            }
            b->stereo_q_unit = at9_tab_band_q_unit_map[stereo_band];
        }

        b->has_band_ext = get_bits1(gb);
        if (b->has_band_ext) {
            ext_band = get_bits(gb, 4) + min_band_count;
            if (ext_band < b->band_count) {
                av_log(s->avctx, AV_LOG_ERROR, "Invalid extension band %i!\n",
                       ext_band);
                return AVERROR_INVALIDDATA;
            }
            b->band_ext_q_unit = at9_tab_band_q_unit_map[ext_band];
        }
        b->reuseable = 1;
    }
    if (!b->reuseable) {
        av_log(s->avctx, AV_LOG_ERROR, "invalid block reused!\n");
        return AVERROR_INVALIDDATA;
    }

    /* Calculate bit alloc gradient */
    if (parse_gradient(s, b, gb))
        return AVERROR_INVALIDDATA;

    /* IS data */
    b->cpe_base_channel = 0;
    if (stereo) {
        b->cpe_base_channel = get_bits1(gb);
        if (get_bits1(gb)) {
            for (int i = b->stereo_q_unit; i < b->q_unit_cnt; i++)
                b->is_signs[i] = 1 - 2*get_bits1(gb);
        } else {
            for (int i = 0; i < FF_ARRAY_ELEMS(b->is_signs); i++)
                b->is_signs[i] = 1;
        }
    }

    /* Band extension */
    if (parse_band_ext(s, b, gb, stereo))
        return AVERROR_INVALIDDATA;

    /* Scalefactors */
    for (int i = 0; i <= stereo; i++) {
        ATRAC9ChannelData *c = &b->channel[i];
        c->q_unit_cnt = i == b->cpe_base_channel ? b->q_unit_cnt :
                                                   b->stereo_q_unit;
        if (read_scalefactors(s, b, c, gb, i, first_in_pkt))
            return AVERROR_INVALIDDATA;

        calc_precision    (s, b, c);
        calc_codebook_idx (s, b, c);
        read_coeffs_coarse(s, b, c, gb);
        read_coeffs_fine  (s, b, c, gb);
        dequantize        (s, b, c);
    }

    b->q_unit_cnt_prev = b->has_band_ext ? b->band_ext_q_unit : b->q_unit_cnt;

    apply_intensity_stereo(s, b, stereo);
    apply_scalefactors    (s, b, stereo);

    if (b->has_band_ext && b->has_band_ext_data)
        apply_band_extension  (s, b, stereo);

imdct:
    for (int i = 0; i <= stereo; i++) {
        ATRAC9ChannelData *c = &b->channel[i];
        const int dst_idx = s->block_config->plane_map[block_idx][i];
        const int wsize = 1 << s->frame_log2;
        const ptrdiff_t offset = wsize*frame_idx*sizeof(float);
        float *dst = (float *)(frame->extended_data[dst_idx] + offset);

        s->tx_fn(s->tx, s->temp, c->coeffs, sizeof(float));
        s->fdsp->vector_fmul_window(dst, c->prev_win, s->temp,
                                    s->imdct_win, wsize >> 1);
        memcpy(c->prev_win, s->temp + (wsize >> 1), sizeof(float)*wsize >> 1);
    }

    return 0;
}

static int atrac9_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                               int *got_frame_ptr, AVPacket *avpkt)
{
    int ret;
    GetBitContext gb;
    ATRAC9Context *s = avctx->priv_data;
    const int frames = FFMIN(avpkt->size / s->avg_frame_size, s->frame_count);

    frame->nb_samples = (1 << s->frame_log2) * frames;
    ret = ff_get_buffer(avctx, frame, 0);
    if (ret < 0)
        return ret;

    ret = init_get_bits8(&gb, avpkt->data, avpkt->size);
    if (ret < 0)
        return ret;

    for (int i = 0; i < frames; i++) {
        for (int j = 0; j < s->block_config->count; j++) {
            ret = atrac9_decode_block(s, &gb, &s->block[j], frame, i, j);
            if (ret)
                return ret;
            align_get_bits(&gb);
        }
    }

    *got_frame_ptr = 1;

    return avctx->block_align;
}

static void atrac9_decode_flush(AVCodecContext *avctx)
{
    ATRAC9Context *s = avctx->priv_data;

    for (int j = 0; j < s->block_config->count; j++) {
        ATRAC9BlockData *b = &s->block[j];
        const int stereo = s->block_config->type[j] == ATRAC9_BLOCK_TYPE_CPE;
        for (int i = 0; i <= stereo; i++) {
            ATRAC9ChannelData *c = &b->channel[i];
            memset(c->prev_win, 0, sizeof(c->prev_win));
        }
    }
}

static av_cold int atrac9_decode_close(AVCodecContext *avctx)
{
    ATRAC9Context *s = avctx->priv_data;

    av_tx_uninit(&s->tx);
    av_freep(&s->fdsp);

    return 0;
}

static av_cold const VLCElem *atrac9_init_vlc(VLCInitState *state,
                                              int nb_bits, int nb_codes,
                                              const uint8_t (**tab)[2], int offset)
{
    const uint8_t (*table)[2] = *tab;

    *tab        += nb_codes;
    return ff_vlc_init_tables_from_lengths(state, nb_bits, nb_codes,
                                           &table[0][1], 2, &table[0][0], 2, 1,
                                           offset, 0);
}

static av_cold void atrac9_init_static(void)
{
    static VLCElem vlc_buf[24812];
    VLCInitState state = VLC_INIT_STATE(vlc_buf);
    const uint8_t (*tab)[2];

    /* Unsigned scalefactor VLCs */
    tab = at9_sfb_a_tab;
    for (int i = 1; i < 7; i++) {
        const HuffmanCodebook *hf = &at9_huffman_sf_unsigned[i];

        sf_vlc[0][i] = atrac9_init_vlc(&state, ATRAC9_SF_VLC_BITS,
                                       hf->size, &tab, 0);
    }

    /* Signed scalefactor VLCs */
    tab = at9_sfb_b_tab;
    for (int i = 2; i < 6; i++) {
        const HuffmanCodebook *hf = &at9_huffman_sf_signed[i];

        /* The symbols are signed integers in the range -16..15;
         * the values in the source table are offset by 16 to make
         * them fit into an uint8_t; the -16 reverses this shift. */
        sf_vlc[1][i] = atrac9_init_vlc(&state, ATRAC9_SF_VLC_BITS,
                                       hf->size, &tab, -16);
    }

    /* Coefficient VLCs */
    tab = at9_coeffs_tab;
    for (int i = 0; i < 2; i++) {
        for (int j = 2; j < 8; j++) {
            for (int k = i; k < 4; k++) {
                const HuffmanCodebook *hf = &at9_huffman_coeffs[i][j][k];
                coeff_vlc[i][j][k] = atrac9_init_vlc(&state, ATRAC9_COEFF_VLC_BITS,
                                                     hf->size, &tab, 0);
            }
        }
    }
}

static av_cold int atrac9_decode_init(AVCodecContext *avctx)
{
    float scale;
    static AVOnce static_table_init = AV_ONCE_INIT;
    GetBitContext gb;
    ATRAC9Context *s = avctx->priv_data;
    int err, version, block_config_idx, superframe_idx, alloc_c_len;

    s->avctx = avctx;

    av_lfg_init(&s->lfg, 0xFBADF00D);

    if (avctx->block_align <= 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid block align\n");
        return AVERROR_INVALIDDATA;
    }

    if (avctx->extradata_size != 12) {
        av_log(avctx, AV_LOG_ERROR, "Invalid extradata length!\n");
        return AVERROR_INVALIDDATA;
    }

    version = AV_RL32(avctx->extradata);
    if (version > 2) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported version (%i)!\n", version);
        return AVERROR_INVALIDDATA;
    }

    err = init_get_bits8(&gb, avctx->extradata + 4, avctx->extradata_size);
    if (err < 0)
        return err;

    if (get_bits(&gb, 8) != 0xFE) {
        av_log(avctx, AV_LOG_ERROR, "Incorrect magic byte!\n");
        return AVERROR_INVALIDDATA;
    }

    s->samplerate_idx = get_bits(&gb, 4);
    avctx->sample_rate = at9_tab_samplerates[s->samplerate_idx];

    block_config_idx = get_bits(&gb, 3);
    if (block_config_idx > 5) {
        av_log(avctx, AV_LOG_ERROR, "Incorrect block config!\n");
        return AVERROR_INVALIDDATA;
    }
    s->block_config = &at9_block_layout[block_config_idx];

    av_channel_layout_uninit(&avctx->ch_layout);
    avctx->ch_layout      = s->block_config->channel_layout;
    avctx->sample_fmt     = AV_SAMPLE_FMT_FLTP;

    if (get_bits1(&gb)) {
        av_log(avctx, AV_LOG_ERROR, "Incorrect verification bit!\n");
        return AVERROR_INVALIDDATA;
    }

    /* Average frame size in bytes */
    s->avg_frame_size = get_bits(&gb, 11) + 1;

    superframe_idx = get_bits(&gb, 2);
    if (superframe_idx & 1) {
        av_log(avctx, AV_LOG_ERROR, "Invalid superframe index!\n");
        return AVERROR_INVALIDDATA;
    }

    s->frame_count = 1 << superframe_idx;
    s->frame_log2  = at9_tab_sri_frame_log2[s->samplerate_idx];

    scale = 1.0f / 32768.0;
    err = av_tx_init(&s->tx, &s->tx_fn, AV_TX_FLOAT_MDCT, 1,
                     1 << s->frame_log2, &scale, 0);
    if (err < 0)
        return err;

    s->fdsp = avpriv_float_dsp_alloc(avctx->flags & AV_CODEC_FLAG_BITEXACT);
    if (!s->fdsp)
        return AVERROR(ENOMEM);

    /* iMDCT window */
    for (int i = 0; i < (1 << s->frame_log2); i++) {
        const int   len  = 1 << s->frame_log2;
        const float sidx = (      i + 0.5f) / len;
        const float eidx = (len - i - 0.5f) / len;
        const float s_c  = sinf(sidx*M_PI - M_PI_2)*0.5f + 0.5f;
        const float e_c  = sinf(eidx*M_PI - M_PI_2)*0.5f + 0.5f;
        s->imdct_win[i]  = s_c / ((s_c * s_c) + (e_c * e_c));
    }

    /* Allocation curve */
    alloc_c_len = FF_ARRAY_ELEMS(at9_tab_b_dist);
    for (int i = 1; i <= alloc_c_len; i++)
        for (int j = 0; j < i; j++)
            s->alloc_curve[i - 1][j] = at9_tab_b_dist[(j * alloc_c_len) / i];

    ff_thread_once(&static_table_init, atrac9_init_static);

    return 0;
}

const FFCodec ff_atrac9_decoder = {
    .p.name         = "atrac9",
    CODEC_LONG_NAME("ATRAC9 (Adaptive TRansform Acoustic Coding 9)"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_ATRAC9,
    .priv_data_size = sizeof(ATRAC9Context),
    .init           = atrac9_decode_init,
    .close          = atrac9_decode_close,
    FF_CODEC_DECODE_CB(atrac9_decode_frame),
    .flush          = atrac9_decode_flush,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_CHANNEL_CONF,
};
