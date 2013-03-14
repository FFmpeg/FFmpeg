/*
 * AMR narrowband decoder
 * Copyright (c) 2006-2007 Robert Swain
 * Copyright (c) 2009 Colin McQuillan
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
 * @file
 * AMR narrowband decoder
 *
 * This decoder uses floats for simplicity and so is not bit-exact. One
 * difference is that differences in phase can accumulate. The test sequences
 * in 3GPP TS 26.074 can still be useful.
 *
 * - Comparing this file's output to the output of the ref decoder gives a
 *   PSNR of 30 to 80. Plotting the output samples shows a difference in
 *   phase in some areas.
 *
 * - Comparing both decoders against their input, this decoder gives a similar
 *   PSNR. If the test sequence homing frames are removed (this decoder does
 *   not detect them), the PSNR is at least as good as the reference on 140
 *   out of 169 tests.
 */


#include <string.h>
#include <math.h>

#include "libavutil/channel_layout.h"
#include "libavutil/float_dsp.h"
#include "avcodec.h"
#include "libavutil/common.h"
#include "libavutil/avassert.h"
#include "celp_math.h"
#include "celp_filters.h"
#include "acelp_filters.h"
#include "acelp_vectors.h"
#include "acelp_pitch_delay.h"
#include "lsp.h"
#include "amr.h"
#include "internal.h"

#include "amrnbdata.h"

#define AMR_BLOCK_SIZE              160   ///< samples per frame
#define AMR_SAMPLE_BOUND        32768.0   ///< threshold for synthesis overflow

/**
 * Scale from constructed speech to [-1,1]
 *
 * AMR is designed to produce 16-bit PCM samples (3GPP TS 26.090 4.2) but
 * upscales by two (section 6.2.2).
 *
 * Fundamentally, this scale is determined by energy_mean through
 * the fixed vector contribution to the excitation vector.
 */
#define AMR_SAMPLE_SCALE  (2.0 / 32768.0)

/** Prediction factor for 12.2kbit/s mode */
#define PRED_FAC_MODE_12k2             0.65

#define LSF_R_FAC          (8000.0 / 32768.0) ///< LSF residual tables to Hertz
#define MIN_LSF_SPACING    (50.0488 / 8000.0) ///< Ensures stability of LPC filter
#define PITCH_LAG_MIN_MODE_12k2          18   ///< Lower bound on decoded lag search in 12.2kbit/s mode

/** Initial energy in dB. Also used for bad frames (unimplemented). */
#define MIN_ENERGY -14.0

/** Maximum sharpening factor
 *
 * The specification says 0.8, which should be 13107, but the reference C code
 * uses 13017 instead. (Amusingly the same applies to SHARP_MAX in g729dec.c.)
 */
#define SHARP_MAX 0.79449462890625

/** Number of impulse response coefficients used for tilt factor */
#define AMR_TILT_RESPONSE   22
/** Tilt factor = 1st reflection coefficient * gamma_t */
#define AMR_TILT_GAMMA_T   0.8
/** Adaptive gain control factor used in post-filter */
#define AMR_AGC_ALPHA      0.9

typedef struct AMRContext {
    AMRNBFrame                        frame; ///< decoded AMR parameters (lsf coefficients, codebook indexes, etc)
    uint8_t             bad_frame_indicator; ///< bad frame ? 1 : 0
    enum Mode                cur_frame_mode;

    int16_t     prev_lsf_r[LP_FILTER_ORDER]; ///< residual LSF vector from previous subframe
    double          lsp[4][LP_FILTER_ORDER]; ///< lsp vectors from current frame
    double   prev_lsp_sub4[LP_FILTER_ORDER]; ///< lsp vector for the 4th subframe of the previous frame

    float         lsf_q[4][LP_FILTER_ORDER]; ///< Interpolated LSF vector for fixed gain smoothing
    float          lsf_avg[LP_FILTER_ORDER]; ///< vector of averaged lsf vector

    float           lpc[4][LP_FILTER_ORDER]; ///< lpc coefficient vectors for 4 subframes

    uint8_t                   pitch_lag_int; ///< integer part of pitch lag from current subframe

    float excitation_buf[PITCH_DELAY_MAX + LP_FILTER_ORDER + 1 + AMR_SUBFRAME_SIZE]; ///< current excitation and all necessary excitation history
    float                       *excitation; ///< pointer to the current excitation vector in excitation_buf

    float   pitch_vector[AMR_SUBFRAME_SIZE]; ///< adaptive code book (pitch) vector
    float   fixed_vector[AMR_SUBFRAME_SIZE]; ///< algebraic codebook (fixed) vector (must be kept zero between frames)

    float               prediction_error[4]; ///< quantified prediction errors {20log10(^gamma_gc)} for previous four subframes
    float                     pitch_gain[5]; ///< quantified pitch gains for the current and previous four subframes
    float                     fixed_gain[5]; ///< quantified fixed gains for the current and previous four subframes

    float                              beta; ///< previous pitch_gain, bounded by [0.0,SHARP_MAX]
    uint8_t                      diff_count; ///< the number of subframes for which diff has been above 0.65
    uint8_t                      hang_count; ///< the number of subframes since a hangover period started

    float            prev_sparse_fixed_gain; ///< previous fixed gain; used by anti-sparseness processing to determine "onset"
    uint8_t               prev_ir_filter_nr; ///< previous impulse response filter "impNr": 0 - strong, 1 - medium, 2 - none
    uint8_t                 ir_filter_onset; ///< flag for impulse response filter strength

    float                postfilter_mem[10]; ///< previous intermediate values in the formant filter
    float                          tilt_mem; ///< previous input to tilt compensation filter
    float                    postfilter_agc; ///< previous factor used for adaptive gain control
    float                  high_pass_mem[2]; ///< previous intermediate values in the high-pass filter

    float samples_in[LP_FILTER_ORDER + AMR_SUBFRAME_SIZE]; ///< floating point samples

    ACELPFContext                     acelpf_ctx; ///< context for filters for ACELP-based codecs
    ACELPVContext                     acelpv_ctx; ///< context for vector operations for ACELP-based codecs
    CELPFContext                       celpf_ctx; ///< context for filters for CELP-based codecs
    CELPMContext                       celpm_ctx; ///< context for fixed point math operations

} AMRContext;

