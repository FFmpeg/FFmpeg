/*
 * VP9 compatible video decoder
 *
 * Copyright (C) 2013 Ronald S. Bultje <rsbultje gmail com>
 * Copyright (C) 2013 Clément Bœsch <u pkh me>
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

#ifndef AVCODEC_VP9_H
#define AVCODEC_VP9_H

#include <stdint.h>

#include "thread.h"
#include "vp56.h"

enum BlockLevel {
    BL_64X64,
    BL_32X32,
    BL_16X16,
    BL_8X8,
};

enum BlockPartition {
    PARTITION_NONE,    // [ ] <-.
    PARTITION_H,       // [-]   |
    PARTITION_V,       // [|]   |
    PARTITION_SPLIT,   // [+] --'
};

enum BlockSize {
    BS_64x64,
    BS_64x32,
    BS_32x64,
    BS_32x32,
    BS_32x16,
    BS_16x32,
    BS_16x16,
    BS_16x8,
    BS_8x16,
    BS_8x8,
    BS_8x4,
    BS_4x8,
    BS_4x4,
    N_BS_SIZES,
};

enum TxfmMode {
    TX_4X4,
    TX_8X8,
    TX_16X16,
    TX_32X32,
    N_TXFM_SIZES,
    TX_SWITCHABLE = N_TXFM_SIZES,
    N_TXFM_MODES
};

enum TxfmType {
    DCT_DCT,
    DCT_ADST,
    ADST_DCT,
    ADST_ADST,
    N_TXFM_TYPES
};

enum IntraPredMode {
    VERT_PRED,
    HOR_PRED,
    DC_PRED,
    DIAG_DOWN_LEFT_PRED,
    DIAG_DOWN_RIGHT_PRED,
    VERT_RIGHT_PRED,
    HOR_DOWN_PRED,
    VERT_LEFT_PRED,
    HOR_UP_PRED,
    TM_VP8_PRED,
    LEFT_DC_PRED,
    TOP_DC_PRED,
    DC_128_PRED,
    DC_127_PRED,
    DC_129_PRED,
    N_INTRA_PRED_MODES
};

enum InterPredMode {
    NEARESTMV = 10,
    NEARMV = 11,
    ZEROMV = 12,
    NEWMV = 13,
};

enum FilterMode {
    FILTER_8TAP_SMOOTH,
    FILTER_8TAP_REGULAR,
    FILTER_8TAP_SHARP,
    FILTER_BILINEAR,
    FILTER_SWITCHABLE,
};

enum CompPredMode {
    PRED_SINGLEREF,
    PRED_COMPREF,
    PRED_SWITCHABLE,
};

struct VP9mvrefPair {
    VP56mv mv[2];
    int8_t ref[2];
};

typedef struct VP9Frame {
    ThreadFrame tf;
    AVBufferRef *extradata;
    uint8_t *segmentation_map;
    struct VP9mvrefPair *mv;
    int uses_2pass;

    AVBufferRef *hwaccel_priv_buf;
    void *hwaccel_picture_private;
} VP9Frame;

typedef struct VP9BitstreamHeader {
    // bitstream header
    uint8_t profile;
    uint8_t keyframe;
    uint8_t invisible;
    uint8_t errorres;
    uint8_t intraonly;
    uint8_t resetctx;
    uint8_t refreshrefmask;
    uint8_t highprecisionmvs;
    enum FilterMode filtermode;
    uint8_t allowcompinter;
    uint8_t refreshctx;
    uint8_t parallelmode;
    uint8_t framectxid;
    uint8_t use_last_frame_mvs;
    uint8_t refidx[3];
    uint8_t signbias[3];
    uint8_t fixcompref;
    uint8_t varcompref[2];
    struct {
        uint8_t level;
        int8_t sharpness;
    } filter;
    struct {
        uint8_t enabled;
        uint8_t updated;
        int8_t mode[2];
        int8_t ref[4];
    } lf_delta;
    uint8_t yac_qi;
    int8_t ydc_qdelta, uvdc_qdelta, uvac_qdelta;
    uint8_t lossless;
#define MAX_SEGMENT 8
    struct {
        uint8_t enabled;
        uint8_t temporal;
        uint8_t absolute_vals;
        uint8_t update_map;
        uint8_t prob[7];
        uint8_t pred_prob[3];
        struct {
            uint8_t q_enabled;
            uint8_t lf_enabled;
            uint8_t ref_enabled;
            uint8_t skip_enabled;
            uint8_t ref_val;
            int16_t q_val;
            int8_t lf_val;
            int16_t qmul[2][2];
            uint8_t lflvl[4][2];
        } feat[MAX_SEGMENT];
    } segmentation;
    enum TxfmMode txfmmode;
    enum CompPredMode comppredmode;
    struct {
        unsigned log2_tile_cols, log2_tile_rows;
        unsigned tile_cols, tile_rows;
    } tiling;

    int uncompressed_header_size;
    int compressed_header_size;
} VP9BitstreamHeader;

typedef struct VP9SharedContext {
    VP9BitstreamHeader h;

    ThreadFrame refs[8];
#define CUR_FRAME 0
#define REF_FRAME_MVPAIR 1
#define REF_FRAME_SEGMAP 2
    VP9Frame frames[3];
} VP9SharedContext;

#endif /* AVCODEC_VP9_H */
