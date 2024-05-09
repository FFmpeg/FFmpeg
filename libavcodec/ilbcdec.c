/*
 * Copyright (c) 2013, The WebRTC project authors. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *
 *   * Neither the name of Google nor the names of its contributors may
 *     be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "libavutil/channel_layout.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"
#include "ilbcdata.h"

#define LPC_N_20MS            1
#define LPC_N_30MS            2
#define LPC_N_MAX             2
#define LSF_NSPLIT            3
#define NASUB_MAX             4
#define LPC_FILTERORDER       10
#define NSUB_MAX              6
#define SUBL                  40

#define ST_MEM_L_TBL          85
#define MEM_LF_TBL            147
#define STATE_SHORT_LEN_20MS  57
#define STATE_SHORT_LEN_30MS  58

#define BLOCKL_MAX            240
#define CB_MEML               147
#define CB_NSTAGES            3
#define CB_HALFFILTERLEN      4
#define CB_FILTERLEN          8

#define ENH_NBLOCKS_TOT 8
#define ENH_BLOCKL     80
#define ENH_BUFL     (ENH_NBLOCKS_TOT)*ENH_BLOCKL
#define ENH_BUFL_FILTEROVERHEAD  3
#define BLOCKL_MAX      240
#define NSUB_20MS         4
#define NSUB_30MS         6
#define NSUB_MAX          6
#define NASUB_20MS        2
#define NASUB_30MS        4
#define NASUB_MAX         4
#define STATE_LEN        80
#define STATE_SHORT_LEN_30MS  58
#define STATE_SHORT_LEN_20MS  57

#define SPL_MUL_16_16(a, b) ((int32_t) (((int16_t)(a)) * ((int16_t)(b))))
#define SPL_MUL_16_16_RSFT(a, b, c) (SPL_MUL_16_16(a, b) >> (c))

typedef struct ILBCFrame {
    int16_t  lsf[LSF_NSPLIT*LPC_N_MAX];
    int16_t  cb_index[CB_NSTAGES*(NASUB_MAX + 1)];
    int16_t  gain_index[CB_NSTAGES*(NASUB_MAX + 1)];
    int16_t  ifm;
    int16_t  state_first;
    int16_t  idx[STATE_SHORT_LEN_30MS];
    int16_t  firstbits;
    int16_t  start;
} ILBCFrame;

typedef struct ILBCContext {
    AVClass         *class;
    int              enhancer;

    int              mode;
    ILBCFrame        frame;

    int              prev_enh_pl;
    int              consPLICount;
    int              last_lag;
    int              state_short_len;
    int              lpc_n;
    int16_t          nasub;
    int16_t          nsub;
    int              block_samples;
    int16_t          no_of_words;
    int16_t          no_of_bytes;
    int16_t          lsfdeq[LPC_FILTERORDER*LPC_N_MAX];
    int16_t          lsfold[LPC_FILTERORDER];
    int16_t          syntMem[LPC_FILTERORDER];
    int16_t          lsfdeqold[LPC_FILTERORDER];
    int16_t          weightdenum[(LPC_FILTERORDER + 1) * NSUB_MAX];
    int16_t          syntdenum[NSUB_MAX * (LPC_FILTERORDER + 1)];
    int16_t          old_syntdenum[NSUB_MAX * (LPC_FILTERORDER + 1)];
    int16_t          enh_buf[ENH_BUFL+ENH_BUFL_FILTEROVERHEAD];
    int16_t          enh_period[ENH_NBLOCKS_TOT];
    int16_t          prevResidual[NSUB_MAX*SUBL];
    int16_t          decresidual[BLOCKL_MAX];
    int16_t          plc_residual[BLOCKL_MAX + LPC_FILTERORDER];
    int16_t          seed;
    int16_t          prevPLI;
    int16_t          prevScale;
    int16_t          prevLag;
    int16_t          per_square;
    int16_t          prev_lpc[LPC_FILTERORDER + 1];
    int16_t          plc_lpc[LPC_FILTERORDER + 1];
    int16_t          hpimemx[2];
    int16_t          hpimemy[4];
} ILBCContext;

static int unpack_frame(ILBCContext *s, const uint8_t *buf, int size)
{
    ILBCFrame *frame = &s->frame;
    GetBitContext gb0, *const gb = &gb0;
    int j, ret;

    if ((ret = init_get_bits8(gb, buf, size)) < 0)
        return ret;

    frame->lsf[0] = get_bits(gb, 6);
    frame->lsf[1] = get_bits(gb, 7);
    frame->lsf[2] = get_bits(gb, 7);

    if (s->mode == 20) {
        frame->start          = get_bits(gb, 2);
        frame->state_first    = get_bits1(gb);
        frame->ifm            = get_bits(gb, 6);
        frame->cb_index[0]    = get_bits(gb, 6) << 1;
        frame->gain_index[0]  = get_bits(gb, 2) << 3;
        frame->gain_index[1]  = get_bits1(gb) << 3;
        frame->cb_index[3]    = get_bits(gb, 7) << 1;
        frame->gain_index[3]  = get_bits1(gb) << 4;
        frame->gain_index[4]  = get_bits1(gb) << 3;
        frame->gain_index[6]  = get_bits1(gb) << 4;
    } else {
        frame->lsf[3]         = get_bits(gb, 6);
        frame->lsf[4]         = get_bits(gb, 7);
        frame->lsf[5]         = get_bits(gb, 7);
        frame->start          = get_bits(gb, 3);
        frame->state_first    = get_bits1(gb);
        frame->ifm            = get_bits(gb, 6);
        frame->cb_index[0]    = get_bits(gb, 4) << 3;
        frame->gain_index[0]  = get_bits1(gb) << 4;
        frame->gain_index[1]  = get_bits1(gb) << 3;
        frame->cb_index[3]    = get_bits(gb, 6) << 2;
        frame->gain_index[3]  = get_bits1(gb) << 4;
        frame->gain_index[4]  = get_bits1(gb) << 3;
    }

    for (j = 0; j < 48; j++)
        frame->idx[j] = get_bits1(gb) << 2;

    if (s->mode == 20) {
        for (; j < 57; j++)
            frame->idx[j] = get_bits1(gb) << 2;

        frame->gain_index[1] |= get_bits1(gb) << 2;
        frame->gain_index[3] |= get_bits(gb, 2) << 2;
        frame->gain_index[4] |= get_bits1(gb) << 2;
        frame->gain_index[6] |= get_bits1(gb) << 3;
        frame->gain_index[7]  = get_bits(gb, 2) << 2;
    } else {
        for (; j < 58; j++)
            frame->idx[j] = get_bits1(gb) << 2;

        frame->cb_index[0]    |= get_bits(gb, 2) << 1;
        frame->gain_index[0]  |= get_bits1(gb) << 3;
        frame->gain_index[1]  |= get_bits1(gb) << 2;
        frame->cb_index[3]    |= get_bits1(gb) << 1;
        frame->cb_index[6]     = get_bits1(gb) << 7;
        frame->cb_index[6]    |= get_bits(gb, 6) << 1;
        frame->cb_index[9]     = get_bits(gb, 7) << 1;
        frame->cb_index[12]    = get_bits(gb, 3) << 5;
        frame->cb_index[12]   |= get_bits(gb, 4) << 1;
        frame->gain_index[3]  |= get_bits(gb, 2) << 2;
        frame->gain_index[4]  |= get_bits(gb, 2) << 1;
        frame->gain_index[6]   = get_bits(gb, 2) << 3;
        frame->gain_index[7]   = get_bits(gb, 2) << 2;
        frame->gain_index[9]   = get_bits1(gb) << 4;
        frame->gain_index[10]  = get_bits1(gb) << 3;
        frame->gain_index[12]  = get_bits1(gb) << 4;
        frame->gain_index[13]  = get_bits1(gb) << 3;
    }

    for (j = 0; j < 56; j++)
        frame->idx[j] |= get_bits(gb, 2);

    if (s->mode == 20) {
        frame->idx[56]        |= get_bits(gb, 2);
        frame->cb_index[0]    |= get_bits1(gb);
        frame->cb_index[1]     = get_bits(gb, 7);
        frame->cb_index[2]     = get_bits(gb, 6) << 1;
        frame->cb_index[2]    |= get_bits1(gb);
        frame->gain_index[0]  |= get_bits(gb, 3);
        frame->gain_index[1]  |= get_bits(gb, 2);
        frame->gain_index[2]   = get_bits(gb, 3);
        frame->cb_index[3]    |= get_bits1(gb);
        frame->cb_index[4]     = get_bits(gb, 6) << 1;
        frame->cb_index[4]    |= get_bits1(gb);
        frame->cb_index[5]     = get_bits(gb, 7);
        frame->cb_index[6]     = get_bits(gb, 8);
        frame->cb_index[7]     = get_bits(gb, 8);
        frame->cb_index[8]     = get_bits(gb, 8);
        frame->gain_index[3]  |= get_bits(gb, 2);
        frame->gain_index[4]  |= get_bits(gb, 2);
        frame->gain_index[5]   = get_bits(gb, 3);
        frame->gain_index[6]  |= get_bits(gb, 3);
        frame->gain_index[7]  |= get_bits(gb, 2);
        frame->gain_index[8]   = get_bits(gb, 3);
    } else {
        frame->idx[56]        |= get_bits(gb, 2);
        frame->idx[57]        |= get_bits(gb, 2);
        frame->cb_index[0]    |= get_bits1(gb);
        frame->cb_index[1]     = get_bits(gb, 7);
        frame->cb_index[2]     = get_bits(gb, 4) << 3;
        frame->cb_index[2]    |= get_bits(gb, 3);
        frame->gain_index[0]  |= get_bits(gb, 3);
        frame->gain_index[1]  |= get_bits(gb, 2);
        frame->gain_index[2]   = get_bits(gb, 3);
        frame->cb_index[3]    |= get_bits1(gb);
        frame->cb_index[4]     = get_bits(gb, 4) << 3;
        frame->cb_index[4]    |= get_bits(gb, 3);
        frame->cb_index[5]     = get_bits(gb, 7);
        frame->cb_index[6]    |= get_bits1(gb);
        frame->cb_index[7]     = get_bits(gb, 5) << 3;
        frame->cb_index[7]    |= get_bits(gb, 3);
        frame->cb_index[8]     = get_bits(gb, 8);
        frame->cb_index[9]    |= get_bits1(gb);
        frame->cb_index[10]    = get_bits(gb, 4) << 4;
        frame->cb_index[10]   |= get_bits(gb, 4);
        frame->cb_index[11]    = get_bits(gb, 8);
        frame->cb_index[12]   |= get_bits1(gb);
        frame->cb_index[13]    = get_bits(gb, 3) << 5;
        frame->cb_index[13]   |= get_bits(gb, 5);
        frame->cb_index[14]    = get_bits(gb, 8);
        frame->gain_index[3]  |= get_bits(gb, 2);
        frame->gain_index[4]  |= get_bits1(gb);
        frame->gain_index[5]   = get_bits(gb, 3);
        frame->gain_index[6]  |= get_bits(gb, 3);
        frame->gain_index[7]  |= get_bits(gb, 2);
        frame->gain_index[8]   = get_bits(gb, 3);
        frame->gain_index[9]  |= get_bits(gb, 4);
        frame->gain_index[10] |= get_bits1(gb) << 2;
        frame->gain_index[10] |= get_bits(gb, 2);
        frame->gain_index[11]  = get_bits(gb, 3);
        frame->gain_index[12] |= get_bits(gb, 4);
        frame->gain_index[13] |= get_bits(gb, 3);
        frame->gain_index[14]  = get_bits(gb, 3);
    }

    return get_bits1(gb);
}

static void index_conv(int16_t *index)
{
    int k;

    for (k = 4; k < 6; k++) {
        if (index[k] >= 44 && index[k] < 108) {
            index[k] += 64;
        } else if (index[k] >= 108 && index[k] < 128) {
            index[k] += 128;
        }
    }
}

static void lsf_dequantization(int16_t *lsfdeq, int16_t *index, int16_t lpc_n)
{
    int i, j, pos = 0, cb_pos = 0;

    for (i = 0; i < LSF_NSPLIT; i++) {
        for (j = 0; j < lsf_dim_codebook[i]; j++) {
            lsfdeq[pos + j] = lsf_codebook[cb_pos + index[i] * lsf_dim_codebook[i] + j];
        }

        pos    += lsf_dim_codebook[i];
        cb_pos += lsf_size_codebook[i] * lsf_dim_codebook[i];
    }

    if (lpc_n > 1) {
        pos = 0;
        cb_pos = 0;
        for (i = 0; i < LSF_NSPLIT; i++) {
            for (j = 0; j < lsf_dim_codebook[i]; j++) {
                lsfdeq[LPC_FILTERORDER + pos + j] = lsf_codebook[cb_pos +
                    index[LSF_NSPLIT + i] * lsf_dim_codebook[i] + j];
            }

            pos    += lsf_dim_codebook[i];
            cb_pos += lsf_size_codebook[i] * lsf_dim_codebook[i];
        }
    }
}

static void lsf_check_stability(int16_t *lsf, int dim, int nb_vectors)
{
    for (int n = 0; n < 2; n++) {
        for (int m = 0; m < nb_vectors; m++) {
            for (int k = 0; k < dim - 1; k++) {
                int i = m * dim + k;

                if ((lsf[i + 1] - lsf[i]) < 319) {
                    if (lsf[i + 1] < lsf[i]) {
                        lsf[i + 1] = lsf[i] + 160;
                        lsf[i]     = lsf[i + 1] - 160;
                    } else {
                        lsf[i]     -= 160;
                        lsf[i + 1] += 160;
                    }
                }

                lsf[i] = av_clip(lsf[i], 82, 25723);
            }
        }
    }
}

static void lsf_interpolate(int16_t *out, const int16_t *in1,
                            const int16_t *in2, int16_t coef,
                            int size)
{
    int invcoef = 16384 - coef, i;

    for (i = 0; i < size; i++)
        out[i] = (coef * in1[i] + invcoef * in2[i] + 8192) >> 14;
}

static void lsf2lsp(const int16_t *lsf, int16_t *lsp, int order)
{
    int16_t diff, freq;
    int32_t tmp;
    int i, k;

    for (i = 0; i < order; i++) {
        freq = (lsf[i] * 20861) >> 15;
        /* 20861: 1.0/(2.0*PI) in Q17 */
        /*
           Upper 8 bits give the index k and
           Lower 8 bits give the difference, which needs
           to be approximated linearly
         */
        k = FFMIN(freq >> 8, 63);
        diff = freq & 0xFF;

        /* Calculate linear approximation */
        tmp = cos_derivative_tbl[k] * diff;
        lsp[i] = cos_tbl[k] + (tmp >> 12);
    }
}

