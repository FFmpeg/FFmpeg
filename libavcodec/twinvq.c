/*
 * TwinVQ decoder
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

#include "avcodec.h"
#include "get_bits.h"
#include "dsputil.h"
#include "fft.h"
#include "lsp.h"
#include "sinewin.h"

#include <math.h>
#include <stdint.h>

#include "twinvq_data.h"

enum FrameType {
    FT_SHORT = 0,  ///< Short frame  (divided in n   sub-blocks)
    FT_MEDIUM,     ///< Medium frame (divided in m<n sub-blocks)
    FT_LONG,       ///< Long frame   (single sub-block + PPC)
    FT_PPC,        ///< Periodic Peak Component (part of the long frame)
};

/**
 * Parameters and tables that are different for each frame type
 */
struct FrameMode {
    uint8_t         sub;      ///< Number subblocks in each frame
    const uint16_t *bark_tab;

    /** number of distinct bark scale envelope values */
    uint8_t         bark_env_size;

    const int16_t  *bark_cb;    ///< codebook for the bark scale envelope (BSE)
    uint8_t         bark_n_coef;///< number of BSE CB coefficients to read
    uint8_t         bark_n_bit; ///< number of bits of the BSE coefs

    //@{
    /** main codebooks for spectrum data */
    const int16_t    *cb0;
    const int16_t    *cb1;
    //@}

    uint8_t         cb_len_read; ///< number of spectrum coefficients to read
};

/**
 * Parameters and tables that are different for every combination of
 * bitrate/sample rate
 */
typedef struct {
    struct FrameMode fmode[3]; ///< frame type-dependant parameters

    uint16_t     size;        ///< frame size in samples
    uint8_t      n_lsp;       ///< number of lsp coefficients
    const float *lspcodebook;

    /* number of bits of the different LSP CB coefficients */
    uint8_t      lsp_bit0;
    uint8_t      lsp_bit1;
    uint8_t      lsp_bit2;

    uint8_t      lsp_split;      ///< number of CB entries for the LSP decoding
    const int16_t *ppc_shape_cb; ///< PPC shape CB

    /** number of the bits for the PPC period value */
    uint8_t      ppc_period_bit;

    uint8_t      ppc_shape_bit;  ///< number of bits of the PPC shape CB coeffs
    uint8_t      ppc_shape_len;  ///< size of PPC shape CB
    uint8_t      pgain_bit;      ///< bits for PPC gain

    /** constant for peak period to peak width conversion */
    uint16_t     peak_per2wid;
} ModeTab;

static const ModeTab mode_08_08 = {
    {
        { 8, bark_tab_s08_64,  10, tab.fcb08s  , 1, 5, tab.cb0808s0, tab.cb0808s1, 18},
        { 2, bark_tab_m08_256, 20, tab.fcb08m  , 2, 5, tab.cb0808m0, tab.cb0808m1, 16},
        { 1, bark_tab_l08_512, 30, tab.fcb08l  , 3, 6, tab.cb0808l0, tab.cb0808l1, 17}
    },
    512 , 12, tab.lsp08,   1, 5, 3, 3, tab.shape08  , 8, 28, 20, 6, 40
};

static const ModeTab mode_11_08 = {
    {
        { 8, bark_tab_s11_64,  10, tab.fcb11s  , 1, 5, tab.cb1108s0, tab.cb1108s1, 29},
        { 2, bark_tab_m11_256, 20, tab.fcb11m  , 2, 5, tab.cb1108m0, tab.cb1108m1, 24},
        { 1, bark_tab_l11_512, 30, tab.fcb11l  , 3, 6, tab.cb1108l0, tab.cb1108l1, 27}
    },
    512 , 16, tab.lsp11,   1, 6, 4, 3, tab.shape11  , 9, 36, 30, 7, 90
};

static const ModeTab mode_11_10 = {
    {
        { 8, bark_tab_s11_64,  10, tab.fcb11s  , 1, 5, tab.cb1110s0, tab.cb1110s1, 21},
        { 2, bark_tab_m11_256, 20, tab.fcb11m  , 2, 5, tab.cb1110m0, tab.cb1110m1, 18},
        { 1, bark_tab_l11_512, 30, tab.fcb11l  , 3, 6, tab.cb1110l0, tab.cb1110l1, 20}
    },
    512 , 16, tab.lsp11,   1, 6, 4, 3, tab.shape11  , 9, 36, 30, 7, 90
};

static const ModeTab mode_16_16 = {
    {
        { 8, bark_tab_s16_128, 10, tab.fcb16s  , 1, 5, tab.cb1616s0, tab.cb1616s1, 16},
        { 2, bark_tab_m16_512, 20, tab.fcb16m  , 2, 5, tab.cb1616m0, tab.cb1616m1, 15},
        { 1, bark_tab_l16_1024,30, tab.fcb16l  , 3, 6, tab.cb1616l0, tab.cb1616l1, 16}
    },
    1024, 16, tab.lsp16,   1, 6, 4, 3, tab.shape16  , 9, 56, 60, 7, 180
};

static const ModeTab mode_22_20 = {
    {
        { 8, bark_tab_s22_128, 10, tab.fcb22s_1, 1, 6, tab.cb2220s0, tab.cb2220s1, 18},
        { 2, bark_tab_m22_512, 20, tab.fcb22m_1, 2, 6, tab.cb2220m0, tab.cb2220m1, 17},
        { 1, bark_tab_l22_1024,32, tab.fcb22l_1, 4, 6, tab.cb2220l0, tab.cb2220l1, 18}
    },
    1024, 16, tab.lsp22_1, 1, 6, 4, 3, tab.shape22_1, 9, 56, 36, 7, 144
};

