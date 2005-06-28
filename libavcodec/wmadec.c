/*
 * WMA compatible decoder
 * Copyright (c) 2002 The FFmpeg Project.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/**
 * @file wmadec.c
 * WMA compatible decoder.
 * This decoder handles Microsoft Windows Media Audio data, versions 1 & 2.
 * WMA v1 is identified by audio format 0x160 in Microsoft media files 
 * (ASF/AVI/WAV). WMA v2 is identified by audio format 0x161.
 *
 * To use this decoder, a calling application must supply the extra data
 * bytes provided with the WMA data. These are the extra, codec-specific
 * bytes at the end of a WAVEFORMATEX data structure. Transmit these bytes 
 * to the decoder using the extradata[_size] fields in AVCodecContext. There 
 * should be 4 extra bytes for v1 data and 6 extra bytes for v2 data.
 */

#include "avcodec.h"
#include "bitstream.h"
#include "dsputil.h"

/* size of blocks */
#define BLOCK_MIN_BITS 7
#define BLOCK_MAX_BITS 11
#define BLOCK_MAX_SIZE (1 << BLOCK_MAX_BITS)

#define BLOCK_NB_SIZES (BLOCK_MAX_BITS - BLOCK_MIN_BITS + 1)

/* XXX: find exact max size */
#define HIGH_BAND_MAX_SIZE 16

#define NB_LSP_COEFS 10

/* XXX: is it a suitable value ? */
#define MAX_CODED_SUPERFRAME_SIZE 16384

#define MAX_CHANNELS 2

#define NOISE_TAB_SIZE 8192

#define LSP_POW_BITS 7

typedef struct WMADecodeContext {
    GetBitContext gb;
    int sample_rate;
    int nb_channels;
    int bit_rate;
    int version; /* 1 = 0x160 (WMAV1), 2 = 0x161 (WMAV2) */
    int block_align;
    int use_bit_reservoir;
    int use_variable_block_len;
    int use_exp_vlc;  /* exponent coding: 0 = lsp, 1 = vlc + delta */
    int use_noise_coding; /* true if perceptual noise is added */
    int byte_offset_bits;
    VLC exp_vlc;
    int exponent_sizes[BLOCK_NB_SIZES];
    uint16_t exponent_bands[BLOCK_NB_SIZES][25];
    int high_band_start[BLOCK_NB_SIZES]; /* index of first coef in high band */
    int coefs_start;               /* first coded coef */
    int coefs_end[BLOCK_NB_SIZES]; /* max number of coded coefficients */
    int exponent_high_sizes[BLOCK_NB_SIZES];
    int exponent_high_bands[BLOCK_NB_SIZES][HIGH_BAND_MAX_SIZE]; 
    VLC hgain_vlc;
    
    /* coded values in high bands */
    int high_band_coded[MAX_CHANNELS][HIGH_BAND_MAX_SIZE];
    int high_band_values[MAX_CHANNELS][HIGH_BAND_MAX_SIZE];

    /* there are two possible tables for spectral coefficients */
    VLC coef_vlc[2];
    uint16_t *run_table[2];
    uint16_t *level_table[2];
    /* frame info */
    int frame_len;       /* frame length in samples */
    int frame_len_bits;  /* frame_len = 1 << frame_len_bits */
    int nb_block_sizes;  /* number of block sizes */
    /* block info */
    int reset_block_lengths;
    int block_len_bits; /* log2 of current block length */
    int next_block_len_bits; /* log2 of next block length */
    int prev_block_len_bits; /* log2 of prev block length */
    int block_len; /* block length in samples */
    int block_num; /* block number in current frame */
    int block_pos; /* current position in frame */
    uint8_t ms_stereo; /* true if mid/side stereo mode */
    uint8_t channel_coded[MAX_CHANNELS]; /* true if channel is coded */
    float exponents[MAX_CHANNELS][BLOCK_MAX_SIZE] __attribute__((aligned(16)));
    float max_exponent[MAX_CHANNELS];
    int16_t coefs1[MAX_CHANNELS][BLOCK_MAX_SIZE];
    float coefs[MAX_CHANNELS][BLOCK_MAX_SIZE] __attribute__((aligned(16)));
    MDCTContext mdct_ctx[BLOCK_NB_SIZES];
    float *windows[BLOCK_NB_SIZES];
    FFTSample mdct_tmp[BLOCK_MAX_SIZE] __attribute__((aligned(16))); /* temporary storage for imdct */
    /* output buffer for one frame and the last for IMDCT windowing */
    float frame_out[MAX_CHANNELS][BLOCK_MAX_SIZE * 2] __attribute__((aligned(16)));
    /* last frame info */
    uint8_t last_superframe[MAX_CODED_SUPERFRAME_SIZE + 4]; /* padding added */
    int last_bitoffset;
    int last_superframe_len;
    float noise_table[NOISE_TAB_SIZE];
    int noise_index;
    float noise_mult; /* XXX: suppress that and integrate it in the noise array */
    /* lsp_to_curve tables */
    float lsp_cos_table[BLOCK_MAX_SIZE];
    float lsp_pow_e_table[256];
    float lsp_pow_m_table1[(1 << LSP_POW_BITS)];
    float lsp_pow_m_table2[(1 << LSP_POW_BITS)];

#ifdef TRACE
    int frame_count;
#endif
} WMADecodeContext;

