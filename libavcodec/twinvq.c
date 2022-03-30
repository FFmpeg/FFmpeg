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

#include <math.h>
#include <stdint.h>

#include "libavutil/channel_layout.h"
#include "libavutil/float_dsp.h"
#include "avcodec.h"
#include "fft.h"
#include "internal.h"
#include "lsp.h"
#include "sinewin.h"
#include "twinvq.h"

/**
 * Evaluate a single LPC amplitude spectrum envelope coefficient from the line
 * spectrum pairs.
 *
 * @param lsp a vector of the cosine of the LSP values
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
    float p         = 0.5f;
    float q         = 0.5f;
    float two_cos_w = 2.0f * cos_val;

    for (j = 0; j + 1 < order; j += 2 * 2) {
        // Unroll the loop once since order is a multiple of four
        q *= lsp[j]     - two_cos_w;
        p *= lsp[j + 1] - two_cos_w;

        q *= lsp[j + 2] - two_cos_w;
        p *= lsp[j + 3] - two_cos_w;
    }

    p *= p * (2.0f - two_cos_w);
    q *= q * (2.0f + two_cos_w);

    return 0.5 / (p + q);
}

/**
 * Evaluate the LPC amplitude spectrum envelope from the line spectrum pairs.
 */
static void eval_lpcenv(TwinVQContext *tctx, const float *cos_vals, float *lpc)
{
    int i;
    const TwinVQModeTab *mtab = tctx->mtab;
    int size_s = mtab->size / mtab->fmode[TWINVQ_FT_SHORT].sub;

    for (i = 0; i < size_s / 2; i++) {
        float cos_i = tctx->cos_tabs[0][i];
        lpc[i]              = eval_lpc_spectrum(cos_vals,  cos_i, mtab->n_lsp);
        lpc[size_s - i - 1] = eval_lpc_spectrum(cos_vals, -cos_i, mtab->n_lsp);
    }
}

static void interpolate(float *out, float v1, float v2, int size)
{
    int i;
    float step = (v1 - v2) / (size + 1);

    for (i = 0; i < size; i++) {
        v2    += step;
        out[i] = v2;
    }
}

static inline float get_cos(int idx, int part, const float *cos_tab, int size)
{
    return part ? -cos_tab[size - idx - 1]
                :  cos_tab[idx];
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
 * @param in the cosine of the LSP data
 * @param part is 0 for 0...PI (positive cosine values) and 1 for PI...2PI
 *        (negative cosine values)
 * @param size the size of the whole output
 */
static inline void eval_lpcenv_or_interp(TwinVQContext *tctx,
                                         enum TwinVQFrameType ftype,
                                         float *out, const float *in,
                                         int size, int step, int part)
{
    int i;
    const TwinVQModeTab *mtab = tctx->mtab;
    const float *cos_tab      = tctx->cos_tabs[ftype];

    // Fill the 's'
    for (i = 0; i < size; i += step)
        out[i] =
            eval_lpc_spectrum(in,
                              get_cos(i, part, cos_tab, size),
                              mtab->n_lsp);

    // Fill the 'iiiibiiii'
    for (i = step; i <= size - 2 * step; i += step) {
        if (out[i + step] + out[i - step] > 1.95 * out[i] ||
            out[i + step]                 >= out[i - step]) {
            interpolate(out + i - step + 1, out[i], out[i - step], step - 1);
        } else {
            out[i - step / 2] =
                eval_lpc_spectrum(in,
                                  get_cos(i - step / 2, part, cos_tab, size),
                                  mtab->n_lsp);
            interpolate(out + i - step + 1, out[i - step / 2],
                        out[i - step], step / 2 - 1);
            interpolate(out + i - step / 2 + 1, out[i],
                        out[i - step / 2], step / 2 - 1);
        }
    }

    interpolate(out + size - 2 * step + 1, out[size - step],
                out[size - 2 * step], step - 1);
}

static void eval_lpcenv_2parts(TwinVQContext *tctx, enum TwinVQFrameType ftype,
                               const float *buf, float *lpc,
                               int size, int step)
{
    eval_lpcenv_or_interp(tctx, ftype, lpc, buf, size / 2, step, 0);
    eval_lpcenv_or_interp(tctx, ftype, lpc + size / 2, buf, size / 2,
                          2 * step, 1);

    interpolate(lpc + size / 2 - step + 1, lpc[size / 2],
                lpc[size / 2 - step], step);

    twinvq_memset_float(lpc + size - 2 * step + 1, lpc[size - 2 * step],
                        2 * step - 1);
}

/**
 * Inverse quantization. Read CB coefficients for cb1 and cb2 from the
 * bitstream, sum the corresponding vectors and write the result to *out
 * after permutation.
 */
static void dequant(TwinVQContext *tctx, const uint8_t *cb_bits, float *out,
                    enum TwinVQFrameType ftype,
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
        tmp0 = *cb_bits++;
        if (bits == 7) {
            if (tmp0 & 0x40)
                sign0 = -1;
            tmp0 &= 0x3F;
        }

        bits = tctx->bits_main_spec[1][ftype][bitstream_second_part];
        tmp1 = *cb_bits++;
        if (bits == 7) {
            if (tmp1 & 0x40)
                sign1 = -1;
            tmp1 &= 0x3F;
        }

        tab0 = cb0 + tmp0 * cb_len;
        tab1 = cb1 + tmp1 * cb_len;

        for (j = 0; j < length; j++)
            out[tctx->permut[ftype][pos + j]] = sign0 * tab0[j] +
                                                sign1 * tab1[j];

        pos += length;
    }
}

