/*
 * AAC ADTS header decoding prototypes and structures
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2003 Michael Niedermayer
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

#ifndef AVCODEC_AACADTSDEC_H
#define AVCODEC_AACADTSDEC_H

#include <stdint.h>
#include "get_bits.h"

#define AAC_ADTS_HEADER_SIZE 7

typedef struct {
    uint32_t sample_rate;
    uint32_t samples;
    uint32_t bit_rate;
    uint8_t  crc_absent;
    uint8_t  object_type;
    uint8_t  sampling_index;
    uint8_t  chan_config;
    uint8_t  num_aac_frames;
} AACADTSHeaderInfo;

/**
 * Parse AAC frame header.
 * Parse the ADTS frame header to the end of the variable header, which is
 * the first 54 bits.
 * @param[in]  gbc BitContext containing the first 54 bits of the frame.
 * @param[out] hdr Pointer to struct where header info is written.
 * @return Returns 0 on success, -1 if there is a sync word mismatch,
 * -2 if the version element is invalid, -3 if the sample rate
 * element is invalid, or -4 if the bit rate element is invalid.
 */
int avpriv_aac_parse_header(GetBitContext *gbc, AACADTSHeaderInfo *hdr);

#endif /* AVCODEC_AACADTSDEC_H */
