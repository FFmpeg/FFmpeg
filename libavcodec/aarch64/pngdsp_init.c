/*
 * Copyright (c) 2026 Zhao Zhili <zhilizhao@tencent.com>
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

#include "libavutil/attributes.h"
#include "libavutil/aarch64/cpu.h"
#include "libavcodec/pngdsp.h"

void ff_png_add_bytes_l2_neon(uint8_t *dst, const uint8_t *src1,
                              const uint8_t *src2, int w);
void ff_png_add_paeth_prediction_neon(uint8_t *dst, const uint8_t *src,
                                      const uint8_t *top, int w, int bpp);

av_cold void ff_pngdsp_init_aarch64(PNGDSPContext *dsp)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
        dsp->add_bytes_l2         = ff_png_add_bytes_l2_neon;
        dsp->add_paeth_prediction = ff_png_add_paeth_prediction_neon;
    }
}
