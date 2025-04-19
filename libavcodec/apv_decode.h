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

typedef struct APVVLCLUTEntry {
    uint16_t result;  // Return value if not reading more.
    uint8_t  consume; // Number of bits to consume.
    uint8_t  more;    // Whether to read additional bits.
} APVVLCLUTEntry;

typedef struct APVVLCLUT {
    APVVLCLUTEntry lut[6][APV_VLC_LUT_SIZE];
} APVVLCLUT;

typedef struct APVEntropyState {
    void *log_ctx;

    const APVVLCLUT *decode_lut;

    int16_t prev_dc;
    int16_t prev_dc_diff;
    int16_t prev_1st_ac_level;
} APVEntropyState;


/**
 * Build the decoder VLC look-up table.
 */
void ff_apv_entropy_build_decode_lut(APVVLCLUT *decode_lut);

/**
 * Entropy decode a single 8x8 block to coefficients.
 *
 * Outputs in block order (dezigzag already applied).
 */
int ff_apv_entropy_decode_block(int16_t *coeff,
                                GetBitContext *gbc,
                                APVEntropyState *state);

/**
 * Read a single APV VLC code.
 *
 * This entrypoint is exposed for testing.
 */
unsigned int ff_apv_read_vlc(GetBitContext *gbc, int k_param,
                             const APVVLCLUT *lut);


#endif /* AVCODEC_APV_DECODE_H */
