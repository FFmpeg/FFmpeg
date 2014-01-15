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

#ifndef AVCODEC_H264CHROMA_H
#define AVCODEC_H264CHROMA_H

#include <stdint.h>

typedef void (*h264_chroma_mc_func)(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int srcStride, int h, int x, int y);

typedef struct H264ChromaContext {
    h264_chroma_mc_func put_h264_chroma_pixels_tab[4];
    h264_chroma_mc_func avg_h264_chroma_pixels_tab[4];
} H264ChromaContext;

void ff_h264chroma_init(H264ChromaContext *c, int bit_depth);

void ff_h264chroma_init_aarch64(H264ChromaContext *c, int bit_depth);
void ff_h264chroma_init_arm(H264ChromaContext *c, int bit_depth);
void ff_h264chroma_init_ppc(H264ChromaContext *c, int bit_depth);
void ff_h264chroma_init_x86(H264ChromaContext *c, int bit_depth);

#endif /* AVCODEC_H264CHROMA_H */
