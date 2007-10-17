/*
 * MJPEG encoder
 * Copyright (c) 2000, 2001 Fabrice Bellard.
 * Copyright (c) 2003 Alex Beregszaszi
 * Copyright (c) 2003-2004 Michael Niedermayer
 *
 * Support for external huffman table, various fixes (AVID workaround),
 * aspecting, new decode_frame mechanism and apple mjpeg-b support
 *                                  by Alex Beregszaszi
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
 * @file mjpegenc.h
 * MJPEG encoder.
 */

#ifndef FFMPEG_MJPEGENC_H
#define FFMPEG_MJPEGENC_H

#include "dsputil.h"
#include "mpegvideo.h"

typedef struct MJpegContext {
    uint8_t huff_size_dc_luminance[12]; //FIXME use array [3] instead of lumi / chrom, for easier addressing
    uint16_t huff_code_dc_luminance[12];
    uint8_t huff_size_dc_chrominance[12];
    uint16_t huff_code_dc_chrominance[12];

    uint8_t huff_size_ac_luminance[256];
    uint16_t huff_code_ac_luminance[256];
    uint8_t huff_size_ac_chrominance[256];
    uint16_t huff_code_ac_chrominance[256];
} MJpegContext;

int  ff_mjpeg_encode_init(MpegEncContext *s);
void ff_mjpeg_encode_close(MpegEncContext *s);
void ff_mjpeg_encode_picture_header(MpegEncContext *s);
void ff_mjpeg_encode_picture_trailer(MpegEncContext *s);
void ff_mjpeg_encode_stuffing(PutBitContext *pbc);
void ff_mjpeg_encode_dc(MpegEncContext *s, int val,
                        uint8_t *huff_size, uint16_t *huff_code);
void ff_mjpeg_encode_mb(MpegEncContext *s, DCTELEM block[6][64]);

#endif /* FFMPEG_MJPEGENC_H */
