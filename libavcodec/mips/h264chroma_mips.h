/*
 * Copyright (c) 2015 Zhou Xiaoyong <zhouxiaoyong@loongson.cn>
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

#ifndef H264_CHROMA_MIPS_H
#define H264_CHROMA_MIPS_H

#include "libavcodec/h264.h"
void ff_put_h264_chroma_mc8_msa(uint8_t *dst, uint8_t *src, int stride,
                                int height, int x, int y);
void ff_put_h264_chroma_mc4_msa(uint8_t *dst, uint8_t *src, int stride,
                                int height, int x, int y);
void ff_put_h264_chroma_mc2_msa(uint8_t *dst, uint8_t *src, int stride,
                                int height, int x, int y);
void ff_avg_h264_chroma_mc8_msa(uint8_t *dst, uint8_t *src, int stride,
                                int height, int x, int y);
void ff_avg_h264_chroma_mc4_msa(uint8_t *dst, uint8_t *src, int stride,
                                int height, int x, int y);
void ff_avg_h264_chroma_mc2_msa(uint8_t *dst, uint8_t *src, int stride,
                                int height, int x, int y);

void ff_put_h264_chroma_mc8_mmi(uint8_t *dst, uint8_t *src, int stride,
        int h, int x, int y);
void ff_avg_h264_chroma_mc8_mmi(uint8_t *dst, uint8_t *src, int stride,
        int h, int x, int y);
void ff_put_h264_chroma_mc4_mmi(uint8_t *dst, uint8_t *src, int stride,
        int h, int x, int y);
void ff_avg_h264_chroma_mc4_mmi(uint8_t *dst, uint8_t *src, int stride,
        int h, int x, int y);

#endif /* H264_CHROMA_MIPS_H */
