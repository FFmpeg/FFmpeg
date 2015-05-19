/*
 * Texture block module
 * Copyright (C) 2015 Vittorio Giovara <vittorio.giovara@gmail.com>
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
 * Texture block (4x4) module
 *
 * References:
 *   https://www.opengl.org/wiki/S3_Texture_Compression
 *   https://www.opengl.org/wiki/Red_Green_Texture_Compression
 *   https://msdn.microsoft.com/en-us/library/bb694531%28v=vs.85%29.aspx
 *
 * All functions return how much data has been written or read.
 *
 * Pixel input or output format is always AV_PIX_FMT_RGBA.
 */

#ifndef AVCODEC_TEXTUREDSP_H
#define AVCODEC_TEXTUREDSP_H

#include <stddef.h>
#include <stdint.h>

#define TEXTURE_BLOCK_W 4
#define TEXTURE_BLOCK_H 4

typedef struct TextureDSPContext {
    int (*dxt1_block)  (uint8_t *dst, ptrdiff_t stride, const uint8_t *block);
    int (*dxt1a_block) (uint8_t *dst, ptrdiff_t stride, const uint8_t *block);
    int (*dxt2_block)  (uint8_t *dst, ptrdiff_t stride, const uint8_t *block);
    int (*dxt3_block)  (uint8_t *dst, ptrdiff_t stride, const uint8_t *block);
    int (*dxt4_block)  (uint8_t *dst, ptrdiff_t stride, const uint8_t *block);
    int (*dxt5_block)  (uint8_t *dst, ptrdiff_t stride, const uint8_t *block);
    int (*dxt5y_block) (uint8_t *dst, ptrdiff_t stride, const uint8_t *block);
    int (*dxt5ys_block)(uint8_t *dst, ptrdiff_t stride, const uint8_t *block);
    int (*rgtc1s_block)(uint8_t *dst, ptrdiff_t stride, const uint8_t *block);
    int (*rgtc1u_block)(uint8_t *dst, ptrdiff_t stride, const uint8_t *block);
    int (*rgtc2s_block)(uint8_t *dst, ptrdiff_t stride, const uint8_t *block);
    int (*rgtc2u_block)(uint8_t *dst, ptrdiff_t stride, const uint8_t *block);
    int (*dxn3dc_block)(uint8_t *dst, ptrdiff_t stride, const uint8_t *block);
} TextureDSPContext;

void ff_texturedsp_init(TextureDSPContext *c);
void ff_texturedspenc_init(TextureDSPContext *c);

#endif /* AVCODEC_TEXTUREDSP_H */
