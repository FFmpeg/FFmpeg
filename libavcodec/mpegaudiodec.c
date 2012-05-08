/*
 * MPEG Audio decoder
 * Copyright (c) 2001, 2002 Fabrice Bellard
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
 * MPEG Audio decoder
 */

#include "libavutil/audioconvert.h"
#include "avcodec.h"
#include "get_bits.h"
#include "mathops.h"
#include "mpegaudiodsp.h"
#include "dsputil.h"

/*
 * TODO:
 *  - test lsf / mpeg25 extensively.
 */

#include "mpegaudio.h"
#include "mpegaudiodecheader.h"

#define BACKSTEP_SIZE 512
#define EXTRABYTES 24
#define LAST_BUF_SIZE 2 * BACKSTEP_SIZE + EXTRABYTES

/* layer 3 "granule" */
typedef struct GranuleDef {
    uint8_t scfsi;
    int part2_3_length;
    int big_values;
    int global_gain;
    int scalefac_compress;
    uint8_t block_type;
    uint8_t switch_point;
    int table_select[3];
    int subblock_gain[3];
    uint8_t scalefac_scale;
    uint8_t count1table_select;
    int region_size[3]; /* number of huffman codes in each region */
    int preflag;
    int short_start, long_end; /* long/short band indexes */
    uint8_t scale_factors[40];
    DECLARE_ALIGNED(16, INTFLOAT, sb_hybrid)[SBLIMIT * 18]; /* 576 samples */
} GranuleDef;

typedef struct MPADecodeContext {
    MPA_DECODE_HEADER
    uint8_t last_buf[LAST_BUF_SIZE];
    int last_buf_size;
    /* next header (used in free format parsing) */
    uint32_t free_format_next_header;
    GetBitContext gb;
    GetBitContext in_gb;
    DECLARE_ALIGNED(32, MPA_INT, synth_buf)[MPA_MAX_CHANNELS][512 * 2];
    int synth_buf_offset[MPA_MAX_CHANNELS];
    DECLARE_ALIGNED(32, INTFLOAT, sb_samples)[MPA_MAX_CHANNELS][36][SBLIMIT];
    INTFLOAT mdct_buf[MPA_MAX_CHANNELS][SBLIMIT * 18]; /* previous samples, for layer 3 MDCT */
    GranuleDef granules[2][2]; /* Used in Layer 3 */
    int adu_mode; ///< 0 for standard mp3, 1 for adu formatted mp3
    int dither_state;
    int err_recognition;
    AVCodecContext* avctx;
    MPADSPContext mpadsp;
    DSPContext dsp;
    AVFrame frame;
} MPADecodeContext;

#if CONFIG_FLOAT
#   define SHR(a,b)       ((a)*(1.0f/(1<<(b))))
#   define FIXR_OLD(a)    ((int)((a) * FRAC_ONE + 0.5))
#   define FIXR(x)        ((float)(x))
#   define FIXHR(x)       ((float)(x))
#   define MULH3(x, y, s) ((s)*(y)*(x))
#   define MULLx(x, y, s) ((y)*(x))
#   define RENAME(a) a ## _float
#   define OUT_FMT AV_SAMPLE_FMT_FLT
#else
#   define SHR(a,b)       ((a)>>(b))
/* WARNING: only correct for positive numbers */
#   define FIXR_OLD(a)    ((int)((a) * FRAC_ONE + 0.5))
#   define FIXR(a)        ((int)((a) * FRAC_ONE + 0.5))
#   define FIXHR(a)       ((int)((a) * (1LL<<32) + 0.5))
#   define MULH3(x, y, s) MULH((s)*(x), y)
#   define MULLx(x, y, s) MULL(x,y,s)
#   define RENAME(a)      a ## _fixed
#   define OUT_FMT AV_SAMPLE_FMT_S16
#endif

/****************/

#define HEADER_SIZE 4

#include "mpegaudiodata.h"
#include "mpegaudiodectab.h"

/* vlc structure for decoding layer 3 huffman tables */
static VLC huff_vlc[16];
static VLC_TYPE huff_vlc_tables[
    0 + 128 + 128 + 128 + 130 + 128 + 154 + 166 +
  142 + 204 + 190 + 170 + 542 + 460 + 662 + 414
  ][2];
static const int huff_vlc_tables_sizes[16] = {
    0,  128,  128,  128,  130,  128,  154,  166,
  142,  204,  190,  170,  542,  460,  662,  414
};
static VLC huff_quad_vlc[2];
static VLC_TYPE  huff_quad_vlc_tables[128+16][2];
static const int huff_quad_vlc_tables_sizes[2] = { 128, 16 };
/* computed from band_size_long */
static uint16_t band_index_long[9][23];
#include "mpegaudio_tablegen.h"
/* intensity stereo coef table */
static INTFLOAT is_table[2][16];
static INTFLOAT is_table_lsf[2][2][16];
static INTFLOAT csa_table[8][4];

static int16_t division_tab3[1<<6 ];
static int16_t division_tab5[1<<8 ];
static int16_t division_tab9[1<<11];

static int16_t * const division_tabs[4] = {
    division_tab3, division_tab5, NULL, division_tab9
};

/* lower 2 bits: modulo 3, higher bits: shift */
static uint16_t scale_factor_modshift[64];
/* [i][j]:  2^(-j/3) * FRAC_ONE * 2^(i+2) / (2^(i+2) - 1) */
static int32_t scale_factor_mult[15][3];
/* mult table for layer 2 group quantization */

#define SCALE_GEN(v) \
{ FIXR_OLD(1.0 * (v)), FIXR_OLD(0.7937005259 * (v)), FIXR_OLD(0.6299605249 * (v)) }

static const int32_t scale_factor_mult2[3][3] = {
    SCALE_GEN(4.0 / 3.0), /* 3 steps */
    SCALE_GEN(4.0 / 5.0), /* 5 steps */
    SCALE_GEN(4.0 / 9.0), /* 9 steps */
};

/**
 * Convert region offsets to region sizes and truncate
 * size to big_values.
 */
static void ff_region_offset2size(GranuleDef *g)
{
    int i, k, j = 0;
    g->region_size[2] = 576 / 2;
    for (i = 0; i < 3; i++) {
        k = FFMIN(g->region_size[i], g->big_values);
        g->region_size[i] = k - j;
        j = k;
    }
}

static void ff_init_short_region(MPADecodeContext *s, GranuleDef *g)
{
    if (g->block_type == 2)
        g->region_size[0] = (36 / 2);
    else {
        if (s->sample_rate_index <= 2)
            g->region_size[0] = (36 / 2);
        else if (s->sample_rate_index != 8)
            g->region_size[0] = (54 / 2);
        else
            g->region_size[0] = (108 / 2);
    }
    g->region_size[1] = (576 / 2);
}

static void ff_init_long_region(MPADecodeContext *s, GranuleDef *g, int ra1, int ra2)
{
    int l;
    g->region_size[0] = band_index_long[s->sample_rate_index][ra1 + 1] >> 1;
    /* should not overflow */
    l = FFMIN(ra1 + ra2 + 2, 22);
    g->region_size[1] = band_index_long[s->sample_rate_index][      l] >> 1;
}

static void ff_compute_band_indexes(MPADecodeContext *s, GranuleDef *g)
{
    if (g->block_type == 2) {
        if (g->switch_point) {
            /* if switched mode, we handle the 36 first samples as
                long blocks.  For 8000Hz, we handle the 48 first
                exponents as long blocks (XXX: check this!) */
            if (s->sample_rate_index <= 2)
                g->long_end = 8;
            else if (s->sample_rate_index != 8)
                g->long_end = 6;
            else
                g->long_end = 4; /* 8000 Hz */

            g->short_start = 2 + (s->sample_rate_index != 8);
        } else {
            g->long_end    = 0;
            g->short_start = 0;
        }
    } else {
        g->short_start = 13;
        g->long_end    = 22;
    }
}

/* layer 1 unscaling */
/* n = number of bits of the mantissa minus 1 */
static inline int l1_unscale(int n, int mant, int scale_factor)
{
    int shift, mod;
    int64_t val;

    shift   = scale_factor_modshift[scale_factor];
    mod     = shift & 3;
    shift >>= 2;
    val     = MUL64(mant + (-1 << n) + 1, scale_factor_mult[n-1][mod]);
    shift  += n;
    /* NOTE: at this point, 1 <= shift >= 21 + 15 */
    return (int)((val + (1LL << (shift - 1))) >> shift);
}

static inline int l2_unscale_group(int steps, int mant, int scale_factor)
{
    int shift, mod, val;

    shift   = scale_factor_modshift[scale_factor];
    mod     = shift & 3;
    shift >>= 2;

    val = (mant - (steps >> 1)) * scale_factor_mult2[steps >> 2][mod];
    /* NOTE: at this point, 0 <= shift <= 21 */
    if (shift > 0)
        val = (val + (1 << (shift - 1))) >> shift;
    return val;
}

/* compute value^(4/3) * 2^(exponent/4). It normalized to FRAC_BITS */
static inline int l3_unscale(int value, int exponent)
{
    unsigned int m;
    int e;

    e  = table_4_3_exp  [4 * value + (exponent & 3)];
    m  = table_4_3_value[4 * value + (exponent & 3)];
    e -= exponent >> 2;
    assert(e >= 1);
    if (e > 31)
        return 0;
    m = (m + (1 << (e - 1))) >> e;

    return m;
}

