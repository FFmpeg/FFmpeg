/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_H264_SEI_H
#define AVCODEC_H264_SEI_H

#include "get_bits.h"

/**
 * SEI message types
 */
typedef enum {
    SEI_TYPE_BUFFERING_PERIOD       = 0,   ///< buffering period (H.264, D.1.1)
    SEI_TYPE_PIC_TIMING             = 1,   ///< picture timing
    SEI_TYPE_USER_DATA_REGISTERED   = 4,   ///< registered user data as specified by Rec. ITU-T T.35
    SEI_TYPE_USER_DATA_UNREGISTERED = 5,   ///< unregistered user data
    SEI_TYPE_RECOVERY_POINT         = 6,   ///< recovery point (frame # to decoder sync)
    SEI_TYPE_FRAME_PACKING          = 45,  ///< frame packing arrangement
    SEI_TYPE_DISPLAY_ORIENTATION    = 47,  ///< display orientation
} SEI_Type;

/**
 * pic_struct in picture timing SEI message
 */
typedef enum {
    SEI_PIC_STRUCT_FRAME             = 0, ///<  0: %frame
    SEI_PIC_STRUCT_TOP_FIELD         = 1, ///<  1: top field
    SEI_PIC_STRUCT_BOTTOM_FIELD      = 2, ///<  2: bottom field
    SEI_PIC_STRUCT_TOP_BOTTOM        = 3, ///<  3: top field, bottom field, in that order
    SEI_PIC_STRUCT_BOTTOM_TOP        = 4, ///<  4: bottom field, top field, in that order
    SEI_PIC_STRUCT_TOP_BOTTOM_TOP    = 5, ///<  5: top field, bottom field, top field repeated, in that order
    SEI_PIC_STRUCT_BOTTOM_TOP_BOTTOM = 6, ///<  6: bottom field, top field, bottom field repeated, in that order
    SEI_PIC_STRUCT_FRAME_DOUBLING    = 7, ///<  7: %frame doubling
    SEI_PIC_STRUCT_FRAME_TRIPLING    = 8  ///<  8: %frame tripling
} SEI_PicStructType;

typedef struct H264SEIPictureTiming {
    SEI_PicStructType pic_struct;

    /**
     * Bit set of clock types for fields/frames in picture timing SEI message.
     * For each found ct_type, appropriate bit is set (e.g., bit 1 for
     * interlaced).
     */
    int ct_type;

    /**
     * dpb_output_delay in picture timing SEI message, see H.264 C.2.2
     */
    int dpb_output_delay;

    /**
     * cpb_removal_delay in picture timing SEI message, see H.264 C.1.2
     */
    int cpb_removal_delay;
} H264SEIPictureTiming;

typedef struct H264SEIAFD {
    int present;
    uint8_t active_format_description;
} H264SEIAFD;

typedef struct H264SEIA53Caption {
    int a53_caption_size;
    uint8_t *a53_caption;
} H264SEIA53Caption;

typedef struct H264SEIUnregistered {
    int x264_build;
} H264SEIUnregistered;

typedef struct H264SEIRecoveryPoint {
    /**
     * recovery_frame_cnt
     *
     * Set to -1 if no recovery point SEI message found or to number of frames
     * before playback synchronizes. Frames having recovery point are key
     * frames.
     */
    int recovery_frame_cnt;
} H264SEIRecoveryPoint;

typedef struct H264SEIBufferingPeriod {
    int present;   ///< Buffering period SEI flag
    int initial_cpb_removal_delay[32];  ///< Initial timestamps for CPBs
} H264SEIBufferingPeriod;

typedef struct H264SEIFramePacking {
    int present;
    int arrangement_type;
    int content_interpretation_type;
    int quincunx_subsampling;
} H264SEIFramePacking;

typedef struct H264SEIDisplayOrientation {
    int present;
    int anticlockwise_rotation;
    int hflip, vflip;
} H264SEIDisplayOrientation;

typedef struct H264SEIContext {
    H264SEIPictureTiming picture_timing;
    H264SEIAFD afd;
    H264SEIA53Caption a53_caption;
    H264SEIUnregistered unregistered;
    H264SEIRecoveryPoint recovery_point;
    H264SEIBufferingPeriod buffering_period;
    H264SEIFramePacking frame_packing;
    H264SEIDisplayOrientation display_orientation;
} H264SEIContext;

struct H264ParamSets;

int ff_h264_sei_decode(H264SEIContext *h, GetBitContext *gb,
                       const struct H264ParamSets *ps, void *logctx);

/**
 * Reset SEI values at the beginning of the frame.
 */
void ff_h264_sei_uninit(H264SEIContext *h);

#endif /* AVCODEC_H264_SEI_H */
