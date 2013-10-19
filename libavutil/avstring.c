/*
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
 * Copyright (c) 2007 Mans Rullgard
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

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "common.h"
#include "mem.h"
#include "avstring.h"

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
        return s1;

    do
        if (av_stristart(s1, s2, NULL))
            return s1;
    while (*s1++);

    return NULL;
}

char *av_strnstr(const char *haystack, const char *needle, size_t hay_length)
{
    size_t needle_len = strlen(needle);
    if (!needle_len)
        return haystack;
    while (hay_length >= needle_len) {
        hay_length--;
        if (!memcmp(haystack, needle, needle_len))
            return haystack;
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
    int len = strlen(dst);
    va_list vl;

    va_start(vl, fmt);
    len += vsnprintf(dst + len, size > len ? size - len : 0, fmt, vl);
    va_end(vl);

    return len;
}

char *av_d2str(double d)
{
    char *str = av_malloc(16);
    if (str)
        snprintf(str, 16, "%f", d);
    return str;
}

#define WHITESPACES " \n\t"

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
    const char *end = a + n;
    uint8_t c1, c2;
    do {
        c1 = av_tolower(*a++);
        c2 = av_tolower(*b++);
    } while (a < end && c1 && c1 == c2);
    return c1 - c2;
}

const char *av_basename(const char *path)
{
    char *p = strrchr(path, '/');

#if HAVE_DOS_PATHS
    char *q = strrchr(path, '\\');
    char *d = strchr(path, ':');

    p = FFMAX3(p, q, d);
#endif

    if (!p)
        return path;

    return p + 1;
}

const char *av_dirname(char *path)
{
    char *p = strrchr(path, '/');

#if HAVE_DOS_PATHS
    char *q = strrchr(path, '\\');
    char *d = strchr(path, ':');

    d = d ? d + 1 : d;

    p = FFMAX3(p, q, d);
#endif

    if (!p)
        return ".";

    *p = '\0';

    return path;
}

int av_isdigit(int c)
{
    return c >= '0' && c <= '9';
}

int av_isgraph(int c)
{
    return c > 32 && c < 127;
}

int av_isspace(int c)
{
    return c == ' ' || c == '\f' || c == '\n' || c == '\r' || c == '\t' ||
           c == '\v';
}

int av_isxdigit(int c)
{
    c = av_tolower(c);
    return av_isdigit(c) || (c >= 'a' && c <= 'f');
}

#ifdef TEST

int main(void)
{
    int i;
    const char *strings[] = {
        "''",
        "",
        ":",
        "\\",
        "'",
        "    ''    :",
        "    ''  ''  :",
        "foo   '' :",
        "'foo'",
        "foo     ",
        "  '  foo  '  ",
        "foo\\",
        "foo':  blah:blah",
        "foo\\:  blah:blah",
        "foo\'",
        "'foo :  '  :blahblah",
        "\\ :blah",
        "     foo",
        "      foo       ",
        "      foo     \\ ",
        "foo ':blah",
        " foo   bar    :   blahblah",
        "\\f\\o\\o",
        "'foo : \\ \\  '   : blahblah",
        "'\\fo\\o:': blahblah",
        "\\'fo\\o\\:':  foo  '  :blahblah"
    };

    printf("Testing av_get_token()\n");
    for (i = 0; i < FF_ARRAY_ELEMS(strings); i++) {
        const char *p = strings[i];
        char *q;
        printf("|%s|", p);
        q = av_get_token(&p, ":");
        printf(" -> |%s|", q);
        printf(" + |%s|\n", p);
        av_free(q);
    }

    return 0;
}

#endif /* TEST */