static av_cold void decode_init_static(void)
{
    int i, j, k;
    int offset;

    /* scale factors table for layer 1/2 */
    for (i = 0; i < 64; i++) {
        int shift, mod;
        /* 1.0 (i = 3) is normalized to 2 ^ FRAC_BITS */
        shift = i / 3;
        mod   = i % 3;
        scale_factor_modshift[i] = mod | (shift << 2);
    }

    /* scale factor multiply for layer 1 */
    for (i = 0; i < 15; i++) {
        int n, norm;
        n = i + 2;
        norm = ((INT64_C(1) << n) * FRAC_ONE) / ((1 << n) - 1);
        scale_factor_mult[i][0] = MULLx(norm, FIXR(1.0          * 2.0), FRAC_BITS);
        scale_factor_mult[i][1] = MULLx(norm, FIXR(0.7937005259 * 2.0), FRAC_BITS);
        scale_factor_mult[i][2] = MULLx(norm, FIXR(0.6299605249 * 2.0), FRAC_BITS);
        av_dlog(NULL, "%d: norm=%x s=%x %x %x\n", i, norm,
                scale_factor_mult[i][0],
                scale_factor_mult[i][1],
                scale_factor_mult[i][2]);
    }

    RENAME(ff_mpa_synth_init)(RENAME(ff_mpa_synth_window));

    /* huffman decode tables */
    offset = 0;
    for (i = 1; i < 16; i++) {
        const HuffTable *h = &mpa_huff_tables[i];
        int xsize, x, y;
        uint8_t  tmp_bits [512] = { 0 };
        uint16_t tmp_codes[512] = { 0 };

        xsize = h->xsize;

        j = 0;
        for (x = 0; x < xsize; x++) {
            for (y = 0; y < xsize; y++) {
                tmp_bits [(x << 5) | y | ((x&&y)<<4)]= h->bits [j  ];
                tmp_codes[(x << 5) | y | ((x&&y)<<4)]= h->codes[j++];
            }
        }

        /* XXX: fail test */
        huff_vlc[i].table = huff_vlc_tables+offset;
        huff_vlc[i].table_allocated = huff_vlc_tables_sizes[i];
        init_vlc(&huff_vlc[i], 7, 512,
                 tmp_bits, 1, 1, tmp_codes, 2, 2,
                 INIT_VLC_USE_NEW_STATIC);
        offset += huff_vlc_tables_sizes[i];
    }
    assert(offset == FF_ARRAY_ELEMS(huff_vlc_tables));

    offset = 0;
    for (i = 0; i < 2; i++) {
        huff_quad_vlc[i].table = huff_quad_vlc_tables+offset;
        huff_quad_vlc[i].table_allocated = huff_quad_vlc_tables_sizes[i];
        init_vlc(&huff_quad_vlc[i], i == 0 ? 7 : 4, 16,
                 mpa_quad_bits[i], 1, 1, mpa_quad_codes[i], 1, 1,
                 INIT_VLC_USE_NEW_STATIC);
        offset += huff_quad_vlc_tables_sizes[i];
    }
    assert(offset == FF_ARRAY_ELEMS(huff_quad_vlc_tables));

    for (i = 0; i < 9; i++) {
        k = 0;
        for (j = 0; j < 22; j++) {
            band_index_long[i][j] = k;
            k += band_size_long[i][j];
        }
        band_index_long[i][22] = k;
    }

    /* compute n ^ (4/3) and store it in mantissa/exp format */

    mpegaudio_tableinit();

    for (i = 0; i < 4; i++) {
        if (ff_mpa_quant_bits[i] < 0) {
            for (j = 0; j < (1 << (-ff_mpa_quant_bits[i]+1)); j++) {
                int val1, val2, val3, steps;
                int val = j;
                steps   = ff_mpa_quant_steps[i];
                val1    = val % steps;
                val    /= steps;
                val2    = val % steps;
                val3    = val / steps;
                division_tabs[i][j] = val1 + (val2 << 4) + (val3 << 8);
            }
        }
    }


    for (i = 0; i < 7; i++) {
        float f;
        INTFLOAT v;
        if (i != 6) {
            f = tan((double)i * M_PI / 12.0);
            v = FIXR(f / (1.0 + f));
        } else {
            v = FIXR(1.0);
        }
        is_table[0][    i] = v;
        is_table[1][6 - i] = v;
    }
    /* invalid values */
    for (i = 7; i < 16; i++)
        is_table[0][i] = is_table[1][i] = 0.0;

    for (i = 0; i < 16; i++) {
        double f;
        int e, k;

        for (j = 0; j < 2; j++) {
            e = -(j + 1) * ((i + 1) >> 1);
            f = pow(2.0, e / 4.0);
            k = i & 1;
            is_table_lsf[j][k ^ 1][i] = FIXR(f);
            is_table_lsf[j][k    ][i] = FIXR(1.0);
            av_dlog(NULL, "is_table_lsf %d %d: %f %f\n",
                    i, j, (float) is_table_lsf[j][0][i],
                    (float) is_table_lsf[j][1][i]);
        }
    }

    for (i = 0; i < 8; i++) {
        float ci, cs, ca;
        ci = ci_table[i];
        cs = 1.0 / sqrt(1.0 + ci * ci);
        ca = cs * ci;
#if !CONFIG_FLOAT
        csa_table[i][0] = FIXHR(cs/4);
        csa_table[i][1] = FIXHR(ca/4);
        csa_table[i][2] = FIXHR(ca/4) + FIXHR(cs/4);
        csa_table[i][3] = FIXHR(ca/4) - FIXHR(cs/4);
#else
        csa_table[i][0] = cs;
        csa_table[i][1] = ca;
        csa_table[i][2] = ca + cs;
        csa_table[i][3] = ca - cs;
#endif
    }
}

static av_cold int decode_init(AVCodecContext * avctx)
{
    static int initialized_tables = 0;
    MPADecodeContext *s = avctx->priv_data;

    if (!initialized_tables) {
        decode_init_static();
        initialized_tables = 1;
    }

    s->avctx = avctx;

    ff_mpadsp_init(&s->mpadsp);
    ff_dsputil_init(&s->dsp, avctx);

    avctx->sample_fmt= OUT_FMT;
    s->err_recognition = avctx->err_recognition;

    if (avctx->codec_id == CODEC_ID_MP3ADU)
        s->adu_mode = 1;

    avcodec_get_frame_defaults(&s->frame);
    avctx->coded_frame = &s->frame;

    return 0;
}

#define C3 FIXHR(0.86602540378443864676/2)
#define C4 FIXHR(0.70710678118654752439/2) //0.5 / cos(pi*(9)/36)
#define C5 FIXHR(0.51763809020504152469/2) //0.5 / cos(pi*(5)/36)
#define C6 FIXHR(1.93185165257813657349/4) //0.5 / cos(pi*(15)/36)

/* 12 points IMDCT. We compute it "by hand" by factorizing obvious
   cases. */
static void imdct12(INTFLOAT *out, INTFLOAT *in)
{
    INTFLOAT in0, in1, in2, in3, in4, in5, t1, t2;

    in0  = in[0*3];
    in1  = in[1*3] + in[0*3];
    in2  = in[2*3] + in[1*3];
    in3  = in[3*3] + in[2*3];
    in4  = in[4*3] + in[3*3];
    in5  = in[5*3] + in[4*3];
    in5 += in3;
    in3 += in1;

    in2  = MULH3(in2, C3, 2);
    in3  = MULH3(in3, C3, 4);

    t1   = in0 - in4;
    t2   = MULH3(in1 - in5, C4, 2);

    out[ 7] =
    out[10] = t1 + t2;
    out[ 1] =
    out[ 4] = t1 - t2;

    in0    += SHR(in4, 1);
    in4     = in0 + in2;
    in5    += 2*in1;
    in1     = MULH3(in5 + in3, C5, 1);
    out[ 8] =
    out[ 9] = in4 + in1;
    out[ 2] =
    out[ 3] = in4 - in1;

    in0    -= in2;
    in5     = MULH3(in5 - in3, C6, 2);
    out[ 0] =
    out[ 5] = in0 - in5;
    out[ 6] =
    out[11] = in0 + in5;
}

/* return the number of decoded frames */
static int mp_decode_layer1(MPADecodeContext *s)
{
    int bound, i, v, n, ch, j, mant;
    uint8_t allocation[MPA_MAX_CHANNELS][SBLIMIT];
    uint8_t scale_factors[MPA_MAX_CHANNELS][SBLIMIT];

    if (s->mode == MPA_JSTEREO)
        bound = (s->mode_ext + 1) * 4;
    else
        bound = SBLIMIT;

    /* allocation bits */
    for (i = 0; i < bound; i++) {
        for (ch = 0; ch < s->nb_channels; ch++) {
            allocation[ch][i] = get_bits(&s->gb, 4);
        }
    }
    for (i = bound; i < SBLIMIT; i++)
        allocation[0][i] = get_bits(&s->gb, 4);

    /* scale factors */
    for (i = 0; i < bound; i++) {
        for (ch = 0; ch < s->nb_channels; ch++) {
            if (allocation[ch][i])
                scale_factors[ch][i] = get_bits(&s->gb, 6);
        }
    }
    for (i = bound; i < SBLIMIT; i++) {
        if (allocation[0][i]) {
            scale_factors[0][i] = get_bits(&s->gb, 6);
            scale_factors[1][i] = get_bits(&s->gb, 6);
        }
    }

    /* compute samples */
    for (j = 0; j < 12; j++) {
        for (i = 0; i < bound; i++) {
            for (ch = 0; ch < s->nb_channels; ch++) {
                n = allocation[ch][i];
                if (n) {
                    mant = get_bits(&s->gb, n + 1);
                    v = l1_unscale(n, mant, scale_factors[ch][i]);
                } else {
                    v = 0;
                }
                s->sb_samples[ch][j][i] = v;
            }
        }
        for (i = bound; i < SBLIMIT; i++) {
            n = allocation[0][i];
            if (n) {
                mant = get_bits(&s->gb, n + 1);
                v = l1_unscale(n, mant, scale_factors[0][i]);
                s->sb_samples[0][j][i] = v;
                v = l1_unscale(n, mant, scale_factors[1][i]);
                s->sb_samples[1][j][i] = v;
            } else {
                s->sb_samples[0][j][i] = 0;
                s->sb_samples[1][j][i] = 0;
            }
        }
    }
    return 12;
}

