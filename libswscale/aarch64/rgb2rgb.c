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

#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/aarch64/cpu.h"
#include "libavutil/cpu.h"
#include "libavutil/bswap.h"
#include "libswscale/rgb2rgb.h"
#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"

// Only handle width aligned to 16
void ff_rgb24toyv12_neon(const uint8_t *src, uint8_t *ydst, uint8_t *udst,
                         uint8_t *vdst, int width, int height, int lumStride,
                         int chromStride, int srcStride, const int32_t *rgb2yuv);

static void rgb24toyv12(const uint8_t *src, uint8_t *ydst, uint8_t *udst,
                        uint8_t *vdst, int width, int height, int lumStride,
                        int chromStride, int srcStride, const int32_t *rgb2yuv)
{
    int width_align = width & (~15);

    if (width_align > 0)
        ff_rgb24toyv12_neon(src, ydst, udst, vdst, width_align, height,
                lumStride, chromStride, srcStride, rgb2yuv);
    if (width_align < width) {
        src += width_align * 3;
        ydst += width_align;
        udst += width_align / 2;
        vdst += width_align / 2;
        ff_rgb24toyv12_c(src, ydst, udst, vdst, width - width_align, height,
                lumStride, chromStride, srcStride, rgb2yuv);
    }
}

void ff_interleave_bytes_neon(const uint8_t *src1, const uint8_t *src2,
                              uint8_t *dest, int width, int height,
                              int src1Stride, int src2Stride, int dstStride);
void ff_deinterleave_bytes_neon(const uint8_t *src, uint8_t *dst1, uint8_t *dst2,
                                int width, int height, int srcStride,
                                int dst1Stride, int dst2Stride);

av_cold void rgb2rgb_init_aarch64(void)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
        ff_rgb24toyv12  = rgb24toyv12;
        interleaveBytes = ff_interleave_bytes_neon;
        deinterleaveBytes = ff_deinterleave_bytes_neon;
    }
}
