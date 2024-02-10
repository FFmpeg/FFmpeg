/*
 * Seeking and index-related functions
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
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

#include <stdint.h>

#include "libavutil/avassert.h"
#include "libavutil/mathematics.h"
#include "libavutil/timestamp.h"

#include "libavcodec/avcodec.h"

#include "avformat.h"
#include "avio_internal.h"
#include "demux.h"
#include "internal.h"

void avpriv_update_cur_dts(AVFormatContext *s, AVStream *ref_st, int64_t timestamp)
{
    for (unsigned i = 0; i < s->nb_streams; i++) {
        AVStream *const st  = s->streams[i];
        FFStream *const sti = ffstream(st);

        sti->cur_dts =
            av_rescale(timestamp,
                       st->time_base.den * (int64_t) ref_st->time_base.num,
                       st->time_base.num * (int64_t) ref_st->time_base.den);
    }
}

void ff_reduce_index(AVFormatContext *s, int stream_index)
{
    AVStream *const st  = s->streams[stream_index];
    FFStream *const sti = ffstream(st);
    unsigned int max_entries = s->max_index_size / sizeof(AVIndexEntry);

    if ((unsigned) sti->nb_index_entries >= max_entries) {
        int i;
        for (i = 0; 2 * i < sti->nb_index_entries; i++)
            sti->index_entries[i] = sti->index_entries[2 * i];
        sti->nb_index_entries = i;
    }
}

int ff_add_index_entry(AVIndexEntry **index_entries,
                       int *nb_index_entries,
                       unsigned int *index_entries_allocated_size,
                       int64_t pos, int64_t timestamp,
                       int size, int distance, int flags)
{
    AVIndexEntry *entries, *ie;
    int index;

    if ((unsigned) *nb_index_entries + 1 >= UINT_MAX / sizeof(AVIndexEntry))
        return -1;

    if (timestamp == AV_NOPTS_VALUE)
        return AVERROR(EINVAL);

    if (size < 0 || size > 0x3FFFFFFF)
        return AVERROR(EINVAL);

    if (is_relative(timestamp)) //FIXME this maintains previous behavior but we should shift by the correct offset once known
        timestamp -= RELATIVE_TS_BASE;

    entries = av_fast_realloc(*index_entries,
                              index_entries_allocated_size,
                              (*nb_index_entries + 1) *
                              sizeof(AVIndexEntry));
    if (!entries)
        return -1;

    *index_entries = entries;

    index = ff_index_search_timestamp(*index_entries, *nb_index_entries,
                                      timestamp, AVSEEK_FLAG_ANY);
    if (index < 0) {
        index = (*nb_index_entries)++;
        ie    = &entries[index];
        av_assert0(index == 0 || ie[-1].timestamp < timestamp);
    } else {
        ie = &entries[index];
        if (ie->timestamp != timestamp) {
            if (ie->timestamp <= timestamp)
                return -1;
            memmove(entries + index + 1, entries + index,
                    sizeof(AVIndexEntry) * (*nb_index_entries - index));
            (*nb_index_entries)++;
        } else if (ie->pos == pos && distance < ie->min_distance)
            // do not reduce the distance
            distance = ie->min_distance;
    }

    ie->pos          = pos;
    ie->timestamp    = timestamp;
    ie->min_distance = distance;
    ie->size         = size;
    ie->flags        = flags;

    return index;
}

int av_add_index_entry(AVStream *st, int64_t pos, int64_t timestamp,
                       int size, int distance, int flags)
{
    FFStream *const sti = ffstream(st);
    timestamp = ff_wrap_timestamp(st, timestamp);
    return ff_add_index_entry(&sti->index_entries, &sti->nb_index_entries,
                              &sti->index_entries_allocated_size, pos,
                              timestamp, size, distance, flags);
}

int ff_index_search_timestamp(const AVIndexEntry *entries, int nb_entries,
                              int64_t wanted_timestamp, int flags)
{
    int a, b, m;
    int64_t timestamp;

    a = -1;
    b = nb_entries;

    // Optimize appending index entries at the end.
    if (b && entries[b - 1].timestamp < wanted_timestamp)
        a = b - 1;

    while (b - a > 1) {
        m         = (a + b) >> 1;

        // Search for the next non-discarded packet.
        while ((entries[m].flags & AVINDEX_DISCARD_FRAME) && m < b && m < nb_entries - 1) {
            m++;
            if (m == b && entries[m].timestamp >= wanted_timestamp) {
                m = b - 1;
                break;
            }
        }

        timestamp = entries[m].timestamp;
        if (timestamp >= wanted_timestamp)
            b = m;
        if (timestamp <= wanted_timestamp)
            a = m;
    }
    m = (flags & AVSEEK_FLAG_BACKWARD) ? a : b;

    if (!(flags & AVSEEK_FLAG_ANY))
        while (m >= 0 && m < nb_entries &&
               !(entries[m].flags & AVINDEX_KEYFRAME))
            m += (flags & AVSEEK_FLAG_BACKWARD) ? -1 : 1;

    if (m == nb_entries)
        return -1;
    return m;
}

void ff_configure_buffers_for_index(AVFormatContext *s, int64_t time_tolerance)
{
    int64_t pos_delta = 0;
    int64_t skip = 0;
    //We could use URLProtocol flags here but as many user applications do not use URLProtocols this would be unreliable
    const char *proto = avio_find_protocol_name(s->url);
    FFIOContext *ctx;

    av_assert0(time_tolerance >= 0);

    if (!proto) {
        av_log(s, AV_LOG_INFO,
               "Protocol name not provided, cannot determine if input is local or "
               "a network protocol, buffers and access patterns cannot be configured "
               "optimally without knowing the protocol\n");
    }

    if (proto && !(strcmp(proto, "file") && strcmp(proto, "pipe") && strcmp(proto, "cache")))
        return;

    for (unsigned ist1 = 0; ist1 < s->nb_streams; ist1++) {
        AVStream *const st1  = s->streams[ist1];
        FFStream *const sti1 = ffstream(st1);
        for (unsigned ist2 = 0; ist2 < s->nb_streams; ist2++) {
            AVStream *const st2  = s->streams[ist2];
            FFStream *const sti2 = ffstream(st2);

            if (ist1 == ist2)
                continue;

            for (int i1 = 0, i2 = 0; i1 < sti1->nb_index_entries; i1++) {
                const AVIndexEntry *const e1 = &sti1->index_entries[i1];
                int64_t e1_pts = av_rescale_q(e1->timestamp, st1->time_base, AV_TIME_BASE_Q);

                if (e1->size < (1 << 23))
                    skip = FFMAX(skip, e1->size);

                for (; i2 < sti2->nb_index_entries; i2++) {
                    const AVIndexEntry *const e2 = &sti2->index_entries[i2];
                    int64_t e2_pts = av_rescale_q(e2->timestamp, st2->time_base, AV_TIME_BASE_Q);
                    int64_t cur_delta;
                    if (e2_pts < e1_pts || e2_pts - (uint64_t)e1_pts < time_tolerance)
                        continue;
                    cur_delta = FFABS(e1->pos - e2->pos);
                    if (cur_delta < (1 << 23))
                        pos_delta = FFMAX(pos_delta, cur_delta);
                    break;
                }
            }
        }
    }

    pos_delta *= 2;
    ctx = ffiocontext(s->pb);
    /* XXX This could be adjusted depending on protocol*/
    if (s->pb->buffer_size < pos_delta) {
        av_log(s, AV_LOG_VERBOSE, "Reconfiguring buffers to size %"PRId64"\n", pos_delta);

        /* realloc the buffer and the original data will be retained */
        if (ffio_realloc_buf(s->pb, pos_delta)) {
            av_log(s, AV_LOG_ERROR, "Realloc buffer fail.\n");
            return;
        }

        ctx->short_seek_threshold = FFMAX(ctx->short_seek_threshold, pos_delta/2);
    }

    ctx->short_seek_threshold = FFMAX(ctx->short_seek_threshold, skip);
}

