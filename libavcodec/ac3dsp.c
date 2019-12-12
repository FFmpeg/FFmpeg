/*
 * AC-3 DSP functions
 * Copyright (c) 2011 Justin Ruggles
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

#include "avcodec.h"
#include "ac3.h"
#include "ac3dsp.h"
#include "mathops.h"

static void ac3_exponent_min_c(uint8_t *exp, int num_reuse_blocks, int nb_coefs)
{
    int blk, i;

    if (!num_reuse_blocks)
        return;

    for (i = 0; i < nb_coefs; i++) {
        uint8_t min_exp = *exp;
        uint8_t *exp1 = exp + 256;
        for (blk = 0; blk < num_reuse_blocks; blk++) {
            uint8_t next_exp = *exp1;
            if (next_exp < min_exp)
                min_exp = next_exp;
            exp1 += 256;
        }
        *exp++ = min_exp;
    }
}

static int ac3_max_msb_abs_int16_c(const int16_t *src, int len)
{
    int i, v = 0;
    for (i = 0; i < len; i++)
        v |= abs(src[i]);
    return v;
}

static void ac3_lshift_int16_c(int16_t *src, unsigned int len,
                               unsigned int shift)
{
    uint32_t *src32 = (uint32_t *)src;
    const uint32_t mask = ~(((1 << shift) - 1) << 16);
    int i;
    len >>= 1;
    for (i = 0; i < len; i += 8) {
        src32[i  ] = (src32[i  ] << shift) & mask;
        src32[i+1] = (src32[i+1] << shift) & mask;
        src32[i+2] = (src32[i+2] << shift) & mask;
        src32[i+3] = (src32[i+3] << shift) & mask;
        src32[i+4] = (src32[i+4] << shift) & mask;
        src32[i+5] = (src32[i+5] << shift) & mask;
        src32[i+6] = (src32[i+6] << shift) & mask;
        src32[i+7] = (src32[i+7] << shift) & mask;
    }
}

static void ac3_rshift_int32_c(int32_t *src, unsigned int len,
                               unsigned int shift)
{
    do {
        *src++ >>= shift;
        *src++ >>= shift;
        *src++ >>= shift;
        *src++ >>= shift;
        *src++ >>= shift;
        *src++ >>= shift;
        *src++ >>= shift;
        *src++ >>= shift;
        len -= 8;
    } while (len > 0);
}

static void float_to_fixed24_c(int32_t *dst, const float *src, unsigned int len)
{
    const float scale = 1 << 24;
    do {
        *dst++ = lrintf(*src++ * scale);
        *dst++ = lrintf(*src++ * scale);
        *dst++ = lrintf(*src++ * scale);
        *dst++ = lrintf(*src++ * scale);
        *dst++ = lrintf(*src++ * scale);
        *dst++ = lrintf(*src++ * scale);
        *dst++ = lrintf(*src++ * scale);
        *dst++ = lrintf(*src++ * scale);
        len -= 8;
    } while (len > 0);
}

static void ac3_bit_alloc_calc_bap_c(int16_t *mask, int16_t *psd,
                                     int start, int end,
                                     int snr_offset, int floor,
                                     const uint8_t *bap_tab, uint8_t *bap)
{
    int bin, band, band_end;

    /* special case, if snr offset is -960, set all bap's to zero */
    if (snr_offset == -960) {
        memset(bap, 0, AC3_MAX_COEFS);
        return;
    }

    bin  = start;
    band = ff_ac3_bin_to_band_tab[start];
    do {
        int m = (FFMAX(mask[band] - snr_offset - floor, 0) & 0x1FE0) + floor;
        band_end = ff_ac3_band_start_tab[++band];
        band_end = FFMIN(band_end, end);

        for (; bin < band_end; bin++) {
            int address = av_clip_uintp2((psd[bin] - m) >> 5, 6);
            bap[bin] = bap_tab[address];
        }
    } while (end > band_end);
}

