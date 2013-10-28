/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/mem.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavfilter/vf_pullup.h"

int ff_pullup_filter_diff_mmx(const uint8_t *a, const uint8_t *b, int s);
int ff_pullup_filter_comb_mmx(const uint8_t *a, const uint8_t *b, int s);
int ff_pullup_filter_var_mmx (const uint8_t *a, const uint8_t *b, int s);

av_cold void ff_pullup_init_x86(PullupContext *s)
{
#if HAVE_YASM
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_MMX(cpu_flags)) {
        s->diff = ff_pullup_filter_diff_mmx;
        s->comb = ff_pullup_filter_comb_mmx;
        s->var  = ff_pullup_filter_var_mmx;
    }
#endif
}
