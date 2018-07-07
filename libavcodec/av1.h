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

/**
 * @file
 * AV1 common definitions
 */

#ifndef AVCODEC_AV1_H
#define AVCODEC_AV1_H

// OBU types (section 6.2.2).
typedef enum {
    // 0 reserved.
    AV1_OBU_SEQUENCE_HEADER        = 1,
    AV1_OBU_TEMPORAL_DELIMITER     = 2,
    AV1_OBU_FRAME_HEADER           = 3,
    AV1_OBU_TILE_GROUP             = 4,
    AV1_OBU_METADATA               = 5,
    AV1_OBU_FRAME                  = 6,
    AV1_OBU_REDUNDANT_FRAME_HEADER = 7,
    AV1_OBU_TILE_LIST              = 8,
    // 9-14 reserved.
    AV1_OBU_PADDING                = 15,
} AV1_OBU_Type;

#endif /* AVCODEC_AV1_H */