typedef struct CoefVLCTable {
    int n; /* total number of codes */
    const uint32_t *huffcodes; /* VLC bit values */
    const uint8_t *huffbits;   /* VLC bit size */
    const uint16_t *levels; /* table to build run/level tables */
} CoefVLCTable;

static void wma_lsp_to_curve_init(WMADecodeContext *s, int frame_len);

#include "wmadata.h"

#ifdef TRACE
static void dump_shorts(const char *name, const short *tab, int n)
{
    int i;

    tprintf("%s[%d]:\n", name, n);
    for(i=0;i<n;i++) {
        if ((i & 7) == 0)
            tprintf("%4d: ", i);
        tprintf(" %5d.0", tab[i]);
        if ((i & 7) == 7)
            tprintf("\n");
    }
}

static void dump_floats(const char *name, int prec, const float *tab, int n)
{
    int i;

    tprintf("%s[%d]:\n", name, n);
    for(i=0;i<n;i++) {
        if ((i & 7) == 0)
            tprintf("%4d: ", i);
        tprintf(" %8.*f", prec, tab[i]);
        if ((i & 7) == 7)
            tprintf("\n");
    }
    if ((i & 7) != 0)
        tprintf("\n");
}
#endif

/* XXX: use same run/length optimization as mpeg decoders */
static void init_coef_vlc(VLC *vlc, 
                          uint16_t **prun_table, uint16_t **plevel_table,
                          const CoefVLCTable *vlc_table)
{
    int n = vlc_table->n;
    const uint8_t *table_bits = vlc_table->huffbits;
    const uint32_t *table_codes = vlc_table->huffcodes;
    const uint16_t *levels_table = vlc_table->levels;
    uint16_t *run_table, *level_table;
    const uint16_t *p;
    int i, l, j, level;

    init_vlc(vlc, 9, n, table_bits, 1, 1, table_codes, 4, 4, 0);

    run_table = av_malloc(n * sizeof(uint16_t));
    level_table = av_malloc(n * sizeof(uint16_t));
    p = levels_table;
    i = 2;
    level = 1;
    while (i < n) {
        l = *p++;
        for(j=0;j<l;j++) {
            run_table[i] = j;
            level_table[i] = level;
            i++;
        }
        level++;
    }
    *prun_table = run_table;
    *plevel_table = level_table;
}

static int wma_decode_init(AVCodecContext * avctx)
{
    WMADecodeContext *s = avctx->priv_data;
    int i, flags1, flags2;
    float *window;
    uint8_t *extradata;
    float bps1, high_freq;
    volatile float bps;
    int sample_rate1;
    int coef_vlc_table;
    
    s->sample_rate = avctx->sample_rate;
    s->nb_channels = avctx->channels;
    s->bit_rate = avctx->bit_rate;
    s->block_align = avctx->block_align;

    if (avctx->codec->id == CODEC_ID_WMAV1) {
        s->version = 1;
    } else {
        s->version = 2;
    }
    
    /* extract flag infos */
    flags1 = 0;
    flags2 = 0;
    extradata = avctx->extradata;
    if (s->version == 1 && avctx->extradata_size >= 4) {
        flags1 = extradata[0] | (extradata[1] << 8);
        flags2 = extradata[2] | (extradata[3] << 8);
    } else if (s->version == 2 && avctx->extradata_size >= 6) {
        flags1 = extradata[0] | (extradata[1] << 8) | 
            (extradata[2] << 16) | (extradata[3] << 24);
        flags2 = extradata[4] | (extradata[5] << 8);
    }
    s->use_exp_vlc = flags2 & 0x0001;
    s->use_bit_reservoir = flags2 & 0x0002;
    s->use_variable_block_len = flags2 & 0x0004;

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

    /* init rate dependant parameters */
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
    s->byte_offset_bits = av_log2((int)(bps * s->frame_len / 8.0)) + 2;

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
    dprintf("flags1=0x%x flags2=0x%x\n", flags1, flags2);
    dprintf("version=%d channels=%d sample_rate=%d bitrate=%d block_align=%d\n",
           s->version, s->nb_channels, s->sample_rate, s->bit_rate, 
           s->block_align);
    dprintf("bps=%f bps1=%f high_freq=%f bitoffset=%d\n", 
           bps, bps1, high_freq, s->byte_offset_bits);
    dprintf("use_noise_coding=%d use_exp_vlc=%d nb_block_sizes=%d\n",
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
            tprintf("%5d: coefs_end=%d high_band_start=%d nb_high_bands=%d: ",
                  s->frame_len >> k, 
                  s->coefs_end[k],
                  s->high_band_start[k],
                  s->exponent_high_sizes[k]);
            for(j=0;j<s->exponent_high_sizes[k];j++)
                tprintf(" %d", s->exponent_high_bands[k][j]);
            tprintf("\n");
#endif
        }
    }

