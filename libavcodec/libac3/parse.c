/* 
 *    parse.c
 *
 *	Copyright (C) Aaron Holtzman - May 1999
 *
 *  This file is part of ac3dec, a free Dolby AC-3 stream decoder.
 *	
 *  ac3dec is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *   
 *  ac3dec is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *   
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 *
 */

#include <inttypes.h>
#include <string.h>

#include "ac3.h"
#include "ac3_internal.h"

#include "bitstream.h"
#include "tables.h"

extern stream_samples_t samples;	// FIXME
static float delay[6][256];

void ac3_init (void)
{
    imdct_init ();
}

static uint8_t halfrate[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3};

int ac3_syncinfo (uint8_t * buf, int * flags,
		  int * sample_rate, int * bit_rate)
{
    static int rate[] = { 32,  40,  48,  56,  64,  80,  96, 112,
			 128, 160, 192, 224, 256, 320, 384, 448,
			 512, 576, 640};
    static uint8_t lfeon[8] = {0x10, 0x10, 0x04, 0x04, 0x04, 0x01, 0x04, 0x01};
    int frmsizecod;
    int bitrate;
    int half;
    int acmod;

    if ((buf[0] != 0x0b) || (buf[1] != 0x77))	// syncword
	return 0;

    if (buf[5] >= 0x60)		// bsid >= 12
	return 0;
    half = halfrate[buf[5] >> 3];

    // acmod, dsurmod and lfeon
    acmod = buf[6] >> 5;
    *flags = (((buf[6] & 0xf8) == 0x50) ? AC3_DOLBY : acmod) |
	      ((buf[6] & lfeon[acmod]) ? AC3_LFE : 0);

    frmsizecod = buf[4] & 63;
    if (frmsizecod >= 38)
	return 0;
    bitrate = rate [frmsizecod >> 1];
    *bit_rate = (bitrate * 1000) >> half;

    switch (buf[4] & 0xc0) {
    case 0:	// 48 KHz
	*sample_rate = 48000 >> half;
	return 4 * bitrate;
    case 0x40:
	*sample_rate = 44100 >> half;
	return 2 * (320 * bitrate / 147 + (frmsizecod & 1));
    case 0x80:
	*sample_rate = 32000 >> half;
	return 6 * bitrate;
    default:
	return 0;
    }
}

int ac3_frame (ac3_state_t * state, uint8_t * buf, int * flags, float * level,
	       float bias)
{
    static float clev[4] = {LEVEL_3DB, LEVEL_45DB, LEVEL_6DB, LEVEL_45DB};
    static float slev[4] = {LEVEL_3DB, LEVEL_6DB, 0, LEVEL_6DB};
    int chaninfo;
    int acmod;

    state->fscod = buf[4] >> 6;
    state->halfrate = halfrate[buf[5] >> 3];
    state->acmod = acmod = buf[6] >> 5;

    bitstream_set_ptr (buf + 6);
    bitstream_get (3);	// skip acmod we already parsed

    if ((acmod == 2) && (bitstream_get (2) == 2))	// dsurmod
	acmod = AC3_DOLBY;

    if ((acmod & 1) && (acmod != 1))
	state->clev = clev[bitstream_get (2)];	// cmixlev

    if (acmod & 4)
	state->slev = slev[bitstream_get (2)];	// surmixlev

    state->lfeon = bitstream_get (1);

    state->output = downmix_init (acmod, *flags, level,
				  state->clev, state->slev);
    if (state->output < 0)
	return 1;
    *flags = state->output;
    state->level = *level;
    state->bias = bias;

    chaninfo = !acmod;
    do {
	bitstream_get (5);	// dialnorm
	if (bitstream_get (1))	// compre
	    bitstream_get (8);	// compr
	if (bitstream_get (1))	// langcode
	    bitstream_get (8);	// langcod
	if (bitstream_get (1))	// audprodie
	    bitstream_get (7);	// mixlevel + roomtyp
    } while (chaninfo--);

    bitstream_get (2);		// copyrightb + origbs

    if (bitstream_get (1))	// timecod1e
	bitstream_get (14);	// timecod1
    if (bitstream_get (1))	// timecod2e
	bitstream_get (14);	// timecod2

    if (bitstream_get (1)) {	// addbsie
	int addbsil;

	addbsil = bitstream_get (6);
	do {
	    bitstream_get (8);	// addbsi
	} while (addbsil--);
    }

    return 0;
}