/** Double version of ff_weighted_vector_sumf() */
static void weighted_vector_sumd(double *out, const double *in_a,
                                 const double *in_b, double weight_coeff_a,
                                 double weight_coeff_b, int length)
{
    int i;

    for (i = 0; i < length; i++)
        out[i] = weight_coeff_a * in_a[i]
               + weight_coeff_b * in_b[i];
}

static av_cold int amrnb_decode_init(AVCodecContext *avctx)
{
    AMRContext *p = avctx->priv_data;
    int i;

    if (avctx->channels > 1) {
        avpriv_report_missing_feature(avctx, "multi-channel AMR");
        return AVERROR_PATCHWELCOME;
    }

    avctx->channels       = 1;
    avctx->channel_layout = AV_CH_LAYOUT_MONO;
    if (!avctx->sample_rate)
        avctx->sample_rate = 8000;
    avctx->sample_fmt     = AV_SAMPLE_FMT_FLT;

    // p->excitation always points to the same position in p->excitation_buf
    p->excitation = &p->excitation_buf[PITCH_DELAY_MAX + LP_FILTER_ORDER + 1];

    for (i = 0; i < LP_FILTER_ORDER; i++) {
        p->prev_lsp_sub4[i] =    lsp_sub4_init[i] * 1000 / (float)(1 << 15);
        p->lsf_avg[i] = p->lsf_q[3][i] = lsp_avg_init[i] / (float)(1 << 15);
    }

    for (i = 0; i < 4; i++)
        p->prediction_error[i] = MIN_ENERGY;

    ff_acelp_filter_init(&p->acelpf_ctx);
    ff_acelp_vectors_init(&p->acelpv_ctx);
    ff_celp_filter_init(&p->celpf_ctx);
    ff_celp_math_init(&p->celpm_ctx);

    return 0;
}


/**
 * Unpack an RFC4867 speech frame into the AMR frame mode and parameters.
 *
 * The order of speech bits is specified by 3GPP TS 26.101.
 *
 * @param p the context
 * @param buf               pointer to the input buffer
 * @param buf_size          size of the input buffer
 *
 * @return the frame mode
 */
static enum Mode unpack_bitstream(AMRContext *p, const uint8_t *buf,
                                  int buf_size)
{
    enum Mode mode;

    // Decode the first octet.
    mode = buf[0] >> 3 & 0x0F;                      // frame type
    p->bad_frame_indicator = (buf[0] & 0x4) != 0x4; // quality bit

    if (mode >= N_MODES || buf_size < frame_sizes_nb[mode] + 1) {
        return NO_DATA;
    }

    if (mode < MODE_DTX)
        ff_amr_bit_reorder((uint16_t *) &p->frame, sizeof(AMRNBFrame), buf + 1,
                           amr_unpacking_bitmaps_per_mode[mode]);

    return mode;
}


/// @name AMR pitch LPC coefficient decoding functions
/// @{

/**
 * Interpolate the LSF vector (used for fixed gain smoothing).
 * The interpolation is done over all four subframes even in MODE_12k2.
 *
 * @param[in]     ctx       The Context
 * @param[in,out] lsf_q     LSFs in [0,1] for each subframe
 * @param[in]     lsf_new   New LSFs in [0,1] for subframe 4
 */
static void interpolate_lsf(ACELPVContext *ctx, float lsf_q[4][LP_FILTER_ORDER], float *lsf_new)
{
    int i;

    for (i = 0; i < 4; i++)
        ctx->weighted_vector_sumf(lsf_q[i], lsf_q[3], lsf_new,
                                0.25 * (3 - i), 0.25 * (i + 1),
                                LP_FILTER_ORDER);
}

/**
 * Decode a set of 5 split-matrix quantized lsf indexes into an lsp vector.
 *
 * @param p the context
 * @param lsp output LSP vector
 * @param lsf_no_r LSF vector without the residual vector added
 * @param lsf_quantizer pointers to LSF dictionary tables
 * @param quantizer_offset offset in tables
 * @param sign for the 3 dictionary table
 * @param update store data for computing the next frame's LSFs
 */
static void lsf2lsp_for_mode12k2(AMRContext *p, double lsp[LP_FILTER_ORDER],
                                 const float lsf_no_r[LP_FILTER_ORDER],
                                 const int16_t *lsf_quantizer[5],
                                 const int quantizer_offset,
                                 const int sign, const int update)
{
    int16_t lsf_r[LP_FILTER_ORDER]; // residual LSF vector
    float lsf_q[LP_FILTER_ORDER]; // quantified LSF vector
    int i;

    for (i = 0; i < LP_FILTER_ORDER >> 1; i++)
        memcpy(&lsf_r[i << 1], &lsf_quantizer[i][quantizer_offset],
               2 * sizeof(*lsf_r));

    if (sign) {
        lsf_r[4] *= -1;
        lsf_r[5] *= -1;
    }

    if (update)
        memcpy(p->prev_lsf_r, lsf_r, LP_FILTER_ORDER * sizeof(*lsf_r));

    for (i = 0; i < LP_FILTER_ORDER; i++)
        lsf_q[i] = lsf_r[i] * (LSF_R_FAC / 8000.0) + lsf_no_r[i] * (1.0 / 8000.0);

    ff_set_min_dist_lsf(lsf_q, MIN_LSF_SPACING, LP_FILTER_ORDER);

    if (update)
        interpolate_lsf(&p->acelpv_ctx, p->lsf_q, lsf_q);

    ff_acelp_lsf2lspd(lsp, lsf_q, LP_FILTER_ORDER);
}

