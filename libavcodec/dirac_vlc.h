/*
 * Copyright (C) 2016 Open Broadcast Systems Ltd.
 * Author        2016 Rostislav Pehlivanov <rpehlivanov@obe.tv>
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

#ifndef AVCODEC_DIRAC_VLC_H
#define AVCODEC_DIRAC_VLC_H

#include "libavutil/avutil.h"

/* Can be 32 bits wide for some performance gain on some machines, but it will
 * incorrectly decode very long coefficients (usually only 1 or 2 per frame) */
typedef uint64_t residual;

#define LUT_BITS 8

/* Exactly 64 bytes */
typedef struct DiracGolombLUT {
    residual preamble, leftover;
    int32_t  ready[LUT_BITS];
    int32_t  preamble_bits, leftover_bits, ready_num;
    int8_t   need_s, sign;
} DiracGolombLUT;

av_cold int ff_dirac_golomb_reader_init(DiracGolombLUT **lut_ctx);

int ff_dirac_golomb_read_32bit(DiracGolombLUT *lut_ctx, const uint8_t *buf,
                               int bytes, uint8_t *dst, int coeffs);

int ff_dirac_golomb_read_16bit(DiracGolombLUT *lut_ctx, const uint8_t *buf,
                               int bytes, uint8_t *_dst, int coeffs);

av_cold void ff_dirac_golomb_reader_end(DiracGolombLUT **lut_ctx);

#endif /* AVCODEC_DIRAC_VLC_H */
