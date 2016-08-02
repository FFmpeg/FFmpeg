/*
 * VP9 compatible video decoder
 *
 * Copyright (C) 2013 Ronald S. Bultje <rsbultje gmail com>
 * Copyright (C) 2013 Clément Bœsch <u pkh me>
 *
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

#ifndef AVCODEC_VP9_H
#define AVCODEC_VP9_H

#include <stddef.h>
#include <stdint.h>

#include "libavutil/buffer.h"
#include "libavutil/internal.h"

#include "avcodec.h"
#include "thread.h"
#include "vp56.h"

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

enum FilterMode {
    FILTER_8TAP_SMOOTH,
    FILTER_8TAP_REGULAR,
    FILTER_8TAP_SHARP,
    FILTER_BILINEAR,
    FILTER_SWITCHABLE,
};

enum BlockPartition {
    PARTITION_NONE,    // [ ] <-.
    PARTITION_H,       // [-]   |
    PARTITION_V,       // [|]   |
    PARTITION_SPLIT,   // [+] --'
};

enum InterPredMode {
    NEARESTMV = 10,
    NEARMV    = 11,
    ZEROMV    = 12,
    NEWMV     = 13,
};

enum MVJoint {
    MV_JOINT_ZERO,
    MV_JOINT_H,
    MV_JOINT_V,
    MV_JOINT_HV,
};

typedef struct ProbContext {
    uint8_t y_mode[4][9];
    uint8_t uv_mode[10][9];
    uint8_t filter[4][2];
    uint8_t mv_mode[7][3];
    uint8_t intra[4];
    uint8_t comp[5];
    uint8_t single_ref[5][2];
    uint8_t comp_ref[5];
    uint8_t tx32p[2][3];
    uint8_t tx16p[2][2];
    uint8_t tx8p[2];
    uint8_t skip[3];
    uint8_t mv_joint[3];
    struct {
        uint8_t sign;
        uint8_t classes[10];
        uint8_t class0;
        uint8_t bits[10];
        uint8_t class0_fp[2][3];
        uint8_t fp[3];
        uint8_t class0_hp;
        uint8_t hp;
    } mv_comp[2];
    uint8_t partition[4][4][3];
} ProbContext;

typedef void (*vp9_mc_func)(uint8_t *dst, const uint8_t *ref,
                            ptrdiff_t dst_stride,
                            ptrdiff_t ref_stride,
                            int h, int mx, int my);

typedef struct VP9DSPContext {
    /*
     * dimension 1: 0=4x4, 1=8x8, 2=16x16, 3=32x32
     * dimension 2: intra prediction modes
     *
     * dst/left/top is aligned by transform-size (i.e. 4, 8, 16 or 32 pixels)
     * stride is aligned by 16 pixels
     * top[-1] is top/left; top[4,7] is top-right for 4x4
     */
    // FIXME(rbultje) maybe replace left/top pointers with HAVE_TOP/
    // HAVE_LEFT/HAVE_TOPRIGHT flags instead, and then handle it in-place?
    // also needs to fit in with what H.264/VP8/etc do
    void (*intra_pred[N_TXFM_SIZES][N_INTRA_PRED_MODES])(uint8_t *dst,
                                                         ptrdiff_t stride,
                                                         const uint8_t *left,
                                                         const uint8_t *top);

    /*
     * dimension 1: 0=4x4, 1=8x8, 2=16x16, 3=32x32, 4=lossless (3-4=dct only)
     * dimension 2: 0=dct/dct, 1=dct/adst, 2=adst/dct, 3=adst/adst
     *
     * dst is aligned by transform-size (i.e. 4, 8, 16 or 32 pixels)
     * stride is aligned by 16 pixels
     * block is 16-byte aligned
     * eob indicates the position (+1) of the last non-zero coefficient,
     * in scan-order. This can be used to write faster versions, e.g. a
     * dc-only 4x4/8x8/16x16/32x32, or a 4x4-only (eob<10) 8x8/16x16/32x32,
     * etc.
     */
    // FIXME also write idct_add_block() versions for whole (inter) pred
    // blocks, so we can do 2 4x4s at once
    void (*itxfm_add[N_TXFM_SIZES + 1][N_TXFM_TYPES])(uint8_t *dst,
                                                      ptrdiff_t stride,
                                                      int16_t *block, int eob);

    /*
     * dimension 1: width of filter (0=4, 1=8, 2=16)
     * dimension 2: 0=col-edge filter (h), 1=row-edge filter (v)
     *
     * dst/stride are aligned by 8
     */
    void (*loop_filter_8[3][2])(uint8_t *dst, ptrdiff_t stride,
                                int mb_lim, int lim, int hev_thr);

    /*
     * dimension 1: 0=col-edge filter (h), 1=row-edge filter (v)
     *
     * The width of filter is assumed to be 16; dst/stride are aligned by 16
     */
    void (*loop_filter_16[2])(uint8_t *dst, ptrdiff_t stride,
                              int mb_lim, int lim, int hev_thr);

    /*
     * dimension 1/2: width of filter (0=4, 1=8) for each filter half
     * dimension 3: 0=col-edge filter (h), 1=row-edge filter (v)
     *
     * dst/stride are aligned by operation size
     * this basically calls loop_filter[d1][d3][0](), followed by
     * loop_filter[d2][d3][0]() on the next 8 pixels
     * mb_lim/lim/hev_thr contain two values in the lowest two bytes of the
     * integer.
     */
    // FIXME perhaps a mix4 that operates on 32px (for AVX2)
    void (*loop_filter_mix2[2][2][2])(uint8_t *dst, ptrdiff_t stride,
                                      int mb_lim, int lim, int hev_thr);

    /*
     * dimension 1: hsize (0: 64, 1: 32, 2: 16, 3: 8, 4: 4)
     * dimension 2: filter type (0: smooth, 1: regular, 2: sharp, 3: bilin)
     * dimension 3: averaging type (0: put, 1: avg)
     * dimension 4: x subpel interpolation (0: none, 1: 8tap/bilin)
     * dimension 5: y subpel interpolation (1: none, 1: 8tap/bilin)
     *
     * dst/stride are aligned by hsize
     */
    vp9_mc_func mc[5][4][2][2][2];
} VP9DSPContext;

