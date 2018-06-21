/*
 * G.729, G729 Annex D postfilter
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
#include <inttypes.h>
#include <limits.h>

#include "avcodec.h"
#include "g729.h"
#include "acelp_pitch_delay.h"
#include "g729postfilter.h"
#include "celp_math.h"
#include "acelp_filters.h"
#include "acelp_vectors.h"
#include "celp_filters.h"

#define FRAC_BITS 15
#include "mathops.h"

/**
 * short interpolation filter (of length 33, according to spec)
 * for computing signal with non-integer delay
 */
static const int16_t ff_g729_interp_filt_short[(ANALYZED_FRAC_DELAYS+1)*SHORT_INT_FILT_LEN] = {
      0, 31650, 28469, 23705, 18050, 12266,  7041,  2873,
      0, -1597, -2147, -1992, -1492,  -933,  -484,  -188,
};

/**
 * long interpolation filter (of length 129, according to spec)
 * for computing signal with non-integer delay
 */
static const int16_t ff_g729_interp_filt_long[(ANALYZED_FRAC_DELAYS+1)*LONG_INT_FILT_LEN] = {
   0, 31915, 29436, 25569, 20676, 15206,  9639,  4439,
   0, -3390, -5579, -6549, -6414, -5392, -3773, -1874,
   0,  1595,  2727,  3303,  3319,  2850,  2030,  1023,
   0,  -887, -1527, -1860, -1876, -1614, -1150,  -579,
   0,   501,   859,  1041,  1044,   892,   631,   315,
   0,  -266,  -453,  -543,  -538,  -455,  -317,  -156,
   0,   130,   218,   258,   253,   212,   147,    72,
   0,   -59,  -101,  -122,  -123,  -106,   -77,   -40,
};

/**
 * formant_pp_factor_num_pow[i] = FORMANT_PP_FACTOR_NUM^(i+1)
 */
static const int16_t formant_pp_factor_num_pow[10]= {
  /* (0.15) */
  18022, 9912, 5451, 2998, 1649, 907, 499, 274, 151, 83
};

/**
 * formant_pp_factor_den_pow[i] = FORMANT_PP_FACTOR_DEN^(i+1)
 */
static const int16_t formant_pp_factor_den_pow[10] = {
  /* (0.15) */
  22938, 16057, 11240, 7868, 5508, 3856, 2699, 1889, 1322, 925
};

/**
 * \brief Residual signal calculation (4.2.1 if G.729)
 * \param out [out] output data filtered through A(z/FORMANT_PP_FACTOR_NUM)
 * \param filter_coeffs (3.12) A(z/FORMANT_PP_FACTOR_NUM) filter coefficients
 * \param in input speech data to process
 * \param subframe_size size of one subframe
 *
 * \note in buffer must contain 10 items of previous speech data before top of the buffer
 * \remark It is safe to pass the same buffer for input and output.
 */
static void residual_filter(int16_t* out, const int16_t* filter_coeffs, const int16_t* in,
                            int subframe_size)
{
    int i, n;

    for (n = subframe_size - 1; n >= 0; n--) {
        int sum = 0x800;
        for (i = 0; i < 10; i++)
            sum += filter_coeffs[i] * in[n - i - 1];

        out[n] = in[n] + (sum >> 12);
    }
}

/**
 * \brief long-term postfilter (4.2.1)
 * \param dsp initialized DSP context
 * \param pitch_delay_int integer part of the pitch delay in the first subframe
 * \param residual filtering input data
 * \param residual_filt [out] speech signal with applied A(z/FORMANT_PP_FACTOR_NUM) filter
 * \param subframe_size size of subframe
 *
 * \return 0 if long-term prediction gain is less than 3dB, 1 -  otherwise
 */
