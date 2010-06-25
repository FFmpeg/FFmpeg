/**
 * Common code for the RTP depacketization of MPEG-4 formats.
 * Copyright (c) 2010 Fabrice Bellard
 *                    Romain Degez
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

#ifndef AVFORMAT_RTPDEC_MPEG4_H
#define AVFORMAT_RTPDEC_MPEG4_H

#include "rtpdec.h"

/**
 * MPEG-4 Video RTP callbacks. (RFC 3016)
 */
extern RTPDynamicProtocolHandler ff_mp4v_es_dynamic_handler;

/**
 * AAC RTP callbacks. (RFC 3640)
 */
extern RTPDynamicProtocolHandler ff_mpeg4_generic_dynamic_handler;

#endif /* AVFORMAT_RTPDEC_MPEG4_H */

