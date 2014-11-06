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
 * JACOsub subtitle decoder
 * @see http://unicorn.us.com/jacosub/jscripts.html
 */

#include <time.h>
#include "ass.h"
#include "jacosub.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/time_internal.h"

#undef time

static int insert_text(AVBPrint *dst, const char *in, const char *arg)
{
    av_bprintf(dst, "%s", arg);
    return 0;
}

static int insert_datetime(AVBPrint *dst, const char *in, const char *arg)
{
    char buf[16] = {0};
    time_t now = time(0);
    struct tm ltime;

    localtime_r(&now, &ltime);
    strftime(buf, sizeof(buf), arg, &ltime);
    av_bprintf(dst, "%s", buf);
    return 0;
}

static int insert_color(AVBPrint *dst, const char *in, const char *arg)
{
    return 1; // skip id
}

static int insert_font(AVBPrint *dst, const char *in, const char *arg)
{
    return 1; // skip id
}

static const struct {
    const char *from;
    const char *arg;
    int (*func)(AVBPrint *dst, const char *in, const char *arg);
} ass_codes_map[] = {
    {"\\~", "~",        insert_text},       // tilde doesn't need escaping
    {"~",   "{\\h}",    insert_text},       // hard space
    {"\\n", "\\N",      insert_text},       // newline
    {"\\D", "%d %b %Y", insert_datetime},   // current date
    {"\\T", "%H:%M",    insert_datetime},   // current time
    {"\\N", "{\\r}",    insert_text},       // reset to default style
    {"\\I", "{\\i1}",   insert_text},       // italic on
    {"\\i", "{\\i0}",   insert_text},       // italic off
    {"\\B", "{\\b1}",   insert_text},       // bold on
    {"\\b", "{\\b0}",   insert_text},       // bold off
    {"\\U", "{\\u1}",   insert_text},       // underline on
    {"\\u", "{\\u0}",   insert_text},       // underline off
    {"\\C", "",         insert_color},      // TODO: color
    {"\\F", "",         insert_font},       // TODO: font
};

enum {
    ALIGN_VB = 1<<0, // vertical bottom, default
    ALIGN_VM = 1<<1, // vertical middle
    ALIGN_VT = 1<<2, // vertical top
    ALIGN_JC = 1<<3, // justify center, default
    ALIGN_JL = 1<<4, // justify left
    ALIGN_JR = 1<<5, // justify right
};

static void jacosub_to_ass(AVCodecContext *avctx, AVBPrint *dst, const char *src)
{
    int i, valign = 0, halign = 0;
    char c = av_toupper(*src);
    char directives[128] = {0};

    /* extract the optional directives */
    if ((c >= 'A' && c <= 'Z') || c == '[') {
        char *p    = directives;
        char *pend = directives + sizeof(directives) - 1;

        do *p++ = av_toupper(*src++);
        while (*src && !jss_whitespace(*src) && p < pend);
        *p = 0;
        src = jss_skip_whitespace(src);
    }

    /* handle directives (TODO: handle more of them, and more reliably) */
    if      (strstr(directives, "VB")) valign = ALIGN_VB;
    else if (strstr(directives, "VM")) valign = ALIGN_VM;
    else if (strstr(directives, "VT")) valign = ALIGN_VT;
    if      (strstr(directives, "JC")) halign = ALIGN_JC;
    else if (strstr(directives, "JL")) halign = ALIGN_JL;
    else if (strstr(directives, "JR")) halign = ALIGN_JR;
    if (valign || halign) {
        if (!valign) valign = ALIGN_VB;
        if (!halign) halign = ALIGN_JC;
        switch (valign | halign) {
        case ALIGN_VB | ALIGN_JL: av_bprintf(dst, "{\\an1}"); break; // bottom left
        case ALIGN_VB | ALIGN_JC: av_bprintf(dst, "{\\an2}"); break; // bottom center
        case ALIGN_VB | ALIGN_JR: av_bprintf(dst, "{\\an3}"); break; // bottom right
        case ALIGN_VM | ALIGN_JL: av_bprintf(dst, "{\\an4}"); break; // middle left
        case ALIGN_VM | ALIGN_JC: av_bprintf(dst, "{\\an5}"); break; // middle center
        case ALIGN_VM | ALIGN_JR: av_bprintf(dst, "{\\an6}"); break; // middle right
        case ALIGN_VT | ALIGN_JL: av_bprintf(dst, "{\\an7}"); break; // top left
        case ALIGN_VT | ALIGN_JC: av_bprintf(dst, "{\\an8}"); break; // top center
        case ALIGN_VT | ALIGN_JR: av_bprintf(dst, "{\\an9}"); break; // top right
        }
    }

    /* process timed line */
    while (*src && *src != '\n') {

        /* text continue on the next line */
        if (src[0] == '\\' && src[1] == '\n') {
            src += 2;
            while (jss_whitespace(*src))
                src++;
            continue;
        }

        /* special character codes */
        for (i = 0; i < FF_ARRAY_ELEMS(ass_codes_map); i++) {
            const char *from = ass_codes_map[i].from;
            const char *arg  = ass_codes_map[i].arg;
            size_t codemap_len = strlen(from);

            if (!strncmp(src, from, codemap_len)) {
                src += codemap_len;
                src += ass_codes_map[i].func(dst, src, arg);
                break;
            }
        }

        /* simple char copy */
        if (i == FF_ARRAY_ELEMS(ass_codes_map))
            av_bprintf(dst, "%c", *src++);
    }
}

static int jacosub_decode_frame(AVCodecContext *avctx,
                                void *data, int *got_sub_ptr, AVPacket *avpkt)
{
    int ret;
    AVSubtitle *sub = data;
    const char *ptr = avpkt->data;

    if (avpkt->size <= 0)
        goto end;

    if (*ptr) {
        AVBPrint buffer;

        // skip timers
        ptr = jss_skip_whitespace(ptr);
        ptr = strchr(ptr, ' '); if (!ptr) goto end; ptr++;
        ptr = strchr(ptr, ' '); if (!ptr) goto end; ptr++;

        av_bprint_init(&buffer, JSS_MAX_LINESIZE, JSS_MAX_LINESIZE);
        jacosub_to_ass(avctx, &buffer, ptr);
        ret = ff_ass_add_rect_bprint(sub, &buffer, avpkt->pts, avpkt->duration);
        av_bprint_finalize(&buffer, NULL);
        if (ret < 0)
            return ret;
    }

end:
    *got_sub_ptr = sub->num_rects > 0;
    return avpkt->size;
}

AVCodec ff_jacosub_decoder = {
    .name           = "jacosub",
    .long_name      = NULL_IF_CONFIG_SMALL("JACOsub subtitle"),
    .type           = AVMEDIA_TYPE_SUBTITLE,
    .id             = AV_CODEC_ID_JACOSUB,
    .init           = ff_ass_subtitle_header_default,
    .decode         = jacosub_decode_frame,
};
