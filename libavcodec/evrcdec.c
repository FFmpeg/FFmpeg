/*
 * Enhanced Variable Rate Codec, Service Option 3 decoder
 * Copyright (c) 2013 Paul B Mahol
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
 * Enhanced Variable Rate Codec, Service Option 3 decoder
 * @author Paul B Mahol
 */

#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "internal.h"
#include "get_bits.h"
#include "evrcdata.h"
#include "acelp_vectors.h"
#include "lsp.h"

#define MIN_LSP_SEP (0.05 / (2.0 * M_PI))
#define MIN_DELAY      20
#define MAX_DELAY     120
#define NB_SUBFRAMES    3
#define SUBFRAME_SIZE  54
#define FILTER_ORDER   10
#define ACB_SIZE      128

typedef enum {
    RATE_ERRS = -1,
    SILENCE,
    RATE_QUANT,
    RATE_QUARTER,
    RATE_HALF,
    RATE_FULL,
} evrc_packet_rate;

/**
 * EVRC-A unpacked data frame
 */
typedef struct EVRCAFrame {
    uint8_t  lpc_flag;        ///< spectral change indicator
    uint16_t lsp[4];          ///< index into LSP codebook
    uint8_t  pitch_delay;     ///< pitch delay for entire frame
    uint8_t  delay_diff;      ///< delay difference for entire frame
    uint8_t  acb_gain[3];     ///< adaptive codebook gain
    uint16_t fcb_shape[3][4]; ///< fixed codebook shape
    uint8_t  fcb_gain[3];     ///< fixed codebook gain index
    uint8_t  energy_gain;     ///< frame energy gain index
    uint8_t  tty;             ///< tty baud rate bit
} EVRCAFrame;

typedef struct EVRCContext {
    AVClass *class;

    int              postfilter;

    GetBitContext    gb;
    evrc_packet_rate bitrate;
    evrc_packet_rate last_valid_bitrate;
    EVRCAFrame       frame;

    float            lspf[FILTER_ORDER];
    float            prev_lspf[FILTER_ORDER];
    float            synthesis[FILTER_ORDER];
    float            postfilter_fir[FILTER_ORDER];
    float            postfilter_iir[FILTER_ORDER];
    float            postfilter_residual[ACB_SIZE + SUBFRAME_SIZE];
    float            pitch_delay;
    float            prev_pitch_delay;
    float            avg_acb_gain;  ///< average adaptive codebook gain
    float            avg_fcb_gain;  ///< average fixed codebook gain
    float            pitch[ACB_SIZE + FILTER_ORDER + SUBFRAME_SIZE];
    float            pitch_back[ACB_SIZE];
    float            interpolation_coeffs[136];
    float            energy_vector[NB_SUBFRAMES];
    float            fade_scale;
    float            last;

    uint8_t          prev_energy_gain;
    uint8_t          prev_error_flag;
    uint8_t          warned_buf_mismatch_bitrate;
} EVRCContext;

/**
 * Frame unpacking for RATE_FULL, RATE_HALF and RATE_QUANT
 *
 * @param e the context
 *
 * TIA/IS-127 Table 4.21-1
 */
