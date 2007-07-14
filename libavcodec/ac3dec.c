/* AC3 Audio Decoder.
 *
 * Copyright (c) 2006 Kartikey Mahendra BHATT (bhattkm at gmail dot com).
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdio.h>
#include <stddef.h>
#include <math.h>
#include <inttypes.h>
#include <string.h>

#define ALT_BITSTREAM_READER

#include "ac3tab.h"
#include "ac3.h"
#include "ac3_decoder.h"
#include "avcodec.h"
#include "bitstream.h"
#include "dsputil.h"
#include "avutil.h"
#include "common.h"
#include "math.h"
#include "crc.h"

#define N 512   /* constant for IMDCT Block size */

#define MAX_CHANNELS    6
#define BLOCK_SIZE    256
#define AUDIO_BLOCKS    6

/* Exponent strategies. */
#define AC3_EXPSTR_D15      0x01
#define AC3_EXPSTR_D25      0x02
#define AC3_EXPSTR_D45      0x03
#define AC3_EXPSTR_REUSE    0x00

/* Bit allocation strategies. */
#define AC3_DBASTR_NEW      0x01
#define AC3_DBASTR_NONE     0x02
#define AC3_DBASTR_RESERVED 0x03
#define AC3_DBASTR_REUSE    0x00

/* Output and input configurations. */
#define AC3_OUTPUT_UNMODIFIED   0x01
#define AC3_OUTPUT_MONO         0x02
#define AC3_OUTPUT_STEREO       0x04
#define AC3_OUTPUT_DOLBY        0x08
#define AC3_OUTPUT_LFEON        0x10

#define AC3_INPUT_DUALMONO      0x00
#define AC3_INPUT_MONO          0x01
#define AC3_INPUT_STEREO        0x02
#define AC3_INPUT_3F            0x03
#define AC3_INPUT_2F_1R         0x04
#define AC3_INPUT_3F_1R         0x05
#define AC3_INPUT_2F_2R         0x06
#define AC3_INPUT_3F_2R         0x07

/* Mersenne Twister */
#define NMT 624
#define MMT 397
#define MATRIX_A    0x9908b0df
#define UPPER_MASK  0x80000000
#define LOWER_MASK  0x7fffffff


typedef struct {
    uint32_t mt[NMT];
    int      mti;
} dither_state;
/* Mersenne Twister */

typedef struct {
    uint32_t flags;
    uint16_t crc1;
    uint8_t  fscod;

    uint8_t  acmod;
    uint8_t  cmixlev;
    uint8_t  surmixlev;
    uint8_t  dsurmod;

    uint8_t  blksw;
    uint8_t  dithflag;
    uint8_t  cplinu;
    uint8_t  chincpl;
    uint8_t  phsflginu;
    uint8_t  cplbegf;
    uint8_t  cplendf;
    uint8_t  cplcoe;
    uint32_t cplbndstrc;
    uint8_t  rematstr;
    uint8_t  rematflg;
    uint8_t  cplexpstr;
    uint8_t  lfeexpstr;
    uint8_t  chexpstr[5];
    uint8_t  sdcycod;
    uint8_t  fdcycod;
    uint8_t  sgaincod;
    uint8_t  dbpbcod;
    uint8_t  floorcod;
    uint8_t  csnroffst;
    uint8_t  cplfsnroffst;
    uint8_t  cplfgaincod;
    uint8_t  fsnroffst[5];
    uint8_t  fgaincod[5];
    uint8_t  lfefsnroffst;
    uint8_t  lfefgaincod;
    uint8_t  cplfleak;
    uint8_t  cplsleak;
    uint8_t  cpldeltbae;
    uint8_t  deltbae[5];
    uint8_t  cpldeltnseg;
    uint8_t  cpldeltoffst[8];
    uint8_t  cpldeltlen[8];
    uint8_t  cpldeltba[8];
    uint8_t  deltnseg[5];
    uint8_t  deltoffst[5][8];
    uint8_t  deltlen[5][8];
    uint8_t  deltba[5][8];

    /* Derived Attributes. */
    int      sampling_rate;
    int      bit_rate;
    int      frame_size;

    int      nfchans;
    int      lfeon;

    float    chcoeffs[6];
    float    cplco[5][18];
    int      ncplbnd;
    int      ncplsubnd;
    int      cplstrtmant;
    int      cplendmant;
    int      endmant[5];
    uint8_t  dcplexps[256];
    uint8_t  dexps[5][256];
    uint8_t  dlfeexps[256];
    uint8_t  cplbap[256];
    uint8_t  bap[5][256];
    uint8_t  lfebap[256];
    int      blkoutput;

    DECLARE_ALIGNED_16(float, transform_coeffs[MAX_CHANNELS][BLOCK_SIZE]);

    /* For IMDCT. */
    MDCTContext imdct_512;  //N/8 point IFFT context
    MDCTContext imdct_256;  //N/4 point IFFT context
    DECLARE_ALIGNED_16(float, output[MAX_CHANNELS][BLOCK_SIZE]);
    DECLARE_ALIGNED_16(float, delay[MAX_CHANNELS][BLOCK_SIZE]);
    DECLARE_ALIGNED_16(float, tmp_imdct[BLOCK_SIZE]);
    DECLARE_ALIGNED_16(float, tmp_output[BLOCK_SIZE * 2]);

    /* Miscellaneous. */
    GetBitContext gb;
    dither_state  dith_state;
} AC3DecodeContext;


/* BEGIN Mersenne Twister Code. */
static void dither_seed(dither_state *state, uint32_t seed)
{
    if (seed == 0)
        seed = 0x1f2e3d4c;

    state->mt[0] = seed;
    for (state->mti = 1; state->mti < NMT; state->mti++)
        state->mt[state->mti] = ((69069 * state->mt[state->mti - 1]) + 1);
}

static uint32_t dither_uint32(dither_state *state)
{
    uint32_t y;
    static const uint32_t mag01[2] = { 0x00, MATRIX_A };
    int kk;

    if (state->mti >= NMT) {
        for (kk = 0; kk < NMT - MMT; kk++) {
            y = (state->mt[kk] & UPPER_MASK) | (state->mt[kk + 1] & LOWER_MASK);
            state->mt[kk] = state->mt[kk + MMT] ^ (y >> 1) ^ mag01[y & 0x01];
        }
        for (;kk < NMT - 1; kk++) {
            y = (state->mt[kk] & UPPER_MASK) | (state->mt[kk + 1] & LOWER_MASK);
            state->mt[kk] = state->mt[kk + (MMT - NMT)] ^ (y >> 1) ^ mag01[y & 0x01];
        }
        y = (state->mt[NMT - 1] & UPPER_MASK) | (state->mt[0] & LOWER_MASK);
        state->mt[NMT - 1] = state->mt[MMT - 1] ^ (y >> 1) ^ mag01[y & 0x01];

        state->mti = 0;
    }

    y = state->mt[state->mti++];
    y ^= (y >> 11);
    y ^= ((y << 7) & 0x9d2c5680);
    y ^= ((y << 15) & 0xefc60000);
    y ^= (y >> 18);

    return y;
}

static inline int16_t dither_int16(dither_state *state)
{
    return ((dither_uint32(state) << 16) >> 16);
}

/* END Mersenne Twister */

/**
 * Generate a Kaiser Window.
 */
static void
k_window_init(int alpha, float *window, int n, int iter)
{
    int j, k;
    float a, x;
    a = alpha * M_PI / n;
    a = a*a;
    for(k=0; k<n; k++) {
        x = k * (n - k) * a;
        window[k] = 1.0;
        for(j=iter; j>0; j--) {
            window[k] = (window[k] * x / (j*j)) + 1.0;
        }
    }
}

/**
 * Generate a Kaiser-Bessel Derived Window.
 * @param alpha  determines window shape
 * @param window array to fill with window values
 * @param n      length of the window
 * @param iter   number of iterations to use in BesselI0
 */
static void
kbd_window_init(int alpha, float *window, int n, int iter)
{
    int k, n2;
    float *kwindow;

    n2 = n >> 1;
    kwindow = &window[n2];
    k_window_init(alpha, kwindow, n2, iter);
    window[0] = kwindow[0];
    for(k=1; k<n2; k++) {
        window[k] = window[k-1] + kwindow[k];
    }
    for(k=0; k<n2; k++) {
        window[k] = sqrt(window[k] / (window[n2-1]+1));
        window[n-1-k] = window[k];
    }
}

static void generate_quantizers_table(int16_t quantizers[], int level, int length)
{
    int i;

    for (i = 0; i < length; i++)
        quantizers[i] = ((2 * i - level + 1) << 15) / level;
}

static void generate_quantizers_table_1(int16_t quantizers[], int level, int length1, int length2, int size)
{
    int i, j;
    int16_t v;

    for (i = 0; i < length1; i++) {
        v = ((2 * i - level + 1) << 15) / level;
        for (j = 0; j < length2; j++)
            quantizers[i * length2 + j] = v;
    }

    for (i = length1 * length2; i < size; i++)
        quantizers[i] = 0;
}

