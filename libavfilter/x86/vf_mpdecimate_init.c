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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavfilter/mpdecimate.h"

int ff_mpdecimate_sad_8x8_16_sse2(const uint8_t *src1, ptrdiff_t stride1,
                                  const uint8_t *src2, ptrdiff_t stride2);

av_cold av_pixelutils_sad_fn ff_mpdecimate_get_sad16_fn_x86(void)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE2(cpu_flags))
        return ff_mpdecimate_sad_8x8_16_sse2;

    return NULL;
}
