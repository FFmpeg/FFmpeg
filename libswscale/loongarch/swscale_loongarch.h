/*
 * Copyright (C) 2022 Loongson Technology Corporation Limited
 * Contributed by Hao Chen(chenhao@loongson.cn)
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

#ifndef SWSCALE_LOONGARCH_SWSCALE_LOONGARCH_H
#define SWSCALE_LOONGARCH_SWSCALE_LOONGARCH_H

#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"

void ff_hscale_8_to_15_lasx(SwsContext *c, int16_t *dst, int dstW,
                            const uint8_t *src, const int16_t *filter,
                            const int32_t *filterPos, int filterSize);

void ff_hscale_8_to_19_lasx(SwsContext *c, int16_t *_dst, int dstW,
                            const uint8_t *src, const int16_t *filter,
                            const int32_t *filterPos, int filterSize);

void ff_hscale_16_to_19_lasx(SwsContext *c, int16_t *_dst, int dstW,
                             const uint8_t *_src, const int16_t *filter,
                             const int32_t *filterPos, int filterSize);

void ff_hscale_16_to_15_lasx(SwsContext *c, int16_t *dst, int dstW,
                             const uint8_t *_src, const int16_t *filter,
                             const int32_t *filterPos, int filterSize);

void planar_rgb_to_uv_lasx(uint8_t *_dstU, uint8_t *_dstV, const uint8_t *src[4],
                           int width, int32_t *rgb2yuv, void *opq);

void planar_rgb_to_y_lasx(uint8_t *_dst, const uint8_t *src[4], int width,
                          int32_t *rgb2yuv, void *opq);

#endif /* SWSCALE_LOONGARCH_SWSCALE_LOONGARCH_H */
