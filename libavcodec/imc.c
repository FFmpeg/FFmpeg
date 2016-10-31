/*
 * IMC compatible decoder
 * Copyright (c) 2002-2004 Maxim Poliakovski
 * Copyright (c) 2006 Benjamin Larsson
 * Copyright (c) 2006 Konstantin Shishkov
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
 *  @file
 *  IMC - Intel Music Coder
 *  A mdct based codec using a 256 points large transform
 *  divided into 32 bands with some mix of scale factors.
 *  Only mono is supported.
 */


#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "libavutil/channel_layout.h"
#include "libavutil/ffmath.h"
#include "libavutil/float_dsp.h"
#include "libavutil/internal.h"
#include "avcodec.h"
#include "bswapdsp.h"
#include "get_bits.h"
#include "fft.h"
#include "internal.h"
#include "sinewin.h"

#include "imcdata.h"

#define IMC_BLOCK_SIZE 64
#define IMC_FRAME_ID 0x21
#define BANDS 32
#define COEFFS 256

typedef struct IMCChannel {
    float old_floor[BANDS];
    float flcoeffs1[BANDS];
    float flcoeffs2[BANDS];
    float flcoeffs3[BANDS];
    float flcoeffs4[BANDS];
    float flcoeffs5[BANDS];
    float flcoeffs6[BANDS];
    float CWdecoded[COEFFS];

    int bandWidthT[BANDS];     ///< codewords per band
    int bitsBandT[BANDS];      ///< how many bits per codeword in band
    int CWlengthT[COEFFS];     ///< how many bits in each codeword
    int levlCoeffBuf[BANDS];
    int bandFlagsBuf[BANDS];   ///< flags for each band
    int sumLenArr[BANDS];      ///< bits for all coeffs in band
    int skipFlagRaw[BANDS];    ///< skip flags are stored in raw form or not
    int skipFlagBits[BANDS];   ///< bits used to code skip flags
    int skipFlagCount[BANDS];  ///< skipped coefficients per band
    int skipFlags[COEFFS];     ///< skip coefficient decoding or not
    int codewords[COEFFS];     ///< raw codewords read from bitstream

    float last_fft_im[COEFFS];

    int decoder_reset;
} IMCChannel;

typedef struct IMCContext {
    IMCChannel chctx[2];

    /** MDCT tables */
    //@{
    float mdct_sine_window[COEFFS];
    float post_cos[COEFFS];
    float post_sin[COEFFS];
    float pre_coef1[COEFFS];
    float pre_coef2[COEFFS];
    //@}

    float sqrt_tab[30];
    GetBitContext gb;

    BswapDSPContext bdsp;
    AVFloatDSPContext *fdsp;
    FFTContext fft;
    DECLARE_ALIGNED(32, FFTComplex, samples)[COEFFS / 2];
    float *out_samples;

    int coef0_pos;

    int8_t cyclTab[32], cyclTab2[32];
    float  weights1[31], weights2[31];
} IMCContext;

static VLC huffman_vlc[4][4];

#define VLC_TABLES_SIZE 9512

static const int vlc_offsets[17] = {
    0,     640, 1156, 1732, 2308, 2852, 3396, 3924,
    4452, 5220, 5860, 6628, 7268, 7908, 8424, 8936, VLC_TABLES_SIZE
};

static VLC_TYPE vlc_tables[VLC_TABLES_SIZE][2];

static inline double freq2bark(double freq)
{
    return 3.5 * atan((freq / 7500.0) * (freq / 7500.0)) + 13.0 * atan(freq * 0.00076);
}

static av_cold void iac_generate_tabs(IMCContext *q, int sampling_rate)
{
    double freqmin[32], freqmid[32], freqmax[32];
    double scale = sampling_rate / (256.0 * 2.0 * 2.0);
    double nyquist_freq = sampling_rate * 0.5;
    double freq, bark, prev_bark = 0, tf, tb;
    int i, j;

    for (i = 0; i < 32; i++) {
        freq = (band_tab[i] + band_tab[i + 1] - 1) * scale;
        bark = freq2bark(freq);

        if (i > 0) {
            tb = bark - prev_bark;
            q->weights1[i - 1] = ff_exp10(-1.0 * tb);
            q->weights2[i - 1] = ff_exp10(-2.7 * tb);
        }
        prev_bark = bark;

        freqmid[i] = freq;

        tf = freq;
        while (tf < nyquist_freq) {
            tf += 0.5;
            tb =  freq2bark(tf);
            if (tb > bark + 0.5)
                break;
        }
        freqmax[i] = tf;

        tf = freq;
        while (tf > 0.0) {
            tf -= 0.5;
            tb =  freq2bark(tf);
            if (tb <= bark - 0.5)
                break;
        }
        freqmin[i] = tf;
    }

    for (i = 0; i < 32; i++) {
        freq = freqmax[i];
        for (j = 31; j > 0 && freq <= freqmid[j]; j--);
        q->cyclTab[i] = j + 1;

        freq = freqmin[i];
        for (j = 0; j < 32 && freq >= freqmid[j]; j++);
        q->cyclTab2[i] = j - 1;
    }
}