static int16_t long_term_filter(AudioDSPContext *adsp, int pitch_delay_int,
                                const int16_t* residual, int16_t *residual_filt,
                                int subframe_size)
{
    int i, k, tmp, tmp2;
    int sum;
    int L_temp0;
    int L_temp1;
    int64_t L64_temp0;
    int64_t L64_temp1;
    int16_t shift;
    int corr_int_num, corr_int_den;

    int ener;
    int16_t sh_ener;

    int16_t gain_num,gain_den; //selected signal's gain numerator and denominator
    int16_t sh_gain_num, sh_gain_den;
    int gain_num_square;

    int16_t gain_long_num,gain_long_den; //filtered through long interpolation filter signal's gain numerator and denominator
    int16_t sh_gain_long_num, sh_gain_long_den;

    int16_t best_delay_int, best_delay_frac;

    int16_t delayed_signal_offset;
    int lt_filt_factor_a, lt_filt_factor_b;

    int16_t * selected_signal;
    const int16_t * selected_signal_const; //Necessary to avoid compiler warning

    int16_t sig_scaled[SUBFRAME_SIZE + RES_PREV_DATA_SIZE];
    int16_t delayed_signal[ANALYZED_FRAC_DELAYS][SUBFRAME_SIZE+1];
    int corr_den[ANALYZED_FRAC_DELAYS][2];

    tmp = 0;
    for(i=0; i<subframe_size + RES_PREV_DATA_SIZE; i++)
        tmp |= FFABS(residual[i]);

    if(!tmp)
        shift = 3;
    else
        shift = av_log2(tmp) - 11;

    if (shift > 0)
        for (i = 0; i < subframe_size + RES_PREV_DATA_SIZE; i++)
            sig_scaled[i] = residual[i] >> shift;
    else
        for (i = 0; i < subframe_size + RES_PREV_DATA_SIZE; i++)
            sig_scaled[i] = residual[i] << -shift;

    /* Start of best delay searching code */
    gain_num = 0;

    ener = adsp->scalarproduct_int16(sig_scaled + RES_PREV_DATA_SIZE,
                                    sig_scaled + RES_PREV_DATA_SIZE,
                                    subframe_size);
    if (ener) {
        sh_ener = av_log2(ener) - 14;
        sh_ener = FFMAX(sh_ener, 0);
        ener >>= sh_ener;
        /* Search for best pitch delay.

                       sum{ r(n) * r(k,n) ] }^2
           R'(k)^2 := -------------------------
                       sum{ r(k,n) * r(k,n) }


           R(T)    :=  sum{ r(n) * r(n-T) ] }


           where
           r(n-T) is integer delayed signal with delay T
           r(k,n) is non-integer delayed signal with integer delay best_delay
           and fractional delay k */

        /* Find integer delay best_delay which maximizes correlation R(T).

           This is also equals to numerator of R'(0),
           since the fine search (second step) is done with 1/8
           precision around best_delay. */
        corr_int_num = 0;
        best_delay_int = pitch_delay_int - 1;
        for (i = pitch_delay_int - 1; i <= pitch_delay_int + 1; i++) {
            sum = adsp->scalarproduct_int16(sig_scaled + RES_PREV_DATA_SIZE,
                                           sig_scaled + RES_PREV_DATA_SIZE - i,
                                           subframe_size);
            if (sum > corr_int_num) {
                corr_int_num = sum;
                best_delay_int = i;
            }
        }
        if (corr_int_num) {
            /* Compute denominator of pseudo-normalized correlation R'(0). */
            corr_int_den = adsp->scalarproduct_int16(sig_scaled - best_delay_int + RES_PREV_DATA_SIZE,
                                                    sig_scaled - best_delay_int + RES_PREV_DATA_SIZE,
                                                    subframe_size);

            /* Compute signals with non-integer delay k (with 1/8 precision),
               where k is in [0;6] range.
               Entire delay is qual to best_delay+(k+1)/8
               This is archieved by applying an interpolation filter of
               legth 33 to source signal. */
            for (k = 0; k < ANALYZED_FRAC_DELAYS; k++) {
                ff_acelp_interpolate(&delayed_signal[k][0],
                                     &sig_scaled[RES_PREV_DATA_SIZE - best_delay_int],
                                     ff_g729_interp_filt_short,
                                     ANALYZED_FRAC_DELAYS+1,
                                     8 - k - 1,
                                     SHORT_INT_FILT_LEN,
                                     subframe_size + 1);
            }

            /* Compute denominator of pseudo-normalized correlation R'(k).

                 corr_den[k][0] is square root of R'(k) denominator, for int(T) == int(T0)
                 corr_den[k][1] is square root of R'(k) denominator, for int(T) == int(T0)+1

              Also compute maximum value of above denominators over all k. */
            tmp = corr_int_den;
            for (k = 0; k < ANALYZED_FRAC_DELAYS; k++) {
                sum = adsp->scalarproduct_int16(&delayed_signal[k][1],
                                               &delayed_signal[k][1],
                                               subframe_size - 1);
                corr_den[k][0] = sum + delayed_signal[k][0            ] * delayed_signal[k][0            ];
                corr_den[k][1] = sum + delayed_signal[k][subframe_size] * delayed_signal[k][subframe_size];

                tmp = FFMAX3(tmp, corr_den[k][0], corr_den[k][1]);
            }

            sh_gain_den = av_log2(tmp) - 14;
            if (sh_gain_den >= 0) {

                sh_gain_num =  FFMAX(sh_gain_den, sh_ener);
                /* Loop through all k and find delay that maximizes
                   R'(k) correlation.
                   Search is done in [int(T0)-1; intT(0)+1] range
                   with 1/8 precision. */
                delayed_signal_offset = 1;
                best_delay_frac = 0;
                gain_den = corr_int_den >> sh_gain_den;
                gain_num = corr_int_num >> sh_gain_num;
                gain_num_square = gain_num * gain_num;
                for (k = 0; k < ANALYZED_FRAC_DELAYS; k++) {
                    for (i = 0; i < 2; i++) {
                        int16_t gain_num_short, gain_den_short;
                        int gain_num_short_square;
                        /* Compute numerator of pseudo-normalized
                           correlation R'(k). */
                        sum = adsp->scalarproduct_int16(&delayed_signal[k][i],
                                                       sig_scaled + RES_PREV_DATA_SIZE,
                                                       subframe_size);
                        gain_num_short = FFMAX(sum >> sh_gain_num, 0);

                        /*
                                      gain_num_short_square                gain_num_square
                           R'(T)^2 = -----------------------, max R'(T)^2= --------------
                                           den                                 gain_den
                        */
                        gain_num_short_square = gain_num_short * gain_num_short;
                        gain_den_short = corr_den[k][i] >> sh_gain_den;

                        tmp = MULL(gain_num_short_square, gain_den, FRAC_BITS);
                        tmp2 = MULL(gain_num_square, gain_den_short, FRAC_BITS);

                        // R'(T)^2 > max R'(T)^2
                        if (tmp > tmp2) {
                            gain_num = gain_num_short;
                            gain_den = gain_den_short;
                            gain_num_square = gain_num_short_square;
                            delayed_signal_offset = i;
                            best_delay_frac = k + 1;
                        }
                    }
                }

                /*
                       R'(T)^2
                  2 * --------- < 1
                        R(0)
                */
                L64_temp0 =  (int64_t)gain_num_square  << ((sh_gain_num << 1) + 1);
                L64_temp1 = ((int64_t)gain_den * ener) << (sh_gain_den + sh_ener);
                if (L64_temp0 < L64_temp1)
                    gain_num = 0;
            } // if(sh_gain_den >= 0)
        } // if(corr_int_num)
    } // if(ener)
    /* End of best delay searching code  */

    if (!gain_num) {
        memcpy(residual_filt, residual + RES_PREV_DATA_SIZE, subframe_size * sizeof(int16_t));

        /* Long-term prediction gain is less than 3dB. Long-term postfilter is disabled. */
        return 0;
    }
    if (best_delay_frac) {
        /* Recompute delayed signal with an interpolation filter of length 129. */
        ff_acelp_interpolate(residual_filt,
                             &sig_scaled[RES_PREV_DATA_SIZE - best_delay_int + delayed_signal_offset],
                             ff_g729_interp_filt_long,
                             ANALYZED_FRAC_DELAYS + 1,
                             8 - best_delay_frac,
                             LONG_INT_FILT_LEN,
                             subframe_size + 1);
        /* Compute R'(k) correlation's numerator. */
        sum = adsp->scalarproduct_int16(residual_filt,
                                       sig_scaled + RES_PREV_DATA_SIZE,
                                       subframe_size);

        if (sum < 0) {
            gain_long_num = 0;
            sh_gain_long_num = 0;
        } else {
            tmp = av_log2(sum) - 14;
            tmp = FFMAX(tmp, 0);
            sum >>= tmp;
            gain_long_num = sum;
            sh_gain_long_num = tmp;
        }

        /* Compute R'(k) correlation's denominator. */
        sum = adsp->scalarproduct_int16(residual_filt, residual_filt, subframe_size);

        tmp = av_log2(sum) - 14;
        tmp = FFMAX(tmp, 0);
        sum >>= tmp;
        gain_long_den = sum;
        sh_gain_long_den = tmp;

        /* Select between original and delayed signal.
           Delayed signal will be selected if it increases R'(k)
           correlation. */
        L_temp0 = gain_num * gain_num;
        L_temp0 = MULL(L_temp0, gain_long_den, FRAC_BITS);

        L_temp1 = gain_long_num * gain_long_num;
        L_temp1 = MULL(L_temp1, gain_den, FRAC_BITS);

        tmp = ((sh_gain_long_num - sh_gain_num) << 1) - (sh_gain_long_den - sh_gain_den);
        if (tmp > 0)
            L_temp0 >>= tmp;
        else
            L_temp1 >>= -tmp;

        /* Check if longer filter increases the values of R'(k). */
        if (L_temp1 > L_temp0) {
            /* Select long filter. */
            selected_signal = residual_filt;
            gain_num = gain_long_num;
            gain_den = gain_long_den;
            sh_gain_num = sh_gain_long_num;
            sh_gain_den = sh_gain_long_den;
        } else
            /* Select short filter. */
            selected_signal = &delayed_signal[best_delay_frac-1][delayed_signal_offset];

        /* Rescale selected signal to original value. */
        if (shift > 0)
            for (i = 0; i < subframe_size; i++)
                selected_signal[i] <<= shift;
        else
            for (i = 0; i < subframe_size; i++)
                selected_signal[i] >>= -shift;

        /* necessary to avoid compiler warning */
        selected_signal_const = selected_signal;
    } // if(best_delay_frac)
    else
        selected_signal_const = residual + RES_PREV_DATA_SIZE - (best_delay_int + 1 - delayed_signal_offset);
#ifdef G729_BITEXACT
    tmp = sh_gain_num - sh_gain_den;
    if (tmp > 0)
        gain_den >>= tmp;
    else
        gain_num >>= -tmp;

    if (gain_num > gain_den)
        lt_filt_factor_a = MIN_LT_FILT_FACTOR_A;
    else {
        gain_num >>= 2;
        gain_den >>= 1;
        lt_filt_factor_a = (gain_den << 15) / (gain_den + gain_num);
    }
#else
    L64_temp0 = (((int64_t)gain_num) << sh_gain_num) >> 1;
    L64_temp1 = ((int64_t)gain_den) << sh_gain_den;
    lt_filt_factor_a = FFMAX((L64_temp1 << 15) / (L64_temp1 + L64_temp0), MIN_LT_FILT_FACTOR_A);
#endif

    /* Filter through selected filter. */
    lt_filt_factor_b = 32767 - lt_filt_factor_a + 1;

    ff_acelp_weighted_vector_sum(residual_filt, residual + RES_PREV_DATA_SIZE,
                                 selected_signal_const,
                                 lt_filt_factor_a, lt_filt_factor_b,
                                 1<<14, 15, subframe_size);

    // Long-term prediction gain is larger than 3dB.
    return 1;
}

