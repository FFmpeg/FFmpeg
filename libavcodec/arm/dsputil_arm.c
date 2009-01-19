/*
 * ARM optimized DSP utils
 * Copyright (c) 2001 Lionel Ulmer
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

#include "libavcodec/dsputil.h"
#if HAVE_IPP
#include <ipp.h>
#endif

void dsputil_init_iwmmxt(DSPContext* c, AVCodecContext *avctx);
void ff_float_init_arm_vfp(DSPContext* c, AVCodecContext *avctx);
void ff_dsputil_init_neon(DSPContext *c, AVCodecContext *avctx);

void j_rev_dct_ARM(DCTELEM *data);
void simple_idct_ARM(DCTELEM *data);

void simple_idct_armv5te(DCTELEM *data);
void simple_idct_put_armv5te(uint8_t *dest, int line_size, DCTELEM *data);
void simple_idct_add_armv5te(uint8_t *dest, int line_size, DCTELEM *data);

void ff_simple_idct_armv6(DCTELEM *data);
void ff_simple_idct_put_armv6(uint8_t *dest, int line_size, DCTELEM *data);
void ff_simple_idct_add_armv6(uint8_t *dest, int line_size, DCTELEM *data);

void ff_simple_idct_neon(DCTELEM *data);
void ff_simple_idct_put_neon(uint8_t *dest, int line_size, DCTELEM *data);
void ff_simple_idct_add_neon(uint8_t *dest, int line_size, DCTELEM *data);

/* XXX: local hack */
static void (*ff_put_pixels_clamped)(const DCTELEM *block, uint8_t *pixels, int line_size);
static void (*ff_add_pixels_clamped)(const DCTELEM *block, uint8_t *pixels, int line_size);

void put_pixels8_arm(uint8_t *block, const uint8_t *pixels, int line_size, int h);
void put_pixels8_x2_arm(uint8_t *block, const uint8_t *pixels, int line_size, int h);
void put_pixels8_y2_arm(uint8_t *block, const uint8_t *pixels, int line_size, int h);
void put_pixels8_xy2_arm(uint8_t *block, const uint8_t *pixels, int line_size, int h);

void put_no_rnd_pixels8_x2_arm(uint8_t *block, const uint8_t *pixels, int line_size, int h);
void put_no_rnd_pixels8_y2_arm(uint8_t *block, const uint8_t *pixels, int line_size, int h);
void put_no_rnd_pixels8_xy2_arm(uint8_t *block, const uint8_t *pixels, int line_size, int h);

void put_pixels16_arm(uint8_t *block, const uint8_t *pixels, int line_size, int h);

void ff_prefetch_arm(void *mem, int stride, int h);

CALL_2X_PIXELS(put_pixels16_x2_arm , put_pixels8_x2_arm , 8)
CALL_2X_PIXELS(put_pixels16_y2_arm , put_pixels8_y2_arm , 8)
CALL_2X_PIXELS(put_pixels16_xy2_arm, put_pixels8_xy2_arm, 8)
CALL_2X_PIXELS(put_no_rnd_pixels16_x2_arm , put_no_rnd_pixels8_x2_arm , 8)
CALL_2X_PIXELS(put_no_rnd_pixels16_y2_arm , put_no_rnd_pixels8_y2_arm , 8)
CALL_2X_PIXELS(put_no_rnd_pixels16_xy2_arm, put_no_rnd_pixels8_xy2_arm, 8)

void ff_add_pixels_clamped_ARM(short *block, unsigned char *dest,
                                      int line_size);

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

#if HAVE_IPP
static void simple_idct_ipp(DCTELEM *block)
{
    ippiDCT8x8Inv_Video_16s_C1I(block);
}
static void simple_idct_ipp_put(uint8_t *dest, int line_size, DCTELEM *block)
{
    ippiDCT8x8Inv_Video_16s8u_C1R(block, dest, line_size);
}

void add_pixels_clamped_iwmmxt(const DCTELEM *block, uint8_t *pixels, int line_size);

static void simple_idct_ipp_add(uint8_t *dest, int line_size, DCTELEM *block)
{
    ippiDCT8x8Inv_Video_16s_C1I(block);
#if HAVE_IWMMXT
    add_pixels_clamped_iwmmxt(block, dest, line_size);
#else
    ff_add_pixels_clamped_ARM(block, dest, line_size);
#endif
}
#endif

int mm_support(void)
{
    return HAVE_IWMMXT * FF_MM_IWMMXT;
}