static av_cold int imc_decode_init(AVCodecContext *avctx)
{
    int i, j, ret;
    IMCContext *q = avctx->priv_data;
    double r1, r2;

    if (avctx->codec_id == AV_CODEC_ID_IAC && avctx->sample_rate > 96000) {
        av_log(avctx, AV_LOG_ERROR,
               "Strange sample rate of %i, file likely corrupt or "
               "needing a new table derivation method.\n",
               avctx->sample_rate);
        return AVERROR_PATCHWELCOME;
    }

    if (avctx->codec_id == AV_CODEC_ID_IMC)
        avctx->channels = 1;

    if (avctx->channels > 2) {
        avpriv_request_sample(avctx, "Number of channels > 2");
        return AVERROR_PATCHWELCOME;
    }

    for (j = 0; j < avctx->channels; j++) {
        q->chctx[j].decoder_reset = 1;

        for (i = 0; i < BANDS; i++)
            q->chctx[j].old_floor[i] = 1.0;

        for (i = 0; i < COEFFS / 2; i++)
            q->chctx[j].last_fft_im[i] = 0;
    }

    /* Build mdct window, a simple sine window normalized with sqrt(2) */
    ff_sine_window_init(q->mdct_sine_window, COEFFS);
    for (i = 0; i < COEFFS; i++)
        q->mdct_sine_window[i] *= sqrt(2.0);
    for (i = 0; i < COEFFS / 2; i++) {
        q->post_cos[i] = (1.0f / 32768) * cos(i / 256.0 * M_PI);
        q->post_sin[i] = (1.0f / 32768) * sin(i / 256.0 * M_PI);

        r1 = sin((i * 4.0 + 1.0) / 1024.0 * M_PI);
        r2 = cos((i * 4.0 + 1.0) / 1024.0 * M_PI);

        if (i & 0x1) {
            q->pre_coef1[i] =  (r1 + r2) * sqrt(2.0);
            q->pre_coef2[i] = -(r1 - r2) * sqrt(2.0);
        } else {
            q->pre_coef1[i] = -(r1 + r2) * sqrt(2.0);
            q->pre_coef2[i] =  (r1 - r2) * sqrt(2.0);
        }
    }

    /* Generate a square root table */

    for (i = 0; i < 30; i++)
        q->sqrt_tab[i] = sqrt(i);

    /* initialize the VLC tables */
    for (i = 0; i < 4 ; i++) {
        for (j = 0; j < 4; j++) {
            huffman_vlc[i][j].table = &vlc_tables[vlc_offsets[i * 4 + j]];
            huffman_vlc[i][j].table_allocated = vlc_offsets[i * 4 + j + 1] - vlc_offsets[i * 4 + j];
            init_vlc(&huffman_vlc[i][j], 9, imc_huffman_sizes[i],
                     imc_huffman_lens[i][j], 1, 1,
                     imc_huffman_bits[i][j], 2, 2, INIT_VLC_USE_NEW_STATIC);
        }
    }

    if (avctx->codec_id == AV_CODEC_ID_IAC) {
        iac_generate_tabs(q, avctx->sample_rate);
    } else {
        memcpy(q->cyclTab,  cyclTab,  sizeof(cyclTab));
        memcpy(q->cyclTab2, cyclTab2, sizeof(cyclTab2));
        memcpy(q->weights1, imc_weights1, sizeof(imc_weights1));
        memcpy(q->weights2, imc_weights2, sizeof(imc_weights2));
    }

    if ((ret = ff_fft_init(&q->fft, 7, 1))) {
        av_log(avctx, AV_LOG_INFO, "FFT init failed\n");
        return ret;
    }
    ff_bswapdsp_init(&q->bdsp);
    q->fdsp = avpriv_float_dsp_alloc(avctx->flags & AV_CODEC_FLAG_BITEXACT);
    if (!q->fdsp) {
        ff_fft_end(&q->fft);

        return AVERROR(ENOMEM);
    }

    avctx->sample_fmt     = AV_SAMPLE_FMT_FLTP;
    avctx->channel_layout = avctx->channels == 1 ? AV_CH_LAYOUT_MONO
                                                 : AV_CH_LAYOUT_STEREO;

    return 0;
}

