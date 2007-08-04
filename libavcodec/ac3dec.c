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
static const uint8_t rematrix_band_tbl[5] = { 13, 25, 37, 61, 253 };

/* table for exponent to scale_factor mapping
 * scale_factor[i] = 2 ^ -(i + 15)
 */
static float scale_factors[25];

/** table for grouping exponents */
static uint8_t exp_ungroup_tbl[128][3];


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

/* Adjustmens in dB gain */
#define LEVEL_MINUS_3DB         0.7071067811865476
#define LEVEL_MINUS_4POINT5DB   0.5946035575013605
#define LEVEL_MINUS_6DB         0.5000000000000000
#define LEVEL_PLUS_3DB          1.4142135623730951
#define LEVEL_PLUS_6DB          2.0000000000000000
#define LEVEL_ZERO              0.0000000000000000

static const float clevs[4] = { LEVEL_MINUS_3DB, LEVEL_MINUS_4POINT5DB,
    LEVEL_MINUS_6DB, LEVEL_MINUS_4POINT5DB };

static const float slevs[4] = { LEVEL_MINUS_3DB, LEVEL_MINUS_6DB, LEVEL_ZERO, LEVEL_MINUS_6DB };

#define AC3_OUTPUT_LFEON  8

typedef struct {
    int acmod;
    int cmixlev;
    int surmixlev;
    int dsurmod;

    int blksw[AC3_MAX_CHANNELS];
    int dithflag[AC3_MAX_CHANNELS];
    int dither_all;
    int cplinu;
    int chincpl[AC3_MAX_CHANNELS];
    int phsflginu;
    int cplcoe;
    uint32_t cplbndstrc;
    int rematstr;
    int nrematbnd;
    int rematflg[AC3_MAX_CHANNELS];
    int cplexpstr;
    int lfeexpstr;
    int chexpstr[5];
    int cplsnroffst;
    int cplfgain;
    int snroffst[5];
    int fgain[5];
    int lfesnroffst;
    int lfefgain;
    int cpldeltbae;
    int deltbae[5];
    int cpldeltnseg;
    uint8_t  cpldeltoffst[8];
    uint8_t  cpldeltlen[8];
    uint8_t  cpldeltba[8];
    int deltnseg[5];
    uint8_t  deltoffst[5][8];
    uint8_t  deltlen[5][8];
    uint8_t  deltba[5][8];

    /* Derived Attributes. */
    int      sampling_rate;
    int      bit_rate;
    int      frame_size;

    int      nchans;            //number of total channels
    int      nfchans;           //number of full-bandwidth channels
    int      lfeon;             //lfe channel in use
    int      output_mode;       ///< output channel configuration
    int      out_channels;      ///< number of output channels

    float    dynrng;            //dynamic range gain
    float    dynrng2;           //dynamic range gain for 1+1 mode
    float    cplco[5][18];      //coupling coordinates
    int      ncplbnd;           //number of coupling bands
    int      ncplsubnd;         //number of coupling sub bands
    int      cplstrtmant;       //coupling start mantissa
    int      cplendmant;        //coupling end mantissa
    int      endmant[5];        //channel end mantissas
    AC3BitAllocParameters bit_alloc_params; ///< bit allocation parameters

    int8_t   dcplexps[256];     //decoded coupling exponents
    int8_t   dexps[5][256];     //decoded fbw channel exponents
    int8_t   dlfeexps[256];     //decoded lfe channel exponents
    uint8_t  cplbap[256];       //coupling bit allocation pointers
    uint8_t  bap[5][256];       //fbw channel bit allocation pointers
    uint8_t  lfebap[256];       //lfe channel bit allocation pointers

    float transform_coeffs_cpl[256];
    DECLARE_ALIGNED_16(float, transform_coeffs[AC3_MAX_CHANNELS][256]);  //transform coefficients

    /* For IMDCT. */
    MDCTContext imdct_512;  //for 512 sample imdct transform
    MDCTContext imdct_256;  //for 256 sample imdct transform
    DSPContext  dsp;        //for optimization

    DECLARE_ALIGNED_16(float, output[AC3_MAX_CHANNELS][256]);   //output after imdct transform and windowing
    DECLARE_ALIGNED_16(float, delay[AC3_MAX_CHANNELS][256]);    //delay - added to the next block
    DECLARE_ALIGNED_16(float, tmp_imdct[256]);                  //temporary storage for imdct transform
    DECLARE_ALIGNED_16(float, tmp_output[512]);                 //temporary storage for output before windowing
    DECLARE_ALIGNED_16(float, window[256]);                     //window coefficients

    /* Miscellaneous. */
    GetBitContext gb;
    AVRandomState dith_state;   //for dither generation
} AC3DecodeContext;

