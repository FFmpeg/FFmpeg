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

#ifndef AVCODEC_SVQ3_H
#define AVCODEC_SVQ3_H

#include <stdint.h>

void ff_svq3_luma_dc_dequant_idct_c(int16_t *output, int16_t *input, int qp);
void ff_svq3_add_idct_c(uint8_t *dst, int16_t *block, int stride, int qp, int dc);

#endif /* AVCODEC_DSPUTIL_H */
