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

#include <assert.h>
#include <stdint.h>
#include <stdatomic.h>

#include "apv.h"
#include "apv_dsp.h"
#include "cbs_apv.h"

#include "cbs.h"
#include "get_bits.h"
#include "libavutil/frame.h"

// Number of bits in the entropy look-up tables.
// It may be desirable to tune this per-architecture, as a larger LUT
// trades greater memory use for fewer instructions.
// (N bits -> 384*2^N bytes of tables; 9 -> 192KB of tables.)
#define APV_VLC_LUT_BITS 9
#define APV_VLC_LUT_SIZE (1 << APV_VLC_LUT_BITS)

// The single-symbol LUT must be able to resolve a symbol with a single
// leading zero for the largest k (5) without reading more bits, which
// takes 2 + 1 + 5 = 8 bits.
static_assert(APV_VLC_LUT_BITS >= 8, "APV VLC LUT too small");

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
    // Level output values.  If fewer than two levels are decoded the
    // remaining ones are zero.
    int8_t  level[2];
    // Scan position of each level relative to the scan position before
    // this entry.  Missing levels are placed at the end of the entry,
    // where writing the level zero is harmless.
    uint8_t level_pos[2];
    // Total scan position advance if all symbols are consumed.  If not
    // all symbols are consumed this can be recalculated from level_pos.
    uint8_t scan_advance;
    // Bits consumed at the end of each of the four possible symbols.
    // Entry 3 always contains the bits from all symbols, even if fewer
    // than four symbols are consumed.
    uint8_t offset[4];
    // Index of the table to use for the next lookup, combining the
    // type of the next symbol with k_run and k_level after this entry.
    uint8_t next;
} APVMultiVLCLUTEntry;

// Types of entry in the entropy table.
enum {
    // Entries are run/level/run/level.
    APV_ENTROPY_RUN_FIRST   = 0,
    // Entries are level/run/level/run.
    APV_ENTROPY_LEVEL_FIRST = 1,
};
// Index of the base point in the multi-symbol table for the given
// type, k_run and k_level.
#define APV_TABLE_INDEX(type, k_run, k_level) \
    (((type) * 3 + (k_run)) * 5 + (k_level))

typedef struct APVVLCLUT {
    // Single-symbol LUT for VLCs.
    // Applies to all coefficients, but used only for DC coefficients
    // in the decoder.
    APVSingleVLCLUTEntry single_lut[6][APV_VLC_LUT_SIZE];
    // Multi-symbol LUT for run/level combinations, decoding up to four
    // symbols per step.
    // Indexed by [run first or level first][k_run][k_level][bits].
    APVMultiVLCLUTEntry multi_lut[2][3][5][APV_VLC_LUT_SIZE];
    // Zigzag scan extended past the end of the block, where every
    // position maps to a dummy coefficient after the block.
    uint8_t zigzag[128];
} APVVLCLUT;


// Number of coefficients in the block buffer used by entropy decode:
// the block itself and a dummy coefficient slot at the end which is
// used to avoid branching.
#define APV_ENTROPY_COEFF_SIZE 65

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

typedef struct APVDerivedTileInfo {
    uint8_t  tile_cols;
    uint8_t  tile_rows;
    uint16_t num_tiles;
    // The spec uses an extra element on the end of these arrays
    // not corresponding to any tile.
    uint16_t col_starts[APV_MAX_TILE_COLS + 1];
    uint16_t row_starts[APV_MAX_TILE_ROWS + 1];
} APVDerivedTileInfo;

typedef struct APVDecodeContext {
    CodedBitstreamContext *cbc;
    APVDSPContext dsp;

    CodedBitstreamFragment au;
    APVDerivedTileInfo tile_info;
    APVRawFrame *cur_raw_frame;

    AVPacket *pkt;
    AVFrame *output_frame;
    void *hwaccel_picture_private;
    atomic_int tile_errors;

    enum AVPixelFormat pix_fmt;
    int nb_unit;

    uint8_t warned_additional_frames;
    uint8_t warned_unknown_pbu_types;
} APVDecodeContext;

extern APVVLCLUT ff_apv_decode_lut;

/**
 * Build the decoder VLC look-up tables.
 */
void ff_apv_entropy_build_decode_lut(APVVLCLUT *decode_lut);

/**
 * Entropy decode a single 8x8 block to coefficients.
 *
 * Outputs nonzero coefficients only to the block row-major order
 * (dezigzag is applied within the function).  The first 64 elements
 * of the output block must have been filled with zeroes before
 * calling this function, and there must be space for
 * APV_ENTROPY_COEFF_SIZE elements in total.
 */
int ff_apv_entropy_decode_block(int16_t *restrict coeff,
                                GetBitContext *restrict gbc,
                                APVEntropyState *restrict state);

#endif /* AVCODEC_APV_DECODE_H */
