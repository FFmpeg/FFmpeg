/*
 * RTP depacketizer declarations
 * Copyright (c) 2010 Martin Storsjo
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

#ifndef AVFORMAT_RTPDEC_FORMATS_H
#define AVFORMAT_RTPDEC_FORMATS_H

#include "rtpdec.h"

/**
 * Parse a Windows Media Server-specific SDP line
 *
 * @param s RTSP demux context
 */
int ff_wms_parse_sdp_a_line(AVFormatContext *s, const char *p);

extern RTPDynamicProtocolHandler ff_amr_nb_dynamic_handler;
extern RTPDynamicProtocolHandler ff_amr_wb_dynamic_handler;
extern RTPDynamicProtocolHandler ff_g726_16_dynamic_handler;
extern RTPDynamicProtocolHandler ff_g726_24_dynamic_handler;
extern RTPDynamicProtocolHandler ff_g726_32_dynamic_handler;
extern RTPDynamicProtocolHandler ff_g726_40_dynamic_handler;
extern RTPDynamicProtocolHandler ff_h263_1998_dynamic_handler;
extern RTPDynamicProtocolHandler ff_h263_2000_dynamic_handler;
extern RTPDynamicProtocolHandler ff_h264_dynamic_handler;
extern RTPDynamicProtocolHandler ff_mp4a_latm_dynamic_handler;
extern RTPDynamicProtocolHandler ff_mp4v_es_dynamic_handler;
extern RTPDynamicProtocolHandler ff_mpeg4_generic_dynamic_handler;
extern RTPDynamicProtocolHandler ff_ms_rtp_asf_pfa_handler;
extern RTPDynamicProtocolHandler ff_ms_rtp_asf_pfv_handler;
extern RTPDynamicProtocolHandler ff_qcelp_dynamic_handler;
extern RTPDynamicProtocolHandler ff_qdm2_dynamic_handler;
extern RTPDynamicProtocolHandler ff_qt_rtp_aud_handler;
extern RTPDynamicProtocolHandler ff_qt_rtp_vid_handler;
extern RTPDynamicProtocolHandler ff_quicktime_rtp_aud_handler;
extern RTPDynamicProtocolHandler ff_quicktime_rtp_vid_handler;
extern RTPDynamicProtocolHandler ff_svq3_dynamic_handler;
extern RTPDynamicProtocolHandler ff_theora_dynamic_handler;
extern RTPDynamicProtocolHandler ff_vorbis_dynamic_handler;
extern RTPDynamicProtocolHandler ff_vp8_dynamic_handler;

#endif /* AVFORMAT_RTPDEC_FORMATS_H */
