/*
 * Bink DSP routines
 * Copyright (c) 2009 Kostya Shishkov
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

/**
 * @file
 * Bink DSP routines
 */

#ifndef AVCODEC_BINKDSP_H
#define AVCODEC_BINKDSP_H

#include <stdint.h>

typedef struct BinkDSPContext {
    void (*idct_put)(uint8_t *dest/*align 8*/, int line_size, int32_t *block/*align 16*/);
    void (*idct_add)(uint8_t *dest/*align 8*/, int line_size, int32_t *block/*align 16*/);
    void (*scale_block)(const uint8_t src[64]/*align 8*/, uint8_t *dst/*align 8*/, int linesize);
} BinkDSPContext;

void ff_binkdsp_init(BinkDSPContext *c);

#endif /* AVCODEC_BINKDSP_H */
