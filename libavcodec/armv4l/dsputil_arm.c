/*
 * ARMv4L optimized DSP utils
 * Copyright (c) 2001 Lionel Ulmer.
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

#include "../dsputil.h"

extern void j_rev_dct_ARM(DCTELEM *data);
extern void simple_idct_ARM(DCTELEM *data);

/* XXX: local hack */
static void (*ff_put_pixels_clamped)(const DCTELEM *block, uint8_t *pixels, int line_size);
static void (*ff_add_pixels_clamped)(const DCTELEM *block, uint8_t *pixels, int line_size);

/* XXX: those functions should be suppressed ASAP when all IDCTs are
   converted */
static void j_rev_dct_ARM_put(uint8_t *dest, int line_size, DCTELEM *block)
{
    j_rev_dct_ARM (block);
    ff_put_pixels_clamped(block, dest, line_size);
}
static void j_rev_dct_ARM_add(uint8_t *dest, int line_size, DCTELEM *block)
{
    j_rev_dct_ARM (block);
    ff_add_pixels_clamped(block, dest, line_size);
}
static void simple_idct_ARM_put(uint8_t *dest, int line_size, DCTELEM *block)
{
    simple_idct_ARM (block);
    ff_put_pixels_clamped(block, dest, line_size);
}
static void simple_idct_ARM_add(uint8_t *dest, int line_size, DCTELEM *block)
{
    simple_idct_ARM (block);
    ff_add_pixels_clamped(block, dest, line_size);
}

void dsputil_init_armv4l(DSPContext* c, AVCodecContext *avctx)
{
    const int idct_algo= avctx->idct_algo;

    ff_put_pixels_clamped = c->put_pixels_clamped;
    ff_add_pixels_clamped = c->add_pixels_clamped;

    if(idct_algo==FF_IDCT_AUTO || idct_algo==FF_IDCT_ARM){
        c->idct_put= j_rev_dct_ARM_put;
        c->idct_add= j_rev_dct_ARM_add;
	c->idct    = j_rev_dct_ARM;
        c->idct_permutation_type= FF_LIBMPEG2_IDCT_PERM;/* FF_NO_IDCT_PERM */
    } else if (idct_algo==FF_IDCT_SIMPLEARM){
	c->idct_put= simple_idct_ARM_put;
	c->idct_add= simple_idct_ARM_add;
	c->idct    = simple_idct_ARM;
	c->idct_permutation_type= FF_NO_IDCT_PERM;
    }
}
