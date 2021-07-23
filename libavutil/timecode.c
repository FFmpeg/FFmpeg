/*
 * Copyright (c) 2006 Smartjog S.A.S, Baptiste Coudurier <baptiste.coudurier@gmail.com>
 * Copyright (c) 2011-2012 Smartjog S.A.S, Clément Bœsch <clement.boesch@smartjog.com>
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
 * @see https://en.wikipedia.org/wiki/SMPTE_time_code
 * @see http://www.dropframetimecode.org
 */

#include <stdio.h>
#include "common.h"
#include "timecode.h"
#include "log.h"
#include "error.h"

int av_timecode_adjust_ntsc_framenum2(int framenum, int fps)
{
    /* only works for multiples of NTSC 29.97 */
    int drop_frames = 0;
    int d, m, frames_per_10mins;

    if (fps && fps % 30 == 0) {
        drop_frames = fps / 30 * 2;
        frames_per_10mins = fps / 30 * 17982;
    } else
        return framenum;

    d = framenum / frames_per_10mins;
    m = framenum % frames_per_10mins;

    return framenum + 9U * drop_frames * d + drop_frames * ((m - drop_frames) / (frames_per_10mins / 10));
}

uint32_t av_timecode_get_smpte_from_framenum(const AVTimecode *tc, int framenum)
{
    unsigned fps = tc->fps;
    int drop = !!(tc->flags & AV_TIMECODE_FLAG_DROPFRAME);
    int hh, mm, ss, ff;

    framenum += tc->start;
    if (drop)
        framenum = av_timecode_adjust_ntsc_framenum2(framenum, tc->fps);
    ff = framenum % fps;
    ss = framenum / fps      % 60;
    mm = framenum / (fps*60) % 60;
    hh = framenum / (fps*3600) % 24;
    return av_timecode_get_smpte(tc->rate, drop, hh, mm, ss, ff);
}

uint32_t av_timecode_get_smpte(AVRational rate, int drop, int hh, int mm, int ss, int ff)
{
    uint32_t tc = 0;

    /* For SMPTE 12-M timecodes, frame count is a special case if > 30 FPS.
       See SMPTE ST 12-1:2014 Sec 12.1 for more info. */
    if (av_cmp_q(rate, (AVRational) {30, 1}) == 1) {
        if (ff % 2 == 1) {
            if (av_cmp_q(rate, (AVRational) {50, 1}) == 0)
                tc |= (1 << 7);
            else
                tc |= (1 << 23);
        }
        ff /= 2;
    }

    hh = hh % 24;
    mm = av_clip(mm, 0, 59);
    ss = av_clip(ss, 0, 59);
    ff = ff % 40;

    tc |= drop << 30;
    tc |= (ff / 10) << 28;
    tc |= (ff % 10) << 24;
    tc |= (ss / 10) << 20;
    tc |= (ss % 10) << 16;
    tc |= (mm / 10) << 12;
    tc |= (mm % 10) << 8;
    tc |= (hh / 10) << 4;
    tc |= (hh % 10);

    return tc;
}

char *av_timecode_make_string(const AVTimecode *tc, char *buf, int framenum)
{
    int fps = tc->fps;
    int drop = tc->flags & AV_TIMECODE_FLAG_DROPFRAME;
    int hh, mm, ss, ff, neg = 0;

    framenum += tc->start;
    if (drop)
        framenum = av_timecode_adjust_ntsc_framenum2(framenum, fps);
    if (framenum < 0) {
        framenum = -framenum;
        neg = tc->flags & AV_TIMECODE_FLAG_ALLOWNEGATIVE;
    }
    ff = framenum % fps;
    ss = framenum / fps        % 60;
    mm = framenum / (fps*60LL) % 60;
    hh = framenum / (fps*3600LL);
    if (tc->flags & AV_TIMECODE_FLAG_24HOURSMAX)
        hh = hh % 24;
    snprintf(buf, AV_TIMECODE_STR_SIZE, "%s%02d:%02d:%02d%c%02d",
             neg ? "-" : "",
             hh, mm, ss, drop ? ';' : ':', ff);
    return buf;
}

static unsigned bcd2uint(uint8_t bcd)
{
   unsigned low  = bcd & 0xf;
   unsigned high = bcd >> 4;
   if (low > 9 || high > 9)
       return 0;
   return low + 10*high;
}

