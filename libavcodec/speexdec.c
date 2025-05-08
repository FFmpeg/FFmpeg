/*
 * Copyright 2002-2008  Xiph.org Foundation
 * Copyright 2002-2008  Jean-Marc Valin
 * Copyright 2005-2007  Analog Devices Inc.
 * Copyright 2005-2008  Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * Copyright 1993, 2002, 2006 David Rowe
 * Copyright 2003       EpicGames
 * Copyright 1992-1994  Jutta Degener, Carsten Bormann

 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:

 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.

 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.

 * - Neither the name of the Xiph.org Foundation nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.

 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/float_dsp.h"
#include "libavutil/intfloat.h"
#include "libavutil/mem.h"
#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"
#include "speexdata.h"

#define SPEEX_NB_MODES 3
#define SPEEX_INBAND_STEREO 9

#define QMF_ORDER 64
#define NB_ORDER 10
#define NB_FRAME_SIZE 160
#define NB_SUBMODES 9
#define NB_SUBMODE_BITS 4
#define SB_SUBMODE_BITS 3

#define NB_SUBFRAME_SIZE 40
#define NB_NB_SUBFRAMES 4
#define NB_PITCH_START 17
#define NB_PITCH_END 144

#define NB_DEC_BUFFER (NB_FRAME_SIZE + 2 * NB_PITCH_END + NB_SUBFRAME_SIZE + 12)

#define SPEEX_MEMSET(dst, c, n) (memset((dst), (c), (n) * sizeof(*(dst))))
#define SPEEX_COPY(dst, src, n) (memcpy((dst), (src), (n) * sizeof(*(dst))))

#define LSP_LINEAR(i) (.25f * (i) + .25f)
#define LSP_LINEAR_HIGH(i) (.3125f * (i) + .75f)
#define LSP_DIV_256(x) (0.00390625f * (x))
#define LSP_DIV_512(x) (0.001953125f * (x))
#define LSP_DIV_1024(x) (0.0009765625f * (x))

typedef struct LtpParams {
    const int8_t *gain_cdbk;
    int gain_bits;
    int pitch_bits;
} LtpParam;

static const LtpParam ltp_params_vlbr = { gain_cdbk_lbr, 5, 0 };
static const LtpParam ltp_params_lbr  = { gain_cdbk_lbr, 5, 7 };
static const LtpParam ltp_params_med  = { gain_cdbk_lbr, 5, 7 };
static const LtpParam ltp_params_nb   = { gain_cdbk_nb,  7, 7 };

typedef struct SplitCodebookParams {
    int subvect_size;
    int nb_subvect;
    const signed char *shape_cb;
    int shape_bits;
    int have_sign;
} SplitCodebookParams;

static const SplitCodebookParams split_cb_nb_ulbr = { 20, 2, exc_20_32_table, 5, 0 };
static const SplitCodebookParams split_cb_nb_vlbr = { 10, 4, exc_10_16_table, 4, 0 };
static const SplitCodebookParams split_cb_nb_lbr  = { 10, 4, exc_10_32_table, 5, 0 };
static const SplitCodebookParams split_cb_nb_med  = {  8, 5, exc_8_128_table, 7, 0 };
static const SplitCodebookParams split_cb_nb      = {  5, 8, exc_5_64_table,  6, 0 };
static const SplitCodebookParams split_cb_sb      = {  5, 8, exc_5_256_table, 8, 0 };
static const SplitCodebookParams split_cb_high    = {  8, 5, hexc_table,      7, 1 };
static const SplitCodebookParams split_cb_high_lbr= { 10, 4, hexc_10_32_table,5, 0 };

/** Quantizes LSPs */
typedef void (*lsp_quant_func)(float *, float *, int, GetBitContext *);

/** Decodes quantized LSPs */
typedef void (*lsp_unquant_func)(float *, int, GetBitContext *);

/** Long-term predictor quantization */
typedef int (*ltp_quant_func)(float *, float *, float *,
    float *, float *, float *,
    const void *, int, int, float, int, int,
    GetBitContext *, char *, float *,
    float *, int, int, int, float *);

/** Long-term un-quantize */
typedef void (*ltp_unquant_func)(float *, float *, int, int,
    float, const void *, int, int *,
    float *, GetBitContext *, int, int,
    float, int);

/** Innovation quantization function */
typedef void (*innovation_quant_func)(float *, float *,
    float *, float *, const void *,
    int, int, float *, float *,
    GetBitContext *, char *, int, int);

/** Innovation unquantization function */
typedef void (*innovation_unquant_func)(float *, const void *, int,
    GetBitContext *, uint32_t *);

typedef struct SpeexSubmode {
    int lbr_pitch; /**< Set to -1 for "normal" modes, otherwise encode pitch using
                  a global pitch and allowing a +- lbr_pitch variation (for
                  low not-rates)*/
    int forced_pitch_gain; /**< Use the same (forced) pitch gain for all
                            sub-frames */
    int have_subframe_gain; /**< Number of bits to use as sub-frame innovation
                           gain */
    int double_codebook; /**< Apply innovation quantization twice for higher
                              quality (and higher bit-rate)*/
    lsp_unquant_func lsp_unquant; /**< LSP unquantization function */

    ltp_unquant_func ltp_unquant; /**< Long-term predictor (pitch) un-quantizer */
    const void *LtpParam; /**< Pitch parameters (options) */

    innovation_unquant_func innovation_unquant; /**< Innovation un-quantization */
    const void *innovation_params; /**< Innovation quantization parameters*/

    float comb_gain; /**< Gain of enhancer comb filter */
} SpeexSubmode;

typedef struct SpeexMode {
    int modeID;                 /**< ID of the mode */
    int (*decode)(AVCodecContext *avctx, void *dec, GetBitContext *gb, float *out, int packets_left);
    int frame_size;             /**< Size of frames used for decoding */
    int subframe_size;          /**< Size of sub-frames used for decoding */
    int lpc_size;               /**< Order of LPC filter */
    float folding_gain;         /**< Folding gain */
    const SpeexSubmode *submodes[NB_SUBMODES]; /**< Sub-mode data for the mode */
    int default_submode;        /**< Default sub-mode to use when decoding */
} SpeexMode;

typedef struct DecoderState {
    const SpeexMode *mode;
    int modeID;             /**< ID of the decoder mode */
    int first;              /**< Is first frame  */
    int full_frame_size;    /**< Length of full-band frames */
    int is_wideband;        /**< If wideband is present */
    int count_lost;         /**< Was the last frame lost? */
    int frame_size;         /**< Length of high-band frames */
    int subframe_size;      /**< Length of high-band sub-frames */
    int nb_subframes;       /**< Number of high-band sub-frames */
    int lpc_size;           /**< Order of high-band LPC analysis */
    float last_ol_gain;     /**< Open-loop gain for previous frame */
    float *innov_save;      /**< If non-NULL, innovation is copied here */

    /* This is used in packet loss concealment */
    int last_pitch;         /**< Pitch of last correctly decoded frame */
    float last_pitch_gain;  /**< Pitch gain of last correctly decoded frame */
    uint32_t seed;          /**< Seed used for random number generation */

    int encode_submode;
    const SpeexSubmode *const *submodes; /**< Sub-mode data */
    int submodeID;          /**< Activated sub-mode */
    int lpc_enh_enabled;    /**< 1 when LPC enhancer is on, 0 otherwise */

    /* Vocoder data */
    float voc_m1;
    float voc_m2;
    float voc_mean;
    int voc_offset;

    int dtx_enabled;
    int highpass_enabled;   /**< Is the input filter enabled */

    float *exc;             /**< Start of excitation frame */
    float mem_hp[2];        /**< High-pass filter memory */
    float exc_buf[NB_DEC_BUFFER]; /**< Excitation buffer */
    float old_qlsp[NB_ORDER]; /**< Quantized LSPs for previous frame */
    float interp_qlpc[NB_ORDER]; /**< Interpolated quantized LPCs */
    float mem_sp[NB_ORDER]; /**< Filter memory for synthesis signal */
    float g0_mem[QMF_ORDER];
    float g1_mem[QMF_ORDER];
    float pi_gain[NB_NB_SUBFRAMES]; /**< Gain of LPC filter at theta=pi (fe/2) */
    float exc_rms[NB_NB_SUBFRAMES]; /**< RMS of excitation per subframe */
} DecoderState;

