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

#include "ac3.h"
#include "ac3tab.h"
#include "ac3_decoder.h"
#include "avcodec.h"
#include "bitstream.h"
#include "dsputil.h"
#include "avutil.h"
#include "common.h"

#define MAX_CHANNELS    6
#define MAX_BLOCK_SIZE  256
#define MAX_BLOCKS      6

/* Synchronization information. */
typedef struct {
    uint16_t sync_word;     //synchronization word = always 0x0b77
    uint16_t crc1;          //crc for the first 5/8 of the frame
    uint8_t  fscod;         //sampling rate code
    uint8_t  frmsizecod;    //frame size code

    /* Derived Attributes */
    int sampling_rate;      //sampling rate - 48, 44.1 or 32 kHz (value in Hz)
    int bit_rate;           //nominal bit rate (value in kbps)
    int framesize;          //frame size - 16 bit words
} ac3_sync_info;

/* flags for the BSI. */
#define AC3_BSI_LFEON       0x00000001  //low frequency effects channel on
#define AC3_BSI_COMPRE      0x00000002  //compression exists
#define AC3_BSI_LANGCODE    0x00000004  //langcode exists
#define AC3_BSI_AUDPRODIE   0x00000008  //audio production information exists
#define AC3_BSI_COMPR2E     0x00000010  //compr2 exists
#define AC3_BSI_LANGCOD2E   0x00000020  //langcod2 exists
#define AC3_BSI_AUDPRODI2E  0x00000040  //audio production information 2 exists
#define AC3_BSI_COPYRIGHTB  0x00000080  //copyright
#define AC3_BSI_ORIGBS      0x00000100  //original bit stream
#define AC3_BSI_TIMECOD1E   0x00000200  //timecod1 exists
#define AC3_BSI_TIMECOD2E   0x00000400  //timecod2 exists
#define AC3_BSI_ADDBSIE     0x00000800  //additional bit stream information exists

/* Bit Stream Information. */
typedef struct {
    uint32_t flags;
    uint8_t  bsid;          //bit stream identification
    uint8_t  bsmod;         //bit stream mode - type of service
    uint8_t  acmod;         //audio coding mode - which channels are in use
    uint8_t  cmixlev;       //center mix level
    uint8_t  surmixlev;     //surround mix level
    uint8_t  dsurmod;       //dynamic surround encoded
    uint8_t  dialnorm;      //dialog normalization
    uint8_t  compr;         //compression gain word
    uint8_t  langcod;       //language code
    uint8_t  mixlevel;      //mixing level
    uint8_t  roomtyp;       //room type
    uint8_t  dialnorm2;     //dialogue normalization for 1+1 mode
    uint8_t  compr2;        //compression gain word for 1+1 mode
    uint8_t  langcod2;      //language code for 1+1 mode
    uint8_t  mixlevel2;     //mixing level for 1+1 mode
    uint8_t  roomtyp2;      //room type for 1+1 mode
    uint16_t timecod1;      //timecode 1
    uint16_t timecod2;      //timecode 2
    uint8_t  addbsil;       //additional bit stream information length

    /* Dervied Attributes */
    int      nfchans;      //number of full bandwidth channels - derived from acmod
} ac3_bsi;

/* #defs relevant to Audio Block. */
#define MAX_FBW_CHANNELS    5       //maximum full bandwidth channels
#define NUM_LFE_GROUPS      3       //number of LFE Groups
#define MAX_NUM_SEGS        8       //maximum number of segments per delta bit allocation
#define NUM_LFE_MANTS       7       //number of lfe mantissas
#define MAX_CPL_SUBNDS      18      //maximum number of coupling sub bands
#define MAX_CPL_BNDS        18      //maximum number of coupling bands
#define MAX_CPL_GRPS        253     //maximum number of coupling groups
#define MAX_CHNL_GRPS       88      //maximum number of channel groups
#define MAX_NUM_MANTISSAS   256     //maximum number of mantissas

/* flags for the Audio Block. */
#define AC3_AB_DYNRNGE      0x00000001  //dynamic range control exists
#define AC3_AB_DYNRNG2E     0x00000002  //dynamic range control 2 exists
#define AC3_AB_CPLSTRE      0x00000004  //coupling strategy exists
#define AC3_AB_CPLINU       0x00000008  //coupling in use
#define AC3_AB_PHSFLGINU    0x00000010  //phase flag in use
#define AC3_AB_REMATSTR     0x00000020  //rematrixing required
#define AC3_AB_LFEEXPSTR    0x00000100  //lfe exponent strategy
#define AC3_AB_BAIE         0x00000200  //bit allocation information exists
#define AC3_AB_SNROFFSTE    0x00000400  //SNR offset exists
#define AC3_AB_CPLLEAKE     0x00000800  //coupling leak initialization exists
#define AC3_AB_DELTBAIE     0x00001000  //delta bit allocation information exists
#define AC3_AB_SKIPLE       0x00002000  //skip length exists

/* Exponent strategies. */
#define AC3_EXPSTR_D15      0x01
#define AC3_EXPSTR_D25      0x02
#define AC3_EXPSTR_D45      0x03
#define AC3_EXPSTR_REUSE    0x00

/* Bit allocation strategies */
#define AC3_DBASTR_NEW      0x01
#define AC3_DBASTR_NONE     0x02
#define AC3_DBASTR_RESERVED 0x03
#define AC3_DBASTR_REUSE    0x00

/* Audio Block */
typedef struct {
    uint32_t flags;
    uint8_t  blksw;                 //block switch flags for channels in use
    uint8_t  dithflag;              //dithering flags for channels in use
    int8_t   dynrng;                //dynamic range word
    int8_t   dynrng2;               //dynamic range word for 1+1 mode
    uint8_t  chincpl;               //channel in coupling flags for channels in use
    uint8_t  cplbegf;               //coupling begin frequency code
    uint8_t  cplendf;               //coupling end frequency code
    uint32_t cplbndstrc;            //coupling band structure
    uint8_t  cplcoe;                //coupling co-ordinates exists for the channel in use
    uint8_t  mstrcplco[5];          //master coupling co-ordinate for channels in use
    uint8_t  cplcoexp[5][18];       //coupling co-ordinate exponenets
    uint8_t  cplcomant[5][18];      //coupling co-ordinate mantissas
    uint32_t phsflg;                //phase flag per band
    uint8_t  rematflg;              //rematrixing flag
    uint8_t  cplexpstr;             //coupling exponent strategy
    uint8_t  chexpstr[5];           //channel exponent strategy
    uint8_t  lfeexpstr;             //lfe exponent strategy
    uint8_t  chbwcod[5];            //channel bandwdith code for channels in use
    uint8_t  cplabsexp;             //coupling absolute exponent
    uint8_t  gainrng[5];            //gain range
    uint8_t  sdcycod;               //slow decay code
    uint8_t  fdcycod;               //fast decay code
    uint8_t  sgaincod;              //slow gain code
    uint8_t  dbpbcod;               //dB per bit code
    uint8_t  floorcod;              //masking floor code
    uint8_t  csnroffst;             //coarse SNR offset
    uint8_t  cplfsnroffst;          //coupling fine SNR offset
    uint8_t  cplfgaincod;           //coupling fast gain code
    uint8_t  fsnroffst[5];          //fine SNR offset for channels in use
    uint8_t  fgaincod[5];           //fast gain code for channels in use
    uint8_t  lfefsnroffst;          //lfe fine SNR offset
    uint8_t  lfefgaincod;           //lfe fast gain code
    uint8_t  cplfleak;              //coupling fast leak initialization value
    uint8_t  cplsleak;              //coupling slow leak initialization value
    uint8_t  cpldeltbae;            //coupling delta bit allocation exists
    uint8_t  deltbae[5];            //delta bit allocation exists for channels in use
    uint8_t  cpldeltnseg;           //coupling delta bit allocation number of segments
    uint8_t  cpldeltoffst[8];       //coupling delta offset
    uint8_t  cpldeltlen[8];         //coupling delta len
    uint8_t  cpldeltba[8];          //coupling delta bit allocation
    uint8_t  deltnseg[5];           //delta bit allocation number of segments per channel
    uint8_t  deltoffst[5][8];       //delta offset for channels in use
    uint8_t  deltlen[5][8];         //delta len for channels in use
    uint8_t  deltba[5][8];          //delta bit allocation
    uint16_t skipl;                 //skip length

    /* Derived Attributes */
    int      ncplsubnd;             //number of active coupling sub bands = 3 + cplendf - cplbegf
    int      ncplbnd;               //derived from ncplsubnd and cplbndstrc
    int      ncplgrps;              //derived from ncplsubnd, cplexpstr
    int      nchgrps[5];            //derived from chexpstr, and cplbegf or chbwcod
    int      nchmant[5];            //derived from cplbegf or chbwcod
    int      ncplmant;              //derived from ncplsubnd = 12 * ncplsubnd

    uint8_t  cplstrtbnd;            //coupling start band for bit allocation
    uint8_t  cplstrtmant;           //coupling start mantissa
    uint8_t  cplendmant;            //coupling end mantissa
    uint8_t  endmant[5];            //channel end mantissas

    uint8_t  dcplexps[256];         //decoded coupling exponents
    uint8_t  dexps[5][256];         //decoded fbw channel exponents
    uint8_t  dlfeexps[256];         //decoded lfe exponents
    uint8_t  cplbap[256];           //coupling bit allocation parameters table
    uint8_t  bap[5][256];           //fbw channels bit allocation parameters table
    uint8_t  lfebap[256];           //lfe bit allocaiton parameters table

    DECLARE_ALIGNED_16(float, transform_coeffs[MAX_CHANNELS][MAX_BLOCK_SIZE]); //transform coefficients
    DECLARE_ALIGNED_16(float, cplcoeffs[256]);        //temporary storage for coupling transform coefficients
    DECLARE_ALIGNED_16(float, block_output[MAX_CHANNELS][MAX_BLOCK_SIZE]);
    float    cplco[5][18];          //coupling coordinates
    float    chcoeffs[6];           //channel coefficients for downmix
} ac3_audio_block;



