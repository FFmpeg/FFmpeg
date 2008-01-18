/*
 * RTP definitions
 * Copyright (c) 2006 Ryan Martell <rdm4@martellventures.com>
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

// this is a bit of a misnomer, because rtp & rtsp internal structures and prototypes are in here.
#ifndef FFMPEG_RTP_INTERNAL_H
#define FFMPEG_RTP_INTERNAL_H

#include <stdint.h>
#include "avcodec.h"
#include "rtp.h"

// these statistics are used for rtcp receiver reports...
typedef struct {
    uint16_t max_seq;           ///< highest sequence number seen
    uint32_t cycles;            ///< shifted count of sequence number cycles
    uint32_t base_seq;          ///< base sequence number
    uint32_t bad_seq;           ///< last bad sequence number + 1
    int probation;              ///< sequence packets till source is valid
    int received;               ///< packets received
    int expected_prior;         ///< packets expected in last interval
    int received_prior;         ///< packets received in last interval
    uint32_t transit;           ///< relative transit time for previous packet
    uint32_t jitter;            ///< estimated jitter.
} RTPStatistics;

/**
 * Packet parsing for "private" payloads in the RTP specs.
 *
 * @param s stream context
 * @param pkt packet in which to write the parsed data
 * @param timestamp pointer in which to write the timestamp of this RTP packet
 * @param buf pointer to raw RTP packet data
 * @param len length of buf
 * @param flags flags from the RTP packet header (PKT_FLAG_*)
 */
typedef int (*DynamicPayloadPacketHandlerProc) (struct RTPDemuxContext * s,
                                                AVPacket * pkt,
                                                uint32_t *timestamp,
                                                const uint8_t * buf,
                                                int len, int flags);

typedef struct RTPDynamicProtocolHandler_s {
    // fields from AVRtpDynamicPayloadType_s
    const char enc_name[50];    /* XXX: still why 50 ? ;-) */
    enum CodecType codec_type;
    enum CodecID codec_id;

    // may be null
    int (*parse_sdp_a_line) (AVStream * stream,
                             void *protocol_data,
                             const char *line); ///< Parse the a= line from the sdp field
    void *(*open) (); ///< allocate any data needed by the rtp parsing for this dynamic data.
    void (*close)(void *protocol_data); ///< free any data needed by the rtp parsing for this dynamic data.
    DynamicPayloadPacketHandlerProc parse_packet; ///< parse handler for this dynamic packet.

    struct RTPDynamicProtocolHandler_s *next;
} RTPDynamicProtocolHandler;

// moved out of rtp.c, because the h264 decoder needs to know about this structure..
struct RTPDemuxContext {
    AVFormatContext *ic;
    AVStream *st;
    int payload_type;
    uint32_t ssrc;
    uint16_t seq;
    uint32_t timestamp;
    uint32_t base_timestamp;
    uint32_t cur_timestamp;
    int max_payload_size;
    struct MpegTSContext *ts;   /* only used for MP2T payloads */
    int read_buf_index;
    int read_buf_size;
    /* used to send back RTCP RR */
    URLContext *rtp_ctx;
    char hostname[256];

    RTPStatistics statistics; ///< Statistics for this stream (used by RTCP receiver reports)

    /* rtcp sender statistics receive */
    int64_t last_rtcp_ntp_time;    // TODO: move into statistics
    int64_t first_rtcp_ntp_time;   // TODO: move into statistics
    uint32_t last_rtcp_timestamp;  // TODO: move into statistics

    /* rtcp sender statistics */
    unsigned int packet_count;     // TODO: move into statistics (outgoing)
    unsigned int octet_count;      // TODO: move into statistics (outgoing)
    unsigned int last_octet_count; // TODO: move into statistics (outgoing)
    int first_packet;
    /* buffer for output */
    uint8_t buf[RTP_MAX_PACKET_LENGTH];
    uint8_t *buf_ptr;

    /* special infos for au headers parsing */
    rtp_payload_data_t *rtp_payload_data; // TODO: Move into dynamic payload handlers

    /* dynamic payload stuff */
    DynamicPayloadPacketHandlerProc parse_packet;     ///< This is also copied from the dynamic protocol handler structure
    void *dynamic_protocol_context;        ///< This is a copy from the values setup from the sdp parsing, in rtsp.c don't free me.
    int max_frames_per_packet;
};

extern RTPDynamicProtocolHandler *RTPFirstDynamicPayloadHandler;

int rtsp_next_attr_and_value(const char **p, char *attr, int attr_size, char *value, int value_size); ///< from rtsp.c, but used by rtp dynamic protocol handlers.

void ff_rtp_send_data(AVFormatContext *s1, const uint8_t *buf1, int len, int m);
const char *ff_rtp_enc_name(int payload_type);
enum CodecID ff_rtp_codec_id(const char *buf, enum CodecType codec_type);

void av_register_rtp_dynamic_payload_handlers(void);

#endif /* FFMPEG_RTP_INTERNAL_H */

