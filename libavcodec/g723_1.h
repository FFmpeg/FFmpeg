/*
 * G.723.1 common header and data tables
 * Copyright (c) 2006 Benjamin Larsson
 * Copyright (c) 2010 Mohamed Naufal Basheer
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
 * G.723.1 types, functions and data tables
 */

#ifndef AVCODEC_G723_1_H
#define AVCODEC_G723_1_H

#include <stdint.h>

#include "libavutil/log.h"

#define SUBFRAMES       4
#define SUBFRAME_LEN    60
#define FRAME_LEN       (SUBFRAME_LEN << 2)
#define HALF_FRAME_LEN  (FRAME_LEN / 2)
#define LPC_FRAME       (HALF_FRAME_LEN + SUBFRAME_LEN)
#define LPC_ORDER       10
#define LSP_BANDS       3
#define LSP_CB_SIZE     256
#define PITCH_MIN       18
#define PITCH_MAX       (PITCH_MIN + 127)
#define PITCH_ORDER     5
#define GRID_SIZE       2
#define PULSE_MAX       6
#define GAIN_LEVELS     24
#define COS_TBL_SIZE    512

/**
 * Bitexact implementation of 2ab scaled by 1/2^16.
 *
 * @param a 32 bit multiplicand
 * @param b 16 bit multiplier
 */
#define MULL2(a, b) \
        ((((a) >> 16) * (b) * 2) + (((a) & 0xffff) * (b) >> 15))

/**
 * G723.1 frame types
 */
enum FrameType {
    ACTIVE_FRAME,        ///< Active speech
    SID_FRAME,           ///< Silence Insertion Descriptor frame
    UNTRANSMITTED_FRAME
};

/**
 * G723.1 rate values
 */
enum Rate {
    RATE_6300,
    RATE_5300
};

/**
 * G723.1 unpacked data subframe
 */
typedef struct G723_1_Subframe {
    int ad_cb_lag;     ///< adaptive codebook lag
    int ad_cb_gain;
    int dirac_train;
    int pulse_sign;
    int grid_index;
    int amp_index;
    int pulse_pos;
} G723_1_Subframe;

/**
 * Pitch postfilter parameters
 */
typedef struct PPFParam {
    int     index;    ///< postfilter backward/forward lag
    int16_t opt_gain; ///< optimal gain
    int16_t sc_gain;  ///< scaling gain
} PPFParam;

/**
 * Harmonic filter parameters
 */
typedef struct HFParam {
    int index;
    int gain;
} HFParam;

/**
 * Optimized fixed codebook excitation parameters
 */
typedef struct FCBParam {
    int min_err;
    int amp_index;
    int grid_index;
    int dirac_train;
    int pulse_pos[PULSE_MAX];
    int pulse_sign[PULSE_MAX];
} FCBParam;

typedef struct G723_1_ChannelContext {
    G723_1_Subframe subframe[4];
    enum FrameType cur_frame_type;
    enum FrameType past_frame_type;
    enum Rate cur_rate;
    uint8_t lsp_index[LSP_BANDS];
    int pitch_lag[2];
    int erased_frames;

    int16_t prev_lsp[LPC_ORDER];
    int16_t sid_lsp[LPC_ORDER];
    int16_t prev_excitation[PITCH_MAX];
    int16_t excitation[PITCH_MAX + FRAME_LEN + 4];
    int16_t synth_mem[LPC_ORDER];
    int16_t fir_mem[LPC_ORDER];
    int     iir_mem[LPC_ORDER];

    int random_seed;
    int cng_random_seed;
    int interp_index;
    int interp_gain;
    int sid_gain;
    int cur_gain;
    int reflection_coef;
    int pf_gain;                 ///< formant postfilter
                                 ///< gain scaling unit memory
    int16_t audio[FRAME_LEN + LPC_ORDER + PITCH_MAX + 4];

    /* encoder */
    int16_t prev_data[HALF_FRAME_LEN];
    int16_t prev_weight_sig[PITCH_MAX];

    int16_t hpf_fir_mem;                   ///< highpass filter fir
    int     hpf_iir_mem;                   ///< and iir memories
    int16_t perf_fir_mem[LPC_ORDER];       ///< perceptual filter fir
    int16_t perf_iir_mem[LPC_ORDER];       ///< and iir memories

    int16_t harmonic_mem[PITCH_MAX];
} G723_1_ChannelContext;

typedef struct G723_1_Context {
    AVClass *class;
    int postfilter;

    G723_1_ChannelContext ch[2];
} G723_1_Context;


/**
 * Scale vector contents based on the largest of their absolutes.
 */
int ff_g723_1_scale_vector(int16_t *dst, const int16_t *vector, int length);

/**
 * Calculate the number of left-shifts required for normalizing the input.
 *
 * @param num   input number
 * @param width width of the input, 16 bits(0) / 32 bits(1)
 */
int ff_g723_1_normalize_bits(int num, int width);

int ff_g723_1_dot_product(const int16_t *a, const int16_t *b, int length);

