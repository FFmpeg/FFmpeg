/*
 * Video Decode and Presentation API for UNIX (VDPAU) is used for
 * HW decode acceleration for MPEG-1/2, H.264 and VC-1.
 *
 * Copyright (C) 2008 NVIDIA.
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

#ifndef AVCODEC_VDPAU_RENDER_H
#define AVCODEC_VDPAU_RENDER_H

/**
 * \defgroup Decoder VDPAU Decoder and Renderer
 *
 * VDPAU HW acceleration has two modules
 * - VDPAU Decoding
 * - VDPAU Presentation
 *
 * VDPAU decoding module parses all headers using FFmpeg
 * parsing mechanism and uses VDPAU for the actual decoding.
 *
 * As per the current implementation, the actual decoding
 * and rendering (API calls) are done as part of VDPAU
 * presentation (vo_vdpau.c) module.
 *
 * @{
 * \defgroup  VDPAU_Decoding VDPAU Decoding
 * \ingroup Decoder
 * @{
 */

#include "vdpau/vdpau.h"
#include "vdpau/vdpau_x11.h"

/**
 * \brief The videoSurface is used for render.
 */
#define FF_VDPAU_STATE_USED_FOR_RENDER 1

/**
 * \brief The videoSurface is needed for reference/prediction,
 * codec manipulates this.
 */
#define FF_VDPAU_STATE_USED_FOR_REFERENCE 2

/**
 * \brief This structure is used as a CALL-BACK between the ffmpeg
 * decoder (vd_) and presentation (vo_) module.
 * This is used for defining a video-frame containing surface,
 * picture-parameter, bitstream informations etc which are passed
 * between ffmpeg decoder and its clients.
 */
struct vdpau_render_state{
    VdpVideoSurface surface; ///< used as rendered surface, never changed.

    int state; ///< Holds FF_VDPAU_STATE_* values

    /** Picture Parameter information for all supported codecs */
    union _VdpPictureInfo {
        VdpPictureInfoH264     h264;
    } info;

    /** Describe size/location of the compressed video data */
    int bitstreamBuffersAlloced;
    int bitstreamBuffersUsed;
    VdpBitstreamBuffer *bitstreamBuffers;
};

/* @}*/

#endif /* AVCODEC_VDPAU_RENDER_H */
