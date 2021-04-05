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
#include "libavutil/avassert.h"
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

static const char *find_delim(const char *delim, const char *cur, const char *end)
{
    while (cur < end && !strchr(delim, *cur))
        cur++;
    return cur;
}

int ff_url_decompose(URLComponents *uc, const char *url, const char *end)
{
    const char *cur, *aend, *p;

    av_assert0(url);
    if (!end)
        end = url + strlen(url);
    cur = uc->url = url;

    /* scheme */
    uc->scheme = cur;
    p = find_delim(":/?#", cur, end); /* lavf "schemes" can contain options but not some RFC 3986 delimiters */
    if (*p == ':')
        cur = p + 1;

    /* authority */
    uc->authority = cur;
    if (end - cur >= 2 && cur[0] == '/' && cur[1] == '/') {
        cur += 2;
        aend = find_delim("/?#", cur, end);

        /* userinfo */
        uc->userinfo = cur;
        p = find_delim("@", cur, aend);
        if (*p == '@')
            cur = p + 1;

        /* host */
        uc->host = cur;
        if (*cur == '[') { /* hello IPv6, thanks for using colons! */
            p = find_delim("]", cur, aend);
            if (*p != ']')
                return AVERROR(EINVAL);
            if (p + 1 < aend && p[1] != ':')
                return AVERROR(EINVAL);
            cur = p + 1;
        } else {
            cur = find_delim(":", cur, aend);
        }

        /* port */
        uc->port = cur;
        cur = aend;
    } else {
        uc->userinfo = uc->host = uc->port = cur;
    }

    /* path */
    uc->path = cur;
    cur = find_delim("?#", cur, end);

    /* query */
    uc->query = cur;
    if (*cur == '?')
        cur = find_delim("#", cur, end);

    /* fragment */
    uc->fragment = cur;

    uc->end = end;
    return 0;
}

static int is_fq_dos_path(const char *path)
{
    if ((path[0] >= 'a' && path[0] <= 'z' || path[0] >= 'A' && path[0] <= 'Z') &&
         path[1] == ':' &&
        (path[2] == '/' || path[2] == '\\'))
        return 1;
    if ((path[0] == '/' || path[0] == '\\') &&
        (path[1] == '/' || path[1] == '\\'))
        return 1;
    return 0;
}

static int append_path(char *root, char *out_end, char **rout,
                       const char *in, const char *in_end)
{
    char *out = *rout;
    const char *d, *next;

    if (in < in_end && *in == '/')
        in++; /* already taken care of */
    while (in < in_end) {
        d = find_delim("/", in, in_end);
        next = d + (d < in_end && *d == '/');
        if (d - in == 1 && in[0] == '.') {
            /* skip */
        } else if (d - in == 2 && in[0] == '.' && in[1] == '.') {
            av_assert1(out[-1] == '/');
            if (out - root > 1)
                while (out > root && (--out)[-1] != '/');
        } else {
            if (out_end - out < next - in)
                return AVERROR(ENOMEM);
            memmove(out, in, next - in);
            out += next - in;
        }
        in = next;
    }
    *rout = out;
    return 0;
}

int ff_make_absolute_url2(char *buf, int size, const char *base,
                          const char *rel, int handle_dos_paths)
{
    URLComponents ub, uc;
    char *out, *out_end, *path;
    const char *keep, *base_path_end;
    int use_base_path, simplify_path = 0, ret;
    const char *base_separators = "/";

    /* This is tricky.
       For HTTP, http://server/site/page + ../media/file
       should resolve into http://server/media/file
       but for filesystem access, dir/playlist + ../media/file
       should resolve into dir/../media/file
       because dir could be a symlink, and .. points to
       the actual parent of the target directory.

       We'll consider that URLs with an actual scheme and authority,
       i.e. starting with scheme://, need parent dir simplification,
       while bare paths or pseudo-URLs starting with proto: without
       the double slash do not.

       For real URLs, the processing is similar to the algorithm described
       here:
       https://tools.ietf.org/html/rfc3986#section-5
     */

    if (!size)
        return AVERROR(ENOMEM);
    out = buf;
    out_end = buf + size - 1;

    if (!base)
        base = "";
    if (handle_dos_paths) {
        if ((ret = ff_url_decompose(&ub, base, NULL)) < 0)
            goto error;
        if (is_fq_dos_path(base) || av_strstart(base, "file:", NULL) || ub.path == ub.url) {
            base_separators = "/\\";
            if (is_fq_dos_path(rel))
                base = "";
        }
    }
    if ((ret = ff_url_decompose(&ub, base, NULL)) < 0 ||
        (ret = ff_url_decompose(&uc, rel,  NULL)) < 0)
        goto error;

    keep = ub.url;
#define KEEP(component, also) do { \
        if (uc.url_component_end_##component == uc.url && \
            ub.url_component_end_##component > keep) { \
            keep = ub.url_component_end_##component; \
            also \
        } \
    } while (0)
    KEEP(scheme, );
    KEEP(authority_full, simplify_path = 1;);
    KEEP(path,);
    KEEP(query,);
    KEEP(fragment,);
#undef KEEP
#define COPY(start, end) do { \
        size_t len = end - start; \
        if (len > out_end - out) { \
            ret = AVERROR(ENOMEM);  \
            goto error; \
        } \
        memmove(out, start, len); \
        out += len; \
    } while (0)
    COPY(ub.url, keep);
    COPY(uc.url, uc.path);

    use_base_path = URL_COMPONENT_HAVE(ub, path) && keep <= ub.path;
    if (uc.path > uc.url)
        use_base_path = 0;
    if (URL_COMPONENT_HAVE(uc, path) && uc.path[0] == '/')
        use_base_path = 0;
    if (use_base_path) {
        base_path_end = ub.url_component_end_path;
        if (URL_COMPONENT_HAVE(uc, path))
            while (base_path_end > ub.path && !strchr(base_separators, base_path_end[-1]))
                base_path_end--;
    }
    if (keep > ub.path)
        simplify_path = 0;
    if (URL_COMPONENT_HAVE(uc, scheme))
        simplify_path = 0;
    if (URL_COMPONENT_HAVE(uc, authority))
        simplify_path = 1;
    /* No path at all, leave it */
    if (!use_base_path && !URL_COMPONENT_HAVE(uc, path))
        simplify_path = 0;

    if (simplify_path) {
        const char *root = "/";
        COPY(root, root + 1);
        path = out;
        if (use_base_path) {
            ret = append_path(path, out_end, &out, ub.path, base_path_end);
            if (ret < 0)
                goto error;
        }
        if (URL_COMPONENT_HAVE(uc, path)) {
            ret = append_path(path, out_end, &out, uc.path, uc.url_component_end_path);
            if (ret < 0)
                goto error;
        }
    } else {
        if (use_base_path)
            COPY(ub.path, base_path_end);
        COPY(uc.path, uc.url_component_end_path);
    }

    COPY(uc.url_component_end_path, uc.end);
#undef COPY
    *out = 0;
    return 0;

error:
    snprintf(buf, size, "invalid:%s",
             ret == AVERROR(ENOMEM) ? "truncated" :
             ret == AVERROR(EINVAL) ? "syntax_error" : "");
    return ret;
}

int ff_make_absolute_url(char *buf, int size, const char *base,
                         const char *rel)
{
    return ff_make_absolute_url2(buf, size, base, rel, HAVE_DOS_PATHS);
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
