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

#ifndef AVCODEC_ALPHA_IDCTDSP_ALPHA_H
#define AVCODEC_ALPHA_IDCTDSP_ALPHA_H

#include <stddef.h>
#include <stdint.h>

extern void (*put_pixels_clamped_axp_p)(const int16_t *block, uint8_t *pixels,
                                        int line_size);
extern void (*add_pixels_clamped_axp_p)(const int16_t *block, uint8_t *pixels,
                                        int line_size);

void ff_simple_idct_axp(int16_t *block);
void ff_simple_idct_put_axp(uint8_t *dest, int line_size, int16_t *block);
void ff_simple_idct_add_axp(uint8_t *dest, int line_size, int16_t *block);

#endif /* AVCODEC_ALPHA_IDCTDSP_ALPHA_H */