static void ac3_update_bap_counts_c(uint16_t mant_cnt[16], uint8_t *bap,
                                    int len)
{
    while (len-- > 0)
        mant_cnt[bap[len]]++;
}

DECLARE_ALIGNED(16, const uint16_t, ff_ac3_bap_bits)[16] = {
    0,  0,  0,  3,  0,  4,  5,  6,  7,  8,  9, 10, 11, 12, 14, 16
};

static int ac3_compute_mantissa_size_c(uint16_t mant_cnt[6][16])
{
    int blk, bap;
    int bits = 0;

    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        // bap=1 : 3 mantissas in 5 bits
        bits += (mant_cnt[blk][1] / 3) * 5;
        // bap=2 : 3 mantissas in 7 bits
        // bap=4 : 2 mantissas in 7 bits
        bits += ((mant_cnt[blk][2] / 3) + (mant_cnt[blk][4] >> 1)) * 7;
        // bap=3 : 1 mantissa in 3 bits
        bits += mant_cnt[blk][3] * 3;
        // bap=5 to 15 : get bits per mantissa from table
        for (bap = 5; bap < 16; bap++)
            bits += mant_cnt[blk][bap] * ff_ac3_bap_bits[bap];
    }
    return bits;
}

static void ac3_extract_exponents_c(uint8_t *exp, int32_t *coef, int nb_coefs)
{
    int i;

    for (i = 0; i < nb_coefs; i++) {
        int v = abs(coef[i]);
        exp[i] = v ? 23 - av_log2(v) : 24;
    }
}

static void ac3_sum_square_butterfly_int32_c(int64_t sum[4],
                                             const int32_t *coef0,
                                             const int32_t *coef1,
                                             int len)
{
    int i;

    sum[0] = sum[1] = sum[2] = sum[3] = 0;

    for (i = 0; i < len; i++) {
        int lt = coef0[i];
        int rt = coef1[i];
        int md = lt + rt;
        int sd = lt - rt;
        MAC64(sum[0], lt, lt);
        MAC64(sum[1], rt, rt);
        MAC64(sum[2], md, md);
        MAC64(sum[3], sd, sd);
    }
}

static void ac3_sum_square_butterfly_float_c(float sum[4],
                                             const float *coef0,
                                             const float *coef1,
                                             int len)
{
    int i;

    sum[0] = sum[1] = sum[2] = sum[3] = 0;

    for (i = 0; i < len; i++) {
        float lt = coef0[i];
        float rt = coef1[i];
        float md = lt + rt;
        float sd = lt - rt;
        sum[0] += lt * lt;
        sum[1] += rt * rt;
        sum[2] += md * md;
        sum[3] += sd * sd;
    }
}

static void ac3_downmix_5_to_2_symmetric_c(float **samples, float **matrix,
                                           int len)
{
    int i;
    float v0, v1;
    float front_mix    = matrix[0][0];
    float center_mix   = matrix[0][1];
    float surround_mix = matrix[0][3];

    for (i = 0; i < len; i++) {
        v0 = samples[0][i] * front_mix  +
             samples[1][i] * center_mix +
             samples[3][i] * surround_mix;

        v1 = samples[1][i] * center_mix +
             samples[2][i] * front_mix  +
             samples[4][i] * surround_mix;

        samples[0][i] = v0;
        samples[1][i] = v1;
    }
}

static void ac3_downmix_5_to_1_symmetric_c(float **samples, float **matrix,
                                           int len)
{
    int i;
    float front_mix    = matrix[0][0];
    float center_mix   = matrix[0][1];
    float surround_mix = matrix[0][3];

    for (i = 0; i < len; i++) {
        samples[0][i] = samples[0][i] * front_mix    +
                        samples[1][i] * center_mix   +
                        samples[2][i] * front_mix    +
                        samples[3][i] * surround_mix +
                        samples[4][i] * surround_mix;
    }
}

