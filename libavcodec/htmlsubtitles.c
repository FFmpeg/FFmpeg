/*
 * Copyright (c) 2010  Aurelien Jacobs <aurel@gnuage.org>
 * Copyright (c) 2017  Clément Bœsch <u@pkh.me>
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

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/common.h"
#include "libavutil/parseutils.h"
#include "htmlsubtitles.h"
#include <ctype.h>

static int html_color_parse(void *log_ctx, const char *str)
{
    uint8_t rgba[4];
    int nb_sharps = 0;
    while (str[nb_sharps] == '#')
        nb_sharps++;
    str += FFMAX(0, nb_sharps - 1);
    if (av_parse_color(rgba, str, strcspn(str, "\" >"), log_ctx) < 0)
        return -1;
    return rgba[0] | rgba[1] << 8 | rgba[2] << 16;
}

static void rstrip_spaces_buf(AVBPrint *buf)
{
    if (av_bprint_is_complete(buf))
        while (buf->len > 0 && buf->str[buf->len - 1] == ' ')
            buf->str[--buf->len] = 0;
}

/*
 * Fast code for scanning text enclosed in braces. Functionally
 * equivalent to this sscanf call:
 *
 * sscanf(in, "{\\an%*1u}%n", &len) >= 0 && len > 0
 */
static int scanbraces(const char* in) {
    if (strncmp(in, "{\\an", 4) != 0) {
        return 0;
    }
    if (!av_isdigit(in[4])) {
        return 0;
    }
    if (in[5] != '}') {
        return 0;
    }
    return 1;
}

/* skip all {\xxx} substrings except for {\an%d}
   and all microdvd like styles such as {Y:xxx} */
