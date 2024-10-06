/*
 * FFV1 encoder
 *
 * Copyright (c) 2003-2013 Michael Niedermayer <michaelni@gmx.at>
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

#ifndef AVCODEC_FFV1ENC_H
#define AVCODEC_FFV1ENC_H

#include "avcodec.h"

av_cold int ff_ffv1_encode_init(AVCodecContext *avctx);
av_cold int ff_ffv1_write_extradata(AVCodecContext *avctx);
av_cold int ff_ffv1_encode_setup_plane_info(AVCodecContext *avctx,
                                            enum AVPixelFormat pix_fmt);

#endif /* AVCODEC_FFV1ENC_H */