#define AC3_OUTPUT_UNMODIFIED   0x00
#define AC3_OUTPUT_MONO         0x01
#define AC3_OUTPUT_STEREO       0x02
#define AC3_OUTPUT_DOLBY        0x03

#define AC3_INPUT_DUALMONO      0x00
#define AC3_INPUT_MONO          0x01
#define AC3_INPUT_STEREO        0x02
#define AC3_INPUT_3F            0x03
#define AC3_INPUT_2F_1R         0x04
#define AC3_INPUT_3F_1R         0x05
#define AC3_INPUT_2F_2R         0x06
#define AC3_INPUT_3F_2R         0x07

/* BEGIN Mersenne Twister Code. */
#define N 624
#define M 397
#define MATRIX_A    0x9908b0df
#define UPPER_MASK  0x80000000
#define LOWER_MASK  0x7fffffff

typedef struct {
    uint32_t mt[N];
    int      mti;
} dither_state;

static void dither_seed(dither_state *state, uint32_t seed)
{
    if (seed == 0)
        seed = 0x1f2e3d4c;

    state->mt[0] = seed;
    for (state->mti = 1; state->mti < N; state->mti++)
        state->mt[state->mti] = ((69069 * state->mt[state->mti - 1]) + 1);
}

static uint32_t dither_uint32(dither_state *state)
{
    uint32_t y;
    static const uint32_t mag01[2] = { 0x00, MATRIX_A };
    int kk;

    if (state->mti >= N) {
        for (kk = 0; kk < N - M; kk++) {
            y = (state->mt[kk] & UPPER_MASK) | (state->mt[kk + 1] & LOWER_MASK);
            state->mt[kk] = state->mt[kk + M] ^ (y >> 1) ^ mag01[y & 0x01];
        }
        for (;kk < N - 1; kk++) {
            y = (state->mt[kk] & UPPER_MASK) | (state->mt[kk + 1] & LOWER_MASK);
            state->mt[kk] = state->mt[kk + (M - N)] ^ (y >> 1) ^ mag01[y & 0x01];
        }
        y = (state->mt[N - 1] & UPPER_MASK) | (state->mt[0] & LOWER_MASK);
        state->mt[N - 1] = state->mt[M - 1] ^ (y >> 1) ^ mag01[y & 0x01];

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

/* AC3 Context. */
typedef struct {
    ac3_sync_info   sync_info;
    ac3_bsi         bsi;
    ac3_audio_block audio_block;
    dither_state    state;
    MDCTContext     imdct_ctx_256;
    MDCTContext     imdct_ctx_512;
    GetBitContext   gb;
    int             output;
    DECLARE_ALIGNED_16(float, delay[MAX_CHANNELS][MAX_BLOCK_SIZE]);
    DECLARE_ALIGNED_16(FFTSample, tmp_imdct[MAX_BLOCK_SIZE * 2]);
    DECLARE_ALIGNED_16(FFTSample, tmp_output[MAX_BLOCK_SIZE * 2]);
} AC3DecodeContext;

static void ac3_common_init1(void)
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
}


static int ac3_decode_init(AVCodecContext *avctx)
{
    AC3DecodeContext *ctx = avctx->priv_data;

    ac3_common_init1();

    ff_mdct_init(&ctx->imdct_ctx_256, 8, 1);
    ff_mdct_init(&ctx->imdct_ctx_512, 9, 1);
    dither_seed(&ctx->state, 0);

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
    ac3_sync_info *sync_info = &ctx->sync_info;
    ac3_bsi *bsi = &ctx->bsi;
    GetBitContext *gb = &ctx->gb;

    sync_info->sync_word = get_bits(gb, 16);
    sync_info->crc1 = get_bits(gb, 16);
    sync_info->fscod = get_bits(gb, 2);
    if (sync_info->fscod == 0x03)
        return 0;
    sync_info->frmsizecod = get_bits(gb, 6);
    if (sync_info->frmsizecod >= 38)
        return 0;
    sync_info->sampling_rate = ac3_freqs[sync_info->fscod];
    sync_info->bit_rate = ac3_bitratetab[sync_info->frmsizecod >> 1];

    /* we include it here in order to determine validity of ac3 frame */
    bsi->bsid = get_bits(gb, 5);
    if (bsi->bsid > 0x08)
        return 0;
    bsi->bsmod = get_bits(gb, 3);

    switch (sync_info->fscod) {
        case 0x00:
            sync_info->framesize = 4 * sync_info->bit_rate;
            return sync_info->framesize;
        case 0x01:
            sync_info->framesize = 2 * (320 * sync_info->bit_rate / 147 + (sync_info->frmsizecod & 1));
            return sync_info->framesize;
        case 0x02:
            sync_info->framesize =  6 * sync_info->bit_rate;
            return sync_info->framesize;
    }

    /* never reached */
    return 0;
}

//Returns -1 when
static int ac3_parse_bsi(AC3DecodeContext *ctx)
{
    ac3_bsi *bsi = &ctx->bsi;
    uint32_t *flags = &bsi->flags;
    GetBitContext *gb = &ctx->gb;
    int i;

    *flags = 0;
    bsi->cmixlev = 0;
    bsi->surmixlev = 0;
    bsi->dsurmod = 0;
    ctx->audio_block.cpldeltbae = AC3_DBASTR_NONE;
    ctx->audio_block.cpldeltnseg = 0;
    for (i = 0; i < 5; i++) {
        ctx->audio_block.deltbae[i] = AC3_DBASTR_NONE;
        ctx->audio_block.deltnseg[i] = 0;
    }

    bsi->acmod = get_bits(gb, 3);
    if (bsi->acmod & 0x01 && bsi->acmod != 0x01)
        bsi->cmixlev = get_bits(gb, 2);
    if (bsi->acmod & 0x04)
        bsi->surmixlev = get_bits(gb, 2);
    if (bsi->acmod == 0x02)
        bsi->dsurmod = get_bits(gb, 2);
    if (get_bits1(gb))
        *flags |= AC3_BSI_LFEON;
    bsi->dialnorm = get_bits(gb, 5);
    if (get_bits1(gb)) {
        *flags |= AC3_BSI_COMPRE;
        bsi->compr = get_bits(gb, 8);
    }
    if (get_bits1(gb)) {
        *flags |= AC3_BSI_LANGCODE;
        bsi->langcod = get_bits(gb, 8);
    }
    if (get_bits1(gb)) {
        *flags |= AC3_BSI_AUDPRODIE;
        bsi->mixlevel = get_bits(gb, 5);
        bsi->roomtyp = get_bits(gb, 2);
    }
    if (bsi->acmod == 0x00) {
        bsi->dialnorm2 = get_bits(gb, 5);
        if (get_bits1(gb)) {
            *flags |= AC3_BSI_COMPR2E;
            bsi->compr2 = get_bits(gb, 8);
        }
        if (get_bits1(gb)) {
            *flags |= AC3_BSI_LANGCOD2E;
            bsi->langcod2 = get_bits(gb, 8);
        }
        if (get_bits1(gb)) {
            *flags |= AC3_BSI_AUDPRODIE;
            bsi->mixlevel2 = get_bits(gb, 5);
            bsi->roomtyp2 = get_bits(gb, 2);
        }
    }
    if (get_bits1(gb))
        *flags |= AC3_BSI_COPYRIGHTB;
    if (get_bits1(gb))
        *flags |= AC3_BSI_ORIGBS;
    if (get_bits1(gb)) {
        *flags |= AC3_BSI_TIMECOD1E;
        bsi->timecod1 = get_bits(gb, 14);
    }
    if (get_bits1(gb)) {
        *flags |= AC3_BSI_TIMECOD2E;
        bsi->timecod2 = get_bits(gb, 14);
    }
    if (get_bits1(gb)) {
        *flags |= AC3_BSI_ADDBSIE;
        bsi->addbsil = get_bits(gb, 6);
        for (i = 0; i < (bsi->addbsil + 1); i++)
            skip_bits(gb, 8);
    }

    bsi->nfchans = nfchans_tbl[bsi->acmod];

    return 0;
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

    address = FFMIN(ABS(c) >> 1, 255);

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
            a = FFMAX(0, a - 64);
    }
    else if (bin < 20) {
        if ((b0 + 256) == b1)
            a = 320;
        else if (b0 > b1)
            a = FFMAX(0, a - 64);
    }
    else
        a = FFMAX(0, a - 128);

    return a;
}

