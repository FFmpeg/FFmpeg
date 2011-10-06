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

#ifndef AVCODEC_X86_DWT_H
#define AVCODEC_X86_DWT_H

#include "libavcodec/dwt.h"

void ff_horizontal_compose_dd97i_end_c(IDWTELEM *b, IDWTELEM *tmp, int w2, int x);
void ff_horizontal_compose_haar1i_end_c(IDWTELEM *b, IDWTELEM *tmp, int w2, int x);
void ff_horizontal_compose_haar0i_end_c(IDWTELEM *b, IDWTELEM *tmp, int w2, int x);

void ff_spatial_idwt_init_mmx(DWTContext *d, enum dwt_type type);

#endif
