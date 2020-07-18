/*
 * Copyright (c) 2015 Zhou Xiaoyong <zhouxiaoyong@loongson.cn>
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

#include "libavutil/mips/cpu.h"
#include "xvididct_mips.h"

av_cold void ff_xvid_idct_init_mips(IDCTDSPContext *c, AVCodecContext *avctx,
        unsigned high_bit_depth)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_mmi(cpu_flags)) {
        if (!high_bit_depth) {
            if (avctx->idct_algo == FF_IDCT_AUTO ||
                    avctx->idct_algo == FF_IDCT_XVID) {
                c->idct_put = ff_xvid_idct_put_mmi;
                c->idct_add = ff_xvid_idct_add_mmi;
                c->idct = ff_xvid_idct_mmi;
                c->perm_type = FF_IDCT_PERM_NONE;
            }
        }
    }
}