static void dec_gain(TwinVQContext *tctx,
                     enum TwinVQFrameType ftype, float *out)
{
    const TwinVQModeTab   *mtab =  tctx->mtab;
    const TwinVQFrameData *bits = &tctx->bits[tctx->cur_frame];
    int i, j;
    int channels   = tctx->avctx->ch_layout.nb_channels;
    int sub        = mtab->fmode[ftype].sub;
    float step     = TWINVQ_AMP_MAX     / ((1 << TWINVQ_GAIN_BITS)     - 1);
    float sub_step = TWINVQ_SUB_AMP_MAX / ((1 << TWINVQ_SUB_GAIN_BITS) - 1);

    if (ftype == TWINVQ_FT_LONG) {
        for (i = 0; i < channels; i++)
            out[i] = (1.0 / (1 << 13)) *
                     twinvq_mulawinv(step * 0.5 + step * bits->gain_bits[i],
                                     TWINVQ_AMP_MAX, TWINVQ_MULAW_MU);
    } else {
        for (i = 0; i < channels; i++) {
            float val = (1.0 / (1 << 23)) *
                        twinvq_mulawinv(step * 0.5 + step * bits->gain_bits[i],
                                        TWINVQ_AMP_MAX, TWINVQ_MULAW_MU);

            for (j = 0; j < sub; j++)
                out[i * sub + j] =
                    val * twinvq_mulawinv(sub_step * 0.5 +
                                          sub_step * bits->sub_gain_bits[i * sub + j],
                                          TWINVQ_SUB_AMP_MAX, TWINVQ_MULAW_MU);
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
        if (lsp[i] - lsp[i - 1] < min_dist) {
            float avg = (lsp[i] + lsp[i - 1]) * 0.5;

            lsp[i - 1] = avg - min_dist2;
            lsp[i]     = avg + min_dist2;
        }
}

static void decode_lsp(TwinVQContext *tctx, int lpc_idx1, uint8_t *lpc_idx2,
                       int lpc_hist_idx, float *lsp, float *hist)
{
    const TwinVQModeTab *mtab = tctx->mtab;
    int i, j;

    const float *cb  = mtab->lspcodebook;
    const float *cb2 = cb  + (1 << mtab->lsp_bit1) * mtab->n_lsp;
    const float *cb3 = cb2 + (1 << mtab->lsp_bit2) * mtab->n_lsp;

    const int8_t funny_rounding[4] = {
        -2,
        mtab->lsp_split == 4 ? -2 : 1,
        mtab->lsp_split == 4 ? -2 : 1,
        0
    };

    j = 0;
    for (i = 0; i < mtab->lsp_split; i++) {
        int chunk_end = ((i + 1) * mtab->n_lsp + funny_rounding[i]) /
                        mtab->lsp_split;
        for (; j < chunk_end; j++)
            lsp[j] = cb[lpc_idx1     * mtab->n_lsp + j] +
                     cb2[lpc_idx2[i] * mtab->n_lsp + j];
    }

    rearrange_lsp(mtab->n_lsp, lsp, 0.0001);

    for (i = 0; i < mtab->n_lsp; i++) {
        float tmp1 = 1.0     - cb3[lpc_hist_idx * mtab->n_lsp + i];
        float tmp2 = hist[i] * cb3[lpc_hist_idx * mtab->n_lsp + i];
        hist[i] = lsp[i];
        lsp[i]  = lsp[i] * tmp1 + tmp2;
    }

    rearrange_lsp(mtab->n_lsp, lsp, 0.0001);
    rearrange_lsp(mtab->n_lsp, lsp, 0.000095);
    ff_sort_nearly_sorted_floats(lsp, mtab->n_lsp);
}

static void dec_lpc_spectrum_inv(TwinVQContext *tctx, float *lsp,
                                 enum TwinVQFrameType ftype, float *lpc)
{
    int i;
    int size = tctx->mtab->size / tctx->mtab->fmode[ftype].sub;

    for (i = 0; i < tctx->mtab->n_lsp; i++)
        lsp[i] = 2 * cos(lsp[i]);

    switch (ftype) {
    case TWINVQ_FT_LONG:
        eval_lpcenv_2parts(tctx, ftype, lsp, lpc, size, 8);
        break;
    case TWINVQ_FT_MEDIUM:
        eval_lpcenv_2parts(tctx, ftype, lsp, lpc, size, 2);
        break;
    case TWINVQ_FT_SHORT:
        eval_lpcenv(tctx, lsp, lpc);
        break;
    }
}

static const uint8_t wtype_to_wsize[] = { 0, 0, 2, 2, 2, 1, 0, 1, 1 };

static void imdct_and_window(TwinVQContext *tctx, enum TwinVQFrameType ftype,
                             int wtype, float *in, float *prev, int ch)
{
    FFTContext *mdct = &tctx->mdct_ctx[ftype];
    const TwinVQModeTab *mtab = tctx->mtab;
    int bsize = mtab->size / mtab->fmode[ftype].sub;
    int size  = mtab->size;
    float *buf1 = tctx->tmp_buf;
    int j, first_wsize, wsize; // Window size
    float *out  = tctx->curr_frame + 2 * ch * mtab->size;
    float *out2 = out;
    float *prev_buf;
    int types_sizes[] = {
        mtab->size /  mtab->fmode[TWINVQ_FT_LONG].sub,
        mtab->size /  mtab->fmode[TWINVQ_FT_MEDIUM].sub,
        mtab->size / (mtab->fmode[TWINVQ_FT_SHORT].sub * 2),
    };

    wsize       = types_sizes[wtype_to_wsize[wtype]];
    first_wsize = wsize;
    prev_buf    = prev + (size - bsize) / 2;

    for (j = 0; j < mtab->fmode[ftype].sub; j++) {
        int sub_wtype = ftype == TWINVQ_FT_MEDIUM ? 8 : wtype;

        if (!j && wtype == 4)
            sub_wtype = 4;
        else if (j == mtab->fmode[ftype].sub - 1 && wtype == 7)
            sub_wtype = 7;

        wsize = types_sizes[wtype_to_wsize[sub_wtype]];

        mdct->imdct_half(mdct, buf1 + bsize * j, in + bsize * j);

        tctx->fdsp->vector_fmul_window(out2, prev_buf + (bsize - wsize) / 2,
                                      buf1 + bsize * j,
                                      ff_sine_windows[av_log2(wsize)],
                                      wsize / 2);
        out2 += wsize;

        memcpy(out2, buf1 + bsize * j + wsize / 2,
               (bsize - wsize / 2) * sizeof(float));

        out2 += ftype == TWINVQ_FT_MEDIUM ? (bsize - wsize) / 2 : bsize - wsize;

        prev_buf = buf1 + bsize * j + bsize / 2;
    }

    tctx->last_block_pos[ch] = (size + first_wsize) / 2;
}

static void imdct_output(TwinVQContext *tctx, enum TwinVQFrameType ftype,
                         int wtype, float **out, int offset)
{
    const TwinVQModeTab *mtab = tctx->mtab;
    float *prev_buf           = tctx->prev_frame + tctx->last_block_pos[0];
    int channels              = tctx->avctx->ch_layout.nb_channels;
    int size1, size2, i;
    float *out1, *out2;

    for (i = 0; i < channels; i++)
        imdct_and_window(tctx, ftype, wtype,
                         tctx->spectrum + i * mtab->size,
                         prev_buf + 2 * i * mtab->size,
                         i);

    if (!out)
        return;

    size2 = tctx->last_block_pos[0];
    size1 = mtab->size - size2;

    out1 = &out[0][0] + offset;
    memcpy(out1,         prev_buf,         size1 * sizeof(*out1));
    memcpy(out1 + size1, tctx->curr_frame, size2 * sizeof(*out1));

    if (channels == 2) {
        out2 = &out[1][0] + offset;
        memcpy(out2, &prev_buf[2 * mtab->size],
               size1 * sizeof(*out2));
        memcpy(out2 + size1, &tctx->curr_frame[2 * mtab->size],
               size2 * sizeof(*out2));
        tctx->fdsp->butterflies_float(out1, out2, mtab->size);
    }
}

static void read_and_decode_spectrum(TwinVQContext *tctx, float *out,
                                     enum TwinVQFrameType ftype)
{
    const TwinVQModeTab *mtab = tctx->mtab;
    TwinVQFrameData *bits     = &tctx->bits[tctx->cur_frame];
    int channels              = tctx->avctx->ch_layout.nb_channels;
    int sub        = mtab->fmode[ftype].sub;
    int block_size = mtab->size / sub;
    float gain[TWINVQ_CHANNELS_MAX * TWINVQ_SUBBLOCKS_MAX];
    float ppc_shape[TWINVQ_PPC_SHAPE_LEN_MAX * TWINVQ_CHANNELS_MAX * 4];

    int i, j;

    dequant(tctx, bits->main_coeffs, out, ftype,
            mtab->fmode[ftype].cb0, mtab->fmode[ftype].cb1,
            mtab->fmode[ftype].cb_len_read);

    dec_gain(tctx, ftype, gain);

    if (ftype == TWINVQ_FT_LONG) {
        int cb_len_p = (tctx->n_div[3] + mtab->ppc_shape_len * channels - 1) /
                       tctx->n_div[3];
        dequant(tctx, bits->ppc_coeffs, ppc_shape,
                TWINVQ_FT_PPC, mtab->ppc_shape_cb,
                mtab->ppc_shape_cb + cb_len_p * TWINVQ_PPC_SHAPE_CB_SIZE,
                cb_len_p);
    }

    for (i = 0; i < channels; i++) {
        float *chunk = out + mtab->size * i;
        float lsp[TWINVQ_LSP_COEFS_MAX];

        for (j = 0; j < sub; j++) {
            tctx->dec_bark_env(tctx, bits->bark1[i][j],
                               bits->bark_use_hist[i][j], i,
                               tctx->tmp_buf, gain[sub * i + j], ftype);

            tctx->fdsp->vector_fmul(chunk + block_size * j,
                                   chunk + block_size * j,
                                   tctx->tmp_buf, block_size);
        }

        if (ftype == TWINVQ_FT_LONG)
            tctx->decode_ppc(tctx, bits->p_coef[i], bits->g_coef[i],
                             ppc_shape + i * mtab->ppc_shape_len, chunk);

        decode_lsp(tctx, bits->lpc_idx1[i], bits->lpc_idx2[i],
                   bits->lpc_hist_idx[i], lsp, tctx->lsp_hist[i]);

        dec_lpc_spectrum_inv(tctx, lsp, ftype, tctx->tmp_buf);

        for (j = 0; j < mtab->fmode[ftype].sub; j++) {
            tctx->fdsp->vector_fmul(chunk, chunk, tctx->tmp_buf, block_size);
            chunk += block_size;
        }
    }
}

const enum TwinVQFrameType ff_twinvq_wtype_to_ftype_table[] = {
    TWINVQ_FT_LONG,   TWINVQ_FT_LONG, TWINVQ_FT_SHORT, TWINVQ_FT_LONG,
    TWINVQ_FT_MEDIUM, TWINVQ_FT_LONG, TWINVQ_FT_LONG,  TWINVQ_FT_MEDIUM,
    TWINVQ_FT_MEDIUM
};

int ff_twinvq_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                           int *got_frame_ptr, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    TwinVQContext *tctx = avctx->priv_data;
    const TwinVQModeTab *mtab = tctx->mtab;
    float **out = NULL;
    int ret;

    /* get output buffer */
    if (tctx->discarded_packets >= 2) {
        frame->nb_samples = mtab->size * tctx->frames_per_packet;
        if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
            return ret;
        out = (float **)frame->extended_data;
    }

    if (buf_size < avctx->block_align) {
        av_log(avctx, AV_LOG_ERROR,
               "Frame too small (%d bytes). Truncated file?\n", buf_size);
        return AVERROR(EINVAL);
    }

    if ((ret = tctx->read_bitstream(avctx, tctx, buf, buf_size)) < 0)
        return ret;

    for (tctx->cur_frame = 0; tctx->cur_frame < tctx->frames_per_packet;
         tctx->cur_frame++) {
        read_and_decode_spectrum(tctx, tctx->spectrum,
                                 tctx->bits[tctx->cur_frame].ftype);

        imdct_output(tctx, tctx->bits[tctx->cur_frame].ftype,
                     tctx->bits[tctx->cur_frame].window_type, out,
                     tctx->cur_frame * mtab->size);

        FFSWAP(float *, tctx->curr_frame, tctx->prev_frame);
    }

    if (tctx->discarded_packets < 2) {
        tctx->discarded_packets++;
        *got_frame_ptr = 0;
        return buf_size;
    }

    *got_frame_ptr = 1;

    // VQF can deliver packets 1 byte greater than block align
    if (buf_size == avctx->block_align + 1)
        return buf_size;
    return avctx->block_align;
}

/**
 * Init IMDCT and windowing tables
 */
static av_cold int init_mdct_win(TwinVQContext *tctx)
{
    int i, j, ret;
    const TwinVQModeTab *mtab = tctx->mtab;
    int size_s = mtab->size / mtab->fmode[TWINVQ_FT_SHORT].sub;
    int size_m = mtab->size / mtab->fmode[TWINVQ_FT_MEDIUM].sub;
    int channels = tctx->avctx->ch_layout.nb_channels;
    float norm = channels == 1 ? 2.0 : 1.0;
    int table_size = 2 * mtab->size * channels;

    for (i = 0; i < 3; i++) {
        int bsize = tctx->mtab->size / tctx->mtab->fmode[i].sub;
        if ((ret = ff_mdct_init(&tctx->mdct_ctx[i], av_log2(bsize) + 1, 1,
                                -sqrt(norm / bsize) / (1 << 15))))
            return ret;
    }

    if (!FF_ALLOC_TYPED_ARRAY(tctx->tmp_buf,    mtab->size) ||
        !FF_ALLOC_TYPED_ARRAY(tctx->spectrum,   table_size) ||
        !FF_ALLOC_TYPED_ARRAY(tctx->curr_frame, table_size) ||
        !FF_ALLOC_TYPED_ARRAY(tctx->prev_frame, table_size))
        return AVERROR(ENOMEM);

    for (i = 0; i < 3; i++) {
        int m       = 4 * mtab->size / mtab->fmode[i].sub;
        double freq = 2 * M_PI / m;
        if (!FF_ALLOC_TYPED_ARRAY(tctx->cos_tabs[i], m / 4))
            return AVERROR(ENOMEM);
        for (j = 0; j <= m / 8; j++)
            tctx->cos_tabs[i][j] = cos((2 * j + 1) * freq);
        for (j = 1; j < m / 8; j++)
            tctx->cos_tabs[i][m / 4 - j] = tctx->cos_tabs[i][j];
    }

    ff_init_ff_sine_windows(av_log2(size_m));
    ff_init_ff_sine_windows(av_log2(size_s / 2));
    ff_init_ff_sine_windows(av_log2(mtab->size));

    return 0;
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
                              enum TwinVQFrameType ftype)
{
    int i, j;

