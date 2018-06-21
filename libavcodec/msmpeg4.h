/*
 * MSMPEG4 backend for encoder and decoder
 * copyright (c) 2007 Aurelien Jacobs <aurel@gnuage.org>
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

#ifndef AVCODEC_MSMPEG4_H
#define AVCODEC_MSMPEG4_H

#include <stdint.h>

#include "config.h"
#include "avcodec.h"
#include "mpegvideo.h"
#include "msmpeg4data.h"
#include "put_bits.h"

#define INTER_INTRA_VLC_BITS 3
#define MB_NON_INTRA_VLC_BITS 9
#define MB_INTRA_VLC_BITS 9

#define II_BITRATE 128*1024
#define MBAC_BITRATE 50*1024

#define DC_MAX 119

extern VLC ff_mb_non_intra_vlc[4];
extern VLC ff_inter_intra_vlc;

void ff_msmpeg4_code012(PutBitContext *pb, int n);
void ff_msmpeg4_common_init(MpegEncContext *s);
void ff_msmpeg4_encode_block(MpegEncContext * s, int16_t * block, int n);
void ff_msmpeg4_handle_slices(MpegEncContext *s);
void ff_msmpeg4_encode_motion(MpegEncContext * s, int mx, int my);
int ff_msmpeg4_coded_block_pred(MpegEncContext * s, int n,
                                uint8_t **coded_block_ptr);

int ff_msmpeg4_encode_init(MpegEncContext *s);
void ff_msmpeg4_encode_picture_header(MpegEncContext *s, int picture_number);
void ff_msmpeg4_encode_ext_header(MpegEncContext *s);
void ff_msmpeg4_encode_mb(MpegEncContext *s, int16_t block[6][64],
                          int motion_x, int motion_y);

int ff_msmpeg4_decode_init(AVCodecContext *avctx);
int ff_msmpeg4_decode_picture_header(MpegEncContext *s);
int ff_msmpeg4_decode_ext_header(MpegEncContext *s, int buf_size);
int ff_msmpeg4_decode_motion(MpegEncContext * s, int *mx_ptr, int *my_ptr);
int ff_msmpeg4_decode_block(MpegEncContext * s, int16_t * block,
                            int n, int coded, const uint8_t *scan_table);
int ff_msmpeg4_pred_dc(MpegEncContext *s, int n,
                       int16_t **dc_val_ptr, int *dir_ptr);


#define CONFIG_MSMPEG4_DECODER (CONFIG_MSMPEG4V1_DECODER || \
                                CONFIG_MSMPEG4V2_DECODER || \
                                CONFIG_MSMPEG4V3_DECODER || \
                                CONFIG_WMV1_DECODER      || \
                                CONFIG_WMV2_DECODER      || \
                                CONFIG_VC1_DECODER)
#define CONFIG_MSMPEG4_ENCODER (CONFIG_MSMPEG4V2_ENCODER || \
                                CONFIG_MSMPEG4V3_ENCODER || \
                                CONFIG_WMV1_ENCODER      || \
                                CONFIG_WMV2_ENCODER)

#endif /* AVCODEC_MSMPEG4_H */
