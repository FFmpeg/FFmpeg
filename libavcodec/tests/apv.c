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

#include "libavutil/lfg.h"
#include "libavutil/random_seed.h"

#include "libavcodec/apv_decode.h"
#include "libavcodec/apv_dsp.h"
#include "libavcodec/put_bits.h"


// Whole file included here to get internal symbols.
#include "libavcodec/apv_entropy.c"


// As defined in 7.1.4, for testing.
// Adds a check to limit loop after reading 16 zero bits to avoid
// getting stuck reading a stream of zeroes forever (this matches
// the behaviour of the faster version).

static unsigned int apv_read_vlc_spec(GetBitContext *gbc, int k_param)
{
    unsigned int symbol_value = 0;
    int parse_exp_golomb = 1;
    int k = k_param;
    int stop_loop = 0;

    if(get_bits1(gbc) == 1) {
        parse_exp_golomb = 0;
    } else {
        if (get_bits1(gbc) == 0) {
            symbol_value += (1 << k);
            parse_exp_golomb = 0;
        } else {
            symbol_value += (2 << k);
            parse_exp_golomb = 1;
        }
    }
    if (parse_exp_golomb) {
        int read_limit = 0;
        do {
            if (get_bits1(gbc) == 1) {
                stop_loop = 1;
            } else {
                if (++read_limit == 16)
                    break;
                symbol_value += (1 << k);
                k++;
            }
        } while (!stop_loop);
    }
    if (k > 0)
        symbol_value += get_bits(gbc, k);

    return symbol_value;
}

// As defined in 7.2.4, for testing.

static void apv_write_vlc_spec(PutBitContext *pbc,
                               unsigned int symbol_val, int k_param)
{
    int prefix_vlc_table[3][2] = {{1, 0}, {0, 0}, {0, 1}};

    unsigned int symbol_value = symbol_val;
    int val_prefix_vlc = av_clip(symbol_val >> k_param, 0, 2);
    int bit_count = 0;
    int k = k_param;

    while (symbol_value >= (1 << k)) {
        symbol_value -= (1 << k);
        if (bit_count < 2)
            put_bits(pbc, 1, prefix_vlc_table[val_prefix_vlc][bit_count]);
        else
            put_bits(pbc, 1, 0);
        if (bit_count >= 2)
            ++k;
        ++bit_count;
    }

    if(bit_count < 2)
        put_bits(pbc, 1, prefix_vlc_table[val_prefix_vlc][bit_count]);
    else
        put_bits(pbc, 1, 1);

    if(k > 0)
        put_bits(pbc, k, symbol_value);
}

// Old version of ff_apv_entropy_decode_block, for test comparison.

static int apv_entropy_decode_block(int16_t *restrict coeff,
                                    GetBitContext *restrict gbc,
                                    APVEntropyState *restrict state)
{
    const APVVLCLUT *lut = state->decode_lut;

    // DC coefficient.
    {
        int abs_dc_coeff_diff;
        int sign_dc_coeff_diff;
        int dc_coeff;

        abs_dc_coeff_diff = apv_read_vlc(gbc, state->prev_k_dc, lut);

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

        state->prev_dc   = dc_coeff;
        state->prev_k_dc = FFMIN(abs_dc_coeff_diff >> 1, 5);
    }

    // AC coefficients.
    {
        int scan_pos = 1;
        int first_ac = 1;
        int k_run    = 0;
        int k_level  = state->prev_k_level;

        do {
            int coeff_zero_run;

            coeff_zero_run = apv_read_vlc(gbc, k_run, lut);

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
            k_run = FFMIN(coeff_zero_run >> 2, 2);

            if (scan_pos < APV_BLK_COEFFS) {
                int abs_ac_coeff_minus1;
                int sign_ac_coeff;
                int abs_level, level;

                abs_ac_coeff_minus1 = apv_read_vlc(gbc, k_level, lut);
                sign_ac_coeff = get_bits(gbc, 1);

                abs_level = abs_ac_coeff_minus1 + 1;
                if (sign_ac_coeff)
                    level = -abs_level;
                else
                    level = abs_level;

                if (level < APV_MIN_TRANS_COEFF ||
                    level > APV_MAX_TRANS_COEFF) {
                    av_log(state->log_ctx, AV_LOG_ERROR,
                           "Out-of-range AC coefficient value: %d "
                           "(from k_param %d abs_ac_coeff_minus1 %d sign_ac_coeff %d)\n",
                           level, k_level, abs_ac_coeff_minus1, sign_ac_coeff);
                }

                coeff[ff_zigzag_direct[scan_pos]] = level;

                k_level = FFMIN(abs_level >> 2, 4);
                if (first_ac) {
                    state->prev_k_level = k_level;
                    first_ac = 0;
                }

                ++scan_pos;
            }

        } while (scan_pos < APV_BLK_COEFFS);
    }

    return 0;
}