    for (i = 0; i < line_len[0]; i++) {
        int shift;

        if (num_blocks == 1                                    ||
            (ftype == TWINVQ_FT_LONG && num_vect % num_blocks) ||
            (ftype != TWINVQ_FT_LONG && num_vect & 1)          ||
            i == line_len[1]) {
            shift = 0;
        } else if (ftype == TWINVQ_FT_LONG) {
            shift = i;
        } else
            shift = i * i;

        for (j = 0; j < num_vect && (j + num_vect * i < block_size * num_blocks); j++)
            tab[i * num_vect + j] = i * num_vect + (j + shift) % num_vect;
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
    int i, j;
    int cont = 0;

    for (i = 0; i < num_vect; i++)
        for (j = 0; j < line_len[i >= length_div]; j++)
            out[cont++] = in[j * num_vect + i];
}

static void linear_perm(int16_t *out, int16_t *in, int n_blocks, int size)
{
    int block_size = size / n_blocks;
    int i;

    for (i = 0; i < size; i++)
        out[i] = block_size * (in[i] % n_blocks) + in[i] / n_blocks;
}

static av_cold void construct_perm_table(TwinVQContext *tctx,
                                         enum TwinVQFrameType ftype)
{
    int block_size, size;
    const TwinVQModeTab *mtab = tctx->mtab;
    int16_t *tmp_perm = (int16_t *)tctx->tmp_buf;

