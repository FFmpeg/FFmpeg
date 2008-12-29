/*
 * gain code, gain pitch and pitch delay decoding
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

#ifndef AVCODEC_ACELP_PITCH_DELAY_H
#define AVCODEC_ACELP_PITCH_DELAY_H

#include <stdint.h>
#include "dsputil.h"

#define PITCH_DELAY_MIN             20
#define PITCH_DELAY_MAX             143

/**
 * \brief Decode pitch delay of the first subframe encoded by 8 bits with 1/3
 *        resolution.
 * \param ac_index adaptive codebook index (8 bits)
 *
 * \return pitch delay in 1/3 units
 *
 * Pitch delay is coded:
 *    with 1/3 resolution, 19  < pitch_delay <  85
 *    integers only,       85 <= pitch_delay <= 143
 */
int ff_acelp_decode_8bit_to_1st_delay3(int ac_index);

/**
 * \brief Decode pitch delay of the second subframe encoded by 5 or 6 bits
 *        with 1/3 precision.
 * \param ac_index adaptive codebook index (5 or 6 bits)
 * \param pitch_delay_min lower bound (integer) of pitch delay interval
 *                      for second subframe
 *
 * \return pitch delay in 1/3 units
 *
 * Pitch delay is coded:
 *    with 1/3 resolution, -6 < pitch_delay - int(prev_pitch_delay) < 5
 *
 * \remark The routine is used in G.729 @8k, AMR @10.2k, AMR @7.95k,
 *         AMR @7.4k for the second subframe.
 */
int ff_acelp_decode_5_6_bit_to_2nd_delay3(
        int ac_index,
        int pitch_delay_min);

/**
 * \brief Decode pitch delay with 1/3 precision.
 * \param ac_index adaptive codebook index (4 bits)
 * \param pitch_delay_min lower bound (integer) of pitch delay interval for
 *                      second subframe
 *
 * \return pitch delay in 1/3 units
 *
 * Pitch delay is coded:
 *    integers only,          -6  < pitch_delay - int(prev_pitch_delay) <= -2
 *    with 1/3 resolution,    -2  < pitch_delay - int(prev_pitch_delay) <  1
 *    integers only,           1 <= pitch_delay - int(prev_pitch_delay) <  5
 *
 * \remark The routine is used in G.729 @6.4k, AMR @6.7k, AMR @5.9k,
 *         AMR @5.15k, AMR @4.75k for the second subframe.
 */
int ff_acelp_decode_4bit_to_2nd_delay3(
        int ac_index,
        int pitch_delay_min);

/**
 * \brief Decode pitch delay of the first subframe encoded by 9 bits
 *        with 1/6 precision.
 * \param ac_index adaptive codebook index (9 bits)
 * \param pitch_delay_min lower bound (integer) of pitch delay interval for
 *                      second subframe
 *
 * \return pitch delay in 1/6 units
 *
 * Pitch delay is coded:
 *    with 1/6 resolution,  17  < pitch_delay <  95
 *    integers only,        95 <= pitch_delay <= 143
 *
 * \remark The routine is used in AMR @12.2k for the first and third subframes.
 */
int ff_acelp_decode_9bit_to_1st_delay6(int ac_index);

/**
 * \brief Decode pitch delay of the second subframe encoded by 6 bits
 *        with 1/6 precision.
 * \param ac_index adaptive codebook index (6 bits)
 * \param pitch_delay_min lower bound (integer) of pitch delay interval for
 *                      second subframe
 *
 * \return pitch delay in 1/6 units
 *
 * Pitch delay is coded:
 *    with 1/6 resolution, -6 < pitch_delay - int(prev_pitch_delay) < 5
 *
 * \remark The routine is used in AMR @12.2k for the second and fourth subframes.
 */
int ff_acelp_decode_6bit_to_2nd_delay6(
        int ac_index,
        int pitch_delay_min);

/**
 * \brief Update past quantized energies
 * \param quant_energy [in/out] past quantized energies (5.10)
 * \param gain_corr_factor gain correction factor
 * \param log2_ma_pred_order log2() of MA prediction order
 * \param erasure frame erasure flag
 *
 * If frame erasure flag is not equal to zero, memory is updated with
 * averaged energy, attenuated by 4dB:
 *     max(avg(quant_energy[i])-4, -14), i=0,ma_pred_order
 *
 * In normal mode memory is updated with
 *     Er - Ep = 20 * log10(gain_corr_factor)
 *
 * \remark The routine is used in G.729 and AMR (all modes).
 */