/* Default handler for user callbacks: skip it */
static int speex_default_user_handler(GetBitContext *gb, void *state, void *data)
{
    const int req_size = get_bits(gb, 4);
    skip_bits_long(gb, 5 + 8 * req_size);
    return 0;
}

typedef struct StereoState {
    float balance; /**< Left/right balance info */
    float e_ratio; /**< Ratio of energies: E(left+right)/[E(left)+E(right)]  */
    float smooth_left; /**< Smoothed left channel gain */
    float smooth_right; /**< Smoothed right channel gain */
} StereoState;

typedef struct SpeexContext {
    AVClass *class;
    GetBitContext gb;

    int32_t version_id; /**< Version for Speex (for checking compatibility) */
    int32_t rate; /**< Sampling rate used */
    int32_t mode; /**< Mode used (0 for narrowband, 1 for wideband) */
    int32_t bitstream_version; /**< Version ID of the bit-stream */
    int32_t nb_channels; /**< Number of channels decoded */
    int32_t bitrate; /**< Bit-rate used */
    int32_t frame_size; /**< Size of frames */
    int32_t vbr; /**< 1 for a VBR decoding, 0 otherwise */
    int32_t frames_per_packet; /**< Number of frames stored per Ogg packet */
    int32_t extra_headers; /**< Number of additional headers after the comments */

    int pkt_size;

    StereoState stereo;
    DecoderState st[SPEEX_NB_MODES];

    AVFloatDSPContext *fdsp;
} SpeexContext;

static void lsp_unquant_lbr(float *lsp, int order, GetBitContext *gb)
{
    int id;

    for (int i = 0; i < order; i++)
        lsp[i] = LSP_LINEAR(i);

    id = get_bits(gb, 6);
    for (int i = 0; i < 10; i++)
        lsp[i] += LSP_DIV_256(cdbk_nb[id * 10 + i]);

    id = get_bits(gb, 6);
    for (int i = 0; i < 5; i++)
        lsp[i] += LSP_DIV_512(cdbk_nb_low1[id * 5 + i]);

    id = get_bits(gb, 6);
    for (int i = 0; i < 5; i++)
        lsp[i + 5] += LSP_DIV_512(cdbk_nb_high1[id * 5 + i]);
}

static void forced_pitch_unquant(float *exc, float *exc_out, int start, int end,
                                 float pitch_coef, const void *par, int nsf,
                                 int *pitch_val, float *gain_val, GetBitContext *gb, int count_lost,
                                 int subframe_offset, float last_pitch_gain, int cdbk_offset)
{
    av_assert0(!isnan(pitch_coef));
    pitch_coef = fminf(pitch_coef, .99f);
    for (int i = 0; i < nsf; i++) {
        exc_out[i] = exc[i - start] * pitch_coef;
        exc[i] = exc_out[i];
    }
    pitch_val[0] = start;
    gain_val[0] = gain_val[2] = 0.f;
    gain_val[1] = pitch_coef;
}

static inline float speex_rand(float std, uint32_t *seed)
{
    const uint32_t jflone = 0x3f800000;
    const uint32_t jflmsk = 0x007fffff;
    float fran;
    uint32_t ran;
    seed[0] = 1664525 * seed[0] + 1013904223;
    ran = jflone | (jflmsk & seed[0]);
    fran = av_int2float(ran);
    fran -= 1.5f;
    fran *= std;
    return fran;
}

static void noise_codebook_unquant(float *exc, const void *par, int nsf,
                                   GetBitContext *gb, uint32_t *seed)
{
    for (int i = 0; i < nsf; i++)
        exc[i] = speex_rand(1.f, seed);
}

static void split_cb_shape_sign_unquant(float *exc, const void *par, int nsf,
                                        GetBitContext *gb, uint32_t *seed)
{
    int subvect_size, nb_subvect, have_sign, shape_bits;
    const SplitCodebookParams *params;
    const signed char *shape_cb;
    int signs[10], ind[10];

    params = par;
    subvect_size = params->subvect_size;
    nb_subvect = params->nb_subvect;

    shape_cb = params->shape_cb;
    have_sign = params->have_sign;
    shape_bits = params->shape_bits;

    /* Decode codewords and gains */
    for (int i = 0; i < nb_subvect; i++) {
        signs[i] = have_sign ? get_bits1(gb) : 0;
        ind[i] = get_bitsz(gb, shape_bits);
    }
    /* Compute decoded excitation */
    for (int i = 0; i < nb_subvect; i++) {
        const float s = signs[i] ? -1.f : 1.f;

        for (int j = 0; j < subvect_size; j++)
            exc[subvect_size * i + j] += s * 0.03125f * shape_cb[ind[i] * subvect_size + j];
    }
}

#define SUBMODE(x) st->submodes[st->submodeID]->x

#define gain_3tap_to_1tap(g) (FFABS(g[1]) + (g[0] > 0.f ? g[0] : -.5f * g[0]) + (g[2] > 0.f ? g[2] : -.5f * g[2]))

static void
pitch_unquant_3tap(float *exc, float *exc_out, int start, int end, float pitch_coef,
                   const void *par, int nsf, int *pitch_val, float *gain_val, GetBitContext *gb,
                   int count_lost, int subframe_offset, float last_pitch_gain, int cdbk_offset)
{
    int pitch, gain_index, gain_cdbk_size;
    const int8_t *gain_cdbk;
    const LtpParam *params;
    float gain[3];

    params = (const LtpParam *)par;
    gain_cdbk_size = 1 << params->gain_bits;
    gain_cdbk = params->gain_cdbk + 4 * gain_cdbk_size * cdbk_offset;

    pitch = get_bitsz(gb, params->pitch_bits);
    pitch += start;
    gain_index = get_bitsz(gb, params->gain_bits);
    gain[0] = 0.015625f * gain_cdbk[gain_index * 4] + .5f;
    gain[1] = 0.015625f * gain_cdbk[gain_index * 4 + 1] + .5f;
    gain[2] = 0.015625f * gain_cdbk[gain_index * 4 + 2] + .5f;

    if (count_lost && pitch > subframe_offset) {
        float tmp = count_lost < 4 ? last_pitch_gain : 0.5f * last_pitch_gain;
        float gain_sum;

        tmp = fminf(tmp, .95f);
        gain_sum = gain_3tap_to_1tap(gain);

        if (gain_sum > tmp && gain_sum > 0.f) {
            float fact = tmp / gain_sum;
            for (int i = 0; i < 3; i++)
                gain[i] *= fact;
        }
    }

    pitch_val[0] = pitch;
    gain_val[0] = gain[0];
    gain_val[1] = gain[1];
    gain_val[2] = gain[2];
    SPEEX_MEMSET(exc_out, 0, nsf);

    for (int i = 0; i < 3; i++) {
        int tmp1, tmp3;
        int pp = pitch + 1 - i;
        tmp1 = nsf;
        if (tmp1 > pp)
            tmp1 = pp;
        for (int j = 0; j < tmp1; j++)
            exc_out[j] += gain[2 - i] * exc[j - pp];
        tmp3 = nsf;
        if (tmp3 > pp + pitch)
            tmp3 = pp + pitch;
        for (int j = tmp1; j < tmp3; j++)
            exc_out[j] += gain[2 - i] * exc[j - pp - pitch];
    }
}

