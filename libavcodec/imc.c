/*
 * IMC compatible decoder
 * Copyright (c) 2002-2004 Maxim Poliakovski
 * Copyright (c) 2006 Benjamin Larsson
 * Copyright (c) 2006 Konstantin Shishkov
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 *  @file
 *  IMC - Intel Music Coder
 *  A mdct based codec using a 256 points large transform
 *  divied into 32 bands with some mix of scale factors.
 *  Only mono is supported.
 *
 */


#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "avcodec.h"
#include "get_bits.h"
#include "dsputil.h"
#include "fft.h"
#include "libavutil/audioconvert.h"
#include "sinewin.h"

#include "imcdata.h"

#define IMC_BLOCK_SIZE 64
#define IMC_FRAME_ID 0x21
#define BANDS 32
#define COEFFS 256

typedef struct {
    AVFrame frame;

    float old_floor[BANDS];
    float flcoeffs1[BANDS];
    float flcoeffs2[BANDS];
    float flcoeffs3[BANDS];
    float flcoeffs4[BANDS];
    float flcoeffs5[BANDS];
    float flcoeffs6[BANDS];
    float CWdecoded[COEFFS];

    /** MDCT tables */
    //@{
    float mdct_sine_window[COEFFS];
    float post_cos[COEFFS];
    float post_sin[COEFFS];
    float pre_coef1[COEFFS];
    float pre_coef2[COEFFS];
    float last_fft_im[COEFFS];
    //@}

    int bandWidthT[BANDS];     ///< codewords per band
    int bitsBandT[BANDS];      ///< how many bits per codeword in band
    int CWlengthT[COEFFS];     ///< how many bits in each codeword
    int levlCoeffBuf[BANDS];
    int bandFlagsBuf[BANDS];   ///< flags for each band
    int sumLenArr[BANDS];      ///< bits for all coeffs in band
    int skipFlagRaw[BANDS];    ///< skip flags are stored in raw form or not
    int skipFlagBits[BANDS];   ///< bits used to code skip flags
    int skipFlagCount[BANDS];  ///< skipped coeffients per band
    int skipFlags[COEFFS];     ///< skip coefficient decoding or not
    int codewords[COEFFS];     ///< raw codewords read from bitstream
    float sqrt_tab[30];
    GetBitContext gb;
    int decoder_reset;
    float one_div_log2;

    DSPContext dsp;
    FFTContext fft;
    DECLARE_ALIGNED(32, FFTComplex, samples)[COEFFS/2];
    float *out_samples;
} IMCContext;

static VLC huffman_vlc[4][4];

#define VLC_TABLES_SIZE 9512

static const int vlc_offsets[17] = {
    0,     640, 1156, 1732, 2308, 2852, 3396, 3924,
    4452, 5220, 5860, 6628, 7268, 7908, 8424, 8936, VLC_TABLES_SIZE};

static VLC_TYPE vlc_tables[VLC_TABLES_SIZE][2];

