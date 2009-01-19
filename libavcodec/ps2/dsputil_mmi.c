/*
 * MMI optimized DSP utils
 * Copyright (c) 2000, 2001 Fabrice Bellard
 *
 * MMI optimization by Leon van Stuivenberg
 * clear_blocks_mmi() by BroadQ
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
#include "mmi.h"

void ff_mmi_idct_put(uint8_t *dest, int line_size, DCTELEM *block);
void ff_mmi_idct_add(uint8_t *dest, int line_size, DCTELEM *block);
void ff_mmi_idct(DCTELEM *block);

static void clear_blocks_mmi(DCTELEM * blocks)
{
        __asm__ volatile(
        ".set noreorder    \n"
        "addiu $9, %0, 768 \n"
        "nop               \n"
        "1:                \n"
        "sq $0, 0(%0)      \n"
        "move $8, %0       \n"
        "addi %0, %0, 64   \n"
        "sq $0, 16($8)     \n"
        "slt $10, %0, $9   \n"
        "sq $0, 32($8)     \n"
        "bnez $10, 1b      \n"
        "sq $0, 48($8)     \n"
        ".set reorder      \n"
        : "+r" (blocks) ::  "$8", "$9", "memory" );
}


static void get_pixels_mmi(DCTELEM *block, const uint8_t *pixels, int line_size)
{
        __asm__ volatile(
        ".set   push            \n\t"
        ".set   mips3           \n\t"
        "ld     $8, 0(%0)       \n\t"
        "add    %0, %0, %2      \n\t"
        "ld     $9, 0(%0)       \n\t"
        "add    %0, %0, %2      \n\t"
        "ld     $10, 0(%0)      \n\t"
        "pextlb $8, $0, $8      \n\t"
        "sq     $8, 0(%1)       \n\t"
        "add    %0, %0, %2      \n\t"
        "ld     $8, 0(%0)       \n\t"
        "pextlb $9, $0, $9      \n\t"
        "sq     $9, 16(%1)      \n\t"
        "add    %0, %0, %2      \n\t"
        "ld     $9, 0(%0)       \n\t"
        "pextlb $10, $0, $10    \n\t"
        "sq     $10, 32(%1)     \n\t"
        "add    %0, %0, %2      \n\t"
        "ld     $10, 0(%0)      \n\t"
        "pextlb $8, $0, $8      \n\t"
        "sq     $8, 48(%1)      \n\t"
        "add    %0, %0, %2      \n\t"
        "ld     $8, 0(%0)       \n\t"
        "pextlb $9, $0, $9      \n\t"
        "sq     $9, 64(%1)      \n\t"
        "add    %0, %0, %2      \n\t"
        "ld     $9, 0(%0)       \n\t"
        "pextlb $10, $0, $10    \n\t"
        "sq     $10, 80(%1)     \n\t"
        "pextlb $8, $0, $8      \n\t"
        "sq     $8, 96(%1)      \n\t"
        "pextlb $9, $0, $9      \n\t"
        "sq     $9, 112(%1)     \n\t"
        ".set   pop             \n\t"
        : "+r" (pixels) : "r" (block), "r" (line_size) : "$8", "$9", "$10", "memory" );
}


static void put_pixels8_mmi(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
        __asm__ volatile(
        ".set   push            \n\t"
        ".set   mips3           \n\t"
        "1:                     \n\t"
        "ldr    $8, 0(%1)       \n\t"
        "addiu  %2, %2, -1      \n\t"
        "ldl    $8, 7(%1)       \n\t"
        "add    %1, %1, %3      \n\t"
        "sd     $8, 0(%0)       \n\t"
        "add    %0, %0, %3      \n\t"
        "bgtz   %2, 1b          \n\t"
        ".set   pop             \n\t"
        : "+r" (block), "+r" (pixels), "+r" (h) : "r" (line_size)
        : "$8", "memory" );
}


static void put_pixels16_mmi(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
        __asm__ volatile (
        ".set   push            \n\t"
        ".set   mips3           \n\t"
        "1:                     \n\t"
        "ldr    $8, 0(%1)       \n\t"
        "add    $11, %1, %3     \n\t"
        "ldl    $8, 7(%1)       \n\t"
        "add    $10, %0, %3     \n\t"
        "ldr    $9, 8(%1)       \n\t"
        "ldl    $9, 15(%1)      \n\t"
        "ldr    $12, 0($11)     \n\t"
        "add    %1, $11, %3     \n\t"
        "ldl    $12, 7($11)     \n\t"
        "pcpyld $8, $9, $8      \n\t"
        "sq     $8, 0(%0)       \n\t"
        "ldr    $13, 8($11)     \n\t"
        "addiu  %2, %2, -2      \n\t"
        "ldl    $13, 15($11)    \n\t"
        "add    %0, $10, %3     \n\t"
        "pcpyld $12, $13, $12   \n\t"
        "sq     $12, 0($10)     \n\t"
        "bgtz   %2, 1b          \n\t"
        ".set   pop             \n\t"
        : "+r" (block), "+r" (pixels), "+r" (h) : "r" (line_size)
        : "$8", "$9", "$10", "$11", "$12", "$13", "memory" );
}


void dsputil_init_mmi(DSPContext* c, AVCodecContext *avctx)
{
    const int idct_algo= avctx->idct_algo;

    c->clear_blocks = clear_blocks_mmi;

    c->put_pixels_tab[1][0] = put_pixels8_mmi;
    c->put_no_rnd_pixels_tab[1][0] = put_pixels8_mmi;

    c->put_pixels_tab[0][0] = put_pixels16_mmi;
    c->put_no_rnd_pixels_tab[0][0] = put_pixels16_mmi;

    c->get_pixels = get_pixels_mmi;

    if(idct_algo==FF_IDCT_AUTO || idct_algo==FF_IDCT_PS2){
        c->idct_put= ff_mmi_idct_put;
        c->idct_add= ff_mmi_idct_add;
        c->idct    = ff_mmi_idct;
        c->idct_permutation_type= FF_LIBMPEG2_IDCT_PERM;
    }
}

