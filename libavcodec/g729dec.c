/*
 * G.729, G729 Annex D decoders
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
#include <string.h>

#include "avcodec.h"
#include "libavutil/avutil.h"
#include "get_bits.h"
#include "dsputil.h"
#include "internal.h"


#include "g729.h"
#include "lsp.h"
#include "celp_math.h"
#include "celp_filters.h"
#include "acelp_filters.h"
#include "acelp_pitch_delay.h"
#include "acelp_vectors.h"
#include "g729data.h"
#include "g729postfilter.h"

/**
 * minimum quantized LSF value (3.2.4)
 * 0.005 in Q13
 */
#define LSFQ_MIN                   40

/**
 * maximum quantized LSF value (3.2.4)
 * 3.135 in Q13
 */
#define LSFQ_MAX                   25681

/**
 * minimum LSF distance (3.2.4)
 * 0.0391 in Q13
 */
#define LSFQ_DIFF_MIN              321

/// interpolation filter length
#define INTERPOL_LEN              11

/**
 * minimum gain pitch value (3.8, Equation 47)
 * 0.2 in (1.14)
 */
#define SHARP_MIN                  3277

/**
 * maximum gain pitch value (3.8, Equation 47)
 * (EE) This does not comply with the specification.
 * Specification says about 0.8, which should be
 * 13107 in (1.14), but reference C code uses
 * 13017 (equals to 0.7945) instead of it.
 */
#define SHARP_MAX                  13017

/**
 * MR_ENERGY (mean removed energy) = mean_energy + 10 * log10(2^26  * subframe_size) in (7.13)
 */
#define MR_ENERGY 1018156

#define DECISION_NOISE        0
#define DECISION_INTERMEDIATE 1
#define DECISION_VOICE        2

typedef enum {
    FORMAT_G729_8K = 0,
    FORMAT_G729D_6K4,
    FORMAT_COUNT,
} G729Formats;

typedef struct {
    uint8_t ac_index_bits[2];   ///< adaptive codebook index for second subframe (size in bits)
    uint8_t parity_bit;         ///< parity bit for pitch delay
    uint8_t gc_1st_index_bits;  ///< gain codebook (first stage) index (size in bits)
    uint8_t gc_2nd_index_bits;  ///< gain codebook (second stage) index (size in bits)
    uint8_t fc_signs_bits;      ///< number of pulses in fixed-codebook vector
    uint8_t fc_indexes_bits;    ///< size (in bits) of fixed-codebook index entry
} G729FormatDescription;

typedef struct {
    DSPContext dsp;
    AVFrame frame;

    /// past excitation signal buffer
    int16_t exc_base[2*SUBFRAME_SIZE+PITCH_DELAY_MAX+INTERPOL_LEN];

    int16_t* exc;               ///< start of past excitation data in buffer
    int pitch_delay_int_prev;   ///< integer part of previous subframe's pitch delay (4.1.3)

    /// (2.13) LSP quantizer outputs
    int16_t  past_quantizer_output_buf[MA_NP + 1][10];
    int16_t* past_quantizer_outputs[MA_NP + 1];

    int16_t lsfq[10];           ///< (2.13) quantized LSF coefficients from previous frame
    int16_t lsp_buf[2][10];     ///< (0.15) LSP coefficients (previous and current frames) (3.2.5)
    int16_t *lsp[2];            ///< pointers to lsp_buf

    int16_t quant_energy[4];    ///< (5.10) past quantized energy

    /// previous speech data for LP synthesis filter
    int16_t syn_filter_data[10];


    /// residual signal buffer (used in long-term postfilter)
    int16_t residual[SUBFRAME_SIZE + RES_PREV_DATA_SIZE];

    /// previous speech data for residual calculation filter
    int16_t res_filter_data[SUBFRAME_SIZE+10];

    /// previous speech data for short-term postfilter
    int16_t pos_filter_data[SUBFRAME_SIZE+10];

    /// (1.14) pitch gain of current and five previous subframes
    int16_t past_gain_pitch[6];

    /// (14.1) gain code from current and previous subframe
    int16_t past_gain_code[2];

    /// voice decision on previous subframe (0-noise, 1-intermediate, 2-voice), G.729D
    int16_t voice_decision;

    int16_t onset;              ///< detected onset level (0-2)
    int16_t was_periodic;       ///< whether previous frame was declared as periodic or not (4.4)
    int16_t ht_prev_data;       ///< previous data for 4.2.3, equation 86
    int gain_coeff;             ///< (1.14) gain coefficient (4.2.4)
    uint16_t rand_value;        ///< random number generator value (4.4.4)
    int ma_predictor_prev;      ///< switched MA predictor of LSP quantizer from last good frame

    /// (14.14) high-pass filter data (past input)
    int hpf_f[2];

    /// high-pass filter data (past output)
    int16_t hpf_z[2];
}  G729Context;

