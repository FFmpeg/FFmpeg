/*
 * Simple URL decoding function
 * Copyright (c) 2012 Antti Seppälä
 *
 * References:
 *  RFC 3986: Uniform Resource Identifier (URI): Generic Syntax
 *       T. Berners-Lee et al. The Internet Society, 2005
 *
 * based on http://www.icosaedro.it/apache/urldecode.c
 *          from Umberto Salsi (salsi@icosaedro.it)
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

#include <ctype.h>
#include <string.h>

#include "libavutil/mem.h"
#include "libavutil/avstring.h"
#include "urldecode.h"

char *ff_urldecode(const char *url)
{
    int s = 0, d = 0, url_len = 0;
    char c;
    char *dest = NULL;

    if (!url)
        return NULL;

    url_len = strlen(url) + 1;
    dest = av_malloc(url_len);

    if (!dest)
        return NULL;

    while (s < url_len) {
        c = url[s++];

        if (c == '%' && s + 2 < url_len) {
            char c2 = url[s++];
            char c3 = url[s++];
            if (isxdigit(c2) && isxdigit(c3)) {
                c2 = av_tolower(c2);
                c3 = av_tolower(c3);

                if (c2 <= '9')
                    c2 = c2 - '0';
                else
                    c2 = c2 - 'a' + 10;

                if (c3 <= '9')
                    c3 = c3 - '0';
                else
                    c3 = c3 - 'a' + 10;

                dest[d++] = 16 * c2 + c3;

            } else { /* %zz or something other invalid */
                dest[d++] = c;
                dest[d++] = c2;
                dest[d++] = c3;
            }
        } else if (c == '+') {
            dest[d++] = ' ';
        } else {
            dest[d++] = c;
        }

    }

    return dest;
}
