/*
 * SSA/ASS common funtions
 * Copyright (c) 2010  Aurelien Jacobs <aurel@gnuage.org>
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

#include "avcodec.h"
#include "ass.h"
#include "libavutil/avstring.h"

/**
 * Generate a suitable AVCodecContext.subtitle_header for SUBTITLE_ASS.
 *
 * @param avctx pointer to the AVCodecContext
 * @param font name of the default font face to use
 * @param font_size default font size to use
 * @param color default text color to use (ABGR)
 * @param back_color default background color to use (ABGR)
 * @param bold 1 for bold text, 0 for normal text
 * @param italic 1 for italic text, 0 for normal text
 * @param underline 1 for underline text, 0 for normal text
 * @param alignment position of the text (left, center, top...), defined after
 *                  the layout of the numpad (1-3 sub, 4-6 mid, 7-9 top)
 * @return >= 0 on success otherwise an error code <0
 */
static int ff_ass_subtitle_header(AVCodecContext *avctx,
                                  const char *font, int font_size,
                                  int color, int back_color,
                                  int bold, int italic, int underline,
                                  int alignment)
{
    char header[512];

    snprintf(header, sizeof(header),
             "[Script Info]\r\n"
             "ScriptType: v4.00+\r\n"
             "\r\n"
             "[V4+ Styles]\r\n"
             "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, AlphaLevel, Encoding\r\n"
             "Style: Default,%s,%d,&H%x,&H%x,&H%x,&H%x,%d,%d,%d,1,1,0,%d,10,10,10,0,0\r\n"
             "\r\n"
             "[Events]\r\n"
             "Format: Layer, Start, End, Text\r\n",
             font, font_size, color, color, back_color, back_color,
             -bold, -italic, -underline, alignment);

    avctx->subtitle_header = av_strdup(header);
    if (!avctx->subtitle_header)
        return AVERROR(ENOMEM);
    avctx->subtitle_header_size = strlen(avctx->subtitle_header);
    return 0;
}

int ff_ass_subtitle_header_default(AVCodecContext *avctx)
{
    return ff_ass_subtitle_header(avctx, ASS_DEFAULT_FONT,
                                         ASS_DEFAULT_FONT_SIZE,
                                         ASS_DEFAULT_COLOR,
                                         ASS_DEFAULT_BACK_COLOR,
                                         ASS_DEFAULT_BOLD,
                                         ASS_DEFAULT_ITALIC,
                                         ASS_DEFAULT_UNDERLINE,
                                         ASS_DEFAULT_ALIGNMENT);
}

void ff_ass_init(AVSubtitle *sub)
{
    memset(sub, 0, sizeof(*sub));
}

static int ts_to_string(char *str, int strlen, int ts)
{
    int h, m, s;
    h = ts/360000;  ts -= 360000*h;
    m = ts/  6000;  ts -=   6000*m;
    s = ts/   100;  ts -=    100*s;
    return snprintf(str, strlen, "%d:%02d:%02d.%02d", h, m, s, ts);
}

int ff_ass_add_rect(AVSubtitle *sub, const char *dialog,
                    int ts_start, int ts_end, int raw)
{
    int len = 0, dlen, duration = ts_end - ts_start;
    char s_start[16], s_end[16], header[48] = {0};
    AVSubtitleRect **rects;

    if (!raw) {
        ts_to_string(s_start, sizeof(s_start), ts_start);
        ts_to_string(s_end,   sizeof(s_end),   ts_end  );
        len = snprintf(header, sizeof(header), "Dialogue: 0,%s,%s,",
                       s_start, s_end);
    }

    dlen = strcspn(dialog, "\n");
    dlen += dialog[dlen] == '\n';

    rects = av_realloc(sub->rects, (sub->num_rects+1) * sizeof(*sub->rects));
    if (!rects)
        return AVERROR(ENOMEM);
    sub->rects = rects;
    sub->end_display_time = FFMAX(sub->end_display_time, 10 * duration);
    rects[sub->num_rects]       = av_mallocz(sizeof(*rects[0]));
    rects[sub->num_rects]->type = SUBTITLE_ASS;
    rects[sub->num_rects]->ass  = av_malloc(len + dlen + 1);
    strcpy (rects[sub->num_rects]->ass      , header);
    av_strlcpy(rects[sub->num_rects]->ass + len, dialog, dlen + 1);
    sub->num_rects++;
    return dlen;
}