static void get_lsp_poly(const int16_t *lsp, int32_t *f)
{
    int16_t high, low;
    int i, j, k, l;
    int32_t tmp;

    f[0] = 16777216;
    f[1] = lsp[0] * -1024;

    for (i = 2, k = 2, l = 2; i <= 5; i++, k += 2) {
        f[l] = f[l - 2];

        for (j = i; j > 1; j--, l--) {
            high = f[l - 1] >> 16;
            low = (f[l - 1] - (high * (1 << 16))) >> 1;

            tmp = ((high * lsp[k]) * 4) + (((low * lsp[k]) >> 15) * 4);

            f[l] += f[l - 2];
            f[l] -= (unsigned)tmp;
        }

        f[l] -= lsp[k] * (1 << 10);
        l += i;
    }
}

static void lsf2poly(int16_t *a, const int16_t *lsf)
{
    int32_t f[2][6];
    int16_t lsp[10];
    int32_t tmp;
    int i;

    lsf2lsp(lsf, lsp, LPC_FILTERORDER);

    get_lsp_poly(&lsp[0], f[0]);
    get_lsp_poly(&lsp[1], f[1]);

    for (i = 5; i > 0; i--) {
        f[0][i] += (unsigned)f[0][i - 1];
        f[1][i] -= (unsigned)f[1][i - 1];
    }

    a[0] = 4096;
    for (i = 5; i > 0; i--) {
        tmp = f[0][6 - i] + (unsigned)f[1][6 - i] + 4096;
        a[6 - i] = tmp >> 13;

        tmp = f[0][6 - i] - (unsigned)f[1][6 - i] + 4096;
        a[5 + i] = tmp >> 13;
    }
}

