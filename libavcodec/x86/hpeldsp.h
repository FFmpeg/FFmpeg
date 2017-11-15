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

#ifndef AVCODEC_X86_HPELDSP_H
#define AVCODEC_X86_HPELDSP_H

#include <stddef.h>
#include <stdint.h>

#include "libavcodec/hpeldsp.h"

void ff_avg_pixels8_x2_mmx(uint8_t *block, const uint8_t *pixels,
                           ptrdiff_t line_size, int h);

void ff_avg_pixels8_xy2_mmx(uint8_t *block, const uint8_t *pixels,
                            ptrdiff_t line_size, int h);
void ff_avg_pixels8_xy2_mmxext(uint8_t *block, const uint8_t *pixels,
                               ptrdiff_t line_size, int h);
void ff_avg_pixels8_xy2_ssse3(uint8_t *block, const uint8_t *pixels,
                               ptrdiff_t line_size, int h);

void ff_avg_pixels16_xy2_mmx(uint8_t *block, const uint8_t *pixels,
                             ptrdiff_t line_size, int h);
void ff_avg_pixels16_xy2_sse2(uint8_t *block, const uint8_t *pixels,
                              ptrdiff_t line_size, int h);
void ff_avg_pixels16_xy2_ssse3(uint8_t *block, const uint8_t *pixels,
                               ptrdiff_t line_size, int h);

void ff_put_pixels8_xy2_mmx(uint8_t *block, const uint8_t *pixels,
                            ptrdiff_t line_size, int h);
void ff_put_pixels8_xy2_ssse3(uint8_t *block, const uint8_t *pixels,
                              ptrdiff_t line_size, int h);
void ff_put_pixels16_xy2_mmx(uint8_t *block, const uint8_t *pixels,
                             ptrdiff_t line_size, int h);
void ff_put_pixels16_xy2_sse2(uint8_t *block, const uint8_t *pixels,
                              ptrdiff_t line_size, int h);
void ff_put_pixels16_xy2_ssse3(uint8_t *block, const uint8_t *pixels,
                               ptrdiff_t line_size, int h);

void ff_hpeldsp_vp3_init_x86(HpelDSPContext *c, int cpu_flags, int flags);

#endif /* AVCODEC_X86_HPELDSP_H */
