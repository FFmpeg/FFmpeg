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
#include "dsputil_arm.h"

void ff_j_rev_dct_arm(DCTELEM *data);
void ff_simple_idct_arm(DCTELEM *data);

/* XXX: local hack */
static void (*ff_put_pixels_clamped)(const DCTELEM *block, uint8_t *pixels, int line_size);
static void (*ff_add_pixels_clamped)(const DCTELEM *block, uint8_t *pixels, int line_size);

void ff_put_pixels8_arm(uint8_t *block, const uint8_t *pixels, int line_size, int h);
void ff_put_pixels8_x2_arm(uint8_t *block, const uint8_t *pixels, int line_size, int h);
void ff_put_pixels8_y2_arm(uint8_t *block, const uint8_t *pixels, int line_size, int h);
void ff_put_pixels8_xy2_arm(uint8_t *block, const uint8_t *pixels, int line_size, int h);

void ff_put_no_rnd_pixels8_x2_arm(uint8_t *block, const uint8_t *pixels, int line_size, int h);
void ff_put_no_rnd_pixels8_y2_arm(uint8_t *block, const uint8_t *pixels, int line_size, int h);
void ff_put_no_rnd_pixels8_xy2_arm(uint8_t *block, const uint8_t *pixels, int line_size, int h);

void ff_put_pixels16_arm(uint8_t *block, const uint8_t *pixels, int line_size, int h);

CALL_2X_PIXELS(ff_put_pixels16_x2_arm,         ff_put_pixels8_x2_arm,        8)
CALL_2X_PIXELS(ff_put_pixels16_y2_arm,         ff_put_pixels8_y2_arm,        8)
CALL_2X_PIXELS(ff_put_pixels16_xy2_arm,        ff_put_pixels8_xy2_arm,       8)
CALL_2X_PIXELS(ff_put_no_rnd_pixels16_x2_arm,  ff_put_no_rnd_pixels8_x2_arm, 8)
CALL_2X_PIXELS(ff_put_no_rnd_pixels16_y2_arm,  ff_put_no_rnd_pixels8_y2_arm, 8)
CALL_2X_PIXELS(ff_put_no_rnd_pixels16_xy2_arm, ff_put_no_rnd_pixels8_xy2_arm,8)

void ff_add_pixels_clamped_arm(const DCTELEM *block, uint8_t *dest,
                               int line_size);

/* XXX: those functions should be suppressed ASAP when all IDCTs are
   converted */
static void j_rev_dct_arm_put(uint8_t *dest, int line_size, DCTELEM *block)
{
    ff_j_rev_dct_arm (block);
    ff_put_pixels_clamped(block, dest, line_size);
}
static void j_rev_dct_arm_add(uint8_t *dest, int line_size, DCTELEM *block)
{
    ff_j_rev_dct_arm (block);
    ff_add_pixels_clamped(block, dest, line_size);
}
static void simple_idct_arm_put(uint8_t *dest, int line_size, DCTELEM *block)
{
    ff_simple_idct_arm (block);
    ff_put_pixels_clamped(block, dest, line_size);
}
static void simple_idct_arm_add(uint8_t *dest, int line_size, DCTELEM *block)
{
    ff_simple_idct_arm (block);
    ff_add_pixels_clamped(block, dest, line_size);
}

void dsputil_init_arm(DSPContext* c, AVCodecContext *avctx)
{
    ff_put_pixels_clamped = c->put_pixels_clamped;
    ff_add_pixels_clamped = c->add_pixels_clamped;

    if (!avctx->lowres) {
        if(avctx->idct_algo == FF_IDCT_AUTO ||
           avctx->idct_algo == FF_IDCT_ARM){
            c->idct_put              = j_rev_dct_arm_put;
            c->idct_add              = j_rev_dct_arm_add;
            c->idct                  = ff_j_rev_dct_arm;
            c->idct_permutation_type = FF_LIBMPEG2_IDCT_PERM;
        } else if (avctx->idct_algo == FF_IDCT_SIMPLEARM){
            c->idct_put              = simple_idct_arm_put;
            c->idct_add              = simple_idct_arm_add;
            c->idct                  = ff_simple_idct_arm;
            c->idct_permutation_type = FF_NO_IDCT_PERM;
        }
    }

    c->add_pixels_clamped = ff_add_pixels_clamped_arm;

    c->put_pixels_tab[0][0] = ff_put_pixels16_arm;
    c->put_pixels_tab[0][1] = ff_put_pixels16_x2_arm;
    c->put_pixels_tab[0][2] = ff_put_pixels16_y2_arm;
    c->put_pixels_tab[0][3] = ff_put_pixels16_xy2_arm;
    c->put_pixels_tab[1][0] = ff_put_pixels8_arm;
    c->put_pixels_tab[1][1] = ff_put_pixels8_x2_arm;
    c->put_pixels_tab[1][2] = ff_put_pixels8_y2_arm;
    c->put_pixels_tab[1][3] = ff_put_pixels8_xy2_arm;

    c->put_no_rnd_pixels_tab[0][0] = ff_put_pixels16_arm;
    c->put_no_rnd_pixels_tab[0][1] = ff_put_no_rnd_pixels16_x2_arm;
    c->put_no_rnd_pixels_tab[0][2] = ff_put_no_rnd_pixels16_y2_arm;
    c->put_no_rnd_pixels_tab[0][3] = ff_put_no_rnd_pixels16_xy2_arm;
    c->put_no_rnd_pixels_tab[1][0] = ff_put_pixels8_arm;
    c->put_no_rnd_pixels_tab[1][1] = ff_put_no_rnd_pixels8_x2_arm;
    c->put_no_rnd_pixels_tab[1][2] = ff_put_no_rnd_pixels8_y2_arm;
    c->put_no_rnd_pixels_tab[1][3] = ff_put_no_rnd_pixels8_xy2_arm;

    if (HAVE_ARMV5TE) ff_dsputil_init_armv5te(c, avctx);
    if (HAVE_ARMV6)   ff_dsputil_init_armv6(c, avctx);
    if (HAVE_IWMMXT)  ff_dsputil_init_iwmmxt(c, avctx);
    if (HAVE_ARMVFP)  ff_dsputil_init_vfp(c, avctx);
    if (HAVE_NEON)    ff_dsputil_init_neon(c, avctx);
}
