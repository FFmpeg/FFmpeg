/*
 * RTP definitions
 * Copyright (c) 2002 Fabrice Bellard
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
#ifndef AVFORMAT_RTP_H
#define AVFORMAT_RTP_H

#include "libavcodec/avcodec.h"

/**
 * Return the payload type for a given codec.
 *
 * @param codec The context of the codec
 * @return In case of unknown payload type or dynamic payload type, a
 * negative value is returned; otherwise, the payload type (the 'PT' field
 * in the RTP header) is returned.
 */
int ff_rtp_get_payload_type(AVCodecContext *codec);

int ff_rtp_get_codec_info(AVCodecContext *codec, int payload_type);
const char *ff_rtp_enc_name(int payload_type);
enum CodecID ff_rtp_codec_id(const char *buf, enum CodecType codec_type);

#define RTP_PT_PRIVATE 96
#define RTP_VERSION 2
#define RTP_MAX_SDES 256   /**< maximum text length for SDES */

/* RTCP paquets use 0.5 % of the bandwidth */
#define RTCP_TX_RATIO_NUM 5
#define RTCP_TX_RATIO_DEN 1000

#endif /* AVFORMAT_RTP_H */
