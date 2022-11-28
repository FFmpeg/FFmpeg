/*
 * AMR wideband decoder
 * Copyright (c) 2010 Marcelo Galvao Povoa
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
 * MERCHANTABILITY or FITNESS FOR A particular PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * AMR wideband decoder
 */

#include "config.h"

#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/lfg.h"

#include "avcodec.h"
#include "lsp.h"
#include "celp_filters.h"
#include "celp_math.h"
#include "acelp_filters.h"
#include "acelp_vectors.h"
#include "acelp_pitch_delay.h"
#include "codec_internal.h"
#include "decode.h"

#define AMR_USE_16BIT_TABLES
#include "amr.h"

#include "amrwbdata.h"
#if ARCH_MIPS
#include "mips/amrwbdec_mips.h"
#endif /* ARCH_MIPS */

typedef struct AMRWBContext {
    AMRWBFrame                             frame; ///< AMRWB parameters decoded from bitstream
    enum Mode                        fr_cur_mode; ///< mode index of current frame
    uint8_t                           fr_quality; ///< frame quality index (FQI)
    float                      isf_cur[LP_ORDER]; ///< working ISF vector from current frame
    float                   isf_q_past[LP_ORDER]; ///< quantized ISF vector of the previous frame
    float               isf_past_final[LP_ORDER]; ///< final processed ISF vector of the previous frame
    double                      isp[4][LP_ORDER]; ///< ISP vectors from current frame
    double               isp_sub4_past[LP_ORDER]; ///< ISP vector for the 4th subframe of the previous frame

    float                   lp_coef[4][LP_ORDER]; ///< Linear Prediction Coefficients from ISP vector

    uint8_t                       base_pitch_lag; ///< integer part of pitch lag for the next relative subframe
    uint8_t                        pitch_lag_int; ///< integer part of pitch lag of the previous subframe

    float excitation_buf[AMRWB_P_DELAY_MAX + LP_ORDER + 2 + AMRWB_SFR_SIZE]; ///< current excitation and all necessary excitation history
    float                            *excitation; ///< points to current excitation in excitation_buf[]

    float           pitch_vector[AMRWB_SFR_SIZE]; ///< adaptive codebook (pitch) vector for current subframe
    float           fixed_vector[AMRWB_SFR_SIZE]; ///< algebraic codebook (fixed) vector for current subframe

    float                    prediction_error[4]; ///< quantified prediction errors {20log10(^gamma_gc)} for previous four subframes
    float                          pitch_gain[6]; ///< quantified pitch gains for the current and previous five subframes
    float                          fixed_gain[2]; ///< quantified fixed gains for the current and previous subframes

    float                              tilt_coef; ///< {beta_1} related to the voicing of the previous subframe

    float                 prev_sparse_fixed_gain; ///< previous fixed gain; used by anti-sparseness to determine "onset"
    uint8_t                    prev_ir_filter_nr; ///< previous impulse response filter "impNr": 0 - strong, 1 - medium, 2 - none
    float                           prev_tr_gain; ///< previous initial gain used by noise enhancer for threshold

    float samples_az[LP_ORDER + AMRWB_SFR_SIZE];         ///< low-band samples and memory from synthesis at 12.8kHz
    float samples_up[UPS_MEM_SIZE + AMRWB_SFR_SIZE];     ///< low-band samples and memory processed for upsampling
    float samples_hb[LP_ORDER_16k + AMRWB_SFR_SIZE_16k]; ///< high-band samples and memory from synthesis at 16kHz

    float          hpf_31_mem[2], hpf_400_mem[2]; ///< previous values in the high pass filters
    float                           demph_mem[1]; ///< previous value in the de-emphasis filter
    float               bpf_6_7_mem[HB_FIR_SIZE]; ///< previous values in the high-band band pass filter
    float                 lpf_7_mem[HB_FIR_SIZE]; ///< previous values in the high-band low pass filter

    AVLFG                                   prng; ///< random number generator for white noise excitation
    uint8_t                          first_frame; ///< flag active during decoding of the first frame
    ACELPFContext                     acelpf_ctx; ///< context for filters for ACELP-based codecs
    ACELPVContext                     acelpv_ctx; ///< context for vector operations for ACELP-based codecs
    CELPFContext                       celpf_ctx; ///< context for filters for CELP-based codecs
    CELPMContext                       celpm_ctx; ///< context for fixed point math operations

} AMRWBContext;

typedef struct AMRWBChannelsContext {
    AMRWBContext ch[2];
} AMRWBChannelsContext;

static av_cold int amrwb_decode_init(AVCodecContext *avctx)
{
    AMRWBChannelsContext *s = avctx->priv_data;
    int i;

    if (avctx->ch_layout.nb_channels > 2) {
        avpriv_report_missing_feature(avctx, ">2 channel AMR");
        return AVERROR_PATCHWELCOME;
    }

    if (!avctx->ch_layout.nb_channels) {
        av_channel_layout_uninit(&avctx->ch_layout);
        avctx->ch_layout      = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
    }
    if (!avctx->sample_rate)
        avctx->sample_rate = 16000;
    avctx->sample_fmt     = AV_SAMPLE_FMT_FLTP;

    for (int ch = 0; ch < avctx->ch_layout.nb_channels; ch++) {
        AMRWBContext *ctx = &s->ch[ch];

        av_lfg_init(&ctx->prng, 1);

        ctx->excitation  = &ctx->excitation_buf[AMRWB_P_DELAY_MAX + LP_ORDER + 1];
        ctx->first_frame = 1;

        for (i = 0; i < LP_ORDER; i++)
            ctx->isf_past_final[i] = isf_init[i] * (1.0f / (1 << 15));

        for (i = 0; i < 4; i++)
            ctx->prediction_error[i] = MIN_ENERGY;

        ff_acelp_filter_init(&ctx->acelpf_ctx);
        ff_acelp_vectors_init(&ctx->acelpv_ctx);
        ff_celp_filter_init(&ctx->celpf_ctx);
        ff_celp_math_init(&ctx->celpm_ctx);
    }

    return 0;
}

/**
 * Decode the frame header in the "MIME/storage" format. This format
 * is simpler and does not carry the auxiliary frame information.
 *
 * @param[in] ctx                  The Context
 * @param[in] buf                  Pointer to the input buffer
 *
 * @return The decoded header length in bytes
 */
static int decode_mime_header(AMRWBContext *ctx, const uint8_t *buf)
{
    /* Decode frame header (1st octet) */
    ctx->fr_cur_mode  = buf[0] >> 3 & 0x0F;
    ctx->fr_quality   = (buf[0] & 0x4) == 0x4;

    return 1;
}

/**
 * Decode quantized ISF vectors using 36-bit indexes (6K60 mode only).
 *
 * @param[in]  ind                 Array of 5 indexes
 * @param[out] isf_q               Buffer for isf_q[LP_ORDER]
 */
