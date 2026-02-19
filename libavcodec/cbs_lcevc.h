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

#ifndef AVCODEC_CBS_LCEVC_H
#define AVCODEC_CBS_LCEVC_H

#include <stddef.h>
#include <stdint.h>

#include "cbs_h2645.h"
#include "cbs_sei.h"
#include "lcevc.h"

typedef struct LCEVCRawNALUnitHeader {
    uint8_t nal_unit_type;
    uint16_t reserved_flag;
} LCEVCRawNALUnitHeader;

typedef struct  LCEVCRawSequenceConfig {
    uint8_t  profile_idc;
    uint8_t  level_idc;
    uint8_t  sublevel_idc;
    uint8_t  conformance_window_flag;
    uint8_t  reserved_zeros_5bit;
    uint8_t  extended_profile_idc;
    uint8_t  extended_level_idc;
    uint8_t  reserved_zeros_1bit;
    uint32_t  conf_win_left_offset;
    uint32_t  conf_win_right_offset;
    uint32_t  conf_win_top_offset;
    uint32_t  conf_win_bottom_offset;
} LCEVCRawSequenceConfig;

typedef struct  LCEVCRawGlobalConfig {
    uint8_t  processed_planes_type_flag;
    uint8_t  resolution_type;
    uint8_t  transform_type;
    uint8_t  chroma_sampling_type;
    uint8_t  base_depth_type;
    uint8_t  enhancement_depth_type;
    uint8_t  temporal_step_width_modifier_signalled_flag;
    uint8_t  predicted_residual_mode_flag;
    uint8_t  temporal_tile_intra_signalling_enabled_flag;
    uint8_t  temporal_enabled_flag;
    uint8_t  upsample_type;
    uint8_t  level1_filtering_signalled_flag;
    uint8_t  scaling_mode_level1;
    uint8_t  scaling_mode_level2;
    uint8_t  tile_dimensions_type;
    uint8_t  user_data_enabled;
    uint8_t  level1_depth_flag;
    uint8_t  chroma_step_width_flag;
    uint8_t  planes_type;
    uint8_t  reserved_zeros_4bit;
    uint8_t  temporal_step_width_modifier;
    uint16_t  upsampler_coeff1;
    uint16_t  upsampler_coeff2;
    uint16_t  upsampler_coeff3;
    uint16_t  upsampler_coeff4;
    uint8_t  level1_filtering_first_coefficient;
    uint8_t  level1_filtering_second_coefficient;
    uint16_t  custom_tile_width;
    uint16_t  custom_tile_height;
    uint16_t  reserved_zeros_5bit;
    uint8_t  compression_type_entropy_enabled_per_tile_flag;
    uint8_t  compression_type_size_per_tile;
    uint16_t  custom_resolution_width;
    uint16_t  custom_resolution_height;
    uint8_t  chroma_step_width_multiplier;
} LCEVCRawGlobalConfig;

typedef struct  LCEVCRawPictureConfig {
    uint8_t  no_enhancement_bit_flag;
    uint8_t  quant_matrix_mode;
    uint8_t  dequant_offset_signalled_flag;
    uint8_t  picture_type_bit_flag;
    uint8_t  temporal_refresh_bit_flag;
    uint8_t  step_width_sublayer1_enabled_flag;
    uint16_t  step_width_sublayer2;
    uint8_t  dithering_control_flag;
    uint8_t  reserved_zeros_4bit;
    uint8_t  temporal_signalling_present_flag;
    uint8_t  field_type_bit_flag;
    uint8_t  reserved_zeros_7bit;
    uint16_t  step_width_sublayer1;
    uint8_t  level1_filtering_enabled_flag;
    uint8_t  qm_coefficient_0[16];
    uint8_t  qm_coefficient_1[16];
    uint8_t  dequant_offset_mode_flag;
    uint8_t  dequant_offset;
    uint8_t  dithering_type;
    uint8_t  reserverd_zero;
    uint8_t  dithering_strength;
    uint8_t  reserved_zeros_5bit;

    LCEVCRawGlobalConfig   *gc; ///< RefStruct references
} LCEVCRawPictureConfig;

typedef struct LCEVCRawEncodedData {
    LCEVCRawNALUnitHeader nal_unit_header;

    uint8_t surfaces_entropy_enabled_flag[3][3][16];
    uint8_t surfaces_rle_only_flag[3][3][16];
    uint8_t temporal_surfaces_entropy_enabled_flag[3];
    uint8_t temporal_surfaces_rle_only_flag[3];

    uint8_t     *data;
    AVBufferRef *data_ref;
    size_t       header_size;
    size_t       data_size;

    LCEVCRawSequenceConfig *sc; ///< RefStruct references
    LCEVCRawGlobalConfig   *gc; ///< RefStruct references
    LCEVCRawPictureConfig  *pc; ///< RefStruct references
} LCEVCRawEncodedData;

typedef struct LCEVCRawVUI {
    uint8_t  aspect_ratio_info_present_flag;
    uint8_t  aspect_ratio_idc;
    uint16_t  sar_width;
    uint8_t  sar_height;
    uint8_t  overscan_info_present_flag;
    uint8_t  overscan_appropriate_flag;
    uint8_t  video_signal_type_present_flag;
    uint8_t  video_format;
    uint8_t  video_full_range_flag;
    uint8_t  colour_description_present_flag;
    uint8_t  colour_primaries;
    uint8_t  transfer_characteristics;
    uint8_t  matrix_coefficients;
    uint8_t  chroma_loc_info_present_flag;
    uint8_t  chroma_sample_loc_type_top_field;
    uint8_t  chroma_sample_loc_type_bottom_field;
} LCEVCRawVUI;