void ff_acelp_update_past_gain(
        int16_t* quant_energy,
        int gain_corr_factor,
        int log2_ma_pred_order,
        int erasure);

/**
 * \brief Decode the adaptive codebook gain and add
 *        correction (4.1.5 and 3.9.1 of G.729).
 * \param dsp initialized dsputil context
 * \param gain_corr_factor gain correction factor (2.13)
 * \param fc_v fixed-codebook vector (2.13)
 * \param mr_energy mean innovation energy and fixed-point correction (7.13)
 * \param quant_energy [in/out] past quantized energies (5.10)
 * \param subframe_size length of subframe
 * \param ma_pred_order MA prediction order
 *
 * \return quantized fixed-codebook gain (14.1)
 *
 * The routine implements equations 69, 66 and 71 of the G.729 specification (3.9.1)
 *
 *    Em   - mean innovation energy (dB, constant, depends on decoding algorithm)
 *    Ep   - mean-removed predicted energy (dB)
 *    Er   - mean-removed innovation energy (dB)
 *    Ei   - mean energy of the fixed-codebook contribution (dB)
 *    N    - subframe_size
 *    M    - MA (Moving Average) prediction order
 *    gc   - fixed-codebook gain
 *    gc_p - predicted fixed-codebook gain
 *
 *    Fixed codebook gain is computed using predicted gain gc_p and
 *    correction factor gain_corr_factor as shown below:
 *
 *        gc = gc_p * gain_corr_factor
 *
 *    The predicted fixed codebook gain gc_p is found by predicting
 *    the energy of the fixed-codebook contribution from the energy
 *    of previous fixed-codebook contributions.
 *
 *        mean = 1/N * sum(i,0,N){ fc_v[i] * fc_v[i] }
 *
 *        Ei = 10log(mean)
 *
 *        Er = 10log(1/N * gc^2 * mean) - Em = 20log(gc) + Ei - Em
 *
 *    Replacing Er with Ep and gc with gc_p we will receive:
 *
 *        Ep = 10log(1/N * gc_p^2 * mean) - Em = 20log(gc_p) + Ei - Em
 *
 *    and from above:
 *
 *        gc_p = 10^((Ep - Ei + Em) / 20)
 *
 *    Ep is predicted using past energies and prediction coefficients:
 *
 *        Ep = sum(i,0,M){ ma_prediction_coeff[i] * quant_energy[i] }
 *
 *    gc_p in fixed-point arithmetic is calculated as following:
 *
 *        mean = 1/N * sum(i,0,N){ (fc_v[i] / 2^13) * (fc_v[i] / 2^13) } =
 *        = 1/N * sum(i,0,N) { fc_v[i] * fc_v[i] } / 2^26
 *
 *        Ei = 10log(mean) = -10log(N) - 10log(2^26) +
 *        + 10log(sum(i,0,N) { fc_v[i] * fc_v[i] })
 *
 *        Ep - Ei + Em = Ep + Em + 10log(N) + 10log(2^26) -
 *        - 10log(sum(i,0,N) { fc_v[i] * fc_v[i] }) =
 *        = Ep + mr_energy - 10log(sum(i,0,N) { fc_v[i] * fc_v[i] })
 *
 *        gc_p = 10 ^ ((Ep - Ei + Em) / 20) =
 *        = 2 ^ (3.3219 * (Ep - Ei + Em) / 20) = 2 ^ (0.166 * (Ep - Ei + Em))
 *
 *    where
 *
 *        mr_energy = Em + 10log(N) + 10log(2^26)
 *
 * \remark The routine is used in G.729 and AMR (all modes).
 */
int16_t ff_acelp_decode_gain_code(
    DSPContext *dsp,
    int gain_corr_factor,
    const int16_t* fc_v,
    int mr_energy,
    const int16_t* quant_energy,
    const int16_t* ma_prediction_coeff,
    int subframe_size,
    int max_pred_order);

#endif /* AVCODEC_ACELP_PITCH_DELAY_H */
