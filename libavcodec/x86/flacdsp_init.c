/*
 * Copyright (c) 2014 James Almer
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

#include "libavcodec/flacdsp.h"
#include "libavutil/x86/cpu.h"
#include "config.h"

void ff_flac_lpc_32_sse4(int32_t *samples, const int coeffs[32], int order,
                         int qlevel, int len);
void ff_flac_lpc_32_xop(int32_t *samples, const int coeffs[32], int order,
                        int qlevel, int len);

void ff_flac_enc_lpc_16_sse4(int32_t *, const int32_t *, int, int, const int32_t *,int);

#define DECORRELATE_FUNCS(fmt, opt)                                                      \
void ff_flac_decorrelate_ls_##fmt##_##opt(uint8_t **out, int32_t **in, int channels,     \
                                          int len, int shift);                           \
void ff_flac_decorrelate_rs_##fmt##_##opt(uint8_t **out, int32_t **in, int channels,     \
                                          int len, int shift);                           \
void ff_flac_decorrelate_ms_##fmt##_##opt(uint8_t **out, int32_t **in, int channels,     \
                                          int len, int shift);                           \
void ff_flac_decorrelate_indep2_##fmt##_##opt(uint8_t **out, int32_t **in, int channels, \
                                             int len, int shift);                        \
void ff_flac_decorrelate_indep4_##fmt##_##opt(uint8_t **out, int32_t **in, int channels, \
                                              int len, int shift);                       \
void ff_flac_decorrelate_indep6_##fmt##_##opt(uint8_t **out, int32_t **in, int channels, \
                                              int len, int shift);                       \
void ff_flac_decorrelate_indep8_##fmt##_##opt(uint8_t **out, int32_t **in, int channels, \
                                              int len, int shift)

DECORRELATE_FUNCS(16, sse2);
DECORRELATE_FUNCS(16,  avx);
DECORRELATE_FUNCS(32, sse2);
DECORRELATE_FUNCS(32,  avx);

av_cold void ff_flacdsp_init_x86(FLACDSPContext *c, enum AVSampleFormat fmt, int channels,
                                 int bps)
{
#if HAVE_YASM
    int cpu_flags = av_get_cpu_flags();

#if CONFIG_FLAC_DECODER
    if (EXTERNAL_SSE2(cpu_flags)) {
        if (fmt == AV_SAMPLE_FMT_S16) {
            if (channels == 2)
                c->decorrelate[0] = ff_flac_decorrelate_indep2_16_sse2;
            else if (channels == 4)
                c->decorrelate[0] = ff_flac_decorrelate_indep4_16_sse2;
            else if (channels == 6)
                c->decorrelate[0] = ff_flac_decorrelate_indep6_16_sse2;
            else if (ARCH_X86_64 && channels == 8)
                c->decorrelate[0] = ff_flac_decorrelate_indep8_16_sse2;
            c->decorrelate[1] = ff_flac_decorrelate_ls_16_sse2;
            c->decorrelate[2] = ff_flac_decorrelate_rs_16_sse2;
            c->decorrelate[3] = ff_flac_decorrelate_ms_16_sse2;
        } else if (fmt == AV_SAMPLE_FMT_S32) {
            if (channels == 2)
                c->decorrelate[0] = ff_flac_decorrelate_indep2_32_sse2;
            else if (channels == 4)
                c->decorrelate[0] = ff_flac_decorrelate_indep4_32_sse2;
            else if (channels == 6)
                c->decorrelate[0] = ff_flac_decorrelate_indep6_32_sse2;
            else if (ARCH_X86_64 && channels == 8)
                c->decorrelate[0] = ff_flac_decorrelate_indep8_32_sse2;
            c->decorrelate[1] = ff_flac_decorrelate_ls_32_sse2;
            c->decorrelate[2] = ff_flac_decorrelate_rs_32_sse2;
            c->decorrelate[3] = ff_flac_decorrelate_ms_32_sse2;
        }
    }
    if (EXTERNAL_SSE4(cpu_flags)) {
        c->lpc32 = ff_flac_lpc_32_sse4;
    }
    if (EXTERNAL_AVX(cpu_flags)) {
        if (fmt == AV_SAMPLE_FMT_S16) {
            if (ARCH_X86_64 && channels == 8)
                c->decorrelate[0] = ff_flac_decorrelate_indep8_16_avx;
        } else if (fmt == AV_SAMPLE_FMT_S32) {
            if (channels == 4)
                c->decorrelate[0] = ff_flac_decorrelate_indep4_32_avx;
            else if (channels == 6)
                c->decorrelate[0] = ff_flac_decorrelate_indep6_32_avx;
            else if (ARCH_X86_64 && channels == 8)
                c->decorrelate[0] = ff_flac_decorrelate_indep8_32_avx;
        }
    }
    if (EXTERNAL_XOP(cpu_flags)) {
        c->lpc32 = ff_flac_lpc_32_xop;
    }
#endif

#if CONFIG_FLAC_ENCODER
    if (EXTERNAL_SSE4(cpu_flags)) {
        if (CONFIG_GPL)
            c->lpc16_encode = ff_flac_enc_lpc_16_sse4;
    }
#endif
#endif /* HAVE_YASM */
}
