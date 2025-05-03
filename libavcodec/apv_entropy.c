/*
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

#include "apv.h"
#include "apv_decode.h"


void ff_apv_entropy_build_decode_lut(APVVLCLUT *decode_lut)
{
    const int code_len = APV_VLC_LUT_BITS;
    const int lut_size = APV_VLC_LUT_SIZE;

    for (int k = 0; k <= 5; k++) {
        for (unsigned int code = 0; code < lut_size; code++) {
            APVVLCLUTEntry *ent = &decode_lut->lut[k][code];
            unsigned int first_bit      = code & (1 << code_len - 1);
            unsigned int remaining_bits = code ^ first_bit;

            if (first_bit) {
                ent->consume = 1 + k;
                ent->result  = remaining_bits >> (code_len - k - 1);
                ent->more    = 0;
            } else {
                unsigned int second_bit = code & (1 << code_len - 2);
                remaining_bits ^= second_bit;

                if (second_bit) {
                    unsigned int bits_left = code_len - 2;
                    unsigned int first_set = bits_left - av_log2(remaining_bits);
                    unsigned int last_bits = first_set - 1 + k;

                    if (first_set + last_bits <= bits_left) {
                        // Whole code fits here.
                        ent->consume = 2 + first_set + last_bits;
                        ent->result  = ((2 << k) +
                                        (((1 << first_set - 1) - 1) << k) +
                                        ((code >> bits_left - first_set - last_bits) & (1 << last_bits) - 1));
                        ent->more    = 0;
                    } else {
                        // Need to read more, collapse to default.
                        ent->consume = 2;
                        ent->more    = 1;
                    }
                } else {
                    ent->consume = 2 + k;
                    ent->result  = (1 << k) + (remaining_bits >> (code_len - k - 2));
                    ent->more    = 0;
                }
            }
        }
    }
}

av_always_inline
static unsigned int apv_read_vlc(GetBitContext *gbc, int k_param,
                                 const APVVLCLUT *lut)
{
    unsigned int next_bits;
    const APVVLCLUTEntry *ent;

    next_bits = show_bits(gbc, APV_VLC_LUT_BITS);
    ent = &lut->lut[k_param][next_bits];

    if (ent->more) {
        unsigned int leading_zeroes;

        skip_bits(gbc, ent->consume);

        next_bits = show_bits(gbc, 16);
        leading_zeroes = 15 - av_log2(next_bits);

        if (leading_zeroes == 0) {
            // This can't happen mid-stream because the lookup would
            // have resolved a leading one into a shorter code, but it
            // can happen if we are hitting the end of the buffer.
            // Return an invalid code to propagate as an error.
            return APV_MAX_TRANS_COEFF + 1;
        }

        skip_bits(gbc, leading_zeroes + 1);

        return (2 << k_param) +
            ((1 << leading_zeroes) - 1) * (1 << k_param) +
            get_bits(gbc, leading_zeroes + k_param);
    } else {
        skip_bits(gbc, ent->consume);
        return ent->result;
    }
}

unsigned int ff_apv_read_vlc(GetBitContext *gbc, int k_param,
                             const APVVLCLUT *lut)
{
    return apv_read_vlc(gbc, k_param, lut);
}

int ff_apv_entropy_decode_block(int16_t *coeff,
                                GetBitContext *gbc,
                                APVEntropyState *state)
{
    const APVVLCLUT *lut = state->decode_lut;
    int k_param;

    // DC coefficient.
    {
        int abs_dc_coeff_diff;
        int sign_dc_coeff_diff;
        int dc_coeff;

        k_param = av_clip(state->prev_dc_diff >> 1, 0, 5);
        abs_dc_coeff_diff = apv_read_vlc(gbc, k_param, lut);

        if (abs_dc_coeff_diff > 0)
            sign_dc_coeff_diff = get_bits1(gbc);
        else
            sign_dc_coeff_diff = 0;

        if (sign_dc_coeff_diff)
            dc_coeff = state->prev_dc - abs_dc_coeff_diff;
        else
            dc_coeff = state->prev_dc + abs_dc_coeff_diff;

        if (dc_coeff < APV_MIN_TRANS_COEFF ||
            dc_coeff > APV_MAX_TRANS_COEFF) {
            av_log(state->log_ctx, AV_LOG_ERROR,
                   "Out-of-range DC coefficient value: %d "
                   "(from prev_dc %d abs_dc_coeff_diff %d sign_dc_coeff_diff %d)\n",
                   dc_coeff, state->prev_dc, abs_dc_coeff_diff, sign_dc_coeff_diff);
            return AVERROR_INVALIDDATA;
        }

        coeff[0] = dc_coeff;

        state->prev_dc      = dc_coeff;
        state->prev_dc_diff = abs_dc_coeff_diff;
    }

    // AC coefficients.
    {
        int scan_pos   = 1;
        int first_ac   = 1;
        int prev_level = state->prev_1st_ac_level;
        int prev_run   = 0;

        do {
            int coeff_zero_run;

            k_param = av_clip(prev_run >> 2, 0, 2);
            coeff_zero_run = apv_read_vlc(gbc, k_param, lut);

            if (coeff_zero_run > APV_BLK_COEFFS - scan_pos) {
                av_log(state->log_ctx, AV_LOG_ERROR,
                       "Out-of-range zero-run value: %d (at scan pos %d)\n",
                       coeff_zero_run, scan_pos);
                return AVERROR_INVALIDDATA;
            }

            for (int i = 0; i < coeff_zero_run; i++) {
                coeff[ff_zigzag_direct[scan_pos]] = 0;
                ++scan_pos;
            }
            prev_run = coeff_zero_run;

            if (scan_pos < APV_BLK_COEFFS) {
                int abs_ac_coeff_minus1;
                int sign_ac_coeff;
                int level;

                k_param = av_clip(prev_level >> 2, 0, 4);
                abs_ac_coeff_minus1 = apv_read_vlc(gbc, k_param, lut);
                sign_ac_coeff = get_bits(gbc, 1);

                if (sign_ac_coeff)
                    level = -abs_ac_coeff_minus1 - 1;
                else
                    level = abs_ac_coeff_minus1 + 1;

                if (level < APV_MIN_TRANS_COEFF ||
                    level > APV_MAX_TRANS_COEFF) {
                    av_log(state->log_ctx, AV_LOG_ERROR,
                           "Out-of-range AC coefficient value: %d "
                           "(from prev_level %d abs_ac_coeff_minus1 %d sign_ac_coeff %d)\n",
                           level, prev_level, abs_ac_coeff_minus1, sign_ac_coeff);
                }

                coeff[ff_zigzag_direct[scan_pos]] = level;

                prev_level = abs_ac_coeff_minus1 + 1;
                if (first_ac) {
                    state->prev_1st_ac_level = prev_level;
                    first_ac = 0;
                }

                ++scan_pos;
            }

        } while (scan_pos < APV_BLK_COEFFS);
    }

    return 0;
}