#ifdef TRACE
    {
        int i, j;
        for(i = 0; i < s->nb_block_sizes; i++) {
            tprintf("%5d: n=%2d:", 
                   s->frame_len >> i, 
                   s->exponent_sizes[i]);
            for(j=0;j<s->exponent_sizes[i];j++)
                tprintf(" %d", s->exponent_bands[i][j]);
            tprintf("\n");
        }
    }
#endif

    /* init MDCT */
    for(i = 0; i < s->nb_block_sizes; i++)
        ff_mdct_init(&s->mdct_ctx[i], s->frame_len_bits - i + 1, 1);
    
    /* init MDCT windows : simple sinus window */
    for(i = 0; i < s->nb_block_sizes; i++) {
        int n, j;
        float alpha;
        n = 1 << (s->frame_len_bits - i);
        window = av_malloc(sizeof(float) * n);
        alpha = M_PI / (2.0 * n);
        for(j=0;j<n;j++) {
            window[n - j - 1] = sin((j + 0.5) * alpha);
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
        init_vlc(&s->hgain_vlc, 9, sizeof(hgain_huffbits), 
                 hgain_huffbits, 1, 1,
                 hgain_huffcodes, 2, 2, 0);
    }

    if (s->use_exp_vlc) {
        init_vlc(&s->exp_vlc, 9, sizeof(scale_huffbits), 
                 scale_huffbits, 1, 1,
                 scale_huffcodes, 4, 4, 0);
    } else {
        wma_lsp_to_curve_init(s, s->frame_len);
    }

    /* choose the VLC tables for the coefficients */
    coef_vlc_table = 2;
    if (s->sample_rate >= 32000) {
        if (bps1 < 0.72)
            coef_vlc_table = 0;
        else if (bps1 < 1.16)
            coef_vlc_table = 1;
    }

    init_coef_vlc(&s->coef_vlc[0], &s->run_table[0], &s->level_table[0],
                  &coef_vlcs[coef_vlc_table * 2]);
    init_coef_vlc(&s->coef_vlc[1], &s->run_table[1], &s->level_table[1],
                  &coef_vlcs[coef_vlc_table * 2 + 1]);
    return 0;
}

/* interpolate values for a bigger or smaller block. The block must
   have multiple sizes */
static void interpolate_array(float *scale, int old_size, int new_size)
{
    int i, j, jincr, k;
    float v;

    if (new_size > old_size) {
        jincr = new_size / old_size;
        j = new_size;
        for(i = old_size - 1; i >=0; i--) {
            v = scale[i];
            k = jincr;
            do {
                scale[--j] = v;
            } while (--k);
        }
    } else if (new_size < old_size) {
        j = 0;
        jincr = old_size / new_size;
        for(i = 0; i < new_size; i++) {
            scale[i] = scale[j];
            j += jincr;
        }
    }
}

/* compute x^-0.25 with an exponent and mantissa table. We use linear
   interpolation to reduce the mantissa table size at a small speed
   expense (linear interpolation approximately doubles the number of
   bits of precision). */
static inline float pow_m1_4(WMADecodeContext *s, float x)
{
    union {
        float f;
        unsigned int v;
    } u, t;
    unsigned int e, m;
    float a, b;

    u.f = x;
    e = u.v >> 23;
    m = (u.v >> (23 - LSP_POW_BITS)) & ((1 << LSP_POW_BITS) - 1);
    /* build interpolation scale: 1 <= t < 2. */
    t.v = ((u.v << LSP_POW_BITS) & ((1 << 23) - 1)) | (127 << 23);
    a = s->lsp_pow_m_table1[m];
    b = s->lsp_pow_m_table2[m];
    return s->lsp_pow_e_table[e] * (a + b * t.f);
}