static void imc_calculate_coeffs(IMCContext *q, float *flcoeffs1,
                                 float *flcoeffs2, int *bandWidthT,
                                 float *flcoeffs3, float *flcoeffs5)
{
    float   workT1[BANDS];
    float   workT2[BANDS];
    float   workT3[BANDS];
    float   snr_limit = 1.e-30;
    float   accum = 0.0;
    int i, cnt2;

    for (i = 0; i < BANDS; i++) {
        flcoeffs5[i] = workT2[i] = 0.0;
        if (bandWidthT[i]) {
            workT1[i] = flcoeffs1[i] * flcoeffs1[i];
            flcoeffs3[i] = 2.0 * flcoeffs2[i];
        } else {
            workT1[i]    = 0.0;
            flcoeffs3[i] = -30000.0;
        }
        workT3[i] = bandWidthT[i] * workT1[i] * 0.01;
        if (workT3[i] <= snr_limit)
            workT3[i] = 0.0;
    }

    for (i = 0; i < BANDS; i++) {
        for (cnt2 = i; cnt2 < q->cyclTab[i]; cnt2++)
            flcoeffs5[cnt2] = flcoeffs5[cnt2] + workT3[i];
        workT2[cnt2 - 1] = workT2[cnt2 - 1] + workT3[i];
    }

    for (i = 1; i < BANDS; i++) {
        accum = (workT2[i - 1] + accum) * q->weights1[i - 1];
        flcoeffs5[i] += accum;
    }

    for (i = 0; i < BANDS; i++)
        workT2[i] = 0.0;

    for (i = 0; i < BANDS; i++) {
        for (cnt2 = i - 1; cnt2 > q->cyclTab2[i]; cnt2--)
            flcoeffs5[cnt2] += workT3[i];
        workT2[cnt2+1] += workT3[i];
    }

    accum = 0.0;

    for (i = BANDS-2; i >= 0; i--) {
        accum = (workT2[i+1] + accum) * q->weights2[i];
        flcoeffs5[i] += accum;
        // there is missing code here, but it seems to never be triggered
    }
}


static void imc_read_level_coeffs(IMCContext *q, int stream_format_code,
                                  int *levlCoeffs)
{
    int i;
    VLC *hufftab[4];
    int start = 0;
    const uint8_t *cb_sel;
    int s;

    s = stream_format_code >> 1;
    hufftab[0] = &huffman_vlc[s][0];
    hufftab[1] = &huffman_vlc[s][1];
    hufftab[2] = &huffman_vlc[s][2];
    hufftab[3] = &huffman_vlc[s][3];
    cb_sel = imc_cb_select[s];

    if (stream_format_code & 4)
        start = 1;
    if (start)
        levlCoeffs[0] = get_bits(&q->gb, 7);
    for (i = start; i < BANDS; i++) {
        levlCoeffs[i] = get_vlc2(&q->gb, hufftab[cb_sel[i]]->table,
                                 hufftab[cb_sel[i]]->bits, 2);
        if (levlCoeffs[i] == 17)
            levlCoeffs[i] += get_bits(&q->gb, 4);
    }
}

static void imc_read_level_coeffs_raw(IMCContext *q, int stream_format_code,
                                      int *levlCoeffs)
{
    int i;

    q->coef0_pos  = get_bits(&q->gb, 5);
    levlCoeffs[0] = get_bits(&q->gb, 7);
    for (i = 1; i < BANDS; i++)
        levlCoeffs[i] = get_bits(&q->gb, 4);
}

static void imc_decode_level_coefficients(IMCContext *q, int *levlCoeffBuf,
                                          float *flcoeffs1, float *flcoeffs2)
{
    int i, level;
    float tmp, tmp2;
    // maybe some frequency division thingy

    flcoeffs1[0] = 20000.0 / exp2 (levlCoeffBuf[0] * 0.18945); // 0.18945 = log2(10) * 0.05703125
    flcoeffs2[0] = log2f(flcoeffs1[0]);
    tmp  = flcoeffs1[0];
    tmp2 = flcoeffs2[0];

    for (i = 1; i < BANDS; i++) {
        level = levlCoeffBuf[i];
        if (level == 16) {
            flcoeffs1[i] = 1.0;
            flcoeffs2[i] = 0.0;
        } else {
            if (level < 17)
                level -= 7;
            else if (level <= 24)
                level -= 32;
            else
                level -= 16;

            tmp  *= imc_exp_tab[15 + level];
            tmp2 += 0.83048 * level;  // 0.83048 = log2(10) * 0.25
            flcoeffs1[i] = tmp;
            flcoeffs2[i] = tmp2;
        }
    }
}


static void imc_decode_level_coefficients2(IMCContext *q, int *levlCoeffBuf,
                                           float *old_floor, float *flcoeffs1,
                                           float *flcoeffs2)
{
    int i;
    /* FIXME maybe flag_buf = noise coding and flcoeffs1 = new scale factors
     *       and flcoeffs2 old scale factors
     *       might be incomplete due to a missing table that is in the binary code
     */
    for (i = 0; i < BANDS; i++) {
        flcoeffs1[i] = 0;
        if (levlCoeffBuf[i] < 16) {
            flcoeffs1[i] = imc_exp_tab2[levlCoeffBuf[i]] * old_floor[i];
            flcoeffs2[i] = (levlCoeffBuf[i] - 7) * 0.83048 + flcoeffs2[i]; // 0.83048 = log2(10) * 0.25
        } else {
            flcoeffs1[i] = old_floor[i];
        }
    }
}