    if (ftype == TWINVQ_FT_PPC) {
        size       = tctx->avctx->ch_layout.nb_channels;
        block_size = mtab->ppc_shape_len;
    } else {
        size       = tctx->avctx->ch_layout.nb_channels * mtab->fmode[ftype].sub;
        block_size = mtab->size / mtab->fmode[ftype].sub;
    }

    permutate_in_line(tmp_perm, tctx->n_div[ftype], size,
                      block_size, tctx->length[ftype],
                      tctx->length_change[ftype], ftype);

    transpose_perm(tctx->permut[ftype], tmp_perm, tctx->n_div[ftype],
                   tctx->length[ftype], tctx->length_change[ftype]);

    linear_perm(tctx->permut[ftype], tctx->permut[ftype], size,
                size * block_size);
}

static av_cold void init_bitstream_params(TwinVQContext *tctx)
{
    const TwinVQModeTab *mtab = tctx->mtab;
    int n_ch                  = tctx->avctx->ch_layout.nb_channels;
    int total_fr_bits         = tctx->avctx->bit_rate * mtab->size /
                                tctx->avctx->sample_rate;

    int lsp_bits_per_block = n_ch * (mtab->lsp_bit0 + mtab->lsp_bit1 +
                                     mtab->lsp_split * mtab->lsp_bit2);

    int ppc_bits = n_ch * (mtab->pgain_bit + mtab->ppc_shape_bit +
                           mtab->ppc_period_bit);

    int bsize_no_main_cb[3], bse_bits[3], i;
    enum TwinVQFrameType frametype;

    for (i = 0; i < 3; i++)
        // +1 for history usage switch
        bse_bits[i] = n_ch *
                      (mtab->fmode[i].bark_n_coef *
                       mtab->fmode[i].bark_n_bit + 1);

    bsize_no_main_cb[2] = bse_bits[2] + lsp_bits_per_block + ppc_bits +
                          TWINVQ_WINDOW_TYPE_BITS + n_ch * TWINVQ_GAIN_BITS;

    for (i = 0; i < 2; i++)
        bsize_no_main_cb[i] =
            lsp_bits_per_block + n_ch * TWINVQ_GAIN_BITS +
            TWINVQ_WINDOW_TYPE_BITS +
            mtab->fmode[i].sub * (bse_bits[i] + n_ch * TWINVQ_SUB_GAIN_BITS);

    if (tctx->codec == TWINVQ_CODEC_METASOUND && !tctx->is_6kbps) {
        bsize_no_main_cb[1] += 2;
        bsize_no_main_cb[2] += 2;
    }

    // The remaining bits are all used for the main spectrum coefficients
    for (i = 0; i < 4; i++) {
        int bit_size, vect_size;
        int rounded_up, rounded_down, num_rounded_down, num_rounded_up;
        if (i == 3) {
            bit_size  = n_ch * mtab->ppc_shape_bit;
            vect_size = n_ch * mtab->ppc_shape_len;
        } else {
            bit_size  = total_fr_bits - bsize_no_main_cb[i];
            vect_size = n_ch * mtab->size;
        }

        tctx->n_div[i] = (bit_size + 13) / 14;

        rounded_up                     = (bit_size + tctx->n_div[i] - 1) /
                                         tctx->n_div[i];
        rounded_down                   = (bit_size) / tctx->n_div[i];
        num_rounded_down               = rounded_up * tctx->n_div[i] - bit_size;
        num_rounded_up                 = tctx->n_div[i] - num_rounded_down;
        tctx->bits_main_spec[0][i][0]  = (rounded_up + 1)   / 2;
        tctx->bits_main_spec[1][i][0]  =  rounded_up        / 2;
        tctx->bits_main_spec[0][i][1]  = (rounded_down + 1) / 2;
        tctx->bits_main_spec[1][i][1]  =  rounded_down      / 2;
        tctx->bits_main_spec_change[i] = num_rounded_up;

        rounded_up             = (vect_size + tctx->n_div[i] - 1) /
                                 tctx->n_div[i];
        rounded_down           = (vect_size) / tctx->n_div[i];
        num_rounded_down       = rounded_up * tctx->n_div[i] - vect_size;
        num_rounded_up         = tctx->n_div[i] - num_rounded_down;
        tctx->length[i][0]     = rounded_up;
        tctx->length[i][1]     = rounded_down;
        tctx->length_change[i] = num_rounded_up;
    }

    for (frametype = TWINVQ_FT_SHORT; frametype <= TWINVQ_FT_PPC; frametype++)
        construct_perm_table(tctx, frametype);
}

av_cold int ff_twinvq_decode_close(AVCodecContext *avctx)
{
    TwinVQContext *tctx = avctx->priv_data;
    int i;

    for (i = 0; i < 3; i++) {
        ff_mdct_end(&tctx->mdct_ctx[i]);
        av_freep(&tctx->cos_tabs[i]);
    }

    av_freep(&tctx->curr_frame);
    av_freep(&tctx->spectrum);
    av_freep(&tctx->prev_frame);
    av_freep(&tctx->tmp_buf);
    av_freep(&tctx->fdsp);

    return 0;
}

av_cold int ff_twinvq_decode_init(AVCodecContext *avctx)
{
    int ret;
    TwinVQContext *tctx = avctx->priv_data;
    int64_t frames_per_packet;

    tctx->avctx       = avctx;
    avctx->sample_fmt = AV_SAMPLE_FMT_FLTP;

    if (!avctx->block_align) {
        avctx->block_align = tctx->frame_size + 7 >> 3;
    }
    frames_per_packet = avctx->block_align * 8LL / tctx->frame_size;
    if (frames_per_packet <= 0) {
        av_log(avctx, AV_LOG_ERROR, "Block align is %"PRId64" bits, expected %d\n",
               avctx->block_align * (int64_t)8, tctx->frame_size);
        return AVERROR_INVALIDDATA;
    }
    if (frames_per_packet > TWINVQ_MAX_FRAMES_PER_PACKET) {
        av_log(avctx, AV_LOG_ERROR, "Too many frames per packet (%"PRId64")\n",
               frames_per_packet);
        return AVERROR_INVALIDDATA;
    }
    tctx->frames_per_packet = frames_per_packet;

    tctx->fdsp = avpriv_float_dsp_alloc(avctx->flags & AV_CODEC_FLAG_BITEXACT);
    if (!tctx->fdsp)
        return AVERROR(ENOMEM);
    if ((ret = init_mdct_win(tctx))) {
        av_log(avctx, AV_LOG_ERROR, "Error initializing MDCT\n");
        return ret;
    }
    init_bitstream_params(tctx);

    twinvq_memset_float(tctx->bark_hist[0][0], 0.1,
                        FF_ARRAY_ELEMS(tctx->bark_hist));

    return 0;
}
