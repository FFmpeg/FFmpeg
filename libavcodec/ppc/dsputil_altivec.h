/*
 * Copyright (c) 2002 Brian Foley
 * Copyright (c) 2002 Dieter Shirley
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

extern int pix_abs16x16_x2_altivec(uint8_t *pix1, uint8_t *pix2, int line_size);
extern int pix_abs16x16_y2_altivec(uint8_t *pix1, uint8_t *pix2, int line_size);
extern int pix_abs16x16_xy2_altivec(uint8_t *pix1, uint8_t *pix2, int line_size);
extern int pix_abs16x16_altivec(uint8_t *pix1, uint8_t *pix2, int line_size);
extern int pix_abs8x8_altivec(uint8_t *pix1, uint8_t *pix2, int line_size);
extern int pix_norm1_altivec(uint8_t *pix, int line_size);
extern int pix_norm_altivec(uint8_t *pix1, uint8_t *pix2, int line_size);
extern int pix_sum_altivec(UINT8 * pix, int line_size);
extern void diff_pixels_altivec(DCTELEM* block, const UINT8* s1, const UINT8* s2, int stride);
extern void get_pixels_altivec(DCTELEM* block, const UINT8 * pixels, int line_size);

extern int has_altivec(void);
