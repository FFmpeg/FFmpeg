/*
 * QCELP decoder
 * Copyright (c) 2007 Reynaldo H. Verdejo Pinochet
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

/**
 * @file qcelpdec.c
 * QCELP decoder
 * @author Reynaldo H. Verdejo Pinochet
 * @remark FFmpeg merging spearheaded by Kenan Gillet
 */

#include <stddef.h>

#include "avcodec.h"
#include "bitstream.h"

#include "qcelp.h"
#include "qcelpdata.h"

#include "celp_math.h"
#include "celp_filters.h"

#undef NDEBUG
#include <assert.h>

static void weighted_vector_sumf(float *out, const float *in_a,
                                 const float *in_b, float weight_coeff_a,
                                 float weight_coeff_b, int length)
{
    int i;

    for(i=0; i<length; i++)
        out[i] = weight_coeff_a * in_a[i]
               + weight_coeff_b * in_b[i];
}

/**
 * Initialize the speech codec according to the specification.
 *
 * TIA/EIA/IS-733 2.4.9
 */
static av_cold int qcelp_decode_init(AVCodecContext *avctx)
{
    QCELPContext *q = avctx->priv_data;
    int i;

    avctx->sample_fmt = SAMPLE_FMT_FLT;

    for (i = 0; i < 10; i++)
        q->prev_lspf[i] = (i + 1) / 11.;

    return 0;
}

/**
 * Decodes the 10 quantized LSP frequencies from the LSPV/LSP
 * transmission codes of any bitrate and checks for badly received packets.
 *
 * @param q the context
 * @param lspf line spectral pair frequencies
 *
 * @return 0 on success, -1 if the packet is badly received
 *
 * TIA/EIA/IS-733 2.4.3.2.6.2-2, 2.4.8.7.3
 */
static int decode_lspf(QCELPContext *q, float *lspf)
{
    int i;
    float tmp_lspf;

    if(q->bitrate == RATE_OCTAVE || q->bitrate == I_F_Q)
    {
        float smooth;
        const float *predictors = (q->prev_bitrate != RATE_OCTAVE &&
                                   q->prev_bitrate != I_F_Q ? q->prev_lspf
                                                            : q->predictor_lspf);

        if(q->bitrate == RATE_OCTAVE)
        {
            q->octave_count++;

            for(i=0; i<10; i++)
            {
                q->predictor_lspf[i] =
                             lspf[i] = (q->lspv[i] ?  QCELP_LSP_SPREAD_FACTOR
                                                   : -QCELP_LSP_SPREAD_FACTOR)
                                     + predictors[i] * QCELP_LSP_OCTAVE_PREDICTOR
                                     + (i + 1) * ((1 - QCELP_LSP_OCTAVE_PREDICTOR)/11);
            }
            smooth = (q->octave_count < 10 ? .875 : 0.1);
        }else
        {
            float erasure_coeff = QCELP_LSP_OCTAVE_PREDICTOR;

            assert(q->bitrate == I_F_Q);

            if(q->erasure_count > 1)
                erasure_coeff *= (q->erasure_count < 4 ? 0.9 : 0.7);

            for(i=0; i<10; i++)
            {
                q->predictor_lspf[i] =
                             lspf[i] = (i + 1) * ( 1 - erasure_coeff)/11
                                     + erasure_coeff * predictors[i];
            }
            smooth = 0.125;
        }

        // Check the stability of the LSP frequencies.
        lspf[0] = FFMAX(lspf[0], QCELP_LSP_SPREAD_FACTOR);
        for(i=1; i<10; i++)
            lspf[i] = FFMAX(lspf[i], (lspf[i-1] + QCELP_LSP_SPREAD_FACTOR));

        lspf[9] = FFMIN(lspf[9], (1.0 - QCELP_LSP_SPREAD_FACTOR));
        for(i=9; i>0; i--)
            lspf[i-1] = FFMIN(lspf[i-1], (lspf[i] - QCELP_LSP_SPREAD_FACTOR));

        // Low-pass filter the LSP frequencies.
        weighted_vector_sumf(lspf, lspf, q->prev_lspf, smooth, 1.0-smooth, 10);
    }else
    {
        q->octave_count = 0;

        tmp_lspf = 0.;
        for(i=0; i<5 ; i++)
        {
            lspf[2*i+0] = tmp_lspf += qcelp_lspvq[i][q->lspv[i]][0] * 0.0001;
            lspf[2*i+1] = tmp_lspf += qcelp_lspvq[i][q->lspv[i]][1] * 0.0001;
        }

        // Check for badly received packets.
        if(q->bitrate == RATE_QUARTER)
        {
            if(lspf[9] <= .70 || lspf[9] >=  .97)
                return -1;
            for(i=3; i<10; i++)
                if(fabs(lspf[i] - lspf[i-2]) < .08)
                    return -1;
        }else
        {
            if(lspf[9] <= .66 || lspf[9] >= .985)
                return -1;
            for(i=4; i<10; i++)
                if (fabs(lspf[i] - lspf[i-4]) < .0931)
                    return -1;
        }
    }
    return 0;
}

