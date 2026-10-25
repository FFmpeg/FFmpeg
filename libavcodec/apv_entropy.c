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
            for (int type = APV_ENTROPY_RUN_FIRST;
                 type <= APV_ENTROPY_LEVEL_FIRST; type++) {
                for (unsigned int code = 0; code < lut_size; code++) {
                    APVMultiVLCLUTEntry *ent;
                    int k_run, k_level, adv, nb_levels;
                    GetBitContext gbc;
                    PutBitContext pbc;
                    uint8_t buffer[16];

                    memset(buffer, 0, sizeof(buffer));
                    init_put_bits(&pbc, buffer, sizeof(buffer));
                    put_bits(&pbc, APV_VLC_LUT_BITS, code);
                    flush_put_bits(&pbc);

                    k_run   = start_run;
                    k_level = start_level;

                    ent = &decode_lut->multi_lut[type][k_run][k_level][code];
                    memset(ent, 0, sizeof(*ent));
                    init_get_bits8(&gbc, buffer, sizeof(buffer));

                    // Symbols alternate between run and level, starting
                    // with whichever the type indicates (so even values
                    // of sym are runs).  Stop at the first one which
                    // does not fit in the lookup bits.
                    adv       = 0;
                    nb_levels = 0;
                    for (int sym = type; sym < type + 4; sym++) {
                        int value, sign, pos;

                        if (sym % 2 == 0) {
                            // Reading a run.
                            value = apv_read_vlc(&gbc, k_run, decode_lut);
                            pos   = get_bits_count(&gbc);
                            if (pos > APV_VLC_LUT_BITS)
                                break;
                            adv  += value;
                            k_run = FFMIN(value >> 2, 2);
                        } else {
                            // Reading a level.
                            value = apv_read_vlc(&gbc, k_level, decode_lut);
                            sign  = get_bits1(&gbc);
                            pos   = get_bits_count(&gbc);
                            if (pos > APV_VLC_LUT_BITS)
                                break;
                            ++value;
                            av_assert0(value <= INT8_MAX);
                            ent->level[nb_levels] = sign ? -value : value;
                            ent->level_pos[nb_levels] = adv;
                            ++nb_levels;
                            adv    += 1;
                            k_level = FFMIN(value >> 2, 4);
                            if (nb_levels == 1)
                                ent->k_level_0 = k_level;
                        }
                        ent->offset[ent->count] = pos;
                        ++ent->count;
                    }
                    // Missing levels write zeroes later in the array where
                    // they will either be overwritten or not used.
                    for (int i = nb_levels; i < 2; i++)
                        ent->level_pos[i] = adv;
                    // Entries are applied at scan positions below 64, so
                    // this keeps every write inside the extended zigzag.
                    av_assert0(adv < 64);
                    ent->scan_advance = adv;
                    if (ent->count > 0 && ent->count < 4)
                        ent->offset[3] = ent->offset[ent->count - 1];
                    ent->k_run     = k_run;
                    ent->k_level_1 = k_level;
                    ent->next = APV_TABLE_INDEX(type ^ (ent->count & 1),
                                                      k_run, k_level);
                }
            }
        }
    }

    // Positions past the end of the block all map to the dummy
    // coefficient slot at the end.
    for (int i = 0; i < FF_ARRAY_ELEMS(decode_lut->zigzag); i++)
        decode_lut->zigzag[i] = i < 64 ? ff_zigzag_direct[i] : 64;
}

// Decode a run code which is too long for the multi-symbol LUT.
// All such codes fit in the 18 bits already read.
av_always_inline
static int apv_decode_long_run(GetBitContext *restrict gbc,
                               uint32_t next_bits, int k_run,
                               APVEntropyState *restrict state)
{
    uint32_t bits, low_bits;
    unsigned int leading_zeroes, low_bit_count, low_bit_shift;

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
    low_bits = av_zero_extend(bits >> low_bit_shift, low_bit_count);
    // Skip over the bits just used.
    skip_bits(gbc, 2 + leading_zeroes + 1 + low_bit_count);
    // Construct run code.
    return (2 << k_run) +
        ((1 << leading_zeroes) - 1) * (1 << k_run) +
        low_bits;
}

