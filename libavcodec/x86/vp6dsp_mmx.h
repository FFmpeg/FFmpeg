/*
 * vp6dsp MMX function declarations
 * Copyright (c) 2009  Sebastien Lucas <sebastien.lucas@gmail.com>
 *
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

#ifndef AVCODEC_X86_VP6DSP_MMX_H
#define AVCODEC_X86_VP6DSP_MMX_H

#include <stdint.h>

void ff_vp6_filter_diag4_mmx(uint8_t *dst, uint8_t *src, int stride,
                             const int16_t *h_weights,const int16_t *v_weights);

#endif /* AVCODEC_X86_VP6DSP_MMX_H */
