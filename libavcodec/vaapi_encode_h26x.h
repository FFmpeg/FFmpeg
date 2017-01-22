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

#ifndef AVCODEC_VAAPI_ENCODE_H26X_H
#define AVCODEC_VAAPI_ENCODE_H26X_H

#include <stddef.h>
#include <stdint.h>

#include "golomb.h"
#include "put_bits.h"


// Debug code may be interested in the name of the syntax element being
// for tracing purposes.  Here, it is just discarded.

#define write_u(pbc, width, value, name) put_bits(pbc, width, value)
#define write_ue(pbc, value, name)       set_ue_golomb(pbc, value)
#define write_se(pbc, value, name)       set_se_golomb(pbc, value)

#define u(width, ...) write_u(pbc, width, __VA_ARGS__)
#define ue(...)       write_ue(pbc, __VA_ARGS__)
#define se(...)       write_se(pbc, __VA_ARGS__)


// Copy from src to dst, applying emulation prevention.
int ff_vaapi_encode_h26x_nal_unit_to_byte_stream(uint8_t *dst, size_t *dst_len,
                                                 uint8_t *src, size_t src_len);

#endif /* AVCODEC_VAAPI_ENCODE_H26X_H */
