/*
 * S3 Texture Compression (S3TC) decoding functions
 * Copyright (c) 2007 by Ivo van Poorten
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

#ifndef AVCODEC_S3TC_H
#define AVCODEC_S3TC_H

#include <stdint.h>

#define FF_S3TC_DXT1    0x31545844
#define FF_S3TC_DXT3    0x33545844

/**
 * Decode DXT1 encoded data to RGB32
 * @param src source buffer, has to be aligned on a 4-byte boundary
 * @param dst destination buffer
 * @param w width of output image
 * @param h height of output image
 * @param stride line size of output image
 */
void ff_decode_dxt1(const uint8_t *src, uint8_t *dst,
                    const unsigned int w, const unsigned int h,
                    const unsigned int stride);
/**
 * Decode DXT3 encoded data to RGB32
 * @param src source buffer, has to be aligned on a 4-byte boundary
 * @param dst destination buffer
 * @param w width of output image
 * @param h height of output image
 * @param stride line size of output image
 */
void ff_decode_dxt3(const uint8_t *src, uint8_t *dst,
                    const unsigned int w, const unsigned int h,
                    const unsigned int stride);

#endif /* AVCODEC_S3TC_H */
