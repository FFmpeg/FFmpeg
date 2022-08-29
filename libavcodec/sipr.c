/*
 * SIPR / ACELP.NET decoder
 *
 * Copyright (c) 2008 Vladimir Voroshilov
 * Copyright (c) 2009 Vitor Sessak
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

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "libavutil/channel_layout.h"
#include "libavutil/float_dsp.h"
#include "libavutil/mathematics.h"

#define BITSTREAM_READER_LE
#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"
#include "lsp.h"
#include "acelp_vectors.h"
#include "acelp_pitch_delay.h"
#include "acelp_filters.h"
#include "celp_filters.h"

#define MAX_SUBFRAME_COUNT   5

#include "sipr.h"
#include "siprdata.h"

typedef struct SiprModeParam {
    const char *mode_name;
    uint16_t bits_per_frame;
    uint8_t subframe_count;
    uint8_t frames_per_packet;
    float pitch_sharp_factor;

    /* bitstream parameters */
    uint8_t number_of_fc_indexes;
    uint8_t ma_predictor_bits;  ///< size in bits of the switched MA predictor

    /** size in bits of the i-th stage vector of quantizer */
    uint8_t vq_indexes_bits[5];

    /** size in bits of the adaptive-codebook index for every subframe */
    uint8_t pitch_delay_bits[5];

    uint8_t gp_index_bits;
    uint8_t fc_index_bits[10]; ///< size in bits of the fixed codebook indexes
    uint8_t gc_index_bits;     ///< size in bits of the gain  codebook indexes
} SiprModeParam;

static const SiprModeParam modes[MODE_COUNT] = {
    [MODE_16k] = {
        .mode_name          = "16k",
        .bits_per_frame     = 160,
        .subframe_count     = SUBFRAME_COUNT_16k,
        .frames_per_packet  = 1,
        .pitch_sharp_factor = 0.00,

        .number_of_fc_indexes = 10,
        .ma_predictor_bits    = 1,
        .vq_indexes_bits      = {7, 8, 7, 7, 7},
        .pitch_delay_bits     = {9, 6},
        .gp_index_bits        = 4,
        .fc_index_bits        = {4, 5, 4, 5, 4, 5, 4, 5, 4, 5},
        .gc_index_bits        = 5
    },

    [MODE_8k5] = {
        .mode_name          = "8k5",
        .bits_per_frame     = 152,
        .subframe_count     = 3,
        .frames_per_packet  = 1,
        .pitch_sharp_factor = 0.8,

        .number_of_fc_indexes = 3,
        .ma_predictor_bits    = 0,
        .vq_indexes_bits      = {6, 7, 7, 7, 5},
        .pitch_delay_bits     = {8, 5, 5},
        .gp_index_bits        = 0,
        .fc_index_bits        = {9, 9, 9},
        .gc_index_bits        = 7
    },

    [MODE_6k5] = {
        .mode_name          = "6k5",
        .bits_per_frame     = 232,
        .subframe_count     = 3,
        .frames_per_packet  = 2,
        .pitch_sharp_factor = 0.8,

        .number_of_fc_indexes = 3,
        .ma_predictor_bits    = 0,
        .vq_indexes_bits      = {6, 7, 7, 7, 5},
        .pitch_delay_bits     = {8, 5, 5},
        .gp_index_bits        = 0,
        .fc_index_bits        = {5, 5, 5},
        .gc_index_bits        = 7
    },

    [MODE_5k0] = {
        .mode_name          = "5k0",
        .bits_per_frame     = 296,
        .subframe_count     = 5,
        .frames_per_packet  = 2,
        .pitch_sharp_factor = 0.85,

        .number_of_fc_indexes = 1,
        .ma_predictor_bits    = 0,
        .vq_indexes_bits      = {6, 7, 7, 7, 5},
        .pitch_delay_bits     = {8, 5, 8, 5, 5},
        .gp_index_bits        = 0,
        .fc_index_bits        = {10},
        .gc_index_bits        = 7
    }
};

const float ff_pow_0_5[] = {
    1.0/(1 <<  1), 1.0/(1 <<  2), 1.0/(1 <<  3), 1.0/(1 <<  4),
    1.0/(1 <<  5), 1.0/(1 <<  6), 1.0/(1 <<  7), 1.0/(1 <<  8),
    1.0/(1 <<  9), 1.0/(1 << 10), 1.0/(1 << 11), 1.0/(1 << 12),
    1.0/(1 << 13), 1.0/(1 << 14), 1.0/(1 << 15), 1.0/(1 << 16)
};