/**
 * \brief Calculate reflection coefficient for tilt compensation filter (4.2.3).
 * \param dsp initialized DSP context
 * \param lp_gn (3.12) coefficients of A(z/FORMANT_PP_FACTOR_NUM) filter
 * \param lp_gd (3.12) coefficients of A(z/FORMANT_PP_FACTOR_DEN) filter
 * \param speech speech to update
 * \param subframe_size size of subframe
 *
 * \return (3.12) reflection coefficient
 *
 * \remark The routine also calculates the gain term for the short-term
 *         filter (gf) and multiplies the speech data by 1/gf.
 *
 * \note All members of lp_gn, except 10-19 must be equal to zero.
 */
static int16_t get_tilt_comp(AudioDSPContext *adsp, int16_t *lp_gn,
                             const int16_t *lp_gd, int16_t* speech,
                             int subframe_size)
{
    int rh1,rh0; // (3.12)
    int temp;
    int i;
    int gain_term;

    lp_gn[10] = 4096; //1.0 in (3.12)

    /* Apply 1/A(z/FORMANT_PP_FACTOR_DEN) filter to hf. */
    ff_celp_lp_synthesis_filter(lp_gn + 11, lp_gd + 1, lp_gn + 11, 22, 10, 0, 0, 0x800);
    /* Now lp_gn (starting with 10) contains impulse response
       of A(z/FORMANT_PP_FACTOR_NUM)/A(z/FORMANT_PP_FACTOR_DEN) filter. */

    rh0 = adsp->scalarproduct_int16(lp_gn + 10, lp_gn + 10, 20);
    rh1 = adsp->scalarproduct_int16(lp_gn + 10, lp_gn + 11, 20);

    /* downscale to avoid overflow */
    temp = av_log2(rh0) - 14;
    if (temp > 0) {
        rh0 >>= temp;
        rh1 >>= temp;
    }

    if (FFABS(rh1) > rh0 || !rh0)
        return 0;

    gain_term = 0;
    for (i = 0; i < 20; i++)
        gain_term += FFABS(lp_gn[i + 10]);
    gain_term >>= 2; // (3.12) -> (5.10)

    if (gain_term > 0x400) { // 1.0 in (5.10)
        temp = 0x2000000 / gain_term; // 1.0/gain_term in (0.15)
        for (i = 0; i < subframe_size; i++)
            speech[i] = (speech[i] * temp + 0x4000) >> 15;
    }

    return -(rh1 << 15) / rh0;
}