static void wma_lsp_to_curve_init(WMADecodeContext *s, int frame_len)
{  
    float wdel, a, b;
    int i, e, m;

    wdel = M_PI / frame_len;
    for(i=0;i<frame_len;i++)
        s->lsp_cos_table[i] = 2.0f * cos(wdel * i);

    /* tables for x^-0.25 computation */
    for(i=0;i<256;i++) {
        e = i - 126;
        s->lsp_pow_e_table[i] = pow(2.0, e * -0.25);
    }

    /* NOTE: these two tables are needed to avoid two operations in
       pow_m1_4 */
    b = 1.0;
    for(i=(1 << LSP_POW_BITS) - 1;i>=0;i--) {
        m = (1 << LSP_POW_BITS) + i;
        a = (float)m * (0.5 / (1 << LSP_POW_BITS));
        a = pow(a, -0.25);
        s->lsp_pow_m_table1[i] = 2 * a - b;
        s->lsp_pow_m_table2[i] = b - a;
        b = a;
    }
#if 0
    for(i=1;i<20;i++) {
        float v, r1, r2;
        v = 5.0 / i;
        r1 = pow_m1_4(s, v);
        r2 = pow(v,-0.25);
        printf("%f^-0.25=%f e=%f\n", v, r1, r2 - r1);
    }
#endif
}

/* NOTE: We use the same code as Vorbis here */
/* XXX: optimize it further with SSE/3Dnow */
static void wma_lsp_to_curve(WMADecodeContext *s, 
                             float *out, float *val_max_ptr, 
                             int n, float *lsp)
{
    int i, j;
    float p, q, w, v, val_max;

    val_max = 0;
    for(i=0;i<n;i++) {
        p = 0.5f;
        q = 0.5f;
        w = s->lsp_cos_table[i];
        for(j=1;j<NB_LSP_COEFS;j+=2){
            q *= w - lsp[j - 1];
            p *= w - lsp[j];
        }
        p *= p * (2.0f - w);
        q *= q * (2.0f + w);
        v = p + q;
        v = pow_m1_4(s, v);
        if (v > val_max)
            val_max = v;
        out[i] = v;
    }
    *val_max_ptr = val_max;
}

/* decode exponents coded with LSP coefficients (same idea as Vorbis) */
static void decode_exp_lsp(WMADecodeContext *s, int ch)
{
    float lsp_coefs[NB_LSP_COEFS];
    int val, i;

    for(i = 0; i < NB_LSP_COEFS; i++) {
        if (i == 0 || i >= 8)
            val = get_bits(&s->gb, 3);
        else
            val = get_bits(&s->gb, 4);
        lsp_coefs[i] = lsp_codebook[i][val];
    }

    wma_lsp_to_curve(s, s->exponents[ch], &s->max_exponent[ch],
                     s->block_len, lsp_coefs);
}

/* decode exponents coded with VLC codes */
static int decode_exp_vlc(WMADecodeContext *s, int ch)
{
    int last_exp, n, code;
    const uint16_t *ptr, *band_ptr;
    float v, *q, max_scale, *q_end;
    
    band_ptr = s->exponent_bands[s->frame_len_bits - s->block_len_bits];
    ptr = band_ptr;
    q = s->exponents[ch];
    q_end = q + s->block_len;
    max_scale = 0;
    if (s->version == 1) {
        last_exp = get_bits(&s->gb, 5) + 10;
        /* XXX: use a table */
        v = pow(10, last_exp * (1.0 / 16.0));
        max_scale = v;
        n = *ptr++;
        do {
            *q++ = v;
        } while (--n);
    }
    last_exp = 36;
    while (q < q_end) {
        code = get_vlc(&s->gb, &s->exp_vlc);
        if (code < 0)
            return -1;
        /* NOTE: this offset is the same as MPEG4 AAC ! */
        last_exp += code - 60;
        /* XXX: use a table */
        v = pow(10, last_exp * (1.0 / 16.0));
        if (v > max_scale)
            max_scale = v;
        n = *ptr++;
        do {
            *q++ = v;
        } while (--n);
    }
    s->max_exponent[ch] = max_scale;
    return 0;
}

/* return 0 if OK. return 1 if last block of frame. return -1 if
   unrecorrable error. */