static void dequant(float *out, const int *idx, const float * const cbs[])
{
    int i;
    int stride  = 2;
    int num_vec = 5;

    for (i = 0; i < num_vec; i++)
        memcpy(out + stride*i, cbs[i] + stride*idx[i], stride*sizeof(float));

}

static void lsf_decode_fp(float *lsfnew, float *lsf_history,
                          const SiprParameters *parm)
{
    int i;
    float lsf_tmp[LP_FILTER_ORDER];

    dequant(lsf_tmp, parm->vq_indexes, lsf_codebooks);

    for (i = 0; i < LP_FILTER_ORDER; i++)
        lsfnew[i] = lsf_history[i] * 0.33 + lsf_tmp[i] + mean_lsf[i];

    ff_sort_nearly_sorted_floats(lsfnew, LP_FILTER_ORDER - 1);

    /* Note that a minimum distance is not enforced between the last value and
       the previous one, contrary to what is done in ff_acelp_reorder_lsf() */
    ff_set_min_dist_lsf(lsfnew, LSFQ_DIFF_MIN, LP_FILTER_ORDER - 1);
    lsfnew[9] = FFMIN(lsfnew[LP_FILTER_ORDER - 1], 1.3 * M_PI);

    memcpy(lsf_history, lsf_tmp, LP_FILTER_ORDER * sizeof(*lsf_history));

    for (i = 0; i < LP_FILTER_ORDER - 1; i++)
        lsfnew[i] = cos(lsfnew[i]);
    lsfnew[LP_FILTER_ORDER - 1] *= 6.153848 / M_PI;
}

/** Apply pitch lag to the fixed vector (AMR section 6.1.2). */
static void pitch_sharpening(int pitch_lag_int, float beta,
                             float *fixed_vector)
{
    int i;

    for (i = pitch_lag_int; i < SUBFR_SIZE; i++)
        fixed_vector[i] += beta * fixed_vector[i - pitch_lag_int];
}

/**
 * Extract decoding parameters from the input bitstream.
 * @param parms          parameters structure
 * @param pgb            pointer to initialized GetBitContext structure
 */
static void decode_parameters(SiprParameters* parms, GetBitContext *pgb,
                              const SiprModeParam *p)
{
    int i, j;

    if (p->ma_predictor_bits)
        parms->ma_pred_switch       = get_bits(pgb, p->ma_predictor_bits);

    for (i = 0; i < 5; i++)
        parms->vq_indexes[i]        = get_bits(pgb, p->vq_indexes_bits[i]);

    for (i = 0; i < p->subframe_count; i++) {
        parms->pitch_delay[i]       = get_bits(pgb, p->pitch_delay_bits[i]);
        if (p->gp_index_bits)
            parms->gp_index[i]      = get_bits(pgb, p->gp_index_bits);

        for (j = 0; j < p->number_of_fc_indexes; j++)
            parms->fc_indexes[i][j] = get_bits(pgb, p->fc_index_bits[j]);

        parms->gc_index[i]          = get_bits(pgb, p->gc_index_bits);
    }
}

static void sipr_decode_lp(float *lsfnew, const float *lsfold, float *Az,
                           int num_subfr)
{
    double lsfint[LP_FILTER_ORDER];
    int i,j;
    float t, t0 = 1.0 / num_subfr;

    t = t0 * 0.5;
    for (i = 0; i < num_subfr; i++) {
        for (j = 0; j < LP_FILTER_ORDER; j++)
            lsfint[j] = lsfold[j] * (1 - t) + t * lsfnew[j];

        ff_amrwb_lsp2lpc(lsfint, Az, LP_FILTER_ORDER);
        Az += LP_FILTER_ORDER;
        t += t0;
    }
}

/**
 * Evaluate the adaptive impulse response.
 */
static void eval_ir(const float *Az, int pitch_lag, float *freq,
                    float pitch_sharp_factor)
{
    float tmp1[SUBFR_SIZE+1], tmp2[LP_FILTER_ORDER+1];
    int i;

    tmp1[0] = 1.0;
    for (i = 0; i < LP_FILTER_ORDER; i++) {
        tmp1[i+1] = Az[i] * ff_pow_0_55[i];
        tmp2[i  ] = Az[i] * ff_pow_0_7 [i];
    }
    memset(tmp1 + 11, 0, 37 * sizeof(float));

    ff_celp_lp_synthesis_filterf(freq, tmp2, tmp1, SUBFR_SIZE,
                                 LP_FILTER_ORDER);

    pitch_sharpening(pitch_lag, pitch_sharp_factor, freq);
}

