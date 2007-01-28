/*
 * ARMv4L optimized DSP utils
 * Copyright (c) 2001 Lionel Ulmer.
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

#include "../dsputil.h"
#ifdef HAVE_IPP
#include "ipp.h"
#endif

extern void dsputil_init_iwmmxt(DSPContext* c, AVCodecContext *avctx);

extern void j_rev_dct_ARM(DCTELEM *data);
extern void simple_idct_ARM(DCTELEM *data);

extern void simple_idct_armv5te(DCTELEM *data);
extern void simple_idct_put_armv5te(uint8_t *dest, int line_size,
                                    DCTELEM *data);
extern void simple_idct_add_armv5te(uint8_t *dest, int line_size,
                                    DCTELEM *data);

extern void ff_simple_idct_armv6(DCTELEM *data);
extern void ff_simple_idct_put_armv6(uint8_t *dest, int line_size,
                                     DCTELEM *data);
extern void ff_simple_idct_add_armv6(uint8_t *dest, int line_size,
                                     DCTELEM *data);

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

CALL_2X_PIXELS(put_pixels16_x2_arm , put_pixels8_x2_arm , 8)
CALL_2X_PIXELS(put_pixels16_y2_arm , put_pixels8_y2_arm , 8)
CALL_2X_PIXELS(put_pixels16_xy2_arm, put_pixels8_xy2_arm, 8)
CALL_2X_PIXELS(put_no_rnd_pixels16_x2_arm , put_no_rnd_pixels8_x2_arm , 8)
CALL_2X_PIXELS(put_no_rnd_pixels16_y2_arm , put_no_rnd_pixels8_y2_arm , 8)
CALL_2X_PIXELS(put_no_rnd_pixels16_xy2_arm, put_no_rnd_pixels8_xy2_arm, 8)

static void add_pixels_clamped_ARM(short *block, unsigned char *dest, int line_size)
{
    asm volatile (
                  "mov r10, #8 \n\t"

                  "1: \n\t"

                  /* load dest */
                  "ldr r4, [%1] \n\t"
                  /* block[0] and block[1]*/
                  "ldrsh r5, [%0] \n\t"
                  "ldrsh r7, [%0, #2] \n\t"
                  "and r6, r4, #0xFF \n\t"
                  "and r8, r4, #0xFF00 \n\t"
                  "add r6, r5, r6 \n\t"
                  "add r8, r7, r8, lsr #8 \n\t"
                  "mvn r5, r5 \n\t"
                  "mvn r7, r7 \n\t"
                  "tst r6, #0x100 \n\t"
                  "movne r6, r5, lsr #24 \n\t"
                  "tst r8, #0x100 \n\t"
                  "movne r8, r7, lsr #24 \n\t"
                  "mov r9, r6 \n\t"
                  "ldrsh r5, [%0, #4] \n\t" /* moved form [A] */
                  "orr r9, r9, r8, lsl #8 \n\t"
                  /* block[2] and block[3] */
                  /* [A] */
                  "ldrsh r7, [%0, #6] \n\t"
                  "and r6, r4, #0xFF0000 \n\t"
                  "and r8, r4, #0xFF000000 \n\t"
                  "add r6, r5, r6, lsr #16 \n\t"
                  "add r8, r7, r8, lsr #24 \n\t"
                  "mvn r5, r5 \n\t"
                  "mvn r7, r7 \n\t"
                  "tst r6, #0x100 \n\t"
                  "movne r6, r5, lsr #24 \n\t"
                  "tst r8, #0x100 \n\t"
                  "movne r8, r7, lsr #24 \n\t"
                  "orr r9, r9, r6, lsl #16 \n\t"
                  "ldr r4, [%1, #4] \n\t"       /* moved form [B] */
                  "orr r9, r9, r8, lsl #24 \n\t"
                  /* store dest */
                  "ldrsh r5, [%0, #8] \n\t" /* moved form [C] */
                  "str r9, [%1] \n\t"

                  /* load dest */
                  /* [B] */
                  /* block[4] and block[5] */
                  /* [C] */
                  "ldrsh r7, [%0, #10] \n\t"
                  "and r6, r4, #0xFF \n\t"
                  "and r8, r4, #0xFF00 \n\t"
                  "add r6, r5, r6 \n\t"
                  "add r8, r7, r8, lsr #8 \n\t"
                  "mvn r5, r5 \n\t"
                  "mvn r7, r7 \n\t"
                  "tst r6, #0x100 \n\t"
                  "movne r6, r5, lsr #24 \n\t"
                  "tst r8, #0x100 \n\t"
                  "movne r8, r7, lsr #24 \n\t"
                  "mov r9, r6 \n\t"
                  "ldrsh r5, [%0, #12] \n\t" /* moved from [D] */
                  "orr r9, r9, r8, lsl #8 \n\t"
                  /* block[6] and block[7] */
                  /* [D] */
                  "ldrsh r7, [%0, #14] \n\t"
                  "and r6, r4, #0xFF0000 \n\t"
                  "and r8, r4, #0xFF000000 \n\t"
                  "add r6, r5, r6, lsr #16 \n\t"
                  "add r8, r7, r8, lsr #24 \n\t"
                  "mvn r5, r5 \n\t"
                  "mvn r7, r7 \n\t"
                  "tst r6, #0x100 \n\t"
                  "movne r6, r5, lsr #24 \n\t"
                  "tst r8, #0x100 \n\t"
                  "movne r8, r7, lsr #24 \n\t"
                  "orr r9, r9, r6, lsl #16 \n\t"
                  "add %0, %0, #16 \n\t" /* moved from [E] */
                  "orr r9, r9, r8, lsl #24 \n\t"
                  "subs r10, r10, #1 \n\t" /* moved from [F] */
                  /* store dest */
                  "str r9, [%1, #4] \n\t"

                  /* [E] */
                  /* [F] */
                  "add %1, %1, %2 \n\t"
                  "bne 1b \n\t"
                  : "+r"(block),
                    "+r"(dest)
                  : "r"(line_size)
                  : "r4", "r5", "r6", "r7", "r8", "r9", "r10", "cc", "memory" );
}

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

