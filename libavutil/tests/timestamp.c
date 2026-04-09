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

#include <inttypes.h>
#include <stdio.h>

#include "libavutil/avutil.h"
#include "libavutil/macros.h"
#include "libavutil/rational.h"
#include "libavutil/timestamp.h"

int main(void)
{
    char buf[AV_TS_MAX_STRING_SIZE];
    static const struct { const char *label; int64_t ts; } ts_vals[] = {
        { "NOPTS",       AV_NOPTS_VALUE  },
        { "0",           0               },
        { "90000",       90000           },
        { "-1",          -1              },
        { "INT64_MAX",   INT64_MAX       },
        { "INT64_MIN+1", INT64_MIN + 1   },
    };
    static const struct { const char *label; int64_t ts; AVRational tb; } time_vals[] = {
        { "NOPTS",             AV_NOPTS_VALUE, {1, 90000} },
        { "90000 at 1/90000",  90000,          {1, 90000} },
        { "0 at 1/1000",       0,              {1, 1000}  },
        { "48000 at 1/48000",  48000,          {1, 48000} },
        { "44100 at 1/44100",  44100,          {1, 44100} },
        { "-90000 at 1/90000", -90000,         {1, 90000} },
    };

    /* av_ts_make_string */
    printf("Testing av_ts_make_string()\n");
    for (int i = 0; i < FF_ARRAY_ELEMS(ts_vals); i++)
        printf("%s: %s\n", ts_vals[i].label,
               av_ts_make_string(buf, ts_vals[i].ts));

    /* av_ts2str macro */
    printf("\nTesting av_ts2str()\n");
    printf("NOPTS: %s\n", av_ts2str(AV_NOPTS_VALUE));
    printf("48000: %s\n", av_ts2str(48000));

    /* av_ts_make_time_string2 */
    printf("\nTesting av_ts_make_time_string2()\n");
    for (int i = 0; i < FF_ARRAY_ELEMS(time_vals); i++)
        printf("%s: %s\n", time_vals[i].label,
               av_ts_make_time_string2(buf, time_vals[i].ts, time_vals[i].tb));

    /* av_ts_make_time_string (AVRational pointer version) */
    printf("\nTesting av_ts_make_time_string()\n");
    {
        AVRational tb = {1, 1000};
        printf("5000 at 1/1000: %s\n",
               av_ts_make_time_string(buf, 5000, &tb));
        printf("NOPTS: %s\n",
               av_ts_make_time_string(buf, AV_NOPTS_VALUE, &tb));
    }

    return 0;
}
