/*
 * HEVC Supplementary Enhancement Information messages
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

#ifndef AVCODEC_HEVC_SEI_H
#define AVCODEC_HEVC_SEI_H

#include <stdint.h>

#include "libavutil/buffer.h"

#include "get_bits.h"
#include "hevc.h"
#include "h2645_sei.h"
#include "sei.h"


typedef enum {
        HEVC_SEI_PIC_STRUCT_FRAME_DOUBLING = 7,
        HEVC_SEI_PIC_STRUCT_FRAME_TRIPLING = 8
} HEVC_SEI_PicStructType;

typedef struct HEVCSEIPictureHash {
    uint8_t       md5[3][16];
    uint8_t is_md5;
} HEVCSEIPictureHash;

typedef struct HEVCSEIFramePacking {
    int present;
    int arrangement_type;
    int content_interpretation_type;
    int quincunx_subsampling;
    int current_frame_is_frame0_flag;
} HEVCSEIFramePacking;

typedef struct HEVCSEIPictureTiming {
    int picture_struct;
} HEVCSEIPictureTiming;

typedef struct HEVCSEIMasteringDisplay {
    int present;
    uint16_t display_primaries[3][2];
    uint16_t white_point[2];
    uint32_t max_luminance;
    uint32_t min_luminance;
} HEVCSEIMasteringDisplay;

typedef struct HEVCSEIContentLight {
    int present;
    uint16_t max_content_light_level;
    uint16_t max_pic_average_light_level;
} HEVCSEIContentLight;

typedef struct HEVCSEIAlternativeTransfer {
    int present;
    int preferred_transfer_characteristics;
} HEVCSEIAlternativeTransfer;

typedef struct HEVCSEITimeCode {
    int      present;
    uint8_t  num_clock_ts;
    uint8_t  clock_timestamp_flag[3];
    uint8_t  units_field_based_flag[3];
    uint8_t  counting_type[3];
    uint8_t  full_timestamp_flag[3];
    uint8_t  discontinuity_flag[3];
    uint8_t  cnt_dropped_flag[3];
    uint16_t n_frames[3];
    uint8_t  seconds_value[3];
    uint8_t  minutes_value[3];
    uint8_t  hours_value[3];
    uint8_t  seconds_flag[3];
    uint8_t  minutes_flag[3];
    uint8_t  hours_flag[3];
    uint8_t  time_offset_length[3];
    int32_t  time_offset_value[3];
} HEVCSEITimeCode;

typedef struct HEVCSEI {
    H2645SEI common;
    HEVCSEIPictureHash picture_hash;
    HEVCSEIPictureTiming picture_timing;
    HEVCSEIMasteringDisplay mastering_display;
    HEVCSEIContentLight content_light;
    int active_seq_parameter_set_id;
    HEVCSEITimeCode timecode;
} HEVCSEI;

struct HEVCParamSets;

int ff_hevc_decode_nal_sei(GetBitContext *gb, void *logctx, HEVCSEI *s,
                           const struct HEVCParamSets *ps, enum HEVCNALUnitType type);

static inline int ff_hevc_sei_ctx_replace(HEVCSEI *dst, const HEVCSEI *src)
{
    return ff_h2645_sei_ctx_replace(&dst->common, &src->common);
}

/**
 * Reset SEI values that are stored on the Context.
 * e.g. Caption data that was extracted during NAL
 * parsing.
 *
 * @param sei HEVCSEI.
 */
static inline void ff_hevc_reset_sei(HEVCSEI *sei)
{
    ff_h2645_sei_reset(&sei->common);
}

#endif /* AVCODEC_HEVC_SEI_H */