static void generate_quantizers_table_2(int16_t quantizers[], int level, int length1, int length2, int size)
{
    int i, j;
    int16_t v;

    for (i = 0; i < length1; i++) {
        v = ((2 * (i % level) - level + 1) << 15) / level;
        for (j = 0; j < length2; j++)
            quantizers[i * length2 + j] = v;
    }

    for (i = length1 * length2; i < size; i++)
        quantizers[i] = 0;

}

static void generate_quantizers_table_3(int16_t quantizers[], int level, int length1, int length2, int size)
{
    int i, j;

    for (i = 0; i < length1; i++)
        for (j = 0; j < length2; j++)
            quantizers[i * length2 + j] = ((2 * (j % level) - level + 1) << 15) / level;

    for (i = length1 * length2; i < size; i++)
        quantizers[i] = 0;
}

static void ac3_tables_init(void)
{
    int i, j, k, l, v;
    /* compute bndtab and masktab from bandsz */
    k = 0;
    l = 0;
    for(i=0;i<50;i++) {
        bndtab[i] = l;
        v = bndsz[i];
        for(j=0;j<v;j++) masktab[k++]=i;
        l += v;
    }
    masktab[253] = masktab[254] = masktab[255] = 0;
    bndtab[50] = 0;

    /* Exponent Decoding Tables */
    for (i = 0; i < 5; i++) {
        v = i - 2;
        for (j = 0; j < 25; j++)
            exp_1[i * 25 + j] = v;
    }

    for (i = 0; i < 25; i++) {
        v = (i % 5) - 2;
        for (j = 0; j < 5; j++)
            exp_2[i * 5 + j] = v;
    }

    for (i = 0; i < 25; i++) {
        v = -2;
        for (j = 0; j < 5; j++)
            exp_3[i * 5 + j] = v++;
    }

    for (i = 125; i < 128; i++)
        exp_1[i] = exp_2[i] = exp_3[i] = 25;
    /* End Exponent Decoding Tables */

    /* Quantizer ungrouping tables. */
    // for level-3 quantizers
    generate_quantizers_table_1(l3_quantizers_1, 3, 3, 9, 32);
    generate_quantizers_table_2(l3_quantizers_2, 3, 9, 3, 32);
    generate_quantizers_table_3(l3_quantizers_3, 3, 9, 3, 32);

    //for level-5 quantizers
    generate_quantizers_table_1(l5_quantizers_1, 5, 5, 25, 128);
    generate_quantizers_table_2(l5_quantizers_2, 5, 25, 5, 128);
    generate_quantizers_table_3(l5_quantizers_3, 5, 25, 5, 128);

    //for level-7 quantizers
    generate_quantizers_table(l7_quantizers, 7, 7);

    //for level-4 quantizers
    generate_quantizers_table_2(l11_quantizers_1, 11, 11, 11, 128);
    generate_quantizers_table_3(l11_quantizers_2, 11, 11, 11, 128);

    //for level-15 quantizers
    generate_quantizers_table(l15_quantizers, 15, 15);

    /* Kaiser-Bessel derived window. */
    kbd_window_init(5, window, 256, 100);
}


static int ac3_decode_init(AVCodecContext *avctx)
{
    AC3DecodeContext *ctx = avctx->priv_data;

    ac3_tables_init();
    ff_mdct_init(&ctx->imdct_256, 8, 1);
    ff_mdct_init(&ctx->imdct_512, 9, 1);
    dither_seed(&ctx->dith_state, 0);

    return 0;
}

static int ac3_synchronize(uint8_t *buf, int buf_size)
{
    int i;

    for (i = 0; i < buf_size - 1; i++)
        if (buf[i] == 0x0b && buf[i + 1] == 0x77)
            return i;

    return -1;
}

//Returns -1 when 'fscod' is not valid;
static int ac3_parse_sync_info(AC3DecodeContext *ctx)
{
    GetBitContext *gb = &ctx->gb;
    int frmsizecod, bsid;

    skip_bits(gb, 16); //skip the sync_word, sync_info->sync_word = get_bits(gb, 16);
    ctx->crc1 = get_bits(gb, 16);
    ctx->fscod = get_bits(gb, 2);
    if (ctx->fscod == 0x03)
        return 0;
    frmsizecod = get_bits(gb, 6);
    if (frmsizecod >= 38)
        return 0;
    ctx->sampling_rate = ac3_freqs[ctx->fscod];
    ctx->bit_rate = ac3_bitratetab[frmsizecod >> 1];

    /* we include it here in order to determine validity of ac3 frame */
    bsid = get_bits(gb, 5);
    if (bsid > 0x08)
        return 0;
    skip_bits(gb, 3); //skip the bsmod, bsi->bsmod = get_bits(gb, 3);

    switch (ctx->fscod) {
        case 0x00:
            ctx->frame_size = 4 * ctx->bit_rate;
            return ctx->frame_size;
        case 0x01:
            ctx->frame_size = 2 * (320 * ctx->bit_rate / 147 + (frmsizecod & 1));
            return ctx->frame_size;
        case 0x02:
            ctx->frame_size =  6 * ctx->bit_rate;
            return ctx->frame_size;
    }

    /* never reached */
    return 0;
}

static void ac3_parse_bsi(AC3DecodeContext *ctx)
{
    GetBitContext *gb = &ctx->gb;
    int i;

    ctx->cmixlev = 0;
    ctx->surmixlev = 0;
    ctx->dsurmod = 0;
    ctx->nfchans = 0;
    ctx->cpldeltbae = AC3_DBASTR_NONE;
    ctx->cpldeltnseg = 0;
    for (i = 0; i < 5; i++) {
        ctx->deltbae[i] = AC3_DBASTR_NONE;
        ctx->deltnseg[i] = 0;
    }

    ctx->acmod = get_bits(gb, 3);
    ctx->nfchans = nfchans_tbl[ctx->acmod];

    if (ctx->acmod & 0x01 && ctx->acmod != 0x01)
        ctx->cmixlev = get_bits(gb, 2);
    if (ctx->acmod & 0x04)
        ctx->surmixlev = get_bits(gb, 2);
    if (ctx->acmod == 0x02)
        ctx->dsurmod = get_bits(gb, 2);

    ctx->lfeon = get_bits1(gb);

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
}

/* Decodes the grouped exponents and stores them
 * in decoded exponents (dexps).
 * The code is derived from liba52.
 * Uses liba52 tables.
 */
static int decode_exponents(GetBitContext *gb, int expstr, int ngrps, uint8_t absexp, uint8_t *dexps)
{
    int exps;

    while (ngrps--) {
        exps = get_bits(gb, 7);

        absexp += exp_1[exps];
        if (absexp > 24) {
            av_log(NULL, AV_LOG_ERROR, "Absolute Exponent > 24, ngrp = %d\n", ngrps);
            return -ngrps;
        }
        switch (expstr) {
            case AC3_EXPSTR_D45:
                *(dexps++) = absexp;
                *(dexps++) = absexp;
            case AC3_EXPSTR_D25:
                *(dexps++) = absexp;
            case AC3_EXPSTR_D15:
                *(dexps++) = absexp;
        }

        absexp += exp_2[exps];
        if (absexp > 24) {
            av_log(NULL, AV_LOG_ERROR, "Absolute Exponent > 24, ngrp = %d\n", ngrps);
            return -ngrps;
        }
        switch (expstr) {
            case AC3_EXPSTR_D45:
                *(dexps++) = absexp;
                *(dexps++) = absexp;
            case AC3_EXPSTR_D25:
                *(dexps++) = absexp;
            case AC3_EXPSTR_D15:
                *(dexps++) = absexp;
        }

        absexp += exp_3[exps];
        if (absexp > 24) {
            av_log(NULL, AV_LOG_ERROR, "Absolute Exponent > 24, ngrp = %d\n", ngrps);
            return -ngrps;
        }
        switch (expstr) {
            case AC3_EXPSTR_D45:
                *(dexps++) = absexp;
                *(dexps++) = absexp;
            case AC3_EXPSTR_D25:
                *(dexps++) = absexp;
            case AC3_EXPSTR_D15:
                *(dexps++) = absexp;
        }
    }

    return 0;
}

static inline int logadd(int a, int b)
{
    int c = a - b;
    int address;

    address = FFMIN((ABS(c) >> 1), 255);

    if (c >= 0)
        return (a + latab[address]);
    else
        return (b + latab[address]);
}

static inline int calc_lowcomp(int a, int b0, int b1, int bin)
{
    if (bin < 7) {
        if ((b0 + 256) == b1)
            a = 384;
        else if (b0 > b1)
            a = FFMAX(0, (a - 64));
    }
    else if (bin < 20) {
        if ((b0 + 256) == b1)
            a = 320;
        else if (b0 > b1)
            a = FFMAX(0, (a - 64));
    }
    else
        a = FFMAX(0, (a - 128));

    return a;
}

/* do the bit allocation for chnl.
 * chnl = 0 to 4 - fbw channel
 * chnl = 5 coupling channel
 * chnl = 6 lfe channel
 */