static void imc_decode_level_coefficients_raw(IMCContext *q, int *levlCoeffBuf,
                                              float *flcoeffs1, float *flcoeffs2)
{
    int i, level, pos;
    float tmp, tmp2;

    pos = q->coef0_pos;
    flcoeffs1[pos] = 20000.0 / pow (2, levlCoeffBuf[0] * 0.18945); // 0.18945 = log2(10) * 0.05703125
    flcoeffs2[pos] = log2f(flcoeffs1[pos]);
    tmp  = flcoeffs1[pos];
    tmp2 = flcoeffs2[pos];

    levlCoeffBuf++;
    for (i = 0; i < BANDS; i++) {
        if (i == pos)
            continue;
        level = *levlCoeffBuf++;
        flcoeffs1[i] = tmp  * powf(10.0, -level * 0.4375); //todo tab
        flcoeffs2[i] = tmp2 - 1.4533435415 * level; // 1.4533435415 = log2(10) * 0.4375
    }
}

/**
 * Perform bit allocation depending on bits available
 */
static int bit_allocation(IMCContext *q, IMCChannel *chctx,
                          int stream_format_code, int freebits, int flag)
{
    int i, j;
    const float limit = -1.e20;
    float highest = 0.0;
    int indx;
    int t1 = 0;
    int t2 = 1;
    float summa = 0.0;
    int iacc = 0;
    int summer = 0;
    int rres, cwlen;
    float lowest = 1.e10;
    int low_indx = 0;
    float workT[32];
    int flg;
    int found_indx = 0;

    for (i = 0; i < BANDS; i++)
        highest = FFMAX(highest, chctx->flcoeffs1[i]);

    for (i = 0; i < BANDS - 1; i++) {
        if (chctx->flcoeffs5[i] <= 0) {
            av_log(NULL, AV_LOG_ERROR, "flcoeffs5 %f invalid\n", chctx->flcoeffs5[i]);
            return AVERROR_INVALIDDATA;
        }
        chctx->flcoeffs4[i] = chctx->flcoeffs3[i] - log2f(chctx->flcoeffs5[i]);
    }
    chctx->flcoeffs4[BANDS - 1] = limit;

    highest = highest * 0.25;

    for (i = 0; i < BANDS; i++) {
        indx = -1;
        if ((band_tab[i + 1] - band_tab[i]) == chctx->bandWidthT[i])
            indx = 0;

        if ((band_tab[i + 1] - band_tab[i]) > chctx->bandWidthT[i])
            indx = 1;

        if (((band_tab[i + 1] - band_tab[i]) / 2) >= chctx->bandWidthT[i])
            indx = 2;

        if (indx == -1)
            return AVERROR_INVALIDDATA;

        chctx->flcoeffs4[i] += xTab[(indx * 2 + (chctx->flcoeffs1[i] < highest)) * 2 + flag];
    }

    if (stream_format_code & 0x2) {
        chctx->flcoeffs4[0] = limit;
        chctx->flcoeffs4[1] = limit;
        chctx->flcoeffs4[2] = limit;
        chctx->flcoeffs4[3] = limit;
    }

    for (i = (stream_format_code & 0x2) ? 4 : 0; i < BANDS - 1; i++) {
        iacc  += chctx->bandWidthT[i];
        summa += chctx->bandWidthT[i] * chctx->flcoeffs4[i];
    }

    if (!iacc)
        return AVERROR_INVALIDDATA;

    chctx->bandWidthT[BANDS - 1] = 0;
    summa = (summa * 0.5 - freebits) / iacc;


    for (i = 0; i < BANDS / 2; i++) {
        rres = summer - freebits;
        if ((rres >= -8) && (rres <= 8))
            break;

        summer = 0;
        iacc   = 0;

        for (j = (stream_format_code & 0x2) ? 4 : 0; j < BANDS; j++) {
            cwlen = av_clipf(((chctx->flcoeffs4[j] * 0.5) - summa + 0.5), 0, 6);

            chctx->bitsBandT[j] = cwlen;
            summer += chctx->bandWidthT[j] * cwlen;

            if (cwlen > 0)
                iacc += chctx->bandWidthT[j];
        }

        flg = t2;
        t2 = 1;
        if (freebits < summer)
            t2 = -1;
        if (i == 0)
            flg = t2;
        if (flg != t2)
            t1++;

        summa = (float)(summer - freebits) / ((t1 + 1) * iacc) + summa;
    }

    for (i = (stream_format_code & 0x2) ? 4 : 0; i < BANDS; i++) {
        for (j = band_tab[i]; j < band_tab[i + 1]; j++)
            chctx->CWlengthT[j] = chctx->bitsBandT[i];
    }

    if (freebits > summer) {
        for (i = 0; i < BANDS; i++) {
            workT[i] = (chctx->bitsBandT[i] == 6) ? -1.e20
                                              : (chctx->bitsBandT[i] * -2 + chctx->flcoeffs4[i] - 0.415);
        }

        highest = 0.0;

        do {
            if (highest <= -1.e20)
                break;

            found_indx = 0;
            highest = -1.e20;

            for (i = 0; i < BANDS; i++) {
                if (workT[i] > highest) {
                    highest = workT[i];
                    found_indx = i;
                }
            }

            if (highest > -1.e20) {
                workT[found_indx] -= 2.0;
                if (++chctx->bitsBandT[found_indx] == 6)
                    workT[found_indx] = -1.e20;

                for (j = band_tab[found_indx]; j < band_tab[found_indx + 1] && (freebits > summer); j++) {
                    chctx->CWlengthT[j]++;
                    summer++;
                }
            }
        } while (freebits > summer);
    }
    if (freebits < summer) {
        for (i = 0; i < BANDS; i++) {
            workT[i] = chctx->bitsBandT[i] ? (chctx->bitsBandT[i] * -2 + chctx->flcoeffs4[i] + 1.585)
                                       : 1.e20;
        }
        if (stream_format_code & 0x2) {
            workT[0] = 1.e20;
            workT[1] = 1.e20;
            workT[2] = 1.e20;
            workT[3] = 1.e20;
        }
        while (freebits < summer) {
            lowest   = 1.e10;
            low_indx = 0;
            for (i = 0; i < BANDS; i++) {
                if (workT[i] < lowest) {
                    lowest   = workT[i];
                    low_indx = i;
                }
            }
            // if (lowest >= 1.e10)
            //     break;
            workT[low_indx] = lowest + 2.0;

            if (!--chctx->bitsBandT[low_indx])
                workT[low_indx] = 1.e20;

            for (j = band_tab[low_indx]; j < band_tab[low_indx+1] && (freebits < summer); j++) {
                if (chctx->CWlengthT[j] > 0) {
                    chctx->CWlengthT[j]--;
                    summer--;
                }
            }
        }
    }
    return 0;
}

