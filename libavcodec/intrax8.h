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

#ifndef AVCODEC_INTRAX8_H
#define AVCODEC_INTRAX8_H

#include "blockdsp.h"
#include "get_bits.h"
#include "mpegvideo.h"
#include "idctdsp.h"
#include "intrax8dsp.h"
#include "wmv2dsp.h"
#include "mpegpicture.h"

typedef struct IntraX8Context {
    VLC *j_ac_vlc[4]; // they point to the static j_mb_vlc
    VLC *j_orient_vlc;
    VLC *j_dc_vlc[3];

    int use_quant_matrix;

    // set by ff_intrax8_common_init
    uint8_t *prediction_table; // 2 * (mb_w * 2)
    ScanTable scantable[3];
    WMV2DSPContext wdsp;
    uint8_t idct_permutation[64];
    AVCodecContext *avctx;

    //set by the caller codec
    MpegEncContext * s;
    IntraX8DSPContext dsp;
    IDCTDSPContext idsp;
    BlockDSPContext bdsp;
    int quant;
    int dquant;
    int qsum;
    int loopfilter;
    AVFrame *frame;
    GetBitContext *gb;

    // calculated per frame
    int quant_dc_chroma;
    int divide_quant_dc_luma;
    int divide_quant_dc_chroma;
    uint8_t *dest[3];
    uint8_t scratchpad[42]; // size of the block is fixed (8x8 plus padding)

    // changed per block
    int edges;
    int flat_dc;
    int predicted_dc;
    int raw_orient;
    int chroma_orient;
    int orient;
    int est_run;
} IntraX8Context;

/**
 * Initialize IntraX8 frame decoder.
 * Requires valid MpegEncContext with valid s->mb_width before calling.
 * @param avctx pointer to AVCodecContext
 * @param w pointer to IntraX8Context
 * @param idsp pointer to IDCTDSPContext
 * @param s pointer to MpegEncContext of the parent codec
 * @return 0 on success, a negative AVERROR value on error
 */
int ff_intrax8_common_init(AVCodecContext *avctx,
                           IntraX8Context *w, IDCTDSPContext *idsp,
                           MpegEncContext *const s);

/**
 * Destroy IntraX8 frame structure.
 * @param w pointer to IntraX8Context
 */
void ff_intrax8_common_end(IntraX8Context *w);

/**
 * Decode single IntraX8 frame.
 * The parent codec must call ff_mpv_frame_start() before calling this function.
 * The parent codec must call ff_mpv_frame_end() after calling this function.
 * This function does not use ff_mpv_decode_mb().
 * lowres decoding is theoretically impossible.
 * @param w pointer to IntraX8Context
 * @param pict the output Picture containing an AVFrame
 * @param gb open bitstream reader
 * @param dquant doubled quantizer, it would be odd in case of VC-1 halfpq==1.
 * @param quant_offset offset away from zero
 * @param loopfilter enable filter after decoding a block
 */
int ff_intrax8_decode_picture(IntraX8Context *w, Picture *pict,
                              GetBitContext *gb,
                              int quant, int halfpq, int loopfilter);

#endif /* AVCODEC_INTRAX8_H */
