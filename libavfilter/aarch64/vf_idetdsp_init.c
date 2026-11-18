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

#include "config.h"

#include "libavutil/attributes.h"
#include "libavutil/aarch64/cpu.h"
#include "libavfilter/vf_idetdsp.h"

int ff_idet_filter_line_neon(const uint8_t *a, const uint8_t *b,
                             const uint8_t *c, int w);
int ff_idet_filter_line_15bit_neon(const uint8_t *a, const uint8_t *b,
                                   const uint8_t *c, int w);

av_cold void ff_idet_dsp_init_aarch64(IDETDSPContext *dsp, int depth)
{
    int cpu_flags = av_get_cpu_flags();

    if (!have_neon(cpu_flags))
        return;

    /* 16-bit samples need 32-bit lanes for a + c and 2 * b, which ran no
     * faster than the C loop, so they stay on it. */
    if (depth <= 8)
        dsp->filter_line = ff_idet_filter_line_neon;
    else if (depth <= 15)
        dsp->filter_line = ff_idet_filter_line_15bit_neon;
}
