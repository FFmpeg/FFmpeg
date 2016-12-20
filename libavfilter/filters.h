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

#endif /* AVFILTER_FILTERS_H */
