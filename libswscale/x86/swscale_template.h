/*
 * Copyright (C) 2001-2003 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */


#ifndef SWSCALE_X86_SWSCALE_TEMPLATE_H
#define SWSCALE_X86_SWSCALE_TEMPLATE_H

DECLARE_ASM_CONST(8, uint64_t, bF8)=       0xF8F8F8F8F8F8F8F8LL;
DECLARE_ASM_CONST(8, uint64_t, bFC)=       0xFCFCFCFCFCFCFCFCLL;
DECLARE_ASM_CONST(8, uint64_t, w10)=       0x0010001000100010LL;
DECLARE_ASM_CONST(8, uint64_t, w02)=       0x0002000200020002LL;
DECLARE_ASM_CONST(8, uint64_t, bm00001111)=0x00000000FFFFFFFFLL;
DECLARE_ASM_CONST(8, uint64_t, bm00000111)=0x0000000000FFFFFFLL;
DECLARE_ASM_CONST(8, uint64_t, bm11111000)=0xFFFFFFFFFF000000LL;
DECLARE_ASM_CONST(8, uint64_t, bm01010101)=0x00FF00FF00FF00FFLL;

const DECLARE_ALIGNED(8, uint64_t, ff_dither4)[2] = {
        0x0103010301030103LL,
        0x0200020002000200LL,};

const DECLARE_ALIGNED(8, uint64_t, ff_dither8)[2] = {
        0x0602060206020602LL,
        0x0004000400040004LL,};

DECLARE_ASM_CONST(8, uint64_t, b16Mask)=   0x001F001F001F001FLL;
DECLARE_ASM_CONST(8, uint64_t, g16Mask)=   0x07E007E007E007E0LL;
DECLARE_ASM_CONST(8, uint64_t, r16Mask)=   0xF800F800F800F800LL;
DECLARE_ASM_CONST(8, uint64_t, b15Mask)=   0x001F001F001F001FLL;
DECLARE_ASM_CONST(8, uint64_t, g15Mask)=   0x03E003E003E003E0LL;
DECLARE_ASM_CONST(8, uint64_t, r15Mask)=   0x7C007C007C007C00LL;

DECLARE_ALIGNED(8, const uint64_t, ff_M24A)         = 0x00FF0000FF0000FFLL;
DECLARE_ALIGNED(8, const uint64_t, ff_M24B)         = 0xFF0000FF0000FF00LL;
DECLARE_ALIGNED(8, const uint64_t, ff_M24C)         = 0x0000FF0000FF0000LL;

#ifdef FAST_BGR2YV12
DECLARE_ALIGNED(8, const uint64_t, ff_bgr2YCoeff)   = 0x000000210041000DULL;
DECLARE_ALIGNED(8, const uint64_t, ff_bgr2UCoeff)   = 0x0000FFEEFFDC0038ULL;
DECLARE_ALIGNED(8, const uint64_t, ff_bgr2VCoeff)   = 0x00000038FFD2FFF8ULL;
#else
DECLARE_ALIGNED(8, const uint64_t, ff_bgr2YCoeff)   = 0x000020E540830C8BULL;
DECLARE_ALIGNED(8, const uint64_t, ff_bgr2UCoeff)   = 0x0000ED0FDAC23831ULL;
DECLARE_ALIGNED(8, const uint64_t, ff_bgr2VCoeff)   = 0x00003831D0E6F6EAULL;
#endif /* FAST_BGR2YV12 */
DECLARE_ALIGNED(8, const uint64_t, ff_bgr2YOffset)  = 0x1010101010101010ULL;
DECLARE_ALIGNED(8, const uint64_t, ff_bgr2UVOffset) = 0x8080808080808080ULL;
DECLARE_ALIGNED(8, const uint64_t, ff_w1111)        = 0x0001000100010001ULL;

DECLARE_ASM_CONST(8, uint64_t, ff_bgr24toY1Coeff) = 0x0C88000040870C88ULL;
DECLARE_ASM_CONST(8, uint64_t, ff_bgr24toY2Coeff) = 0x20DE4087000020DEULL;
DECLARE_ASM_CONST(8, uint64_t, ff_rgb24toY1Coeff) = 0x20DE0000408720DEULL;
DECLARE_ASM_CONST(8, uint64_t, ff_rgb24toY2Coeff) = 0x0C88408700000C88ULL;
DECLARE_ASM_CONST(8, uint64_t, ff_bgr24toYOffset) = 0x0008400000084000ULL;

DECLARE_ASM_CONST(8, uint64_t, ff_bgr24toUV)[2][4] = {
    {0x38380000DAC83838ULL, 0xECFFDAC80000ECFFULL, 0xF6E40000D0E3F6E4ULL, 0x3838D0E300003838ULL},
    {0xECFF0000DAC8ECFFULL, 0x3838DAC800003838ULL, 0x38380000D0E33838ULL, 0xF6E4D0E30000F6E4ULL},
};

DECLARE_ASM_CONST(8, uint64_t, ff_bgr24toUVOffset)= 0x0040400000404000ULL;

#endif /* SWSCALE_X86_SWSCALE_TEMPLATE_H */