/**
 * If the received packet is Rate 1/4 a further sanity check is made of the
 * codebook gain.
 *
 * @param cbgain the unpacked cbgain array
 * @return -1 if the sanity check fails, 0 otherwise
 *
 * TIA/EIA/IS-733 2.4.8.7.3
 */
static int codebook_sanity_check_for_rate_quarter(const uint8_t *cbgain)
{
   int i, prev_diff=0;

    for(i=1; i<5; i++)
    {
        int diff = cbgain[i] - cbgain[i-1];
        if(FFABS(diff) > 10)
            return -1;
        else if(FFABS(diff - prev_diff) > 12)
            return -1;
        prev_diff = diff;
   }
   return 0;
}

/**
 * Computes the scaled codebook vector Cdn From INDEX and GAIN
 * for all rates.
 *
 * The specification lacks some information here.
 *
 * TIA/EIA/IS-733 has an omission on the codebook index determination
 * formula for RATE_FULL and RATE_HALF frames at section 2.4.8.1.1. It says
 * you have to subtract the decoded index parameter from the given scaled
 * codebook vector index 'n' to get the desired circular codebook index, but
 * it does not mention that you have to clamp 'n' to [0-9] in order to get
 * RI-compliant results.
 *
 * The reason for this mistake seems to be the fact they forgot to mention you
 * have to do these calculations per codebook subframe and adjust given
 * equation values accordingly.
 *
 * @param q the context
 * @param gain array holding the 4 pitch subframe gain values
 * @param cdn_vector array for the generated scaled codebook vector
 */
static void compute_svector(const QCELPContext *q, const float *gain,
                            float *cdn_vector)
{
    int      i, j, k;
    uint16_t cbseed, cindex;
    float    *rnd, tmp_gain, fir_filter_value;

    switch(q->bitrate)
    {
        case RATE_FULL:
            for(i=0; i<16; i++)
            {
                tmp_gain = gain[i] * QCELP_RATE_FULL_CODEBOOK_RATIO;
                cindex = -q->cindex[i];
                for(j=0; j<10; j++)
                    *cdn_vector++ = tmp_gain * qcelp_rate_full_codebook[cindex++ & 127];
            }
        break;
        case RATE_HALF:
            for(i=0; i<4; i++)
            {
                tmp_gain = gain[i] * QCELP_RATE_HALF_CODEBOOK_RATIO;
                cindex = -q->cindex[i];
                for (j = 0; j < 40; j++)
                *cdn_vector++ = tmp_gain * qcelp_rate_half_codebook[cindex++ & 127];
            }
        break;
        case RATE_QUARTER:
            cbseed = (0x0003 & q->lspv[4])<<14 |
                     (0x003F & q->lspv[3])<< 8 |
                     (0x0060 & q->lspv[2])<< 1 |
                     (0x0007 & q->lspv[1])<< 3 |
                     (0x0038 & q->lspv[0])>> 3 ;
            rnd = q->rnd_fir_filter_mem + 20;
            for(i=0; i<8; i++)
            {
                tmp_gain = gain[i] * (QCELP_SQRT1887 / 32768.0);
                for(k=0; k<20; k++)
                {
                    cbseed = 521 * cbseed + 259;
                    *rnd = (int16_t)cbseed;

                    // FIR filter
                    fir_filter_value = 0.0;
                    for(j=0; j<10; j++)
                        fir_filter_value += qcelp_rnd_fir_coefs[j ]
                                          * (rnd[-j ] + rnd[-20+j]);

                    fir_filter_value += qcelp_rnd_fir_coefs[10] * rnd[-10];
                    *cdn_vector++ = tmp_gain * fir_filter_value;
                    rnd++;
                }
            }
            memcpy(q->rnd_fir_filter_mem, q->rnd_fir_filter_mem + 160, 20 * sizeof(float));
        break;
        case RATE_OCTAVE:
            cbseed = q->first16bits;
            for(i=0; i<8; i++)
            {
                tmp_gain = gain[i] * (QCELP_SQRT1887 / 32768.0);
                for(j=0; j<20; j++)
                {
                    cbseed = 521 * cbseed + 259;
                    *cdn_vector++ = tmp_gain * (int16_t)cbseed;
                }
            }
        break;
        case I_F_Q:
            cbseed = -44; // random codebook index
            for(i=0; i<4; i++)
            {
                tmp_gain = gain[i] * QCELP_RATE_FULL_CODEBOOK_RATIO;
                for(j=0; j<40; j++)
                    *cdn_vector++ = tmp_gain * qcelp_rate_full_codebook[cbseed++ & 127];
            }
        break;
    }
}