static void decode_isf_indices_36b(uint16_t *ind, float *isf_q)
{
    int i;

    for (i = 0; i < 9; i++)
        isf_q[i]      = dico1_isf[ind[0]][i]      * (1.0f / (1 << 15));

    for (i = 0; i < 7; i++)
        isf_q[i + 9]  = dico2_isf[ind[1]][i]      * (1.0f / (1 << 15));

    for (i = 0; i < 5; i++)
        isf_q[i]     += dico21_isf_36b[ind[2]][i] * (1.0f / (1 << 15));

    for (i = 0; i < 4; i++)
        isf_q[i + 5] += dico22_isf_36b[ind[3]][i] * (1.0f / (1 << 15));

    for (i = 0; i < 7; i++)
        isf_q[i + 9] += dico23_isf_36b[ind[4]][i] * (1.0f / (1 << 15));
}

/**
 * Decode quantized ISF vectors using 46-bit indexes (except 6K60 mode).
 *
 * @param[in]  ind                 Array of 7 indexes
 * @param[out] isf_q               Buffer for isf_q[LP_ORDER]
 */
static void decode_isf_indices_46b(uint16_t *ind, float *isf_q)
{
    int i;

    for (i = 0; i < 9; i++)
        isf_q[i]       = dico1_isf[ind[0]][i]  * (1.0f / (1 << 15));

    for (i = 0; i < 7; i++)
        isf_q[i + 9]   = dico2_isf[ind[1]][i]  * (1.0f / (1 << 15));

    for (i = 0; i < 3; i++)
        isf_q[i]      += dico21_isf[ind[2]][i] * (1.0f / (1 << 15));

    for (i = 0; i < 3; i++)
        isf_q[i + 3]  += dico22_isf[ind[3]][i] * (1.0f / (1 << 15));

    for (i = 0; i < 3; i++)
        isf_q[i + 6]  += dico23_isf[ind[4]][i] * (1.0f / (1 << 15));

    for (i = 0; i < 3; i++)
        isf_q[i + 9]  += dico24_isf[ind[5]][i] * (1.0f / (1 << 15));

    for (i = 0; i < 4; i++)
        isf_q[i + 12] += dico25_isf[ind[6]][i] * (1.0f / (1 << 15));
}

/**
 * Apply mean and past ISF values using the prediction factor.
 * Updates past ISF vector.
 *
 * @param[in,out] isf_q            Current quantized ISF
 * @param[in,out] isf_past         Past quantized ISF
 */
static void isf_add_mean_and_past(float *isf_q, float *isf_past)
{
    int i;
    float tmp;

    for (i = 0; i < LP_ORDER; i++) {
        tmp = isf_q[i];
        isf_q[i] += isf_mean[i] * (1.0f / (1 << 15));
        isf_q[i] += PRED_FACTOR * isf_past[i];
        isf_past[i] = tmp;
    }
}

/**
 * Interpolate the fourth ISP vector from current and past frames
 * to obtain an ISP vector for each subframe.
 *
 * @param[in,out] isp_q            ISPs for each subframe
 * @param[in]     isp4_past        Past ISP for subframe 4
 */
static void interpolate_isp(double isp_q[4][LP_ORDER], const double *isp4_past)
{
    int i, k;

    for (k = 0; k < 3; k++) {
        float c = isfp_inter[k];
        for (i = 0; i < LP_ORDER; i++)
            isp_q[k][i] = (1.0 - c) * isp4_past[i] + c * isp_q[3][i];
    }
}

/**
 * Decode an adaptive codebook index into pitch lag (except 6k60, 8k85 modes).
 * Calculate integer lag and fractional lag always using 1/4 resolution.
 * In 1st and 3rd subframes the index is relative to last subframe integer lag.
 *
 * @param[out]    lag_int          Decoded integer pitch lag
 * @param[out]    lag_frac         Decoded fractional pitch lag
 * @param[in]     pitch_index      Adaptive codebook pitch index
 * @param[in,out] base_lag_int     Base integer lag used in relative subframes
 * @param[in]     subframe         Current subframe index (0 to 3)
 */
static void decode_pitch_lag_high(int *lag_int, int *lag_frac, int pitch_index,
                                  uint8_t *base_lag_int, int subframe)
{
    if (subframe == 0 || subframe == 2) {
        if (pitch_index < 376) {
            *lag_int  = (pitch_index + 137) >> 2;
            *lag_frac = pitch_index - (*lag_int << 2) + 136;
        } else if (pitch_index < 440) {
            *lag_int  = (pitch_index + 257 - 376) >> 1;
            *lag_frac = (pitch_index - (*lag_int << 1) + 256 - 376) * 2;
            /* the actual resolution is 1/2 but expressed as 1/4 */
        } else {
            *lag_int  = pitch_index - 280;
            *lag_frac = 0;
        }
        /* minimum lag for next subframe */
        *base_lag_int = av_clip(*lag_int - 8 - (*lag_frac < 0),
                                AMRWB_P_DELAY_MIN, AMRWB_P_DELAY_MAX - 15);
        // XXX: the spec states clearly that *base_lag_int should be
        // the nearest integer to *lag_int (minus 8), but the ref code
        // actually always uses its floor, I'm following the latter
    } else {
        *lag_int  = (pitch_index + 1) >> 2;
        *lag_frac = pitch_index - (*lag_int << 2);
        *lag_int += *base_lag_int;
    }
}

/**
 * Decode an adaptive codebook index into pitch lag for 8k85 and 6k60 modes.
 * The description is analogous to decode_pitch_lag_high, but in 6k60 the
 * relative index is used for all subframes except the first.
 */
static void decode_pitch_lag_low(int *lag_int, int *lag_frac, int pitch_index,
                                 uint8_t *base_lag_int, int subframe, enum Mode mode)
{
    if (subframe == 0 || (subframe == 2 && mode != MODE_6k60)) {
        if (pitch_index < 116) {
            *lag_int  = (pitch_index + 69) >> 1;
            *lag_frac = (pitch_index - (*lag_int << 1) + 68) * 2;
        } else {
            *lag_int  = pitch_index - 24;
            *lag_frac = 0;
        }
        // XXX: same problem as before
        *base_lag_int = av_clip(*lag_int - 8 - (*lag_frac < 0),
                                AMRWB_P_DELAY_MIN, AMRWB_P_DELAY_MAX - 15);
    } else {
        *lag_int  = (pitch_index + 1) >> 1;
        *lag_frac = (pitch_index - (*lag_int << 1)) * 2;
        *lag_int += *base_lag_int;
    }
}

/**
 * Find the pitch vector by interpolating the past excitation at the
 * pitch delay, which is obtained in this function.
 *
 * @param[in,out] ctx              The context
 * @param[in]     amr_subframe     Current subframe data
 * @param[in]     subframe         Current subframe index (0 to 3)
 */