/**
 * Decode a set of 5 split-matrix quantized lsf indexes into 2 lsp vectors.
 *
 * @param p                 pointer to the AMRContext
 */
static void lsf2lsp_5(AMRContext *p)
{
    const uint16_t *lsf_param = p->frame.lsf;
    float lsf_no_r[LP_FILTER_ORDER]; // LSFs without the residual vector
    const int16_t *lsf_quantizer[5];
    int i;

    lsf_quantizer[0] = lsf_5_1[lsf_param[0]];
    lsf_quantizer[1] = lsf_5_2[lsf_param[1]];
    lsf_quantizer[2] = lsf_5_3[lsf_param[2] >> 1];
    lsf_quantizer[3] = lsf_5_4[lsf_param[3]];
    lsf_quantizer[4] = lsf_5_5[lsf_param[4]];

    for (i = 0; i < LP_FILTER_ORDER; i++)
        lsf_no_r[i] = p->prev_lsf_r[i] * LSF_R_FAC * PRED_FAC_MODE_12k2 + lsf_5_mean[i];

    lsf2lsp_for_mode12k2(p, p->lsp[1], lsf_no_r, lsf_quantizer, 0, lsf_param[2] & 1, 0);
    lsf2lsp_for_mode12k2(p, p->lsp[3], lsf_no_r, lsf_quantizer, 2, lsf_param[2] & 1, 1);

    // interpolate LSP vectors at subframes 1 and 3
    weighted_vector_sumd(p->lsp[0], p->prev_lsp_sub4, p->lsp[1], 0.5, 0.5, LP_FILTER_ORDER);
    weighted_vector_sumd(p->lsp[2], p->lsp[1]       , p->lsp[3], 0.5, 0.5, LP_FILTER_ORDER);
}

/**
 * Decode a set of 3 split-matrix quantized lsf indexes into an lsp vector.
 *
 * @param p                 pointer to the AMRContext
 */
static void lsf2lsp_3(AMRContext *p)
{
    const uint16_t *lsf_param = p->frame.lsf;
    int16_t lsf_r[LP_FILTER_ORDER]; // residual LSF vector
    float lsf_q[LP_FILTER_ORDER]; // quantified LSF vector
    const int16_t *lsf_quantizer;
    int i, j;

    lsf_quantizer = (p->cur_frame_mode == MODE_7k95 ? lsf_3_1_MODE_7k95 : lsf_3_1)[lsf_param[0]];
    memcpy(lsf_r, lsf_quantizer, 3 * sizeof(*lsf_r));

    lsf_quantizer = lsf_3_2[lsf_param[1] << (p->cur_frame_mode <= MODE_5k15)];
    memcpy(lsf_r + 3, lsf_quantizer, 3 * sizeof(*lsf_r));

    lsf_quantizer = (p->cur_frame_mode <= MODE_5k15 ? lsf_3_3_MODE_5k15 : lsf_3_3)[lsf_param[2]];
    memcpy(lsf_r + 6, lsf_quantizer, 4 * sizeof(*lsf_r));

    // calculate mean-removed LSF vector and add mean
    for (i = 0; i < LP_FILTER_ORDER; i++)
        lsf_q[i] = (lsf_r[i] + p->prev_lsf_r[i] * pred_fac[i]) * (LSF_R_FAC / 8000.0) + lsf_3_mean[i] * (1.0 / 8000.0);

    ff_set_min_dist_lsf(lsf_q, MIN_LSF_SPACING, LP_FILTER_ORDER);

    // store data for computing the next frame's LSFs
    interpolate_lsf(&p->acelpv_ctx, p->lsf_q, lsf_q);
    memcpy(p->prev_lsf_r, lsf_r, LP_FILTER_ORDER * sizeof(*lsf_r));

    ff_acelp_lsf2lspd(p->lsp[3], lsf_q, LP_FILTER_ORDER);

    // interpolate LSP vectors at subframes 1, 2 and 3
    for (i = 1; i <= 3; i++)
        for(j = 0; j < LP_FILTER_ORDER; j++)
            p->lsp[i-1][j] = p->prev_lsp_sub4[j] +
                (p->lsp[3][j] - p->prev_lsp_sub4[j]) * 0.25 * i;
}

/// @}


/// @name AMR pitch vector decoding functions
/// @{

/**
 * Like ff_decode_pitch_lag(), but with 1/6 resolution
 */
static void decode_pitch_lag_1_6(int *lag_int, int *lag_frac, int pitch_index,
                                 const int prev_lag_int, const int subframe)
{
    if (subframe == 0 || subframe == 2) {
        if (pitch_index < 463) {
            *lag_int  = (pitch_index + 107) * 10923 >> 16;
            *lag_frac = pitch_index - *lag_int * 6 + 105;
        } else {
            *lag_int  = pitch_index - 368;
            *lag_frac = 0;
        }
    } else {
        *lag_int  = ((pitch_index + 5) * 10923 >> 16) - 1;
        *lag_frac = pitch_index - *lag_int * 6 - 3;
        *lag_int += av_clip(prev_lag_int - 5, PITCH_LAG_MIN_MODE_12k2,
                            PITCH_DELAY_MAX - 9);
    }
}

