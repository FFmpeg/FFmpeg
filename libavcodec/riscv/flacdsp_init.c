/*
 * Copyright © 2023 Rémi Denis-Courmont.
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

#include "config.h"

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/riscv/cpu.h"
#include "libavcodec/flacdsp.h"

void ff_flac_lpc16_rvv(int32_t *decoded, const int coeffs[32],
                       int pred_order, int qlevel, int len);
void ff_flac_lpc32_rvv(int32_t *decoded, const int coeffs[32],
                       int pred_order, int qlevel, int len);
void ff_flac_lpc32_rvv_simple(int32_t *decoded, const int coeffs[32],
                              int pred_order, int qlevel, int len);
void ff_flac_lpc33_rvv(int64_t *, const int32_t *, const int coeffs[32],
                       int pred_order, int qlevel, int len);
void ff_flac_wasted32_rvv(int32_t *, int shift, int len);
void ff_flac_wasted33_rvv(int64_t *, const int32_t *, int shift, int len);
void ff_flac_decorrelate_indep2_16_rvv(uint8_t **out, int32_t **in,
                                       int channels, int len, int shift);
void ff_flac_decorrelate_indep4_16_rvv(uint8_t **out, int32_t **in,
                                       int channels, int len, int shift);
void ff_flac_decorrelate_indep6_16_rvv(uint8_t **out, int32_t **in,
                                       int channels, int len, int shift);
void ff_flac_decorrelate_indep8_16_rvv(uint8_t **out, int32_t **in,
                                       int channels, int len, int shift);
void ff_flac_decorrelate_ls_16_rvv(uint8_t **out, int32_t **in,
                                   int channels, int len, int shift);
void ff_flac_decorrelate_rs_16_rvv(uint8_t **out, int32_t **in,
                                   int channels, int len, int shift);
void ff_flac_decorrelate_ms_16_rvv(uint8_t **out, int32_t **in,
                                   int channels, int len, int shift);
void ff_flac_decorrelate_indep2_32_rvv(uint8_t **out, int32_t **in,
                                       int channels, int len, int shift);
void ff_flac_decorrelate_indep4_32_rvv(uint8_t **out, int32_t **in,
                                       int channels, int len, int shift);
void ff_flac_decorrelate_indep6_32_rvv(uint8_t **out, int32_t **in,
                                       int channels, int len, int shift);
void ff_flac_decorrelate_indep8_32_rvv(uint8_t **out, int32_t **in,
                                       int channels, int len, int shift);
void ff_flac_decorrelate_ls_32_rvv(uint8_t **out, int32_t **in,
                                   int channels, int len, int shift);
void ff_flac_decorrelate_rs_32_rvv(uint8_t **out, int32_t **in,
                                   int channels, int len, int shift);
void ff_flac_decorrelate_ms_32_rvv(uint8_t **out, int32_t **in,
                                   int channels, int len, int shift);

av_cold void ff_flacdsp_init_riscv(FLACDSPContext *c, enum AVSampleFormat fmt,
                                   int channels)
{
#if HAVE_RVV
    int flags = av_get_cpu_flags();

    if ((flags & AV_CPU_FLAG_RVV_I32) && (flags & AV_CPU_FLAG_RVB)) {
        int vlenb = ff_get_rv_vlenb();

        if (vlenb >= 16) {
            c->lpc16 = ff_flac_lpc16_rvv;

# if (__riscv_xlen >= 64)
            if (flags & AV_CPU_FLAG_RVV_I64) {
                if (vlenb > 16) {
                    c->lpc32 = ff_flac_lpc32_rvv_simple;
                    c->lpc33 = ff_flac_lpc33_rvv;
                } else
                    c->lpc32 = ff_flac_lpc32_rvv;
            }
# endif
        }

        c->wasted32 = ff_flac_wasted32_rvv;

        if (flags & AV_CPU_FLAG_RVV_I64)
            c->wasted33 = ff_flac_wasted33_rvv;

# if (__riscv_xlen >= 64)
        switch (fmt) {
        case AV_SAMPLE_FMT_S16:
            switch (channels) {
            case 2:
                c->decorrelate[0] = ff_flac_decorrelate_indep2_16_rvv;
                break;
            case 4:
                c->decorrelate[0] = ff_flac_decorrelate_indep4_16_rvv;
                break;
            case 6:
                c->decorrelate[0] = ff_flac_decorrelate_indep6_16_rvv;
                break;
            case 8:
                c->decorrelate[0] = ff_flac_decorrelate_indep8_16_rvv;
                break;
            }
            c->decorrelate[1] = ff_flac_decorrelate_ls_16_rvv;
            c->decorrelate[2] = ff_flac_decorrelate_rs_16_rvv;
            c->decorrelate[3] = ff_flac_decorrelate_ms_16_rvv;
            break;
        case AV_SAMPLE_FMT_S32:
            switch (channels) {
            case 2:
                c->decorrelate[0] = ff_flac_decorrelate_indep2_32_rvv;
                break;
            case 4:
                c->decorrelate[0] = ff_flac_decorrelate_indep4_32_rvv;
                break;
            case 6:
                c->decorrelate[0] = ff_flac_decorrelate_indep6_32_rvv;
                break;
            case 8:
                c->decorrelate[0] = ff_flac_decorrelate_indep8_32_rvv;
                break;
            }
            c->decorrelate[1] = ff_flac_decorrelate_ls_32_rvv;
            c->decorrelate[2] = ff_flac_decorrelate_rs_32_rvv;
            c->decorrelate[3] = ff_flac_decorrelate_ms_32_rvv;
            break;
        }
# endif
    }
#endif
}
