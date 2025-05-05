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

#ifndef AVCODEC_APV_DECODE_H
#define AVCODEC_APV_DECODE_H

#include <stdint.h>

#include "apv.h"
#include "avcodec.h"
#include "get_bits.h"


// Number of bits in the entropy look-up tables.
// It may be desirable to tune this per-architecture, as a larger LUT
// trades greater memory use for fewer instructions.
// (N bits -> 24*2^N bytes of tables; 9 -> 12KB of tables.)
#define APV_VLC_LUT_BITS 9
#define APV_VLC_LUT_SIZE (1 << APV_VLC_LUT_BITS)

typedef struct APVSingleVLCLUTEntry {
    uint16_t result;  // Return value if not reading more.
    uint8_t  consume; // Number of bits to consume.
    uint8_t  more;    // Whether to read additional bits.
} APVSingleVLCLUTEntry;

typedef struct APVMultiVLCLUTEntry {
    // Number of symbols this bit stream resolves to.
    uint8_t count;
    // k_run after decoding all symbols.
    uint8_t k_run     : 2;
    // k_level after decoding the first level symbol.
    uint8_t k_level_0 : 3;
    // k_level after decoding all symbols.
    uint8_t k_level_1 : 3;
    // Run output values.
    uint8_t run[2];
    // Level output values.
    int16_t level[2];
    // Bit index of the end of each code.
    uint8_t offset[4];
} APVMultiVLCLUTEntry;

typedef struct APVVLCLUT {
    // Single-symbol LUT for VLCs.
    // Applies to all coefficients, but used only for DC coefficients
    // in the decoder.
    APVSingleVLCLUTEntry single_lut[6][APV_VLC_LUT_SIZE];
    // Multi-symbol LUT for run/level combinations, decoding up to four
    // symbols per step.  Comes in two versions, which to use depends on
    // whether the next symbol is a run or a level.
    APVMultiVLCLUTEntry run_first_lut[3][5][APV_VLC_LUT_SIZE];
    APVMultiVLCLUTEntry level_first_lut[3][5][APV_VLC_LUT_SIZE];
} APVVLCLUT;

typedef struct APVEntropyState {
    void *log_ctx;

    const APVVLCLUT *decode_lut;

    // Previous DC level value.
    int16_t prev_dc;
    // k parameter implied by the previous DC level value.
    uint8_t prev_k_dc;
    // k parameter implied by the previous first AC level value.
    uint8_t prev_k_level;
} APVEntropyState;


/**
 * Build the decoder VLC look-up tables.
 */
void ff_apv_entropy_build_decode_lut(APVVLCLUT *decode_lut);

/**
 * Entropy decode a single 8x8 block to coefficients.
 *
 * Outputs nonzero coefficients only to the block row-major order
 * (dezigzag is applied within the function).  The output block
 * must have been filled with zeroes before calling this function.
 */
int ff_apv_entropy_decode_block(int16_t *restrict coeff,
                                GetBitContext *restrict gbc,
                                APVEntropyState *restrict state);

#endif /* AVCODEC_APV_DECODE_H */