static void imc_get_skip_coeff(IMCContext *q, IMCChannel *chctx)
{
    int i, j;

    memset(chctx->skipFlagBits,  0, sizeof(chctx->skipFlagBits));
    memset(chctx->skipFlagCount, 0, sizeof(chctx->skipFlagCount));
    for (i = 0; i < BANDS; i++) {
        if (!chctx->bandFlagsBuf[i] || !chctx->bandWidthT[i])
            continue;

        if (!chctx->skipFlagRaw[i]) {
            chctx->skipFlagBits[i] = band_tab[i + 1] - band_tab[i];

            for (j = band_tab[i]; j < band_tab[i + 1]; j++) {
                chctx->skipFlags[j] = get_bits1(&q->gb);
                if (chctx->skipFlags[j])
                    chctx->skipFlagCount[i]++;
            }
        } else {
            for (j = band_tab[i]; j < band_tab[i + 1] - 1; j += 2) {
                if (!get_bits1(&q->gb)) { // 0
                    chctx->skipFlagBits[i]++;
                    chctx->skipFlags[j]      = 1;
                    chctx->skipFlags[j + 1]  = 1;
                    chctx->skipFlagCount[i] += 2;
                } else {
                    if (get_bits1(&q->gb)) { // 11
                        chctx->skipFlagBits[i] += 2;
                        chctx->skipFlags[j]     = 0;
                        chctx->skipFlags[j + 1] = 1;
                        chctx->skipFlagCount[i]++;
                    } else {
                        chctx->skipFlagBits[i] += 3;
                        chctx->skipFlags[j + 1] = 0;
                        if (!get_bits1(&q->gb)) { // 100
                            chctx->skipFlags[j] = 1;
                            chctx->skipFlagCount[i]++;
                        } else { // 101
                            chctx->skipFlags[j] = 0;
                        }
                    }
                }
            }

            if (j < band_tab[i + 1]) {
                chctx->skipFlagBits[i]++;
                if ((chctx->skipFlags[j] = get_bits1(&q->gb)))
                    chctx->skipFlagCount[i]++;
            }
        }
    }
}

/**
 * Increase highest' band coefficient sizes as some bits won't be used
 */
static void imc_adjust_bit_allocation(IMCContext *q, IMCChannel *chctx,
                                      int summer)
{
    float workT[32];
    int corrected = 0;
    int i, j;
    float highest  = 0;
    int found_indx = 0;

    for (i = 0; i < BANDS; i++) {
        workT[i] = (chctx->bitsBandT[i] == 6) ? -1.e20
                                          : (chctx->bitsBandT[i] * -2 + chctx->flcoeffs4[i] - 0.415);
    }

    while (corrected < summer) {
        if (highest <= -1.e20)
            break;

        highest = -1.e20;

        for (i = 0; i < BANDS; i++) {
            if (workT[i] > highest) {
                highest = workT[i];
                found_indx = i;
            }
        }

        if (highest > -1.e20) {
            workT[found_indx] -= 2.0;
            if (++(chctx->bitsBandT[found_indx]) == 6)
                workT[found_indx] = -1.e20;

            for (j = band_tab[found_indx]; j < band_tab[found_indx+1] && (corrected < summer); j++) {
                if (!chctx->skipFlags[j] && (chctx->CWlengthT[j] < 6)) {
                    chctx->CWlengthT[j]++;
                    corrected++;
                }
            }
        }
    }
}