static const G729FormatDescription format_g729_8k = {
    .ac_index_bits     = {8,5},
    .parity_bit        = 1,
    .gc_1st_index_bits = GC_1ST_IDX_BITS_8K,
    .gc_2nd_index_bits = GC_2ND_IDX_BITS_8K,
    .fc_signs_bits     = 4,
    .fc_indexes_bits   = 13,
};

static const G729FormatDescription format_g729d_6k4 = {
    .ac_index_bits     = {8,4},
    .parity_bit        = 0,
    .gc_1st_index_bits = GC_1ST_IDX_BITS_6K4,
    .gc_2nd_index_bits = GC_2ND_IDX_BITS_6K4,
    .fc_signs_bits     = 2,
    .fc_indexes_bits   = 9,
};

/**
 * @brief pseudo random number generator
 */
static inline uint16_t g729_prng(uint16_t value)
{
    return 31821 * value + 13849;
}

/**
 * Get parity bit of bit 2..7
 */
static inline int get_parity(uint8_t value)
{
   return (0x6996966996696996ULL >> (value >> 2)) & 1;
}

/**
 * Decodes LSF (Line Spectral Frequencies) from L0-L3 (3.2.4).
 * @param[out] lsfq (2.13) quantized LSF coefficients
 * @param[in,out] past_quantizer_outputs (2.13) quantizer outputs from previous frames
 * @param ma_predictor switched MA predictor of LSP quantizer
 * @param vq_1st first stage vector of quantizer
 * @param vq_2nd_low second stage lower vector of LSP quantizer
 * @param vq_2nd_high second stage higher vector of LSP quantizer
 */
static void lsf_decode(int16_t* lsfq, int16_t* past_quantizer_outputs[MA_NP + 1],
                       int16_t ma_predictor,
                       int16_t vq_1st, int16_t vq_2nd_low, int16_t vq_2nd_high)
{
    int i,j;
    static const uint8_t min_distance[2]={10, 5}; //(2.13)
    int16_t* quantizer_output = past_quantizer_outputs[MA_NP];

    for (i = 0; i < 5; i++) {
        quantizer_output[i]     = cb_lsp_1st[vq_1st][i    ] + cb_lsp_2nd[vq_2nd_low ][i    ];
        quantizer_output[i + 5] = cb_lsp_1st[vq_1st][i + 5] + cb_lsp_2nd[vq_2nd_high][i + 5];
    }

    for (j = 0; j < 2; j++) {
        for (i = 1; i < 10; i++) {
            int diff = (quantizer_output[i - 1] - quantizer_output[i] + min_distance[j]) >> 1;
            if (diff > 0) {
                quantizer_output[i - 1] -= diff;
                quantizer_output[i    ] += diff;
            }
        }
    }

    for (i = 0; i < 10; i++) {
        int sum = quantizer_output[i] * cb_ma_predictor_sum[ma_predictor][i];
        for (j = 0; j < MA_NP; j++)
            sum += past_quantizer_outputs[j][i] * cb_ma_predictor[ma_predictor][j][i];

        lsfq[i] = sum >> 15;
    }

    ff_acelp_reorder_lsf(lsfq, LSFQ_DIFF_MIN, LSFQ_MIN, LSFQ_MAX, 10);
}

