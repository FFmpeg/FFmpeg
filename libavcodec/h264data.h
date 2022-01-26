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

#ifndef AVCODEC_H264DATA_H
#define AVCODEC_H264DATA_H

#include <stdint.h>

#include "libavutil/rational.h"
#include "h264.h"

extern const uint8_t ff_h264_golomb_to_pict_type[5];
extern const uint8_t ff_h264_golomb_to_intra4x4_cbp[48];
extern const uint8_t ff_h264_golomb_to_inter_cbp[48];

extern const uint8_t ff_h264_chroma_dc_scan[4];
extern const uint8_t ff_h264_chroma422_dc_scan[8];

typedef struct IMbInfo {
    uint16_t type;
    uint8_t pred_mode;
    uint8_t cbp;
} IMbInfo;

extern const IMbInfo ff_h264_i_mb_type_info[26];

typedef struct PMbInfo {
    uint16_t type;
    uint8_t partition_count;
} PMbInfo;

extern const PMbInfo ff_h264_p_mb_type_info[5];
extern const PMbInfo ff_h264_p_sub_mb_type_info[4];
extern const PMbInfo ff_h264_b_mb_type_info[23];
extern const PMbInfo ff_h264_b_sub_mb_type_info[13];

static const AVRational ff_h264_pixel_aspect[17] = {
    {   0,  1 },
    {   1,  1 },
    {  12, 11 },
    {  10, 11 },
    {  16, 11 },
    {  40, 33 },
    {  24, 11 },
    {  20, 11 },
    {  32, 11 },
    {  80, 33 },
    {  18, 11 },
    {  15, 11 },
    {  64, 33 },
    { 160, 99 },
    {   4,  3 },
    {   3,  2 },
    {   2,  1 },
};

extern const uint8_t ff_h264_dequant4_coeff_init[6][3];
extern const uint8_t ff_h264_dequant8_coeff_init_scan[16];
extern const uint8_t ff_h264_dequant8_coeff_init[6][6];
extern const uint8_t ff_h264_quant_rem6[QP_MAX_NUM + 1];
extern const uint8_t ff_h264_quant_div6[QP_MAX_NUM + 1];

extern const uint8_t ff_h264_chroma_qp[7][QP_MAX_NUM + 1];

#endif /* AVCODEC_H264DATA_H */
