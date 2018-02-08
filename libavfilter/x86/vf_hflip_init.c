/*
 * Copyright (c) 2017 Paul B Mahol
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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavfilter/hflip.h"

void ff_hflip_byte_ssse3(const uint8_t *src, uint8_t *dst, int w);
void ff_hflip_byte_avx2(const uint8_t *src, uint8_t *dst, int w);
void ff_hflip_short_ssse3(const uint8_t *src, uint8_t *dst, int w);
void ff_hflip_short_avx2(const uint8_t *src, uint8_t *dst, int w);

av_cold void ff_hflip_init_x86(FlipContext *s, int step[4], int nb_planes)
{
    int cpu_flags = av_get_cpu_flags();
    int i;

    for (i = 0; i < nb_planes; i++) {
        if (step[i] == 1) {
            if (EXTERNAL_SSSE3(cpu_flags)) {
                s->flip_line[i] = ff_hflip_byte_ssse3;
            }
            if (EXTERNAL_AVX2_FAST(cpu_flags)) {
                s->flip_line[i] = ff_hflip_byte_avx2;
            }
        } else if (step[i] == 2) {
            if (EXTERNAL_SSSE3(cpu_flags)) {
                s->flip_line[i] = ff_hflip_short_ssse3;
            }
            if (EXTERNAL_AVX2_FAST(cpu_flags)) {
                s->flip_line[i] = ff_hflip_short_avx2;
            }
        }
    }
}
