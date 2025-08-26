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

#ifndef AVFORMAT_URLDECODE_H
#define AVFORMAT_URLDECODE_H

#include <stddef.h>

/**
 * Decodes an URL from its percent-encoded form back into normal
 * representation. This function returns the decoded URL in a string.
 * The URL to be decoded does not necessarily have to be encoded but
 * in that case the original string is duplicated.
 *
 * @param url a string to be decoded.
 * @param decode_plus_sign if nonzero plus sign is decoded to space
 * @return new string with the URL decoded or NULL if decoding failed.
 * Note that the returned string should be explicitly freed when not
 * used anymore.
 */
char *ff_urldecode(const char *url, int decode_plus_sign);

/**
 * Decodes an URL from its percent-encoded form back into normal
 * representation. This function returns the decoded URL in a string.
 * The URL to be decoded does not necessarily have to be encoded but
 * in that case the original string is duplicated.
 *
 * @param dest the destination buffer.
 * @param dest_len the maximum available space in the destination buffer.
 *                 Must be bigger than FFMIN(strlen(url), url_max_len) to avoid
 *                 an AVERROR(EINVAL) result
 * @param url_max_len the maximum number of chars to read from url
 * @param decode_plus_sign if nonzero plus sign is decoded to space
 * @return the number of written bytes to dest excluding the zero terminator,
 *         negative on error
 */
int ff_urldecode_len(char *dest, size_t dest_len, const char *url, size_t url_max_len, int decode_plus_sign);
#endif /* AVFORMAT_URLDECODE_H */
