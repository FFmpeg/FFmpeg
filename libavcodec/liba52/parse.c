/*
 * parse.c
 * Copyright (C) 2000-2003 Michel Lespinasse <walken@zoy.org>
 * Copyright (C) 1999-2000 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * This file is part of a52dec, a free ATSC A-52 stream decoder.
 * See http://liba52.sourceforge.net/ for updates.
 *
 * a52dec is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * a52dec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "a52.h"
#include "a52_internal.h"
#include "bitstream.h"
#include "tables.h"

#if defined(HAVE_MEMALIGN) && !defined(__cplusplus)
/* some systems have memalign() but no declaration for it */
void * memalign (size_t align, size_t size);
#else
/* assume malloc alignment is sufficient */
#define memalign(align,size) malloc (size)
#endif

typedef struct {
    quantizer_t q1[2];
    quantizer_t q2[2];
    quantizer_t q4;
    int q1_ptr;
    int q2_ptr;
    int q4_ptr;
} quantizer_set_t;

static uint8_t halfrate[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3};

a52_state_t * a52_init (uint32_t mm_accel)
{
    a52_state_t * state;
    int i;

    state = (a52_state_t *) malloc (sizeof (a52_state_t));
    if (state == NULL)
	return NULL;

    state->samples = (sample_t *) memalign (16, 256 * 12 * sizeof (sample_t));
    if (state->samples == NULL) {
	free (state);
	return NULL;
    }

    for (i = 0; i < 256 * 12; i++)
	state->samples[i] = 0;

    state->downmixed = 1;

    state->lfsr_state = 1;

    a52_imdct_init (mm_accel);

    return state;
}

sample_t * a52_samples (a52_state_t * state)
{
    return state->samples;
}