static void decode_pitch_vector(AMRWBContext *ctx,
                                const AMRWBSubFrame *amr_subframe,
                                const int subframe)
{
    int pitch_lag_int, pitch_lag_frac;
    int i;
    float *exc     = ctx->excitation;
    enum Mode mode = ctx->fr_cur_mode;

    if (mode <= MODE_8k85) {
        decode_pitch_lag_low(&pitch_lag_int, &pitch_lag_frac, amr_subframe->adap,
                              &ctx->base_pitch_lag, subframe, mode);
    } else
        decode_pitch_lag_high(&pitch_lag_int, &pitch_lag_frac, amr_subframe->adap,
                              &ctx->base_pitch_lag, subframe);

    ctx->pitch_lag_int = pitch_lag_int;
    pitch_lag_int += pitch_lag_frac > 0;

    /* Calculate the pitch vector by interpolating the past excitation at the
       pitch lag using a hamming windowed sinc function */
    ctx->acelpf_ctx.acelp_interpolatef(exc,
                          exc + 1 - pitch_lag_int,
                          ac_inter, 4,
                          pitch_lag_frac + (pitch_lag_frac > 0 ? 0 : 4),
                          LP_ORDER, AMRWB_SFR_SIZE + 1);

    /* Check which pitch signal path should be used
     * 6k60 and 8k85 modes have the ltp flag set to 0 */
    if (amr_subframe->ltp) {
        memcpy(ctx->pitch_vector, exc, AMRWB_SFR_SIZE * sizeof(float));
    } else {
        for (i = 0; i < AMRWB_SFR_SIZE; i++)
            ctx->pitch_vector[i] = 0.18 * exc[i - 1] + 0.64 * exc[i] +
                                   0.18 * exc[i + 1];
        memcpy(exc, ctx->pitch_vector, AMRWB_SFR_SIZE * sizeof(float));
    }
}

/** Get x bits in the index interval [lsb,lsb+len-1] inclusive */
#define BIT_STR(x,lsb,len) av_mod_uintp2((x) >> (lsb), (len))

/** Get the bit at specified position */
#define BIT_POS(x, p) (((x) >> (p)) & 1)

/**
 * The next six functions decode_[i]p_track decode exactly i pulses
 * positions and amplitudes (-1 or 1) in a subframe track using
 * an encoded pulse indexing (TS 26.190 section 5.8.2).
 *
 * The results are given in out[], in which a negative number means
 * amplitude -1 and vice versa (i.e., ampl(x) = x / abs(x) ).
 *
 * @param[out] out                 Output buffer (writes i elements)
 * @param[in]  code                Pulse index (no. of bits varies, see below)
 * @param[in]  m                   (log2) Number of potential positions
 * @param[in]  off                 Offset for decoded positions
 */
static inline void decode_1p_track(int *out, int code, int m, int off)
{
    int pos = BIT_STR(code, 0, m) + off; ///code: m+1 bits

    out[0] = BIT_POS(code, m) ? -pos : pos;
}

static inline void decode_2p_track(int *out, int code, int m, int off) ///code: 2m+1 bits
{
    int pos0 = BIT_STR(code, m, m) + off;
    int pos1 = BIT_STR(code, 0, m) + off;

    out[0] = BIT_POS(code, 2*m) ? -pos0 : pos0;
    out[1] = BIT_POS(code, 2*m) ? -pos1 : pos1;
    out[1] = pos0 > pos1 ? -out[1] : out[1];
}

static void decode_3p_track(int *out, int code, int m, int off) ///code: 3m+1 bits
{
    int half_2p = BIT_POS(code, 2*m - 1) << (m - 1);

    decode_2p_track(out, BIT_STR(code, 0, 2*m - 1),
                    m - 1, off + half_2p);
    decode_1p_track(out + 2, BIT_STR(code, 2*m, m + 1), m, off);
}

static void decode_4p_track(int *out, int code, int m, int off) ///code: 4m bits
{
    int half_4p, subhalf_2p;
    int b_offset = 1 << (m - 1);

    switch (BIT_STR(code, 4*m - 2, 2)) { /* case ID (2 bits) */
    case 0: /* 0 pulses in A, 4 pulses in B or vice versa */
        half_4p = BIT_POS(code, 4*m - 3) << (m - 1); // which has 4 pulses
        subhalf_2p = BIT_POS(code, 2*m - 3) << (m - 2);

        decode_2p_track(out, BIT_STR(code, 0, 2*m - 3),
                        m - 2, off + half_4p + subhalf_2p);
        decode_2p_track(out + 2, BIT_STR(code, 2*m - 2, 2*m - 1),
                        m - 1, off + half_4p);
        break;
    case 1: /* 1 pulse in A, 3 pulses in B */
        decode_1p_track(out, BIT_STR(code,  3*m - 2, m),
                        m - 1, off);
        decode_3p_track(out + 1, BIT_STR(code, 0, 3*m - 2),
                        m - 1, off + b_offset);
        break;
    case 2: /* 2 pulses in each half */
        decode_2p_track(out, BIT_STR(code, 2*m - 1, 2*m - 1),
                        m - 1, off);
        decode_2p_track(out + 2, BIT_STR(code, 0, 2*m - 1),
                        m - 1, off + b_offset);
        break;
    case 3: /* 3 pulses in A, 1 pulse in B */
        decode_3p_track(out, BIT_STR(code, m, 3*m - 2),
                        m - 1, off);
        decode_1p_track(out + 3, BIT_STR(code, 0, m),
                        m - 1, off + b_offset);
        break;
    }
}

static void decode_5p_track(int *out, int code, int m, int off) ///code: 5m bits
{
    int half_3p = BIT_POS(code, 5*m - 1) << (m - 1);

    decode_3p_track(out, BIT_STR(code, 2*m + 1, 3*m - 2),
                    m - 1, off + half_3p);

    decode_2p_track(out + 3, BIT_STR(code, 0, 2*m + 1), m, off);
}

static void decode_6p_track(int *out, int code, int m, int off) ///code: 6m-2 bits
{
    int b_offset = 1 << (m - 1);
    /* which half has more pulses in cases 0 to 2 */
    int half_more  = BIT_POS(code, 6*m - 5) << (m - 1);
    int half_other = b_offset - half_more;

    switch (BIT_STR(code, 6*m - 4, 2)) { /* case ID (2 bits) */
    case 0: /* 0 pulses in A, 6 pulses in B or vice versa */
        decode_1p_track(out, BIT_STR(code, 0, m),
                        m - 1, off + half_more);
        decode_5p_track(out + 1, BIT_STR(code, m, 5*m - 5),
                        m - 1, off + half_more);
        break;
    case 1: /* 1 pulse in A, 5 pulses in B or vice versa */
        decode_1p_track(out, BIT_STR(code, 0, m),
                        m - 1, off + half_other);
        decode_5p_track(out + 1, BIT_STR(code, m, 5*m - 5),
                        m - 1, off + half_more);
        break;
    case 2: /* 2 pulses in A, 4 pulses in B or vice versa */
        decode_2p_track(out, BIT_STR(code, 0, 2*m - 1),
                        m - 1, off + half_other);
        decode_4p_track(out + 2, BIT_STR(code, 2*m - 1, 4*m - 4),
                        m - 1, off + half_more);
        break;
    case 3: /* 3 pulses in A, 3 pulses in B */
        decode_3p_track(out, BIT_STR(code, 3*m - 2, 3*m - 2),
                        m - 1, off);
        decode_3p_track(out + 3, BIT_STR(code, 0, 3*m - 2),
                        m - 1, off + b_offset);
        break;
    }
}

