/*
 * Copyright (c) 2012 Clément Bœsch
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

/**
 * @file
 * MicroDVD subtitle decoder
 *
 * Based on the specifications found here:
 * https://trac.videolan.org/vlc/ticket/1825#comment:6
 */

#include "libavutil/avstring.h"
#include "libavutil/parseutils.h"
#include "libavutil/bprint.h"
#include "avcodec.h"
#include "ass.h"

static int indexof(const char *s, int c)
{
    char *f = strchr(s, c);
    return f ? (f - s) : -1;
}

struct microdvd_tag {
    char key;
    int persistent;
    uint32_t data1;
    uint32_t data2;
    char *data_string;
    int data_string_len;
};

#define MICRODVD_PERSISTENT_OFF     0
#define MICRODVD_PERSISTENT_ON      1
#define MICRODVD_PERSISTENT_OPENED  2

// Color, Font, Size, cHarset, stYle, Position, cOordinate
#define MICRODVD_TAGS "cfshyYpo"

static void microdvd_set_tag(struct microdvd_tag *tags, struct microdvd_tag tag)
{
    int tag_index = indexof(MICRODVD_TAGS, tag.key);

    if (tag_index < 0)
        return;
    memcpy(&tags[tag_index], &tag, sizeof(tag));
}

// italic, bold, underline, strike-through
#define MICRODVD_STYLES "ibus"

/* some samples have lines that start with a / indicating non persistent italic
 * marker */
static char *check_for_italic_slash_marker(struct microdvd_tag *tags, char *s)
{
    if (*s == '/') {
        struct microdvd_tag tag = tags[indexof(MICRODVD_TAGS, 'y')];
        tag.key = 'y';
        tag.data1 |= 1 << 0 /* 'i' position in MICRODVD_STYLES */;
        microdvd_set_tag(tags, tag);
        s++;
    }
    return s;
}

static char *microdvd_load_tags(struct microdvd_tag *tags, char *s)
{
    s = check_for_italic_slash_marker(tags, s);

    while (*s == '{') {
        char *start = s;
        char tag_char = *(s + 1);
        struct microdvd_tag tag = {0};

        if (!tag_char || *(s + 2) != ':')
            break;
        s += 3;

        switch (tag_char) {

        /* Style */
        case 'Y':
            tag.persistent = MICRODVD_PERSISTENT_ON;
        case 'y':
            while (*s && *s != '}') {
                int style_index = indexof(MICRODVD_STYLES, *s);

                if (style_index >= 0)
                    tag.data1 |= (1 << style_index);
                s++;
            }
            if (*s != '}')
                break;
            /* We must distinguish persistent and non-persistent styles
             * to handle this kind of style tags: {y:ib}{Y:us} */
            tag.key = tag_char;
            break;

        /* Color */
        case 'C':
            tag.persistent = MICRODVD_PERSISTENT_ON;
        case 'c':
            while (*s == '$' || *s == '#')
                s++;
            tag.data1 = strtol(s, &s, 16) & 0x00ffffff;
            if (*s != '}')
                break;
            tag.key = 'c';
            break;

        /* Font name */
        case 'F':
            tag.persistent = MICRODVD_PERSISTENT_ON;
        case 'f': {
            int len = indexof(s, '}');
            if (len < 0)
                break;
            tag.data_string = s;
            tag.data_string_len = len;
            s += len;
            tag.key = 'f';
            break;
        }

        /* Font size */
        case 'S':
            tag.persistent = MICRODVD_PERSISTENT_ON;
        case 's':
            tag.data1 = strtol(s, &s, 10);
            if (*s != '}')
                break;
            tag.key = 's';
            break;

        /* Charset */
        case 'H': {
            //TODO: not yet handled, just parsed.
            int len = indexof(s, '}');
            if (len < 0)
                break;
            tag.data_string = s;
            tag.data_string_len = len;
            s += len;
            tag.key = 'h';
            break;
        }

        /* Position */
        case 'P':
            tag.persistent = MICRODVD_PERSISTENT_ON;
            tag.data1 = (*s++ == '1');
            if (*s != '}')
                break;
            tag.key = 'p';
            break;

        /* Coordinates */
        case 'o':
            tag.persistent = MICRODVD_PERSISTENT_ON;
            tag.data1 = strtol(s, &s, 10);
            if (*s != ',')
                break;
            s++;
            tag.data2 = strtol(s, &s, 10);
            if (*s != '}')
                break;
            tag.key = 'o';
            break;

        default:    /* Unknown tag, we consider it's text */
            break;
        }

        if (tag.key == 0)
            return start;

        microdvd_set_tag(tags, tag);
        s++;
    }
    return check_for_italic_slash_marker(tags, s);
}

static void microdvd_open_tags(AVBPrint *new_line, struct microdvd_tag *tags)
{
    int i, sidx;
    for (i = 0; i < sizeof(MICRODVD_TAGS) - 1; i++) {
        if (tags[i].persistent == MICRODVD_PERSISTENT_OPENED)
            continue;
        switch (tags[i].key) {
        case 'Y':
        case 'y':
            for (sidx = 0; sidx < sizeof(MICRODVD_STYLES) - 1; sidx++)
                if (tags[i].data1 & (1 << sidx))
                    av_bprintf(new_line, "{\\%c1}", MICRODVD_STYLES[sidx]);
            break;

        case 'c':
            av_bprintf(new_line, "{\\c&H%06X&}", tags[i].data1);
            break;

        case 'f':
            av_bprintf(new_line, "{\\fn%.*s}",
                       tags[i].data_string_len, tags[i].data_string);
            break;

        case 's':
            av_bprintf(new_line, "{\\fs%d}", tags[i].data1);
            break;

        case 'p':
            if (tags[i].data1 == 0)
                av_bprintf(new_line, "{\\an8}");
            break;

        case 'o':
            av_bprintf(new_line, "{\\pos(%d,%d)}",
                       tags[i].data1, tags[i].data2);
            break;
        }
        if (tags[i].persistent == MICRODVD_PERSISTENT_ON)
            tags[i].persistent = MICRODVD_PERSISTENT_OPENED;
    }
}

