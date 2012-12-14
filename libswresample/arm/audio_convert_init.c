/*
 * This file is part of libswresample.
 *
 * libswresample is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * libswresample is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with libswresample; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/arm/cpu.h"
#include "libavutil/samplefmt.h"
#include "libswresample/swresample_internal.h"
#include "libswresample/audioconvert.h"

void swri_oldapi_conv_flt_to_s16_neon(int16_t *dst, const float *src, int len);
void swri_oldapi_conv_fltp_to_s16_2ch_neon(int16_t *dst, float *const *src, int len, int channels);
void swri_oldapi_conv_fltp_to_s16_nch_neon(int16_t *dst, float *const *src, int len, int channels);

static void conv_flt_to_s16_neon(uint8_t **dst, const uint8_t **src, int len){
    swri_oldapi_conv_flt_to_s16_neon((int16_t*)*dst, (const float*)*src, len);
}

static void conv_fltp_to_s16_2ch_neon(uint8_t **dst, const uint8_t **src, int len){
    swri_oldapi_conv_fltp_to_s16_2ch_neon((int16_t*)*dst, (float *const*)src, len, 2);
}

static void conv_fltp_to_s16_nch_neon(uint8_t **dst, const uint8_t **src, int len){
    int channels;
    for(channels=3; channels<SWR_CH_MAX && src[channels]; channels++)
        ;
    swri_oldapi_conv_fltp_to_s16_nch_neon((int16_t*)*dst, (float *const*)src, len, channels);
}

av_cold void swri_audio_convert_init_arm(struct AudioConvert *ac,
                                       enum AVSampleFormat out_fmt,
                                       enum AVSampleFormat in_fmt,
                                       int channels)
{
    int cpu_flags = av_get_cpu_flags();

    ac->simd_f= NULL;

    if (have_neon(cpu_flags)) {
        if(out_fmt == AV_SAMPLE_FMT_S16 && in_fmt == AV_SAMPLE_FMT_FLT || out_fmt == AV_SAMPLE_FMT_S16P && in_fmt == AV_SAMPLE_FMT_FLTP)
            ac->simd_f = conv_flt_to_s16_neon;
        if(out_fmt == AV_SAMPLE_FMT_S16 && in_fmt == AV_SAMPLE_FMT_FLTP && channels == 2)
            ac->simd_f = conv_fltp_to_s16_2ch_neon;
        if(out_fmt == AV_SAMPLE_FMT_S16 && in_fmt == AV_SAMPLE_FMT_FLTP && channels >  2)
            ac->simd_f = conv_fltp_to_s16_nch_neon;
        if(ac->simd_f)
            ac->in_simd_align_mask = ac->out_simd_align_mask = 15;
    }
}
