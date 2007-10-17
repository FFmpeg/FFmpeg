/*
 * H263/MPEG4 backend for ffmpeg encoder and decoder
 * copyright (c) 2007 Aurelien Jacobs <aurel@gnuage.org>
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

#ifndef FFMPEG_H263_H
#define FFMPEG_H263_H

#include "config.h"
#include "msmpeg4.h"

#define ENABLE_ANY_H263_DECODER (ENABLE_H263_DECODER    || \
                                 ENABLE_H263I_DECODER   || \
                                 ENABLE_FLV_DECODER     || \
                                 ENABLE_RV10_DECODER    || \
                                 ENABLE_RV20_DECODER    || \
                                 ENABLE_MPEG4_DECODER   || \
                                 ENABLE_MSMPEG4_DECODER || \
                                 ENABLE_WMV_DECODER)
#define ENABLE_ANY_H263_ENCODER (ENABLE_H263_ENCODER    || \
                                 ENABLE_H263P_ENCODER   || \
                                 ENABLE_FLV_ENCODER     || \
                                 ENABLE_RV10_ENCODER    || \
                                 ENABLE_RV20_ENCODER    || \
                                 ENABLE_MPEG4_ENCODER   || \
                                 ENABLE_MSMPEG4_ENCODER || \
                                 ENABLE_WMV_ENCODER)
#define ENABLE_ANY_H263 (ENABLE_ANY_H263_DECODER || ENABLE_ANY_H263_ENCODER)

#endif /* FFMPEG_H263_H */