static void lsp_interpolate2polydec(int16_t *a, const int16_t *lsf1,
                                    const int16_t *lsf2, int coef, int length)
{
    int16_t lsftmp[LPC_FILTERORDER];

    lsf_interpolate(lsftmp, lsf1, lsf2, coef, length);
    lsf2poly(a, lsftmp);
}

static void bw_expand(int16_t *out, const int16_t *in, const int16_t *coef, int length)
{
    int i;

    out[0] = in[0];
    for (i = 1; i < length; i++)
        out[i] = (coef[i] * in[i] + 16384) >> 15;
}

static void lsp_interpolate(int16_t *syntdenum, int16_t *weightdenum,
                            const int16_t *lsfdeq, int16_t length,
                            ILBCContext *s)
{
    int16_t lp[LPC_FILTERORDER + 1];
    const int16_t *const lsfdeq2 = lsfdeq + length;
    int i, pos, lp_length;

    lp_length = length + 1;

    if (s->mode == 30) {
        lsp_interpolate2polydec(lp, (*s).lsfdeqold, lsfdeq, lsf_weight_30ms[0], length);
        memcpy(syntdenum, lp, lp_length * 2);
        bw_expand(weightdenum, lp, kLpcChirpSyntDenum, lp_length);

        pos = lp_length;
        for (i = 1; i < 6; i++) {
            lsp_interpolate2polydec(lp, lsfdeq, lsfdeq2,
                                                 lsf_weight_30ms[i],
                                                 length);
            memcpy(syntdenum + pos, lp, lp_length * 2);
            bw_expand(weightdenum + pos, lp, kLpcChirpSyntDenum, lp_length);
            pos += lp_length;
        }
    } else {
        pos = 0;
        for (i = 0; i < s->nsub; i++) {
            lsp_interpolate2polydec(lp, s->lsfdeqold, lsfdeq,
                                    lsf_weight_20ms[i], length);
            memcpy(syntdenum + pos, lp, lp_length * 2);
            bw_expand(weightdenum + pos, lp, kLpcChirpSyntDenum, lp_length);
            pos += lp_length;
        }
    }

    if (s->mode == 30) {
        memcpy(s->lsfdeqold, lsfdeq2, length * 2);
    } else {
        memcpy(s->lsfdeqold, lsfdeq, length * 2);
    }
}

static void filter_mafq12(const int16_t *in_ptr, int16_t *out_ptr,
                          const int16_t *B, int16_t B_length,
                          int16_t length)
{
    int o, i, j;

    for (i = 0; i < length; i++) {
        const int16_t *b_ptr = &B[0];
        const int16_t *x_ptr = &in_ptr[i];

        o = 0;
        for (j = 0; j < B_length; j++)
            o += b_ptr[j] * *x_ptr--;

        o = av_clip(o, -134217728, 134215679);

        out_ptr[i] = ((o + 2048) >> 12);
    }
}

static void filter_arfq12(const int16_t *data_in,
                          int16_t *data_out,
                          const int16_t *coefficients,
                          int coefficients_length,
                          int data_length)
{
    int i, j;

    for (i = 0; i < data_length; i++) {
        int output = 0, sum = 0;

        for (j = coefficients_length - 1; j > 0; j--) {
            sum += (unsigned)(coefficients[j] * data_out[i - j]);
        }

        output = coefficients[0] * data_in[i] - (unsigned)sum;
        output = av_clip(output, -134217728, 134215679);

        data_out[i] = (output + 2048) >> 12;
    }
}