/*********** BEGIN INIT HELPER FUNCTIONS ***********/
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
       for (j = 100; j > 0; j--) /* defaul to 100 iterations */
           bessel = bessel * tmp / (j * j) + 1;
       sum += bessel;
       local_window[i] = sum;
   }

   sum++;
   for (i = 0; i < 256; i++)
       window[i] = sqrt(local_window[i] / sum);
}

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

    //generate scale factors
    for (i = 0; i < 25; i++)
        scale_factors[i] = pow(2.0, -i);

    /* generate exponent tables
       reference: Section 7.1.3 Exponent Decoding */
    for(i=0; i<128; i++) {
        exp_ungroup_tbl[i][0] =  i / 25;
        exp_ungroup_tbl[i][1] = (i % 25) / 5;
        exp_ungroup_tbl[i][2] = (i % 25) % 5;
    }
}


static int ac3_decode_init(AVCodecContext *avctx)
{
    AC3DecodeContext *ctx = avctx->priv_data;

    ac3_common_init();
    ac3_tables_init();
    ff_mdct_init(&ctx->imdct_256, 8, 1);
    ff_mdct_init(&ctx->imdct_512, 9, 1);
    ac3_window_init(ctx->window);
    dsputil_init(&ctx->dsp, avctx);
    av_init_random(0, &ctx->dith_state);

    return 0;
}
/*********** END INIT FUNCTIONS ***********/

/**
 * Parses the 'sync info' and 'bit stream info' from the AC-3 bitstream.
 * GetBitContext within AC3DecodeContext must point to
 * start of the synchronized ac3 bitstream.
 */
static int ac3_parse_header(AC3DecodeContext *ctx)
{
    AC3HeaderInfo hdr;
    GetBitContext *gb = &ctx->gb;
    int err, i;

    err = ff_ac3_parse_header(gb->buffer, &hdr);
    if(err)
        return err;

    /* get decoding parameters from header info */
    ctx->bit_alloc_params.fscod       = hdr.fscod;
    ctx->acmod                        = hdr.acmod;
    ctx->cmixlev                      = hdr.cmixlev;
    ctx->surmixlev                    = hdr.surmixlev;
    ctx->dsurmod                      = hdr.dsurmod;
    ctx->lfeon                        = hdr.lfeon;
    ctx->bit_alloc_params.halfratecod = hdr.halfratecod;
    ctx->sampling_rate                = hdr.sample_rate;
    ctx->bit_rate                     = hdr.bit_rate;
    ctx->nchans                       = hdr.channels;
    ctx->nfchans                      = ctx->nchans - ctx->lfeon;
    ctx->frame_size                   = hdr.frame_size;

    /* set default output to all source channels */
    ctx->out_channels = ctx->nchans;
    ctx->output_mode = ctx->acmod;
    if(ctx->lfeon)
        ctx->output_mode |= AC3_OUTPUT_LFEON;

    /* skip over portion of header which has already been read */
    skip_bits(gb, 16); //skip the sync_word, sync_info->sync_word = get_bits(gb, 16);
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
        skip_bits(gb, 5); //skip dialog normalization
        if (get_bits1(gb))
            skip_bits(gb, 8); //skip compression
        if (get_bits1(gb))
            skip_bits(gb, 8); //skip language code
        if (get_bits1(gb))
            skip_bits(gb, 7); //skip audio production information
    } while (i--);

    skip_bits(gb, 2); //skip copyright bit and original bitstream bit

    /* FIXME: read & use the xbsi1 downmix levels */
    if (get_bits1(gb))
        skip_bits(gb, 14); //skip timecode1
    if (get_bits1(gb))
        skip_bits(gb, 14); //skip timecode2

    if (get_bits1(gb)) {
        i = get_bits(gb, 6); //additional bsi length
        do {
            skip_bits(gb, 8);
        } while(i--);
    }

    return 0;
}