static void unpack_frame(EVRCContext *e)
{
    EVRCAFrame *frame = &e->frame;
    GetBitContext *gb = &e->gb;

    switch (e->bitrate) {
    case RATE_FULL:
        frame->lpc_flag        = get_bits1(gb);
        frame->lsp[0]          = get_bits(gb,  6);
        frame->lsp[1]          = get_bits(gb,  6);
        frame->lsp[2]          = get_bits(gb,  9);
        frame->lsp[3]          = get_bits(gb,  7);
        frame->pitch_delay     = get_bits(gb,  7);
        frame->delay_diff      = get_bits(gb,  5);
        frame->acb_gain[0]     = get_bits(gb,  3);
        frame->fcb_shape[0][0] = get_bits(gb,  8);
        frame->fcb_shape[0][1] = get_bits(gb,  8);
        frame->fcb_shape[0][2] = get_bits(gb,  8);
        frame->fcb_shape[0][3] = get_bits(gb, 11);
        frame->fcb_gain[0]     = get_bits(gb,  5);
        frame->acb_gain[1]     = get_bits(gb,  3);
        frame->fcb_shape[1][0] = get_bits(gb,  8);
        frame->fcb_shape[1][1] = get_bits(gb,  8);
        frame->fcb_shape[1][2] = get_bits(gb,  8);
        frame->fcb_shape[1][3] = get_bits(gb, 11);
        frame->fcb_gain    [1] = get_bits(gb,  5);
        frame->acb_gain    [2] = get_bits(gb,  3);
        frame->fcb_shape[2][0] = get_bits(gb,  8);
        frame->fcb_shape[2][1] = get_bits(gb,  8);
        frame->fcb_shape[2][2] = get_bits(gb,  8);
        frame->fcb_shape[2][3] = get_bits(gb, 11);
        frame->fcb_gain    [2] = get_bits(gb,  5);
        frame->tty             = get_bits1(gb);
        break;
    case RATE_HALF:
        frame->lsp         [0] = get_bits(gb,  7);
        frame->lsp         [1] = get_bits(gb,  7);
        frame->lsp         [2] = get_bits(gb,  8);
        frame->pitch_delay     = get_bits(gb,  7);
        frame->acb_gain    [0] = get_bits(gb,  3);
        frame->fcb_shape[0][0] = get_bits(gb, 10);
        frame->fcb_gain    [0] = get_bits(gb,  4);
        frame->acb_gain    [1] = get_bits(gb,  3);
        frame->fcb_shape[1][0] = get_bits(gb, 10);
        frame->fcb_gain    [1] = get_bits(gb,  4);
        frame->acb_gain    [2] = get_bits(gb,  3);
        frame->fcb_shape[2][0] = get_bits(gb, 10);
        frame->fcb_gain    [2] = get_bits(gb,  4);
        break;
    case RATE_QUANT:
        frame->lsp         [0] = get_bits(gb, 4);
        frame->lsp         [1] = get_bits(gb, 4);
        frame->energy_gain     = get_bits(gb, 8);
        break;
    }
}

static evrc_packet_rate buf_size2bitrate(const int buf_size)
{
    switch (buf_size) {
    case 23: return RATE_FULL;
    case 11: return RATE_HALF;
    case  6: return RATE_QUARTER;
    case  3: return RATE_QUANT;
    case  1: return SILENCE;
    }

    return RATE_ERRS;
}

/**
 * Determine the bitrate from the frame size and/or the first byte of the frame.
 *
 * @param avctx the AV codec context
 * @param buf_size length of the buffer
 * @param buf the bufffer
 *
 * @return the bitrate on success,
 *         RATE_ERRS  if the bitrate cannot be satisfactorily determined
 */
static evrc_packet_rate determine_bitrate(AVCodecContext *avctx,
                                          int *buf_size,
                                          const uint8_t **buf)
{
    evrc_packet_rate bitrate;

    if ((bitrate = buf_size2bitrate(*buf_size)) >= 0) {
        if (bitrate > **buf) {
            EVRCContext *e = avctx->priv_data;
            if (!e->warned_buf_mismatch_bitrate) {
                av_log(avctx, AV_LOG_WARNING,
                       "Claimed bitrate and buffer size mismatch.\n");
                e->warned_buf_mismatch_bitrate = 1;
            }
            bitrate = **buf;
        } else if (bitrate < **buf) {
            av_log(avctx, AV_LOG_ERROR,
                   "Buffer is too small for the claimed bitrate.\n");
            return RATE_ERRS;
        }
        (*buf)++;
        *buf_size -= 1;
    } else if ((bitrate = buf_size2bitrate(*buf_size + 1)) >= 0) {
        av_log(avctx, AV_LOG_DEBUG,
               "Bitrate byte is missing, guessing the bitrate from packet size.\n");
    } else
        return RATE_ERRS;

    return bitrate;
}

static void warn_insufficient_frame_quality(AVCodecContext *avctx,
                                            const char *message)
{
    av_log(avctx, AV_LOG_WARNING, "Frame #%d, %s\n",
           avctx->frame_number, message);
}

/**
 * Initialize the speech codec according to the specification.
 *
 * TIA/IS-127 5.2
 */
