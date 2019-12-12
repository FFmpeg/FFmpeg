/*
 * Bluetooth low-complexity, subband codec (SBC)
 *
 * Copyright (C) 2017  Aurelien Jacobs <aurel@gnuage.org>
 * Copyright (C) 2012-2013  Intel Corporation
 * Copyright (C) 2008-2010  Nokia Corporation
 * Copyright (C) 2004-2010  Marcel Holtmann <marcel@holtmann.org>
 * Copyright (C) 2004-2005  Henryk Ploetz <henryk@ploetzli.ch>
 * Copyright (C) 2005-2006  Brad Midgley <bmidgley@xmission.com>
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

/**
 * @file
 * SBC basic "building bricks"
 */

#include <stdint.h>
#include <limits.h>
#include <string.h>
#include "libavutil/common.h"
#include "libavutil/intmath.h"
#include "libavutil/intreadwrite.h"
#include "sbc.h"
#include "sbcdsp.h"
#include "sbcdsp_data.h"

/*
 * A reference C code of analysis filter with SIMD-friendly tables
 * reordering and code layout. This code can be used to develop platform
 * specific SIMD optimizations. Also it may be used as some kind of test
 * for compiler autovectorization capabilities (who knows, if the compiler
 * is very good at this stuff, hand optimized assembly may be not strictly
 * needed for some platform).
 *
 * Note: It is also possible to make a simple variant of analysis filter,
 * which needs only a single constants table without taking care about
 * even/odd cases. This simple variant of filter can be implemented without
 * input data permutation. The only thing that would be lost is the
 * possibility to use pairwise SIMD multiplications. But for some simple
 * CPU cores without SIMD extensions it can be useful. If anybody is
 * interested in implementing such variant of a filter, sourcecode from
 * bluez versions 4.26/4.27 can be used as a reference and the history of
 * the changes in git repository done around that time may be worth checking.
 */

static av_always_inline void sbc_analyze_simd(const int16_t *in, int32_t *out,
                                              const int16_t *consts,
                                              unsigned subbands)
{
    int32_t t1[8];
    int16_t t2[8];
    int i, j, hop = 0;

    /* rounding coefficient */
    for (i = 0; i < subbands; i++)
        t1[i] = 1 << (SBC_PROTO_FIXED_SCALE - 1);

    /* low pass polyphase filter */
    for (hop = 0; hop < 10*subbands; hop += 2*subbands)
        for (i = 0; i < 2*subbands; i++)
            t1[i >> 1] += in[hop + i] * consts[hop + i];

    /* scaling */
    for (i = 0; i < subbands; i++)
        t2[i] = t1[i] >> SBC_PROTO_FIXED_SCALE;

    memset(t1, 0, sizeof(t1));

    /* do the cos transform */
    for (i = 0; i < subbands/2; i++)
        for (j = 0; j < 2*subbands; j++)
            t1[j>>1] += t2[i * 2 + (j&1)] * consts[10*subbands + i*2*subbands + j];

    for (i = 0; i < subbands; i++)
        out[i] = t1[i] >> (SBC_COS_TABLE_FIXED_SCALE - SCALE_OUT_BITS);
}

static void sbc_analyze_4_simd(const int16_t *in, int32_t *out,
                               const int16_t *consts)
{
    sbc_analyze_simd(in, out, consts, 4);
}

static void sbc_analyze_8_simd(const int16_t *in, int32_t *out,
                               const int16_t *consts)
{
    sbc_analyze_simd(in, out, consts, 8);
}

static inline void sbc_analyze_4b_4s_simd(SBCDSPContext *s,
                                          int16_t *x, int32_t *out, int out_stride)
{
    /* Analyze blocks */
    s->sbc_analyze_4(x + 12, out, ff_sbcdsp_analysis_consts_fixed4_simd_odd);
    out += out_stride;
    s->sbc_analyze_4(x + 8, out, ff_sbcdsp_analysis_consts_fixed4_simd_even);
    out += out_stride;
    s->sbc_analyze_4(x + 4, out, ff_sbcdsp_analysis_consts_fixed4_simd_odd);
    out += out_stride;
    s->sbc_analyze_4(x + 0, out, ff_sbcdsp_analysis_consts_fixed4_simd_even);
}

