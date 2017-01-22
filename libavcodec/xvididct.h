/*
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

#ifndef AVCODEC_XVIDIDCT_H
#define AVCODEC_XVIDIDCT_H

#include <stdint.h>

#include "avcodec.h"
#include "idctdsp.h"

void ff_xvid_idct(int16_t *const in);

void ff_xvid_idct_init(IDCTDSPContext *c, AVCodecContext *avctx);

void ff_xvid_idct_init_x86(IDCTDSPContext *c, AVCodecContext *avctx,
                           unsigned high_bit_depth);
void ff_xvid_idct_init_mips(IDCTDSPContext *c, AVCodecContext *avctx,
                            unsigned high_bit_depth);

#endif /* AVCODEC_XVIDIDCT_H */
