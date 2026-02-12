/*
 * Copyright © 2026 Rodrigo Camacho.
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

#include "libavutil/cpu.h"
#include "libavfilter/scene_sad.h"

void ff_scene_sad8_rvv(SCENE_SAD_PARAMS);

void ff_scene_sad16_rvv(SCENE_SAD_PARAMS);

ff_scene_sad_fn ff_scene_sad_get_fn_riscv(int depth) {
#if HAVE_RVV && (__riscv_xlen >= 64)
    int flags = av_get_cpu_flags();

    if (flags & AV_CPU_FLAG_RVV_I64) {
        if (depth <= 8) {
            return ff_scene_sad8_rvv;
        } else if (depth <= 16) {
            return ff_scene_sad16_rvv;
        }
    }
#endif
    return NULL;
}
