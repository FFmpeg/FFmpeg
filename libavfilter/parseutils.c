/*
 * copyright (c) 2009 Stefano Sabatini
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

/**
 * @file libavfilter/parseutils.c
 * parsing utils
 */

#include <string.h>
#include "libavutil/avutil.h"
#include "parseutils.h"

#define WHITESPACES " \n\t"

char *av_get_token(const char **buf, const char *term)
{
    char *out = av_malloc(strlen(*buf) + 1);
    char *ret= out, *end= out;
    const char *p = *buf;
    p += strspn(p, WHITESPACES);

    while(*p && !strspn(p, term)) {
        char c = *p++;
        if(c == '\\' && *p){
            *out++ = *p++;
            end= out;
        }else if(c == '\''){
            while(*p && *p != '\'')
                *out++ = *p++;
            if(*p){
                p++;
                end= out;
            }
        }else{
            *out++ = c;
        }
    }

    do{
        *out-- = 0;
    }while(out >= end && strspn(out, WHITESPACES));

    *buf = p;

    return ret;
}

#ifdef TEST

#undef printf

int main()
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

    for (i=0; i < FF_ARRAY_ELEMS(strings); i++) {
        const char *p= strings[i];
        printf("|%s|", p);
        printf(" -> |%s|", av_get_token(&p, ":"));
        printf(" + |%s|\n", p);
    }

    return 0;
}

#endif
