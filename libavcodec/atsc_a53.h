/*
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

#ifndef AVCODEC_ATSC_A53_H
#define AVCODEC_ATSC_A53_H

#include <stdint.h>

#include "libavutil/buffer.h"

/**
 * Parse a data array for ATSC A53 Part 4 Closed Captions and store them in an AVBufferRef.
 *
 * @param pbuf Pointer to an AVBufferRef to append the closed captions. *pbuf may be NULL, in
 *             which case a new buffer will be allocated and put in it.
 * @param data The data array containing the raw A53 data.
 * @param size Size of the data array in bytes.
 *
 * @return Number of closed captions parsed on success, negative error code on failure.
 *         If no Closed Captions are parsed, *pbuf is untouched.
 */
int ff_parse_a53_cc(AVBufferRef **pbuf, const uint8_t *data, int size);

#endif /* AVCODEC_ATSC_A53_H */
