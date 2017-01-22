/*
 * Copyright (c) 2013 Nicolas George
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVFILTER_FRAMESYNC_H
#define AVFILTER_FRAMESYNC_H

#include "bufferqueue.h"

/*
 * TODO
 * Callback-based API similar to dualinput.
 * Export convenient options.
 */

/**
 * This API is intended as a helper for filters that have several video
 * input and need to combine them somehow. If the inputs have different or
 * variable frame rate, getting the input frames to match requires a rather
 * complex logic and a few user-tunable options.
 *
 * In this API, when a set of synchronized input frames is ready to be
 * procesed is called a frame event. Frame event can be generated in
 * response to input frames on any or all inputs and the handling of
 * situations where some stream extend beyond the beginning or the end of
 * others can be configured.
 *
 * The basic working of this API is the following:
 *
 * - When a frame is available on any input, add it using
 *   ff_framesync_add_frame().
 *
 * - When a frame event is ready to be processed (i.e. after adding a frame
 *   or when requested on input):
 *   - call ff_framesync_next();
 *   - if fs->frame_ready is true, process the frames;
 *   - call ff_framesync_drop().
 */

/**
 * Stream extrapolation mode
 *
 * Describe how the frames of a stream are extrapolated before the first one
 * and after EOF to keep sync with possibly longer other streams.
 */
enum FFFrameSyncExtMode {

    /**
     * Completely stop all streams with this one.
     */
    EXT_STOP,

    /**
     * Ignore this stream and continue processing the other ones.
     */
    EXT_NULL,

    /**
     * Extend the frame to infinity.
     */
    EXT_INFINITY,
};

/**
 * Input stream structure
 */
typedef struct FFFrameSyncIn {

    /**
     * Queue of incoming AVFrame, and NULL to mark EOF
     */
    struct FFBufQueue queue;

    /**
     * Extrapolation mode for timestamps before the first frame
     */
    enum FFFrameSyncExtMode before;

    /**
     * Extrapolation mode for timestamps after the last frame
     */
    enum FFFrameSyncExtMode after;

    /**
     * Time base for the incoming frames
     */
    AVRational time_base;

    /**
     * Current frame, may be NULL before the first one or after EOF
     */
    AVFrame *frame;

    /**
     * Next frame, for internal use
     */
    AVFrame *frame_next;

    /**
     * PTS of the current frame
     */
    int64_t pts;

    /**
     * PTS of the next frame, for internal use
     */
    int64_t pts_next;

    /**
     * Boolean flagging the next frame, for internal use
     */
    uint8_t have_next;

    /**
     * State: before first, in stream or after EOF, for internal use
     */
    uint8_t state;

    /**
     * Synchronization level: frames on input at the highest sync level will
     * generate output frame events.
     *
     * For example, if inputs #0 and #1 have sync level 2 and input #2 has
     * sync level 1, then a frame on either input #0 or #1 will generate a
     * frame event, but not a frame on input #2 until both inputs #0 and #1
     * have reached EOF.
     *
     * If sync is 0, no frame event will be generated.
     */
    unsigned sync;

} FFFrameSyncIn;

/**
 * Frame sync structure.
 */
typedef struct FFFrameSync {
    const AVClass *class;
    void *parent;

    /**
     * Number of input streams
     */
    unsigned nb_in;

    /**
     * Time base for the output events
     */
    AVRational time_base;

    /**
     * Timestamp of the current event
     */
    int64_t pts;

    /**
     * Callback called when a frame event is ready
     */
    int (*on_event)(struct FFFrameSync *fs);

    /**
     * Opaque pointer, not used by the API
     */
    void *opaque;

    /**
     * Index of the input that requires a request
     */
    unsigned in_request;

    /**
     * Synchronization level: only inputs with the same sync level are sync
     * sources.
     */
    unsigned sync_level;

    /**
     * Flag indicating that a frame event is ready
     */
    uint8_t frame_ready;

    /**
     * Flag indicating that output has reached EOF.
     */
    uint8_t eof;

    /**
     * Pointer to array of inputs.
     */
    FFFrameSyncIn *in;

} FFFrameSync;

/**
 * Initialize a frame sync structure.
 *
 * The entire structure is expected to be already set to 0.
 *
 * @param  fs      frame sync structure to initialize
 * @param  parent  parent object, used for logging
 * @param  nb_in   number of inputs
 * @return  >= 0 for success or a negative error code
 */
int ff_framesync_init(FFFrameSync *fs, void *parent, unsigned nb_in);

/**
 * Configure a frame sync structure.
 *
 * Must be called after all options are set but before all use.
 *
 * @return  >= 0 for success or a negative error code
 */
int ff_framesync_configure(FFFrameSync *fs);

/**
 * Free all memory currently allocated.
 */
void ff_framesync_uninit(FFFrameSync *fs);

/**
 * Add a frame to an input
 *
 * Typically called from the filter_frame() method.
 *
 * @param fs     frame sync structure
 * @param in     index of the input
 * @param frame  input frame, or NULL for EOF
 */
int ff_framesync_add_frame(FFFrameSync *fs, unsigned in, AVFrame *frame);

/**
 * Prepare the next frame event.
 *
 * The status of the operation can be found in fs->frame_ready and fs->eof.
 */
void ff_framesync_next(FFFrameSync *fs);

/**
 * Drop the current frame event.
 */
void ff_framesync_drop(FFFrameSync *fs);

/**
 * Get the current frame in an input.
 *
 * @param fs      frame sync structure
 * @param in      index of the input
 * @param rframe  used to return the current frame (or NULL)
 * @param get     if not zero, the calling code needs to get ownership of
 *                the returned frame; the current frame will either be
 *                duplicated or removed from the framesync structure
 */
int ff_framesync_get_frame(FFFrameSync *fs, unsigned in, AVFrame **rframe,
                           unsigned get);

/**
 * Process one or several frame using the on_event callback.
 *
 * @return  number of frames processed or negative error code
 */
int ff_framesync_process_frame(FFFrameSync *fs, unsigned all);


/**
 * Accept a frame on a filter input.
 *
 * This function can be the complete implementation of all filter_frame
 * methods of a filter using framesync.
 */
int ff_framesync_filter_frame(FFFrameSync *fs, AVFilterLink *inlink,
                              AVFrame *in);

/**
 * Request a frame on the filter output.
 *
 * This function can be the complete implementation of all filter_frame
 * methods of a filter using framesync if it has only one output.
 */
int ff_framesync_request_frame(FFFrameSync *fs, AVFilterLink *outlink);

#endif /* AVFILTER_FRAMESYNC_H */
