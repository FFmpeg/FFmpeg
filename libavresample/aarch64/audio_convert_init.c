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

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/aarch64/cpu.h"
#include "libavutil/samplefmt.h"
#include "libavresample/audio_convert.h"

void ff_conv_flt_to_s16_neon(int16_t *dst, const float *src, int len);
void ff_conv_fltp_to_s16_neon(int16_t *dst, float *const *src,
                              int len, int channels);
void ff_conv_fltp_to_s16_2ch_neon(int16_t *dst, float *const *src,
                                  int len, int channels);

av_cold void ff_audio_convert_init_aarch64(AudioConvert *ac)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
        ff_audio_convert_set_func(ac, AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_FLT,
                                  0, 16, 8, "NEON",
                                  ff_conv_flt_to_s16_neon);
        ff_audio_convert_set_func(ac, AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_FLTP,
                                  2, 16, 8, "NEON",
                                  ff_conv_fltp_to_s16_2ch_neon);
        ff_audio_convert_set_func(ac, AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_FLTP,
                                  0, 16, 8, "NEON",
                                  ff_conv_fltp_to_s16_neon);
    }
}