static const ModeTab mode_22_24 = {
    {
        { 8, bark_tab_s22_128, 10, tab.fcb22s_1, 1, 6, tab.cb2224s0, tab.cb2224s1, 15},
        { 2, bark_tab_m22_512, 20, tab.fcb22m_1, 2, 6, tab.cb2224m0, tab.cb2224m1, 14},
        { 1, bark_tab_l22_1024,32, tab.fcb22l_1, 4, 6, tab.cb2224l0, tab.cb2224l1, 15}
    },
    1024, 16, tab.lsp22_1, 1, 6, 4, 3, tab.shape22_1, 9, 56, 36, 7, 144
};

static const ModeTab mode_22_32 = {
    {
        { 4, bark_tab_s22_128, 10, tab.fcb22s_2, 1, 6, tab.cb2232s0, tab.cb2232s1, 11},
        { 2, bark_tab_m22_256, 20, tab.fcb22m_2, 2, 6, tab.cb2232m0, tab.cb2232m1, 11},
        { 1, bark_tab_l22_512, 32, tab.fcb22l_2, 4, 6, tab.cb2232l0, tab.cb2232l1, 12}
    },
    512 , 16, tab.lsp22_2, 1, 6, 4, 4, tab.shape22_2, 9, 56, 36, 7, 72
};

static const ModeTab mode_44_40 = {
    {
        {16, bark_tab_s44_128, 10, tab.fcb44s  , 1, 6, tab.cb4440s0, tab.cb4440s1, 18},
        { 4, bark_tab_m44_512, 20, tab.fcb44m  , 2, 6, tab.cb4440m0, tab.cb4440m1, 17},
        { 1, bark_tab_l44_2048,40, tab.fcb44l  , 4, 6, tab.cb4440l0, tab.cb4440l1, 17}
    },
    2048, 20, tab.lsp44,   1, 6, 4, 4, tab.shape44  , 9, 84, 54, 7, 432
};

static const ModeTab mode_44_48 = {
    {
        {16, bark_tab_s44_128, 10, tab.fcb44s  , 1, 6, tab.cb4448s0, tab.cb4448s1, 15},
        { 4, bark_tab_m44_512, 20, tab.fcb44m  , 2, 6, tab.cb4448m0, tab.cb4448m1, 14},
        { 1, bark_tab_l44_2048,40, tab.fcb44l  , 4, 6, tab.cb4448l0, tab.cb4448l1, 14}
    },
    2048, 20, tab.lsp44,   1, 6, 4, 4, tab.shape44  , 9, 84, 54, 7, 432
};

typedef struct TwinContext {
    AVCodecContext *avctx;
    AVFrame frame;
    DSPContext      dsp;
    FFTContext mdct_ctx[3];

    const ModeTab *mtab;

    // history
    float lsp_hist[2][20];           ///< LSP coefficients of the last frame
    float bark_hist[3][2][40];       ///< BSE coefficients of last frame

    // bitstream parameters
    int16_t permut[4][4096];
    uint8_t length[4][2];            ///< main codebook stride
    uint8_t length_change[4];
    uint8_t bits_main_spec[2][4][2]; ///< bits for the main codebook
    int bits_main_spec_change[4];
    int n_div[4];

    float *spectrum;
    float *curr_frame;               ///< non-interleaved output
    float *prev_frame;               ///< non-interleaved previous frame
    int last_block_pos[2];
    int discarded_packets;

    float *cos_tabs[3];

    // scratch buffers
    float *tmp_buf;
} TwinContext;

#define PPC_SHAPE_CB_SIZE 64
#define PPC_SHAPE_LEN_MAX 60
#define SUB_AMP_MAX       4500.0
#define MULAW_MU          100.0
#define GAIN_BITS         8
#define AMP_MAX           13000.0
#define SUB_GAIN_BITS     5
#define WINDOW_TYPE_BITS  4
#define PGAIN_MU          200
#define LSP_COEFS_MAX     20
#define LSP_SPLIT_MAX     4
#define CHANNELS_MAX      2
#define SUBBLOCKS_MAX     16
#define BARK_N_COEF_MAX   4

/** @note not speed critical, hence not optimized */
static void memset_float(float *buf, float val, int size)
{
    while (size--)
        *buf++ = val;
}

/**
 * Evaluate a single LPC amplitude spectrum envelope coefficient from the line
 * spectrum pairs.
 *
 * @param lsp a vector of the cosinus of the LSP values
 * @param cos_val cos(PI*i/N) where i is the index of the LPC amplitude
 * @param order the order of the LSP (and the size of the *lsp buffer). Must
 *        be a multiple of four.
 * @return the LPC value
 *
 * @todo reuse code from Vorbis decoder: vorbis_floor0_decode
 */
static float eval_lpc_spectrum(const float *lsp, float cos_val, int order)
{
    int j;
    float p = 0.5f;
    float q = 0.5f;
    float two_cos_w = 2.0f*cos_val;

    for (j = 0; j + 1 < order; j += 2*2) {
        // Unroll the loop once since order is a multiple of four
        q *= lsp[j  ] - two_cos_w;
        p *= lsp[j+1] - two_cos_w;

        q *= lsp[j+2] - two_cos_w;
        p *= lsp[j+3] - two_cos_w;
    }

    p *= p * (2.0f - two_cos_w);
    q *= q * (2.0f + two_cos_w);

    return 0.5 / (p + q);
}

/**
 * Evaluate the LPC amplitude spectrum envelope from the line spectrum pairs.
 */
static void eval_lpcenv(TwinContext *tctx, const float *cos_vals, float *lpc)
{
    int i;
    const ModeTab *mtab = tctx->mtab;
    int size_s = mtab->size / mtab->fmode[FT_SHORT].sub;

    for (i = 0; i < size_s/2; i++) {
        float cos_i = tctx->cos_tabs[0][i];
        lpc[i]          = eval_lpc_spectrum(cos_vals,  cos_i, mtab->n_lsp);
        lpc[size_s-i-1] = eval_lpc_spectrum(cos_vals, -cos_i, mtab->n_lsp);
    }
}

