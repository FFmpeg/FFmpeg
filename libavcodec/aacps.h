/*
 * MPEG-4 Parametric Stereo definitions and declarations
 * Copyright (c) 2010 Alex Converse <alex.converse@gmail.com>
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

#ifndef AVCODEC_PS_H
#define AVCODEC_PS_H

#include <stdint.h>

#include "aacpsdsp.h"
#include "avcodec.h"
#include "get_bits.h"

#define PS_MAX_NUM_ENV 5
#define PS_MAX_NR_IIDICC 34
#define PS_MAX_NR_IPDOPD 17
#define PS_MAX_SSB 91
#define PS_MAX_AP_BANDS 50
#define PS_QMF_TIME_SLOTS 32
#define PS_MAX_DELAY 14
#define PS_AP_LINKS 3
#define PS_MAX_AP_DELAY 5

typedef struct {
    int    start;
    int    enable_iid;
    int    iid_quant;
    int    nr_iid_par;
    int    nr_ipdopd_par;
    int    enable_icc;
    int    icc_mode;
    int    nr_icc_par;
    int    enable_ext;
    int    frame_class;
    int    num_env_old;
    int    num_env;
    int    enable_ipdopd;
    int    border_position[PS_MAX_NUM_ENV+1];
    int8_t iid_par[PS_MAX_NUM_ENV][PS_MAX_NR_IIDICC]; ///< Inter-channel Intensity Difference Parameters
    int8_t icc_par[PS_MAX_NUM_ENV][PS_MAX_NR_IIDICC]; ///< Inter-Channel Coherence Parameters
    /* ipd/opd is iid/icc sized so that the same functions can handle both */
    int8_t ipd_par[PS_MAX_NUM_ENV][PS_MAX_NR_IIDICC]; ///< Inter-channel Phase Difference Parameters
    int8_t opd_par[PS_MAX_NUM_ENV][PS_MAX_NR_IIDICC]; ///< Overall Phase Difference Parameters
    int    is34bands;
    int    is34bands_old;

    DECLARE_ALIGNED(16, float, in_buf)[5][44][2];
    DECLARE_ALIGNED(16, float, delay)[PS_MAX_SSB][PS_QMF_TIME_SLOTS + PS_MAX_DELAY][2];
    DECLARE_ALIGNED(16, float, ap_delay)[PS_MAX_AP_BANDS][PS_AP_LINKS][PS_QMF_TIME_SLOTS + PS_MAX_AP_DELAY][2];
    DECLARE_ALIGNED(16, float, peak_decay_nrg)[34];
    DECLARE_ALIGNED(16, float, power_smooth)[34];
    DECLARE_ALIGNED(16, float, peak_decay_diff_smooth)[34];
    DECLARE_ALIGNED(16, float, H11)[2][PS_MAX_NUM_ENV+1][PS_MAX_NR_IIDICC];
    DECLARE_ALIGNED(16, float, H12)[2][PS_MAX_NUM_ENV+1][PS_MAX_NR_IIDICC];
    DECLARE_ALIGNED(16, float, H21)[2][PS_MAX_NUM_ENV+1][PS_MAX_NR_IIDICC];
    DECLARE_ALIGNED(16, float, H22)[2][PS_MAX_NUM_ENV+1][PS_MAX_NR_IIDICC];
    int8_t opd_hist[PS_MAX_NR_IIDICC];
    int8_t ipd_hist[PS_MAX_NR_IIDICC];
    PSDSPContext dsp;
} PSContext;

void ff_ps_init(void);
void ff_ps_ctx_init(PSContext *ps);
int ff_ps_read_data(AVCodecContext *avctx, GetBitContext *gb, PSContext *ps, int bits_left);
int ff_ps_apply(AVCodecContext *avctx, PSContext *ps, float L[2][38][64], float R[2][38][64], int top);

#endif /* AVCODEC_PS_H */
