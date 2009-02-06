/*
 * RTSP definitions
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
#ifndef FFMPEG_RTSP_H
#define FFMPEG_RTSP_H

#include <stdint.h>
#include "avformat.h"
#include "rtspcodes.h"
#include "rtpdec.h"
#include "network.h"

enum RTSPLowerTransport {
    RTSP_LOWER_TRANSPORT_UDP = 0,
    RTSP_LOWER_TRANSPORT_TCP = 1,
    RTSP_LOWER_TRANSPORT_UDP_MULTICAST = 2,
    /**
     * This is not part of public API and shouldn't be used outside of ffmpeg.
     */
    RTSP_LOWER_TRANSPORT_LAST
};

enum RTSPTransport {
    RTSP_TRANSPORT_RTP,
    RTSP_TRANSPORT_RDT,
    RTSP_TRANSPORT_LAST
};

#define RTSP_DEFAULT_PORT   554
#define RTSP_MAX_TRANSPORTS 8
#define RTSP_TCP_MAX_PACKET_SIZE 1472
#define RTSP_DEFAULT_NB_AUDIO_CHANNELS 2
#define RTSP_DEFAULT_AUDIO_SAMPLERATE 44100
#define RTSP_RTP_PORT_MIN 5000
#define RTSP_RTP_PORT_MAX 10000

typedef struct RTSPTransportField {
    int interleaved_min, interleaved_max;  /**< interleave ids, if TCP transport */
    int port_min, port_max; /**< RTP ports */
    int client_port_min, client_port_max; /**< RTP ports */
    int server_port_min, server_port_max; /**< RTP ports */
    int ttl; /**< ttl value */
    uint32_t destination; /**< destination IP address */
    enum RTSPTransport transport;
    enum RTSPLowerTransport lower_transport;
} RTSPTransportField;

typedef struct RTSPHeader {
    int content_length;
    enum RTSPStatusCode status_code; /**< response code from server */
    int nb_transports;
    /** in AV_TIME_BASE unit, AV_NOPTS_VALUE if not used */
    int64_t range_start, range_end;
    RTSPTransportField transports[RTSP_MAX_TRANSPORTS];
    int seq; /**< sequence number */
    char session_id[512];
    char real_challenge[64]; /**< the RealChallenge1 field from the server */
    char server[64];
} RTSPHeader;

enum RTSPClientState {
    RTSP_STATE_IDLE,
    RTSP_STATE_PLAYING,
    RTSP_STATE_PAUSED,
};

enum RTSPServerType {
    RTSP_SERVER_RTP,  /**< Standards-compliant RTP-server */
    RTSP_SERVER_REAL, /**< Realmedia-style server */
    RTSP_SERVER_WMS,  /**< Windows Media server */
    RTSP_SERVER_LAST
};

typedef struct RTSPState {
    URLContext *rtsp_hd; /* RTSP TCP connexion handle */
    int nb_rtsp_streams;
    struct RTSPStream **rtsp_streams;

    enum RTSPClientState state;
    int64_t seek_timestamp;

    /* XXX: currently we use unbuffered input */
    //    ByteIOContext rtsp_gb;
    int seq;        /* RTSP command sequence number */
    char session_id[512];
    enum RTSPTransport transport;
    enum RTSPLowerTransport lower_transport;
    enum RTSPServerType server_type;
    char last_reply[2048]; /* XXX: allocate ? */
    void *cur_transport_priv;
    int need_subscription;
    enum AVDiscard real_setup_cache[MAX_STREAMS];
    char last_subscription[1024];
} RTSPState;

typedef struct RTSPStream {
    URLContext *rtp_handle; /* RTP stream handle */
    void *transport_priv; /* RTP/RDT parse context */

    int stream_index; /* corresponding stream index, if any. -1 if none (MPEG2TS case) */
    int interleaved_min, interleaved_max;  /* interleave ids, if TCP transport */
    char control_url[1024]; /* url for this stream (from SDP) */

    int sdp_port; /* port (from SDP content - not used in RTSP) */
    struct in_addr sdp_ip; /* IP address  (from SDP content - not used in RTSP) */
    int sdp_ttl;  /* IP TTL (from SDP content - not used in RTSP) */
    int sdp_payload_type; /* payload type - only used in SDP */
    RTPPayloadData rtp_payload_data; /* rtp payload parsing infos from SDP */

    RTPDynamicProtocolHandler *dynamic_handler; ///< Only valid if it's a dynamic protocol. (This is the handler structure)
    PayloadContext *dynamic_protocol_context; ///< Only valid if it's a dynamic protocol. (This is any private data associated with the dynamic protocol)
} RTSPStream;

int rtsp_init(void);
void rtsp_parse_line(RTSPHeader *reply, const char *buf);

#if LIBAVFORMAT_VERSION_INT < (53 << 16)
extern int rtsp_default_protocols;
#endif
extern int rtsp_rtp_port_min;
extern int rtsp_rtp_port_max;

int rtsp_pause(AVFormatContext *s);
int rtsp_resume(AVFormatContext *s);

#endif /* FFMPEG_RTSP_H */