char *av_timecode_make_smpte_tc_string2(char *buf, AVRational rate, uint32_t tcsmpte, int prevent_df, int skip_field)
{
    unsigned hh   = bcd2uint(tcsmpte     & 0x3f);    // 6-bit hours
    unsigned mm   = bcd2uint(tcsmpte>>8  & 0x7f);    // 7-bit minutes
    unsigned ss   = bcd2uint(tcsmpte>>16 & 0x7f);    // 7-bit seconds
    unsigned ff   = bcd2uint(tcsmpte>>24 & 0x3f);    // 6-bit frames
    unsigned drop = tcsmpte & 1<<30 && !prevent_df;  // 1-bit drop if not arbitrary bit

    if (av_cmp_q(rate, (AVRational) {30, 1}) == 1) {
        ff <<= 1;
        if (!skip_field) {
            if (av_cmp_q(rate, (AVRational) {50, 1}) == 0)
                ff += !!(tcsmpte & 1 << 7);
            else
                ff += !!(tcsmpte & 1 << 23);
        }
    }

    snprintf(buf, AV_TIMECODE_STR_SIZE, "%02u:%02u:%02u%c%02u",
             hh, mm, ss, drop ? ';' : ':', ff);
    return buf;

}

char *av_timecode_make_smpte_tc_string(char *buf, uint32_t tcsmpte, int prevent_df)
{
    return av_timecode_make_smpte_tc_string2(buf, (AVRational){30, 1}, tcsmpte, prevent_df, 1);
}

char *av_timecode_make_mpeg_tc_string(char *buf, uint32_t tc25bit)
{
    snprintf(buf, AV_TIMECODE_STR_SIZE,
             "%02"PRIu32":%02"PRIu32":%02"PRIu32"%c%02"PRIu32,
             tc25bit>>19 & 0x1f,              // 5-bit hours
             tc25bit>>13 & 0x3f,              // 6-bit minutes
             tc25bit>>6  & 0x3f,              // 6-bit seconds
             tc25bit     & 1<<24 ? ';' : ':', // 1-bit drop flag
             tc25bit     & 0x3f);             // 6-bit frames
    return buf;
}

static int check_fps(int fps)
{
    int i;
    static const int supported_fps[] = {
        24, 25, 30, 48, 50, 60, 100, 120, 150,
    };

    for (i = 0; i < FF_ARRAY_ELEMS(supported_fps); i++)
        if (fps == supported_fps[i])
            return 0;
    return -1;
}

static int check_timecode(void *log_ctx, AVTimecode *tc)
{
    if ((int)tc->fps <= 0) {
        av_log(log_ctx, AV_LOG_ERROR, "Valid timecode frame rate must be specified. Minimum value is 1\n");
        return AVERROR(EINVAL);
    }
    if ((tc->flags & AV_TIMECODE_FLAG_DROPFRAME) && tc->fps % 30 != 0) {
        av_log(log_ctx, AV_LOG_ERROR, "Drop frame is only allowed with multiples of 30000/1001 FPS\n");
        return AVERROR(EINVAL);
    }
    if (check_fps(tc->fps) < 0) {
        av_log(log_ctx, AV_LOG_WARNING, "Using non-standard frame rate %d/%d\n",
               tc->rate.num, tc->rate.den);
    }
    return 0;
}

static int fps_from_frame_rate(AVRational rate)
{
    if (!rate.den || !rate.num)
        return -1;
    return (rate.num + rate.den/2) / rate.den;
}

int av_timecode_check_frame_rate(AVRational rate)
{
    return check_fps(fps_from_frame_rate(rate));
}

int av_timecode_init(AVTimecode *tc, AVRational rate, int flags, int frame_start, void *log_ctx)
{
    memset(tc, 0, sizeof(*tc));
    tc->start = frame_start;
    tc->flags = flags;
    tc->rate  = rate;
    tc->fps   = fps_from_frame_rate(rate);
    return check_timecode(log_ctx, tc);
}

int av_timecode_init_from_components(AVTimecode *tc, AVRational rate, int flags, int hh, int mm, int ss, int ff, void *log_ctx)
{
    int ret;

    memset(tc, 0, sizeof(*tc));
    tc->flags = flags;
    tc->rate  = rate;
    tc->fps   = fps_from_frame_rate(rate);

    ret = check_timecode(log_ctx, tc);
    if (ret < 0)
        return ret;

    tc->start = (hh*3600 + mm*60 + ss) * tc->fps + ff;
    if (tc->flags & AV_TIMECODE_FLAG_DROPFRAME) { /* adjust frame number */
        int tmins = 60*hh + mm;
        tc->start -= (tc->fps / 30 * 2) * (tmins - tmins/10);
    }
    return 0;
}

int av_timecode_init_from_string(AVTimecode *tc, AVRational rate, const char *str, void *log_ctx)
{
    char c;
    int hh, mm, ss, ff, flags;

    if (sscanf(str, "%d:%d:%d%c%d", &hh, &mm, &ss, &c, &ff) != 5) {
        av_log(log_ctx, AV_LOG_ERROR, "Unable to parse timecode, "
                                      "syntax: hh:mm:ss[:;.]ff\n");
        return AVERROR_INVALIDDATA;
    }
    flags = c != ':' ? AV_TIMECODE_FLAG_DROPFRAME : 0; // drop if ';', '.', ...

    return av_timecode_init_from_components(tc, rate, flags, hh, mm, ss, ff, log_ctx);
}
