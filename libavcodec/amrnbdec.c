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
#include "avcodec.h"
#include "libavutil/common.h"
#include "libavutil/avassert.h"
#include "celp_math.h"
#include "celp_filters.h"
#include "acelp_filters.h"
#include "acelp_vectors.h"
#include "acelp_pitch_delay.h"
#include "get_bits.h"
#include "lsp.h"
#include "amr.h"
#include "codec_internal.h"
#include "decode.h"

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

/**
 * Weights of the past LSFs and of their mean when concealing a lost frame,
 * ALPHA and ONE_ALPHA of 3GPP TS 26.073. The 12.2 kbit/s mode uses a
 * different pair than all the other modes do:
 *
 *   D_plsf_3()  d_plsf_3.c:51-52  29491 /  3277 (0.90 / 0.10)
 *   D_plsf_5()  d_plsf_5.c:49-50  31128 /  1639 (0.95 / 0.05)
 */
#define LSF_ALPHA        (29491.0f / 32768.0f)
#define LSF_ONE_ALPHA    ( 3277.0f / 32768.0f)
#define LSF_ALPHA_5      (31128.0f / 32768.0f)
#define LSF_ONE_ALPHA_5  ( 1639.0f / 32768.0f)

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

/**
 * Scaling of the comfort noise excitation level, the pulses of the fixed
 * codebook of the reference have an amplitude of 4096 in a Q15 domain
 */
#define CN_LEVEL_SCALE      (1.0f / 8.0f)

/** Received frame types used by the comfort noise (DTX) state machine */
enum RXFrameType {
    RX_SPEECH_GOOD = 0,
    RX_SPEECH_DEGRADED,
    RX_ONSET,
    RX_SPEECH_BAD,
    RX_SID_FIRST,
    RX_SID_UPDATE,
    RX_SID_BAD,
    RX_NO_DATA
};

/** States of the comfort noise (DTX) state machine */
enum DTXState {
    DTX_SPEECH = 0,
    DTX,
    DTX_MUTE
};

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

    /* Comfort noise (DTX) state */
    int         dtx_state;                            ///< current DTX state (SPEECH, DTX or DTX_MUTE)
    int         since_last_sid;                       ///< frames since the last SID update
    float       sid_period_inv;                       ///< inverse of the SID update period
    float       cn_log_en;                            ///< comfort noise log2 energy of the last SID
    float       cn_log_en_old;                        ///< comfort noise log2 energy before the last SID
    float       cn_lsp[LP_FILTER_ORDER];              ///< comfort noise LSP vector of the last SID
    float       cn_lsp_old[LP_FILTER_ORDER];          ///< comfort noise LSP vector before the last SID
    uint32_t    cn_seed;                              ///< comfort noise generator shift register
    float       lsf_hist[DTX_HIST_SIZE][LP_FILTER_ORDER];      ///< LSF history used when a SID frame starts DTX
    float       lsf_hist_mean[DTX_HIST_SIZE][LP_FILTER_ORDER]; ///< deviation from the mean of the LSF history
    float       log_en_hist[DTX_HIST_SIZE];           ///< energy history used when a SID frame starts DTX
    int         lsf_hist_ptr;                         ///< next position to write in the LSF history
    int         log_en_hist_ptr;                      ///< next position to write in the energy history
    float       log_en_adjust;                        ///< speech mode dependent level adjustment
    float       log_pg_mean;                          ///< smoothed logarithm of the prediction gain
    int         hangover_count;                       ///< remaining DTX hangover frames
    int         dec_ana_elapsed;                      ///< frames since the last DTX hangover
    int         hangover_added;                       ///< a DTX hangover was assumed for this frame
    int         sid_frame;                            ///< the current frame carries CN parameters
    int         valid_data;                           ///< the CN parameters of this frame may be used
    int         data_updated;                         ///< CN parameters have been renewed at least once
    int         prev_bf;                              ///< the previous frame could not be used

    /* Bad frame handling and error concealment state, 3GPP TS 26.091 */
    int         bfh_state;                            ///< bad frame handling state (0..6)
    uint32_t    no_data_seed;                         ///< pseudo-random source for lost frames
    float       ec_gain_pbuf[5], ec_gain_past_pit, ec_gain_prev_pit;
    float       ec_gain_gbuf[5], ec_gain_past_code, ec_gain_prev_code;
} AMRContext;

typedef struct AMRChannelsContext {
    AMRContext ch[2];
} AMRChannelsContext;

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

/**
 * Reset the bad frame handling and gain concealment states, matching
 * Decoder_amr_reset() and ec_gain_*_reset() in 3GPP TS 26.073.
 */
static void ec_reset(AMRContext *p)
{
    int i;

    p->bfh_state    = 0;
    p->no_data_seed = 21845;
    /* ec_gain_pitch_reset() stores 1640 in Q14. ec_gain_code_reset() stores 1
       in Q1 (0.5). In this floating-point decoder p->excitation[] uses unit
       pulse amplitude and is truncated with truncf(), so 1.0f is used as an
       intentional unit-pulse floor rather than the 0.5 fixed-point scale. */
    for (i = 0; i < 5; i++) {
        p->ec_gain_pbuf[i] = 1640.0f / 16384.0f;
        p->ec_gain_gbuf[i] = 1.0f;
    }
    p->ec_gain_past_pit  = 0.0f;
    p->ec_gain_prev_pit  = 1.0f;
    p->ec_gain_past_code = 0.0f;
    p->ec_gain_prev_code = 1.0f;
}