int av_index_search_timestamp(AVStream *st, int64_t wanted_timestamp, int flags)
{
    const FFStream *const sti = ffstream(st);
    return ff_index_search_timestamp(sti->index_entries, sti->nb_index_entries,
                                     wanted_timestamp, flags);
}

int avformat_index_get_entries_count(const AVStream *st)
{
    return cffstream(st)->nb_index_entries;
}

const AVIndexEntry *avformat_index_get_entry(AVStream *st, int idx)
{
    const FFStream *const sti = ffstream(st);
    if (idx < 0 || idx >= sti->nb_index_entries)
        return NULL;

    return &sti->index_entries[idx];
}

const AVIndexEntry *avformat_index_get_entry_from_timestamp(AVStream *st,
                                                            int64_t wanted_timestamp,
                                                            int flags)
{
    const FFStream *const sti = ffstream(st);
    int idx = ff_index_search_timestamp(sti->index_entries,
                                        sti->nb_index_entries,
                                        wanted_timestamp, flags);

    if (idx < 0)
        return NULL;

    return &sti->index_entries[idx];
}

static int64_t read_timestamp(AVFormatContext *s, int stream_index, int64_t *ppos, int64_t pos_limit,
                              int64_t (*read_timestamp)(struct AVFormatContext *, int , int64_t *, int64_t ))
{
    int64_t ts = read_timestamp(s, stream_index, ppos, pos_limit);
    if (stream_index >= 0)
        ts = ff_wrap_timestamp(s->streams[stream_index], ts);
    return ts;
}

