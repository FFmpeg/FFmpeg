/*
 * ARM NEON optimised block operations
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavcodec/blockdsp.h"
#include "blockdsp_arm.h"

void ff_clear_block_neon(int16_t *block);
void ff_clear_blocks_neon(int16_t *blocks);

av_cold void ff_blockdsp_init_neon(BlockDSPContext *c, unsigned high_bit_depth)
{
    if (!high_bit_depth) {
        c->clear_block  = ff_clear_block_neon;
        c->clear_blocks = ff_clear_blocks_neon;
    }
}
