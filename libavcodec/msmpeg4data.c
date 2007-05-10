/*
 * MSMPEG4 backend for ffmpeg encoder and decoder
 * copyright (c) 2001 Fabrice Bellard
 * copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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
 *
 * msmpeg4v1 & v2 stuff by Michael Niedermayer <michaelni@gmx.at>
 */

/**
 * @file msmpeg4data.c
 * MSMPEG4 data tables.
 */

#include "msmpeg4data.h"

VLC ff_msmp4_mb_i_vlc;
VLC ff_msmp4_dc_luma_vlc[2];
VLC ff_msmp4_dc_chroma_vlc[2];

/* intra picture macro block coded block pattern */
const uint16_t ff_msmp4_mb_i_table[64][2] = {
{ 0x1, 1 },{ 0x17, 6 },{ 0x9, 5 },{ 0x5, 5 },
{ 0x6, 5 },{ 0x47, 9 },{ 0x20, 7 },{ 0x10, 7 },
{ 0x2, 5 },{ 0x7c, 9 },{ 0x3a, 7 },{ 0x1d, 7 },
{ 0x2, 6 },{ 0xec, 9 },{ 0x77, 8 },{ 0x0, 8 },
{ 0x3, 5 },{ 0xb7, 9 },{ 0x2c, 7 },{ 0x13, 7 },
{ 0x1, 6 },{ 0x168, 10 },{ 0x46, 8 },{ 0x3f, 8 },
{ 0x1e, 6 },{ 0x712, 13 },{ 0xb5, 9 },{ 0x42, 8 },
{ 0x22, 7 },{ 0x1c5, 11 },{ 0x11e, 10 },{ 0x87, 9 },
{ 0x6, 4 },{ 0x3, 9 },{ 0x1e, 7 },{ 0x1c, 6 },
{ 0x12, 7 },{ 0x388, 12 },{ 0x44, 9 },{ 0x70, 9 },
{ 0x1f, 6 },{ 0x23e, 11 },{ 0x39, 8 },{ 0x8e, 9 },
{ 0x1, 7 },{ 0x1c6, 11 },{ 0xb6, 9 },{ 0x45, 9 },
{ 0x14, 6 },{ 0x23f, 11 },{ 0x7d, 9 },{ 0x18, 9 },
{ 0x7, 7 },{ 0x1c7, 11 },{ 0x86, 9 },{ 0x19, 9 },
{ 0x15, 6 },{ 0x1db, 10 },{ 0x2, 9 },{ 0x46, 9 },
{ 0xd, 8 },{ 0x713, 13 },{ 0x1da, 10 },{ 0x169, 10 },
};