static void decode_pitch_vector(AMRContext *p,
                                const AMRNBSubframe *amr_subframe,
                                const int subframe)
{
    int pitch_lag_int, pitch_lag_frac;
    enum Mode mode = p->cur_frame_mode;

    if (p->cur_frame_mode == MODE_12k2) {
        decode_pitch_lag_1_6(&pitch_lag_int, &pitch_lag_frac,
                             amr_subframe->p_lag, p->pitch_lag_int,
                             subframe);
    } else
        ff_decode_pitch_lag(&pitch_lag_int, &pitch_lag_frac,
                            amr_subframe->p_lag,
                            p->pitch_lag_int, subframe,
                            mode != MODE_4k75 && mode != MODE_5k15,
                            mode <= MODE_6k7 ? 4 : (mode == MODE_7k95 ? 5 : 6));

    p->pitch_lag_int = pitch_lag_int; // store previous lag in a uint8_t

    pitch_lag_frac <<= (p->cur_frame_mode != MODE_12k2);

    pitch_lag_int += pitch_lag_frac > 0;

    /* Calculate the pitch vector by interpolating the past excitation at the
       pitch lag using a b60 hamming windowed sinc function.   */
    p->acelpf_ctx.acelp_interpolatef(p->excitation,
                          p->excitation + 1 - pitch_lag_int,
                          ff_b60_sinc, 6,
                          pitch_lag_frac + 6 - 6*(pitch_lag_frac > 0),
                          10, AMR_SUBFRAME_SIZE);

    memcpy(p->pitch_vector, p->excitation, AMR_SUBFRAME_SIZE * sizeof(float));
}

/// @}


/// @name AMR algebraic code book (fixed) vector decoding functions
/// @{

/**
 * Decode a 10-bit algebraic codebook index from a 10.2 kbit/s frame.
 */
static void decode_10bit_pulse(int code, int pulse_position[8],
                               int i1, int i2, int i3)
{
    // coded using 7+3 bits with the 3 LSBs being, individually, the LSB of 1 of
    // the 3 pulses and the upper 7 bits being coded in base 5
    const uint8_t *positions = base_five_table[code >> 3];
    pulse_position[i1] = (positions[2] << 1) + ( code       & 1);
    pulse_position[i2] = (positions[1] << 1) + ((code >> 1) & 1);
    pulse_position[i3] = (positions[0] << 1) + ((code >> 2) & 1);
}

/**
 * Decode the algebraic codebook index to pulse positions and signs and
 * construct the algebraic codebook vector for MODE_10k2.
 *
 * @param fixed_index          positions of the eight pulses
 * @param fixed_sparse         pointer to the algebraic codebook vector
 */
static void decode_8_pulses_31bits(const int16_t *fixed_index,
                                   AMRFixed *fixed_sparse)
{
    int pulse_position[8];
    int i, temp;

    decode_10bit_pulse(fixed_index[4], pulse_position, 0, 4, 1);
    decode_10bit_pulse(fixed_index[5], pulse_position, 2, 6, 5);

    // coded using 5+2 bits with the 2 LSBs being, individually, the LSB of 1 of
    // the 2 pulses and the upper 5 bits being coded in base 5
    temp = ((fixed_index[6] >> 2) * 25 + 12) >> 5;
    pulse_position[3] = temp % 5;
    pulse_position[7] = temp / 5;
    if (pulse_position[7] & 1)
        pulse_position[3] = 4 - pulse_position[3];
    pulse_position[3] = (pulse_position[3] << 1) + ( fixed_index[6]       & 1);
    pulse_position[7] = (pulse_position[7] << 1) + ((fixed_index[6] >> 1) & 1);

    fixed_sparse->n = 8;
    for (i = 0; i < 4; i++) {
        const int pos1   = (pulse_position[i]     << 2) + i;
        const int pos2   = (pulse_position[i + 4] << 2) + i;
        const float sign = fixed_index[i] ? -1.0 : 1.0;
        fixed_sparse->x[i    ] = pos1;
        fixed_sparse->x[i + 4] = pos2;
        fixed_sparse->y[i    ] = sign;
        fixed_sparse->y[i + 4] = pos2 < pos1 ? -sign : sign;
    }
}

/**
 * Decode the algebraic codebook index to pulse positions and signs,
 * then construct the algebraic codebook vector.
 *
 *                              nb of pulses | bits encoding pulses
 * For MODE_4k75 or MODE_5k15,             2 | 1-3, 4-6, 7
 *                  MODE_5k9,              2 | 1,   2-4, 5-6, 7-9
 *                  MODE_6k7,              3 | 1-3, 4,   5-7, 8,  9-11
 *      MODE_7k4 or MODE_7k95,             4 | 1-3, 4-6, 7-9, 10, 11-13
 *
 * @param fixed_sparse pointer to the algebraic codebook vector
 * @param pulses       algebraic codebook indexes
 * @param mode         mode of the current frame
 * @param subframe     current subframe number
 */
static void decode_fixed_sparse(AMRFixed *fixed_sparse, const uint16_t *pulses,
                                const enum Mode mode, const int subframe)
{
    av_assert1(MODE_4k75 <= (signed)mode && mode <= MODE_12k2);

    if (mode == MODE_12k2) {
        ff_decode_10_pulses_35bits(pulses, fixed_sparse, gray_decode, 5, 3);
    } else if (mode == MODE_10k2) {
        decode_8_pulses_31bits(pulses, fixed_sparse);
    } else {
        int *pulse_position = fixed_sparse->x;
        int i, pulse_subset;
        const int fixed_index = pulses[0];

        if (mode <= MODE_5k15) {
            pulse_subset      = ((fixed_index >> 3) & 8)     + (subframe << 1);
            pulse_position[0] = ( fixed_index       & 7) * 5 + track_position[pulse_subset];
            pulse_position[1] = ((fixed_index >> 3) & 7) * 5 + track_position[pulse_subset + 1];
            fixed_sparse->n = 2;
        } else if (mode == MODE_5k9) {
            pulse_subset      = ((fixed_index & 1) << 1) + 1;
            pulse_position[0] = ((fixed_index >> 1) & 7) * 5 + pulse_subset;
            pulse_subset      = (fixed_index  >> 4) & 3;
            pulse_position[1] = ((fixed_index >> 6) & 7) * 5 + pulse_subset + (pulse_subset == 3 ? 1 : 0);
            fixed_sparse->n = pulse_position[0] == pulse_position[1] ? 1 : 2;
        } else if (mode == MODE_6k7) {
            pulse_position[0] = (fixed_index        & 7) * 5;
            pulse_subset      = (fixed_index  >> 2) & 2;
            pulse_position[1] = ((fixed_index >> 4) & 7) * 5 + pulse_subset + 1;
            pulse_subset      = (fixed_index  >> 6) & 2;
            pulse_position[2] = ((fixed_index >> 8) & 7) * 5 + pulse_subset + 2;
            fixed_sparse->n = 3;
        } else { // mode <= MODE_7k95
            pulse_position[0] = gray_decode[ fixed_index        & 7];
            pulse_position[1] = gray_decode[(fixed_index >> 3)  & 7] + 1;
            pulse_position[2] = gray_decode[(fixed_index >> 6)  & 7] + 2;
            pulse_subset      = (fixed_index >> 9) & 1;
            pulse_position[3] = gray_decode[(fixed_index >> 10) & 7] + pulse_subset + 3;
            fixed_sparse->n = 4;
        }
        for (i = 0; i < fixed_sparse->n; i++)
            fixed_sparse->y[i] = (pulses[1] >> i) & 1 ? 1.0 : -1.0;
    }
}

