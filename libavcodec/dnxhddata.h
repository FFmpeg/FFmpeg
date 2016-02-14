/*
 * VC3/DNxHD decoder.
 * Copyright (c) 2007 SmartJog S.A., Baptiste Coudurier <baptiste dot coudurier at smartjog dot com>
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

#ifndef AVCODEC_DNXHDDATA_H
#define AVCODEC_DNXHDDATA_H

#include <stdint.h>
#include "avcodec.h"
#include "libavutil/internal.h"

/** Additional profile info flags */
#define DNXHD_INTERLACED   (1<<0)
#define DNXHD_MBAFF        (1<<1)
#define DNXHD_444          (1<<2)

/** Frame headers, extra 0x00 added to end for parser */
#define DNXHD_HEADER_INITIAL 0x000002800100
#define DNXHD_HEADER_444     0x000002800200
#define DNXHD_HEADER_HR1     0x000002800300
#define DNXHD_HEADER_HR2     0x0000038C0300

/** Indicate that a CIDEntry value must be read in the bitstream */
#define DNXHD_VARIABLE 0

typedef struct CIDEntry {
    int cid;
    unsigned int width, height;
    unsigned int frame_size;
    unsigned int coding_unit_size;
    uint16_t flags;
    int index_bits;
    int bit_depth;
    int eob_index;
    const uint8_t *luma_weight, *chroma_weight;
    const uint8_t *dc_codes, *dc_bits;
    const uint16_t *ac_codes;
    const uint8_t *ac_bits, *ac_info;
    const uint16_t *run_codes;
    const uint8_t *run_bits, *run;
    int bit_rates[5]; ///< Helper to choose variants, rounded to nearest 5Mb/s
    AVRational frame_rates[5];
} CIDEntry;

extern const CIDEntry ff_dnxhd_cid_table[];

int ff_dnxhd_get_cid_table(int cid);
int ff_dnxhd_find_cid(AVCodecContext *avctx, int bit_depth);
void ff_dnxhd_print_profiles(AVCodecContext *avctx, int loglevel);

static av_always_inline uint64_t ff_dnxhd_check_header_prefix(uint64_t prefix)
{
    if (prefix == DNXHD_HEADER_INITIAL ||
        prefix == DNXHD_HEADER_444     ||
        prefix == DNXHD_HEADER_HR1     ||
        prefix == DNXHD_HEADER_HR2)
        return prefix;
    return 0;
}

int avpriv_dnxhd_get_frame_size(int cid);
int avpriv_dnxhd_get_interlaced(int cid);
uint64_t avpriv_dnxhd_parse_header_prefix(const uint8_t *buf);
#endif /* AVCODEC_DNXHDDATA_H */
