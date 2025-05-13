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

#include "put_bits.h"


av_always_inline
static unsigned int apv_read_vlc(GetBitContext *restrict gbc, int k_param,
                                 const APVVLCLUT *restrict lut)
{
    unsigned int next_bits;
    const APVSingleVLCLUTEntry *ent;

    next_bits = show_bits(gbc, APV_VLC_LUT_BITS);
    ent = &lut->single_lut[k_param][next_bits];

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

void ff_apv_entropy_build_decode_lut(APVVLCLUT *decode_lut)
{
    const int code_len = APV_VLC_LUT_BITS;
    const int lut_size = APV_VLC_LUT_SIZE;

    // Build the single-symbol VLC table.
    for (int k = 0; k <= 5; k++) {
        for (unsigned int code = 0; code < lut_size; code++) {
            APVSingleVLCLUTEntry   *ent = &decode_lut->single_lut[k][code];
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

    // Build the multi-symbol VLC table.
    for (int start_run = 0; start_run <= 2; start_run++) {
        for (int start_level = 0; start_level <= 4; start_level++) {
            for (unsigned int code = 0; code < lut_size; code++) {
                APVMultiVLCLUTEntry *ent;
                int k_run, k_level;
                GetBitContext gbc;
                PutBitContext pbc;
                uint8_t buffer[16];
                uint8_t run_first_buffer[16];
                uint8_t level_first_buffer[16];

                memset(buffer, 0, sizeof(buffer));
                init_put_bits(&pbc, buffer, sizeof(buffer));
                put_bits(&pbc, APV_VLC_LUT_BITS, code);
                flush_put_bits(&pbc);

                memcpy(run_first_buffer,   buffer, sizeof(buffer));
                memcpy(level_first_buffer, buffer, sizeof(buffer));

                k_run   = start_run;
                k_level = start_level;

                ent = &decode_lut->run_first_lut[k_run][k_level][code];
                memset(ent, 0, sizeof(*ent));
                init_get_bits8(&gbc, run_first_buffer, sizeof(run_first_buffer));

                ent->count = 0;
                for (int i = 0; i <= 1; i++) {
                    int value, sign, pos;

                    value = apv_read_vlc(&gbc, k_run, decode_lut);
                    pos = get_bits_count(&gbc);
                    if (pos > APV_VLC_LUT_BITS)
                        break;
                    ent->run[i] = value;
                    ent->offset[ent->count] = pos;
                    ++ent->count;
                    k_run = FFMIN(value >> 2, 2);

                    value = apv_read_vlc(&gbc, k_level, decode_lut);
                    sign = get_bits1(&gbc);
                    pos = get_bits_count(&gbc);
                    if (pos > APV_VLC_LUT_BITS)
                        break;
                    ++value;
                    ent->level[i] = sign ? -value : value;
                    ent->offset[ent->count] = pos;
                    ++ent->count;
                    k_level = FFMIN(value >> 2, 4);
                    if (i == 0)
                        ent->k_level_0 = k_level;
                }
                if (ent->count > 0 && ent->count < 4)
                    ent->offset[3] = ent->offset[ent->count - 1];
                ent->k_run     = k_run;
                ent->k_level_1 = k_level;

                k_run   = start_run;
                k_level = start_level;

                ent = &decode_lut->level_first_lut[k_run][k_level][code];
                memset(ent, 0, sizeof(*ent));
                init_get_bits8(&gbc, level_first_buffer, sizeof(level_first_buffer));

                ent->count = 0;
                for (int i = 0; i <= 1; i++) {
                    int value, sign, pos;

                    value = apv_read_vlc(&gbc, k_level, decode_lut);
                    sign = get_bits1(&gbc);
                    pos = get_bits_count(&gbc);
                    if (pos > APV_VLC_LUT_BITS)
                        break;
                    ++value;
                    ent->level[i] = sign ? -value : value;
                    ent->offset[ent->count] = pos;
                    ++ent->count;
                    k_level = FFMIN(value >> 2, 4);
                    if (i == 0)
                        ent->k_level_0 = k_level;

                    value = apv_read_vlc(&gbc, k_run, decode_lut);
                    pos = get_bits_count(&gbc);
                    if (pos > APV_VLC_LUT_BITS)
                        break;
                    ent->run[i] = value;
                    ent->offset[ent->count] = pos;
                    ++ent->count;
                    k_run = FFMIN(value >> 2, 2);
                }
                if (ent->count > 0 && ent->count < 4)
                    ent->offset[3] = ent->offset[ent->count - 1];
                ent->k_run     = k_run;
                ent->k_level_1 = k_level;
            }
        }
    }
}

int ff_apv_entropy_decode_block(int16_t *restrict coeff,
                                GetBitContext *restrict gbc,
                                APVEntropyState *restrict state)
{
    const APVVLCLUT *lut = state->decode_lut;
    int scan_pos;
    int k_dc = state->prev_k_dc;
    int k_run, k_level;
    uint32_t next_bits, lut_bits;
    const APVMultiVLCLUTEntry *ent;

    // DC coefficient is likely to be large and cannot be usefully
    // combined with other read steps, so extract it separately.
    {
        int dc_coeff, abs_diff, sign;

        abs_diff = apv_read_vlc(gbc, k_dc, lut);

        if (abs_diff) {
            sign = get_bits1(gbc);
            if (sign)
                dc_coeff = state->prev_dc - abs_diff;
            else
                dc_coeff = state->prev_dc + abs_diff;
        } else {
            dc_coeff = state->prev_dc;
        }


        if (dc_coeff < APV_MIN_TRANS_COEFF ||
            dc_coeff > APV_MAX_TRANS_COEFF) {
            av_log(state->log_ctx, AV_LOG_ERROR,
                   "Out-of-range DC coefficient value: %d.\n",
                   dc_coeff);
            return AVERROR_INVALIDDATA;
        }

        coeff[0] = dc_coeff;

        state->prev_dc   = dc_coeff;
        state->prev_k_dc = FFMIN(abs_diff >> 1, 5);
    }

    // Repeatedly read 18 bits, look up the first half of them in either
    // the run-first or the level-first table.  If the next code is too
    // long the 18 bits will allow resolving a run code (up to 63)
    // without reading any more bits, and will allow the exact length
    // of a level code to be determined.  (Note that reusing the
    // single-symbol LUT is never useful here as the multisymbol lookup
    // has already determined that the code is too long.)

    // Run a single iteration of the run-first LUT to start, then a
    // single iteration of the level-first LUT if that only read a
    // single code.  This avoids dealing with the first-AC logic inside
    // the normal code lookup sequence.

    k_level = state->prev_k_level;
    {
        next_bits = show_bits(gbc, 18);
        lut_bits = next_bits >> (18 - APV_VLC_LUT_BITS);

        ent = &lut->run_first_lut[0][k_level][lut_bits];

        if (ent->count == 0) {
            // One long code.
            uint32_t bits, low_bits;
            unsigned int leading_zeroes, low_bit_count, low_bit_shift;
            int run;

            // Remove the prefix bits.
            bits = next_bits & 0xffff;
            // Determine code length.
            leading_zeroes = 15 - av_log2(bits);
            if (leading_zeroes >= 6) {
                // 6 zeroes implies run > 64, which is always invalid.
                av_log(state->log_ctx, AV_LOG_ERROR,
                       "Out-of-range run value: %d leading zeroes.\n",
                       leading_zeroes);
                return AVERROR_INVALIDDATA;
            }
            // Extract the low bits.
            low_bit_count = leading_zeroes;
            low_bit_shift = 16 - (1 + 2 * leading_zeroes);
            low_bits = (bits >> low_bit_shift) & ((1 << low_bit_count) - 1);
            // Construct run code.
            run = 2 + ((1 << leading_zeroes) - 1) + low_bits;
            // Skip over the bits just used.
            skip_bits(gbc, 2 + leading_zeroes + 1 + low_bit_count);

            scan_pos = run + 1;
            if (scan_pos >= 64)
                goto end_of_block;
            k_run = FFMIN(run >> 2, 2);
            goto first_level;
        } else {
            // One or more short codes starting with a run; if there is
            // a level code then the length needs to be saved for the
            // next block.

            scan_pos = ent->run[0] + 1;
            if (scan_pos >= 64) {
                skip_bits(gbc, ent->offset[0]);
                goto end_of_block;
            }
            if (ent->count > 1) {
                coeff[ff_zigzag_direct[scan_pos]] = ent->level[0];
                ++scan_pos;
                state->prev_k_level = ent->k_level_0;
                if (scan_pos >= 64) {
                    skip_bits(gbc, ent->offset[1]);
                    goto end_of_block;
                }
            }
            if (ent->count > 2) {
                scan_pos += ent->run[1];
                if (scan_pos >= 64) {
                    skip_bits(gbc, ent->offset[2]);
                    goto end_of_block;
                }
            }
            if (ent->count > 3) {
                coeff[ff_zigzag_direct[scan_pos]] = ent->level[1];
                ++scan_pos;
                if (scan_pos >= 64) {
                    skip_bits(gbc, ent->offset[3]);
                    goto end_of_block;
                }
            }
            skip_bits(gbc, ent->offset[3]);
            k_run   = ent->k_run;
            k_level = ent->k_level_1;
            if (ent->count == 1)
                goto first_level;
            else if (ent->count & 1)
                goto next_is_level;
            else
                goto next_is_run;
        }
    }

    first_level: {
        next_bits = show_bits(gbc, 18);
        lut_bits = next_bits >> (18 - APV_VLC_LUT_BITS);

        ent = &lut->level_first_lut[k_run][k_level][lut_bits];

        if (ent->count == 0) {
            // One long code.
            uint32_t bits;
            unsigned int leading_zeroes;
            int level, abs_level, sign;

            // Remove the prefix bits.
            bits = next_bits & 0xffff;
            // Determine code length.
            leading_zeroes = 15 - av_log2(bits);
            // Skip the prefix and length bits.
            skip_bits(gbc, 2 + leading_zeroes + 1);
            // Read the rest of the code and construct the level.
            // Include the + 1 offset for nonzero value here.
            abs_level = (2 << k_level) +
                ((1 << leading_zeroes) - 1) * (1 << k_level) +
                get_bits(gbc, leading_zeroes + k_level) + 1;

            sign = get_bits(gbc, 1);
            if (sign)
                level = -abs_level;
            else
                level = abs_level;

            // Check range (not checked in any other case, only a long
            // code can be out of range).
            if (level < APV_MIN_TRANS_COEFF ||
                level > APV_MAX_TRANS_COEFF) {
                av_log(state->log_ctx, AV_LOG_ERROR,
                       "Out-of-range AC coefficient value at %d: %d.\n",
                       scan_pos, level);
                return AVERROR_INVALIDDATA;
            }
            coeff[ff_zigzag_direct[scan_pos]] = level;
            ++scan_pos;
            k_level = FFMIN(abs_level >> 2, 4);
            state->prev_k_level = k_level;
            if (scan_pos >= 64)
                goto end_of_block;
            goto next_is_run;

        } else {
            // One or more short codes.

            coeff[ff_zigzag_direct[scan_pos]] = ent->level[0];
            ++scan_pos;
            state->prev_k_level = ent->k_level_0;
            if (scan_pos >= 64) {
                skip_bits(gbc, ent->offset[0]);
                goto end_of_block;
            }
            if (ent->count > 1) {
                scan_pos += ent->run[0];
                if (scan_pos >= 64) {
                    skip_bits(gbc, ent->offset[1]);
                    goto end_of_block;
                }
            }
            if (ent->count > 2) {
                coeff[ff_zigzag_direct[scan_pos]] = ent->level[1];
                ++scan_pos;
                if (scan_pos >= 64) {
                    skip_bits(gbc, ent->offset[2]);
                    goto end_of_block;
                }
            }
            if (ent->count > 3) {
                scan_pos += ent->run[1];
                if (scan_pos >= 64) {
                    skip_bits(gbc, ent->offset[3]);
                    goto end_of_block;
                }
            }
            skip_bits(gbc, ent->offset[3]);
            k_run   = ent->k_run;
            k_level = ent->k_level_1;
            if (ent->count & 1)
                goto next_is_run;
            else
                goto next_is_level;
        }
    }

    next_is_run: {
        next_bits = show_bits(gbc, 18);
        lut_bits = next_bits >> (18 - APV_VLC_LUT_BITS);

        ent = &lut->run_first_lut[k_run][k_level][lut_bits];

        if (ent->count == 0) {
            // One long code.
            uint32_t bits, low_bits;
            unsigned int leading_zeroes, low_bit_count, low_bit_shift;
            int run;

            // Remove the prefix bits.
            bits = next_bits & 0xffff;
            // Determine code length.
            leading_zeroes = 15 - av_log2(bits);
            if (leading_zeroes >= 6) {
                // 6 zeroes implies run > 64, which is always invalid.
                av_log(state->log_ctx, AV_LOG_ERROR,
                       "Out-of-range run value: %d leading zeroes.\n",
                       leading_zeroes);
                return AVERROR_INVALIDDATA;
            }
            // Extract the low bits.
            low_bit_count = leading_zeroes + k_run;
            low_bit_shift = 16 - (1 + 2 * leading_zeroes + k_run);
            low_bits = (bits >> low_bit_shift) & ((1 << low_bit_count) - 1);
            // Construct run code.
            run = (2 << k_run) +
                ((1 << leading_zeroes) - 1) * (1 << k_run) +
                low_bits;
            // Skip over the bits just used.
            skip_bits(gbc, 2 + leading_zeroes + 1 + low_bit_count);

            scan_pos += run;
            if (scan_pos >= 64)
                goto end_of_block;
            k_run = FFMIN(run >> 2, 2);
            goto next_is_level;

        } else {
            // One or more short codes.

            scan_pos += ent->run[0];
            if (scan_pos >= 64) {
                skip_bits(gbc, ent->offset[0]);
                goto end_of_block;
            }
            if (ent->count > 1) {
                coeff[ff_zigzag_direct[scan_pos]] = ent->level[0];
                ++scan_pos;
                if (scan_pos >= 64) {
                    skip_bits(gbc, ent->offset[1]);
                    goto end_of_block;
                }
            }
            if (ent->count > 2) {
                scan_pos += ent->run[1];
                if (scan_pos >= 64) {
                    skip_bits(gbc, ent->offset[2]);
                    goto end_of_block;
                }
            }
            if (ent->count > 3) {
                coeff[ff_zigzag_direct[scan_pos]] = ent->level[1];
                ++scan_pos;
                if (scan_pos >= 64) {
                    skip_bits(gbc, ent->offset[3]);
                    goto end_of_block;
                }
            }
            skip_bits(gbc, ent->offset[3]);
            k_run   = ent->k_run;
            k_level = ent->k_level_1;
            if (ent->count & 1)
                goto next_is_level;
            else
                goto next_is_run;
        }
    }

    next_is_level: {
        next_bits = show_bits(gbc, 18);
        lut_bits = next_bits >> (18 - APV_VLC_LUT_BITS);

        ent = &lut->level_first_lut[k_run][k_level][lut_bits];

        if (ent->count == 0) {
            // One long code.
            uint32_t bits;
            unsigned int leading_zeroes;
            int level, abs_level, sign;

            // Remove the prefix bits.
            bits = next_bits & 0xffff;
            // Determine code length.
            leading_zeroes = 15 - av_log2(bits);
            // Skip the prefix and length bits.
            skip_bits(gbc, 2 + leading_zeroes + 1);
            // Read the rest of the code and construct the level.
            // Include the + 1 offset for nonzero value here.
            abs_level = (2 << k_level) +
                ((1 << leading_zeroes) - 1) * (1 << k_level) +
                get_bits(gbc, leading_zeroes + k_level) + 1;

            sign = get_bits(gbc, 1);
            if (sign)
                level = -abs_level;
            else
                level = abs_level;

            // Check range (not checked in any other case, only a long
            // code can be out of range).
            if (level < APV_MIN_TRANS_COEFF ||
                level > APV_MAX_TRANS_COEFF) {
                av_log(state->log_ctx, AV_LOG_ERROR,
                       "Out-of-range AC coefficient value at %d: %d.\n",
                       scan_pos, level);
                return AVERROR_INVALIDDATA;
            }
            coeff[ff_zigzag_direct[scan_pos]] = level;
            ++scan_pos;
            k_level = FFMIN(abs_level >> 2, 4);
            if (scan_pos >= 64)
                goto end_of_block;
            goto next_is_run;

        } else {
            // One or more short codes.

            coeff[ff_zigzag_direct[scan_pos]] = ent->level[0];
            ++scan_pos;
            if (scan_pos >= 64) {
                skip_bits(gbc, ent->offset[0]);
                goto end_of_block;
            }
            if (ent->count > 1) {
                scan_pos += ent->run[0];
                if (scan_pos >= 64) {
                    skip_bits(gbc, ent->offset[1]);
                    goto end_of_block;
                }
            }
            if (ent->count > 2) {
                coeff[ff_zigzag_direct[scan_pos]] = ent->level[1];
                ++scan_pos;
                if (scan_pos >= 64) {
                    skip_bits(gbc, ent->offset[2]);
                    goto end_of_block;
                }
            }
            if (ent->count > 3) {
                scan_pos += ent->run[1];
                if (scan_pos >= 64) {
                    skip_bits(gbc, ent->offset[3]);
                    goto end_of_block;
                }
            }
            skip_bits(gbc, ent->offset[3]);
            k_run   = ent->k_run;
            k_level = ent->k_level_1;
            if (ent->count & 1)
                goto next_is_run;
            else
                goto next_is_level;
        }
    }

    end_of_block: {
        if (scan_pos > 64) {
            av_log(state->log_ctx, AV_LOG_ERROR,
                   "Block decode reached invalid scan position %d.\n",
                   scan_pos);
            return AVERROR_INVALIDDATA;
        }
        return 0;
    }
}