/**
 * Apply pitch lag to obtain the sharpened fixed vector (section 6.1.2)
 *
 * @param p the context
 * @param subframe unpacked amr subframe
 * @param mode mode of the current frame
 * @param fixed_sparse sparse respresentation of the fixed vector
 */
static void pitch_sharpening(AMRContext *p, int subframe, enum Mode mode,
                             AMRFixed *fixed_sparse)
{
    // The spec suggests the current pitch gain is always used, but in other
    // modes the pitch and codebook gains are joinly quantized (sec 5.8.2)
    // so the codebook gain cannot depend on the quantized pitch gain.
    if (mode == MODE_12k2)
        p->beta = FFMIN(p->pitch_gain[4], 1.0);

    fixed_sparse->pitch_lag  = p->pitch_lag_int;
    fixed_sparse->pitch_fac  = p->beta;

    // Save pitch sharpening factor for the next subframe
    // MODE_4k75 only updates on the 2nd and 4th subframes - this follows from
    // the fact that the gains for two subframes are jointly quantized.
    if (mode != MODE_4k75 || subframe & 1)
        p->beta = av_clipf(p->pitch_gain[4], 0.0, SHARP_MAX);
}
/// @}


/// @name AMR gain decoding functions
/// @{

/**
 * fixed gain smoothing
 * Note that where the spec specifies the "spectrum in the q domain"
 * in section 6.1.4, in fact frequencies should be used.
 *
 * @param p the context
 * @param lsf LSFs for the current subframe, in the range [0,1]
 * @param lsf_avg averaged LSFs
 * @param mode mode of the current frame
 *
 * @return fixed gain smoothed
 */
static float fixed_gain_smooth(AMRContext *p , const float *lsf,
                               const float *lsf_avg, const enum Mode mode)
{
    float diff = 0.0;
    int i;

    for (i = 0; i < LP_FILTER_ORDER; i++)
        diff += fabs(lsf_avg[i] - lsf[i]) / lsf_avg[i];

    // If diff is large for ten subframes, disable smoothing for a 40-subframe
    // hangover period.
    p->diff_count++;
    if (diff <= 0.65)
        p->diff_count = 0;

    if (p->diff_count > 10) {
        p->hang_count = 0;
        p->diff_count--; // don't let diff_count overflow
    }

    if (p->hang_count < 40) {
        p->hang_count++;
    } else if (mode < MODE_7k4 || mode == MODE_10k2) {
        const float smoothing_factor = av_clipf(4.0 * diff - 1.6, 0.0, 1.0);
        const float fixed_gain_mean = (p->fixed_gain[0] + p->fixed_gain[1] +
                                       p->fixed_gain[2] + p->fixed_gain[3] +
                                       p->fixed_gain[4]) * 0.2;
        return smoothing_factor * p->fixed_gain[4] +
               (1.0 - smoothing_factor) * fixed_gain_mean;
    }
    return p->fixed_gain[4];
}

/**
 * Decode pitch gain and fixed gain factor (part of section 6.1.3).
 *
 * @param p the context
 * @param amr_subframe unpacked amr subframe
 * @param mode mode of the current frame
 * @param subframe current subframe number
 * @param fixed_gain_factor decoded gain correction factor
 */
static void decode_gains(AMRContext *p, const AMRNBSubframe *amr_subframe,
                         const enum Mode mode, const int subframe,
                         float *fixed_gain_factor)
{
    if (mode == MODE_12k2 || mode == MODE_7k95) {
        p->pitch_gain[4]   = qua_gain_pit [amr_subframe->p_gain    ]
            * (1.0 / 16384.0);
        *fixed_gain_factor = qua_gain_code[amr_subframe->fixed_gain]
            * (1.0 /  2048.0);
    } else {
        const uint16_t *gains;

        if (mode >= MODE_6k7) {
            gains = gains_high[amr_subframe->p_gain];
        } else if (mode >= MODE_5k15) {
            gains = gains_low [amr_subframe->p_gain];
        } else {
            // gain index is only coded in subframes 0,2 for MODE_4k75
            gains = gains_MODE_4k75[(p->frame.subframe[subframe & 2].p_gain << 1) + (subframe & 1)];
        }

        p->pitch_gain[4]   = gains[0] * (1.0 / 16384.0);
        *fixed_gain_factor = gains[1] * (1.0 /  4096.0);
    }
}

/// @}


/// @name AMR preprocessing functions
/// @{

/**
 * Circularly convolve a sparse fixed vector with a phase dispersion impulse
 * response filter (D.6.2 of G.729 and 6.1.5 of AMR).
 *
 * @param out vector with filter applied
 * @param in source vector
 * @param filter phase filter coefficients
 *
 *  out[n] = sum(i,0,len-1){ in[i] * filter[(len + n - i)%len] }
 */
