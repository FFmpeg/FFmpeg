/*
 * Utilities for SMPTE ST 2110 decoding
 * Copyright (c) 2018 Savoir-faire Linux, Inc
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

/* Development sponsored by CBC/Radio-Canada */

#include "common.h"
#include "time.h"

#include "libavformat/avformat.h"

#include "smpte2110.h"

struct smpte2110_timestamp {
    int64_t last_sync;
    int64_t previous_timestamp;
};


#define USEC_IN_SEC 1000000LL
static int64_t time_to_timebase(int64_t time, AVRational timebase)
{
    return (time / USEC_IN_SEC) * timebase.den / timebase.num +
           (time % USEC_IN_SEC) * (timebase.den / timebase.num) / USEC_IN_SEC;
}
#undef USEC_IN_SEC

#define RTP_TIMESTAMP_WRAP (1LL << 32)

struct smpte2110_timestamp* smpte2110_alloc(void)
{
    return av_mallocz(sizeof(struct smpte2110_timestamp));
}

static int __smpte2110_compute_pts(void *avlc, int64_t *last_sync,
                                   int64_t *computed_pts,
                                   uint32_t previous_timestamp,
                                   uint32_t current_timestamp,
                                   AVRational time_base)
{
    int64_t last_sync_point = *last_sync;
    int64_t pts;

    /* if we failed to compute the base time, there is no need to keep trying */
    if (last_sync_point == AV_NOPTS_VALUE)
        return AVERROR(EINVAL);

    if (!last_sync_point) {
        int64_t current_time =  av_gettime();
        int64_t now = time_to_timebase(current_time, time_base);
        int64_t wrap_detect;

        last_sync_point = (now / RTP_TIMESTAMP_WRAP) * RTP_TIMESTAMP_WRAP;

        pts = last_sync_point + current_timestamp;

        /*
         *      last
         *      sync   now    wrap   timestamp
         * |-----|------|------|--------|------------> time
         *
         * Last sync point is derived from the current time, but the timestamp we
         * get might be just after a wrap, so its value would be very low and
         * last_sync + timestamp would be way before "now". Let's try to detect
         * that and move the last sync point to the next occurrence.
         *
         * The opposite situation, where timestamp < wrap < now, is also
         * possible. In that case, move the last sync point back.
         */

        wrap_detect = av_rescale(600, time_base.den, time_base.num);
        if (pts > now && (pts - now) > wrap_detect) {
            last_sync_point -= RTP_TIMESTAMP_WRAP;
        } else if (now > pts && (now - pts) > wrap_detect) {
            last_sync_point += RTP_TIMESTAMP_WRAP;
        }

        pts = last_sync_point + current_timestamp;

        /*
         * Check that after a potential wrap was taken into account, we computed
         * a pts value that is "close enough" of the current time. If it is
         * still too far, give up and let common code timestamp the frames with
         * another method.
         */
        if (FFABS(now - pts) > wrap_detect) {
            av_log(avlc, AV_LOG_WARNING, "Unable to determine base time\n");
            *last_sync = AV_NOPTS_VALUE;
            return AVERROR(EINVAL);
        } else {
            av_log(avlc, AV_LOG_DEBUG, "now:           %" PRId64 "\n", now);
            av_log(avlc, AV_LOG_DEBUG, "last_sync:     %" PRId64 "\n", last_sync_point);
            av_log(avlc, AV_LOG_DEBUG, "RTP timestamp: %" PRId32 "\n", current_timestamp);
            av_log(avlc, AV_LOG_DEBUG, "wrap in:       %" PRId64 "s\n",
                   ((int64_t)RTP_TIMESTAMP_WRAP - current_timestamp) / time_base.den);
            av_log(avlc, AV_LOG_DEBUG, "pts:           %" PRId64 "\n", pts);
            av_log(avlc, AV_LOG_DEBUG, "(now - pts) / %dk: %" PRId64 "\n",
                   time_base.den / 1000,
                   (now - pts) / time_base.den);
        }

    } else {
        if (current_timestamp < previous_timestamp) {
            last_sync_point += RTP_TIMESTAMP_WRAP;
            av_log(avlc, AV_LOG_DEBUG, "PTS WRAP\n");
        }

        pts = last_sync_point + current_timestamp;
    }

    *last_sync = last_sync_point;
    *computed_pts = pts;

    return 0;
}

int64_t smpte2110_compute_pts(void *avlc, struct smpte2110_timestamp *ts,
                              uint32_t current_timestamp, AVRational time_base)
{
    int64_t pts;
    int ret;

    ret = __smpte2110_compute_pts(avlc, &ts->last_sync, &pts,
                                  ts->previous_timestamp, current_timestamp,
                                  time_base);
    if (ret < 0)
        pts = AV_NOPTS_VALUE;

    ts->previous_timestamp = current_timestamp;

    return pts;
}