static void lsp_unquant_nb(float *lsp, int order, GetBitContext *gb)
{
    int id;

    for (int i = 0; i < order; i++)
        lsp[i] = LSP_LINEAR(i);

    id = get_bits(gb, 6);
    for (int i = 0; i < 10; i++)
        lsp[i] += LSP_DIV_256(cdbk_nb[id * 10 + i]);

    id = get_bits(gb, 6);
    for (int i = 0; i < 5; i++)
        lsp[i] += LSP_DIV_512(cdbk_nb_low1[id * 5 + i]);

    id = get_bits(gb, 6);
    for (int i = 0; i < 5; i++)
        lsp[i] += LSP_DIV_1024(cdbk_nb_low2[id * 5 + i]);

    id = get_bits(gb, 6);
    for (int i = 0; i < 5; i++)
        lsp[i + 5] += LSP_DIV_512(cdbk_nb_high1[id * 5 + i]);

    id = get_bits(gb, 6);
    for (int i = 0; i < 5; i++)
        lsp[i + 5] += LSP_DIV_1024(cdbk_nb_high2[id * 5 + i]);
}

static void lsp_unquant_high(float *lsp, int order, GetBitContext *gb)
{
    int id;

    for (int i = 0; i < order; i++)
        lsp[i] = LSP_LINEAR_HIGH(i);

    id = get_bits(gb, 6);
    for (int i = 0; i < order; i++)
        lsp[i] += LSP_DIV_256(high_lsp_cdbk[id * order + i]);

    id = get_bits(gb, 6);
    for (int i = 0; i < order; i++)
        lsp[i] += LSP_DIV_512(high_lsp_cdbk2[id * order + i]);
}

/* 2150 bps "vocoder-like" mode for comfort noise */
static const SpeexSubmode nb_submode1 = {
    0, 1, 0, 0, lsp_unquant_lbr, forced_pitch_unquant, NULL,
    noise_codebook_unquant, NULL, -1.f
};

/* 5.95 kbps very low bit-rate mode */
static const SpeexSubmode nb_submode2 = {
    0, 0, 0, 0, lsp_unquant_lbr, pitch_unquant_3tap, &ltp_params_vlbr,
    split_cb_shape_sign_unquant, &split_cb_nb_vlbr, .6f
};

/* 8 kbps low bit-rate mode */
static const SpeexSubmode nb_submode3 = {
    -1, 0, 1, 0, lsp_unquant_lbr, pitch_unquant_3tap, &ltp_params_lbr,
    split_cb_shape_sign_unquant, &split_cb_nb_lbr, .55f
};

/* 11 kbps medium bit-rate mode */
static const SpeexSubmode nb_submode4 = {
    -1, 0, 1, 0, lsp_unquant_lbr, pitch_unquant_3tap, &ltp_params_med,
    split_cb_shape_sign_unquant, &split_cb_nb_med, .45f
};

/* 15 kbps high bit-rate mode */
static const SpeexSubmode nb_submode5 = {
    -1, 0, 3, 0, lsp_unquant_nb, pitch_unquant_3tap, &ltp_params_nb,
    split_cb_shape_sign_unquant, &split_cb_nb, .25f
};

/* 18.2 high bit-rate mode */
static const SpeexSubmode nb_submode6 = {
    -1, 0, 3, 0, lsp_unquant_nb, pitch_unquant_3tap, &ltp_params_nb,
    split_cb_shape_sign_unquant, &split_cb_sb, .15f
};

/* 24.6 kbps high bit-rate mode */
static const SpeexSubmode nb_submode7 = {
    -1, 0, 3, 1, lsp_unquant_nb, pitch_unquant_3tap, &ltp_params_nb,
    split_cb_shape_sign_unquant, &split_cb_nb, 0.05f
};

/* 3.95 kbps very low bit-rate mode */
static const SpeexSubmode nb_submode8 = {
    0, 1, 0, 0, lsp_unquant_lbr, forced_pitch_unquant, NULL,
    split_cb_shape_sign_unquant, &split_cb_nb_ulbr, .5f
};

static const SpeexSubmode wb_submode1 = {
    0, 0, 1, 0, lsp_unquant_high, NULL, NULL,
    NULL, NULL, -1.f
};

static const SpeexSubmode wb_submode2 = {
    0, 0, 1, 0, lsp_unquant_high, NULL, NULL,
    split_cb_shape_sign_unquant, &split_cb_high_lbr, -1.f
};

static const SpeexSubmode wb_submode3 = {
    0, 0, 1, 0, lsp_unquant_high, NULL, NULL,
    split_cb_shape_sign_unquant, &split_cb_high, -1.f
};

static const SpeexSubmode wb_submode4 = {
    0, 0, 1, 1, lsp_unquant_high, NULL, NULL,
    split_cb_shape_sign_unquant, &split_cb_high, -1.f
};

static int nb_decode(AVCodecContext *, void *, GetBitContext *, float *, int packets_left);
static int sb_decode(AVCodecContext *, void *, GetBitContext *, float *, int packets_left);

static const SpeexMode speex_modes[SPEEX_NB_MODES] = {
    {
        .modeID = 0,
        .decode = nb_decode,
        .frame_size = NB_FRAME_SIZE,
        .subframe_size = NB_SUBFRAME_SIZE,
        .lpc_size = NB_ORDER,
        .submodes = {
            NULL, &nb_submode1, &nb_submode2, &nb_submode3, &nb_submode4,
            &nb_submode5, &nb_submode6, &nb_submode7, &nb_submode8
        },
        .default_submode = 5,
    },
    {
        .modeID = 1,
        .decode = sb_decode,
        .frame_size = NB_FRAME_SIZE,
        .subframe_size = NB_SUBFRAME_SIZE,
        .lpc_size = 8,
        .folding_gain = 0.9f,
        .submodes = {
            NULL, &wb_submode1, &wb_submode2, &wb_submode3, &wb_submode4
        },
        .default_submode = 3,
    },
    {
        .modeID = 2,
        .decode = sb_decode,
        .frame_size = 320,
        .subframe_size = 80,
        .lpc_size = 8,
        .folding_gain = 0.7f,
        .submodes = {
            NULL, &wb_submode1
        },
        .default_submode = 1,
    },
};

static float compute_rms(const float *x, int len)
{
    float sum = 0.f;

    for (int i = 0; i < len; i++)
        sum += x[i] * x[i];

    av_assert0(len > 0);
    return sqrtf(.1f + sum / len);
}

static void bw_lpc(float gamma, const float *lpc_in,
                   float *lpc_out, int order)
{
    float tmp = gamma;

    for (int i = 0; i < order; i++) {
        lpc_out[i] = tmp * lpc_in[i];
        tmp *= gamma;
    }
}

static void iir_mem(const float *x, const float *den,
    float *y, int N, int ord, float *mem)
{
    for (int i = 0; i < N; i++) {
        float yi = x[i] + mem[0];
        float nyi = -yi;
        for (int j = 0; j < ord - 1; j++)
            mem[j] = mem[j + 1] + den[j] * nyi;
        mem[ord - 1] = den[ord - 1] * nyi;
        y[i] = yi;
    }
}

static void highpass(const float *x, float *y, int len, float *mem, int wide)
{
    static const float Pcoef[2][3] = {{ 1.00000f, -1.92683f, 0.93071f }, { 1.00000f, -1.97226f, 0.97332f } };
    static const float Zcoef[2][3] = {{ 0.96446f, -1.92879f, 0.96446f }, { 0.98645f, -1.97277f, 0.98645f } };
    const float *den, *num;

    den = Pcoef[wide];
    num = Zcoef[wide];
    for (int i = 0; i < len; i++) {
        float yi = num[0] * x[i] + mem[0];
        mem[0] = mem[1] + num[1] * x[i] + -den[1] * yi;
        mem[1] = num[2] * x[i] + -den[2] * yi;
        y[i] = yi;
    }
}