/* do the bit allocation for chnl.
 * chnl = 0 to 4 - fbw channel
 * chnl = 5 coupling channel
 * chnl = 6 lfe channel
 */
static void do_bit_allocation1(AC3DecodeContext *ctx, int chnl)
{
    ac3_audio_block *ab = &ctx->audio_block;
    int sdecay, fdecay, sgain, dbknee, floor;
    int lowcomp = 0, fgain = 0, snroffset = 0, fastleak = 0, slowleak = 0;
    int psd[256], bndpsd[50], excite[50], mask[50], delta;
    int start = 0, end = 0, bin = 0, i = 0, j = 0, k = 0, lastbin = 0, bndstrt = 0;
    int bndend = 0, begin = 0, deltnseg = 0, band = 0, seg = 0, address = 0;
    int fscod = ctx->sync_info.fscod;
    uint8_t *exps, *deltoffst = 0, *deltlen = 0, *deltba = 0;
    uint8_t *baps;
    int do_delta = 0;

    /* initialization */
    sdecay = sdecaytab[ab->sdcycod];
    fdecay = fdecaytab[ab->fdcycod];
    sgain = sgaintab[ab->sgaincod];
    dbknee = dbkneetab[ab->dbpbcod];
    floor = floortab[ab->floorcod];

    if (chnl == 5) {
        start = ab->cplstrtmant;
        end = ab->cplendmant;
        fgain = fgaintab[ab->cplfgaincod];
        snroffset = (((ab->csnroffst - 15) << 4) + ab->cplfsnroffst) << 2;
        fastleak = (ab->cplfleak << 8) + 768;
        slowleak = (ab->cplsleak << 8) + 768;
        exps = ab->dcplexps;
        baps = ab->cplbap;
        if (ab->cpldeltbae == AC3_DBASTR_NEW || ab->cpldeltbae == AC3_DBASTR_REUSE) {
            do_delta = 1;
            deltnseg = ab->cpldeltnseg;
            deltoffst = ab->cpldeltoffst;
            deltlen = ab->cpldeltlen;
            deltba = ab->cpldeltba;
        }
    }
    else if (chnl == 6) {
        start = 0;
        end = 7;
        lowcomp = 0;
        fastleak = 0;
        slowleak = 0;
        fgain = fgaintab[ab->lfefgaincod];
        snroffset = (((ab->csnroffst - 15) << 4) + ab->lfefsnroffst) << 2;
        exps = ab->dlfeexps;
        baps = ab->lfebap;
    }
    else {
        start = 0;
        end = ab->endmant[chnl];
        lowcomp = 0;
        fastleak = 0;
        slowleak = 0;
        fgain = fgaintab[ab->fgaincod[chnl]];
        snroffset = (((ab->csnroffst - 15) << 4) + ab->fsnroffst[chnl]) << 2;
        exps = ab->dexps[chnl];
        baps = ab->bap[chnl];
        if (ab->deltbae[chnl] == AC3_DBASTR_NEW || ab->deltbae[chnl] == AC3_DBASTR_REUSE) {
            do_delta = 1;
            deltnseg = ab->deltnseg[chnl];
            deltoffst = ab->deltoffst[chnl];
            deltlen = ab->deltlen[chnl];
            deltba = ab->deltba[chnl];
        }
    }

    for (bin = start; bin < end; bin++) /* exponent mapping into psd */
        psd[bin] = (3072 - ((int)(exps[bin]) << 7));

    /* psd integration */
    j = start;
    k = masktab[start];
    do {
        lastbin = FFMIN(bndtab[k] + bndsz[k], end);
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
            if (bndend != 7 || bin != 6)
                lowcomp = calc_lowcomp(lowcomp, bndpsd[bin], bndpsd[bin + 1], bin);
            fastleak = bndpsd[bin] - fgain;
            slowleak = bndpsd[bin] - sgain;
            excite[bin] = fastleak - lowcomp;
            if (bndend != 7 || bin != 6)
                if (bndpsd[bin] <= bndpsd[bin + 1]) {
                    begin = bin + 1;
                    break;
                }
        }
        for (bin = begin; bin < FFMIN(bndend, 22); bin++) {
            if (bndend != 7 || bin != 6)
                lowcomp = calc_lowcomp(lowcomp, bndpsd[bin], bndpsd[bin + 1], bin);
            fastleak -= fdecay;
            fastleak = FFMAX(fastleak, bndpsd[bin] - fgain);
            slowleak -= sdecay;
            slowleak = FFMAX(slowleak, bndpsd[bin] - sgain);
            excite[bin] = FFMAX(fastleak - lowcomp, slowleak);
        }
        begin = 22;
    }
    else {
        begin = bndstrt;
    }
    for (bin = begin; bin < bndend; bin++) {
        fastleak -= fdecay;
        fastleak = FFMAX(fastleak, bndpsd[bin] - fgain);
        slowleak -= sdecay;
        slowleak = FFMAX(slowleak, bndpsd[bin] - sgain);
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
            band += (int)(deltoffst[seg]);
            if ((int)(deltba[seg]) >= 4)
                delta = ((int)(deltba[seg]) - 3) << 7;
            else
                delta = ((int)(deltba[seg]) - 4) << 7;
            for (k = 0; k < (int)(deltlen[seg]); k++) {
                mask[band] += delta;
                band++;
            }
        }
    }

    /*compute the bit allocation */
    i = start;
    j = masktab[start];
    do {
        lastbin = FFMIN(bndtab[j] + bndsz[j], end);
        mask[j] -= snroffset;
        mask[j] -= floor;
        if (mask[j] < 0)
            mask[j] = 0;
        mask[j] &= 0x1fe0;
        mask[j] += floor;
        for (k = i; k < lastbin; k++) {
            address = (psd[i] - mask[j]) >> 5;
            address = FFMIN(63, FFMAX(0, address));
            baps[i] = baptab[address];
            i++;
        }
        j++;
    } while (end > lastbin);
}

