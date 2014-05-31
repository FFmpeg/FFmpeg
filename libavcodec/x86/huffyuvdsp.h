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

#ifndef AVCODEC_X86_HUFFYUVDSP_H
#define AVCODEC_X86_HUFFYUVDSP_H

#include <stdint.h>

void ff_add_hfyu_median_pred_cmov(uint8_t *dst, const uint8_t *top,
                                  const uint8_t *diff, intptr_t w,
                                  int *left, int *left_top);

#endif /* AVCODEC_X86_HUFFYUVDSP_H */
