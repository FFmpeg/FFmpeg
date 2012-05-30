/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVFILTER_INTERNAL_H
#define AVFILTER_INTERNAL_H

/**
 * @file
 * internal API functions
 */

#include "avfilter.h"

/** default handler for freeing audio/video buffer when there are no references left */
void ff_avfilter_default_free_buffer(AVFilterBuffer *buf);

/** Tell is a format is contained in the provided list terminated by -1. */
int ff_fmt_is_in(int fmt, const int *fmts);

#define FF_DPRINTF_START(ctx, func) av_dlog(NULL, "%-16s: ", #func)

void ff_dlog_link(void *ctx, AVFilterLink *link, int end);

/**
 * Insert a new pad.
 *
 * @param idx Insertion point. Pad is inserted at the end if this point
 *            is beyond the end of the list of pads.
 * @param count Pointer to the number of pads in the list
 * @param padidx_off Offset within an AVFilterLink structure to the element
 *                   to increment when inserting a new pad causes link
 *                   numbering to change
 * @param pads Pointer to the pointer to the beginning of the list of pads
 * @param links Pointer to the pointer to the beginning of the list of links
 * @param newpad The new pad to add. A copy is made when adding.
 */
void ff_insert_pad(unsigned idx, unsigned *count, size_t padidx_off,
                   AVFilterPad **pads, AVFilterLink ***links,
                   AVFilterPad *newpad);

/** Insert a new input pad for the filter. */
static inline void ff_insert_inpad(AVFilterContext *f, unsigned index,
                                   AVFilterPad *p)
{
    ff_insert_pad(index, &f->input_count, offsetof(AVFilterLink, dstpad),
                  &f->input_pads, &f->inputs, p);
}

/** Insert a new output pad for the filter. */
static inline void ff_insert_outpad(AVFilterContext *f, unsigned index,
                                    AVFilterPad *p)
{
    ff_insert_pad(index, &f->output_count, offsetof(AVFilterLink, srcpad),
                  &f->output_pads, &f->outputs, p);
}

/**
 * Poll a frame from the filter chain.
 *
 * @param  link the input link
 * @return the number of immediately available frames, a negative
 * number in case of error
 */
int ff_poll_frame(AVFilterLink *link);

/**
 * Request an input frame from the filter at the other end of the link.
 *
 * @param link the input link
 * @return     zero on success
 */
int ff_request_frame(AVFilterLink *link);

#endif /* AVFILTER_INTERNAL_H */
