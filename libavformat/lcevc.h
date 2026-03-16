/*
 * LCEVC helper functions for muxers
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

#ifndef AVFORMAT_LCEVC_H
#define AVFORMAT_LCEVC_H

#include <stdint.h>
#include "avio.h"

typedef struct LCEVCDecoderConfigurationRecord {
    uint8_t  profile_idc;
    uint8_t  level_idc;
    uint8_t  chroma_format_idc;
    uint8_t  bit_depth_luma_minus8;
    uint8_t  bit_depth_chroma_minus8;
    uint32_t pic_width_in_luma_samples;
    uint32_t pic_height_in_luma_samples;
} LCEVCDecoderConfigurationRecord;

int ff_isom_write_lvcc(AVIOContext *pb, const uint8_t *data, int len);

int ff_lcvec_parse_config_record(LCEVCDecoderConfigurationRecord *lvcc,
                                 const uint8_t *buf, int size);

#endif /* AVFORMAT_LCEVC_H */
