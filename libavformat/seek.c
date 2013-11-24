/*
 * seek utility functions for use within format handlers
 *
 * Copyright (c) 2009 Ivan Schreter
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

#include "seek.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem.h"
#include "internal.h"

// NOTE: implementation should be moved here in another patch, to keep patches
// separated.

/**
 * helper structure describing keyframe search state of one stream
 */
typedef struct {
    int64_t     pos_lo;      ///< position of the frame with low timestamp in file or INT64_MAX if not found (yet)
    int64_t     ts_lo;       ///< frame presentation timestamp or same as pos_lo for byte seeking

    int64_t     pos_hi;      ///< position of the frame with high timestamp in file or INT64_MAX if not found (yet)
    int64_t     ts_hi;       ///< frame presentation timestamp or same as pos_hi for byte seeking

    int64_t     last_pos;    ///< last known position of a frame, for multi-frame packets

    int64_t     term_ts;     ///< termination timestamp (which TS we already read)
    AVRational  term_ts_tb;  ///< timebase for term_ts
    int64_t     first_ts;    ///< first packet timestamp in this iteration (to fill term_ts later)
    AVRational  first_ts_tb; ///< timebase for first_ts

    int         terminated;  ///< termination flag for the current iteration
} AVSyncPoint;

/**
 * Compute a distance between timestamps.
 *
 * Distances are only comparable, if same time bases are used for computing
 * distances.
 *
 * @param ts_hi high timestamp
 * @param tb_hi high timestamp time base
 * @param ts_lo low timestamp
 * @param tb_lo low timestamp time base
 * @return representation of distance between high and low timestamps
 */
static int64_t ts_distance(int64_t ts_hi,
                           AVRational tb_hi,
                           int64_t ts_lo,
                           AVRational tb_lo)
{
    int64_t hi, lo;

    hi = ts_hi * tb_hi.num * tb_lo.den;
    lo = ts_lo * tb_lo.num * tb_hi.den;

    return hi - lo;
}

/**
 * Partial search for keyframes in multiple streams.
 *
 * This routine searches in each stream for the next lower and the next higher
 * timestamp compared to the given target timestamp. The search starts at the current
 * file position and ends at the file position, where all streams have already been
 * examined (or when all higher key frames are found in the first iteration).
 *
 * This routine is called iteratively with an exponential backoff to find the lower
 * timestamp.
 *
 * @param s                 format context
 * @param timestamp         target timestamp (or position, if AVSEEK_FLAG_BYTE)
 * @param timebase          time base for timestamps
 * @param flags             seeking flags
 * @param sync              array with information per stream
 * @param keyframes_to_find count of keyframes to find in total
 * @param found_lo          ptr to the count of already found low timestamp keyframes
 * @param found_hi          ptr to the count of already found high timestamp keyframes
 * @param first_iter        flag for first iteration
 */
