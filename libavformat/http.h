/*
 * HTTP definitions
 * Copyright (c) 2010 Josh Allmann
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

#ifndef AVFORMAT_HTTP_H
#define AVFORMAT_HTTP_H

#include "url.h"

/**
 * Set custom HTTP headers.
 * A trailing CRLF ("\r\n") is required for custom headers.
 * Passing in an empty header string ("\0") will reset to defaults.
 *
 * The following headers can be overriden by custom values,
 * otherwise they will be set to their defaults.
 *  -User-Agent
 *  -Accept
 *  -Range
 *  -Host
 *  -Connection
 *
 * @param h URL context for this HTTP connection
 * @param headers the custom headers to set
 */
void ff_http_set_headers(URLContext *h, const char *headers);

/**
 * Enable or disable chunked transfer encoding. (default is enabled)
 *
 * @param h URL context for this HTTP connection
 * @param is_chunked 0 to disable chunking, nonzero otherwise.
 */
void ff_http_set_chunked_transfer_encoding(URLContext *h, int is_chunked);

/**
 * Initialize the authentication state based on another HTTP URLContext.
 * This can be used to pre-initialize the authentication parameters if
 * they are known beforehand, to avoid having to do an initial failing
 * request just to get the parameters.
 *
 * @param dest URL context whose authentication state gets updated
 * @param src URL context whose authentication state gets copied
 */
void ff_http_init_auth_state(URLContext *dest, const URLContext *src);

#endif /* AVFORMAT_HTTP_H */
