/**
 * VP8 compatible video decoder
 *
 * Copyright (C) 2010 David Conrad
 * Copyright (C) 2010 Ronald S. Bultje
 * Copyright (C) 2010 Jason Garrett-Glaser
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

#ifndef AVCODEC_VP8_H
#define AVCODEC_VP8_H

#include "vp56.h"
#include "vp56data.h"
#include "vp8dsp.h"
#include "h264pred.h"

#define VP8_MAX_QUANT 127

enum dct_token {
    DCT_0,
    DCT_1,
    DCT_2,
    DCT_3,
    DCT_4,
    DCT_CAT1,
    DCT_CAT2,
    DCT_CAT3,
    DCT_CAT4,
    DCT_CAT5,
    DCT_CAT6,
    DCT_EOB,

    NUM_DCT_TOKENS
};

// used to signal 4x4 intra pred in luma MBs
#define MODE_I4x4 4

enum inter_mvmode {
    VP8_MVMODE_ZERO = MODE_I4x4 + 1,
    VP8_MVMODE_MV,
    VP8_MVMODE_SPLIT
};

enum inter_splitmvmode {
    VP8_SPLITMVMODE_16x8 = 0,    ///< 2 16x8 blocks (vertical)
    VP8_SPLITMVMODE_8x16,        ///< 2 8x16 blocks (horizontal)
    VP8_SPLITMVMODE_8x8,         ///< 2x2 blocks of 8x8px each
    VP8_SPLITMVMODE_4x4,         ///< 4x4 blocks of 4x4px each
    VP8_SPLITMVMODE_NONE,        ///< (only used in prediction) no split MVs
};

typedef struct {
    uint8_t filter_level;
    uint8_t inner_limit;
    uint8_t inner_filter;
} VP8FilterStrength;

typedef struct {
    uint8_t skip;
    // todo: make it possible to check for at least (i4x4 or split_mv)
    // in one op. are others needed?
    uint8_t mode;
    uint8_t ref_frame;
    uint8_t partitioning;
    VP56mv mv;
    VP56mv bmv[16];
} VP8Macroblock;

typedef struct {
    AVCodecContext *avctx;
    AVFrame *framep[4];
    AVFrame *next_framep[4];
    uint8_t *edge_emu_buffer;

    uint16_t mb_width;   /* number of horizontal MB */
    uint16_t mb_height;  /* number of vertical MB */
    int linesize;
    int uvlinesize;

    uint8_t keyframe;
    uint8_t deblock_filter;
    uint8_t mbskip_enabled;
    uint8_t segment;             ///< segment of the current macroblock
    uint8_t chroma_pred_mode;    ///< 8x8c pred mode of the current macroblock
    uint8_t profile;
    VP56mv mv_min;
    VP56mv mv_max;

    int8_t sign_bias[4]; ///< one state [0, 1] per ref frame type
    int ref_count[3];

    /**
     * Base parameters for segmentation, i.e. per-macroblock parameters.
     * These must be kept unchanged even if segmentation is not used for
     * a frame, since the values persist between interframes.
     */
    struct {
        uint8_t enabled;
        uint8_t absolute_vals;
        uint8_t update_map;
        int8_t base_quant[4];
        int8_t filter_level[4];     ///< base loop filter level
    } segmentation;

    struct {
        uint8_t simple;
        uint8_t level;
        uint8_t sharpness;
    } filter;

    VP8Macroblock *macroblocks;
    VP8FilterStrength *filter_strength;

    uint8_t *intra4x4_pred_mode_top;
    uint8_t intra4x4_pred_mode_left[4];

    /**
     * Macroblocks can have one of 4 different quants in a frame when
     * segmentation is enabled.
     * If segmentation is disabled, only the first segment's values are used.
     */
    struct {
        // [0] - DC qmul  [1] - AC qmul
        int16_t luma_qmul[2];
        int16_t luma_dc_qmul[2];    ///< luma dc-only block quant
        int16_t chroma_qmul[2];
    } qmat[4];

    struct {
        uint8_t enabled;    ///< whether each mb can have a different strength based on mode/ref

        /**
         * filter strength adjustment for the following macroblock modes:
         * [0-3] - i16x16 (always zero)
         * [4]   - i4x4
         * [5]   - zero mv
         * [6]   - inter modes except for zero or split mv
         * [7]   - split mv
         *  i16x16 modes never have any adjustment
         */
        int8_t mode[VP8_MVMODE_SPLIT+1];

        /**
         * filter strength adjustment for macroblocks that reference:
         * [0] - intra / VP56_FRAME_CURRENT
         * [1] - VP56_FRAME_PREVIOUS
         * [2] - VP56_FRAME_GOLDEN
         * [3] - altref / VP56_FRAME_GOLDEN2
         */
        int8_t ref[4];
    } lf_delta;

    /**
     * Cache of the top row needed for intra prediction
     * 16 for luma, 8 for each chroma plane
     */
    uint8_t (*top_border)[16+8+8];

    /**
     * For coeff decode, we need to know whether the above block had non-zero
     * coefficients. This means for each macroblock, we need data for 4 luma
     * blocks, 2 u blocks, 2 v blocks, and the luma dc block, for a total of 9
     * per macroblock. We keep the last row in top_nnz.
     */
    uint8_t (*top_nnz)[9];
    DECLARE_ALIGNED(8, uint8_t, left_nnz)[9];

    /**
     * This is the index plus one of the last non-zero coeff
     * for each of the blocks in the current macroblock.
     * So, 0 -> no coeffs
     *     1 -> dc-only (special transform)
     *     2+-> full transform
     */
    DECLARE_ALIGNED(16, uint8_t, non_zero_count_cache)[6][4];
    VP56RangeCoder c;   ///< header context, includes mb modes and motion vectors
    DECLARE_ALIGNED(16, DCTELEM, block)[6][4][16];
    DECLARE_ALIGNED(16, DCTELEM, block_dc)[16];
    uint8_t intra4x4_pred_mode_mb[16];

    /**
     * These are all of the updatable probabilities for binary decisions.
     * They are only implictly reset on keyframes, making it quite likely
     * for an interframe to desync if a prior frame's header was corrupt
     * or missing outright!
     */
    struct {
        uint8_t segmentid[3];
        uint8_t mbskip;
        uint8_t intra;
        uint8_t last;
        uint8_t golden;
        uint8_t pred16x16[4];
        uint8_t pred8x8c[3];
        uint8_t token[4][16][3][NUM_DCT_TOKENS-1];
        uint8_t mvc[2][19];
    } prob[2];

    VP8Macroblock *macroblocks_base;
    int invisible;
    int update_last;    ///< update VP56_FRAME_PREVIOUS with the current one
    int update_golden;  ///< VP56_FRAME_NONE if not updated, or which frame to copy if so
    int update_altref;

    /**
     * If this flag is not set, all the probability updates
     * are discarded after this frame is decoded.
     */
    int update_probabilities;

    /**
     * All coefficients are contained in separate arith coding contexts.
     * There can be 1, 2, 4, or 8 of these after the header context.
     */
    int num_coeff_partitions;
    VP56RangeCoder coeff_partition[8];
    DSPContext dsp;
    VP8DSPContext vp8dsp;
    H264PredContext hpc;
    vp8_mc_func put_pixels_tab[3][3][3];
    AVFrame frames[5];

    /**
     * A list of segmentation_map buffers that are to be free()'ed in
     * the next decoding iteration. We can't free() them right away
     * because the map may still be used by subsequent decoding threads.
     * Unused if frame threading is off.
     */
    uint8_t *segmentation_maps[5];
    int num_maps_to_be_freed;
    int maps_are_invalid;
} VP8Context;

#endif /* AVCODEC_VP8_H */
