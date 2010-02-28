/*
 * Microsoft RTP/ASF support.
 * Copyright (c) 2008 Ronald S. Bultje
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

#ifndef AVFORMAT_RTPDEC_ASF_H
#define AVFORMAT_RTPDEC_ASF_H

#include "avformat.h"
#include "rtpdec.h"

/**
 * Parse a Windows Media Server-specific SDP line
 *
 * @param s RTSP demux context
 * @param line the SDP line to be parsed
 */
void ff_wms_parse_sdp_a_line(AVFormatContext *s, const char *p);

/**
 * Handlers for the x-asf-pf payloads (the payload ID for RTP/ASF).
 * Defined and implemented in rtp_asf.c, registered in rtpdec.c.
 */
extern RTPDynamicProtocolHandler ff_ms_rtp_asf_pfv_handler,
                                 ff_ms_rtp_asf_pfa_handler;

#endif /* AVFORMAT_RTPDEC_ASF_H */