static av_cold int imc_decode_init(AVCodecContext * avctx)
{
    int i, j, ret;
    IMCContext *q = avctx->priv_data;
    double r1, r2;

    if (avctx->channels != 1) {
        av_log_ask_for_sample(avctx, "Number of channels is not supported\n");
        return AVERROR_PATCHWELCOME;
    }

    q->decoder_reset = 1;

    for(i = 0; i < BANDS; i++)
        q->old_floor[i] = 1.0;

    /* Build mdct window, a simple sine window normalized with sqrt(2) */
    ff_sine_window_init(q->mdct_sine_window, COEFFS);
    for(i = 0; i < COEFFS; i++)
        q->mdct_sine_window[i] *= sqrt(2.0);
    for(i = 0; i < COEFFS/2; i++){
        q->post_cos[i] = (1.0f / 32768) * cos(i / 256.0 * M_PI);
        q->post_sin[i] = (1.0f / 32768) * sin(i / 256.0 * M_PI);

        r1 = sin((i * 4.0 + 1.0) / 1024.0 * M_PI);
        r2 = cos((i * 4.0 + 1.0) / 1024.0 * M_PI);

        if (i & 0x1)
        {
            q->pre_coef1[i] =  (r1 + r2) * sqrt(2.0);
            q->pre_coef2[i] = -(r1 - r2) * sqrt(2.0);
        }
        else
        {
            q->pre_coef1[i] = -(r1 + r2) * sqrt(2.0);
            q->pre_coef2[i] =  (r1 - r2) * sqrt(2.0);
        }

        q->last_fft_im[i] = 0;
    }

    /* Generate a square root table */

    for(i = 0; i < 30; i++) {
        q->sqrt_tab[i] = sqrt(i);
    }

    /* initialize the VLC tables */
    for(i = 0; i < 4 ; i++) {
        for(j = 0; j < 4; j++) {
            huffman_vlc[i][j].table = &vlc_tables[vlc_offsets[i * 4 + j]];
            huffman_vlc[i][j].table_allocated = vlc_offsets[i * 4 + j + 1] - vlc_offsets[i * 4 + j];
            init_vlc(&huffman_vlc[i][j], 9, imc_huffman_sizes[i],
                     imc_huffman_lens[i][j], 1, 1,
                     imc_huffman_bits[i][j], 2, 2, INIT_VLC_USE_NEW_STATIC);
        }
    }
    q->one_div_log2 = 1/log(2);

    if ((ret = ff_fft_init(&q->fft, 7, 1))) {
        av_log(avctx, AV_LOG_INFO, "FFT init failed\n");
        return ret;
    }
    dsputil_init(&q->dsp, avctx);
    avctx->sample_fmt = AV_SAMPLE_FMT_FLT;
    avctx->channel_layout = AV_CH_LAYOUT_MONO;

    avcodec_get_frame_defaults(&q->frame);
    avctx->coded_frame = &q->frame;

    return 0;
}

static void imc_calculate_coeffs(IMCContext* q, float* flcoeffs1, float* flcoeffs2, int* bandWidthT,
                                float* flcoeffs3, float* flcoeffs5)
{
    float   workT1[BANDS];
    float   workT2[BANDS];
    float   workT3[BANDS];
    float   snr_limit = 1.e-30;
    float   accum = 0.0;
    int i, cnt2;

    for(i = 0; i < BANDS; i++) {
        flcoeffs5[i] = workT2[i] = 0.0;
        if (bandWidthT[i]){
            workT1[i] = flcoeffs1[i] * flcoeffs1[i];
            flcoeffs3[i] = 2.0 * flcoeffs2[i];
        } else {
            workT1[i] = 0.0;
            flcoeffs3[i] = -30000.0;
        }
        workT3[i] = bandWidthT[i] * workT1[i] * 0.01;
        if (workT3[i] <= snr_limit)
            workT3[i] = 0.0;
    }

    for(i = 0; i < BANDS; i++) {
        for(cnt2 = i; cnt2 < cyclTab[i]; cnt2++)
            flcoeffs5[cnt2] = flcoeffs5[cnt2] + workT3[i];
        workT2[cnt2-1] = workT2[cnt2-1] + workT3[i];
    }

    for(i = 1; i < BANDS; i++) {
        accum = (workT2[i-1] + accum) * imc_weights1[i-1];
        flcoeffs5[i] += accum;
    }

    for(i = 0; i < BANDS; i++)
        workT2[i] = 0.0;

    for(i = 0; i < BANDS; i++) {
        for(cnt2 = i-1; cnt2 > cyclTab2[i]; cnt2--)
            flcoeffs5[cnt2] += workT3[i];
        workT2[cnt2+1] += workT3[i];
    }

    accum = 0.0;

    for(i = BANDS-2; i >= 0; i--) {
        accum = (workT2[i+1] + accum) * imc_weights2[i];
        flcoeffs5[i] += accum;
        //there is missing code here, but it seems to never be triggered
    }
}


static void imc_read_level_coeffs(IMCContext* q, int stream_format_code, int* levlCoeffs)
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

    if(stream_format_code & 4)
        start = 1;
    if(start)
        levlCoeffs[0] = get_bits(&q->gb, 7);
    for(i = start; i < BANDS; i++){
        levlCoeffs[i] = get_vlc2(&q->gb, hufftab[cb_sel[i]]->table, hufftab[cb_sel[i]]->bits, 2);
        if(levlCoeffs[i] == 17)
            levlCoeffs[i] += get_bits(&q->gb, 4);
    }
}