static av_cold int evrc_decode_init(AVCodecContext *avctx)
{
    EVRCContext *e = avctx->priv_data;
    int i, n, idx = 0;
    float denom = 2.0 / (2.0 * 8.0 + 1.0);

    avctx->channels       = 1;
    avctx->channel_layout = AV_CH_LAYOUT_MONO;
    avctx->sample_fmt     = AV_SAMPLE_FMT_FLT;

    for (i = 0; i < FILTER_ORDER; i++) {
        e->prev_lspf[i] = (i + 1) * 0.048;
        e->synthesis[i] = 0.0;
    }

    for (i = 0; i < ACB_SIZE; i++)
        e->pitch[i] = e->pitch_back[i] = 0.0;

    e->last_valid_bitrate = RATE_QUANT;
    e->prev_pitch_delay   = 40.0;
    e->fade_scale         = 1.0;
    e->prev_error_flag    = 0;
    e->avg_acb_gain = e->avg_fcb_gain = 0.0;

    for (i = 0; i < 8; i++) {
        float tt = ((float)i - 8.0 / 2.0) / 8.0;

        for (n = -8; n <= 8; n++, idx++) {
            float arg1 = M_PI * 0.9 * (tt - n);
            float arg2 = M_PI * (tt - n);

            e->interpolation_coeffs[idx] = 0.9;
            if (arg1)
                e->interpolation_coeffs[idx] *= (0.54 + 0.46 * cos(arg2 * denom)) *
                                                 sin(arg1) / arg1;
        }
    }

    return 0;
}

/**
 * Decode the 10 vector quantized line spectral pair frequencies from the LSP
 * transmission codes of any bitrate and check for badly received packets.
 *
 * @param e the context
 *
 * @return 0 on success, -1 if the packet is badly received
 *
 * TIA/IS-127 5.2.1, 5.7.1
 */
static int decode_lspf(EVRCContext *e)
{
    const float * const *codebooks = evrc_lspq_codebooks[e->bitrate];
    int i, j, k = 0;

    for (i = 0; i < evrc_lspq_nb_codebooks[e->bitrate]; i++) {
        int row_size = evrc_lspq_codebooks_row_sizes[e->bitrate][i];
        const float *codebook = codebooks[i];

        for (j = 0; j < row_size; j++)
            e->lspf[k++] = codebook[e->frame.lsp[i] * row_size + j];
    }

    // check for monotonic LSPs
    for (i = 1; i < FILTER_ORDER; i++)
        if (e->lspf[i] <= e->lspf[i - 1])
            return -1;

    // check for minimum separation of LSPs at the splits
    for (i = 0, k = 0; i < evrc_lspq_nb_codebooks[e->bitrate] - 1; i++) {
        k += evrc_lspq_codebooks_row_sizes[e->bitrate][i];
        if (e->lspf[k] - e->lspf[k - 1] <= MIN_LSP_SEP)
            return -1;
    }

    return 0;
}

/*
 * Interpolation of LSP parameters.
 *
 * TIA/IS-127 5.2.3.1, 5.7.3.2
 */
static void interpolate_lsp(float *ilsp, const float *lsp,
                            const float *prev, int index)
{
    static const float lsp_interpolation_factors[] = { 0.1667, 0.5, 0.8333 };
    ff_weighted_vector_sumf(ilsp, prev, lsp,
                            1.0 - lsp_interpolation_factors[index],
                            lsp_interpolation_factors[index], FILTER_ORDER);
}

/*
 * Reconstruction of the delay contour.
 *
 * TIA/IS-127 5.2.2.3.2
 */
static void interpolate_delay(float *dst, float current, float prev, int index)
{
    static const float d_interpolation_factors[] = { 0, 0.3313, 0.6625, 1, 1 };
    dst[0] = (1.0 - d_interpolation_factors[index    ]) * prev
                  + d_interpolation_factors[index    ]  * current;
    dst[1] = (1.0 - d_interpolation_factors[index + 1]) * prev
                  + d_interpolation_factors[index + 1]  * current;
    dst[2] = (1.0 - d_interpolation_factors[index + 2]) * prev
                  + d_interpolation_factors[index + 2]  * current;
}

/*
 * Convert the quantized, interpolated line spectral frequencies,
 * to prediction coefficients.
 *
 * TIA/IS-127 5.2.3.2, 4.7.2.2
 */