static void do_bit_allocation(AC3DecodeContext *ctx, int flags)
{
    ac3_audio_block *ab = &ctx->audio_block;
    int i, snroffst = 0;

    if (!flags) /* bit allocation is not required */
        return;

    if (ab->flags & AC3_AB_SNROFFSTE) { /* check whether snroffsts are zero */
        snroffst += ab->csnroffst;
        if (ab->flags & AC3_AB_CPLINU)
            snroffst += ab->cplfsnroffst;
        for (i = 0; i < ctx->bsi.nfchans; i++)
            snroffst += ab->fsnroffst[i];
        if (ctx->bsi.flags & AC3_BSI_LFEON)
            snroffst += ab->lfefsnroffst;
        if (!snroffst) {
            memset(ab->cplbap, 0, sizeof (ab->cplbap));
            for (i = 0; i < ctx->bsi.nfchans; i++)
                memset(ab->bap[i], 0, sizeof (ab->bap[i]));
            memset(ab->lfebap, 0, sizeof (ab->lfebap));

            return;
        }
    }

    /* perform bit allocation */
    if ((ab->flags & AC3_AB_CPLINU) && (flags & 64))
        do_bit_allocation1(ctx, 5);
    for (i = 0; i < ctx->bsi.nfchans; i++)
        if (flags & (1 << i))
            do_bit_allocation1(ctx, i);
    if ((ctx->bsi.flags & AC3_BSI_LFEON) && (flags & 32))
        do_bit_allocation1(ctx, 6);
}

static inline float to_float(uint8_t exp, int16_t mantissa)
{
    return ((float) (mantissa * scale_factors[exp]));
}

typedef struct { /* grouped mantissas for 3-level 5-leve and 11-level quantization */
    uint8_t gcodes[3];
    uint8_t gcptr;
} mant_group;

/* Get the transform coefficients for particular channel */
static int get_transform_coeffs1(uint8_t *exps, uint8_t *bap, float chcoeff,
        float *coeffs, int start, int end, int dith_flag, GetBitContext *gb,
        dither_state *state)
{
    int16_t mantissa;
    int i;
    int gcode;
    mant_group l3_grp, l5_grp, l11_grp;

    for (i = 0; i < 3; i++)
        l3_grp.gcodes[i] = l5_grp.gcodes[i] = l11_grp.gcodes[i] = -1;
    l3_grp.gcptr = l5_grp.gcptr = 3;
    l11_grp.gcptr = 2;

    i = 0;
    while (i < start)
        coeffs[i++] = 0;

    for (i = start; i < end; i++) {
        switch (bap[i]) {
            case 0:
                if (!dith_flag) {
                    coeffs[i] = 0;
                    continue;
                }
                else {
                    mantissa = dither_int16(state);
                    coeffs[i] = to_float(exps[i], mantissa) * chcoeff;
                    continue;
                }

            case 1:
                if (l3_grp.gcptr > 2) {
                    gcode = get_bits(gb, 5);
                    if (gcode > 26)
                        return -1;
                    l3_grp.gcodes[0] = gcode / 9;
                    l3_grp.gcodes[1] = (gcode % 9) / 3;
                    l3_grp.gcodes[2] = (gcode % 9) % 3;
                    l3_grp.gcptr = 0;
                }
                mantissa = l3_q_tab[l3_grp.gcodes[l3_grp.gcptr++]];
                coeffs[i] = to_float(exps[i], mantissa) * chcoeff;
                continue;

            case 2:
                if (l5_grp.gcptr > 2) {
                    gcode = get_bits(gb, 7);
                    if (gcode > 124)
                        return -1;
                    l5_grp.gcodes[0] = gcode / 25;
                    l5_grp.gcodes[1] = (gcode % 25) / 5;
                    l5_grp.gcodes[2] = (gcode % 25) % 5;
                    l5_grp.gcptr = 0;
                }
                mantissa = l5_q_tab[l5_grp.gcodes[l5_grp.gcptr++]];
                coeffs[i] = to_float(exps[i], mantissa) * chcoeff;
                continue;

            case 3:
                mantissa = get_bits(gb, 3);
                if (mantissa > 6)
                    return -1;
                mantissa = l7_q_tab[mantissa];
                coeffs[i] = to_float(exps[i], mantissa);
                continue;

            case 4:
                if (l11_grp.gcptr > 1) {
                    gcode = get_bits(gb, 7);
                    if (gcode > 120)
                        return -1;
                    l11_grp.gcodes[0] = gcode / 11;
                    l11_grp.gcodes[1] = gcode % 11;
                }
                mantissa = l11_q_tab[l11_grp.gcodes[l11_grp.gcptr++]];
                coeffs[i] = to_float(exps[i], mantissa) * chcoeff;
                continue;

            case 5:
                mantissa = get_bits(gb, 4);
                if (mantissa > 14)
                    return -1;
                mantissa = l15_q_tab[mantissa];
                coeffs[i] = to_float(exps[i], mantissa) * chcoeff;
                continue;

            default:
                mantissa = get_bits(gb, qntztab[bap[i]]) << (16 - qntztab[bap[i]]);
                coeffs[i] = to_float(exps[i], mantissa) * chcoeff;
                continue;
        }
    }

    i = end;
    while (i < 256)
        coeffs[i++] = 0;

    return 0;
}

static void uncouple_channels(AC3DecodeContext * ctx)
{
    ac3_audio_block *ab = &ctx->audio_block;
    int ch, sbnd, bin;
    int index;
    int16_t mantissa;

    /* uncouple channels */
    for (ch = 0; ch < ctx->bsi.nfchans; ch++)
        if (ab->chincpl & (1 << ch))
            for (sbnd = ab->cplbegf; sbnd < 3 + ab->cplendf; sbnd++)
                for (bin = 0; bin < 12; bin++) {
                    index = sbnd * 12 + bin + 37;
                    ab->transform_coeffs[ch + 1][index] = ab->cplcoeffs[index] * ab->cplco[ch][sbnd] * ab->chcoeffs[ch];
                    /* generate dither if required */
                    if (!ab->bap[ch][index] && (ab->chincpl & (1 << ch)) && (ab->dithflag & (1 << ch))) {
                        mantissa = dither_int16(&ctx->state);
                        ab->transform_coeffs[ch + 1][index] = to_float(ab->dexps[ch][index], mantissa) * ab->chcoeffs[ch];
                    }
                }
}

static int get_transform_coeffs(AC3DecodeContext * ctx)
{
    int i;
    ac3_audio_block *ab = &ctx->audio_block;
    int got_cplchan = 0;
    int dithflag = 0;

    for (i = 0; i < ctx->bsi.nfchans; i++) {
        dithflag = ab->dithflag & (1 << i);
        /* transform coefficients for individual channel */
        if (get_transform_coeffs1(ab->dexps[i], ab->bap[i], ab->chcoeffs[i], ab->transform_coeffs[i + 1],
                    0, ab->endmant[i], dithflag, &ctx->gb, &ctx->state))
            return -1;
        /* tranform coefficients for coupling channels */
        if ((ab->flags & AC3_AB_CPLINU) && (ab->chincpl & (1 << i)) && !got_cplchan) {
            if (get_transform_coeffs1(ab->dcplexps, ab->cplbap, 1.0f, ab->cplcoeffs,
                        ab->cplstrtmant, ab->cplendmant, 0, &ctx->gb, &ctx->state))
                return -1;
            got_cplchan = 1;
        }
    }
    if (ctx->bsi.flags & AC3_BSI_LFEON)
        if (get_transform_coeffs1(ab->dlfeexps, ab->lfebap, 1.0f, ab->transform_coeffs[0], 0, 7, 0, &ctx->gb, &ctx->state))
                return -1;

    /* uncouple the channels from the coupling channel */
    if (ab->flags & AC3_AB_CPLINU)
        uncouple_channels(ctx);

    return 0;
}