int a52_syncinfo (uint8_t * buf, int * flags,
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

    if ((buf[0] != 0x0b) || (buf[1] != 0x77))	/* syncword */
	return 0;

    if (buf[5] >= 0x60)		/* bsid >= 12 */
	return 0;
    half = halfrate[buf[5] >> 3];

    /* acmod, dsurmod and lfeon */
    acmod = buf[6] >> 5;
    *flags = ((((buf[6] & 0xf8) == 0x50) ? A52_DOLBY : acmod) |
	      ((buf[6] & lfeon[acmod]) ? A52_LFE : 0));

    frmsizecod = buf[4] & 63;
    if (frmsizecod >= 38)
	return 0;
    bitrate = rate [frmsizecod >> 1];
    *bit_rate = (bitrate * 1000) >> half;

    switch (buf[4] & 0xc0) {
    case 0:
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

int a52_frame (a52_state_t * state, uint8_t * buf, int * flags,
	       level_t * level, sample_t bias)
{
    static level_t clev[4] = { LEVEL (LEVEL_3DB), LEVEL (LEVEL_45DB),
			       LEVEL (LEVEL_6DB), LEVEL (LEVEL_45DB) };
    static level_t slev[4] = { LEVEL (LEVEL_3DB), LEVEL (LEVEL_6DB), 
			       0,                 LEVEL (LEVEL_6DB) };
    int chaninfo;
    int acmod;

    state->fscod = buf[4] >> 6;
    state->halfrate = halfrate[buf[5] >> 3];
    state->acmod = acmod = buf[6] >> 5;

    a52_bitstream_set_ptr (state, buf + 6);
    bitstream_get (state, 3);	/* skip acmod we already parsed */

    if ((acmod == 2) && (bitstream_get (state, 2) == 2))	/* dsurmod */
	acmod = A52_DOLBY;

    state->clev = state->slev = 0;

    if ((acmod & 1) && (acmod != 1))
	state->clev = clev[bitstream_get (state, 2)];	/* cmixlev */

    if (acmod & 4)
	state->slev = slev[bitstream_get (state, 2)];	/* surmixlev */

    state->lfeon = bitstream_get (state, 1);

    state->output = a52_downmix_init (acmod, *flags, level,
				      state->clev, state->slev);
    if (state->output < 0)
	return 1;
    if (state->lfeon && (*flags & A52_LFE))
	state->output |= A52_LFE;
    *flags = state->output;
    /* the 2* compensates for differences in imdct */
    state->dynrng = state->level = MUL_C (*level, 2);
    state->bias = bias;
    state->dynrnge = 1;
    state->dynrngcall = NULL;
    state->cplba.deltbae = DELTA_BIT_NONE;
    state->ba[0].deltbae = state->ba[1].deltbae = state->ba[2].deltbae =
	state->ba[3].deltbae = state->ba[4].deltbae = DELTA_BIT_NONE;

    chaninfo = !acmod;
    do {
	bitstream_get (state, 5);	/* dialnorm */
	if (bitstream_get (state, 1))	/* compre */
	    bitstream_get (state, 8);	/* compr */
	if (bitstream_get (state, 1))	/* langcode */
	    bitstream_get (state, 8);	/* langcod */
	if (bitstream_get (state, 1))	/* audprodie */
	    bitstream_get (state, 7);	/* mixlevel + roomtyp */
    } while (chaninfo--);

    bitstream_get (state, 2);		/* copyrightb + origbs */

    if (bitstream_get (state, 1))	/* timecod1e */
	bitstream_get (state, 14);	/* timecod1 */
    if (bitstream_get (state, 1))	/* timecod2e */
	bitstream_get (state, 14);	/* timecod2 */

    if (bitstream_get (state, 1)) {	/* addbsie */
	int addbsil;

	addbsil = bitstream_get (state, 6);
	do {
	    bitstream_get (state, 8);	/* addbsi */
	} while (addbsil--);
    }

    return 0;
}

void a52_dynrng (a52_state_t * state,
		 level_t (* call) (level_t, void *), void * data)
{
    state->dynrnge = 0;
    if (call) {
	state->dynrnge = 1;
	state->dynrngcall = call;
	state->dynrngdata = data;
    }
}

static int parse_exponents (a52_state_t * state, int expstr, int ngrps,
			    uint8_t exponent, uint8_t * dest)
{
    int exps;

    while (ngrps--) {
	exps = bitstream_get (state, 7);

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

static int parse_deltba (a52_state_t * state, int8_t * deltba)
{
    int deltnseg, deltlen, delta, j;

    memset (deltba, 0, 50);

    deltnseg = bitstream_get (state, 3);
    j = 0;
    do {
	j += bitstream_get (state, 5);
	deltlen = bitstream_get (state, 4);
	delta = bitstream_get (state, 3);
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

static inline int zero_snr_offsets (int nfchans, a52_state_t * state)
{
    int i;

    if ((state->csnroffst) ||
	(state->chincpl && state->cplba.bai >> 3) ||	/* cplinu, fsnroffst */
	(state->lfeon && state->lfeba.bai >> 3))	/* fsnroffst */
	return 0;
    for (i = 0; i < nfchans; i++)
	if (state->ba[i].bai >> 3)			/* fsnroffst */
	    return 0;
    return 1;
}

static inline int16_t dither_gen (a52_state_t * state)
{
    int16_t nstate;

    nstate = dither_lut[state->lfsr_state >> 8] ^ (state->lfsr_state << 8);
	
    state->lfsr_state = (uint16_t) nstate;

    return (3 * nstate) >> 2;
}

#ifndef LIBA52_FIXED
#define COEFF(c,t,l,s,e) (c) = (t) * (s)[e]
#else
#define COEFF(c,_t,_l,s,e) do {					\
    quantizer_t t = (_t);					\
    level_t l = (_l);						\
    int shift = e - 5;						\
    sample_t tmp = t * (l >> 16) + ((t * (l & 0xffff)) >> 16);	\
    if (shift >= 0)						\
	(c) = tmp >> shift;					\
    else							\
	(c) = tmp << -shift;					\
} while (0)
#endif

static void coeff_get (a52_state_t * state, sample_t * coeff,
		       expbap_t * expbap, quantizer_set_t * quant,
		       level_t level, int dither, int end)
{
    int i;
    uint8_t * exp;
    int8_t * bap;

#ifndef LIBA52_FIXED
    sample_t factor[25];

    for (i = 0; i <= 24; i++)
	factor[i] = scale_factor[i] * level;
#endif

    exp = expbap->exp;
    bap = expbap->bap;

    for (i = 0; i < end; i++) {
	int bapi;

	bapi = bap[i];
	switch (bapi) {
	case 0:
	    if (dither) {
		COEFF (coeff[i], dither_gen (state), level, factor, exp[i]);
		continue;
	    } else {
		coeff[i] = 0;
		continue;
	    }

	case -1:
	    if (quant->q1_ptr >= 0) {
		COEFF (coeff[i], quant->q1[quant->q1_ptr--], level,
		       factor, exp[i]);
		continue;
	    } else {
		int code;

		code = bitstream_get (state, 5);

		quant->q1_ptr = 1;
		quant->q1[0] = q_1_2[code];
		quant->q1[1] = q_1_1[code];
		COEFF (coeff[i], q_1_0[code], level, factor, exp[i]);
		continue;
	    }

	case -2:
	    if (quant->q2_ptr >= 0) {
		COEFF (coeff[i], quant->q2[quant->q2_ptr--], level,
		       factor, exp[i]);
		continue;
	    } else {
		int code;

		code = bitstream_get (state, 7);

		quant->q2_ptr = 1;
		quant->q2[0] = q_2_2[code];
		quant->q2[1] = q_2_1[code];
		COEFF (coeff[i], q_2_0[code], level, factor, exp[i]);
		continue;
	    }

	case 3:
	    COEFF (coeff[i], q_3[bitstream_get (state, 3)], level,
		   factor, exp[i]);
	    continue;

	case -3:
	    if (quant->q4_ptr == 0) {
		quant->q4_ptr = -1;
		COEFF (coeff[i], quant->q4, level, factor, exp[i]);
		continue;
	    } else {
		int code;

		code = bitstream_get (state, 7);

		quant->q4_ptr = 0;
		quant->q4 = q_4_1[code];
		COEFF (coeff[i], q_4_0[code], level, factor, exp[i]);
		continue;
	    }

	case 4:
	    COEFF (coeff[i], q_5[bitstream_get (state, 4)], level,
		   factor, exp[i]);
	    continue;

	default:
	    COEFF (coeff[i], bitstream_get_2 (state, bapi) << (16 - bapi),
		   level, factor, exp[i]);
	}
    }
}

static void coeff_get_coupling (a52_state_t * state, int nfchans,
				level_t * coeff, sample_t (* samples)[256],
				quantizer_set_t * quant, uint8_t dithflag[5])
{
    int cplbndstrc, bnd, i, i_end, ch;
    uint8_t * exp;
    int8_t * bap;
    level_t cplco[5];

    exp = state->cpl_expbap.exp;
    bap = state->cpl_expbap.bap;
    bnd = 0;
    cplbndstrc = state->cplbndstrc;
    i = state->cplstrtmant;
    while (i < state->cplendmant) {
	i_end = i + 12;
	while (cplbndstrc & 1) {
	    cplbndstrc >>= 1;
	    i_end += 12;
	}
	cplbndstrc >>= 1;
	for (ch = 0; ch < nfchans; ch++)
	    cplco[ch] = MUL_L (state->cplco[ch][bnd], coeff[ch]);
	bnd++;

	while (i < i_end) {
	    quantizer_t cplcoeff;
	    int bapi;

	    bapi = bap[i];
	    switch (bapi) {
	    case 0:
		for (ch = 0; ch < nfchans; ch++)
		    if ((state->chincpl >> ch) & 1) {
			if (dithflag[ch])
#ifndef LIBA52_FIXED
			    samples[ch][i] = (scale_factor[exp[i]] *
					      cplco[ch] * dither_gen (state));
#else
			    COEFF (samples[ch][i], dither_gen (state),
				   cplco[ch], scale_factor, exp[i]);
#endif
			else
			    samples[ch][i] = 0;
		    }
		i++;
		continue;

	    case -1:
		if (quant->q1_ptr >= 0) {
		    cplcoeff = quant->q1[quant->q1_ptr--];
		    break;
		} else {
		    int code;

		    code = bitstream_get (state, 5);

		    quant->q1_ptr = 1;
		    quant->q1[0] = q_1_2[code];
		    quant->q1[1] = q_1_1[code];
		    cplcoeff = q_1_0[code];
		    break;
		}

	    case -2:
		if (quant->q2_ptr >= 0) {
		    cplcoeff = quant->q2[quant->q2_ptr--];
		    break;
		} else {
		    int code;

		    code = bitstream_get (state, 7);

		    quant->q2_ptr = 1;
		    quant->q2[0] = q_2_2[code];
		    quant->q2[1] = q_2_1[code];
		    cplcoeff = q_2_0[code];
		    break;
		}

	    case 3:
		cplcoeff = q_3[bitstream_get (state, 3)];
		break;

	    case -3:
		if (quant->q4_ptr == 0) {
		    quant->q4_ptr = -1;
		    cplcoeff = quant->q4;
		    break;
		} else {
		    int code;

		    code = bitstream_get (state, 7);

		    quant->q4_ptr = 0;
		    quant->q4 = q_4_1[code];
		    cplcoeff = q_4_0[code];
		    break;
		}

	    case 4:
		cplcoeff = q_5[bitstream_get (state, 4)];
		break;

	    default:
		cplcoeff = bitstream_get_2 (state, bapi) << (16 - bapi);
	    }
#ifndef LIBA52_FIXED
	    cplcoeff *= scale_factor[exp[i]];
#endif
	    for (ch = 0; ch < nfchans; ch++)
	       if ((state->chincpl >> ch) & 1)
#ifndef LIBA52_FIXED
		    samples[ch][i] = cplcoeff * cplco[ch];
#else
		    COEFF (samples[ch][i], cplcoeff, cplco[ch],
			   scale_factor, exp[i]);
#endif
	    i++;
	}
    }
}

int a52_block (a52_state_t * state)
{
    static const uint8_t nfchans_tbl[] = {2, 1, 2, 3, 3, 4, 4, 5, 1, 1, 2};
    static int rematrix_band[4] = {25, 37, 61, 253};
    int i, nfchans, chaninfo;
    uint8_t cplexpstr, chexpstr[5], lfeexpstr, do_bit_alloc, done_cpl;
    uint8_t blksw[5], dithflag[5];
    level_t coeff[5];
    int chanbias;
    quantizer_set_t quant;
    sample_t * samples;

    nfchans = nfchans_tbl[state->acmod];

    for (i = 0; i < nfchans; i++)
	blksw[i] = bitstream_get (state, 1);

    for (i = 0; i < nfchans; i++)
	dithflag[i] = bitstream_get (state, 1);

    chaninfo = !state->acmod;
    do {
	if (bitstream_get (state, 1)) {	/* dynrnge */
	    int dynrng;

	    dynrng = bitstream_get_2 (state, 8);
	    if (state->dynrnge) {
		level_t range;

#if !defined(LIBA52_FIXED)
		range = ((((dynrng & 0x1f) | 0x20) << 13) *
			 scale_factor[3 - (dynrng >> 5)]);
#else
		range = ((dynrng & 0x1f) | 0x20) << (21 + (dynrng >> 5));
#endif
		if (state->dynrngcall)
		    range = state->dynrngcall (range, state->dynrngdata);
		state->dynrng = MUL_L (state->level, range);
	    }
	}
    } while (chaninfo--);

    if (bitstream_get (state, 1)) {	/* cplstre */
	state->chincpl = 0;
	if (bitstream_get (state, 1)) {	/* cplinu */
	    static uint8_t bndtab[16] = {31, 35, 37, 39, 41, 42, 43, 44,
					 45, 45, 46, 46, 47, 47, 48, 48};
	    int cplbegf;
	    int cplendf;
	    int ncplsubnd;

	    for (i = 0; i < nfchans; i++)
		state->chincpl |= bitstream_get (state, 1) << i;
	    switch (state->acmod) {
	    case 0: case 1:
		return 1;
	    case 2:
		state->phsflginu = bitstream_get (state, 1);
	    }
	    cplbegf = bitstream_get (state, 4);
	    cplendf = bitstream_get (state, 4);

	    if (cplendf + 3 - cplbegf < 0)
		return 1;
	    state->ncplbnd = ncplsubnd = cplendf + 3 - cplbegf;
	    state->cplstrtbnd = bndtab[cplbegf];
	    state->cplstrtmant = cplbegf * 12 + 37;
	    state->cplendmant = cplendf * 12 + 73;

	    state->cplbndstrc = 0;
	    for (i = 0; i < ncplsubnd - 1; i++)
		if (bitstream_get (state, 1)) {
		    state->cplbndstrc |= 1 << i;
		    state->ncplbnd--;
		}
	}
    }

    if (state->chincpl) {	/* cplinu */
	int j, cplcoe;

	cplcoe = 0;
	for (i = 0; i < nfchans; i++)
	    if ((state->chincpl) >> i & 1)
		if (bitstream_get (state, 1)) {	/* cplcoe */
		    int mstrcplco, cplcoexp, cplcomant;

		    cplcoe = 1;
		    mstrcplco = 3 * bitstream_get (state, 2);
		    for (j = 0; j < state->ncplbnd; j++) {
			cplcoexp = bitstream_get (state, 4);
			cplcomant = bitstream_get (state, 4);
			if (cplcoexp == 15)
			    cplcomant <<= 14;
			else
			    cplcomant = (cplcomant | 0x10) << 13;
#ifndef LIBA52_FIXED
			state->cplco[i][j] =
			    cplcomant * scale_factor[cplcoexp + mstrcplco];
#else
			state->cplco[i][j] = (cplcomant << 11) >> (cplcoexp + mstrcplco);
#endif

		    }
		}
	if ((state->acmod == 2) && state->phsflginu && cplcoe)
	    for (j = 0; j < state->ncplbnd; j++)
		if (bitstream_get (state, 1))	/* phsflg */
		    state->cplco[1][j] = -state->cplco[1][j];
    }

    if ((state->acmod == 2) && (bitstream_get (state, 1))) {	/* rematstr */
	int end;

	state->rematflg = 0;
	end = (state->chincpl) ? state->cplstrtmant : 253;	/* cplinu */
	i = 0;
	do
	    state->rematflg |= bitstream_get (state, 1) << i;
	while (rematrix_band[i++] < end);
    }

    cplexpstr = EXP_REUSE;
    lfeexpstr = EXP_REUSE;
    if (state->chincpl)	/* cplinu */
	cplexpstr = bitstream_get (state, 2);
    for (i = 0; i < nfchans; i++)
	chexpstr[i] = bitstream_get (state, 2);
    if (state->lfeon) 
	lfeexpstr = bitstream_get (state, 1);

    for (i = 0; i < nfchans; i++)
	if (chexpstr[i] != EXP_REUSE) {
	    if ((state->chincpl >> i) & 1)
		state->endmant[i] = state->cplstrtmant;
	    else {
		int chbwcod;

		chbwcod = bitstream_get (state, 6);
		if (chbwcod > 60)
		    return 1;
		state->endmant[i] = chbwcod * 3 + 73;
	    }
	}

    do_bit_alloc = 0;

    if (cplexpstr != EXP_REUSE) {
	int cplabsexp, ncplgrps;

	do_bit_alloc = 64;
	ncplgrps = ((state->cplendmant - state->cplstrtmant) /
		    (3 << (cplexpstr - 1)));
	cplabsexp = bitstream_get (state, 4) << 1;
	if (parse_exponents (state, cplexpstr, ncplgrps, cplabsexp,
			     state->cpl_expbap.exp + state->cplstrtmant))
	    return 1;
    }
    for (i = 0; i < nfchans; i++)
	if (chexpstr[i] != EXP_REUSE) {
	    int grp_size, nchgrps;

	    do_bit_alloc |= 1 << i;
	    grp_size = 3 << (chexpstr[i] - 1);
	    nchgrps = (state->endmant[i] + grp_size - 4) / grp_size;
	    state->fbw_expbap[i].exp[0] = bitstream_get (state, 4);
	    if (parse_exponents (state, chexpstr[i], nchgrps,
				 state->fbw_expbap[i].exp[0],
				 state->fbw_expbap[i].exp + 1))
		return 1;
	    bitstream_get (state, 2);	/* gainrng */
	}
    if (lfeexpstr != EXP_REUSE) {
	do_bit_alloc |= 32;
	state->lfe_expbap.exp[0] = bitstream_get (state, 4);
	if (parse_exponents (state, lfeexpstr, 2, state->lfe_expbap.exp[0],
			     state->lfe_expbap.exp + 1))
	    return 1;
    }

    if (bitstream_get (state, 1)) {	/* baie */
	do_bit_alloc = 127;
	state->bai = bitstream_get (state, 11);
    }
    if (bitstream_get (state, 1)) {	/* snroffste */
	do_bit_alloc = 127;
	state->csnroffst = bitstream_get (state, 6);
	if (state->chincpl)	/* cplinu */
	    state->cplba.bai = bitstream_get (state, 7);
	for (i = 0; i < nfchans; i++)
	    state->ba[i].bai = bitstream_get (state, 7);
	if (state->lfeon)
	    state->lfeba.bai = bitstream_get (state, 7);
    }
    if ((state->chincpl) && (bitstream_get (state, 1))) { /* cplleake */
	do_bit_alloc |= 64;
	state->cplfleak = 9 - bitstream_get (state, 3);
	state->cplsleak = 9 - bitstream_get (state, 3);
    }

    if (bitstream_get (state, 1)) {	/* deltbaie */
	do_bit_alloc = 127;
	if (state->chincpl)	/* cplinu */
	    state->cplba.deltbae = bitstream_get (state, 2);
	for (i = 0; i < nfchans; i++)
	    state->ba[i].deltbae = bitstream_get (state, 2);
	if (state->chincpl &&	/* cplinu */
	    (state->cplba.deltbae == DELTA_BIT_NEW) &&
	    parse_deltba (state, state->cplba.deltba))
	    return 1;
	for (i = 0; i < nfchans; i++)
	    if ((state->ba[i].deltbae == DELTA_BIT_NEW) &&
		parse_deltba (state, state->ba[i].deltba))
		return 1;
    }

    if (do_bit_alloc) {
	if (zero_snr_offsets (nfchans, state)) {
	    memset (state->cpl_expbap.bap, 0, sizeof (state->cpl_expbap.bap));
	    for (i = 0; i < nfchans; i++)
		memset (state->fbw_expbap[i].bap, 0,
			sizeof (state->fbw_expbap[i].bap));
	    memset (state->lfe_expbap.bap, 0, sizeof (state->lfe_expbap.bap));
	} else {
	    if (state->chincpl && (do_bit_alloc & 64))	/* cplinu */
		a52_bit_allocate (state, &state->cplba, state->cplstrtbnd,
				  state->cplstrtmant, state->cplendmant,
				  state->cplfleak << 8, state->cplsleak << 8,
				  &state->cpl_expbap);
	    for (i = 0; i < nfchans; i++)
		if (do_bit_alloc & (1 << i))
		    a52_bit_allocate (state, state->ba + i, 0, 0,
				      state->endmant[i], 0, 0,
				      state->fbw_expbap +i);
	    if (state->lfeon && (do_bit_alloc & 32)) {
		state->lfeba.deltbae = DELTA_BIT_NONE;
		a52_bit_allocate (state, &state->lfeba, 0, 0, 7, 0, 0,
				  &state->lfe_expbap);
	    }
	}
    }

    if (bitstream_get (state, 1)) {	/* skiple */
	i = bitstream_get (state, 9);	/* skipl */
	while (i--)
	    bitstream_get (state, 8);
    }

    samples = state->samples;
    if (state->output & A52_LFE)
	samples += 256;	/* shift for LFE channel */

    chanbias = a52_downmix_coeff (coeff, state->acmod, state->output,
				  state->dynrng, state->clev, state->slev);

    quant.q1_ptr = quant.q2_ptr = quant.q4_ptr = -1;
    done_cpl = 0;

    for (i = 0; i < nfchans; i++) {
	int j;

	coeff_get (state, samples + 256 * i, state->fbw_expbap +i, &quant,
		   coeff[i], dithflag[i], state->endmant[i]);

	if ((state->chincpl >> i) & 1) {
	    if (!done_cpl) {
		done_cpl = 1;
		coeff_get_coupling (state, nfchans, coeff,
				    (sample_t (*)[256])samples, &quant,
				    dithflag);
	    }
	    j = state->cplendmant;
	} else
	    j = state->endmant[i];
	do
	    (samples + 256 * i)[j] = 0;
	while (++j < 256);
    }

    if (state->acmod == 2) {
	int j, end, band, rematflg;

	end = ((state->endmant[0] < state->endmant[1]) ?
	       state->endmant[0] : state->endmant[1]);

	i = 0;
	j = 13;
	rematflg = state->rematflg;
	do {
	    if (! (rematflg & 1)) {
		rematflg >>= 1;
		j = rematrix_band[i++];
		continue;
	    }
	    rematflg >>= 1;
	    band = rematrix_band[i++];
	    if (band > end)
		band = end;
	    do {
		sample_t tmp0, tmp1;

		tmp0 = samples[j];
		tmp1 = (samples+256)[j];
		samples[j] = tmp0 + tmp1;
		(samples+256)[j] = tmp0 - tmp1;
	    } while (++j < band);
	} while (j < end);
    }

    if (state->lfeon) {
	if (state->output & A52_LFE) {
	    coeff_get (state, samples - 256, &state->lfe_expbap, &quant,
		       state->dynrng, 0, 7);
	    for (i = 7; i < 256; i++)
		(samples-256)[i] = 0;
	    a52_imdct_512 (samples - 256, samples + 1536 - 256, state->bias);
	} else {
	    /* just skip the LFE coefficients */
	    coeff_get (state, samples + 1280, &state->lfe_expbap, &quant,
		       0, 0, 7);
	}
    }

    i = 0;
    if (nfchans_tbl[state->output & A52_CHANNEL_MASK] < nfchans)
	for (i = 1; i < nfchans; i++)
	    if (blksw[i] != blksw[0])
		break;

    if (i < nfchans) {
	if (state->downmixed) {
	    state->downmixed = 0;
	    a52_upmix (samples + 1536, state->acmod, state->output);
	}

	for (i = 0; i < nfchans; i++) {
	    sample_t bias;

	    bias = 0;
	    if (!(chanbias & (1 << i)))
		bias = state->bias;

	    if (coeff[i]) {
		if (blksw[i])
		    a52_imdct_256 (samples + 256 * i, samples + 1536 + 256 * i,
				   bias);
		else 
		    a52_imdct_512 (samples + 256 * i, samples + 1536 + 256 * i,
				   bias);
	    } else {
		int j;

		for (j = 0; j < 256; j++)
		    (samples + 256 * i)[j] = bias;
	    }
	}

	a52_downmix (samples, state->acmod, state->output, state->bias,
		     state->clev, state->slev);
    } else {
	nfchans = nfchans_tbl[state->output & A52_CHANNEL_MASK];

	a52_downmix (samples, state->acmod, state->output, 0,
		     state->clev, state->slev);

	if (!state->downmixed) {
	    state->downmixed = 1;
	    a52_downmix (samples + 1536, state->acmod, state->output, 0,
			 state->clev, state->slev);
	}

	if (blksw[0])
	    for (i = 0; i < nfchans; i++)
		a52_imdct_256 (samples + 256 * i, samples + 1536 + 256 * i,
			       state->bias);
	else 
	    for (i = 0; i < nfchans; i++)
		a52_imdct_512 (samples + 256 * i, samples + 1536 + 256 * i,
			       state->bias);
    }

    return 0;
}

void a52_free (a52_state_t * state)
{
    free (state->samples);
    free (state);
}