static int mp_decode_layer2(MPADecodeContext *s)
{
    int sblimit; /* number of used subbands */
    const unsigned char *alloc_table;
    int table, bit_alloc_bits, i, j, ch, bound, v;
    unsigned char bit_alloc[MPA_MAX_CHANNELS][SBLIMIT];
    unsigned char scale_code[MPA_MAX_CHANNELS][SBLIMIT];
    unsigned char scale_factors[MPA_MAX_CHANNELS][SBLIMIT][3], *sf;
    int scale, qindex, bits, steps, k, l, m, b;

    /* select decoding table */
    table = ff_mpa_l2_select_table(s->bit_rate / 1000, s->nb_channels,
                                   s->sample_rate, s->lsf);
    sblimit     = ff_mpa_sblimit_table[table];
    alloc_table = ff_mpa_alloc_tables[table];

    if (s->mode == MPA_JSTEREO)
        bound = (s->mode_ext + 1) * 4;
    else
        bound = sblimit;

    av_dlog(s->avctx, "bound=%d sblimit=%d\n", bound, sblimit);

    /* sanity check */
    if (bound > sblimit)
        bound = sblimit;

    /* parse bit allocation */
    j = 0;
    for (i = 0; i < bound; i++) {
        bit_alloc_bits = alloc_table[j];
        for (ch = 0; ch < s->nb_channels; ch++)
            bit_alloc[ch][i] = get_bits(&s->gb, bit_alloc_bits);
        j += 1 << bit_alloc_bits;
    }
    for (i = bound; i < sblimit; i++) {
        bit_alloc_bits = alloc_table[j];
        v = get_bits(&s->gb, bit_alloc_bits);
        bit_alloc[0][i] = v;
        bit_alloc[1][i] = v;
        j += 1 << bit_alloc_bits;
    }

    /* scale codes */
    for (i = 0; i < sblimit; i++) {
        for (ch = 0; ch < s->nb_channels; ch++) {
            if (bit_alloc[ch][i])
                scale_code[ch][i] = get_bits(&s->gb, 2);
        }
    }

    /* scale factors */
    for (i = 0; i < sblimit; i++) {
        for (ch = 0; ch < s->nb_channels; ch++) {
            if (bit_alloc[ch][i]) {
                sf = scale_factors[ch][i];
                switch (scale_code[ch][i]) {
                default:
                case 0:
                    sf[0] = get_bits(&s->gb, 6);
                    sf[1] = get_bits(&s->gb, 6);
                    sf[2] = get_bits(&s->gb, 6);
                    break;
                case 2:
                    sf[0] = get_bits(&s->gb, 6);
                    sf[1] = sf[0];
                    sf[2] = sf[0];
                    break;
                case 1:
                    sf[0] = get_bits(&s->gb, 6);
                    sf[2] = get_bits(&s->gb, 6);
                    sf[1] = sf[0];
                    break;
                case 3:
                    sf[0] = get_bits(&s->gb, 6);
                    sf[2] = get_bits(&s->gb, 6);
                    sf[1] = sf[2];
                    break;
                }
            }
        }
    }

    /* samples */
    for (k = 0; k < 3; k++) {
        for (l = 0; l < 12; l += 3) {
            j = 0;
            for (i = 0; i < bound; i++) {
                bit_alloc_bits = alloc_table[j];
                for (ch = 0; ch < s->nb_channels; ch++) {
                    b = bit_alloc[ch][i];
                    if (b) {
                        scale = scale_factors[ch][i][k];
                        qindex = alloc_table[j+b];
                        bits = ff_mpa_quant_bits[qindex];
                        if (bits < 0) {
                            int v2;
                            /* 3 values at the same time */
                            v = get_bits(&s->gb, -bits);
                            v2 = division_tabs[qindex][v];
                            steps  = ff_mpa_quant_steps[qindex];

                            s->sb_samples[ch][k * 12 + l + 0][i] =
                                l2_unscale_group(steps,  v2       & 15, scale);
                            s->sb_samples[ch][k * 12 + l + 1][i] =
                                l2_unscale_group(steps, (v2 >> 4) & 15, scale);
                            s->sb_samples[ch][k * 12 + l + 2][i] =
                                l2_unscale_group(steps,  v2 >> 8      , scale);
                        } else {
                            for (m = 0; m < 3; m++) {
                                v = get_bits(&s->gb, bits);
                                v = l1_unscale(bits - 1, v, scale);
                                s->sb_samples[ch][k * 12 + l + m][i] = v;
                            }
                        }
                    } else {
                        s->sb_samples[ch][k * 12 + l + 0][i] = 0;
                        s->sb_samples[ch][k * 12 + l + 1][i] = 0;
                        s->sb_samples[ch][k * 12 + l + 2][i] = 0;
                    }
                }
                /* next subband in alloc table */
                j += 1 << bit_alloc_bits;
            }
            /* XXX: find a way to avoid this duplication of code */
            for (i = bound; i < sblimit; i++) {
                bit_alloc_bits = alloc_table[j];
                b = bit_alloc[0][i];
                if (b) {
                    int mant, scale0, scale1;
                    scale0 = scale_factors[0][i][k];
                    scale1 = scale_factors[1][i][k];
                    qindex = alloc_table[j+b];
                    bits = ff_mpa_quant_bits[qindex];
                    if (bits < 0) {
                        /* 3 values at the same time */
                        v = get_bits(&s->gb, -bits);
                        steps = ff_mpa_quant_steps[qindex];
                        mant = v % steps;
                        v = v / steps;
                        s->sb_samples[0][k * 12 + l + 0][i] =
                            l2_unscale_group(steps, mant, scale0);
                        s->sb_samples[1][k * 12 + l + 0][i] =
                            l2_unscale_group(steps, mant, scale1);
                        mant = v % steps;
                        v = v / steps;
                        s->sb_samples[0][k * 12 + l + 1][i] =
                            l2_unscale_group(steps, mant, scale0);
                        s->sb_samples[1][k * 12 + l + 1][i] =
                            l2_unscale_group(steps, mant, scale1);
                        s->sb_samples[0][k * 12 + l + 2][i] =
                            l2_unscale_group(steps, v, scale0);
                        s->sb_samples[1][k * 12 + l + 2][i] =
                            l2_unscale_group(steps, v, scale1);
                    } else {
                        for (m = 0; m < 3; m++) {
                            mant = get_bits(&s->gb, bits);
                            s->sb_samples[0][k * 12 + l + m][i] =
                                l1_unscale(bits - 1, mant, scale0);
                            s->sb_samples[1][k * 12 + l + m][i] =
                                l1_unscale(bits - 1, mant, scale1);
                        }
                    }
                } else {
                    s->sb_samples[0][k * 12 + l + 0][i] = 0;
                    s->sb_samples[0][k * 12 + l + 1][i] = 0;
                    s->sb_samples[0][k * 12 + l + 2][i] = 0;
                    s->sb_samples[1][k * 12 + l + 0][i] = 0;
                    s->sb_samples[1][k * 12 + l + 1][i] = 0;
                    s->sb_samples[1][k * 12 + l + 2][i] = 0;
                }
                /* next subband in alloc table */
                j += 1 << bit_alloc_bits;
            }
            /* fill remaining samples to zero */
            for (i = sblimit; i < SBLIMIT; i++) {
                for (ch = 0; ch < s->nb_channels; ch++) {
                    s->sb_samples[ch][k * 12 + l + 0][i] = 0;
                    s->sb_samples[ch][k * 12 + l + 1][i] = 0;
                    s->sb_samples[ch][k * 12 + l + 2][i] = 0;
                }
            }
        }
    }
    return 3 * 12;
}

#define SPLIT(dst,sf,n)             \
    if (n == 3) {                   \
        int m = (sf * 171) >> 9;    \
        dst   = sf - 3 * m;         \
        sf    = m;                  \
    } else if (n == 4) {            \
        dst  = sf & 3;              \
        sf >>= 2;                   \
    } else if (n == 5) {            \
        int m = (sf * 205) >> 10;   \
        dst   = sf - 5 * m;         \
        sf    = m;                  \
    } else if (n == 6) {            \
        int m = (sf * 171) >> 10;   \
        dst   = sf - 6 * m;         \
        sf    = m;                  \
    } else {                        \
        dst = 0;                    \
    }

static av_always_inline void lsf_sf_expand(int *slen, int sf, int n1, int n2,
                                           int n3)
{
    SPLIT(slen[3], sf, n3)
    SPLIT(slen[2], sf, n2)
    SPLIT(slen[1], sf, n1)
    slen[0] = sf;
}

