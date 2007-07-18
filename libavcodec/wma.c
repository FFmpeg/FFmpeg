/*
 * WMA compatible codec
 * Copyright (c) 2002-2007 The FFmpeg Project.
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
#include "wma.h"
#include "wmadata.h"

#undef NDEBUG
#include <assert.h>

/* XXX: use same run/length optimization as mpeg decoders */
//FIXME maybe split decode / encode or pass flag
static void init_coef_vlc(VLC *vlc,
                          uint16_t **prun_table, uint16_t **plevel_table, uint16_t **pint_table,
                          const CoefVLCTable *vlc_table)
{
    int n = vlc_table->n;
    const uint8_t *table_bits = vlc_table->huffbits;
    const uint32_t *table_codes = vlc_table->huffcodes;
    const uint16_t *levels_table = vlc_table->levels;
    uint16_t *run_table, *level_table, *int_table;
    int i, l, j, k, level;

    init_vlc(vlc, VLCBITS, n, table_bits, 1, 1, table_codes, 4, 4, 0);

    run_table = av_malloc(n * sizeof(uint16_t));
    level_table = av_malloc(n * sizeof(uint16_t));
    int_table = av_malloc(n * sizeof(uint16_t));
    i = 2;
    level = 1;
    k = 0;
    while (i < n) {
        int_table[k]= i;
        l = levels_table[k++];
        for(j=0;j<l;j++) {
            run_table[i] = j;
            level_table[i] = level;
            i++;
        }
        level++;
    }
    *prun_table = run_table;
    *plevel_table = level_table;
    *pint_table= int_table;
}

