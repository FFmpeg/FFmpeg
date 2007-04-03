/*
 * BlackFin DSPUTILS
 *
 * Copyright (C) 2007 Marc Hoffman <marc.hoffman@analog.com>
 * Copyright (c) 2006 Michael Benjamin <michael.benjamin@analog.com>
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

#include <unistd.h>
#include <bits/bfin_sram.h>
#include "../avcodec.h"
#include "../dsputil.h"

#define USE_L1CODE

#ifdef USE_L1CODE
#define L1CODE __attribute__ ((l1_text))
#else
#define L1CODE
#endif
int off;


extern void ff_bfin_idct (DCTELEM *block) L1CODE;
extern void ff_bfin_fdct (DCTELEM *block) L1CODE;
extern void ff_bfin_add_pixels_clamped (DCTELEM *block, uint8_t *dest, int line_size) L1CODE;
extern void ff_bfin_put_pixels_clamped (DCTELEM *block, uint8_t *dest, int line_size) L1CODE;
extern void ff_bfin_diff_pixels (DCTELEM *block, uint8_t *s1, uint8_t *s2, int stride)  L1CODE;
extern void ff_bfin_get_pixels  (DCTELEM *restrict block, const uint8_t *pixels, int line_size) L1CODE;
extern int  ff_bfin_pix_norm1  (uint8_t * pix, int line_size) L1CODE;
extern int  ff_bfin_z_sad8x8   (uint8_t *blk1, uint8_t *blk2, int dsz, int line_size, int h) L1CODE;
extern int  ff_bfin_z_sad16x16 (uint8_t *blk1, uint8_t *blk2, int dsz, int line_size, int h) L1CODE;

extern void ff_bfin_z_put_pixels16_xy2     (uint8_t *block, const uint8_t *s0, int dest_size, int line_size, int h) L1CODE;
extern void ff_bfin_z_put_pixels8_xy2      (uint8_t *block, const uint8_t *s0, int dest_size, int line_size, int h) L1CODE;
extern void ff_bfin_put_pixels16_xy2_nornd (uint8_t *block, const uint8_t *s0, int line_size, int h) L1CODE;
extern void ff_bfin_put_pixels8_xy2_nornd  (uint8_t *block, const uint8_t *s0, int line_size, int h) L1CODE;


extern int  ff_bfin_pix_sum (uint8_t *p, int stride) L1CODE;

extern void ff_bfin_put_pixels8uc        (uint8_t *block, const uint8_t *s0, const uint8_t *s1, int dest_size, int line_size, int h) L1CODE;
extern void ff_bfin_put_pixels16uc       (uint8_t *block, const uint8_t *s0, const uint8_t *s1, int dest_size, int line_size, int h) L1CODE;
extern void ff_bfin_put_pixels8uc_nornd  (uint8_t *block, const uint8_t *s0, const uint8_t *s1, int line_size, int h) L1CODE;
extern void ff_bfin_put_pixels16uc_nornd (uint8_t *block, const uint8_t *s0, const uint8_t *s1, int line_size, int h) L1CODE;

extern int ff_bfin_sse4  (void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h) L1CODE;
extern int ff_bfin_sse8  (void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h) L1CODE;
extern int ff_bfin_sse16 (void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h) L1CODE;


static void bfin_idct_add (uint8_t *dest, int line_size, DCTELEM *block)
{
    ff_bfin_idct (block);
    ff_bfin_add_pixels_clamped (block, dest, line_size);
}

static void bfin_idct_put (uint8_t *dest, int line_size, DCTELEM *block)
{
    ff_bfin_idct (block);
    ff_bfin_put_pixels_clamped (block, dest, line_size);
}


static void bfin_clear_blocks (DCTELEM *blocks)
{
    // This is just a simple memset.
    //
    asm("P0=192; "
        "I0=%0;  "
        "R0=0;   "
        "LSETUP(clear_blocks_blkfn_lab,clear_blocks_blkfn_lab)LC0=P0;"
        "clear_blocks_blkfn_lab:"
        "[I0++]=R0;"
        ::"a" (blocks):"P0","I0","R0");
}



static void bfin_put_pixels8 (uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    ff_bfin_put_pixels8uc (block, pixels, pixels, line_size, line_size, h);
}

static void bfin_put_pixels8_x2(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    ff_bfin_put_pixels8uc (block, pixels, pixels+1, line_size, line_size, h);
}

static void bfin_put_pixels8_y2 (uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    ff_bfin_put_pixels8uc (block, pixels, pixels+line_size, line_size, line_size, h);
}

static void bfin_put_pixels8_xy2 (uint8_t *block, const uint8_t *s0, int line_size, int h)
{
    ff_bfin_z_put_pixels8_xy2 (block,s0,line_size, line_size, h);
}

static void bfin_put_pixels16 (uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    ff_bfin_put_pixels16uc (block, pixels, pixels, line_size, line_size, h);
}

static void bfin_put_pixels16_x2 (uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    ff_bfin_put_pixels16uc (block, pixels, pixels+1, line_size, line_size, h);
}

static void bfin_put_pixels16_y2 (uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    ff_bfin_put_pixels16uc (block, pixels, pixels+line_size, line_size, line_size, h);
}

static void bfin_put_pixels16_xy2 (uint8_t *block, const uint8_t *s0, int line_size, int h)
{
    ff_bfin_z_put_pixels16_xy2 (block,s0,line_size, line_size, h);
}

void bfin_put_pixels8_nornd (uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    ff_bfin_put_pixels8uc_nornd (block, pixels, pixels, line_size, h);
}

static void bfin_put_pixels8_x2_nornd (uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    ff_bfin_put_pixels8uc_nornd (block, pixels, pixels+1, line_size, h);
}

static void bfin_put_pixels8_y2_nornd (uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    ff_bfin_put_pixels8uc_nornd (block, pixels, pixels+line_size, line_size, h);
}


void bfin_put_pixels16_nornd (uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    ff_bfin_put_pixels16uc_nornd (block, pixels, pixels, line_size, h);
}

static void bfin_put_pixels16_x2_nornd (uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    ff_bfin_put_pixels16uc_nornd (block, pixels, pixels+1, line_size, h);
}

static void bfin_put_pixels16_y2_nornd (uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    ff_bfin_put_pixels16uc_nornd (block, pixels, pixels+line_size, line_size, h);
}

static int bfin_pix_abs16 (void *c, uint8_t *blk1, uint8_t *blk2, int line_size, int h)
{
    return ff_bfin_z_sad16x16 (blk1,blk2,line_size,line_size,h);
}

static uint8_t vtmp_blk[256] __attribute__((l1_data_B));

static int bfin_pix_abs16_x2 (void *c, uint8_t *blk1, uint8_t *blk2, int line_size, int h)
{
    ff_bfin_put_pixels16uc (vtmp_blk, blk2, blk2+1, 16, line_size, h);
    return ff_bfin_z_sad16x16 (blk1, vtmp_blk, line_size, 16, h);
}

static int bfin_pix_abs16_y2 (void *c, uint8_t *blk1, uint8_t *blk2, int line_size, int h)
{
    ff_bfin_put_pixels16uc (vtmp_blk, blk2, blk2+line_size, 16, line_size, h);
    return ff_bfin_z_sad16x16 (blk1, vtmp_blk, line_size, 16, h);
}

static int bfin_pix_abs16_xy2 (void *c, uint8_t *blk1, uint8_t *blk2, int line_size, int h)
{
    ff_bfin_z_put_pixels16_xy2 (vtmp_blk, blk2, 16, line_size, h);
    return ff_bfin_z_sad16x16 (blk1, vtmp_blk, line_size, 16, h);
}

static int bfin_pix_abs8 (void *c, uint8_t *blk1, uint8_t *blk2, int line_size, int h)
{
    return ff_bfin_z_sad8x8 (blk1,blk2,line_size,line_size, h);
}

static int bfin_pix_abs8_x2 (void *c, uint8_t *blk1, uint8_t *blk2, int line_size, int h)
{
    ff_bfin_put_pixels8uc (vtmp_blk, blk2, blk2+1, 8, line_size, h);
    return ff_bfin_z_sad8x8 (blk1, vtmp_blk, line_size, 8, h);
}

static int bfin_pix_abs8_y2 (void *c, uint8_t *blk1, uint8_t *blk2, int line_size, int h)
{
    ff_bfin_put_pixels8uc (vtmp_blk, blk2, blk2+line_size, 8, line_size, h);
    return ff_bfin_z_sad8x8 (blk1, vtmp_blk, line_size, 8, h);
}

static int bfin_pix_abs8_xy2 (void *c, uint8_t *blk1, uint8_t *blk2, int line_size, int h)
{
    ff_bfin_z_put_pixels8_xy2 (vtmp_blk, blk2, 8, line_size, h);
    return ff_bfin_z_sad8x8 (blk1, vtmp_blk, line_size, 8, h);
}


/*
  decoder optimization
  start on 2/11 100 frames of 352x240@25 compiled with no optimization -g debugging
  9.824s ~ 2.44x off
  6.360s ~ 1.58x off with -O2
  5.740s ~ 1.43x off with idcts

  2.64s    2/20 same sman.mp4 decode only

*/