static void imc_imdct256(IMCContext *q, IMCChannel *chctx, int channels)
{
    int i;
    float re, im;
    float *dst1 = q->out_samples;
    float *dst2 = q->out_samples + (COEFFS - 1);

    /* prerotation */
    for (i = 0; i < COEFFS / 2; i++) {
        q->samples[i].re = -(q->pre_coef1[i] * chctx->CWdecoded[COEFFS - 1 - i * 2]) -
                            (q->pre_coef2[i] * chctx->CWdecoded[i * 2]);
        q->samples[i].im =  (q->pre_coef2[i] * chctx->CWdecoded[COEFFS - 1 - i * 2]) -
                            (q->pre_coef1[i] * chctx->CWdecoded[i * 2]);
    }

    /* FFT */
    q->fft.fft_permute(&q->fft, q->samples);
    q->fft.fft_calc(&q->fft, q->samples);

    /* postrotation, window and reorder */
    for (i = 0; i < COEFFS / 2; i++) {
        re = ( q->samples[i].re * q->post_cos[i]) + (-q->samples[i].im * q->post_sin[i]);
        im = (-q->samples[i].im * q->post_cos[i]) - ( q->samples[i].re * q->post_sin[i]);
        *dst1 =  (q->mdct_sine_window[COEFFS - 1 - i * 2] * chctx->last_fft_im[i])
               + (q->mdct_sine_window[i * 2] * re);
        *dst2 =  (q->mdct_sine_window[i * 2] * chctx->last_fft_im[i])
               - (q->mdct_sine_window[COEFFS - 1 - i * 2] * re);
        dst1 += 2;
        dst2 -= 2;
        chctx->last_fft_im[i] = im;
    }
}

static int inverse_quant_coeff(IMCContext *q, IMCChannel *chctx,
                               int stream_format_code)
{
    int i, j;
    int middle_value, cw_len, max_size;
    const float *quantizer;

    for (i = 0; i < BANDS; i++) {
        for (j = band_tab[i]; j < band_tab[i + 1]; j++) {
            chctx->CWdecoded[j] = 0;
            cw_len = chctx->CWlengthT[j];

            if (cw_len <= 0 || chctx->skipFlags[j])
                continue;

            max_size     = 1 << cw_len;
            middle_value = max_size >> 1;

            if (chctx->codewords[j] >= max_size || chctx->codewords[j] < 0)
                return AVERROR_INVALIDDATA;

            if (cw_len >= 4) {
                quantizer = imc_quantizer2[(stream_format_code & 2) >> 1];
                if (chctx->codewords[j] >= middle_value)
                    chctx->CWdecoded[j] =  quantizer[chctx->codewords[j] - 8]                * chctx->flcoeffs6[i];
                else
                    chctx->CWdecoded[j] = -quantizer[max_size - chctx->codewords[j] - 8 - 1] * chctx->flcoeffs6[i];
            }else{
                quantizer = imc_quantizer1[((stream_format_code & 2) >> 1) | (chctx->bandFlagsBuf[i] << 1)];
                if (chctx->codewords[j] >= middle_value)
                    chctx->CWdecoded[j] =  quantizer[chctx->codewords[j] - 1]            * chctx->flcoeffs6[i];
                else
                    chctx->CWdecoded[j] = -quantizer[max_size - 2 - chctx->codewords[j]] * chctx->flcoeffs6[i];
            }
        }
    }
    return 0;
}


static void imc_get_coeffs(AVCodecContext *avctx,
                           IMCContext *q, IMCChannel *chctx)
{
    int i, j, cw_len, cw;

    for (i = 0; i < BANDS; i++) {
        if (!chctx->sumLenArr[i])
            continue;
        if (chctx->bandFlagsBuf[i] || chctx->bandWidthT[i]) {
            for (j = band_tab[i]; j < band_tab[i + 1]; j++) {
                cw_len = chctx->CWlengthT[j];
                cw = 0;

                if (cw_len && (!chctx->bandFlagsBuf[i] || !chctx->skipFlags[j])) {
                    if (get_bits_count(&q->gb) + cw_len > 512) {
                        av_log(avctx, AV_LOG_WARNING,
                            "Potential problem on band %i, coefficient %i"
                            ": cw_len=%i\n", i, j, cw_len);
                    } else
                        cw = get_bits(&q->gb, cw_len);
                }

                chctx->codewords[j] = cw;
            }
        }
    }
}

