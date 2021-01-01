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

#ifndef AVCODEC_CBS_SEI_H
#define AVCODEC_CBS_SEI_H

#include <stddef.h>
#include <stdint.h>
#include "libavutil/buffer.h"


typedef struct SEIRawUserDataRegistered {
    uint8_t      itu_t_t35_country_code;
    uint8_t      itu_t_t35_country_code_extension_byte;
    uint8_t     *data;
    AVBufferRef *data_ref;
    size_t       data_length;
} SEIRawUserDataRegistered;

typedef struct SEIRawUserDataUnregistered {
    uint8_t      uuid_iso_iec_11578[16];
    uint8_t     *data;
    AVBufferRef *data_ref;
    size_t       data_length;
} SEIRawUserDataUnregistered;

typedef struct SEIRawMasteringDisplayColourVolume {
    uint16_t display_primaries_x[3];
    uint16_t display_primaries_y[3];
    uint16_t white_point_x;
    uint16_t white_point_y;
    uint32_t max_display_mastering_luminance;
    uint32_t min_display_mastering_luminance;
} SEIRawMasteringDisplayColourVolume;

typedef struct SEIRawContentLightLevelInfo {
    uint16_t max_content_light_level;
    uint16_t max_pic_average_light_level;
} SEIRawContentLightLevelInfo;

typedef struct SEIRawAlternativeTransferCharacteristics {
    uint8_t preferred_transfer_characteristics;
} SEIRawAlternativeTransferCharacteristics;

#endif /* AVCODEC_CBS_SEI_H */
