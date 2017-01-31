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

#include "libavcodec/avcodec.h"
#include "libavcodec/hpeldsp.h"

#include "hpeldsp.h"

void ff_put_no_rnd_pixels8_x2_exact_mmxext(uint8_t *block,
                                           const uint8_t *pixels,
                                           ptrdiff_t line_size, int h);
void ff_put_no_rnd_pixels8_x2_exact_3dnow(uint8_t *block,
                                          const uint8_t *pixels,
                                          ptrdiff_t line_size, int h);
void ff_put_no_rnd_pixels8_y2_exact_mmxext(uint8_t *block,
                                           const uint8_t *pixels,
                                           ptrdiff_t line_size, int h);
void ff_put_no_rnd_pixels8_y2_exact_3dnow(uint8_t *block,
                                          const uint8_t *pixels,
                                          ptrdiff_t line_size, int h);

av_cold void ff_hpeldsp_vp3_init_x86(HpelDSPContext *c, int cpu_flags, int flags)
{
    if (EXTERNAL_AMD3DNOW(cpu_flags)) {
        if (flags & AV_CODEC_FLAG_BITEXACT) {
            c->put_no_rnd_pixels_tab[1][1] = ff_put_no_rnd_pixels8_x2_exact_3dnow;
            c->put_no_rnd_pixels_tab[1][2] = ff_put_no_rnd_pixels8_y2_exact_3dnow;
        }
    }

    if (EXTERNAL_MMXEXT(cpu_flags)) {
        if (flags & AV_CODEC_FLAG_BITEXACT) {
            c->put_no_rnd_pixels_tab[1][1] = ff_put_no_rnd_pixels8_x2_exact_mmxext;
            c->put_no_rnd_pixels_tab[1][2] = ff_put_no_rnd_pixels8_y2_exact_mmxext;
        }
    }
}
