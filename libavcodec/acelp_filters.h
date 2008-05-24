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
 * low-pass FIR (Finite Impulse Response) filter coefficients
 *
 *   A similar filter is named b30 in G.729.
 *
 *   G.729 specification says:
 *     b30 is based on Hamming windowed sinc functions, truncated at +/-29 and
 *     padded with zeros at +/-30 b30[30]=0.
 *     The filter has a cut-off frequency (-3 dB) at 3600 Hz in the oversampled
 *     domain.
 *
 *   After some analysis, I found this approximation:
 *
 *                                    PI * x
 *   Hamm(x,N) = 0.53836-0.46164*cos(--------)
 *                                      N-1
 *                                      ---
 *                                       2
 *
 *                                                             PI * x
 *   Hamm'(x,k) = Hamm(x - k, 2*k+1) =  0.53836 + 0.46164*cos(--------)
 *                                                                k
 *
 *             sin(PI * x)
 *   Sinc(x) = ----------- (normalized sinc function)
 *               PI * x
 *
 *   h(t,B) = 2 * B * Sinc(2 * B * t) (impulse response of sinc low-pass filter)
 *
 *   b(k,B, n) = Hamm'(n, k) * h(n, B)
 *
 *
 *       3600
 *   B = ----
 *       8000
 *
 *   3600 - cut-off frequency
 *   8000 - sampling rate
 *   k    - filter order
 *
 *   ff_acelp_interp_filter[6*i+j] = b(10, 3600/8000, i+j/6)
 *
 * The filter assumes the following order of fractions (X - integer delay):
 *
 * 1/3 precision: X     1/3      2/3      X     1/3      2/3      X
 * 1/6 precision: X 1/6 2/6 3/6  4/6  5/6 X 1/6 2/6 3/6  4/6  5/6 X
 *
 * The filter can be used for 1/3 precision, too, by
 * passing 2*pitch_delay_frac as third parameter to the interpolation routine.
 *
 */
extern const int16_t ff_acelp_interp_filter[61];

/**
 * \brief Generic interpolation routine
 * \param out [out] buffer for interpolated data
 * \param in input data
 * \param filter_coeffs interpolation filter coefficients (0.15)
 * \param precision filter is able to interpolate with 1/precision precision of pitch delay
 * \param pitch_delay_frac pitch delay, fractional part [0..precision-1]
 * \param filter_length filter length
 * \param length length of speech data to process
 *
 * filter_coeffs contains coefficients of the positive half of the symmetric
 * interpolation filter. filter_coeffs[0] should the central (unpaired) coefficient.
 * See ff_acelp_interp_filter fot example.
 *
 */
void ff_acelp_interpolate(
        int16_t* out,
        const int16_t* in,
        const int16_t* filter_coeffs,
        int precision,
        int pitch_delay_frac,
        int filter_length,
        int length);

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

#endif /* FFMPEG_ACELP_FILTERS_H */
