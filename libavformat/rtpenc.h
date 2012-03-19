/*
 * RTP muxer definitions
 * Copyright (c) 2002 Fabrice Bellard
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
#ifndef AVFORMAT_RTPENC_H
#define AVFORMAT_RTPENC_H

#include "avformat.h"
#include "rtp.h"

struct RTPMuxContext {
    const AVClass *av_class;
    AVFormatContext *ic;
    AVStream *st;
    int payload_type;
    uint32_t ssrc;
    uint16_t seq;
    uint32_t timestamp;
    uint32_t base_timestamp;
    uint32_t cur_timestamp;
    int max_payload_size;
    int num_frames;

    /* rtcp sender statistics receive */
    int64_t last_rtcp_ntp_time;    // TODO: move into statistics
    int64_t first_rtcp_ntp_time;   // TODO: move into statistics

    /* rtcp sender statistics */
    unsigned int packet_count;     // TODO: move into statistics (outgoing)
    unsigned int octet_count;      // TODO: move into statistics (outgoing)
    unsigned int last_octet_count; // TODO: move into statistics (outgoing)
    int first_packet;
    /* buffer for output */
    uint8_t *buf;
    uint8_t *buf_ptr;

    int max_frames_per_packet;

    /**
     * Number of bytes used for H.264 NAL length, if the MP4 syntax is used
     * (1, 2 or 4)
     */
    int nal_length_size;

    int flags;
};

typedef struct RTPMuxContext RTPMuxContext;

#define FF_RTP_FLAG_MP4A_LATM 1
#define FF_RTP_FLAG_RFC2190   2
#define FF_RTP_FLAG_SKIP_RTCP 4

#define FF_RTP_FLAG_OPTS(ctx, fieldname) \
    { "rtpflags", "RTP muxer flags", offsetof(ctx, fieldname), AV_OPT_TYPE_FLAGS, {.dbl = 0}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, "rtpflags" }, \
    { "latm", "Use MP4A-LATM packetization instead of MPEG4-GENERIC for AAC", 0, AV_OPT_TYPE_CONST, {.dbl = FF_RTP_FLAG_MP4A_LATM}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, "rtpflags" }, \
    { "rfc2190", "Use RFC 2190 packetization instead of RFC 4629 for H.263", 0, AV_OPT_TYPE_CONST, {.dbl = FF_RTP_FLAG_RFC2190}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, "rtpflags" }, \
    { "skip_rtcp", "Don't send RTCP sender reports", 0, AV_OPT_TYPE_CONST, {.dbl = FF_RTP_FLAG_SKIP_RTCP}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, "rtpflags" }, \

void ff_rtp_send_data(AVFormatContext *s1, const uint8_t *buf1, int len, int m);

void ff_rtp_send_h264(AVFormatContext *s1, const uint8_t *buf1, int size);
void ff_rtp_send_h263(AVFormatContext *s1, const uint8_t *buf1, int size);
void ff_rtp_send_h263_rfc2190(AVFormatContext *s1, const uint8_t *buf1, int size,
                              const uint8_t *mb_info, int mb_info_size);
void ff_rtp_send_aac(AVFormatContext *s1, const uint8_t *buff, int size);
void ff_rtp_send_latm(AVFormatContext *s1, const uint8_t *buff, int size);
void ff_rtp_send_amr(AVFormatContext *s1, const uint8_t *buff, int size);
void ff_rtp_send_mpegvideo(AVFormatContext *s1, const uint8_t *buf1, int size);
void ff_rtp_send_xiph(AVFormatContext *s1, const uint8_t *buff, int size);
void ff_rtp_send_vp8(AVFormatContext *s1, const uint8_t *buff, int size);

const uint8_t *ff_h263_find_resync_marker_reverse(const uint8_t *restrict start,
                                                  const uint8_t *restrict end);

#endif /* AVFORMAT_RTPENC_H */