static void decode_predictor_coeffs(const float *ilspf, float *ilpc)
{
    double lsp[FILTER_ORDER];
    float a[FILTER_ORDER / 2 + 1], b[FILTER_ORDER / 2 + 1];
    float a1[FILTER_ORDER / 2] = { 0 };
    float a2[FILTER_ORDER / 2] = { 0 };
    float b1[FILTER_ORDER / 2] = { 0 };
    float b2[FILTER_ORDER / 2] = { 0 };
    int i, k;

    ff_acelp_lsf2lspd(lsp, ilspf, FILTER_ORDER);

    for (k = 0; k <= FILTER_ORDER; k++) {
        a[0] = k < 2 ? 0.25 : 0;
        b[0] = k < 2 ? k < 1 ? 0.25 : -0.25 : 0;

        for (i = 0; i < FILTER_ORDER / 2; i++) {
            a[i + 1] = a[i] - 2 * lsp[i * 2    ] * a1[i] + a2[i];
            b[i + 1] = b[i] - 2 * lsp[i * 2 + 1] * b1[i] + b2[i];
            a2[i] = a1[i];
            a1[i] = a[i];
            b2[i] = b1[i];
            b1[i] = b[i];
        }

        if (k)
            ilpc[k - 1] = 2.0 * (a[FILTER_ORDER / 2] + b[FILTER_ORDER / 2]);
    }
}

static void bl_intrp(EVRCContext *e, float *ex, float delay)
{
    float *f;
    int offset, i, coef_idx;
    int16_t t;

    offset = lrintf(delay);

    t = (offset - delay + 0.5) * 8.0 + 0.5;
    if (t == 8) {
        t = 0;
        offset--;
    }

    f = ex - offset - 8;

    coef_idx = t * (2 * 8 + 1);

    ex[0] = 0.0;
    for (i = 0; i < 2 * 8 + 1; i++)
        ex[0] += e->interpolation_coeffs[coef_idx + i] * f[i];
}

/*
 * Adaptive codebook excitation.
 *
 * TIA/IS-127 5.2.2.3.3, 4.12.5.2
 */
static void acb_excitation(EVRCContext *e, float *excitation, float gain,
                           const float delay[3], int length)
{
    float denom, locdelay, dpr, invl;
    int i;

    invl = 1.0 / ((float) length);
    dpr = length;

    /* first at-most extra samples */
    denom = (delay[1] - delay[0]) * invl;
    for (i = 0; i < dpr; i++) {
        locdelay = delay[0] + i * denom;
        bl_intrp(e, excitation + i, locdelay);
    }

    denom = (delay[2] - delay[1]) * invl;
    /* interpolation */
    for (i = dpr; i < dpr + 10; i++) {
        locdelay = delay[1] + (i - dpr) * denom;
        bl_intrp(e, excitation + i, locdelay);
    }

    for (i = 0; i < length; i++)
        excitation[i] *= gain;
}

static void decode_8_pulses_35bits(const uint16_t *fixed_index, float *cod)
{
    int i, pos1, pos2, offset;

    offset = (fixed_index[3] >> 9) & 3;

    for (i = 0; i < 3; i++) {
        pos1 = ((fixed_index[i] & 0x7f) / 11) * 5 + ((i + offset) % 5);
        pos2 = ((fixed_index[i] & 0x7f) % 11) * 5 + ((i + offset) % 5);

        cod[pos1] = (fixed_index[i] & 0x80) ? -1.0 : 1.0;

        if (pos2 < pos1)
            cod[pos2]  = -cod[pos1];
        else
            cod[pos2] +=  cod[pos1];
    }

    pos1 = ((fixed_index[3] & 0x7f) / 11) * 5 + ((3 + offset) % 5);
    pos2 = ((fixed_index[3] & 0x7f) % 11) * 5 + ((4 + offset) % 5);

    cod[pos1] = (fixed_index[3] & 0x100) ? -1.0 : 1.0;
    cod[pos2] = (fixed_index[3] & 0x80 ) ? -1.0 : 1.0;
}

static void decode_3_pulses_10bits(uint16_t fixed_index, float *cod)
{
    float sign;
    int pos;

    sign = (fixed_index & 0x200) ? -1.0 : 1.0;

    pos = ((fixed_index        & 0x7) * 7) + 4;
    cod[pos] += sign;
    pos = (((fixed_index >> 3) & 0x7) * 7) + 2;
    cod[pos] -= sign;
    pos = (((fixed_index >> 6) & 0x7) * 7);
    cod[pos] += sign;
}

/*
 * Reconstruction of ACELP fixed codebook excitation for full and half rate.
 *
 * TIA/IS-127 5.2.3.7
 */
static void fcb_excitation(EVRCContext *e, const uint16_t *codebook,
                           float *excitation, float pitch_gain,
                           int pitch_lag, int subframe_size)
{
    int i;

    if (e->bitrate == RATE_FULL)
        decode_8_pulses_35bits(codebook, excitation);
    else
        decode_3_pulses_10bits(*codebook, excitation);

    pitch_gain = av_clipf(pitch_gain, 0.2, 0.9);

    for (i = pitch_lag; i < subframe_size; i++)
        excitation[i] += pitch_gain * excitation[i - pitch_lag];
}

