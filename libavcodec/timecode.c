/*
 * Copyright (C) 2006 Smartjog S.A.S, Baptiste Coudurier <baptiste.coudurier@gmail.com>
 * Copyright (C) 2011 Smartjog S.A.S, Clément Bœsch      <clement.boesch@smartjog.com>
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
 * Timecode helpers
 */

#include <stdio.h>
#include "timecode.h"
#include "libavutil/log.h"

int ff_framenum_to_drop_timecode(int frame_num)
{
    /* only works for NTSC 29.97 */
    int d = frame_num / 17982;
    int m = frame_num % 17982;
    //if (m < 2) m += 2; /* not needed since -2,-1 / 1798 in C returns 0 */
    return frame_num + 18 * d + 2 * ((m - 2) / 1798);
}

uint32_t ff_framenum_to_smtpe_timecode(unsigned frame, int fps, int drop)
{
    return (0                                    << 31) | // color frame flag
           (drop                                 << 30) | // drop  frame flag
           ( ((frame % fps) / 10)                << 28) | // tens  of frames
           ( ((frame % fps) % 10)                << 24) | // units of frames
           (0                                    << 23) | // field phase (NTSC), b0 (PAL)
           ((((frame / fps) % 60) / 10)          << 20) | // tens  of seconds
           ((((frame / fps) % 60) % 10)          << 16) | // units of seconds
           (0                                    << 15) | // b0 (NTSC), b2 (PAL)
           ((((frame / (fps * 60)) % 60) / 10)   << 12) | // tens  of minutes
           ((((frame / (fps * 60)) % 60) % 10)   <<  8) | // units of minutes
           (0                                    <<  7) | // b1
           (0                                    <<  6) | // b2 (NTSC), field phase (PAL)
           ((((frame / (fps * 3600) % 24)) / 10) <<  4) | // tens  of hours
           (  (frame / (fps * 3600) % 24)) % 10;          // units of hours
}

int ff_init_smtpe_timecode(void *avcl, struct ff_timecode *tc)
{
    int hh, mm, ss, ff, fps;
    char c;

    if (sscanf(tc->str, "%d:%d:%d%c%d", &hh, &mm, &ss, &c, &ff) != 5) {
        av_log(avcl, AV_LOG_ERROR, "unable to parse timecode, "
                                   "syntax: hh:mm:ss[:;.]ff\n");
        return -1;
    }

    fps       = (tc->rate.num + tc->rate.den/2) / tc->rate.den;
    tc->start = (hh*3600 + mm*60 + ss) * fps + ff;
    tc->drop  = c != ':'; // drop if ';', '.', ...

    if (tc->drop) { /* adjust frame number */
        int tmins = 60*hh + mm;
        if (tc->rate.den != 1001 || fps != 30) {
            av_log(avcl, AV_LOG_ERROR, "error: drop frame is only allowed with"
                                       "30000/1001 FPS");
            return -2;
        }
        tc->start -= 2 * (tmins - tmins/10);
    }
    return 0;
}
