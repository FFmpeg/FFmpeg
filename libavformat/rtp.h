/*
 * RTP definitions
 * Copyright (c) 2002 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef RTP_H
#define RTP_H

enum RTPPayloadType {
    RTP_PT_ULAW = 0,
    RTP_PT_GSM = 3,
    RTP_PT_G723 = 4,
    RTP_PT_ALAW = 8,
    RTP_PT_S16BE_STEREO = 10,
    RTP_PT_S16BE_MONO = 11,
    RTP_PT_MPEGAUDIO = 14,
    RTP_PT_JPEG = 26,
    RTP_PT_H261 = 31,
    RTP_PT_MPEGVIDEO = 32,
    RTP_PT_MPEG2TS = 33,
    RTP_PT_H263 = 34, /* old H263 encapsulation */
    RTP_PT_PRIVATE = 96,
};

#define RTP_MIN_PACKET_LENGTH 12
#define RTP_MAX_PACKET_LENGTH 1500 /* XXX: suppress this define */

int rtp_init(void);
int rtp_get_codec_info(AVCodecContext *codec, int payload_type);
int rtp_get_payload_type(AVCodecContext *codec);

typedef struct RTPDemuxContext RTPDemuxContext;

RTPDemuxContext *rtp_parse_open(AVFormatContext *s1, AVStream *st, int payload_type);
int rtp_parse_packet(RTPDemuxContext *s, AVPacket *pkt, 
                     const uint8_t *buf, int len);
void rtp_parse_close(RTPDemuxContext *s);

extern AVOutputFormat rtp_mux;
extern AVInputFormat rtp_demux;

int rtp_get_local_port(URLContext *h);
int rtp_set_remote_url(URLContext *h, const char *uri);
void rtp_get_file_handles(URLContext *h, int *prtp_fd, int *prtcp_fd);

extern URLProtocol rtp_protocol;

#endif /* RTP_H */