#define median3(a, b, c)                                     \
    ((a) < (b) ? ((b) < (c) ? (b) : ((a) < (c) ? (c) : (a))) \
               : ((c) < (b) ? (b) : ((c) < (a) ? (c) : (a))))

static int speex_std_stereo(GetBitContext *gb, void *state, void *data)
{
    StereoState *stereo = data;
    float sign = get_bits1(gb) ? -1.f : 1.f;

    stereo->balance = exp(sign * .25f * get_bits(gb, 5));
    stereo->e_ratio = e_ratio_quant[get_bits(gb, 2)];

    return 0;
}

static int speex_inband_handler(GetBitContext *gb, void *state, StereoState *stereo)
{
    int id = get_bits(gb, 4);

    if (id == SPEEX_INBAND_STEREO) {
        return speex_std_stereo(gb, state, stereo);
    } else {
        int adv;

        if (id < 2)
            adv = 1;
        else if (id < 8)
            adv = 4;
        else if (id < 10)
            adv = 8;
        else if (id < 12)
            adv = 16;
        else if (id < 14)
            adv = 32;
        else
            adv = 64;
        skip_bits_long(gb, adv);
    }
    return 0;
}

static void sanitize_values(float *vec, float min_val, float max_val, int len)
{
    for (int i = 0; i < len; i++) {
        if (!isnormal(vec[i]) || fabsf(vec[i]) < 1e-8f)
            vec[i] = 0.f;
        else
            vec[i] = av_clipf(vec[i], min_val, max_val);
    }
}

static void signal_mul(const float *x, float *y, float scale, int len)
{
    for (int i = 0; i < len; i++)
        y[i] = scale * x[i];
}

static float inner_prod(const float *x, const float *y, int len)
{
    float sum = 0.f;

    for (int i = 0; i < len; i += 8) {
        float part = 0.f;
        part += x[i + 0] * y[i + 0];
        part += x[i + 1] * y[i + 1];
        part += x[i + 2] * y[i + 2];
        part += x[i + 3] * y[i + 3];
        part += x[i + 4] * y[i + 4];
        part += x[i + 5] * y[i + 5];
        part += x[i + 6] * y[i + 6];
        part += x[i + 7] * y[i + 7];
        sum += part;
    }

    return sum;
}

static int interp_pitch(const float *exc, float *interp, int pitch, int len)
{
    float corr[4][7], maxcorr;
    int maxi, maxj;

    for (int i = 0; i < 7; i++)
        corr[0][i] = inner_prod(exc, exc - pitch - 3 + i, len);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 7; j++) {
            int i1, i2;
            float tmp = 0.f;

            i1 = 3 - j;
            if (i1 < 0)
                i1 = 0;
            i2 = 10 - j;
            if (i2 > 7)
                i2 = 7;
            for (int k = i1; k < i2; k++)
                tmp += shift_filt[i][k] * corr[0][j + k - 3];
            corr[i + 1][j] = tmp;
        }
    }
    maxi = maxj = 0;
    maxcorr = corr[0][0];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 7; j++) {
            if (corr[i][j] > maxcorr) {
                maxcorr = corr[i][j];
                maxi = i;
                maxj = j;
            }
        }
    }
    for (int i = 0; i < len; i++) {
        float tmp = 0.f;
        if (maxi > 0.f) {
            for (int k = 0; k < 7; k++)
                tmp += exc[i - (pitch - maxj + 3) + k - 3] * shift_filt[maxi - 1][k];
        } else {
            tmp = exc[i - (pitch - maxj + 3)];
        }
        interp[i] = tmp;
    }
    return pitch - maxj + 3;
}

static void multicomb(const float *exc, float *new_exc, float *ak, int p, int nsf,
                      int pitch, int max_pitch, float comb_gain)
{
    float old_ener, new_ener;
    float iexc0_mag, iexc1_mag, exc_mag;
    float iexc[4 * NB_SUBFRAME_SIZE];
    float corr0, corr1, gain0, gain1;
    float pgain1, pgain2;
    float c1, c2, g1, g2;
    float ngain, gg1, gg2;
    int corr_pitch = pitch;

    interp_pitch(exc, iexc, corr_pitch, 80);
    if (corr_pitch > max_pitch)
        interp_pitch(exc, iexc + nsf, 2 * corr_pitch, 80);
    else
        interp_pitch(exc, iexc + nsf, -corr_pitch, 80);

    iexc0_mag = sqrtf(1000.f + inner_prod(iexc, iexc, nsf));
    iexc1_mag = sqrtf(1000.f + inner_prod(iexc + nsf, iexc + nsf, nsf));
    exc_mag = sqrtf(1.f + inner_prod(exc, exc, nsf));
    corr0 = inner_prod(iexc, exc, nsf);
    corr1 = inner_prod(iexc + nsf, exc, nsf);
    if (corr0 > iexc0_mag * exc_mag)
        pgain1 = 1.f;
    else
        pgain1 = (corr0 / exc_mag) / iexc0_mag;
    if (corr1 > iexc1_mag * exc_mag)
        pgain2 = 1.f;
    else
        pgain2 = (corr1 / exc_mag) / iexc1_mag;
    gg1 = exc_mag / iexc0_mag;
    gg2 = exc_mag / iexc1_mag;
    if (comb_gain > 0.f) {
        c1 = .4f * comb_gain + .07f;
        c2 = .5f + 1.72f * (c1 - .07f);
    } else {
        c1 = c2 = 0.f;
    }
    g1 = 1.f - c2 * pgain1 * pgain1;
    g2 = 1.f - c2 * pgain2 * pgain2;
    g1 = fmaxf(g1, c1);
    g2 = fmaxf(g2, c1);
    g1 = c1 / g1;
    g2 = c1 / g2;

    if (corr_pitch > max_pitch) {
        gain0 = .7f * g1 * gg1;
        gain1 = .3f * g2 * gg2;
    } else {
        gain0 = .6f * g1 * gg1;
        gain1 = .6f * g2 * gg2;
    }
    for (int i = 0; i < nsf; i++)
        new_exc[i] = exc[i] + (gain0 * iexc[i]) + (gain1 * iexc[i + nsf]);
    new_ener = compute_rms(new_exc, nsf);
    old_ener = compute_rms(exc, nsf);

    old_ener = fmaxf(old_ener, 1.f);
    new_ener = fmaxf(new_ener, 1.f);
    old_ener = fminf(old_ener, new_ener);
    ngain = old_ener / new_ener;

    for (int i = 0; i < nsf; i++)
        new_exc[i] *= ngain;
}

static void lsp_interpolate(const float *old_lsp, const float *new_lsp,
                            float *lsp, int len, int subframe,
                            int nb_subframes, float margin)
{
    const float tmp = (1.f + subframe) / nb_subframes;

    for (int i = 0; i < len; i++) {
        lsp[i] = (1.f - tmp) * old_lsp[i] + tmp * new_lsp[i];
        lsp[i] = av_clipf(lsp[i], margin, M_PI - margin);
    }
    for (int i = 1; i < len - 1; i++) {
        lsp[i] = fmaxf(lsp[i], lsp[i - 1] + margin);
        if (lsp[i] > lsp[i + 1] - margin)
            lsp[i] = .5f * (lsp[i] + lsp[i + 1] - margin);
    }
}

