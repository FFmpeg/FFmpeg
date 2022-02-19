/*
 * Filters implementation helper functions
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

#ifndef AVFILTER_FILTERS_H
#define AVFILTER_FILTERS_H

/**
 * Filters implementation helper functions
 */

#include "avfilter.h"
#include "internal.h"

/**
 * Special return code when activate() did not do anything.
 */
#define FFERROR_NOT_READY FFERRTAG('N','R','D','Y')

/**
 * Mark a filter ready and schedule it for activation.
 *
 * This is automatically done when something happens to the filter (queued
 * frame, status change, request on output).
 * Filters implementing the activate callback can call it directly to
 * perform one more round of processing later.
 * It is also useful for filters reacting to external or asynchronous
 * events.
 */
void ff_filter_set_ready(AVFilterContext *filter, unsigned priority);

/**
 * Process the commands queued in the link up to the time of the frame.
 * Commands will trigger the process_command() callback.
 * @return  >= 0 or AVERROR code.
 */
int ff_inlink_process_commands(AVFilterLink *link, const AVFrame *frame);

/**
 * Evaluate the timeline expression of the link for the time and properties
 * of the frame.
 * @return  >0 if enabled, 0 if disabled
 * @note  It does not update link->dst->is_disabled.
 */
int ff_inlink_evaluate_timeline_at_frame(AVFilterLink *link, const AVFrame *frame);

/**
 * Get the number of frames available on the link.
 * @return the number of frames available in the link fifo.
 */
size_t ff_inlink_queued_frames(AVFilterLink *link);

/**
 * Test if a frame is available on the link.
 * @return  >0 if a frame is available
 */
int ff_inlink_check_available_frame(AVFilterLink *link);


/***
  * Get the number of samples available on the link.
  * @return the numer of samples available on the link.
  */
int ff_inlink_queued_samples(AVFilterLink *link);

/**
 * Test if enough samples are available on the link.
 * @return  >0 if enough samples are available
 * @note  on EOF and error, min becomes 1
 */
int ff_inlink_check_available_samples(AVFilterLink *link, unsigned min);

/**
 * Take a frame from the link's FIFO and update the link's stats.
 *
 * If ff_inlink_check_available_frame() was previously called, the
 * preferred way of expressing it is "av_assert1(ret);" immediately after
 * ff_inlink_consume_frame(). Negative error codes must still be checked.
 *
 * @note  May trigger process_command() and/or update is_disabled.
 * @return  >0 if a frame is available,
 *          0 and set rframe to NULL if no frame available,
 *          or AVERROR code
 */
int ff_inlink_consume_frame(AVFilterLink *link, AVFrame **rframe);

/**
 * Take samples from the link's FIFO and update the link's stats.
 *
 * If ff_inlink_check_available_samples() was previously called, the
 * preferred way of expressing it is "av_assert1(ret);" immediately after
 * ff_inlink_consume_samples(). Negative error codes must still be checked.
 *
 * @note  May trigger process_command() and/or update is_disabled.
 * @return  >0 if a frame is available,
 *          0 and set rframe to NULL if no frame available,
 *          or AVERROR code
 */
int ff_inlink_consume_samples(AVFilterLink *link, unsigned min, unsigned max,
                            AVFrame **rframe);

/**
 * Access a frame in the link fifo without consuming it.
 * The first frame is numbered 0; the designated frame must exist.
 * @return the frame at idx position in the link fifo.
 */
AVFrame *ff_inlink_peek_frame(AVFilterLink *link, size_t idx);

/**
 * Make sure a frame is writable.
 * This is similar to av_frame_make_writable() except it uses the link's
 * buffer allocation callback, and therefore allows direct rendering.
 */
int ff_inlink_make_frame_writable(AVFilterLink *link, AVFrame **rframe);

