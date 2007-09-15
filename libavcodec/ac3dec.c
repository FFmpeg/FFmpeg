/*
 * AC-3 Audio Decoder
 * This code is developed as part of Google Summer of Code 2006 Program.
 *
 * Copyright (c) 2006 Kartikey Mahendra BHATT (bhattkm at gmail dot com).
 * Copyright (c) 2007 Justin Ruggles
 *
 * Portions of this code are derived from liba52
 * http://liba52.sourceforge.net
 * Copyright (C) 2000-2003 Michel Lespinasse <walken@zoy.org>
 * Copyright (C) 1999-2000 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdio.h>
#include <stddef.h>
#include <math.h>
#include <string.h>

#include "avcodec.h"
#include "ac3_parser.h"
#include "bitstream.h"
#include "dsputil.h"
#include "random.h"

/**
 * Table of bin locations for rematrixing bands
 * reference: Section 7.5.2 Rematrixing : Frequency Band Definitions
 */
static const uint8_t rematrix_band_tab[5] = { 13, 25, 37, 61, 253 };

/**
 * table for exponent to scale_factor mapping
 * scale_factors[i] = 2 ^ -i
 */
static float scale_factors[25];

/** table for grouping exponents */
static uint8_t exp_ungroup_tab[128][3];


/** tables for ungrouping mantissas */
static float b1_mantissas[32][3];
static float b2_mantissas[128][3];
static float b3_mantissas[8];
static float b4_mantissas[128][2];
static float b5_mantissas[16];

/**
 * Quantization table: levels for symmetric. bits for asymmetric.
 * reference: Table 7.18 Mapping of bap to Quantizer
 */
static const uint8_t qntztab[16] = {
    0, 3, 5, 7, 11, 15,
    5, 6, 7, 8, 9, 10, 11, 12, 14, 16
};

/** dynamic range table. converts codes to scale factors. */
static float dynrng_tab[256];

/** dialogue normalization table */
static float dialnorm_tab[32];

/** Adjustments in dB gain */
#define LEVEL_MINUS_3DB         0.7071067811865476
#define LEVEL_MINUS_4POINT5DB   0.5946035575013605
#define LEVEL_MINUS_6DB         0.5000000000000000
#define LEVEL_MINUS_9DB         0.3535533905932738
#define LEVEL_ZERO              0.0000000000000000
#define LEVEL_ONE               1.0000000000000000

static const float gain_levels[6] = {
    LEVEL_ZERO,
    LEVEL_ONE,
    LEVEL_MINUS_3DB,
    LEVEL_MINUS_4POINT5DB,
    LEVEL_MINUS_6DB,
    LEVEL_MINUS_9DB
};

/**
 * Table for center mix levels
 * reference: Section 5.4.2.4 cmixlev
 */
static const uint8_t clevs[4] = { 2, 3, 4, 3 };

/**
 * Table for surround mix levels
 * reference: Section 5.4.2.5 surmixlev
 */
static const uint8_t slevs[4] = { 2, 4, 0, 4 };

/**
 * Table for default stereo downmixing coefficients
 * reference: Section 7.8.2 Downmixing Into Two Channels
 */
static const uint8_t ac3_default_coeffs[8][5][2] = {
    { { 1, 0 }, { 0, 1 },                               },
    { { 2, 2 },                                         },
    { { 1, 0 }, { 0, 1 },                               },
    { { 1, 0 }, { 3, 3 }, { 0, 1 },                     },
    { { 1, 0 }, { 0, 1 }, { 4, 4 },                     },
    { { 1, 0 }, { 3, 3 }, { 0, 1 }, { 5, 5 },           },
    { { 1, 0 }, { 0, 1 }, { 4, 0 }, { 0, 4 },           },
    { { 1, 0 }, { 3, 3 }, { 0, 1 }, { 4, 0 }, { 0, 4 }, },
};

/* override ac3.h to include coupling channel */
#undef AC3_MAX_CHANNELS
#define AC3_MAX_CHANNELS 7
#define CPL_CH 0

#define AC3_OUTPUT_LFEON  8

