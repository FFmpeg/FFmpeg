/*
 * Copyright (c) 2006 Michael Benjamin
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

#include "../avcodec.h"
#include "../dsputil.h"

static int sad8x8_bfin( void *c, uint8_t *blk1, uint8_t *blk2, int line_size, int h )
{
    int sum;
    __asm__ __volatile__ (
    "P0 = %1;" // blk1
    "P1 = %2;" // blk2
    "P2 = %3;\n" // h
    "I0 = P0;"
    "I1 = P1;\n"
    "A0 = 0;"
    "A1 = 0;\n"
    "M0 = P2;\n"
    "P3 = 32;\n"
    "LSETUP (sad8x8LoopBegin, sad8x8LoopEnd) LC0=P3;\n"
    "sad8x8LoopBegin:\n"
    "  DISALGNEXCPT || R0 = [I0] || R2 = [I1];\n"
    "  DISALGNEXCPT || R1 = [I0++] || R3 = [I1++];\n"
    "sad8x8LoopEnd:\n"
    "  SAA ( R1:0 , R3:2 );\n"
    "R3 = A1.L + A1.H, R2 = A0.L + A0.H;\n"
    "%0 = R2 + R3 (S);\n"
    : "=&d" (sum)
    : "m"(blk1), "m"(blk2), "m"(h)
    : "P0","P1","P2","I0","I1","A0","A1","R0","R1","R2","R3");
    return sum;
}

void dsputil_init_bfin( DSPContext* c, AVCodecContext *avctx )
{
    c->pix_abs[1][0] = sad8x8_bfin;
    c->sad[1] = sad8x8_bfin;
}