int ff_seek_frame_binary(AVFormatContext *s, int stream_index,
                         int64_t target_ts, int flags)
{
    const FFInputFormat *const avif = ffifmt(s->iformat);
    int64_t pos_min = 0, pos_max = 0, pos, pos_limit;
    int64_t ts_min, ts_max, ts;
    int index;
    int64_t ret;
    AVStream *st;
    FFStream *sti;

    if (stream_index < 0)
        return -1;

    av_log(s, AV_LOG_TRACE, "read_seek: %d %s\n", stream_index, av_ts2str(target_ts));

    ts_max =
    ts_min = AV_NOPTS_VALUE;
    pos_limit = -1; // GCC falsely says it may be uninitialized.

    st  = s->streams[stream_index];
    sti = ffstream(st);
    if (sti->index_entries) {
        const AVIndexEntry *e;

        /* FIXME: Whole function must be checked for non-keyframe entries in
         * index case, especially read_timestamp(). */
        index = av_index_search_timestamp(st, target_ts,
                                          flags | AVSEEK_FLAG_BACKWARD);
        index = FFMAX(index, 0);
        e     = &sti->index_entries[index];

        if (e->timestamp <= target_ts || e->pos == e->min_distance) {
            pos_min = e->pos;
            ts_min  = e->timestamp;
            av_log(s, AV_LOG_TRACE, "using cached pos_min=0x%"PRIx64" dts_min=%s\n",
                    pos_min, av_ts2str(ts_min));
        } else {
            av_assert1(index == 0);
        }

        index = av_index_search_timestamp(st, target_ts,
                                          flags & ~AVSEEK_FLAG_BACKWARD);
        av_assert0(index < sti->nb_index_entries);
        if (index >= 0) {
            e = &sti->index_entries[index];
            av_assert1(e->timestamp >= target_ts);
            pos_max   = e->pos;
            ts_max    = e->timestamp;
            pos_limit = pos_max - e->min_distance;
            av_log(s, AV_LOG_TRACE, "using cached pos_max=0x%"PRIx64" pos_limit=0x%"PRIx64
                    " dts_max=%s\n", pos_max, pos_limit, av_ts2str(ts_max));
        }
    }

    pos = ff_gen_search(s, stream_index, target_ts, pos_min, pos_max, pos_limit,
                        ts_min, ts_max, flags, &ts, avif->read_timestamp);
    if (pos < 0)
        return -1;

    /* do the seek */
    if ((ret = avio_seek(s->pb, pos, SEEK_SET)) < 0)
        return ret;

    ff_read_frame_flush(s);
    avpriv_update_cur_dts(s, st, ts);

    return 0;
}

