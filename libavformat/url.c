/*
 * URL utility functions
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */


#include "avformat.h"
#include "config.h"
#include "url.h"
#if CONFIG_NETWORK
#include "network.h"
#endif
#include "libavutil/avstring.h"

/**
 * @file
 * URL utility functions.
 */

int ff_url_join(char *str, int size, const char *proto,
                const char *authorization, const char *hostname,
                int port, const char *fmt, ...)
{
#if CONFIG_NETWORK
    struct addrinfo hints = { 0 }, *ai;
#endif

    str[0] = '\0';
    if (proto)
        av_strlcatf(str, size, "%s://", proto);
    if (authorization && authorization[0])
        av_strlcatf(str, size, "%s@", authorization);
#if CONFIG_NETWORK && defined(AF_INET6)
    /* Determine if hostname is a numerical IPv6 address,
     * properly escape it within [] in that case. */
    hints.ai_flags = AI_NUMERICHOST;
    if (!getaddrinfo(hostname, NULL, &hints, &ai)) {
        if (ai->ai_family == AF_INET6) {
            av_strlcat(str, "[", size);
            av_strlcat(str, hostname, size);
            av_strlcat(str, "]", size);
        } else {
            av_strlcat(str, hostname, size);
        }
        freeaddrinfo(ai);
    } else
#endif
        /* Not an IPv6 address, just output the plain string. */
        av_strlcat(str, hostname, size);

    if (port >= 0)
        av_strlcatf(str, size, ":%d", port);
    if (fmt) {
        va_list vl;
        int len = strlen(str);

        va_start(vl, fmt);
        vsnprintf(str + len, size > len ? size - len : 0, fmt, vl);
        va_end(vl);
    }
    return strlen(str);
}

void ff_make_absolute_url(char *buf, int size, const char *base,
                          const char *rel)
{
    char *sep, *path_query;
    /* Absolute path, relative to the current server */
    if (base && strstr(base, "://") && rel[0] == '/') {
        if (base != buf)
            av_strlcpy(buf, base, size);
        sep = strstr(buf, "://");
        if (sep) {
            /* Take scheme from base url */
            if (rel[1] == '/') {
                sep[1] = '\0';
            } else {
                /* Take scheme and host from base url */
                sep += 3;
                sep = strchr(sep, '/');
                if (sep)
                    *sep = '\0';
            }
        }
        av_strlcat(buf, rel, size);
        return;
    }
    /* If rel actually is an absolute url, just copy it */
    if (!base || strstr(rel, "://") || rel[0] == '/') {
        av_strlcpy(buf, rel, size);
        return;
    }
    if (base != buf)
        av_strlcpy(buf, base, size);

    /* Strip off any query string from base */
    path_query = strchr(buf, '?');
    if (path_query)
        *path_query = '\0';

    /* Is relative path just a new query part? */
    if (rel[0] == '?') {
        av_strlcat(buf, rel, size);
        return;
    }

    /* Remove the file name from the base url */
    sep = strrchr(buf, '/');
    if (sep)
        sep[1] = '\0';
    else
        buf[0] = '\0';
    while (av_strstart(rel, "../", NULL) && sep) {
        /* Remove the path delimiter at the end */
        sep[0] = '\0';
        sep = strrchr(buf, '/');
        /* If the next directory name to pop off is "..", break here */
        if (!strcmp(sep ? &sep[1] : buf, "..")) {
            /* Readd the slash we just removed */
            av_strlcat(buf, "/", size);
            break;
        }
        /* Cut off the directory name */
        if (sep)
            sep[1] = '\0';
        else
            buf[0] = '\0';
        rel += 3;
    }
    av_strlcat(buf, rel, size);
}
