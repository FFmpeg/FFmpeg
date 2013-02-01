/*
 * Copyright (c) 2008 BBC, Anuradha Suraparaju <asuraparaju at gmail dot com >
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

/**
* @file
* function definitions common to libschroedinger decoder and encoder
*/

#include "libavutil/attributes.h"
#include "libavutil/mem.h"
#include "libschroedinger.h"

static const SchroVideoFormatInfo ff_schro_video_format_info[] = {
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

static unsigned int get_video_format_idx(AVCodecContext *avctx)
{
    unsigned int ret_idx = 0;
    unsigned int idx;
    unsigned int num_formats = sizeof(ff_schro_video_format_info) /
                               sizeof(ff_schro_video_format_info[0]);

    for (idx = 1; idx < num_formats; ++idx) {
        const SchroVideoFormatInfo *vf = &ff_schro_video_format_info[idx];
        if (avctx->width  == vf->width &&
            avctx->height == vf->height) {
            ret_idx = idx;
            if (avctx->time_base.den == vf->frame_rate_num &&
                avctx->time_base.num == vf->frame_rate_denom)
                return idx;
        }
    }
    return ret_idx;
}

av_cold void ff_schro_queue_init(FFSchroQueue *queue)
{
    queue->p_head = queue->p_tail = NULL;
    queue->size = 0;
}

void ff_schro_queue_free(FFSchroQueue *queue, void (*free_func)(void *))
{
    while (queue->p_head)
        free_func(ff_schro_queue_pop(queue));
}

int ff_schro_queue_push_back(FFSchroQueue *queue, void *p_data)
{
    FFSchroQueueElement *p_new = av_mallocz(sizeof(FFSchroQueueElement));

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

void *ff_schro_queue_pop(FFSchroQueue *queue)
{
    FFSchroQueueElement *top = queue->p_head;

    if (top) {
        void *data = top->data;
        queue->p_head = queue->p_head->next;
        --queue->size;
        av_freep(&top);
        return data;
    }

    return NULL;
}

/**
* Schroedinger video preset table. Ensure that this tables matches up correctly
* with the ff_schro_video_format_info table.
*/
static const SchroVideoFormatEnum ff_schro_video_formats[]={
    SCHRO_VIDEO_FORMAT_CUSTOM     ,
    SCHRO_VIDEO_FORMAT_QSIF       ,
    SCHRO_VIDEO_FORMAT_QCIF       ,
    SCHRO_VIDEO_FORMAT_SIF        ,
    SCHRO_VIDEO_FORMAT_CIF        ,
    SCHRO_VIDEO_FORMAT_4SIF       ,
    SCHRO_VIDEO_FORMAT_4CIF       ,
    SCHRO_VIDEO_FORMAT_SD480I_60  ,
    SCHRO_VIDEO_FORMAT_SD576I_50  ,
    SCHRO_VIDEO_FORMAT_HD720P_60  ,
    SCHRO_VIDEO_FORMAT_HD720P_50  ,
    SCHRO_VIDEO_FORMAT_HD1080I_60 ,
    SCHRO_VIDEO_FORMAT_HD1080I_50 ,
    SCHRO_VIDEO_FORMAT_HD1080P_60 ,
    SCHRO_VIDEO_FORMAT_HD1080P_50 ,
    SCHRO_VIDEO_FORMAT_DC2K_24    ,
    SCHRO_VIDEO_FORMAT_DC4K_24    ,
};

SchroVideoFormatEnum ff_get_schro_video_format_preset(AVCodecContext *avctx)
{
    unsigned int num_formats = sizeof(ff_schro_video_formats) /
                               sizeof(ff_schro_video_formats[0]);

    unsigned int idx = get_video_format_idx(avctx);

    return (idx < num_formats) ? ff_schro_video_formats[idx] :
                                 SCHRO_VIDEO_FORMAT_CUSTOM;
}

int ff_get_schro_frame_format (SchroChromaFormat schro_pix_fmt,
                               SchroFrameFormat  *schro_frame_fmt)
{
    unsigned int num_formats = sizeof(schro_pixel_format_map) /
                               sizeof(schro_pixel_format_map[0]);

    int idx;

    for (idx = 0; idx < num_formats; ++idx) {
        if (schro_pixel_format_map[idx].schro_pix_fmt == schro_pix_fmt) {
            *schro_frame_fmt = schro_pixel_format_map[idx].schro_frame_fmt;
            return 0;
        }
    }
    return -1;
}

static void free_schro_frame(SchroFrame *frame, void *priv)
{
    AVPicture *p_pic = priv;

    if (!p_pic)
        return;

    avpicture_free(p_pic);
    av_freep(&p_pic);
}

SchroFrame *ff_create_schro_frame(AVCodecContext *avctx,
                                  SchroFrameFormat schro_frame_fmt)
{
    AVPicture *p_pic;
    SchroFrame *p_frame;
    int y_width, uv_width;
    int y_height, uv_height;
    int i;

    y_width   = avctx->width;
    y_height  = avctx->height;
    uv_width  = y_width  >> (SCHRO_FRAME_FORMAT_H_SHIFT(schro_frame_fmt));
    uv_height = y_height >> (SCHRO_FRAME_FORMAT_V_SHIFT(schro_frame_fmt));

    p_pic = av_mallocz(sizeof(AVPicture));
    avpicture_alloc(p_pic, avctx->pix_fmt, y_width, y_height);

    p_frame         = schro_frame_new();
    p_frame->format = schro_frame_fmt;
    p_frame->width  = y_width;
    p_frame->height = y_height;
    schro_frame_set_free_callback(p_frame, free_schro_frame, (void *)p_pic);

    for (i = 0; i < 3; ++i) {
        p_frame->components[i].width  = i ? uv_width : y_width;
        p_frame->components[i].stride = p_pic->linesize[i];
        p_frame->components[i].height = i ? uv_height : y_height;
        p_frame->components[i].length =
                 p_frame->components[i].stride * p_frame->components[i].height;
        p_frame->components[i].data   = p_pic->data[i];

        if (i) {
            p_frame->components[i].v_shift =
                SCHRO_FRAME_FORMAT_V_SHIFT(p_frame->format);
            p_frame->components[i].h_shift =
                SCHRO_FRAME_FORMAT_H_SHIFT(p_frame->format);
        }
    }

    return p_frame;
}