static void binary(char *buf, uint32_t value, int bits)
{
    for (int i = 0; i < bits; i++)
        buf[i] = (value >> (bits - i - 1) & 1) ? '1' : '0';
    buf[bits] = '\0';
}

static int test_apv_read_vlc(void)
{
    APVVLCLUT lut;
    int err = 0;

    ff_apv_entropy_build_decode_lut(&lut);

    // Generate all possible 20 bit sequences (padded with zeroes), then
    // verify that spec and improved parsing functions get the same result
    // and consume the same number of bits for each possible k_param.

    for (int k = 0; k <= 5; k++) {
        for (uint32_t b = 0; b < (1 << 20); b++) {
            uint8_t buf[8] = {
                b >> 12,
                b >> 4,
                b << 4,
                0, 0, 0, 0, 0
            };

            GetBitContext gbc_test, gbc_spec;
            unsigned int  res_test, res_spec;
            int           con_test, con_spec;

            init_get_bits8(&gbc_test, buf, 8);
            init_get_bits8(&gbc_spec, buf, 8);

            res_test = apv_read_vlc     (&gbc_test, k, &lut);
            res_spec = apv_read_vlc_spec(&gbc_spec, k);

            con_test = get_bits_count(&gbc_test);
            con_spec = get_bits_count(&gbc_spec);

            if (res_test != res_spec ||
                con_test != con_spec) {
                char str[21];
                binary(str, b, 20);
                av_log(NULL, AV_LOG_ERROR,
                       "Mismatch reading %s (%d) with k=%d:\n", str, b, k);
                av_log(NULL, AV_LOG_ERROR,
                       "Test function result %d consumed %d bits.\n",
                       res_test, con_test);
                av_log(NULL, AV_LOG_ERROR,
                       "Spec function result %d consumed %d bits.\n",
                       res_spec, con_spec);
                ++err;
                if (err > 10)
                    return err;
            }
        }
    }

    return err;
}

static int random_coeff(AVLFG *lfg)
{
    // Geometric distribution of code lengths (1-14 bits),
    // uniform distribution within codes of the length,
    // equal probability of either sign.
    int length = (av_lfg_get(lfg) / (UINT_MAX / 14 + 1));
    int random = av_lfg_get(lfg);
    int value = (1 << length) + (random & (1 << length) - 1);
    if (random & (1 << length))
        return value;
    else
        return -value;
}

static int random_run(AVLFG *lfg)
{
    // Expoenential distrbution of run lengths.
    unsigned int random = av_lfg_get(lfg);
    for (int len = 0;; len++) {
        if (random & (1 << len))
            return len;
    }
    // You rolled zero on a 2^32 sided die; well done!
    return 64;
}

