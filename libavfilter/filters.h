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
 * Test if a frame is available on the link.
 * @return  >0 if a frame is available
 */
int ff_inlink_check_available_frame(AVFilterLink *link);

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

#endif /* AVFILTER_FILTERS_H */