static void imc_decode_level_coefficients(IMCContext* q, int* levlCoeffBuf, float* flcoeffs1,
                                         float* flcoeffs2)
{
    int i, level;
    float tmp, tmp2;
    //maybe some frequency division thingy

    flcoeffs1[0] = 20000.0 / pow (2, levlCoeffBuf[0] * 0.18945); // 0.18945 = log2(10) * 0.05703125
    flcoeffs2[0] = log(flcoeffs1[0])/log(2);
    tmp = flcoeffs1[0];
    tmp2 = flcoeffs2[0];

    for(i = 1; i < BANDS; i++) {
        level = levlCoeffBuf[i];
        if (level == 16) {
            flcoeffs1[i] = 1.0;
            flcoeffs2[i] = 0.0;
        } else {
            if (level < 17)
                level -=7;
            else if (level <= 24)
                level -=32;
            else
                level -=16;

            tmp  *= imc_exp_tab[15 + level];
            tmp2 += 0.83048 * level;  // 0.83048 = log2(10) * 0.25
            flcoeffs1[i] = tmp;
            flcoeffs2[i] = tmp2;
        }
    }
}


static void imc_decode_level_coefficients2(IMCContext* q, int* levlCoeffBuf, float* old_floor, float* flcoeffs1,
                                          float* flcoeffs2) {
    int i;
        //FIXME maybe flag_buf = noise coding and flcoeffs1 = new scale factors
        //      and flcoeffs2 old scale factors
        //      might be incomplete due to a missing table that is in the binary code
    for(i = 0; i < BANDS; i++) {
        flcoeffs1[i] = 0;
        if(levlCoeffBuf[i] < 16) {
            flcoeffs1[i] = imc_exp_tab2[levlCoeffBuf[i]] * old_floor[i];
            flcoeffs2[i] = (levlCoeffBuf[i]-7) * 0.83048 + flcoeffs2[i]; // 0.83048 = log2(10) * 0.25
        } else {
            flcoeffs1[i] = old_floor[i];
        }
    }
}

/**
 * Perform bit allocation depending on bits available
 */