int ff_find_last_ts(AVFormatContext *s, int stream_index, int64_t *ts, int64_t *pos,
                    int64_t (*read_timestamp_func)(struct AVFormatContext *, int , int64_t *, int64_t ))
{
    int64_t step = 1024;
    int64_t limit, ts_max;
    int64_t filesize = avio_size(s->pb);
    int64_t pos_max  = filesize - 1;
    do {
        limit   = pos_max;
        pos_max = FFMAX(0, (pos_max) - step);
        ts_max  = read_timestamp(s, stream_index,
                                 &pos_max, limit, read_timestamp_func);
        step   += step;
    } while (ts_max == AV_NOPTS_VALUE && 2*limit > step);
    if (ts_max == AV_NOPTS_VALUE)
        return -1;

    for (;;) {
        int64_t tmp_pos = pos_max + 1;
        int64_t tmp_ts  = read_timestamp(s, stream_index,
                                         &tmp_pos, INT64_MAX, read_timestamp_func);
        if (tmp_ts == AV_NOPTS_VALUE)
            break;
        av_assert0(tmp_pos > pos_max);
        ts_max  = tmp_ts;
        pos_max = tmp_pos;
        if (tmp_pos >= filesize)
            break;
    }

    if (ts)
        *ts  = ts_max;
    if (pos)
        *pos = pos_max;

    return 0;
}

int64_t ff_gen_search(AVFormatContext *s, int stream_index, int64_t target_ts,
                      int64_t pos_min, int64_t pos_max, int64_t pos_limit,
                      int64_t ts_min, int64_t ts_max,
                      int flags, int64_t *ts_ret,
                      int64_t (*read_timestamp_func)(struct AVFormatContext *,
                                                     int, int64_t *, int64_t))
{
    FFFormatContext *const si = ffformatcontext(s);
    int64_t pos, ts;
    int64_t start_pos;
    int no_change;
    int ret;

    av_log(s, AV_LOG_TRACE, "gen_seek: %d %s\n", stream_index, av_ts2str(target_ts));

    if (ts_min == AV_NOPTS_VALUE) {
        pos_min = si->data_offset;
        ts_min  = read_timestamp(s, stream_index, &pos_min, INT64_MAX, read_timestamp_func);
        if (ts_min == AV_NOPTS_VALUE)
            return -1;
    }

    if (ts_min >= target_ts) {
        *ts_ret = ts_min;
        return pos_min;
    }

    if (ts_max == AV_NOPTS_VALUE) {
        if ((ret = ff_find_last_ts(s, stream_index, &ts_max, &pos_max, read_timestamp_func)) < 0)
            return ret;
        pos_limit = pos_max;
    }

    if (ts_max <= target_ts) {
        *ts_ret = ts_max;
        return pos_max;
    }

    av_assert0(ts_min < ts_max);

    no_change = 0;
    while (pos_min < pos_limit) {
        av_log(s, AV_LOG_TRACE,
                "pos_min=0x%"PRIx64" pos_max=0x%"PRIx64" dts_min=%s dts_max=%s\n",
                pos_min, pos_max, av_ts2str(ts_min), av_ts2str(ts_max));
        av_assert0(pos_limit <= pos_max);

        if (no_change == 0) {
            int64_t approximate_keyframe_distance = pos_max - pos_limit;
            // interpolate position (better than dichotomy)
            pos = av_rescale(target_ts - ts_min, pos_max - pos_min,
                             ts_max - ts_min) +
                  pos_min - approximate_keyframe_distance;
        } else if (no_change == 1) {
            // bisection if interpolation did not change min / max pos last time
            pos = (pos_min + pos_limit) >> 1;
        } else {
            /* linear search if bisection failed, can only happen if there
             * are very few or no keyframes between min/max */
            pos = pos_min;
        }
        if (pos <= pos_min)
            pos = pos_min + 1;
        else if (pos > pos_limit)
            pos = pos_limit;
        start_pos = pos;

        // May pass pos_limit instead of -1.
        ts = read_timestamp(s, stream_index, &pos, INT64_MAX, read_timestamp_func);
        if (pos == pos_max)
            no_change++;
        else
            no_change = 0;
        av_log(s, AV_LOG_TRACE, "%"PRId64" %"PRId64" %"PRId64" / %s %s %s"
                " target:%s limit:%"PRId64" start:%"PRId64" noc:%d\n",
                pos_min, pos, pos_max,
                av_ts2str(ts_min), av_ts2str(ts), av_ts2str(ts_max), av_ts2str(target_ts),
                pos_limit, start_pos, no_change);
        if (ts == AV_NOPTS_VALUE) {
            av_log(s, AV_LOG_ERROR, "read_timestamp() failed in the middle\n");
            return -1;
        }
        if (target_ts <= ts) {
            pos_limit = start_pos - 1;
            pos_max   = pos;
            ts_max    = ts;
        }
        if (target_ts >= ts) {
            pos_min = pos;
            ts_min  = ts;
        }
    }

    pos     = (flags & AVSEEK_FLAG_BACKWARD) ? pos_min : pos_max;
    ts      = (flags & AVSEEK_FLAG_BACKWARD) ? ts_min  : ts_max;
#if 0
    pos_min = pos;
    ts_min  = read_timestamp(s, stream_index, &pos_min, INT64_MAX, read_timestamp_func);
    pos_min++;
    ts_max  = read_timestamp(s, stream_index, &pos_min, INT64_MAX, read_timestamp_func);
    av_log(s, AV_LOG_TRACE, "pos=0x%"PRIx64" %s<=%s<=%s\n",
            pos, av_ts2str(ts_min), av_ts2str(target_ts), av_ts2str(ts_max));
#endif
    *ts_ret = ts;
    return pos;
}