/**
 * Decode the algebraic codebook index to pulse positions and signs,
 * then construct the algebraic codebook vector.
 *
 * @param[out] fixed_vector        Buffer for the fixed codebook excitation
 * @param[in]  pulse_hi            MSBs part of the pulse index array (higher modes only)
 * @param[in]  pulse_lo            LSBs part of the pulse index array
 * @param[in]  mode                Mode of the current frame
 */
static void decode_fixed_vector(float *fixed_vector, const uint16_t *pulse_hi,
                                const uint16_t *pulse_lo, const enum Mode mode)
{
    /* sig_pos stores for each track the decoded pulse position indexes
     * (1-based) multiplied by its corresponding amplitude (+1 or -1) */
    int sig_pos[4][6];
    int spacing = (mode == MODE_6k60) ? 2 : 4;
    int i, j;

    switch (mode) {
    case MODE_6k60:
        for (i = 0; i < 2; i++)
            decode_1p_track(sig_pos[i], pulse_lo[i], 5, 1);
        break;
    case MODE_8k85:
        for (i = 0; i < 4; i++)
            decode_1p_track(sig_pos[i], pulse_lo[i], 4, 1);
        break;
    case MODE_12k65:
        for (i = 0; i < 4; i++)
            decode_2p_track(sig_pos[i], pulse_lo[i], 4, 1);
        break;
    case MODE_14k25:
        for (i = 0; i < 2; i++)
            decode_3p_track(sig_pos[i], pulse_lo[i], 4, 1);
        for (i = 2; i < 4; i++)
            decode_2p_track(sig_pos[i], pulse_lo[i], 4, 1);
        break;
    case MODE_15k85:
        for (i = 0; i < 4; i++)
            decode_3p_track(sig_pos[i], pulse_lo[i], 4, 1);
        break;
    case MODE_18k25:
        for (i = 0; i < 4; i++)
            decode_4p_track(sig_pos[i], (int) pulse_lo[i] +
                           ((int) pulse_hi[i] << 14), 4, 1);
        break;
    case MODE_19k85:
        for (i = 0; i < 2; i++)
            decode_5p_track(sig_pos[i], (int) pulse_lo[i] +
                           ((int) pulse_hi[i] << 10), 4, 1);
        for (i = 2; i < 4; i++)
            decode_4p_track(sig_pos[i], (int) pulse_lo[i] +
                           ((int) pulse_hi[i] << 14), 4, 1);
        break;
    case MODE_23k05:
    case MODE_23k85:
        for (i = 0; i < 4; i++)
            decode_6p_track(sig_pos[i], (int) pulse_lo[i] +
                           ((int) pulse_hi[i] << 11), 4, 1);
        break;
    }

    memset(fixed_vector, 0, sizeof(float) * AMRWB_SFR_SIZE);

    for (i = 0; i < 4; i++)
        for (j = 0; j < pulses_nb_per_mode_tr[mode][i]; j++) {
            int pos = (FFABS(sig_pos[i][j]) - 1) * spacing + i;

            fixed_vector[pos] += sig_pos[i][j] < 0 ? -1.0 : 1.0;
        }
}

/**
 * Decode pitch gain and fixed gain correction factor.
 *
 * @param[in]  vq_gain             Vector-quantized index for gains
 * @param[in]  mode                Mode of the current frame
 * @param[out] fixed_gain_factor   Decoded fixed gain correction factor
 * @param[out] pitch_gain          Decoded pitch gain
 */
static void decode_gains(const uint8_t vq_gain, const enum Mode mode,
                         float *fixed_gain_factor, float *pitch_gain)
{
    const int16_t *gains = (mode <= MODE_8k85 ? qua_gain_6b[vq_gain] :
                                                qua_gain_7b[vq_gain]);

    *pitch_gain        = gains[0] * (1.0f / (1 << 14));
    *fixed_gain_factor = gains[1] * (1.0f / (1 << 11));
}

/**
 * Apply pitch sharpening filters to the fixed codebook vector.
 *
 * @param[in]     ctx              The context
 * @param[in,out] fixed_vector     Fixed codebook excitation
 */
// XXX: Spec states this procedure should be applied when the pitch
// lag is less than 64, but this checking seems absent in reference and AMR-NB
static void pitch_sharpening(AMRWBContext *ctx, float *fixed_vector)
{
    int i;

    /* Tilt part */
    for (i = AMRWB_SFR_SIZE - 1; i != 0; i--)
        fixed_vector[i] -= fixed_vector[i - 1] * ctx->tilt_coef;

    /* Periodicity enhancement part */
    for (i = ctx->pitch_lag_int; i < AMRWB_SFR_SIZE; i++)
        fixed_vector[i] += fixed_vector[i - ctx->pitch_lag_int] * 0.85;
}

/**
 * Calculate the voicing factor (-1.0 = unvoiced to 1.0 = voiced).
 *
 * @param[in] p_vector, f_vector   Pitch and fixed excitation vectors
 * @param[in] p_gain, f_gain       Pitch and fixed gains
 * @param[in] ctx                  The context
 */
// XXX: There is something wrong with the precision here! The magnitudes
// of the energies are not correct. Please check the reference code carefully
static float voice_factor(float *p_vector, float p_gain,
                          float *f_vector, float f_gain,
                          CELPMContext *ctx)
{
    double p_ener = (double) ctx->dot_productf(p_vector, p_vector,
                                                          AMRWB_SFR_SIZE) *
                    p_gain * p_gain;
    double f_ener = (double) ctx->dot_productf(f_vector, f_vector,
                                                          AMRWB_SFR_SIZE) *
                    f_gain * f_gain;

    return (p_ener - f_ener) / (p_ener + f_ener + 0.01);
}

/**
 * Reduce fixed vector sparseness by smoothing with one of three IR filters,
 * also known as "adaptive phase dispersion".
 *
 * @param[in]     ctx              The context
 * @param[in,out] fixed_vector     Unfiltered fixed vector
 * @param[out]    buf              Space for modified vector if necessary
 *
 * @return The potentially overwritten filtered fixed vector address
 */