static void apply_ir_filter(float *out, const AMRFixed *in,
                            const float *filter)
{
    float filter1[AMR_SUBFRAME_SIZE],     ///< filters at pitch lag*1 and *2
          filter2[AMR_SUBFRAME_SIZE];
    int   lag = in->pitch_lag;
    float fac = in->pitch_fac;
    int i;

    if (lag < AMR_SUBFRAME_SIZE) {
        ff_celp_circ_addf(filter1, filter, filter, lag, fac,
                          AMR_SUBFRAME_SIZE);

        if (lag < AMR_SUBFRAME_SIZE >> 1)
            ff_celp_circ_addf(filter2, filter, filter1, lag, fac,
                              AMR_SUBFRAME_SIZE);
    }

    memset(out, 0, sizeof(float) * AMR_SUBFRAME_SIZE);
    for (i = 0; i < in->n; i++) {
        int   x = in->x[i];
        float y = in->y[i];
        const float *filterp;

        if (x >= AMR_SUBFRAME_SIZE - lag) {
            filterp = filter;
        } else if (x >= AMR_SUBFRAME_SIZE - (lag << 1)) {
            filterp = filter1;
        } else
            filterp = filter2;

        ff_celp_circ_addf(out, out, filterp, x, y, AMR_SUBFRAME_SIZE);
    }
}

/**
 * Reduce fixed vector sparseness by smoothing with one of three IR filters.
 * Also know as "adaptive phase dispersion".
 *
 * This implements 3GPP TS 26.090 section 6.1(5).
 *
 * @param p the context
 * @param fixed_sparse algebraic codebook vector
 * @param fixed_vector unfiltered fixed vector
 * @param fixed_gain smoothed gain
 * @param out space for modified vector if necessary
 */
static const float *anti_sparseness(AMRContext *p, AMRFixed *fixed_sparse,
                                    const float *fixed_vector,
                                    float fixed_gain, float *out)
{
    int ir_filter_nr;

    if (p->pitch_gain[4] < 0.6) {
        ir_filter_nr = 0;      // strong filtering
    } else if (p->pitch_gain[4] < 0.9) {
        ir_filter_nr = 1;      // medium filtering
    } else
        ir_filter_nr = 2;      // no filtering

    // detect 'onset'
    if (fixed_gain > 2.0 * p->prev_sparse_fixed_gain) {
        p->ir_filter_onset = 2;
    } else if (p->ir_filter_onset)
        p->ir_filter_onset--;

    if (!p->ir_filter_onset) {
        int i, count = 0;

        for (i = 0; i < 5; i++)
            if (p->pitch_gain[i] < 0.6)
                count++;
        if (count > 2)
            ir_filter_nr = 0;

        if (ir_filter_nr > p->prev_ir_filter_nr + 1)
            ir_filter_nr--;
    } else if (ir_filter_nr < 2)
        ir_filter_nr++;

    // Disable filtering for very low level of fixed_gain.
    // Note this step is not specified in the technical description but is in
    // the reference source in the function Ph_disp.
    if (fixed_gain < 5.0)
        ir_filter_nr = 2;

    if (p->cur_frame_mode != MODE_7k4 && p->cur_frame_mode < MODE_10k2
         && ir_filter_nr < 2) {
        apply_ir_filter(out, fixed_sparse,
                        (p->cur_frame_mode == MODE_7k95 ?
                             ir_filters_lookup_MODE_7k95 :
                             ir_filters_lookup)[ir_filter_nr]);
        fixed_vector = out;
    }

    // update ir filter strength history
    p->prev_ir_filter_nr       = ir_filter_nr;
    p->prev_sparse_fixed_gain  = fixed_gain;

    return fixed_vector;
}

/// @}


/// @name AMR synthesis functions
/// @{

/**
 * Conduct 10th order linear predictive coding synthesis.
 *
 * @param p             pointer to the AMRContext
 * @param lpc           pointer to the LPC coefficients
 * @param fixed_gain    fixed codebook gain for synthesis
 * @param fixed_vector  algebraic codebook vector
 * @param samples       pointer to the output speech samples
 * @param overflow      16-bit overflow flag
 */
static int synthesis(AMRContext *p, float *lpc,
                     float fixed_gain, const float *fixed_vector,
                     float *samples, uint8_t overflow)
{
    int i;
    float excitation[AMR_SUBFRAME_SIZE];

    // if an overflow has been detected, the pitch vector is scaled down by a
    // factor of 4
    if (overflow)
        for (i = 0; i < AMR_SUBFRAME_SIZE; i++)
            p->pitch_vector[i] *= 0.25;

    p->acelpv_ctx.weighted_vector_sumf(excitation, p->pitch_vector, fixed_vector,
                            p->pitch_gain[4], fixed_gain, AMR_SUBFRAME_SIZE);

    // emphasize pitch vector contribution
    if (p->pitch_gain[4] > 0.5 && !overflow) {
        float energy = p->celpm_ctx.dot_productf(excitation, excitation,
                                                    AMR_SUBFRAME_SIZE);
        float pitch_factor =
            p->pitch_gain[4] *
            (p->cur_frame_mode == MODE_12k2 ?
                0.25 * FFMIN(p->pitch_gain[4], 1.0) :
                0.5  * FFMIN(p->pitch_gain[4], SHARP_MAX));

        for (i = 0; i < AMR_SUBFRAME_SIZE; i++)
            excitation[i] += pitch_factor * p->pitch_vector[i];

        ff_scale_vector_to_given_sum_of_squares(excitation, excitation, energy,
                                                AMR_SUBFRAME_SIZE);
    }

    p->celpf_ctx.celp_lp_synthesis_filterf(samples, lpc, excitation,
                                 AMR_SUBFRAME_SIZE,
                                 LP_FILTER_ORDER);

    // detect overflow
    for (i = 0; i < AMR_SUBFRAME_SIZE; i++)
        if (fabsf(samples[i]) > AMR_SAMPLE_BOUND) {
            return 1;
        }

    return 0;
}

/// @}


