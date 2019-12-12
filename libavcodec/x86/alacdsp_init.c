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
#include "libavutil/x86/cpu.h"
#include "libavcodec/alacdsp.h"
#include "config.h"

void ff_alac_decorrelate_stereo_sse4(int32_t *buffer[2], int nb_samples,
                                     int decorr_shift, int decorr_left_weight);
void ff_alac_append_extra_bits_stereo_sse2(int32_t *buffer[2], int32_t *extra_bits_buffer[2],
                                           int extra_bits, int channels, int nb_samples);
void ff_alac_append_extra_bits_mono_sse2(int32_t *buffer[2], int32_t *extra_bits_buffer[2],
                                         int extra_bits, int channels, int nb_samples);

av_cold void ff_alacdsp_init_x86(ALACDSPContext *c)
{
#if HAVE_X86ASM
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE2(cpu_flags)) {
        c->append_extra_bits[0] = ff_alac_append_extra_bits_mono_sse2;
        c->append_extra_bits[1] = ff_alac_append_extra_bits_stereo_sse2;
    }
    if (EXTERNAL_SSE4(cpu_flags)) {
        c->decorrelate_stereo   = ff_alac_decorrelate_stereo_sse4;
    }
#endif /* HAVE_X86ASM */
}