static inline void sbc_analyze_4b_8s_simd(SBCDSPContext *s,
                                          int16_t *x, int32_t *out, int out_stride)
{
    /* Analyze blocks */
    s->sbc_analyze_8(x + 24, out, ff_sbcdsp_analysis_consts_fixed8_simd_odd);
    out += out_stride;
    s->sbc_analyze_8(x + 16, out, ff_sbcdsp_analysis_consts_fixed8_simd_even);
    out += out_stride;
    s->sbc_analyze_8(x + 8, out, ff_sbcdsp_analysis_consts_fixed8_simd_odd);
    out += out_stride;
    s->sbc_analyze_8(x + 0, out, ff_sbcdsp_analysis_consts_fixed8_simd_even);
}

static inline void sbc_analyze_1b_8s_simd_even(SBCDSPContext *s,
                                               int16_t *x, int32_t *out,
                                               int out_stride);

static inline void sbc_analyze_1b_8s_simd_odd(SBCDSPContext *s,
                                              int16_t *x, int32_t *out,
                                              int out_stride)
{
    s->sbc_analyze_8(x, out, ff_sbcdsp_analysis_consts_fixed8_simd_odd);
    s->sbc_analyze_8s = sbc_analyze_1b_8s_simd_even;
}

static inline void sbc_analyze_1b_8s_simd_even(SBCDSPContext *s,
                                               int16_t *x, int32_t *out,
                                               int out_stride)
{
    s->sbc_analyze_8(x, out, ff_sbcdsp_analysis_consts_fixed8_simd_even);
    s->sbc_analyze_8s = sbc_analyze_1b_8s_simd_odd;
}

/*
 * Input data processing functions. The data is endian converted if needed,
 * channels are deintrleaved and audio samples are reordered for use in
 * SIMD-friendly analysis filter function. The results are put into "X"
 * array, getting appended to the previous data (or it is better to say
 * prepended, as the buffer is filled from top to bottom). Old data is
 * discarded when neededed, but availability of (10 * nrof_subbands)
 * contiguous samples is always guaranteed for the input to the analysis
 * filter. This is achieved by copying a sufficient part of old data
 * to the top of the buffer on buffer wraparound.
 */

static int sbc_enc_process_input_4s(int position, const uint8_t *pcm,
                                    int16_t X[2][SBC_X_BUFFER_SIZE],
                                    int nsamples, int nchannels)
{
    int c;

    /* handle X buffer wraparound */
    if (position < nsamples) {
        for (c = 0; c < nchannels; c++)
            memcpy(&X[c][SBC_X_BUFFER_SIZE - 40], &X[c][position],
                            36 * sizeof(int16_t));
        position = SBC_X_BUFFER_SIZE - 40;
    }

    /* copy/permutate audio samples */
    for (; nsamples >= 8; nsamples -= 8, pcm += 16 * nchannels) {
        position -= 8;
        for (c = 0; c < nchannels; c++) {
            int16_t *x = &X[c][position];
            x[0] = AV_RN16(pcm + 14*nchannels + 2*c);
            x[1] = AV_RN16(pcm +  6*nchannels + 2*c);
            x[2] = AV_RN16(pcm + 12*nchannels + 2*c);
            x[3] = AV_RN16(pcm +  8*nchannels + 2*c);
            x[4] = AV_RN16(pcm +  0*nchannels + 2*c);
            x[5] = AV_RN16(pcm +  4*nchannels + 2*c);
            x[6] = AV_RN16(pcm +  2*nchannels + 2*c);
            x[7] = AV_RN16(pcm + 10*nchannels + 2*c);
        }
    }

    return position;
}