static float *anti_sparseness(AMRWBContext *ctx,
                              float *fixed_vector, float *buf)
{
    int ir_filter_nr;

    if (ctx->fr_cur_mode > MODE_8k85) // no filtering in higher modes
        return fixed_vector;

    if (ctx->pitch_gain[0] < 0.6) {
        ir_filter_nr = 0;      // strong filtering
    } else if (ctx->pitch_gain[0] < 0.9) {
        ir_filter_nr = 1;      // medium filtering
    } else
        ir_filter_nr = 2;      // no filtering

    /* detect 'onset' */
    if (ctx->fixed_gain[0] > 3.0 * ctx->fixed_gain[1]) {
        if (ir_filter_nr < 2)
            ir_filter_nr++;
    } else {
        int i, count = 0;

        for (i = 0; i < 6; i++)
            if (ctx->pitch_gain[i] < 0.6)
                count++;

        if (count > 2)
            ir_filter_nr = 0;

        if (ir_filter_nr > ctx->prev_ir_filter_nr + 1)
            ir_filter_nr--;
    }

    /* update ir filter strength history */
    ctx->prev_ir_filter_nr = ir_filter_nr;

    ir_filter_nr += (ctx->fr_cur_mode == MODE_8k85);

    if (ir_filter_nr < 2) {
        int i;
        const float *coef = ir_filters_lookup[ir_filter_nr];

        /* Circular convolution code in the reference
         * decoder was modified to avoid using one
         * extra array. The filtered vector is given by:
         *
         * c2(n) = sum(i,0,len-1){ c(i) * coef( (n - i + len) % len ) }
         */

        memset(buf, 0, sizeof(float) * AMRWB_SFR_SIZE);
        for (i = 0; i < AMRWB_SFR_SIZE; i++)
            if (fixed_vector[i])
                ff_celp_circ_addf(buf, buf, coef, i, fixed_vector[i],
                                  AMRWB_SFR_SIZE);
        fixed_vector = buf;
    }

    return fixed_vector;
}

/**
 * Calculate a stability factor {teta} based on distance between
 * current and past isf. A value of 1 shows maximum signal stability.
 */
static float stability_factor(const float *isf, const float *isf_past)
{
    int i;
    float acc = 0.0;

    for (i = 0; i < LP_ORDER - 1; i++)
        acc += (isf[i] - isf_past[i]) * (isf[i] - isf_past[i]);

    // XXX: This part is not so clear from the reference code
    // the result is more accurate changing the "/ 256" to "* 512"
    return FFMAX(0.0, 1.25 - acc * 0.8 * 512);
}

/**
 * Apply a non-linear fixed gain smoothing in order to reduce
 * fluctuation in the energy of excitation.
 *
 * @param[in]     fixed_gain       Unsmoothed fixed gain
 * @param[in,out] prev_tr_gain     Previous threshold gain (updated)
 * @param[in]     voice_fac        Frame voicing factor
 * @param[in]     stab_fac         Frame stability factor
 *
 * @return The smoothed gain
 */
static float noise_enhancer(float fixed_gain, float *prev_tr_gain,
                            float voice_fac,  float stab_fac)
{
    float sm_fac = 0.5 * (1 - voice_fac) * stab_fac;
    float g0;

    // XXX: the following fixed-point constants used to in(de)crement
    // gain by 1.5dB were taken from the reference code, maybe it could
    // be simpler
    if (fixed_gain < *prev_tr_gain) {
        g0 = FFMIN(*prev_tr_gain, fixed_gain + fixed_gain *
                     (6226 * (1.0f / (1 << 15)))); // +1.5 dB
    } else
        g0 = FFMAX(*prev_tr_gain, fixed_gain *
                    (27536 * (1.0f / (1 << 15)))); // -1.5 dB

    *prev_tr_gain = g0; // update next frame threshold

    return sm_fac * g0 + (1 - sm_fac) * fixed_gain;
}

/**
 * Filter the fixed_vector to emphasize the higher frequencies.
 *
 * @param[in,out] fixed_vector     Fixed codebook vector
 * @param[in]     voice_fac        Frame voicing factor
 */
static void pitch_enhancer(float *fixed_vector, float voice_fac)
{
    int i;
    float cpe  = 0.125 * (1 + voice_fac);
    float last = fixed_vector[0]; // holds c(i - 1)

    fixed_vector[0] -= cpe * fixed_vector[1];

    for (i = 1; i < AMRWB_SFR_SIZE - 1; i++) {
        float cur = fixed_vector[i];

        fixed_vector[i] -= cpe * (last + fixed_vector[i + 1]);
        last = cur;
    }

    fixed_vector[AMRWB_SFR_SIZE - 1] -= cpe * last;
}

/**
 * Conduct 16th order linear predictive coding synthesis from excitation.
 *
 * @param[in]     ctx              Pointer to the AMRWBContext
 * @param[in]     lpc              Pointer to the LPC coefficients
 * @param[out]    excitation       Buffer for synthesis final excitation
 * @param[in]     fixed_gain       Fixed codebook gain for synthesis
 * @param[in]     fixed_vector     Algebraic codebook vector
 * @param[in,out] samples          Pointer to the output samples and memory
 */
static void synthesis(AMRWBContext *ctx, float *lpc, float *excitation,
                      float fixed_gain, const float *fixed_vector,
                      float *samples)
{
    ctx->acelpv_ctx.weighted_vector_sumf(excitation, ctx->pitch_vector, fixed_vector,
                            ctx->pitch_gain[0], fixed_gain, AMRWB_SFR_SIZE);

    /* emphasize pitch vector contribution in low bitrate modes */
    if (ctx->pitch_gain[0] > 0.5 && ctx->fr_cur_mode <= MODE_8k85) {
        int i;
        float energy = ctx->celpm_ctx.dot_productf(excitation, excitation,
                                                    AMRWB_SFR_SIZE);

        // XXX: Weird part in both ref code and spec. A unknown parameter
        // {beta} seems to be identical to the current pitch gain
        float pitch_factor = 0.25 * ctx->pitch_gain[0] * ctx->pitch_gain[0];

        for (i = 0; i < AMRWB_SFR_SIZE; i++)
            excitation[i] += pitch_factor * ctx->pitch_vector[i];

        ff_scale_vector_to_given_sum_of_squares(excitation, excitation,
                                                energy, AMRWB_SFR_SIZE);
    }

    ctx->celpf_ctx.celp_lp_synthesis_filterf(samples, lpc, excitation,
                                 AMRWB_SFR_SIZE, LP_ORDER);
}

/**
 * Apply to synthesis a de-emphasis filter of the form:
 * H(z) = 1 / (1 - m * z^-1)
 *
 * @param[out]    out              Output buffer
 * @param[in]     in               Input samples array with in[-1]
 * @param[in]     m                Filter coefficient
 * @param[in,out] mem              State from last filtering
 */
static void de_emphasis(float *out, float *in, float m, float mem[1])
{
    int i;

    out[0] = in[0] + m * mem[0];

    for (i = 1; i < AMRWB_SFR_SIZE; i++)
         out[i] = in[i] + out[i - 1] * m;

    mem[0] = out[AMRWB_SFR_SIZE - 1];
}

