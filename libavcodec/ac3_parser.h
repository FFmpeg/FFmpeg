/*
 * AC-3 parser prototypes
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

#ifndef AVCODEC_AC3_PARSER_H
#define AVCODEC_AC3_PARSER_H

#include "ac3.h"
#include "get_bits.h"

/**
 * Parse AC-3 frame header.
 * Parse the header up to the lfeon element, which is the first 52 or 54 bits
 * depending on the audio coding mode.
 * @param[in]  gbc BitContext containing the first 54 bits of the frame.
 * @param[out] hdr Pointer to struct where header info is written.
 * @return Returns 0 on success, -1 if there is a sync word mismatch,
 * -2 if the bsid (version) element is invalid, -3 if the fscod (sample rate)
 * element is invalid, or -4 if the frmsizecod (bit rate) element is invalid.
 */
int ff_ac3_parse_header(GetBitContext *gbc, AC3HeaderInfo *hdr);

#endif /* AVCODEC_AC3_PARSER_H */