static void exponents_from_scale_factors(MPADecodeContext *s, GranuleDef *g,
                                         int16_t *exponents)
{
    const uint8_t *bstab, *pretab;
    int len, i, j, k, l, v0, shift, gain, gains[3];
    int16_t *exp_ptr;

    exp_ptr = exponents;
    gain    = g->global_gain - 210;
    shift   = g->scalefac_scale + 1;

    bstab  = band_size_long[s->sample_rate_index];
    pretab = mpa_pretab[g->preflag];
    for (i = 0; i < g->long_end; i++) {
        v0 = gain - ((g->scale_factors[i] + pretab[i]) << shift) + 400;
        len = bstab[i];
        for (j = len; j > 0; j--)
            *exp_ptr++ = v0;
    }

    if (g->short_start < 13) {
        bstab    = band_size_short[s->sample_rate_index];
        gains[0] = gain - (g->subblock_gain[0] << 3);
        gains[1] = gain - (g->subblock_gain[1] << 3);
        gains[2] = gain - (g->subblock_gain[2] << 3);
        k        = g->long_end;
        for (i = g->short_start; i < 13; i++) {
            len = bstab[i];
            for (l = 0; l < 3; l++) {
                v0 = gains[l] - (g->scale_factors[k++] << shift) + 400;
                for (j = len; j > 0; j--)
                    *exp_ptr++ = v0;
            }
        }
    }
}

/* handle n = 0 too */
static inline int get_bitsz(GetBitContext *s, int n)
{
    return n ? get_bits(s, n) : 0;
}


static void switch_buffer(MPADecodeContext *s, int *pos, int *end_pos,
                          int *end_pos2)
{
    if (s->in_gb.buffer && *pos >= s->gb.size_in_bits) {
        s->gb           = s->in_gb;
        s->in_gb.buffer = NULL;
        assert((get_bits_count(&s->gb) & 7) == 0);
        skip_bits_long(&s->gb, *pos - *end_pos);
        *end_pos2 =
        *end_pos  = *end_pos2 + get_bits_count(&s->gb) - *pos;
        *pos      = get_bits_count(&s->gb);
    }
}

/* Following is a optimized code for
            INTFLOAT v = *src
            if(get_bits1(&s->gb))
                v = -v;
            *dst = v;
*/
#if CONFIG_FLOAT
#define READ_FLIP_SIGN(dst,src)                     \
    v = AV_RN32A(src) ^ (get_bits1(&s->gb) << 31);  \
    AV_WN32A(dst, v);
#else
#define READ_FLIP_SIGN(dst,src)     \
    v      = -get_bits1(&s->gb);    \
    *(dst) = (*(src) ^ v) - v;
#endif

static int huffman_decode(MPADecodeContext *s, GranuleDef *g,
                          int16_t *exponents, int end_pos2)
{
    int s_index;
    int i;
    int last_pos, bits_left;
    VLC *vlc;
    int end_pos = FFMIN(end_pos2, s->gb.size_in_bits);

    /* low frequencies (called big values) */
    s_index = 0;
    for (i = 0; i < 3; i++) {
        int j, k, l, linbits;
        j = g->region_size[i];
        if (j == 0)
            continue;
        /* select vlc table */
        k       = g->table_select[i];
        l       = mpa_huff_data[k][0];
        linbits = mpa_huff_data[k][1];
        vlc     = &huff_vlc[l];

        if (!l) {
            memset(&g->sb_hybrid[s_index], 0, sizeof(*g->sb_hybrid) * 2 * j);
            s_index += 2 * j;
            continue;
        }

        /* read huffcode and compute each couple */
        for (; j > 0; j--) {
            int exponent, x, y;
            int v;
            int pos = get_bits_count(&s->gb);

            if (pos >= end_pos){
//                av_log(NULL, AV_LOG_ERROR, "pos: %d %d %d %d\n", pos, end_pos, end_pos2, s_index);
                switch_buffer(s, &pos, &end_pos, &end_pos2);
//                av_log(NULL, AV_LOG_ERROR, "new pos: %d %d\n", pos, end_pos);
                if (pos >= end_pos)
                    break;
            }
            y = get_vlc2(&s->gb, vlc->table, 7, 3);

            if (!y) {
                g->sb_hybrid[s_index  ] =
                g->sb_hybrid[s_index+1] = 0;
                s_index += 2;
                continue;
            }

            exponent= exponents[s_index];

            av_dlog(s->avctx, "region=%d n=%d x=%d y=%d exp=%d\n",
                    i, g->region_size[i] - j, x, y, exponent);
            if (y & 16) {
                x = y >> 5;
                y = y & 0x0f;
                if (x < 15) {
                    READ_FLIP_SIGN(g->sb_hybrid + s_index, RENAME(expval_table)[exponent] + x)
                } else {
                    x += get_bitsz(&s->gb, linbits);
                    v  = l3_unscale(x, exponent);
                    if (get_bits1(&s->gb))
                        v = -v;
                    g->sb_hybrid[s_index] = v;
                }
                if (y < 15) {
                    READ_FLIP_SIGN(g->sb_hybrid + s_index + 1, RENAME(expval_table)[exponent] + y)
                } else {
                    y += get_bitsz(&s->gb, linbits);
                    v  = l3_unscale(y, exponent);
                    if (get_bits1(&s->gb))
                        v = -v;
                    g->sb_hybrid[s_index+1] = v;
                }
            } else {
                x = y >> 5;
                y = y & 0x0f;
                x += y;
                if (x < 15) {
                    READ_FLIP_SIGN(g->sb_hybrid + s_index + !!y, RENAME(expval_table)[exponent] + x)
                } else {
                    x += get_bitsz(&s->gb, linbits);
                    v  = l3_unscale(x, exponent);
                    if (get_bits1(&s->gb))
                        v = -v;
                    g->sb_hybrid[s_index+!!y] = v;
                }
                g->sb_hybrid[s_index + !y] = 0;
            }
            s_index += 2;
        }
    }

    /* high frequencies */
    vlc = &huff_quad_vlc[g->count1table_select];
    last_pos = 0;
    while (s_index <= 572) {
        int pos, code;
        pos = get_bits_count(&s->gb);
        if (pos >= end_pos) {
            if (pos > end_pos2 && last_pos) {
                /* some encoders generate an incorrect size for this
                   part. We must go back into the data */
                s_index -= 4;
                skip_bits_long(&s->gb, last_pos - pos);
                av_log(s->avctx, AV_LOG_INFO, "overread, skip %d enddists: %d %d\n", last_pos - pos, end_pos-pos, end_pos2-pos);
                if(s->err_recognition & (AV_EF_BITSTREAM|AV_EF_COMPLIANT))
                    s_index=0;
                break;
            }
//                av_log(NULL, AV_LOG_ERROR, "pos2: %d %d %d %d\n", pos, end_pos, end_pos2, s_index);
            switch_buffer(s, &pos, &end_pos, &end_pos2);
//                av_log(NULL, AV_LOG_ERROR, "new pos2: %d %d %d\n", pos, end_pos, s_index);
            if (pos >= end_pos)
                break;
        }
        last_pos = pos;

        code = get_vlc2(&s->gb, vlc->table, vlc->bits, 1);
        av_dlog(s->avctx, "t=%d code=%d\n", g->count1table_select, code);
        g->sb_hybrid[s_index+0] =
        g->sb_hybrid[s_index+1] =
        g->sb_hybrid[s_index+2] =
        g->sb_hybrid[s_index+3] = 0;
        while (code) {
            static const int idxtab[16] = { 3,3,2,2,1,1,1,1,0,0,0,0,0,0,0,0 };
            int v;
            int pos = s_index + idxtab[code];
            code   ^= 8 >> idxtab[code];
            READ_FLIP_SIGN(g->sb_hybrid + pos, RENAME(exp_table)+exponents[pos])
        }
        s_index += 4;
    }
    /* skip extension bits */
    bits_left = end_pos2 - get_bits_count(&s->gb);
//av_log(NULL, AV_LOG_ERROR, "left:%d buf:%p\n", bits_left, s->in_gb.buffer);
    if (bits_left < 0 && (s->err_recognition & (AV_EF_BUFFER|AV_EF_COMPLIANT))) {
        av_log(s->avctx, AV_LOG_ERROR, "bits_left=%d\n", bits_left);
        s_index=0;
    } else if (bits_left > 0 && (s->err_recognition & (AV_EF_BUFFER|AV_EF_AGGRESSIVE))) {
        av_log(s->avctx, AV_LOG_ERROR, "bits_left=%d\n", bits_left);
        s_index = 0;
    }
    memset(&g->sb_hybrid[s_index], 0, sizeof(*g->sb_hybrid) * (576 - s_index));
    skip_bits_long(&s->gb, bits_left);

    i = get_bits_count(&s->gb);
    switch_buffer(s, &i, &end_pos, &end_pos2);

    return 0;
}

/* Reorder short blocks from bitstream order to interleaved order. It
   would be faster to do it in parsing, but the code would be far more
   complicated */
static void reorder_block(MPADecodeContext *s, GranuleDef *g)
{
    int i, j, len;
    INTFLOAT *ptr, *dst, *ptr1;
    INTFLOAT tmp[576];

    if (g->block_type != 2)
        return;

    if (g->switch_point) {
        if (s->sample_rate_index != 8)
            ptr = g->sb_hybrid + 36;
        else
            ptr = g->sb_hybrid + 48;
    } else {
        ptr = g->sb_hybrid;
    }

    for (i = g->short_start; i < 13; i++) {
        len  = band_size_short[s->sample_rate_index][i];
        ptr1 = ptr;
        dst  = tmp;
        for (j = len; j > 0; j--) {
            *dst++ = ptr[0*len];
            *dst++ = ptr[1*len];
            *dst++ = ptr[2*len];
            ptr++;
        }
        ptr += 2 * len;
        memcpy(ptr1, tmp, len * 3 * sizeof(*ptr1));
    }
}