static void interpolate(float *out, float v1, float v2, int size)
{
    int i;
    float step = (v1 - v2)/(size + 1);

    for (i = 0; i < size; i++) {
        v2 += step;
        out[i] = v2;
    }
}

static inline float get_cos(int idx, int part, const float *cos_tab, int size)
{
    return part ? -cos_tab[size - idx - 1] :
                   cos_tab[       idx    ];
}

/**
 * Evaluate the LPC amplitude spectrum envelope from the line spectrum pairs.
 * Probably for speed reasons, the coefficients are evaluated as
 * siiiibiiiisiiiibiiiisiiiibiiiisiiiibiiiis ...
 * where s is an evaluated value, i is a value interpolated from the others
 * and b might be either calculated or interpolated, depending on an
 * unexplained condition.
 *
 * @param step the size of a block "siiiibiiii"
 * @param in the cosinus of the LSP data
 * @param part is 0 for 0...PI (positive cossinus values) and 1 for PI...2PI
          (negative cossinus values)
 * @param size the size of the whole output
 */
static inline void eval_lpcenv_or_interp(TwinContext *tctx,
                                         enum FrameType ftype,
                                         float *out, const float *in,
                                         int size, int step, int part)
{
    int i;
    const ModeTab *mtab = tctx->mtab;
    const float *cos_tab = tctx->cos_tabs[ftype];

    // Fill the 's'
    for (i = 0; i < size; i += step)
        out[i] =
            eval_lpc_spectrum(in,
                              get_cos(i, part, cos_tab, size),
                              mtab->n_lsp);

    // Fill the 'iiiibiiii'
    for (i = step; i <= size - 2*step; i += step) {
        if (out[i + step] + out[i - step] >  1.95*out[i] ||
            out[i + step]                 >=  out[i - step]) {
            interpolate(out + i - step + 1, out[i], out[i-step], step - 1);
        } else {
            out[i - step/2] =
                eval_lpc_spectrum(in,
                                  get_cos(i-step/2, part, cos_tab, size),
                                  mtab->n_lsp);
            interpolate(out + i - step   + 1, out[i-step/2], out[i-step  ], step/2 - 1);
            interpolate(out + i - step/2 + 1, out[i       ], out[i-step/2], step/2 - 1);
        }
    }

    interpolate(out + size - 2*step + 1, out[size-step], out[size - 2*step], step - 1);
}

static void eval_lpcenv_2parts(TwinContext *tctx, enum FrameType ftype,
                               const float *buf, float *lpc,
                               int size, int step)
{
    eval_lpcenv_or_interp(tctx, ftype, lpc         , buf, size/2,   step, 0);
    eval_lpcenv_or_interp(tctx, ftype, lpc + size/2, buf, size/2, 2*step, 1);

    interpolate(lpc+size/2-step+1, lpc[size/2], lpc[size/2-step], step);

    memset_float(lpc + size - 2*step + 1, lpc[size - 2*step], 2*step - 1);
}

/**
 * Inverse quantization. Read CB coefficients for cb1 and cb2 from the
 * bitstream, sum the corresponding vectors and write the result to *out
 * after permutation.
 */
static void dequant(TwinContext *tctx, GetBitContext *gb, float *out,
                    enum FrameType ftype,
                    const int16_t *cb0, const int16_t *cb1, int cb_len)
{
    int pos = 0;
    int i, j;

    for (i = 0; i < tctx->n_div[ftype]; i++) {
        int tmp0, tmp1;
        int sign0 = 1;
        int sign1 = 1;
        const int16_t *tab0, *tab1;
        int length = tctx->length[ftype][i >= tctx->length_change[ftype]];
        int bitstream_second_part = (i >= tctx->bits_main_spec_change[ftype]);

        int bits = tctx->bits_main_spec[0][ftype][bitstream_second_part];
        if (bits == 7) {
            if (get_bits1(gb))
                sign0 = -1;
            bits = 6;
        }
        tmp0 = get_bits(gb, bits);

        bits = tctx->bits_main_spec[1][ftype][bitstream_second_part];

        if (bits == 7) {
            if (get_bits1(gb))
                sign1 = -1;

            bits = 6;
        }
        tmp1 = get_bits(gb, bits);

        tab0 = cb0 + tmp0*cb_len;
        tab1 = cb1 + tmp1*cb_len;

        for (j = 0; j < length; j++)
            out[tctx->permut[ftype][pos+j]] = sign0*tab0[j] + sign1*tab1[j];

        pos += length;
    }

}

static inline float mulawinv(float y, float clip, float mu)
{
    y = av_clipf(y/clip, -1, 1);
    return clip * FFSIGN(y) * (exp(log(1+mu) * fabs(y)) - 1) / mu;
}

/**
 * Evaluate a*b/400 rounded to the nearest integer. When, for example,
 * a*b == 200 and the nearest integer is ill-defined, use a table to emulate
 * the following broken float-based implementation used by the binary decoder:
 *
 * @code
 * static int very_broken_op(int a, int b)
 * {
 *    static float test; // Ugh, force gcc to do the division first...
 *
 *    test = a/400.;
 *    return b * test +  0.5;
 * }
 * @endcode
 *
 * @note if this function is replaced by just ROUNDED_DIV(a*b,400.), the stddev
 * between the original file (before encoding with Yamaha encoder) and the
 * decoded output increases, which leads one to believe that the encoder expects
 * exactly this broken calculation.
 */
static int very_broken_op(int a, int b)
{
    int x = a*b + 200;
    int size;
    const uint8_t *rtab;

    if (x%400 || b%5)
        return x/400;

    x /= 400;

    size = tabs[b/5].size;
    rtab = tabs[b/5].tab;
    return x - rtab[size*av_log2(2*(x - 1)/size)+(x - 1)%size];
}