static void lsp_to_lpc(const float *freq, float *ak, int lpcrdr)
{
    float xout1, xout2, xin1, xin2;
    float *pw, *n0;
    float Wp[4 * NB_ORDER + 2] = { 0 };
    float x_freq[NB_ORDER];
    const int m = lpcrdr >> 1;

    pw = Wp;

    xin1 = xin2 = 1.f;

    for (int i = 0; i < lpcrdr; i++)
        x_freq[i] = -cosf(freq[i]);

    /* reconstruct P(z) and Q(z) by  cascading second order
     * polynomials in form 1 - 2xz(-1) +z(-2), where x is the
     * LSP coefficient
     */
    for (int j = 0; j <= lpcrdr; j++) {
        int i2 = 0;
        for (int i = 0; i < m; i++, i2 += 2) {
            n0 = pw + (i * 4);
            xout1 = xin1 + 2.f * x_freq[i2    ] * n0[0] + n0[1];
            xout2 = xin2 + 2.f * x_freq[i2 + 1] * n0[2] + n0[3];
            n0[1] = n0[0];
            n0[3] = n0[2];
            n0[0] = xin1;
            n0[2] = xin2;
            xin1 = xout1;
            xin2 = xout2;
        }
        xout1 = xin1 + n0[4];
        xout2 = xin2 - n0[5];
        if (j > 0)
            ak[j - 1] = (xout1 + xout2) * 0.5f;
        n0[4] = xin1;
        n0[5] = xin2;

        xin1 = 0.f;
        xin2 = 0.f;
    }
}

static int nb_decode(AVCodecContext *avctx, void *ptr_st,
                     GetBitContext *gb, float *out, int packets_left)
{
    DecoderState *st = ptr_st;
    float ol_gain = 0, ol_pitch_coef = 0, best_pitch_gain = 0, pitch_average = 0;
    int m, pitch, wideband, ol_pitch = 0, best_pitch = 40;
    SpeexContext *s = avctx->priv_data;
    float innov[NB_SUBFRAME_SIZE];
    float exc32[NB_SUBFRAME_SIZE];
    float interp_qlsp[NB_ORDER];
    float qlsp[NB_ORDER];
    float ak[NB_ORDER];
    float pitch_gain[3] = { 0 };

    st->exc = st->exc_buf + 2 * NB_PITCH_END + NB_SUBFRAME_SIZE + 6;

    if (st->encode_submode) {
        do { /* Search for next narrowband block (handle requests, skip wideband blocks) */
            if (get_bits_left(gb) < 5)
                return AVERROR_INVALIDDATA;
            wideband = get_bits1(gb);
            if (wideband) /* Skip wideband block (for compatibility) */ {
                int submode, advance;

                submode = get_bits(gb, SB_SUBMODE_BITS);
                advance = wb_skip_table[submode];
                advance -= SB_SUBMODE_BITS + 1;
                if (advance < 0)
                    return AVERROR_INVALIDDATA;
                skip_bits_long(gb, advance);

                if (get_bits_left(gb) < 5)
                    return AVERROR_INVALIDDATA;
                wideband = get_bits1(gb);
                if (wideband) {
                    submode = get_bits(gb, SB_SUBMODE_BITS);
                    advance = wb_skip_table[submode];
                    advance -= SB_SUBMODE_BITS + 1;
                    if (advance < 0)
                        return AVERROR_INVALIDDATA;
                    skip_bits_long(gb, advance);
                    wideband = get_bits1(gb);
                    if (wideband) {
                        av_log(avctx, AV_LOG_ERROR, "more than two wideband layers found\n");
                        return AVERROR_INVALIDDATA;
                    }
                }
            }
            if (get_bits_left(gb) < 4)
                return AVERROR_INVALIDDATA;
            m = get_bits(gb, 4);
            if (m == 15) /* We found a terminator */ {
                return AVERROR_INVALIDDATA;
            } else if (m == 14) /* Speex in-band request */ {
                int ret = speex_inband_handler(gb, st, &s->stereo);
                if (ret)
                    return ret;
            } else if (m == 13) /* User in-band request */ {
                int ret = speex_default_user_handler(gb, st, NULL);
                if (ret)
                    return ret;
            } else if (m > 8) /* Invalid mode */ {
                return AVERROR_INVALIDDATA;
            }
        } while (m > 8);

        st->submodeID = m; /* Get the sub-mode that was used */
    }

    /* Shift all buffers by one frame */
    memmove(st->exc_buf, st->exc_buf + NB_FRAME_SIZE, (2 * NB_PITCH_END + NB_SUBFRAME_SIZE + 12) * sizeof(float));

    /* If null mode (no transmission), just set a couple things to zero */
    if (st->submodes[st->submodeID] == NULL) {
        float lpc[NB_ORDER];
        float innov_gain = 0.f;

        bw_lpc(0.93f, st->interp_qlpc, lpc, NB_ORDER);
        innov_gain = compute_rms(st->exc, NB_FRAME_SIZE);
        for (int i = 0; i < NB_FRAME_SIZE; i++)
            st->exc[i] = speex_rand(innov_gain, &st->seed);

        /* Final signal synthesis from excitation */
        iir_mem(st->exc, lpc, out, NB_FRAME_SIZE, NB_ORDER, st->mem_sp);
        st->count_lost = 0;

        return 0;
    }

    /* Unquantize LSPs */
    SUBMODE(lsp_unquant)(qlsp, NB_ORDER, gb);

    /* Damp memory if a frame was lost and the LSP changed too much */
    if (st->count_lost) {
        float fact, lsp_dist = 0;

        for (int i = 0; i < NB_ORDER; i++)
            lsp_dist = lsp_dist + FFABS(st->old_qlsp[i] - qlsp[i]);
        fact = .6f * exp(-.2f * lsp_dist);
        for (int i = 0; i < NB_ORDER; i++)
            st->mem_sp[i] = fact * st->mem_sp[i];
    }

    /* Handle first frame and lost-packet case */
    if (st->first || st->count_lost)
        memcpy(st->old_qlsp, qlsp, sizeof(st->old_qlsp));

    /* Get open-loop pitch estimation for low bit-rate pitch coding */
    if (SUBMODE(lbr_pitch) != -1)
        ol_pitch = NB_PITCH_START + get_bits(gb, 7);

    if (SUBMODE(forced_pitch_gain))
        ol_pitch_coef = 0.066667f * get_bits(gb, 4);

    /* Get global excitation gain */
    ol_gain = expf(get_bits(gb, 5) / 3.5f);

    if (st->submodeID == 1)
        st->dtx_enabled = get_bits(gb, 4) == 15;

    if (st->submodeID > 1)
        st->dtx_enabled = 0;

    for (int sub = 0; sub < NB_NB_SUBFRAMES; sub++) { /* Loop on subframes */
        float *exc, *innov_save = NULL, tmp, ener;
        int pit_min, pit_max, offset, q_energy;

        offset = NB_SUBFRAME_SIZE * sub; /* Offset relative to start of frame */
        exc = st->exc + offset; /* Excitation */
        if (st->innov_save) /* Original signal */
            innov_save = st->innov_save + offset;

        SPEEX_MEMSET(exc, 0, NB_SUBFRAME_SIZE); /* Reset excitation */

        /* Adaptive codebook contribution */
        av_assert0(SUBMODE(ltp_unquant));
        /* Handle pitch constraints if any */
        if (SUBMODE(lbr_pitch) != -1) {
            int margin = SUBMODE(lbr_pitch);

            if (margin) {
                pit_min = ol_pitch - margin + 1;
                pit_min = FFMAX(pit_min, NB_PITCH_START);
                pit_max = ol_pitch + margin;
                pit_max = FFMIN(pit_max, NB_PITCH_START);
            } else {
                pit_min = pit_max = ol_pitch;
            }
        } else {
            pit_min = NB_PITCH_START;
            pit_max = NB_PITCH_END;
        }

        SUBMODE(ltp_unquant)(exc, exc32, pit_min, pit_max, ol_pitch_coef, SUBMODE(LtpParam),
                             NB_SUBFRAME_SIZE, &pitch, pitch_gain, gb, st->count_lost, offset,
                             st->last_pitch_gain, 0);

        sanitize_values(exc32, -32000, 32000, NB_SUBFRAME_SIZE);

        tmp = gain_3tap_to_1tap(pitch_gain);

        pitch_average += tmp;
        if ((tmp > best_pitch_gain &&
             FFABS(2 * best_pitch - pitch) >= 3 &&
             FFABS(3 * best_pitch - pitch) >= 4 &&
             FFABS(4 * best_pitch - pitch) >= 5) ||
            (tmp > .6f * best_pitch_gain &&
             (FFABS(best_pitch - 2 * pitch) < 3 ||
              FFABS(best_pitch - 3 * pitch) < 4 ||
              FFABS(best_pitch - 4 * pitch) < 5)) ||
            ((.67f * tmp) > best_pitch_gain &&
             (FFABS(2 * best_pitch - pitch) < 3 ||
              FFABS(3 * best_pitch - pitch) < 4 ||
              FFABS(4 * best_pitch - pitch) < 5))) {
            best_pitch = pitch;
            if (tmp > best_pitch_gain)
                best_pitch_gain = tmp;
        }

        memset(innov, 0, sizeof(innov));

        /* Decode sub-frame gain correction */
        if (SUBMODE(have_subframe_gain) == 3) {
            q_energy = get_bits(gb, 3);
            ener = exc_gain_quant_scal3[q_energy] * ol_gain;
        } else if (SUBMODE(have_subframe_gain) == 1) {
            q_energy = get_bits1(gb);
            ener = exc_gain_quant_scal1[q_energy] * ol_gain;
        } else {
            ener = ol_gain;
        }

        av_assert0(SUBMODE(innovation_unquant));
        /* Fixed codebook contribution */
        SUBMODE(innovation_unquant)(innov, SUBMODE(innovation_params), NB_SUBFRAME_SIZE, gb, &st->seed);
        /* De-normalize innovation and update excitation */

        signal_mul(innov, innov, ener, NB_SUBFRAME_SIZE);

        /* Decode second codebook (only for some modes) */
        if (SUBMODE(double_codebook)) {
            float innov2[NB_SUBFRAME_SIZE] = { 0 };

            SUBMODE(innovation_unquant)(innov2, SUBMODE(innovation_params), NB_SUBFRAME_SIZE, gb, &st->seed);
            signal_mul(innov2, innov2, 0.454545f * ener, NB_SUBFRAME_SIZE);
            for (int i = 0; i < NB_SUBFRAME_SIZE; i++)
                innov[i] += innov2[i];
        }
        for (int i = 0; i < NB_SUBFRAME_SIZE; i++)
            exc[i] = exc32[i] + innov[i];
        if (innov_save)
            memcpy(innov_save, innov, sizeof(innov));

        /* Vocoder mode */
        if (st->submodeID == 1) {
            float g = ol_pitch_coef;

            g = av_clipf(1.5f * (g - .2f), 0.f, 1.f);

            SPEEX_MEMSET(exc, 0, NB_SUBFRAME_SIZE);
            while (st->voc_offset < NB_SUBFRAME_SIZE) {
                if (st->voc_offset >= 0)
                    exc[st->voc_offset] = sqrtf(2.f * ol_pitch) * (g * ol_gain);
                st->voc_offset += ol_pitch;
            }
            st->voc_offset -= NB_SUBFRAME_SIZE;

            for (int i = 0; i < NB_SUBFRAME_SIZE; i++) {
                float exci = exc[i];
                exc[i] = (.7f * exc[i] + .3f * st->voc_m1) + ((1.f - .85f * g) * innov[i]) - .15f * g * st->voc_m2;
                st->voc_m1 = exci;
                st->voc_m2 = innov[i];
                st->voc_mean = .8f * st->voc_mean + .2f * exc[i];
                exc[i] -= st->voc_mean;
            }
        }
    }

    if (st->lpc_enh_enabled && SUBMODE(comb_gain) > 0 && !st->count_lost) {
        multicomb(st->exc - NB_SUBFRAME_SIZE, out, st->interp_qlpc, NB_ORDER,
            2 * NB_SUBFRAME_SIZE, best_pitch, 40, SUBMODE(comb_gain));
        multicomb(st->exc + NB_SUBFRAME_SIZE, out + 2 * NB_SUBFRAME_SIZE,
            st->interp_qlpc, NB_ORDER, 2 * NB_SUBFRAME_SIZE, best_pitch, 40,
            SUBMODE(comb_gain));
    } else {
        SPEEX_COPY(out, &st->exc[-NB_SUBFRAME_SIZE], NB_FRAME_SIZE);
    }

    /* If the last packet was lost, re-scale the excitation to obtain the same
     * energy as encoded in ol_gain */
    if (st->count_lost) {
        float exc_ener, gain;

        exc_ener = compute_rms(st->exc, NB_FRAME_SIZE);
        av_assert0(exc_ener + 1.f > 0.f);
        gain = fminf(ol_gain / (exc_ener + 1.f), 2.f);
        for (int i = 0; i < NB_FRAME_SIZE; i++) {
            st->exc[i] *= gain;
            out[i] = st->exc[i - NB_SUBFRAME_SIZE];
        }
    }

    for (int sub = 0; sub < NB_NB_SUBFRAMES; sub++) { /* Loop on subframes */
        const int offset = NB_SUBFRAME_SIZE * sub; /* Offset relative to start of frame */
        float pi_g = 1.f, *sp = out + offset; /* Original signal */

        lsp_interpolate(st->old_qlsp, qlsp, interp_qlsp, NB_ORDER, sub, NB_NB_SUBFRAMES, 0.002f);
        lsp_to_lpc(interp_qlsp, ak, NB_ORDER); /* Compute interpolated LPCs (unquantized) */

        for (int i = 0; i < NB_ORDER; i += 2) /* Compute analysis filter at w=pi */
            pi_g += ak[i + 1] - ak[i];
        st->pi_gain[sub] = pi_g;
        st->exc_rms[sub] = compute_rms(st->exc + offset, NB_SUBFRAME_SIZE);

        iir_mem(sp, st->interp_qlpc, sp, NB_SUBFRAME_SIZE, NB_ORDER, st->mem_sp);

        memcpy(st->interp_qlpc, ak, sizeof(st->interp_qlpc));
    }

    if (st->highpass_enabled)
        highpass(out, out, NB_FRAME_SIZE, st->mem_hp, st->is_wideband);

    /* Store the LSPs for interpolation in the next frame */
    memcpy(st->old_qlsp, qlsp, sizeof(st->old_qlsp));

    st->count_lost = 0;
    st->last_pitch = best_pitch;
    st->last_pitch_gain = .25f * pitch_average;
    st->last_ol_gain = ol_gain;
    st->first = 0;

    return 0;
}

