/*
 * Copyright (c) 2002 Brian Foley
 * Copyright (c) 2002 Dieter Shirley
 * Copyright (c) 2003-2004 Romain Dolbeau <romain@dolbeau.org>
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

#ifndef AVCODEC_PPC_DSPUTIL_ALTIVEC_H
#define AVCODEC_PPC_DSPUTIL_ALTIVEC_H

#include <stdint.h>
#include "libavcodec/dsputil.h"

void ff_put_pixels16_altivec(uint8_t *block, const uint8_t *pixels, int line_size, int h);

void ff_avg_pixels16_altivec(uint8_t *block, const uint8_t *pixels, int line_size, int h);

void ff_fdct_altivec(int16_t *block);
void ff_gmc1_altivec(uint8_t *dst, uint8_t *src, int stride, int h,
                     int x16, int y16, int rounder);
void ff_idct_put_altivec(uint8_t *dest, int line_size, int16_t *block);
void ff_idct_add_altivec(uint8_t *dest, int line_size, int16_t *block);

void ff_vp3_idct_altivec(DCTELEM *block);
void ff_vp3_idct_put_altivec(uint8_t *dest, int line_size, DCTELEM *block);
void ff_vp3_idct_add_altivec(uint8_t *dest, int line_size, DCTELEM *block);

void ff_dsputil_h264_init_ppc(DSPContext* c, AVCodecContext *avctx);

void ff_dsputil_init_altivec(DSPContext* c, AVCodecContext *avctx);
void ff_float_init_altivec(DSPContext* c, AVCodecContext *avctx);
void ff_int_init_altivec(DSPContext* c, AVCodecContext *avctx);

#endif /* AVCODEC_PPC_DSPUTIL_ALTIVEC_H */