static av_cold int amrnb_decode_init(AVCodecContext *avctx)
{
    AMRChannelsContext *s = avctx->priv_data;
    int i, j;

    if (avctx->ch_layout.nb_channels > 2) {
        avpriv_report_missing_feature(avctx, ">2 channel AMR");
        return AVERROR_PATCHWELCOME;
    }

    if (!avctx->ch_layout.nb_channels) {
        av_channel_layout_uninit(&avctx->ch_layout);
        avctx->ch_layout      = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
    }
    if (!avctx->sample_rate)
        avctx->sample_rate = 8000;
    avctx->sample_fmt     = AV_SAMPLE_FMT_FLTP;

    for (int ch = 0; ch < avctx->ch_layout.nb_channels; ch++) {
        AMRContext *p = &s->ch[ch];
        // p->excitation always points to the same position in p->excitation_buf
        p->excitation = &p->excitation_buf[PITCH_DELAY_MAX + LP_FILTER_ORDER + 1];

        for (i = 0; i < LP_FILTER_ORDER; i++) {
            p->prev_lsp_sub4[i] =    lsp_sub4_init[i] * 1000 / (float)(1 << 15);
            p->lsf_avg[i] = p->lsf_q[3][i] = lsp_avg_init[i] / (float)(1 << 15);
        }

        for (i = 0; i < 4; i++)
            p->prediction_error[i] = MIN_ENERGY;

        /* Comfort noise state, matches the DTX decoder reset */
        p->dtx_state       = DTX;
        p->sid_period_inv  = 0.25f;
        p->cn_log_en       = 3500.0f / 2048.0f;
        p->cn_log_en_old   = p->cn_log_en;
        p->cn_seed         = CN_PN_SEED_INIT;
        p->hangover_count  = DTX_HANG_CONST;
        p->dec_ana_elapsed = 32767;
        p->prev_bf         = 0;

        ec_reset(p);

        for (i = 0; i < LP_FILTER_ORDER; i++)
            p->cn_lsp[i] = p->cn_lsp_old[i] = cn_lsp_init[i];

        for (i = 0; i < DTX_HIST_SIZE; i++) {
            for (j = 0; j < LP_FILTER_ORDER; j++)
                p->lsf_hist[i][j] = lsf_3_mean[j] * (1.0 / 8000.0);
            memset(p->lsf_hist_mean[i], 0, sizeof(p->lsf_hist_mean[i]));
            p->log_en_hist[i] = p->cn_log_en;
        }

        ff_acelp_filter_init(&p->acelpf_ctx);
        ff_acelp_vectors_init(&p->acelpv_ctx);
        ff_celp_filter_init(&p->celpf_ctx);
        ff_celp_math_init(&p->celpm_ctx);
    }

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
 * @param bfi               the frame is unusable, conceal the LSF vector
 */
static void lsf2lsp_5(AMRContext *p, int bfi)
{
    const uint16_t *lsf_param = p->frame.lsf;
    float lsf_no_r[LP_FILTER_ORDER]; // LSFs without the residual vector
    const int16_t *lsf_quantizer[5];
    float lsf_q[LP_FILTER_ORDER];
    int i;

    if (bfi) {
        /* Use the past LSFs slightly shifted towards their mean, and update
           the prediction residual, see D_plsf_5() in 3GPP TS 26.073
           (d_plsf_5.c:88-115). The LSFs are kept in Hertz while the
           residual prev_lsf_r is in the codebook domain (LSF_R_FAC). */
        for (i = 0; i < LP_FILTER_ORDER; i++)
            lsf_q[i] = (LSF_ALPHA_5 * p->lsf_q[3][i] * 8000.0f +
                        LSF_ONE_ALPHA_5 * lsf_5_mean[i]) * (1.0f / 8000.0f);

        for (i = 0; i < LP_FILTER_ORDER; i++)
            p->prev_lsf_r[i] = av_clip_int16(lrintf(
                (lsf_q[i] * 8000.0f -
                 (lsf_5_mean[i] + PRED_FAC_MODE_12k2 * p->prev_lsf_r[i] *
                  LSF_R_FAC)) / LSF_R_FAC));

        ff_set_min_dist_lsf(lsf_q, MIN_LSF_SPACING, LP_FILTER_ORDER);
        interpolate_lsf(&p->acelpv_ctx, p->lsf_q, lsf_q);
        ff_acelp_lsf2lspd(p->lsp[3], lsf_q, LP_FILTER_ORDER);

        /* the first and second LSP vectors are concealed identically */
        memcpy(p->lsp[1], p->lsp[3], sizeof(p->lsp[1]));
        weighted_vector_sumd(p->lsp[0], p->prev_lsp_sub4, p->lsp[1], 0.5, 0.5,
                             LP_FILTER_ORDER);
        weighted_vector_sumd(p->lsp[2], p->lsp[1], p->lsp[3], 0.5, 0.5,
                             LP_FILTER_ORDER);
        return;
    }

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
 * @param bfi               the frame is unusable, conceal the LSF vector
 */
static void lsf2lsp_3(AMRContext *p, int bfi)
{
    const uint16_t *lsf_param = p->frame.lsf;
    float lsf_q[LP_FILTER_ORDER]; // quantified LSF vector
    int i, j;

    if (bfi) {
        /* Use the past LSFs slightly shifted towards their mean, and update
           the prediction residual, see D_plsf_3() in 3GPP TS 26.073
           (d_plsf_3.c:84-115). The LSFs are kept in Hertz while the residual
           prev_lsf_r is in the codebook domain (LSF_R_FAC). */
        for (i = 0; i < LP_FILTER_ORDER; i++)
            lsf_q[i] = (LSF_ALPHA * p->lsf_q[3][i] * 8000.0f +
                        LSF_ONE_ALPHA * lsf_3_mean[i]) * (1.0f / 8000.0f);

        for (i = 0; i < LP_FILTER_ORDER; i++)
            p->prev_lsf_r[i] = av_clip_int16(lrintf(
                (lsf_q[i] * 8000.0f -
                 (lsf_3_mean[i] + pred_fac[i] * p->prev_lsf_r[i] *
                  LSF_R_FAC)) / LSF_R_FAC));

        ff_set_min_dist_lsf(lsf_q, MIN_LSF_SPACING, LP_FILTER_ORDER);
        interpolate_lsf(&p->acelpv_ctx, p->lsf_q, lsf_q);
        ff_acelp_lsf2lspd(p->lsp[3], lsf_q, LP_FILTER_ORDER);

        // interpolate LSP vectors at subframes 1, 2 and 3
        for (i = 1; i <= 3; i++)
            for (j = 0; j < LP_FILTER_ORDER; j++)
                p->lsp[i-1][j] = p->prev_lsp_sub4[j] +
                    (p->lsp[3][j] - p->prev_lsp_sub4[j]) * 0.25 * i;
        return;
    }

    int16_t lsf_r[LP_FILTER_ORDER]; // residual LSF vector
    const int16_t *lsf_quantizer;

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
                                const int subframe, const int bfi)
{
    int pitch_lag_int, pitch_lag_frac;
    enum Mode mode = p->cur_frame_mode;

    if (bfi) {
        /* Graceful pitch degradation for a lost frame, see Decoder_amr() in
           3GPP TS 26.073 (dec_amr.c:527-548 for the delta-coded modes and
           dec_amr.c:559-568 for 12.2kbit/s). The integer lag keeps growing
           until it reaches its maximum instead of being decoded. */
        if (mode != MODE_12k2)
            pitch_lag_int = FFMIN(p->pitch_lag_int + 1, PITCH_DELAY_MAX);
        else
            pitch_lag_int = p->pitch_lag_int;
        pitch_lag_frac = 0;
    } else if (p->cur_frame_mode == MODE_12k2) {
        decode_pitch_lag_1_6(&pitch_lag_int, &pitch_lag_frac,
                             amr_subframe->p_lag, p->pitch_lag_int,
                             subframe);
    } else {
        ff_decode_pitch_lag(&pitch_lag_int, &pitch_lag_frac,
                            amr_subframe->p_lag,
                            p->pitch_lag_int, subframe,
                            mode != MODE_4k75 && mode != MODE_5k15,
                            mode <= MODE_6k7 ? 4 : (mode == MODE_7k95 ? 5 : 6));
        pitch_lag_frac *= 2;
    }

    p->pitch_lag_int = pitch_lag_int; // store previous lag in a uint8_t

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
 * @param fixed_sparse sparse representation of the fixed vector
 */
static void pitch_sharpening(AMRContext *p, int subframe, enum Mode mode,
                             AMRFixed *fixed_sparse)
{
    // The spec suggests the current pitch gain is always used, but in other
    // modes the pitch and codebook gains are jointly quantized (sec 5.8.2)
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


/// @name AMR error concealment functions
/// @{

/**
 * Median of the last five gain values, equivalent to gmed_n() in
 * 3GPP TS 26.073 (gmed_n.c:57-84).
 */
static float ec_gain_median(const float *buf)
{
    float tmp[5];
    int i, j;

    memcpy(tmp, buf, sizeof(tmp));

    for (i = 1; i < 5; i++) {
        float v = tmp[i];

        for (j = i - 1; j >= 0 && tmp[j] > v; j--)
            tmp[j + 1] = tmp[j];
        tmp[j + 1] = v;
    }

    return tmp[2];
}

/**
 * Conceal the pitch gain of a lost subframe, ec_gain_pitch() in
 * 3GPP TS 26.073 (ec_gains.c:306-330).
 *
 * @param p     the context
 * @param state state of the bad frame handling state machine
 */
static void ec_gain_pitch(AMRContext *p, int state)
{
    /* pdown[] from 3GPP TS 26.073, kept as exact fractions of 32768 */
    static const float pdown[7] = {
        32767.0f / 32768.0f, 32112.0f / 32768.0f, 32112.0f / 32768.0f,
        26214.0f / 32768.0f,  9830.0f / 32768.0f,  6553.0f / 32768.0f,
         6553.0f / 32768.0f,
    };
    float gain = ec_gain_median(p->ec_gain_pbuf);

    if (gain > p->ec_gain_past_pit)
        gain = p->ec_gain_past_pit;
    p->pitch_gain[4] = gain * pdown[state];
}

/**
 * Update the pitch gain concealment state, ec_gain_pitch_update() in
 * 3GPP TS 26.073 (ec_gains.c:343-379).
 *
 * @param p     the context
 * @param bfi   the current frame is unusable
 */
static void ec_gain_pitch_update(AMRContext *p, int bfi)
{
    int i;

    if (!bfi) {
        /* Limit the gain of the first usable subframe after an unusable
           frame to the last usable gain (3GPP TS 26.091 clause 7.2.2). */
        if (p->prev_bf && p->pitch_gain[4] > p->ec_gain_prev_pit)
            p->pitch_gain[4] = p->ec_gain_prev_pit;
        p->ec_gain_prev_pit = p->pitch_gain[4];
    }

    p->ec_gain_past_pit = FFMIN(p->pitch_gain[4], 1.0f);

    for (i = 1; i < 5; i++)
        p->ec_gain_pbuf[i - 1] = p->ec_gain_pbuf[i];
    p->ec_gain_pbuf[4] = p->ec_gain_past_pit;
}

/**
 * Advance the gain predictor with the average of the four past energies,
 * gc_pred_average_limited() followed by gc_pred_update(), as ec_gain_code()
 * does in 3GPP TS 26.073 when the energies of the frame are not available.
 *
 * @param p the context
 */
static void ec_gain_code_predictor_update(AMRContext *p)
{
    float avg = 0.0f;
    int i;

    for (i = 0; i < 4; i++)
        avg += p->prediction_error[i];
    avg = FFMAX(avg * 0.25f, MIN_ENERGY);

    memmove(&p->prediction_error[0], &p->prediction_error[1],
            3 * sizeof(p->prediction_error[0]));
    p->prediction_error[3] = avg;
}

/**
 * Conceal the codebook gain of a lost subframe, ec_gain_code() in
 * 3GPP TS 26.073 (ec_gains.c:137-171).
 *
 * @param p     the context
 * @param state state of the bad frame handling state machine
 */
static void ec_gain_code(AMRContext *p, int state)
{
    /* cdown[] from 3GPP TS 26.073, kept as exact fractions of 32768 */
    static const float cdown[7] = {
        32767.0f / 32768.0f, 32112.0f / 32768.0f, 32112.0f / 32768.0f,
        32112.0f / 32768.0f, 32112.0f / 32768.0f, 32112.0f / 32768.0f,
        22937.0f / 32768.0f,
    };
    float gain = ec_gain_median(p->ec_gain_gbuf);

    if (gain > p->ec_gain_past_code)
        gain = p->ec_gain_past_code;
    p->fixed_gain[4] = gain * cdown[state];
}

/**
 * Update the codebook gain concealment state, ec_gain_code_update() in
 * 3GPP TS 26.073 (ec_gains.c:184-219).
 *
 * @param p     the context
 * @param bfi   the current frame is unusable
 */
static void ec_gain_code_update(AMRContext *p, int bfi)
{
    int i;

    if (!bfi) {
        if (p->prev_bf && p->fixed_gain[4] > p->ec_gain_prev_code)
            p->fixed_gain[4] = p->ec_gain_prev_code;
        p->ec_gain_prev_code = p->fixed_gain[4];
    }

    p->ec_gain_past_code = p->fixed_gain[4];

    for (i = 1; i < 5; i++)
        p->ec_gain_gbuf[i - 1] = p->ec_gain_gbuf[i];
    p->ec_gain_gbuf[4] = p->fixed_gain[4];
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

/**
 * Comfort noise (DTX) generation, see 3GPP TS 26.092.
 *
 * As for AMR-WB the implementation follows the decoder of the reference
 * codec (3GPP TS 26.073) with the fixed point arithmetic replaced by
 * floating point: LSF vectors are kept in the same unit as in the speech
 * path (normalized frequency) and energies as log2 values.
 */


/**
 * Draw a value from the pseudo noise generator, see 3GPP TS 26.073.
 *
 * @param[in,out] shift_reg        State of the shift register
 * @param[in]     no_bits          Number of bits to draw
 */
static int pseudonoise(uint32_t *shift_reg, int no_bits)
{
    int noise_bits = 0, i;

    for (i = 0; i < no_bits; i++) {
        int Sn = (*shift_reg & 1) ^ ((*shift_reg >> 28) & 1);

        noise_bits = (noise_bits << 1) | (*shift_reg & 1);
        *shift_reg >>= 1;
        if (Sn)
            *shift_reg |= 0x40000000u;
    }

    return noise_bits;
}

/**
 * Build the comfort noise excitation, a vector of sparse pulses.
 *
 * @param[in,out] seed             State of the pseudo noise generator
 * @param[out]    cod              Excitation for one subframe
 */
static void build_cn_code(uint32_t *seed, float *cod)
{
    int i, k;

    memset(cod, 0, AMR_SUBFRAME_SIZE * sizeof(*cod));

    for (k = 0; k < CN_NB_PULSE; k++) {
        i = pseudonoise(seed, 2) * 10 + k;
        cod[i] = pseudonoise(seed, 1) > 0 ? 1.0f : -1.0f;
    }
}

/**
 * Decode the comfort noise parameters of a SID frame.
 *
 * The payload carries 39 bits: five parameters (3, 8, 9, 9 and 6 bits),
 * a flag telling whether this is the first SID frame of a silent period
 * and the speech mode used before the silence.
 *
 * @param[out]    ind              The five SID parameters
 * @param[out]    mode             Speech mode used before the silence
 *
 * @return non zero if this is a SID update frame
 */
static int decode_sid_frame(const uint8_t *buf, int *ind, int *mode)
{
    GetBitContext gb;
    int sid_type;

    init_get_bits8(&gb, buf, 5);

    ind[0] = get_bits(&gb, 3);
    ind[1] = get_bits(&gb, 8);
    ind[2] = get_bits(&gb, 9);
    ind[3] = get_bits(&gb, 9);
    ind[4] = get_bits(&gb, 6);
    sid_type = get_bits1(&gb);

    /* the three bits of the speech mode indication are interleaved */
    *mode = get_bits(&gb, 3);
    *mode = ((*mode & 4) >> 2) | (*mode & 2) | ((*mode & 1) << 2);

    return sid_type;
}

/**
 * Decode the LSF vector of a SID frame, the comfort noise quantizer does
 * not use the prediction of the speech modes.
 *
 * @param[in]     ind              The five SID parameters
 * @param[out]    lsf_q            Decoded LSF vector
 */
static void decode_lsf_noise(const int *ind, float *lsf_q)
{
    int i;

    for (i = 0; i < 3; i++)
        lsf_q[i] = lsf_3_1[ind[1]][i] * (LSF_R_FAC / 8000.0);

    for (i = 0; i < 3; i++)
        lsf_q[i + 3] = lsf_3_2[ind[2]][i] * (LSF_R_FAC / 8000.0);

    for (i = 0; i < 4; i++)
        lsf_q[i + 6] = lsf_3_3[ind[3]][i] * (LSF_R_FAC / 8000.0);

    /* the first parameter selects the initial prediction residual */
    for (i = 0; i < LP_FILTER_ORDER; i++) {
        lsf_q[i] += past_rq_init[ind[0]][i] * (LSF_R_FAC / 8000.0);
        lsf_q[i] += lsf_3_mean[i] * (1.0 / 8000.0);
    }

    ff_set_min_dist_lsf(lsf_q, MIN_LSF_SPACING, LP_FILTER_ORDER);
}

/**
 * Store the LSF vector and the energy of a speech frame so that the first
 * SID frame of a silent period can reuse them.
 *
 * @param[in,out] p                The Context
 * @param[in]     synth            Synthesis of the decoded frame
 */
static void dtx_activity_update(AMRContext *p, const float *synth)
{
    float energy = 0.0f;
    int i;

    p->lsf_hist_ptr = (p->lsf_hist_ptr + 1) % DTX_HIST_SIZE;
    memcpy(p->lsf_hist[p->lsf_hist_ptr], p->lsf_q[3], sizeof(p->lsf_q[3]));

    p->log_en_hist_ptr = (p->log_en_hist_ptr + 1) % DTX_HIST_SIZE;

    for (i = 0; i < AMR_BLOCK_SIZE; i++)
        energy += synth[i] * synth[i];

    /* The reference accumulates the energy with L_mac(), which shifts every
       product left by one, so after dividing by 320 what is left is the
       logarithm of the mean square of the frame. The history keeps one more
       fractional bit than the energy carried by a SID frame. */
    p->log_en_hist[p->log_en_hist_ptr] =
        0.5f * log2f(FFMAX(energy, 1.0f) / AMR_BLOCK_SIZE);
}

/**
 * Compute the reflection coefficients of an LP filter with the backward
 * Levinson recursion, they give the prediction gain of the filter.
 *
 * @param[in]     lpc              LP coefficients of the filter
 * @param[out]    refl             Reflection coefficients
 */
static void lpc2refl(const float *lpc, float *refl)
{
    float a[LP_FILTER_ORDER + 1], b[LP_FILTER_ORDER + 1];
    int i, m;

    for (i = 1; i <= LP_FILTER_ORDER; i++)
        a[i] = lpc[i - 1];

    for (m = LP_FILTER_ORDER; m >= 1; m--) {
        float k = a[m];

        refl[m - 1] = k;
        for (i = 1; i < m; i++)
            b[i] = (a[i] - k * a[m - i]) / (1.0f - k * k);
        for (i = 1; i < m; i++)
            a[i] = b[i];
    }
}

/**
 * Update the comfort noise state machine for a received frame.
 *
 * @param[in,out] p                The Context
 * @param[in]     frame_type       Type of the received frame
 *
 * @return the DTX state to decode this frame with
 */
static int dtx_handler(AMRContext *p, int frame_type)
{
    int new_state, enc_state;

    if (frame_type == RX_SID_FIRST || frame_type == RX_SID_UPDATE ||
        frame_type == RX_SID_BAD   ||
        ((p->dtx_state == DTX || p->dtx_state == DTX_MUTE) &&
         (frame_type == RX_NO_DATA    || frame_type == RX_SPEECH_BAD ||
          frame_type == RX_ONSET))) {
        new_state = DTX;

        /* stay muted for these input types */
        if (p->dtx_state == DTX_MUTE &&
            (frame_type == RX_SID_BAD   || frame_type == RX_SID_FIRST ||
             frame_type == RX_ONSET     || frame_type == RX_NO_DATA))
            new_state = DTX_MUTE;

        p->since_last_sid = FFMIN(p->since_last_sid + 1, 32767);

        /* SID updates are handled separately to avoid muting a late one */
        if (frame_type != RX_SID_UPDATE &&
            p->since_last_sid > DTX_MAX_EMPTY_THRESH)
            new_state = DTX_MUTE;
    } else {
        new_state = DTX_SPEECH;
        p->since_last_sid = 0;
    }

    if (!p->data_updated && frame_type == RX_SID_UPDATE)
        p->dec_ana_elapsed = 0;

    p->dec_ana_elapsed = FFMIN(p->dec_ana_elapsed + 1, 32767);
    p->hangover_added  = 0;

    if (frame_type == RX_SID_FIRST || frame_type == RX_SID_UPDATE ||
        frame_type == RX_SID_BAD   || frame_type == RX_ONSET   ||
        frame_type == RX_NO_DATA)
        enc_state = DTX;
    else
        enc_state = DTX_SPEECH;

    /* dtx_dec.c: a RX_NO_DATA frame may be a speech packet that was lost on
       the way, so the assumed encoder state is SPEECH in that case. */
    if (frame_type == RX_NO_DATA && new_state == DTX_SPEECH)
        enc_state = DTX_SPEECH;

    if (enc_state == DTX_SPEECH) {
        p->hangover_count = DTX_HANG_CONST;
    } else if (p->dec_ana_elapsed > DTX_ELAPSED_FRAMES_THRESH) {
        p->hangover_added  = 1;
        p->dec_ana_elapsed = 0;
        p->hangover_count  = 0;
    } else if (!p->hangover_count) {
        p->dec_ana_elapsed = 0;
    } else {
        p->hangover_count--;
    }

    if (new_state != DTX_SPEECH) {
        p->sid_frame  = 0;
        p->valid_data = 0;

        if (frame_type == RX_SID_FIRST) {
            p->sid_frame = 1;
        } else if (frame_type == RX_SID_UPDATE) {
            p->sid_frame  = 1;
            p->valid_data = 1;
        } else if (frame_type == RX_SID_BAD) {
            p->sid_frame      = 1;
            p->hangover_added = 0;
        }
    }

    return new_state;
}

/**
 * Compute the logarithm of the prediction gain of an LP filter, it is
 * used to normalize the level of the comfort noise.
 *
 * @param[in]     lpc              LP coefficients of the filter
 *
 * @return log2 of the amplitude gained by filtering, negated
 */
static float pred_gain_log(const float *lpc)
{
    float refl[LP_FILTER_ORDER], pred_err = 1.0f;
    int i;

    lpc2refl(lpc, refl);
    for (i = 0; i < LP_FILTER_ORDER; i++)
        pred_err *= 1.0f - refl[i] * refl[i];

    /* the prediction gain is applied to the amplitude of the excitation,
       so only half of it is subtracted from the logarithm of the level */
    return -0.5f * log2f(FFMAX(pred_err, 1e-6f));
}

/**
 * Reuse the noise parameters of the speech frames preceding a silent
 * period, they are the average of the LSF and energy history.
 *
 * @param[in,out] p                The Context
 * @param[in]     mode             Speech mode used before the silence
 */
static void cn_reuse_speech_history(AMRContext *p, int mode)
{
    float lsf[LP_FILTER_ORDER];
    double lsp[LP_FILTER_ORDER];
    int i, j, ptr;

    ptr = (p->lsf_hist_ptr + 1) % DTX_HIST_SIZE;
    memcpy(p->lsf_hist[ptr], p->lsf_hist[p->lsf_hist_ptr],
           sizeof(p->lsf_hist[0]));
    ptr = (p->log_en_hist_ptr + 1) % DTX_HIST_SIZE;
    p->log_en_hist[ptr] = p->log_en_hist[p->log_en_hist_ptr];

    p->log_en_adjust = dtx_log_en_adjust[mode];

    memset(lsf, 0, sizeof(lsf));
    p->cn_log_en = 0.0f;
    for (i = 0; i < DTX_HIST_SIZE; i++) {
        p->cn_log_en += p->log_en_hist[i];
        for (j = 0; j < LP_FILTER_ORDER; j++)
            lsf[j] += p->lsf_hist[i][j];
    }
    p->cn_log_en /= DTX_HIST_SIZE;
    for (j = 0; j < LP_FILTER_ORDER; j++)
        lsf[j] /= DTX_HIST_SIZE;

    /* make the energy independent of the speech coder mode */
    p->cn_log_en -= p->log_en_adjust;

    /* store the deviation from the mean of the LSF history, it is
       used to make the comfort noise spectrum vary like the one of
       the background noise */
    for (j = 0; j < LP_FILTER_ORDER; j++) {
        float mean = 0.0f;

        for (i = 0; i < DTX_HIST_SIZE; i++)
            mean += p->lsf_hist[i][j];
        mean /= DTX_HIST_SIZE;

        for (i = 0; i < DTX_HIST_SIZE; i++) {
            float dev = (p->lsf_hist[i][j] - mean) *
                        lsf_hist_mean_scale[j];
            float sign = dev < 0.0f ? -1.0f : 1.0f;

            dev = fabsf(dev);
            /* soft and hard limits on the deviation */
            if (dev > 655.0f / 32768.0f)
                dev = 655.0f / 32768.0f +
                      (dev - 655.0f / 32768.0f) * 0.25f;
            dev = FFMIN(dev, 1310.0f / 32768.0f);
            p->lsf_hist_mean[i][j] = dev * sign;
        }
    }

    ff_set_min_dist_lsf(lsf, MIN_LSF_SPACING, LP_FILTER_ORDER);
    ff_acelp_lsf2lspd(lsp, lsf, LP_FILTER_ORDER);
    for (i = 0; i < LP_FILTER_ORDER; i++)
        p->cn_lsp[i] = lsp[i];
}

/**
 * Update the comfort noise parameters with the ones carried by a SID
 * frame and reset the state the speech decoder starts from.
 *
 * @param[in,out] p                The Context
 * @param[in]     ind              SID parameters of this frame
 */
static void cn_update_from_sid(AMRContext *p, const int *ind)
{
    float lsf[LP_FILTER_ORDER];
    double lsp[LP_FILTER_ORDER];
    float pred_init;
    int i, period;

    memcpy(p->cn_lsp_old, p->cn_lsp, sizeof(p->cn_lsp));
    p->cn_log_en_old = p->cn_log_en;

    if (p->valid_data) {
        period = p->since_last_sid;
        p->since_last_sid = 0;
        period = FFMIN(period, 32);
        p->sid_period_inv = period >= 2 ? 1.0f / period : 0.5f;

        decode_lsf_noise(ind, lsf);
        ff_acelp_lsf2lspd(lsp, lsf, LP_FILTER_ORDER);
        for (i = 0; i < LP_FILTER_ORDER; i++)
            p->cn_lsp[i] = lsp[i];

        /* index 0 is reserved for silence */
        p->cn_log_en = ind[4] ? (ind[4] / 4.0f) - 2.5f : -16.0f;

        if (!p->data_updated || p->dtx_state == DTX_SPEECH) {
            memcpy(p->cn_lsp_old, p->cn_lsp, sizeof(p->cn_lsp));
            p->cn_log_en_old = p->cn_log_en;
        }
    }

    /* Initialize the gain predictor using this SID frame's energy.

       Note: this decoder's log2-vs-dB gain-predictor modelling (pred_init)
       differs from the reference's fixed-point ma_pred_init (dtx_dec.c:446)
       on the first subframe after DTX; ma_pred_init corresponds to TS 26.092
       clause 6.2's "predictor memories are initialized". */
    pred_init = FFMIN(p->cn_log_en - 9000.0f / 1024.0f, 0.0f);
    pred_init = FFMAX(pred_init, MIN_ENERGY);
    for (i = 0; i < 4; i++)
        p->prediction_error[i] = pred_init;
}

/**
 * Synthesize one frame of comfort noise into the output buffer.
 *
 * @param[in,out] p                The Context
 * @param[in]     lpc_variab       LP coefficients of the dithered filter
 * @param[in]     level            Amplitude of the excitation
 * @param[in]     new_state        DTX state of this frame
 * @param[out]    buf_out          Synthesis of the frame, before filtering
 */
static void cn_synthesize(AMRContext *p, const float *lpc_variab, float level,
                          int new_state, float *buf_out)
{
    float ex[AMR_SUBFRAME_SIZE];
    float synth_mem[LP_FILTER_ORDER + AMR_BLOCK_SIZE];
    int i, sub;

    /* The synthesis filter needs the previous samples in front of the
       output. The reference decoder resets its state with the MRDTX mode,
       which keeps the synthesis memory, so the comfort noise of a silent
       period starts from the memory of the speech frames before it. */
    memcpy(synth_mem, p->samples_in, LP_FILTER_ORDER * sizeof(*synth_mem));

    /* 3GPP TS 26.092 clause 6.2: while comfort noise is generated the
       adaptive codebook gain is zero and the memory of the adaptive codebook
       is set to zero, so that the speech decoder starts from a known state
       when the speech activity begins again. The predictor memory of the LSF
       quantizer is reset the same way. */
    memset(p->excitation_buf, 0, (PITCH_DELAY_MAX + LP_FILTER_ORDER + 1) *
           sizeof(*p->excitation_buf));
    memset(p->prev_lsf_r, 0, sizeof(p->prev_lsf_r));

    /* 3GPP TS 26.073 Decoder_amr_reset() sets the pitch sharpening factor to
       SHARPMIN and the pitch lag to 40 whenever a silent period is entered
       (the reference resets these unconditionally, including for MRDTX). */
    p->beta          = 0.0f;
    p->pitch_lag_int = 40;

    /* The reference decoder resets the bad frame handling and error
       concealment states with every frame of a silent period
       (Decoder_amr_reset() and ec_gain_*_reset() in 3GPP TS 26.073), so the
       gains of the speech frame starting the next speech burst are limited
       accordingly (3GPP TS 26.091 clause 7.2.2). It also marks the frame as
       unusable when the comfort noise had been muted. */
    ec_reset(p);
    p->prev_bf = new_state == DTX_MUTE;

    /* Decoder_amr_reset(MRDTX) clears the fixed-gain history and the
       phase-dispersion state. dtx_dec() then restarts smoothing with
       hangVar already past the 10-subframe threshold and hangCount at 0,
       so resumed speech does not blend in the gains from before the pause. */
    memset(p->fixed_gain, 0, sizeof(p->fixed_gain));
    memset(p->pitch_gain, 0, sizeof(p->pitch_gain));
    p->diff_count              = 10;
    p->hang_count              = 0;
    p->prev_ir_filter_nr       = 0;
    p->ir_filter_onset         = 0;
    p->prev_sparse_fixed_gain  = 0.0f;

    for (sub = 0; sub < 4; sub++) {
        build_cn_code(&p->cn_seed, ex);
        for (i = 0; i < AMR_SUBFRAME_SIZE; i++)
            ex[i] *= level;

        p->celpf_ctx.celp_lp_synthesis_filterf(
            &synth_mem[LP_FILTER_ORDER + sub * AMR_SUBFRAME_SIZE], lpc_variab, ex,
            AMR_SUBFRAME_SIZE, LP_FILTER_ORDER);
    }

    memcpy(buf_out, &synth_mem[LP_FILTER_ORDER], AMR_BLOCK_SIZE * sizeof(*buf_out));

    /* keep the synthesis memory so that the next speech frame starts from it */
    memcpy(p->samples_in, &synth_mem[AMR_BLOCK_SIZE], LP_FILTER_ORDER * sizeof(*p->samples_in));
}

/**
 * Decode one frame of comfort noise into the output buffer.
 *
 * @param[in,out] p                The Context
 * @param[in]     new_state        DTX state of this frame
 * @param[in]     ind              SID parameters of this frame
 * @param[in]     mode             Speech mode used before the silence
 * @param[out]    buf_out          Synthesis of the frame, before filtering
 */
static void cn_frame(AMRContext *p, int new_state, const int *ind, int mode,
                     float *buf_out)
{
    float lsf[LP_FILTER_ORDER], lsf_variab[LP_FILTER_ORDER];
    double lsp[LP_FILTER_ORDER], lsp_variab[LP_FILTER_ORDER];
    float lpc[LP_FILTER_ORDER], lpc_variab[LP_FILTER_ORDER];
    float variab_fac, log_en, int_fac, level, log_pg;
    int i, sub, period, variab_index;

    /* A SID frame starting a silent period reuses the noise parameters
       of the speech frames preceding it */
    if (p->hangover_added && p->sid_frame)
        cn_reuse_speech_history(p, mode);

    /* A SID frame carries new comfort noise parameters */
    if (p->sid_frame)
        cn_update_from_sid(p, ind);

    /* The level adjustment of the speech mode is low pass filtered so that
       it does not jump when the mode changes */
    p->log_en_adjust = 0.9f * p->log_en_adjust +
                       0.1f * dtx_log_en_adjust[mode];

    /* Interpolate the comfort noise parameters since the last update */
    int_fac = FFMIN((1 + p->since_last_sid) * p->sid_period_inv, 1.0f);

    log_en = int_fac * p->cn_log_en + (1.0f - int_fac) * p->cn_log_en_old;
    for (i = 0; i < LP_FILTER_ORDER; i++)
        lsp[i] = int_fac * p->cn_lsp[i] + (1.0f - int_fac) * p->cn_lsp_old[i];

    /* convert to LSF to apply the variability of the background noise */
    for (i = 0; i < LP_FILTER_ORDER; i++)
        lsf[i] = acosf(av_clipd(lsp[i], -1.0, 1.0)) / (2.0f * M_PI);

    variab_fac = 1.0f - (p->log_pg_mean - 0.6f) * 0.3f;
    variab_fac = av_clipf(variab_fac, 0.0f, 1.0f);
    variab_index = pseudonoise(&p->cn_seed, 3);
    variab_index = FFMIN(variab_index, DTX_HIST_SIZE - 1);

    memcpy(lsf_variab, lsf, sizeof(lsf_variab));
    for (i = 0; i < LP_FILTER_ORDER; i++)
        lsf_variab[i] += variab_fac * p->lsf_hist_mean[variab_index][i];

    ff_set_min_dist_lsf(lsf, MIN_LSF_SPACING, LP_FILTER_ORDER);
    ff_set_min_dist_lsf(lsf_variab, MIN_LSF_SPACING, LP_FILTER_ORDER);

    ff_acelp_lsf2lspd(lsp, lsf, LP_FILTER_ORDER);
    ff_acelp_lsf2lspd(lsp_variab, lsf_variab, LP_FILTER_ORDER);

    /* TS 26.073 publishes the comfort noise LSF to the speech decoder on
       every comfort noise frame (dtx_dec.c:548) and converts it there so
       that the first speech frame after the silent period interpolates from
       it (dec_amr.c:329-330). */
    memcpy(p->lsf_q[3], lsf, sizeof(p->lsf_q[3]));
    memcpy(p->prev_lsp_sub4, lsp, sizeof(p->prev_lsp_sub4));
    p->acelpv_ctx.weighted_vector_sumf(p->lsf_avg, p->lsf_avg, lsf,
                                       0.84, 0.16, LP_FILTER_ORDER);

    ff_acelp_lspd2lpc(lsp, lpc, LP_FILTER_ORDER / 2);
    ff_acelp_lspd2lpc(lsp_variab, lpc_variab, LP_FILTER_ORDER / 2);

    /* the filter without variability is used to normalize the level and
       by the postfilter */
    for (sub = 0; sub < 4; sub++)
        memcpy(p->lpc[sub], lpc, sizeof(*lpc) * LP_FILTER_ORDER);

    /* level normalization with the prediction gain of the filter, the
       reference decoder normalizes with the gain of this very frame, its
       average is only used for the variability of the spectrum */
    log_pg = pred_gain_log(lpc);
    p->log_pg_mean = 0.9f * p->log_pg_mean + 0.1f * log_pg;

    level = exp2f(log_en + 4.0f - log_pg + p->log_en_adjust) *
            CN_LEVEL_SCALE;

    cn_synthesize(p, lpc_variab, level, new_state, buf_out);

    if (new_state == DTX_MUTE) {
        /* mute the comfort noise, nothing has been updated for a while */
        period = p->since_last_sid;
        if (period > 32)
            period = 32;
        if (period <= 0)
            period = 8;
        p->sid_period_inv = 1.0f / period;
        p->since_last_sid  = 0;
        /* Snapshot the stored SID target, not this frame's interpolated
           vector, so the next period does not fall back toward the
           previous SID (dtx_dec.c, DTX_MUTE). */
        memcpy(p->cn_lsp_old, p->cn_lsp, sizeof(p->cn_lsp));
        p->cn_log_en_old   = p->cn_log_en;
        p->cn_log_en      -= 0.125f;
    }

    if (p->sid_frame &&
        (p->valid_data || (!p->valid_data && p->hangover_added))) {
        p->since_last_sid = 0;
        p->data_updated   = 1;
    }

    p->dtx_state = new_state;
}

/**
 * Fill the frame parameters of a lost speech frame with pseudo-random
 * values, see build_CN_param() in 3GPP TS 26.073 (b_cn_cod.c:145-162).
 * Only the fields the concealment does not itself overwrite are actually
 * consumed (notably the fixed codebook pulses). The field order follows
 * the AMRNBFrame order table, not the reference's bitstream parameter
 * order, so this is deliberately not bit-exact with the reference -- the
 * decoder is float and not bit-exact either (see the file header).
 *
 * @param p     the context
 * @param seed  state of the pseudo-random generator
 * @param mode  speech mode of the lost frame
 */
static void build_bad_frame_params(AMRContext *p, uint32_t *seed, enum Mode mode)
{
    const uint8_t *ord = amr_unpacking_bitmaps_per_mode[mode];
    const int16_t *win;

    /* reference LCG: seed = (seed * 31821 + 13849) mod 2^16 */
    *seed = (*seed * 31821U + 13849U) & 0xFFFFU;
    win   = &window_200_40[*seed & 0x7F];

    while (*ord) {
        int field_size = *ord++;
        int field      = *ord++;            /* byte offset into AMRNBFrame */

        ((uint16_t *)&p->frame)[field >> 1] =
            *win++ & ((1 << field_size) - 1);

        ord += field_size;                  /* skip the bit-position list */
    }
}

static int amrnb_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                              int *got_frame_ptr, AVPacket *avpkt)
{

    AMRChannelsContext *s = avctx->priv_data;        // pointer to private data
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    int ret;

    /* get output buffer */
    frame->nb_samples = AMR_BLOCK_SIZE;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    for (int ch = 0; ch < avctx->ch_layout.nb_channels; ch++) {
        AMRContext *p = &s->ch[ch];
        float fixed_gain_factor;
        AMRFixed fixed_sparse = {0};             // fixed vector up to anti-sparseness processing
        float spare_vector[AMR_SUBFRAME_SIZE];   // extra stack space to hold result from anti-sparseness processing
        float synth_fixed_gain;                  // the fixed gain that synthesis should use
        const float *synth_fixed_vector;         // pointer to the fixed vector that synthesis should use
        float *buf_out = (float *)frame->extended_data[ch];
        /* Frames carrying no data only consist of the table of contents, so
           a packet may hold no more than one byte per channel. */
        if (buf_size < 1)
            return AVERROR_INVALIDDATA;

        int mode = buf[0] >> 3 & 0x0F;
        int channel_size, rx_type, new_state, bfi;
        int prev_dtx_state = p->dtx_state;       // DTX state before this frame
        int sid_ind[5] = { 0 };
        int sid_mode = 0;
        float synth[AMR_BLOCK_SIZE];             // synthesis before post-filtering
        int i, subframe;

        /* Frames carrying no data only consist of the table of contents, and
           so do the frame types between the SID one and NO_DATA, which are
           reserved: the reference decoder treats them like NO_DATA. */
        if (mode == NO_DATA || (mode > MODE_DTX && mode < NO_DATA))
            channel_size = 1;
        else
            channel_size = frame_sizes_nb[mode] + 1; // +7 for rounding and +8 for TOC

        if (channel_size > buf_size) {
            av_log(avctx, AV_LOG_ERROR, "Frame too small (%d bytes)\n",
                   buf_size);
            return AVERROR_INVALIDDATA;
        }

        p->bad_frame_indicator = (buf[0] & 0x4) != 0x4; // quality bit

        /* Classify the received frame for the comfort noise state machine */
        if (mode == NO_DATA || (mode > MODE_DTX && mode < NO_DATA)) {
            rx_type = RX_NO_DATA;
        } else if (mode == MODE_DTX) {
            int sid_update = decode_sid_frame(buf + 1, sid_ind, &sid_mode);

            /* the comfort noise is scaled with the speech mode of the SID
               frame, it is also needed by the frames carrying no data */
            p->cur_frame_mode = sid_mode;

            if (p->bad_frame_indicator)
                rx_type = RX_SID_BAD;
            else if (sid_update)
                rx_type = RX_SID_UPDATE;
            else
                rx_type = RX_SID_FIRST;
        } else if (p->bad_frame_indicator) {
            rx_type = RX_SPEECH_BAD;
        } else {
            rx_type = RX_SPEECH_GOOD;
        }

        if (rx_type == RX_SPEECH_GOOD || rx_type == RX_SPEECH_BAD) {
            p->cur_frame_mode = unpack_bitstream(p, buf, buf_size);
            if (p->cur_frame_mode == NO_DATA) {
                av_log(avctx, AV_LOG_ERROR, "Corrupt bitstream\n");
                return AVERROR_INVALIDDATA;
            }
        }

        new_state = dtx_handler(p, rx_type);

        if (new_state != DTX_SPEECH) {
            /* Comfort noise is generated for SID frames and, once a silent
               period has started, for the frames carrying no data at all.
               Speech_Decode_Frame() then post-filters that synthesis with
               the same filter as speech. samples_in keeps the unfiltered
               synthesis memory written by cn_synthesize(). */
            cn_frame(p, new_state, sid_ind, p->cur_frame_mode, synth);
            for (subframe = 0; subframe < 4; subframe++) {
                memcpy(p->samples_in + LP_FILTER_ORDER,
                       synth + subframe * AMR_SUBFRAME_SIZE,
                       AMR_SUBFRAME_SIZE * sizeof(float));
                postfilter(p, p->lpc[subframe],
                           buf_out + subframe * AMR_SUBFRAME_SIZE);
            }

            p->acelpf_ctx.acelp_apply_order_2_transfer_function(
                buf_out, buf_out, highpass_zeros, highpass_poles,
                highpass_gain * AMR_SAMPLE_SCALE, p->high_pass_mem,
                AMR_BLOCK_SIZE);

            buf += channel_size;
            buf_size -= channel_size;
            continue;
        }

        /* A speech frame that is either flagged as bad or carries no data
           at all cannot be decoded: its parameters are concealed instead
           (3GPP TS 26.091 clause 7). */
        bfi = rx_type == RX_SPEECH_BAD || rx_type == RX_NO_DATA;

        /* Bad frame handling state machine, 3GPP TS 26.073
           (dec_amr.c:357-398). The state saturates at 6 and, once it has
           reached 6, only decays to 5 on a good frame. */
        if (bfi)
            p->bfh_state = FFMIN(p->bfh_state + 1, 6);
        else
            p->bfh_state = p->bfh_state == 6 ? 5 : 0;

        /* The first speech frame after a comfort noise period is seeded with
           the DTX state of the frame before it: prev_dtx_state was saved
           above, before this frame could overwrite dtx_state. */
        if (prev_dtx_state == DTX) {
            p->bfh_state = 5;
            p->prev_bf   = 0;
        } else if (prev_dtx_state == DTX_MUTE) {
            p->bfh_state = 5;
            p->prev_bf   = 1;
        }

        /* A frame carrying no data has no parameters of its own, fill them
           with pseudo-random values (3GPP TS 26.073 build_CN_param()). */
        if (rx_type == RX_NO_DATA)
            build_bad_frame_params(p, &p->no_data_seed, p->cur_frame_mode);

        p->dtx_state = new_state;

        if (p->cur_frame_mode == MODE_12k2) {
            lsf2lsp_5(p, bfi);
        } else
            lsf2lsp_3(p, bfi);

        for (i = 0; i < 4; i++)
            ff_acelp_lspd2lpc(p->lsp[i], p->lpc[i], 5);

        for (subframe = 0; subframe < 4; subframe++) {
            const AMRNBSubframe *amr_subframe = &p->frame.subframe[subframe];

            decode_pitch_vector(p, amr_subframe, subframe, bfi);

            decode_fixed_sparse(&fixed_sparse, amr_subframe->pulses,
                                p->cur_frame_mode, subframe);

            // The fixed gain (section 6.1.3) depends on the fixed vector
            // (section 6.1.2), but the fixed vector calculation uses
            // pitch sharpening based on the on the pitch gain (section 6.1.3).
            // So the correct order is: pitch gain, pitch sharpening, fixed gain.
            if (bfi)
                ec_gain_pitch(p, p->bfh_state);
            else
                decode_gains(p, amr_subframe, p->cur_frame_mode, subframe,
                             &fixed_gain_factor);

            /* Keep the concealment history and limit the gain of the first
               usable subframe after an unusable one (3GPP TS 26.073
               ec_gain_pitch_update(), 3GPP TS 26.091 clause 7.2.2). */
            ec_gain_pitch_update(p, bfi);

            pitch_sharpening(p, subframe, p->cur_frame_mode, &fixed_sparse);

            if (fixed_sparse.pitch_lag == 0) {
                av_log(avctx, AV_LOG_ERROR, "The file is corrupted, pitch_lag = 0 is not allowed\n");
                return AVERROR_INVALIDDATA;
            }
            ff_set_fixed_vector(p->fixed_vector, &fixed_sparse, 1.0,
                                AMR_SUBFRAME_SIZE);

            if (bfi) {
                ec_gain_code(p, p->bfh_state);
                ec_gain_code_predictor_update(p);
            } else {
                p->fixed_gain[4] =
                    ff_amr_set_fixed_gain(fixed_gain_factor,
                                          p->celpm_ctx.dot_productf(p->fixed_vector,
                                                                    p->fixed_vector,
                                                                    AMR_SUBFRAME_SIZE) /
                                          AMR_SUBFRAME_SIZE,
                                          p->prediction_error,
                                          energy_mean[p->cur_frame_mode], energy_pred_fac);
            }
            ec_gain_code_update(p, bfi);

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

            memcpy(synth + subframe * AMR_SUBFRAME_SIZE,
                   &p->samples_in[LP_FILTER_ORDER],
                   AMR_SUBFRAME_SIZE * sizeof(*synth));

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

        p->prev_bf = bfi;

        /* keep the noise parameters of this speech frame for the next SID */
        dtx_activity_update(p, synth);

        /* Update averaged lsf vector (used for fixed gain smoothing).
         *
         * Note that lsf_avg should not incorporate the current frame's LSFs
         * for fixed_gain_smooth.
         * The specification has an incorrect formula: the reference decoder uses
         * qbar(n-1) rather than qbar(n) in section 6.1(4) equation 71. */
        p->acelpv_ctx.weighted_vector_sumf(p->lsf_avg, p->lsf_avg, p->lsf_q[3],
                                           0.84, 0.16, LP_FILTER_ORDER);
        buf += channel_size;
        buf_size -= channel_size;
    }

    *got_frame_ptr = 1;

    return buf - avpkt->data;
}


const FFCodec ff_amrnb_decoder = {
    .p.name         = "amrnb",
    CODEC_LONG_NAME("AMR-NB (Adaptive Multi-Rate NarrowBand)"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_AMR_NB,
    .priv_data_size = sizeof(AMRChannelsContext),
    .init           = amrnb_decode_init,
    FF_CODEC_DECODE_CB(amrnb_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_CHANNEL_CONF,
};