/**
 * Evaluate the convolution of a vector with a sparse vector.
 */
static void convolute_with_sparse(float *out, const AMRFixed *pulses,
                                  const float *shape, int length)
{
    int i, j;

    memset(out, 0, length*sizeof(float));
    for (i = 0; i < pulses->n; i++)
        for (j = pulses->x[i]; j < length; j++)
            out[j] += pulses->y[i] * shape[j - pulses->x[i]];
}

/**
 * Apply postfilter, very similar to AMR one.
 */
static void postfilter_5k0(SiprContext *ctx, const float *lpc, float *samples)
{
    float buf[SUBFR_SIZE + LP_FILTER_ORDER];
    float *pole_out = buf + LP_FILTER_ORDER;
    float lpc_n[LP_FILTER_ORDER];
    float lpc_d[LP_FILTER_ORDER];
    int i;

    for (i = 0; i < LP_FILTER_ORDER; i++) {
        lpc_d[i] = lpc[i] * ff_pow_0_75[i];
        lpc_n[i] = lpc[i] * ff_pow_0_5 [i];
    };

    memcpy(pole_out - LP_FILTER_ORDER, ctx->postfilter_mem,
           LP_FILTER_ORDER*sizeof(float));

    ff_celp_lp_synthesis_filterf(pole_out, lpc_d, samples, SUBFR_SIZE,
                                 LP_FILTER_ORDER);

    memcpy(ctx->postfilter_mem, pole_out + SUBFR_SIZE - LP_FILTER_ORDER,
           LP_FILTER_ORDER*sizeof(float));

    ff_tilt_compensation(&ctx->tilt_mem, 0.4, pole_out, SUBFR_SIZE);

    memcpy(pole_out - LP_FILTER_ORDER, ctx->postfilter_mem5k0,
           LP_FILTER_ORDER*sizeof(*pole_out));

    memcpy(ctx->postfilter_mem5k0, pole_out + SUBFR_SIZE - LP_FILTER_ORDER,
           LP_FILTER_ORDER*sizeof(*pole_out));

    ff_celp_lp_zero_synthesis_filterf(samples, lpc_n, pole_out, SUBFR_SIZE,
                                      LP_FILTER_ORDER);

}

static void decode_fixed_sparse(AMRFixed *fixed_sparse, const int16_t *pulses,
                                SiprMode mode, int low_gain)
{
    int i;

    switch (mode) {
    case MODE_6k5:
        for (i = 0; i < 3; i++) {
            fixed_sparse->x[i] = 3 * (pulses[i] & 0xf) + i;
            fixed_sparse->y[i] = pulses[i] & 0x10 ? -1 : 1;
        }
        fixed_sparse->n = 3;
        break;
    case MODE_8k5:
        for (i = 0; i < 3; i++) {
            fixed_sparse->x[2*i    ] = 3 * ((pulses[i] >> 4) & 0xf) + i;
            fixed_sparse->x[2*i + 1] = 3 * ( pulses[i]       & 0xf) + i;

            fixed_sparse->y[2*i    ] = (pulses[i] & 0x100) ? -1.0: 1.0;

            fixed_sparse->y[2*i + 1] =
                (fixed_sparse->x[2*i + 1] < fixed_sparse->x[2*i]) ?
                -fixed_sparse->y[2*i    ] : fixed_sparse->y[2*i];
        }

        fixed_sparse->n = 6;
        break;
    case MODE_5k0:
    default:
        if (low_gain) {
            int offset = (pulses[0] & 0x200) ? 2 : 0;
            int val = pulses[0];

            for (i = 0; i < 3; i++) {
                int index = (val & 0x7) * 6 + 4 - i*2;

                fixed_sparse->y[i] = (offset + index) & 0x3 ? -1 : 1;
                fixed_sparse->x[i] = index;

                val >>= 3;
            }
            fixed_sparse->n = 3;
        } else {
            int pulse_subset = (pulses[0] >> 8) & 1;

            fixed_sparse->x[0] = ((pulses[0] >> 4) & 15) * 3 + pulse_subset;
            fixed_sparse->x[1] = ( pulses[0]       & 15) * 3 + pulse_subset + 1;

            fixed_sparse->y[0] = pulses[0] & 0x200 ? -1 : 1;
            fixed_sparse->y[1] = -fixed_sparse->y[0];
            fixed_sparse->n = 2;
        }
        break;
    }
}