static int bit_allocation (IMCContext* q, int stream_format_code, int freebits, int flag) {
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

    for(i = 0; i < BANDS; i++)
        highest = FFMAX(highest, q->flcoeffs1[i]);

    for(i = 0; i < BANDS-1; i++) {
        q->flcoeffs4[i] = q->flcoeffs3[i] - log(q->flcoeffs5[i])/log(2);
    }
    q->flcoeffs4[BANDS - 1] = limit;

    highest = highest * 0.25;

    for(i = 0; i < BANDS; i++) {
        indx = -1;
        if ((band_tab[i+1] - band_tab[i]) == q->bandWidthT[i])
            indx = 0;

        if ((band_tab[i+1] - band_tab[i]) > q->bandWidthT[i])
            indx = 1;

        if (((band_tab[i+1] - band_tab[i])/2) >= q->bandWidthT[i])
            indx = 2;

        if (indx == -1)
            return AVERROR_INVALIDDATA;

        q->flcoeffs4[i] = q->flcoeffs4[i] + xTab[(indx*2 + (q->flcoeffs1[i] < highest)) * 2 + flag];
    }

    if (stream_format_code & 0x2) {
        q->flcoeffs4[0] = limit;
        q->flcoeffs4[1] = limit;
        q->flcoeffs4[2] = limit;
        q->flcoeffs4[3] = limit;
    }

    for(i = (stream_format_code & 0x2)?4:0; i < BANDS-1; i++) {
        iacc += q->bandWidthT[i];
        summa += q->bandWidthT[i] * q->flcoeffs4[i];
    }
    q->bandWidthT[BANDS-1] = 0;
    summa = (summa * 0.5 - freebits) / iacc;


    for(i = 0; i < BANDS/2; i++) {
        rres = summer - freebits;
        if((rres >= -8) && (rres <= 8)) break;

        summer = 0;
        iacc = 0;

        for(j = (stream_format_code & 0x2)?4:0; j < BANDS; j++) {
            cwlen = av_clipf(((q->flcoeffs4[j] * 0.5) - summa + 0.5), 0, 6);

            q->bitsBandT[j] = cwlen;
            summer += q->bandWidthT[j] * cwlen;

            if (cwlen > 0)
                iacc += q->bandWidthT[j];
        }

        flg = t2;
        t2 = 1;
        if (freebits < summer)
            t2 = -1;
        if (i == 0)
            flg = t2;
        if(flg != t2)
            t1++;

        summa = (float)(summer - freebits) / ((t1 + 1) * iacc) + summa;
    }

    for(i = (stream_format_code & 0x2)?4:0; i < BANDS; i++) {
        for(j = band_tab[i]; j < band_tab[i+1]; j++)
            q->CWlengthT[j] = q->bitsBandT[i];
    }

    if (freebits > summer) {
        for(i = 0; i < BANDS; i++) {
            workT[i] = (q->bitsBandT[i] == 6) ? -1.e20 : (q->bitsBandT[i] * -2 + q->flcoeffs4[i] - 0.415);
        }

        highest = 0.0;

        do{
            if (highest <= -1.e20)
                break;

            found_indx = 0;
            highest = -1.e20;

            for(i = 0; i < BANDS; i++) {
                if (workT[i] > highest) {
                    highest = workT[i];
                    found_indx = i;
                }
            }

            if (highest > -1.e20) {
                workT[found_indx] -= 2.0;
                if (++(q->bitsBandT[found_indx]) == 6)
                    workT[found_indx] = -1.e20;

                for(j = band_tab[found_indx]; j < band_tab[found_indx+1] && (freebits > summer); j++){
                    q->CWlengthT[j]++;
                    summer++;
                }
            }
        }while (freebits > summer);
    }
    if (freebits < summer) {
        for(i = 0; i < BANDS; i++) {
            workT[i] = q->bitsBandT[i] ? (q->bitsBandT[i] * -2 + q->flcoeffs4[i] + 1.585) : 1.e20;
        }
        if (stream_format_code & 0x2) {
            workT[0] = 1.e20;
            workT[1] = 1.e20;
            workT[2] = 1.e20;
            workT[3] = 1.e20;
        }
        while (freebits < summer){
            lowest = 1.e10;
            low_indx = 0;
            for(i = 0; i < BANDS; i++) {
                if (workT[i] < lowest) {
                    lowest = workT[i];
                    low_indx = i;
                }
            }
            //if(lowest >= 1.e10) break;
            workT[low_indx] = lowest + 2.0;

            if (!(--q->bitsBandT[low_indx]))
                workT[low_indx] = 1.e20;

            for(j = band_tab[low_indx]; j < band_tab[low_indx+1] && (freebits < summer); j++){
                if(q->CWlengthT[j] > 0){
                    q->CWlengthT[j]--;
                    summer--;
                }
            }
        }
    }
    return 0;
}

static void imc_get_skip_coeff(IMCContext* q) {
    int i, j;

    memset(q->skipFlagBits, 0, sizeof(q->skipFlagBits));
    memset(q->skipFlagCount, 0, sizeof(q->skipFlagCount));
    for(i = 0; i < BANDS; i++) {
        if (!q->bandFlagsBuf[i] || !q->bandWidthT[i])
            continue;

        if (!q->skipFlagRaw[i]) {
            q->skipFlagBits[i] = band_tab[i+1] - band_tab[i];

            for(j = band_tab[i]; j < band_tab[i+1]; j++) {
                if ((q->skipFlags[j] = get_bits1(&q->gb)))
                    q->skipFlagCount[i]++;
            }
        } else {
            for(j = band_tab[i]; j < (band_tab[i+1]-1); j += 2) {
                if(!get_bits1(&q->gb)){//0
                    q->skipFlagBits[i]++;
                    q->skipFlags[j]=1;
                    q->skipFlags[j+1]=1;
                    q->skipFlagCount[i] += 2;
                }else{
                    if(get_bits1(&q->gb)){//11
                        q->skipFlagBits[i] +=2;
                        q->skipFlags[j]=0;
                        q->skipFlags[j+1]=1;
                        q->skipFlagCount[i]++;
                    }else{
                        q->skipFlagBits[i] +=3;
                        q->skipFlags[j+1]=0;
                        if(!get_bits1(&q->gb)){//100
                            q->skipFlags[j]=1;
                            q->skipFlagCount[i]++;
                        }else{//101
                            q->skipFlags[j]=0;
                        }
                    }
                }
            }

            if (j < band_tab[i+1]) {
                q->skipFlagBits[i]++;
                if ((q->skipFlags[j] = get_bits1(&q->gb)))
                    q->skipFlagCount[i]++;
            }
        }
    }
}