void dsputil_init_bfin( DSPContext* c, AVCodecContext *avctx )
{
    c->get_pixels         = ff_bfin_get_pixels;
    c->diff_pixels        = ff_bfin_diff_pixels;
    c->put_pixels_clamped = ff_bfin_put_pixels_clamped;
    c->add_pixels_clamped = ff_bfin_add_pixels_clamped;

    c->clear_blocks       = bfin_clear_blocks;
    c->pix_sum            = ff_bfin_pix_sum;
    c->pix_norm1          = ff_bfin_pix_norm1;

    c->sad[0]             = bfin_pix_abs16;
    c->sad[1]             = bfin_pix_abs8;

    /* TODO [0] 16  [1] 8 */
    c->pix_abs[0][0] = bfin_pix_abs16;
    c->pix_abs[0][1] = bfin_pix_abs16_x2;
    c->pix_abs[0][2] = bfin_pix_abs16_y2;
    c->pix_abs[0][3] = bfin_pix_abs16_xy2;

    c->pix_abs[1][0] = bfin_pix_abs8;
    c->pix_abs[1][1] = bfin_pix_abs8_x2;
    c->pix_abs[1][2] = bfin_pix_abs8_y2;
    c->pix_abs[1][3] = bfin_pix_abs8_xy2;


    c->sse[0] = ff_bfin_sse16;
    c->sse[1] = ff_bfin_sse8;
    c->sse[2] = ff_bfin_sse4;


    /**
     * Halfpel motion compensation with rounding (a+b+1)>>1.
     * This is an array[4][4] of motion compensation functions for 4
     * horizontal blocksizes (8,16) and the 4 halfpel positions
     * *pixels_tab[ 0->16xH 1->8xH ][ xhalfpel + 2*yhalfpel ]
     * @param block destination where the result is stored
     * @param pixels source
     * @param line_size number of bytes in a horizontal line of block
     * @param h height
     */

    c->put_pixels_tab[0][0] = bfin_put_pixels16;
    c->put_pixels_tab[0][1] = bfin_put_pixels16_x2;
    c->put_pixels_tab[0][2] = bfin_put_pixels16_y2;
    c->put_pixels_tab[0][3] = bfin_put_pixels16_xy2;

    c->put_pixels_tab[1][0] = bfin_put_pixels8;
    c->put_pixels_tab[1][1] = bfin_put_pixels8_x2;
    c->put_pixels_tab[1][2] = bfin_put_pixels8_y2;
    c->put_pixels_tab[1][3] = bfin_put_pixels8_xy2;

    c->put_no_rnd_pixels_tab[1][0] = bfin_put_pixels8_nornd;
    c->put_no_rnd_pixels_tab[1][1] = bfin_put_pixels8_x2_nornd;
    c->put_no_rnd_pixels_tab[1][2] = bfin_put_pixels8_y2_nornd;
    c->put_no_rnd_pixels_tab[1][3] = ff_bfin_put_pixels8_xy2_nornd;

    c->put_no_rnd_pixels_tab[0][0] = bfin_put_pixels16_nornd;
    c->put_no_rnd_pixels_tab[0][1] = bfin_put_pixels16_x2_nornd;
    c->put_no_rnd_pixels_tab[0][2] = bfin_put_pixels16_y2_nornd;
    c->put_no_rnd_pixels_tab[0][3] = ff_bfin_put_pixels16_xy2_nornd;

    c->fdct               = ff_bfin_fdct;
    c->idct               = ff_bfin_idct;
    c->idct_add           = bfin_idct_add;
    c->idct_put           = bfin_idct_put;
}



