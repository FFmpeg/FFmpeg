/*
 * Copyright (c) 2008 BBC, Anuradha Suraparaju <asuraparaju at gmail dot com >
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

/**
* @file libavcodec/libdirac_libschro.c
* functions common to libdirac and libschroedinger
*/

#include "libdirac_libschro.h"

static const FfmpegDiracSchroVideoFormatInfo ff_dirac_schro_video_format_info[] = {
    { 640,  480,  24000, 1001},
    { 176,  120,  15000, 1001},
    { 176,  144,  25,    2   },
    { 352,  240,  15000, 1001},
    { 352,  288,  25,    2   },
    { 704,  480,  15000, 1001},
    { 704,  576,  25,    2   },
    { 720,  480,  30000, 1001},
    { 720,  576,  25,    1   },
    { 1280, 720,  60000, 1001},
    { 1280, 720,  50,    1   },
    { 1920, 1080, 30000, 1001},
    { 1920, 1080, 25,    1   },
    { 1920, 1080, 60000, 1001},
    { 1920, 1080, 50,    1   },
    { 2048, 1080, 24,    1   },
    { 4096, 2160, 24,    1   },
};

unsigned int ff_dirac_schro_get_video_format_idx(AVCodecContext *avccontext)
{
    unsigned int ret_idx = 0;
    unsigned int idx;
    unsigned int num_formats = sizeof(ff_dirac_schro_video_format_info) /
                               sizeof(ff_dirac_schro_video_format_info[0]);

    for (idx = 1; idx < num_formats; ++idx) {
        const FfmpegDiracSchroVideoFormatInfo *vf = &ff_dirac_schro_video_format_info[idx];
        if (avccontext->width  == vf->width &&
            avccontext->height == vf->height) {
            ret_idx = idx;
            if (avccontext->time_base.den == vf->frame_rate_num &&
                avccontext->time_base.num == vf->frame_rate_denom)
                return idx;
        }
    }
    return ret_idx;
}

void ff_dirac_schro_queue_init(FfmpegDiracSchroQueue *queue)
{
    queue->p_head = queue->p_tail = NULL;
    queue->size = 0;
}

void ff_dirac_schro_queue_free(FfmpegDiracSchroQueue *queue,
                               void (*free_func)(void *))
{
    while (queue->p_head)
        free_func(ff_dirac_schro_queue_pop(queue));
}

int ff_dirac_schro_queue_push_back(FfmpegDiracSchroQueue *queue, void *p_data)
{
    FfmpegDiracSchroQueueElement *p_new = av_mallocz(sizeof(FfmpegDiracSchroQueueElement));

    if (!p_new)
        return -1;

    p_new->data = p_data;

    if (!queue->p_head)
        queue->p_head = p_new;
    else
        queue->p_tail->next = p_new;
    queue->p_tail = p_new;

    ++queue->size;
    return 0;
}

void *ff_dirac_schro_queue_pop(FfmpegDiracSchroQueue *queue)
{
    FfmpegDiracSchroQueueElement *top = queue->p_head;

    if (top) {
        void *data = top->data;
        queue->p_head = queue->p_head->next;
        --queue->size;
        av_freep(&top);
        return data;
    }

    return NULL;
}