static void state_construct(int16_t ifm, const int16_t *idx,
                            const int16_t *synt_denum, int16_t *Out_fix,
                           int16_t len)
{
    int k;
    int16_t maxVal;
    int16_t *tmp1, *tmp3;
    const int16_t *tmp2;
    /* Stack based */
    int16_t numerator[1 + LPC_FILTERORDER];
    int16_t sampleValVec[2 * STATE_SHORT_LEN_30MS + LPC_FILTERORDER];
    int16_t sampleMaVec[2 * STATE_SHORT_LEN_30MS + LPC_FILTERORDER];
    int16_t *sampleVal = &sampleValVec[LPC_FILTERORDER];
    int16_t *sampleMa = &sampleMaVec[LPC_FILTERORDER];
    int16_t *sampleAr = &sampleValVec[LPC_FILTERORDER];

    /* initialization of coefficients */

    for (k = 0; k < LPC_FILTERORDER + 1; k++) {
        numerator[k] = synt_denum[LPC_FILTERORDER - k];
    }

    /* decoding of the maximum value */

    maxVal = frg_quant_mod[ifm];

    /* decoding of the sample values */
    tmp1 = sampleVal;
    tmp2 = &idx[len - 1];

    if (ifm < 37) {
        for (k = 0; k < len; k++) {
            /*the shifting is due to the Q13 in sq4_fixQ13[i], also the adding of 2097152 (= 0.5 << 22)
               maxVal is in Q8 and result is in Q(-1) */
            (*tmp1) = (int16_t) ((SPL_MUL_16_16(maxVal, ilbc_state[(*tmp2)]) + 2097152) >> 22);
            tmp1++;
            tmp2--;
        }
    } else if (ifm < 59) {
        for (k = 0; k < len; k++) {
            /*the shifting is due to the Q13 in sq4_fixQ13[i], also the adding of 262144 (= 0.5 << 19)
               maxVal is in Q5 and result is in Q(-1) */
            (*tmp1) = (int16_t) ((SPL_MUL_16_16(maxVal, ilbc_state[(*tmp2)]) + 262144) >> 19);
            tmp1++;
            tmp2--;
        }
    } else {
        for (k = 0; k < len; k++) {
            /*the shifting is due to the Q13 in sq4_fixQ13[i], also the adding of 65536 (= 0.5 << 17)
               maxVal is in Q3 and result is in Q(-1) */
            (*tmp1) = (int16_t) ((SPL_MUL_16_16(maxVal, ilbc_state[(*tmp2)]) + 65536) >> 17);
            tmp1++;
            tmp2--;
        }
    }

    /* Set the rest of the data to zero */
    memset(&sampleVal[len], 0, len * 2);

    /* circular convolution with all-pass filter */

    /* Set the state to zero */
    memset(sampleValVec, 0, LPC_FILTERORDER * 2);

    /* Run MA filter + AR filter */
    filter_mafq12(sampleVal, sampleMa, numerator, LPC_FILTERORDER + 1, len + LPC_FILTERORDER);
    memset(&sampleMa[len + LPC_FILTERORDER], 0, (len - LPC_FILTERORDER) * 2);
    filter_arfq12(sampleMa, sampleAr, synt_denum, LPC_FILTERORDER + 1, 2 * len);

    tmp1 = &sampleAr[len - 1];
    tmp2 = &sampleAr[2 * len - 1];
    tmp3 = Out_fix;
    for (k = 0; k < len; k++) {
        (*tmp3) = (*tmp1) + (*tmp2);
        tmp1--;
        tmp2--;
        tmp3++;
    }
}

static int16_t gain_dequantization(int index, int max_in, int stage)
{
    int16_t scale = FFMAX(1638, FFABS(max_in));

    return ((scale * ilbc_gain[stage][index]) + 8192) >> 14;
}

static void vector_rmultiplication(int16_t *out, const int16_t *in,
                                   const int16_t *win,
                                   int length, int shift)
{
    for (int i = 0; i < length; i++)
        out[i] = (in[i] * win[-i]) >> shift;
}

static void vector_multiplication(int16_t *out, const int16_t *in,
                                  const int16_t *win, int length,
                                  int shift)
{
    for (int i = 0; i < length; i++)
        out[i] = (in[i] * win[i]) >> shift;
}

static void add_vector_and_shift(int16_t *out, const int16_t *in1,
                                 const int16_t *in2, int length,
                                 int shift)
{
    for (int i = 0; i < length; i++)
        out[i] = (in1[i] + in2[i]) >> shift;
}

static void create_augmented_vector(int index, const int16_t *buffer, int16_t *cbVec)
{
    int16_t cbVecTmp[4];
    int interpolation_length = FFMIN(4, index);
    int16_t ilow = index - interpolation_length;

    memcpy(cbVec, buffer - index, index * 2);

    vector_multiplication(&cbVec[ilow], buffer - index - interpolation_length, alpha, interpolation_length, 15);
    vector_rmultiplication(cbVecTmp, buffer - interpolation_length, &alpha[interpolation_length - 1], interpolation_length, 15);
    add_vector_and_shift(&cbVec[ilow], &cbVec[ilow], cbVecTmp, interpolation_length, 0);

    memcpy(cbVec + index, buffer - index, FFMIN(SUBL - index, index) * sizeof(*cbVec));
}

static void get_codebook(int16_t * cbvec,   /* (o) Constructed codebook vector */
                     int16_t * mem,     /* (i) Codebook buffer */
                     int16_t index,     /* (i) Codebook index */
                     int16_t lMem,      /* (i) Length of codebook buffer */
                     int16_t cbveclen   /* (i) Codebook vector length */
)
{
    int16_t k, base_size;
    int16_t lag;
    /* Stack based */
    int16_t tempbuff2[SUBL + 5];

    /* Determine size of codebook sections */
    base_size = lMem - cbveclen + 1;

    if (cbveclen == SUBL) {
        base_size += cbveclen / 2;
    }

    /* No filter -> First codebook section */
    if (index < lMem - cbveclen + 1) {
        /* first non-interpolated vectors */

        k = index + cbveclen;
        /* get vector */
        memcpy(cbvec, mem + lMem - k, cbveclen * 2);
    } else if (index < base_size) {

        /* Calculate lag */

        k = (int16_t) SPL_MUL_16_16(2, (index - (lMem - cbveclen + 1))) + cbveclen;

        lag = k / 2;

        create_augmented_vector(lag, mem + lMem, cbvec);
    } else {
        int16_t memIndTest;

        /* first non-interpolated vectors */

        if (index - base_size < lMem - cbveclen + 1) {

            /* Set up filter memory, stuff zeros outside memory buffer */

            memIndTest = lMem - (index - base_size + cbveclen);

            memset(mem - CB_HALFFILTERLEN, 0, CB_HALFFILTERLEN * 2);
            memset(mem + lMem, 0, CB_HALFFILTERLEN * 2);

            /* do filtering to get the codebook vector */

            filter_mafq12(&mem[memIndTest + 4], cbvec, kCbFiltersRev, CB_FILTERLEN, cbveclen);
        } else {
            /* interpolated vectors */
            /* Stuff zeros outside memory buffer  */
            memIndTest = lMem - cbveclen - CB_FILTERLEN;
            memset(mem + lMem, 0, CB_HALFFILTERLEN * 2);

            /* do filtering */
            filter_mafq12(&mem[memIndTest + 7], tempbuff2, kCbFiltersRev, CB_FILTERLEN, (int16_t) (cbveclen + 5));

            /* Calculate lag index */
            lag = (cbveclen << 1) - 20 + index - base_size - lMem - 1;

            create_augmented_vector(lag, tempbuff2 + SUBL + 5, cbvec);
        }
    }
}

