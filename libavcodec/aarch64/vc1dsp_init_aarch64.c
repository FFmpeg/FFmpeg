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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/aarch64/cpu.h"
#include "libavcodec/vc1dsp.h"

#include "config.h"

void ff_put_vc1_chroma_mc8_neon(uint8_t *dst, uint8_t *src, int stride, int h,
                                int x, int y);
void ff_avg_vc1_chroma_mc8_neon(uint8_t *dst, uint8_t *src, int stride, int h,
                                int x, int y);
void ff_put_vc1_chroma_mc4_neon(uint8_t *dst, uint8_t *src, int stride, int h,
                                int x, int y);
void ff_avg_vc1_chroma_mc4_neon(uint8_t *dst, uint8_t *src, int stride, int h,
                                int x, int y);

av_cold void ff_vc1dsp_init_aarch64(VC1DSPContext *dsp)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
        dsp->put_no_rnd_vc1_chroma_pixels_tab[0] = ff_put_vc1_chroma_mc8_neon;
        dsp->avg_no_rnd_vc1_chroma_pixels_tab[0] = ff_avg_vc1_chroma_mc8_neon;
        dsp->put_no_rnd_vc1_chroma_pixels_tab[1] = ff_put_vc1_chroma_mc4_neon;
        dsp->avg_no_rnd_vc1_chroma_pixels_tab[1] = ff_avg_vc1_chroma_mc4_neon;
    }
}