static int sbc_enc_process_input_8s(int position, const uint8_t *pcm,
                                    int16_t X[2][SBC_X_BUFFER_SIZE],
                                    int nsamples, int nchannels)
{
    int c;

    /* handle X buffer wraparound */
    if (position < nsamples) {
        for (c = 0; c < nchannels; c++)
            memcpy(&X[c][SBC_X_BUFFER_SIZE - 72], &X[c][position],
                            72 * sizeof(int16_t));
        position = SBC_X_BUFFER_SIZE - 72;
    }

    if (position % 16 == 8) {
        position -= 8;
        nsamples -= 8;
        for (c = 0; c < nchannels; c++) {
            int16_t *x = &X[c][position];
            x[0] = AV_RN16(pcm + 14*nchannels + 2*c);
            x[2] = AV_RN16(pcm + 12*nchannels + 2*c);
            x[3] = AV_RN16(pcm +  0*nchannels + 2*c);
            x[4] = AV_RN16(pcm + 10*nchannels + 2*c);
            x[5] = AV_RN16(pcm +  2*nchannels + 2*c);
            x[6] = AV_RN16(pcm +  8*nchannels + 2*c);
            x[7] = AV_RN16(pcm +  4*nchannels + 2*c);
            x[8] = AV_RN16(pcm +  6*nchannels + 2*c);
        }
        pcm += 16 * nchannels;
    }

    /* copy/permutate audio samples */
    for (; nsamples >= 16; nsamples -= 16, pcm += 32 * nchannels) {
        position -= 16;
        for (c = 0; c < nchannels; c++) {
            int16_t *x = &X[c][position];
            x[0]  = AV_RN16(pcm + 30*nchannels + 2*c);
            x[1]  = AV_RN16(pcm + 14*nchannels + 2*c);
            x[2]  = AV_RN16(pcm + 28*nchannels + 2*c);
            x[3]  = AV_RN16(pcm + 16*nchannels + 2*c);
            x[4]  = AV_RN16(pcm + 26*nchannels + 2*c);
            x[5]  = AV_RN16(pcm + 18*nchannels + 2*c);
            x[6]  = AV_RN16(pcm + 24*nchannels + 2*c);
            x[7]  = AV_RN16(pcm + 20*nchannels + 2*c);
            x[8]  = AV_RN16(pcm + 22*nchannels + 2*c);
            x[9]  = AV_RN16(pcm +  6*nchannels + 2*c);
            x[10] = AV_RN16(pcm + 12*nchannels + 2*c);
            x[11] = AV_RN16(pcm +  0*nchannels + 2*c);
            x[12] = AV_RN16(pcm + 10*nchannels + 2*c);
            x[13] = AV_RN16(pcm +  2*nchannels + 2*c);
            x[14] = AV_RN16(pcm +  8*nchannels + 2*c);
            x[15] = AV_RN16(pcm +  4*nchannels + 2*c);
        }
    }

    if (nsamples == 8) {
        position -= 8;
        for (c = 0; c < nchannels; c++) {
            int16_t *x = &X[c][position];
            x[-7] = AV_RN16(pcm + 14*nchannels + 2*c);
            x[1]  = AV_RN16(pcm +  6*nchannels + 2*c);
            x[2]  = AV_RN16(pcm + 12*nchannels + 2*c);
            x[3]  = AV_RN16(pcm +  0*nchannels + 2*c);
            x[4]  = AV_RN16(pcm + 10*nchannels + 2*c);
            x[5]  = AV_RN16(pcm +  2*nchannels + 2*c);
            x[6]  = AV_RN16(pcm +  8*nchannels + 2*c);
            x[7]  = AV_RN16(pcm +  4*nchannels + 2*c);
        }
    }

    return position;
}

static void sbc_calc_scalefactors(int32_t sb_sample_f[16][2][8],
                                  uint32_t scale_factor[2][8],
                                  int blocks, int channels, int subbands)
{
    int ch, sb, blk;
    for (ch = 0; ch < channels; ch++) {
        for (sb = 0; sb < subbands; sb++) {
            uint32_t x = 1 << SCALE_OUT_BITS;
            for (blk = 0; blk < blocks; blk++) {
                int32_t tmp = FFABS(sb_sample_f[blk][ch][sb]);
                if (tmp != 0)
                    x |= tmp - 1;
            }
            scale_factor[ch][sb] = (31 - SCALE_OUT_BITS) - ff_clz(x);
        }
    }
}