typedef struct {
    int acmod;                              ///< audio coding mode
    int dsurmod;                            ///< dolby surround mode
    int blksw[AC3_MAX_CHANNELS];            ///< block switch flags
    int dithflag[AC3_MAX_CHANNELS];         ///< dither flags
    int dither_all;                         ///< true if all channels are dithered
    int cplinu;                             ///< coupling in use
    int chincpl[AC3_MAX_CHANNELS];          ///< channel in coupling
    int phsflginu;                          ///< phase flags in use
    int cplbndstrc[18];                     ///< coupling band structure
    int rematstr;                           ///< rematrixing strategy
    int nrematbnd;                          ///< number of rematrixing bands
    int rematflg[4];                        ///< rematrixing flags
    int expstr[AC3_MAX_CHANNELS];           ///< exponent strategies
    int snroffst[AC3_MAX_CHANNELS];         ///< signal-to-noise ratio offsets
    int fgain[AC3_MAX_CHANNELS];            ///< fast gain values (signal-to-mask ratio)
    int deltbae[AC3_MAX_CHANNELS];          ///< delta bit allocation exists
    int deltnseg[AC3_MAX_CHANNELS];         ///< number of delta segments
    uint8_t deltoffst[AC3_MAX_CHANNELS][8]; ///< delta segment offsets
    uint8_t deltlen[AC3_MAX_CHANNELS][8];   ///< delta segment lengths
    uint8_t deltba[AC3_MAX_CHANNELS][8];    ///< delta values for each segment

    int sampling_rate;                      ///< sample frequency, in Hz
    int bit_rate;                           ///< stream bit rate, in bits-per-second
    int frame_size;                         ///< current frame size, in bytes

    int nchans;                             ///< number of total channels
    int nfchans;                            ///< number of full-bandwidth channels
    int lfeon;                              ///< lfe channel in use
    int lfe_ch;                             ///< index of LFE channel
    int output_mode;                        ///< output channel configuration
    int out_channels;                       ///< number of output channels

    float downmix_coeffs[AC3_MAX_CHANNELS][2];  ///< stereo downmix coefficients
    float dialnorm[2];                      ///< dialogue normalization
    float dynrng[2];                        ///< dynamic range
    float cplco[AC3_MAX_CHANNELS][18];      ///< coupling coordinates
    int   ncplbnd;                          ///< number of coupling bands
    int   ncplsubnd;                        ///< number of coupling sub bands
    int   startmant[AC3_MAX_CHANNELS];      ///< start frequency bin
    int   endmant[AC3_MAX_CHANNELS];        ///< end frequency bin
    AC3BitAllocParameters bit_alloc_params; ///< bit allocation parameters

    int8_t  dexps[AC3_MAX_CHANNELS][256];   ///< decoded exponents
    uint8_t bap[AC3_MAX_CHANNELS][256];     ///< bit allocation pointers
    int16_t psd[AC3_MAX_CHANNELS][256];     ///< scaled exponents
    int16_t bndpsd[AC3_MAX_CHANNELS][50];   ///< interpolated exponents
    int16_t mask[AC3_MAX_CHANNELS][50];     ///< masking curve values

    DECLARE_ALIGNED_16(float, transform_coeffs[AC3_MAX_CHANNELS][256]);  ///< transform coefficients

    /* For IMDCT. */
    MDCTContext imdct_512;                  ///< for 512 sample IMDCT
    MDCTContext imdct_256;                  ///< for 256 sample IMDCT
    DSPContext  dsp;                        ///< for optimization
    float       add_bias;                   ///< offset for float_to_int16 conversion
    float       mul_bias;                   ///< scaling for float_to_int16 conversion

    DECLARE_ALIGNED_16(float, output[AC3_MAX_CHANNELS-1][256]);     ///< output after imdct transform and windowing
    DECLARE_ALIGNED_16(short, int_output[AC3_MAX_CHANNELS-1][256]); ///< final 16-bit integer output
    DECLARE_ALIGNED_16(float, delay[AC3_MAX_CHANNELS-1][256]);      ///< delay - added to the next block
    DECLARE_ALIGNED_16(float, tmp_imdct[256]);                      ///< temporary storage for imdct transform
    DECLARE_ALIGNED_16(float, tmp_output[512]);                     ///< temporary storage for output before windowing
    DECLARE_ALIGNED_16(float, window[256]);                         ///< window coefficients

    /* Miscellaneous. */
    GetBitContext gb;                       ///< bitstream reader
    AVRandomState dith_state;               ///< for dither generation
    AVCodecContext *avctx;                  ///< parent context
} AC3DecodeContext;

/**
 * Generate a Kaiser-Bessel Derived Window.
 */
static void ac3_window_init(float *window)
{
   int i, j;
   double sum = 0.0, bessel, tmp;
   double local_window[256];
   double alpha2 = (5.0 * M_PI / 256.0) * (5.0 * M_PI / 256.0);

   for (i = 0; i < 256; i++) {
       tmp = i * (256 - i) * alpha2;
       bessel = 1.0;
       for (j = 100; j > 0; j--) /* default to 100 iterations */
           bessel = bessel * tmp / (j * j) + 1;
       sum += bessel;
       local_window[i] = sum;
   }

   sum++;
   for (i = 0; i < 256; i++)
       window[i] = sqrt(local_window[i] / sum);
}

/**
 * Symmetrical Dequantization
 * reference: Section 7.3.3 Expansion of Mantissas for Symmetrical Quantization
 *            Tables 7.19 to 7.23
 */
static inline float
symmetric_dequant(int code, int levels)
{
    return (code - (levels >> 1)) * (2.0f / levels);
}

/*
 * Initialize tables at runtime.
 */
static void ac3_tables_init(void)
{
    int i;

    /* generate grouped mantissa tables
       reference: Section 7.3.5 Ungrouping of Mantissas */
    for(i=0; i<32; i++) {
        /* bap=1 mantissas */
        b1_mantissas[i][0] = symmetric_dequant( i / 9     , 3);
        b1_mantissas[i][1] = symmetric_dequant((i % 9) / 3, 3);
        b1_mantissas[i][2] = symmetric_dequant((i % 9) % 3, 3);
    }
    for(i=0; i<128; i++) {
        /* bap=2 mantissas */
        b2_mantissas[i][0] = symmetric_dequant( i / 25     , 5);
        b2_mantissas[i][1] = symmetric_dequant((i % 25) / 5, 5);
        b2_mantissas[i][2] = symmetric_dequant((i % 25) % 5, 5);

        /* bap=4 mantissas */
        b4_mantissas[i][0] = symmetric_dequant(i / 11, 11);
        b4_mantissas[i][1] = symmetric_dequant(i % 11, 11);
    }
    /* generate ungrouped mantissa tables
       reference: Tables 7.21 and 7.23 */
    for(i=0; i<7; i++) {
        /* bap=3 mantissas */
        b3_mantissas[i] = symmetric_dequant(i, 7);
    }
    for(i=0; i<15; i++) {
        /* bap=5 mantissas */
        b5_mantissas[i] = symmetric_dequant(i, 15);
    }

    /* generate dynamic range table
       reference: Section 7.7.1 Dynamic Range Control */
    for(i=0; i<256; i++) {
        int v = (i >> 5) - ((i >> 7) << 3) - 5;
        dynrng_tab[i] = powf(2.0f, v) * ((i & 0x1F) | 0x20);
    }

    /* generate dialogue normalization table
       references: Section 5.4.2.8 dialnorm
                   Section 7.6 Dialogue Normalization */
    for(i=1; i<32; i++) {
        dialnorm_tab[i] = expf((i-31) * M_LN10 / 20.0f);
    }
    dialnorm_tab[0] = dialnorm_tab[31];

    /* generate scale factors for exponents and asymmetrical dequantization
       reference: Section 7.3.2 Expansion of Mantissas for Asymmetric Quantization */
    for (i = 0; i < 25; i++)
        scale_factors[i] = pow(2.0, -i);

    /* generate exponent tables
       reference: Section 7.1.3 Exponent Decoding */
    for(i=0; i<128; i++) {
        exp_ungroup_tab[i][0] =  i / 25;
        exp_ungroup_tab[i][1] = (i % 25) / 5;
        exp_ungroup_tab[i][2] = (i % 25) % 5;
    }
}