// Decode a level code which is too long for the multi-symbol LUT,
// returning the signed level and setting the absolute value.
// The 18 bits already read are sufficient for the length, but we may
// need to read more bits to complete the value.
av_always_inline
static int apv_decode_long_level(GetBitContext *restrict gbc,
                                 uint32_t next_bits, int k_level,
                                 int *abs_level_out, int scan_pos,
                                 APVEntropyState *restrict state)
{
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

    sign = get_bits1(gbc);
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
    *abs_level_out = abs_level;
    return level;
}

// Apply the symbols from a multi-symbol LUT entry.  Both levels are
// always written: absent ones are zero, and any beyond the end of the
// block go to the dummy coefficient.  Returns nonzero if the end of
// the block was reached, in which case only the bits up to the symbol
// which reached it are consumed.
av_always_inline
static int apv_apply_multi_entry(int16_t *restrict coeff,
                                 GetBitContext *restrict gbc,
                                 const APVMultiVLCLUTEntry *ent,
                                 const uint8_t *zigzag,
                                 int *scan_pos_ptr, int type)
{
    int scan_pos = *scan_pos_ptr;

    coeff[zigzag[scan_pos + ent->level_pos[0]]] = ent->level[0];
    coeff[zigzag[scan_pos + ent->level_pos[1]]] = ent->level[1];

    if (scan_pos + ent->scan_advance >= 64) {
        // We've gone over the end: find the symbol which reached the
        // end and stop there.
        int end[4], i;

        if (type == APV_ENTROPY_RUN_FIRST) {
            end[0] = ent->level_pos[0];
            end[1] = ent->level_pos[0] + 1;
            end[2] = ent->level_pos[1];
            end[3] = ent->level_pos[1] + 1;
        } else {
            end[0] = 1;
            end[1] = ent->level_pos[1];
            end[2] = ent->level_pos[1] + 1;
            end[3] = ent->scan_advance;
        }
        for (i = 0; i < ent->count - 1; i++) {
            if (scan_pos + end[i] >= 64)
                break;
        }
        skip_bits(gbc, ent->offset[i]);
        *scan_pos_ptr = scan_pos + end[i];
        return 1;
    } else {
        // All codes used: advance the bitstream and scan position by
        // the full values.
        skip_bits(gbc, ent->offset[3]);
        *scan_pos_ptr = scan_pos + ent->scan_advance;
        return 0;
    }
}

