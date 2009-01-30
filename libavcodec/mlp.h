/*
 * MLP codec common header file
 * Copyright (c) 2007-2008 Ian Caulfield
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

#ifndef AVCODEC_MLP_H
#define AVCODEC_MLP_H

#include <stdint.h>

#include "avcodec.h"

/** Maximum number of channels that can be decoded. */
#define MAX_CHANNELS        16

/** Maximum number of matrices used in decoding; most streams have one matrix
 *  per output channel, but some rematrix a channel (usually 0) more than once.
 */
#define MAX_MATRICES        15

/** Maximum number of substreams that can be decoded. This could also be set
 *  higher, but I haven't seen any examples with more than two.
 */
#define MAX_SUBSTREAMS      2

/** maximum sample frequency seen in files */
#define MAX_SAMPLERATE      192000

/** maximum number of audio samples within one access unit */
#define MAX_BLOCKSIZE       (40 * (MAX_SAMPLERATE / 48000))
/** next power of two greater than MAX_BLOCKSIZE */
#define MAX_BLOCKSIZE_POW2  (64 * (MAX_SAMPLERATE / 48000))

/** number of allowed filters */
#define NUM_FILTERS         2

/** The maximum number of taps in either the IIR or FIR filter;
 *  I believe MLP actually specifies the maximum order for IIR filters as four,
 *  and that the sum of the orders of both filters must be <= 8.
*/
#define MAX_FILTER_ORDER    8

/** Code that signals end of a stream. */
#define END_OF_STREAM       0xd234d234

#define FIR 0
#define IIR 1

/** filter data */
typedef struct {
    uint8_t     order; ///< number of taps in filter
    uint8_t     shift; ///< Right shift to apply to output of filter.

    int32_t     coeff[MAX_FILTER_ORDER];
    int32_t     state[MAX_FILTER_ORDER];
} FilterParams;

/** sample data coding information */
typedef struct {
    FilterParams filter_params[NUM_FILTERS];

    int16_t     huff_offset;      ///< Offset to apply to residual values.
    int32_t     sign_huff_offset; ///< sign/rounding-corrected version of huff_offset
    uint8_t     codebook;         ///< Which VLC codebook to use to read residuals.
    uint8_t     huff_lsbs;        ///< Size of residual suffix not encoded using VLC.
} ChannelParams;

/** Tables defining the Huffman codes.
 *  There are three entropy coding methods used in MLP (four if you count
 *  "none" as a method). These use the same sequences for codes starting with
 *  00 or 01, but have different codes starting with 1.
 */
extern const uint8_t ff_mlp_huffman_tables[3][18][2];

/** MLP uses checksums that seem to be based on the standard CRC algorithm, but
 *  are not (in implementation terms, the table lookup and XOR are reversed).
 *  We can implement this behavior using a standard av_crc on all but the
 *  last element, then XOR that with the last element.
 */
uint8_t  ff_mlp_checksum8 (const uint8_t *buf, unsigned int buf_size);
uint16_t ff_mlp_checksum16(const uint8_t *buf, unsigned int buf_size);

/** Calculate an 8-bit checksum over a restart header -- a non-multiple-of-8
 *  number of bits, starting two bits into the first byte of buf.
 */
uint8_t ff_mlp_restart_checksum(const uint8_t *buf, unsigned int bit_size);

/** XOR together all the bytes of a buffer.
 *  Does this belong in dspcontext?
 */
uint8_t ff_mlp_calculate_parity(const uint8_t *buf, unsigned int buf_size);

void ff_mlp_init_crc(void);

/** XOR four bytes into one. */
static inline uint8_t xor_32_to_8(uint32_t value)
{
    value ^= value >> 16;
    value ^= value >>  8;
    return value;
}

#endif /* AVCODEC_MLP_H */
