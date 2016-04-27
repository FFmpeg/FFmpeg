/*
 * VC3/DNxHD decoder.
 * Copyright (c) 2007 SmartJog S.A., Baptiste Coudurier <baptiste dot coudurier at smartjog dot com>
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

#ifndef AVCODEC_DNXHDDATA_H
#define AVCODEC_DNXHDDATA_H

#include <stdint.h>
#include "avcodec.h"

typedef struct CIDEntry {
    int cid;
    unsigned int width, height;
    int interlaced;
    unsigned int frame_size;
    unsigned int coding_unit_size;
    int index_bits;
    int bit_depth;
    const uint8_t *luma_weight, *chroma_weight;
    const uint8_t *dc_codes, *dc_bits;
    const uint16_t *ac_codes;
    const uint8_t *ac_bits, *ac_level;
    const uint8_t *ac_run_flag, *ac_index_flag;
    const uint16_t *run_codes;
    const uint8_t *run_bits, *run;
    int bit_rates[5]; ///< Helper to choose variants, rounded to nearest 5Mb/s
} CIDEntry;

extern const CIDEntry ff_dnxhd_cid_table[];

int ff_dnxhd_get_cid_table(int cid);
int ff_dnxhd_find_cid(AVCodecContext *avctx, int bit_depth);
void ff_dnxhd_list_cid(AVCodecContext *avctx);

#endif /* AVCODEC_DNXHDDATA_H */
