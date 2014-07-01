/*
 * lossless JPEG shared bits
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

#ifndef AVCODEC_MJPEGENC_COMMON_H
#define AVCODEC_MJPEGENC_COMMON_H

#include <stdint.h>

#include "avcodec.h"
#include "idctdsp.h"
#include "mpegvideo.h"
#include "put_bits.h"

void ff_mjpeg_encode_picture_header(AVCodecContext *avctx, PutBitContext *pb,
                                    ScanTable *intra_scantable,
                                    uint16_t luma_intra_matrix[64],
                                    uint16_t chroma_intra_matrix[64]);
void ff_mjpeg_encode_picture_trailer(PutBitContext *pb, int header_bits);
void ff_mjpeg_escape_FF(PutBitContext *pb, int start);
void ff_mjpeg_encode_stuffing(MpegEncContext *s);
void ff_mjpeg_init_hvsample(AVCodecContext *avctx, int hsample[3], int vsample[3]);

void ff_mjpeg_encode_dc(PutBitContext *pb, int val,
                        uint8_t *huff_size, uint16_t *huff_code);

#endif /* AVCODEC_MJPEGENC_COMMON_H */
