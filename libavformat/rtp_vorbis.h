/*
 * RTP Vorbis Protocol (RFC 5215)
 * Copyright (c) 2009 Colin McQuillan
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

#ifndef AVFORMAT_RTP_VORBIS_H
#define AVFORMAT_RTP_VORBIS_H

#include "libavcodec/avcodec.h"
#include "rtpdec.h"

/**
 * Handle a Vorbis-specific FMTP parameter
 *
 * @param codec The context of the codec
 * @param ctx Private Vorbis RTP context
 * @param attr Format-specific parameter name
 * @param value Format-specific paremeter value
 */
int
ff_vorbis_parse_fmtp_config(AVCodecContext * codec,
                            void *ctx, char *attr, char *value);

/**
 * Vorbis RTP callbacks.
 */
extern RTPDynamicProtocolHandler ff_vorbis_dynamic_handler;

#endif /* AVFORMAT_RTP_VORBIS_H */