/* generate coupling co-ordinates for each coupling subband
 * from coupling co-ordinates of each band and coupling band
 * structure information
 */
static void generate_coupling_coordinates(AC3DecodeContext * ctx)
{
    ac3_audio_block *ab = &ctx->audio_block;
    uint8_t exp, mstrcplco;
    int16_t mant;
    uint32_t cplbndstrc = (1 << ab->ncplsubnd) >> 1;
    int ch, bnd, sbnd;
    float cplco;

    if (ab->cplcoe)
        for (ch = 0; ch < ctx->bsi.nfchans; ch++)
            if (ab->cplcoe & (1 << ch)) {
                mstrcplco = 3 * ab->mstrcplco[ch];
                sbnd = ab->cplbegf;
                for (bnd = 0; bnd < ab->ncplbnd; bnd++) {
                    exp = ab->cplcoexp[ch][bnd];
                    if (exp == 15)
                        mant = ab->cplcomant[ch][bnd] <<= 14;
                    else
                        mant = (ab->cplcomant[ch][bnd] | 0x10) << 13;
                    cplco = to_float(exp + mstrcplco, mant);
                    if (ctx->bsi.acmod == 0x02 && (ab->flags & AC3_AB_PHSFLGINU) && ch == 1
                            && (ab->phsflg & (1 << bnd)))
                        cplco = -cplco; /* invert the right channel */
                    ab->cplco[ch][sbnd++] = cplco;
                    while (cplbndstrc & ab->cplbndstrc) {
                        cplbndstrc >>= 1;
                        ab->cplco[ch][sbnd++] = cplco;
                    }
                    cplbndstrc >>= 1;
                }
            }
}

static void do_rematrixing1(AC3DecodeContext *ctx, int start, int end)
{
    float tmp0, tmp1;

    while (start < end) {
        tmp0 = ctx->audio_block.transform_coeffs[1][start];
        tmp1 = ctx->audio_block.transform_coeffs[2][start];
        ctx->audio_block.transform_coeffs[1][start] = tmp0 + tmp1;
        ctx->audio_block.transform_coeffs[2][start] = tmp0 - tmp1;
        start++;
    }
}

static void do_rematrixing(AC3DecodeContext *ctx)
{
    ac3_audio_block *ab = &ctx->audio_block;
    uint8_t bnd1 = 13, bnd2 = 25, bnd3 = 37, bnd4 = 61;
    uint8_t bndend;

    bndend = FFMIN(ab->endmant[0], ab->endmant[1]);
    if (ab->rematflg & 1)
        do_rematrixing1(ctx, bnd1, bnd2);
    if (ab->rematflg & 2)
        do_rematrixing1(ctx, bnd2, bnd3);
    if (ab->rematflg & 4) {
        if (ab->cplbegf > 0 && ab->cplbegf <= 2 && (ab->flags & AC3_AB_CPLINU))
            do_rematrixing1(ctx, bnd3, bndend);
        else {
            do_rematrixing1(ctx, bnd3, bnd4);
            if (ab->rematflg & 8)
                do_rematrixing1(ctx, bnd4, bndend);
        }
    }
}

static void get_downmix_coeffs(AC3DecodeContext *ctx)
{
    int from = ctx->bsi.acmod;
    int to = ctx->output;
    float clev = clevs[ctx->bsi.cmixlev];
    float slev = slevs[ctx->bsi.surmixlev];
    ac3_audio_block *ab = &ctx->audio_block;

    if (to == AC3_OUTPUT_UNMODIFIED)
        return;

    switch (from) {
        case AC3_INPUT_DUALMONO:
            switch (to) {
                case AC3_OUTPUT_MONO:
                case AC3_OUTPUT_STEREO: /* We Assume that sum of both mono channels is requested */
                    ab->chcoeffs[0] *= LEVEL_MINUS_6DB;
                    ab->chcoeffs[1] *= LEVEL_MINUS_6DB;
                    break;
            }
            break;
        case AC3_INPUT_MONO:
            switch (to) {
                case AC3_OUTPUT_STEREO:
                    ab->chcoeffs[0] *= LEVEL_MINUS_3DB;
                    break;
            }
            break;
        case AC3_INPUT_STEREO:
            switch (to) {
                case AC3_OUTPUT_MONO:
                    ab->chcoeffs[0] *= LEVEL_MINUS_3DB;
                    ab->chcoeffs[1] *= LEVEL_MINUS_3DB;
                    break;
            }
            break;
        case AC3_INPUT_3F:
            switch (to) {
                case AC3_OUTPUT_MONO:
                    ab->chcoeffs[0] *= LEVEL_MINUS_3DB;
                    ab->chcoeffs[2] *= LEVEL_MINUS_3DB;
                    ab->chcoeffs[1] *= clev * LEVEL_PLUS_3DB;
                    break;
                case AC3_OUTPUT_STEREO:
                    ab->chcoeffs[1] *= clev;
                    break;
            }
            break;
        case AC3_INPUT_2F_1R:
            switch (to) {
                case AC3_OUTPUT_MONO:
                    ab->chcoeffs[0] *= LEVEL_MINUS_3DB;
                    ab->chcoeffs[1] *= LEVEL_MINUS_3DB;
                    ab->chcoeffs[2] *= slev * LEVEL_MINUS_3DB;
                    break;
                case AC3_OUTPUT_STEREO:
                    ab->chcoeffs[2] *= slev * LEVEL_MINUS_3DB;
                    break;
                case AC3_OUTPUT_DOLBY:
                    ab->chcoeffs[2] *= LEVEL_MINUS_3DB;
                    break;
            }
            break;
        case AC3_INPUT_3F_1R:
            switch (to) {
                case AC3_OUTPUT_MONO:
                    ab->chcoeffs[0] *= LEVEL_MINUS_3DB;
                    ab->chcoeffs[2] *= LEVEL_MINUS_3DB;
                    ab->chcoeffs[1] *= clev * LEVEL_PLUS_3DB;
                    ab->chcoeffs[3] *= slev * LEVEL_MINUS_3DB;
                    break;
                case AC3_OUTPUT_STEREO:
                    ab->chcoeffs[1] *= clev;
                    ab->chcoeffs[3] *= slev * LEVEL_MINUS_3DB;
                    break;
                case AC3_OUTPUT_DOLBY:
                    ab->chcoeffs[1] *= LEVEL_MINUS_3DB;
                    ab->chcoeffs[3] *= LEVEL_MINUS_3DB;
                    break;
            }
            break;
        case AC3_INPUT_2F_2R:
            switch (to) {
                case AC3_OUTPUT_MONO:
                    ab->chcoeffs[0] *= LEVEL_MINUS_3DB;
                    ab->chcoeffs[1] *= LEVEL_MINUS_3DB;
                    ab->chcoeffs[2] *= slev * LEVEL_MINUS_3DB;
                    ab->chcoeffs[3] *= slev * LEVEL_MINUS_3DB;
                    break;
                case AC3_OUTPUT_STEREO:
                    ab->chcoeffs[2] *= slev;
                    ab->chcoeffs[3] *= slev;
                    break;
                case AC3_OUTPUT_DOLBY:
                    ab->chcoeffs[2] *= LEVEL_MINUS_3DB;
                    ab->chcoeffs[3] *= LEVEL_MINUS_3DB;
                    break;
            }
            break;
        case AC3_INPUT_3F_2R:
            switch (to) {
                case AC3_OUTPUT_MONO:
                    ab->chcoeffs[0] *= LEVEL_MINUS_3DB;
                    ab->chcoeffs[2] *= LEVEL_MINUS_3DB;
                    ab->chcoeffs[1] *= clev * LEVEL_PLUS_3DB;
                    ab->chcoeffs[3] *= slev * LEVEL_MINUS_3DB;
                    ab->chcoeffs[4] *= slev * LEVEL_MINUS_3DB;
                    break;
                case AC3_OUTPUT_STEREO:
                    ab->chcoeffs[1] *= clev;
                    ab->chcoeffs[3] *= slev;
                    ab->chcoeffs[4] *= slev;
                    break;
                case AC3_OUTPUT_DOLBY:
                    ab->chcoeffs[1] *= LEVEL_MINUS_3DB;
                    ab->chcoeffs[3] *= LEVEL_MINUS_3DB;
                    ab->chcoeffs[4] *= LEVEL_MINUS_3DB;
                    break;
            }
            break;
    }
}