static void decode_frame(SiprContext *ctx, SiprParameters *params,
                         float *out_data)
{
    int i, j;
    int subframe_count = modes[ctx->mode].subframe_count;
    int frame_size = subframe_count * SUBFR_SIZE;
    float Az[LP_FILTER_ORDER * MAX_SUBFRAME_COUNT];
    float *excitation;
    float ir_buf[SUBFR_SIZE + LP_FILTER_ORDER];
    float lsf_new[LP_FILTER_ORDER];
    float *impulse_response = ir_buf + LP_FILTER_ORDER;
    float *synth = ctx->synth_buf + 16; // 16 instead of LP_FILTER_ORDER for
                                        // memory alignment
    int t0_first = 0;
    AMRFixed fixed_cb;

    memset(ir_buf, 0, LP_FILTER_ORDER * sizeof(float));
    lsf_decode_fp(lsf_new, ctx->lsf_history, params);

    sipr_decode_lp(lsf_new, ctx->lsp_history, Az, subframe_count);

    memcpy(ctx->lsp_history, lsf_new, LP_FILTER_ORDER * sizeof(float));

    excitation = ctx->excitation + PITCH_DELAY_MAX + L_INTERPOL;

    for (i = 0; i < subframe_count; i++) {
        float *pAz = Az + i*LP_FILTER_ORDER;
        float fixed_vector[SUBFR_SIZE];
        int T0,T0_frac;
        float pitch_gain, gain_code, avg_energy;

        ff_decode_pitch_lag(&T0, &T0_frac, params->pitch_delay[i], t0_first, i,
                            ctx->mode == MODE_5k0, 6);

        if (i == 0 || (i == 2 && ctx->mode == MODE_5k0))
            t0_first = T0;

        ff_acelp_interpolatef(excitation, excitation - T0 + (T0_frac <= 0),
                              ff_b60_sinc, 6,
                              2 * ((2 + T0_frac)%3 + 1), LP_FILTER_ORDER,
                              SUBFR_SIZE);

        decode_fixed_sparse(&fixed_cb, params->fc_indexes[i], ctx->mode,
                            ctx->past_pitch_gain < 0.8);

        eval_ir(pAz, T0, impulse_response, modes[ctx->mode].pitch_sharp_factor);

        convolute_with_sparse(fixed_vector, &fixed_cb, impulse_response,
                              SUBFR_SIZE);

        avg_energy = (0.01 + avpriv_scalarproduct_float_c(fixed_vector,
                                                          fixed_vector,
                                                          SUBFR_SIZE)) /
                     SUBFR_SIZE;

        ctx->past_pitch_gain = pitch_gain = gain_cb[params->gc_index[i]][0];

        gain_code = ff_amr_set_fixed_gain(gain_cb[params->gc_index[i]][1],
                                          avg_energy, ctx->energy_history,
                                          34 - 15.0/(0.05*M_LN10/M_LN2),
                                          pred);

        ff_weighted_vector_sumf(excitation, excitation, fixed_vector,
                                pitch_gain, gain_code, SUBFR_SIZE);

        pitch_gain *= 0.5 * pitch_gain;
        pitch_gain = FFMIN(pitch_gain, 0.4);

        ctx->gain_mem = 0.7 * ctx->gain_mem + 0.3 * pitch_gain;
        ctx->gain_mem = FFMIN(ctx->gain_mem, pitch_gain);
        gain_code *= ctx->gain_mem;

        for (j = 0; j < SUBFR_SIZE; j++)
            fixed_vector[j] = excitation[j] - gain_code * fixed_vector[j];

        if (ctx->mode == MODE_5k0) {
            postfilter_5k0(ctx, pAz, fixed_vector);

            ff_celp_lp_synthesis_filterf(ctx->postfilter_syn5k0 + LP_FILTER_ORDER + i*SUBFR_SIZE,
                                         pAz, excitation, SUBFR_SIZE,
                                         LP_FILTER_ORDER);
        }

        ff_celp_lp_synthesis_filterf(synth + i*SUBFR_SIZE, pAz, fixed_vector,
                                     SUBFR_SIZE, LP_FILTER_ORDER);

        excitation += SUBFR_SIZE;
    }

    memcpy(synth - LP_FILTER_ORDER, synth + frame_size - LP_FILTER_ORDER,
           LP_FILTER_ORDER * sizeof(float));

    if (ctx->mode == MODE_5k0) {
        for (i = 0; i < subframe_count; i++) {
            float energy = avpriv_scalarproduct_float_c(ctx->postfilter_syn5k0 + LP_FILTER_ORDER + i * SUBFR_SIZE,
                                                        ctx->postfilter_syn5k0 + LP_FILTER_ORDER + i * SUBFR_SIZE,
                                                        SUBFR_SIZE);
            ff_adaptive_gain_control(&synth[i * SUBFR_SIZE],
                                     &synth[i * SUBFR_SIZE], energy,
                                     SUBFR_SIZE, 0.9, &ctx->postfilter_agc);
        }

        memcpy(ctx->postfilter_syn5k0, ctx->postfilter_syn5k0 + frame_size,
               LP_FILTER_ORDER*sizeof(float));
    }
    memmove(ctx->excitation, excitation - PITCH_DELAY_MAX - L_INTERPOL,
           (PITCH_DELAY_MAX + L_INTERPOL) * sizeof(float));

    ff_acelp_apply_order_2_transfer_function(out_data, synth,
                                             (const float[2]) {-1.99997   , 1.000000000},
                                             (const float[2]) {-1.93307352, 0.935891986},
                                             0.939805806,
                                             ctx->highpass_filt_mem,
                                             frame_size);
}