/**
 * Decodes the grouped exponents.
 * This function decodes the coded exponents according to exponent strategy
 * and stores them in the decoded exponents buffer.
 *
 * @param[in]  gb      GetBitContext which points to start of coded exponents
 * @param[in]  expstr  Exponent coding strategy
 * @param[in]  ngrps   Number of grouped exponents
 * @param[in]  absexp  Absolute exponent or DC exponent
 * @param[out] dexps   Decoded exponents are stored in dexps
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
        dexp[i++] = exp_ungroup_tbl[expacc][0];
        dexp[i++] = exp_ungroup_tbl[expacc][1];
        dexp[i++] = exp_ungroup_tbl[expacc][2];
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
 * Generates transform coefficients for each coupled channel in the coupling
 * range using the coupling coefficients and coupling coordinates.
 * reference: Section 7.4.3 Coupling Coordinate Format
 */
static void uncouple_channels(AC3DecodeContext *ctx)
{
    int i, j, ch, bnd, subbnd;

    subbnd = -1;
    i = ctx->cplstrtmant;
    for(bnd=0; bnd<ctx->ncplbnd; bnd++) {
        do {
            subbnd++;
            for(j=0; j<12; j++) {
                for(ch=1; ch<=ctx->nfchans; ch++) {
                    if(ctx->chincpl[ch-1])
                        ctx->transform_coeffs[ch][i] = ctx->transform_coeffs_cpl[i] * ctx->cplco[ch-1][bnd] * 8.0f;
                }
                i++;
            }
        } while((ctx->cplbndstrc >> subbnd) & 1);
    }
}

typedef struct { /* grouped mantissas for 3-level 5-leve and 11-level quantization */
    float b1_mant[3];
    float b2_mant[3];
    float b4_mant[2];
    int b1ptr;
    int b2ptr;
    int b4ptr;
} mant_groups;

/* Get the transform coefficients for particular channel */
static int get_transform_coeffs_ch(AC3DecodeContext *ctx, int ch_index, mant_groups *m)
{
    GetBitContext *gb = &ctx->gb;
    int i, gcode, tbap, start, end;
    uint8_t *exps;
    uint8_t *bap;
    float *coeffs;

    if (ch_index >= 0) { /* fbw channels */
        exps = ctx->dexps[ch_index];
        bap = ctx->bap[ch_index];
        coeffs = ctx->transform_coeffs[ch_index + 1];
        start = 0;
        end = ctx->endmant[ch_index];
    } else if (ch_index == -1) {
        exps = ctx->dlfeexps;
        bap = ctx->lfebap;
        coeffs = ctx->transform_coeffs[0];
        start = 0;
        end = 7;
    } else {
        exps = ctx->dcplexps;
        bap = ctx->cplbap;
        coeffs = ctx->transform_coeffs_cpl;
        start = ctx->cplstrtmant;
        end = ctx->cplendmant;
    }


    for (i = start; i < end; i++) {
        tbap = bap[i];
        switch (tbap) {
            case 0:
                coeffs[i] = ((av_random(&ctx->dith_state) & 0xFFFF) * LEVEL_MINUS_3DB) / 32768.0f;
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
                coeffs[i] = get_sbits(gb, qntztab[tbap]) * scale_factors[qntztab[tbap]-1];
                break;
        }
        coeffs[i] *= scale_factors[exps[i]];
    }

    return 0;
}

/**
 * Removes random dithering from coefficients with zero-bit mantissas
 * reference: Section 7.3.4 Dither for Zero Bit Mantissas (bap=0)
 */
static void remove_dithering(AC3DecodeContext *ctx) {
    int ch, i;
    int end=0;
    float *coeffs;
    uint8_t *bap;

    for(ch=1; ch<=ctx->nfchans; ch++) {
        if(!ctx->dithflag[ch-1]) {
            coeffs = ctx->transform_coeffs[ch];
            bap = ctx->bap[ch-1];
            if(ctx->chincpl[ch-1])
                end = ctx->cplstrtmant;
            else
                end = ctx->endmant[ch-1];
            for(i=0; i<end; i++) {
                if(bap[i] == 0)
                    coeffs[i] = 0.0f;
            }
            if(ctx->chincpl[ch-1]) {
                bap = ctx->cplbap;
                for(; i<ctx->cplendmant; i++) {
                    if(bap[i] == 0)
                        coeffs[i] = 0.0f;
                }
            }
        }
    }
}

