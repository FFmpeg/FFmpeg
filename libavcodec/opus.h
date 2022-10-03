/*
 * Opus common header
 * Copyright (c) 2012 Andrew D'Addesio
 * Copyright (c) 2013-2014 Mozilla Corporation
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

#ifndef AVCODEC_OPUS_H
#define AVCODEC_OPUS_H

#include <stdint.h>

#include "avcodec.h"
#include "opus_rc.h"

#define MAX_FRAME_SIZE               1275
#define MAX_FRAMES                   48
#define MAX_PACKET_DUR               5760

#define CELT_SHORT_BLOCKSIZE         120
#define CELT_OVERLAP                 CELT_SHORT_BLOCKSIZE
#define CELT_MAX_LOG_BLOCKS          3
#define CELT_MAX_FRAME_SIZE          (CELT_SHORT_BLOCKSIZE * (1 << CELT_MAX_LOG_BLOCKS))
#define CELT_MAX_BANDS               21

#define SILK_HISTORY                 322
#define SILK_MAX_LPC                 16

#define ROUND_MULL(a,b,s) (((MUL64(a, b) >> ((s) - 1)) + 1) >> 1)
#define ROUND_MUL16(a,b)  ((MUL16(a, b) + 16384) >> 15)

#define OPUS_TS_HEADER     0x7FE0        // 0x3ff (11 bits)
#define OPUS_TS_MASK       0xFFE0        // top 11 bits

static const uint8_t opus_default_extradata[30] = {
    'O', 'p', 'u', 's', 'H', 'e', 'a', 'd',
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

enum OpusMode {
    OPUS_MODE_SILK,
    OPUS_MODE_HYBRID,
    OPUS_MODE_CELT,

    OPUS_MODE_NB
};

enum OpusBandwidth {
    OPUS_BANDWIDTH_NARROWBAND,
    OPUS_BANDWIDTH_MEDIUMBAND,
    OPUS_BANDWIDTH_WIDEBAND,
    OPUS_BANDWIDTH_SUPERWIDEBAND,
    OPUS_BANDWIDTH_FULLBAND,

    OPUS_BANDWITH_NB
};

typedef struct SilkContext SilkContext;

typedef struct CeltFrame CeltFrame;

int ff_silk_init(AVCodecContext *avctx, SilkContext **ps, int output_channels);
void ff_silk_free(SilkContext **ps);
void ff_silk_flush(SilkContext *s);

/**
 * Decode the LP layer of one Opus frame (which may correspond to several SILK
 * frames).
 */
int ff_silk_decode_superframe(SilkContext *s, OpusRangeCoder *rc,
                              float *output[2],
                              enum OpusBandwidth bandwidth, int coded_channels,
                              int duration_ms);

/* Encode or decode CELT bands */
void ff_celt_quant_bands(CeltFrame *f, OpusRangeCoder *rc);

/* Encode or decode CELT bitallocation */
void ff_celt_bitalloc(CeltFrame *f, OpusRangeCoder *rc, int encode);

#endif /* AVCODEC_OPUS_H */
