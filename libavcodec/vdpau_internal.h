/*
 * Video Decode and Presentation API for UNIX (VDPAU) is used for
 * HW decode acceleration for MPEG-1/2, H.264 and VC-1.
 *
 * Copyright (C) 2008 NVIDIA
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

#ifndef AVCODEC_VDPAU_INTERNAL_H
#define AVCODEC_VDPAU_INTERNAL_H

#include <stdint.h>
#include "mpegvideo.h"

void ff_vdpau_add_data_chunk(MpegEncContext *s, const uint8_t *buf,
                             int buf_size);

void ff_vdpau_mpeg_picture_complete(MpegEncContext *s, const uint8_t *buf,
                                    int buf_size, int slice_count);

void ff_vdpau_h264_picture_start(MpegEncContext *s);
void ff_vdpau_h264_set_reference_frames(MpegEncContext *s);
void ff_vdpau_h264_picture_complete(MpegEncContext *s);

void ff_vdpau_vc1_decode_picture(MpegEncContext *s, const uint8_t *buf,
                                 int buf_size);

void ff_vdpau_mpeg4_decode_picture(MpegEncContext *s, const uint8_t *buf,
                                   int buf_size);

#endif /* AVCODEC_VDPAU_INTERNAL_H */
