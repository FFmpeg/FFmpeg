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

#ifndef AVCODEC_X86_QPEL_H
#define AVCODEC_X86_QPEL_H

#include <stddef.h>
#include <stdint.h>

void ff_put_pixels8x8_l2_mmxext(uint8_t *dst,
                                const uint8_t *src1, const uint8_t *src2,
                                ptrdiff_t dstStride, ptrdiff_t src1Stride);
void ff_avg_pixels8x8_l2_mmxext(uint8_t *dst,
                                const uint8_t *src1, const uint8_t *src2,
                                ptrdiff_t dstStride, ptrdiff_t src1Stride);
void ff_put_pixels16x16_l2_mmxext(uint8_t *dst,
                                  const uint8_t *src1, const uint8_t *src2,
                                  ptrdiff_t dstStride, ptrdiff_t src1Stride);
void ff_put_pixels16x16_l2_sse2(uint8_t *dst,
                                const uint8_t *src1, const uint8_t *src2,
                                ptrdiff_t dstStride, ptrdiff_t src1Stride);
void ff_avg_pixels16x16_l2_mmxext(uint8_t *dst,
                                  const uint8_t *src1, const uint8_t *src2,
                                  ptrdiff_t dstStride, ptrdiff_t src1Stride);
void ff_avg_pixels16x16_l2_sse2(uint8_t *dst,
                                const uint8_t *src1, const uint8_t *src2,
                                ptrdiff_t dstStride, ptrdiff_t src1Stride);

#endif /* AVCODEC_X86_QPEL_H */
