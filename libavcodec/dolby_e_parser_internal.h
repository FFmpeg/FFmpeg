/*
 * Dolby E parser internal code
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

#ifndef AVCODEC_DOLBY_E_PARSER_INTERNAL_H
#define AVCODEC_DOLBY_E_PARSER_INTERNAL_H

#include "dolby_e.h"

/**
 * Initialize DBEContext.
 * Set word_bits/word_bytes, input, input_size, key_present.
 * @param[out] s DBEContext.
 * @param[in]  buf raw input buffer.
 * @param[in]  buf_size must be 3 bytes at least.
 * @return Returns 0 on success, AVERROR_INVALIDDATA on error
 */
int ff_dolby_e_parse_init(DBEContext *s, const uint8_t *buf, int buf_size);

/**
 * Parse Dolby E metadata.
 * Parse the header up to the end_gain element.
 * @param[in]  s DBEContext .
 * @param[out] hdr Pointer to struct where header info is written.
 * @return Returns 0 on success, AVERROR_INVALIDDATA on error
 */
int ff_dolby_e_parse_header(DBEContext *s, DolbyEHeaderInfo *hdr);


#endif /* AVCODEC_DOLBY_E_PARSER_INTERNAL_H */
