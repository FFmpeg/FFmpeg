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

#ifndef AVCODEC_CBS_APV_H
#define AVCODEC_CBS_APV_H

#include <stddef.h>
#include <stdint.h>

#include "libavutil/buffer.h"
#include "apv.h"

// Arbitrary limits to avoid large structures.
#define CBS_APV_MAX_AU_FRAMES         8
#define CBS_APV_MAX_METADATA_PAYLOADS 8


typedef struct APVRawPBUHeader {
    uint8_t  pbu_type;
    uint16_t group_id;
    uint8_t  reserved_zero_8bits;
} APVRawPBUHeader;

typedef struct APVRawFiller {
    size_t   filler_size;
} APVRawFiller;

typedef struct APVRawFrameInfo {
    uint8_t  profile_idc;
    uint8_t  level_idc;
    uint8_t  band_idc;
    uint8_t  reserved_zero_5bits;
    uint32_t frame_width;
    uint32_t frame_height;
    uint8_t  chroma_format_idc;
    uint8_t  bit_depth_minus8;
    uint8_t  capture_time_distance;
    uint8_t  reserved_zero_8bits;
} APVRawFrameInfo;

typedef struct APVRawQuantizationMatrix {
    uint8_t  q_matrix[APV_MAX_NUM_COMP][APV_TR_SIZE][APV_TR_SIZE];
} APVRawQuantizationMatrix;

typedef struct APVRawTileInfo {
    uint32_t tile_width_in_mbs;
    uint32_t tile_height_in_mbs;
    uint8_t  tile_size_present_in_fh_flag;
    uint32_t tile_size_in_fh[APV_MAX_TILE_COUNT];
} APVRawTileInfo;

typedef struct APVRawFrameHeader {
    APVRawFrameInfo frame_info;
    uint8_t  reserved_zero_8bits;

    uint8_t  color_description_present_flag;
    uint8_t  color_primaries;
    uint8_t  transfer_characteristics;
    uint8_t  matrix_coefficients;
    uint8_t  full_range_flag;

    uint8_t  use_q_matrix;
    APVRawQuantizationMatrix quantization_matrix;

    APVRawTileInfo tile_info;

    uint8_t  reserved_zero_8bits_2;
} APVRawFrameHeader;

typedef struct APVRawTileHeader {
    uint16_t tile_header_size;
    uint16_t tile_index;
    uint32_t tile_data_size[APV_MAX_NUM_COMP];
    uint8_t  tile_qp       [APV_MAX_NUM_COMP];
    uint8_t  reserved_zero_8bits;
} APVRawTileHeader;

typedef struct APVRawTile {
    APVRawTileHeader tile_header;

    uint8_t         *tile_data[APV_MAX_NUM_COMP];
    uint8_t         *tile_dummy_byte;
    uint32_t         tile_dummy_byte_size;
} APVRawTile;

typedef struct APVRawFrame {
    APVRawPBUHeader   pbu_header;
    APVRawFrameHeader frame_header;
    uint32_t          tile_size[APV_MAX_TILE_COUNT];
    APVRawTile        tile     [APV_MAX_TILE_COUNT];
    APVRawFiller      filler;

    AVBufferRef      *tile_data_ref;
} APVRawFrame;

typedef struct APVRawAUInfo {
    uint16_t num_frames;

    uint8_t  pbu_type           [CBS_APV_MAX_AU_FRAMES];
    uint8_t  group_id           [CBS_APV_MAX_AU_FRAMES];
    uint8_t  reserved_zero_8bits[CBS_APV_MAX_AU_FRAMES];
    APVRawFrameInfo frame_info  [CBS_APV_MAX_AU_FRAMES];

    uint8_t  reserved_zero_8bits_2;

    APVRawFiller filler;
} APVRawAUInfo;

typedef struct APVRawMetadataITUTT35 {
    uint8_t  itu_t_t35_country_code;
    uint8_t  itu_t_t35_country_code_extension;

    uint8_t     *data;
    AVBufferRef *data_ref;
    size_t       data_size;
} APVRawMetadataITUTT35;

typedef struct APVRawMetadataMDCV {
    uint16_t primary_chromaticity_x[3];
    uint16_t primary_chromaticity_y[3];
    uint16_t white_point_chromaticity_x;
    uint16_t white_point_chromaticity_y;
    uint32_t max_mastering_luminance;
    uint32_t min_mastering_luminance;
} APVRawMetadataMDCV;

typedef struct APVRawMetadataCLL {
    uint16_t max_cll;
    uint16_t max_fall;
} APVRawMetadataCLL;

typedef struct APVRawMetadataFiller {
    uint32_t payload_size;
} APVRawMetadataFiller;

typedef struct APVRawMetadataUserDefined {
    uint8_t  uuid[16];

    uint8_t     *data;
    AVBufferRef *data_ref;
    size_t       data_size;
} APVRawMetadataUserDefined;

typedef struct APVRawMetadataUndefined {
    uint8_t     *data;
    AVBufferRef *data_ref;
    size_t       data_size;
} APVRawMetadataUndefined;

typedef struct APVRawMetadataPayload {
    uint32_t payload_type;
    uint32_t payload_size;
    union {
        APVRawMetadataITUTT35     itu_t_t35;
        APVRawMetadataMDCV        mdcv;
        APVRawMetadataCLL         cll;
        APVRawMetadataFiller      filler;
        APVRawMetadataUserDefined user_defined;
        APVRawMetadataUndefined   undefined;
    };
} APVRawMetadataPayload;

typedef struct APVRawMetadata {
    APVRawPBUHeader pbu_header;

    uint32_t metadata_size;
    uint32_t metadata_count;

    APVRawMetadataPayload payloads[CBS_APV_MAX_METADATA_PAYLOADS];

    APVRawFiller filler;
} APVRawMetadata;


typedef struct APVDerivedTileInfo {
    uint8_t  tile_cols;
    uint8_t  tile_rows;
    uint16_t num_tiles;
    // The spec uses an extra element on the end of these arrays
    // not corresponding to any tile.
    uint16_t col_starts[APV_MAX_TILE_COLS + 1];
    uint16_t row_starts[APV_MAX_TILE_ROWS + 1];
} APVDerivedTileInfo;

typedef struct CodedBitstreamAPVContext {
    int bit_depth;
    int num_comp;

    APVDerivedTileInfo tile_info;
} CodedBitstreamAPVContext;

#endif /* AVCODEC_CBS_APV_H */
