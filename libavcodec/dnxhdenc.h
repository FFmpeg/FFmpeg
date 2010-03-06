/*
 * VC3/DNxHD encoder structure definitions and prototypes
 * Copyright (c) 2007 Baptiste Coudurier <baptiste dot coudurier at smartjog dot com>
 *
 * VC-3 encoder funded by the British Broadcasting Corporation
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

#ifndef AVCODEC_DNXHDENC_H
#define AVCODEC_DNXHDENC_H

#include <stdint.h>
#include "libavcodec/mpegvideo.h"
#include "libavcodec/dnxhddata.h"

typedef struct {
    uint16_t mb;
    int value;
} RCCMPEntry;

typedef struct {
    int ssd;
    int bits;
} RCEntry;

typedef struct DNXHDEncContext {
    MpegEncContext m; ///< Used for quantization dsp functions

    AVFrame frame;
    int cid;
    const CIDEntry *cid_table;
    uint8_t *msip; ///< Macroblock Scan Indexes Payload
    uint32_t *slice_size;
    uint32_t *slice_offs;

    struct DNXHDEncContext *thread[MAX_THREADS];

    unsigned dct_y_offset;
    unsigned dct_uv_offset;
    int interlaced;
    int cur_field;

    DECLARE_ALIGNED(16, DCTELEM, blocks)[8][64];

    int      (*qmatrix_c)     [64];
    int      (*qmatrix_l)     [64];
    uint16_t (*qmatrix_l16)[2][64];
    uint16_t (*qmatrix_c16)[2][64];

    unsigned frame_bits;
    uint8_t *src[3];

    uint32_t *vlc_codes;
    uint8_t  *vlc_bits;
    uint16_t *run_codes;
    uint8_t  *run_bits;

    /** Rate control */
    unsigned slice_bits;
    unsigned qscale;
    unsigned lambda;

    unsigned thread_size;

    uint16_t *mb_bits;
    uint8_t  *mb_qscale;

    RCCMPEntry *mb_cmp;
    RCEntry   (*mb_rc)[8160];

    void (*get_pixels_8x4_sym)(DCTELEM */*align 16*/, const uint8_t *, int);
} DNXHDEncContext;

void ff_dnxhd_init_mmx(DNXHDEncContext *ctx);

#endif /* AVCODEC_DNXHDENC_H */