static av_cold int sipr_decoder_init(AVCodecContext * avctx)
{
    SiprContext *ctx = avctx->priv_data;
    int i;

    switch (avctx->block_align) {
    case 20: ctx->mode = MODE_16k; break;
    case 19: ctx->mode = MODE_8k5; break;
    case 29: ctx->mode = MODE_6k5; break;
    case 37: ctx->mode = MODE_5k0; break;
    default:
        if      (avctx->bit_rate > 12200) ctx->mode = MODE_16k;
        else if (avctx->bit_rate > 7500 ) ctx->mode = MODE_8k5;
        else if (avctx->bit_rate > 5750 ) ctx->mode = MODE_6k5;
        else                              ctx->mode = MODE_5k0;
        av_log(avctx, AV_LOG_WARNING,
               "Invalid block_align: %d. Mode %s guessed based on bitrate: %"PRId64"\n",
               avctx->block_align, modes[ctx->mode].mode_name, avctx->bit_rate);
    }

    av_log(avctx, AV_LOG_DEBUG, "Mode: %s\n", modes[ctx->mode].mode_name);

    if (ctx->mode == MODE_16k) {
        ff_sipr_init_16k(ctx);
        ctx->decode_frame = ff_sipr_decode_frame_16k;
    } else {
        ctx->decode_frame = decode_frame;
    }

    for (i = 0; i < LP_FILTER_ORDER; i++)
        ctx->lsp_history[i] = cos((i+1) * M_PI / (LP_FILTER_ORDER + 1));

    for (i = 0; i < 4; i++)
        ctx->energy_history[i] = -14;

    av_channel_layout_uninit(&avctx->ch_layout);
    avctx->ch_layout      = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
    avctx->sample_fmt     = AV_SAMPLE_FMT_FLT;

    return 0;
}

static int sipr_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                             int *got_frame_ptr, AVPacket *avpkt)
{
    SiprContext *ctx = avctx->priv_data;
    const uint8_t *buf=avpkt->data;
    SiprParameters parm;
    const SiprModeParam *mode_par = &modes[ctx->mode];
    GetBitContext gb;
    float *samples;
    int subframe_size = ctx->mode == MODE_16k ? L_SUBFR_16k : SUBFR_SIZE;
    int i, ret;

    ctx->avctx = avctx;
    if (avpkt->size < (mode_par->bits_per_frame >> 3)) {
        av_log(avctx, AV_LOG_ERROR,
               "Error processing packet: packet size (%d) too small\n",
               avpkt->size);
        return AVERROR_INVALIDDATA;
    }

    /* get output buffer */
    frame->nb_samples = mode_par->frames_per_packet * subframe_size *
                        mode_par->subframe_count;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;
    samples = (float *)frame->data[0];

    init_get_bits(&gb, buf, mode_par->bits_per_frame);

    for (i = 0; i < mode_par->frames_per_packet; i++) {
        decode_parameters(&parm, &gb, mode_par);

        ctx->decode_frame(ctx, &parm, samples);

        samples += subframe_size * mode_par->subframe_count;
    }

    *got_frame_ptr = 1;

    return mode_par->bits_per_frame >> 3;
}

const FFCodec ff_sipr_decoder = {
    .p.name         = "sipr",
    CODEC_LONG_NAME("RealAudio SIPR / ACELP.NET"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_SIPR,
    .priv_data_size = sizeof(SiprContext),
    .init           = sipr_decoder_init,
    FF_CODEC_DECODE_CB(sipr_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_CHANNEL_CONF,
};