/**
 * Sum to data a periodic peak of a given period, width and shape.
 *
 * @param period the period of the peak divised by 400.0
 */
static void add_peak(int period, int width, const float *shape,
                     float ppc_gain, float *speech, int len)
{
    int i, j;

    const float *shape_end = shape + len;
    int center;

    // First peak centered around zero
    for (i = 0; i < width/2; i++)
        speech[i] += ppc_gain * *shape++;

    for (i = 1; i < ROUNDED_DIV(len,width) ; i++) {
        center = very_broken_op(period, i);
        for (j = -width/2; j < (width+1)/2; j++)
            speech[j+center] += ppc_gain * *shape++;
    }

    // For the last block, be careful not to go beyond the end of the buffer
    center = very_broken_op(period, i);
    for (j = -width/2; j < (width + 1)/2 && shape < shape_end; j++)
        speech[j+center] += ppc_gain * *shape++;
}

static void decode_ppc(TwinContext *tctx, int period_coef, const float *shape,
                       float ppc_gain, float *speech)
{
    const ModeTab *mtab = tctx->mtab;
    int isampf = tctx->avctx->sample_rate/1000;
    int ibps = tctx->avctx->bit_rate/(1000 * tctx->avctx->channels);
    int min_period = ROUNDED_DIV(  40*2*mtab->size, isampf);
    int max_period = ROUNDED_DIV(6*40*2*mtab->size, isampf);
    int period_range = max_period - min_period;

    // This is actually the period multiplied by 400. It is just linearly coded
    // between its maximum and minimum value.
    int period = min_period +
        ROUNDED_DIV(period_coef*period_range, (1 << mtab->ppc_period_bit) - 1);
    int width;

    if (isampf == 22 && ibps == 32) {
        // For some unknown reason, NTT decided to code this case differently...
        width = ROUNDED_DIV((period + 800)* mtab->peak_per2wid, 400*mtab->size);
    } else
        width =             (period      )* mtab->peak_per2wid/(400*mtab->size);

    add_peak(period, width, shape, ppc_gain, speech, mtab->ppc_shape_len);
}

static void dec_gain(TwinContext *tctx, GetBitContext *gb, enum FrameType ftype,
                     float *out)
{
    const ModeTab *mtab = tctx->mtab;
    int i, j;
    int sub = mtab->fmode[ftype].sub;
    float step     = AMP_MAX     / ((1 <<     GAIN_BITS) - 1);
    float sub_step = SUB_AMP_MAX / ((1 << SUB_GAIN_BITS) - 1);

    if (ftype == FT_LONG) {
        for (i = 0; i < tctx->avctx->channels; i++)
            out[i] = (1./(1<<13)) *
                mulawinv(step * 0.5 + step * get_bits(gb, GAIN_BITS),
                         AMP_MAX, MULAW_MU);
    } else {
        for (i = 0; i < tctx->avctx->channels; i++) {
            float val = (1./(1<<23)) *
                mulawinv(step * 0.5 + step * get_bits(gb, GAIN_BITS),
                         AMP_MAX, MULAW_MU);

            for (j = 0; j < sub; j++) {
                out[i*sub + j] =
                    val*mulawinv(sub_step* 0.5 +
                                 sub_step* get_bits(gb, SUB_GAIN_BITS),
                                 SUB_AMP_MAX, MULAW_MU);
            }
        }
    }
}

/**
 * Rearrange the LSP coefficients so that they have a minimum distance of
 * min_dist. This function does it exactly as described in section of 3.2.4
 * of the G.729 specification (but interestingly is different from what the
 * reference decoder actually does).
 */
static void rearrange_lsp(int order, float *lsp, float min_dist)
{
    int i;
    float min_dist2 = min_dist * 0.5;
    for (i = 1; i < order; i++)
        if (lsp[i] - lsp[i-1] < min_dist) {
            float avg = (lsp[i] + lsp[i-1]) * 0.5;

            lsp[i-1] = avg - min_dist2;
            lsp[i  ] = avg + min_dist2;
        }
}

static void decode_lsp(TwinContext *tctx, int lpc_idx1, uint8_t *lpc_idx2,
                       int lpc_hist_idx, float *lsp, float *hist)
{
    const ModeTab *mtab = tctx->mtab;
    int i, j;

    const float *cb  =  mtab->lspcodebook;
    const float *cb2 =  cb  + (1 << mtab->lsp_bit1)*mtab->n_lsp;
    const float *cb3 =  cb2 + (1 << mtab->lsp_bit2)*mtab->n_lsp;

    const int8_t funny_rounding[4] = {
        -2,
        mtab->lsp_split == 4 ? -2 : 1,
        mtab->lsp_split == 4 ? -2 : 1,
        0
    };

    j = 0;
    for (i = 0; i < mtab->lsp_split; i++) {
        int chunk_end = ((i + 1)*mtab->n_lsp + funny_rounding[i])/mtab->lsp_split;
        for (; j < chunk_end; j++)
            lsp[j] = cb [lpc_idx1    * mtab->n_lsp + j] +
                     cb2[lpc_idx2[i] * mtab->n_lsp + j];
    }

    rearrange_lsp(mtab->n_lsp, lsp, 0.0001);

    for (i = 0; i < mtab->n_lsp; i++) {
        float tmp1 = 1. -          cb3[lpc_hist_idx*mtab->n_lsp + i];
        float tmp2 =     hist[i] * cb3[lpc_hist_idx*mtab->n_lsp + i];
        hist[i] = lsp[i];
        lsp[i]  = lsp[i] * tmp1 + tmp2;
    }

    rearrange_lsp(mtab->n_lsp, lsp, 0.0001);
    rearrange_lsp(mtab->n_lsp, lsp, 0.000095);
    ff_sort_nearly_sorted_floats(lsp, mtab->n_lsp);
}

