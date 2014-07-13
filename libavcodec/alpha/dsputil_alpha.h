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

int pix_abs8x8_mvi(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h);
int pix_abs16x16_mvi_asm(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h);
int pix_abs16x16_x2_mvi(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h);
int pix_abs16x16_y2_mvi(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h);
int pix_abs16x16_xy2_mvi(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h);


#endif /* AVCODEC_ALPHA_DSPUTIL_ALPHA_H */