static void imc_refine_bit_allocation(IMCContext *q, IMCChannel *chctx)
{
    int i, j;
    int bits, summer;

    for (i = 0; i < BANDS; i++) {
        chctx->sumLenArr[i]   = 0;
        chctx->skipFlagRaw[i] = 0;
        for (j = band_tab[i]; j < band_tab[i + 1]; j++)
            chctx->sumLenArr[i] += chctx->CWlengthT[j];
        if (chctx->bandFlagsBuf[i])
            if ((((band_tab[i + 1] - band_tab[i]) * 1.5) > chctx->sumLenArr[i]) && (chctx->sumLenArr[i] > 0))
                chctx->skipFlagRaw[i] = 1;
    }

    imc_get_skip_coeff(q, chctx);

    for (i = 0; i < BANDS; i++) {
        chctx->flcoeffs6[i] = chctx->flcoeffs1[i];
        /* band has flag set and at least one coded coefficient */
        if (chctx->bandFlagsBuf[i] && (band_tab[i + 1] - band_tab[i]) != chctx->skipFlagCount[i]) {
            chctx->flcoeffs6[i] *= q->sqrt_tab[ band_tab[i + 1] - band_tab[i]] /
                                   q->sqrt_tab[(band_tab[i + 1] - band_tab[i] - chctx->skipFlagCount[i])];
        }
    }

    /* calculate bits left, bits needed and adjust bit allocation */
    bits = summer = 0;

    for (i = 0; i < BANDS; i++) {
        if (chctx->bandFlagsBuf[i]) {
            for (j = band_tab[i]; j < band_tab[i + 1]; j++) {
                if (chctx->skipFlags[j]) {
                    summer += chctx->CWlengthT[j];
                    chctx->CWlengthT[j] = 0;
                }
            }
            bits   += chctx->skipFlagBits[i];
            summer -= chctx->skipFlagBits[i];
        }
    }
    imc_adjust_bit_allocation(q, chctx, summer);
}

