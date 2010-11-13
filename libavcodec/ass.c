/*
 * SSA/ASS common funtions
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

#include "avcodec.h"
#include "ass.h"

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
    strncpy(rects[sub->num_rects]->ass + len, dialog, dlen);
    sub->num_rects++;
    return dlen;
}