static void construct_vector (
    int16_t *decvector,   /* (o) Decoded vector */
    const int16_t *index,       /* (i) Codebook indices */
    const int16_t *gain_index,  /* (i) Gain quantization indices */
    int16_t *mem,         /* (i) Buffer for codevector construction */
    int16_t lMem,         /* (i) Length of buffer */
    int16_t veclen)
{
    int16_t gain[CB_NSTAGES];
    int16_t cbvec0[SUBL];
    int16_t cbvec1[SUBL];
    int16_t cbvec2[SUBL];
    unsigned a32;
    int16_t *gainPtr;
    int j;

    /* gain de-quantization */

    gain[0] = gain_dequantization(gain_index[0], 16384, 0);
    gain[1] = gain_dequantization(gain_index[1], gain[0], 1);
    gain[2] = gain_dequantization(gain_index[2], gain[1], 2);

    /* codebook vector construction and construction of total vector */

    /* Stack based */
    get_codebook(cbvec0, mem, index[0], lMem, veclen);
    get_codebook(cbvec1, mem, index[1], lMem, veclen);
    get_codebook(cbvec2, mem, index[2], lMem, veclen);

    gainPtr = &gain[0];
    for (j = 0; j < veclen; j++) {
        a32 = SPL_MUL_16_16(*gainPtr++, cbvec0[j]);
        a32 += SPL_MUL_16_16(*gainPtr++, cbvec1[j]);
        a32 += SPL_MUL_16_16(*gainPtr, cbvec2[j]);
        gainPtr -= 2;
        decvector[j] = (int)(a32 + 8192) >> 14;
    }
}

static void reverse_memcpy(int16_t *dest, const int16_t *source, int length)
{
    int16_t* destPtr = dest;
    const int16_t *sourcePtr = source;
    int j;

    for (j = 0; j < length; j++)
        *destPtr-- = *sourcePtr++;
}

static void decode_residual(ILBCContext *s,
                            ILBCFrame *encbits,
                            int16_t *decresidual,
                            const int16_t *syntdenum)
{
    int16_t meml_gotten, Nfor, Nback, diff, start_pos;
    int16_t subcount, subframe;
    int16_t *reverseDecresidual = s->enh_buf;        /* Reversed decoded data, used for decoding backwards in time (reuse memory in state) */
    int16_t *memVec = s->prevResidual;
    int16_t *mem = &memVec[CB_HALFFILTERLEN];   /* Memory for codebook */

    diff = STATE_LEN - s->state_short_len;

    if (encbits->state_first == 1) {
        start_pos = (encbits->start - 1) * SUBL;
    } else {
        start_pos = (encbits->start - 1) * SUBL + diff;
    }

    /* decode scalar part of start state */

    state_construct(encbits->ifm, encbits->idx, &syntdenum[(encbits->start - 1) * (LPC_FILTERORDER + 1)], &decresidual[start_pos], s->state_short_len);

    if (encbits->state_first) { /* put adaptive part in the end */
        /* setup memory */
        memset(mem, 0, (int16_t) (CB_MEML - s->state_short_len) * 2);
        memcpy(mem + CB_MEML - s->state_short_len, decresidual + start_pos, s->state_short_len * 2);

        /* construct decoded vector */

        construct_vector(&decresidual[start_pos + s->state_short_len], encbits->cb_index, encbits->gain_index, mem + CB_MEML - ST_MEM_L_TBL, ST_MEM_L_TBL, (int16_t) diff);

    } else { /* put adaptive part in the beginning */
        /* setup memory */
        meml_gotten = s->state_short_len;
        reverse_memcpy(mem + CB_MEML - 1, decresidual + start_pos, meml_gotten);
        memset(mem, 0, (int16_t) (CB_MEML - meml_gotten) * 2);

        /* construct decoded vector */
        construct_vector(reverseDecresidual, encbits->cb_index, encbits->gain_index, mem + CB_MEML - ST_MEM_L_TBL, ST_MEM_L_TBL, diff);

        /* get decoded residual from reversed vector */
        reverse_memcpy(&decresidual[start_pos - 1], reverseDecresidual, diff);
    }

    /* counter for predicted subframes */
    subcount = 1;

    /* forward prediction of subframes */
    Nfor = s->nsub - encbits->start - 1;

    if (Nfor > 0) {
        /* setup memory */
        memset(mem, 0, (CB_MEML - STATE_LEN) * 2);
        memcpy(mem + CB_MEML - STATE_LEN, decresidual + (encbits->start - 1) * SUBL, STATE_LEN * 2);

        /* loop over subframes to encode */
        for (subframe = 0; subframe < Nfor; subframe++) {
            /* construct decoded vector */
            construct_vector(&decresidual[(encbits->start + 1 + subframe) * SUBL], encbits->cb_index + subcount * CB_NSTAGES, encbits->gain_index + subcount * CB_NSTAGES, mem, MEM_LF_TBL, SUBL);

            /* update memory */
            memmove(mem, mem + SUBL, (CB_MEML - SUBL) * sizeof(*mem));
            memcpy(mem + CB_MEML - SUBL, &decresidual[(encbits->start + 1 + subframe) * SUBL], SUBL * 2);

            subcount++;
        }

    }

    /* backward prediction of subframes */
    Nback = encbits->start - 1;

    if (Nback > 0) {
        /* setup memory */
        meml_gotten = SUBL * (s->nsub + 1 - encbits->start);
        if (meml_gotten > CB_MEML) {
            meml_gotten = CB_MEML;
        }

        reverse_memcpy(mem + CB_MEML - 1, decresidual + (encbits->start - 1) * SUBL, meml_gotten);
        memset(mem, 0, (int16_t) (CB_MEML - meml_gotten) * 2);

        /* loop over subframes to decode */
        for (subframe = 0; subframe < Nback; subframe++) {
            /* construct decoded vector */
            construct_vector(&reverseDecresidual[subframe * SUBL], encbits->cb_index + subcount * CB_NSTAGES,
                        encbits->gain_index + subcount * CB_NSTAGES, mem, MEM_LF_TBL, SUBL);

            /* update memory */
            memmove(mem, mem + SUBL, (CB_MEML - SUBL) * sizeof(*mem));
            memcpy(mem + CB_MEML - SUBL, &reverseDecresidual[subframe * SUBL], SUBL * 2);

            subcount++;
        }

        /* get decoded residual from reversed vector */
        reverse_memcpy(decresidual + SUBL * Nback - 1, reverseDecresidual, SUBL * Nback);
    }
}

static int16_t max_abs_value_w16(const int16_t* vector, int length)
{
    int i = 0, absolute = 0, maximum = 0;

    if (vector == NULL || length <= 0) {
        return -1;
    }

    for (i = 0; i < length; i++) {
        absolute = FFABS(vector[i]);
        if (absolute > maximum)
            maximum = absolute;
    }

    // Guard the case for abs(-32768).
    return FFMIN(maximum, INT16_MAX);
}

static int16_t get_size_in_bits(uint32_t n)
{
    int16_t bits;

    if (0xFFFF0000 & n) {
        bits = 16;
    } else {
        bits = 0;
    }

    if (0x0000FF00 & (n >> bits)) bits += 8;
    if (0x000000F0 & (n >> bits)) bits += 4;
    if (0x0000000C & (n >> bits)) bits += 2;
    if (0x00000002 & (n >> bits)) bits += 1;
    if (0x00000001 & (n >> bits)) bits += 1;

    return bits;
}

static int32_t scale_dot_product(const int16_t *v1, const int16_t *v2, int length, int scaling)
{
    int64_t sum = 0;

    for (int i = 0; i < length; i++)
        sum += (v1[i] * v2[i]) >> scaling;

    return av_clipl_int32(sum);
}

