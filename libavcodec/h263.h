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

#ifndef AVCODEC_H263_H
#define AVCODEC_H263_H

#include "config.h"
#include "msmpeg4.h"

#define CONFIG_ANY_H263_DECODER (CONFIG_H263_DECODER    || \
                                 CONFIG_H263I_DECODER   || \
                                 CONFIG_FLV_DECODER     || \
                                 CONFIG_RV10_DECODER    || \
                                 CONFIG_RV20_DECODER    || \
                                 CONFIG_MPEG4_DECODER   || \
                                 CONFIG_MSMPEG4_DECODER || \
                                 CONFIG_WMV_DECODER)
#define CONFIG_ANY_H263_ENCODER (CONFIG_H263_ENCODER    || \
                                 CONFIG_H263P_ENCODER   || \
                                 CONFIG_FLV_ENCODER     || \
                                 CONFIG_RV10_ENCODER    || \
                                 CONFIG_RV20_ENCODER    || \
                                 CONFIG_MPEG4_ENCODER   || \
                                 CONFIG_MSMPEG4_ENCODER || \
                                 CONFIG_WMV_ENCODER)
#define CONFIG_ANY_H263 (CONFIG_ANY_H263_DECODER || CONFIG_ANY_H263_ENCODER)

#endif /* AVCODEC_H263_H */