/**
 * Increase highest' band coefficient sizes as some bits won't be used
 */
static void imc_adjust_bit_allocation (IMCContext* q, int summer) {
    float workT[32];
    int corrected = 0;
    int i, j;
    float highest = 0;
    int found_indx=0;

    for(i = 0; i < BANDS; i++) {
        workT[i] = (q->bitsBandT[i] == 6) ? -1.e20 : (q->bitsBandT[i] * -2 + q->flcoeffs4[i] - 0.415);
    }

    while (corrected < summer) {
        if(highest <= -1.e20)
            break;

        highest = -1.e20;

        for(i = 0; i < BANDS; i++) {
            if (workT[i] > highest) {
                highest = workT[i];
                found_indx = i;
            }
        }

        if (highest > -1.e20) {
            workT[found_indx] -= 2.0;
            if (++(q->bitsBandT[found_indx]) == 6)
                workT[found_indx] = -1.e20;

            for(j = band_tab[found_indx]; j < band_tab[found_indx+1] && (corrected < summer); j++) {
                if (!q->skipFlags[j] && (q->CWlengthT[j] < 6)) {
                    q->CWlengthT[j]++;
                    corrected++;
                }
            }
        }
    }
}

static void imc_imdct256(IMCContext *q) {
    int i;
    float re, im;

    /* prerotation */
    for(i=0; i < COEFFS/2; i++){
        q->samples[i].re = -(q->pre_coef1[i] * q->CWdecoded[COEFFS-1-i*2]) -
                           (q->pre_coef2[i] * q->CWdecoded[i*2]);
        q->samples[i].im = (q->pre_coef2[i] * q->CWdecoded[COEFFS-1-i*2]) -
                           (q->pre_coef1[i] * q->CWdecoded[i*2]);
    }

    /* FFT */
    q->fft.fft_permute(&q->fft, q->samples);
    q->fft.fft_calc   (&q->fft, q->samples);

    /* postrotation, window and reorder */
    for(i = 0; i < COEFFS/2; i++){
        re = (q->samples[i].re * q->post_cos[i]) + (-q->samples[i].im * q->post_sin[i]);
        im = (-q->samples[i].im * q->post_cos[i]) - (q->samples[i].re * q->post_sin[i]);
        q->out_samples[i*2] = (q->mdct_sine_window[COEFFS-1-i*2] * q->last_fft_im[i]) + (q->mdct_sine_window[i*2] * re);
        q->out_samples[COEFFS-1-i*2] = (q->mdct_sine_window[i*2] * q->last_fft_im[i]) - (q->mdct_sine_window[COEFFS-1-i*2] * re);
        q->last_fft_im[i] = im;
    }
}

static int inverse_quant_coeff (IMCContext* q, int stream_format_code) {
    int i, j;
    int middle_value, cw_len, max_size;
    const float* quantizer;

    for(i = 0; i < BANDS; i++) {
        for(j = band_tab[i]; j < band_tab[i+1]; j++) {
            q->CWdecoded[j] = 0;
            cw_len = q->CWlengthT[j];

            if (cw_len <= 0 || q->skipFlags[j])
                continue;

            max_size = 1 << cw_len;
            middle_value = max_size >> 1;

            if (q->codewords[j] >= max_size || q->codewords[j] < 0)
                return AVERROR_INVALIDDATA;

            if (cw_len >= 4){
                quantizer = imc_quantizer2[(stream_format_code & 2) >> 1];
                if (q->codewords[j] >= middle_value)
                    q->CWdecoded[j] = quantizer[q->codewords[j] - 8] * q->flcoeffs6[i];
                else
                    q->CWdecoded[j] = -quantizer[max_size - q->codewords[j] - 8 - 1] * q->flcoeffs6[i];
            }else{
                quantizer = imc_quantizer1[((stream_format_code & 2) >> 1) | (q->bandFlagsBuf[i] << 1)];
                if (q->codewords[j] >= middle_value)
                    q->CWdecoded[j] = quantizer[q->codewords[j] - 1] * q->flcoeffs6[i];
                else
                    q->CWdecoded[j] = -quantizer[max_size - 2 - q->codewords[j]] * q->flcoeffs6[i];
            }
        }
    }
    return 0;
}