#define ISQRT2 FIXR(0.70710678118654752440)

static void compute_stereo(MPADecodeContext *s, GranuleDef *g0, GranuleDef *g1)
{
    int i, j, k, l;
    int sf_max, sf, len, non_zero_found;
    INTFLOAT (*is_tab)[16], *tab0, *tab1, tmp0, tmp1, v1, v2;
    int non_zero_found_short[3];

    /* intensity stereo */
    if (s->mode_ext & MODE_EXT_I_STEREO) {
        if (!s->lsf) {
            is_tab = is_table;
            sf_max = 7;
        } else {
            is_tab = is_table_lsf[g1->scalefac_compress & 1];
            sf_max = 16;
        }

        tab0 = g0->sb_hybrid + 576;
        tab1 = g1->sb_hybrid + 576;

        non_zero_found_short[0] = 0;
        non_zero_found_short[1] = 0;
        non_zero_found_short[2] = 0;
        k = (13 - g1->short_start) * 3 + g1->long_end - 3;
        for (i = 12; i >= g1->short_start; i--) {
            /* for last band, use previous scale factor */
            if (i != 11)
                k -= 3;
            len = band_size_short[s->sample_rate_index][i];
            for (l = 2; l >= 0; l--) {
                tab0 -= len;
                tab1 -= len;
                if (!non_zero_found_short[l]) {
                    /* test if non zero band. if so, stop doing i-stereo */
                    for (j = 0; j < len; j++) {
                        if (tab1[j] != 0) {
                            non_zero_found_short[l] = 1;
                            goto found1;
                        }
                    }
                    sf = g1->scale_factors[k + l];
                    if (sf >= sf_max)
                        goto found1;

                    v1 = is_tab[0][sf];
                    v2 = is_tab[1][sf];
                    for (j = 0; j < len; j++) {
                        tmp0    = tab0[j];
                        tab0[j] = MULLx(tmp0, v1, FRAC_BITS);
                        tab1[j] = MULLx(tmp0, v2, FRAC_BITS);
                    }
                } else {
found1:
                    if (s->mode_ext & MODE_EXT_MS_STEREO) {
                        /* lower part of the spectrum : do ms stereo
                           if enabled */
                        for (j = 0; j < len; j++) {
                            tmp0    = tab0[j];
                            tmp1    = tab1[j];
                            tab0[j] = MULLx(tmp0 + tmp1, ISQRT2, FRAC_BITS);
                            tab1[j] = MULLx(tmp0 - tmp1, ISQRT2, FRAC_BITS);
                        }
                    }
                }
            }
        }

        non_zero_found = non_zero_found_short[0] |
                         non_zero_found_short[1] |
                         non_zero_found_short[2];

        for (i = g1->long_end - 1;i >= 0;i--) {
            len   = band_size_long[s->sample_rate_index][i];
            tab0 -= len;
            tab1 -= len;
            /* test if non zero band. if so, stop doing i-stereo */
            if (!non_zero_found) {
                for (j = 0; j < len; j++) {
                    if (tab1[j] != 0) {
                        non_zero_found = 1;
                        goto found2;
                    }
                }
                /* for last band, use previous scale factor */
                k  = (i == 21) ? 20 : i;
                sf = g1->scale_factors[k];
                if (sf >= sf_max)
                    goto found2;
                v1 = is_tab[0][sf];
                v2 = is_tab[1][sf];
                for (j = 0; j < len; j++) {
                    tmp0    = tab0[j];
                    tab0[j] = MULLx(tmp0, v1, FRAC_BITS);
                    tab1[j] = MULLx(tmp0, v2, FRAC_BITS);
                }
            } else {
found2:
                if (s->mode_ext & MODE_EXT_MS_STEREO) {
                    /* lower part of the spectrum : do ms stereo
                       if enabled */
                    for (j = 0; j < len; j++) {
                        tmp0    = tab0[j];
                        tmp1    = tab1[j];
                        tab0[j] = MULLx(tmp0 + tmp1, ISQRT2, FRAC_BITS);
                        tab1[j] = MULLx(tmp0 - tmp1, ISQRT2, FRAC_BITS);
                    }
                }
            }
        }
    } else if (s->mode_ext & MODE_EXT_MS_STEREO) {
        /* ms stereo ONLY */
        /* NOTE: the 1/sqrt(2) normalization factor is included in the
           global gain */
#if CONFIG_FLOAT
       s-> dsp.butterflies_float(g0->sb_hybrid, g1->sb_hybrid, 576);
#else
        tab0 = g0->sb_hybrid;
        tab1 = g1->sb_hybrid;
        for (i = 0; i < 576; i++) {
            tmp0    = tab0[i];
            tmp1    = tab1[i];
            tab0[i] = tmp0 + tmp1;
            tab1[i] = tmp0 - tmp1;
        }
#endif
    }
}

#if CONFIG_FLOAT
#define AA(j) do {                                                      \
        float tmp0 = ptr[-1-j];                                         \
        float tmp1 = ptr[   j];                                         \
        ptr[-1-j] = tmp0 * csa_table[j][0] - tmp1 * csa_table[j][1];    \
        ptr[   j] = tmp0 * csa_table[j][1] + tmp1 * csa_table[j][0];    \
    } while (0)
#else
#define AA(j) do {                                              \
        int tmp0 = ptr[-1-j];                                   \
        int tmp1 = ptr[   j];                                   \
        int tmp2 = MULH(tmp0 + tmp1, csa_table[j][0]);          \
        ptr[-1-j] = 4 * (tmp2 - MULH(tmp1, csa_table[j][2]));   \
        ptr[   j] = 4 * (tmp2 + MULH(tmp0, csa_table[j][3]));   \
    } while (0)
#endif

static void compute_antialias(MPADecodeContext *s, GranuleDef *g)
{
    INTFLOAT *ptr;
    int n, i;

    /* we antialias only "long" bands */
    if (g->block_type == 2) {
        if (!g->switch_point)
            return;
        /* XXX: check this for 8000Hz case */
        n = 1;
    } else {
        n = SBLIMIT - 1;
    }

    ptr = g->sb_hybrid + 18;
    for (i = n; i > 0; i--) {
        AA(0);
        AA(1);
        AA(2);
        AA(3);
        AA(4);
        AA(5);
        AA(6);
        AA(7);

        ptr += 18;
    }
}

static void compute_imdct(MPADecodeContext *s, GranuleDef *g,
                          INTFLOAT *sb_samples, INTFLOAT *mdct_buf)
{
    INTFLOAT *win, *out_ptr, *ptr, *buf, *ptr1;
    INTFLOAT out2[12];
    int i, j, mdct_long_end, sblimit;

    /* find last non zero block */
    ptr  = g->sb_hybrid + 576;
    ptr1 = g->sb_hybrid + 2 * 18;
    while (ptr >= ptr1) {
        int32_t *p;
        ptr -= 6;
        p    = (int32_t*)ptr;
        if (p[0] | p[1] | p[2] | p[3] | p[4] | p[5])
            break;
    }
    sblimit = ((ptr - g->sb_hybrid) / 18) + 1;

    if (g->block_type == 2) {
        /* XXX: check for 8000 Hz */
        if (g->switch_point)
            mdct_long_end = 2;
        else
            mdct_long_end = 0;
    } else {
        mdct_long_end = sblimit;
    }

    s->mpadsp.RENAME(imdct36_blocks)(sb_samples, mdct_buf, g->sb_hybrid,
                                     mdct_long_end, g->switch_point,
                                     g->block_type);

    buf = mdct_buf + 4*18*(mdct_long_end >> 2) + (mdct_long_end & 3);
    ptr = g->sb_hybrid + 18 * mdct_long_end;

    for (j = mdct_long_end; j < sblimit; j++) {
        /* select frequency inversion */
        win     = RENAME(ff_mdct_win)[2 + (4  & -(j & 1))];
        out_ptr = sb_samples + j;

        for (i = 0; i < 6; i++) {
            *out_ptr = buf[4*i];
            out_ptr += SBLIMIT;
        }
        imdct12(out2, ptr + 0);
        for (i = 0; i < 6; i++) {
            *out_ptr     = MULH3(out2[i    ], win[i    ], 1) + buf[4*(i + 6*1)];
            buf[4*(i + 6*2)] = MULH3(out2[i + 6], win[i + 6], 1);
            out_ptr += SBLIMIT;
        }
        imdct12(out2, ptr + 1);
        for (i = 0; i < 6; i++) {
            *out_ptr     = MULH3(out2[i    ], win[i    ], 1) + buf[4*(i + 6*2)];
            buf[4*(i + 6*0)] = MULH3(out2[i + 6], win[i + 6], 1);
            out_ptr += SBLIMIT;
        }
        imdct12(out2, ptr + 2);
        for (i = 0; i < 6; i++) {
            buf[4*(i + 6*0)] = MULH3(out2[i    ], win[i    ], 1) + buf[4*(i + 6*0)];
            buf[4*(i + 6*1)] = MULH3(out2[i + 6], win[i + 6], 1);
            buf[4*(i + 6*2)] = 0;
        }
        ptr += 18;
        buf += (j&3) != 3 ? 1 : (4*18-3);
    }
    /* zero bands */
    for (j = sblimit; j < SBLIMIT; j++) {
        /* overlap */
        out_ptr = sb_samples + j;
        for (i = 0; i < 18; i++) {
            *out_ptr = buf[4*i];
            buf[4*i]   = 0;
            out_ptr += SBLIMIT;
        }
        buf += (j&3) != 3 ? 1 : (4*18-3);
    }
}