static int imc_decode_block(AVCodecContext *avctx, IMCContext *q, int ch)
{
    int stream_format_code;
    int imc_hdr, i, j, ret;
    int flag;
    int bits;
    int counter, bitscount;
    IMCChannel *chctx = q->chctx + ch;


    /* Check the frame header */
    imc_hdr = get_bits(&q->gb, 9);
    if (imc_hdr & 0x18) {
        av_log(avctx, AV_LOG_ERROR, "frame header check failed!\n");
        av_log(avctx, AV_LOG_ERROR, "got %X.\n", imc_hdr);
        return AVERROR_INVALIDDATA;
    }
    stream_format_code = get_bits(&q->gb, 3);

    if (stream_format_code & 0x04)
        chctx->decoder_reset = 1;

    if (chctx->decoder_reset) {
        for (i = 0; i < BANDS; i++)
            chctx->old_floor[i] = 1.0;
        for (i = 0; i < COEFFS; i++)
            chctx->CWdecoded[i] = 0;
        chctx->decoder_reset = 0;
    }

    flag = get_bits1(&q->gb);
    if (stream_format_code & 0x1)
        imc_read_level_coeffs_raw(q, stream_format_code, chctx->levlCoeffBuf);
    else
        imc_read_level_coeffs(q, stream_format_code, chctx->levlCoeffBuf);

    if (stream_format_code & 0x1)
        imc_decode_level_coefficients_raw(q, chctx->levlCoeffBuf,
                                          chctx->flcoeffs1, chctx->flcoeffs2);
    else if (stream_format_code & 0x4)
        imc_decode_level_coefficients(q, chctx->levlCoeffBuf,
                                      chctx->flcoeffs1, chctx->flcoeffs2);
    else
        imc_decode_level_coefficients2(q, chctx->levlCoeffBuf, chctx->old_floor,
                                       chctx->flcoeffs1, chctx->flcoeffs2);

    for(i=0; i<BANDS; i++) {
        if(chctx->flcoeffs1[i] > INT_MAX) {
            av_log(avctx, AV_LOG_ERROR, "scalefactor out of range\n");
            return AVERROR_INVALIDDATA;
        }
    }

    memcpy(chctx->old_floor, chctx->flcoeffs1, 32 * sizeof(float));

    counter = 0;
    if (stream_format_code & 0x1) {
        for (i = 0; i < BANDS; i++) {
            chctx->bandWidthT[i]   = band_tab[i + 1] - band_tab[i];
            chctx->bandFlagsBuf[i] = 0;
            chctx->flcoeffs3[i]    = chctx->flcoeffs2[i] * 2;
            chctx->flcoeffs5[i]    = 1.0;
        }
    } else {
        for (i = 0; i < BANDS; i++) {
            if (chctx->levlCoeffBuf[i] == 16) {
                chctx->bandWidthT[i] = 0;
                counter++;
            } else
                chctx->bandWidthT[i] = band_tab[i + 1] - band_tab[i];
        }

        memset(chctx->bandFlagsBuf, 0, BANDS * sizeof(int));
        for (i = 0; i < BANDS - 1; i++)
            if (chctx->bandWidthT[i])
                chctx->bandFlagsBuf[i] = get_bits1(&q->gb);

        imc_calculate_coeffs(q, chctx->flcoeffs1, chctx->flcoeffs2,
                             chctx->bandWidthT, chctx->flcoeffs3,
                             chctx->flcoeffs5);
    }

    bitscount = 0;
    /* first 4 bands will be assigned 5 bits per coefficient */
    if (stream_format_code & 0x2) {
        bitscount += 15;

        chctx->bitsBandT[0] = 5;
        chctx->CWlengthT[0] = 5;
        chctx->CWlengthT[1] = 5;
        chctx->CWlengthT[2] = 5;
        for (i = 1; i < 4; i++) {
            if (stream_format_code & 0x1)
                bits = 5;
            else
                bits = (chctx->levlCoeffBuf[i] == 16) ? 0 : 5;
            chctx->bitsBandT[i] = bits;
            for (j = band_tab[i]; j < band_tab[i + 1]; j++) {
                chctx->CWlengthT[j] = bits;
                bitscount      += bits;
            }
        }
    }
    if (avctx->codec_id == AV_CODEC_ID_IAC) {
        bitscount += !!chctx->bandWidthT[BANDS - 1];
        if (!(stream_format_code & 0x2))
            bitscount += 16;
    }

    if ((ret = bit_allocation(q, chctx, stream_format_code,
                              512 - bitscount - get_bits_count(&q->gb),
                              flag)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Bit allocations failed\n");
        chctx->decoder_reset = 1;
        return ret;
    }

    if (stream_format_code & 0x1) {
        for (i = 0; i < BANDS; i++)
            chctx->skipFlags[i] = 0;
    } else {
        imc_refine_bit_allocation(q, chctx);
    }

    for (i = 0; i < BANDS; i++) {
        chctx->sumLenArr[i] = 0;

        for (j = band_tab[i]; j < band_tab[i + 1]; j++)
            if (!chctx->skipFlags[j])
                chctx->sumLenArr[i] += chctx->CWlengthT[j];
    }

    memset(chctx->codewords, 0, sizeof(chctx->codewords));

    imc_get_coeffs(avctx, q, chctx);

    if (inverse_quant_coeff(q, chctx, stream_format_code) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Inverse quantization of coefficients failed\n");
        chctx->decoder_reset = 1;
        return AVERROR_INVALIDDATA;
    }

    memset(chctx->skipFlags, 0, sizeof(chctx->skipFlags));

    imc_imdct256(q, chctx, avctx->channels);

    return 0;
}

static int imc_decode_frame(AVCodecContext *avctx, void *data,
                            int *got_frame_ptr, AVPacket *avpkt)
{
    AVFrame *frame     = data;
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    int ret, i;

    IMCContext *q = avctx->priv_data;

    LOCAL_ALIGNED_16(uint16_t, buf16, [(IMC_BLOCK_SIZE + AV_INPUT_BUFFER_PADDING_SIZE) / 2]);

    if (buf_size < IMC_BLOCK_SIZE * avctx->channels) {
        av_log(avctx, AV_LOG_ERROR, "frame too small!\n");
        return AVERROR_INVALIDDATA;
    }

    /* get output buffer */
    frame->nb_samples = COEFFS;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    for (i = 0; i < avctx->channels; i++) {
        q->out_samples = (float *)frame->extended_data[i];

        q->bdsp.bswap16_buf(buf16, (const uint16_t *) buf, IMC_BLOCK_SIZE / 2);

        init_get_bits(&q->gb, (const uint8_t*)buf16, IMC_BLOCK_SIZE * 8);

        buf += IMC_BLOCK_SIZE;

        if ((ret = imc_decode_block(avctx, q, i)) < 0)
            return ret;
    }

    if (avctx->channels == 2) {
        q->fdsp->butterflies_float((float *)frame->extended_data[0],
                                  (float *)frame->extended_data[1], COEFFS);
    }

    *got_frame_ptr = 1;

    return IMC_BLOCK_SIZE * avctx->channels;
}

static av_cold int imc_decode_close(AVCodecContext * avctx)
{
    IMCContext *q = avctx->priv_data;

    ff_fft_end(&q->fft);
    av_freep(&q->fdsp);

    return 0;
}

static av_cold void flush(AVCodecContext *avctx)
{
    IMCContext *q = avctx->priv_data;

    q->chctx[0].decoder_reset =
    q->chctx[1].decoder_reset = 1;
}

#if CONFIG_IMC_DECODER
AVCodec ff_imc_decoder = {
    .name           = "imc",
    .long_name      = NULL_IF_CONFIG_SMALL("IMC (Intel Music Coder)"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_IMC,
    .priv_data_size = sizeof(IMCContext),
    .init           = imc_decode_init,
    .close          = imc_decode_close,
    .decode         = imc_decode_frame,
    .flush          = flush,
    .capabilities   = AV_CODEC_CAP_DR1,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_FLTP,
                                                      AV_SAMPLE_FMT_NONE },
};
#endif
#if CONFIG_IAC_DECODER
AVCodec ff_iac_decoder = {
    .name           = "iac",
    .long_name      = NULL_IF_CONFIG_SMALL("IAC (Indeo Audio Coder)"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_IAC,
    .priv_data_size = sizeof(IMCContext),
    .init           = imc_decode_init,
    .close          = imc_decode_close,
    .decode         = imc_decode_frame,
    .flush          = flush,
    .capabilities   = AV_CODEC_CAP_DR1,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_FLTP,
                                                      AV_SAMPLE_FMT_NONE },
};
#endif
