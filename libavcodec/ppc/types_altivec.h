/*
 * Copyright (c) 2006 Guillaume Poirier <gpoirier@mplayerhq.hu>
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

#ifndef AVCODEC_PPC_TYPES_ALTIVEC_H
#define AVCODEC_PPC_TYPES_ALTIVEC_H

/***********************************************************************
 * Vector types
 **********************************************************************/
#define vec_u8  vector unsigned char
#define vec_s8  vector signed char
#define vec_u16 vector unsigned short
#define vec_s16 vector signed short
#define vec_u32 vector unsigned int
#define vec_s32 vector signed int
#define vec_f   vector float

/***********************************************************************
 * Null vector
 **********************************************************************/
#define LOAD_ZERO const vec_u8 zerov = vec_splat_u8( 0 )

#define zero_u8v  (vec_u8)  zerov
#define zero_s8v  (vec_s8)  zerov
#define zero_u16v (vec_u16) zerov
#define zero_s16v (vec_s16) zerov
#define zero_u32v (vec_u32) zerov
#define zero_s32v (vec_s32) zerov

#endif /* AVCODEC_PPC_TYPES_ALTIVEC_H */