/**
 * Upsample a signal by 5/4 ratio (from 12.8kHz to 16kHz) using
 * a FIR interpolation filter. Uses past data from before *in address.
 *
 * @param[out] out                 Buffer for interpolated signal
 * @param[in]  in                  Current signal data (length 0.8*o_size)
 * @param[in]  o_size              Output signal length
 * @param[in] ctx                  The context
 */
static void upsample_5_4(float *out, const float *in, int o_size, CELPMContext *ctx)
{
    const float *in0 = in - UPS_FIR_SIZE + 1;
    int i, j, k;
    int int_part = 0, frac_part;

    i = 0;
    for (j = 0; j < o_size / 5; j++) {
        out[i] = in[int_part];
        frac_part = 4;
        i++;

        for (k = 1; k < 5; k++) {
            out[i] = ctx->dot_productf(in0 + int_part,
                                                  upsample_fir[4 - frac_part],
                                                  UPS_MEM_SIZE);
            int_part++;
            frac_part--;
            i++;
        }
    }
}

/**
 * Calculate the high-band gain based on encoded index (23k85 mode) or
 * on the low-band speech signal and the Voice Activity Detection flag.
 *
 * @param[in] ctx                  The context
 * @param[in] synth                LB speech synthesis at 12.8k
 * @param[in] hb_idx               Gain index for mode 23k85 only
 * @param[in] vad                  VAD flag for the frame
 */
static float find_hb_gain(AMRWBContext *ctx, const float *synth,
                          uint16_t hb_idx, uint8_t vad)
{
    int wsp = (vad > 0);
    float tilt;
    float tmp;

    if (ctx->fr_cur_mode == MODE_23k85)
        return qua_hb_gain[hb_idx] * (1.0f / (1 << 14));

    tmp = ctx->celpm_ctx.dot_productf(synth, synth + 1, AMRWB_SFR_SIZE - 1);

    if (tmp > 0) {
        tilt = tmp / ctx->celpm_ctx.dot_productf(synth, synth, AMRWB_SFR_SIZE);
    } else
        tilt = 0;

    /* return gain bounded by [0.1, 1.0] */
    return av_clipf((1.0 - tilt) * (1.25 - 0.25 * wsp), 0.1, 1.0);
}

/**
 * Generate the high-band excitation with the same energy from the lower
 * one and scaled by the given gain.
 *
 * @param[in]  ctx                 The context
 * @param[out] hb_exc              Buffer for the excitation
 * @param[in]  synth_exc           Low-band excitation used for synthesis
 * @param[in]  hb_gain             Wanted excitation gain
 */
static void scaled_hb_excitation(AMRWBContext *ctx, float *hb_exc,
                                 const float *synth_exc, float hb_gain)
{
    int i;
    float energy = ctx->celpm_ctx.dot_productf(synth_exc, synth_exc,
                                                AMRWB_SFR_SIZE);

    /* Generate a white-noise excitation */
    for (i = 0; i < AMRWB_SFR_SIZE_16k; i++)
        hb_exc[i] = 32768.0 - (uint16_t) av_lfg_get(&ctx->prng);

    ff_scale_vector_to_given_sum_of_squares(hb_exc, hb_exc,
                                            energy * hb_gain * hb_gain,
                                            AMRWB_SFR_SIZE_16k);
}

/**
 * Calculate the auto-correlation for the ISF difference vector.
 */
static float auto_correlation(float *diff_isf, float mean, int lag)
{
    int i;
    float sum = 0.0;

    for (i = 7; i < LP_ORDER - 2; i++) {
        float prod = (diff_isf[i] - mean) * (diff_isf[i - lag] - mean);
        sum += prod * prod;
    }
    return sum;
}

/**
 * Extrapolate a ISF vector to the 16kHz range (20th order LP)
 * used at mode 6k60 LP filter for the high frequency band.
 *
 * @param[out] isf Buffer for extrapolated isf; contains LP_ORDER
 *                 values on input
 */
static void extrapolate_isf(float isf[LP_ORDER_16k])
{
    float diff_isf[LP_ORDER - 2], diff_mean;
    float corr_lag[3];
    float est, scale;
    int i, j, i_max_corr;

    isf[LP_ORDER_16k - 1] = isf[LP_ORDER - 1];

    /* Calculate the difference vector */
    for (i = 0; i < LP_ORDER - 2; i++)
        diff_isf[i] = isf[i + 1] - isf[i];

    diff_mean = 0.0;
    for (i = 2; i < LP_ORDER - 2; i++)
        diff_mean += diff_isf[i] * (1.0f / (LP_ORDER - 4));

    /* Find which is the maximum autocorrelation */
    i_max_corr = 0;
    for (i = 0; i < 3; i++) {
        corr_lag[i] = auto_correlation(diff_isf, diff_mean, i + 2);

        if (corr_lag[i] > corr_lag[i_max_corr])
            i_max_corr = i;
    }
    i_max_corr++;

    for (i = LP_ORDER - 1; i < LP_ORDER_16k - 1; i++)
        isf[i] = isf[i - 1] + isf[i - 1 - i_max_corr]
                            - isf[i - 2 - i_max_corr];

    /* Calculate an estimate for ISF(18) and scale ISF based on the error */
    est   = 7965 + (isf[2] - isf[3] - isf[4]) / 6.0;
    scale = 0.5 * (FFMIN(est, 7600) - isf[LP_ORDER - 2]) /
            (isf[LP_ORDER_16k - 2] - isf[LP_ORDER - 2]);

    for (i = LP_ORDER - 1, j = 0; i < LP_ORDER_16k - 1; i++, j++)
        diff_isf[j] = scale * (isf[i] - isf[i - 1]);

    /* Stability insurance */
    for (i = 1; i < LP_ORDER_16k - LP_ORDER; i++)
        if (diff_isf[i] + diff_isf[i - 1] < 5.0) {
            if (diff_isf[i] > diff_isf[i - 1]) {
                diff_isf[i - 1] = 5.0 - diff_isf[i];
            } else
                diff_isf[i] = 5.0 - diff_isf[i - 1];
        }

    for (i = LP_ORDER - 1, j = 0; i < LP_ORDER_16k - 1; i++, j++)
        isf[i] = isf[i - 1] + diff_isf[j] * (1.0f / (1 << 15));

    /* Scale the ISF vector for 16000 Hz */
    for (i = 0; i < LP_ORDER_16k - 1; i++)
        isf[i] *= 0.8;
}

/**
 * Spectral expand the LP coefficients using the equation:
 *   y[i] = x[i] * (gamma ** i)
 *
 * @param[out] out                 Output buffer (may use input array)
 * @param[in]  lpc                 LP coefficients array
 * @param[in]  gamma               Weighting factor
 * @param[in]  size                LP array size
 */
static void lpc_weighting(float *out, const float *lpc, float gamma, int size)
{
    int i;
    float fac = gamma;

    for (i = 0; i < size; i++) {
        out[i] = lpc[i] * fac;
        fac   *= gamma;
    }
}