/* Get the transform coefficients.
 * This function extracts the tranform coefficients form the ac3 bitstream.
 * This function is called after bit allocation is performed.
 */
static int get_transform_coeffs(AC3DecodeContext * ctx)
{
    int i, end;
    int got_cplchan = 0;
    mant_groups m;

    m.b1ptr = m.b2ptr = m.b4ptr = 3;

    for (i = 0; i < ctx->nfchans; i++) {
        /* transform coefficients for individual channel */
        if (get_transform_coeffs_ch(ctx, i, &m))
            return -1;
        /* tranform coefficients for coupling channels */
        if (ctx->chincpl[i])  {
            if (!got_cplchan) {
                if (get_transform_coeffs_ch(ctx, -2, &m)) {
                    av_log(NULL, AV_LOG_ERROR, "error in decoupling channels\n");
                    return -1;
                }
                uncouple_channels(ctx);
                got_cplchan = 1;
            }
            end = ctx->cplendmant;
        } else
            end = ctx->endmant[i];
        do
            ctx->transform_coeffs[i + 1][end] = 0;
        while(++end < 256);
    }
    if (ctx->lfeon) {
        if (get_transform_coeffs_ch(ctx, -1, &m))
                return -1;
        for (i = 7; i < 256; i++) {
            ctx->transform_coeffs[0][i] = 0;
        }
    }

    /* if any channel doesn't use dithering, zero appropriate coefficients */
    if(!ctx->dither_all)
        remove_dithering(ctx);

    return 0;
}

/**
 * Performs stereo rematrixing.
 * reference: Section 7.5.4 Rematrixing : Decoding Technique
 */
static void do_rematrixing(AC3DecodeContext *ctx)
{
    int bnd, i;
    int end, bndend;
    float tmp0, tmp1;

    end = FFMIN(ctx->endmant[0], ctx->endmant[1]);

    for(bnd=0; bnd<ctx->nrematbnd; bnd++) {
        if(ctx->rematflg[bnd]) {
            bndend = FFMIN(end, rematrix_band_tbl[bnd+1]);
            for(i=rematrix_band_tbl[bnd]; i<bndend; i++) {
                tmp0 = ctx->transform_coeffs[1][i];
                tmp1 = ctx->transform_coeffs[2][i];
                ctx->transform_coeffs[1][i] = tmp0 + tmp1;
                ctx->transform_coeffs[2][i] = tmp0 - tmp1;
            }
        }
    }
}

/* This function performs the imdct on 256 sample transform
 * coefficients.
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

/* IMDCT Transform. */
static inline void do_imdct(AC3DecodeContext *ctx)
{
    int ch;

    if (ctx->output_mode & AC3_OUTPUT_LFEON) {
        ctx->imdct_512.fft.imdct_calc(&ctx->imdct_512, ctx->tmp_output,
                                      ctx->transform_coeffs[0], ctx->tmp_imdct);
        ctx->dsp.vector_fmul_add_add(ctx->output[0], ctx->tmp_output,
                                     ctx->window, ctx->delay[0], 384, 256, 1);
        ctx->dsp.vector_fmul_reverse(ctx->delay[0], ctx->tmp_output+256,
                                     ctx->window, 256);
    }
    for (ch=1; ch<=ctx->nfchans; ch++) {
        if (ctx->blksw[ch-1])
            do_imdct_256(ctx, ch);
        else
            ctx->imdct_512.fft.imdct_calc(&ctx->imdct_512, ctx->tmp_output,
                                          ctx->transform_coeffs[ch],
                                          ctx->tmp_imdct);

        ctx->dsp.vector_fmul_add_add(ctx->output[ch], ctx->tmp_output,
                                     ctx->window, ctx->delay[ch], 384, 256, 1);
        ctx->dsp.vector_fmul_reverse(ctx->delay[ch], ctx->tmp_output+256,
                                     ctx->window, 256);
    }
}