static inline void mix_dualmono_to_mono(AC3DecodeContext *ctx)
{
    int i;
    float (*output)[256] = ctx->audio_block.block_output;

    for (i = 0; i < 256; i++)
        output[1][i] += output[2][i];
    memset(output[2], 0, sizeof(output[2]));
}

static inline void mix_dualmono_to_stereo(AC3DecodeContext *ctx)
{
    int i;
    float tmp;
    float (*output)[256] = ctx->audio_block.block_output;

    for (i = 0; i < 256; i++) {
        tmp = output[1][i] + output[2][i];
        output[1][i] = output[2][i] = tmp;
    }
}

static inline void upmix_mono_to_stereo(AC3DecodeContext *ctx)
{
    int i;
    float (*output)[256] = ctx->audio_block.block_output;

    for (i = 0; i < 256; i++)
        output[2][i] = output[1][i];
}

static inline void mix_stereo_to_mono(AC3DecodeContext *ctx)
{
    int i;
    float (*output)[256] = ctx->audio_block.block_output;

    for (i = 0; i < 256; i++)
        output[1][i] += output[2][i];
    memset(output[2], 0, sizeof(output[2]));
}

static inline void mix_3f_to_mono(AC3DecodeContext *ctx)
{
    int i;
    float (*output)[256] = ctx->audio_block.block_output;

    for (i = 0; i < 256; i++)
        output[1][i] += (output[2][i] + output[3][i]);
    memset(output[2], 0, sizeof(output[2]));
    memset(output[3], 0, sizeof(output[3]));
}

static inline void mix_3f_to_stereo(AC3DecodeContext *ctx)
{
    int i;
    float (*output)[256] = ctx->audio_block.block_output;

    for (i = 0; i < 256; i++) {
        output[1][i] += output[2][i];
        output[2][i] += output[3][i];
    }
    memset(output[3], 0, sizeof(output[3]));
}

static inline void mix_2f_1r_to_mono(AC3DecodeContext *ctx)
{
    int i;
    float (*output)[256] = ctx->audio_block.block_output;

    for (i = 0; i < 256; i++)
        output[1][i] += (output[2][i] + output[3][i]);
    memset(output[2], 0, sizeof(output[2]));
    memset(output[3], 0, sizeof(output[3]));

}

static inline void mix_2f_1r_to_stereo(AC3DecodeContext *ctx)
{
    int i;
    float (*output)[256] = ctx->audio_block.block_output;

    for (i = 0; i < 256; i++) {
        output[1][i] += output[2][i];
        output[2][i] += output[3][i];
    }
    memset(output[3], 0, sizeof(output[3]));
}

static inline void mix_2f_1r_to_dolby(AC3DecodeContext *ctx)
{
    int i;
    float (*output)[256] = ctx->audio_block.block_output;

    for (i = 0; i < 256; i++) {
        output[1][i] -= output[3][i];
        output[2][i] += output[3][i];
    }
    memset(output[3], 0, sizeof(output[3]));
}

static inline void mix_3f_1r_to_mono(AC3DecodeContext *ctx)
{
    int i;
    float (*output)[256] = ctx->audio_block.block_output;

    for (i = 0; i < 256; i++)
        output[1][i] = (output[2][i] + output[3][i] + output[4][i]);
    memset(output[2], 0, sizeof(output[2]));
    memset(output[3], 0, sizeof(output[3]));
    memset(output[4], 0, sizeof(output[4]));
}

static inline void mix_3f_1r_to_stereo(AC3DecodeContext *ctx)
{
    int i;
    float (*output)[256] = ctx->audio_block.block_output;

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
    float (*output)[256] = ctx->audio_block.block_output;

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
    float (*output)[256] = ctx->audio_block.block_output;

    for (i = 0; i < 256; i++)
        output[1][i] = (output[2][i] + output[3][i] + output[4][i]);
    memset(output[2], 0, sizeof(output[2]));
    memset(output[3], 0, sizeof(output[3]));
    memset(output[4], 0, sizeof(output[4]));
}