/**
 * Synthesis of the decoder output signal.
 *
 * param[in]     in              input signal
 * param[in]     filter_coeffs   LPC coefficients
 * param[in/out] memory          synthesis filter memory
 * param         buffer_length   amount of data to process
 * param[out]    samples         output samples
 *
 * TIA/IS-127 5.2.3.15, 5.7.3.4
 */
static void synthesis_filter(const float *in, const float *filter_coeffs,
                             float *memory, int buffer_length, float *samples)
{
    int i, j;

    for (i = 0; i < buffer_length; i++) {
        samples[i] = in[i];
        for (j = FILTER_ORDER - 1; j > 0; j--) {
            samples[i] -= filter_coeffs[j] * memory[j];
            memory[j]   = memory[j - 1];
        }
        samples[i] -= filter_coeffs[0] * memory[0];
        memory[0]   = samples[i];
    }
}

static void bandwidth_expansion(float *coeff, const float *inbuf, float gamma)
{
    double fac = gamma;
    int i;

    for (i = 0; i < FILTER_ORDER; i++) {
        coeff[i] = inbuf[i] * fac;
        fac *= gamma;
    }
}

static void residual_filter(float *output, const float *input,
                            const float *coef, float *memory, int length)
{
    float sum;
    int i, j;

    for (i = 0; i < length; i++) {
        sum = input[i];

        for (j = FILTER_ORDER - 1; j > 0; j--) {
            sum      += coef[j] * memory[j];
            memory[j] = memory[j - 1];
        }
        sum += coef[0] * memory[0];
        memory[0] = input[i];
        output[i] = sum;
    }
}

/*
 * TIA/IS-127 Table 5.9.1-1.
 */
static const struct PfCoeff {
    float tilt;
    float ltgain;
    float p1;
    float p2;
} postfilter_coeffs[5] = {
    { 0.0 , 0.0 , 0.0 , 0.0  },
    { 0.0 , 0.0 , 0.57, 0.57 },
    { 0.0 , 0.0 , 0.0 , 0.0  },
    { 0.35, 0.50, 0.50, 0.75 },
    { 0.20, 0.50, 0.57, 0.75 },
};

/*
 * Adaptive postfilter.
 *
 * TIA/IS-127 5.9
 */
static void postfilter(EVRCContext *e, float *in, const float *coeff,
                       float *out, int idx, const struct PfCoeff *pfc,
                       int length)
{
    float wcoef1[FILTER_ORDER], wcoef2[FILTER_ORDER],
          scratch[SUBFRAME_SIZE], temp[SUBFRAME_SIZE],
          mem[SUBFRAME_SIZE];
    float sum1 = 0.0, sum2 = 0.0, gamma, gain;
    float tilt = pfc->tilt;
    int i, n, best;

    bandwidth_expansion(wcoef1, coeff, pfc->p1);
    bandwidth_expansion(wcoef2, coeff, pfc->p2);

    /* Tilt compensation filter, TIA/IS-127 5.9.1 */
    for (i = 0; i < length - 1; i++)
        sum2 += in[i] * in[i + 1];
    if (sum2 < 0.0)
        tilt = 0.0;

    for (i = 0; i < length; i++) {
        scratch[i] = in[i] - tilt * e->last;
        e->last = in[i];
    }

    /* Short term residual filter, TIA/IS-127 5.9.2 */
    residual_filter(&e->postfilter_residual[ACB_SIZE], scratch, wcoef1, e->postfilter_fir, length);

    /* Long term postfilter */
    best = idx;
    for (i = FFMIN(MIN_DELAY, idx - 3); i <= FFMAX(MAX_DELAY, idx + 3); i++) {
        for (n = ACB_SIZE, sum2 = 0; n < ACB_SIZE + length; n++)
            sum2 += e->postfilter_residual[n] * e->postfilter_residual[n - i];
        if (sum2 > sum1) {
            sum1 = sum2;
            best = i;
        }
    }

    for (i = ACB_SIZE, sum1 = 0; i < ACB_SIZE + length; i++)
        sum1 += e->postfilter_residual[i - best] * e->postfilter_residual[i - best];
    for (i = ACB_SIZE, sum2 = 0; i < ACB_SIZE + length; i++)
        sum2 += e->postfilter_residual[i] * e->postfilter_residual[i - best];

    if (sum2 * sum1 == 0 || e->bitrate == RATE_QUANT) {
        memcpy(temp, e->postfilter_residual + ACB_SIZE, length * sizeof(float));
    } else {
        gamma = sum2 / sum1;
        if (gamma < 0.5)
            memcpy(temp, e->postfilter_residual + ACB_SIZE, length * sizeof(float));
        else {
            gamma = FFMIN(gamma, 1.0);

            for (i = 0; i < length; i++) {
                temp[i] = e->postfilter_residual[ACB_SIZE + i] + gamma *
                    pfc->ltgain * e->postfilter_residual[ACB_SIZE + i - best];
            }
        }
    }

    memcpy(scratch, temp, length * sizeof(float));
    memcpy(mem, e->postfilter_iir, FILTER_ORDER * sizeof(float));
    synthesis_filter(scratch, wcoef2, mem, length, scratch);

    /* Gain computation, TIA/IS-127 5.9.4-2 */
    for (i = 0, sum1 = 0, sum2 = 0; i < length; i++) {
        sum1 += in[i] * in[i];
        sum2 += scratch[i] * scratch[i];
    }
    gain = sum2 ? sqrt(sum1 / sum2) : 1.0;

    for (i = 0; i < length; i++)
        temp[i] *= gain;

    /* Short term postfilter */
    synthesis_filter(temp, wcoef2, e->postfilter_iir, length, out);

    memmove(e->postfilter_residual,
           e->postfilter_residual + length, ACB_SIZE * sizeof(float));
}

