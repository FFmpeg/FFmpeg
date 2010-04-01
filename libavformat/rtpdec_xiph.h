/*
 * Xiph RTP Protocols
 * Based off RFC 5215 (Vorbis RTP) and the Theora RTP draft.
 * Copyright (c) 2009 Colin McQuillian
 * Copyright (c) 2010 Josh Allmann
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

#ifndef AVFORMAT_RTPDEC_XIPH_H
#define AVFORMAT_RTPDEC_XIPH_H

#include "libavcodec/avcodec.h"
#include "rtpdec.h"

/**
 * Theora RTP callbacks.
 */
extern RTPDynamicProtocolHandler ff_theora_dynamic_handler;

/**
 * Vorbis RTP callbacks.
 */
extern RTPDynamicProtocolHandler ff_vorbis_dynamic_handler;

#endif /* AVFORMAT_RTPDEC_XIPH_H */