static int seek_frame_byte(AVFormatContext *s, int stream_index,
                           int64_t pos, int flags)
{
    FFFormatContext *const si = ffformatcontext(s);
    int64_t pos_min, pos_max;

    pos_min = si->data_offset;
    pos_max = avio_size(s->pb) - 1;

    if (pos < pos_min)
        pos = pos_min;
    else if (pos > pos_max)
        pos = pos_max;

    avio_seek(s->pb, pos, SEEK_SET);

    s->io_repositioned = 1;

    return 0;
}

static int seek_frame_generic(AVFormatContext *s, int stream_index,
                              int64_t timestamp, int flags)
{
    FFFormatContext *const si = ffformatcontext(s);
    AVStream *const st  = s->streams[stream_index];
    FFStream *const sti = ffstream(st);
    const AVIndexEntry *ie;
    int index;
    int64_t ret;

    index = av_index_search_timestamp(st, timestamp, flags);

    if (index < 0 && sti->nb_index_entries &&
        timestamp < sti->index_entries[0].timestamp)
        return -1;

    if (index < 0 || index == sti->nb_index_entries - 1) {
        AVPacket *const pkt = si->pkt;
        int nonkey = 0;

        if (sti->nb_index_entries) {
            av_assert0(sti->index_entries);
            ie = &sti->index_entries[sti->nb_index_entries - 1];
            if ((ret = avio_seek(s->pb, ie->pos, SEEK_SET)) < 0)
                return ret;
            s->io_repositioned = 1;
            avpriv_update_cur_dts(s, st, ie->timestamp);
        } else {
            if ((ret = avio_seek(s->pb, si->data_offset, SEEK_SET)) < 0)
                return ret;
            s->io_repositioned = 1;
        }
        av_packet_unref(pkt);
        for (;;) {
            int read_status;
            do {
                read_status = av_read_frame(s, pkt);
            } while (read_status == AVERROR(EAGAIN));
            if (read_status < 0)
                break;
            if (stream_index == pkt->stream_index && pkt->dts > timestamp) {
                if (pkt->flags & AV_PKT_FLAG_KEY) {
                    av_packet_unref(pkt);
                    break;
                }
                if (nonkey++ > 1000 && st->codecpar->codec_id != AV_CODEC_ID_CDGRAPHICS) {
                    av_log(s, AV_LOG_ERROR,"seek_frame_generic failed as this stream seems to contain no keyframes after the target timestamp, %d non keyframes found\n", nonkey);
                    av_packet_unref(pkt);
                    break;
                }
            }
            av_packet_unref(pkt);
        }
        index = av_index_search_timestamp(st, timestamp, flags);
    }
    if (index < 0)
        return -1;

    ff_read_frame_flush(s);
    if (ffifmt(s->iformat)->read_seek)
        if (ffifmt(s->iformat)->read_seek(s, stream_index, timestamp, flags) >= 0)
            return 0;
    ie = &sti->index_entries[index];
    if ((ret = avio_seek(s->pb, ie->pos, SEEK_SET)) < 0)
        return ret;
    s->io_repositioned = 1;
    avpriv_update_cur_dts(s, st, ie->timestamp);

    return 0;
}