static void do_bit_allocation(AC3DecodeContext *ctx, int chnl)
{
    int16_t psd[256], bndpsd[50], excite[50], mask[50], delta;
    int sdecay, fdecay, sgain, dbknee, floor;
    int lowcomp = 0, fgain = 0, snroffset = 0, fastleak = 0, slowleak = 0, do_delta = 0;
    int start = 0, end = 0, bin = 0, i = 0, j = 0, k = 0, lastbin = 0, bndstrt = 0;
    int bndend = 0, begin = 0, deltnseg = 0, band = 0, seg = 0, address = 0;
    int fscod = ctx->fscod;
    uint8_t *deltoffst = 0, *deltlen = 0, *deltba = 0;
    uint8_t *exps = 0, *bap = 0;

    /* initialization */
    sdecay = sdecaytab[ctx->sdcycod];
    fdecay = fdecaytab[ctx->fdcycod];
    sgain = sgaintab[ctx->sgaincod];
    dbknee = dbkneetab[ctx->dbpbcod];
    floor = floortab[ctx->floorcod];

    if (chnl == 5) {
        start = ctx->cplstrtmant;
        end = ctx->cplendmant;
        fgain = fgaintab[ctx->cplfgaincod];
        snroffset = (((ctx->csnroffst - 15) << 4) + ctx->cplfsnroffst) << 2;
        fastleak = (ctx->cplfleak << 8) + 768;
        slowleak = (ctx->cplsleak << 8) + 768;
        exps = ctx->dcplexps;
        bap = ctx->cplbap;
        if (ctx->cpldeltbae == AC3_DBASTR_NEW || ctx->deltbae == AC3_DBASTR_REUSE) {
            do_delta = 1;
            deltnseg = ctx->cpldeltnseg;
            deltoffst = ctx->cpldeltoffst;
            deltlen = ctx->cpldeltlen;
            deltba = ctx->cpldeltba;
        }
    }
    else if (chnl == 6) {
        start = 0;
        end = 7;
        lowcomp = 0;
        fastleak = 0;
        slowleak = 0;
        fgain = fgaintab[ctx->lfefgaincod];
        snroffset = (((ctx->csnroffst - 15) << 4) + ctx->lfefsnroffst) << 2;
        exps = ctx->dlfeexps;
        bap = ctx->lfebap;
    }
    else {
        start = 0;
        end = ctx->endmant[chnl];
        lowcomp = 0;
        fastleak = 0;
        slowleak = 0;
        fgain = fgaintab[ctx->fgaincod[chnl]];
        snroffset = (((ctx->csnroffst - 15) << 4) + ctx->fsnroffst[chnl]) << 2;
        exps = ctx->dexps[chnl];
        bap = ctx->bap[chnl];
        if (ctx->deltbae[chnl] == AC3_DBASTR_NEW || ctx->deltbae[chnl] == AC3_DBASTR_REUSE) {
            do_delta = 1;
            deltnseg = ctx->deltnseg[chnl];
            deltoffst = ctx->deltoffst[chnl];
            deltlen = ctx->deltlen[chnl];
            deltba = ctx->deltba[chnl];
        }
    }

    for (bin = start; bin < end; bin++) /* exponent mapping into psd */
        psd[bin] = (3072 - (exps[bin] << 7));

    /* psd integration */
    j = start;
    k = masktab[start];
    do {
        lastbin = FFMIN((bndtab[k] + bndsz[k]), end);
        bndpsd[k] = psd[j];
        j++;
        for (i = j; i < lastbin; i++) {
            bndpsd[k] = logadd(bndpsd[k], psd[j]);
            j++;
        }
        k++;
    } while (end > lastbin);

    /* compute the excite function */
    bndstrt = masktab[start];
    bndend = masktab[end - 1] + 1;
    if (bndstrt == 0) {
        lowcomp = calc_lowcomp(lowcomp, bndpsd[0], bndpsd[1], 0);
        excite[0] = bndpsd[0] - fgain - lowcomp;
        lowcomp = calc_lowcomp(lowcomp, bndpsd[1], bndpsd[2], 1);
        excite[1] = bndpsd[1] - fgain - lowcomp;
        begin = 7;
        for (bin = 2; bin < 7; bin++) {
            if ((bndend != 7) || (bin != 6))
                lowcomp = calc_lowcomp(lowcomp, bndpsd[bin], bndpsd[bin + 1], bin);
            fastleak = bndpsd[bin] - fgain;
            slowleak = bndpsd[bin] - sgain;
            excite[bin] = fastleak - lowcomp;
            if ((bndend != 7) || (bin != 6))
                if (bndpsd[bin] <= bndpsd[bin + 1]) {
                    begin = bin + 1;
                    break;
                }
        }
        for (bin = begin; bin < FFMIN(bndend, 22); bin++) {
            if ((bndend != 7) || (bin != 6))
                lowcomp = calc_lowcomp(lowcomp, bndpsd[bin], bndpsd[bin + 1], bin);
            fastleak -= fdecay;
            fastleak = FFMAX(fastleak, (bndpsd[bin] - fgain));
            slowleak -= sdecay;
            slowleak = FFMAX(slowleak, (bndpsd[bin] - sgain));
            excite[bin] = FFMAX((fastleak - lowcomp), slowleak);
        }
        begin = 22;
    }
    else {
        begin = bndstrt;
    }
    for (bin = begin; bin < bndend; bin++) {
        fastleak -= fdecay;
        fastleak = FFMAX(fastleak, (bndpsd[bin] - fgain));
        slowleak -= sdecay;
        slowleak = FFMAX(slowleak, (bndpsd[bin] - sgain));
        excite[bin] = FFMAX(fastleak, slowleak);
    }

    /* compute the masking curve */
    for (bin = bndstrt; bin < bndend; bin++) {
        if (bndpsd[bin] < dbknee)
            excite[bin] += ((dbknee - bndpsd[bin]) >> 2);
        mask[bin] = FFMAX(excite[bin], hth[bin][fscod]);
    }

    /* apply the delta bit allocation */
    if (do_delta) {
        band = 0;
        for (seg = 0; seg < deltnseg + 1; seg++) {
            band += deltoffst[seg];
            if (deltba[seg] >= 4)
                delta = (deltba[seg] - 3) << 7;
            else
                delta = (deltba[seg] - 4) << 7;
            for (k = 0; k < deltlen[seg]; k++) {
                mask[band] += delta;
                band++;
            }
        }
    }

    /*compute the bit allocation */
    i = start;
    j = masktab[start];
    do {
        lastbin = FFMIN((bndtab[j] + bndsz[j]), end);
        mask[j] -= snroffset;
        mask[j] -= floor;
        if (mask[j] < 0)
            mask[j] = 0;
        mask[j] &= 0x1fe0;
        mask[j] += floor;
        for (k = i; k < lastbin; k++) {
            address = (psd[i] - mask[j]) >> 5;
            address = FFMIN(63, (FFMAX(0, address)));
            bap[i] = baptab[address];
            i++;
        }
        j++;
    } while (end > lastbin);
}

/* Check if snroffsets are zero. */
static int is_snr_offsets_zero(AC3DecodeContext *ctx)
{
    int i;

    if ((ctx->csnroffst) || (ctx->cplinu && ctx->cplfsnroffst) ||
            (ctx->lfeon && ctx->lfefsnroffst))
        return 0;

    for (i = 0; i < ctx->nfchans; i++)
        if (ctx->fsnroffst[i])
            return 0;

    return 1;
}

typedef struct { /* grouped mantissas for 3-level 5-leve and 11-level quantization */
    int16_t l3_quantizers[3];
    int16_t l5_quantizers[3];
    int16_t l11_quantizers[2];
    int l3ptr;
    int l5ptr;
    int l11ptr;
} mant_groups;

#define TRANSFORM_COEFF(tc, m, e, f) (tc) = (m) * (f)[(e)]

/* Get the transform coefficients for coupling channel and uncouple channels.
 * The coupling transform coefficients starts at the the cplstrtmant, which is
 * equal to endmant[ch] for fbw channels. Hence we can uncouple channels before
 * getting transform coefficients for the channel.
 */
