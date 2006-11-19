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

/***********************************************************************
 * Vector types
 **********************************************************************/
#define vec_u8_t  vector unsigned char
#define vec_s8_t  vector signed char
#define vec_u16_t vector unsigned short
#define vec_s16_t vector signed short
#define vec_u32_t vector unsigned int
#define vec_s32_t vector signed int

/***********************************************************************
 * Null vector
 **********************************************************************/
#define LOAD_ZERO const vec_u8_t zerov = vec_splat_u8( 0 )

#define zero_u8v  (vec_u8_t)  zerov
#define zero_s8v  (vec_s8_t)  zerov
#define zero_u16v (vec_u16_t) zerov
#define zero_s16v (vec_s16_t) zerov
#define zero_u32v (vec_u32_t) zerov
#define zero_s32v (vec_s32_t) zerov