static void ac3_downmix_c(float **samples, float **matrix,
                          int out_ch, int in_ch, int len)
{
    int i, j;
    float v0, v1;

    if (out_ch == 2) {
        for (i = 0; i < len; i++) {
            v0 = v1 = 0.0f;
            for (j = 0; j < in_ch; j++) {
                v0 += samples[j][i] * matrix[0][j];
                v1 += samples[j][i] * matrix[1][j];
            }
            samples[0][i] = v0;
            samples[1][i] = v1;
        }
    } else if (out_ch == 1) {
        for (i = 0; i < len; i++) {
            v0 = 0.0f;
            for (j = 0; j < in_ch; j++)
                v0 += samples[j][i] * matrix[0][j];
            samples[0][i] = v0;
        }
    }
}

static void ac3_downmix_5_to_2_symmetric_c_fixed(int32_t **samples, int16_t **matrix,
                                           int len)
{
    int i;
    int64_t v0, v1;
    int16_t front_mix    = matrix[0][0];
    int16_t center_mix   = matrix[0][1];
    int16_t surround_mix = matrix[0][3];

    for (i = 0; i < len; i++) {
        v0 = (int64_t)samples[0][i] * front_mix  +
             (int64_t)samples[1][i] * center_mix +
             (int64_t)samples[3][i] * surround_mix;

        v1 = (int64_t)samples[1][i] * center_mix +
             (int64_t)samples[2][i] * front_mix  +
             (int64_t)samples[4][i] * surround_mix;

        samples[0][i] = (v0+2048)>>12;
        samples[1][i] = (v1+2048)>>12;
    }
}

static void ac3_downmix_5_to_1_symmetric_c_fixed(int32_t **samples, int16_t **matrix,
                                                 int len)
{
    int i;
    int64_t v0;
    int16_t front_mix    = matrix[0][0];
    int16_t center_mix   = matrix[0][1];
    int16_t surround_mix = matrix[0][3];

    for (i = 0; i < len; i++) {
        v0 = (int64_t)samples[0][i] * front_mix    +
             (int64_t)samples[1][i] * center_mix   +
             (int64_t)samples[2][i] * front_mix    +
             (int64_t)samples[3][i] * surround_mix +
             (int64_t)samples[4][i] * surround_mix;

        samples[0][i] = (v0+2048)>>12;
    }
}

static void ac3_downmix_c_fixed(int32_t **samples, int16_t **matrix,
                                int out_ch, int in_ch, int len)
{
    int i, j;
    int64_t v0, v1;
    if (out_ch == 2) {
        for (i = 0; i < len; i++) {
            v0 = v1 = 0;
            for (j = 0; j < in_ch; j++) {
                v0 += (int64_t)samples[j][i] * matrix[0][j];
                v1 += (int64_t)samples[j][i] * matrix[1][j];
            }
            samples[0][i] = (v0+2048)>>12;
            samples[1][i] = (v1+2048)>>12;
        }
    } else if (out_ch == 1) {
        for (i = 0; i < len; i++) {
            v0 = 0;
            for (j = 0; j < in_ch; j++)
                v0 += (int64_t)samples[j][i] * matrix[0][j];
            samples[0][i] = (v0+2048)>>12;
        }
    }
}

