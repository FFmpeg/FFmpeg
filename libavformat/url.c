/*
 * URL utility functions
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
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


#include "avformat.h"
#include "internal.h"
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
        size_t len = strlen(str);

        va_start(vl, fmt);
        vsnprintf(str + len, size > len ? size - len : 0, fmt, vl);
        va_end(vl);
    }
    return strlen(str);
}

static void trim_double_dot_url(char *buf, const char *rel, int size)
{
    const char *p = rel;
    const char *root = rel;
    char tmp_path[MAX_URL_SIZE] = {0, };
    char *sep;
    char *node;

    /* Get the path root of the url which start by "://" */
    if (p && (sep = strstr(p, "://"))) {
        sep += 3;
        root = strchr(sep, '/');
    }

    /* set new current position if the root node is changed */
    p = root;
    while (p && (node = strstr(p, ".."))) {
        av_strlcat(tmp_path, p, node - p + strlen(tmp_path));
        p = node + 3;
        sep = strrchr(tmp_path, '/');
        if (sep)
            sep[0] = '\0';
        else
            tmp_path[0] = '\0';
    }

    if (!av_stristart(p, "/", NULL) && root != rel)
        av_strlcat(tmp_path, "/", size);

    av_strlcat(tmp_path, p, size);
    /* start set buf after temp path process. */
    av_strlcpy(buf, rel, root - rel + 1);

    if (!av_stristart(tmp_path, "/", NULL) && root != rel)
        av_strlcat(buf, "/", size);

    av_strlcat(buf, tmp_path, size);
}

void ff_make_absolute_url(char *buf, int size, const char *base,
                          const char *rel)
{
    char *sep, *path_query;
    char *root, *p;
    char tmp_path[MAX_URL_SIZE];

    memset(tmp_path, 0, sizeof(tmp_path));
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
        trim_double_dot_url(tmp_path, buf, size);
        memset(buf, 0, size);
        av_strlcpy(buf, tmp_path, size);
        return;
    }
    /* If rel actually is an absolute url, just copy it */
    if (!base || strstr(rel, "://") || rel[0] == '/') {
        trim_double_dot_url(buf, rel, size);
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
        trim_double_dot_url(tmp_path, buf, size);
        memset(buf, 0, size);
        av_strlcpy(buf, tmp_path, size);
        return;
    }

    root = p = buf;
    /* Get the path root of the url which start by "://" */
    if (p && strstr(p, "://")) {
        sep = strstr(p, "://");
        if (sep) {
            sep += 3;
            root = strchr(sep, '/');
        }
    }

    /* Remove the file name from the base url */
    sep = strrchr(buf, '/');
    if (sep && sep <= root)
        sep = root;

    if (sep)
        sep[1] = '\0';
    else
        buf[0] = '\0';
    while (av_strstart(rel, "..", NULL) && sep) {
        /* Remove the path delimiter at the end */
        if (sep > root) {
            sep[0] = '\0';
            sep = strrchr(buf, '/');
        }

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
    trim_double_dot_url(tmp_path, buf, size);
    memset(buf, 0, size);
    av_strlcpy(buf, tmp_path, size);
}

AVIODirEntry *ff_alloc_dir_entry(void)
{
    AVIODirEntry *entry = av_mallocz(sizeof(AVIODirEntry));
    if (entry) {
        entry->type = AVIO_ENTRY_UNKNOWN;
        entry->size = -1;
        entry->modification_timestamp = -1;
        entry->access_timestamp = -1;
        entry->status_change_timestamp = -1;
        entry->user_id = -1;
        entry->group_id = -1;
        entry->filemode = -1;
    }
    return entry;
}
