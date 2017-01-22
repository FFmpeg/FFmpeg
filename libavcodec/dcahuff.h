/*
 * DCA compatible decoder - huffman tables
 * Copyright (C) 2004 Gildas Bazin
 * Copyright (C) 2007 Konstantin Shishkov
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

#ifndef AVCODEC_DCAHUFF_H
#define AVCODEC_DCAHUFF_H

#include "libavutil/common.h"

#include "avcodec.h"
#include "get_bits.h"
#include "put_bits.h"

#define DCA_CODE_BOOKS      10
#define DCA_BITALLOC_12_COUNT    5

typedef struct DCAVLC {
    int offset;         ///< Code values offset
    int max_depth;      ///< Parameter for get_vlc2()
    VLC vlc[7];         ///< Actual codes
} DCAVLC;

extern DCAVLC   ff_dca_vlc_bit_allocation;
extern DCAVLC   ff_dca_vlc_transition_mode;
extern DCAVLC   ff_dca_vlc_scale_factor;
extern DCAVLC   ff_dca_vlc_quant_index[DCA_CODE_BOOKS];

extern VLC  ff_dca_vlc_tnl_grp[5];
extern VLC  ff_dca_vlc_tnl_scf;
extern VLC  ff_dca_vlc_damp;
extern VLC  ff_dca_vlc_dph;
extern VLC  ff_dca_vlc_fst_rsd_amp;
extern VLC  ff_dca_vlc_rsd_apprx;
extern VLC  ff_dca_vlc_rsd_amp;
extern VLC  ff_dca_vlc_avg_g3;
extern VLC  ff_dca_vlc_st_grid;
extern VLC  ff_dca_vlc_grid_2;
extern VLC  ff_dca_vlc_grid_3;
extern VLC  ff_dca_vlc_rsd;

av_cold void ff_dca_init_vlcs(void);
uint32_t ff_dca_vlc_calc_quant_bits(int *values, uint8_t n, uint8_t sel, uint8_t abits);
void ff_dca_vlc_enc_quant(PutBitContext *pb, int *values, uint8_t n, uint8_t sel, uint8_t abits);
uint32_t ff_dca_vlc_calc_alloc_bits(int *values, uint8_t n, uint8_t sel);
void ff_dca_vlc_enc_alloc(PutBitContext *pb, int *values, uint8_t n, uint8_t sel);

#endif /* AVCODEC_DCAHUFF_H */
