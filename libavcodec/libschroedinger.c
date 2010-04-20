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
* @file
* function definitions common to libschroedingerdec.c and libschroedingerenc.c
*/

#include "libdirac_libschro.h"
#include "libschroedinger.h"

/**
* Schroedinger video preset table. Ensure that this tables matches up correctly
* with the ff_dirac_schro_video_format_info table in libdirac_libschro.c.
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

SchroVideoFormatEnum ff_get_schro_video_format_preset(AVCodecContext *avccontext)
{
    unsigned int num_formats = sizeof(ff_schro_video_formats) /
                               sizeof(ff_schro_video_formats[0]);

    unsigned int idx = ff_dirac_schro_get_video_format_idx (avccontext);

    return (idx < num_formats) ? ff_schro_video_formats[idx] :
                                 SCHRO_VIDEO_FORMAT_CUSTOM;
}

int ff_get_schro_frame_format (SchroChromaFormat schro_pix_fmt,
                               SchroFrameFormat  *schro_frame_fmt)
{
    unsigned int num_formats = sizeof(ffmpeg_schro_pixel_format_map) /
                               sizeof(ffmpeg_schro_pixel_format_map[0]);

    int idx;

    for (idx = 0; idx < num_formats; ++idx) {
        if (ffmpeg_schro_pixel_format_map[idx].schro_pix_fmt == schro_pix_fmt) {
            *schro_frame_fmt = ffmpeg_schro_pixel_format_map[idx].schro_frame_fmt;
            return 0;
        }
    }
    return -1;
}

static void FreeSchroFrame(SchroFrame *frame, void *priv)
{
    AVPicture *p_pic = priv;

    if (!p_pic)
        return;

    avpicture_free(p_pic);
    av_freep(&p_pic);
}

SchroFrame *ff_create_schro_frame(AVCodecContext *avccontext,
                                  SchroFrameFormat schro_frame_fmt)
{
    AVPicture *p_pic;
    SchroFrame *p_frame;
    int y_width, uv_width;
    int y_height, uv_height;
    int i;

    y_width   = avccontext->width;
    y_height  = avccontext->height;
    uv_width  = y_width  >> (SCHRO_FRAME_FORMAT_H_SHIFT(schro_frame_fmt));
    uv_height = y_height >> (SCHRO_FRAME_FORMAT_V_SHIFT(schro_frame_fmt));

    p_pic = av_mallocz(sizeof(AVPicture));
    avpicture_alloc(p_pic, avccontext->pix_fmt, y_width, y_height);

    p_frame         = schro_frame_new();
    p_frame->format = schro_frame_fmt;
    p_frame->width  = y_width;
    p_frame->height = y_height;
    schro_frame_set_free_callback(p_frame, FreeSchroFrame, (void *)p_pic);

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
