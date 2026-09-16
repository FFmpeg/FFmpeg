/*
 * Unpack bit-packed streams to formats supported by FFmpeg
 * Copyright (c) 2026 Devin Heitmueller <dheitmueller@ltnglobal.com>
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

#ifndef AVCODEC_BITPACKED_DEC_H
#define AVCODEC_BITPACKED_DEC_H

#include <stdint.h>

#include "avcodec.h"

struct BitpackedContext {
    int (*decode)(AVCodecContext *avctx, AVFrame *frame,
                  const AVPacket *pkt);

    /* width must be a positive multiple of 8; src contains width * 5 / 2 bytes. */
    void (*unpack_yuv422p10)(const uint8_t *src,
                             uint16_t *y, uint16_t *u, uint16_t *v,
                             int width);
};

void ff_bitpacked_init_aarch64(struct BitpackedContext *s);
void ff_bitpacked_init_x86(struct BitpackedContext *s);

#endif /* AVCODEC_BITPACKED_DEC_H */