#ifdef HAVE_IPP
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
#ifdef HAVE_IWMMXT
    add_pixels_clamped_iwmmxt(block, dest, line_size);
#else
    add_pixels_clamped_ARM(block, dest, line_size);
#endif
}
#endif

void dsputil_init_armv4l(DSPContext* c, AVCodecContext *avctx)
{
    int idct_algo= avctx->idct_algo;

    ff_put_pixels_clamped = c->put_pixels_clamped;
    ff_add_pixels_clamped = c->add_pixels_clamped;

    if(idct_algo == FF_IDCT_AUTO){
#if defined(HAVE_IPP)
        idct_algo = FF_IDCT_IPP;
#elif defined(HAVE_ARMV6)
        idct_algo = FF_IDCT_SIMPLEARMV6;
#elif defined(HAVE_ARMV5TE)
        idct_algo = FF_IDCT_SIMPLEARMV5TE;
#else
        idct_algo = FF_IDCT_ARM;
#endif
    }

    if(idct_algo==FF_IDCT_ARM){
        c->idct_put= j_rev_dct_ARM_put;
        c->idct_add= j_rev_dct_ARM_add;
        c->idct    = j_rev_dct_ARM;
        c->idct_permutation_type= FF_LIBMPEG2_IDCT_PERM;/* FF_NO_IDCT_PERM */
    } else if (idct_algo==FF_IDCT_SIMPLEARM){
        c->idct_put= simple_idct_ARM_put;
        c->idct_add= simple_idct_ARM_add;
        c->idct    = simple_idct_ARM;
        c->idct_permutation_type= FF_NO_IDCT_PERM;
#ifdef HAVE_ARMV6
    } else if (idct_algo==FF_IDCT_SIMPLEARMV6){
        c->idct_put= ff_simple_idct_put_armv6;
        c->idct_add= ff_simple_idct_add_armv6;
        c->idct    = ff_simple_idct_armv6;
        c->idct_permutation_type= FF_LIBMPEG2_IDCT_PERM;
#endif
#ifdef HAVE_ARMV5TE
    } else if (idct_algo==FF_IDCT_SIMPLEARMV5TE){
        c->idct_put= simple_idct_put_armv5te;
        c->idct_add= simple_idct_add_armv5te;
        c->idct    = simple_idct_armv5te;
        c->idct_permutation_type = FF_NO_IDCT_PERM;
#endif
#ifdef HAVE_IPP
    } else if (idct_algo==FF_IDCT_IPP){
        c->idct_put= simple_idct_ipp_put;
        c->idct_add= simple_idct_ipp_add;
        c->idct    = simple_idct_ipp;
        c->idct_permutation_type= FF_NO_IDCT_PERM;
#endif
    }

/*     c->put_pixels_tab[0][0] = put_pixels16_arm; */ // NG!
    c->put_pixels_tab[0][1] = put_pixels16_x2_arm; //OK!
    c->put_pixels_tab[0][2] = put_pixels16_y2_arm; //OK!
/*     c->put_pixels_tab[0][3] = put_pixels16_xy2_arm; /\* NG *\/ */
/*     c->put_no_rnd_pixels_tab[0][0] = put_pixels16_arm; */
    c->put_no_rnd_pixels_tab[0][1] = put_no_rnd_pixels16_x2_arm; // OK
    c->put_no_rnd_pixels_tab[0][2] = put_no_rnd_pixels16_y2_arm; //OK
/*     c->put_no_rnd_pixels_tab[0][3] = put_no_rnd_pixels16_xy2_arm; //NG */
    c->put_pixels_tab[1][0] = put_pixels8_arm; //OK
    c->put_pixels_tab[1][1] = put_pixels8_x2_arm; //OK
/*     c->put_pixels_tab[1][2] = put_pixels8_y2_arm; //NG */
/*     c->put_pixels_tab[1][3] = put_pixels8_xy2_arm; //NG */
    c->put_no_rnd_pixels_tab[1][0] = put_pixels8_arm;//OK
    c->put_no_rnd_pixels_tab[1][1] = put_no_rnd_pixels8_x2_arm; //OK
    c->put_no_rnd_pixels_tab[1][2] = put_no_rnd_pixels8_y2_arm; //OK
/*     c->put_no_rnd_pixels_tab[1][3] = put_no_rnd_pixels8_xy2_arm;//NG */

#ifdef HAVE_IWMMXT
    dsputil_init_iwmmxt(c, avctx);
#endif
}
