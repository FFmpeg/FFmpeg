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

#ifndef AVCODEC_ALPHA_DSPUTIL_ALPHA_H
#define AVCODEC_ALPHA_DSPUTIL_ALPHA_H

#include <stddef.h>
#include <stdint.h>

void ff_simple_idct_axp(int16_t *block);
void ff_simple_idct_put_axp(uint8_t *dest, int line_size, int16_t *block);
void ff_simple_idct_add_axp(uint8_t *dest, int line_size, int16_t *block);

void put_pixels_clamped_mvi_asm(const int16_t *block, uint8_t *pixels,
                                int line_size);
void add_pixels_clamped_mvi_asm(const int16_t *block, uint8_t *pixels,
                                int line_size);
extern void (*put_pixels_clamped_axp_p)(const int16_t *block, uint8_t *pixels,
                                        int line_size);
extern void (*add_pixels_clamped_axp_p)(const int16_t *block, uint8_t *pixels,
                                        int line_size);

void get_pixels_mvi(int16_t *restrict block,
                    const uint8_t *restrict pixels, int line_size);
void diff_pixels_mvi(int16_t *block, const uint8_t *s1, const uint8_t *s2,
                     int stride);
int pix_abs8x8_mvi(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h);
int pix_abs16x16_mvi_asm(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h);
int pix_abs16x16_x2_mvi(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h);
int pix_abs16x16_y2_mvi(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h);
int pix_abs16x16_xy2_mvi(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h);


#endif /* AVCODEC_ALPHA_DSPUTIL_ALPHA_H */