static void search_hi_lo_keyframes(AVFormatContext *s,
                                   int64_t timestamp,
                                   AVRational timebase,
                                   int flags,
                                   AVSyncPoint *sync,
                                   int keyframes_to_find,
                                   int *found_lo,
                                   int *found_hi,
                                   int first_iter)
{
    AVPacket pkt;
    AVSyncPoint *sp;
    AVStream *st;
    int idx;
    int flg;
    int terminated_count = 0;
    int64_t pos;
    int64_t pts, dts;   // PTS/DTS from stream
    int64_t ts;         // PTS in stream-local time base or position for byte seeking
    AVRational ts_tb;   // Time base of the stream or 1:1 for byte seeking

    for (;;) {
        if (av_read_frame(s, &pkt) < 0) {
            // EOF or error, make sure high flags are set
            for (idx = 0; idx < s->nb_streams; ++idx) {
                if (s->streams[idx]->discard < AVDISCARD_ALL) {
                    sp = &sync[idx];
                    if (sp->pos_hi == INT64_MAX) {
                        // no high frame exists for this stream
                        (*found_hi)++;
                        sp->ts_hi  = INT64_MAX;
                        sp->pos_hi = INT64_MAX - 1;
                    }
                }
            }
            break;
        }

        idx = pkt.stream_index;
        st = s->streams[idx];
        if (st->discard >= AVDISCARD_ALL)
            // this stream is not active, skip packet
            continue;

        sp = &sync[idx];

        flg = pkt.flags;
        pos = pkt.pos;
        pts = pkt.pts;
        dts = pkt.dts;
        if (pts == AV_NOPTS_VALUE)
            // some formats don't provide PTS, only DTS
            pts = dts;

        av_free_packet(&pkt);

        // Multi-frame packets only return position for the very first frame.
        // Other frames are read with position == -1. Therefore, we note down
        // last known position of a frame and use it if a frame without
        // position arrives. In this way, it's possible to seek to proper
        // position. Additionally, for parsers not providing position at all,
        // an approximation will be used (starting position of this iteration).
        if (pos < 0)
            pos = sp->last_pos;
        else
            sp->last_pos = pos;

        // Evaluate key frames with known TS (or any frames, if AVSEEK_FLAG_ANY set).
        if (pts != AV_NOPTS_VALUE &&
            ((flg & AV_PKT_FLAG_KEY) || (flags & AVSEEK_FLAG_ANY))) {
            if (flags & AVSEEK_FLAG_BYTE) {
                // for byte seeking, use position as timestamp
                ts        = pos;
                ts_tb.num = 1;
                ts_tb.den = 1;
            } else {
                // otherwise, get stream time_base
                ts    = pts;
                ts_tb = st->time_base;
            }

            if (sp->first_ts == AV_NOPTS_VALUE) {
                // Note down termination timestamp for the next iteration - when
                // we encounter a packet with the same timestamp, we will ignore
                // any further packets for this stream in next iteration (as they
                // are already evaluated).
                sp->first_ts    = ts;
                sp->first_ts_tb = ts_tb;
            }

            if (sp->term_ts != AV_NOPTS_VALUE &&
                av_compare_ts(ts, ts_tb, sp->term_ts, sp->term_ts_tb) > 0) {
                // past the end position from last iteration, ignore packet
                if (!sp->terminated) {
                    sp->terminated = 1;
                    ++terminated_count;
                    if (sp->pos_hi == INT64_MAX) {
                        // no high frame exists for this stream
                        (*found_hi)++;
                        sp->ts_hi  = INT64_MAX;
                        sp->pos_hi = INT64_MAX - 1;
                    }
                    if (terminated_count == keyframes_to_find)
                        break;  // all terminated, iteration done
                }
                continue;
            }

            if (av_compare_ts(ts, ts_tb, timestamp, timebase) <= 0) {
                // keyframe found before target timestamp
                if (sp->pos_lo == INT64_MAX) {
                    // found first keyframe lower than target timestamp
                    (*found_lo)++;
                    sp->ts_lo  = ts;
                    sp->pos_lo = pos;
                } else if (sp->ts_lo < ts) {
                    // found a better match (closer to target timestamp)
                    sp->ts_lo  = ts;
                    sp->pos_lo = pos;
                }
            }
            if (av_compare_ts(ts, ts_tb, timestamp, timebase) >= 0) {
                // keyframe found after target timestamp
                if (sp->pos_hi == INT64_MAX) {
                    // found first keyframe higher than target timestamp
                    (*found_hi)++;
                    sp->ts_hi  = ts;
                    sp->pos_hi = pos;
                    if (*found_hi >= keyframes_to_find && first_iter) {
                        // We found high frame for all. They may get updated
                        // to TS closer to target TS in later iterations (which
                        // will stop at start position of previous iteration).
                        break;
                    }
                } else if (sp->ts_hi > ts) {
                    // found a better match (actually, shouldn't happen)
                    sp->ts_hi  = ts;
                    sp->pos_hi = pos;
                }
            }
        }
    }

    // Clean up the parser.
    ff_read_frame_flush(s);
}