static void frame_erasure(EVRCContext *e, float *samples)
{
    float ilspf[FILTER_ORDER], ilpc[FILTER_ORDER], idelay[NB_SUBFRAMES],
          tmp[SUBFRAME_SIZE + 6], f;
    int i, j;

    for (i = 0; i < FILTER_ORDER; i++) {
        if (e->bitrate != RATE_QUANT)
            e->lspf[i] = e->prev_lspf[i] * 0.875 + 0.125 * (i + 1) * 0.048;
        else
            e->lspf[i] = e->prev_lspf[i];
    }

    if (e->prev_error_flag)
        e->avg_acb_gain *= 0.75;
    if (e->bitrate == RATE_FULL)
        memcpy(e->pitch_back, e->pitch, ACB_SIZE * sizeof(float));
    if (e->last_valid_bitrate == RATE_QUANT)
        e->bitrate = RATE_QUANT;
    else
        e->bitrate = RATE_FULL;

    if (e->bitrate == RATE_FULL || e->bitrate == RATE_HALF) {
        e->pitch_delay = e->prev_pitch_delay;
    } else {
        float sum = 0;

        idelay[0] = idelay[1] = idelay[2] = MIN_DELAY;

        for (i = 0; i < NB_SUBFRAMES; i++)
            sum += evrc_energy_quant[e->prev_energy_gain][i];
        sum /= (float) NB_SUBFRAMES;
        sum  = pow(10, sum);
        for (i = 0; i < NB_SUBFRAMES; i++)
            e->energy_vector[i] = sum;
    }

    if (fabs(e->pitch_delay - e->prev_pitch_delay) > 15)
        e->prev_pitch_delay = e->pitch_delay;

    for (i = 0; i < NB_SUBFRAMES; i++) {
        int subframe_size = subframe_sizes[i];
        int pitch_lag;

        interpolate_lsp(ilspf, e->lspf, e->prev_lspf, i);

        if (e->bitrate != RATE_QUANT) {
            if (e->avg_acb_gain < 0.3) {
                idelay[0] = estimation_delay[i];
                idelay[1] = estimation_delay[i + 1];
                idelay[2] = estimation_delay[i + 2];
            } else {
                interpolate_delay(idelay, e->pitch_delay, e->prev_pitch_delay, i);
            }
        }

        pitch_lag = lrintf((idelay[1] + idelay[0]) / 2.0);
        decode_predictor_coeffs(ilspf, ilpc);

        if (e->bitrate != RATE_QUANT) {
            acb_excitation(e, e->pitch + ACB_SIZE,
                           e->avg_acb_gain, idelay, subframe_size);
            for (j = 0; j < subframe_size; j++)
                e->pitch[ACB_SIZE + j] *= e->fade_scale;
            e->fade_scale = FFMAX(e->fade_scale - 0.05, 0.0);
        } else {
            for (j = 0; j < subframe_size; j++)
                e->pitch[ACB_SIZE + j] = e->energy_vector[i];
        }

        memmove(e->pitch, e->pitch + subframe_size, ACB_SIZE * sizeof(float));

        if (e->bitrate != RATE_QUANT && e->avg_acb_gain < 0.4) {
            f = 0.1 * e->avg_fcb_gain;
            for (j = 0; j < subframe_size; j++)
                e->pitch[ACB_SIZE + j] += f;
        } else if (e->bitrate == RATE_QUANT) {
            for (j = 0; j < subframe_size; j++)
                e->pitch[ACB_SIZE + j] = e->energy_vector[i];
        }

        synthesis_filter(e->pitch + ACB_SIZE, ilpc,
                         e->synthesis, subframe_size, tmp);
        postfilter(e, tmp, ilpc, samples, pitch_lag,
                   &postfilter_coeffs[e->bitrate], subframe_size);

        samples += subframe_size;
    }
}

