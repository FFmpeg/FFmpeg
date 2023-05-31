/*
 * Muxer internal APIs - should not be included outside of ffmpeg_mux*
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

#ifndef FFTOOLS_FFMPEG_MUX_H
#define FFTOOLS_FFMPEG_MUX_H

#include <stdatomic.h>
#include <stdint.h>

#include "thread_queue.h"

#include "libavformat/avformat.h"

#include "libavcodec/packet.h"

#include "libavutil/dict.h"
#include "libavutil/fifo.h"
#include "libavutil/thread.h"

typedef struct MuxStream {
    OutputStream ost;

    // name used for logging
    char log_name[32];

    /* the packets are buffered here until the muxer is ready to be initialized */
    AVFifo *muxing_queue;

    AVBSFContext *bsf_ctx;
    AVPacket     *bsf_pkt;

    AVPacket     *pkt;

    EncStats stats;

    int64_t max_frames;

    /*
     * The size of the AVPackets' buffers in queue.
     * Updated when a packet is either pushed or pulled from the queue.
     */
    size_t muxing_queue_data_size;

    int max_muxing_queue_size;

    /* Threshold after which max_muxing_queue_size will be in effect */
    size_t muxing_queue_data_threshold;

    // timestamp from which the streamcopied streams should start,
    // in AV_TIME_BASE_Q;
    // everything before it should be discarded
    int64_t ts_copy_start;

    /* dts of the last packet sent to the muxer, in the stream timebase
     * used for making up missing dts values */
    int64_t last_mux_dts;

    int64_t    stream_duration;
    AVRational stream_duration_tb;

    // audio streamcopy - state for av_rescale_delta()
    int64_t ts_rescale_delta_last;

    // combined size of all the packets sent to the muxer
    uint64_t data_size_mux;

    int copy_initial_nonkeyframes;
    int copy_prior_start;
    int streamcopy_started;
} MuxStream;

typedef struct Muxer {
    OutputFile of;

    // name used for logging
    char log_name[32];

    AVFormatContext *fc;

    pthread_t    thread;
    ThreadQueue *tq;

    AVDictionary *opts;

    int thread_queue_size;

    /* filesize limit expressed in bytes */
    int64_t limit_filesize;
    atomic_int_least64_t last_filesize;
    int header_written;

    SyncQueue *sq_mux;
    AVPacket *sq_pkt;
} Muxer;

/* whether we want to print an SDP, set in of_open() */
extern int want_sdp;

int mux_check_init(Muxer *mux);

static MuxStream *ms_from_ost(OutputStream *ost)
{
    return (MuxStream*)ost;
}

#endif /* FFTOOLS_FFMPEG_MUX_H */
