/*
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
 * Copyright (c) 2007 Mans Rullgard
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

#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "mem.h"
#include "avassert.h"
#include "avstring.h"
#include "bprint.h"
#include "error.h"
#include "macros.h"

int av_strstart(const char *str, const char *pfx, const char **ptr)
{
    while (*pfx && *pfx == *str) {
        pfx++;
        str++;
    }
    if (!*pfx && ptr)
        *ptr = str;
    return !*pfx;
}

int av_stristart(const char *str, const char *pfx, const char **ptr)
{
    while (*pfx && av_toupper((unsigned)*pfx) == av_toupper((unsigned)*str)) {
        pfx++;
        str++;
    }
    if (!*pfx && ptr)
        *ptr = str;
    return !*pfx;
}

char *av_stristr(const char *s1, const char *s2)
{
    if (!*s2)
        return (char*)(intptr_t)s1;

    do
        if (av_stristart(s1, s2, NULL))
            return (char*)(intptr_t)s1;
    while (*s1++);

    return NULL;
}

char *av_strnstr(const char *haystack, const char *needle, size_t hay_length)
{
    size_t needle_len = strlen(needle);
    if (!needle_len)
        return (char*)haystack;
    while (hay_length >= needle_len) {
        hay_length--;
        if (!memcmp(haystack, needle, needle_len))
            return (char*)haystack;
        haystack++;
    }
    return NULL;
}

size_t av_strlcpy(char *dst, const char *src, size_t size)
{
    size_t len = 0;
    while (++len < size && *src)
        *dst++ = *src++;
    if (len <= size)
        *dst = 0;
    return len + strlen(src) - 1;
}

size_t av_strlcat(char *dst, const char *src, size_t size)
{
    size_t len = strlen(dst);
    if (size <= len + 1)
        return len + strlen(src);
    return len + av_strlcpy(dst + len, src, size - len);
}

size_t av_strlcatf(char *dst, size_t size, const char *fmt, ...)
{
    size_t len = strlen(dst);
    va_list vl;

    va_start(vl, fmt);
    len += vsnprintf(dst + len, size > len ? size - len : 0, fmt, vl);
    va_end(vl);

    return len;
}

char *av_asprintf(const char *fmt, ...)
{
    char *p = NULL;
    va_list va;
    int len;

    va_start(va, fmt);
    len = vsnprintf(NULL, 0, fmt, va);
    va_end(va);
    if (len < 0)
        goto end;

    p = av_malloc(len + 1);
    if (!p)
        goto end;

    va_start(va, fmt);
    len = vsnprintf(p, len + 1, fmt, va);
    va_end(va);
    if (len < 0)
        av_freep(&p);

end:
    return p;
}

#define WHITESPACES " \n\t\r"

char *av_get_token(const char **buf, const char *term)
{
    char *out     = av_malloc(strlen(*buf) + 1);
    char *ret     = out, *end = out;
    const char *p = *buf;
    if (!out)
        return NULL;
    p += strspn(p, WHITESPACES);

    while (*p && !strspn(p, term)) {
        char c = *p++;
        if (c == '\\' && *p) {
            *out++ = *p++;
            end    = out;
        } else if (c == '\'') {
            while (*p && *p != '\'')
                *out++ = *p++;
            if (*p) {
                p++;
                end = out;
            }
        } else {
            *out++ = c;
        }
    }

    do
        *out-- = 0;
    while (out >= end && strspn(out, WHITESPACES));

    *buf = p;

    return ret;
}

char *av_strtok(char *s, const char *delim, char **saveptr)
{
    char *tok;

    if (!s && !(s = *saveptr))
        return NULL;

    /* skip leading delimiters */
    s += strspn(s, delim);

    /* s now points to the first non delimiter char, or to the end of the string */
    if (!*s) {
        *saveptr = NULL;
        return NULL;
    }
    tok = s++;

    /* skip non delimiters */
    s += strcspn(s, delim);
    if (*s) {
        *s = 0;
        *saveptr = s+1;
    } else {
        *saveptr = NULL;
    }

    return tok;
}