/**
 * Get delayed contribution from the previous excitation vector.
 */
void ff_g723_1_get_residual(int16_t *residual, int16_t *prev_excitation,
                            int lag);

/**
 * Generate a train of dirac functions with period as pitch lag.
 */
void ff_g723_1_gen_dirac_train(int16_t *buf, int pitch_lag);


/**
 * Generate adaptive codebook excitation.
 */
void ff_g723_1_gen_acb_excitation(int16_t *vector, int16_t *prev_excitation,
                                  int pitch_lag, G723_1_Subframe *subfrm,
                                  enum Rate cur_rate);
/**
 * Quantize LSP frequencies by interpolation and convert them to
 * the corresponding LPC coefficients.
 *
 * @param lpc      buffer for LPC coefficients
 * @param cur_lsp  the current LSP vector
 * @param prev_lsp the previous LSP vector
 */
void ff_g723_1_lsp_interpolate(int16_t *lpc, int16_t *cur_lsp,
                               int16_t *prev_lsp);

/**
 * Perform inverse quantization of LSP frequencies.
 *
 * @param cur_lsp    the current LSP vector
 * @param prev_lsp   the previous LSP vector
 * @param lsp_index  VQ indices
 * @param bad_frame  bad frame flag
 */
void ff_g723_1_inverse_quant(int16_t *cur_lsp, int16_t *prev_lsp,
                             uint8_t *lsp_index, int bad_frame);

static const uint8_t frame_size[4] = { 24, 20, 4, 1 };

/**
 * Postfilter gain weighting factors scaled by 2^15
 */
static const int16_t ppf_gain_weight[2] = {0x1800, 0x2000};

/**
 * LSP DC component
 */
static const int16_t dc_lsp[LPC_ORDER] = {
    0x0c3b,
    0x1271,
    0x1e0a,
    0x2a36,
    0x3630,
    0x406f,
    0x4d28,
    0x56f4,
    0x638c,
    0x6c46
};

/* Cosine table scaled by 2^14 */
extern const int16_t ff_g723_1_cos_tab[COS_TBL_SIZE + 1];
#define G723_1_COS_TAB_FIRST_ELEMENT 16384

/**
 *  LSP VQ tables
 */
extern const int16_t ff_g723_1_lsp_band0[LSP_CB_SIZE][3];
extern const int16_t ff_g723_1_lsp_band1[LSP_CB_SIZE][3];
extern const int16_t ff_g723_1_lsp_band2[LSP_CB_SIZE][4];

/**
 * Used for the coding/decoding of the pulses positions
 * for the MP-MLQ codebook
 */
extern const int32_t ff_g723_1_combinatorial_table[PULSE_MAX][SUBFRAME_LEN/GRID_SIZE];

static const int16_t pitch_contrib[340] = {
    60,     0,  0,  2489, 60,     0,  0,  5217,
     1,  6171,  0,  3953,  0, 10364,  1,  9357,
    -1,  8843,  1,  9396,  0,  5794, -1, 10816,
     2, 11606, -2, 12072,  0,  8616,  1, 12170,
     0, 14440,  0,  7787, -1, 13721,  0, 18205,
     0, 14471,  0, 15807,  1, 15275,  0, 13480,
    -1, 18375, -1,     0,  1, 11194, -1, 13010,
     1, 18836, -2, 20354,  1, 16233, -1,     0,
    60,     0,  0, 12130,  0, 13385,  1, 17834,
     1, 20875,  0, 21996,  1,     0,  1, 18277,
    -1, 21321,  1, 13738, -1, 19094, -1, 20387,
    -1,     0,  0, 21008, 60,     0, -2, 22807,
     0, 15900,  1,     0,  0, 17989, -1, 22259,
     1, 24395,  1, 23138,  0, 23948,  1, 22997,
     2, 22604, -1, 25942,  0, 26246,  1, 25321,
     0, 26423,  0, 24061,  0, 27247, 60,     0,
    -1, 25572,  1, 23918,  1, 25930,  2, 26408,
    -1, 19049,  1, 27357, -1, 24538, 60,     0,
    -1, 25093,  0, 28549,  1,     0,  0, 22793,
    -1, 25659,  0, 29377,  0, 30276,  0, 26198,
     1, 22521, -1, 28919,  0, 27384,  1, 30162,
    -1,     0,  0, 24237, -1, 30062,  0, 21763,
     1, 30917, 60,     0,  0, 31284,  0, 29433,
     1, 26821,  1, 28655,  0, 31327,  2, 30799,
     1, 31389,  0, 32322,  1, 31760, -2, 31830,
     0, 26936, -1, 31180,  1, 30875,  0, 27873,
    -1, 30429,  1, 31050,  0,     0,  0, 31912,
     1, 31611,  0, 31565,  0, 25557,  0, 31357,
    60,     0,  1, 29536,  1, 28985, -1, 26984,
    -1, 31587,  2, 30836, -2, 31133,  0, 30243,
    -1, 30742, -1, 32090, 60,     0,  2, 30902,
    60,     0,  0, 30027,  0, 29042, 60,     0,
     0, 31756,  0, 24553,  0, 25636, -2, 30501,
    60,     0, -1, 29617,  0, 30649, 60,     0,
     0, 29274,  2, 30415,  0, 27480,  0, 31213,
    -1, 28147,  0, 30600,  1, 31652,  2, 29068,
    60,     0,  1, 28571,  1, 28730,  1, 31422,
     0, 28257,  0, 24797, 60,     0,  0,     0,
    60,     0,  0, 22105,  0, 27852, 60,     0,
    60,     0, -1, 24214,  0, 24642,  0, 23305,
    60,     0, 60,     0,  1, 22883,  0, 21601,
    60,     0,  2, 25650, 60,     0, -2, 31253,
    -2, 25144,  0, 17998
};