/**
 * \brief Apply tilt compensation filter (4.2.3).
 * \param res_pst [in/out] residual signal (partially filtered)
 * \param k1 (3.12) reflection coefficient
 * \param subframe_size size of subframe
 * \param ht_prev_data previous data for 4.2.3, equation 86
 *
 * \return new value for ht_prev_data
*/
static int16_t apply_tilt_comp(int16_t* out, int16_t* res_pst, int refl_coeff,
                               int subframe_size, int16_t ht_prev_data)
{
    int tmp, tmp2;
    int i;
    int gt, ga;
    int fact, sh_fact;

    if (refl_coeff > 0) {
        gt = (refl_coeff * G729_TILT_FACTOR_PLUS + 0x4000) >> 15;
        fact = 0x4000; // 0.5 in (0.15)
        sh_fact = 15;
    } else {
        gt = (refl_coeff * G729_TILT_FACTOR_MINUS + 0x4000) >> 15;
        fact = 0x800; // 0.5 in (3.12)
        sh_fact = 12;
    }
    ga = (fact << 15) / av_clip_int16(32768 - FFABS(gt));
    gt >>= 1;

    /* Apply tilt compensation filter to signal. */
    tmp = res_pst[subframe_size - 1];

    for (i = subframe_size - 1; i >= 1; i--) {
        tmp2 = (res_pst[i] << 15) + ((gt * res_pst[i-1]) << 1);
        tmp2 = (tmp2 + 0x4000) >> 15;

        tmp2 = (tmp2 * ga * 2 + fact) >> sh_fact;
        out[i] = tmp2;
    }
    tmp2 = (res_pst[0] << 15) + ((gt * ht_prev_data) << 1);
    tmp2 = (tmp2 + 0x4000) >> 15;
    tmp2 = (tmp2 * ga * 2 + fact) >> sh_fact;
    out[0] = tmp2;

    return tmp;
}

