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

#include <string.h>
#include <stdio.h>

#include "libavutil/buffer.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"

#include "avfilter.h"
#include "internal.h"
#include "video.h"

AVFrame *ff_null_get_video_buffer(AVFilterLink *link, int w, int h)
{
    return ff_get_video_buffer(link->dst->outputs[0], w, h);
}

/* TODO: set the buffer's priv member to a context structure for the whole
 * filter chain.  This will allow for a buffer pool instead of the constant
 * alloc & free cycle currently implemented. */
AVFrame *ff_default_get_video_buffer(AVFilterLink *link, int w, int h)
{
    AVFrame *frame = av_frame_alloc();
    int ret;

    if (!frame)
        return NULL;

    frame->width  = w;
    frame->height = h;
    frame->format = link->format;

    ret = av_frame_get_buffer(frame, 32);
    if (ret < 0)
        av_frame_free(&frame);

    return frame;
}

AVFrame *ff_get_video_buffer(AVFilterLink *link, int w, int h)
{
    AVFrame *ret = NULL;

    FF_DPRINTF_START(NULL, get_video_buffer); ff_dlog_link(NULL, link, 0);

    if (link->dstpad->get_video_buffer)
        ret = link->dstpad->get_video_buffer(link, w, h);

    if (!ret)
        ret = ff_default_get_video_buffer(link, w, h);

    return ret;
}