static int sbc_calc_scalefactors_j(int32_t sb_sample_f[16][2][8],
                                   uint32_t scale_factor[2][8],
                                   int blocks, int subbands)
{
    int blk, joint = 0;
    int32_t tmp0, tmp1;
    uint32_t x, y;

    /* last subband does not use joint stereo */
    int sb = subbands - 1;
    x = 1 << SCALE_OUT_BITS;
    y = 1 << SCALE_OUT_BITS;
    for (blk = 0; blk < blocks; blk++) {
        tmp0 = FFABS(sb_sample_f[blk][0][sb]);
        tmp1 = FFABS(sb_sample_f[blk][1][sb]);
        if (tmp0 != 0)
            x |= tmp0 - 1;
        if (tmp1 != 0)
            y |= tmp1 - 1;
    }
    scale_factor[0][sb] = (31 - SCALE_OUT_BITS) - ff_clz(x);
    scale_factor[1][sb] = (31 - SCALE_OUT_BITS) - ff_clz(y);

    /* the rest of subbands can use joint stereo */
    while (--sb >= 0) {
        int32_t sb_sample_j[16][2];
        x = 1 << SCALE_OUT_BITS;
        y = 1 << SCALE_OUT_BITS;
        for (blk = 0; blk < blocks; blk++) {
            tmp0 = sb_sample_f[blk][0][sb];
            tmp1 = sb_sample_f[blk][1][sb];
            sb_sample_j[blk][0] = (tmp0 >> 1) + (tmp1 >> 1);
            sb_sample_j[blk][1] = (tmp0 >> 1) - (tmp1 >> 1);
            tmp0 = FFABS(tmp0);
            tmp1 = FFABS(tmp1);
            if (tmp0 != 0)
                x |= tmp0 - 1;
            if (tmp1 != 0)
                y |= tmp1 - 1;
        }
        scale_factor[0][sb] = (31 - SCALE_OUT_BITS) -
            ff_clz(x);
        scale_factor[1][sb] = (31 - SCALE_OUT_BITS) -
            ff_clz(y);
        x = 1 << SCALE_OUT_BITS;
        y = 1 << SCALE_OUT_BITS;
        for (blk = 0; blk < blocks; blk++) {
            tmp0 = FFABS(sb_sample_j[blk][0]);
            tmp1 = FFABS(sb_sample_j[blk][1]);
            if (tmp0 != 0)
                x |= tmp0 - 1;
            if (tmp1 != 0)
                y |= tmp1 - 1;
        }
        x = (31 - SCALE_OUT_BITS) - ff_clz(x);
        y = (31 - SCALE_OUT_BITS) - ff_clz(y);

        /* decide whether to use joint stereo for this subband */
        if ((scale_factor[0][sb] + scale_factor[1][sb]) > x + y) {
            joint |= 1 << (subbands - 1 - sb);
            scale_factor[0][sb] = x;
            scale_factor[1][sb] = y;
            for (blk = 0; blk < blocks; blk++) {
                sb_sample_f[blk][0][sb] = sb_sample_j[blk][0];
                sb_sample_f[blk][1][sb] = sb_sample_j[blk][1];
            }
        }
    }

    /* bitmask with the information about subbands using joint stereo */
    return joint;
}

/*
 * Detect CPU features and setup function pointers
 */
av_cold void ff_sbcdsp_init(SBCDSPContext *s)
{
    /* Default implementation for analyze functions */
    s->sbc_analyze_4 = sbc_analyze_4_simd;
    s->sbc_analyze_8 = sbc_analyze_8_simd;
    s->sbc_analyze_4s = sbc_analyze_4b_4s_simd;
    if (s->increment == 1)
        s->sbc_analyze_8s = sbc_analyze_1b_8s_simd_odd;
    else
        s->sbc_analyze_8s = sbc_analyze_4b_8s_simd;

    /* Default implementation for input reordering / deinterleaving */
    s->sbc_enc_process_input_4s = sbc_enc_process_input_4s;
    s->sbc_enc_process_input_8s = sbc_enc_process_input_8s;

    /* Default implementation for scale factors calculation */
    s->sbc_calc_scalefactors = sbc_calc_scalefactors;
    s->sbc_calc_scalefactors_j = sbc_calc_scalefactors_j;

    if (ARCH_ARM)
        ff_sbcdsp_init_arm(s);
    if (ARCH_X86)
        ff_sbcdsp_init_x86(s);
}
