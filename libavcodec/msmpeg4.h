/*
 * MSMPEG4 backend for ffmpeg encoder and decoder
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

/**
 * @file msmpeg4.h
 */

#ifndef MSMPEG4_H
#define MSMPEG4_H

#include "config.h"

#define ENABLE_MSMPEG4_DECODER (ENABLE_MSMPEG4V1_DECODER || \
                                ENABLE_MSMPEG4V2_DECODER || \
                                ENABLE_MSMPEG4V3_DECODER || \
                                ENABLE_WMV2_DECODER)
#define ENABLE_MSMPEG4_ENCODER (ENABLE_MSMPEG4V1_ENCODER || \
                                ENABLE_MSMPEG4V2_ENCODER || \
                                ENABLE_MSMPEG4V3_ENCODER || \
                                ENABLE_WMV2_ENCODER)
#define ENABLE_MSMPEG4 (ENABLE_MSMPEG4_DECODER || ENABLE_MSMPEG4_ENCODER)
#define ENABLE_WMV2 (ENABLE_WMV2_DECODER || ENABLE_WMV2_ENCODER)
#define ENABLE_WMV_DECODER (ENABLE_WMV1_DECODER || ENABLE_WMV2_DECODER)
#define ENABLE_WMV_ENCODER (ENABLE_WMV1_ENCODER || ENABLE_WMV2_ENCODER)

#endif /* MSMPEG4_H */