int av_strcasecmp(const char *a, const char *b)
{
    uint8_t c1, c2;
    do {
        c1 = av_tolower(*a++);
        c2 = av_tolower(*b++);
    } while (c1 && c1 == c2);
    return c1 - c2;
}

int av_strncasecmp(const char *a, const char *b, size_t n)
{
    uint8_t c1, c2;
    if (n <= 0)
        return 0;
    do {
        c1 = av_tolower(*a++);
        c2 = av_tolower(*b++);
    } while (--n && c1 && c1 == c2);
    return c1 - c2;
}

char *av_strireplace(const char *str, const char *from, const char *to)
{
    char *ret = NULL;
    const char *pstr2, *pstr = str;
    size_t tolen = strlen(to), fromlen = strlen(from);
    AVBPrint pbuf;

    av_bprint_init(&pbuf, 1, AV_BPRINT_SIZE_UNLIMITED);
    while ((pstr2 = av_stristr(pstr, from))) {
        av_bprint_append_data(&pbuf, pstr, pstr2 - pstr);
        pstr = pstr2 + fromlen;
        av_bprint_append_data(&pbuf, to, tolen);
    }
    av_bprint_append_data(&pbuf, pstr, strlen(pstr));
    if (!av_bprint_is_complete(&pbuf)) {
        av_bprint_finalize(&pbuf, NULL);
    } else {
        av_bprint_finalize(&pbuf, &ret);
    }

    return ret;
}

const char *av_basename(const char *path)
{
    char *p;
#if HAVE_DOS_PATHS
    char *q, *d;
#endif

    if (!path || *path == '\0')
        return ".";

    p = strrchr(path, '/');
#if HAVE_DOS_PATHS
    q = strrchr(path, '\\');
    d = strchr(path, ':');
    p = FFMAX3(p, q, d);
#endif

    if (!p)
        return path;

    return p + 1;
}

const char *av_dirname(char *path)
{
    char *p = path ? strrchr(path, '/') : NULL;

#if HAVE_DOS_PATHS
    char *q = path ? strrchr(path, '\\') : NULL;
    char *d = path ? strchr(path, ':')  : NULL;

    d = d ? d + 1 : d;

    p = FFMAX3(p, q, d);
#endif

    if (!p)
        return ".";

    *p = '\0';

    return path;
}

char *av_append_path_component(const char *path, const char *component)
{
    size_t p_len, c_len;
    char *fullpath;

    if (!path)
        return av_strdup(component);
    if (!component)
        return av_strdup(path);

    p_len = strlen(path);
    c_len = strlen(component);
    if (p_len > SIZE_MAX - c_len || p_len + c_len > SIZE_MAX - 2)
        return NULL;
    fullpath = av_malloc(p_len + c_len + 2);
    if (fullpath) {
        if (p_len) {
            av_strlcpy(fullpath, path, p_len + 1);
            if (c_len) {
                if (fullpath[p_len - 1] != '/' && component[0] != '/')
                    fullpath[p_len++] = '/';
                else if (fullpath[p_len - 1] == '/' && component[0] == '/')
                    p_len--;
            }
        }
        av_strlcpy(&fullpath[p_len], component, c_len + 1);
        fullpath[p_len + c_len] = 0;
    }
    return fullpath;
}

int av_escape(char **dst, const char *src, const char *special_chars,
              enum AVEscapeMode mode, int flags)
{
    AVBPrint dstbuf;
    int ret;

    av_bprint_init(&dstbuf, 1, INT_MAX); /* (int)dstbuf.len must be >= 0 */
    av_bprint_escape(&dstbuf, src, special_chars, mode, flags);

