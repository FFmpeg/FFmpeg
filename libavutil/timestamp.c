/*
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

#include "timestamp.h"

char *av_ts_make_time_string2(char *buf, int64_t ts, AVRational tb)
{
    if (ts == AV_NOPTS_VALUE) {
        snprintf(buf, AV_TS_MAX_STRING_SIZE, "NOPTS");
    } else {
        double val = av_q2d(tb) * ts;
        double log = floor(log10(fabs(val)));
        int precision = (isfinite(log) && log < 0) ? -log + 5 : 6;
        int last = snprintf(buf, AV_TS_MAX_STRING_SIZE, "%.*f", precision, val);
        last = FFMIN(last, AV_TS_MAX_STRING_SIZE - 1) - 1;
        for (; last && buf[last] == '0'; last--);
        for (; last && buf[last] != 'f' && (buf[last] < '0' || buf[0] > '9'); last--);
        buf[last + 1] = '\0';
    }
    return buf;
}