static int get_transform_coeffs_cpling(AC3DecodeContext *ctx, mant_groups *m)
{
    GetBitContext *gb = &ctx->gb;
    int ch, start, end, cplbndstrc, bnd, gcode, tbap;
    float cplcos[5], cplcoeff;
    uint8_t *exps = ctx->dcplexps;
    uint8_t *bap = ctx->cplbap;

    cplbndstrc = ctx->cplbndstrc;
    start = ctx->cplstrtmant;
    bnd = 0;

    while (start < ctx->cplendmant) {
        end = start + 12;
        while (cplbndstrc & 1) {
            end += 12;
            cplbndstrc >>= 1;
        }
        cplbndstrc >>= 1;
        for (ch = 0; ch < ctx->nfchans; ch++)
            cplcos[ch] = ctx->chcoeffs[ch] * ctx->cplco[ch][bnd];
        bnd++;

        while (start < end) {
            tbap = bap[start];
            switch(tbap) {
                case 0:
                    for (ch = 0; ch < ctx->nfchans; ch++)
                        if (((ctx->chincpl) >> ch) & 1) {
                            if ((ctx->dithflag >> ch) & 1) {
                                TRANSFORM_COEFF(cplcoeff, dither_int16(&ctx->dith_state), exps[start], scale_factors);
                                ctx->transform_coeffs[ch + 1][start] = cplcoeff * cplcos[ch] * LEVEL_MINUS_3DB;
                            } else
                                ctx->transform_coeffs[ch + 1][start] = 0;
                        }
                    start++;
                    continue;
                case 1:
                    if (m->l3ptr > 2) {
                        gcode = get_bits(gb, 5);
                        m->l3_quantizers[0] = l3_quantizers_1[gcode];
                        m->l3_quantizers[1] = l3_quantizers_2[gcode];
                        m->l3_quantizers[2] = l3_quantizers_3[gcode];
                        m->l3ptr = 0;
                    }
                    TRANSFORM_COEFF(cplcoeff, m->l3_quantizers[m->l3ptr++], exps[start], scale_factors);
                    break;

                case 2:
                    if (m->l5ptr > 2) {
                        gcode = get_bits(gb, 7);
                        m->l5_quantizers[0] = l5_quantizers_1[gcode];
                        m->l5_quantizers[1] = l5_quantizers_2[gcode];
                        m->l5_quantizers[2] = l5_quantizers_3[gcode];
                        m->l5ptr = 0;
                    }
                    TRANSFORM_COEFF(cplcoeff, m->l5_quantizers[m->l5ptr++], exps[start], scale_factors);
                    break;

                case 3:
                    TRANSFORM_COEFF(cplcoeff, l7_quantizers[get_bits(gb, 3)], exps[start], scale_factors);
                    break;

                case 4:
                    if (m->l11ptr > 1) {
                        gcode = get_bits(gb, 7);
                        m->l11_quantizers[0] = l11_quantizers_1[gcode];
                        m->l11_quantizers[1] = l11_quantizers_2[gcode];
                        m->l11ptr = 0;
                    }
                    TRANSFORM_COEFF(cplcoeff, m->l11_quantizers[m->l11ptr++], exps[start], scale_factors);
                    break;

                case 5:
                    TRANSFORM_COEFF(cplcoeff, l15_quantizers[get_bits(gb, 4)], exps[start], scale_factors);
                    break;

                default:
                    TRANSFORM_COEFF(cplcoeff, get_sbits(gb, qntztab[tbap]) << (16 - qntztab[tbap]),
                            exps[start], scale_factors);
            }
            for (ch = 0; ch < ctx->nfchans; ch++)
                if ((ctx->chincpl >> ch) & 1)
                    ctx->transform_coeffs[ch + 1][start] = cplcoeff * cplcos[ch];
            start++;
        }
    }

    return 0;
}

/* Get the transform coefficients for particular channel */
static int get_transform_coeffs_ch(AC3DecodeContext *ctx, int ch_index, mant_groups *m)
{
    GetBitContext *gb = &ctx->gb;
    int i, gcode, tbap, dithflag, end;
    uint8_t *exps;
    uint8_t *bap;
    float *coeffs;
    float factors[25];

    for (i = 0; i < 25; i++)
        factors[i] = scale_factors[i] * ctx->chcoeffs[ch_index];

    if (ch_index != -1) { /* fbw channels */
        dithflag = (ctx->dithflag >> ch_index) & 1;
        exps = ctx->dexps[ch_index];
        bap = ctx->bap[ch_index];
        coeffs = ctx->transform_coeffs[ch_index + 1];
        end = ctx->endmant[ch_index];
    } else if (ch_index == -1) {
        dithflag = 0;
        exps = ctx->dlfeexps;
        bap = ctx->lfebap;
        coeffs = ctx->transform_coeffs[0];
        end = 7;
    }


    for (i = 0; i < end; i++) {
        tbap = bap[i];
        switch (tbap) {
            case 0:
                if (!dithflag) {
                    coeffs[i] = 0;
                    continue;
                }
                else {
                    TRANSFORM_COEFF(coeffs[i], dither_int16(&ctx->dith_state), exps[i], factors);
                    coeffs[i] *= LEVEL_MINUS_3DB;
                    continue;
                }

            case 1:
                if (m->l3ptr > 2) {
                    gcode = get_bits(gb, 5);
                    m->l3_quantizers[0] = l3_quantizers_1[gcode];
                    m->l3_quantizers[1] = l3_quantizers_2[gcode];
                    m->l3_quantizers[2] = l3_quantizers_3[gcode];
                    m->l3ptr = 0;
                }
                TRANSFORM_COEFF(coeffs[i], m->l3_quantizers[m->l3ptr++], exps[i], factors);
                continue;

            case 2:
                if (m->l5ptr > 2) {
                    gcode = get_bits(gb, 7);
                    m->l5_quantizers[0] = l5_quantizers_1[gcode];
                    m->l5_quantizers[1] = l5_quantizers_2[gcode];
                    m->l5_quantizers[2] = l5_quantizers_3[gcode];
                    m->l5ptr = 0;
                }
                TRANSFORM_COEFF(coeffs[i], m->l5_quantizers[m->l5ptr++], exps[i], factors);
                continue;

            case 3:
                TRANSFORM_COEFF(coeffs[i], l7_quantizers[get_bits(gb, 3)], exps[i], factors);
                continue;

            case 4:
                if (m->l11ptr > 1) {
                    gcode = get_bits(gb, 7);
                    m->l11_quantizers[0] = l11_quantizers_1[gcode];
                    m->l11_quantizers[1] = l11_quantizers_2[gcode];
                    m->l11ptr = 0;
                }
                TRANSFORM_COEFF(coeffs[i], m->l11_quantizers[m->l11ptr++], exps[i], factors);
                continue;

            case 5:
                TRANSFORM_COEFF(coeffs[i], l15_quantizers[get_bits(gb, 4)], exps[i], factors);
                continue;

            default:
                TRANSFORM_COEFF(coeffs[i], get_sbits(gb, qntztab[tbap]) << (16 - qntztab[tbap]), exps[i], factors);
                continue;
        }
    }

    return 0;
}