static void dec_lpc_spectrum_inv(TwinContext *tctx, float *lsp,
                                 enum FrameType ftype, float *lpc)
{
    int i;
    int size = tctx->mtab->size / tctx->mtab->fmode[ftype].sub;

    for (i = 0; i < tctx->mtab->n_lsp; i++)
        lsp[i] =  2*cos(lsp[i]);

    switch (ftype) {
    case FT_LONG:
        eval_lpcenv_2parts(tctx, ftype, lsp, lpc, size, 8);
        break;
    case FT_MEDIUM:
        eval_lpcenv_2parts(tctx, ftype, lsp, lpc, size, 2);
        break;
    case FT_SHORT:
        eval_lpcenv(tctx, lsp, lpc);
        break;
    }
}

static void imdct_and_window(TwinContext *tctx, enum FrameType ftype, int wtype,
                            float *in, float *prev, int ch)
{
    FFTContext *mdct = &tctx->mdct_ctx[ftype];
    const ModeTab *mtab = tctx->mtab;
    int bsize = mtab->size / mtab->fmode[ftype].sub;
    int size  = mtab->size;
    float *buf1 = tctx->tmp_buf;
    int j;
    int wsize; // Window size
    float *out = tctx->curr_frame + 2*ch*mtab->size;
    float *out2 = out;
    float *prev_buf;
    int first_wsize;

    static const uint8_t wtype_to_wsize[]      = {0, 0, 2, 2, 2, 1, 0, 1, 1};
    int types_sizes[] = {
        mtab->size /    mtab->fmode[FT_LONG  ].sub,
        mtab->size /    mtab->fmode[FT_MEDIUM].sub,
        mtab->size / (2*mtab->fmode[FT_SHORT ].sub),
    };

    wsize = types_sizes[wtype_to_wsize[wtype]];
    first_wsize = wsize;
    prev_buf = prev + (size - bsize)/2;

    for (j = 0; j < mtab->fmode[ftype].sub; j++) {
        int sub_wtype = ftype == FT_MEDIUM ? 8 : wtype;

        if (!j && wtype == 4)
            sub_wtype = 4;
        else if (j == mtab->fmode[ftype].sub-1 && wtype == 7)
            sub_wtype = 7;

        wsize = types_sizes[wtype_to_wsize[sub_wtype]];

        mdct->imdct_half(mdct, buf1 + bsize*j, in + bsize*j);

        tctx->dsp.vector_fmul_window(out2,
                                     prev_buf + (bsize-wsize)/2,
                                     buf1 + bsize*j,
                                     ff_sine_windows[av_log2(wsize)],
                                     wsize/2);
        out2 += wsize;

        memcpy(out2, buf1 + bsize*j + wsize/2, (bsize - wsize/2)*sizeof(float));

        out2 += ftype == FT_MEDIUM ? (bsize-wsize)/2 : bsize - wsize;

        prev_buf = buf1 + bsize*j + bsize/2;
    }

    tctx->last_block_pos[ch] = (size + first_wsize)/2;
}

static void imdct_output(TwinContext *tctx, enum FrameType ftype, int wtype,
                         float *out)
{
    const ModeTab *mtab = tctx->mtab;
    int size1, size2;
    float *prev_buf = tctx->prev_frame + tctx->last_block_pos[0];
    int i;

    for (i = 0; i < tctx->avctx->channels; i++) {
        imdct_and_window(tctx, ftype, wtype,
                         tctx->spectrum + i*mtab->size,
                         prev_buf + 2*i*mtab->size,
                         i);
    }

    if (!out)
        return;

    size2 = tctx->last_block_pos[0];
    size1 = mtab->size - size2;
    if (tctx->avctx->channels == 2) {
        tctx->dsp.butterflies_float_interleave(out, prev_buf,
                                               &prev_buf[2*mtab->size],
                                               size1);

        out += 2 * size1;

        tctx->dsp.butterflies_float_interleave(out, tctx->curr_frame,
                                               &tctx->curr_frame[2*mtab->size],
                                               size2);
    } else {
        memcpy(out, prev_buf, size1 * sizeof(*out));

        out += size1;

        memcpy(out, tctx->curr_frame, size2 * sizeof(*out));
    }

}

static void dec_bark_env(TwinContext *tctx, const uint8_t *in, int use_hist,
                         int ch, float *out, float gain, enum FrameType ftype)
{
    const ModeTab *mtab = tctx->mtab;
    int i,j;
    float *hist = tctx->bark_hist[ftype][ch];
    float val = ((const float []) {0.4, 0.35, 0.28})[ftype];
    int bark_n_coef  = mtab->fmode[ftype].bark_n_coef;
    int fw_cb_len = mtab->fmode[ftype].bark_env_size / bark_n_coef;
    int idx = 0;

    for (i = 0; i < fw_cb_len; i++)
        for (j = 0; j < bark_n_coef; j++, idx++) {
            float tmp2 =
                mtab->fmode[ftype].bark_cb[fw_cb_len*in[j] + i] * (1./4096);
            float st = use_hist ?
                (1. - val) * tmp2 + val*hist[idx] + 1. : tmp2 + 1.;

            hist[idx] = tmp2;
            if (st < -1.) st = 1.;

            memset_float(out, st * gain, mtab->fmode[ftype].bark_tab[idx]);
            out += mtab->fmode[ftype].bark_tab[idx];
        }

}

