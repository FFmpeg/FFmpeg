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

#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"

/**
 * Return the payload type for a given codec used in the given format context.
 *
 * @param fmt   The context of the format
 * @param codec The context of the codec
 * @return The payload type (the 'PT' field in the RTP header).
 */
int ff_rtp_get_payload_type(AVFormatContext *fmt, AVCodecContext *codec);

/**
 * Initialize a codec context based on the payload type.
 *
 * Fill the codec_type and codec_id fields of a codec context with
 * information depending on the payload type; for audio codecs, the
 * channels and sample_rate fields are also filled.
 *
 * @param codec The context of the codec
 * @param payload_type The payload type (the 'PT' field in the RTP header)
 * @return In case of unknown payload type or dynamic payload type, a
 * negative value is returned; otherwise, 0 is returned
 */
int ff_rtp_get_codec_info(AVCodecContext *codec, int payload_type);

/**
 * Return the encoding name (as defined in
 * http://www.iana.org/assignments/rtp-parameters) for a given payload type.
 *
 * @param payload_type The payload type (the 'PT' field in the RTP header)
 * @return In case of unknown payload type or dynamic payload type, a pointer
 * to an empty string is returned; otherwise, a pointer to a string containing
 * the encoding name is returned
 */
const char *ff_rtp_enc_name(int payload_type);

/**
 * Return the codec id for the given encoding name and codec type.
 *
 * @param buf A pointer to the string containing the encoding name
 * @param codec_type The codec type
 * @return In case of unknown encoding name, CODEC_ID_NONE is returned;
 * otherwise, the codec id is returned
 */
enum CodecID ff_rtp_codec_id(const char *buf, enum AVMediaType codec_type);

#define RTP_PT_PRIVATE 96
#define RTP_VERSION 2
#define RTP_MAX_SDES 256   /**< maximum text length for SDES */

/* RTCP paquets use 0.5 % of the bandwidth */
#define RTCP_TX_RATIO_NUM 5
#define RTCP_TX_RATIO_DEN 1000

/* An arbitrary id value for RTP Xiph streams - only relevant to indicate
 * the the configuration has changed within a stream (by changing the
 * ident value sent).
 */
#define RTP_XIPH_IDENT 0xfecdba

/* RTCP packet types */
enum RTCPType {
    RTCP_SR     = 200,
    RTCP_RR,   // 201
    RTCP_SDES, // 202
    RTCP_BYE,  // 203
    RTCP_APP   // 204
};

#endif /* AVFORMAT_RTP_H */
