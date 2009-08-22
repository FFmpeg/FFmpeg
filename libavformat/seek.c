/*
 * Utility functions for seeking for use within FFmpeg format handlers.
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

#include "seek.h"
#include "libavutil/mem.h"

// NOTE: implementation should be moved here in another patch, to keep patches
// separated.
extern void av_read_frame_flush(AVFormatContext *s);

/**
 * Helper structure to store parser state of AVStream.
 */
typedef struct AVStreamState {
    // Saved members of AVStream
    AVCodecParserContext   *parser;
    AVPacket                cur_pkt;
    int64_t                 last_IP_pts;
    int64_t                 cur_dts;
    int64_t                 reference_dts;
    const uint8_t          *cur_ptr;
    int                     cur_len;
    int                     probe_packets;
} AVStreamState;

/**
 * Helper structure to store parser state of AVFormat.
 */
struct AVParserState {
    int64_t         fpos;                   ///< File position at the time of call.

    // Saved members of AVFormatContext
    AVStream       *cur_st;                 ///< Current stream.
    AVPacketList   *packet_buffer;          ///< Packet buffer of original state.
    AVPacketList   *raw_packet_buffer;      ///< Raw packet buffer of original state.
    int raw_packet_buffer_remaining_size;   ///< Remaining size available for raw_packet_buffer.

    // Saved info for streams.
    int             nb_streams;             ///< Number of streams with stored state.
    AVStreamState  *stream_states;          ///< States of individual streams (array).
};

/**
 * Helper structure describing keyframe search state of one stream.
 */
typedef struct {
    int64_t pos_lo;     ///< Position of the frame with low timestamp in file or INT64_MAX if not found (yet).
    int64_t ts_lo;      ///< Frame presentation timestamp or same as pos_lo for byte seeking.

    int64_t pos_hi;     ///< Position of the frame with high timestamp in file or INT64_MAX if not found (yet).
    int64_t ts_hi;      ///< Frame presentation timestamp or same as pos_hi for byte seeking.

    int64_t last_pos;   ///< Last known position of a frame, for multi-frame packets.

    int64_t     term_ts;    ///< Termination timestamp (which TS we already read).
    AVRational  term_ts_tb; ///< Timebase for term_ts.
    int64_t     first_ts;   ///< First packet timestamp in this iteration (to fill term_ts later).
    AVRational  first_ts_tb;///< Timebase for first_ts.

    int         terminated; ///< Termination flag for current iteration.
} AVSyncPoint;

/**
 * Compare two timestamps exactly, taking into account their respective time bases.
 *
 * @param ts_a timestamp A.
 * @param tb_a time base for timestamp A.
 * @param ts_b timestamp B.
 * @param tb_b time base for timestamp A.
 * @return -1. 0 or 1 if timestamp A is less than, equal or greater than timestamp B.
 */
static int compare_ts(int64_t ts_a, AVRational tb_a, int64_t ts_b, AVRational tb_b)
{
    int64_t a, b, res;

    if (ts_a == INT64_MIN)
        return ts_a < ts_b ? -1 : 0;
    if (ts_a == INT64_MAX)
        return ts_a > ts_b ? 1 : 0;
    if (ts_b == INT64_MIN)
        return ts_a > ts_b ? 1 : 0;
    if (ts_b == INT64_MAX)
        return ts_a < ts_b ? -1 : 0;

    a = ts_a * tb_a.num * tb_b.den;
    b = ts_b * tb_b.num * tb_a.den;

    res = a - b;
    if (res == 0)
        return 0;
    else
        return (res >> 63) | 1;
}

/**
 * Compute a distance between timestamps.
 *
 * Distances are only comparable, if same time bases are used for computing
 * distances.
 *
 * @param ts_hi high timestamp.
 * @param tb_hi high timestamp time base.
 * @param ts_lo low timestamp.
 * @param tb_lo low timestamp time base.
 * @return representation of distance between high and low timestamps.
 */
static int64_t ts_distance(int64_t ts_hi, AVRational tb_hi, int64_t ts_lo, AVRational tb_lo)
{
    int64_t hi, lo;

    hi = ts_hi * tb_hi.num * tb_lo.den;
    lo = ts_lo * tb_lo.num * tb_hi.den;

    return hi - lo;
}

