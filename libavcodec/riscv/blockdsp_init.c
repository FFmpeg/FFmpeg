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
#include "libavcodec/blockdsp.h"

void ff_clear_block_rvv(int16_t *block);
void ff_clear_blocks_rvv(int16_t *block);
void ff_fill_block16_rvv(uint8_t *block, uint8_t value, ptrdiff_t line_size,
                           int h);
void ff_fill_block8_rvv(uint8_t *block, uint8_t value, ptrdiff_t line_size,
                           int h);

av_cold void ff_blockdsp_init_riscv(BlockDSPContext *c)
{
#if HAVE_RVV
    int flags = av_get_cpu_flags();

    if (flags & AV_CPU_FLAG_RVV_I64 && ff_rv_vlen_least(128)) {
        c->clear_block = ff_clear_block_rvv;
        c->clear_blocks = ff_clear_blocks_rvv;
        c->fill_block_tab[0] = ff_fill_block16_rvv;
        c->fill_block_tab[1] = ff_fill_block8_rvv;
    }
#endif
}