/**
 * Conduct 20th order linear predictive coding synthesis for the high
 * frequency band excitation at 16kHz.
 *
 * @param[in]     ctx              The context
 * @param[in]     subframe         Current subframe index (0 to 3)
 * @param[in,out] samples          Pointer to the output speech samples
 * @param[in]     exc              Generated white-noise scaled excitation
 * @param[in]     isf              Current frame isf vector
 * @param[in]     isf_past         Past frame final isf vector
 */
static void hb_synthesis(AMRWBContext *ctx, int subframe, float *samples,
                         const float *exc, const float *isf, const float *isf_past)
{
    float hb_lpc[LP_ORDER_16k];
    enum Mode mode = ctx->fr_cur_mode;

    if (mode == MODE_6k60) {
        float e_isf[LP_ORDER_16k]; // ISF vector for extrapolation
        double e_isp[LP_ORDER_16k];

        ctx->acelpv_ctx.weighted_vector_sumf(e_isf, isf_past, isf, isfp_inter[subframe],
                                1.0 - isfp_inter[subframe], LP_ORDER);

        extrapolate_isf(e_isf);

        e_isf[LP_ORDER_16k - 1] *= 2.0;
        ff_acelp_lsf2lspd(e_isp, e_isf, LP_ORDER_16k);
        ff_amrwb_lsp2lpc(e_isp, hb_lpc, LP_ORDER_16k);

        lpc_weighting(hb_lpc, hb_lpc, 0.9, LP_ORDER_16k);
    } else {
        lpc_weighting(hb_lpc, ctx->lp_coef[subframe], 0.6, LP_ORDER);
    }

    ctx->celpf_ctx.celp_lp_synthesis_filterf(samples, hb_lpc, exc, AMRWB_SFR_SIZE_16k,
                                 (mode == MODE_6k60) ? LP_ORDER_16k : LP_ORDER);
}

/**
 * Apply a 15th order filter to high-band samples.
 * The filter characteristic depends on the given coefficients.
 *
 * @param[out]    out              Buffer for filtered output
 * @param[in]     fir_coef         Filter coefficients
 * @param[in,out] mem              State from last filtering (updated)
 * @param[in]     in               Input speech data (high-band)
 *
 * @remark It is safe to pass the same array in in and out parameters
 */

#ifndef hb_fir_filter
static void hb_fir_filter(float *out, const float fir_coef[HB_FIR_SIZE + 1],
                          float mem[HB_FIR_SIZE], const float *in)
{
    int i, j;
    float data[AMRWB_SFR_SIZE_16k + HB_FIR_SIZE]; // past and current samples

    memcpy(data, mem, HB_FIR_SIZE * sizeof(float));
    memcpy(data + HB_FIR_SIZE, in, AMRWB_SFR_SIZE_16k * sizeof(float));

    for (i = 0; i < AMRWB_SFR_SIZE_16k; i++) {
        out[i] = 0.0;
        for (j = 0; j <= HB_FIR_SIZE; j++)
            out[i] += data[i + j] * fir_coef[j];
    }

    memcpy(mem, data + AMRWB_SFR_SIZE_16k, HB_FIR_SIZE * sizeof(float));
}
#endif /* hb_fir_filter */

/**
 * Update context state before the next subframe.
 */
static void update_sub_state(AMRWBContext *ctx)
{
    memmove(&ctx->excitation_buf[0], &ctx->excitation_buf[AMRWB_SFR_SIZE],
            (AMRWB_P_DELAY_MAX + LP_ORDER + 1) * sizeof(float));

    memmove(&ctx->pitch_gain[1], &ctx->pitch_gain[0], 5 * sizeof(float));
    memmove(&ctx->fixed_gain[1], &ctx->fixed_gain[0], 1 * sizeof(float));

    memmove(&ctx->samples_az[0], &ctx->samples_az[AMRWB_SFR_SIZE],
            LP_ORDER * sizeof(float));
    memmove(&ctx->samples_up[0], &ctx->samples_up[AMRWB_SFR_SIZE],
            UPS_MEM_SIZE * sizeof(float));
    memmove(&ctx->samples_hb[0], &ctx->samples_hb[AMRWB_SFR_SIZE_16k],
            LP_ORDER_16k * sizeof(float));
}

