/*
 * MJPEG encoder
 * Copyright (c) 2016 William Ma, Ted Ying, Jerry Jiang
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
 * Huffman table generation for MJPEG encoder.
 */

#ifndef AVCODEC_MJPEGENC_HUFFMAN_H
#define AVCODEC_MJPEGENC_HUFFMAN_H

#include <stdint.h>

typedef struct MJpegEncHuffmanContext {
    int val_count[256];
} MJpegEncHuffmanContext;

// Uses the package merge algorithm to compute the Huffman table.
void ff_mjpeg_encode_huffman_init(MJpegEncHuffmanContext *s);
static inline void ff_mjpeg_encode_huffman_increment(MJpegEncHuffmanContext *s,
                                                     uint8_t val)
{
    s->val_count[val]++;
}
void ff_mjpeg_encode_huffman_close(MJpegEncHuffmanContext *s,
                                   uint8_t bits[17], uint8_t val[],
                                   int max_nval);

#endif /* AVCODEC_MJPEGENC_HUFFMAN_H */