void ff_ac3dsp_downmix_fixed(AC3DSPContext *c, int32_t **samples, int16_t **matrix,
                             int out_ch, int in_ch, int len)
{
    if (c->in_channels != in_ch || c->out_channels != out_ch) {
        c->in_channels  = in_ch;
        c->out_channels = out_ch;
        c->downmix_fixed = NULL;

        if (in_ch == 5 && out_ch == 2 &&
            !(matrix[1][0] | matrix[0][2]  |
              matrix[1][3] | matrix[0][4]  |
             (matrix[0][1] ^ matrix[1][1]) |
             (matrix[0][0] ^ matrix[1][2]))) {
            c->downmix_fixed = ac3_downmix_5_to_2_symmetric_c_fixed;
        } else if (in_ch == 5 && out_ch == 1 &&
                   matrix[0][0] == matrix[0][2] &&
                   matrix[0][3] == matrix[0][4]) {
            c->downmix_fixed = ac3_downmix_5_to_1_symmetric_c_fixed;
        }
    }

    if (c->downmix_fixed)
        c->downmix_fixed(samples, matrix, len);
    else
        ac3_downmix_c_fixed(samples, matrix, out_ch, in_ch, len);
}

static void apply_window_int16_c(int16_t *output, const int16_t *input,
                                 const int16_t *window, unsigned int len)
{
    int i;
    int len2 = len >> 1;

    for (i = 0; i < len2; i++) {
        int16_t w       = window[i];
        output[i]       = (MUL16(input[i],       w) + (1 << 14)) >> 15;
        output[len-i-1] = (MUL16(input[len-i-1], w) + (1 << 14)) >> 15;
    }
}

void ff_ac3dsp_downmix(AC3DSPContext *c, float **samples, float **matrix,
                       int out_ch, int in_ch, int len)
{
    if (c->in_channels != in_ch || c->out_channels != out_ch) {
        int **matrix_cmp = (int **)matrix;

        c->in_channels  = in_ch;
        c->out_channels = out_ch;
        c->downmix      = NULL;

        if (in_ch == 5 && out_ch == 2 &&
            !(matrix_cmp[1][0] | matrix_cmp[0][2]   |
              matrix_cmp[1][3] | matrix_cmp[0][4]   |
             (matrix_cmp[0][1] ^ matrix_cmp[1][1]) |
             (matrix_cmp[0][0] ^ matrix_cmp[1][2]))) {
            c->downmix = ac3_downmix_5_to_2_symmetric_c;
        } else if (in_ch == 5 && out_ch == 1 &&
                   matrix_cmp[0][0] == matrix_cmp[0][2] &&
                   matrix_cmp[0][3] == matrix_cmp[0][4]) {
            c->downmix = ac3_downmix_5_to_1_symmetric_c;
        }

        if (ARCH_X86)
            ff_ac3dsp_set_downmix_x86(c);
    }

    if (c->downmix)
        c->downmix(samples, matrix, len);
    else
        ac3_downmix_c(samples, matrix, out_ch, in_ch, len);
}

av_cold void ff_ac3dsp_init(AC3DSPContext *c, int bit_exact)
{
    c->ac3_exponent_min = ac3_exponent_min_c;
    c->ac3_max_msb_abs_int16 = ac3_max_msb_abs_int16_c;
    c->ac3_lshift_int16 = ac3_lshift_int16_c;
    c->ac3_rshift_int32 = ac3_rshift_int32_c;
    c->float_to_fixed24 = float_to_fixed24_c;
    c->bit_alloc_calc_bap = ac3_bit_alloc_calc_bap_c;
    c->update_bap_counts = ac3_update_bap_counts_c;
    c->compute_mantissa_size = ac3_compute_mantissa_size_c;
    c->extract_exponents = ac3_extract_exponents_c;
    c->sum_square_butterfly_int32 = ac3_sum_square_butterfly_int32_c;
    c->sum_square_butterfly_float = ac3_sum_square_butterfly_float_c;
    c->in_channels           = 0;
    c->out_channels          = 0;
    c->downmix               = NULL;
    c->downmix_fixed         = NULL;
    c->apply_window_int16 = apply_window_int16_c;

    if (ARCH_ARM)
        ff_ac3dsp_init_arm(c, bit_exact);
    if (ARCH_X86)
        ff_ac3dsp_init_x86(c, bit_exact);
    if (ARCH_MIPS)
        ff_ac3dsp_init_mips(c, bit_exact);
}