static int parse_exponents (int expstr, int ngrps, uint8_t exponent,
			    uint8_t * dest)
{
    int exps;

    while (ngrps--) {
	exps = bitstream_get (7);

	exponent += exp_1[exps];
	if (exponent > 24)
	    return 1;

	switch (expstr) {
	case EXP_D45:
	    *(dest++) = exponent;
	    *(dest++) = exponent;
	case EXP_D25:
	    *(dest++) = exponent;
	case EXP_D15:
	    *(dest++) = exponent;
	}

	exponent += exp_2[exps];
	if (exponent > 24)
	    return 1;

	switch (expstr) {
	case EXP_D45:
	    *(dest++) = exponent;
	    *(dest++) = exponent;
	case EXP_D25:
	    *(dest++) = exponent;
	case EXP_D15:
	    *(dest++) = exponent;
	}

	exponent += exp_3[exps];
	if (exponent > 24)
	    return 1;

	switch (expstr) {
	case EXP_D45:
	    *(dest++) = exponent;
	    *(dest++) = exponent;
	case EXP_D25:
	    *(dest++) = exponent;
	case EXP_D15:
	    *(dest++) = exponent;
	}
    }	

    return 0;
}

static int parse_deltba (int8_t * deltba)
{
    int deltnseg, deltlen, delta, j;

    memset (deltba, 0, 50);

    deltnseg = bitstream_get (3);
    j = 0;
    do {
	j += bitstream_get (5);
	deltlen = bitstream_get (4);
	delta = bitstream_get (3);
	delta -= (delta >= 4) ? 3 : 4;
	if (!deltlen)
	    continue;
	if (j + deltlen >= 50)
	    return 1;
	while (deltlen--)
	    deltba[j++] = delta;
    } while (deltnseg--);

    return 0;
}

static inline int zero_snr_offsets (int nfchans, ac3_state_t * state)
{
    int i;

    if ((state->csnroffst) || (state->cplinu && state->cplba.fsnroffst) ||
	(state->lfeon && state->lfeba.fsnroffst))
	return 0;
    for (i = 0; i < nfchans; i++)
	if (state->ba[i].fsnroffst)
	    return 0;
    return 1;
}

static float q_1[2];
static float q_2[2];
static float q_4;
static int q_1_pointer;
static int q_2_pointer;
static int q_4_pointer;

#define GET_COEFF(COEFF,DITHER)						\
    switch (bap[i]) {							\
    case 0:								\
	DITHER (scale_factor[exp[i]]);					\
									\
    case -1:								\
	if (q_1_pointer >= 0) {						\
	    COEFF (q_1[q_1_pointer--] * scale_factor[exp[i]]);		\
	} else {							\
	    int code;							\
									\
	    code = bitstream_get (5);					\
									\
	    q_1_pointer = 1;						\
	    q_1[0] = q_1_2[code];					\
	    q_1[1] = q_1_1[code];					\
	    COEFF (q_1_0[code] * scale_factor[exp[i]]);			\
	}								\
									\
    case -2:								\
	if (q_2_pointer >= 0) {						\
	    COEFF (q_2[q_2_pointer--] * scale_factor[exp[i]]);		\
	} else {							\
	    int code;							\
									\
	    code = bitstream_get (7);					\
									\
	    q_2_pointer = 1;						\
	    q_2[0] = q_2_2[code];					\
	    q_2[1] = q_2_1[code];					\
	    COEFF (q_2_0[code] * scale_factor[exp[i]]);			\
	}								\
									\
    case 3:								\
	COEFF (q_3[bitstream_get (3)] * scale_factor[exp[i]]);		\
									\
    case -3:								\
	if (q_4_pointer == 0) {						\
	    q_4_pointer = -1;						\
	    COEFF (q_4 * scale_factor[exp[i]]);				\
	} else {							\
	    int code;							\
									\
	    code = bitstream_get (7);					\
									\
	    q_4_pointer = 0;						\
	    q_4 = q_4_1[code];						\
	    COEFF (q_4_0[code] * scale_factor[exp[i]]);			\
	}								\
									\
    case 4:								\
	COEFF (q_5[bitstream_get (4)] * scale_factor[exp[i]]);		\
									\
    default:								\
	COEFF (((int16_t)(bitstream_get(bap[i]) << (16 - bap[i]))) *	\
	       scale_factor[exp[i]]);					\
    }