static int test_apv_entropy_decode_block(void)
{
    // Generate random entropy blocks, code them, then ensure they
    // decode to the same block with both implementations.

    APVVLCLUT decode_lut;
    AVLFG lfg;
    unsigned int seed = av_get_random_seed();
    av_lfg_init(&lfg, seed);

    av_log(NULL, AV_LOG_INFO, "seed = %u\n", seed);

    ff_apv_entropy_build_decode_lut(&decode_lut);

    for (int t = 0; t < 100; t++) {
        APVEntropyState state, save_state;
        int16_t block[64];
        int16_t block_test1[64];
        int16_t block_test2[64];
        uint8_t buffer[1024];
        PutBitContext pbc;
        GetBitContext gbc;
        int bits_written;
        int pos, run, coeff, level, err;
        int k_dc, k_run, k_level;

        memset(block,  0, sizeof(block));
        memset(buffer, 0, sizeof(buffer));
        init_put_bits(&pbc, buffer, sizeof(buffer));

        // Randomly-constructed state.
        memset(&state, 0, sizeof(state));
        state.decode_lut   = &decode_lut;
        state.prev_dc      = random_coeff(&lfg);
        state.prev_k_dc    = av_lfg_get(&lfg) % 5;
        state.prev_k_level = av_lfg_get(&lfg) % 4;
        save_state = state;

        k_dc    = state.prev_k_dc;
        k_run   = 0;
        k_level = state.prev_k_level;

        coeff = random_coeff(&lfg) / 2;
        block[ff_zigzag_direct[0]] = state.prev_dc + coeff;
        apv_write_vlc_spec(&pbc, FFABS(coeff), k_dc);
        if (coeff != 0)
            put_bits(&pbc, 1, coeff < 0);

        pos = 1;
        while (pos < 64) {
            run = random_run(&lfg);
            if (pos + run > 64)
                run = 64 - pos;
            apv_write_vlc_spec(&pbc, run, k_run);
            k_run = av_clip(run >> 2, 0, 2);
            pos += run;
            if (pos < 64) {
                coeff = random_coeff(&lfg);
                level = FFABS(coeff) - 1;
                block[ff_zigzag_direct[pos]] = coeff;
                apv_write_vlc_spec(&pbc, level, k_level);
                put_bits(&pbc, 1, coeff < 0);
                k_level = av_clip((level + 1) >> 2, 0, 4);
                ++pos;
            }
        }
        bits_written = put_bits_count(&pbc);
        flush_put_bits(&pbc);

        // Fill output block with a distinctive error value.
        for (int i = 0; i < 64; i++)
            block_test1[i] = -9999;
        init_get_bits8(&gbc, buffer, sizeof(buffer));

        err = apv_entropy_decode_block(block_test1, &gbc, &state);
        if (err < 0) {
            av_log(NULL, AV_LOG_ERROR, "Entropy decode returned error.\n");
            return 1;
        } else {
            int bits_read = get_bits_count(&gbc);
            if (bits_written != bits_read) {
                av_log(NULL, AV_LOG_ERROR, "Wrote %d bits but read %d.\n",
                       bits_written, bits_read);
                return 1;
            } else {
                err = 0;
                for (int i = 0; i < 64; i++) {
                    if (block[i] != block_test1[i])
                        ++err;
                }
                if (err > 0) {
                    av_log(NULL, AV_LOG_ERROR, "%d mismatches in output block.\n", err);
                    return err;
                }
            }
        }

        init_get_bits8(&gbc, buffer, sizeof(buffer));
        memset(block_test2, 0, 64 * sizeof(int16_t));

        err = ff_apv_entropy_decode_block(block_test2, &gbc, &save_state);
        if (err < 0) {
            av_log(NULL, AV_LOG_ERROR, "Entropy decode returned error.\n");
            return 1;
        } else {
            int bits_read = get_bits_count(&gbc);
            if (bits_written != bits_read) {
                av_log(NULL, AV_LOG_ERROR, "Wrote %d bits but read %d.\n",
                       bits_written, bits_read);
                return 1;
            } else {
                err = 0;
                for (int i = 0; i < 64; i++) {
                    if (block[i] != block_test2[i])
                        ++err;
                }
                if (err > 0) {
                    av_log(NULL, AV_LOG_ERROR, "%d mismatches in output block.\n", err);
                    return err;
                }
            }
        }

        if (state.prev_dc      != save_state.prev_dc   ||
            state.prev_k_dc    != save_state.prev_k_dc ||
            state.prev_k_level != save_state.prev_k_level) {
            av_log(NULL, AV_LOG_ERROR, "Entropy state mismatch.\n");
            return 1;
        }
    }

    return 0;
}

int main(void)
{
    int err;

    err = test_apv_read_vlc();
    if (err) {
        av_log(NULL, AV_LOG_ERROR, "Read VLC test failed.\n");
        return err;
    }

    err = test_apv_entropy_decode_block();
    if (err) {
        av_log(NULL, AV_LOG_ERROR, "Entropy decode block test failed.\n");
        return err;
    }

    return 0;
}
