/*
 * RTP definitions
 * Copyright (c) 2002 Fabrice Bellard.
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
#include "avformat.h"

/** Structure listing useful vars to parse RTP packet payload*/
typedef struct rtp_payload_data
{
    int sizelength;
    int indexlength;
    int indexdeltalength;
    int profile_level_id;
    int streamtype;
    int objecttype;
    char *mode;

    /** mpeg 4 AU headers */
    struct AUHeaders {
        int size;
        int index;
        int cts_flag;
        int cts;
        int dts_flag;
        int dts;
        int rap_flag;
        int streamstate;
    } *au_headers;
    int nb_au_headers;
    int au_headers_length_bytes;
    int cur_au_index;
} RTPPayloadData;

typedef struct PayloadContext PayloadContext;
typedef struct RTPDynamicProtocolHandler_s RTPDynamicProtocolHandler;

#define RTP_MIN_PACKET_LENGTH 12
#define RTP_MAX_PACKET_LENGTH 1500 /* XXX: suppress this define */

int rtp_get_codec_info(AVCodecContext *codec, int payload_type);

/** return < 0 if unknown payload type */
int rtp_get_payload_type(AVCodecContext *codec);

typedef struct RTPDemuxContext RTPDemuxContext;
RTPDemuxContext *rtp_parse_open(AVFormatContext *s1, AVStream *st, URLContext *rtpc, int payload_type, RTPPayloadData *rtp_payload_data);
void rtp_parse_set_dynamic_protocol(RTPDemuxContext *s, PayloadContext *ctx,
                                    RTPDynamicProtocolHandler *handler);
int rtp_parse_packet(RTPDemuxContext *s, AVPacket *pkt,
                     const uint8_t *buf, int len);
void rtp_parse_close(RTPDemuxContext *s);

int rtp_get_local_port(URLContext *h);
int rtp_set_remote_url(URLContext *h, const char *uri);
void rtp_get_file_handles(URLContext *h, int *prtp_fd, int *prtcp_fd);

/**
 * some rtp servers assume client is dead if they don't hear from them...
 * so we send a Receiver Report to the provided ByteIO context
 * (we don't have access to the rtcp handle from here)
 */
int rtp_check_and_send_back_rr(RTPDemuxContext *s, int count);

#define RTP_PT_PRIVATE 96
#define RTP_VERSION 2
#define RTP_MAX_SDES 256   /**< maximum text length for SDES */

/* RTCP paquets use 0.5 % of the bandwidth */
#define RTCP_TX_RATIO_NUM 5
#define RTCP_TX_RATIO_DEN 1000

#endif /* AVFORMAT_RTP_H */