void dsputil_init_arm(DSPContext* c, AVCodecContext *avctx)
{
    int idct_algo= avctx->idct_algo;

    ff_put_pixels_clamped = c->put_pixels_clamped;
    ff_add_pixels_clamped = c->add_pixels_clamped;

    if (avctx->lowres == 0) {
        if(idct_algo == FF_IDCT_AUTO){
#if   HAVE_IPP
            idct_algo = FF_IDCT_IPP;
#elif HAVE_NEON
            idct_algo = FF_IDCT_SIMPLENEON;
#elif HAVE_ARMV6
            idct_algo = FF_IDCT_SIMPLEARMV6;
#elif HAVE_ARMV5TE
            idct_algo = FF_IDCT_SIMPLEARMV5TE;
#else
            idct_algo = FF_IDCT_ARM;
#endif
        }

        if(idct_algo==FF_IDCT_ARM){
            c->idct_put= j_rev_dct_ARM_put;
            c->idct_add= j_rev_dct_ARM_add;
            c->idct    = j_rev_dct_ARM;
            c->idct_permutation_type= FF_LIBMPEG2_IDCT_PERM;
        } else if (idct_algo==FF_IDCT_SIMPLEARM){
            c->idct_put= simple_idct_ARM_put;
            c->idct_add= simple_idct_ARM_add;
            c->idct    = simple_idct_ARM;
            c->idct_permutation_type= FF_NO_IDCT_PERM;
#if HAVE_ARMV6
        } else if (idct_algo==FF_IDCT_SIMPLEARMV6){
            c->idct_put= ff_simple_idct_put_armv6;
            c->idct_add= ff_simple_idct_add_armv6;
            c->idct    = ff_simple_idct_armv6;
            c->idct_permutation_type= FF_LIBMPEG2_IDCT_PERM;
#endif
#if HAVE_ARMV5TE
        } else if (idct_algo==FF_IDCT_SIMPLEARMV5TE){
            c->idct_put= simple_idct_put_armv5te;
            c->idct_add= simple_idct_add_armv5te;
            c->idct    = simple_idct_armv5te;
            c->idct_permutation_type = FF_NO_IDCT_PERM;
#endif
#if HAVE_IPP
        } else if (idct_algo==FF_IDCT_IPP){
            c->idct_put= simple_idct_ipp_put;
            c->idct_add= simple_idct_ipp_add;
            c->idct    = simple_idct_ipp;
            c->idct_permutation_type= FF_NO_IDCT_PERM;
#endif
#if HAVE_NEON
        } else if (idct_algo==FF_IDCT_SIMPLENEON){
            c->idct_put= ff_simple_idct_put_neon;
            c->idct_add= ff_simple_idct_add_neon;
            c->idct    = ff_simple_idct_neon;
            c->idct_permutation_type = FF_PARTTRANS_IDCT_PERM;
#endif
        }
    }

    c->put_pixels_tab[0][0] = put_pixels16_arm;
    c->put_pixels_tab[0][1] = put_pixels16_x2_arm;
    c->put_pixels_tab[0][2] = put_pixels16_y2_arm;
    c->put_pixels_tab[0][3] = put_pixels16_xy2_arm;
    c->put_no_rnd_pixels_tab[0][0] = put_pixels16_arm;
    c->put_no_rnd_pixels_tab[0][1] = put_no_rnd_pixels16_x2_arm;
    c->put_no_rnd_pixels_tab[0][2] = put_no_rnd_pixels16_y2_arm;
    c->put_no_rnd_pixels_tab[0][3] = put_no_rnd_pixels16_xy2_arm;
    c->put_pixels_tab[1][0] = put_pixels8_arm;
    c->put_pixels_tab[1][1] = put_pixels8_x2_arm;
    c->put_pixels_tab[1][2] = put_pixels8_y2_arm;
    c->put_pixels_tab[1][3] = put_pixels8_xy2_arm;
    c->put_no_rnd_pixels_tab[1][0] = put_pixels8_arm;
    c->put_no_rnd_pixels_tab[1][1] = put_no_rnd_pixels8_x2_arm;
    c->put_no_rnd_pixels_tab[1][2] = put_no_rnd_pixels8_y2_arm;
    c->put_no_rnd_pixels_tab[1][3] = put_no_rnd_pixels8_xy2_arm;

#if HAVE_ARMV5TE
    c->prefetch = ff_prefetch_arm;
#endif

#if HAVE_IWMMXT
    dsputil_init_iwmmxt(c, avctx);
#endif
#if HAVE_ARMVFP
    ff_float_init_arm_vfp(c, avctx);
#endif
#if HAVE_NEON
    ff_dsputil_init_neon(c, avctx);
#endif
}