static int evrc_decode_frame(AVCodecContext *avctx, void *data,
                             int *got_frame_ptr, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    AVFrame *frame     = data;
    EVRCContext *e     = avctx->priv_data;
    int buf_size       = avpkt->size;
    float ilspf[FILTER_ORDER], ilpc[FILTER_ORDER], idelay[NB_SUBFRAMES];
    float *samples;
    int   i, j, ret, error_flag = 0;

    frame->nb_samples = 160;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;
    samples = (float *)frame->data[0];

    if ((e->bitrate = determine_bitrate(avctx, &buf_size, &buf)) == RATE_ERRS) {
        warn_insufficient_frame_quality(avctx, "bitrate cannot be determined.");
        goto erasure;
    }
    if (e->bitrate <= SILENCE || e->bitrate == RATE_QUARTER)
        goto erasure;
    if (e->bitrate == RATE_QUANT && e->last_valid_bitrate == RATE_FULL
                                 && !e->prev_error_flag)
        goto erasure;

    if ((ret = init_get_bits8(&e->gb, buf, buf_size)) < 0)
        return ret;
    memset(&e->frame, 0, sizeof(EVRCAFrame));

    unpack_frame(e);

    if (e->bitrate != RATE_QUANT) {
        uint8_t *p = (uint8_t *) &e->frame;
        for (i = 0; i < sizeof(EVRCAFrame); i++) {
            if (p[i])
                break;
        }
        if (i == sizeof(EVRCAFrame))
            goto erasure;
    } else if (e->frame.lsp[0] == 0xf &&
               e->frame.lsp[1] == 0xf &&
               e->frame.energy_gain == 0xff) {
        goto erasure;
    }

    if (decode_lspf(e) < 0)
        goto erasure;

    if (e->bitrate == RATE_FULL || e->bitrate == RATE_HALF) {
        /* Pitch delay parameter checking as per TIA/IS-127 5.1.5.1 */
        if (e->frame.pitch_delay > MAX_DELAY - MIN_DELAY)
            goto erasure;

        e->pitch_delay = e->frame.pitch_delay + MIN_DELAY;

        /* Delay diff parameter checking as per TIA/IS-127 5.1.5.2 */
        if (e->frame.delay_diff) {
            int p = e->pitch_delay - e->frame.delay_diff + 16;
            if (p < MIN_DELAY || p > MAX_DELAY)
                goto erasure;
        }

        /* Delay contour reconstruction as per TIA/IS-127 5.2.2.2 */
        if (e->frame.delay_diff &&
            e->bitrate == RATE_FULL && e->prev_error_flag) {
            float delay;

            memcpy(e->pitch, e->pitch_back, ACB_SIZE * sizeof(float));

            delay = e->prev_pitch_delay;
            e->prev_pitch_delay = delay - e->frame.delay_diff + 16.0;

            if (fabs(e->pitch_delay - delay) > 15)
                delay = e->pitch_delay;

            for (i = 0; i < NB_SUBFRAMES; i++) {
                int subframe_size = subframe_sizes[i];

                interpolate_delay(idelay, delay, e->prev_pitch_delay, i);
                acb_excitation(e, e->pitch + ACB_SIZE, e->avg_acb_gain, idelay, subframe_size);
                memmove(e->pitch, e->pitch + subframe_size, ACB_SIZE * sizeof(float));
            }
        }

        /* Smoothing of the decoded delay as per TIA/IS-127 5.2.2.5 */
        if (fabs(e->pitch_delay - e->prev_pitch_delay) > 15)
            e->prev_pitch_delay = e->pitch_delay;

        e->avg_acb_gain = e->avg_fcb_gain = 0.0;
    } else {
        idelay[0] = idelay[1] = idelay[2] = MIN_DELAY;

        /* Decode frame energy vectors as per TIA/IS-127 5.7.2 */
        for (i = 0; i < NB_SUBFRAMES; i++)
            e->energy_vector[i] = pow(10, evrc_energy_quant[e->frame.energy_gain][i]);
        e->prev_energy_gain = e->frame.energy_gain;
    }

    for (i = 0; i < NB_SUBFRAMES; i++) {
        float tmp[SUBFRAME_SIZE + 6] = { 0 };
        int subframe_size = subframe_sizes[i];
        int pitch_lag;

        interpolate_lsp(ilspf, e->lspf, e->prev_lspf, i);

        if (e->bitrate != RATE_QUANT)
            interpolate_delay(idelay, e->pitch_delay, e->prev_pitch_delay, i);

        pitch_lag = lrintf((idelay[1] + idelay[0]) / 2.0);
        decode_predictor_coeffs(ilspf, ilpc);

        /* Bandwidth expansion as per TIA/IS-127 5.2.3.3 */
        if (e->frame.lpc_flag && e->prev_error_flag)
            bandwidth_expansion(ilpc, ilpc, 0.75);

        if (e->bitrate != RATE_QUANT) {
            float acb_sum, f;

            f = exp((e->bitrate == RATE_HALF ? 0.5 : 0.25)
                         * (e->frame.fcb_gain[i] + 1));
            acb_sum = pitch_gain_vq[e->frame.acb_gain[i]];
            e->avg_acb_gain += acb_sum / NB_SUBFRAMES;
            e->avg_fcb_gain += f / NB_SUBFRAMES;

            acb_excitation(e, e->pitch + ACB_SIZE,
                           acb_sum, idelay, subframe_size);
            fcb_excitation(e, e->frame.fcb_shape[i], tmp,
                           acb_sum, pitch_lag, subframe_size);

            /* Total excitation generation as per TIA/IS-127 5.2.3.9 */
            for (j = 0; j < subframe_size; j++)
                e->pitch[ACB_SIZE + j] += f * tmp[j];
            e->fade_scale = FFMIN(e->fade_scale + 0.2, 1.0);
        } else {
            for (j = 0; j < subframe_size; j++)
                e->pitch[ACB_SIZE + j] = e->energy_vector[i];
        }

        memmove(e->pitch, e->pitch + subframe_size, ACB_SIZE * sizeof(float));

        synthesis_filter(e->pitch + ACB_SIZE, ilpc,
                         e->synthesis, subframe_size,
                         e->postfilter ? tmp : samples);
        if (e->postfilter)
            postfilter(e, tmp, ilpc, samples, pitch_lag,
                       &postfilter_coeffs[e->bitrate], subframe_size);

        samples += subframe_size;
    }

    if (error_flag) {
erasure:
        error_flag = 1;
        av_log(avctx, AV_LOG_WARNING, "frame erasure\n");
        frame_erasure(e, samples);
    }

    memcpy(e->prev_lspf, e->lspf, sizeof(e->prev_lspf));
    e->prev_error_flag    = error_flag;
    e->last_valid_bitrate = e->bitrate;

    if (e->bitrate != RATE_QUANT)
        e->prev_pitch_delay = e->pitch_delay;

    samples = (float *)frame->data[0];
    for (i = 0; i < 160; i++)
        samples[i] /= 32768;

    *got_frame_ptr   = 1;

    return avpkt->size;
}

#define OFFSET(x) offsetof(EVRCContext, x)
#define AD AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    { "postfilter", "enable postfilter", OFFSET(postfilter), AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, AD },
    { NULL }
};

static const AVClass evrcdec_class = {
    .class_name = "evrc",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_evrc_decoder = {
    .name           = "evrc",
    .long_name      = NULL_IF_CONFIG_SMALL("EVRC (Enhanced Variable Rate Codec)"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_EVRC,
    .init           = evrc_decode_init,
    .decode         = evrc_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
    .priv_data_size = sizeof(EVRCContext),
    .priv_class     = &evrcdec_class,
};