static int get_transform_coeffs(AC3DecodeContext * ctx)
{
    int i, end;
    int got_cplchan = 0;
    mant_groups m;

    m.l3ptr = m.l5ptr = m.l11ptr = 3;

    for (i = 0; i < ctx->nfchans; i++) {
        /* transform coefficients for individual channel */
        if (get_transform_coeffs_ch(ctx, i, &m))
            return -1;
        /* tranform coefficients for coupling channels */
        if ((ctx->chincpl >> i) & 1)  {
            if (!got_cplchan) {
                if (get_transform_coeffs_cpling(ctx, &m)) {
                    av_log(NULL, AV_LOG_ERROR, "error in decoupling channels\n");
                    return -1;
                }
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

    return 0;
}

static void do_rematrixing1(AC3DecodeContext *ctx, int start, int end)
{
    float tmp0, tmp1;

    while (start < end) {
        tmp0 = ctx->transform_coeffs[1][start];
        tmp1 = ctx->transform_coeffs[2][start];
        ctx->transform_coeffs[1][start] = tmp0 + tmp1;
        ctx->transform_coeffs[2][start] = tmp0 - tmp1;
        start++;
    }
}

static void do_rematrixing(AC3DecodeContext *ctx)
{
    int bnd1 = 13, bnd2 = 25, bnd3 = 37, bnd4 = 61;
    int end, bndend;

    end = FFMIN(ctx->endmant[0], ctx->endmant[1]);

    if (ctx->rematflg & 1)
        do_rematrixing1(ctx, bnd1, bnd2);

    if (ctx->rematflg & 2)
        do_rematrixing1(ctx, bnd2, bnd3);

    bndend = bnd4;
    if (bndend > end) {
        bndend = end;
        if (ctx->rematflg & 4)
            do_rematrixing1(ctx, bnd3, bndend);
    } else {
        if (ctx->rematflg & 4)
            do_rematrixing1(ctx, bnd3, bnd4);
        if (ctx->rematflg & 8)
            do_rematrixing1(ctx, bnd4, end);
    }
}

static void get_downmix_coeffs(AC3DecodeContext *ctx)
{
    int from = ctx->acmod;
    int to = ctx->blkoutput;
    float clev = clevs[ctx->cmixlev];
    float slev = slevs[ctx->surmixlev];
    float nf = 1.0; //normalization factor for downmix coeffs

    if (to == AC3_OUTPUT_UNMODIFIED)
        return;

    switch (from) {
        case AC3_INPUT_DUALMONO:
            switch (to) {
                case AC3_OUTPUT_MONO:
                case AC3_OUTPUT_STEREO: /* We Assume that sum of both mono channels is requested */
                    nf = 0.5;
                    ctx->chcoeffs[0] *= nf;
                    ctx->chcoeffs[1] *= nf;
                    break;
            }
            break;
        case AC3_INPUT_MONO:
            switch (to) {
                case AC3_OUTPUT_STEREO:
                    nf = LEVEL_MINUS_3DB;
                    ctx->chcoeffs[0] *= nf;
                    break;
            }
            break;
        case AC3_INPUT_STEREO:
            switch (to) {
                case AC3_OUTPUT_MONO:
                    nf = LEVEL_MINUS_3DB;
                    ctx->chcoeffs[0] *= nf;
                    ctx->chcoeffs[1] *= nf;
                    break;
            }
            break;
        case AC3_INPUT_3F:
            switch (to) {
                case AC3_OUTPUT_MONO:
                    nf = LEVEL_MINUS_3DB / (1.0 + clev);
                    ctx->chcoeffs[0] *= (nf * LEVEL_MINUS_3DB);
                    ctx->chcoeffs[2] *= (nf * LEVEL_MINUS_3DB);
                    ctx->chcoeffs[1] *= ((nf * clev * LEVEL_MINUS_3DB) / 2.0);
                    break;
                case AC3_OUTPUT_STEREO:
                    nf = 1.0 / (1.0 + clev);
                    ctx->chcoeffs[0] *= nf;
                    ctx->chcoeffs[2] *= nf;
                    ctx->chcoeffs[1] *= (nf * clev);
                    break;
            }
            break;
        case AC3_INPUT_2F_1R:
            switch (to) {
                case AC3_OUTPUT_MONO:
                    nf = 2.0 * LEVEL_MINUS_3DB / (2.0 + slev);
                    ctx->chcoeffs[0] *= (nf * LEVEL_MINUS_3DB);
                    ctx->chcoeffs[1] *= (nf * LEVEL_MINUS_3DB);
                    ctx->chcoeffs[2] *= (nf * slev * LEVEL_MINUS_3DB);
                    break;
                case AC3_OUTPUT_STEREO:
                    nf = 1.0 / (1.0 + (slev * LEVEL_MINUS_3DB));
                    ctx->chcoeffs[0] *= nf;
                    ctx->chcoeffs[1] *= nf;
                    ctx->chcoeffs[2] *= (nf * slev * LEVEL_MINUS_3DB);
                    break;
                case AC3_OUTPUT_DOLBY:
                    nf = 1.0 / (1.0 + LEVEL_MINUS_3DB);
                    ctx->chcoeffs[0] *= nf;
                    ctx->chcoeffs[1] *= nf;
                    ctx->chcoeffs[2] *= (nf * LEVEL_MINUS_3DB);
                    break;
            }
            break;
        case AC3_INPUT_3F_1R:
            switch (to) {
                case AC3_OUTPUT_MONO:
                    nf = LEVEL_MINUS_3DB / (1.0 + clev + (slev / 2.0));
                    ctx->chcoeffs[0] *= (nf * LEVEL_MINUS_3DB);
                    ctx->chcoeffs[2] *= (nf * LEVEL_MINUS_3DB);
                    ctx->chcoeffs[1] *= (nf * clev * LEVEL_PLUS_3DB);
                    ctx->chcoeffs[3] *= (nf * slev * LEVEL_MINUS_3DB);
                    break;
                case AC3_OUTPUT_STEREO:
                    nf = 1.0 / (1.0 + clev + (slev * LEVEL_MINUS_3DB));
                    ctx->chcoeffs[0] *= nf;
                    ctx->chcoeffs[2] *= nf;
                    ctx->chcoeffs[1] *= (nf * clev);
                    ctx->chcoeffs[3] *= (nf * slev * LEVEL_MINUS_3DB);
                    break;
                case AC3_OUTPUT_DOLBY:
                    nf = 1.0 / (1.0 + (2.0 * LEVEL_MINUS_3DB));
                    ctx->chcoeffs[0] *= nf;
                    ctx->chcoeffs[1] *= nf;
                    ctx->chcoeffs[1] *= (nf * LEVEL_MINUS_3DB);
                    ctx->chcoeffs[3] *= (nf * LEVEL_MINUS_3DB);
                    break;
            }
            break;
        case AC3_INPUT_2F_2R:
            switch (to) {
                case AC3_OUTPUT_MONO:
                    nf = LEVEL_MINUS_3DB / (1.0 + slev);
                    ctx->chcoeffs[0] *= (nf * LEVEL_MINUS_3DB);
                    ctx->chcoeffs[1] *= (nf * LEVEL_MINUS_3DB);
                    ctx->chcoeffs[2] *= (nf * slev * LEVEL_MINUS_3DB);
                    ctx->chcoeffs[3] *= (nf * slev * LEVEL_MINUS_3DB);
                    break;
                case AC3_OUTPUT_STEREO:
                    nf = 1.0 / (1.0 + slev);
                    ctx->chcoeffs[0] *= nf;
                    ctx->chcoeffs[1] *= nf;
                    ctx->chcoeffs[2] *= (nf * slev);
                    ctx->chcoeffs[3] *= (nf * slev);
                    break;
                case AC3_OUTPUT_DOLBY:
                    nf = 1.0 / (1.0 + (2.0 * LEVEL_MINUS_3DB));
                    ctx->chcoeffs[0] *= nf;
                    ctx->chcoeffs[1] *= nf;
                    ctx->chcoeffs[2] *= (nf * LEVEL_MINUS_3DB);
                    ctx->chcoeffs[3] *= (nf * LEVEL_MINUS_3DB);
                    break;
            }
            break;
        case AC3_INPUT_3F_2R:
            switch (to) {
                case AC3_OUTPUT_MONO:
                    nf = LEVEL_MINUS_3DB / (1.0 + clev + slev);
                    ctx->chcoeffs[0] *= (nf * LEVEL_MINUS_3DB);
                    ctx->chcoeffs[2] *= (nf * LEVEL_MINUS_3DB);
                    ctx->chcoeffs[1] *= (nf * clev * LEVEL_PLUS_3DB);
                    ctx->chcoeffs[3] *= (nf * slev * LEVEL_MINUS_3DB);
                    ctx->chcoeffs[4] *= (nf * slev * LEVEL_MINUS_3DB);
                    break;
                case AC3_OUTPUT_STEREO:
                    nf = 1.0 / (1.0 + clev + slev);
                    ctx->chcoeffs[0] *= nf;
                    ctx->chcoeffs[2] *= nf;
                    ctx->chcoeffs[1] *= (nf * clev);
                    ctx->chcoeffs[3] *= (nf * slev);
                    ctx->chcoeffs[4] *= (nf * slev);
                    break;
                case AC3_OUTPUT_DOLBY:
                    nf = 1.0 / (1.0 + (3.0 * LEVEL_MINUS_3DB));
                    ctx->chcoeffs[0] *= nf;
                    ctx->chcoeffs[1] *= nf;
                    ctx->chcoeffs[1] *= (nf * LEVEL_MINUS_3DB);
                    ctx->chcoeffs[3] *= (nf * LEVEL_MINUS_3DB);
                    ctx->chcoeffs[4] *= (nf * LEVEL_MINUS_3DB);
                    break;
            }
            break;
    }
}

static inline void mix_dualmono_to_mono(AC3DecodeContext *ctx)
{
    int i;
    float (*output)[BLOCK_SIZE] = ctx->output;

    for (i = 0; i < 256; i++)
        output[1][i] += output[2][i];
    memset(output[2], 0, sizeof(output[2]));
}

static inline void mix_dualmono_to_stereo(AC3DecodeContext *ctx)
{
    int i;
    float tmp;
    float (*output)[BLOCK_SIZE] = ctx->output;

    for (i = 0; i < 256; i++) {
        tmp = output[1][i] + output[2][i];
        output[1][i] = output[2][i] = tmp;
    }
}

static inline void upmix_mono_to_stereo(AC3DecodeContext *ctx)
{
    int i;
    float (*output)[BLOCK_SIZE] = ctx->output;

    for (i = 0; i < 256; i++)
        output[2][i] = output[1][i];
}

static inline void mix_stereo_to_mono(AC3DecodeContext *ctx)
{
    int i;
    float (*output)[BLOCK_SIZE] = ctx->output;

    for (i = 0; i < 256; i++)
        output[1][i] += output[2][i];
    memset(output[2], 0, sizeof(output[2]));
}

static inline void mix_3f_to_mono(AC3DecodeContext *ctx)
{
    int i;
    float (*output)[BLOCK_SIZE] = ctx->output;

    for (i = 0; i < 256; i++)
        output[1][i] += (output[2][i] + output[3][i]);
    memset(output[2], 0, sizeof(output[2]));
    memset(output[3], 0, sizeof(output[3]));
}

static inline void mix_3f_to_stereo(AC3DecodeContext *ctx)
{
    int i;
    float (*output)[BLOCK_SIZE] = ctx->output;

    for (i = 0; i < 256; i++) {
        output[1][i] += output[2][i];
        output[2][i] += output[3][i];
    }
    memset(output[3], 0, sizeof(output[3]));
}

static inline void mix_2f_1r_to_mono(AC3DecodeContext *ctx)
{
    int i;
    float (*output)[BLOCK_SIZE] = ctx->output;

    for (i = 0; i < 256; i++)
        output[1][i] += (output[2][i] + output[3][i]);
    memset(output[2], 0, sizeof(output[2]));
    memset(output[3], 0, sizeof(output[3]));

}

static inline void mix_2f_1r_to_stereo(AC3DecodeContext *ctx)
{
    int i;
    float (*output)[BLOCK_SIZE] = ctx->output;

    for (i = 0; i < 256; i++) {
        output[1][i] += output[2][i];
        output[2][i] += output[3][i];
    }
    memset(output[3], 0, sizeof(output[3]));
}

static inline void mix_2f_1r_to_dolby(AC3DecodeContext *ctx)
{
    int i;
    float (*output)[BLOCK_SIZE] = ctx->output;

    for (i = 0; i < 256; i++) {
        output[1][i] -= output[3][i];
        output[2][i] += output[3][i];
    }
    memset(output[3], 0, sizeof(output[3]));
}

static inline void mix_3f_1r_to_mono(AC3DecodeContext *ctx)
{
    int i;
    float (*output)[BLOCK_SIZE] = ctx->output;

    for (i = 0; i < 256; i++)
        output[1][i] = (output[2][i] + output[3][i] + output[4][i]);
    memset(output[2], 0, sizeof(output[2]));
    memset(output[3], 0, sizeof(output[3]));
    memset(output[4], 0, sizeof(output[4]));
}

static inline void mix_3f_1r_to_stereo(AC3DecodeContext *ctx)
{
    int i;
    float (*output)[BLOCK_SIZE] = ctx->output;

    for (i = 0; i < 256; i++) {
        output[1][i] += (output[2][i] + output[4][i]);
        output[2][i] += (output[3][i] + output[4][i]);
    }
    memset(output[3], 0, sizeof(output[3]));
    memset(output[4], 0, sizeof(output[4]));
}

static inline void mix_3f_1r_to_dolby(AC3DecodeContext *ctx)
{
    int i;
    float (*output)[BLOCK_SIZE] = ctx->output;

    for (i = 0; i < 256; i++) {
        output[1][i] += (output[2][i] - output[4][i]);
        output[2][i] += (output[3][i] + output[4][i]);
    }
    memset(output[3], 0, sizeof(output[3]));
    memset(output[4], 0, sizeof(output[4]));
}

static inline void mix_2f_2r_to_mono(AC3DecodeContext *ctx)
{
    int i;
    float (*output)[BLOCK_SIZE] = ctx->output;

    for (i = 0; i < 256; i++)
        output[1][i] = (output[2][i] + output[3][i] + output[4][i]);
    memset(output[2], 0, sizeof(output[2]));
    memset(output[3], 0, sizeof(output[3]));
    memset(output[4], 0, sizeof(output[4]));
}

static inline void mix_2f_2r_to_stereo(AC3DecodeContext *ctx)
{
    int i;
    float (*output)[BLOCK_SIZE] = ctx->output;

    for (i = 0; i < 256; i++) {
        output[1][i] += output[3][i];
        output[2][i] += output[4][i];
    }
    memset(output[3], 0, sizeof(output[3]));
    memset(output[4], 0, sizeof(output[4]));
}

static inline void mix_2f_2r_to_dolby(AC3DecodeContext *ctx)
{
    int i;
    float (*output)[BLOCK_SIZE] = ctx->output;

    for (i = 0; i < 256; i++) {
        output[1][i] -= output[3][i];
        output[2][i] += output[4][i];
    }
    memset(output[3], 0, sizeof(output[3]));
    memset(output[4], 0, sizeof(output[4]));
}

static inline void mix_3f_2r_to_mono(AC3DecodeContext *ctx)
{
    int i;
    float (*output)[BLOCK_SIZE] = ctx->output;

    for (i = 0; i < 256; i++)
        output[1][i] += (output[2][i] + output[3][i] + output[4][i] + output[5][i]);
    memset(output[2], 0, sizeof(output[2]));
    memset(output[3], 0, sizeof(output[3]));
    memset(output[4], 0, sizeof(output[4]));
    memset(output[5], 0, sizeof(output[5]));
}

static inline void mix_3f_2r_to_stereo(AC3DecodeContext *ctx)
{
    int i;
    float (*output)[BLOCK_SIZE] = ctx->output;

    for (i = 0; i < 256; i++) {
        output[1][i] += (output[2][i] + output[4][i]);
        output[2][i] += (output[3][i] + output[5][i]);
    }
    memset(output[3], 0, sizeof(output[3]));
    memset(output[4], 0, sizeof(output[4]));
    memset(output[5], 0, sizeof(output[5]));
}

static inline void mix_3f_2r_to_dolby(AC3DecodeContext *ctx)
{
    int i;
    float (*output)[BLOCK_SIZE] = ctx->output;

    for (i = 0; i < 256; i++) {
        output[1][i] += (output[2][i] - output[4][i] - output[5][i]);
        output[2][i] += (output[3][i] + output[4][i] + output[5][i]);
    }
    memset(output[3], 0, sizeof(output[3]));
    memset(output[4], 0, sizeof(output[4]));
    memset(output[5], 0, sizeof(output[5]));
}

static void do_downmix(AC3DecodeContext *ctx)
{
    int from = ctx->acmod;
    int to = ctx->blkoutput;

    switch (from) {
        case AC3_INPUT_DUALMONO:
            switch (to) {
                case AC3_OUTPUT_MONO:
                    mix_dualmono_to_mono(ctx);
                    break;
                case AC3_OUTPUT_STEREO: /* We assume that sum of both mono channels is requested */
                    mix_dualmono_to_stereo(ctx);
                    break;
            }
            break;
        case AC3_INPUT_MONO:
            switch (to) {
                case AC3_OUTPUT_STEREO:
                    upmix_mono_to_stereo(ctx);
                    break;
            }
            break;
        case AC3_INPUT_STEREO:
            switch (to) {
                case AC3_OUTPUT_MONO:
                    mix_stereo_to_mono(ctx);
                    break;
            }
            break;
        case AC3_INPUT_3F:
            switch (to) {
                case AC3_OUTPUT_MONO:
                    mix_3f_to_mono(ctx);
                    break;
                case AC3_OUTPUT_STEREO:
                    mix_3f_to_stereo(ctx);
                    break;
            }
            break;
        case AC3_INPUT_2F_1R:
            switch (to) {
                case AC3_OUTPUT_MONO:
                    mix_2f_1r_to_mono(ctx);
                    break;
                case AC3_OUTPUT_STEREO:
                    mix_2f_1r_to_stereo(ctx);
                    break;
                case AC3_OUTPUT_DOLBY:
                    mix_2f_1r_to_dolby(ctx);
                    break;
            }
            break;
        case AC3_INPUT_3F_1R:
            switch (to) {
                case AC3_OUTPUT_MONO:
                    mix_3f_1r_to_mono(ctx);
                    break;
                case AC3_OUTPUT_STEREO:
                    mix_3f_1r_to_stereo(ctx);
                    break;
                case AC3_OUTPUT_DOLBY:
                    mix_3f_1r_to_dolby(ctx);
                    break;
            }
            break;
        case AC3_INPUT_2F_2R:
            switch (to) {
                case AC3_OUTPUT_MONO:
                    mix_2f_2r_to_mono(ctx);
                    break;
                case AC3_OUTPUT_STEREO:
                    mix_2f_2r_to_stereo(ctx);
                    break;
                case AC3_OUTPUT_DOLBY:
                    mix_2f_2r_to_dolby(ctx);
                    break;
            }
            break;
        case AC3_INPUT_3F_2R:
            switch (to) {
                case AC3_OUTPUT_MONO:
                    mix_3f_2r_to_mono(ctx);
                    break;
                case AC3_OUTPUT_STEREO:
                    mix_3f_2r_to_stereo(ctx);
                    break;
                case AC3_OUTPUT_DOLBY:
                    mix_3f_2r_to_dolby(ctx);
                    break;
            }
            break;
    }
}

static void dump_floats(const char *name, int prec, const float *tab, int n)
{
    int i;

    av_log(NULL, AV_LOG_INFO, "%s[%d]:\n", name, n);
    for(i=0;i<n;i++) {
        if ((i & 7) == 0)
            av_log(NULL, AV_LOG_INFO, "%4d: ", i);
        av_log(NULL, AV_LOG_INFO, " %8.*f", prec, tab[i]);
        if ((i & 7) == 7)
            av_log(NULL, AV_LOG_INFO, "\n");
    }
    if ((i & 7) != 0)
        av_log(NULL, AV_LOG_INFO, "\n");
}

#define CMUL(pre, pim, are, aim, bre, bim) \
{\
    float _are = (are);\
    float _aim = (aim);\
    float _bre = (bre);\
    float _bim = (bim);\
    (pre) = _are * _bre - _aim * _bim;\
    (pim) = _are * _bim + _aim * _bre;\
}

static void do_imdct_256(AC3DecodeContext *ctx, int chindex)
{
    int k;
    float x1[128], x2[128];

    for (k = 0; k < N / 4; k++) {
        x1[k] = ctx->transform_coeffs[chindex][2 * k];
        x2[k] = ctx->transform_coeffs[chindex][2 * k + 1];
    }

    ff_imdct_calc(&ctx->imdct_256, ctx->tmp_output, x1, ctx->tmp_imdct);
    ff_imdct_calc(&ctx->imdct_256, ctx->tmp_output + 256, x2, ctx->tmp_imdct);

    for (k = 0; k < N / 2; k++) {
        ctx->output[chindex][k] = ctx->tmp_output[k] * window[k] + ctx->delay[chindex][k];
        //dump_floats("samples", 10, ctx->output[chindex], 256);
        ctx->delay[chindex][k] = ctx->tmp_output[N / 2 + k] * window[255 - k];
    }
}

static void do_imdct_512(AC3DecodeContext *ctx, int chindex)
{
    int k;

    ff_imdct_calc(&ctx->imdct_512, ctx->tmp_output,
            ctx->transform_coeffs[chindex], ctx->tmp_imdct);
    //ff_imdct_calc_ac3_512(&ctx->imdct_512, ctx->tmp_output, ctx->transform_coeffs[chindex],
    //        ctx->tmp_imdct, window);

    for (k = 0; k < N / 2; k++) {
        ctx->output[chindex][k] = ctx->tmp_output[k] * window[k] + ctx->delay[chindex][k];
        //dump_floats("samples", 10, ctx->output[chindex], 256);
        ctx->delay[chindex][k] = ctx->tmp_output[N / 2 + k] * window[255 - k];
    }
}

static inline void do_imdct(AC3DecodeContext *ctx)
{
    int i;

    if (ctx->blkoutput & AC3_OUTPUT_LFEON) {
        do_imdct_512(ctx, 0);
    }
    for (i = 0; i < ctx->nfchans; i++) {
        if ((ctx->blksw >> i) & 1)
            do_imdct_256(ctx, i + 1);
        else
            do_imdct_512(ctx, i + 1);
    }
}

static int ac3_parse_audio_block(AC3DecodeContext * ctx)
{
    int nfchans = ctx->nfchans;
    int acmod = ctx->acmod;
    int i, bnd, rbnd, seg, grpsize;
    GetBitContext *gb = &ctx->gb;
    int bit_alloc_flags = 0;
    float drange;
    uint8_t *dexps;
    int mstrcplco, cplcoexp, cplcomant;
    int dynrng, chbwcod, ngrps, cplabsexp, skipl;

    for (i = 0; i < 5; i++)
        ctx->chcoeffs[i] = 2.0;

    ctx->blksw = 0;
    for (i = 0; i < nfchans; i++) /*block switch flag */
        ctx->blksw |= get_bits1(gb) << i;

    ctx->dithflag = 0;
    for (i = 0; i < nfchans; i++) /* dithering flag */
        ctx->dithflag |= get_bits1(gb) << i;

    if (get_bits1(gb)) { /* dynamic range */
        dynrng = get_sbits(gb, 8);
        drange = ((((dynrng & 0x1f) | 0x20) << 13) * scale_factors[3 - (dynrng >> 5)]);
        for (i = 0; i < nfchans; i++)
            ctx->chcoeffs[i] *= drange;
    }

    if (acmod == 0x00 && get_bits1(gb)) { /* dynamic range 1+1 mode */
        dynrng = get_sbits(gb, 8);
        drange = ((((dynrng & 0x1f) | 0x20) << 13) * scale_factors[3 - (dynrng >> 5)]);
        ctx->chcoeffs[1] *= drange;
    }

    get_downmix_coeffs(ctx);

    if (get_bits1(gb)) { /* coupling strategy */
        ctx->cplinu = get_bits1(gb);
        ctx->cplbndstrc = 0;
        ctx->chincpl = 0;
        if (ctx->cplinu) { /* coupling in use */
            for (i = 0; i < nfchans; i++)
                ctx->chincpl |= get_bits1(gb) << i;

            if (acmod == 0x02)
                ctx->phsflginu = get_bits1(gb); //phase flag in use

            ctx->cplbegf = get_bits(gb, 4);
            ctx->cplendf = get_bits(gb, 4);

            if (3 + ctx->cplendf - ctx->cplbegf < 0) {
                av_log(NULL, AV_LOG_ERROR, "cplendf = %d < cplbegf = %d\n", ctx->cplendf, ctx->cplbegf);
                return -1;
            }

            ctx->ncplbnd = ctx->ncplsubnd = 3 + ctx->cplendf - ctx->cplbegf;
            ctx->cplstrtmant = ctx->cplbegf * 12 + 37;
            ctx->cplendmant = ctx->cplendf * 12 + 73;
            for (i = 0; i < ctx->ncplsubnd - 1; i++) /* coupling band structure */
                if (get_bits1(gb)) {
                    ctx->cplbndstrc |= 1 << i;
                    ctx->ncplbnd--;
                }
        }
    }

    if (ctx->cplinu) {
        ctx->cplcoe = 0;

        for (i = 0; i < nfchans; i++)
            if ((ctx->chincpl) >> i & 1)
                if (get_bits1(gb)) { /* coupling co-ordinates */
                    ctx->cplcoe |= 1 << i;
                    mstrcplco = 3 * get_bits(gb, 2);
                    for (bnd = 0; bnd < ctx->ncplbnd; bnd++) {
                        cplcoexp = get_bits(gb, 4);
                        cplcomant = get_bits(gb, 4);
                        if (cplcoexp == 15)
                            cplcomant <<= 14;
                        else
                            cplcomant = (cplcomant | 0x10) << 13;
                        ctx->cplco[i][bnd] = cplcomant * scale_factors[cplcoexp + mstrcplco];
                    }
                }

        if (acmod == 0x02 && ctx->phsflginu && (ctx->cplcoe & 1 || ctx->cplcoe & 2))
            for (bnd = 0; bnd < ctx->ncplbnd; bnd++)
                if (get_bits1(gb))
                    ctx->cplco[1][bnd] = -ctx->cplco[1][bnd];
    }

    if (acmod == 0x02) {/* rematrixing */
        ctx->rematstr = get_bits1(gb);
        if (ctx->rematstr) {
            ctx->rematflg = 0;

            if (!(ctx->cplinu) || ctx->cplbegf > 2)
                for (rbnd = 0; rbnd < 4; rbnd++)
                    ctx->rematflg |= get_bits1(gb) << rbnd;
            if (ctx->cplbegf > 0 && ctx->cplbegf <= 2 && ctx->cplinu)
                for (rbnd = 0; rbnd < 3; rbnd++)
                    ctx->rematflg |= get_bits1(gb) << rbnd;
            if (ctx->cplbegf == 0 && ctx->cplinu)
                for (rbnd = 0; rbnd < 2; rbnd++)
                    ctx->rematflg |= get_bits1(gb) << rbnd;
        }
    }

    ctx->cplexpstr = AC3_EXPSTR_REUSE;
    ctx->lfeexpstr = AC3_EXPSTR_REUSE;
    if (ctx->cplinu) /* coupling exponent strategy */
        ctx->cplexpstr = get_bits(gb, 2);
    for (i = 0; i < nfchans; i++)  /* channel exponent strategy */
        ctx->chexpstr[i] = get_bits(gb, 2);
    if (ctx->lfeon)  /* lfe exponent strategy */
        ctx->lfeexpstr = get_bits1(gb);

    for (i = 0; i < nfchans; i++) /* channel bandwidth code */
        if (ctx->chexpstr[i] != AC3_EXPSTR_REUSE) {
            if ((ctx->chincpl >> i) & 1)
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

    if (ctx->cplexpstr != AC3_EXPSTR_REUSE) {/* coupling exponents */
        bit_alloc_flags = 64;
        cplabsexp = get_bits(gb, 4) << 1;
        ngrps = (ctx->cplendmant - ctx->cplstrtmant) / (3 << (ctx->cplexpstr - 1));
        if (decode_exponents(gb, ctx->cplexpstr, ngrps, cplabsexp, ctx->dcplexps + ctx->cplstrtmant)) {
            av_log(NULL, AV_LOG_ERROR, "error decoding coupling exponents\n");
            return -1;
        }
    }

    for (i = 0; i < nfchans; i++) /* fbw channel exponents */
        if (ctx->chexpstr[i] != AC3_EXPSTR_REUSE) {
            bit_alloc_flags |= 1 << i;
            grpsize = 3 << (ctx->chexpstr[i] - 1);
            ngrps = (ctx->endmant[i] + grpsize - 4) / grpsize;
            dexps = ctx->dexps[i];
            dexps[0] = get_bits(gb, 4);
            if (decode_exponents(gb, ctx->chexpstr[i], ngrps, dexps[0], dexps + 1)) {
                av_log(NULL, AV_LOG_ERROR, "error decoding channel %d exponents\n", i);
                return -1;
            }
            skip_bits(gb, 2); /* skip gainrng */
        }

    if (ctx->lfeexpstr != AC3_EXPSTR_REUSE) { /* lfe exponents */
        bit_alloc_flags |= 32;
        ctx->dlfeexps[0] = get_bits(gb, 4);
        if (decode_exponents(gb, ctx->lfeexpstr, 2, ctx->dlfeexps[0], ctx->dlfeexps + 1)) {
            av_log(NULL, AV_LOG_ERROR, "error decoding lfe exponents\n");
            return -1;
        }
    }

    if (get_bits1(gb)) { /* bit allocation information */
        bit_alloc_flags = 127;
        ctx->sdcycod = get_bits(gb, 2);
        ctx->fdcycod = get_bits(gb, 2);
        ctx->sgaincod = get_bits(gb, 2);
        ctx->dbpbcod = get_bits(gb, 2);
        ctx->floorcod = get_bits(gb, 3);
    }

    if (get_bits1(gb)) { /* snroffset */
        bit_alloc_flags = 127;
        ctx->csnroffst = get_bits(gb, 6);
        if (ctx->cplinu) { /* couling fine snr offset and fast gain code */
            ctx->cplfsnroffst = get_bits(gb, 4);
            ctx->cplfgaincod = get_bits(gb, 3);
        }
        for (i = 0; i < nfchans; i++) { /* channel fine snr offset and fast gain code */
            ctx->fsnroffst[i] = get_bits(gb, 4);
            ctx->fgaincod[i] = get_bits(gb, 3);
        }
        if (ctx->lfeon) { /* lfe fine snr offset and fast gain code */
            ctx->lfefsnroffst = get_bits(gb, 4);
            ctx->lfefgaincod = get_bits(gb, 3);
        }
    }

    if (ctx->cplinu && get_bits1(gb)) { /* coupling leak information */
        bit_alloc_flags |= 64;
        ctx->cplfleak = get_bits(gb, 3);
        ctx->cplsleak = get_bits(gb, 3);
    }

    if (get_bits1(gb)) { /* delta bit allocation information */
        bit_alloc_flags = 127;

        if (ctx->cplinu) {
            ctx->cpldeltbae = get_bits(gb, 2);
            if (ctx->cpldeltbae == AC3_DBASTR_RESERVED) {
                av_log(NULL, AV_LOG_ERROR, "coupling delta bit allocation strategy reserved\n");
                return -1;
            }
        }

        for (i = 0; i < nfchans; i++) {
            ctx->deltbae[i] = get_bits(gb, 2);
            if (ctx->deltbae[i] == AC3_DBASTR_RESERVED) {
                av_log(NULL, AV_LOG_ERROR, "delta bit allocation strategy reserved\n");
                return -1;
            }
        }

        if (ctx->cplinu)
            if (ctx->cpldeltbae == AC3_DBASTR_NEW) { /*coupling delta offset, len and bit allocation */
                ctx->cpldeltnseg = get_bits(gb, 3);
                for (seg = 0; seg <= ctx->cpldeltnseg; seg++) {
                    ctx->cpldeltoffst[seg] = get_bits(gb, 5);
                    ctx->cpldeltlen[seg] = get_bits(gb, 4);
                    ctx->cpldeltba[seg] = get_bits(gb, 3);
                }
            }

        for (i = 0; i < nfchans; i++)
            if (ctx->deltbae[i] == AC3_DBASTR_NEW) {/*channel delta offset, len and bit allocation */
                ctx->deltnseg[i] = get_bits(gb, 3);
                for (seg = 0; seg <= ctx->deltnseg[i]; seg++) {
                    ctx->deltoffst[i][seg] = get_bits(gb, 5);
                    ctx->deltlen[i][seg] = get_bits(gb, 4);
                    ctx->deltba[i][seg] = get_bits(gb, 3);
                }
            }
    }

    if (bit_alloc_flags) {
        if (is_snr_offsets_zero(ctx)) {
            memset(ctx->cplbap, 0, sizeof (ctx->cplbap));
            memset(ctx->lfebap, 0, sizeof (ctx->lfebap));
            for (i = 0; i < nfchans; i++)
                memset(ctx->bap[i], 0, sizeof(ctx->bap[i]));
        } else {
            if (ctx->chincpl && (bit_alloc_flags & 64))
                do_bit_allocation(ctx, 5);
            for (i = 0; i < nfchans; i++)
                if ((bit_alloc_flags >> i) & 1)
                    do_bit_allocation(ctx, i);
            if (ctx->lfeon && (bit_alloc_flags & 32))
                do_bit_allocation(ctx, 6);
        }
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
    /*for (i = 0; i < nfchans; i++)
        dump_floats("channel transform coefficients", 10, ctx->transform_coeffs[i + 1], BLOCK_SIZE);*/

    /* recover coefficients if rematrixing is in use */
    if (ctx->rematflg)
        do_rematrixing(ctx);

    do_imdct(ctx);
    /*for(i = 0; i < nfchans; i++)
        dump_floats("channel output", 10, ctx->output[i + 1], BLOCK_SIZE);*/

    do_downmix(ctx);

    return 0;
}

static inline int16_t convert(float f)
{
    if (f >= 1.0)
        return 32767;
    else if (f <= -1.0)
        return -32768;
    else
        return (lrintf(f * 32767.0));
}

static int frame_count = 0;

static int ac3_decode_frame(AVCodecContext * avctx, void *data, int *data_size, uint8_t *buf, int buf_size)
{
    AC3DecodeContext *ctx = (AC3DecodeContext *)avctx->priv_data;
    int frame_start;
    int16_t *out_samples = (int16_t *)data;
    int i, j, k, value;

    av_log(NULL, AV_LOG_INFO, "decoding frame %d buf_size = %d\n", frame_count++, buf_size);

    //Synchronize the frame.
    frame_start = ac3_synchronize(buf, buf_size);
    if (frame_start == -1) {
        av_log(avctx, AV_LOG_ERROR, "frame is not synchronized\n");
        *data_size = 0;
        return buf_size;
    }

    //Initialize the GetBitContext with the start of valid AC3 Frame.
    init_get_bits(&(ctx->gb), buf + frame_start, (buf_size - frame_start) * 8);

    //Parse the syncinfo.
    //If 'fscod' or 'bsid' is not valid the decoder shall mute as per the standard.
    if (!ac3_parse_sync_info(ctx)) {
        av_log(avctx, AV_LOG_ERROR, "\n");
        *data_size = 0;
        return buf_size;
    }

    //Parse the BSI.
    //If 'bsid' is not valid decoder shall not decode the audio as per the standard.
    ac3_parse_bsi(ctx);

    avctx->sample_rate = ctx->sampling_rate;
    avctx->bit_rate = ctx->bit_rate;

    if (avctx->channels == 0) {
        ctx->blkoutput |= AC3_OUTPUT_UNMODIFIED;
        if (ctx->lfeon)
            ctx->blkoutput |= AC3_OUTPUT_LFEON;
        avctx->channels = ctx->nfchans + ctx->lfeon;
    }
    else if (avctx->channels == 1)
        ctx->blkoutput |= AC3_OUTPUT_MONO;
    else if (avctx->channels == 2) {
        if (ctx->dsurmod == 0x02)
            ctx->blkoutput |= AC3_OUTPUT_DOLBY;
        else
            ctx->blkoutput |= AC3_OUTPUT_STEREO;
    }
    else {
        if (avctx->channels < (ctx->nfchans + ctx->lfeon))
            av_log(avctx, AV_LOG_INFO, "ac3_decoder: AC3 Source Channels Are Less Then Specified %d: Output to %d Channels\n",avctx->channels, ctx->nfchans + ctx->lfeon);
        ctx->blkoutput |= AC3_OUTPUT_UNMODIFIED;
        if (ctx->lfeon)
            ctx->blkoutput |= AC3_OUTPUT_LFEON;
        avctx->channels = ctx->nfchans + ctx->lfeon;
    }

    av_log(avctx, AV_LOG_INFO, "channels = %d \t bit rate = %d \t sampling rate = %d \n", avctx->channels, avctx->bit_rate * 1000, avctx->sample_rate);

    //Parse the Audio Blocks.
    for (i = 0; i < AUDIO_BLOCKS; i++) {
        if (ac3_parse_audio_block(ctx)) {
            av_log(avctx, AV_LOG_ERROR, "error parsing the audio block\n");
            *data_size = 0;
            return ctx->frame_size;
        }
        for (k = 0; k < BLOCK_SIZE; k++) {
            j = (ctx->blkoutput & AC3_OUTPUT_LFEON) ? 0 : 1;
            for (; j <= avctx->channels; j++) {
                value = convert(ctx->output[j][k]);
                *(out_samples++) = value;
            }
        }
    }
    *data_size = AUDIO_BLOCKS * BLOCK_SIZE * avctx->channels * sizeof (int16_t);
    return ctx->frame_size;
}

static int ac3_decode_end(AVCodecContext *ctx)
{
    return 0;
}

AVCodec lgpl_ac3_decoder = {
    "ac3",
    CODEC_TYPE_AUDIO,
    CODEC_ID_AC3,
    sizeof (AC3DecodeContext),
    ac3_decode_init,
    NULL,
    ac3_decode_end,
    ac3_decode_frame,
};