static int amrwb_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                              int *got_frame_ptr, AVPacket *avpkt)
{
    AMRWBChannelsContext *s  = avctx->priv_data;
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    int sub, i, ret;

    /* get output buffer */
    frame->nb_samples = 4 * AMRWB_SFR_SIZE_16k;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    for (int ch = 0; ch < avctx->ch_layout.nb_channels; ch++) {
        AMRWBContext *ctx  = &s->ch[ch];
        AMRWBFrame   *cf   = &ctx->frame;
        int expected_fr_size, header_size;
        float spare_vector[AMRWB_SFR_SIZE];      // extra stack space to hold result from anti-sparseness processing
        float fixed_gain_factor;                 // fixed gain correction factor (gamma)
        float *synth_fixed_vector;               // pointer to the fixed vector that synthesis should use
        float synth_fixed_gain;                  // the fixed gain that synthesis should use
        float voice_fac, stab_fac;               // parameters used for gain smoothing
        float synth_exc[AMRWB_SFR_SIZE];         // post-processed excitation for synthesis
        float hb_exc[AMRWB_SFR_SIZE_16k];        // excitation for the high frequency band
        float hb_samples[AMRWB_SFR_SIZE_16k];    // filtered high-band samples from synthesis
        float hb_gain;
        float *buf_out = (float *)frame->extended_data[ch];

        header_size      = decode_mime_header(ctx, buf);
        expected_fr_size = ((cf_sizes_wb[ctx->fr_cur_mode] + 7) >> 3) + 1;

        if (!ctx->fr_quality)
            av_log(avctx, AV_LOG_ERROR, "Encountered a bad or corrupted frame\n");

        if (ctx->fr_cur_mode == NO_DATA || !ctx->fr_quality) {
            /* The specification suggests a "random signal" and
               "a muting technique" to "gradually decrease the output level". */
            av_samples_set_silence(&frame->extended_data[ch], 0, frame->nb_samples, 1, AV_SAMPLE_FMT_FLT);
            buf += expected_fr_size;
            buf_size -= expected_fr_size;
            continue;
        }
        if (ctx->fr_cur_mode > MODE_SID) {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid mode %d\n", ctx->fr_cur_mode);
            return AVERROR_INVALIDDATA;
        }

        if (buf_size < expected_fr_size) {
            av_log(avctx, AV_LOG_ERROR,
                   "Frame too small (%d bytes). Truncated file?\n", buf_size);
            *got_frame_ptr = 0;
            return AVERROR_INVALIDDATA;
        }

        if (ctx->fr_cur_mode == MODE_SID) { /* Comfort noise frame */
            avpriv_request_sample(avctx, "SID mode");
            return AVERROR_PATCHWELCOME;
        }

        ff_amr_bit_reorder((uint16_t *) &ctx->frame, sizeof(AMRWBFrame),
                           buf + header_size, amr_bit_orderings_by_mode[ctx->fr_cur_mode]);

        /* Decode the quantized ISF vector */
        if (ctx->fr_cur_mode == MODE_6k60) {
            decode_isf_indices_36b(cf->isp_id, ctx->isf_cur);
        } else {
            decode_isf_indices_46b(cf->isp_id, ctx->isf_cur);
        }

        isf_add_mean_and_past(ctx->isf_cur, ctx->isf_q_past);
        ff_set_min_dist_lsf(ctx->isf_cur, MIN_ISF_SPACING, LP_ORDER - 1);

        stab_fac = stability_factor(ctx->isf_cur, ctx->isf_past_final);

        ctx->isf_cur[LP_ORDER - 1] *= 2.0;
        ff_acelp_lsf2lspd(ctx->isp[3], ctx->isf_cur, LP_ORDER);

        /* Generate a ISP vector for each subframe */
        if (ctx->first_frame) {
            ctx->first_frame = 0;
            memcpy(ctx->isp_sub4_past, ctx->isp[3], LP_ORDER * sizeof(double));
        }
        interpolate_isp(ctx->isp, ctx->isp_sub4_past);

        for (sub = 0; sub < 4; sub++)
            ff_amrwb_lsp2lpc(ctx->isp[sub], ctx->lp_coef[sub], LP_ORDER);

        for (sub = 0; sub < 4; sub++) {
            const AMRWBSubFrame *cur_subframe = &cf->subframe[sub];
            float *sub_buf = buf_out + sub * AMRWB_SFR_SIZE_16k;

            /* Decode adaptive codebook (pitch vector) */
            decode_pitch_vector(ctx, cur_subframe, sub);
            /* Decode innovative codebook (fixed vector) */
            decode_fixed_vector(ctx->fixed_vector, cur_subframe->pul_ih,
                                cur_subframe->pul_il, ctx->fr_cur_mode);

            pitch_sharpening(ctx, ctx->fixed_vector);

            decode_gains(cur_subframe->vq_gain, ctx->fr_cur_mode,
                         &fixed_gain_factor, &ctx->pitch_gain[0]);

            ctx->fixed_gain[0] =
                ff_amr_set_fixed_gain(fixed_gain_factor,
                                      ctx->celpm_ctx.dot_productf(ctx->fixed_vector,
                                                                  ctx->fixed_vector,
                                                                  AMRWB_SFR_SIZE) /
                                      AMRWB_SFR_SIZE,
                                      ctx->prediction_error,
                                      ENERGY_MEAN, energy_pred_fac);

            /* Calculate voice factor and store tilt for next subframe */
            voice_fac      = voice_factor(ctx->pitch_vector, ctx->pitch_gain[0],
                                          ctx->fixed_vector, ctx->fixed_gain[0],
                                          &ctx->celpm_ctx);
            ctx->tilt_coef = voice_fac * 0.25 + 0.25;

            /* Construct current excitation */
            for (i = 0; i < AMRWB_SFR_SIZE; i++) {
                ctx->excitation[i] *= ctx->pitch_gain[0];
                ctx->excitation[i] += ctx->fixed_gain[0] * ctx->fixed_vector[i];
                ctx->excitation[i] = truncf(ctx->excitation[i]);
            }

            /* Post-processing of excitation elements */
            synth_fixed_gain = noise_enhancer(ctx->fixed_gain[0], &ctx->prev_tr_gain,
                                              voice_fac, stab_fac);

            synth_fixed_vector = anti_sparseness(ctx, ctx->fixed_vector,
                                                 spare_vector);

            pitch_enhancer(synth_fixed_vector, voice_fac);

            synthesis(ctx, ctx->lp_coef[sub], synth_exc, synth_fixed_gain,
                      synth_fixed_vector, &ctx->samples_az[LP_ORDER]);

            /* Synthesis speech post-processing */
            de_emphasis(&ctx->samples_up[UPS_MEM_SIZE],
                        &ctx->samples_az[LP_ORDER], PREEMPH_FAC, ctx->demph_mem);

            ctx->acelpf_ctx.acelp_apply_order_2_transfer_function(&ctx->samples_up[UPS_MEM_SIZE],
                                                                  &ctx->samples_up[UPS_MEM_SIZE], hpf_zeros, hpf_31_poles,
                                                                  hpf_31_gain, ctx->hpf_31_mem, AMRWB_SFR_SIZE);

            upsample_5_4(sub_buf, &ctx->samples_up[UPS_FIR_SIZE],
                         AMRWB_SFR_SIZE_16k, &ctx->celpm_ctx);

            /* High frequency band (6.4 - 7.0 kHz) generation part */
            ctx->acelpf_ctx.acelp_apply_order_2_transfer_function(hb_samples,
                                                                  &ctx->samples_up[UPS_MEM_SIZE], hpf_zeros, hpf_400_poles,
                                                                  hpf_400_gain, ctx->hpf_400_mem, AMRWB_SFR_SIZE);

            hb_gain = find_hb_gain(ctx, hb_samples,
                                   cur_subframe->hb_gain, cf->vad);

            scaled_hb_excitation(ctx, hb_exc, synth_exc, hb_gain);

            hb_synthesis(ctx, sub, &ctx->samples_hb[LP_ORDER_16k],
                         hb_exc, ctx->isf_cur, ctx->isf_past_final);

            /* High-band post-processing filters */
            hb_fir_filter(hb_samples, bpf_6_7_coef, ctx->bpf_6_7_mem,
                          &ctx->samples_hb[LP_ORDER_16k]);

            if (ctx->fr_cur_mode == MODE_23k85)
                hb_fir_filter(hb_samples, lpf_7_coef, ctx->lpf_7_mem,
                              hb_samples);

            /* Add the low and high frequency bands */
            for (i = 0; i < AMRWB_SFR_SIZE_16k; i++)
                sub_buf[i] = (sub_buf[i] + hb_samples[i]) * (1.0f / (1 << 15));

            /* Update buffers and history */
            update_sub_state(ctx);
        }

        /* update state for next frame */
        memcpy(ctx->isp_sub4_past, ctx->isp[3], LP_ORDER * sizeof(ctx->isp[3][0]));
        memcpy(ctx->isf_past_final, ctx->isf_cur, LP_ORDER * sizeof(float));

        buf += expected_fr_size;
        buf_size -= expected_fr_size;
    }

    *got_frame_ptr = 1;

    return buf - avpkt->data;
}

const FFCodec ff_amrwb_decoder = {
    .p.name         = "amrwb",
    CODEC_LONG_NAME("AMR-WB (Adaptive Multi-Rate WideBand)"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_AMR_WB,
    .priv_data_size = sizeof(AMRWBChannelsContext),
    .init           = amrwb_decode_init,
    FF_CODEC_DECODE_CB(amrwb_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_CHANNEL_CONF,
    .p.sample_fmts  = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_FLTP,
                                                     AV_SAMPLE_FMT_NONE },
};