/* main layer3 decoding function */
static int mp_decode_layer3(MPADecodeContext *s)
{
    int nb_granules, main_data_begin;
    int gr, ch, blocksplit_flag, i, j, k, n, bits_pos;
    GranuleDef *g;
    int16_t exponents[576]; //FIXME try INTFLOAT

    /* read side info */
    if (s->lsf) {
        main_data_begin = get_bits(&s->gb, 8);
        skip_bits(&s->gb, s->nb_channels);
        nb_granules = 1;
    } else {
        main_data_begin = get_bits(&s->gb, 9);
        if (s->nb_channels == 2)
            skip_bits(&s->gb, 3);
        else
            skip_bits(&s->gb, 5);
        nb_granules = 2;
        for (ch = 0; ch < s->nb_channels; ch++) {
            s->granules[ch][0].scfsi = 0;/* all scale factors are transmitted */
            s->granules[ch][1].scfsi = get_bits(&s->gb, 4);
        }
    }

    for (gr = 0; gr < nb_granules; gr++) {
        for (ch = 0; ch < s->nb_channels; ch++) {
            av_dlog(s->avctx, "gr=%d ch=%d: side_info\n", gr, ch);
            g = &s->granules[ch][gr];
            g->part2_3_length = get_bits(&s->gb, 12);
            g->big_values     = get_bits(&s->gb,  9);
            if (g->big_values > 288) {
                av_log(s->avctx, AV_LOG_ERROR, "big_values too big\n");
                return AVERROR_INVALIDDATA;
            }

            g->global_gain = get_bits(&s->gb, 8);
            /* if MS stereo only is selected, we precompute the
               1/sqrt(2) renormalization factor */
            if ((s->mode_ext & (MODE_EXT_MS_STEREO | MODE_EXT_I_STEREO)) ==
                MODE_EXT_MS_STEREO)
                g->global_gain -= 2;
            if (s->lsf)
                g->scalefac_compress = get_bits(&s->gb, 9);
            else
                g->scalefac_compress = get_bits(&s->gb, 4);
            blocksplit_flag = get_bits1(&s->gb);
            if (blocksplit_flag) {
                g->block_type = get_bits(&s->gb, 2);
                if (g->block_type == 0) {
                    av_log(s->avctx, AV_LOG_ERROR, "invalid block type\n");
                    return AVERROR_INVALIDDATA;
                }
                g->switch_point = get_bits1(&s->gb);
                for (i = 0; i < 2; i++)
                    g->table_select[i] = get_bits(&s->gb, 5);
                for (i = 0; i < 3; i++)
                    g->subblock_gain[i] = get_bits(&s->gb, 3);
                ff_init_short_region(s, g);
            } else {
                int region_address1, region_address2;
                g->block_type = 0;
                g->switch_point = 0;
                for (i = 0; i < 3; i++)
                    g->table_select[i] = get_bits(&s->gb, 5);
                /* compute huffman coded region sizes */
                region_address1 = get_bits(&s->gb, 4);
                region_address2 = get_bits(&s->gb, 3);
                av_dlog(s->avctx, "region1=%d region2=%d\n",
                        region_address1, region_address2);
                ff_init_long_region(s, g, region_address1, region_address2);
            }
            ff_region_offset2size(g);
            ff_compute_band_indexes(s, g);

            g->preflag = 0;
            if (!s->lsf)
                g->preflag = get_bits1(&s->gb);
            g->scalefac_scale     = get_bits1(&s->gb);
            g->count1table_select = get_bits1(&s->gb);
            av_dlog(s->avctx, "block_type=%d switch_point=%d\n",
                    g->block_type, g->switch_point);
        }
    }

    if (!s->adu_mode) {
        int skip;
        const uint8_t *ptr = s->gb.buffer + (get_bits_count(&s->gb)>>3);
        int extrasize = av_clip(get_bits_left(&s->gb) >> 3, 0, EXTRABYTES);
        assert((get_bits_count(&s->gb) & 7) == 0);
        /* now we get bits from the main_data_begin offset */
        av_dlog(s->avctx, "seekback: %d\n", main_data_begin);
    //av_log(NULL, AV_LOG_ERROR, "backstep:%d, lastbuf:%d\n", main_data_begin, s->last_buf_size);

        memcpy(s->last_buf + s->last_buf_size, ptr, extrasize);
        s->in_gb = s->gb;
        init_get_bits(&s->gb, s->last_buf, s->last_buf_size*8);
#if !UNCHECKED_BITSTREAM_READER
        s->gb.size_in_bits_plus8 += FFMAX(extrasize, LAST_BUF_SIZE - s->last_buf_size) * 8;
#endif
        s->last_buf_size <<= 3;
        for (gr = 0; gr < nb_granules && (s->last_buf_size >> 3) < main_data_begin; gr++) {
            for (ch = 0; ch < s->nb_channels; ch++) {
                g = &s->granules[ch][gr];
                s->last_buf_size += g->part2_3_length;
                memset(g->sb_hybrid, 0, sizeof(g->sb_hybrid));
            }
        }
        skip = s->last_buf_size - 8 * main_data_begin;
        if (skip >= s->gb.size_in_bits && s->in_gb.buffer) {
            skip_bits_long(&s->in_gb, skip - s->gb.size_in_bits);
            s->gb           = s->in_gb;
            s->in_gb.buffer = NULL;
        } else {
            skip_bits_long(&s->gb, skip);
        }
    } else {
        gr = 0;
    }

    for (; gr < nb_granules; gr++) {
        for (ch = 0; ch < s->nb_channels; ch++) {
            g = &s->granules[ch][gr];
            bits_pos = get_bits_count(&s->gb);

            if (!s->lsf) {
                uint8_t *sc;
                int slen, slen1, slen2;

                /* MPEG1 scale factors */
                slen1 = slen_table[0][g->scalefac_compress];
                slen2 = slen_table[1][g->scalefac_compress];
                av_dlog(s->avctx, "slen1=%d slen2=%d\n", slen1, slen2);
                if (g->block_type == 2) {
                    n = g->switch_point ? 17 : 18;
                    j = 0;
                    if (slen1) {
                        for (i = 0; i < n; i++)
                            g->scale_factors[j++] = get_bits(&s->gb, slen1);
                    } else {
                        for (i = 0; i < n; i++)
                            g->scale_factors[j++] = 0;
                    }
                    if (slen2) {
                        for (i = 0; i < 18; i++)
                            g->scale_factors[j++] = get_bits(&s->gb, slen2);
                        for (i = 0; i < 3; i++)
                            g->scale_factors[j++] = 0;
                    } else {
                        for (i = 0; i < 21; i++)
                            g->scale_factors[j++] = 0;
                    }
                } else {
                    sc = s->granules[ch][0].scale_factors;
                    j = 0;
                    for (k = 0; k < 4; k++) {
                        n = k == 0 ? 6 : 5;
                        if ((g->scfsi & (0x8 >> k)) == 0) {
                            slen = (k < 2) ? slen1 : slen2;
                            if (slen) {
                                for (i = 0; i < n; i++)
                                    g->scale_factors[j++] = get_bits(&s->gb, slen);
                            } else {
                                for (i = 0; i < n; i++)
                                    g->scale_factors[j++] = 0;
                            }
                        } else {
                            /* simply copy from last granule */
                            for (i = 0; i < n; i++) {
                                g->scale_factors[j] = sc[j];
                                j++;
                            }
                        }
                    }
                    g->scale_factors[j++] = 0;
                }
            } else {
                int tindex, tindex2, slen[4], sl, sf;

                /* LSF scale factors */
                if (g->block_type == 2)
                    tindex = g->switch_point ? 2 : 1;
                else
                    tindex = 0;

                sf = g->scalefac_compress;
                if ((s->mode_ext & MODE_EXT_I_STEREO) && ch == 1) {
                    /* intensity stereo case */
                    sf >>= 1;
                    if (sf < 180) {
                        lsf_sf_expand(slen, sf, 6, 6, 0);
                        tindex2 = 3;
                    } else if (sf < 244) {
                        lsf_sf_expand(slen, sf - 180, 4, 4, 0);
                        tindex2 = 4;
                    } else {
                        lsf_sf_expand(slen, sf - 244, 3, 0, 0);
                        tindex2 = 5;
                    }
                } else {
                    /* normal case */
                    if (sf < 400) {
                        lsf_sf_expand(slen, sf, 5, 4, 4);
                        tindex2 = 0;
                    } else if (sf < 500) {
                        lsf_sf_expand(slen, sf - 400, 5, 4, 0);
                        tindex2 = 1;
                    } else {
                        lsf_sf_expand(slen, sf - 500, 3, 0, 0);
                        tindex2 = 2;
                        g->preflag = 1;
                    }
                }

                j = 0;
                for (k = 0; k < 4; k++) {
                    n  = lsf_nsf_table[tindex2][tindex][k];
                    sl = slen[k];
                    if (sl) {
                        for (i = 0; i < n; i++)
                            g->scale_factors[j++] = get_bits(&s->gb, sl);
                    } else {
                        for (i = 0; i < n; i++)
                            g->scale_factors[j++] = 0;
                    }
                }
                /* XXX: should compute exact size */
                for (; j < 40; j++)
                    g->scale_factors[j] = 0;
            }

            exponents_from_scale_factors(s, g, exponents);

            /* read Huffman coded residue */
            huffman_decode(s, g, exponents, bits_pos + g->part2_3_length);
        } /* ch */

        if (s->mode == MPA_JSTEREO)
            compute_stereo(s, &s->granules[0][gr], &s->granules[1][gr]);

        for (ch = 0; ch < s->nb_channels; ch++) {
            g = &s->granules[ch][gr];

            reorder_block(s, g);
            compute_antialias(s, g);
            compute_imdct(s, g, &s->sb_samples[ch][18 * gr][0], s->mdct_buf[ch]);
        }
    } /* gr */
    if (get_bits_count(&s->gb) < 0)
        skip_bits_long(&s->gb, -get_bits_count(&s->gb));
    return nb_granules * 18;
}

