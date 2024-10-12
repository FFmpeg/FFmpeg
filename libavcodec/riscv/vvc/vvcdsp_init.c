/*
 * Copyright (c) 2024 Institue of Software Chinese Academy of Sciences (ISCAS).
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
#include "libavcodec/vvc/dsp.h"

#define bf(fn, bd,  opt) fn##_##bd##_##opt

#define AVG_PROTOTYPES(bd, opt)                                                                      \
void bf(ff_vvc_avg, bd, opt)(uint8_t *dst, ptrdiff_t dst_stride,                                     \
    const int16_t *src0, const int16_t *src1, int width, int height);                                \
void bf(ff_vvc_w_avg, bd, opt)(uint8_t *dst, ptrdiff_t dst_stride,                                   \
    const int16_t *src0, const int16_t *src1, int width, int height,                                 \
    int denom, int w0, int w1, int o0, int o1);

AVG_PROTOTYPES(8, rvv_128)
AVG_PROTOTYPES(8, rvv_256)

void ff_vvc_dsp_init_riscv(VVCDSPContext *const c, const int bd)
{
#if HAVE_RVV
    const int flags = av_get_cpu_flags();
    int vlenb;

    if (!(flags & AV_CPU_FLAG_RVV_I32) || !(flags & AV_CPU_FLAG_RVB))
        return;

    vlenb = ff_get_rv_vlenb();
    if (vlenb >= 32) {
        switch (bd) {
            case 8:
                c->inter.avg    = ff_vvc_avg_8_rvv_256;
# if (__riscv_xlen == 64)
                c->inter.w_avg    = ff_vvc_w_avg_8_rvv_256;
# endif
                break;
            default:
                break;
        }
    } else if (vlenb >= 16) {
        switch (bd) {
            case 8:
                c->inter.avg    = ff_vvc_avg_8_rvv_128;
# if (__riscv_xlen == 64)
                c->inter.w_avg    = ff_vvc_w_avg_8_rvv_128;
# endif
                break;
            default:
                break;
        }
    }
#endif
}