/* Parse the audio block from ac3 bitstream.
 * This function extract the audio block from the ac3 bitstream
 * and produces the output for the block. This function must
 * be called for each of the six audio block in the ac3 bitstream.
 */
static int ac3_parse_audio_block(AC3DecodeContext *ctx, int blk)
{
    int nfchans = ctx->nfchans;
    int acmod = ctx->acmod;
    int i, bnd, seg, grpsize, ch;
    GetBitContext *gb = &ctx->gb;
    int bit_alloc_flags = 0;
    int8_t *dexps;
    int mstrcplco, cplcoexp, cplcomant;
    int dynrng, chbwcod, ngrps, cplabsexp, skipl;

    for (i = 0; i < nfchans; i++) /*block switch flag */
        ctx->blksw[i] = get_bits1(gb);

    ctx->dither_all = 1;
    for (i = 0; i < nfchans; i++) { /* dithering flag */
        ctx->dithflag[i] = get_bits1(gb);
        if(!ctx->dithflag[i])
            ctx->dither_all = 0;
    }

    if (get_bits1(gb)) { /* dynamic range */
        dynrng = get_sbits(gb, 8);
        ctx->dynrng = (((dynrng & 0x1f) | 0x20) << 13) * pow(2.0, -(18 - (dynrng >> 5)));
    } else if(blk == 0) {
        ctx->dynrng = 1.0;
    }

    if(acmod == AC3_ACMOD_DUALMONO) { /* dynamic range 1+1 mode */
        if(get_bits1(gb)) {
            dynrng = get_sbits(gb, 8);
            ctx->dynrng2 = (((dynrng & 0x1f) | 0x20) << 13) * pow(2.0, -(18 - (dynrng >> 5)));
        } else if(blk == 0) {
            ctx->dynrng2 = 1.0;
        }
    }

    if (get_bits1(gb)) { /* coupling strategy */
        ctx->cplinu = get_bits1(gb);
        ctx->cplbndstrc = 0;
        if (ctx->cplinu) { /* coupling in use */
            int cplbegf, cplendf;

            for (i = 0; i < nfchans; i++)
                ctx->chincpl[i] = get_bits1(gb);

            if (acmod == AC3_ACMOD_STEREO)
                ctx->phsflginu = get_bits1(gb); //phase flag in use

            cplbegf = get_bits(gb, 4);
            cplendf = get_bits(gb, 4);

            if (3 + cplendf - cplbegf < 0) {
                av_log(NULL, AV_LOG_ERROR, "cplendf = %d < cplbegf = %d\n", cplendf, cplbegf);
                return -1;
            }

            ctx->ncplbnd = ctx->ncplsubnd = 3 + cplendf - cplbegf;
            ctx->cplstrtmant = cplbegf * 12 + 37;
            ctx->cplendmant = cplendf * 12 + 73;
            for (i = 0; i < ctx->ncplsubnd - 1; i++) /* coupling band structure */
                if (get_bits1(gb)) {
                    ctx->cplbndstrc |= 1 << i;
                    ctx->ncplbnd--;
                }
        } else {
            for (i = 0; i < nfchans; i++)
                ctx->chincpl[i] = 0;
        }
    }

    if (ctx->cplinu) {
        ctx->cplcoe = 0;

        for (i = 0; i < nfchans; i++)
            if (ctx->chincpl[i])
                if (get_bits1(gb)) { /* coupling co-ordinates */
                    ctx->cplcoe |= 1 << i;
                    mstrcplco = 3 * get_bits(gb, 2);
                    for (bnd = 0; bnd < ctx->ncplbnd; bnd++) {
                        cplcoexp = get_bits(gb, 4);
                        cplcomant = get_bits(gb, 4);
                        if (cplcoexp == 15)
                            ctx->cplco[i][bnd] = cplcomant / 16.0f;
                        else
                            ctx->cplco[i][bnd] = (cplcomant + 16.0f) / 32.0f;
                        ctx->cplco[i][bnd] *= scale_factors[cplcoexp + mstrcplco];
                    }
                }

        if (acmod == AC3_ACMOD_STEREO && ctx->phsflginu && (ctx->cplcoe & 1 || ctx->cplcoe & 2))
            for (bnd = 0; bnd < ctx->ncplbnd; bnd++)
                if (get_bits1(gb))
                    ctx->cplco[1][bnd] = -ctx->cplco[1][bnd];
    }

    if (acmod == AC3_ACMOD_STEREO) {/* rematrixing */
        ctx->rematstr = get_bits1(gb);
        if (ctx->rematstr) {
            ctx->nrematbnd = 4;
            if(ctx->cplinu && ctx->cplstrtmant <= 61)
                ctx->nrematbnd -= 1 + (ctx->cplstrtmant == 37);
            for(bnd=0; bnd<ctx->nrematbnd; bnd++)
                ctx->rematflg[bnd] = get_bits1(gb);
        }
    }

    ctx->cplexpstr = EXP_REUSE;
    ctx->lfeexpstr = EXP_REUSE;
    if (ctx->cplinu) /* coupling exponent strategy */
        ctx->cplexpstr = get_bits(gb, 2);
    for (i = 0; i < nfchans; i++)  /* channel exponent strategy */
        ctx->chexpstr[i] = get_bits(gb, 2);
    if (ctx->lfeon)  /* lfe exponent strategy */
        ctx->lfeexpstr = get_bits1(gb);

    for (i = 0; i < nfchans; i++) /* channel bandwidth code */
        if (ctx->chexpstr[i] != EXP_REUSE) {
            if (ctx->chincpl[i])
                ctx->endmant[i] = ctx->cplstrtmant;
            else {
                chbwcod = get_bits(gb, 6);
                if (chbwcod > 60) {
                    av_log(NULL, AV_LOG_ERROR, "chbwcod = %d > 60", chbwcod);
                    return -1;
                }
                ctx->endmant[i] = chbwcod * 3 + 73;
            }
        }

    if (ctx->cplexpstr != EXP_REUSE) {/* coupling exponents */
        bit_alloc_flags = 64;
        cplabsexp = get_bits(gb, 4) << 1;
        ngrps = (ctx->cplendmant - ctx->cplstrtmant) / (3 << (ctx->cplexpstr - 1));
        decode_exponents(gb, ctx->cplexpstr, ngrps, cplabsexp, ctx->dcplexps + ctx->cplstrtmant);
    }

    for (i = 0; i < nfchans; i++) /* fbw channel exponents */
        if (ctx->chexpstr[i] != EXP_REUSE) {
            bit_alloc_flags |= 1 << i;
            grpsize = 3 << (ctx->chexpstr[i] - 1);
            ngrps = (ctx->endmant[i] + grpsize - 4) / grpsize;
            dexps = ctx->dexps[i];
            dexps[0] = get_bits(gb, 4);
            decode_exponents(gb, ctx->chexpstr[i], ngrps, dexps[0], dexps + 1);
            skip_bits(gb, 2); /* skip gainrng */
        }

    if (ctx->lfeexpstr != EXP_REUSE) { /* lfe exponents */
        bit_alloc_flags |= 32;
        ctx->dlfeexps[0] = get_bits(gb, 4);
        decode_exponents(gb, ctx->lfeexpstr, 2, ctx->dlfeexps[0], ctx->dlfeexps + 1);
    }

    if (get_bits1(gb)) { /* bit allocation information */
        bit_alloc_flags = 127;
        ctx->bit_alloc_params.sdecay = ff_sdecaytab[get_bits(gb, 2)];
        ctx->bit_alloc_params.fdecay = ff_fdecaytab[get_bits(gb, 2)];
        ctx->bit_alloc_params.sgain  = ff_sgaintab[get_bits(gb, 2)];
        ctx->bit_alloc_params.dbknee = ff_dbkneetab[get_bits(gb, 2)];
        ctx->bit_alloc_params.floor  = ff_floortab[get_bits(gb, 3)];
    }

    if (get_bits1(gb)) { /* snroffset */
        int csnr;
        bit_alloc_flags = 127;
        csnr = (get_bits(gb, 6) - 15) << 4;
        if (ctx->cplinu) { /* coupling fine snr offset and fast gain code */
            ctx->cplsnroffst = (csnr + get_bits(gb, 4)) << 2;
            ctx->cplfgain = ff_fgaintab[get_bits(gb, 3)];
        }
        for (i = 0; i < nfchans; i++) { /* channel fine snr offset and fast gain code */
            ctx->snroffst[i] = (csnr + get_bits(gb, 4)) << 2;
            ctx->fgain[i] = ff_fgaintab[get_bits(gb, 3)];
        }
        if (ctx->lfeon) { /* lfe fine snr offset and fast gain code */
            ctx->lfesnroffst = (csnr + get_bits(gb, 4)) << 2;
            ctx->lfefgain = ff_fgaintab[get_bits(gb, 3)];
        }
    }

    if (ctx->cplinu && get_bits1(gb)) { /* coupling leak information */
        bit_alloc_flags |= 64;
        ctx->bit_alloc_params.cplfleak = get_bits(gb, 3);
        ctx->bit_alloc_params.cplsleak = get_bits(gb, 3);
    }

    if (get_bits1(gb)) { /* delta bit allocation information */
        bit_alloc_flags = 127;

        if (ctx->cplinu) {
            ctx->cpldeltbae = get_bits(gb, 2);
            if (ctx->cpldeltbae == DBA_RESERVED) {
                av_log(NULL, AV_LOG_ERROR, "coupling delta bit allocation strategy reserved\n");
                return -1;
            }
        }

        for (i = 0; i < nfchans; i++) {
            ctx->deltbae[i] = get_bits(gb, 2);
            if (ctx->deltbae[i] == DBA_RESERVED) {
                av_log(NULL, AV_LOG_ERROR, "delta bit allocation strategy reserved\n");
                return -1;
            }
        }

        if (ctx->cplinu)
            if (ctx->cpldeltbae == DBA_NEW) { /*coupling delta offset, len and bit allocation */
                ctx->cpldeltnseg = get_bits(gb, 3);
                for (seg = 0; seg <= ctx->cpldeltnseg; seg++) {
                    ctx->cpldeltoffst[seg] = get_bits(gb, 5);
                    ctx->cpldeltlen[seg] = get_bits(gb, 4);
                    ctx->cpldeltba[seg] = get_bits(gb, 3);
                }
            }

        for (i = 0; i < nfchans; i++)
            if (ctx->deltbae[i] == DBA_NEW) {/*channel delta offset, len and bit allocation */
                ctx->deltnseg[i] = get_bits(gb, 3);
                for (seg = 0; seg <= ctx->deltnseg[i]; seg++) {
                    ctx->deltoffst[i][seg] = get_bits(gb, 5);
                    ctx->deltlen[i][seg] = get_bits(gb, 4);
                    ctx->deltba[i][seg] = get_bits(gb, 3);
                }
            }
    } else if(blk == 0) {
        if(ctx->cplinu)
            ctx->cpldeltbae = DBA_NONE;
        for(i=0; i<nfchans; i++) {
            ctx->deltbae[i] = DBA_NONE;
        }
    }

    if (bit_alloc_flags) {
        if (ctx->cplinu && (bit_alloc_flags & 64))
            ac3_parametric_bit_allocation(&ctx->bit_alloc_params, ctx->cplbap,
                                          ctx->dcplexps, ctx->cplstrtmant,
                                          ctx->cplendmant, ctx->cplsnroffst,
                                          ctx->cplfgain, 0,
                                          ctx->cpldeltbae, ctx->cpldeltnseg,
                                          ctx->cpldeltoffst, ctx->cpldeltlen,
                                          ctx->cpldeltba);
        for (i = 0; i < nfchans; i++)
            if ((bit_alloc_flags >> i) & 1)
                ac3_parametric_bit_allocation(&ctx->bit_alloc_params,
                                              ctx->bap[i], ctx->dexps[i], 0,
                                              ctx->endmant[i], ctx->snroffst[i],
                                              ctx->fgain[i], 0, ctx->deltbae[i],
                                              ctx->deltnseg[i], ctx->deltoffst[i],
                                              ctx->deltlen[i], ctx->deltba[i]);
        if (ctx->lfeon && (bit_alloc_flags & 32))
            ac3_parametric_bit_allocation(&ctx->bit_alloc_params, ctx->lfebap,
                                          ctx->dlfeexps, 0, 7, ctx->lfesnroffst,
                                          ctx->lfefgain, 1,
                                          DBA_NONE, 0, NULL, NULL, NULL);
    }

    if (get_bits1(gb)) { /* unused dummy data */
        skipl = get_bits(gb, 9);
        while(skipl--)
            skip_bits(gb, 8);
    }
    /* unpack the transform coefficients
     * * this also uncouples channels if coupling is in use.
     */
    if (get_transform_coeffs(ctx)) {
        av_log(NULL, AV_LOG_ERROR, "Error in routine get_transform_coeffs\n");
        return -1;
    }

    /* recover coefficients if rematrixing is in use */
    if(ctx->acmod == AC3_ACMOD_STEREO)
        do_rematrixing(ctx);

    /* apply scaling to coefficients (headroom, dynrng) */
    if(ctx->lfeon) {
        for(i=0; i<7; i++) {
            ctx->transform_coeffs[0][i] *= 2.0f * ctx->dynrng;
        }
    }
    for(ch=1; ch<=ctx->nfchans; ch++) {
        float gain = 2.0f;
        if(ctx->acmod == AC3_ACMOD_DUALMONO && ch == 2) {
            gain *= ctx->dynrng2;
        } else {
            gain *= ctx->dynrng;
        }
        for(i=0; i<ctx->endmant[ch-1]; i++) {
            ctx->transform_coeffs[ch][i] *= gain;
        }
    }

    do_imdct(ctx);

    return 0;
}