static int mp_decode_frame(MPADecodeContext *s, OUT_INT *samples,
                           const uint8_t *buf, int buf_size)
{
    int i, nb_frames, ch, ret;
    OUT_INT *samples_ptr;

    init_get_bits(&s->gb, buf + HEADER_SIZE, (buf_size - HEADER_SIZE) * 8);

    /* skip error protection field */
    if (s->error_protection)
        skip_bits(&s->gb, 16);

    switch(s->layer) {
    case 1:
        s->avctx->frame_size = 384;
        nb_frames = mp_decode_layer1(s);
        break;
    case 2:
        s->avctx->frame_size = 1152;
        nb_frames = mp_decode_layer2(s);
        break;
    case 3:
        s->avctx->frame_size = s->lsf ? 576 : 1152;
    default:
        nb_frames = mp_decode_layer3(s);

        s->last_buf_size=0;
        if (s->in_gb.buffer) {
            align_get_bits(&s->gb);
            i = get_bits_left(&s->gb)>>3;
            if (i >= 0 && i <= BACKSTEP_SIZE) {
                memmove(s->last_buf, s->gb.buffer + (get_bits_count(&s->gb)>>3), i);
                s->last_buf_size=i;
            } else
                av_log(s->avctx, AV_LOG_ERROR, "invalid old backstep %d\n", i);
            s->gb           = s->in_gb;
            s->in_gb.buffer = NULL;
        }

        align_get_bits(&s->gb);
        assert((get_bits_count(&s->gb) & 7) == 0);
        i = get_bits_left(&s->gb) >> 3;

        if (i < 0 || i > BACKSTEP_SIZE || nb_frames < 0) {
            if (i < 0)
                av_log(s->avctx, AV_LOG_ERROR, "invalid new backstep %d\n", i);
            i = FFMIN(BACKSTEP_SIZE, buf_size - HEADER_SIZE);
        }
        assert(i <= buf_size - HEADER_SIZE && i >= 0);
        memcpy(s->last_buf + s->last_buf_size, s->gb.buffer + buf_size - HEADER_SIZE - i, i);
        s->last_buf_size += i;
    }

    /* get output buffer */
    if (!samples) {
        s->frame.nb_samples = s->avctx->frame_size;
        if ((ret = s->avctx->get_buffer(s->avctx, &s->frame)) < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "get_buffer() failed\n");
            return ret;
        }
        samples = (OUT_INT *)s->frame.data[0];
    }

    /* apply the synthesis filter */
    for (ch = 0; ch < s->nb_channels; ch++) {
        samples_ptr = samples + ch;
        for (i = 0; i < nb_frames; i++) {
            RENAME(ff_mpa_synth_filter)(
                         &s->mpadsp,
                         s->synth_buf[ch], &(s->synth_buf_offset[ch]),
                         RENAME(ff_mpa_synth_window), &s->dither_state,
                         samples_ptr, s->nb_channels,
                         s->sb_samples[ch][i]);
            samples_ptr += 32 * s->nb_channels;
        }
    }

    return nb_frames * 32 * sizeof(OUT_INT) * s->nb_channels;
}

static int decode_frame(AVCodecContext * avctx, void *data, int *got_frame_ptr,
                        AVPacket *avpkt)
{
    const uint8_t *buf  = avpkt->data;
    int buf_size        = avpkt->size;
    MPADecodeContext *s = avctx->priv_data;
    uint32_t header;
    int out_size;

    if (buf_size < HEADER_SIZE)
        return AVERROR_INVALIDDATA;

    header = AV_RB32(buf);
    if (header>>8 == AV_RB32("TAG")>>8) {
        av_log(avctx, AV_LOG_DEBUG, "discarding ID3 tag\n");
        return buf_size;
    }
    if (ff_mpa_check_header(header) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Header missing\n");
        return AVERROR_INVALIDDATA;
    }

    if (avpriv_mpegaudio_decode_header((MPADecodeHeader *)s, header) == 1) {
        /* free format: prepare to compute frame size */
        s->frame_size = -1;
        return AVERROR_INVALIDDATA;
    }
    /* update codec info */
    avctx->channels       = s->nb_channels;
    avctx->channel_layout = s->nb_channels == 1 ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
    if (!avctx->bit_rate)
        avctx->bit_rate = s->bit_rate;

    if (s->frame_size <= 0 || s->frame_size > buf_size) {
        av_log(avctx, AV_LOG_ERROR, "incomplete frame\n");
        return AVERROR_INVALIDDATA;
    }else if(s->frame_size < buf_size){
        av_log(avctx, AV_LOG_DEBUG, "incorrect frame size - multiple frames in buffer?\n");
        buf_size= s->frame_size;
    }

    out_size = mp_decode_frame(s, NULL, buf, buf_size);
    if (out_size >= 0) {
        *got_frame_ptr   = 1;
        *(AVFrame *)data = s->frame;
        avctx->sample_rate = s->sample_rate;
        //FIXME maybe move the other codec info stuff from above here too
    } else {
        av_log(avctx, AV_LOG_ERROR, "Error while decoding MPEG audio frame.\n");
        /* Only return an error if the bad frame makes up the whole packet.
           If there is more data in the packet, just consume the bad frame
           instead of returning an error, which would discard the whole
           packet. */
        *got_frame_ptr = 0;
        if (buf_size == avpkt->size)
            return out_size;
    }
    s->frame_size = 0;
    return buf_size;
}

static void flush(AVCodecContext *avctx)
{
    MPADecodeContext *s = avctx->priv_data;
    memset(s->synth_buf, 0, sizeof(s->synth_buf));
    s->last_buf_size = 0;
}

#if CONFIG_MP3ADU_DECODER || CONFIG_MP3ADUFLOAT_DECODER
static int decode_frame_adu(AVCodecContext *avctx, void *data,
                            int *got_frame_ptr, AVPacket *avpkt)
{
    const uint8_t *buf  = avpkt->data;
    int buf_size        = avpkt->size;
    MPADecodeContext *s = avctx->priv_data;
    uint32_t header;
    int len;
    int av_unused out_size;

    len = buf_size;

    // Discard too short frames
    if (buf_size < HEADER_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "Packet is too small\n");
        return AVERROR_INVALIDDATA;
    }


    if (len > MPA_MAX_CODED_FRAME_SIZE)
        len = MPA_MAX_CODED_FRAME_SIZE;

    // Get header and restore sync word
    header = AV_RB32(buf) | 0xffe00000;

    if (ff_mpa_check_header(header) < 0) { // Bad header, discard frame
        av_log(avctx, AV_LOG_ERROR, "Invalid frame header\n");
        return AVERROR_INVALIDDATA;
    }

    avpriv_mpegaudio_decode_header((MPADecodeHeader *)s, header);
    /* update codec info */
    avctx->sample_rate = s->sample_rate;
    avctx->channels    = s->nb_channels;
    if (!avctx->bit_rate)
        avctx->bit_rate = s->bit_rate;

    s->frame_size = len;

    out_size = mp_decode_frame(s, NULL, buf, buf_size);
    if (out_size < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error while decoding MPEG audio frame.\n");
        return AVERROR_INVALIDDATA;
    }

    *got_frame_ptr   = 1;
    *(AVFrame *)data = s->frame;

    return buf_size;
}
#endif /* CONFIG_MP3ADU_DECODER || CONFIG_MP3ADUFLOAT_DECODER */

#if CONFIG_MP3ON4_DECODER || CONFIG_MP3ON4FLOAT_DECODER

/**
 * Context for MP3On4 decoder
 */
typedef struct MP3On4DecodeContext {
    AVFrame *frame;
    int frames;                     ///< number of mp3 frames per block (number of mp3 decoder instances)
    int syncword;                   ///< syncword patch
    const uint8_t *coff;            ///< channel offsets in output buffer
    MPADecodeContext *mp3decctx[5]; ///< MPADecodeContext for every decoder instance
    OUT_INT *decoded_buf;           ///< output buffer for decoded samples
} MP3On4DecodeContext;

#include "mpeg4audio.h"

/* Next 3 arrays are indexed by channel config number (passed via codecdata) */

/* number of mp3 decoder instances */
static const uint8_t mp3Frames[8] = { 0, 1, 1, 2, 3, 3, 4, 5 };

/* offsets into output buffer, assume output order is FL FR C LFE BL BR SL SR */
static const uint8_t chan_offset[8][5] = {
    { 0             },
    { 0             },  // C
    { 0             },  // FLR
    { 2, 0          },  // C FLR
    { 2, 0, 3       },  // C FLR BS
    { 2, 0, 3       },  // C FLR BLRS
    { 2, 0, 4, 3    },  // C FLR BLRS LFE
    { 2, 0, 6, 4, 3 },  // C FLR BLRS BLR LFE
};