int ff_wma_init(AVCodecContext * avctx, int flags2)
{
    WMACodecContext *s = avctx->priv_data;
    int i;
    float *window;
    float bps1, high_freq;
    volatile float bps;
    int sample_rate1;
    int coef_vlc_table;

    if(   avctx->sample_rate<=0 || avctx->sample_rate>50000
       || avctx->channels<=0 || avctx->channels>8
       || avctx->bit_rate<=0)
        return -1;

    s->sample_rate = avctx->sample_rate;
    s->nb_channels = avctx->channels;
    s->bit_rate = avctx->bit_rate;
    s->block_align = avctx->block_align;

    dsputil_init(&s->dsp, avctx);

    if (avctx->codec->id == CODEC_ID_WMAV1) {
        s->version = 1;
    } else {
        s->version = 2;
    }

    /* compute MDCT block size */
    if (s->sample_rate <= 16000) {
        s->frame_len_bits = 9;
    } else if (s->sample_rate <= 22050 ||
               (s->sample_rate <= 32000 && s->version == 1)) {
        s->frame_len_bits = 10;
    } else {
        s->frame_len_bits = 11;
    }
    s->frame_len = 1 << s->frame_len_bits;
    if (s->use_variable_block_len) {
        int nb_max, nb;
        nb = ((flags2 >> 3) & 3) + 1;
        if ((s->bit_rate / s->nb_channels) >= 32000)
            nb += 2;
        nb_max = s->frame_len_bits - BLOCK_MIN_BITS;
        if (nb > nb_max)
            nb = nb_max;
        s->nb_block_sizes = nb + 1;
    } else {
        s->nb_block_sizes = 1;
    }

    /* init rate dependent parameters */
    s->use_noise_coding = 1;
    high_freq = s->sample_rate * 0.5;

    /* if version 2, then the rates are normalized */
    sample_rate1 = s->sample_rate;
    if (s->version == 2) {
        if (sample_rate1 >= 44100)
            sample_rate1 = 44100;
        else if (sample_rate1 >= 22050)
            sample_rate1 = 22050;
        else if (sample_rate1 >= 16000)
            sample_rate1 = 16000;
        else if (sample_rate1 >= 11025)
            sample_rate1 = 11025;
        else if (sample_rate1 >= 8000)
            sample_rate1 = 8000;
    }

    bps = (float)s->bit_rate / (float)(s->nb_channels * s->sample_rate);
    s->byte_offset_bits = av_log2((int)(bps * s->frame_len / 8.0 + 0.5)) + 2;

    /* compute high frequency value and choose if noise coding should
       be activated */
    bps1 = bps;
    if (s->nb_channels == 2)
        bps1 = bps * 1.6;
    if (sample_rate1 == 44100) {
        if (bps1 >= 0.61)
            s->use_noise_coding = 0;
        else
            high_freq = high_freq * 0.4;
    } else if (sample_rate1 == 22050) {
        if (bps1 >= 1.16)
            s->use_noise_coding = 0;
        else if (bps1 >= 0.72)
            high_freq = high_freq * 0.7;
        else
            high_freq = high_freq * 0.6;
    } else if (sample_rate1 == 16000) {
        if (bps > 0.5)
            high_freq = high_freq * 0.5;
        else
            high_freq = high_freq * 0.3;
    } else if (sample_rate1 == 11025) {
        high_freq = high_freq * 0.7;
    } else if (sample_rate1 == 8000) {
        if (bps <= 0.625) {
            high_freq = high_freq * 0.5;
        } else if (bps > 0.75) {
            s->use_noise_coding = 0;
        } else {
            high_freq = high_freq * 0.65;
        }
    } else {
        if (bps >= 0.8) {
            high_freq = high_freq * 0.75;
        } else if (bps >= 0.6) {
            high_freq = high_freq * 0.6;
        } else {
            high_freq = high_freq * 0.5;
        }
    }
    dprintf(s->avctx, "flags2=0x%x\n", flags2);
    dprintf(s->avctx, "version=%d channels=%d sample_rate=%d bitrate=%d block_align=%d\n",
           s->version, s->nb_channels, s->sample_rate, s->bit_rate,
           s->block_align);
    dprintf(s->avctx, "bps=%f bps1=%f high_freq=%f bitoffset=%d\n",
           bps, bps1, high_freq, s->byte_offset_bits);
    dprintf(s->avctx, "use_noise_coding=%d use_exp_vlc=%d nb_block_sizes=%d\n",
           s->use_noise_coding, s->use_exp_vlc, s->nb_block_sizes);

    /* compute the scale factor band sizes for each MDCT block size */
    {
        int a, b, pos, lpos, k, block_len, i, j, n;
        const uint8_t *table;

        if (s->version == 1) {
            s->coefs_start = 3;
        } else {
            s->coefs_start = 0;
        }
        for(k = 0; k < s->nb_block_sizes; k++) {
            block_len = s->frame_len >> k;

            if (s->version == 1) {
                lpos = 0;
                for(i=0;i<25;i++) {
                    a = wma_critical_freqs[i];
                    b = s->sample_rate;
                    pos = ((block_len * 2 * a)  + (b >> 1)) / b;
                    if (pos > block_len)
                        pos = block_len;
                    s->exponent_bands[0][i] = pos - lpos;
                    if (pos >= block_len) {
                        i++;
                        break;
                    }
                    lpos = pos;
                }
                s->exponent_sizes[0] = i;
            } else {
                /* hardcoded tables */
                table = NULL;
                a = s->frame_len_bits - BLOCK_MIN_BITS - k;
                if (a < 3) {
                    if (s->sample_rate >= 44100)
                        table = exponent_band_44100[a];
                    else if (s->sample_rate >= 32000)
                        table = exponent_band_32000[a];
                    else if (s->sample_rate >= 22050)
                        table = exponent_band_22050[a];
                }
                if (table) {
                    n = *table++;
                    for(i=0;i<n;i++)
                        s->exponent_bands[k][i] = table[i];
                    s->exponent_sizes[k] = n;
                } else {
                    j = 0;
                    lpos = 0;
                    for(i=0;i<25;i++) {
                        a = wma_critical_freqs[i];
                        b = s->sample_rate;
                        pos = ((block_len * 2 * a)  + (b << 1)) / (4 * b);
                        pos <<= 2;
                        if (pos > block_len)
                            pos = block_len;
                        if (pos > lpos)
                            s->exponent_bands[k][j++] = pos - lpos;
                        if (pos >= block_len)
                            break;
                        lpos = pos;
                    }
                    s->exponent_sizes[k] = j;
                }
            }

            /* max number of coefs */
            s->coefs_end[k] = (s->frame_len - ((s->frame_len * 9) / 100)) >> k;
            /* high freq computation */
            s->high_band_start[k] = (int)((block_len * 2 * high_freq) /
                                          s->sample_rate + 0.5);
            n = s->exponent_sizes[k];
            j = 0;
            pos = 0;
            for(i=0;i<n;i++) {
                int start, end;
                start = pos;
                pos += s->exponent_bands[k][i];
                end = pos;
                if (start < s->high_band_start[k])
                    start = s->high_band_start[k];
                if (end > s->coefs_end[k])
                    end = s->coefs_end[k];
                if (end > start)
                    s->exponent_high_bands[k][j++] = end - start;
            }
            s->exponent_high_sizes[k] = j;
#if 0
            tprintf(s->avctx, "%5d: coefs_end=%d high_band_start=%d nb_high_bands=%d: ",
                  s->frame_len >> k,
                  s->coefs_end[k],
                  s->high_band_start[k],
                  s->exponent_high_sizes[k]);
            for(j=0;j<s->exponent_high_sizes[k];j++)
                tprintf(s->avctx, " %d", s->exponent_high_bands[k][j]);
            tprintf(s->avctx, "\n");
#endif
        }
    }

#ifdef TRACE
    {
        int i, j;
        for(i = 0; i < s->nb_block_sizes; i++) {
            tprintf(s->avctx, "%5d: n=%2d:",
                   s->frame_len >> i,
                   s->exponent_sizes[i]);
            for(j=0;j<s->exponent_sizes[i];j++)
                tprintf(s->avctx, " %d", s->exponent_bands[i][j]);
            tprintf(s->avctx, "\n");
        }
    }
#endif

    /* init MDCT windows : simple sinus window */
    for(i = 0; i < s->nb_block_sizes; i++) {
        int n, j;
        float alpha;
        n = 1 << (s->frame_len_bits - i);
        window = av_malloc(sizeof(float) * n);
        alpha = M_PI / (2.0 * n);
        for(j=0;j<n;j++) {
            window[j] = sin((j + 0.5) * alpha);
        }
        s->windows[i] = window;
    }

    s->reset_block_lengths = 1;

    if (s->use_noise_coding) {

        /* init the noise generator */
        if (s->use_exp_vlc)
            s->noise_mult = 0.02;
        else
            s->noise_mult = 0.04;

#ifdef TRACE
        for(i=0;i<NOISE_TAB_SIZE;i++)
            s->noise_table[i] = 1.0 * s->noise_mult;
#else
        {
            unsigned int seed;
            float norm;
            seed = 1;
            norm = (1.0 / (float)(1LL << 31)) * sqrt(3) * s->noise_mult;
            for(i=0;i<NOISE_TAB_SIZE;i++) {
                seed = seed * 314159 + 1;
                s->noise_table[i] = (float)((int)seed) * norm;
            }
        }
#endif
    }

    /* choose the VLC tables for the coefficients */
    coef_vlc_table = 2;
    if (s->sample_rate >= 32000) {
        if (bps1 < 0.72)
            coef_vlc_table = 0;
        else if (bps1 < 1.16)
            coef_vlc_table = 1;
    }
    s->coef_vlcs[0]= &coef_vlcs[coef_vlc_table * 2    ];
    s->coef_vlcs[1]= &coef_vlcs[coef_vlc_table * 2 + 1];
    init_coef_vlc(&s->coef_vlc[0], &s->run_table[0], &s->level_table[0], &s->int_table[0],
                  s->coef_vlcs[0]);
    init_coef_vlc(&s->coef_vlc[1], &s->run_table[1], &s->level_table[1], &s->int_table[1],
                  s->coef_vlcs[1]);

    return 0;
}

int ff_wma_total_gain_to_bits(int total_gain){
         if (total_gain < 15) return 13;
    else if (total_gain < 32) return 12;
    else if (total_gain < 40) return 11;
    else if (total_gain < 45) return 10;
    else                      return  9;
}

int ff_wma_end(AVCodecContext *avctx)
{
    WMACodecContext *s = avctx->priv_data;
    int i;

    for(i = 0; i < s->nb_block_sizes; i++)
        ff_mdct_end(&s->mdct_ctx[i]);
    for(i = 0; i < s->nb_block_sizes; i++)
        av_free(s->windows[i]);

    if (s->use_exp_vlc) {
        free_vlc(&s->exp_vlc);
    }
    if (s->use_noise_coding) {
        free_vlc(&s->hgain_vlc);
    }
    for(i = 0;i < 2; i++) {
        free_vlc(&s->coef_vlc[i]);
        av_free(s->run_table[i]);
        av_free(s->level_table[i]);
        av_free(s->int_table[i]);
    }

    return 0;
}
