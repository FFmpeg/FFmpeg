/*
 * Bluetooth low-complexity, subband codec (SBC)
 *
 * Copyright (C) 2017  Aurelien Jacobs <aurel@gnuage.org>
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
 * SBC ARMv6 optimization for some basic "building bricks"
 */

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/mem_internal.h"
#include "libavutil/arm/cpu.h"
#include "libavcodec/sbcdsp.h"

void ff_sbc_analyze_4_armv6(const int16_t *in, int32_t *out, const int16_t *consts);
void ff_sbc_analyze_8_armv6(const int16_t *in, int32_t *out, const int16_t *consts);

void ff_sbc_analyze_4_neon(const int16_t *in, int32_t *out, const int16_t *consts);
void ff_sbc_analyze_8_neon(const int16_t *in, int32_t *out, const int16_t *consts);
void ff_sbc_calc_scalefactors_neon(int32_t sb_sample_f[16][2][8],
                                   uint32_t scale_factor[2][8],
                                   int blocks, int channels, int subbands);
int ff_sbc_calc_scalefactors_j_neon(int32_t sb_sample_f[16][2][8],
                                    uint32_t scale_factor[2][8],
                                    int blocks, int subbands);
int ff_sbc_enc_process_input_4s_neon(int position, const uint8_t *pcm,
                                     int16_t X[2][SBC_X_BUFFER_SIZE],
                                     int nsamples, int nchannels);
int ff_sbc_enc_process_input_8s_neon(int position, const uint8_t *pcm,
                                     int16_t X[2][SBC_X_BUFFER_SIZE],
                                     int nsamples, int nchannels);

DECLARE_ALIGNED(SBC_ALIGN, int32_t, ff_sbcdsp_joint_bits_mask)[8] = {
    8,   4,  2,  1, 128, 64, 32, 16
};

#if HAVE_BIGENDIAN
#define PERM(a, b, c, d) {        \
        (a * 2) + 1, (a * 2) + 0, \
        (b * 2) + 1, (b * 2) + 0, \
        (c * 2) + 1, (c * 2) + 0, \
        (d * 2) + 1, (d * 2) + 0  \
    }
#else
#define PERM(a, b, c, d) {        \
        (a * 2) + 0, (a * 2) + 1, \
        (b * 2) + 0, (b * 2) + 1, \
        (c * 2) + 0, (c * 2) + 1, \
        (d * 2) + 0, (d * 2) + 1  \
    }
#endif

DECLARE_ALIGNED(SBC_ALIGN, uint8_t, ff_sbc_input_perm_4)[2][8] = {
    PERM(7, 3, 6, 4),
    PERM(0, 2, 1, 5)
};

DECLARE_ALIGNED(SBC_ALIGN, uint8_t, ff_sbc_input_perm_8)[4][8] = {
    PERM(15, 7, 14,  8),
    PERM(13, 9, 12, 10),
    PERM(11, 3,  6,  0),
    PERM( 5, 1,  4,  2)
};

av_cold void ff_sbcdsp_init_arm(SBCDSPContext *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_armv6(cpu_flags)) {
        s->sbc_analyze_4 = ff_sbc_analyze_4_armv6;
        s->sbc_analyze_8 = ff_sbc_analyze_8_armv6;
    }

    if (have_neon(cpu_flags)) {
        s->sbc_analyze_4 = ff_sbc_analyze_4_neon;
        s->sbc_analyze_8 = ff_sbc_analyze_8_neon;
        s->sbc_calc_scalefactors = ff_sbc_calc_scalefactors_neon;
        s->sbc_calc_scalefactors_j = ff_sbc_calc_scalefactors_j_neon;
        if (s->increment != 1) {
            s->sbc_enc_process_input_4s = ff_sbc_enc_process_input_4s_neon;
            s->sbc_enc_process_input_8s = ff_sbc_enc_process_input_8s_neon;
        }
    }
}
