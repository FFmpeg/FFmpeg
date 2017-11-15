/*
 * ARM optimized Format Conversion Utils
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

#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/arm/cpu.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/fmtconvert.h"

void ff_int32_to_float_fmul_array8_neon(FmtConvertContext *c, float *dst,
                                        const int32_t *src, const float *mul,
                                        int len);
void ff_int32_to_float_fmul_scalar_neon(float *dst, const int32_t *src,
                                        float mul, int len);

void ff_int32_to_float_fmul_scalar_vfp(float *dst, const int32_t *src,
                                       float mul, int len);
void ff_int32_to_float_fmul_array8_vfp(FmtConvertContext *c, float *dst,
                                       const int32_t *src, const float *mul,
                                       int len);

av_cold void ff_fmt_convert_init_arm(FmtConvertContext *c, AVCodecContext *avctx)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_vfp_vm(cpu_flags)) {
        c->int32_to_float_fmul_scalar = ff_int32_to_float_fmul_scalar_vfp;
        c->int32_to_float_fmul_array8 = ff_int32_to_float_fmul_array8_vfp;
    }

    if (have_neon(cpu_flags)) {
        c->int32_to_float_fmul_array8 = ff_int32_to_float_fmul_array8_neon;
        c->int32_to_float_fmul_scalar = ff_int32_to_float_fmul_scalar_neon;
    }
}