static void read_and_decode_spectrum(TwinContext *tctx, GetBitContext *gb,
                                     float *out, enum FrameType ftype)
{
    const ModeTab *mtab = tctx->mtab;
    int channels = tctx->avctx->channels;
    int sub = mtab->fmode[ftype].sub;
    int block_size = mtab->size / sub;
    float gain[CHANNELS_MAX*SUBBLOCKS_MAX];
    float ppc_shape[PPC_SHAPE_LEN_MAX * CHANNELS_MAX * 4];
    uint8_t bark1[CHANNELS_MAX][SUBBLOCKS_MAX][BARK_N_COEF_MAX];
    uint8_t bark_use_hist[CHANNELS_MAX][SUBBLOCKS_MAX];

    uint8_t lpc_idx1[CHANNELS_MAX];
    uint8_t lpc_idx2[CHANNELS_MAX][LSP_SPLIT_MAX];
    uint8_t lpc_hist_idx[CHANNELS_MAX];

    int i, j, k;

    dequant(tctx, gb, out, ftype,
            mtab->fmode[ftype].cb0, mtab->fmode[ftype].cb1,
            mtab->fmode[ftype].cb_len_read);

    for (i = 0; i < channels; i++)
        for (j = 0; j < sub; j++)
            for (k = 0; k < mtab->fmode[ftype].bark_n_coef; k++)
                bark1[i][j][k] =
                    get_bits(gb, mtab->fmode[ftype].bark_n_bit);

    for (i = 0; i < channels; i++)
        for (j = 0; j < sub; j++)
            bark_use_hist[i][j] = get_bits1(gb);

    dec_gain(tctx, gb, ftype, gain);

    for (i = 0; i < channels; i++) {
        lpc_hist_idx[i] = get_bits(gb, tctx->mtab->lsp_bit0);
        lpc_idx1    [i] = get_bits(gb, tctx->mtab->lsp_bit1);

        for (j = 0; j < tctx->mtab->lsp_split; j++)
            lpc_idx2[i][j] = get_bits(gb, tctx->mtab->lsp_bit2);
    }

    if (ftype == FT_LONG) {
        int cb_len_p = (tctx->n_div[3] + mtab->ppc_shape_len*channels - 1)/
            tctx->n_div[3];
        dequant(tctx, gb, ppc_shape, FT_PPC, mtab->ppc_shape_cb,
                mtab->ppc_shape_cb + cb_len_p*PPC_SHAPE_CB_SIZE, cb_len_p);
    }

    for (i = 0; i < channels; i++) {
        float *chunk = out + mtab->size * i;
        float lsp[LSP_COEFS_MAX];

        for (j = 0; j < sub; j++) {
            dec_bark_env(tctx, bark1[i][j], bark_use_hist[i][j], i,
                         tctx->tmp_buf, gain[sub*i+j], ftype);

            tctx->dsp.vector_fmul(chunk + block_size*j, chunk + block_size*j, tctx->tmp_buf,
                                  block_size);

        }

        if (ftype == FT_LONG) {
            float pgain_step = 25000. / ((1 << mtab->pgain_bit) - 1);
            int p_coef = get_bits(gb, tctx->mtab->ppc_period_bit);
            int g_coef = get_bits(gb, tctx->mtab->pgain_bit);
            float v = 1./8192*
                mulawinv(pgain_step*g_coef+ pgain_step/2, 25000., PGAIN_MU);

            decode_ppc(tctx, p_coef, ppc_shape + i*mtab->ppc_shape_len, v,
                       chunk);
        }

        decode_lsp(tctx, lpc_idx1[i], lpc_idx2[i], lpc_hist_idx[i], lsp,
                   tctx->lsp_hist[i]);

        dec_lpc_spectrum_inv(tctx, lsp, ftype, tctx->tmp_buf);

        for (j = 0; j < mtab->fmode[ftype].sub; j++) {
            tctx->dsp.vector_fmul(chunk, chunk, tctx->tmp_buf, block_size);
            chunk += block_size;
        }
    }
}