/**
 * Restores past LSP quantizer output using LSF from previous frame
 * @param[in,out] lsfq (2.13) quantized LSF coefficients
 * @param[in,out] past_quantizer_outputs (2.13) quantizer outputs from previous frames
 * @param ma_predictor_prev MA predictor from previous frame
 * @param lsfq_prev (2.13) quantized LSF coefficients from previous frame
 */
static void lsf_restore_from_previous(int16_t* lsfq,
                                      int16_t* past_quantizer_outputs[MA_NP + 1],
                                      int ma_predictor_prev)
{
    int16_t* quantizer_output = past_quantizer_outputs[MA_NP];
    int i,k;

    for (i = 0; i < 10; i++) {
        int tmp = lsfq[i] << 15;

        for (k = 0; k < MA_NP; k++)
            tmp -= past_quantizer_outputs[k][i] * cb_ma_predictor[ma_predictor_prev][k][i];

        quantizer_output[i] = ((tmp >> 15) * cb_ma_predictor_sum_inv[ma_predictor_prev][i]) >> 12;
    }
}

/**
 * Constructs new excitation signal and applies phase filter to it
 * @param[out] out constructed speech signal
 * @param in original excitation signal
 * @param fc_cur (2.13) original fixed-codebook vector
 * @param gain_code (14.1) gain code
 * @param subframe_size length of the subframe
 */
static void g729d_get_new_exc(
        int16_t* out,
        const int16_t* in,
        const int16_t* fc_cur,
        int dstate,
        int gain_code,
        int subframe_size)
{
    int i;
    int16_t fc_new[SUBFRAME_SIZE];

    ff_celp_convolve_circ(fc_new, fc_cur, phase_filter[dstate], subframe_size);

    for(i=0; i<subframe_size; i++)
    {
        out[i]  = in[i];
        out[i] -= (gain_code * fc_cur[i] + 0x2000) >> 14;
        out[i] += (gain_code * fc_new[i] + 0x2000) >> 14;
    }
}

/**
 * Makes decision about onset in current subframe
 * @param past_onset decision result of previous subframe
 * @param past_gain_code gain code of current and previous subframe
 *
 * @return onset decision result for current subframe
 */
static int g729d_onset_decision(int past_onset, const int16_t* past_gain_code)
{
    if((past_gain_code[0] >> 1) > past_gain_code[1])
        return 2;
    else
        return FFMAX(past_onset-1, 0);
}

/**
 * Makes decision about voice presence in current subframe
 * @param onset onset level
 * @param prev_voice_decision voice decision result from previous subframe
 * @param past_gain_pitch pitch gain of current and previous subframes
 *
 * @return voice decision result for current subframe
 */
static int16_t g729d_voice_decision(int onset, int prev_voice_decision, const int16_t* past_gain_pitch)
{
    int i, low_gain_pitch_cnt, voice_decision;

    if(past_gain_pitch[0] >= 14745)      // 0.9
        voice_decision = DECISION_VOICE;
    else if (past_gain_pitch[0] <= 9830) // 0.6
        voice_decision = DECISION_NOISE;
    else
        voice_decision = DECISION_INTERMEDIATE;

    for(i=0, low_gain_pitch_cnt=0; i<6; i++)
        if(past_gain_pitch[i] < 9830)
            low_gain_pitch_cnt++;

    if(low_gain_pitch_cnt > 2 && !onset)
        voice_decision = DECISION_NOISE;

    if(!onset && voice_decision > prev_voice_decision + 1)
        voice_decision--;

    if(onset && voice_decision < DECISION_VOICE)
        voice_decision++;

    return voice_decision;
}

static int32_t scalarproduct_int16_c(const int16_t * v1, const int16_t * v2, int order)
{
    int res = 0;

    while (order--)
        res += *v1++ * *v2++;

    return res;
}