av_always_inline
static int apv_entropy_decode_block_internal(int16_t *restrict coeff,
                                             GetBitContext *restrict gbc,
                                             APVEntropyState *restrict state)
{
    const APVVLCLUT *lut = state->decode_lut;
    const APVMultiVLCLUTEntry *tables = lut->multi_lut[0][0][0];
    const uint8_t *zigzag = lut->zigzag;
    int scan_pos, type, table_index;
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

    // The first level in the block sets the k_level for the next block,
    // so run the first lookup (and the second, if the first did not
    // contain a level) separately.

    scan_pos = 1;
    k_run    = 0;
    k_level  = state->prev_k_level;
    {
        next_bits = show_bits(gbc, 18);
        lut_bits = next_bits >> (18 - APV_VLC_LUT_BITS);

        ent = &lut->multi_lut[APV_ENTROPY_RUN_FIRST][0][k_level][lut_bits];

        if (ent->count == 0) {
            int run = apv_decode_long_run(gbc, next_bits, 0, state);
            if (run < 0)
                return run;
            scan_pos += run;
            if (scan_pos >= 64)
                goto end_of_block;
            k_run = FFMIN(run >> 2, 2);
        } else {
            // The level is only real if the run before it did not
            // reach the end of the block.
            if (ent->count > 1 && scan_pos + ent->level_pos[0] < 64)
                state->prev_k_level = ent->k_level_0;
            if (apv_apply_multi_entry(coeff, gbc, ent, zigzag,
                                      &scan_pos, APV_ENTROPY_RUN_FIRST))
                goto end_of_block;
            k_run   = ent->k_run;
            k_level = ent->k_level_1;
            if (ent->count > 1) {
                type = APV_ENTROPY_RUN_FIRST ^ (ent->count & 1);
                goto normal_loop;
            }
        }
    }

    // Only read a run, still need to read the first level.
    {
        next_bits = show_bits(gbc, 18);
        lut_bits = next_bits >> (18 - APV_VLC_LUT_BITS);

        ent = &lut->multi_lut[APV_ENTROPY_LEVEL_FIRST][k_run][k_level][lut_bits];

        if (ent->count == 0) {
            int level, abs_level;
            level = apv_decode_long_level(gbc, next_bits, k_level,
                                          &abs_level, scan_pos, state);
            if (level < APV_MIN_TRANS_COEFF)
                return level;
            coeff[zigzag[scan_pos]] = level;
            ++scan_pos;
            k_level = FFMIN(abs_level >> 2, 4);
            state->prev_k_level = k_level;
            if (scan_pos >= 64)
                goto end_of_block;
            type = APV_ENTROPY_RUN_FIRST;
        } else {
            state->prev_k_level = ent->k_level_0;
            if (apv_apply_multi_entry(coeff, gbc, ent, zigzag,
                                      &scan_pos, APV_ENTROPY_LEVEL_FIRST))
                goto end_of_block;
            k_run   = ent->k_run;
            k_level = ent->k_level_1;
            type = APV_ENTROPY_LEVEL_FIRST ^ (ent->count & 1);
        }
    }

normal_loop:
    // From here the only state carried between lookups is the index of
    // the table to use, which each entry supplies for the next one.
    table_index = APV_TABLE_INDEX(type, k_run, k_level);
    for (;;) {
        next_bits = show_bits(gbc, 18);
        lut_bits = next_bits >> (18 - APV_VLC_LUT_BITS);

        ent = &tables[table_index * APV_VLC_LUT_SIZE + lut_bits];

        if (ent->count == 0) {
            // Missed in the lookup, so need to re-derive the type,
            // k_run and k_level from the table index.
            k_run   = table_index / 5 % 3;
            k_level = table_index % 5;
            if (table_index < APV_TABLE_INDEX(APV_ENTROPY_LEVEL_FIRST, 0, 0)) {
                int run = apv_decode_long_run(gbc, next_bits, k_run, state);
                if (run < 0)
                    return run;
                scan_pos += run;
                if (scan_pos >= 64)
                    goto end_of_block;
                k_run = FFMIN(run >> 2, 2);
                table_index = APV_TABLE_INDEX(APV_ENTROPY_LEVEL_FIRST, k_run, k_level);
            } else {
                int level, abs_level;
                level = apv_decode_long_level(gbc, next_bits, k_level,
                                              &abs_level, scan_pos, state);
                if (level < APV_MIN_TRANS_COEFF)
                    return level;
                coeff[zigzag[scan_pos]] = level;
                ++scan_pos;
                k_level = FFMIN(abs_level >> 2, 4);
                if (scan_pos >= 64)
                    goto end_of_block;
                table_index = APV_TABLE_INDEX(APV_ENTROPY_RUN_FIRST, k_run, k_level);
            }
        } else {
            type = table_index >= APV_TABLE_INDEX(APV_ENTROPY_LEVEL_FIRST, 0, 0);
            if (apv_apply_multi_entry(coeff, gbc, ent, zigzag,
                                      &scan_pos, type))
                goto end_of_block;
            table_index = ent->next;
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

int ff_apv_entropy_decode_block(int16_t *restrict coeff,
                                GetBitContext *restrict gbc,
                                APVEntropyState *restrict state)
{
    // Use a local copy of the bitstream reader in this translation unit
    // to improve optimisation opportunities.  Only the position in the
    // bitstream needs to be written back.
    GetBitContext gb = *gbc;
    int err = apv_entropy_decode_block_internal(coeff, &gb, state);
    gbc->index = gb.index;
    return err;
}