static int seek_frame_internal(AVFormatContext *s, int stream_index,
                               int64_t timestamp, int flags)
{
    AVStream *st;
    int ret;

    if (flags & AVSEEK_FLAG_BYTE) {
        if (s->iformat->flags & AVFMT_NO_BYTE_SEEK)
            return -1;
        ff_read_frame_flush(s);
        return seek_frame_byte(s, stream_index, timestamp, flags);
    }

    if (stream_index < 0) {
        stream_index = av_find_default_stream_index(s);
        if (stream_index < 0)
            return -1;

        st = s->streams[stream_index];
        /* timestamp for default must be expressed in AV_TIME_BASE units */
        timestamp = av_rescale(timestamp, st->time_base.den,
                               AV_TIME_BASE * (int64_t) st->time_base.num);
    }

    /* first, we try the format specific seek */
    if (ffifmt(s->iformat)->read_seek) {
        ff_read_frame_flush(s);
        ret = ffifmt(s->iformat)->read_seek(s, stream_index, timestamp, flags);
    } else
        ret = -1;
    if (ret >= 0)
        return 0;

    if (ffifmt(s->iformat)->read_timestamp &&
        !(s->iformat->flags & AVFMT_NOBINSEARCH)) {
        ff_read_frame_flush(s);
        return ff_seek_frame_binary(s, stream_index, timestamp, flags);
    } else if (!(s->iformat->flags & AVFMT_NOGENSEARCH)) {
        ff_read_frame_flush(s);
        return seek_frame_generic(s, stream_index, timestamp, flags);
    } else
        return -1;
}

int av_seek_frame(AVFormatContext *s, int stream_index,
                  int64_t timestamp, int flags)
{
    int ret;

    if (ffifmt(s->iformat)->read_seek2 && !ffifmt(s->iformat)->read_seek) {
        int64_t min_ts = INT64_MIN, max_ts = INT64_MAX;
        if ((flags & AVSEEK_FLAG_BACKWARD))
            max_ts = timestamp;
        else
            min_ts = timestamp;
        return avformat_seek_file(s, stream_index, min_ts, timestamp, max_ts,
                                  flags & ~AVSEEK_FLAG_BACKWARD);
    }

    ret = seek_frame_internal(s, stream_index, timestamp, flags);

    if (ret >= 0)
        ret = avformat_queue_attached_pictures(s);

    return ret;
}