/**
 * AVCodec initialization
 */
static int ac3_decode_init(AVCodecContext *avctx)
{
    AC3DecodeContext *ctx = avctx->priv_data;
    ctx->avctx = avctx;

    ac3_common_init();
    ac3_tables_init();
    ff_mdct_init(&ctx->imdct_256, 8, 1);
    ff_mdct_init(&ctx->imdct_512, 9, 1);
    ac3_window_init(ctx->window);
    dsputil_init(&ctx->dsp, avctx);
    av_init_random(0, &ctx->dith_state);

    /* set bias values for float to int16 conversion */
    if(ctx->dsp.float_to_int16 == ff_float_to_int16_c) {
        ctx->add_bias = 385.0f;
        ctx->mul_bias = 1.0f;
    } else {
        ctx->add_bias = 0.0f;
        ctx->mul_bias = 32767.0f;
    }

    return 0;
}

/**
 * Parse the 'sync info' and 'bit stream info' from the AC-3 bitstream.
 * GetBitContext within AC3DecodeContext must point to
 * start of the synchronized ac3 bitstream.
 */
static int ac3_parse_header(AC3DecodeContext *ctx)
{
    AC3HeaderInfo hdr;
    GetBitContext *gb = &ctx->gb;
    float cmixlev, surmixlev;
    int err, i;

    err = ff_ac3_parse_header(gb->buffer, &hdr);
    if(err)
        return err;

    /* get decoding parameters from header info */
    ctx->bit_alloc_params.fscod       = hdr.fscod;
    ctx->acmod                        = hdr.acmod;
    cmixlev                           = gain_levels[clevs[hdr.cmixlev]];
    surmixlev                         = gain_levels[slevs[hdr.surmixlev]];
    ctx->dsurmod                      = hdr.dsurmod;
    ctx->lfeon                        = hdr.lfeon;
    ctx->bit_alloc_params.halfratecod = hdr.halfratecod;
    ctx->sampling_rate                = hdr.sample_rate;
    ctx->bit_rate                     = hdr.bit_rate;
    ctx->nchans                       = hdr.channels;
    ctx->nfchans                      = ctx->nchans - ctx->lfeon;
    ctx->lfe_ch                       = ctx->nfchans + 1;
    ctx->frame_size                   = hdr.frame_size;

    /* set default output to all source channels */
    ctx->out_channels = ctx->nchans;
    ctx->output_mode = ctx->acmod;
    if(ctx->lfeon)
        ctx->output_mode |= AC3_OUTPUT_LFEON;

    /* skip over portion of header which has already been read */
    skip_bits(gb, 16); // skip the sync_word
    skip_bits(gb, 16); // skip crc1
    skip_bits(gb, 8);  // skip fscod and frmsizecod
    skip_bits(gb, 11); // skip bsid, bsmod, and acmod
    if(ctx->acmod == AC3_ACMOD_STEREO) {
        skip_bits(gb, 2); // skip dsurmod
    } else {
        if((ctx->acmod & 1) && ctx->acmod != AC3_ACMOD_MONO)
            skip_bits(gb, 2); // skip cmixlev
        if(ctx->acmod & 4)
            skip_bits(gb, 2); // skip surmixlev
    }
    skip_bits1(gb); // skip lfeon

    /* read the rest of the bsi. read twice for dual mono mode. */
    i = !(ctx->acmod);
    do {
        ctx->dialnorm[i] = dialnorm_tab[get_bits(gb, 5)]; // dialogue normalization
        if (get_bits1(gb))
            skip_bits(gb, 8); //skip compression
        if (get_bits1(gb))
            skip_bits(gb, 8); //skip language code
        if (get_bits1(gb))
            skip_bits(gb, 7); //skip audio production information
    } while (i--);

    skip_bits(gb, 2); //skip copyright bit and original bitstream bit

    /* skip the timecodes (or extra bitstream information for Alternate Syntax)
       TODO: read & use the xbsi1 downmix levels */
    if (get_bits1(gb))
        skip_bits(gb, 14); //skip timecode1 / xbsi1
    if (get_bits1(gb))
        skip_bits(gb, 14); //skip timecode2 / xbsi2

    /* skip additional bitstream info */
    if (get_bits1(gb)) {
        i = get_bits(gb, 6);
        do {
            skip_bits(gb, 8);
        } while(i--);
    }

    /* set stereo downmixing coefficients
       reference: Section 7.8.2 Downmixing Into Two Channels */
    for(i=0; i<ctx->nfchans; i++) {
        ctx->downmix_coeffs[i][0] = gain_levels[ac3_default_coeffs[ctx->acmod][i][0]];
        ctx->downmix_coeffs[i][1] = gain_levels[ac3_default_coeffs[ctx->acmod][i][1]];
    }
    if(ctx->acmod > 1 && ctx->acmod & 1) {
        ctx->downmix_coeffs[1][0] = ctx->downmix_coeffs[1][1] = cmixlev;
    }
    if(ctx->acmod == AC3_ACMOD_2F1R || ctx->acmod == AC3_ACMOD_3F1R) {
        int nf = ctx->acmod - 2;
        ctx->downmix_coeffs[nf][0] = ctx->downmix_coeffs[nf][1] = surmixlev * LEVEL_MINUS_3DB;
    }
    if(ctx->acmod == AC3_ACMOD_2F2R || ctx->acmod == AC3_ACMOD_3F2R) {
        int nf = ctx->acmod - 4;
        ctx->downmix_coeffs[nf][0] = ctx->downmix_coeffs[nf+1][1] = surmixlev;
    }

    return 0;
}