static void qmf_synth(const float *x1, const float *x2, const float *a, float *y, int N, int M, float *mem1, float *mem2)
{
    const int M2 = M >> 1, N2 = N >> 1;
    float xx1[352], xx2[352];

    for (int i = 0; i < N2; i++)
        xx1[i] = x1[N2-1-i];
    for (int i = 0; i < M2; i++)
        xx1[N2+i] = mem1[2*i+1];
    for (int i = 0; i < N2; i++)
        xx2[i] = x2[N2-1-i];
    for (int i = 0; i < M2; i++)
        xx2[N2+i] = mem2[2*i+1];

    for (int i = 0; i < N2; i += 2) {
        float y0, y1, y2, y3;
        float x10, x20;

        y0 = y1 = y2 = y3 = 0.f;
        x10 = xx1[N2-2-i];
        x20 = xx2[N2-2-i];

        for (int j = 0; j < M2; j += 2) {
            float x11, x21;
            float a0, a1;

            a0 = a[2*j];
            a1 = a[2*j+1];
            x11 = xx1[N2-1+j-i];
            x21 = xx2[N2-1+j-i];

            y0 += a0 * (x11-x21);
            y1 += a1 * (x11+x21);
            y2 += a0 * (x10-x20);
            y3 += a1 * (x10+x20);
            a0 = a[2*j+2];
            a1 = a[2*j+3];
            x10 = xx1[N2+j-i];
            x20 = xx2[N2+j-i];

            y0 += a0 * (x10-x20);
            y1 += a1 * (x10+x20);
            y2 += a0 * (x11-x21);
            y3 += a1 * (x11+x21);
        }
        y[2 * i  ] = 2.f * y0;
        y[2 * i+1] = 2.f * y1;
        y[2 * i+2] = 2.f * y2;
        y[2 * i+3] = 2.f * y3;
    }

    for (int i = 0; i < M2; i++)
        mem1[2*i+1] = xx1[i];
    for (int i = 0; i < M2; i++)
        mem2[2*i+1] = xx2[i];
}

