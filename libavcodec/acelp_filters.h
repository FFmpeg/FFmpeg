/*
 * various filters for ACELP-based codecs
 *
 * Copyright (c) 2008 Vladimir Voroshilov
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

#ifndef FFMPEG_ACELP_FILTERS_H
#define FFMPEG_ACELP_FILTERS_H

#include <stdint.h>

/**
 * \brief Circularly convolve fixed vector with a phase dispersion impulse
 *        response filter (D.6.2 of G.729 and 6.1.5 of AMR).
 * \param fc_out vector with filter applied
 * \param fc_in source vector
 * \param filter phase filter coefficients
 *
 *  fc_out[n] = sum(i,0,len-1){ fc_in[i] * filter[(len + n - i)%len] }
 *
 * \note fc_in and fc_out should not overlap!
 */
void ff_acelp_convolve_circ(
        int16_t* fc_out,
        const int16_t* fc_in,
        const int16_t* filter,
        int subframe_size);

/**
 * \brief LP synthesis filter
 * \param out [out] pointer to output buffer
 * \param filter_coeffs filter coefficients (-0x8000 <= (3.12) < 0x8000)
 * \param in input signal
 * \param buffer_length amount of data to process
 * \param filter_length filter length (11 for 10th order LP filter)
 * \param stop_on_overflow   1 - return immediately if overflow occurs
 *                           0 - ignore overflows
 *
 * \return 1 if overflow occurred, 0 - otherwise
 *
 * \note Output buffer must contain 10 samples of past
 *       speech data before pointer.
 *
 * Routine applies 1/A(z) filter to given speech data.
 */
int ff_acelp_lp_synthesis_filter(
        int16_t *out,
        const int16_t* filter_coeffs,
        const int16_t* in,
        int buffer_length,
        int filter_length,
        int stop_on_overflow);

/**
 * \brief Calculates coefficients of weighted A(z/weight) filter.
 * \param out [out] weighted A(z/weight) result
 *                  filter (-0x8000 <= (3.12) < 0x8000)
 * \param in source filter (-0x8000 <= (3.12) < 0x8000)
 * \param weight_pow array containing weight^i (-0x8000 <= (0.15) < 0x8000)
 * \param filter_length filter length (11 for 10th order LP filter)
 *
 * out[i]=weight_pow[i]*in[i] , i=0..9
 */
void ff_acelp_weighted_filter(
        int16_t *out,
        const int16_t* in,
        const int16_t *weight_pow,
        int filter_length);

/**
 * \brief high-pass filtering and upscaling (4.2.5 of G.729)
 * \param out [out] output buffer for filtered speech data
 * \param hpf_f [in/out] past filtered data from previous (2 items long)
 *                       frames (-0x20000000 <= (14.13) < 0x20000000)
 * \param in speech data to process
 * \param length input data size
 *
 * out[i] = 0.93980581 * in[i] - 1.8795834 * in[i-1] + 0.93980581 * in[i-2] +
 *          1.9330735 * out[i-1] - 0.93589199 * out[i-2]
 *
 * The filter has a cut-off frequency of 100Hz
 *
 * \note Two items before the top of the out buffer must contain two items from the
 *       tail of the previous subframe.
 *
 * \remark It is safe to pass the same array in in and out parameters.
 *
 * \remark AMR uses mostly the same filter (cut-off frequency 60Hz, same formula,
 *         but constants differs in 5th sign after comma). Fortunately in
 *         fixed-point all coefficients are the same as in G.729. Thus this
 *         routine can be used for the fixed-point AMR decoder, too.
 */
void ff_acelp_high_pass_filter(
        int16_t* out,
        int hpf_f[2],
        const int16_t* in,
        int length);

#endif // FFMPEG_ACELP_FILTERS_H