/**
 * Decode the grouped exponents according to exponent strategy.
 * reference: Section 7.1.3 Exponent Decoding
 */
static void decode_exponents(GetBitContext *gb, int expstr, int ngrps,
                             uint8_t absexp, int8_t *dexps)
{
    int i, j, grp, grpsize;
    int dexp[256];
    int expacc, prevexp;

    /* unpack groups */
    grpsize = expstr + (expstr == EXP_D45);
    for(grp=0,i=0; grp<ngrps; grp++) {
        expacc = get_bits(gb, 7);
        dexp[i++] = exp_ungroup_tab[expacc][0];
        dexp[i++] = exp_ungroup_tab[expacc][1];
        dexp[i++] = exp_ungroup_tab[expacc][2];
    }

    /* convert to absolute exps and expand groups */
    prevexp = absexp;
    for(i=0; i<ngrps*3; i++) {
        prevexp = av_clip(prevexp + dexp[i]-2, 0, 24);
        for(j=0; j<grpsize; j++) {
            dexps[(i*grpsize)+j] = prevexp;
        }
    }
}

/**
 * Generate transform coefficients for each coupled channel in the coupling
 * range using the coupling coefficients and coupling coordinates.
 * reference: Section 7.4.3 Coupling Coordinate Format
 */
static void uncouple_channels(AC3DecodeContext *ctx)
{
    int i, j, ch, bnd, subbnd;

    subbnd = -1;
    i = ctx->startmant[CPL_CH];
    for(bnd=0; bnd<ctx->ncplbnd; bnd++) {
        do {
            subbnd++;
            for(j=0; j<12; j++) {
                for(ch=1; ch<=ctx->nfchans; ch++) {
                    if(ctx->chincpl[ch])
                        ctx->transform_coeffs[ch][i] = ctx->transform_coeffs[CPL_CH][i] * ctx->cplco[ch][bnd] * 8.0f;
                }
                i++;
            }
        } while(ctx->cplbndstrc[subbnd]);
    }
}

/**
 * Grouped mantissas for 3-level 5-level and 11-level quantization
 */
typedef struct {
    float b1_mant[3];
    float b2_mant[3];
    float b4_mant[2];
    int b1ptr;
    int b2ptr;
    int b4ptr;
} mant_groups;

/**
 * Get the transform coefficients for a particular channel
 * reference: Section 7.3 Quantization and Decoding of Mantissas
 */
static int get_transform_coeffs_ch(AC3DecodeContext *ctx, int ch_index, mant_groups *m)
{
    GetBitContext *gb = &ctx->gb;
    int i, gcode, tbap, start, end;
    uint8_t *exps;
    uint8_t *bap;
    float *coeffs;

    exps = ctx->dexps[ch_index];
    bap = ctx->bap[ch_index];
    coeffs = ctx->transform_coeffs[ch_index];
    start = ctx->startmant[ch_index];
    end = ctx->endmant[ch_index];

    for (i = start; i < end; i++) {
        tbap = bap[i];
        switch (tbap) {
            case 0:
                coeffs[i] = ((av_random(&ctx->dith_state) & 0xFFFF) / 65535.0f) - 0.5f;
                break;

            case 1:
                if(m->b1ptr > 2) {
                    gcode = get_bits(gb, 5);
                    m->b1_mant[0] = b1_mantissas[gcode][0];
                    m->b1_mant[1] = b1_mantissas[gcode][1];
                    m->b1_mant[2] = b1_mantissas[gcode][2];
                    m->b1ptr = 0;
                }
                coeffs[i] = m->b1_mant[m->b1ptr++];
                break;

            case 2:
                if(m->b2ptr > 2) {
                    gcode = get_bits(gb, 7);
                    m->b2_mant[0] = b2_mantissas[gcode][0];
                    m->b2_mant[1] = b2_mantissas[gcode][1];
                    m->b2_mant[2] = b2_mantissas[gcode][2];
                    m->b2ptr = 0;
                }
                coeffs[i] = m->b2_mant[m->b2ptr++];
                break;

            case 3:
                coeffs[i] = b3_mantissas[get_bits(gb, 3)];
                break;

            case 4:
                if(m->b4ptr > 1) {
                    gcode = get_bits(gb, 7);
                    m->b4_mant[0] = b4_mantissas[gcode][0];
                    m->b4_mant[1] = b4_mantissas[gcode][1];
                    m->b4ptr = 0;
                }
                coeffs[i] = m->b4_mant[m->b4ptr++];
                break;

            case 5:
                coeffs[i] = b5_mantissas[get_bits(gb, 4)];
                break;

            default:
                /* asymmetric dequantization */
                coeffs[i] = get_sbits(gb, qntztab[tbap]) * scale_factors[qntztab[tbap]-1];
                break;
        }
        coeffs[i] *= scale_factors[exps[i]];
    }

    return 0;
}

/**
 * Remove random dithering from coefficients with zero-bit mantissas
 * reference: Section 7.3.4 Dither for Zero Bit Mantissas (bap=0)
 */
static void remove_dithering(AC3DecodeContext *ctx) {
    int ch, i;
    int end=0;
    float *coeffs;
    uint8_t *bap;

    for(ch=1; ch<=ctx->nfchans; ch++) {
        if(!ctx->dithflag[ch]) {
            coeffs = ctx->transform_coeffs[ch];
            bap = ctx->bap[ch];
            if(ctx->chincpl[ch])
                end = ctx->startmant[CPL_CH];
            else
                end = ctx->endmant[ch];
            for(i=0; i<end; i++) {
                if(bap[i] == 0)
                    coeffs[i] = 0.0f;
            }
            if(ctx->chincpl[ch]) {
                bap = ctx->bap[CPL_CH];
                for(; i<ctx->endmant[CPL_CH]; i++) {
                    if(bap[i] == 0)
                        coeffs[i] = 0.0f;
                }
            }
        }
    }
}

