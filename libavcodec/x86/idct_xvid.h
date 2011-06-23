/*
 * XVID MPEG-4 VIDEO CODEC
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

/**
 * @file
 * header for Xvid IDCT functions
 */

#ifndef AVCODEC_X86_IDCT_XVID_H
#define AVCODEC_X86_IDCT_XVID_H

#include <stdint.h>

void ff_idct_xvid_mmx(short *block);
void ff_idct_xvid_mmx2(short *block);
void ff_idct_xvid_sse2(short *block);
void ff_idct_xvid_sse2_put(uint8_t *dest, int line_size, short *block);
void ff_idct_xvid_sse2_add(uint8_t *dest, int line_size, short *block);

#endif /* AVCODEC_X86_IDCT_XVID_H */