#define CHANNEL_COEFF(val)			\
    coeff[i++] = val;				\
    continue;

#define CHANNEL_DITHER(val)			\
    if (dither) {				\
	coeff[i++] = dither_gen () * val;	\
	continue;				\
    } else {					\
	coeff[i++] = 0;				\
	continue;				\
    }

static uint16_t lfsr_state = 1;

static inline int16_t dither_gen(void)
{
    int16_t state;

    state = dither_lut[lfsr_state >> 8] ^ (lfsr_state << 8);
	
    lfsr_state = (uint16_t) state;

    return ((state * (int) (LEVEL_3DB * 256)) >> 8);
}

static void coeff_get (float * coeff, uint8_t * exp, int8_t * bap,
		       int dither, int end)
{
    int i;

    i = 0;
    while (i < end)
	GET_COEFF (CHANNEL_COEFF, CHANNEL_DITHER);
}

#define COUPLING_COEFF(val)	\
    cplcoeff = val;		\
    break;

#define COUPLING_DITHER(val)						\
    cplcoeff = val;							\
    for (ch = 0; ch < nfchans; ch++)					\
	if (state->chincpl[ch]) {					\
	    if (dithflag[ch])						\
		samples[ch][i] =					\
		    state->cplco[ch][bnd] * dither_gen () * cplcoeff;	\
	    else							\
		samples[ch][i] = 0;					\
	}								\
    i++;								\
    continue;