static int wma_decode_block(WMADecodeContext *s)
{
    int n, v, a, ch, code, bsize;
    int coef_nb_bits, total_gain, parse_exponents;
    float window[BLOCK_MAX_SIZE * 2];
// XXX: FIXME!! there's a bug somewhere which makes this mandatory under altivec
#ifdef HAVE_ALTIVEC
    volatile int nb_coefs[MAX_CHANNELS] __attribute__((aligned(16)));
#else
    int nb_coefs[MAX_CHANNELS];
#endif
    float mdct_norm;

#ifdef TRACE
    tprintf("***decode_block: %d:%d\n", s->frame_count - 1, s->block_num);
#endif

    /* compute current block length */
    if (s->use_variable_block_len) {
        n = av_log2(s->nb_block_sizes - 1) + 1;
    
        if (s->reset_block_lengths) {
            s->reset_block_lengths = 0;
            v = get_bits(&s->gb, n);
            if (v >= s->nb_block_sizes)
                return -1;
            s->prev_block_len_bits = s->frame_len_bits - v;
            v = get_bits(&s->gb, n);
            if (v >= s->nb_block_sizes)
                return -1;
            s->block_len_bits = s->frame_len_bits - v;
        } else {
            /* update block lengths */
            s->prev_block_len_bits = s->block_len_bits;
            s->block_len_bits = s->next_block_len_bits;
        }
        v = get_bits(&s->gb, n);
        if (v >= s->nb_block_sizes)
            return -1;
        s->next_block_len_bits = s->frame_len_bits - v;
    } else {
        /* fixed block len */
        s->next_block_len_bits = s->frame_len_bits;
        s->prev_block_len_bits = s->frame_len_bits;
        s->block_len_bits = s->frame_len_bits;
    }

    /* now check if the block length is coherent with the frame length */
    s->block_len = 1 << s->block_len_bits;
    if ((s->block_pos + s->block_len) > s->frame_len)
        return -1;

    if (s->nb_channels == 2) {
        s->ms_stereo = get_bits(&s->gb, 1);
    }
    v = 0;
    for(ch = 0; ch < s->nb_channels; ch++) {
        a = get_bits(&s->gb, 1);
        s->channel_coded[ch] = a;
        v |= a;
    }
    /* if no channel coded, no need to go further */
    /* XXX: fix potential framing problems */
    if (!v)
        goto next;

    bsize = s->frame_len_bits - s->block_len_bits;

    /* read total gain and extract corresponding number of bits for
       coef escape coding */
    total_gain = 1;
    for(;;) {
        a = get_bits(&s->gb, 7);
        total_gain += a;
        if (a != 127)
            break;
    }
    
    if (total_gain < 15)
        coef_nb_bits = 13;
    else if (total_gain < 32)
        coef_nb_bits = 12;
    else if (total_gain < 40)
        coef_nb_bits = 11;
    else if (total_gain < 45)
        coef_nb_bits = 10;
    else
        coef_nb_bits = 9;

    /* compute number of coefficients */
    n = s->coefs_end[bsize] - s->coefs_start;
    for(ch = 0; ch < s->nb_channels; ch++)
        nb_coefs[ch] = n;

    /* complex coding */
    if (s->use_noise_coding) {

        for(ch = 0; ch < s->nb_channels; ch++) {
            if (s->channel_coded[ch]) {
                int i, n, a;
                n = s->exponent_high_sizes[bsize];
                for(i=0;i<n;i++) {
                    a = get_bits(&s->gb, 1);
                    s->high_band_coded[ch][i] = a;
                    /* if noise coding, the coefficients are not transmitted */
                    if (a)
                        nb_coefs[ch] -= s->exponent_high_bands[bsize][i];
                }
            }
        }
        for(ch = 0; ch < s->nb_channels; ch++) {
            if (s->channel_coded[ch]) {
                int i, n, val, code;

                n = s->exponent_high_sizes[bsize];
                val = (int)0x80000000;
                for(i=0;i<n;i++) {
                    if (s->high_band_coded[ch][i]) {
                        if (val == (int)0x80000000) {
                            val = get_bits(&s->gb, 7) - 19;
                        } else {
                            code = get_vlc(&s->gb, &s->hgain_vlc);
                            if (code < 0)
                                return -1;
                            val += code - 18;
                        }
                        s->high_band_values[ch][i] = val;
                    }
                }
            }
        }
    }
           
    /* exposant can be interpolated in short blocks. */
    parse_exponents = 1;
    if (s->block_len_bits != s->frame_len_bits) {
        parse_exponents = get_bits(&s->gb, 1);
    }
    
    if (parse_exponents) {
        for(ch = 0; ch < s->nb_channels; ch++) {
            if (s->channel_coded[ch]) {
                if (s->use_exp_vlc) {
                    if (decode_exp_vlc(s, ch) < 0)
                        return -1;
                } else {
                    decode_exp_lsp(s, ch);
                }
            }
        }
    } else {
        for(ch = 0; ch < s->nb_channels; ch++) {
            if (s->channel_coded[ch]) {
                interpolate_array(s->exponents[ch], 1 << s->prev_block_len_bits, 
                                  s->block_len);
            }
        }
    }

    /* parse spectral coefficients : just RLE encoding */
    for(ch = 0; ch < s->nb_channels; ch++) {
        if (s->channel_coded[ch]) {
            VLC *coef_vlc;
            int level, run, sign, tindex;
            int16_t *ptr, *eptr;
            const int16_t *level_table, *run_table;

            /* special VLC tables are used for ms stereo because
               there is potentially less energy there */
            tindex = (ch == 1 && s->ms_stereo);
            coef_vlc = &s->coef_vlc[tindex];
            run_table = s->run_table[tindex];
            level_table = s->level_table[tindex];
            /* XXX: optimize */
            ptr = &s->coefs1[ch][0];
            eptr = ptr + nb_coefs[ch];
            memset(ptr, 0, s->block_len * sizeof(int16_t));
            for(;;) {
                code = get_vlc(&s->gb, coef_vlc);
                if (code < 0)
                    return -1;
                if (code == 1) {
                    /* EOB */
                    break;
                } else if (code == 0) {
                    /* escape */
                    level = get_bits(&s->gb, coef_nb_bits);
                    /* NOTE: this is rather suboptimal. reading
                       block_len_bits would be better */
                    run = get_bits(&s->gb, s->frame_len_bits);
                } else {
                    /* normal code */
                    run = run_table[code];
                    level = level_table[code];
                }
                sign = get_bits(&s->gb, 1);
                if (!sign)
                    level = -level;
                ptr += run;
                if (ptr >= eptr)
                    return -1;
                *ptr++ = level;
                /* NOTE: EOB can be omitted */
                if (ptr >= eptr)
                    break;
            }
        }
        if (s->version == 1 && s->nb_channels >= 2) {
            align_get_bits(&s->gb);
        }
    }
     
    /* normalize */
    {
        int n4 = s->block_len / 2;
        mdct_norm = 1.0 / (float)n4;
        if (s->version == 1) {
            mdct_norm *= sqrt(n4);
        }
    }

    /* finally compute the MDCT coefficients */
    for(ch = 0; ch < s->nb_channels; ch++) {
        if (s->channel_coded[ch]) {
            int16_t *coefs1;
            float *coefs, *exponents, mult, mult1, noise, *exp_ptr;
            int i, j, n, n1, last_high_band;
            float exp_power[HIGH_BAND_MAX_SIZE];

            coefs1 = s->coefs1[ch];
            exponents = s->exponents[ch];
            mult = pow(10, total_gain * 0.05) / s->max_exponent[ch];
            mult *= mdct_norm;
            coefs = s->coefs[ch];
            if (s->use_noise_coding) {
                mult1 = mult;
                /* very low freqs : noise */
                for(i = 0;i < s->coefs_start; i++) {
                    *coefs++ = s->noise_table[s->noise_index] * (*exponents++) * mult1;
                    s->noise_index = (s->noise_index + 1) & (NOISE_TAB_SIZE - 1);
                }
                
                n1 = s->exponent_high_sizes[bsize];

                /* compute power of high bands */
                exp_ptr = exponents + 
                    s->high_band_start[bsize] - 
                    s->coefs_start;
                last_high_band = 0; /* avoid warning */
                for(j=0;j<n1;j++) {
                    n = s->exponent_high_bands[s->frame_len_bits - 
                                              s->block_len_bits][j];
                    if (s->high_band_coded[ch][j]) {
                        float e2, v;
                        e2 = 0;
                        for(i = 0;i < n; i++) {
                            v = exp_ptr[i];
                            e2 += v * v;
                        }
                        exp_power[j] = e2 / n;
                        last_high_band = j;
                        tprintf("%d: power=%f (%d)\n", j, exp_power[j], n);
                    }
                    exp_ptr += n;
                }

                /* main freqs and high freqs */
                for(j=-1;j<n1;j++) {
                    if (j < 0) {
                        n = s->high_band_start[bsize] - 
                            s->coefs_start;
                    } else {
                        n = s->exponent_high_bands[s->frame_len_bits - 
                                                  s->block_len_bits][j];
                    }
                    if (j >= 0 && s->high_band_coded[ch][j]) {
                        /* use noise with specified power */
                        mult1 = sqrt(exp_power[j] / exp_power[last_high_band]);
                        /* XXX: use a table */
                        mult1 = mult1 * pow(10, s->high_band_values[ch][j] * 0.05);
                        mult1 = mult1 / (s->max_exponent[ch] * s->noise_mult);
                        mult1 *= mdct_norm;
                        for(i = 0;i < n; i++) {
                            noise = s->noise_table[s->noise_index];
                            s->noise_index = (s->noise_index + 1) & (NOISE_TAB_SIZE - 1);
                            *coefs++ = (*exponents++) * noise * mult1;
                        }
                    } else {
                        /* coded values + small noise */
                        for(i = 0;i < n; i++) {
                            noise = s->noise_table[s->noise_index];
                            s->noise_index = (s->noise_index + 1) & (NOISE_TAB_SIZE - 1);
                            *coefs++ = ((*coefs1++) + noise) * (*exponents++) * mult;
                        }
                    }
                }

                /* very high freqs : noise */
                n = s->block_len - s->coefs_end[bsize];
                mult1 = mult * exponents[-1];
                for(i = 0; i < n; i++) {
                    *coefs++ = s->noise_table[s->noise_index] * mult1;
                    s->noise_index = (s->noise_index + 1) & (NOISE_TAB_SIZE - 1);
                }
            } else {
                /* XXX: optimize more */
                for(i = 0;i < s->coefs_start; i++)
                    *coefs++ = 0.0;
                n = nb_coefs[ch];
                for(i = 0;i < n; i++) {
                    *coefs++ = coefs1[i] * exponents[i] * mult;
                }
                n = s->block_len - s->coefs_end[bsize];
                for(i = 0;i < n; i++)
                    *coefs++ = 0.0;
            }
        }
    }

#ifdef TRACE
    for(ch = 0; ch < s->nb_channels; ch++) {
        if (s->channel_coded[ch]) {
            dump_floats("exponents", 3, s->exponents[ch], s->block_len);
            dump_floats("coefs", 1, s->coefs[ch], s->block_len);
        }
    }
#endif
    
    if (s->ms_stereo && s->channel_coded[1]) {
        float a, b;
        int i;

        /* nominal case for ms stereo: we do it before mdct */
        /* no need to optimize this case because it should almost
           never happen */
        if (!s->channel_coded[0]) {
            tprintf("rare ms-stereo case happened\n");
            memset(s->coefs[0], 0, sizeof(float) * s->block_len);
            s->channel_coded[0] = 1;
        }
        
        for(i = 0; i < s->block_len; i++) {
            a = s->coefs[0][i];
            b = s->coefs[1][i];
            s->coefs[0][i] = a + b;
            s->coefs[1][i] = a - b;
        }
    }

    /* build the window : we ensure that when the windows overlap
       their squared sum is always 1 (MDCT reconstruction rule) */
    /* XXX: merge with output */
    {
        int i, next_block_len, block_len, prev_block_len, n;
        float *wptr;

        block_len = s->block_len;
        prev_block_len = 1 << s->prev_block_len_bits;
        next_block_len = 1 << s->next_block_len_bits;

        /* right part */
        wptr = window + block_len;
        if (block_len <= next_block_len) {
            for(i=0;i<block_len;i++)
                *wptr++ = s->windows[bsize][i];
        } else {
            /* overlap */
            n = (block_len / 2) - (next_block_len / 2);
            for(i=0;i<n;i++)
                *wptr++ = 1.0;
            for(i=0;i<next_block_len;i++)
                *wptr++ = s->windows[s->frame_len_bits - s->next_block_len_bits][i];
            for(i=0;i<n;i++)
                *wptr++ = 0.0;
        }

        /* left part */
        wptr = window + block_len;
        if (block_len <= prev_block_len) {
            for(i=0;i<block_len;i++)
                *--wptr = s->windows[bsize][i];
        } else {
            /* overlap */
            n = (block_len / 2) - (prev_block_len / 2);
            for(i=0;i<n;i++)
                *--wptr = 1.0;
            for(i=0;i<prev_block_len;i++)
                *--wptr = s->windows[s->frame_len_bits - s->prev_block_len_bits][i];
            for(i=0;i<n;i++)
                *--wptr = 0.0;
        }
    }

    
    for(ch = 0; ch < s->nb_channels; ch++) {
        if (s->channel_coded[ch]) {
            FFTSample output[BLOCK_MAX_SIZE * 2] __attribute__((aligned(16)));
            float *ptr;
            int i, n4, index, n;

            n = s->block_len;
            n4 = s->block_len / 2;
            ff_imdct_calc(&s->mdct_ctx[bsize], 
                          output, s->coefs[ch], s->mdct_tmp);

            /* XXX: optimize all that by build the window and
               multipying/adding at the same time */
            /* multiply by the window */
            for(i=0;i<n * 2;i++) {
                output[i] *= window[i];
            }

            /* add in the frame */
            index = (s->frame_len / 2) + s->block_pos - n4;
            ptr = &s->frame_out[ch][index];
            for(i=0;i<n * 2;i++) {
                *ptr += output[i];
                ptr++;
            }

            /* specific fast case for ms-stereo : add to second
               channel if it is not coded */
            if (s->ms_stereo && !s->channel_coded[1]) {
                ptr = &s->frame_out[1][index];
                for(i=0;i<n * 2;i++) {
                    *ptr += output[i];
                    ptr++;
                }
            }
        }
    }
 next:
    /* update block number */
    s->block_num++;
    s->block_pos += s->block_len;
    if (s->block_pos >= s->frame_len)
        return 1;
    else
        return 0;
}

