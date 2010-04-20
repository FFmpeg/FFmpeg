/*
 * Simple IDCT
 *
 * Copyright (c) 2001 Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file
 * simple idct header.
 */

#ifndef AVCODEC_SIMPLE_IDCT_H
#define AVCODEC_SIMPLE_IDCT_H

#include <stdint.h>
#include "dsputil.h"

void ff_simple_idct_put(uint8_t *dest, int line_size, DCTELEM *block);
void ff_simple_idct_add(uint8_t *dest, int line_size, DCTELEM *block);
void ff_simple_idct_mmx(int16_t *block);
void ff_simple_idct_add_mmx(uint8_t *dest, int line_size, int16_t *block);
void ff_simple_idct_put_mmx(uint8_t *dest, int line_size, int16_t *block);
void ff_simple_idct(DCTELEM *block);

void ff_simple_idct248_put(uint8_t *dest, int line_size, DCTELEM *block);

void ff_simple_idct84_add(uint8_t *dest, int line_size, DCTELEM *block);
void ff_simple_idct48_add(uint8_t *dest, int line_size, DCTELEM *block);
void ff_simple_idct44_add(uint8_t *dest, int line_size, DCTELEM *block);

#endif /* AVCODEC_SIMPLE_IDCT_H */