static inline void mix_2f_2r_to_stereo(AC3DecodeContext *ctx)
{
    int i;
    float (*output)[256] = ctx->audio_block.block_output;

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
    float (*output)[256] = ctx->audio_block.block_output;

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
    float (*output)[256] = ctx->audio_block.block_output;

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
    float (*output)[256] = ctx->audio_block.block_output;

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
    float (*output)[256] = ctx->audio_block.block_output;

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
    int from = ctx->bsi.acmod;
    int to = ctx->output;

    switch (from) {
        case AC3_INPUT_DUALMONO:
            switch (to) {
                case AC3_OUTPUT_MONO:
                    mix_dualmono_to_mono(ctx);
                    break;
                case AC3_OUTPUT_STEREO: /* We Assume that sum of both mono channels is requested */
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

static int ac3_parse_audio_block(AC3DecodeContext * ctx, int index)
{
    ac3_audio_block *ab = &ctx->audio_block;
    int nfchans = ctx->bsi.nfchans;
    int acmod = ctx->bsi.acmod;
    int i, bnd, rbnd, seg, grpsize;
    GetBitContext *gb = &ctx->gb;
    uint32_t *flags = &ab->flags;
    int bit_alloc_flags = 0;
    float drange;
    uint8_t *dexps;

    *flags = 0;
    ab->blksw = 0;
    for (i = 0; i < 5; i++)
        ab->chcoeffs[i] = 1.0;
    for (i = 0; i < nfchans; i++) /*block switch flag */
        ab->blksw |= get_bits1(gb) << i;
    ab->dithflag = 0;
    for (i = 0; i < nfchans; i++) /* dithering flag */
        ab->dithflag |= get_bits1(gb) << i;
    if (get_bits1(gb)) { /* dynamic range */
        *flags |= AC3_AB_DYNRNGE;
        ab->dynrng = get_bits(gb, 8);
        drange = ((((ab->dynrng & 0x1f) | 0x20) << 13) * scale_factors[3 - (ab->dynrng >> 5)]);
        for (i = 0; i < nfchans; i++)
            ab->chcoeffs[i] *= drange;
    }
    if (acmod == 0x00) { /* dynamic range 1+1 mode */
        if (get_bits1(gb)) {
            *flags |= AC3_AB_DYNRNG2E;
            ab->dynrng2 = get_bits(gb, 8);
            drange = ((((ab->dynrng2 & 0x1f) | 0x20) << 13) * scale_factors[3 - (ab->dynrng2 >> 5)]);
            ab->chcoeffs[1] *= drange;
        }
    }
    get_downmix_coeffs(ctx);
    ab->chincpl = 0;
    if (get_bits1(gb)) { /* coupling strategy */
        *flags |= AC3_AB_CPLSTRE;
        ab->cplbndstrc = 0;
        if (get_bits1(gb)) { /* coupling in use */
            *flags |= AC3_AB_CPLINU;
            for (i = 0; i < nfchans; i++)
                ab->chincpl |= get_bits1(gb) << i;
            if (acmod == 0x00 || acmod == 0x01)
                return -1; /* coupling needs atleast two shared channels */
            if (acmod == 0x02)
                if (get_bits1(gb)) /* phase flag in use */
                    *flags |= AC3_AB_PHSFLGINU;
            ab->cplbegf = get_bits(gb, 4);
            ab->cplendf = get_bits(gb, 4);
            if (3 + ab->cplendf - ab->cplbegf < 0)
                return -1;
            ab->ncplbnd = ab->ncplsubnd = 3 + ab->cplendf - ab->cplbegf;
            ab->cplstrtmant = ab->cplbegf * 12 + 37;
            ab->cplendmant = ((ab->cplendf + 3) * 12) + 37;
            for (i = 0; i < ab->ncplsubnd - 1; i++) /* coupling band structure */
                if (get_bits1(gb)) {
                    ab->cplbndstrc |= 1 << i;
                    ab->ncplbnd--;
                }
        }
    }
    if (*flags & AC3_AB_CPLINU) {
        ab->cplcoe = 0;
        for (i = 0; i < nfchans; i++)
            if (ab->chincpl & (1 << i))
                if (get_bits1(gb)) { /* coupling co-ordinates */
                    ab->cplcoe |= 1 << i;
                    ab->mstrcplco[i] = get_bits(gb, 2);
                    for (bnd = 0; bnd < ab->ncplbnd; bnd++) {
                        ab->cplcoexp[i][bnd] = get_bits(gb, 4);
                        ab->cplcomant[i][bnd] = get_bits(gb, 4);
                    }
                }
        ab->phsflg = 0;
        if ((acmod == 0x02) && (*flags & AC3_AB_PHSFLGINU) && (ab->cplcoe & 1 || ab->cplcoe & (1 << 1))) {
            for (bnd = 0; bnd < ab->ncplbnd; bnd++)
                if (get_bits1(gb))
                    ab->phsflg |= 1 << bnd;
        }
    }
    generate_coupling_coordinates(ctx);
    ab->rematflg = 0;
    if (acmod == 0x02) /* rematrixing */
        if (get_bits1(gb)) {
            *flags |= AC3_AB_REMATSTR;
            if (!(*flags & AC3_AB_CPLINU) || ab->cplbegf > 2)
                for (rbnd = 0; rbnd < 4; rbnd++)
                    ab->rematflg |= get_bits1(gb) << rbnd;
            if (ab->cplbegf > 0 && ab->cplbegf <= 2 && (*flags & AC3_AB_CPLINU))
                for (rbnd = 0; rbnd < 3; rbnd++)
                    ab->rematflg |= get_bits1(gb) << rbnd;
            if (ab->cplbegf == 0 && (*flags & AC3_AB_CPLINU))
                for (rbnd = 0; rbnd < 2; rbnd++)
                    ab->rematflg |= get_bits1(gb) << rbnd;
        }
    ab->cplexpstr = AC3_EXPSTR_REUSE;
    ab->lfeexpstr = AC3_EXPSTR_REUSE;
    if (*flags & AC3_AB_CPLINU) /* coupling exponent strategy */
        ab->cplexpstr = get_bits(gb, 2);
    for (i = 0; i < nfchans; i++) /* channel exponent strategy */
        ab->chexpstr[i] = get_bits(gb, 2);
    if (ctx->bsi.flags & AC3_BSI_LFEON) /* lfe exponent strategy */
        ab->lfeexpstr = get_bits1(gb);
    for (i = 0; i < nfchans; i++) /* channel bandwidth code */
        if (ab->chexpstr[i] != AC3_EXPSTR_REUSE) {
            if ((ab->chincpl & (1 << i)))
                ab->endmant[i] = ab->cplstrtmant;
            else {
                ab->chbwcod[i] = get_bits(gb, 6);
                if (ab->chbwcod[i] > 60) {
                    av_log(NULL, AV_LOG_ERROR, "chbwcod = %d > 60", ab->chbwcod[i]);
                    return -1;
                }
                ab->endmant[i] = ((ab->chbwcod[i] + 12) * 3) + 37;
            }
        }
    if (*flags & AC3_AB_CPLINU)
        if (ab->cplexpstr != AC3_EXPSTR_REUSE) {/* coupling exponents */
            bit_alloc_flags |= 64;
            ab->cplabsexp = get_bits(gb, 4) << 1;
            ab->ncplgrps = (ab->cplendmant - ab->cplstrtmant) / (3 << (ab->cplexpstr - 1));
            if (decode_exponents(gb, ab->cplexpstr, ab->ncplgrps, ab->cplabsexp, ab->dcplexps + ab->cplstrtmant)) {
                    av_log(NULL, AV_LOG_ERROR, "error decoding coupling exponents\n");
                    return -1;
            }
        }
    for (i = 0; i < nfchans; i++) /* fbw channel exponents */
        if (ab->chexpstr[i] != AC3_EXPSTR_REUSE) {
            bit_alloc_flags |= 1 << i;
            grpsize = 3 << (ab->chexpstr[i] - 1);
            ab->nchgrps[i] = (ab->endmant[i] + grpsize - 4) / grpsize;
            dexps = ab->dexps[i];
            dexps[0] = get_bits(gb, 4);
            if (decode_exponents(gb, ab->chexpstr[i], ab->nchgrps[i], dexps[0], dexps + 1)) {
                av_log(NULL, AV_LOG_ERROR, "error decoding channel %d exponents\n", i);
                return -1;
            }
            ab->gainrng[i] = get_bits(gb, 2);
        }
    if (ctx->bsi.flags & AC3_BSI_LFEON) /* lfe exponents */
        if (ab->lfeexpstr != AC3_EXPSTR_REUSE) {
            bit_alloc_flags |= 32;
            ab->dlfeexps[0] = get_bits(gb, 4);
            if (decode_exponents(gb, ab->lfeexpstr, 2, ab->dlfeexps[0], ab->dlfeexps + 1)) {
                av_log(NULL, AV_LOG_ERROR, "error decoding lfe exponents\n");
                return -1;
            }
        }

    if (get_bits1(gb)) { /* bit allocation information */
        *flags |= AC3_AB_BAIE;
        bit_alloc_flags |= 127;
        ab->sdcycod = get_bits(gb, 2);
        ab->fdcycod = get_bits(gb, 2);
        ab->sgaincod = get_bits(gb, 2);
        ab->dbpbcod = get_bits(gb, 2);
        ab->floorcod = get_bits(gb, 3);
    }
    if (get_bits1(gb)) { /* snroffset */
        *flags |= AC3_AB_SNROFFSTE;
        bit_alloc_flags |= 127;
        ab->csnroffst = get_bits(gb, 6);
        if (*flags & AC3_AB_CPLINU) { /* couling fine snr offset and fast gain code */
            ab->cplfsnroffst = get_bits(gb, 4);
            ab->cplfgaincod = get_bits(gb, 3);
        }
        for (i = 0; i < nfchans; i++) { /* channel fine snr offset and fast gain code */
            ab->fsnroffst[i] = get_bits(gb, 4);
            ab->fgaincod[i] = get_bits(gb, 3);
        }
        if (ctx->bsi.flags & AC3_BSI_LFEON) { /* lfe fine snr offset and fast gain code */
            ab->lfefsnroffst = get_bits(gb, 4);
            ab->lfefgaincod = get_bits(gb, 3);
        }
    }
    if (*flags & AC3_AB_CPLINU)
        if (get_bits1(gb)) { /* coupling leak information */
            bit_alloc_flags |= 64;
            *flags |= AC3_AB_CPLLEAKE;
            ab->cplfleak = get_bits(gb, 3);
            ab->cplsleak = get_bits(gb, 3);
        }
    if (get_bits1(gb)) { /* delta bit allocation information */
        *flags |= AC3_AB_DELTBAIE;
        bit_alloc_flags |= 127;
        if (*flags & AC3_AB_CPLINU) {
            ab->cpldeltbae = get_bits(gb, 2);
            if (ab->cpldeltbae == AC3_DBASTR_RESERVED) {
                av_log(NULL, AV_LOG_ERROR, "coupling delta bit allocation strategy reserved\n");
                return -1;
            }
        }
        for (i = 0; i < nfchans; i++) {
            ab->deltbae[i] = get_bits(gb, 2);
            if (ab->deltbae[i] == AC3_DBASTR_RESERVED) {
                av_log(NULL, AV_LOG_ERROR, "delta bit allocation strategy reserved\n");
                return -1;
            }
        }
        if (*flags & AC3_AB_CPLINU)
            if (ab->cpldeltbae == AC3_DBASTR_NEW) { /*coupling delta offset, len and bit allocation */
                ab->cpldeltnseg = get_bits(gb, 3);
                for (seg = 0; seg <= ab->cpldeltnseg; seg++) {
                    ab->cpldeltoffst[seg] = get_bits(gb, 5);
                    ab->cpldeltlen[seg] = get_bits(gb, 4);
                    ab->cpldeltba[seg] = get_bits(gb, 3);
                }
            }
        for (i = 0; i < nfchans; i++)
            if (ab->deltbae[i] == AC3_DBASTR_NEW) {/*channel delta offset, len and bit allocation */
                ab->deltnseg[i] = get_bits(gb, 3);
                for (seg = 0; seg <= ab->deltnseg[i]; seg++) {
                    ab->deltoffst[i][seg] = get_bits(gb, 5);
                    ab->deltlen[i][seg] = get_bits(gb, 4);
                    ab->deltba[i][seg] = get_bits(gb, 3);
                }
            }
    }

    do_bit_allocation (ctx, bit_alloc_flags); /* perform the bit allocation */

    if (get_bits1(gb)) { /* unused dummy data */
        *flags |= AC3_AB_SKIPLE;
        ab->skipl = get_bits(gb, 9);
        for (i = 0; i < ab->skipl; i++)
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
    if (*flags & AC3_AB_REMATSTR)
        do_rematrixing(ctx);

    return 0;
}

static int ac3_decode_frame(AVCodecContext * avctx, void *data, int *data_size, uint8_t *buf, int buf_size)
{
    AC3DecodeContext *ctx = (AC3DecodeContext *)avctx->priv_data;
    ac3_audio_block *ab = &ctx->audio_block;
    int frame_start;
    int i, j, k, l, value;
    float tmp_block_first_half[128], tmp_block_second_half[128];
    int16_t *out_samples = (int16_t *)data;
    int nfchans;

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
        return -1;
    }

    //Check for the errors.
    /* if (ac3_error_check(ctx)) {
        *data_size = 0;
        return -1;
    } */

    //Parse the BSI.
    //If 'bsid' is not valid decoder shall not decode the audio as per the standard.
    if (ac3_parse_bsi(ctx)) {
        av_log(avctx, AV_LOG_ERROR, "bsid is not valid\n");
        *data_size = 0;
        return -1;
    }

    for (i = 0; i < MAX_BLOCKS; i++)
        memset(ctx->delay[i], 0, sizeof(ctx->delay[i]));
    avctx->sample_rate = ctx->sync_info.sampling_rate;
    avctx->bit_rate = ctx->sync_info.bit_rate;
    if (avctx->channels == 0) {
        //avctx->channels = ctx->bsi.nfchans + ((ctx->bsi.flags & AC3_BSI_LFEON) ? 1 : 0);
        ctx->output = AC3_OUTPUT_UNMODIFIED;
    }
    else if ((ctx->bsi.nfchans + ((ctx->bsi.flags & AC3_BSI_LFEON) ? 1 : 0)) < avctx->channels) {
        av_log(avctx, AV_LOG_INFO, "ac3_decoder: AC3 Source Channels Are Less Then Specified %d: Output to %d Channels\n",
                avctx->channels, (ctx->bsi.nfchans + ((ctx->bsi.flags & AC3_BSI_LFEON) ? 1 : 0)));
        //avctx->channels = ctx->bsi.nfchans + ((ctx->bsi.flags & AC3_BSI_LFEON) ? 1 : 0);
        ctx->output = AC3_OUTPUT_UNMODIFIED;
    }
    else if (avctx->channels == 1) {
        ctx->output = AC3_OUTPUT_MONO;
    } else if (avctx->channels == 2) {
        if (ctx->bsi.dsurmod == 0x02)
            ctx->output = AC3_OUTPUT_DOLBY;
        else
            ctx->output = AC3_OUTPUT_STEREO;
    }


    av_log(avctx, AV_LOG_INFO, "channels = %d \t bit rate = %d \t sampling rate = %d \n", avctx->channels, avctx->sample_rate, avctx->bit_rate);

    //Parse the Audio Blocks.
    *data_size = 0;
    for (i = 0; i < 6; i++) {
        if (ac3_parse_audio_block(ctx, i)) {
            av_log(avctx, AV_LOG_ERROR, "error parsing the audio block\n");
            *data_size = 0;
            return -1;
        }

        av_log(NULL, AV_LOG_INFO, "doing imdct\n");

        if (ctx->bsi.flags & AC3_BSI_LFEON) {
            ff_imdct_calc(&ctx->imdct_ctx_512, ctx->tmp_output, ab->transform_coeffs[0], ctx->tmp_imdct);
            for (l = 0; l < 256; l++)
                ab->block_output[0][l] = ctx->tmp_output[l] * window[l] + ctx->delay[0][l] * window[255 -l];
            memcpy(ctx->delay[0], ctx->tmp_output + 256, sizeof(ctx->delay[0]));
        }

        for (j = 0; j < ctx->bsi.nfchans; j++) {
            if (ctx->audio_block.blksw & (1 << j)) {
                for (k = 0; k < 128; k++) {
                    tmp_block_first_half[k] = ab->transform_coeffs[j + 1][2 * k];
                    tmp_block_second_half[k] = ab->transform_coeffs[j + 1][2 * k + 1];
                }
                ff_imdct_calc(&ctx->imdct_ctx_256, ctx->tmp_output, tmp_block_first_half, ctx->tmp_imdct);
                for (l = 0; l < 256; l++)
                    ab->block_output[j + 1][l] = ctx->tmp_output[l] * window[l] + ctx->delay[j + 1][l] * window[255 - l];
                ff_imdct_calc(&ctx->imdct_ctx_256, ctx->delay[j + 1], tmp_block_second_half, ctx->tmp_imdct);
            } else {
                ff_imdct_calc(&ctx->imdct_ctx_512, ctx->tmp_output, ab->transform_coeffs[j + 1], ctx->tmp_imdct);
                for (l = 0; l < 256; l++)
                    ab->block_output[j + 1][l] = ctx->tmp_output[l] * window[l] + ctx->delay[j + 1][l] * window[255 - l];
                memcpy(ctx->delay[j + 1], ctx->tmp_output + 256, sizeof(ctx->delay[j + 1]));
            }
        }
        if (ctx->bsi.flags & AC3_BSI_LFEON) {
            for (l = 0; l < 256; l++) {
                value = lrint(ab->block_output[0][l]);
                if (value < -32768)
                    value = -32768;
                else if (value > 32767)
                    value = 32767;
                *(out_samples++) = value;
            }
            *data_size += 256 * sizeof(int16_t);
        }
        do_downmix(ctx);
        if (ctx->output == AC3_OUTPUT_UNMODIFIED)
            nfchans = ctx->bsi.nfchans;
        else
            nfchans = avctx->channels;
        for (k = 0; k < nfchans; k++)
            for (l = 0; l < 256; l++) {
                value = lrint(ab->block_output[k + 1][l]);
                if (value < -32768)
                    value = -32768;
                else if (value > 32767)
                    value = 32767;
                *(out_samples++) = value;
            }
        *data_size += nfchans * 256 * sizeof (int16_t);
    }

    return ctx->sync_info.framesize;
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