static void microdvd_close_no_persistent_tags(AVBPrint *new_line,
                                              struct microdvd_tag *tags)
{
    int i, sidx;

    for (i = sizeof(MICRODVD_TAGS) - 2; i >= 0; i--) {
        if (tags[i].persistent != MICRODVD_PERSISTENT_OFF)
            continue;
        switch (tags[i].key) {

        case 'y':
            for (sidx = sizeof(MICRODVD_STYLES) - 2; sidx >= 0; sidx--)
                if (tags[i].data1 & (1 << sidx))
                    av_bprintf(new_line, "{\\%c0}", MICRODVD_STYLES[sidx]);
            break;

        case 'c':
            av_bprintf(new_line, "{\\c}");
            break;

        case 'f':
            av_bprintf(new_line, "{\\fn}");
            break;

        case 's':
            av_bprintf(new_line, "{\\fs}");
            break;
        }
        tags[i].key = 0;
    }
}

static int microdvd_decode_frame(AVCodecContext *avctx,
                                 void *data, int *got_sub_ptr, AVPacket *avpkt)
{
    AVSubtitle *sub = data;
    AVBPrint new_line;
    char *line = avpkt->data;
    char *end = avpkt->data + avpkt->size;
    struct microdvd_tag tags[sizeof(MICRODVD_TAGS) - 1] = {{0}};

    if (avpkt->size <= 0)
        return avpkt->size;

    av_bprint_init(&new_line, 0, 2048);

    // subtitle content
    while (line < end && *line) {

        // parse MicroDVD tags, and open them in ASS
        line = microdvd_load_tags(tags, line);
        microdvd_open_tags(&new_line, tags);

        // simple copy until EOL or forced carriage return
        while (line < end && *line && *line != '|') {
            av_bprint_chars(&new_line, *line, 1);
            line++;
        }

        // line split
        if (line < end && *line == '|') {
            microdvd_close_no_persistent_tags(&new_line, tags);
            av_bprintf(&new_line, "\\N");
            line++;
        }
    }
    if (new_line.len) {
        int ret;
            int64_t start    = avpkt->pts;
            int64_t duration = avpkt->duration;
            int ts_start     = av_rescale_q(start,    avctx->time_base, (AVRational){1,100});
            int ts_duration  = duration != -1 ?
                av_rescale_q(duration, avctx->time_base, (AVRational){1,100}) : -1;

        ret = ff_ass_add_rect_bprint(sub, &new_line, ts_start, ts_duration);
        av_bprint_finalize(&new_line, NULL);
        if (ret < 0)
            return ret;
    }

    *got_sub_ptr = sub->num_rects > 0;
    return avpkt->size;
}

static int microdvd_init(AVCodecContext *avctx)
{
    int i, sidx;
    AVBPrint font_buf;
    int font_size    = ASS_DEFAULT_FONT_SIZE;
    int color        = ASS_DEFAULT_COLOR;
    int bold         = ASS_DEFAULT_BOLD;
    int italic       = ASS_DEFAULT_ITALIC;
    int underline    = ASS_DEFAULT_UNDERLINE;
    int alignment    = ASS_DEFAULT_ALIGNMENT;
    struct microdvd_tag tags[sizeof(MICRODVD_TAGS) - 1] = {{0}};

    av_bprint_init(&font_buf, 0, AV_BPRINT_SIZE_AUTOMATIC);
    av_bprintf(&font_buf, "%s", ASS_DEFAULT_FONT);

    if (avctx->extradata) {
        microdvd_load_tags(tags, avctx->extradata);
        for (i = 0; i < sizeof(MICRODVD_TAGS) - 1; i++) {
            switch (av_tolower(tags[i].key)) {
            case 'y':
                for (sidx = 0; sidx < sizeof(MICRODVD_STYLES) - 1; sidx++) {
                    if (tags[i].data1 & (1 << sidx)) {
                        switch (MICRODVD_STYLES[sidx]) {
                        case 'i': italic    = 1; break;
                        case 'b': bold      = 1; break;
                        case 'u': underline = 1; break;
                        }
                    }
                }
                break;

            case 'c': color     = tags[i].data1; break;
            case 's': font_size = tags[i].data1; break;
            case 'p': alignment =             8; break;

            case 'f':
                av_bprint_clear(&font_buf);
                av_bprintf(&font_buf, "%.*s",
                           tags[i].data_string_len, tags[i].data_string);
                break;
            }
        }
    }
    return ff_ass_subtitle_header(avctx, font_buf.str, font_size, color,
                                  ASS_DEFAULT_BACK_COLOR, bold, italic,
                                  underline, alignment);
}

AVCodec ff_microdvd_decoder = {
    .name         = "microdvd",
    .long_name    = NULL_IF_CONFIG_SMALL("MicroDVD subtitle"),
    .type         = AVMEDIA_TYPE_SUBTITLE,
    .id           = AV_CODEC_ID_MICRODVD,
    .init         = microdvd_init,
    .decode       = microdvd_decode_frame,
};
