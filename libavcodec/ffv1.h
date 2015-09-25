/*
 * FFV1 codec for libavcodec
 *
 * Copyright (c) 2003-2012 Michael Niedermayer <michaelni@gmx.at>
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

#ifndef AVCODEC_FFV1_H
#define AVCODEC_FFV1_H

/**
 * @file
 * FF Video Codec 1 (a lossless codec)
 */

#include "libavutil/avassert.h"
#include "libavutil/crc.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/timer.h"
#include "avcodec.h"
#include "get_bits.h"
#include "internal.h"
#include "mathops.h"
#include "put_bits.h"
#include "rangecoder.h"
#include "thread.h"

#ifdef __INTEL_COMPILER
#undef av_flatten
#define av_flatten
#endif

#define MAX_PLANES 4
#define CONTEXT_SIZE 32

#define MAX_QUANT_TABLES 8
#define MAX_CONTEXT_INPUTS 5

typedef struct VlcState {
    int16_t drift;
    uint16_t error_sum;
    int8_t bias;
    uint8_t count;
} VlcState;

typedef struct PlaneContext {
    int16_t quant_table[MAX_CONTEXT_INPUTS][256];
    int quant_table_index;
    int context_count;
    uint8_t (*state)[CONTEXT_SIZE];
    VlcState *vlc_state;
    uint8_t interlace_bit_state[2];
} PlaneContext;

#define MAX_SLICES 256

typedef struct FFV1Context {
    AVClass *class;
    AVCodecContext *avctx;
    RangeCoder c;
    GetBitContext gb;
    PutBitContext pb;
    uint64_t rc_stat[256][2];
    uint64_t (*rc_stat2[MAX_QUANT_TABLES])[32][2];
    int version;
    int micro_version;
    int width, height;
    int chroma_planes;
    int chroma_h_shift, chroma_v_shift;
    int transparency;
    int flags;
    int picture_number;
    int key_frame;
    ThreadFrame picture, last_picture;
    struct FFV1Context *fsrc;

    AVFrame *cur;
    int plane_count;
    int ac;                              ///< 1=range coder <-> 0=golomb rice
    int ac_byte_count;                   ///< number of bytes used for AC coding
    PlaneContext plane[MAX_PLANES];
    int16_t quant_table[MAX_CONTEXT_INPUTS][256];
    int16_t quant_tables[MAX_QUANT_TABLES][MAX_CONTEXT_INPUTS][256];
    int context_count[MAX_QUANT_TABLES];
    uint8_t state_transition[256];
    uint8_t (*initial_states[MAX_QUANT_TABLES])[32];
    int run_index;
    int colorspace;
    int16_t *sample_buffer;

    int ec;
    int intra;
    int slice_damaged;
    int key_frame_ok;

    int bits_per_raw_sample;
    int packed_at_lsb;

    int gob_count;
    int quant_table_count;

    struct FFV1Context *slice_context[MAX_SLICES];
    int slice_count;
    int max_slice_count;
    int num_v_slices;
    int num_h_slices;
    int slice_width;
    int slice_height;
    int slice_x;
    int slice_y;
    int slice_reset_contexts;
    int slice_coding_mode;
    int slice_rct_by_coef;
    int slice_rct_ry_coef;
} FFV1Context;

int ff_ffv1_common_init(AVCodecContext *avctx);
int ff_ffv1_init_slice_state(FFV1Context *f, FFV1Context *fs);
int ff_ffv1_init_slices_state(FFV1Context *f);
int ff_ffv1_init_slice_contexts(FFV1Context *f);
int ff_ffv1_allocate_initial_states(FFV1Context *f);
void ff_ffv1_clear_slice_state(FFV1Context *f, FFV1Context *fs);
int ff_ffv1_close(AVCodecContext *avctx);

static av_always_inline int fold(int diff, int bits)
{
    if (bits == 8)
        diff = (int8_t)diff;
    else {
        diff +=  1 << (bits  - 1);
        diff  = av_mod_uintp2(diff, bits);
        diff -=  1 << (bits  - 1);
    }

    return diff;
}

static inline int predict(int16_t *src, int16_t *last)
{
    const int LT = last[-1];
    const int T  = last[0];
    const int L  = src[-1];

    return mid_pred(L, L + T - LT, T);
}

static inline int get_context(PlaneContext *p, int16_t *src,
                              int16_t *last, int16_t *last2)
{
    const int LT = last[-1];
    const int T  = last[0];
    const int RT = last[1];
    const int L  = src[-1];

    if (p->quant_table[3][127]) {
        const int TT = last2[0];
        const int LL = src[-2];
        return p->quant_table[0][(L - LT) & 0xFF] +
               p->quant_table[1][(LT - T) & 0xFF] +
               p->quant_table[2][(T - RT) & 0xFF] +
               p->quant_table[3][(LL - L) & 0xFF] +
               p->quant_table[4][(TT - T) & 0xFF];
    } else
        return p->quant_table[0][(L - LT) & 0xFF] +
               p->quant_table[1][(LT - T) & 0xFF] +
               p->quant_table[2][(T - RT) & 0xFF];
}

static inline void update_vlc_state(VlcState *const state, const int v)
{
    int drift = state->drift;
    int count = state->count;
    state->error_sum += FFABS(v);
    drift            += v;

    if (count == 128) { // FIXME: variable
        count            >>= 1;
        drift            >>= 1;
        state->error_sum >>= 1;
    }
    count++;

    if (drift <= -count) {
        if (state->bias > -128)
            state->bias--;

        drift += count;
        if (drift <= -count)
            drift = -count + 1;
    } else if (drift > 0) {
        if (state->bias < 127)
            state->bias++;

        drift -= count;
        if (drift > 0)
            drift = 0;
    }

    state->drift = drift;
    state->count = count;
}

#endif /* AVCODEC_FFV1_H */
