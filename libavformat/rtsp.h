/*
 * RTSP definitions
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
#ifndef FFMPEG_RTSP_H
#define FFMPEG_RTSP_H

#include <stdint.h>
#include "avformat.h"
#include "rtspcodes.h"

enum RTSPProtocol {
    RTSP_PROTOCOL_RTP_UDP = 0,
    RTSP_PROTOCOL_RTP_TCP = 1,
    RTSP_PROTOCOL_RTP_UDP_MULTICAST = 2,
    /**
     * This is not part of public API and shouldn't be used outside of ffmpeg.
     */
    RTSP_PROTOCOL_RTP_LAST
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
    enum RTSPProtocol protocol;
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
} RTSPHeader;

/** the callback can be used to extend the connection setup/teardown step */
enum RTSPCallbackAction {
    RTSP_ACTION_SERVER_SETUP,
    RTSP_ACTION_SERVER_TEARDOWN,
    RTSP_ACTION_CLIENT_SETUP,
    RTSP_ACTION_CLIENT_TEARDOWN,
};

typedef struct RTSPActionServerSetup {
    uint32_t ipaddr;
    char transport_option[512];
} RTSPActionServerSetup;

typedef int FFRTSPCallback(enum RTSPCallbackAction action,
                           const char *session_id,
                           char *buf, int buf_size,
                           void *arg);

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
