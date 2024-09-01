/*
 * Copyright © 2022 Rémi Denis-Courmont.
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

#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libswscale/rgb2rgb.h"

void ff_shuffle_bytes_0321_rvv(const uint8_t *src, uint8_t *dst, int src_len);
void ff_shuffle_bytes_2103_rvv(const uint8_t *src, uint8_t *dst, int src_len);
void ff_shuffle_bytes_1230_rvv(const uint8_t *src, uint8_t *dst, int src_len);
void ff_shuffle_bytes_3012_rvv(const uint8_t *src, uint8_t *dst, int src_len);
void ff_shuffle_bytes_3210_rvb(const uint8_t *src, uint8_t *dst, int src_len);
void ff_interleave_bytes_rvv(const uint8_t *src1, const uint8_t *src2,
                             uint8_t *dst, int width, int height, int s1stride,
                             int s2stride, int dstride);
void ff_deinterleave_bytes_rvv(const uint8_t *src, uint8_t *dst1,
                               uint8_t *dst2, int width, int height,
                               int srcStride, int dst1Stride, int dst2Stride);
void ff_uyvytoyuv422_rvv(uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
                         const uint8_t *src, int width, int height,
                         int ystride, int uvstride, int src_stride);
void ff_yuyvtoyuv422_rvv(uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
                         const uint8_t *src, int width, int height,
                         int ystride, int uvstride, int src_stride);

av_cold void rgb2rgb_init_riscv(void)
{
#if HAVE_RV
    int flags = av_get_cpu_flags();

#if (__riscv_xlen == 64)
    if (flags & AV_CPU_FLAG_RVB_BASIC)
        shuffle_bytes_3210 = ff_shuffle_bytes_3210_rvb;
#endif
#if HAVE_RVV
    if ((flags & AV_CPU_FLAG_RVV_I32) && (flags & AV_CPU_FLAG_RVB)) {
        shuffle_bytes_0321 = ff_shuffle_bytes_0321_rvv;
        shuffle_bytes_2103 = ff_shuffle_bytes_2103_rvv;
        shuffle_bytes_1230 = ff_shuffle_bytes_1230_rvv;
        shuffle_bytes_3012 = ff_shuffle_bytes_3012_rvv;
        interleaveBytes = ff_interleave_bytes_rvv;
        deinterleaveBytes = ff_deinterleave_bytes_rvv;
        uyvytoyuv422 = ff_uyvytoyuv422_rvv;
        yuyvtoyuv422 = ff_yuyvtoyuv422_rvv;
    }
#endif
#endif
}
