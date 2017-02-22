/*
 * MJPEG encoder
 * Copyright (c) 2000, 2001 Fabrice Bellard
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
 * @file
 * MJPEG encoder.
 */

#ifndef AVCODEC_MJPEGENC_H
#define AVCODEC_MJPEGENC_H

#include <stdint.h>

#include "mjpeg.h"
#include "mpegvideo.h"
#include "put_bits.h"

/**
 * Buffer of JPEG frame data.
 *
 * Optimal Huffman table generation requires the frame data to be loaded into
 * a buffer so that the tables can be computed.
 * There are at most mb_width*mb_height*12*64 of these per frame.
 */
typedef struct MJpegHuffmanCode {
    // 0=DC lum, 1=DC chrom, 2=AC lum, 3=AC chrom
    uint8_t table_id; ///< The Huffman table id associated with the data.
    uint8_t code;     ///< The exponent.
    uint16_t mant;    ///< The mantissa.
} MJpegHuffmanCode;

/**
 * Holds JPEG frame data and Huffman table data.
 */
typedef struct MJpegContext {
    //FIXME use array [3] instead of lumi / chroma, for easier addressing
    uint8_t huff_size_dc_luminance[12];     ///< DC luminance Huffman table size.
    uint16_t huff_code_dc_luminance[12];    ///< DC luminance Huffman table codes.
    uint8_t huff_size_dc_chrominance[12];   ///< DC chrominance Huffman table size.
    uint16_t huff_code_dc_chrominance[12];  ///< DC chrominance Huffman table codes.

    uint8_t huff_size_ac_luminance[256];    ///< AC luminance Huffman table size.
    uint16_t huff_code_ac_luminance[256];   ///< AC luminance Huffman table codes.
    uint8_t huff_size_ac_chrominance[256];  ///< AC chrominance Huffman table size.
    uint16_t huff_code_ac_chrominance[256]; ///< AC chrominance Huffman table codes.

    /** Storage for AC luminance VLC (in MpegEncContext) */
    uint8_t uni_ac_vlc_len[64 * 64 * 2];
    /** Storage for AC chrominance VLC (in MpegEncContext) */
    uint8_t uni_chroma_ac_vlc_len[64 * 64 * 2];

    // Default DC tables have exactly 12 values
    uint8_t bits_dc_luminance[17];   ///< DC luminance Huffman bits.
    uint8_t val_dc_luminance[12];    ///< DC luminance Huffman values.
    uint8_t bits_dc_chrominance[17]; ///< DC chrominance Huffman bits.
    uint8_t val_dc_chrominance[12];  ///< DC chrominance Huffman values.

    // 8-bit JPEG has max 256 values
    uint8_t bits_ac_luminance[17];   ///< AC luminance Huffman bits.
    uint8_t val_ac_luminance[256];   ///< AC luminance Huffman values.
    uint8_t bits_ac_chrominance[17]; ///< AC chrominance Huffman bits.
    uint8_t val_ac_chrominance[256]; ///< AC chrominance Huffman values.

    size_t huff_ncode;               ///< Number of current entries in the buffer.
    MJpegHuffmanCode *huff_buffer;   ///< Buffer for Huffman code values.
} MJpegContext;

/**
 * Enum for the Huffman encoding strategy.
 */
enum HuffmanTableOption {
    HUFFMAN_TABLE_DEFAULT = 0, ///< Use the default Huffman tables.
    HUFFMAN_TABLE_OPTIMAL = 1, ///< Compute and use optimal Huffman tables.
    NB_HUFFMAN_TABLE_OPTION = 2
};

static inline void put_marker(PutBitContext *p, enum JpegMarker code)
{
    put_bits(p, 8, 0xff);
    put_bits(p, 8, code);
}

int  ff_mjpeg_encode_init(MpegEncContext *s);
void ff_mjpeg_encode_picture_frame(MpegEncContext *s);
void ff_mjpeg_encode_close(MpegEncContext *s);
void ff_mjpeg_encode_mb(MpegEncContext *s, int16_t block[12][64]);

#endif /* AVCODEC_MJPEGENC_H */