int64_t ff_gen_syncpoint_search(AVFormatContext *s,
                                int stream_index,
                                int64_t pos,
                                int64_t ts_min,
                                int64_t ts,
                                int64_t ts_max,
                                int flags)
{
    AVSyncPoint *sync, *sp;
    AVStream *st;
    int i;
    int keyframes_to_find = 0;
    int64_t curpos;
    int64_t step;
    int found_lo = 0, found_hi = 0;
    int64_t min_distance, distance;
    int64_t min_pos = 0;
    int first_iter = 1;
    AVRational time_base;

    if (flags & AVSEEK_FLAG_BYTE) {
        // for byte seeking, we have exact 1:1 "timestamps" - positions
        time_base.num = 1;
        time_base.den = 1;
    } else {
        if (stream_index >= 0) {
            // we have a reference stream, which time base we use
            st = s->streams[stream_index];
            time_base = st->time_base;
        } else {
            // no reference stream, use AV_TIME_BASE as reference time base
            time_base.num = 1;
            time_base.den = AV_TIME_BASE;
        }
    }

    // Initialize syncpoint structures for each stream.
    sync = av_malloc(s->nb_streams * sizeof(AVSyncPoint));
    if (!sync)
        // cannot allocate helper structure
        return -1;

    for (i = 0; i < s->nb_streams; ++i) {
        st = s->streams[i];
        sp = &sync[i];

        sp->pos_lo     = INT64_MAX;
        sp->ts_lo      = INT64_MAX;
        sp->pos_hi     = INT64_MAX;
        sp->ts_hi      = INT64_MAX;
        sp->terminated = 0;
        sp->first_ts   = AV_NOPTS_VALUE;
        sp->term_ts    = ts_max;
        sp->term_ts_tb = time_base;
        sp->last_pos   = pos;

        st->cur_dts    = AV_NOPTS_VALUE;

        if (st->discard < AVDISCARD_ALL)
            ++keyframes_to_find;
    }

    if (!keyframes_to_find) {
        // no stream active, error
        av_free(sync);
        return -1;
    }

    // Find keyframes in all active streams with timestamp/position just before
    // and just after requested timestamp/position.
    step = s->pb->buffer_size;
    curpos = FFMAX(pos - step / 2, 0);
    for (;;) {
        avio_seek(s->pb, curpos, SEEK_SET);
        search_hi_lo_keyframes(s,
                               ts, time_base,
                               flags,
                               sync,
                               keyframes_to_find,
                               &found_lo, &found_hi,
                               first_iter);
        if (found_lo == keyframes_to_find && found_hi == keyframes_to_find)
            break;  // have all keyframes we wanted
        if (!curpos)
            break;  // cannot go back anymore

        curpos = pos - step;
        if (curpos < 0)
            curpos = 0;
        step *= 2;

        // switch termination positions
        for (i = 0; i < s->nb_streams; ++i) {
            st = s->streams[i];
            st->cur_dts = AV_NOPTS_VALUE;

            sp = &sync[i];
            if (sp->first_ts != AV_NOPTS_VALUE) {
                sp->term_ts    = sp->first_ts;
                sp->term_ts_tb = sp->first_ts_tb;
                sp->first_ts   = AV_NOPTS_VALUE;
            }
            sp->terminated = 0;
            sp->last_pos = curpos;
        }
        first_iter = 0;
    }

    // Find actual position to start decoding so that decoder synchronizes
    // closest to ts and between ts_min and ts_max.
    pos = INT64_MAX;

    for (i = 0; i < s->nb_streams; ++i) {
        st = s->streams[i];
        if (st->discard < AVDISCARD_ALL) {
            sp = &sync[i];
            min_distance = INT64_MAX;
            // Find timestamp closest to requested timestamp within min/max limits.
            if (sp->pos_lo != INT64_MAX
                && av_compare_ts(ts_min, time_base, sp->ts_lo, st->time_base) <= 0
                && av_compare_ts(sp->ts_lo, st->time_base, ts_max, time_base) <= 0) {
                // low timestamp is in range
                min_distance = ts_distance(ts, time_base, sp->ts_lo, st->time_base);
                min_pos = sp->pos_lo;
            }
            if (sp->pos_hi != INT64_MAX
                && av_compare_ts(ts_min, time_base, sp->ts_hi, st->time_base) <= 0
                && av_compare_ts(sp->ts_hi, st->time_base, ts_max, time_base) <= 0) {
                // high timestamp is in range, check distance
                distance = ts_distance(sp->ts_hi, st->time_base, ts, time_base);
                if (distance < min_distance) {
                    min_distance = distance;
                    min_pos = sp->pos_hi;
                }
            }
            if (min_distance == INT64_MAX) {
                // no timestamp is in range, cannot seek
                av_free(sync);
                return -1;
            }
            if (min_pos < pos)
                pos = min_pos;
        }
    }

    avio_seek(s->pb, pos, SEEK_SET);
    av_free(sync);
    return pos;
}