void ff_g729_postfilter(AudioDSPContext *adsp, int16_t* ht_prev_data, int* voicing,
                     const int16_t *lp_filter_coeffs, int pitch_delay_int,
                     int16_t* residual, int16_t* res_filter_data,
                     int16_t* pos_filter_data, int16_t *speech, int subframe_size)
{
    int16_t residual_filt_buf[SUBFRAME_SIZE+11];
    int16_t lp_gn[33]; // (3.12)
    int16_t lp_gd[11]; // (3.12)
    int tilt_comp_coeff;
    int i;

    /* Zero-filling is necessary for tilt-compensation filter. */
    memset(lp_gn, 0, 33 * sizeof(int16_t));

    /* Calculate A(z/FORMANT_PP_FACTOR_NUM) filter coefficients. */
    for (i = 0; i < 10; i++)
        lp_gn[i + 11] = (lp_filter_coeffs[i + 1] * formant_pp_factor_num_pow[i] + 0x4000) >> 15;

    /* Calculate A(z/FORMANT_PP_FACTOR_DEN) filter coefficients. */
    for (i = 0; i < 10; i++)
        lp_gd[i + 1] = (lp_filter_coeffs[i + 1] * formant_pp_factor_den_pow[i] + 0x4000) >> 15;

    /* residual signal calculation (one-half of short-term postfilter) */
    memcpy(speech - 10, res_filter_data, 10 * sizeof(int16_t));
    residual_filter(residual + RES_PREV_DATA_SIZE, lp_gn + 11, speech, subframe_size);
    /* Save data to use it in the next subframe. */
    memcpy(res_filter_data, speech + subframe_size - 10, 10 * sizeof(int16_t));

    /* long-term filter. If long-term prediction gain is larger than 3dB (returned value is
       nonzero) then declare current subframe as periodic. */
    i = long_term_filter(adsp, pitch_delay_int,
                                                residual, residual_filt_buf + 10,
                                                subframe_size);
    *voicing = FFMAX(*voicing, i);

    /* shift residual for using in next subframe */
    memmove(residual, residual + subframe_size, RES_PREV_DATA_SIZE * sizeof(int16_t));

    /* short-term filter tilt compensation */
    tilt_comp_coeff = get_tilt_comp(adsp, lp_gn, lp_gd, residual_filt_buf + 10, subframe_size);

    /* Apply second half of short-term postfilter: 1/A(z/FORMANT_PP_FACTOR_DEN) */
    ff_celp_lp_synthesis_filter(pos_filter_data + 10, lp_gd + 1,
                                residual_filt_buf + 10,
                                subframe_size, 10, 0, 0, 0x800);
    memcpy(pos_filter_data, pos_filter_data + subframe_size, 10 * sizeof(int16_t));

    *ht_prev_data = apply_tilt_comp(speech, pos_filter_data + 10, tilt_comp_coeff,
                                    subframe_size, *ht_prev_data);
}