static int sb_decode(AVCodecContext *avctx, void *ptr_st,
                     GetBitContext *gb, float *out, int packets_left)
{
    SpeexContext *s = avctx->priv_data;
    DecoderState *st = ptr_st;
    float low_pi_gain[NB_NB_SUBFRAMES];
    float low_exc_rms[NB_NB_SUBFRAMES];
    float interp_qlsp[NB_ORDER];
    int ret, wideband;
    float *low_innov_alias;
    float qlsp[NB_ORDER];
    float ak[NB_ORDER];
    const SpeexMode *mode;

    mode = st->mode;

    if (st->modeID > 0) {
        if (packets_left <= 1)
            return AVERROR_INVALIDDATA;
        low_innov_alias = out + st->frame_size;
        s->st[st->modeID - 1].innov_save = low_innov_alias;
        ret = speex_modes[st->modeID - 1].decode(avctx, &s->st[st->modeID - 1], gb, out, packets_left);
        if (ret < 0)
            return ret;
    }

    if (st->encode_submode) { /* Check "wideband bit" */
        if (get_bits_left(gb) > 0)
            wideband = show_bits1(gb);
        else
            wideband = 0;
        if (wideband) { /* Regular wideband frame, read the submode */
            wideband = get_bits1(gb);
            st->submodeID = get_bits(gb, SB_SUBMODE_BITS);
        } else { /* Was a narrowband frame, set "null submode" */
            st->submodeID = 0;
        }
        if (st->submodeID != 0 && st->submodes[st->submodeID] == NULL)
            return AVERROR_INVALIDDATA;
    }

    /* If null mode (no transmission), just set a couple things to zero */
    if (st->submodes[st->submodeID] == NULL) {
        for (int i = 0; i < st->frame_size; i++)
            out[st->frame_size + i] = 1e-15f;

        st->first = 1;

        /* Final signal synthesis from excitation */
        iir_mem(out + st->frame_size, st->interp_qlpc, out + st->frame_size, st->frame_size, st->lpc_size, st->mem_sp);

        qmf_synth(out, out + st->frame_size, h0, out, st->full_frame_size, QMF_ORDER, st->g0_mem, st->g1_mem);

        return 0;
    }

    memcpy(low_pi_gain, s->st[st->modeID - 1].pi_gain, sizeof(low_pi_gain));
    memcpy(low_exc_rms, s->st[st->modeID - 1].exc_rms, sizeof(low_exc_rms));

    SUBMODE(lsp_unquant)(qlsp, st->lpc_size, gb);

    if (st->first)
        memcpy(st->old_qlsp, qlsp, sizeof(st->old_qlsp));

    for (int sub = 0; sub < st->nb_subframes; sub++) {
        float filter_ratio, el, rl, rh;
        float *innov_save = NULL, *sp;
        float exc[80];
        int offset;

        offset = st->subframe_size * sub;
        sp = out + st->frame_size + offset;
        /* Pointer for saving innovation */
        if (st->innov_save) {
            innov_save = st->innov_save + 2 * offset;
            SPEEX_MEMSET(innov_save, 0, 2 * st->subframe_size);
        }

        av_assert0(st->nb_subframes > 0);
        lsp_interpolate(st->old_qlsp, qlsp, interp_qlsp, st->lpc_size, sub, st->nb_subframes, 0.05f);
        lsp_to_lpc(interp_qlsp, ak, st->lpc_size);

        /* Calculate reponse ratio between the low and high filter in the middle
           of the band (4000 Hz) */
        st->pi_gain[sub] = 1.f;
        rh = 1.f;
        for (int i = 0; i < st->lpc_size; i += 2) {
            rh += ak[i + 1] - ak[i];
            st->pi_gain[sub] += ak[i] + ak[i + 1];
        }

        rl = low_pi_gain[sub];
        filter_ratio = (rl + .01f) / (rh + .01f);

        SPEEX_MEMSET(exc, 0, st->subframe_size);
        if (!SUBMODE(innovation_unquant)) {
            const int x = get_bits(gb, 5);
            const float g = expf(.125f * (x - 10)) / filter_ratio;

            for (int i = 0; i < st->subframe_size; i += 2) {
                exc[i    ] =  mode->folding_gain * low_innov_alias[offset + i    ] * g;
                exc[i + 1] = -mode->folding_gain * low_innov_alias[offset + i + 1] * g;
            }
        } else {
            float gc, scale;

            el = low_exc_rms[sub];
            gc = 0.87360f * gc_quant_bound[get_bits(gb, 4)];

            if (st->subframe_size == 80)
                gc *= M_SQRT2;

            scale = (gc * el) / filter_ratio;
            SUBMODE(innovation_unquant)
                (exc, SUBMODE(innovation_params), st->subframe_size,
                 gb, &st->seed);

            signal_mul(exc, exc, scale, st->subframe_size);
            if (SUBMODE(double_codebook)) {
                float innov2[80];

                SPEEX_MEMSET(innov2, 0, st->subframe_size);
                SUBMODE(innovation_unquant)(innov2, SUBMODE(innovation_params), st->subframe_size, gb, &st->seed);
                signal_mul(innov2, innov2, 0.4f * scale, st->subframe_size);
                for (int i = 0; i < st->subframe_size; i++)
                    exc[i] += innov2[i];
            }
        }

        if (st->innov_save) {
            for (int i = 0; i < st->subframe_size; i++)
                innov_save[2 * i] = exc[i];
        }

        iir_mem(st->exc_buf, st->interp_qlpc, sp, st->subframe_size, st->lpc_size, st->mem_sp);
        memcpy(st->exc_buf, exc, sizeof(exc));
        memcpy(st->interp_qlpc, ak, sizeof(st->interp_qlpc));
        st->exc_rms[sub] = compute_rms(st->exc_buf, st->subframe_size);
    }

    qmf_synth(out, out + st->frame_size, h0, out, st->full_frame_size, QMF_ORDER, st->g0_mem, st->g1_mem);
    memcpy(st->old_qlsp, qlsp, sizeof(st->old_qlsp));

    st->first = 0;

    return 0;
}

static int decoder_init(SpeexContext *s, DecoderState *st, const SpeexMode *mode)
{
    st->mode = mode;
    st->modeID = mode->modeID;

    st->first = 1;
    st->encode_submode = 1;
    st->is_wideband = st->modeID > 0;
    st->innov_save = NULL;

    st->submodes = mode->submodes;
    st->submodeID = mode->default_submode;
    st->subframe_size = mode->subframe_size;
    st->lpc_size = mode->lpc_size;
    st->full_frame_size = (1 + (st->modeID > 0)) * mode->frame_size;
    st->nb_subframes = mode->frame_size / mode->subframe_size;
    st->frame_size = mode->frame_size;

    st->lpc_enh_enabled = 1;

    st->last_pitch = 40;
    st->count_lost = 0;
    st->seed = 1000;
    st->last_ol_gain = 0;

    st->voc_m1 = st->voc_m2 = st->voc_mean = 0;
    st->voc_offset = 0;
    st->dtx_enabled = 0;
    st->highpass_enabled = mode->modeID == 0;

    return 0;
}