/* decode a frame of frame_len samples */
static int wma_decode_frame(WMADecodeContext *s, int16_t *samples)
{
    int ret, i, n, a, ch, incr;
    int16_t *ptr;
    float *iptr;

#ifdef TRACE
    tprintf("***decode_frame: %d size=%d\n", s->frame_count++, s->frame_len);
#endif

    /* read each block */
    s->block_num = 0;
    s->block_pos = 0;
    for(;;) {
        ret = wma_decode_block(s);
        if (ret < 0) 
            return -1;
        if (ret)
            break;
    }

    /* convert frame to integer */
    n = s->frame_len;
    incr = s->nb_channels;
    for(ch = 0; ch < s->nb_channels; ch++) {
        ptr = samples + ch;
        iptr = s->frame_out[ch];

        for(i=0;i<n;i++) {
            a = lrintf(*iptr++);
            if (a > 32767)
                a = 32767;
            else if (a < -32768)
                a = -32768;
            *ptr = a;
            ptr += incr;
        }
        /* prepare for next block */
        memmove(&s->frame_out[ch][0], &s->frame_out[ch][s->frame_len],
                s->frame_len * sizeof(float));
        /* XXX: suppress this */
        memset(&s->frame_out[ch][s->frame_len], 0, 
               s->frame_len * sizeof(float));
    }

#ifdef TRACE
    dump_shorts("samples", samples, n * s->nb_channels);
#endif
    return 0;
}

