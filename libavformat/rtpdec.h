/*
 * RTP demuxer definitions
 * Copyright (c) 2002 Fabrice Bellard
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
#ifndef AVFORMAT_RTPDEC_H
#define AVFORMAT_RTPDEC_H

#include "libavcodec/avcodec.h"
#include "avformat.h"
#include "rtp.h"

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

int rtp_get_codec_info(AVCodecContext *codec, int payload_type);

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
 * @param ctx RTSP demuxer context
 * @param s stream context
 * @param st stream that this packet belongs to
 * @param pkt packet in which to write the parsed data
 * @param timestamp pointer in which to write the timestamp of this RTP packet
 * @param buf pointer to raw RTP packet data
 * @param len length of buf
 * @param flags flags from the RTP packet header (PKT_FLAG_*)
 */
typedef int (*DynamicPayloadPacketHandlerProc) (AVFormatContext *ctx,
                                                PayloadContext *s,
                                                AVStream *st,
                                                AVPacket * pkt,
                                                uint32_t *timestamp,
                                                const uint8_t * buf,
                                                int len, int flags);

struct RTPDynamicProtocolHandler_s {
    // fields from AVRtpDynamicPayloadType_s
    const char enc_name[50];    /* XXX: still why 50 ? ;-) */
    enum CodecType codec_type;
    enum CodecID codec_id;

    // may be null
    int (*parse_sdp_a_line) (AVFormatContext *s,
                             int st_index,
                             PayloadContext *priv_data,
                             const char *line); ///< Parse the a= line from the sdp field
    PayloadContext *(*open) (); ///< allocate any data needed by the rtp parsing for this dynamic data.
    void (*close)(PayloadContext *protocol_data); ///< free any data needed by the rtp parsing for this dynamic data.
    DynamicPayloadPacketHandlerProc parse_packet; ///< parse handler for this dynamic packet.

    struct RTPDynamicProtocolHandler_s *next;
};

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
    RTPPayloadData *rtp_payload_data; // TODO: Move into dynamic payload handlers

    /* dynamic payload stuff */
    DynamicPayloadPacketHandlerProc parse_packet;     ///< This is also copied from the dynamic protocol handler structure
    PayloadContext *dynamic_protocol_context;        ///< This is a copy from the values setup from the sdp parsing, in rtsp.c don't free me.
    int max_frames_per_packet;
};

extern RTPDynamicProtocolHandler *RTPFirstDynamicPayloadHandler;
void ff_register_dynamic_payload_handler(RTPDynamicProtocolHandler *handler);

int rtsp_next_attr_and_value(const char **p, char *attr, int attr_size, char *value, int value_size); ///< from rtsp.c, but used by rtp dynamic protocol handlers.

const char *ff_rtp_enc_name(int payload_type);
enum CodecID ff_rtp_codec_id(const char *buf, enum CodecType codec_type);

void av_register_rtp_dynamic_payload_handlers(void);

#endif /* AVFORMAT_RTPDEC_H */