static av_cold int decoder_init(AVCodecContext * avctx)
{
    G729Context* ctx = avctx->priv_data;
    int i,k;

    if (avctx->channels != 1) {
        av_log(avctx, AV_LOG_ERROR, "Only mono sound is supported (requested channels: %d).\n", avctx->channels);
        return AVERROR(EINVAL);
    }
    avctx->sample_fmt = AV_SAMPLE_FMT_S16;

    /* Both 8kbit/s and 6.4kbit/s modes uses two subframes per frame. */
    avctx->frame_size = SUBFRAME_SIZE << 1;

    ctx->gain_coeff = 16384; // 1.0 in (1.14)

    for (k = 0; k < MA_NP + 1; k++) {
        ctx->past_quantizer_outputs[k] = ctx->past_quantizer_output_buf[k];
        for (i = 1; i < 11; i++)
            ctx->past_quantizer_outputs[k][i - 1] = (18717 * i) >> 3;
    }

    ctx->lsp[0] = ctx->lsp_buf[0];
    ctx->lsp[1] = ctx->lsp_buf[1];
    memcpy(ctx->lsp[0], lsp_init, 10 * sizeof(int16_t));

    ctx->exc = &ctx->exc_base[PITCH_DELAY_MAX+INTERPOL_LEN];

    ctx->pitch_delay_int_prev = PITCH_DELAY_MIN;

    /* random seed initialization */
    ctx->rand_value = 21845;

    /* quantized prediction error */
    for(i=0; i<4; i++)
        ctx->quant_energy[i] = -14336; // -14 in (5.10)

    ff_dsputil_init(&ctx->dsp, avctx);
    ctx->dsp.scalarproduct_int16 = scalarproduct_int16_c;

    avcodec_get_frame_defaults(&ctx->frame);
    avctx->coded_frame = &ctx->frame;

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *got_frame_ptr,
                        AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    int16_t *out_frame;
    GetBitContext gb;
    const G729FormatDescription *format;
    int frame_erasure = 0;    ///< frame erasure detected during decoding
    int bad_pitch = 0;        ///< parity check failed
    int i;
    int16_t *tmp;
    G729Formats packet_type;
    G729Context *ctx = avctx->priv_data;
    int16_t lp[2][11];           // (3.12)
    uint8_t ma_predictor;     ///< switched MA predictor of LSP quantizer
    uint8_t quantizer_1st;    ///< first stage vector of quantizer
    uint8_t quantizer_2nd_lo; ///< second stage lower vector of quantizer (size in bits)
    uint8_t quantizer_2nd_hi; ///< second stage higher vector of quantizer (size in bits)

    int pitch_delay_int[2];      // pitch delay, integer part
    int pitch_delay_3x;          // pitch delay, multiplied by 3
    int16_t fc[SUBFRAME_SIZE];   // fixed-codebook vector
    int16_t synth[SUBFRAME_SIZE+10]; // fixed-codebook vector
    int j, ret;
    int gain_before, gain_after;
    int is_periodic = 0;         // whether one of the subframes is declared as periodic or not

    ctx->frame.nb_samples = SUBFRAME_SIZE<<1;
    if ((ret = ff_get_buffer(avctx, &ctx->frame)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }
    out_frame = (int16_t*) ctx->frame.data[0];

    if (buf_size == 10) {
        packet_type = FORMAT_G729_8K;
        format = &format_g729_8k;
        //Reset voice decision
        ctx->onset = 0;
        ctx->voice_decision = DECISION_VOICE;
        av_log(avctx, AV_LOG_DEBUG, "Packet type: %s\n", "G.729 @ 8kbit/s");
    } else if (buf_size == 8) {
        packet_type = FORMAT_G729D_6K4;
        format = &format_g729d_6k4;
        av_log(avctx, AV_LOG_DEBUG, "Packet type: %s\n", "G.729D @ 6.4kbit/s");
    } else {
        av_log(avctx, AV_LOG_ERROR, "Packet size %d is unknown.\n", buf_size);
        return AVERROR_INVALIDDATA;
    }

    for (i=0; i < buf_size; i++)
        frame_erasure |= buf[i];
    frame_erasure = !frame_erasure;

    init_get_bits(&gb, buf, 8*buf_size);

    ma_predictor     = get_bits(&gb, 1);
    quantizer_1st    = get_bits(&gb, VQ_1ST_BITS);
    quantizer_2nd_lo = get_bits(&gb, VQ_2ND_BITS);
    quantizer_2nd_hi = get_bits(&gb, VQ_2ND_BITS);

    if(frame_erasure)
        lsf_restore_from_previous(ctx->lsfq, ctx->past_quantizer_outputs,
                                  ctx->ma_predictor_prev);
    else {
        lsf_decode(ctx->lsfq, ctx->past_quantizer_outputs,
                   ma_predictor,
                   quantizer_1st, quantizer_2nd_lo, quantizer_2nd_hi);
        ctx->ma_predictor_prev = ma_predictor;
    }

    tmp = ctx->past_quantizer_outputs[MA_NP];
    memmove(ctx->past_quantizer_outputs + 1, ctx->past_quantizer_outputs,
            MA_NP * sizeof(int16_t*));
    ctx->past_quantizer_outputs[0] = tmp;

    ff_acelp_lsf2lsp(ctx->lsp[1], ctx->lsfq, 10);

    ff_acelp_lp_decode(&lp[0][0], &lp[1][0], ctx->lsp[1], ctx->lsp[0], 10);

    FFSWAP(int16_t*, ctx->lsp[1], ctx->lsp[0]);

    for (i = 0; i < 2; i++) {
        int gain_corr_factor;

        uint8_t ac_index;      ///< adaptive codebook index
        uint8_t pulses_signs;  ///< fixed-codebook vector pulse signs
        int fc_indexes;        ///< fixed-codebook indexes
        uint8_t gc_1st_index;  ///< gain codebook (first stage) index
        uint8_t gc_2nd_index;  ///< gain codebook (second stage) index

        ac_index      = get_bits(&gb, format->ac_index_bits[i]);
        if(!i && format->parity_bit)
            bad_pitch = get_parity(ac_index) == get_bits1(&gb);
        fc_indexes    = get_bits(&gb, format->fc_indexes_bits);
        pulses_signs  = get_bits(&gb, format->fc_signs_bits);
        gc_1st_index  = get_bits(&gb, format->gc_1st_index_bits);
        gc_2nd_index  = get_bits(&gb, format->gc_2nd_index_bits);

        if (frame_erasure)
            pitch_delay_3x   = 3 * ctx->pitch_delay_int_prev;
        else if(!i) {
            if (bad_pitch)
                pitch_delay_3x   = 3 * ctx->pitch_delay_int_prev;
            else
                pitch_delay_3x = ff_acelp_decode_8bit_to_1st_delay3(ac_index);
        } else {
            int pitch_delay_min = av_clip(ctx->pitch_delay_int_prev - 5,
                                          PITCH_DELAY_MIN, PITCH_DELAY_MAX - 9);

            if(packet_type == FORMAT_G729D_6K4)
                pitch_delay_3x = ff_acelp_decode_4bit_to_2nd_delay3(ac_index, pitch_delay_min);
            else
                pitch_delay_3x = ff_acelp_decode_5_6_bit_to_2nd_delay3(ac_index, pitch_delay_min);
        }

        /* Round pitch delay to nearest (used everywhere except ff_acelp_interpolate). */
        pitch_delay_int[i]  = (pitch_delay_3x + 1) / 3;
        if (pitch_delay_int[i] > PITCH_DELAY_MAX) {
            av_log(avctx, AV_LOG_WARNING, "pitch_delay_int %d is too large\n", pitch_delay_int[i]);
            pitch_delay_int[i] = PITCH_DELAY_MAX;
        }

        if (frame_erasure) {
            ctx->rand_value = g729_prng(ctx->rand_value);
            fc_indexes   = ctx->rand_value & ((1 << format->fc_indexes_bits) - 1);

            ctx->rand_value = g729_prng(ctx->rand_value);
            pulses_signs = ctx->rand_value;
        }


        memset(fc, 0, sizeof(int16_t) * SUBFRAME_SIZE);
        switch (packet_type) {
            case FORMAT_G729_8K:
                ff_acelp_fc_pulse_per_track(fc, ff_fc_4pulses_8bits_tracks_13,
                                            ff_fc_4pulses_8bits_track_4,
                                            fc_indexes, pulses_signs, 3, 3);
                break;
            case FORMAT_G729D_6K4:
                ff_acelp_fc_pulse_per_track(fc, ff_fc_2pulses_9bits_track1_gray,
                                            ff_fc_2pulses_9bits_track2_gray,
                                            fc_indexes, pulses_signs, 1, 4);
                break;
        }

        /*
          This filter enhances harmonic components of the fixed-codebook vector to
          improve the quality of the reconstructed speech.

                     / fc_v[i],                                    i < pitch_delay
          fc_v[i] = <
                     \ fc_v[i] + gain_pitch * fc_v[i-pitch_delay], i >= pitch_delay
        */
        ff_acelp_weighted_vector_sum(fc + pitch_delay_int[i],
                                     fc + pitch_delay_int[i],
                                     fc, 1 << 14,
                                     av_clip(ctx->past_gain_pitch[0], SHARP_MIN, SHARP_MAX),
                                     0, 14,
                                     SUBFRAME_SIZE - pitch_delay_int[i]);

        memmove(ctx->past_gain_pitch+1, ctx->past_gain_pitch, 5 * sizeof(int16_t));
        ctx->past_gain_code[1] = ctx->past_gain_code[0];

        if (frame_erasure) {
            ctx->past_gain_pitch[0] = (29491 * ctx->past_gain_pitch[0]) >> 15; // 0.90 (0.15)
            ctx->past_gain_code[0]  = ( 2007 * ctx->past_gain_code[0] ) >> 11; // 0.98 (0.11)

            gain_corr_factor = 0;
        } else {
            if (packet_type == FORMAT_G729D_6K4) {
                ctx->past_gain_pitch[0]  = cb_gain_1st_6k4[gc_1st_index][0] +
                                           cb_gain_2nd_6k4[gc_2nd_index][0];
                gain_corr_factor = cb_gain_1st_6k4[gc_1st_index][1] +
                                   cb_gain_2nd_6k4[gc_2nd_index][1];

                /* Without check below overflow can occur in ff_acelp_update_past_gain.
                   It is not issue for G.729, because gain_corr_factor in it's case is always
                   greater than 1024, while in G.729D it can be even zero. */
                gain_corr_factor = FFMAX(gain_corr_factor, 1024);
#ifndef G729_BITEXACT
                gain_corr_factor >>= 1;
#endif
            } else {
                ctx->past_gain_pitch[0]  = cb_gain_1st_8k[gc_1st_index][0] +
                                           cb_gain_2nd_8k[gc_2nd_index][0];
                gain_corr_factor = cb_gain_1st_8k[gc_1st_index][1] +
                                   cb_gain_2nd_8k[gc_2nd_index][1];
            }

            /* Decode the fixed-codebook gain. */
            ctx->past_gain_code[0] = ff_acelp_decode_gain_code(&ctx->dsp, gain_corr_factor,
                                                               fc, MR_ENERGY,
                                                               ctx->quant_energy,
                                                               ma_prediction_coeff,
                                                               SUBFRAME_SIZE, 4);
#ifdef G729_BITEXACT
            /*
              This correction required to get bit-exact result with
              reference code, because gain_corr_factor in G.729D is
              two times larger than in original G.729.

              If bit-exact result is not issue then gain_corr_factor
              can be simpler divided by 2 before call to g729_get_gain_code
              instead of using correction below.
            */
            if (packet_type == FORMAT_G729D_6K4) {
                gain_corr_factor >>= 1;
                ctx->past_gain_code[0] >>= 1;
            }
#endif
        }
        ff_acelp_update_past_gain(ctx->quant_energy, gain_corr_factor, 2, frame_erasure);

        /* Routine requires rounding to lowest. */
        ff_acelp_interpolate(ctx->exc + i * SUBFRAME_SIZE,
                             ctx->exc + i * SUBFRAME_SIZE - pitch_delay_3x / 3,
                             ff_acelp_interp_filter, 6,
                             (pitch_delay_3x % 3) << 1,
                             10, SUBFRAME_SIZE);

        ff_acelp_weighted_vector_sum(ctx->exc + i * SUBFRAME_SIZE,
                                     ctx->exc + i * SUBFRAME_SIZE, fc,
                                     (!ctx->was_periodic && frame_erasure) ? 0 : ctx->past_gain_pitch[0],
                                     ( ctx->was_periodic && frame_erasure) ? 0 : ctx->past_gain_code[0],
                                     1 << 13, 14, SUBFRAME_SIZE);

        memcpy(synth, ctx->syn_filter_data, 10 * sizeof(int16_t));

        if (ff_celp_lp_synthesis_filter(
            synth+10,
            &lp[i][1],
            ctx->exc  + i * SUBFRAME_SIZE,
            SUBFRAME_SIZE,
            10,
            1,
            0,
            0x800))
            /* Overflow occurred, downscale excitation signal... */
            for (j = 0; j < 2 * SUBFRAME_SIZE + PITCH_DELAY_MAX + INTERPOL_LEN; j++)
                ctx->exc_base[j] >>= 2;

        /* ... and make synthesis again. */
        if (packet_type == FORMAT_G729D_6K4) {
            int16_t exc_new[SUBFRAME_SIZE];

            ctx->onset = g729d_onset_decision(ctx->onset, ctx->past_gain_code);
            ctx->voice_decision = g729d_voice_decision(ctx->onset, ctx->voice_decision, ctx->past_gain_pitch);

            g729d_get_new_exc(exc_new, ctx->exc  + i * SUBFRAME_SIZE, fc, ctx->voice_decision, ctx->past_gain_code[0], SUBFRAME_SIZE);

            ff_celp_lp_synthesis_filter(
                    synth+10,
                    &lp[i][1],
                    exc_new,
                    SUBFRAME_SIZE,
                    10,
                    0,
                    0,
                    0x800);
        } else {
            ff_celp_lp_synthesis_filter(
                    synth+10,
                    &lp[i][1],
                    ctx->exc  + i * SUBFRAME_SIZE,
                    SUBFRAME_SIZE,
                    10,
                    0,
                    0,
                    0x800);
        }
        /* Save data (without postfilter) for use in next subframe. */
        memcpy(ctx->syn_filter_data, synth+SUBFRAME_SIZE, 10 * sizeof(int16_t));

        /* Calculate gain of unfiltered signal for use in AGC. */
        gain_before = 0;
        for (j = 0; j < SUBFRAME_SIZE; j++)
            gain_before += FFABS(synth[j+10]);

        /* Call postfilter and also update voicing decision for use in next frame. */
        ff_g729_postfilter(
                &ctx->dsp,
                &ctx->ht_prev_data,
                &is_periodic,
                &lp[i][0],
                pitch_delay_int[0],
                ctx->residual,
                ctx->res_filter_data,
                ctx->pos_filter_data,
                synth+10,
                SUBFRAME_SIZE);

        /* Calculate gain of filtered signal for use in AGC. */
        gain_after = 0;
        for(j=0; j<SUBFRAME_SIZE; j++)
            gain_after += FFABS(synth[j+10]);

        ctx->gain_coeff = ff_g729_adaptive_gain_control(
                gain_before,
                gain_after,
                synth+10,
                SUBFRAME_SIZE,
                ctx->gain_coeff);

        if (frame_erasure)
            ctx->pitch_delay_int_prev = FFMIN(ctx->pitch_delay_int_prev + 1, PITCH_DELAY_MAX);
        else
            ctx->pitch_delay_int_prev = pitch_delay_int[i];

        memcpy(synth+8, ctx->hpf_z, 2*sizeof(int16_t));
        ff_acelp_high_pass_filter(
                out_frame + i*SUBFRAME_SIZE,
                ctx->hpf_f,
                synth+10,
                SUBFRAME_SIZE);
        memcpy(ctx->hpf_z, synth+8+SUBFRAME_SIZE, 2*sizeof(int16_t));
    }

    ctx->was_periodic = is_periodic;

    /* Save signal for use in next frame. */
    memmove(ctx->exc_base, ctx->exc_base + 2 * SUBFRAME_SIZE, (PITCH_DELAY_MAX+INTERPOL_LEN)*sizeof(int16_t));

    *got_frame_ptr = 1;
    *(AVFrame*)data = ctx->frame;
    return buf_size;
}

AVCodec ff_g729_decoder = {
    .name           = "g729",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_G729,
    .priv_data_size = sizeof(G729Context),
    .init           = decoder_init,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("G.729"),
};
