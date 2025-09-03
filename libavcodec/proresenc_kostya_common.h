/*
 * Apple ProRes encoder
 *
 * Copyright (c) 2011 Anatoliy Wasserman
 * Copyright (c) 2012 Konstantin Shishkov
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

#ifndef AVCODEC_PRORESENC_KOSTYA_COMMON_H
#define AVCODEC_PRORESENC_KOSTYA_COMMON_H

#include <stddef.h>

#include "libavutil/attributes_internal.h"
#include "libavutil/log.h"
#include "libavutil/mem_internal.h"
#include "libavutil/pixfmt.h"
#include "fdctdsp.h"

FF_VISIBILITY_PUSH_HIDDEN

#define CFACTOR_Y422 2
#define CFACTOR_Y444 3

#define MAX_MBS_PER_SLICE 8

#define MAX_PLANES 4

#define NUM_MB_LIMITS 4

#define MAX_STORED_Q 16

enum {
    PRORES_PROFILE_AUTO  = -1,
    PRORES_PROFILE_PROXY = 0,
    PRORES_PROFILE_LT,
    PRORES_PROFILE_STANDARD,
    PRORES_PROFILE_HQ,
    PRORES_PROFILE_4444,
    PRORES_PROFILE_4444XQ,
};

enum {
    QUANT_MAT_PROXY = 0,
    QUANT_MAT_PROXY_CHROMA,
    QUANT_MAT_LT,
    QUANT_MAT_STANDARD,
    QUANT_MAT_HQ,
    QUANT_MAT_XQ_LUMA,
    QUANT_MAT_DEFAULT,
};

struct AVCodecContext;
struct AVFrame;

typedef struct prores_profile {
    const char *full_name;
    uint32_t    tag;
    int         min_quant;
    int         max_quant;
    int         br_tab[NUM_MB_LIMITS];
    int         quant;
    int         quant_chroma;
} prores_profile;

typedef struct ProresContext {
    AVClass *class;
    DECLARE_ALIGNED(16, int16_t, blocks)[MAX_PLANES][64 * 4 * MAX_MBS_PER_SLICE];
    DECLARE_ALIGNED(16, uint16_t, emu_buf)[16*16];
    int16_t quants[MAX_STORED_Q][64];
    int16_t quants_chroma[MAX_STORED_Q][64];
    int16_t custom_q[64];
    int16_t custom_chroma_q[64];
    const uint8_t *quant_mat;
    const uint8_t *quant_chroma_mat;
    const uint8_t *scantable;

    void (*fdct)(FDCTDSPContext *fdsp, const uint16_t *src,
                 ptrdiff_t linesize, int16_t *block);
    FDCTDSPContext fdsp;

    const struct AVFrame *pic;
    int mb_width, mb_height;
    int mbs_per_slice;
    int num_chroma_blocks, chroma_factor;
    int slices_width;
    int slices_per_picture;
    int pictures_per_frame; // 1 for progressive, 2 for interlaced
    int cur_picture_idx;
    int num_planes;
    int bits_per_mb;
    int force_quant;
    int alpha_bits;
    int warn;

    char *vendor;
    int quant_sel;

    int frame_size_upper_bound;

    int profile;
    const struct prores_profile *profile_info;

    int *slice_q;

    struct ProresThreadData *tdata;
} ProresContext;

av_cold int ff_prores_kostya_encode_init(struct AVCodecContext *avctx, ProresContext *ctx,
                                         enum AVPixelFormat pixfmt);

uint8_t *ff_prores_kostya_write_frame_header(struct AVCodecContext *avctx, ProresContext *ctx,
                                             uint8_t **orig_buf, int flags,
                                             enum AVColorPrimaries color_primaries,
                                             enum AVColorTransferCharacteristic color_trc,
                                             enum AVColorSpace colorspace);

uint8_t *ff_prores_kostya_write_picture_header(ProresContext *ctx, uint8_t *buf);

FF_VISIBILITY_POP_HIDDEN

#endif // AVCODEC_PRORESENC_KOSTYA_COMMON_H