/**
 * Get the transform coefficients.
 */
static int get_transform_coeffs(AC3DecodeContext * ctx)
{
    int ch, end;
    int got_cplchan = 0;
    mant_groups m;

    m.b1ptr = m.b2ptr = m.b4ptr = 3;

    for (ch = 1; ch <= ctx->nchans; ch++) {
        /* transform coefficients for full-bandwidth channel */
        if (get_transform_coeffs_ch(ctx, ch, &m))
            return -1;
        /* tranform coefficients for coupling channel come right after the
           coefficients for the first coupled channel*/
        if (ctx->chincpl[ch])  {
            if (!got_cplchan) {
                if (get_transform_coeffs_ch(ctx, CPL_CH, &m)) {
                    av_log(ctx->avctx, AV_LOG_ERROR, "error in decoupling channels\n");
                    return -1;
                }
                uncouple_channels(ctx);
                got_cplchan = 1;
            }
            end = ctx->endmant[CPL_CH];
        } else {
            end = ctx->endmant[ch];
        }
        do
            ctx->transform_coeffs[ch][end] = 0;
        while(++end < 256);
    }

    /* if any channel doesn't use dithering, zero appropriate coefficients */
    if(!ctx->dither_all)
        remove_dithering(ctx);

    return 0;
}

/**
 * Stereo rematrixing.
 * reference: Section 7.5.4 Rematrixing : Decoding Technique
 */
static void do_rematrixing(AC3DecodeContext *ctx)
{
    int bnd, i;
    int end, bndend;
    float tmp0, tmp1;

    end = FFMIN(ctx->endmant[1], ctx->endmant[2]);

    for(bnd=0; bnd<ctx->nrematbnd; bnd++) {
        if(ctx->rematflg[bnd]) {
            bndend = FFMIN(end, rematrix_band_tab[bnd+1]);
            for(i=rematrix_band_tab[bnd]; i<bndend; i++) {
                tmp0 = ctx->transform_coeffs[1][i];
                tmp1 = ctx->transform_coeffs[2][i];
                ctx->transform_coeffs[1][i] = tmp0 + tmp1;
                ctx->transform_coeffs[2][i] = tmp0 - tmp1;
            }
        }
    }
}

/**
 * Perform the 256-point IMDCT
 */
static void do_imdct_256(AC3DecodeContext *ctx, int chindex)
{
    int i, k;
    DECLARE_ALIGNED_16(float, x[128]);
    FFTComplex z[2][64];
    float *o_ptr = ctx->tmp_output;

    for(i=0; i<2; i++) {
        /* de-interleave coefficients */
        for(k=0; k<128; k++) {
            x[k] = ctx->transform_coeffs[chindex][2*k+i];
        }

        /* run standard IMDCT */
        ctx->imdct_256.fft.imdct_calc(&ctx->imdct_256, o_ptr, x, ctx->tmp_imdct);

        /* reverse the post-rotation & reordering from standard IMDCT */
        for(k=0; k<32; k++) {
            z[i][32+k].re = -o_ptr[128+2*k];
            z[i][32+k].im = -o_ptr[2*k];
            z[i][31-k].re =  o_ptr[2*k+1];
            z[i][31-k].im =  o_ptr[128+2*k+1];
        }
    }

    /* apply AC-3 post-rotation & reordering */
    for(k=0; k<64; k++) {
        o_ptr[    2*k  ] = -z[0][   k].im;
        o_ptr[    2*k+1] =  z[0][63-k].re;
        o_ptr[128+2*k  ] = -z[0][   k].re;
        o_ptr[128+2*k+1] =  z[0][63-k].im;
        o_ptr[256+2*k  ] = -z[1][   k].re;
        o_ptr[256+2*k+1] =  z[1][63-k].im;
        o_ptr[384+2*k  ] =  z[1][   k].im;
        o_ptr[384+2*k+1] = -z[1][63-k].re;
    }
}

/**
 * Inverse MDCT Transform.
 * Convert frequency domain coefficients to time-domain audio samples.
 * reference: Section 7.9.4 Transformation Equations
 */
static inline void do_imdct(AC3DecodeContext *ctx)
{
    int ch;
    int nchans;

    /* Don't perform the IMDCT on the LFE channel unless it's used in the output */
    nchans = ctx->nfchans;
    if(ctx->output_mode & AC3_OUTPUT_LFEON)
        nchans++;

    for (ch=1; ch<=nchans; ch++) {
        if (ctx->blksw[ch]) {
            do_imdct_256(ctx, ch);
        } else {
            ctx->imdct_512.fft.imdct_calc(&ctx->imdct_512, ctx->tmp_output,
                                          ctx->transform_coeffs[ch],
                                          ctx->tmp_imdct);
        }
        /* For the first half of the block, apply the window, add the delay
           from the previous block, and send to output */
        ctx->dsp.vector_fmul_add_add(ctx->output[ch-1], ctx->tmp_output,
                                     ctx->window, ctx->delay[ch-1], 0, 256, 1);
        /* For the second half of the block, apply the window and store the
           samples to delay, to be combined with the next block */
        ctx->dsp.vector_fmul_reverse(ctx->delay[ch-1], ctx->tmp_output+256,
                                     ctx->window, 256);
    }
}

/**
 * Downmix the output to mono or stereo.
 */
static void ac3_downmix(float samples[AC3_MAX_CHANNELS][256], int nfchans,
                        int output_mode, float coef[AC3_MAX_CHANNELS][2])
{
    int i, j;
    float v0, v1, s0, s1;

    for(i=0; i<256; i++) {
        v0 = v1 = s0 = s1 = 0.0f;
        for(j=0; j<nfchans; j++) {
            v0 += samples[j][i] * coef[j][0];
            v1 += samples[j][i] * coef[j][1];
            s0 += coef[j][0];
            s1 += coef[j][1];
        }
        v0 /= s0;
        v1 /= s1;
        if(output_mode == AC3_ACMOD_MONO) {
            samples[0][i] = (v0 + v1) * LEVEL_MINUS_3DB;
        } else if(output_mode == AC3_ACMOD_STEREO) {
            samples[0][i] = v0;
            samples[1][i] = v1;
        }
    }
}