static int parse_speex_extradata(AVCodecContext *avctx,
    const uint8_t *extradata, int extradata_size)
{
    SpeexContext *s = avctx->priv_data;
    const uint8_t *buf = av_strnstr(extradata, "Speex   ", extradata_size);

    if (!buf)
        return AVERROR_INVALIDDATA;

    buf += 28;

    s->version_id = bytestream_get_le32(&buf);
    buf += 4;
    s->rate = bytestream_get_le32(&buf);
    if (s->rate <= 0)
        return AVERROR_INVALIDDATA;
    s->mode = bytestream_get_le32(&buf);
    if (s->mode < 0 || s->mode >= SPEEX_NB_MODES)
        return AVERROR_INVALIDDATA;
    s->bitstream_version = bytestream_get_le32(&buf);
    if (s->bitstream_version != 4)
        return AVERROR_INVALIDDATA;
    s->nb_channels = bytestream_get_le32(&buf);
    if (s->nb_channels <= 0 || s->nb_channels > 2)
        return AVERROR_INVALIDDATA;
    s->bitrate = bytestream_get_le32(&buf);
    s->frame_size = bytestream_get_le32(&buf);
    if (s->frame_size < NB_FRAME_SIZE << (s->mode > 1) ||
        s->frame_size >     INT32_MAX >> (s->mode > 1))
        return AVERROR_INVALIDDATA;
    s->frame_size = FFMIN(s->frame_size << (s->mode > 1), NB_FRAME_SIZE << s->mode);
    s->vbr = bytestream_get_le32(&buf);
    s->frames_per_packet = bytestream_get_le32(&buf);
    if (s->frames_per_packet <= 0 ||
        s->frames_per_packet > 64 ||
        s->frames_per_packet >= INT32_MAX / s->nb_channels / s->frame_size)
        return AVERROR_INVALIDDATA;
    s->extra_headers = bytestream_get_le32(&buf);

    return 0;
}

static av_cold int speex_decode_init(AVCodecContext *avctx)
{
    SpeexContext *s = avctx->priv_data;
    int ret;

    s->fdsp = avpriv_float_dsp_alloc(0);
    if (!s->fdsp)
        return AVERROR(ENOMEM);

    if (avctx->extradata && avctx->extradata_size >= 80) {
        ret = parse_speex_extradata(avctx, avctx->extradata, avctx->extradata_size);
        if (ret < 0)
            return ret;
    } else {
        s->rate = avctx->sample_rate;
        if (s->rate <= 0)
            return AVERROR_INVALIDDATA;

        s->nb_channels = avctx->ch_layout.nb_channels;
        if (s->nb_channels <= 0 || s->nb_channels > 2)
            return AVERROR_INVALIDDATA;

        switch (s->rate) {
        case 8000:  s->mode = 0; break;
        case 16000: s->mode = 1; break;
        case 32000: s->mode = 2; break;
        default: s->mode = 2;
        }

        s->frames_per_packet = 64;
        s->frame_size = NB_FRAME_SIZE << s->mode;
    }

    if (avctx->codec_tag == MKTAG('S', 'P', 'X', 'N')) {
        int quality;

        if (!avctx->extradata || avctx->extradata && avctx->extradata_size < 47) {
            av_log(avctx, AV_LOG_ERROR, "Missing or invalid extradata.\n");
            return AVERROR_INVALIDDATA;
        }

        quality = avctx->extradata[37];
        if (quality > 10) {
            av_log(avctx, AV_LOG_ERROR, "Unsupported quality mode %d.\n", quality);
            return AVERROR_PATCHWELCOME;
        }

        s->pkt_size = ((const uint8_t[]){ 5, 10, 15, 20, 20, 28, 28, 38, 38, 46, 62 })[quality];

        s->mode = 0;
        s->nb_channels = 1;
        s->rate = avctx->sample_rate;
        if (s->rate <= 0)
            return AVERROR_INVALIDDATA;
        s->frames_per_packet = 1;
        s->frame_size = NB_FRAME_SIZE;
    }

    if (s->bitrate > 0)
        avctx->bit_rate = s->bitrate;
    av_channel_layout_uninit(&avctx->ch_layout);
    avctx->ch_layout.order       = AV_CHANNEL_ORDER_UNSPEC;
    avctx->ch_layout.nb_channels = s->nb_channels;
    avctx->sample_rate = s->rate;
    avctx->sample_fmt = AV_SAMPLE_FMT_FLT;

    for (int m = 0; m <= s->mode; m++) {
        ret = decoder_init(s, &s->st[m], &speex_modes[m]);
        if (ret < 0)
            return ret;
    }

    s->stereo.balance = 1.f;
    s->stereo.e_ratio = .5f;
    s->stereo.smooth_left = 1.f;
    s->stereo.smooth_right = 1.f;

    return 0;
}

static void speex_decode_stereo(float *data, int frame_size, StereoState *stereo)
{
    float balance, e_left, e_right, e_ratio;

    balance = stereo->balance;
    e_ratio = stereo->e_ratio;

    /* These two are Q14, with max value just below 2. */
    e_right = 1.f / sqrtf(e_ratio * (1.f + balance));
    e_left = sqrtf(balance) * e_right;

    for (int i = frame_size - 1; i >= 0; i--) {
        float tmp = data[i];
        stereo->smooth_left  = stereo->smooth_left  * 0.98f + e_left  * 0.02f;
        stereo->smooth_right = stereo->smooth_right * 0.98f + e_right * 0.02f;
        data[2 * i    ] = stereo->smooth_left  * tmp;
        data[2 * i + 1] = stereo->smooth_right * tmp;
    }
}

static int speex_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                              int *got_frame_ptr, AVPacket *avpkt)
{
    SpeexContext *s = avctx->priv_data;
    int frames_per_packet = s->frames_per_packet;
    const float scale = 1.f / 32768.f;
    int buf_size = avpkt->size;
    float *dst;
    int ret;

    if (s->pkt_size && avpkt->size == 62)
        buf_size = s->pkt_size;
    if ((ret = init_get_bits8(&s->gb, avpkt->data, buf_size)) < 0)
        return ret;

    frame->nb_samples = FFALIGN(s->frame_size * frames_per_packet, 4);
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    dst = (float *)frame->extended_data[0];
    for (int i = 0; i < frames_per_packet; i++) {
        ret = speex_modes[s->mode].decode(avctx, &s->st[s->mode], &s->gb, dst + i * s->frame_size, frames_per_packet - i);
        if (ret < 0)
            return ret;
        if (avctx->ch_layout.nb_channels == 2)
            speex_decode_stereo(dst + i * s->frame_size, s->frame_size, &s->stereo);
        if (get_bits_left(&s->gb) < 5 ||
            show_bits(&s->gb, 5) == 15) {
            frames_per_packet = i + 1;
            break;
        }
    }

    dst = (float *)frame->extended_data[0];
    s->fdsp->vector_fmul_scalar(dst, dst, scale, frame->nb_samples * frame->ch_layout.nb_channels);
    frame->nb_samples = s->frame_size * frames_per_packet;

    *got_frame_ptr = 1;

    return (get_bits_count(&s->gb) + 7) >> 3;
}

static av_cold int speex_decode_close(AVCodecContext *avctx)
{
    SpeexContext *s = avctx->priv_data;
    av_freep(&s->fdsp);
    return 0;
}

const FFCodec ff_speex_decoder = {
    .p.name         = "speex",
    CODEC_LONG_NAME("Speex"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_SPEEX,
    .init           = speex_decode_init,
    FF_CODEC_DECODE_CB(speex_decode_frame),
    .close          = speex_decode_close,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_CHANNEL_CONF,
    .priv_data_size = sizeof(SpeexContext),
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
