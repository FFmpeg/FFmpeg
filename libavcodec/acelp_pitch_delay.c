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

#include "avcodec.h"
#include "dsputil.h"
#include "acelp_pitch_delay.h"
#include "celp_math.h"

int ff_acelp_decode_8bit_to_1st_delay3(int ac_index)
{
    ac_index += 58;
    if(ac_index > 254)
        ac_index = 3 * ac_index - 510;
    return ac_index;
}

int ff_acelp_decode_4bit_to_2nd_delay3(
        int ac_index,
        int pitch_delay_min)
{
    if(ac_index < 4)
        return 3 * (ac_index + pitch_delay_min);
    else if(ac_index < 12)
        return 3 * pitch_delay_min + ac_index + 6;
    else
        return 3 * (ac_index + pitch_delay_min) - 18;
}

int ff_acelp_decode_5_6_bit_to_2nd_delay3(
        int ac_index,
        int pitch_delay_min)
{
        return 3 * pitch_delay_min + ac_index - 2;
}

int ff_acelp_decode_9bit_to_1st_delay6(int ac_index)
{
    if(ac_index < 463)
        return ac_index + 105;
    else
        return 6 * (ac_index - 368);
}
int ff_acelp_decode_6bit_to_2nd_delay6(
        int ac_index,
        int pitch_delay_min)
{
    return 6 * pitch_delay_min + ac_index - 3;
}

void ff_acelp_update_past_gain(
    int16_t* quant_energy,
    int gain_corr_factor,
    int log2_ma_pred_order,
    int erasure)
{
    int i;
    int avg_gain=quant_energy[(1 << log2_ma_pred_order) - 1]; // (5.10)

    for(i=(1 << log2_ma_pred_order) - 1; i>0; i--)
    {
        avg_gain       += quant_energy[i-1];
        quant_energy[i] = quant_energy[i-1];
    }

    if(erasure)
        quant_energy[0] = FFMAX(avg_gain >> log2_ma_pred_order, -10240) - 4096; // -10 and -4 in (5.10)
    else
        quant_energy[0] = (6165 * ((ff_log2(gain_corr_factor) >> 2) - (13 << 13))) >> 13;
}

int16_t ff_acelp_decode_gain_code(
    DSPContext *dsp,
    int gain_corr_factor,
    const int16_t* fc_v,
    int mr_energy,
    const int16_t* quant_energy,
    const int16_t* ma_prediction_coeff,
    int subframe_size,
    int ma_pred_order)
{
    int i;

    mr_energy <<= 10;

    for(i=0; i<ma_pred_order; i++)
        mr_energy += quant_energy[i] * ma_prediction_coeff[i];

#ifdef G729_BITEXACT
    mr_energy += (((-6165LL * ff_log2(dsp->scalarproduct_int16(fc_v, fc_v, subframe_size, 0))) >> 3) & ~0x3ff);

    mr_energy = (5439 * (mr_energy >> 15)) >> 8;           // (0.15) = (0.15) * (7.23)

    return bidir_sal(
               ((ff_exp2(mr_energy & 0x7fff) + 16) >> 5) * (gain_corr_factor >> 1),
               (mr_energy >> 15) - 25
           );
#else
    mr_energy = gain_corr_factor * exp(M_LN10 / (20 << 23) * mr_energy) /
                sqrt(dsp->scalarproduct_int16(fc_v, fc_v, subframe_size, 0));
    return mr_energy >> 12;
#endif
}
