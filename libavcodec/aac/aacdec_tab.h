/*
 * AAC decoder data
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
 * AAC decoder data
 * @author Oded Shimon  ( ods15 ods15 dyndns org )
 * @author Maxim Gavrilov ( maxim.gavrilov gmail com )
 */

#ifndef AVCODEC_AAC_AACDEC_TAB_H
#define AVCODEC_AAC_AACDEC_TAB_H

#include <stdint.h>

#include "libavcodec/vlc.h"

#include "libavutil/attributes_internal.h"
#include "libavutil/channel_layout.h"

FF_VISIBILITY_PUSH_HIDDEN
void ff_aacdec_common_init_once(void);

extern const VLCElem *ff_aac_sbr_vlc[10];

extern VLCElem ff_vlc_scalefactors[];
extern const VLCElem *ff_vlc_spectral[11];

extern const int8_t ff_tags_per_config[16];

extern const uint8_t ff_aac_channel_layout_map[16][16][3];

extern const int16_t ff_aac_channel_map[3][4][6];

extern const AVChannelLayout ff_aac_ch_layout[];

extern const int16_t ff_aac_hcod_firstband_CLD[30][2];
extern const int16_t ff_aac_hcod1D_CLD[2 /* DT_/DF_ */][30][2];
extern const int16_t ff_aac_hcod2D_CLD_03[2 /* _TP/_FP */ ][2 /* DT_/DF_ */ ][15][2];
extern const int16_t ff_aac_hcod2D_CLD_05[2 /* _TP/_FP */ ][2 /* DT_/DF_ */ ][35][2];
extern const int16_t ff_aac_hcod2D_CLD_07[2 /* _TP/_FP */ ][2 /* DT_/DF_ */ ][63][2];
extern const int16_t ff_aac_hcod2D_CLD_09[2 /* _TP/_FP */ ][2 /* DT_/DF_ */ ][99][2];

extern const int16_t ff_aac_hcod_firstband_ICC[7][2];
extern const int16_t ff_aac_hcod1D_ICC[7][2];
extern const int16_t ff_aac_hcod2D_ICC_01[2 /* _TP/_FP */ ][2 /* DT_/DF_ */ ][3][2];
extern const int16_t ff_aac_hcod2D_ICC_03[2 /* _TP/_FP */ ][2 /* DT_/DF_ */ ][15][2];
extern const int16_t ff_aac_hcod2D_ICC_05[2 /* _TP/_FP */ ][2 /* DT_/DF_ */ ][35][2];
extern const int16_t ff_aac_hcod2D_ICC_07[2 /* _TP/_FP */ ][2 /* DT_/DF_ */ ][63][2];

extern const int16_t ff_aac_hcod_firstband_IPD[7][2];
extern const int16_t ff_aac_hcod1D_IPD[2 /* DF, DT */ ][7][2];
extern const int16_t ff_aac_hcod2D_IPD_01[2 /* _TP/_FP */ ][2 /* DT_/DF_ */ ][3][2];
extern const int16_t ff_aac_hcod2D_IPD_03[2 /* _TP/_FP */ ][2 /* DT_/DF_ */ ][15][2];
extern const int16_t ff_aac_hcod2D_IPD_05[2 /* _TP/_FP */ ][2 /* DT_/DF_ */ ][35][2];
extern const int16_t ff_aac_hcod2D_IPD_07[2 /* _TP/_FP */ ][2 /* DT_/DF_ */ ][63][2];

extern const int16_t ff_aac_hcod_lav_idx[3][2];

extern const int16_t ff_aac_hcod2D_reshape[39][2];

extern const uint8_t ff_aac_lav_tab_XXX[3][4];
FF_VISIBILITY_POP_HIDDEN

#endif /* AVCODEC_AAC_AACDEC_TAB_H */