static void handle_open_brace(AVBPrint *dst, const char **inp, int *an, int *closing_brace_missing)
{
    const char *in = *inp;

    *an += scanbraces(in);

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

struct font_tag {
    char face[128];
    int size;
    uint32_t color;
};

/*
 * Fast code for scanning the rest of a tag. Functionally equivalent to
 * this sscanf call:
 *
 * sscanf(in, "%127[^<>]>%n", buffer, lenp) == 2
 */
static int scantag(const char* in, char* buffer, int* lenp) {
    int len;

    for (len = 0; len < 128; len++) {
        const char c = *in++;
        switch (c) {
        case '\0':
            return 0;
        case '<':
            return 0;
        case '>':
            buffer[len] = '\0';
            *lenp = len+1;
            return 1;
        default:
            break;
        }
        buffer[len] = c;
    }
    return 0;
}

/*
 * The general politic of the convert is to mask unsupported tags or formatting
 * errors (but still alert the user/subtitles writer with an error/warning)
 * without dropping any actual text content for the final user.
 */
int ff_htmlmarkup_to_ass(void *log_ctx, AVBPrint *dst, const char *in)
{
    char *param, buffer[128];
    int len, tag_close, sptr = 0, line_start = 1, an = 0, end = 0;
    int closing_brace_missing = 0;
    int i, likely_a_tag;

    /*
     * state stack is only present for fonts since they are the only tags where
     * the state is not binary. Here is a typical use case:
     *
     *   <font color="red" size=10>
     *     red 10
     *     <font size=50> RED AND BIG </font>
     *     red 10 again
     *   </font>
     *
     * On the other hand, using the state system for all the tags should be
     * avoided because it breaks wrongly nested tags such as:
     *
     *   <b> foo <i> bar </b> bla </i>
     *
     * We don't want to break here; instead, we will treat all these tags as
     * binary state markers. Basically, "<b>" will activate bold, and "</b>"
     * will deactivate it, whatever the current state.
     *
     * This will also prevents cases where we have a random closing tag
     * remaining after the opening one was dropped. Yes, this happens and we
     * still don't want to print a "</b>" at the end of the dialog event.
     */
    struct font_tag stack[16];

    memset(&stack[0], 0, sizeof(stack[0]));

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
            /*
             * "<<" are likely latin guillemets in ASCII or some kind of random
             * style effect; see sub/badsyntax.srt in the FATE samples
             * directory for real test cases.
             */

            likely_a_tag = 1;
            for (i = 0; in[1] == '<'; i++) {
                av_bprint_chars(dst, '<', 1);
                likely_a_tag = 0;
                in++;
            }

            tag_close = in[1] == '/';
            if (tag_close)
                likely_a_tag = 1;

            av_assert0(in[0] == '<');

            len = 0;

            if (scantag(in+tag_close+1, buffer, &len) && len > 0) {
                const int skip = len + tag_close;
                const char *tagname = buffer;
                while (*tagname == ' ') {
                    likely_a_tag = 0;
                    tagname++;
                }
                if ((param = strchr(tagname, ' ')))
                    *param++ = 0;

                /* Check if this is likely a tag */
#define LIKELY_A_TAG_CHAR(x) (((x) >= '0' && (x) <= '9') || \
                              ((x) >= 'a' && (x) <= 'z') || \
                              ((x) >= 'A' && (x) <= 'Z') || \
                               (x) == '_' || (x) == '/')
                for (i = 0; tagname[i]; i++) {
                    if (!LIKELY_A_TAG_CHAR(tagname[i])) {
                        likely_a_tag = 0;
                        break;
                    }
                }

                if (!av_strcasecmp(tagname, "font")) {
                    if (tag_close && sptr > 0) {
                        struct font_tag *cur_tag  = &stack[sptr--];
                        struct font_tag *last_tag = &stack[sptr];

                        if (cur_tag->size) {
                            if (!last_tag->size)
                                av_bprintf(dst, "{\\fs}");
                            else if (last_tag->size != cur_tag->size)
                                av_bprintf(dst, "{\\fs%d}", last_tag->size);
                        }

                        if (cur_tag->color & 0xff000000) {
                            if (!(last_tag->color & 0xff000000))
                                av_bprintf(dst, "{\\c}");
                            else if (last_tag->color != cur_tag->color)
                                av_bprintf(dst, "{\\c&H%"PRIX32"&}", last_tag->color & 0xffffff);
                        }

                        if (cur_tag->face[0]) {
                            if (!last_tag->face[0])
                                av_bprintf(dst, "{\\fn}");
                            else if (strcmp(last_tag->face, cur_tag->face))
                                av_bprintf(dst, "{\\fn%s}", last_tag->face);
                        }
                    } else if (!tag_close && sptr < FF_ARRAY_ELEMS(stack) - 1) {
                        struct font_tag *new_tag = &stack[sptr + 1];

                        *new_tag = stack[sptr++];

                        while (param) {
                            if (!av_strncasecmp(param, "size=", 5)) {
                                param += 5 + (param[5] == '"');
                                if (sscanf(param, "%u", &new_tag->size) == 1)
                                    av_bprintf(dst, "{\\fs%u}", new_tag->size);
                            } else if (!av_strncasecmp(param, "color=", 6)) {
                                int color;
                                param += 6 + (param[6] == '"');
                                color = html_color_parse(log_ctx, param);
                                if (color >= 0) {
                                    new_tag->color = 0xff000000 | color;
                                    av_bprintf(dst, "{\\c&H%"PRIX32"&}", new_tag->color & 0xffffff);
                                }
                            } else if (!av_strncasecmp(param, "face=", 5)) {
                                param += 5 + (param[5] == '"');
                                len = strcspn(param,
                                              param[-1] == '"' ? "\"" :" ");
                                av_strlcpy(new_tag->face, param,
                                           FFMIN(sizeof(new_tag->face), len+1));
                                param += len;
                                av_bprintf(dst, "{\\fn%s}", new_tag->face);
                            }
                            if ((param = strchr(param, ' ')))
                                param++;
                        }
                    }
                    in += skip;
                } else if (tagname[0] && !tagname[1] && strchr("bisu", av_tolower(tagname[0]))) {
                    av_bprintf(dst, "{\\%c%d}", (char)av_tolower(tagname[0]), !tag_close);
                    in += skip;
                } else if (!av_strncasecmp(tagname, "br", 2) &&
                           (!tagname[2] || (tagname[2] == '/' && !tagname[3]))) {
                    av_bprintf(dst, "\\N");
                    in += skip;
                } else if (likely_a_tag) {
                    if (!tag_close) // warn only once
                        av_log(log_ctx, AV_LOG_WARNING, "Unrecognized tag %s\n", tagname);
                    in += skip;
                } else {
                    av_bprint_chars(dst, '<', 1);
                }
            } else {
                av_bprint_chars(dst, *in, 1);
            }
            break;
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
