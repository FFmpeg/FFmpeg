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

int avpriv_framenum_to_drop_timecode(int frame_num)
{
    /* only works for NTSC 29.97 */
    int d = frame_num / 17982;
    int m = frame_num % 17982;
    //if (m < 2) m += 2; /* not needed since -2,-1 / 1798 in C returns 0 */
    return frame_num + 18 * d + 2 * ((m - 2) / 1798);
}

uint32_t avpriv_framenum_to_smpte_timecode(unsigned frame, int fps, int drop)
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

int avpriv_check_timecode_rate(void *avcl, AVRational rate, int drop)
{
    int fps;

    if (!rate.num || !rate.den) {
        av_log(avcl, AV_LOG_ERROR, "Timecode frame rate must be specified\n");
        return -1;
    }
    fps = (rate.num + rate.den/2) / rate.den;
    if (drop && fps != 30) {
        av_log(avcl, AV_LOG_ERROR, "Drop frame is only allowed with 30000/1001 FPS\n");
        return -2;
    }
    switch (fps) {
    case 24:
    case 25:
    case 30: return  0;

    default:
        av_log(avcl, AV_LOG_ERROR, "Timecode frame rate not supported\n");
        return -3;
    }
}

char *avpriv_timecode_to_string(char *buf, const struct ff_timecode *tc, unsigned frame)
{
    int frame_num = tc->start + frame;
    int fps = (tc->rate.num + tc->rate.den/2) / tc->rate.den;
    int hh, mm, ss, ff, neg = 0;

    if (tc->drop)
        frame_num = avpriv_framenum_to_drop_timecode(frame_num);
    if (frame_num < 0) {
        frame_num = -frame_num;
        neg = 1;
    }
    ff = frame_num % fps;
    ss = frame_num / fps        % 60;
    mm = frame_num / (fps*60)   % 60;
    hh = frame_num / (fps*3600);
    snprintf(buf, 16, "%s%02d:%02d:%02d%c%02d",
             neg ? "-" : "",
             hh, mm, ss, tc->drop ? ';' : ':', ff);
    return buf;
}

int avpriv_init_smpte_timecode(void *avcl, struct ff_timecode *tc)
{
    int hh, mm, ss, ff, fps, ret;
    char c;

    if (sscanf(tc->str, "%d:%d:%d%c%d", &hh, &mm, &ss, &c, &ff) != 5) {
        av_log(avcl, AV_LOG_ERROR, "unable to parse timecode, "
                                   "syntax: hh:mm:ss[:;.]ff\n");
        return -1;
    }

    tc->drop  = c != ':'; // drop if ';', '.', ...

    ret = avpriv_check_timecode_rate(avcl, tc->rate, tc->drop);
    if (ret < 0)
        return ret;

    fps       = (tc->rate.num + tc->rate.den/2) / tc->rate.den;
    tc->start = (hh*3600 + mm*60 + ss) * fps + ff;

    if (tc->drop) { /* adjust frame number */
        int tmins = 60*hh + mm;
        tc->start -= 2 * (tmins - tmins/10);
    }
    return 0;
}

#if FF_API_OLD_TIMECODE
int ff_framenum_to_drop_timecode(int frame_num)
{
    return avpriv_framenum_to_drop_timecode(frame_num);
}

uint32_t ff_framenum_to_smtpe_timecode(unsigned frame, int fps, int drop)
{
    return avpriv_framenum_to_smpte_timecode(frame, fps, drop);
}

int ff_init_smtpe_timecode(void *avcl, struct ff_timecode *tc)
{
    return avpriv_init_smpte_timecode(avcl, tc);
}
#endif
