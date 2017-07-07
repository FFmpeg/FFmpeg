/*
 * Copyright (c) 2010  Aurelien Jacobs <aurel@gnuage.org>
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

#include "libavutil/avstring.h"
#include "libavutil/common.h"
#include "libavutil/parseutils.h"
#include "htmlsubtitles.h"

static int html_color_parse(void *log_ctx, const char *str)
{
    uint8_t rgba[4];
    if (av_parse_color(rgba, str, strcspn(str, "\" >"), log_ctx) < 0)
        return -1;
    return rgba[0] | rgba[1] << 8 | rgba[2] << 16;
}

enum {
    PARAM_UNKNOWN = -1,
    PARAM_SIZE,
    PARAM_COLOR,
    PARAM_FACE,
    PARAM_NUMBER
};

typedef struct SrtStack {
    char tag[128];
    char param[PARAM_NUMBER][128];
} SrtStack;

static void rstrip_spaces_buf(AVBPrint *buf)
{
    if (av_bprint_is_complete(buf))
        while (buf->len > 0 && buf->str[buf->len - 1] == ' ')
            buf->str[--buf->len] = 0;
}

/* skip all {\xxx} substrings except for {\an%d}
   and all microdvd like styles such as {Y:xxx} */
static void handle_open_brace(AVBPrint *dst, const char **inp, int *an, int *closing_brace_missing)
{
    int len = 0;
    const char *in = *inp;

    *an += sscanf(in, "{\\an%*1u}%n", &len) >= 0 && len > 0;

    if (!*closing_brace_missing) {
        if (   (*an != 1 && in[1] == '\\')
            || (in[1] && strchr("CcFfoPSsYy", in[1]) && in[2] == ':')) {
            char *bracep = strchr(in+2, '}');
            if (bracep) {
                *inp = bracep;
                return;
            } else
                *closing_brace_missing = 1;
        }
    }

    av_bprint_chars(dst, *in, 1);
}

int ff_htmlmarkup_to_ass(void *log_ctx, AVBPrint *dst, const char *in)
{
    char *param, buffer[128], tmp[128];
    int len, tag_close, sptr = 1, line_start = 1, an = 0, end = 0;
    SrtStack stack[16];
    int closing_brace_missing = 0;

    stack[0].tag[0] = 0;
    strcpy(stack[0].param[PARAM_SIZE],  "{\\fs}");
    strcpy(stack[0].param[PARAM_COLOR], "{\\c}");
    strcpy(stack[0].param[PARAM_FACE],  "{\\fn}");

    for (; !end && *in; in++) {
        switch (*in) {
        case '\r':
            break;
        case '\n':
            if (line_start) {
                end = 1;
                break;
            }
            rstrip_spaces_buf(dst);
            av_bprintf(dst, "\\N");
            line_start = 1;
            break;
        case ' ':
            if (!line_start)
                av_bprint_chars(dst, *in, 1);
            break;
        case '{':
            handle_open_brace(dst, &in, &an, &closing_brace_missing);
            break;
        case '<':
            tag_close = in[1] == '/';
            len = 0;
            if (sscanf(in+tag_close+1, "%127[^>]>%n", buffer, &len) >= 1 && len > 0) {
                const char *tagname = buffer;
                while (*tagname == ' ')
                    tagname++;
                if ((param = strchr(tagname, ' ')))
                    *param++ = 0;
                if ((!tag_close && sptr < FF_ARRAY_ELEMS(stack)) ||
                    ( tag_close && sptr > 0 && !av_strcasecmp(stack[sptr-1].tag, tagname))) {
                    int i, j, unknown = 0;
                    in += len + tag_close;
                    if (!tag_close)
                        memset(stack+sptr, 0, sizeof(*stack));
                    if (!av_strcasecmp(tagname, "font")) {
                        if (tag_close) {
                            for (i=PARAM_NUMBER-1; i>=0; i--)
                                if (stack[sptr-1].param[i][0])
                                    for (j=sptr-2; j>=0; j--)
                                        if (stack[j].param[i][0]) {
                                            av_bprintf(dst, "%s", stack[j].param[i]);
                                            break;
                                        }
                        } else {
                            while (param) {
                                if (!av_strncasecmp(param, "size=", 5)) {
                                    unsigned font_size;
                                    param += 5 + (param[5] == '"');
                                    if (sscanf(param, "%u", &font_size) == 1) {
                                        snprintf(stack[sptr].param[PARAM_SIZE],
                                             sizeof(stack[0].param[PARAM_SIZE]),
                                             "{\\fs%u}", font_size);
                                    }
                                } else if (!av_strncasecmp(param, "color=", 6)) {
                                    param += 6 + (param[6] == '"');
                                    snprintf(stack[sptr].param[PARAM_COLOR],
                                         sizeof(stack[0].param[PARAM_COLOR]),
                                         "{\\c&H%X&}",
                                         html_color_parse(log_ctx, param));
                                } else if (!av_strncasecmp(param, "face=", 5)) {
                                    param += 5 + (param[5] == '"');
                                    len = strcspn(param,
                                                  param[-1] == '"' ? "\"" :" ");
                                    av_strlcpy(tmp, param,
                                               FFMIN(sizeof(tmp), len+1));
                                    param += len;
                                    snprintf(stack[sptr].param[PARAM_FACE],
                                             sizeof(stack[0].param[PARAM_FACE]),
                                             "{\\fn%s}", tmp);
                                }
                                if ((param = strchr(param, ' ')))
                                    param++;
                            }
                            for (i=0; i<PARAM_NUMBER; i++)
                                if (stack[sptr].param[i][0])
                                    av_bprintf(dst, "%s", stack[sptr].param[i]);
                        }
                    } else if (tagname[0] && !tagname[1] && av_stristr("bisu", tagname)) {
                        av_bprintf(dst, "{\\%c%d}", (char)av_tolower(tagname[0]), !tag_close);
                    } else if (!av_strcasecmp(tagname, "br")) {
                        av_bprintf(dst, "\\N");
                    } else {
                        unknown = 1;
                        snprintf(tmp, sizeof(tmp), "</%s>", tagname);
                    }
                    if (tag_close) {
                        sptr--;
                    } else if (unknown && !strstr(in, tmp)) {
                        in -= len + tag_close;
                        av_bprint_chars(dst, *in, 1);
                    } else
                        av_strlcpy(stack[sptr++].tag, tagname,
                                   sizeof(stack[0].tag));
                    break;
                }
            }
        default:
            av_bprint_chars(dst, *in, 1);
            break;
        }
        if (*in != ' ' && *in != '\r' && *in != '\n')
            line_start = 0;
    }

    if (!av_bprint_is_complete(dst))
        return AVERROR(ENOMEM);

    while (dst->len >= 2 && !strncmp(&dst->str[dst->len - 2], "\\N", 2))
        dst->len -= 2;
    dst->str[dst->len] = 0;
    rstrip_spaces_buf(dst);

    return 0;
}