/**
 * \brief Adaptive gain control (4.2.4)
 * \param gain_before gain of speech before applying postfilters
 * \param gain_after  gain of speech after applying postfilters
 * \param speech [in/out] signal buffer
 * \param subframe_size length of subframe
 * \param gain_prev (3.12) previous value of gain coefficient
 *
 * \return (3.12) last value of gain coefficient
 */
int16_t ff_g729_adaptive_gain_control(int gain_before, int gain_after, int16_t *speech,
                                   int subframe_size, int16_t gain_prev)
{
    int gain; // (3.12)
    int n;
    int exp_before, exp_after;

    if(!gain_after && gain_before)
        return 0;

    if (gain_before) {

        exp_before  = 14 - av_log2(gain_before);
        gain_before = bidir_sal(gain_before, exp_before);

        exp_after  = 14 - av_log2(gain_after);
        gain_after = bidir_sal(gain_after, exp_after);

        if (gain_before < gain_after) {
            gain = (gain_before << 15) / gain_after;
            gain = bidir_sal(gain, exp_after - exp_before - 1);
        } else {
            gain = ((gain_before - gain_after) << 14) / gain_after + 0x4000;
            gain = bidir_sal(gain, exp_after - exp_before);
        }
        gain = (gain * G729_AGC_FAC1 + 0x4000) >> 15; // gain * (1-0.9875)
    } else
        gain = 0;

    for (n = 0; n < subframe_size; n++) {
        // gain_prev = gain + 0.9875 * gain_prev
        gain_prev = (G729_AGC_FACTOR * gain_prev + 0x4000) >> 15;
        gain_prev = av_clip_int16(gain + gain_prev);
        speech[n] = av_clip_int16((speech[n] * gain_prev + 0x2000) >> 14);
    }
    return gain_prev;
}