static void correlation(int32_t *corr, int32_t *ener, const int16_t *buffer,
                        int16_t lag, int16_t blen, int16_t srange, int16_t scale)
{
    const int16_t *w16ptr = &buffer[blen - srange - lag];

    *corr = scale_dot_product(&buffer[blen - srange], w16ptr, srange, scale);
    *ener = scale_dot_product(w16ptr, w16ptr, srange, scale);

    if (*ener == 0) {
        *corr = 0;
        *ener = 1;
    }
}

#define SPL_SHIFT_W32(x, c) (((c) >= 0) ? ((x) << (c)) : ((x) >> (-(c))))

static int16_t norm_w32(int32_t a)
{
    if (a == 0) {
        return 0;
    } else if (a < 0) {
        a = ~a;
    }

    return ff_clz(a);
}

static int32_t div_w32_w16(int32_t num, int16_t den)
{
    if (den != 0)
        return num / den;
    else
        return 0x7FFFFFFF;
}

static void do_plc(int16_t *plc_residual,      /* (o) concealed residual */
                   int16_t *plc_lpc,           /* (o) concealed LP parameters */
                   int16_t PLI,                /* (i) packet loss indicator
                                                      0 - no PL, 1 = PL */
                   const int16_t *decresidual, /* (i) decoded residual */
                   const int16_t *lpc,         /* (i) decoded LPC (only used for no PL) */
                   int16_t inlag,              /* (i) pitch lag */
                   ILBCContext *s)             /* (i/o) decoder instance */
{
    int16_t i, pick;
    int32_t cross, ener, cross_comp, ener_comp = 0;
    int32_t measure, max_measure, energy;
    int16_t max, cross_square_max, cross_square;
    int16_t j, lag, tmp1, tmp2, randlag;
    int16_t shift1, shift2, shift3, shift_max;
    int16_t scale3;
    int16_t corrLen;
    int32_t tmpW32, tmp2W32;
    int16_t use_gain;
    int16_t tot_gain;
    int16_t max_perSquare;
    int16_t scale1, scale2;
    int16_t totscale;
    int32_t nom;
    int16_t denom;
    int16_t pitchfact;
    int16_t use_lag;
    int ind;
    int16_t randvec[BLOCKL_MAX];

    /* Packet Loss */
    if (PLI == 1) {

        s->consPLICount += 1;

        /* if previous frame not lost,
           determine pitch pred. gain */

        if (s->prevPLI != 1) {

            /* Maximum 60 samples are correlated, preserve as high accuracy
               as possible without getting overflow */
            max = max_abs_value_w16(s->prevResidual, s->block_samples);
            scale3 = (get_size_in_bits(max) << 1) - 25;
            if (scale3 < 0) {
                scale3 = 0;
            }

            /* Store scale for use when interpolating between the
             * concealment and the received packet */
            s->prevScale = scale3;

            /* Search around the previous lag +/-3 to find the
               best pitch period */
            lag = inlag - 3;

            /* Guard against getting outside the frame */
            corrLen = FFMIN(60, s->block_samples - (inlag + 3));

            correlation(&cross, &ener, s->prevResidual, lag, s->block_samples, corrLen, scale3);

            /* Normalize and store cross^2 and the number of shifts */
            shift_max = get_size_in_bits(FFABS(cross)) - 15;
            cross_square_max = (int16_t) SPL_MUL_16_16_RSFT(SPL_SHIFT_W32(cross, -shift_max), SPL_SHIFT_W32(cross, -shift_max), 15);

            for (j = inlag - 2; j <= inlag + 3; j++) {
                correlation(&cross_comp, &ener_comp, s->prevResidual, j, s->block_samples, corrLen, scale3);

                /* Use the criteria (corr*corr)/energy to compare if
                   this lag is better or not. To avoid the division,
                   do a cross multiplication */
                shift1 = get_size_in_bits(FFABS(cross_comp)) - 15;
                cross_square = (int16_t) SPL_MUL_16_16_RSFT(SPL_SHIFT_W32(cross_comp, -shift1), SPL_SHIFT_W32(cross_comp, -shift1), 15);

                shift2 = get_size_in_bits(ener) - 15;
                measure = SPL_MUL_16_16(SPL_SHIFT_W32(ener, -shift2), cross_square);

                shift3 = get_size_in_bits(ener_comp) - 15;
                max_measure = SPL_MUL_16_16(SPL_SHIFT_W32(ener_comp, -shift3), cross_square_max);

                /* Calculate shift value, so that the two measures can
                   be put in the same Q domain */
                if (((shift_max << 1) + shift3) > ((shift1 << 1) + shift2)) {
                    tmp1 = FFMIN(31, (shift_max << 1) + shift3 - (shift1 << 1) - shift2);
                    tmp2 = 0;
                } else {
                    tmp1 = 0;
                    tmp2 = FFMIN(31, (shift1 << 1) + shift2 - (shift_max << 1) - shift3);
                }

                if ((measure >> tmp1) > (max_measure >> tmp2)) {
                    /* New lag is better => record lag, measure and domain */
                    lag = j;
                    cross_square_max = cross_square;
                    cross = cross_comp;
                    shift_max = shift1;
                    ener = ener_comp;
                }
            }

            /* Calculate the periodicity for the lag with the maximum correlation.

               Definition of the periodicity:
               abs(corr(vec1, vec2))/(sqrt(energy(vec1))*sqrt(energy(vec2)))

               Work in the Square domain to simplify the calculations
               max_perSquare is less than 1 (in Q15)
             */
            tmp2W32 = scale_dot_product(&s->prevResidual[s->block_samples - corrLen], &s->prevResidual[s->block_samples - corrLen], corrLen, scale3);

            if ((tmp2W32 > 0) && (ener_comp > 0)) {
                /* norm energies to int16_t, compute the product of the energies and
                   use the upper int16_t as the denominator */

                scale1 = norm_w32(tmp2W32) - 16;
                tmp1 = SPL_SHIFT_W32(tmp2W32, scale1);

                scale2 = norm_w32(ener) - 16;
                tmp2 =  SPL_SHIFT_W32(ener, scale2);
                denom = SPL_MUL_16_16_RSFT(tmp1, tmp2, 16);    /* denom in Q(scale1+scale2-16) */

                /* Square the cross correlation and norm it such that max_perSquare
                   will be in Q15 after the division */

                totscale = scale1 + scale2 - 1;
                tmp1 = SPL_SHIFT_W32(cross, (totscale >> 1));
                tmp2 = SPL_SHIFT_W32(cross, totscale - (totscale >> 1));

                nom = SPL_MUL_16_16(tmp1, tmp2);
                max_perSquare = div_w32_w16(nom, denom);
            } else {
                max_perSquare = 0;
            }
        } else {
            /* previous frame lost, use recorded lag and gain */
            lag = s->prevLag;
            max_perSquare = s->per_square;
        }

        /* Attenuate signal and scale down pitch pred gain if
           several frames lost consecutively */

        use_gain = 32767;       /* 1.0 in Q15 */

        if (s->consPLICount * s->block_samples > 320) {
            use_gain = 29491;   /* 0.9 in Q15 */
        }

        /* Compute mixing factor of picth repeatition and noise:
           for max_per>0.7 set periodicity to 1.0
           0.4<max_per<0.7 set periodicity to (maxper-0.4)/0.7-0.4)
           max_per<0.4 set periodicity to 0.0
         */

        if (max_perSquare > 7868) {     /* periodicity > 0.7  (0.7^4=0.2401 in Q15) */
            pitchfact = 32767;
        } else if (max_perSquare > 839) {       /* 0.4 < periodicity < 0.7 (0.4^4=0.0256 in Q15) */
            /* find best index and interpolate from that */
            ind = 5;
            while ((max_perSquare < kPlcPerSqr[ind]) && (ind > 0)) {
                ind--;
            }
            /* pitch fact is approximated by first order */
            tmpW32 = kPlcPitchFact[ind] + SPL_MUL_16_16_RSFT(kPlcPfSlope[ind], (max_perSquare - kPlcPerSqr[ind]), 11);

            pitchfact = FFMIN(tmpW32, 32767); /* guard against overflow */

        } else {                /* periodicity < 0.4 */
            pitchfact = 0;
        }

        /* avoid repetition of same pitch cycle (buzzyness) */
        use_lag = lag;
        if (lag < 80) {
            use_lag = 2 * lag;
        }

        /* compute concealed residual */
        energy = 0;

        for (i = 0; i < s->block_samples; i++) {
            /* noise component -  52 < randlagFIX < 117 */
            s->seed = SPL_MUL_16_16(s->seed, 31821) + 13849;
            randlag = 53 + (s->seed & 63);

            pick = i - randlag;

            if (pick < 0) {
                randvec[i] = s->prevResidual[s->block_samples + pick];
            } else {
                randvec[i] = s->prevResidual[pick];
            }

            /* pitch repeatition component */
            pick = i - use_lag;

            if (pick < 0) {
                plc_residual[i] = s->prevResidual[s->block_samples + pick];
            } else {
                plc_residual[i] = plc_residual[pick];
            }

            /* Attinuate total gain for each 10 ms */
            if (i < 80) {
                tot_gain = use_gain;
            } else if (i < 160) {
                tot_gain = SPL_MUL_16_16_RSFT(31130, use_gain, 15);    /* 0.95*use_gain */
            } else {
                tot_gain = SPL_MUL_16_16_RSFT(29491, use_gain, 15);    /* 0.9*use_gain */
            }

            /* mix noise and pitch repeatition */
            plc_residual[i] = SPL_MUL_16_16_RSFT(tot_gain, (pitchfact * plc_residual[i] + (32767 - pitchfact) * randvec[i] + 16384) >> 15, 15);

            /* Shifting down the result one step extra to ensure that no overflow
               will occur */
            energy += SPL_MUL_16_16_RSFT(plc_residual[i], plc_residual[i], (s->prevScale + 1));

        }

        /* less than 30 dB, use only noise */
        if (energy < SPL_SHIFT_W32(s->block_samples * 900, -s->prevScale - 1)) {
            energy = 0;
            for (i = 0; i < s->block_samples; i++) {
                plc_residual[i] = randvec[i];
            }
        }

        /* use the old LPC */
        memcpy(plc_lpc, (*s).prev_lpc, (LPC_FILTERORDER + 1) * 2);

        /* Update state in case there are multiple frame losses */
        s->prevLag = lag;
        s->per_square = max_perSquare;
    } else { /* no packet loss, copy input */
        memcpy(plc_residual, decresidual, s->block_samples * 2);
        memcpy(plc_lpc, lpc, (LPC_FILTERORDER + 1) * 2);
        s->consPLICount = 0;
    }

    /* update state */
    s->prevPLI = PLI;
    memcpy(s->prev_lpc, plc_lpc, (LPC_FILTERORDER + 1) * 2);
    memcpy(s->prevResidual, plc_residual, s->block_samples * 2);

    return;
}

