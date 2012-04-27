/*
 *
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

#ifndef AVFILTER_BUFFERSRC_H
#define AVFILTER_BUFFERSRC_H

/**
 * @file
 * Memory buffer source API.
 */

#include "avfilter.h"

enum {

    /**
     * Do not check for format changes.
     */
    AV_BUFFERSRC_FLAG_NO_CHECK_FORMAT = 1,

    /**
     * Do not copy buffer data.
     */
    AV_BUFFERSRC_FLAG_NO_COPY = 2,

};

/**
 * Add video buffer data in picref to buffer_src.
 *
 * @param buffer_src  pointer to a buffer source context
 * @param picref      a buffer reference, or NULL to mark EOF
 * @param flags       a combination of AV_BUFFERSRC_FLAG_*
 * @return            >= 0 in case of success, a negative AVERROR code
 *                    in case of failure
 */
int av_buffersrc_add_ref(AVFilterContext *buffer_src,
                         AVFilterBufferRef *picref, int flags);

/**
 * Add a buffer to the filtergraph s.
 *
 * @param buf buffer containing frame data to be passed down the filtergraph.
 * This function will take ownership of buf, the user must not free it.
 * A NULL buf signals EOF -- i.e. no more frames will be sent to this filter.
 */
int av_buffersrc_buffer(AVFilterContext *s, AVFilterBufferRef *buf);

#endif /* AVFILTER_BUFFERSRC_H */
