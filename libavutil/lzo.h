/*
 * LZO 1x decompression
 * copyright (c) 2006 Reimar Doeffinger
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

#ifndef LZO_H
#define LZO_H

#define LZO_INPUT_DEPLETED 1
#define LZO_OUTPUT_FULL 2
#define LZO_INVALID_BACKPTR 4
#define LZO_ERROR 8

#define LZO_INPUT_PADDING 8
#define LZO_OUTPUT_PADDING 12

int lzo1x_decode(void *out, int *outlen, void *in, int *inlen);

#endif
