/*
 * MPEG-4 encoder/decoder internal header.
 * Copyright (c) 2000,2001 Fabrice Bellard
 * Copyright (c) 2002-2010 Michael Niedermayer <michaelni@gmx.at>
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

#ifndef AVCODEC_MPEG4VIDEO_H
#define AVCODEC_MPEG4VIDEO_H

#include <stdint.h>

#include "mpegvideo.h"

void ff_mpeg4_clean_buffers(MpegEncContext *s);
int ff_mpeg4_get_video_packet_prefix_length(enum AVPictureType pict_type,
                                            int f_code, int b_code);
void ff_mpeg4_init_direct_mv(MpegEncContext *s);

/**
 * @return the mb_type
 */
int ff_mpeg4_set_direct_mv(MpegEncContext *s, int mx, int my);

#endif /* AVCODEC_MPEG4VIDEO_H */