/**
 * Test and acknowledge the change of status on the link.
 *
 * Status means EOF or an error condition; a change from the normal (0)
 * status to a non-zero status can be queued in a filter's input link, it
 * becomes relevant after the frames queued in the link's FIFO are
 * processed. This function tests if frames are still queued and if a queued
 * status change has not yet been processed. In that case it performs basic
 * treatment (updating the link's timestamp) and returns a positive value to
 * let the filter do its own treatments (flushing...).
 *
 * Filters implementing the activate callback should call this function when
 * they think it might succeed (usually after checking unsuccessfully for a
 * queued frame).
 * Filters implementing the filter_frame and request_frame callbacks do not
 * need to call that since the same treatment happens in ff_filter_frame().
 *
 * @param[out] rstatus  new or current status
 * @param[out] rpts     current timestamp of the link in link time base
 * @return  >0 if status changed, <0 if status already acked, 0 otherwise
 */
int ff_inlink_acknowledge_status(AVFilterLink *link, int *rstatus, int64_t *rpts);

/**
 * Mark that a frame is wanted on the link.
 * Unlike ff_filter_frame(), it must not be called when the link has a
 * non-zero status, and thus does not acknowledge it.
 * Also it cannot fail.
 */
void ff_inlink_request_frame(AVFilterLink *link);

/**
 * Set the status on an input link.
 * Also discard all frames in the link's FIFO.
 */
void ff_inlink_set_status(AVFilterLink *link, int status);

/**
 * Test if a frame is wanted on an output link.
 */
static inline int ff_outlink_frame_wanted(AVFilterLink *link)
{
    return link->frame_wanted_out;
}

/**
 * Get the status on an output link.
 */
int ff_outlink_get_status(AVFilterLink *link);

/**
 * Set the status field of a link from the source filter.
 * The pts should reflect the timestamp of the status change,
 * in link time base and relative to the frames timeline.
 * In particular, for AVERROR_EOF, it should reflect the
 * end time of the last frame.
 */
static inline void ff_outlink_set_status(AVFilterLink *link, int status, int64_t pts)
{
    ff_avfilter_link_set_in_status(link, status, pts);
}

/**
 * Forward the status on an output link to an input link.
 * If the status is set, it will discard all queued frames and this macro
 * will return immediately.
 */
#define FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink) do { \
    int ret = ff_outlink_get_status(outlink); \
    if (ret) { \
        ff_inlink_set_status(inlink, ret); \
        return 0; \
    } \
} while (0)

/**
 * Forward the status on an output link to all input links.
 * If the status is set, it will discard all queued frames and this macro
 * will return immediately.
 */
#define FF_FILTER_FORWARD_STATUS_BACK_ALL(outlink, filter) do { \
    int ret = ff_outlink_get_status(outlink); \
    if (ret) { \
        unsigned i; \
        for (i = 0; i < filter->nb_inputs; i++) \
            ff_inlink_set_status(filter->inputs[i], ret); \
        return 0; \
    } \
} while (0)

/**
 * Acknowledge the status on an input link and forward it to an output link.
 * If the status is set, this macro will return immediately.
 */
#define FF_FILTER_FORWARD_STATUS(inlink, outlink) do { \
    int status; \
    int64_t pts; \
    if (ff_inlink_acknowledge_status(inlink, &status, &pts)) { \
        ff_outlink_set_status(outlink, status, pts); \
        return 0; \
    } \
} while (0)

/**
 * Acknowledge the status on an input link and forward it to an output link.
 * If the status is set, this macro will return immediately.
 */
#define FF_FILTER_FORWARD_STATUS_ALL(inlink, filter) do { \
    int status; \
    int64_t pts; \
    if (ff_inlink_acknowledge_status(inlink, &status, &pts)) { \
        unsigned i; \
        for (i = 0; i < filter->nb_outputs; i++) \
            ff_outlink_set_status(filter->outputs[i], status, pts); \
        return 0; \
    } \
} while (0)

/**
 * Forward the frame_wanted_out flag from an output link to an input link.
 * If the flag is set, this macro will return immediately.
 */
#define FF_FILTER_FORWARD_WANTED(outlink, inlink) do { \
    if (ff_outlink_frame_wanted(outlink)) { \
        ff_inlink_request_frame(inlink); \
        return 0; \
    } \
} while (0)

/**
 * Check for flow control between input and output.
 * This is necessary for filters that may produce several output frames for
 * a single input event, otherwise they may produce them all at once,
 * causing excessive memory consumption.
 */
int ff_inoutlink_check_flow(AVFilterLink *inlink, AVFilterLink *outlink);

#endif /* AVFILTER_FILTERS_H */