static int wma_decode_superframe(AVCodecContext *avctx, 
                                 void *data, int *data_size,
                                 uint8_t *buf, int buf_size)
{
    WMADecodeContext *s = avctx->priv_data;
    int nb_frames, bit_offset, i, pos, len;
    uint8_t *q;
    int16_t *samples;
    
    tprintf("***decode_superframe:\n");

    if(buf_size==0){
        s->last_superframe_len = 0;
        return 0;
    }
    
    samples = data;

    init_get_bits(&s->gb, buf, buf_size*8);
    
    if (s->use_bit_reservoir) {
        /* read super frame header */
        get_bits(&s->gb, 4); /* super frame index */
        nb_frames = get_bits(&s->gb, 4) - 1;

        bit_offset = get_bits(&s->gb, s->byte_offset_bits + 3);

        if (s->last_superframe_len > 0) {
            //        printf("skip=%d\n", s->last_bitoffset);
            /* add bit_offset bits to last frame */
            if ((s->last_superframe_len + ((bit_offset + 7) >> 3)) > 
                MAX_CODED_SUPERFRAME_SIZE)
                goto fail;
            q = s->last_superframe + s->last_superframe_len;
            len = bit_offset;
            while (len > 0) {
                *q++ = (get_bits)(&s->gb, 8);
                len -= 8;
            }
            if (len > 0) {
                *q++ = (get_bits)(&s->gb, len) << (8 - len);
            }
            
            /* XXX: bit_offset bits into last frame */
            init_get_bits(&s->gb, s->last_superframe, MAX_CODED_SUPERFRAME_SIZE*8);
            /* skip unused bits */
            if (s->last_bitoffset > 0)
                skip_bits(&s->gb, s->last_bitoffset);
            /* this frame is stored in the last superframe and in the
               current one */
            if (wma_decode_frame(s, samples) < 0)
                goto fail;
            samples += s->nb_channels * s->frame_len;
        }

        /* read each frame starting from bit_offset */
        pos = bit_offset + 4 + 4 + s->byte_offset_bits + 3;
        init_get_bits(&s->gb, buf + (pos >> 3), (MAX_CODED_SUPERFRAME_SIZE - (pos >> 3))*8);
        len = pos & 7;
        if (len > 0)
            skip_bits(&s->gb, len);
    
        s->reset_block_lengths = 1;
        for(i=0;i<nb_frames;i++) {
            if (wma_decode_frame(s, samples) < 0)
                goto fail;
            samples += s->nb_channels * s->frame_len;
        }

        /* we copy the end of the frame in the last frame buffer */
        pos = get_bits_count(&s->gb) + ((bit_offset + 4 + 4 + s->byte_offset_bits + 3) & ~7);
        s->last_bitoffset = pos & 7;
        pos >>= 3;
        len = buf_size - pos;
        if (len > MAX_CODED_SUPERFRAME_SIZE || len < 0) {
            goto fail;
        }
        s->last_superframe_len = len;
        memcpy(s->last_superframe, buf + pos, len);
    } else {
        /* single frame decode */
        if (wma_decode_frame(s, samples) < 0)
            goto fail;
        samples += s->nb_channels * s->frame_len;
    }
    *data_size = (int8_t *)samples - (int8_t *)data;
    return s->block_align;
 fail:
    /* when error, we reset the bit reservoir */
    s->last_superframe_len = 0;
    return -1;
}

static int wma_decode_end(AVCodecContext *avctx)
{
    WMADecodeContext *s = avctx->priv_data;
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
    }
    
    return 0;
}

AVCodec wmav1_decoder =
{
    "wmav1",
    CODEC_TYPE_AUDIO,
    CODEC_ID_WMAV1,
    sizeof(WMADecodeContext),
    wma_decode_init,
    NULL,
    wma_decode_end,
    wma_decode_superframe,
};

AVCodec wmav2_decoder =
{
    "wmav2",
    CODEC_TYPE_AUDIO,
    CODEC_ID_WMAV2,
    sizeof(WMADecodeContext),
    wma_decode_init,
    NULL,
    wma_decode_end,
    wma_decode_superframe,
};