static inline int16_t convert(int32_t i)
{
    if (i > 0x43c07fff)
        return 32767;
    else if (i <= 0x43bf8000)
        return -32768;
    else
        return (i - 0x43c00000);
}

/* Decode ac3 frame.
 *
 * @param avctx Pointer to AVCodecContext
 * @param data Pointer to pcm smaples
 * @param data_size Set to number of pcm samples produced by decoding
 * @param buf Data to be decoded
 * @param buf_size Size of the buffer
 */
static int ac3_decode_frame(AVCodecContext * avctx, void *data, int *data_size, uint8_t *buf, int buf_size)
{
    AC3DecodeContext *ctx = (AC3DecodeContext *)avctx->priv_data;
    int16_t *out_samples = (int16_t *)data;
    int i, j, k, start;
    int32_t *int_ptr[6];

    for (i = 0; i < 6; i++)
        int_ptr[i] = (int32_t *)(&ctx->output[i]);

    //Initialize the GetBitContext with the start of valid AC3 Frame.
    init_get_bits(&ctx->gb, buf, buf_size * 8);

    //Parse the syncinfo.
    if (ac3_parse_header(ctx)) {
        av_log(avctx, AV_LOG_ERROR, "\n");
        *data_size = 0;
        return buf_size;
    }

    avctx->sample_rate = ctx->sampling_rate;
    avctx->bit_rate = ctx->bit_rate;

    /* channel config */
    if (avctx->channels == 0) {
        avctx->channels = ctx->out_channels;
    }
    if(avctx->channels != ctx->out_channels) {
        av_log(avctx, AV_LOG_ERROR, "Cannot mix AC3 to %d channels.\n",
               avctx->channels);
        return -1;
    }

    //av_log(avctx, AV_LOG_INFO, "channels = %d \t bit rate = %d \t sampling rate = %d \n", avctx->channels, avctx->bit_rate * 1000, avctx->sample_rate);

    //Parse the Audio Blocks.
    for (i = 0; i < NB_BLOCKS; i++) {
        if (ac3_parse_audio_block(ctx, i)) {
            av_log(avctx, AV_LOG_ERROR, "error parsing the audio block\n");
            *data_size = 0;
            return ctx->frame_size;
        }
        start = (ctx->output_mode & AC3_OUTPUT_LFEON) ? 0 : 1;
        for (k = 0; k < 256; k++)
            for (j = start; j <= ctx->nfchans; j++)
                *(out_samples++) = convert(int_ptr[j][k]);
    }
    *data_size = NB_BLOCKS * 256 * avctx->channels * sizeof (int16_t);
    return ctx->frame_size;
}

/* Uninitialize ac3 decoder.
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

