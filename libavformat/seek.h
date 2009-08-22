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

#ifndef AVFORMAT_SEEK_H
#define AVFORMAT_SEEK_H

#include "avformat.h"

/// Opaque structure for parser state.
typedef struct AVParserState AVParserState;

/**
 * Search for sync point of all active streams.
 *
 * This is not supposed to be called directly by a user application,
 * but by demuxers.
 *
 * A sync point is a point in stream, so that decoding from this point,
 * output of decoders of all streams synchronizes closest to given timestamp
 * ts (but taking timestamp limits into account, i.e., no sooner than ts_min
 * and no later than ts_max).
 *
 * @param stream_index stream index for time base reference of timestamps.
 * @param pos approximate position where to start searching for key frames.
 * @param min_ts minimum allowed timestamp (position, if AVSEEK_FLAG_BYTE set).
 * @param ts target timestamp (or position, if AVSEEK_FLAG_BYTE set in flags).
 * @param max_ts maximum allowed timestamp (position, if AVSEEK_FLAG_BYTE set).
 * @param flags if AVSEEK_FLAG_ANY is set, seek to any frame, otherwise only
 *              to a keyframe. If AVSEEK_FLAG_BYTE is set, search by
 *              position, not by timestamp.
 * @return < 0 if no such sync point could be found, otherwise stream position
 *              (stream is repositioned to this position).
 */
int64_t ff_gen_syncpoint_search(AVFormatContext *s,
                                int stream_index,
                                int64_t pos,
                                int64_t min_ts,
                                int64_t ts,
                                int64_t max_ts,
                                int flags);

/**
 * Store current parser state and file position.
 *
 * This function can be used by demuxers before destructive seeking algorithm
 * to store parser state. After the seek, depending on outcome, original state
 * can be restored or new state kept and original state freed.
 *
 * @note As a side effect, original parser state is reset, since structures
 *       are relinked to stored state instead of being deeply-copied (for
 *       performance reasons and to keep code simple).
 *
 * @param s context from which to save state.
 * @return parser state object or NULL if memory could not be allocated.
 */
AVParserState *ff_store_parser_state(AVFormatContext *s);

/**
 * Restore previously saved parser state and file position.
 *
 * Saved state will be invalidated and freed by this call, since internal
 * structures will be relinked back to stored state instead of being
 * deeply-copied.
 *
 * @param s context to which to restore state (same as used for storing state).
 * @param state state to restore.
 */
void ff_restore_parser_state(AVFormatContext *s, AVParserState *state);

/**
 * Free previously saved parser state.
 *
 * @param s context to which the state belongs (same as used for storing state).
 * @param state state to free.
 */
void ff_free_parser_state(AVFormatContext *s, AVParserState *state);

#endif /* AVFORMAT_SEEK_H */