    if (!av_bprint_is_complete(&dstbuf)) {
        av_bprint_finalize(&dstbuf, NULL);
        return AVERROR(ENOMEM);
    }
    if ((ret = av_bprint_finalize(&dstbuf, dst)) < 0)
        return ret;
    return dstbuf.len;
}

int av_match_name(const char *name, const char *names)
{
    const char *p;
    size_t len, namelen;

    if (!name || !names)
        return 0;

    namelen = strlen(name);
    while (*names) {
        int negate = '-' == *names;
        p = strchr(names, ',');
        if (!p)
            p = names + strlen(names);
        names += negate;
        len = FFMAX(p - names, namelen);
        if (!av_strncasecmp(name, names, len) || !strncmp("ALL", names, FFMAX(3, p - names)))
            return !negate;
        names = p + (*p == ',');
    }
    return 0;
}

int av_utf8_decode(int32_t *codep, const uint8_t **bufp, const uint8_t *buf_end,
                   unsigned int flags)
{
    const uint8_t *p = *bufp;
    uint32_t top;
    uint64_t code;
    int ret = 0, tail_len;
    uint32_t overlong_encoding_mins[6] = {
        0x00000000, 0x00000080, 0x00000800, 0x00010000, 0x00200000, 0x04000000,
    };

    if (p >= buf_end)
        return 0;

    code = *p++;

    /* first sequence byte starts with 10, or is 1111-1110 or 1111-1111,
       which is not admitted */
    if ((code & 0xc0) == 0x80 || code >= 0xFE) {
        ret = AVERROR(EILSEQ);
        goto end;
    }
    top = (code & 128) >> 1;

    tail_len = 0;
    while (code & top) {
        int tmp;
        tail_len++;
        if (p >= buf_end) {
            (*bufp) ++;
            return AVERROR(EILSEQ); /* incomplete sequence */
        }

        /* we assume the byte to be in the form 10xx-xxxx */
        tmp = *p++ - 128;   /* strip leading 1 */
        if (tmp>>6) {
            (*bufp) ++;
            return AVERROR(EILSEQ);
        }
        code = (code<<6) + tmp;
        top <<= 5;
    }
    code &= (top << 1) - 1;

    /* check for overlong encodings */
    av_assert0(tail_len <= 5);
    if (code < overlong_encoding_mins[tail_len]) {
        ret = AVERROR(EILSEQ);
        goto end;
    }

    if (code >= 1U<<31) {
        ret = AVERROR(EILSEQ);  /* out-of-range value */
        goto end;
    }

    *codep = code;

    if (code > 0x10FFFF &&
        !(flags & AV_UTF8_FLAG_ACCEPT_INVALID_BIG_CODES))
        ret = AVERROR(EILSEQ);
    if (code < 0x20 && code != 0x9 && code != 0xA && code != 0xD &&
        flags & AV_UTF8_FLAG_EXCLUDE_XML_INVALID_CONTROL_CODES)
        ret = AVERROR(EILSEQ);
    if (code >= 0xD800 && code <= 0xDFFF &&
        !(flags & AV_UTF8_FLAG_ACCEPT_SURROGATES))
        ret = AVERROR(EILSEQ);
    if ((code == 0xFFFE || code == 0xFFFF) &&
        !(flags & AV_UTF8_FLAG_ACCEPT_NON_CHARACTERS))
        ret = AVERROR(EILSEQ);

end:
    *bufp = p;
    return ret;
}

int av_match_list(const char *name, const char *list, char separator)
{
    const char *p, *q;

    for (p = name; p && *p; ) {
        for (q = list; q && *q; ) {
            int k;
            for (k = 0; p[k] == q[k] || (p[k]*q[k] == 0 && p[k]+q[k] == separator); k++)
                if (k && (!p[k] || p[k] == separator))
                    return 1;
            q = strchr(q, separator);
            if(q)
                q++;
        }
        p = strchr(p, separator);
        if (p)
            p++;
    }

    return 0;
}