static int imc_get_coeffs (IMCContext* q) {
    int i, j, cw_len, cw;

    for(i = 0; i < BANDS; i++) {
        if(!q->sumLenArr[i]) continue;
        if (q->bandFlagsBuf[i] || q->bandWidthT[i]) {
            for(j = band_tab[i]; j < band_tab[i+1]; j++) {
                cw_len = q->CWlengthT[j];
                cw = 0;

                if (get_bits_count(&q->gb) + cw_len > 512){
//av_log(NULL,0,"Band %i coeff %i cw_len %i\n",i,j,cw_len);
                    return AVERROR_INVALIDDATA;
                }

                if(cw_len && (!q->bandFlagsBuf[i] || !q->skipFlags[j]))
                    cw = get_bits(&q->gb, cw_len);

                q->codewords[j] = cw;
            }
        }
    }
    return 0;
}

static int imc_decode_frame(AVCodecContext * avctx, void *data,
                            int *got_frame_ptr, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;

    IMCContext *q = avctx->priv_data;

    int stream_format_code;
    int imc_hdr, i, j, ret;
    int flag;
    int bits, summer;
    int counter, bitscount;
    LOCAL_ALIGNED_16(uint16_t, buf16, [IMC_BLOCK_SIZE / 2]);

    if (buf_size < IMC_BLOCK_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "imc frame too small!\n");
        return AVERROR_INVALIDDATA;
    }

    /* get output buffer */
    q->frame.nb_samples = COEFFS;
    if ((ret = avctx->get_buffer(avctx, &q->frame)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }
    q->out_samples = (float *)q->frame.data[0];

    q->dsp.bswap16_buf(buf16, (const uint16_t*)buf, IMC_BLOCK_SIZE / 2);

    init_get_bits(&q->gb, (const uint8_t*)buf16, IMC_BLOCK_SIZE * 8);

    /* Check the frame header */
    imc_hdr = get_bits(&q->gb, 9);
    if (imc_hdr != IMC_FRAME_ID) {
        av_log(avctx, AV_LOG_ERROR, "imc frame header check failed!\n");
        av_log(avctx, AV_LOG_ERROR, "got %x instead of 0x21.\n", imc_hdr);
        return AVERROR_INVALIDDATA;
    }
    stream_format_code = get_bits(&q->gb, 3);

    if(stream_format_code & 1){
        av_log(avctx, AV_LOG_ERROR, "Stream code format %X is not supported\n", stream_format_code);
        return AVERROR_INVALIDDATA;
    }

//    av_log(avctx, AV_LOG_DEBUG, "stream_format_code = %d\n", stream_format_code);

    if (stream_format_code & 0x04)
        q->decoder_reset = 1;

    if(q->decoder_reset) {
        memset(q->out_samples, 0, sizeof(q->out_samples));
        for(i = 0; i < BANDS; i++)q->old_floor[i] = 1.0;
        for(i = 0; i < COEFFS; i++)q->CWdecoded[i] = 0;
        q->decoder_reset = 0;
    }

    flag = get_bits1(&q->gb);
    imc_read_level_coeffs(q, stream_format_code, q->levlCoeffBuf);

    if (stream_format_code & 0x4)
        imc_decode_level_coefficients(q, q->levlCoeffBuf, q->flcoeffs1, q->flcoeffs2);
    else
        imc_decode_level_coefficients2(q, q->levlCoeffBuf, q->old_floor, q->flcoeffs1, q->flcoeffs2);

    memcpy(q->old_floor, q->flcoeffs1, 32 * sizeof(float));

    counter = 0;
    for (i=0 ; i<BANDS ; i++) {
        if (q->levlCoeffBuf[i] == 16) {
            q->bandWidthT[i] = 0;
            counter++;
        } else
            q->bandWidthT[i] = band_tab[i+1] - band_tab[i];
    }
    memset(q->bandFlagsBuf, 0, BANDS * sizeof(int));
    for(i = 0; i < BANDS-1; i++) {
        if (q->bandWidthT[i])
            q->bandFlagsBuf[i] = get_bits1(&q->gb);
    }

    imc_calculate_coeffs(q, q->flcoeffs1, q->flcoeffs2, q->bandWidthT, q->flcoeffs3, q->flcoeffs5);

    bitscount = 0;
    /* first 4 bands will be assigned 5 bits per coefficient */
    if (stream_format_code & 0x2) {
        bitscount += 15;

        q->bitsBandT[0] = 5;
        q->CWlengthT[0] = 5;
        q->CWlengthT[1] = 5;
        q->CWlengthT[2] = 5;
        for(i = 1; i < 4; i++){
            bits = (q->levlCoeffBuf[i] == 16) ? 0 : 5;
            q->bitsBandT[i] = bits;
            for(j = band_tab[i]; j < band_tab[i+1]; j++) {
                q->CWlengthT[j] = bits;
                bitscount += bits;
            }
        }
    }

    if((ret = bit_allocation (q, stream_format_code,
                              512 - bitscount - get_bits_count(&q->gb), flag)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Bit allocations failed\n");
        q->decoder_reset = 1;
        return ret;
    }

    for(i = 0; i < BANDS; i++) {
        q->sumLenArr[i] = 0;
        q->skipFlagRaw[i] = 0;
        for(j = band_tab[i]; j < band_tab[i+1]; j++)
            q->sumLenArr[i] += q->CWlengthT[j];
        if (q->bandFlagsBuf[i])
            if( (((band_tab[i+1] - band_tab[i]) * 1.5) > q->sumLenArr[i]) && (q->sumLenArr[i] > 0))
                q->skipFlagRaw[i] = 1;
    }

    imc_get_skip_coeff(q);

    for(i = 0; i < BANDS; i++) {
        q->flcoeffs6[i] = q->flcoeffs1[i];
        /* band has flag set and at least one coded coefficient */
        if (q->bandFlagsBuf[i] && (band_tab[i+1] - band_tab[i]) != q->skipFlagCount[i]){
                q->flcoeffs6[i] *= q->sqrt_tab[band_tab[i+1] - band_tab[i]] /
                                   q->sqrt_tab[(band_tab[i+1] - band_tab[i] - q->skipFlagCount[i])];
        }
    }

    /* calculate bits left, bits needed and adjust bit allocation */
    bits = summer = 0;

    for(i = 0; i < BANDS; i++) {
        if (q->bandFlagsBuf[i]) {
            for(j = band_tab[i]; j < band_tab[i+1]; j++) {
                if(q->skipFlags[j]) {
                    summer += q->CWlengthT[j];
                    q->CWlengthT[j] = 0;
                }
            }
            bits += q->skipFlagBits[i];
            summer -= q->skipFlagBits[i];
        }
    }
    imc_adjust_bit_allocation(q, summer);

    for(i = 0; i < BANDS; i++) {
        q->sumLenArr[i] = 0;

        for(j = band_tab[i]; j < band_tab[i+1]; j++)
            if (!q->skipFlags[j])
                q->sumLenArr[i] += q->CWlengthT[j];
    }

    memset(q->codewords, 0, sizeof(q->codewords));

    if(imc_get_coeffs(q) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Read coefficients failed\n");
        q->decoder_reset = 1;
        return AVERROR_INVALIDDATA;
    }

    if(inverse_quant_coeff(q, stream_format_code) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Inverse quantization of coefficients failed\n");
        q->decoder_reset = 1;
        return AVERROR_INVALIDDATA;
    }

    memset(q->skipFlags, 0, sizeof(q->skipFlags));

    imc_imdct256(q);

    *got_frame_ptr   = 1;
    *(AVFrame *)data = q->frame;

    return IMC_BLOCK_SIZE;
}


static av_cold int imc_decode_close(AVCodecContext * avctx)
{
    IMCContext *q = avctx->priv_data;

    ff_fft_end(&q->fft);

    return 0;
}


AVCodec ff_imc_decoder = {
    .name = "imc",
    .type = AVMEDIA_TYPE_AUDIO,
    .id = CODEC_ID_IMC,
    .priv_data_size = sizeof(IMCContext),
    .init = imc_decode_init,
    .close = imc_decode_close,
    .decode = imc_decode_frame,
    .capabilities = CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("IMC (Intel Music Coder)"),
};