/// @name AMR update functions
/// @{

/**
 * Update buffers and history at the end of decoding a subframe.
 *
 * @param p             pointer to the AMRContext
 */
static void update_state(AMRContext *p)
{
    memcpy(p->prev_lsp_sub4, p->lsp[3], LP_FILTER_ORDER * sizeof(p->lsp[3][0]));

    memmove(&p->excitation_buf[0], &p->excitation_buf[AMR_SUBFRAME_SIZE],
            (PITCH_DELAY_MAX + LP_FILTER_ORDER + 1) * sizeof(float));

    memmove(&p->pitch_gain[0], &p->pitch_gain[1], 4 * sizeof(float));
    memmove(&p->fixed_gain[0], &p->fixed_gain[1], 4 * sizeof(float));

    memmove(&p->samples_in[0], &p->samples_in[AMR_SUBFRAME_SIZE],
            LP_FILTER_ORDER * sizeof(float));
}

/// @}


/// @name AMR Postprocessing functions
/// @{

/**
 * Get the tilt factor of a formant filter from its transfer function
 *
 * @param p     The Context
 * @param lpc_n LP_FILTER_ORDER coefficients of the numerator
 * @param lpc_d LP_FILTER_ORDER coefficients of the denominator
 */
static float tilt_factor(AMRContext *p, float *lpc_n, float *lpc_d)
{
    float rh0, rh1; // autocorrelation at lag 0 and 1

    // LP_FILTER_ORDER prior zeros are needed for ff_celp_lp_synthesis_filterf
    float impulse_buffer[LP_FILTER_ORDER + AMR_TILT_RESPONSE] = { 0 };
    float *hf = impulse_buffer + LP_FILTER_ORDER; // start of impulse response

    hf[0] = 1.0;
    memcpy(hf + 1, lpc_n, sizeof(float) * LP_FILTER_ORDER);
    p->celpf_ctx.celp_lp_synthesis_filterf(hf, lpc_d, hf,
                                 AMR_TILT_RESPONSE,
                                 LP_FILTER_ORDER);

    rh0 = p->celpm_ctx.dot_productf(hf, hf,     AMR_TILT_RESPONSE);
    rh1 = p->celpm_ctx.dot_productf(hf, hf + 1, AMR_TILT_RESPONSE - 1);

    // The spec only specifies this check for 12.2 and 10.2 kbit/s
    // modes. But in the ref source the tilt is always non-negative.
    return rh1 >= 0.0 ? rh1 / rh0 * AMR_TILT_GAMMA_T : 0.0;
}

/**
 * Perform adaptive post-filtering to enhance the quality of the speech.
 * See section 6.2.1.
 *
 * @param p             pointer to the AMRContext
 * @param lpc           interpolated LP coefficients for this subframe
 * @param buf_out       output of the filter
 */
static void postfilter(AMRContext *p, float *lpc, float *buf_out)
{
    int i;
    float *samples          = p->samples_in + LP_FILTER_ORDER; // Start of input

    float speech_gain       = p->celpm_ctx.dot_productf(samples, samples,
                                                           AMR_SUBFRAME_SIZE);

    float pole_out[AMR_SUBFRAME_SIZE + LP_FILTER_ORDER];  // Output of pole filter
    const float *gamma_n, *gamma_d;                       // Formant filter factor table
    float lpc_n[LP_FILTER_ORDER], lpc_d[LP_FILTER_ORDER]; // Transfer function coefficients

    if (p->cur_frame_mode == MODE_12k2 || p->cur_frame_mode == MODE_10k2) {
        gamma_n = ff_pow_0_7;
        gamma_d = ff_pow_0_75;
    } else {
        gamma_n = ff_pow_0_55;
        gamma_d = ff_pow_0_7;
    }

    for (i = 0; i < LP_FILTER_ORDER; i++) {
         lpc_n[i] = lpc[i] * gamma_n[i];
         lpc_d[i] = lpc[i] * gamma_d[i];
    }

    memcpy(pole_out, p->postfilter_mem, sizeof(float) * LP_FILTER_ORDER);
    p->celpf_ctx.celp_lp_synthesis_filterf(pole_out + LP_FILTER_ORDER, lpc_d, samples,
                                 AMR_SUBFRAME_SIZE, LP_FILTER_ORDER);
    memcpy(p->postfilter_mem, pole_out + AMR_SUBFRAME_SIZE,
           sizeof(float) * LP_FILTER_ORDER);

    p->celpf_ctx.celp_lp_zero_synthesis_filterf(buf_out, lpc_n,
                                      pole_out + LP_FILTER_ORDER,
                                      AMR_SUBFRAME_SIZE, LP_FILTER_ORDER);

    ff_tilt_compensation(&p->tilt_mem, tilt_factor(p, lpc_n, lpc_d), buf_out,
                         AMR_SUBFRAME_SIZE);

    ff_adaptive_gain_control(buf_out, buf_out, speech_gain, AMR_SUBFRAME_SIZE,
                             AMR_AGC_ALPHA, &p->postfilter_agc);
}

/// @}

