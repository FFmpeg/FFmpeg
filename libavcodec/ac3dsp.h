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

#ifndef AVCODEC_AC3DSP_H
#define AVCODEC_AC3DSP_H

#include <stdint.h>

/**
 * Number of mantissa bits written for each bap value.
 * bap values with fractional bits are set to 0 and are calculated separately.
 */
extern const uint16_t ff_ac3_bap_bits[16];

typedef struct AC3DSPContext {
    /**
     * Set each encoded exponent in a block to the minimum of itself and the
     * exponents in the same frequency bin of up to 5 following blocks.
     * @param exp   pointer to the start of the current block of exponents.
     *              constraints: align 16
     * @param num_reuse_blocks  number of blocks that will reuse exponents from the current block.
     *                          constraints: range 0 to 5
     * @param nb_coefs  number of frequency coefficients.
     */
    void (*ac3_exponent_min)(uint8_t *exp, int num_reuse_blocks, int nb_coefs);

    /**
     * Calculate the maximum MSB of the absolute value of each element in an
     * array of int16_t.
     * @param src input array
     *            constraints: align 16. values must be in range [-32767,32767]
     * @param len number of values in the array
     *            constraints: multiple of 16 greater than 0
     * @return    a value with the same MSB as max(abs(src[]))
     */
    int (*ac3_max_msb_abs_int16)(const int16_t *src, int len);

    /**
     * Left-shift each value in an array of int16_t by a specified amount.
     * @param src    input array
     *               constraints: align 16
     * @param len    number of values in the array
     *               constraints: multiple of 32 greater than 0
     * @param shift  left shift amount
     *               constraints: range [0,15]
     */
    void (*ac3_lshift_int16)(int16_t *src, unsigned int len, unsigned int shift);

    /**
     * Right-shift each value in an array of int32_t by a specified amount.
     * @param src    input array
     *               constraints: align 16
     * @param len    number of values in the array
     *               constraints: multiple of 16 greater than 0
     * @param shift  right shift amount
     *               constraints: range [0,31]
     */
    void (*ac3_rshift_int32)(int32_t *src, unsigned int len, unsigned int shift);

    /**
     * Convert an array of float in range [-1.0,1.0] to int32_t with range
     * [-(1<<24),(1<<24)]
     *
     * @param dst destination array of int32_t.
     *            constraints: 16-byte aligned
     * @param src source array of float.
     *            constraints: 16-byte aligned
     * @param len number of elements to convert.
     *            constraints: multiple of 32 greater than zero
     */
    void (*float_to_fixed24)(int32_t *dst, const float *src, unsigned int len);

    /**
     * Calculate bit allocation pointers.
     * The SNR is the difference between the masking curve and the signal.  AC-3
     * uses this value for each frequency bin to allocate bits.  The snroffset
     * parameter is a global adjustment to the SNR for all bins.
     *
     * @param[in]  mask       masking curve
     * @param[in]  psd        signal power for each frequency bin
     * @param[in]  start      starting bin location
     * @param[in]  end        ending bin location
     * @param[in]  snr_offset SNR adjustment
     * @param[in]  floor      noise floor
     * @param[in]  bap_tab    look-up table for bit allocation pointers
     * @param[out] bap        bit allocation pointers
     */
    void (*bit_alloc_calc_bap)(int16_t *mask, int16_t *psd, int start, int end,
                               int snr_offset, int floor,
                               const uint8_t *bap_tab, uint8_t *bap);

    /**
     * Update bap counts using the supplied array of bap.
     *
     * @param[out] mant_cnt   bap counts for 1 block
     * @param[in]  bap        array of bap, pointing to start coef bin
     * @param[in]  len        number of elements to process
     */
    void (*update_bap_counts)(uint16_t mant_cnt[16], uint8_t *bap, int len);

    /**
     * Calculate the number of bits needed to encode a set of mantissas.
     *
     * @param[in] mant_cnt    bap counts for all blocks
     * @return                mantissa bit count
     */
    int (*compute_mantissa_size)(uint16_t mant_cnt[6][16]);

    void (*extract_exponents)(uint8_t *exp, int32_t *coef, int nb_coefs);

    void (*sum_square_butterfly_int32)(int64_t sum[4], const int32_t *coef0,
                                       const int32_t *coef1, int len);

    void (*sum_square_butterfly_float)(float sum[4], const float *coef0,
                                       const float *coef1, int len);

    void (*downmix)(float **samples, float (*matrix)[2], int out_ch,
                    int in_ch, int len);

    void (*downmix_fixed)(int32_t **samples, int16_t (*matrix)[2], int out_ch,
                          int in_ch, int len);

    /**
     * Apply symmetric window in 16-bit fixed-point.
     * @param output destination array
     *               constraints: 16-byte aligned
     * @param input  source array
     *               constraints: 16-byte aligned
     * @param window window array
     *               constraints: 16-byte aligned, at least len/2 elements
     * @param len    full window length
     *               constraints: multiple of ? greater than zero
     */
    void (*apply_window_int16)(int16_t *output, const int16_t *input,
                               const int16_t *window, unsigned int len);
} AC3DSPContext;

void ff_ac3dsp_init    (AC3DSPContext *c, int bit_exact);
void ff_ac3dsp_init_arm(AC3DSPContext *c, int bit_exact);
void ff_ac3dsp_init_x86(AC3DSPContext *c, int bit_exact);
void ff_ac3dsp_init_mips(AC3DSPContext *c, int bit_exact);

#endif /* AVCODEC_AC3DSP_H */
