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
 *
 */

#ifndef FF_S3TC_H
#define FF_S3TC_H

#define FF_S3TC_DXT1    0x31545844
#define FF_S3TC_DXT3    0x33545844

void ff_decode_dxt1(const uint8_t *src, uint8_t *dst,
                    const unsigned int w, const unsigned int h,
                    const unsigned int stride);
void ff_decode_dxt3(const uint8_t *src, uint8_t *dst,
                    const unsigned int w, const unsigned int h,
                    const unsigned int stride);

#endif