/**
 * Number of non-zero pulses in the MP-MLQ excitation
 */
static const int8_t pulses[4] = {6, 5, 6, 5};

/**
 * Size of the MP-MLQ fixed excitation codebooks
 */
static const int32_t max_pos[4] = {593775, 142506, 593775, 142506};

extern const int16_t ff_g723_1_fixed_cb_gain[GAIN_LEVELS];

extern const int16_t ff_g723_1_adaptive_cb_gain85 [ 85 * 20];
extern const int16_t ff_g723_1_adaptive_cb_gain170[170 * 20];

/**
 * 0.65^i (Zero part) and 0.75^i (Pole part) scaled by 2^15
 */
static const int16_t postfilter_tbl[2][LPC_ORDER] = {
    /* Zero */
    {21299, 13844,  8999,  5849, 3802, 2471, 1606, 1044,  679,  441},
    /* Pole */
    {24576, 18432, 13824, 10368, 7776, 5832, 4374, 3281, 2460, 1845}
};

/**
 * Hamming window coefficients scaled by 2^15
 */
static const int16_t hamming_window[LPC_FRAME] = {
     2621,  2631,  2659,  2705,  2770,  2853,  2955,  3074,  3212,  3367,
     3541,  3731,  3939,  4164,  4405,  4663,  4937,  5226,  5531,  5851,
     6186,  6534,  6897,  7273,  7661,  8062,  8475,  8899,  9334,  9780,
    10235, 10699, 11172, 11653, 12141, 12636, 13138, 13645, 14157, 14673,
    15193, 15716, 16242, 16769, 17298, 17827, 18356, 18884, 19411, 19935,
    20457, 20975, 21489, 21999, 22503, 23002, 23494, 23978, 24455, 24924,
    25384, 25834, 26274, 26704, 27122, 27529, 27924, 28306, 28675, 29031,
    29373, 29700, 30012, 30310, 30592, 30857, 31107, 31340, 31557, 31756,
    31938, 32102, 32249, 32377, 32488, 32580, 32654, 32710, 32747, 32766,
    32766, 32747, 32710, 32654, 32580, 32488, 32377, 32249, 32102, 31938,
    31756, 31557, 31340, 31107, 30857, 30592, 30310, 30012, 29700, 29373,
    29031, 28675, 28306, 27924, 27529, 27122, 26704, 26274, 25834, 25384,
    24924, 24455, 23978, 23494, 23002, 22503, 21999, 21489, 20975, 20457,
    19935, 19411, 18884, 18356, 17827, 17298, 16769, 16242, 15716, 15193,
    14673, 14157, 13645, 13138, 12636, 12141, 11653, 11172, 10699, 10235,
     9780, 9334,   8899,  8475,  8062,  7661,  7273,  6897,  6534,  6186,
     5851, 5531,   5226,  4937,  4663,  4405,  4164,  3939,  3731,  3541,
     3367, 3212,   3074,  2955,  2853,  2770,  2705,  2659,  2631,  2621
};

/**
 * Binomial window coefficients scaled by 2^15
 */
static const int16_t binomial_window[LPC_ORDER] = {
    32749, 32695, 32604, 32477, 32315, 32118, 31887, 31622, 31324, 30995
};

/**
 * 0.994^i scaled by 2^15
 */
static const int16_t bandwidth_expand[LPC_ORDER] = {
    32571, 32376, 32182, 31989, 31797, 31606, 31416, 31228, 31040, 30854
};

/**
 * 0.5^i scaled by 2^15
 */
static const int16_t percept_flt_tbl[2][LPC_ORDER] = {
    /* Zero part */
    {29491, 26542, 23888, 21499, 19349, 17414, 15673, 14106, 12695, 11425},
    /* Pole part */
    {16384,  8192,  4096,  2048,  1024,   512,   256,   128,    64,    32}
};

static const int cng_adaptive_cb_lag[4] = { 1, 0, 1, 3 };

static const int cng_filt[4] = { 273, 998, 499, 333 };

static const int cng_bseg[3] = { 2048, 18432, 231233 };

#endif /* AVCODEC_G723_1_H */