typedef struct LCEVCRawSEI {
    SEIRawMessage message;

    uint8_t       payload_type;
    uint32_t      payload_size;
    void         *payload;
    void         *payload_ref;    ///< RefStruct reference
} LCEVCRawSEI;

typedef struct LCEVCRawAdditionalInfo {
    uint8_t additional_info_type;

    LCEVCRawSEI           sei;
    LCEVCRawVUI           vui;

    uint32_t     payload_size;
    void        *payload;
    void        *payload_ref;    ///< RefStruct reference
} LCEVCRawAdditionalInfo;

typedef struct LCEVCRawFiller {
    uint32_t filler_size;
} LCEVCRawFiller;

typedef struct LCEVCRawProcessBlock {
    uint32_t     payload_type;
    uint32_t     payload_size;
    void        *payload;
    void        *payload_ref;    ///< RefStruct reference
    uint8_t     *extension_data; ///< RefStruct reference
    size_t       extension_bit_length;
} LCEVCRawProcessBlock;

typedef struct LCEVCRawProcessBlockList {
    LCEVCRawProcessBlock *blocks;
    int         nb_blocks;
    int         nb_blocks_allocated;
} LCEVCRawProcessBlockList;

typedef struct LCEVCRawNAL {
    LCEVCRawNALUnitHeader nal_unit_header;

    LCEVCRawProcessBlockList process_block_list;
} LCEVCRawNAL;

typedef struct LCEVCProcessBlockState {
    // The type of the payload being written.
    uint32_t payload_type;
    // When reading, contains the size of the payload to allow finding the
    // end of variable-length fields (such as user_data_payload_byte[]).
    // (When writing, the size will be derived from the total number of
    // bytes actually written.)
    uint32_t payload_size;
    // When writing, indicates that payload extension data is present so
    // all extended fields must be written.  May be updated by the writer
    // to indicate that extended fields have been written, so the extension
    // end bits must be written too.
    uint8_t  extension_present;
} LCEVCProcessBlockState;

typedef int (*LCEVCRawProcessBlockReadFunction)(CodedBitstreamContext *ctx,
                                      struct GetBitContext *rw,
                                      void *current,
                                      LCEVCProcessBlockState *state,
                                      int nal_unit_type);

typedef int (*LCEVCRawProcessBlockWriteFunction)(CodedBitstreamContext *ctx,
                                       struct PutBitContext *rw,
                                       void *current,
                                       LCEVCProcessBlockState *state,
                                       int nal_unit_type);

typedef struct LCEVCProcessBlockTypeDescriptor {
    // Payload type for the block.  (-1 in this field ends a list.)
    int     payload_type;
    // Size of the decomposed structure.
    size_t  payload_size;
    // Read bitstream into Process Block.
    LCEVCRawProcessBlockReadFunction  read;
    // Write bitstream from Process Block.
    LCEVCRawProcessBlockWriteFunction write;
} LCEVCProcessBlockTypeDescriptor;

// End-of-list sentinel element.
#define LCEVC_PROCESS_BLOCK_TYPE_END { .payload_type = -1 }

typedef struct CodedBitstreamLCEVCContext {
    // Reader/writer context in common with the H.264 implementation.
    CodedBitstreamH2645Context common;

    int dithering_control_flag;

    // All currently available parameter sets.  These are updated when
    // any parameter set NAL unit is read/written with this context.
    LCEVCRawSequenceConfig *sc; ///< RefStruct references
    LCEVCRawGlobalConfig   *gc; ///< RefStruct references
    LCEVCRawPictureConfig  *pc; ///< RefStruct references
} CodedBitstreamLCEVCContext;

/**
 * Find the type descriptor for the given payload type.
 *
 * Returns NULL if the payload type is not known.
 */
const LCEVCProcessBlockTypeDescriptor *ff_cbs_lcevc_process_block_find_type(CodedBitstreamContext *ctx,
                                                     int payload_type);

/**
 * Allocate a new payload for the given Process Block.
 */
int ff_cbs_lcevc_alloc_process_block_payload(LCEVCRawProcessBlock *block,
                                     const LCEVCProcessBlockTypeDescriptor *desc);

/**
 * Allocate a new empty Process Block in a block list at a given position.
 */
int ff_cbs_lcevc_list_add(LCEVCRawProcessBlockList *list, int position);

/**
 * Free all Process Block in a block list.
 */
void ff_cbs_lcevc_free_process_block_list(LCEVCRawProcessBlockList *list);

/**
 * Add a process block to an access unit.
 *
 * An existing NAL unit of type IDR or NON_IDR are required.
 *
 * If set, payload_ref must be a RefStruct reference backing payload_data.
 * This function creates a new reference to payload_ref in this case.
 * If payload_ref is NULL, the new message will not be reference counted.
 */
int ff_cbs_lcevc_add_process_block(CodedBitstreamContext *ctx,
                                   CodedBitstreamFragment *au,
                                   int position,
                                   uint32_t     payload_type,
                                   void        *payload_data,
                                   void        *payload_ref);

/**
 * Iterate over blocks with the given payload type in an access unit.
 *
 * Set block to NULL in the first call. Returns 0 while more blocks
 * are available, AVERROR(ENOENT) when all blocks have been found.
 */
int ff_cbs_lcevc_find_process_block(CodedBitstreamContext *ctx,
                                    CodedBitstreamFragment *au,
                                    uint32_t payload_type,
                                    LCEVCRawProcessBlock **block);

/**
 * Delete all blocks with the given payload type from an access unit.
 */
void ff_cbs_lcevc_delete_process_block_type(CodedBitstreamContext *ctx,
                                            CodedBitstreamFragment *au,
                                            uint32_t payload_type);

#endif /* AVCODEC_CBS_LCEVC_H */
