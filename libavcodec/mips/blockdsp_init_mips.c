/*
 * Copyright (c) 2015 Parag Salasakar (parag.salasakar@imgtec.com)
 *                    Zhou Xiaoyong <zhouxiaoyong@loongson.cn>
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
#include "blockdsp_mips.h"

void ff_blockdsp_init_mips(BlockDSPContext *c)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_mmi(cpu_flags)) {
        c->clear_block = ff_clear_block_mmi;
        c->clear_blocks = ff_clear_blocks_mmi;

        c->fill_block_tab[0] = ff_fill_block16_mmi;
        c->fill_block_tab[1] = ff_fill_block8_mmi;
    }

    if (have_msa(cpu_flags)) {
        c->clear_block = ff_clear_block_msa;
        c->clear_blocks = ff_clear_blocks_msa;

        c->fill_block_tab[0] = ff_fill_block16_msa;
        c->fill_block_tab[1] = ff_fill_block8_msa;
    }
}