/**
 * Partial search for keyframes in multiple streams.
 *
 * This routine searches for the next lower and next higher timestamp to
 * given target timestamp in each stream, starting at current file position
 * and ending at position, where all streams have already been examined
 * (or when all higher key frames found in first iteration).
 *
 * This routine is called iteratively with exponential backoff to find lower
 * timestamp.
 *
 * @param s                 format context.
 * @param timestamp         target timestamp (or position, if AVSEEK_FLAG_BYTE).
 * @param timebase          time base for timestamps.
 * @param flags             seeking flags.
 * @param sync              array with information per stream.
 * @param keyframes_to_find count of keyframes to find in total.
 * @param found_lo          pointer to count of already found low timestamp keyframes.
 * @param found_hi          pointer to count of already found high timestamp keyframes.
 * @param first_iter        flag for first iteration.
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
                        // No high frame exists for this stream
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
        if (st->discard >= AVDISCARD_ALL) {
            // This stream is not active, skip packet.
            continue;
        }
        sp = &sync[idx];

        flg = pkt.flags;
        pos = pkt.pos;
        pts = pkt.pts;
        dts = pkt.dts;
        if (pts == AV_NOPTS_VALUE) {
            // Some formats don't provide PTS, only DTS.
            pts = dts;
        }
        av_free_packet(&pkt);

        // Multi-frame packets only return position for the very first frame.
        // Other frames are read with position == -1. Therefore, we note down
        // last known position of a frame and use it if a frame without
        // position arrives. In this way, it's possible to seek to proper
        // position. Additionally, for parsers not providing position at all,
        // an approximation will be used (starting position of this iteration).
        if (pos < 0) {
            pos = sp->last_pos;
        } else {
            sp->last_pos = pos;
        }

        // Evaluate key frames with known TS (or any frames, if AVSEEK_FLAG_ANY set).
        if (pts != AV_NOPTS_VALUE && ((flg & PKT_FLAG_KEY) || (flags & AVSEEK_FLAG_ANY))) {
            if (flags & AVSEEK_FLAG_BYTE) {
                // For byte seeking, use position as timestamp.
                ts        = pos;
                ts_tb.num = 1;
                ts_tb.den = 1;
            } else {
                // Get stream time_base.
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

            if (sp->term_ts != AV_NOPTS_VALUE && compare_ts(ts, ts_tb, sp->term_ts, sp->term_ts_tb) > 0) {
                // We are past the end position from last iteration, ignore packet.
                if (!sp->terminated) {
                    sp->terminated = 1;
                    ++terminated_count;
                    if (sp->pos_hi == INT64_MAX) {
                        // No high frame exists for this stream
                        (*found_hi)++;
                        sp->ts_hi  = INT64_MAX;
                        sp->pos_hi = INT64_MAX - 1;
                    }
                    if (terminated_count == keyframes_to_find)
                        break;  // all terminated, iteration done
                }
                continue;
            }

            if (compare_ts(ts, ts_tb, timestamp, timebase) <= 0) {
                // Keyframe found before target timestamp.
                if (sp->pos_lo == INT64_MAX) {
                    // Found first keyframe lower than target timestamp.
                    (*found_lo)++;
                    sp->ts_lo  = ts;
                    sp->pos_lo = pos;
                } else if (sp->ts_lo < ts) {
                    // Found a better match (closer to target timestamp).
                    sp->ts_lo  = ts;
                    sp->pos_lo = pos;
                }
            }
            if (compare_ts(ts, ts_tb, timestamp, timebase) >= 0) {
                // Keyframe found after target timestamp.
                if (sp->pos_hi == INT64_MAX) {
                    // Found first keyframe higher than target timestamp.
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
                    // Found a better match (actually, shouldn't happen).
                    sp->ts_hi  = ts;
                    sp->pos_hi = pos;
                }
            }
        }
    }

    // Clean up the parser.
    av_read_frame_flush(s);
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
        /* For byte seeking, we have exact 1:1 "timestamps" - positions */
        time_base.num = 1;
        time_base.den = 1;
    } else {
        if (stream_index >= 0) {
            /* We have a reference stream, which time base we use */
            st = s->streams[stream_index];
            time_base = st->time_base;
        } else {
            /* No reference stream, use AV_TIME_BASE as reference time base */
            time_base.num = 1;
            time_base.den = AV_TIME_BASE;
        }
    }

    // Initialize syncpoint structures for each stream.
    sync = (AVSyncPoint*) av_malloc(s->nb_streams * sizeof(AVSyncPoint));
    if (!sync) {
        // cannot allocate helper structure
        return -1;
    }
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

    if (keyframes_to_find == 0) {
        // No stream active, error.
        av_free(sync);
        return -1;
    }

    // Find keyframes in all active streams with timestamp/position just before
    // and just after requested timestamp/position.
    step = 1024;
    curpos = pos;
    for (;;) {
        url_fseek(s->pb, curpos, SEEK_SET);
        search_hi_lo_keyframes(s,
                               ts, time_base,
                               flags,
                               sync,
                               keyframes_to_find,
                               &found_lo, &found_hi,
                               first_iter);
        if (found_lo == keyframes_to_find && found_hi == keyframes_to_find)
            break;  // have all keyframes we wanted
        if (curpos == 0)
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
                && compare_ts(ts_min, time_base, sp->ts_lo, st->time_base) <= 0
                && compare_ts(sp->ts_lo, st->time_base, ts_max, time_base) <= 0) {
                // low timestamp is in range
                min_distance = ts_distance(ts, time_base, sp->ts_lo, st->time_base);
                min_pos = sp->pos_lo;
            }
            if (sp->pos_hi != INT64_MAX
                && compare_ts(ts_min, time_base, sp->ts_hi, st->time_base) <= 0
                && compare_ts(sp->ts_hi, st->time_base, ts_max, time_base) <= 0) {
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

    url_fseek(s->pb, pos, SEEK_SET);
    av_free(sync);
    return pos;
}