int ac3_block (ac3_state_t * state)
{
    static const uint8_t nfchans_tbl[8] = {2, 1, 2, 3, 3, 4, 4, 5};
    static int rematrix_band[4] = {25, 37, 61, 253};
    int i, nfchans, chaninfo;
    uint8_t cplexpstr, chexpstr[5], lfeexpstr, do_bit_alloc, done_cpl;
    uint8_t blksw[5], dithflag[5];

    nfchans = nfchans_tbl[state->acmod];

    for (i = 0; i < nfchans; i++)
	blksw[i] = bitstream_get (1);

    for (i = 0; i < nfchans; i++)
	dithflag[i] = bitstream_get (1);

    chaninfo = !(state->acmod);
    do {
	if (bitstream_get (1))	// dynrnge
	    bitstream_get (8);	// dynrng
    } while (chaninfo--);

    if (bitstream_get (1)) {	// cplstre
	state->cplinu = bitstream_get (1);
	if (state->cplinu) {
	    static int bndtab[16] = {31, 35, 37, 39, 41, 42, 43, 44,
				     45, 45, 46, 46, 47, 47, 48, 48};
	    int cplbegf;
	    int cplendf;
	    int ncplsubnd;

	    for (i = 0; i < nfchans; i++)
		state->chincpl[i] = bitstream_get (1);
	    switch (state->acmod) {
	    case 0: case 1:
		return 1;
	    case 2:
		state->phsflginu = bitstream_get (1);
	    }
	    cplbegf = bitstream_get (4);
	    cplendf = bitstream_get (4);

	    if (cplendf + 3 - cplbegf < 0)
		return 1;
	    state->ncplbnd = ncplsubnd = cplendf + 3 - cplbegf;
	    state->cplstrtbnd = bndtab[cplbegf];
	    state->cplstrtmant = cplbegf * 12 + 37;
	    state->cplendmant = cplendf * 12 + 73;

	    for (i = 0; i < ncplsubnd - 1; i++) {
		state->cplbndstrc[i] = bitstream_get (1);
		state->ncplbnd -= state->cplbndstrc[i];
	    }
	    state->cplbndstrc[i] = 0;	// last value is a sentinel
	}
    }

    if (state->cplinu) {
	int j, cplcoe;

	cplcoe = 0;
	for (i = 0; i < nfchans; i++)
	    if (state->chincpl[i])
		if (bitstream_get (1)) {	// cplcoe
		    int mstrcplco, cplcoexp, cplcomant;

		    cplcoe = 1;
		    mstrcplco = 3 * bitstream_get (2);
		    for (j = 0; j < state->ncplbnd; j++) {
			cplcoexp = bitstream_get (4);
			cplcomant = bitstream_get (4);
			if (cplcoexp == 15)
			    cplcomant <<= 14;
			else
			    cplcomant = (cplcomant | 0x10) << 13;
			state->cplco[i][j] =
			    cplcomant * scale_factor[cplcoexp + mstrcplco];
		    }
		}
	if ((state->acmod == 2) && state->phsflginu && cplcoe)
	    for (j = 0; j < state->ncplbnd; j++)
		if (bitstream_get (1))	// phsflg
		    state->cplco[1][j] = -state->cplco[1][j];
    }

    if ((state->acmod == 2) && (bitstream_get (1))) {	// rematstr
	int end;

	end = (state->cplinu) ? state->cplstrtmant : 253;
	i = 0;
	do
	    state->rematflg[i] = bitstream_get (1);
	while (rematrix_band[i++] < end);
    }

    cplexpstr = EXP_REUSE;
    lfeexpstr = EXP_REUSE;
    if (state->cplinu)
	cplexpstr = bitstream_get (2);
    for (i = 0; i < nfchans; i++)
	chexpstr[i] = bitstream_get (2);
    if (state->lfeon) 
	lfeexpstr = bitstream_get (1);

    for (i = 0; i < nfchans; i++)
	if (chexpstr[i] != EXP_REUSE) {
	    if (state->cplinu && state->chincpl[i])
		state->endmant[i] = state->cplstrtmant;
	    else {
		int chbwcod;

		chbwcod = bitstream_get (6);
		if (chbwcod > 60)
		    return 1;
		state->endmant[i] = chbwcod * 3 + 73;
	    }
	}

    do_bit_alloc = 0;

    if (cplexpstr != EXP_REUSE) {
	int cplabsexp, ncplgrps;

	do_bit_alloc = 1;
	ncplgrps = ((state->cplendmant - state->cplstrtmant) /
		    (3 << (cplexpstr - 1)));
	cplabsexp = bitstream_get (4) << 1;
	if (parse_exponents (cplexpstr, ncplgrps, cplabsexp,
			     state->cpl_exp + state->cplstrtmant))
	    return 1;
    }
    for (i = 0; i < nfchans; i++)
	if (chexpstr[i] != EXP_REUSE) {
	    int grp_size, nchgrps;

	    do_bit_alloc = 1;
	    grp_size = 3 << (chexpstr[i] - 1);
	    nchgrps = (state->endmant[i] + grp_size - 4) / grp_size;
	    state->fbw_exp[i][0] = bitstream_get (4);
	    if (parse_exponents (chexpstr[i], nchgrps, state->fbw_exp[i][0],
				 state->fbw_exp[i] + 1))
		return 1;
	    bitstream_get (2);	// gainrng
	}
    if (lfeexpstr != EXP_REUSE) {
	do_bit_alloc = 1;
	state->lfe_exp[0] = bitstream_get (4);
	if (parse_exponents (lfeexpstr, 2, state->lfe_exp[0],
			     state->lfe_exp + 1))
	    return 1;
    }

    if (bitstream_get (1)) {	// baie
	do_bit_alloc = 1;
	state->sdcycod = bitstream_get (2);
	state->fdcycod = bitstream_get (2);
	state->sgaincod = bitstream_get (2);
	state->dbpbcod = bitstream_get (2);
	state->floorcod = bitstream_get (3);
    }
    if (bitstream_get (1)) {	//snroffste
	do_bit_alloc = 1;
	state->csnroffst = bitstream_get (6);
	if (state->cplinu) {
	    state->cplba.fsnroffst = bitstream_get (4);
	    state->cplba.fgaincod = bitstream_get (3);
	}
	for (i = 0; i < nfchans; i++) {
	    state->ba[i].fsnroffst = bitstream_get (4);
	    state->ba[i].fgaincod = bitstream_get (3);
	}
	if (state->lfeon) {
	    state->lfeba.fsnroffst = bitstream_get (4);
	    state->lfeba.fgaincod = bitstream_get (3);
	}
    }
    if ((state->cplinu) && (bitstream_get (1))) {	// cplleake
	do_bit_alloc = 1;
	state->cplfleak = 2304 - (bitstream_get (3) << 8);
	state->cplsleak = 2304 - (bitstream_get (3) << 8);
    }

    if (bitstream_get (1)) {	// deltbaie
	do_bit_alloc = 1;
	if (state->cplinu)
	    state->cplba.deltbae = bitstream_get (2);
	for (i = 0; i < nfchans; i++)
	    state->ba[i].deltbae = bitstream_get (2);
	if (state->cplinu && (state->cplba.deltbae == DELTA_BIT_NEW) &&
	    parse_deltba (state->cplba.deltba))
	    return 1;
	for (i = 0; i < nfchans; i++)
	    if ((state->ba[i].deltbae == DELTA_BIT_NEW) &&
		parse_deltba (state->ba[i].deltba))
		return 1;
    }

    if (do_bit_alloc) {
	if (zero_snr_offsets (nfchans, state)) {
	    memset (state->cpl_bap, 0, sizeof (state->cpl_bap));
	    memset (state->fbw_bap, 0, sizeof (state->fbw_bap));
	    memset (state->lfe_bap, 0, sizeof (state->lfe_bap));
	} else {
	    if (state->cplinu)
		bit_allocate (state, &state->cplba, state->cplstrtbnd,
			      state->cplstrtmant, state->cplendmant,
			      state->cplfleak, state->cplsleak,
			      state->cpl_exp, state->cpl_bap);
	    for (i = 0; i < nfchans; i++)
		bit_allocate (state, state->ba + i, 0, 0, state->endmant[i],
			      0, 0, state->fbw_exp[i], state->fbw_bap[i]);
	    if (state->lfeon) {
		state->lfeba.deltbae = DELTA_BIT_NONE;
		bit_allocate (state, &state->lfeba, 0, 0, 7, 0, 0,
			      state->lfe_exp, state->lfe_bap);
	    }
	}
    }

    if (bitstream_get (1)) {	// skiple
	i = bitstream_get (9);	// skipl
	while (i--)
	    bitstream_get (8);
    }

    q_1_pointer = q_2_pointer = q_4_pointer = -1;
    done_cpl = 0;

    for (i = 0; i < nfchans; i++) {
	int j;

	coeff_get (samples[i], state->fbw_exp[i], state->fbw_bap[i],
		   dithflag[i], state->endmant[i]);

	if (state->cplinu && state->chincpl[i]) {
	    if (!done_cpl) {
		int i, i_end, bnd, sub_bnd, ch;
		float cplcoeff;

		done_cpl = 1;

#define bap state->cpl_bap
#define exp state->cpl_exp

		sub_bnd = bnd = 0;
		i = state->cplstrtmant;
		while (i < state->cplendmant) {
		    i_end = i + 12;
		    while (state->cplbndstrc[sub_bnd++])
			i_end += 12;

		    while (i < i_end) {
			GET_COEFF (COUPLING_COEFF, COUPLING_DITHER);
			for (ch = 0; ch < nfchans; ch++)
			    if (state->chincpl[ch])
				samples[ch][i] =
				    state->cplco[ch][bnd] * cplcoeff;
			i++;
		    }
		    bnd++;
		}

#undef bap
#undef exp
	    }
	    j = state->cplendmant;
	} else
	    j = state->endmant[i];
	for (; j < 256; j++)
	    samples[i][j] = 0;
    }

    if (state->acmod == 2) {
	int j, end, band;

	end = ((state->endmant[0] < state->endmant[1]) ?
	       state->endmant[0] : state->endmant[1]);

	i = 0;
	j = 13;
	do {
	    if (!state->rematflg[i]) {
		j = rematrix_band[i++];
		continue;
	    }
	    band = rematrix_band[i++];
	    if (band > end)
		band = end;
	    do {
		float tmp0, tmp1;

		tmp0 = samples[0][j];
		tmp1 = samples[1][j];
		samples[0][j] = tmp0 + tmp1;
		samples[1][j] = tmp0 - tmp1;
	    } while (++j < band);
	} while (j < end);
    }

    if (state->lfeon) {
	coeff_get (samples[5], state->lfe_exp, state->lfe_bap, 0, 7);
#if 0
	for (i = 7; i < 256; i++)
	    samples[5][i] = 0;
#endif
    }

    for (i = 0; i < nfchans; i++)
	if (blksw[i])
            imdct_256 (samples[i], delay[i]);
        else 
            imdct_512 (samples[i], delay[i]);

#if 0
    if (state->lfeon)
	imdct_512 (samples[5], delay[5]);
#endif

    downmix (*samples, state->acmod, state->output, state->level, state->bias,
	     state->clev, state->slev);

    return 0;
}
