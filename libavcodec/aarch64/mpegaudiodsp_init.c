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

#include <stddef.h>
#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/aarch64/cpu.h"
#include "libavcodec/mpegaudiodsp.h"
#include "config.h"

void ff_mpadsp_apply_window_fixed_neon(int32_t *synth_buf, int32_t *window,
                                       int *dither, int16_t *samples, ptrdiff_t incr);
void ff_mpadsp_apply_window_float_neon(float *synth_buf, float *window,
                                       int *dither, float *samples, ptrdiff_t incr);

av_cold void ff_mpadsp_init_aarch64(MPADSPContext *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
        s->apply_window_fixed = ff_mpadsp_apply_window_fixed_neon;
        s->apply_window_float = ff_mpadsp_apply_window_float_neon;
    }
}