/**
 * Parse an audio block from AC-3 bitstream.
 */
static int ac3_parse_audio_block(AC3DecodeContext *ctx, int blk)
{
    int nfchans = ctx->nfchans;
    int acmod = ctx->acmod;
    int i, bnd, seg, ch;
    GetBitContext *gb = &ctx->gb;
    uint8_t bit_alloc_stages[AC3_MAX_CHANNELS];

    memset(bit_alloc_stages, 0, AC3_MAX_CHANNELS);

    /* block switch flags */
    for (ch = 1; ch <= nfchans; ch++)
        ctx->blksw[ch] = get_bits1(gb);

    /* dithering flags */
    ctx->dither_all = 1;
    for (ch = 1; ch <= nfchans; ch++) {
        ctx->dithflag[ch] = get_bits1(gb);
        if(!ctx->dithflag[ch])
            ctx->dither_all = 0;
    }

    /* dynamic range */
    i = !(ctx->acmod);
    do {
        if(get_bits1(gb)) {
            ctx->dynrng[i] = dynrng_tab[get_bits(gb, 8)];
        } else if(blk == 0) {
            ctx->dynrng[i] = 1.0f;
        }
    } while(i--);

    /* coupling strategy */
    if (get_bits1(gb)) {
        memset(bit_alloc_stages, 3, AC3_MAX_CHANNELS);
        ctx->cplinu = get_bits1(gb);
        if (ctx->cplinu) {
            /* coupling in use */
            int cplbegf, cplendf;

            /* determine which channels are coupled */
            for (ch = 1; ch <= nfchans; ch++)
                ctx->chincpl[ch] = get_bits1(gb);

            /* phase flags in use */
            if (acmod == AC3_ACMOD_STEREO)
                ctx->phsflginu = get_bits1(gb);

            /* coupling frequency range and band structure */
            cplbegf = get_bits(gb, 4);
            cplendf = get_bits(gb, 4);
            if (3 + cplendf - cplbegf < 0) {
                av_log(ctx->avctx, AV_LOG_ERROR, "cplendf = %d < cplbegf = %d\n", cplendf, cplbegf);
                return -1;
            }
            ctx->ncplbnd = ctx->ncplsubnd = 3 + cplendf - cplbegf;
            ctx->startmant[CPL_CH] = cplbegf * 12 + 37;
            ctx->endmant[CPL_CH] = cplendf * 12 + 73;
            for (bnd = 0; bnd < ctx->ncplsubnd - 1; bnd++) {
                if (get_bits1(gb)) {
                    ctx->cplbndstrc[bnd] = 1;
                    ctx->ncplbnd--;
                }
            }
        } else {
            /* coupling not in use */
            for (ch = 1; ch <= nfchans; ch++)
                ctx->chincpl[ch] = 0;
        }
    }

    /* coupling coordinates */
    if (ctx->cplinu) {
        int cplcoe = 0;

        for (ch = 1; ch <= nfchans; ch++) {
            if (ctx->chincpl[ch]) {
                if (get_bits1(gb)) {
                    int mstrcplco, cplcoexp, cplcomant;
                    cplcoe = 1;
                    mstrcplco = 3 * get_bits(gb, 2);
                    for (bnd = 0; bnd < ctx->ncplbnd; bnd++) {
                        cplcoexp = get_bits(gb, 4);
                        cplcomant = get_bits(gb, 4);
                        if (cplcoexp == 15)
                            ctx->cplco[ch][bnd] = cplcomant / 16.0f;
                        else
                            ctx->cplco[ch][bnd] = (cplcomant + 16.0f) / 32.0f;
                        ctx->cplco[ch][bnd] *= scale_factors[cplcoexp + mstrcplco];
                    }
                }
            }
        }
        /* phase flags */
        if (acmod == AC3_ACMOD_STEREO && ctx->phsflginu && cplcoe) {
            for (bnd = 0; bnd < ctx->ncplbnd; bnd++) {
                if (get_bits1(gb))
                    ctx->cplco[2][bnd] = -ctx->cplco[2][bnd];
            }
        }
    }

    /* stereo rematrixing strategy and band structure */
    if (acmod == AC3_ACMOD_STEREO) {
        ctx->rematstr = get_bits1(gb);
        if (ctx->rematstr) {
            ctx->nrematbnd = 4;
            if(ctx->cplinu && ctx->startmant[CPL_CH] <= 61)
                ctx->nrematbnd -= 1 + (ctx->startmant[CPL_CH] == 37);
            for(bnd=0; bnd<ctx->nrematbnd; bnd++)
                ctx->rematflg[bnd] = get_bits1(gb);
        }
    }

    /* exponent strategies for each channel */
    ctx->expstr[CPL_CH] = EXP_REUSE;
    ctx->expstr[ctx->lfe_ch] = EXP_REUSE;
    for (ch = !ctx->cplinu; ch <= ctx->nchans; ch++) {
        if(ch == ctx->lfe_ch)
            ctx->expstr[ch] = get_bits(gb, 1);
        else
            ctx->expstr[ch] = get_bits(gb, 2);
        if(ctx->expstr[ch] != EXP_REUSE)
            bit_alloc_stages[ch] = 3;
    }

    /* channel bandwidth */
    for (ch = 1; ch <= nfchans; ch++) {
        ctx->startmant[ch] = 0;
        if (ctx->expstr[ch] != EXP_REUSE) {
            int prev = ctx->endmant[ch];
            if (ctx->chincpl[ch])
                ctx->endmant[ch] = ctx->startmant[CPL_CH];
            else {
                int chbwcod = get_bits(gb, 6);
                if (chbwcod > 60) {
                    av_log(ctx->avctx, AV_LOG_ERROR, "chbwcod = %d > 60", chbwcod);
                    return -1;
                }
                ctx->endmant[ch] = chbwcod * 3 + 73;
            }
            if(blk > 0 && ctx->endmant[ch] != prev)
                memset(bit_alloc_stages, 3, AC3_MAX_CHANNELS);
        }
    }
    ctx->startmant[ctx->lfe_ch] = 0;
    ctx->endmant[ctx->lfe_ch] = 7;

    /* decode exponents for each channel */
    for (ch = !ctx->cplinu; ch <= ctx->nchans; ch++) {
        if (ctx->expstr[ch] != EXP_REUSE) {
            int grpsize, ngrps;
            grpsize = 3 << (ctx->expstr[ch] - 1);
            if(ch == CPL_CH)
                ngrps = (ctx->endmant[ch] - ctx->startmant[ch]) / grpsize;
            else if(ch == ctx->lfe_ch)
                ngrps = 2;
            else
                ngrps = (ctx->endmant[ch] + grpsize - 4) / grpsize;
            ctx->dexps[ch][0] = get_bits(gb, 4) << !ch;
            decode_exponents(gb, ctx->expstr[ch], ngrps, ctx->dexps[ch][0],
                             &ctx->dexps[ch][ctx->startmant[ch]+!!ch]);
            if(ch != CPL_CH && ch != ctx->lfe_ch)
                skip_bits(gb, 2); /* skip gainrng */
        }
    }

    /* bit allocation information */
    if (get_bits1(gb)) {
        ctx->bit_alloc_params.sdecay = ff_sdecaytab[get_bits(gb, 2)] >> ctx->bit_alloc_params.halfratecod;
        ctx->bit_alloc_params.fdecay = ff_fdecaytab[get_bits(gb, 2)] >> ctx->bit_alloc_params.halfratecod;
        ctx->bit_alloc_params.sgain  = ff_sgaintab[get_bits(gb, 2)];
        ctx->bit_alloc_params.dbknee = ff_dbkneetab[get_bits(gb, 2)];
        ctx->bit_alloc_params.floor  = ff_floortab[get_bits(gb, 3)];
        for(ch=!ctx->cplinu; ch<=ctx->nchans; ch++) {
            bit_alloc_stages[ch] = FFMAX(bit_alloc_stages[ch], 2);
        }
    }

    /* signal-to-noise ratio offsets and fast gains (signal-to-mask ratios) */
    if (get_bits1(gb)) {
        int csnr;
        csnr = (get_bits(gb, 6) - 15) << 4;
        for (ch = !ctx->cplinu; ch <= ctx->nchans; ch++) { /* snr offset and fast gain */
            ctx->snroffst[ch] = (csnr + get_bits(gb, 4)) << 2;
            ctx->fgain[ch] = ff_fgaintab[get_bits(gb, 3)];
        }
        memset(bit_alloc_stages, 3, AC3_MAX_CHANNELS);
    }

    /* coupling leak information */
    if (ctx->cplinu && get_bits1(gb)) {
        ctx->bit_alloc_params.cplfleak = get_bits(gb, 3);
        ctx->bit_alloc_params.cplsleak = get_bits(gb, 3);
        bit_alloc_stages[CPL_CH] = FFMAX(bit_alloc_stages[CPL_CH], 2);
    }

    /* delta bit allocation information */
    if (get_bits1(gb)) {
        /* delta bit allocation exists (strategy) */
        for (ch = !ctx->cplinu; ch <= nfchans; ch++) {
            ctx->deltbae[ch] = get_bits(gb, 2);
            if (ctx->deltbae[ch] == DBA_RESERVED) {
                av_log(ctx->avctx, AV_LOG_ERROR, "delta bit allocation strategy reserved\n");
                return -1;
            }
            bit_alloc_stages[ch] = FFMAX(bit_alloc_stages[ch], 2);
        }
        /* channel delta offset, len and bit allocation */
        for (ch = !ctx->cplinu; ch <= nfchans; ch++) {
            if (ctx->deltbae[ch] == DBA_NEW) {
                ctx->deltnseg[ch] = get_bits(gb, 3);
                for (seg = 0; seg <= ctx->deltnseg[ch]; seg++) {
                    ctx->deltoffst[ch][seg] = get_bits(gb, 5);
                    ctx->deltlen[ch][seg] = get_bits(gb, 4);
                    ctx->deltba[ch][seg] = get_bits(gb, 3);
                }
            }
        }
    } else if(blk == 0) {
        for(ch=0; ch<=ctx->nchans; ch++) {
            ctx->deltbae[ch] = DBA_NONE;
        }
    }

    /* Bit allocation */
    for(ch=!ctx->cplinu; ch<=ctx->nchans; ch++) {
        if(bit_alloc_stages[ch] > 2) {
            /* Exponent mapping into PSD and PSD integration */
            ff_ac3_bit_alloc_calc_psd(ctx->dexps[ch],
                                      ctx->startmant[ch], ctx->endmant[ch],
                                      ctx->psd[ch], ctx->bndpsd[ch]);
        }
        if(bit_alloc_stages[ch] > 1) {
            /* Compute excitation function, Compute masking curve, and
               Apply delta bit allocation */
            ff_ac3_bit_alloc_calc_mask(&ctx->bit_alloc_params, ctx->bndpsd[ch],
                                       ctx->startmant[ch], ctx->endmant[ch],
                                       ctx->fgain[ch], (ch == ctx->lfe_ch),
                                       ctx->deltbae[ch], ctx->deltnseg[ch],
                                       ctx->deltoffst[ch], ctx->deltlen[ch],
                                       ctx->deltba[ch], ctx->mask[ch]);
        }
        if(bit_alloc_stages[ch] > 0) {
            /* Compute bit allocation */
            ff_ac3_bit_alloc_calc_bap(ctx->mask[ch], ctx->psd[ch],
                                      ctx->startmant[ch], ctx->endmant[ch],
                                      ctx->snroffst[ch],
                                      ctx->bit_alloc_params.floor,
                                      ctx->bap[ch]);
        }
    }

    /* unused dummy data */
    if (get_bits1(gb)) {
        int skipl = get_bits(gb, 9);
        while(skipl--)
            skip_bits(gb, 8);
    }

    /* unpack the transform coefficients
       this also uncouples channels if coupling is in use. */
    if (get_transform_coeffs(ctx)) {
        av_log(ctx->avctx, AV_LOG_ERROR, "Error in routine get_transform_coeffs\n");
        return -1;
    }

    /* recover coefficients if rematrixing is in use */
    if(ctx->acmod == AC3_ACMOD_STEREO)
        do_rematrixing(ctx);

    /* apply scaling to coefficients (headroom, dialnorm, dynrng) */
    for(ch=1; ch<=ctx->nchans; ch++) {
        float gain = 2.0f * ctx->mul_bias;
        if(ctx->acmod == AC3_ACMOD_DUALMONO) {
            gain *= ctx->dialnorm[ch-1] * ctx->dynrng[ch-1];
        } else {
            gain *= ctx->dialnorm[0] * ctx->dynrng[0];
        }
        for(i=0; i<ctx->endmant[ch]; i++) {
            ctx->transform_coeffs[ch][i] *= gain;
        }
    }

    do_imdct(ctx);

    /* downmix output if needed */
    if(ctx->nchans != ctx->out_channels && !((ctx->output_mode & AC3_OUTPUT_LFEON) &&
            ctx->nfchans == ctx->out_channels)) {
        ac3_downmix(ctx->output, ctx->nfchans, ctx->output_mode,
                    ctx->downmix_coeffs);
    }

    /* convert float to 16-bit integer */
    for(ch=0; ch<ctx->out_channels; ch++) {
        for(i=0; i<256; i++) {
            ctx->output[ch][i] += ctx->add_bias;
        }
        ctx->dsp.float_to_int16(ctx->int_output[ch], ctx->output[ch], 256);
    }

    return 0;
}