enum CompPredMode {
    PRED_SINGLEREF,
    PRED_COMPREF,
    PRED_SWITCHABLE,
};

typedef struct VP9MVRefPair {
    VP56mv mv[2];
    int8_t ref[2];
} VP9MVRefPair;

typedef struct VP9Filter {
    uint8_t level[8 * 8];
    uint8_t /* bit=col */ mask[2 /* 0=y, 1=uv */][2 /* 0=col, 1=row */]
                              [8 /* rows */][4 /* 0=16, 1=8, 2=4, 3=inner4 */];
} VP9Filter;

typedef struct VP9Frame {
    ThreadFrame tf;

    uint8_t *segmentation_map;
    VP9MVRefPair *mv;

    AVBufferRef *segmentation_map_buf;
    AVBufferRef *mv_buf;
} VP9Frame;

enum BlockLevel {
    BL_64X64,
    BL_32X32,
    BL_16X16,
    BL_8X8,
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

typedef struct VP9Block {
    uint8_t seg_id, intra, comp, ref[2], mode[4], uvmode, skip;
    enum FilterMode filter;
    VP56mv mv[4 /* b_idx */][2 /* ref */];
    enum BlockSize bs;
    enum TxfmMode tx, uvtx;

    int row, row7, col, col7;
    uint8_t *dst[3];
    ptrdiff_t y_stride, uv_stride;

    enum BlockLevel bl;
    enum BlockPartition bp;
} VP9Block;

typedef struct VP9Context {
    VP9DSPContext dsp;
    VideoDSPContext vdsp;
    GetBitContext gb;
    VP56RangeCoder c;
    VP56RangeCoder *c_b;
    unsigned c_b_size;
    VP9Block *b;
    VP9Block *b_base;

    int alloc_width;
    int alloc_height;

    int pass;
    int uses_2pass;
    int last_uses_2pass;
    int setup_finished;

    // bitstream header
    uint8_t profile;
    uint8_t keyframe, last_keyframe;
    uint8_t invisible;
    uint8_t use_last_frame_mvs;
    uint8_t errorres;
    uint8_t colorspace;
    uint8_t sub_x;
    uint8_t sub_y;
    uint8_t fullrange;
    uint8_t intraonly;
    uint8_t resetctx;
    uint8_t refreshrefmask;
    uint8_t highprecisionmvs;
    enum FilterMode filtermode;
    uint8_t allowcompinter;
    uint8_t fixcompref;
    uint8_t refreshctx;
    uint8_t parallelmode;
    uint8_t framectxid;
    uint8_t refidx[3];
    uint8_t signbias[3];
    uint8_t varcompref[2];

    ThreadFrame refs[8];

#define CUR_FRAME 0
#define LAST_FRAME 1
    VP9Frame frames[2];

    struct {
        uint8_t level;
        int8_t sharpness;
        uint8_t lim_lut[64];
        uint8_t mblim_lut[64];
    } filter;
    struct {
        uint8_t enabled;
        int8_t mode[2];
        int8_t ref[4];
    } lf_delta;
    uint8_t yac_qi;
    int8_t ydc_qdelta, uvdc_qdelta, uvac_qdelta;
    uint8_t lossless;
    struct {
        uint8_t enabled;
        uint8_t temporal;
        uint8_t absolute_vals;
        uint8_t update_map;
        #define MAX_SEGMENT 8
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
    struct {
        unsigned log2_tile_cols, log2_tile_rows;
        unsigned tile_cols, tile_rows;
        unsigned tile_row_start, tile_row_end, tile_col_start, tile_col_end;
    } tiling;
    unsigned sb_cols, sb_rows, rows, cols;
    struct {
        ProbContext p;
        uint8_t coef[4][2][2][6][6][3];
    } prob_ctx[4];
    struct {
        ProbContext p;
        uint8_t coef[4][2][2][6][6][11];
        uint8_t seg[7];
        uint8_t segpred[3];
    } prob;
    struct {
        unsigned y_mode[4][10];
        unsigned uv_mode[10][10];
        unsigned filter[4][3];
        unsigned mv_mode[7][4];
        unsigned intra[4][2];
        unsigned comp[5][2];
        unsigned single_ref[5][2][2];
        unsigned comp_ref[5][2];
        unsigned tx32p[2][4];
        unsigned tx16p[2][3];
        unsigned tx8p[2][2];
        unsigned skip[3][2];
        unsigned mv_joint[4];
        struct {
            unsigned sign[2];
            unsigned classes[11];
            unsigned class0[2];
            unsigned bits[10][2];
            unsigned class0_fp[2][4];
            unsigned fp[4];
            unsigned class0_hp[2];
            unsigned hp[2];
        } mv_comp[2];
        unsigned partition[4][4][4];
        unsigned coef[4][2][2][6][6][3];
        unsigned eob[4][2][2][6][6][2];
    } counts;
    enum TxfmMode txfmmode;
    enum CompPredMode comppredmode;

    // contextual (left/above) cache
    uint8_t left_partition_ctx[8], *above_partition_ctx;
    uint8_t left_mode_ctx[16], *above_mode_ctx;
    // FIXME maybe merge some of the below in a flags field?
    uint8_t left_y_nnz_ctx[16], *above_y_nnz_ctx;
    uint8_t left_uv_nnz_ctx[2][8], *above_uv_nnz_ctx[2];
    uint8_t left_skip_ctx[8], *above_skip_ctx; // 1bit
    uint8_t left_txfm_ctx[8], *above_txfm_ctx; // 2bit
    uint8_t left_segpred_ctx[8], *above_segpred_ctx; // 1bit
    uint8_t left_intra_ctx[8], *above_intra_ctx; // 1bit
    uint8_t left_comp_ctx[8], *above_comp_ctx; // 1bit
    uint8_t left_ref_ctx[8], *above_ref_ctx; // 2bit
    uint8_t left_filter_ctx[8], *above_filter_ctx;
    VP56mv left_mv_ctx[16][2], (*above_mv_ctx)[2];

    // whole-frame cache
    uint8_t *intra_pred_data[3];
    VP9Filter *lflvl;
    DECLARE_ALIGNED(32, uint8_t, edge_emu_buffer)[71 * 80];

    // block reconstruction intermediates
    int16_t *block_base, *block, *uvblock_base[2], *uvblock[2];
    uint8_t *eob_base, *uveob_base[2], *eob, *uveob[2];
    struct { int x, y; } min_mv, max_mv;
    DECLARE_ALIGNED(32, uint8_t, tmp_y)[64 * 64];
    DECLARE_ALIGNED(32, uint8_t, tmp_uv)[2][32 * 32];
} VP9Context;

void ff_vp9dsp_init(VP9DSPContext *dsp);

void ff_vp9dsp_init_x86(VP9DSPContext *dsp);

void ff_vp9_fill_mv(VP9Context *s, VP56mv *mv, int mode, int sb);

void ff_vp9_adapt_probs(VP9Context *s);

int ff_vp9_decode_block(AVCodecContext *avctx, int row, int col,
                        VP9Filter *lflvl, ptrdiff_t yoff, ptrdiff_t uvoff,
                        enum BlockLevel bl, enum BlockPartition bp);

#endif /* AVCODEC_VP9_H */
