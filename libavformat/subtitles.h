/*
 * Copyright (c) 2012 Clément Bœsch
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

#ifndef AVFORMAT_SUBTITLES_H
#define AVFORMAT_SUBTITLES_H

#include <stdint.h>
#include "avformat.h"

typedef struct {
    AVPacket *subs;         ///< array of subtitles packets
    int nb_subs;            ///< number of subtitles packets
    int allocated_size;     ///< allocated size for subs
    int current_sub_idx;    ///< current position for the read packet callback
} FFDemuxSubtitlesQueue;

/**
 * Insert a new subtitle event.
 *
 * @param event the subtitle line, may not be zero terminated
 * @param len   the length of the event (in strlen() sense, so without '\0')
 * @param merge set to 1 if the current event should be concatenated with the
 *              previous one instead of adding a new entry, 0 otherwise
 */
AVPacket *ff_subtitles_queue_insert(FFDemuxSubtitlesQueue *q,
                                    const uint8_t *event, int len, int merge);

/**
 * Set missing durations and sort subtitles by PTS, and then byte position.
 */
void ff_subtitles_queue_finalize(FFDemuxSubtitlesQueue *q);

/**
 * Generic read_packet() callback for subtitles demuxers using this queue
 * system.
 */
int ff_subtitles_queue_read_packet(FFDemuxSubtitlesQueue *q, AVPacket *pkt);

/**
 * Remove and destroy all the subtitles packets.
 */
void ff_subtitles_queue_clean(FFDemuxSubtitlesQueue *q);

#endif /* AVFORMAT_SUBTITLES_H */
