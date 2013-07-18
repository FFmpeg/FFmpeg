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
#include "libavutil/cpu.h"
#include "libavutil/samplefmt.h"
#include "libavutil/x86/cpu.h"
#include "libavfilter/af_volume.h"

void ff_scale_samples_s16_sse2(uint8_t *dst, const uint8_t *src, int len,
                               int volume);

void ff_scale_samples_s32_sse2(uint8_t *dst, const uint8_t *src, int len,
                               int volume);
void ff_scale_samples_s32_ssse3_atom(uint8_t *dst, const uint8_t *src, int len,
                                     int volume);
void ff_scale_samples_s32_avx(uint8_t *dst, const uint8_t *src, int len,
                              int volume);

av_cold void ff_volume_init_x86(VolumeContext *vol)
{
    int cpu_flags = av_get_cpu_flags();
    enum AVSampleFormat sample_fmt = av_get_packed_sample_fmt(vol->sample_fmt);

    if (sample_fmt == AV_SAMPLE_FMT_S16) {
        if (EXTERNAL_SSE2(cpu_flags) && vol->volume_i < 32768) {
            vol->scale_samples = ff_scale_samples_s16_sse2;
            vol->samples_align = 8;
        }
    } else if (sample_fmt == AV_SAMPLE_FMT_S32) {
        if (EXTERNAL_SSE2(cpu_flags)) {
            vol->scale_samples = ff_scale_samples_s32_sse2;
            vol->samples_align = 4;
        }
        if (EXTERNAL_SSSE3(cpu_flags) && cpu_flags & AV_CPU_FLAG_ATOM) {
            vol->scale_samples = ff_scale_samples_s32_ssse3_atom;
            vol->samples_align = 4;
        }
        if (EXTERNAL_AVX(cpu_flags)) {
            vol->scale_samples = ff_scale_samples_s32_avx;
            vol->samples_align = 8;
        }
    }
}
