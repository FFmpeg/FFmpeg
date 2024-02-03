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
#include "libavcodec/me_cmp.h"
#include "libavcodec/mpegvideo.h"

int ff_pix_abs16_rvv(MpegEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                              ptrdiff_t stride, int h);
int ff_pix_abs8_rvv(MpegEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                             ptrdiff_t stride, int h);

av_cold void ff_me_cmp_init_riscv(MECmpContext *c, AVCodecContext *avctx)
{
#if HAVE_RVV
    int flags = av_get_cpu_flags();

    if (flags & AV_CPU_FLAG_RVV_I32 && ff_get_rv_vlenb() >= 16) {
        c->pix_abs[0][0] = ff_pix_abs16_rvv;
        c->sad[0] = ff_pix_abs16_rvv;
        c->pix_abs[1][0] = ff_pix_abs8_rvv;
        c->sad[1] = ff_pix_abs8_rvv;
    }
#endif
}