static int amrnb_decode_frame(AVCodecContext *avctx, void *data,
                              int *got_frame_ptr, AVPacket *avpkt)
{

    AMRContext *p = avctx->priv_data;        // pointer to private data
    AVFrame *frame     = data;
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    float *buf_out;                          // pointer to the output data buffer
    int i, subframe, ret;
    float fixed_gain_factor;
    AMRFixed fixed_sparse = {0};             // fixed vector up to anti-sparseness processing
    float spare_vector[AMR_SUBFRAME_SIZE];   // extra stack space to hold result from anti-sparseness processing
    float synth_fixed_gain;                  // the fixed gain that synthesis should use
    const float *synth_fixed_vector;         // pointer to the fixed vector that synthesis should use

    /* get output buffer */
    frame->nb_samples = AMR_BLOCK_SIZE;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;
    buf_out = (float *)frame->data[0];

    p->cur_frame_mode = unpack_bitstream(p, buf, buf_size);
    if (p->cur_frame_mode == NO_DATA) {
        av_log(avctx, AV_LOG_ERROR, "Corrupt bitstream\n");
        return AVERROR_INVALIDDATA;
    }
    if (p->cur_frame_mode == MODE_DTX) {
        avpriv_report_missing_feature(avctx, "dtx mode");
        av_log(avctx, AV_LOG_INFO, "Note: libopencore_amrnb supports dtx\n");
        return AVERROR_PATCHWELCOME;
    }

    if (p->cur_frame_mode == MODE_12k2) {
        lsf2lsp_5(p);
    } else
        lsf2lsp_3(p);

    for (i = 0; i < 4; i++)
        ff_acelp_lspd2lpc(p->lsp[i], p->lpc[i], 5);

    for (subframe = 0; subframe < 4; subframe++) {
        const AMRNBSubframe *amr_subframe = &p->frame.subframe[subframe];

        decode_pitch_vector(p, amr_subframe, subframe);

        decode_fixed_sparse(&fixed_sparse, amr_subframe->pulses,
                            p->cur_frame_mode, subframe);

        // The fixed gain (section 6.1.3) depends on the fixed vector
        // (section 6.1.2), but the fixed vector calculation uses
        // pitch sharpening based on the on the pitch gain (section 6.1.3).
        // So the correct order is: pitch gain, pitch sharpening, fixed gain.
        decode_gains(p, amr_subframe, p->cur_frame_mode, subframe,
                     &fixed_gain_factor);

        pitch_sharpening(p, subframe, p->cur_frame_mode, &fixed_sparse);

        if (fixed_sparse.pitch_lag == 0) {
            av_log(avctx, AV_LOG_ERROR, "The file is corrupted, pitch_lag = 0 is not allowed\n");
            return AVERROR_INVALIDDATA;
        }
        ff_set_fixed_vector(p->fixed_vector, &fixed_sparse, 1.0,
                            AMR_SUBFRAME_SIZE);

        p->fixed_gain[4] =
            ff_amr_set_fixed_gain(fixed_gain_factor,
                       p->celpm_ctx.dot_productf(p->fixed_vector,
                                                               p->fixed_vector,
                                                               AMR_SUBFRAME_SIZE) /
                                  AMR_SUBFRAME_SIZE,
                       p->prediction_error,
                       energy_mean[p->cur_frame_mode], energy_pred_fac);

        // The excitation feedback is calculated without any processing such
        // as fixed gain smoothing. This isn't mentioned in the specification.
        for (i = 0; i < AMR_SUBFRAME_SIZE; i++)
            p->excitation[i] *= p->pitch_gain[4];
        ff_set_fixed_vector(p->excitation, &fixed_sparse, p->fixed_gain[4],
                            AMR_SUBFRAME_SIZE);

        // In the ref decoder, excitation is stored with no fractional bits.
        // This step prevents buzz in silent periods. The ref encoder can
        // emit long sequences with pitch factor greater than one. This
        // creates unwanted feedback if the excitation vector is nonzero.
        // (e.g. test sequence T19_795.COD in 3GPP TS 26.074)
        for (i = 0; i < AMR_SUBFRAME_SIZE; i++)
            p->excitation[i] = truncf(p->excitation[i]);

        // Smooth fixed gain.
        // The specification is ambiguous, but in the reference source, the
        // smoothed value is NOT fed back into later fixed gain smoothing.
        synth_fixed_gain = fixed_gain_smooth(p, p->lsf_q[subframe],
                                             p->lsf_avg, p->cur_frame_mode);

        synth_fixed_vector = anti_sparseness(p, &fixed_sparse, p->fixed_vector,
                                             synth_fixed_gain, spare_vector);

        if (synthesis(p, p->lpc[subframe], synth_fixed_gain,
                      synth_fixed_vector, &p->samples_in[LP_FILTER_ORDER], 0))
            // overflow detected -> rerun synthesis scaling pitch vector down
            // by a factor of 4, skipping pitch vector contribution emphasis
            // and adaptive gain control
            synthesis(p, p->lpc[subframe], synth_fixed_gain,
                      synth_fixed_vector, &p->samples_in[LP_FILTER_ORDER], 1);

        postfilter(p, p->lpc[subframe], buf_out + subframe * AMR_SUBFRAME_SIZE);

        // update buffers and history
        ff_clear_fixed_vector(p->fixed_vector, &fixed_sparse, AMR_SUBFRAME_SIZE);
        update_state(p);
    }

    p->acelpf_ctx.acelp_apply_order_2_transfer_function(buf_out,
                                             buf_out, highpass_zeros,
                                             highpass_poles,
                                             highpass_gain * AMR_SAMPLE_SCALE,
                                             p->high_pass_mem, AMR_BLOCK_SIZE);

    /* Update averaged lsf vector (used for fixed gain smoothing).
     *
     * Note that lsf_avg should not incorporate the current frame's LSFs
     * for fixed_gain_smooth.
     * The specification has an incorrect formula: the reference decoder uses
     * qbar(n-1) rather than qbar(n) in section 6.1(4) equation 71. */
    p->acelpv_ctx.weighted_vector_sumf(p->lsf_avg, p->lsf_avg, p->lsf_q[3],
                            0.84, 0.16, LP_FILTER_ORDER);

    *got_frame_ptr = 1;

    /* return the amount of bytes consumed if everything was OK */
    return frame_sizes_nb[p->cur_frame_mode] + 1; // +7 for rounding and +8 for TOC
}


AVCodec ff_amrnb_decoder = {
    .name           = "amrnb",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_AMR_NB,
    .priv_data_size = sizeof(AMRContext),
    .init           = amrnb_decode_init,
    .decode         = amrnb_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("AMR-NB (Adaptive Multi-Rate NarrowBand)"),
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_FLT,
                                                     AV_SAMPLE_FMT_NONE },
};