/**
 * Apply generic gain control.
 *
 * @param v_out output vector
 * @param v_in gain-controlled vector
 * @param v_ref vector to control gain of
 *
 * FIXME: If v_ref is a zero vector, it energy is zero
 *        and the behavior of the gain control is
 *        undefined in the specs.
 *
 * TIA/EIA/IS-733 2.4.8.3-2/3/4/5, 2.4.8.6
 */
static void apply_gain_ctrl(float *v_out, const float *v_ref,
                            const float *v_in)
{
    int   i, j, len;
    float scalefactor;

    for(i=0, j=0; i<4; i++)
    {
        scalefactor = ff_dot_productf(v_in + j, v_in + j, 40);
        if(scalefactor)
            scalefactor = sqrt(ff_dot_productf(v_ref + j, v_ref + j, 40)
                        / scalefactor);
        else
            av_log_missing_feature(NULL, "Zero energy for gain control", 1);
        for(len=j+40; j<len; j++)
            v_out[j] = scalefactor * v_in[j];
    }
}

/**
 * Apply filter in pitch-subframe steps.
 *
 * @param memory buffer for the previous state of the filter
 *        - must be able to contain 303 elements
 *        - the 143 first elements are from the previous state
 *        - the next 160 are for output
 * @param v_in input filter vector
 * @param gain per-subframe gain array, each element is between 0.0 and 2.0
 * @param lag per-subframe lag array, each element is
 *        - between 16 and 143 if its corresponding pfrac is 0,
 *        - between 16 and 139 otherwise
 * @param pfrac per-subframe boolean array, 1 if the lag is fractional, 0
 *        otherwise
 *
 * @return filter output vector
 */
static const float *do_pitchfilter(float memory[303], const float v_in[160],
                                   const float gain[4], const uint8_t *lag,
                                   const uint8_t pfrac[4])
{
    int         i, j;
    float       *v_lag, *v_out;
    const float *v_len;

    v_out = memory + 143; // Output vector starts at memory[143].

    for(i=0; i<4; i++)
    {
        if(gain[i])
        {
            v_lag = memory + 143 + 40 * i - lag[i];
            for(v_len=v_in+40; v_in<v_len; v_in++)
            {
                if(pfrac[i]) // If it is a fractional lag...
                {
                    for(j=0, *v_out=0.; j<4; j++)
                        *v_out += qcelp_hammsinc_table[j] * (v_lag[j-4] + v_lag[3-j]);
                }else
                    *v_out = *v_lag;

                *v_out = *v_in + gain[i] * *v_out;

                v_lag++;
                v_out++;
            }
        }else
        {
            memcpy(v_out, v_in, 40 * sizeof(float));
            v_in  += 40;
            v_out += 40;
        }
    }

    memmove(memory, memory + 160, 143 * sizeof(float));
    return memory + 143;
}

/**
 * Interpolates LSP frequencies and computes LPC coefficients
 * for a given bitrate & pitch subframe.
 *
 * TIA/EIA/IS-733 2.4.3.3.4
 *
 * @param q the context
 * @param curr_lspf LSP frequencies vector of the current frame
 * @param lpc float vector for the resulting LPC
 * @param subframe_num frame number in decoded stream
 */
void interpolate_lpc(QCELPContext *q, const float *curr_lspf, float *lpc,
                     const int subframe_num)
{
    float interpolated_lspf[10];
    float weight;

    if(q->bitrate >= RATE_QUARTER)
        weight = 0.25 * (subframe_num + 1);
    else if(q->bitrate == RATE_OCTAVE && !subframe_num)
        weight = 0.625;
    else
        weight = 1.0;

    if(weight != 1.0)
    {
        weighted_vector_sumf(interpolated_lspf, curr_lspf, q->prev_lspf,
                             weight, 1.0 - weight, 10);
        qcelp_lspf2lpc(interpolated_lspf, lpc);
    }else if(q->bitrate >= RATE_QUARTER || (q->bitrate == I_F_Q && !subframe_num))
        qcelp_lspf2lpc(curr_lspf, lpc);
}

static int buf_size2bitrate(const int buf_size)
{
    switch(buf_size)
    {
        case 35:
            return RATE_FULL;
        case 17:
            return RATE_HALF;
        case  8:
            return RATE_QUARTER;
        case  4:
            return RATE_OCTAVE;
        case  1:
            return SILENCE;
    }

    return -1;
}

static void warn_insufficient_frame_quality(AVCodecContext *avctx,
                                            const char *message)
{
    av_log(avctx, AV_LOG_WARNING, "Frame #%d, IFQ: %s\n", avctx->frame_number,
           message);
}

AVCodec qcelp_decoder =
{
    .name   = "qcelp",
    .type   = CODEC_TYPE_AUDIO,
    .id     = CODEC_ID_QCELP,
    .init   = qcelp_decode_init,
    .decode = qcelp_decode_frame,
    .priv_data_size = sizeof(QCELPContext),
    .long_name = NULL_IF_CONFIG_SMALL("QCELP / PureVoice"),
};