static int xcorr_coeff(const int16_t *target, const int16_t *regressor,
                       int16_t subl, int16_t searchLen,
                       int16_t offset, int16_t step)
{
    int16_t maxlag;
    int16_t pos;
    int16_t max;
    int16_t cross_corr_scale, energy_scale;
    int16_t cross_corr_sg_mod, cross_corr_sg_mod_max;
    int32_t cross_corr, energy;
    int16_t cross_corr_mod, energy_mod, enery_mod_max;
    const int16_t *rp;
    const int16_t *rp_beg, *rp_end;
    int16_t totscale, totscale_max;
    int16_t scalediff;
    int32_t new_crit, max_crit;
    int shifts;
    int k;

    /* Initializations, to make sure that the first one is selected */
    cross_corr_sg_mod_max = 0;
    enery_mod_max = INT16_MAX;
    totscale_max = -500;
    maxlag = 0;
    pos = 0;

    /* Find scale value and start position */
    if (step == 1) {
        max = max_abs_value_w16(regressor, (int16_t) (subl + searchLen - 1));
        rp_beg = regressor;
        rp_end = &regressor[subl];
    } else {                    /* step== -1 */
        max = max_abs_value_w16(&regressor[-searchLen], (int16_t) (subl + searchLen - 1));
        rp_beg = &regressor[-1];
        rp_end = &regressor[subl - 1];
    }

    /* Introduce a scale factor on the energy in int32_t in
       order to make sure that the calculation does not
       overflow */

    if (max > 5000) {
        shifts = 2;
    } else {
        shifts = 0;
    }

    /* Calculate the first energy, then do a +/- to get the other energies */
    energy = scale_dot_product(regressor, regressor, subl, shifts);

    for (k = 0; k < searchLen; k++) {
        rp = &regressor[pos];

        cross_corr = scale_dot_product(target, rp, subl, shifts);

        if ((energy > 0) && (cross_corr > 0)) {
            /* Put cross correlation and energy on 16 bit word */
            cross_corr_scale = norm_w32(cross_corr) - 16;
            cross_corr_mod = (int16_t) SPL_SHIFT_W32(cross_corr, cross_corr_scale);
            energy_scale = norm_w32(energy) - 16;
            energy_mod = (int16_t) SPL_SHIFT_W32(energy, energy_scale);

            /* Square cross correlation and store upper int16_t */
            cross_corr_sg_mod = (int16_t) SPL_MUL_16_16_RSFT(cross_corr_mod, cross_corr_mod, 16);

            /* Calculate the total number of (dynamic) right shifts that have
               been performed on (cross_corr*cross_corr)/energy
             */
            totscale = energy_scale - (cross_corr_scale * 2);

            /* Calculate the shift difference in order to be able to compare the two
               (cross_corr*cross_corr)/energy in the same domain
             */
            scalediff = totscale - totscale_max;
            scalediff = FFMIN(scalediff, 31);
            scalediff = FFMAX(scalediff, -31);

            /* Compute the cross multiplication between the old best criteria
               and the new one to be able to compare them without using a
               division */

            if (scalediff < 0) {
                new_crit = ((int32_t) cross_corr_sg_mod * enery_mod_max) >> (-scalediff);
                max_crit = ((int32_t) cross_corr_sg_mod_max * energy_mod);
            } else {
                new_crit = ((int32_t) cross_corr_sg_mod * enery_mod_max);
                max_crit = ((int32_t) cross_corr_sg_mod_max * energy_mod) >> scalediff;
            }

            /* Store the new lag value if the new criteria is larger
               than previous largest criteria */

            if (new_crit > max_crit) {
                cross_corr_sg_mod_max = cross_corr_sg_mod;
                enery_mod_max = energy_mod;
                totscale_max = totscale;
                maxlag = k;
            }
        }
        pos += step;

        /* Do a +/- to get the next energy */
        energy += (unsigned)step * ((*rp_end * *rp_end - *rp_beg * *rp_beg) >> shifts);

        rp_beg += step;
        rp_end += step;
    }

    return maxlag + offset;
}