/**
 * Decode a single AC-3 frame.
 */
static int ac3_decode_frame(AVCodecContext * avctx, void *data, int *data_size, uint8_t *buf, int buf_size)
{
    AC3DecodeContext *ctx = (AC3DecodeContext *)avctx->priv_data;
    int16_t *out_samples = (int16_t *)data;
    int i, blk, ch, err;

    /* initialize the GetBitContext with the start of valid AC-3 Frame */
    init_get_bits(&ctx->gb, buf, buf_size * 8);

    /* parse the syncinfo */
    err = ac3_parse_header(ctx);
    if(err) {
        switch(err) {
            case AC3_PARSE_ERROR_SYNC:
                av_log(avctx, AV_LOG_ERROR, "frame sync error\n");
                break;
            case AC3_PARSE_ERROR_BSID:
                av_log(avctx, AV_LOG_ERROR, "invalid bitstream id\n");
                break;
            case AC3_PARSE_ERROR_SAMPLE_RATE:
                av_log(avctx, AV_LOG_ERROR, "invalid sample rate\n");
                break;
            case AC3_PARSE_ERROR_FRAME_SIZE:
                av_log(avctx, AV_LOG_ERROR, "invalid frame size\n");
                break;
            default:
                av_log(avctx, AV_LOG_ERROR, "invalid header\n");
                break;
        }
        return -1;
    }

    avctx->sample_rate = ctx->sampling_rate;
    avctx->bit_rate = ctx->bit_rate;

    /* check that reported frame size fits in input buffer */
    if(ctx->frame_size > buf_size) {
        av_log(avctx, AV_LOG_ERROR, "incomplete frame\n");
        return -1;
    }

    /* channel config */
    ctx->out_channels = ctx->nchans;
    if (avctx->channels == 0) {
        avctx->channels = ctx->out_channels;
    } else if(ctx->out_channels < avctx->channels) {
        av_log(avctx, AV_LOG_ERROR, "Cannot upmix AC3 from %d to %d channels.\n",
               ctx->out_channels, avctx->channels);
        return -1;
    }
    if(avctx->channels == 2) {
        ctx->output_mode = AC3_ACMOD_STEREO;
    } else if(avctx->channels == 1) {
        ctx->output_mode = AC3_ACMOD_MONO;
    } else if(avctx->channels != ctx->out_channels) {
        av_log(avctx, AV_LOG_ERROR, "Cannot downmix AC3 from %d to %d channels.\n",
               ctx->out_channels, avctx->channels);
        return -1;
    }
    ctx->out_channels = avctx->channels;

    /* parse the audio blocks */
    for (blk = 0; blk < NB_BLOCKS; blk++) {
        if (ac3_parse_audio_block(ctx, blk)) {
            av_log(avctx, AV_LOG_ERROR, "error parsing the audio block\n");
            *data_size = 0;
            return ctx->frame_size;
        }
        for (i = 0; i < 256; i++)
            for (ch = 0; ch < ctx->out_channels; ch++)
                *(out_samples++) = ctx->int_output[ch][i];
    }
    *data_size = NB_BLOCKS * 256 * avctx->channels * sizeof (int16_t);
    return ctx->frame_size;
}

/**
 * Uninitialize the AC-3 decoder.
 */
static int ac3_decode_end(AVCodecContext *avctx)
{
    AC3DecodeContext *ctx = (AC3DecodeContext *)avctx->priv_data;
    ff_mdct_end(&ctx->imdct_512);
    ff_mdct_end(&ctx->imdct_256);

    return 0;
}

AVCodec ac3_decoder = {
    .name = "ac3",
    .type = CODEC_TYPE_AUDIO,
    .id = CODEC_ID_AC3,
    .priv_data_size = sizeof (AC3DecodeContext),
    .init = ac3_decode_init,
    .close = ac3_decode_end,
    .decode = ac3_decode_frame,
};
