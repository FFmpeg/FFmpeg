/*
 * Canopus HQ/HQA decoder
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_HQ_HQA_H
#define AVCODEC_HQ_HQA_H

#include <stdint.h>

#include "avcodec.h"
#include "bytestream.h"
#include "hq_hqadsp.h"
#include "vlc.h"

#define NUM_HQ_AC_ENTRIES 746
#define NUM_HQ_PROFILES   22
#define NUM_HQ_QUANTS     16

typedef struct HQContext {
    AVCodecContext *avctx;
    HQDSPContext hqhqadsp;
    GetByteContext gbc;

    VLC hq_ac_vlc;
    VLC hqa_cbp_vlc;
    DECLARE_ALIGNED(16, int16_t, block)[12][64];
} HQContext;

typedef struct HQProfile {
    const uint8_t *perm_tab;
    int width, height;
    int num_slices;
    int tab_w, tab_h;
} HQProfile;

extern const int32_t * const ff_hq_quants[16][2][4];
extern const HQProfile ff_hq_profile[NUM_HQ_PROFILES];

extern const uint8_t ff_hq_ac_skips[NUM_HQ_AC_ENTRIES];
extern const int16_t ff_hq_ac_syms [NUM_HQ_AC_ENTRIES];

int ff_hq_init_vlcs(HQContext *c);

#endif /* AVCODEC_HQ_HQA_H */