static void hp_output(int16_t *signal, const int16_t *ba, int16_t *y,
                      int16_t *x, int16_t len)
{
    int32_t tmp;

    for (int i = 0; i < len; i++) {
        tmp = SPL_MUL_16_16(y[1], ba[3]);     /* (-a[1])*y[i-1] (low part) */
        tmp += SPL_MUL_16_16(y[3], ba[4]);    /* (-a[2])*y[i-2] (low part) */
        tmp = (tmp >> 15);
        tmp += SPL_MUL_16_16(y[0], ba[3]);    /* (-a[1])*y[i-1] (high part) */
        tmp += SPL_MUL_16_16(y[2], ba[4]);    /* (-a[2])*y[i-2] (high part) */
        tmp = (tmp * 2);

        tmp += SPL_MUL_16_16(signal[i], ba[0]);       /* b[0]*x[0] */
        tmp += SPL_MUL_16_16(x[0], ba[1]);    /* b[1]*x[i-1] */
        tmp += SPL_MUL_16_16(x[1], ba[2]);    /* b[2]*x[i-2] */

        /* Update state (input part) */
        x[1] = x[0];
        x[0] = signal[i];

        /* Convert back to Q0 and multiply with 2 */
        signal[i] = av_clip_intp2(tmp + 1024, 26) >> 11;

        /* Update state (filtered part) */
        y[2] = y[0];
        y[3] = y[1];

        /* upshift tmp by 3 with saturation */
        if (tmp > 268435455) {
            tmp = INT32_MAX;
        } else if (tmp < -268435456) {
            tmp = INT32_MIN;
        } else {
            tmp = tmp * 8;
        }

        y[0] = tmp >> 16;
        y[1] = (tmp - (y[0] * (1 << 16))) >> 1;
    }
}

static int ilbc_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                             int *got_frame_ptr, AVPacket *avpkt)
{
    ILBCContext *s     = avctx->priv_data;
    int mode = s->mode, ret;
    int16_t *plc_data = &s->plc_residual[LPC_FILTERORDER];

    memset(&s->frame, 0, sizeof(ILBCFrame));
    ret = unpack_frame(s, avpkt->data, avpkt->size);
    if (ret < 0)
        return ret;
    if (ret)
        mode = 0;

    frame->nb_samples = s->block_samples;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    if (s->frame.start < 1 || s->frame.start > 5)
        mode = 0;

    if (mode) {
        index_conv(s->frame.cb_index);

        lsf_dequantization(s->lsfdeq, s->frame.lsf, s->lpc_n);
        lsf_check_stability(s->lsfdeq, LPC_FILTERORDER, s->lpc_n);
        lsp_interpolate(s->syntdenum, s->weightdenum,
                        s->lsfdeq, LPC_FILTERORDER, s);
        decode_residual(s, &s->frame, s->decresidual, s->syntdenum);

        do_plc(s->plc_residual, s->plc_lpc, 0,
                               s->decresidual, s->syntdenum + (LPC_FILTERORDER + 1) * (s->nsub - 1),
                               s->last_lag, s);

        memcpy(s->decresidual, s->plc_residual, s->block_samples * 2);
    }

    if (s->enhancer) {
        /* TODO */
    } else {
        int16_t lag, i;

        /* Find last lag (since the enhancer is not called to give this info) */
        if (s->mode == 20) {
            lag = xcorr_coeff(&s->decresidual[s->block_samples-60], &s->decresidual[s->block_samples-80],
                              60, 80, 20, -1);
        } else {
            lag = xcorr_coeff(&s->decresidual[s->block_samples-ENH_BLOCKL],
                              &s->decresidual[s->block_samples-ENH_BLOCKL-20],
                              ENH_BLOCKL, 100, 20, -1);
        }

        /* Store lag (it is needed if next packet is lost) */
        s->last_lag = lag;

        /* copy data and run synthesis filter */
        memcpy(plc_data, s->decresidual, s->block_samples * 2);

        /* Set up the filter state */
        memcpy(&plc_data[-LPC_FILTERORDER], s->syntMem, LPC_FILTERORDER * 2);

        for (i = 0; i < s->nsub; i++) {
            filter_arfq12(plc_data+i*SUBL, plc_data+i*SUBL,
                                      s->syntdenum + i*(LPC_FILTERORDER + 1),
                                      LPC_FILTERORDER + 1, SUBL);
        }

        /* Save the filter state */
        memcpy(s->syntMem, &plc_data[s->block_samples-LPC_FILTERORDER], LPC_FILTERORDER * 2);
    }

    memcpy(frame->data[0], plc_data, s->block_samples * 2);

    hp_output((int16_t *)frame->data[0], hp_out_coeffs,
              s->hpimemy, s->hpimemx, s->block_samples);

    memcpy(s->old_syntdenum, s->syntdenum, s->nsub*(LPC_FILTERORDER + 1) * 2);

    s->prev_enh_pl = 0;
    if (mode == 0)
        s->prev_enh_pl = 1;

    *got_frame_ptr = 1;

    return avpkt->size;
}

static av_cold int ilbc_decode_init(AVCodecContext *avctx)
{
    ILBCContext *s  = avctx->priv_data;

    if (avctx->block_align == 38)
        s->mode = 20;
    else if (avctx->block_align == 50)
        s->mode = 30;
    else if (avctx->bit_rate > 0)
        s->mode = avctx->bit_rate <= 14000 ? 30 : 20;
    else
        return AVERROR_INVALIDDATA;

    av_channel_layout_uninit(&avctx->ch_layout);
    avctx->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
    avctx->sample_rate    = 8000;
    avctx->sample_fmt     = AV_SAMPLE_FMT_S16;

    if (s->mode == 30) {
        s->block_samples = 240;
        s->nsub = NSUB_30MS;
        s->nasub = NASUB_30MS;
        s->lpc_n = LPC_N_30MS;
        s->state_short_len = STATE_SHORT_LEN_30MS;
    } else {
        s->block_samples = 160;
        s->nsub = NSUB_20MS;
        s->nasub = NASUB_20MS;
        s->lpc_n = LPC_N_20MS;
        s->state_short_len = STATE_SHORT_LEN_20MS;
    }

    return 0;
}

const FFCodec ff_ilbc_decoder = {
    .p.name         = "ilbc",
    CODEC_LONG_NAME("iLBC (Internet Low Bitrate Codec)"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_ILBC,
    .init           = ilbc_decode_init,
    FF_CODEC_DECODE_CB(ilbc_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_CHANNEL_CONF,
    .priv_data_size = sizeof(ILBCContext),
};
