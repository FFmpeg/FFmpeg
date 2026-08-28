/*
 * Chinese AVS video (AVS1-P16, Broadcasting profile) AEC decoder.
 * Copyright (c) 2026 Rainbaby <r@bgr.jp>
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

/**
 * @file
 * Chinese AVS video (AVS1-P16, Broadcasting profile) AEC decoder
 *
 * Clause numbers refer to GY/T 257.1-2012. The renormalization loops end
 * with the buffer, so truncated input terminates. The caller must have
 * removed the annex A pseudo start codes from the payload.
 */

#include <stdint.h>

#include "cavs_aec.h"
#include "get_bits.h"

void ff_cavs_aec_init(CAVSAECContext *aec, GetBitContext *gb)
{
    for (int i = 0; i < CAVS_AEC_NUM_CTX; i++) {
        aec->ctx[i].mps     = 0;
        aec->ctx[i].cycno   = 0;
        aec->ctx[i].lg_pmps = CAVS_AEC_LGPMPS_INIT;
    }

    aec->gb      = gb;
    aec->rs1     = 0;
    aec->rt1     = CAVS_AEC_RT1_INIT;
    aec->vs      = 0;
    aec->vt      = get_bits(gb, 9);
    while (!((aec->vt >> 8) & 0x01) && get_bits_left(gb) > 0) {
        aec->vt = (aec->vt << 1) | get_bits1(gb);
        aec->vs++;
    }
    aec->vt &= 0xFF;
}
