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

#include <stdint.h>

#include "libavutil/attributes.h"

#include "vlc.h"

#define DCA_CODE_BOOKS      10
#define DCA_BITALLOC_12_COUNT    5
#define DCA_NUM_BITALLOC_CODES (1 * 3 + \
                                3 * (5 + 7 + 9 + 13) \
                                + 7 * (17 + 25 + 33 + 65 + 129))

typedef struct DCAVLC {
    int max_depth;      ///< Parameter for get_vlc2()
    VLC vlc[7];         ///< Actual codes
} DCAVLC;

extern DCAVLC   ff_dca_vlc_bit_allocation;
#define DCA_TMODE_VLC_BITS 3
extern VLC  ff_dca_vlc_transition_mode[4];
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

extern const int8_t  ff_dca_bitalloc_offsets[DCA_CODE_BOOKS];
extern const uint8_t ff_dca_bitalloc_sizes[DCA_CODE_BOOKS];
extern const uint8_t ff_dca_vlc_src_tables[][2];

av_cold void ff_dca_init_vlcs(void);

#endif /* AVCODEC_DCAHUFF_H */
