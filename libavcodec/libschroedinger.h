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
* @file libavcodec/libschroedinger.h
* data structures common to libschroedingerdec.c and libschroedingerenc.c
*/

#ifndef AVCODEC_LIBSCHROEDINGER_H
#define AVCODEC_LIBSCHROEDINGER_H

#include "config.h"

#if CONFIG_LIBSCHROEDINGER

#include <schroedinger/schrobitstream.h>
#include <schroedinger/schroframe.h>
#include "avcodec.h"

static const struct {
    enum PixelFormat  ff_pix_fmt;
    SchroChromaFormat schro_pix_fmt;
    SchroFrameFormat  schro_frame_fmt;
} ffmpeg_schro_pixel_format_map[] = {
    { PIX_FMT_YUV420P, SCHRO_CHROMA_420, SCHRO_FRAME_FORMAT_U8_420 },
    { PIX_FMT_YUV422P, SCHRO_CHROMA_422, SCHRO_FRAME_FORMAT_U8_422 },
    { PIX_FMT_YUV444P, SCHRO_CHROMA_444, SCHRO_FRAME_FORMAT_U8_444 },
};

/**
* Returns the video format preset matching the input video dimensions and
* time base.
*/
SchroVideoFormatEnum ff_get_schro_video_format_preset (AVCodecContext *avccontext);

/**
* Sets the Schroedinger frame format corresponding to the Schro chroma format
* passed. Returns 0 on success, -1 on failure.
*/
int ff_get_schro_frame_format (SchroChromaFormat schro_chroma_fmt,
                               SchroFrameFormat  *schro_frame_fmt);

#endif /* CONFIG_LIBSCHROEDINGER */
#endif /* AVCODEC_LIBSCHROEDINGER_H */