static int twin_decode_frame(AVCodecContext * avctx, void *data,
                             int *got_frame_ptr, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    TwinContext *tctx = avctx->priv_data;
    GetBitContext gb;
    const ModeTab *mtab = tctx->mtab;
    float *out = NULL;
    enum FrameType ftype;
    int window_type, ret;
    static const enum FrameType wtype_to_ftype_table[] = {
        FT_LONG,   FT_LONG, FT_SHORT, FT_LONG,
        FT_MEDIUM, FT_LONG, FT_LONG,  FT_MEDIUM, FT_MEDIUM
    };

    if (buf_size*8 < avctx->bit_rate*mtab->size/avctx->sample_rate + 8) {
        av_log(avctx, AV_LOG_ERROR,
               "Frame too small (%d bytes). Truncated file?\n", buf_size);
        return AVERROR(EINVAL);
    }

    /* get output buffer */
    if (tctx->discarded_packets >= 2) {
        tctx->frame.nb_samples = mtab->size;
        if ((ret = avctx->get_buffer(avctx, &tctx->frame)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
            return ret;
        }
        out = (float *)tctx->frame.data[0];
    }

    init_get_bits(&gb, buf, buf_size * 8);
    skip_bits(&gb, get_bits(&gb, 8));
    window_type = get_bits(&gb, WINDOW_TYPE_BITS);

    if (window_type > 8) {
        av_log(avctx, AV_LOG_ERROR, "Invalid window type, broken sample?\n");
        return -1;
    }

    ftype = wtype_to_ftype_table[window_type];

    read_and_decode_spectrum(tctx, &gb, tctx->spectrum, ftype);

    imdct_output(tctx, ftype, window_type, out);

    FFSWAP(float*, tctx->curr_frame, tctx->prev_frame);

    if (tctx->discarded_packets < 2) {
        tctx->discarded_packets++;
        *got_frame_ptr = 0;
        return buf_size;
    }

    *got_frame_ptr   = 1;
    *(AVFrame *)data = tctx->frame;

    return buf_size;
}

/**
 * Init IMDCT and windowing tables
 */
static av_cold int init_mdct_win(TwinContext *tctx)
{
    int i, j, ret;
    const ModeTab *mtab = tctx->mtab;
    int size_s = mtab->size / mtab->fmode[FT_SHORT].sub;
    int size_m = mtab->size / mtab->fmode[FT_MEDIUM].sub;
    int channels = tctx->avctx->channels;
    float norm = channels == 1 ? 2. : 1.;

    for (i = 0; i < 3; i++) {
        int bsize = tctx->mtab->size/tctx->mtab->fmode[i].sub;
        if ((ret = ff_mdct_init(&tctx->mdct_ctx[i], av_log2(bsize) + 1, 1,
                                -sqrt(norm/bsize) / (1<<15))))
            return ret;
    }

    FF_ALLOC_OR_GOTO(tctx->avctx, tctx->tmp_buf,
                     mtab->size * sizeof(*tctx->tmp_buf), alloc_fail);

    FF_ALLOC_OR_GOTO(tctx->avctx, tctx->spectrum,
                     2 * mtab->size * channels * sizeof(*tctx->spectrum),
                     alloc_fail);
    FF_ALLOC_OR_GOTO(tctx->avctx, tctx->curr_frame,
                     2 * mtab->size * channels * sizeof(*tctx->curr_frame),
                     alloc_fail);
    FF_ALLOC_OR_GOTO(tctx->avctx, tctx->prev_frame,
                     2 * mtab->size * channels * sizeof(*tctx->prev_frame),
                     alloc_fail);

    for (i = 0; i < 3; i++) {
        int m = 4*mtab->size/mtab->fmode[i].sub;
        double freq = 2*M_PI/m;
        FF_ALLOC_OR_GOTO(tctx->avctx, tctx->cos_tabs[i],
                         (m / 4) * sizeof(*tctx->cos_tabs[i]), alloc_fail);

        for (j = 0; j <= m/8; j++)
            tctx->cos_tabs[i][j] = cos((2*j + 1)*freq);
        for (j = 1; j <  m/8; j++)
            tctx->cos_tabs[i][m/4-j] = tctx->cos_tabs[i][j];
    }


    ff_init_ff_sine_windows(av_log2(size_m));
    ff_init_ff_sine_windows(av_log2(size_s/2));
    ff_init_ff_sine_windows(av_log2(mtab->size));

    return 0;
alloc_fail:
    return AVERROR(ENOMEM);
}

/**
 * Interpret the data as if it were a num_blocks x line_len[0] matrix and for
 * each line do a cyclic permutation, i.e.
 * abcdefghijklm -> defghijklmabc
 * where the amount to be shifted is evaluated depending on the column.
 */
static void permutate_in_line(int16_t *tab, int num_vect, int num_blocks,
                              int block_size,
                              const uint8_t line_len[2], int length_div,
                              enum FrameType ftype)

{
    int i,j;

    for (i = 0; i < line_len[0]; i++) {
        int shift;

        if (num_blocks == 1 ||
            (ftype == FT_LONG && num_vect % num_blocks) ||
            (ftype != FT_LONG && num_vect & 1         ) ||
            i == line_len[1]) {
            shift = 0;
        } else if (ftype == FT_LONG) {
            shift = i;
        } else
            shift = i*i;

        for (j = 0; j < num_vect && (j+num_vect*i < block_size*num_blocks); j++)
            tab[i*num_vect+j] = i*num_vect + (j + shift) % num_vect;
    }
}

/**
 * Interpret the input data as in the following table:
 *
 * @verbatim
 *
 * abcdefgh
 * ijklmnop
 * qrstuvw
 * x123456
 *
 * @endverbatim
 *
 * and transpose it, giving the output
 * aiqxbjr1cks2dlt3emu4fvn5gow6hp
 */
static void transpose_perm(int16_t *out, int16_t *in, int num_vect,
                           const uint8_t line_len[2], int length_div)
{
    int i,j;
    int cont= 0;
    for (i = 0; i < num_vect; i++)
        for (j = 0; j < line_len[i >= length_div]; j++)
            out[cont++] = in[j*num_vect + i];
}

static void linear_perm(int16_t *out, int16_t *in, int n_blocks, int size)
{
    int block_size = size/n_blocks;
    int i;

    for (i = 0; i < size; i++)
        out[i] = block_size * (in[i] % n_blocks) + in[i] / n_blocks;
}

static av_cold void construct_perm_table(TwinContext *tctx,enum FrameType ftype)
{
    int block_size;
    const ModeTab *mtab = tctx->mtab;
    int size;
    int16_t *tmp_perm = (int16_t *) tctx->tmp_buf;

    if (ftype == FT_PPC) {
        size  = tctx->avctx->channels;
        block_size = mtab->ppc_shape_len;
    } else {
        size       = tctx->avctx->channels * mtab->fmode[ftype].sub;
        block_size = mtab->size / mtab->fmode[ftype].sub;
    }

    permutate_in_line(tmp_perm, tctx->n_div[ftype], size,
                      block_size, tctx->length[ftype],
                      tctx->length_change[ftype], ftype);

    transpose_perm(tctx->permut[ftype], tmp_perm, tctx->n_div[ftype],
                   tctx->length[ftype], tctx->length_change[ftype]);

    linear_perm(tctx->permut[ftype], tctx->permut[ftype], size,
                size*block_size);
}

static av_cold void init_bitstream_params(TwinContext *tctx)
{
    const ModeTab *mtab = tctx->mtab;
    int n_ch = tctx->avctx->channels;
    int total_fr_bits = tctx->avctx->bit_rate*mtab->size/
                             tctx->avctx->sample_rate;

    int lsp_bits_per_block = n_ch*(mtab->lsp_bit0 + mtab->lsp_bit1 +
                                   mtab->lsp_split*mtab->lsp_bit2);

    int ppc_bits = n_ch*(mtab->pgain_bit + mtab->ppc_shape_bit +
                         mtab->ppc_period_bit);

    int bsize_no_main_cb[3];
    int bse_bits[3];
    int i;
    enum FrameType frametype;

    for (i = 0; i < 3; i++)
        // +1 for history usage switch
        bse_bits[i] = n_ch *
            (mtab->fmode[i].bark_n_coef * mtab->fmode[i].bark_n_bit + 1);

    bsize_no_main_cb[2] = bse_bits[2] + lsp_bits_per_block + ppc_bits +
                          WINDOW_TYPE_BITS + n_ch*GAIN_BITS;

    for (i = 0; i < 2; i++)
        bsize_no_main_cb[i] =
            lsp_bits_per_block + n_ch*GAIN_BITS + WINDOW_TYPE_BITS +
            mtab->fmode[i].sub*(bse_bits[i] + n_ch*SUB_GAIN_BITS);

    // The remaining bits are all used for the main spectrum coefficients
    for (i = 0; i < 4; i++) {
        int bit_size;
        int vect_size;
        int rounded_up, rounded_down, num_rounded_down, num_rounded_up;
        if (i == 3) {
            bit_size  = n_ch * mtab->ppc_shape_bit;
            vect_size = n_ch * mtab->ppc_shape_len;
        } else {
            bit_size = total_fr_bits - bsize_no_main_cb[i];
            vect_size = n_ch * mtab->size;
        }

        tctx->n_div[i] = (bit_size + 13) / 14;

        rounded_up   = (bit_size + tctx->n_div[i] - 1)/tctx->n_div[i];
        rounded_down = (bit_size           )/tctx->n_div[i];
        num_rounded_down = rounded_up * tctx->n_div[i] - bit_size;
        num_rounded_up = tctx->n_div[i] - num_rounded_down;
        tctx->bits_main_spec[0][i][0] = (rounded_up   + 1)/2;
        tctx->bits_main_spec[1][i][0] = (rounded_up      )/2;
        tctx->bits_main_spec[0][i][1] = (rounded_down + 1)/2;
        tctx->bits_main_spec[1][i][1] = (rounded_down    )/2;
        tctx->bits_main_spec_change[i] = num_rounded_up;

        rounded_up   = (vect_size + tctx->n_div[i] - 1)/tctx->n_div[i];
        rounded_down = (vect_size                     )/tctx->n_div[i];
        num_rounded_down = rounded_up * tctx->n_div[i] - vect_size;
        num_rounded_up = tctx->n_div[i] - num_rounded_down;
        tctx->length[i][0] = rounded_up;
        tctx->length[i][1] = rounded_down;
        tctx->length_change[i] = num_rounded_up;
    }

    for (frametype = FT_SHORT; frametype <= FT_PPC; frametype++)
        construct_perm_table(tctx, frametype);
}

static av_cold int twin_decode_close(AVCodecContext *avctx)
{
    TwinContext *tctx = avctx->priv_data;
    int i;

    for (i = 0; i < 3; i++) {
        ff_mdct_end(&tctx->mdct_ctx[i]);
        av_free(tctx->cos_tabs[i]);
    }


    av_free(tctx->curr_frame);
    av_free(tctx->spectrum);
    av_free(tctx->prev_frame);
    av_free(tctx->tmp_buf);

    return 0;
}

static av_cold int twin_decode_init(AVCodecContext *avctx)
{
    int ret;
    TwinContext *tctx = avctx->priv_data;
    int isampf, ibps;

    tctx->avctx       = avctx;
    avctx->sample_fmt = AV_SAMPLE_FMT_FLT;

    if (!avctx->extradata || avctx->extradata_size < 12) {
        av_log(avctx, AV_LOG_ERROR, "Missing or incomplete extradata\n");
        return AVERROR_INVALIDDATA;
    }
    avctx->channels = AV_RB32(avctx->extradata    ) + 1;
    avctx->bit_rate = AV_RB32(avctx->extradata + 4) * 1000;
    isampf          = AV_RB32(avctx->extradata + 8);
    switch (isampf) {
    case 44: avctx->sample_rate = 44100;         break;
    case 22: avctx->sample_rate = 22050;         break;
    case 11: avctx->sample_rate = 11025;         break;
    default: avctx->sample_rate = isampf * 1000; break;
    }

    if (avctx->channels > CHANNELS_MAX) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported number of channels: %i\n",
               avctx->channels);
        return -1;
    }
    ibps = avctx->bit_rate / (1000 * avctx->channels);

    switch ((isampf << 8) +  ibps) {
    case (8 <<8) +  8: tctx->mtab = &mode_08_08; break;
    case (11<<8) +  8: tctx->mtab = &mode_11_08; break;
    case (11<<8) + 10: tctx->mtab = &mode_11_10; break;
    case (16<<8) + 16: tctx->mtab = &mode_16_16; break;
    case (22<<8) + 20: tctx->mtab = &mode_22_20; break;
    case (22<<8) + 24: tctx->mtab = &mode_22_24; break;
    case (22<<8) + 32: tctx->mtab = &mode_22_32; break;
    case (44<<8) + 40: tctx->mtab = &mode_44_40; break;
    case (44<<8) + 48: tctx->mtab = &mode_44_48; break;
    default:
        av_log(avctx, AV_LOG_ERROR, "This version does not support %d kHz - %d kbit/s/ch mode.\n", isampf, isampf);
        return -1;
    }

    ff_dsputil_init(&tctx->dsp, avctx);
    if ((ret = init_mdct_win(tctx))) {
        av_log(avctx, AV_LOG_ERROR, "Error initializing MDCT\n");
        twin_decode_close(avctx);
        return ret;
    }
    init_bitstream_params(tctx);

    memset_float(tctx->bark_hist[0][0], 0.1, FF_ARRAY_ELEMS(tctx->bark_hist));

    avcodec_get_frame_defaults(&tctx->frame);
    avctx->coded_frame = &tctx->frame;

    return 0;
}

AVCodec ff_twinvq_decoder = {
    .name           = "twinvq",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_TWINVQ,
    .priv_data_size = sizeof(TwinContext),
    .init           = twin_decode_init,
    .close          = twin_decode_close,
    .decode         = twin_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("VQF TwinVQ"),
};