AVParserState *ff_store_parser_state(AVFormatContext *s)
{
    int i;
    AVStream *st;
    AVParserStreamState *ss;
    AVParserState *state = av_malloc(sizeof(AVParserState));
    if (!state)
        return NULL;

    state->stream_states = av_malloc(sizeof(AVParserStreamState) * s->nb_streams);
    if (!state->stream_states) {
        av_free(state);
        return NULL;
    }

    state->fpos = avio_tell(s->pb);

    // copy context structures
    state->packet_buffer                    = s->packet_buffer;
    state->parse_queue                      = s->parse_queue;
    state->raw_packet_buffer                = s->raw_packet_buffer;
    state->raw_packet_buffer_remaining_size = s->raw_packet_buffer_remaining_size;

    s->packet_buffer                        = NULL;
    s->parse_queue                          = NULL;
    s->raw_packet_buffer                    = NULL;
    s->raw_packet_buffer_remaining_size     = RAW_PACKET_BUFFER_SIZE;

    // copy stream structures
    state->nb_streams = s->nb_streams;
    for (i = 0; i < s->nb_streams; i++) {
        st = s->streams[i];
        ss = &state->stream_states[i];

        ss->parser        = st->parser;
        ss->last_IP_pts   = st->last_IP_pts;
        ss->cur_dts       = st->cur_dts;
        ss->probe_packets = st->probe_packets;

        st->parser        = NULL;
        st->last_IP_pts   = AV_NOPTS_VALUE;
        st->cur_dts       = AV_NOPTS_VALUE;
        st->probe_packets = MAX_PROBE_PACKETS;
    }

    return state;
}

void ff_restore_parser_state(AVFormatContext *s, AVParserState *state)
{
    int i;
    AVStream *st;
    AVParserStreamState *ss;
    ff_read_frame_flush(s);

    if (!state)
        return;

    avio_seek(s->pb, state->fpos, SEEK_SET);

    // copy context structures
    s->packet_buffer                    = state->packet_buffer;
    s->parse_queue                      = state->parse_queue;
    s->raw_packet_buffer                = state->raw_packet_buffer;
    s->raw_packet_buffer_remaining_size = state->raw_packet_buffer_remaining_size;

    // copy stream structures
    for (i = 0; i < state->nb_streams; i++) {
        st = s->streams[i];
        ss = &state->stream_states[i];

        st->parser        = ss->parser;
        st->last_IP_pts   = ss->last_IP_pts;
        st->cur_dts       = ss->cur_dts;
        st->probe_packets = ss->probe_packets;
    }

    av_free(state->stream_states);
    av_free(state);
}

static void free_packet_list(AVPacketList *pktl)
{
    AVPacketList *cur;
    while (pktl) {
        cur = pktl;
        pktl = cur->next;
        av_free_packet(&cur->pkt);
        av_free(cur);
    }
}

void ff_free_parser_state(AVFormatContext *s, AVParserState *state)
{
    int i;
    AVParserStreamState *ss;

    if (!state)
        return;

    for (i = 0; i < state->nb_streams; i++) {
        ss = &state->stream_states[i];
        if (ss->parser)
            av_parser_close(ss->parser);
    }

    free_packet_list(state->packet_buffer);
    free_packet_list(state->parse_queue);
    free_packet_list(state->raw_packet_buffer);

    av_free(state->stream_states);
    av_free(state);
}