/* mp3on4 channel layouts */
static const int16_t chan_layout[8] = {
    0,
    AV_CH_LAYOUT_MONO,
    AV_CH_LAYOUT_STEREO,
    AV_CH_LAYOUT_SURROUND,
    AV_CH_LAYOUT_4POINT0,
    AV_CH_LAYOUT_5POINT0,
    AV_CH_LAYOUT_5POINT1,
    AV_CH_LAYOUT_7POINT1
};

static av_cold int decode_close_mp3on4(AVCodecContext * avctx)
{
    MP3On4DecodeContext *s = avctx->priv_data;
    int i;

    for (i = 0; i < s->frames; i++)
        av_free(s->mp3decctx[i]);

    av_freep(&s->decoded_buf);

    return 0;
}


static int decode_init_mp3on4(AVCodecContext * avctx)
{
    MP3On4DecodeContext *s = avctx->priv_data;
    MPEG4AudioConfig cfg;
    int i;

    if ((avctx->extradata_size < 2) || (avctx->extradata == NULL)) {
        av_log(avctx, AV_LOG_ERROR, "Codec extradata missing or too short.\n");
        return AVERROR_INVALIDDATA;
    }

    avpriv_mpeg4audio_get_config(&cfg, avctx->extradata,
                                 avctx->extradata_size * 8, 1);
    if (!cfg.chan_config || cfg.chan_config > 7) {
        av_log(avctx, AV_LOG_ERROR, "Invalid channel config number.\n");
        return AVERROR_INVALIDDATA;
    }
    s->frames             = mp3Frames[cfg.chan_config];
    s->coff               = chan_offset[cfg.chan_config];
    avctx->channels       = ff_mpeg4audio_channels[cfg.chan_config];
    avctx->channel_layout = chan_layout[cfg.chan_config];

    if (cfg.sample_rate < 16000)
        s->syncword = 0xffe00000;
    else
        s->syncword = 0xfff00000;

    /* Init the first mp3 decoder in standard way, so that all tables get builded
     * We replace avctx->priv_data with the context of the first decoder so that
     * decode_init() does not have to be changed.
     * Other decoders will be initialized here copying data from the first context
     */
    // Allocate zeroed memory for the first decoder context
    s->mp3decctx[0] = av_mallocz(sizeof(MPADecodeContext));
    if (!s->mp3decctx[0])
        goto alloc_fail;
    // Put decoder context in place to make init_decode() happy
    avctx->priv_data = s->mp3decctx[0];
    decode_init(avctx);
    s->frame = avctx->coded_frame;
    // Restore mp3on4 context pointer
    avctx->priv_data = s;
    s->mp3decctx[0]->adu_mode = 1; // Set adu mode

    /* Create a separate codec/context for each frame (first is already ok).
     * Each frame is 1 or 2 channels - up to 5 frames allowed
     */
    for (i = 1; i < s->frames; i++) {
        s->mp3decctx[i] = av_mallocz(sizeof(MPADecodeContext));
        if (!s->mp3decctx[i])
            goto alloc_fail;
        s->mp3decctx[i]->adu_mode = 1;
        s->mp3decctx[i]->avctx = avctx;
        s->mp3decctx[i]->mpadsp = s->mp3decctx[0]->mpadsp;
    }

    /* Allocate buffer for multi-channel output if needed */
    if (s->frames > 1) {
        s->decoded_buf = av_malloc(MPA_FRAME_SIZE * MPA_MAX_CHANNELS *
                                   sizeof(*s->decoded_buf));
        if (!s->decoded_buf)
            goto alloc_fail;
    }

    return 0;
alloc_fail:
    decode_close_mp3on4(avctx);
    return AVERROR(ENOMEM);
}


static void flush_mp3on4(AVCodecContext *avctx)
{
    int i;
    MP3On4DecodeContext *s = avctx->priv_data;

    for (i = 0; i < s->frames; i++) {
        MPADecodeContext *m = s->mp3decctx[i];
        memset(m->synth_buf, 0, sizeof(m->synth_buf));
        m->last_buf_size = 0;
    }
}


static int decode_frame_mp3on4(AVCodecContext *avctx, void *data,
                               int *got_frame_ptr, AVPacket *avpkt)
{
    const uint8_t *buf     = avpkt->data;
    int buf_size           = avpkt->size;
    MP3On4DecodeContext *s = avctx->priv_data;
    MPADecodeContext *m;
    int fsize, len = buf_size, out_size = 0;
    uint32_t header;
    OUT_INT *out_samples;
    OUT_INT *outptr, *bp;
    int fr, j, n, ch, ret;

    /* get output buffer */
    s->frame->nb_samples = s->frames * MPA_FRAME_SIZE;
    if ((ret = avctx->get_buffer(avctx, s->frame)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }
    out_samples = (OUT_INT *)s->frame->data[0];

    // Discard too short frames
    if (buf_size < HEADER_SIZE)
        return AVERROR_INVALIDDATA;

    // If only one decoder interleave is not needed
    outptr = s->frames == 1 ? out_samples : s->decoded_buf;

    avctx->bit_rate = 0;

    ch = 0;
    for (fr = 0; fr < s->frames; fr++) {
        fsize = AV_RB16(buf) >> 4;
        fsize = FFMIN3(fsize, len, MPA_MAX_CODED_FRAME_SIZE);
        m     = s->mp3decctx[fr];
        assert(m != NULL);

        if (fsize < HEADER_SIZE) {
            av_log(avctx, AV_LOG_ERROR, "Frame size smaller than header size\n");
            return AVERROR_INVALIDDATA;
        }
        header = (AV_RB32(buf) & 0x000fffff) | s->syncword; // patch header

        if (ff_mpa_check_header(header) < 0) // Bad header, discard block
            break;

        avpriv_mpegaudio_decode_header((MPADecodeHeader *)m, header);

        if (ch + m->nb_channels > avctx->channels) {
            av_log(avctx, AV_LOG_ERROR, "frame channel count exceeds codec "
                                        "channel count\n");
            return AVERROR_INVALIDDATA;
        }
        ch += m->nb_channels;

        out_size += mp_decode_frame(m, outptr, buf, fsize);
        buf      += fsize;
        len      -= fsize;

        if (s->frames > 1) {
            n = m->avctx->frame_size*m->nb_channels;
            /* interleave output data */
            bp = out_samples + s->coff[fr];
            if (m->nb_channels == 1) {
                for (j = 0; j < n; j++) {
                    *bp = s->decoded_buf[j];
                    bp += avctx->channels;
                }
            } else {
                for (j = 0; j < n; j++) {
                    bp[0] = s->decoded_buf[j++];
                    bp[1] = s->decoded_buf[j];
                    bp   += avctx->channels;
                }
            }
        }
        avctx->bit_rate += m->bit_rate;
    }

    /* update codec info */
    avctx->sample_rate = s->mp3decctx[0]->sample_rate;

    s->frame->nb_samples = out_size / (avctx->channels * sizeof(OUT_INT));
    *got_frame_ptr   = 1;
    *(AVFrame *)data = *s->frame;

    return buf_size;
}
#endif /* CONFIG_MP3ON4_DECODER || CONFIG_MP3ON4FLOAT_DECODER */

#if !CONFIG_FLOAT
#if CONFIG_MP1_DECODER
AVCodec ff_mp1_decoder = {
    .name           = "mp1",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_MP1,
    .priv_data_size = sizeof(MPADecodeContext),
    .init           = decode_init,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .flush          = flush,
    .long_name      = NULL_IF_CONFIG_SMALL("MP1 (MPEG audio layer 1)"),
};
#endif
#if CONFIG_MP2_DECODER
AVCodec ff_mp2_decoder = {
    .name           = "mp2",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_MP2,
    .priv_data_size = sizeof(MPADecodeContext),
    .init           = decode_init,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .flush          = flush,
    .long_name      = NULL_IF_CONFIG_SMALL("MP2 (MPEG audio layer 2)"),
};
#endif
#if CONFIG_MP3_DECODER
AVCodec ff_mp3_decoder = {
    .name           = "mp3",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_MP3,
    .priv_data_size = sizeof(MPADecodeContext),
    .init           = decode_init,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .flush          = flush,
    .long_name      = NULL_IF_CONFIG_SMALL("MP3 (MPEG audio layer 3)"),
};
#endif
#if CONFIG_MP3ADU_DECODER
AVCodec ff_mp3adu_decoder = {
    .name           = "mp3adu",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_MP3ADU,
    .priv_data_size = sizeof(MPADecodeContext),
    .init           = decode_init,
    .decode         = decode_frame_adu,
    .capabilities   = CODEC_CAP_DR1,
    .flush          = flush,
    .long_name      = NULL_IF_CONFIG_SMALL("ADU (Application Data Unit) MP3 (MPEG audio layer 3)"),
};
#endif
#if CONFIG_MP3ON4_DECODER
AVCodec ff_mp3on4_decoder = {
    .name           = "mp3on4",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_MP3ON4,
    .priv_data_size = sizeof(MP3On4DecodeContext),
    .init           = decode_init_mp3on4,
    .close          = decode_close_mp3on4,
    .decode         = decode_frame_mp3on4,
    .capabilities   = CODEC_CAP_DR1,
    .flush          = flush_mp3on4,
    .long_name      = NULL_IF_CONFIG_SMALL("MP3onMP4"),
};
#endif
#endif
