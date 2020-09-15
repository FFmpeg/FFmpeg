/*
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

#include "config.h"

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavfilter/convolution.h"

void ff_filter_3x3_sse4(uint8_t *dst, int width,
                        float rdiv, float bias, const int *const matrix,
                        const uint8_t *c[], int peak, int radius,
                        int dstride, int stride);

av_cold void ff_convolution_init_x86(ConvolutionContext *s)
{
#if ARCH_X86_64
    int i;
    int cpu_flags = av_get_cpu_flags();
    for (i = 0; i < 4; i++) {
        if (s->mode[i] == MATRIX_SQUARE) {
            if (s->matrix_length[i] == 9 && s->depth == 8) {
                if (EXTERNAL_SSE4(cpu_flags))
                    s->filter[i] = ff_filter_3x3_sse4;
            }
        }
    }
#endif
}
