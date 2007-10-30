/*
 * Base64.c
 * Copyright (c) 2006 Ryan Martell. (rdm4@martellventures.com)
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

#ifndef FFMPEG_BASE64_H
#define FFMPEG_BASE64_H

#include <stdint.h>

/**
 * decodes base64
 * param order as strncpy()
 */
int av_base64_decode(uint8_t * out, const char *in, int out_length);

/**
 * encodes base64
 * @param src data, not a string
 * @param buf output string
 */
char *av_base64_encode(char * buf, int buf_len, const uint8_t * src, int len);

#endif /* FFMPEG_BASE64_H */