int avformat_seek_file(AVFormatContext *s, int stream_index, int64_t min_ts,
                       int64_t ts, int64_t max_ts, int flags)
{
    if (min_ts > ts || max_ts < ts)
        return -1;
    if (stream_index < -1 || stream_index >= (int)s->nb_streams)
        return AVERROR(EINVAL);

    if (s->seek2any > 0)
        flags |= AVSEEK_FLAG_ANY;
    flags &= ~AVSEEK_FLAG_BACKWARD;

    if (ffifmt(s->iformat)->read_seek2) {
        int ret;
        ff_read_frame_flush(s);

        if (stream_index == -1 && s->nb_streams == 1) {
            AVRational time_base = s->streams[0]->time_base;
            ts = av_rescale_q(ts, AV_TIME_BASE_Q, time_base);
            min_ts = av_rescale_rnd(min_ts, time_base.den,
                                    time_base.num * (int64_t)AV_TIME_BASE,
                                    AV_ROUND_UP   | AV_ROUND_PASS_MINMAX);
            max_ts = av_rescale_rnd(max_ts, time_base.den,
                                    time_base.num * (int64_t)AV_TIME_BASE,
                                    AV_ROUND_DOWN | AV_ROUND_PASS_MINMAX);
            stream_index = 0;
        }

        ret = ffifmt(s->iformat)->read_seek2(s, stream_index, min_ts,
                                     ts, max_ts, flags);

        if (ret >= 0)
            ret = avformat_queue_attached_pictures(s);
        return ret;
    }

    if (ffifmt(s->iformat)->read_timestamp) {
        // try to seek via read_timestamp()
    }

    // Fall back on old API if new is not implemented but old is.
    // Note the old API has somewhat different semantics.
    if (ffifmt(s->iformat)->read_seek || 1) {
        int dir = (ts - (uint64_t)min_ts > (uint64_t)max_ts - ts ? AVSEEK_FLAG_BACKWARD : 0);
        int ret = av_seek_frame(s, stream_index, ts, flags | dir);
        if (ret < 0 && ts != min_ts && max_ts != ts) {
            ret = av_seek_frame(s, stream_index, dir ? max_ts : min_ts, flags | dir);
            if (ret >= 0)
                ret = av_seek_frame(s, stream_index, ts, flags | (dir^AVSEEK_FLAG_BACKWARD));
        }
        return ret;
    }

    // try some generic seek like seek_frame_generic() but with new ts semantics
    return -1; //unreachable
}

/** Flush the frame reader. */
void ff_read_frame_flush(AVFormatContext *s)
{
    FFFormatContext *const si = ffformatcontext(s);

    ff_flush_packet_queue(s);

    /* Reset read state for each stream. */
    for (unsigned i = 0; i < s->nb_streams; i++) {
        AVStream *const st  = s->streams[i];
        FFStream *const sti = ffstream(st);

        if (sti->parser) {
            av_parser_close(sti->parser);
            sti->parser = NULL;
        }
        sti->last_IP_pts = AV_NOPTS_VALUE;
        sti->last_dts_for_order_check = AV_NOPTS_VALUE;
        if (sti->first_dts == AV_NOPTS_VALUE)
            sti->cur_dts = RELATIVE_TS_BASE;
        else
            /* We set the current DTS to an unspecified origin. */
            sti->cur_dts = AV_NOPTS_VALUE;

        sti->probe_packets = s->max_probe_packets;

        for (int j = 0; j < MAX_REORDER_DELAY + 1; j++)
            sti->pts_buffer[j] = AV_NOPTS_VALUE;

#if FF_API_AVSTREAM_SIDE_DATA
        if (si->inject_global_side_data)
            sti->inject_global_side_data = 1;
#endif

        sti->skip_samples = 0;
    }
}

int avformat_flush(AVFormatContext *s)
{
    ff_read_frame_flush(s);
    return 0;
}

void ff_rescale_interval(AVRational tb_in, AVRational tb_out,
                         int64_t *min_ts, int64_t *ts, int64_t *max_ts)
{
    *ts     = av_rescale_q    (*    ts, tb_in, tb_out);
    *min_ts = av_rescale_q_rnd(*min_ts, tb_in, tb_out,
                               AV_ROUND_UP   | AV_ROUND_PASS_MINMAX);
    *max_ts = av_rescale_q_rnd(*max_ts, tb_in, tb_out,
                               AV_ROUND_DOWN | AV_ROUND_PASS_MINMAX);
}
