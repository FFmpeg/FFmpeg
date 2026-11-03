/*
 * Copyright (c) 2026 Patryk Darmofalski <darpat039@gmail.com>
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
#include "libavutil/cpu.h"
#include "libavutil/aarch64/cpu.h"
#include "libavcodec/audiodsp.h"

void ff_vector_clip_int32_neon(int32_t *dst, const int32_t *src, int32_t min,
                               int32_t max, unsigned int len);
void ff_vector_clipf_neon(float *dst, const float *src, int len, float min,
                          float max);

av_cold void ff_audiodsp_init_aarch64(AudioDSPContext *c)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
        c->vector_clip_int32 = ff_vector_clip_int32_neon;
        c->vector_clipf      = ff_vector_clipf_neon;
    }
}
