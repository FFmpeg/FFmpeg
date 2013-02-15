/*
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


#ifndef AVCODEC_BFIN_VP3_BFIN_H
#define AVCODEC_BFIN_VP3_BFIN_H

#include <stdint.h>

void ff_bfin_vp3_idct(int16_t *block);
void ff_bfin_vp3_idct_put(uint8_t *dest, int line_size, int16_t *block);
void ff_bfin_vp3_idct_add(uint8_t *dest, int line_size, int16_t *block);

#endif /* AVCODEC_BFIN_VP3_BFIN_H */