AVParserState *ff_store_parser_state(AVFormatContext *s)
{
    int i;
    AVStream *st;
    AVStreamState *ss;
    AVParserState *state = (AVParserState*) av_malloc(sizeof(AVParserState));
    if (!state)
        return NULL;

    state->stream_states = (AVStreamState*) av_malloc(sizeof(AVStreamState) * s->nb_streams);
    if (!state->stream_states) {
        av_free(state);
        return NULL;
    }

    state->fpos = url_ftell(s->pb);

    // copy context structures
    state->cur_st                           = s->cur_st;
    state->packet_buffer                    = s->packet_buffer;
    state->raw_packet_buffer                = s->raw_packet_buffer;
    state->raw_packet_buffer_remaining_size = s->raw_packet_buffer_remaining_size;

    s->cur_st                               = NULL;
    s->packet_buffer                        = NULL;
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
        ss->reference_dts = st->reference_dts;
        ss->cur_ptr       = st->cur_ptr;
        ss->cur_len       = st->cur_len;
        ss->probe_packets = st->probe_packets;
        ss->cur_pkt       = st->cur_pkt;

        st->parser        = NULL;
        st->last_IP_pts   = AV_NOPTS_VALUE;
        st->cur_dts       = AV_NOPTS_VALUE;
        st->reference_dts = AV_NOPTS_VALUE;
        st->cur_ptr       = NULL;
        st->cur_len       = 0;
        st->probe_packets = MAX_PROBE_PACKETS;
        av_init_packet(&st->cur_pkt);
    }

    return state;
}

void ff_restore_parser_state(AVFormatContext *s, AVParserState *state)
{
    int i;
    AVStream *st;
    AVStreamState *ss;
    av_read_frame_flush(s);

    if (!state)
        return;

    url_fseek(s->pb, state->fpos, SEEK_SET);

    // copy context structures
    s->cur_st                           = state->cur_st;
    s->packet_buffer                    = state->packet_buffer;
    s->raw_packet_buffer                = state->raw_packet_buffer;
    s->raw_packet_buffer_remaining_size = state->raw_packet_buffer_remaining_size;

    // copy stream structures
    for (i = 0; i < state->nb_streams; i++) {
        st = s->streams[i];
        ss = &state->stream_states[i];

        st->parser        = ss->parser;
        st->last_IP_pts   = ss->last_IP_pts;
        st->cur_dts       = ss->cur_dts;
        st->reference_dts = ss->reference_dts;
        st->cur_ptr       = ss->cur_ptr;
        st->cur_len       = ss->cur_len;
        st->probe_packets = ss->probe_packets;
        st->cur_pkt       = ss->cur_pkt;
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
    AVStreamState *ss;

    if (!state)
        return;

    for (i = 0; i < state->nb_streams; i++) {
        ss = &state->stream_states[i];
        if (ss->parser)
            av_parser_close(ss->parser);
        av_free_packet(&ss->cur_pkt);
    }

    free_packet_list(state->packet_buffer);
    free_packet_list(state->raw_packet_buffer);

    av_free(state->stream_states);
    av_free(state);
}

