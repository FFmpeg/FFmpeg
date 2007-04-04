/*
 * RTP input/output format
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
#include "avformat.h"
#include "mpegts.h"
#include "bitstream.h"

#include <unistd.h>
#include "network.h"

#include "rtp_internal.h"
#include "rtp_h264.h"

//#define DEBUG


/* TODO: - add RTCP statistics reporting (should be optional).

         - add support for h263/mpeg4 packetized output : IDEA: send a
         buffer to 'rtp_write_packet' contains all the packets for ONE
         frame. Each packet should have a four byte header containing
         the length in big endian format (same trick as
         'url_open_dyn_packet_buf')
*/

/* from http://www.iana.org/assignments/rtp-parameters last updated 05 January 2005 */
AVRtpPayloadType_t AVRtpPayloadTypes[]=
{
  {0, "PCMU",        CODEC_TYPE_AUDIO,   CODEC_ID_PCM_MULAW, 8000, 1},
  {1, "Reserved",    CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {2, "Reserved",    CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {3, "GSM",         CODEC_TYPE_AUDIO,   CODEC_ID_NONE, 8000, 1},
  {4, "G723",        CODEC_TYPE_AUDIO,   CODEC_ID_NONE, 8000, 1},
  {5, "DVI4",        CODEC_TYPE_AUDIO,   CODEC_ID_NONE, 8000, 1},
  {6, "DVI4",        CODEC_TYPE_AUDIO,   CODEC_ID_NONE, 16000, 1},
  {7, "LPC",         CODEC_TYPE_AUDIO,   CODEC_ID_NONE, 8000, 1},
  {8, "PCMA",        CODEC_TYPE_AUDIO,   CODEC_ID_PCM_ALAW, 8000, 1},
  {9, "G722",        CODEC_TYPE_AUDIO,   CODEC_ID_NONE, 8000, 1},
  {10, "L16",        CODEC_TYPE_AUDIO,   CODEC_ID_PCM_S16BE, 44100, 2},
  {11, "L16",        CODEC_TYPE_AUDIO,   CODEC_ID_PCM_S16BE, 44100, 1},
  {12, "QCELP",      CODEC_TYPE_AUDIO,   CODEC_ID_QCELP, 8000, 1},
  {13, "CN",         CODEC_TYPE_AUDIO,   CODEC_ID_NONE, 8000, 1},
  {14, "MPA",        CODEC_TYPE_AUDIO,   CODEC_ID_MP2, 90000, -1},
  {15, "G728",       CODEC_TYPE_AUDIO,   CODEC_ID_NONE, 8000, 1},
  {16, "DVI4",       CODEC_TYPE_AUDIO,   CODEC_ID_NONE, 11025, 1},
  {17, "DVI4",       CODEC_TYPE_AUDIO,   CODEC_ID_NONE, 22050, 1},
  {18, "G729",       CODEC_TYPE_AUDIO,   CODEC_ID_NONE, 8000, 1},
  {19, "reserved",   CODEC_TYPE_AUDIO,   CODEC_ID_NONE, -1, -1},
  {20, "unassigned", CODEC_TYPE_AUDIO,   CODEC_ID_NONE, -1, -1},
  {21, "unassigned", CODEC_TYPE_AUDIO,   CODEC_ID_NONE, -1, -1},
  {22, "unassigned", CODEC_TYPE_AUDIO,   CODEC_ID_NONE, -1, -1},
  {23, "unassigned", CODEC_TYPE_AUDIO,   CODEC_ID_NONE, -1, -1},
  {24, "unassigned", CODEC_TYPE_VIDEO,   CODEC_ID_NONE, -1, -1},
  {25, "CelB",       CODEC_TYPE_VIDEO,   CODEC_ID_NONE, 90000, -1},
  {26, "JPEG",       CODEC_TYPE_VIDEO,   CODEC_ID_MJPEG, 90000, -1},
  {27, "unassigned", CODEC_TYPE_VIDEO,   CODEC_ID_NONE, -1, -1},
  {28, "nv",         CODEC_TYPE_VIDEO,   CODEC_ID_NONE, 90000, -1},
  {29, "unassigned", CODEC_TYPE_VIDEO,   CODEC_ID_NONE, -1, -1},
  {30, "unassigned", CODEC_TYPE_VIDEO,   CODEC_ID_NONE, -1, -1},
  {31, "H261",       CODEC_TYPE_VIDEO,   CODEC_ID_H261, 90000, -1},
  {32, "MPV",        CODEC_TYPE_VIDEO,   CODEC_ID_MPEG1VIDEO, 90000, -1},
  {33, "MP2T",       CODEC_TYPE_DATA,    CODEC_ID_MPEG2TS, 90000, -1},
  {34, "H263",       CODEC_TYPE_VIDEO,   CODEC_ID_H263, 90000, -1},
  {35, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {36, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {37, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {38, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {39, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {40, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {41, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {42, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {43, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {44, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {45, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {46, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {47, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {48, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {49, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {50, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {51, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {52, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {53, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {54, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {55, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {56, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {57, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {58, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {59, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {60, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {61, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {62, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {63, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {64, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {65, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {66, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {67, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {68, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {69, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {70, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {71, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {72, "reserved for RTCP conflict avoidance", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {73, "reserved for RTCP conflict avoidance", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {74, "reserved for RTCP conflict avoidance", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {75, "reserved for RTCP conflict avoidance", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {76, "reserved for RTCP conflict avoidance", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {77, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {78, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {79, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {80, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {81, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {82, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {83, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {84, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {85, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {86, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {87, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {88, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {89, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {90, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {91, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {92, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {93, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {94, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {95, "unassigned", CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {96, "dynamic",    CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {97, "dynamic",    CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {98, "dynamic",    CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {99, "dynamic",    CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {100, "dynamic",   CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {101, "dynamic",   CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {102, "dynamic",   CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {103, "dynamic",   CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {104, "dynamic",   CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {105, "dynamic",   CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {106, "dynamic",   CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {107, "dynamic",   CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {108, "dynamic",   CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {109, "dynamic",   CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {110, "dynamic",   CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {111, "dynamic",   CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {112, "dynamic",   CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {113, "dynamic",   CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {114, "dynamic",   CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {115, "dynamic",   CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {116, "dynamic",   CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {117, "dynamic",   CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {118, "dynamic",   CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {119, "dynamic",   CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {120, "dynamic",   CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {121, "dynamic",   CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {122, "dynamic",   CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {123, "dynamic",   CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {124, "dynamic",   CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {125, "dynamic",   CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {126, "dynamic",   CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {127, "dynamic",   CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1},
  {-1, "",           CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1}
};

/* statistics functions */
RTPDynamicProtocolHandler *RTPFirstDynamicPayloadHandler= NULL;

static RTPDynamicProtocolHandler mp4v_es_handler= {"MP4V-ES", CODEC_TYPE_VIDEO, CODEC_ID_MPEG4};
static RTPDynamicProtocolHandler mpeg4_generic_handler= {"mpeg4-generic", CODEC_TYPE_AUDIO, CODEC_ID_AAC};

static void register_dynamic_payload_handler(RTPDynamicProtocolHandler *handler)
{
    handler->next= RTPFirstDynamicPayloadHandler;
    RTPFirstDynamicPayloadHandler= handler;
}

void av_register_rtp_dynamic_payload_handlers(void)
{
    register_dynamic_payload_handler(&mp4v_es_handler);
    register_dynamic_payload_handler(&mpeg4_generic_handler);
    register_dynamic_payload_handler(&ff_h264_dynamic_handler);
}

int rtp_get_codec_info(AVCodecContext *codec, int payload_type)
{
    if (AVRtpPayloadTypes[payload_type].codec_id != CODEC_ID_NONE) {
        codec->codec_type = AVRtpPayloadTypes[payload_type].codec_type;
        codec->codec_id = AVRtpPayloadTypes[payload_type].codec_id;
        if (AVRtpPayloadTypes[payload_type].audio_channels > 0)
            codec->channels = AVRtpPayloadTypes[payload_type].audio_channels;
        if (AVRtpPayloadTypes[payload_type].clock_rate > 0)
            codec->sample_rate = AVRtpPayloadTypes[payload_type].clock_rate;
        return 0;
    }
    return -1;
}

int rtp_get_payload_type(AVCodecContext *codec)
{
    int i, payload_type;

    /* compute the payload type */
    for (payload_type = -1, i = 0; AVRtpPayloadTypes[i].pt >= 0; ++i)
        if (AVRtpPayloadTypes[i].codec_id == codec->codec_id) {
            if (codec->codec_id == CODEC_ID_PCM_S16BE)
                if (codec->channels != AVRtpPayloadTypes[i].audio_channels)
                    continue;
            payload_type = AVRtpPayloadTypes[i].pt;
        }
    return payload_type;
}

static inline uint32_t decode_be32(const uint8_t *p)
{
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static inline uint64_t decode_be64(const uint8_t *p)
{
    return ((uint64_t)decode_be32(p) << 32) | decode_be32(p + 4);
}

static int rtcp_parse_packet(RTPDemuxContext *s, const unsigned char *buf, int len)
{
    if (buf[1] != 200)
        return -1;
    s->last_rtcp_ntp_time = decode_be64(buf + 8);
    if (s->first_rtcp_ntp_time == AV_NOPTS_VALUE)
        s->first_rtcp_ntp_time = s->last_rtcp_ntp_time;
    s->last_rtcp_timestamp = decode_be32(buf + 16);
    return 0;
}

#define RTP_SEQ_MOD (1<<16)

/**
* called on parse open packet
*/
static void rtp_init_statistics(RTPStatistics *s, uint16_t base_sequence) // called on parse open packet.
{
    memset(s, 0, sizeof(RTPStatistics));
    s->max_seq= base_sequence;
    s->probation= 1;
}

/**
* called whenever there is a large jump in sequence numbers, or when they get out of probation...
*/
static void rtp_init_sequence(RTPStatistics *s, uint16_t seq)
{
    s->max_seq= seq;
    s->cycles= 0;
    s->base_seq= seq -1;
    s->bad_seq= RTP_SEQ_MOD + 1;
    s->received= 0;
    s->expected_prior= 0;
    s->received_prior= 0;
    s->jitter= 0;
    s->transit= 0;
}

/**
* returns 1 if we should handle this packet.
*/
static int rtp_valid_packet_in_sequence(RTPStatistics *s, uint16_t seq)
{
    uint16_t udelta= seq - s->max_seq;
    const int MAX_DROPOUT= 3000;
    const int MAX_MISORDER = 100;
    const int MIN_SEQUENTIAL = 2;

    /* source not valid until MIN_SEQUENTIAL packets with sequence seq. numbers have been received */
    if(s->probation)
    {
        if(seq==s->max_seq + 1) {
            s->probation--;
            s->max_seq= seq;
            if(s->probation==0) {
                rtp_init_sequence(s, seq);
                s->received++;
                return 1;
            }
        } else {
            s->probation= MIN_SEQUENTIAL - 1;
            s->max_seq = seq;
        }
    } else if (udelta < MAX_DROPOUT) {
        // in order, with permissible gap
        if(seq < s->max_seq) {
            //sequence number wrapped; count antother 64k cycles
            s->cycles += RTP_SEQ_MOD;
        }
        s->max_seq= seq;
    } else if (udelta <= RTP_SEQ_MOD - MAX_MISORDER) {
        // sequence made a large jump...
        if(seq==s->bad_seq) {
            // two sequential packets-- assume that the other side restarted without telling us; just resync.
            rtp_init_sequence(s, seq);
        } else {
            s->bad_seq= (seq + 1) & (RTP_SEQ_MOD-1);
            return 0;
        }
    } else {
        // duplicate or reordered packet...
    }
    s->received++;
    return 1;
}

#if 0
/**
* This function is currently unused; without a valid local ntp time, I don't see how we could calculate the
* difference between the arrival and sent timestamp.  As a result, the jitter and transit statistics values
* never change.  I left this in in case someone else can see a way. (rdm)
*/
static void rtcp_update_jitter(RTPStatistics *s, uint32_t sent_timestamp, uint32_t arrival_timestamp)
{
    uint32_t transit= arrival_timestamp - sent_timestamp;
    int d;
    s->transit= transit;
    d= FFABS(transit - s->transit);
    s->jitter += d - ((s->jitter + 8)>>4);
}
#endif

int rtp_check_and_send_back_rr(RTPDemuxContext *s, int count)
{
    ByteIOContext pb;
    uint8_t *buf;
    int len;
    int rtcp_bytes;
    RTPStatistics *stats= &s->statistics;
    uint32_t lost;
    uint32_t extended_max;
    uint32_t expected_interval;
    uint32_t received_interval;
    uint32_t lost_interval;
    uint32_t expected;
    uint32_t fraction;
    uint64_t ntp_time= s->last_rtcp_ntp_time; // TODO: Get local ntp time?

    if (!s->rtp_ctx || (count < 1))
        return -1;

    /* TODO: I think this is way too often; RFC 1889 has algorithm for this */
    /* XXX: mpeg pts hardcoded. RTCP send every 0.5 seconds */
    s->octet_count += count;
    rtcp_bytes = ((s->octet_count - s->last_octet_count) * RTCP_TX_RATIO_NUM) /
        RTCP_TX_RATIO_DEN;
    rtcp_bytes /= 50; // mmu_man: that's enough for me... VLC sends much less btw !?
    if (rtcp_bytes < 28)
        return -1;
    s->last_octet_count = s->octet_count;

    if (url_open_dyn_buf(&pb) < 0)
        return -1;

    // Receiver Report
    put_byte(&pb, (RTP_VERSION << 6) + 1); /* 1 report block */
    put_byte(&pb, 201);
    put_be16(&pb, 7); /* length in words - 1 */
    put_be32(&pb, s->ssrc); // our own SSRC
    put_be32(&pb, s->ssrc); // XXX: should be the server's here!
    // some placeholders we should really fill...
    // RFC 1889/p64
    extended_max= stats->cycles + stats->max_seq;
    expected= extended_max - stats->base_seq + 1;
    lost= expected - stats->received;
    lost= FFMIN(lost, 0xffffff); // clamp it since it's only 24 bits...
    expected_interval= expected - stats->expected_prior;
    stats->expected_prior= expected;
    received_interval= stats->received - stats->received_prior;
    stats->received_prior= stats->received;
    lost_interval= expected_interval - received_interval;
    if (expected_interval==0 || lost_interval<=0) fraction= 0;
    else fraction = (lost_interval<<8)/expected_interval;

    fraction= (fraction<<24) | lost;

    put_be32(&pb, fraction); /* 8 bits of fraction, 24 bits of total packets lost */
    put_be32(&pb, extended_max); /* max sequence received */
    put_be32(&pb, stats->jitter>>4); /* jitter */

    if(s->last_rtcp_ntp_time==AV_NOPTS_VALUE)
    {
        put_be32(&pb, 0); /* last SR timestamp */
        put_be32(&pb, 0); /* delay since last SR */
    } else {
        uint32_t middle_32_bits= s->last_rtcp_ntp_time>>16; // this is valid, right? do we need to handle 64 bit values special?
        uint32_t delay_since_last= ntp_time - s->last_rtcp_ntp_time;

        put_be32(&pb, middle_32_bits); /* last SR timestamp */
        put_be32(&pb, delay_since_last); /* delay since last SR */
    }

    // CNAME
    put_byte(&pb, (RTP_VERSION << 6) + 1); /* 1 report block */
    put_byte(&pb, 202);
    len = strlen(s->hostname);
    put_be16(&pb, (6 + len + 3) / 4); /* length in words - 1 */
    put_be32(&pb, s->ssrc);
    put_byte(&pb, 0x01);
    put_byte(&pb, len);
    put_buffer(&pb, s->hostname, len);
    // padding
    for (len = (6 + len) % 4; len % 4; len++) {
        put_byte(&pb, 0);
    }

    put_flush_packet(&pb);
    len = url_close_dyn_buf(&pb, &buf);
    if ((len > 0) && buf) {
        int result;
#if defined(DEBUG)
        printf("sending %d bytes of RR\n", len);
#endif
        result= url_write(s->rtp_ctx, buf, len);
#if defined(DEBUG)
        printf("result from url_write: %d\n", result);
#endif
        av_free(buf);
    }
    return 0;
}

/**
 * open a new RTP parse context for stream 'st'. 'st' can be NULL for
 * MPEG2TS streams to indicate that they should be demuxed inside the
 * rtp demux (otherwise CODEC_ID_MPEG2TS packets are returned)
 * TODO: change this to not take rtp_payload data, and use the new dynamic payload system.
 */
RTPDemuxContext *rtp_parse_open(AVFormatContext *s1, AVStream *st, URLContext *rtpc, int payload_type, rtp_payload_data_t *rtp_payload_data)
{
    RTPDemuxContext *s;

    s = av_mallocz(sizeof(RTPDemuxContext));
    if (!s)
        return NULL;
    s->payload_type = payload_type;
    s->last_rtcp_ntp_time = AV_NOPTS_VALUE;
    s->first_rtcp_ntp_time = AV_NOPTS_VALUE;
    s->ic = s1;
    s->st = st;
    s->rtp_payload_data = rtp_payload_data;
    rtp_init_statistics(&s->statistics, 0); // do we know the initial sequence from sdp?
    if (!strcmp(AVRtpPayloadTypes[payload_type].enc_name, "MP2T")) {
        s->ts = mpegts_parse_open(s->ic);
        if (s->ts == NULL) {
            av_free(s);
            return NULL;
        }
    } else {
        switch(st->codec->codec_id) {
        case CODEC_ID_MPEG1VIDEO:
        case CODEC_ID_MPEG2VIDEO:
        case CODEC_ID_MP2:
        case CODEC_ID_MP3:
        case CODEC_ID_MPEG4:
        case CODEC_ID_H264:
            st->need_parsing = 1;
            break;
        default:
            break;
        }
    }
    // needed to send back RTCP RR in RTSP sessions
    s->rtp_ctx = rtpc;
    gethostname(s->hostname, sizeof(s->hostname));
    return s;
}

static int rtp_parse_mp4_au(RTPDemuxContext *s, const uint8_t *buf)
{
    int au_headers_length, au_header_size, i;
    GetBitContext getbitcontext;
    rtp_payload_data_t *infos;

    infos = s->rtp_payload_data;

    if (infos == NULL)
        return -1;

    /* decode the first 2 bytes where are stored the AUHeader sections
       length in bits */
    au_headers_length = AV_RB16(buf);

    if (au_headers_length > RTP_MAX_PACKET_LENGTH)
      return -1;

    infos->au_headers_length_bytes = (au_headers_length + 7) / 8;

    /* skip AU headers length section (2 bytes) */
    buf += 2;

    init_get_bits(&getbitcontext, buf, infos->au_headers_length_bytes * 8);

    /* XXX: Wrong if optionnal additional sections are present (cts, dts etc...) */
    au_header_size = infos->sizelength + infos->indexlength;
    if (au_header_size <= 0 || (au_headers_length % au_header_size != 0))
        return -1;

    infos->nb_au_headers = au_headers_length / au_header_size;
    infos->au_headers = av_malloc(sizeof(struct AUHeaders) * infos->nb_au_headers);

    /* XXX: We handle multiple AU Section as only one (need to fix this for interleaving)
       In my test, the faad decoder doesnt behave correctly when sending each AU one by one
       but does when sending the whole as one big packet...  */
    infos->au_headers[0].size = 0;
    infos->au_headers[0].index = 0;
    for (i = 0; i < infos->nb_au_headers; ++i) {
        infos->au_headers[0].size += get_bits_long(&getbitcontext, infos->sizelength);
        infos->au_headers[0].index = get_bits_long(&getbitcontext, infos->indexlength);
    }

    infos->nb_au_headers = 1;

    return 0;
}

/**
 * This was the second switch in rtp_parse packet.  Normalizes time, if required, sets stream_index, etc.
 */
static void finalize_packet(RTPDemuxContext *s, AVPacket *pkt, uint32_t timestamp)
{
    switch(s->st->codec->codec_id) {
        case CODEC_ID_MP2:
        case CODEC_ID_MPEG1VIDEO:
            if (s->last_rtcp_ntp_time != AV_NOPTS_VALUE) {
                int64_t addend;

                int delta_timestamp;
                /* XXX: is it really necessary to unify the timestamp base ? */
                /* compute pts from timestamp with received ntp_time */
                delta_timestamp = timestamp - s->last_rtcp_timestamp;
                /* convert to 90 kHz without overflow */
                addend = (s->last_rtcp_ntp_time - s->first_rtcp_ntp_time) >> 14;
                addend = (addend * 5625) >> 14;
                pkt->pts = addend + delta_timestamp;
            }
            break;
        case CODEC_ID_AAC:
        case CODEC_ID_H264:
        case CODEC_ID_MPEG4:
            pkt->pts = timestamp;
            break;
        default:
            /* no timestamp info yet */
            break;
    }
    pkt->stream_index = s->st->index;
}

/**
 * Parse an RTP or RTCP packet directly sent as a buffer.
 * @param s RTP parse context.
 * @param pkt returned packet
 * @param buf input buffer or NULL to read the next packets
 * @param len buffer len
 * @return 0 if a packet is returned, 1 if a packet is returned and more can follow
 * (use buf as NULL to read the next). -1 if no packet (error or no more packet).
 */
int rtp_parse_packet(RTPDemuxContext *s, AVPacket *pkt,
                     const uint8_t *buf, int len)
{
    unsigned int ssrc, h;
    int payload_type, seq, ret;
    AVStream *st;
    uint32_t timestamp;
    int rv= 0;

    if (!buf) {
        /* return the next packets, if any */
        if(s->st && s->parse_packet) {
            timestamp= 0; ///< Should not be used if buf is NULL, but should be set to the timestamp of the packet returned....
            rv= s->parse_packet(s, pkt, &timestamp, NULL, 0);
            finalize_packet(s, pkt, timestamp);
            return rv;
        } else {
            // TODO: Move to a dynamic packet handler (like above)
            if (s->read_buf_index >= s->read_buf_size)
                return -1;
            ret = mpegts_parse_packet(s->ts, pkt, s->buf + s->read_buf_index,
                                      s->read_buf_size - s->read_buf_index);
            if (ret < 0)
                return -1;
            s->read_buf_index += ret;
            if (s->read_buf_index < s->read_buf_size)
                return 1;
            else
                return 0;
        }
    }

    if (len < 12)
        return -1;

    if ((buf[0] & 0xc0) != (RTP_VERSION << 6))
        return -1;
    if (buf[1] >= 200 && buf[1] <= 204) {
        rtcp_parse_packet(s, buf, len);
        return -1;
    }
    payload_type = buf[1] & 0x7f;
    seq  = (buf[2] << 8) | buf[3];
    timestamp = decode_be32(buf + 4);
    ssrc = decode_be32(buf + 8);
    /* store the ssrc in the RTPDemuxContext */
    s->ssrc = ssrc;

    /* NOTE: we can handle only one payload type */
    if (s->payload_type != payload_type)
        return -1;

    st = s->st;
    // only do something with this if all the rtp checks pass...
    if(!rtp_valid_packet_in_sequence(&s->statistics, seq))
    {
        av_log(st?st->codec:NULL, AV_LOG_ERROR, "RTP: PT=%02x: bad cseq %04x expected=%04x\n",
               payload_type, seq, ((s->seq + 1) & 0xffff));
        return -1;
    }

    s->seq = seq;
    len -= 12;
    buf += 12;

    if (!st) {
        /* specific MPEG2TS demux support */
        ret = mpegts_parse_packet(s->ts, pkt, buf, len);
        if (ret < 0)
            return -1;
        if (ret < len) {
            s->read_buf_size = len - ret;
            memcpy(s->buf, buf + ret, s->read_buf_size);
            s->read_buf_index = 0;
            return 1;
        }
    } else {
        // at this point, the RTP header has been stripped;  This is ASSUMING that there is only 1 CSRC, which in't wise.
        switch(st->codec->codec_id) {
        case CODEC_ID_MP2:
            /* better than nothing: skip mpeg audio RTP header */
            if (len <= 4)
                return -1;
            h = decode_be32(buf);
            len -= 4;
            buf += 4;
            av_new_packet(pkt, len);
            memcpy(pkt->data, buf, len);
            break;
        case CODEC_ID_MPEG1VIDEO:
            /* better than nothing: skip mpeg video RTP header */
            if (len <= 4)
                return -1;
            h = decode_be32(buf);
            buf += 4;
            len -= 4;
            if (h & (1 << 26)) {
                /* mpeg2 */
                if (len <= 4)
                    return -1;
                buf += 4;
                len -= 4;
            }
            av_new_packet(pkt, len);
            memcpy(pkt->data, buf, len);
            break;
            // moved from below, verbatim.  this is because this section handles packets, and the lower switch handles
            // timestamps.
            // TODO: Put this into a dynamic packet handler...
        case CODEC_ID_AAC:
            if (rtp_parse_mp4_au(s, buf))
                return -1;
            {
                rtp_payload_data_t *infos = s->rtp_payload_data;
                if (infos == NULL)
                    return -1;
                buf += infos->au_headers_length_bytes + 2;
                len -= infos->au_headers_length_bytes + 2;

                /* XXX: Fixme we only handle the case where rtp_parse_mp4_au define
                    one au_header */
                av_new_packet(pkt, infos->au_headers[0].size);
                memcpy(pkt->data, buf, infos->au_headers[0].size);
                buf += infos->au_headers[0].size;
                len -= infos->au_headers[0].size;
            }
            s->read_buf_size = len;
            s->buf_ptr = buf;
            rv= 0;
            break;
        default:
            if(s->parse_packet) {
                rv= s->parse_packet(s, pkt, &timestamp, buf, len);
            } else {
                av_new_packet(pkt, len);
                memcpy(pkt->data, buf, len);
            }
            break;
        }

        // now perform timestamp things....
        finalize_packet(s, pkt, timestamp);
    }
    return rv;
}

void rtp_parse_close(RTPDemuxContext *s)
{
    // TODO: fold this into the protocol specific data fields.
    if (!strcmp(AVRtpPayloadTypes[s->payload_type].enc_name, "MP2T")) {
        mpegts_parse_close(s->ts);
    }
    av_free(s);
}

/* rtp output */

static int rtp_write_header(AVFormatContext *s1)
{
    RTPDemuxContext *s = s1->priv_data;
    int payload_type, max_packet_size, n;
    AVStream *st;

    if (s1->nb_streams != 1)
        return -1;
    st = s1->streams[0];

    payload_type = rtp_get_payload_type(st->codec);
    if (payload_type < 0)
        payload_type = RTP_PT_PRIVATE; /* private payload type */
    s->payload_type = payload_type;

// following 2 FIXMies could be set based on the current time, theres normaly no info leak, as rtp will likely be transmitted immedeatly
    s->base_timestamp = 0; /* FIXME: was random(), what should this be? */
    s->timestamp = s->base_timestamp;
    s->ssrc = 0; /* FIXME: was random(), what should this be? */
    s->first_packet = 1;

    max_packet_size = url_fget_max_packet_size(&s1->pb);
    if (max_packet_size <= 12)
        return AVERROR_IO;
    s->max_payload_size = max_packet_size - 12;

    switch(st->codec->codec_id) {
    case CODEC_ID_MP2:
    case CODEC_ID_MP3:
        s->buf_ptr = s->buf + 4;
        s->cur_timestamp = 0;
        break;
    case CODEC_ID_MPEG1VIDEO:
        s->cur_timestamp = 0;
        break;
    case CODEC_ID_MPEG2TS:
        n = s->max_payload_size / TS_PACKET_SIZE;
        if (n < 1)
            n = 1;
        s->max_payload_size = n * TS_PACKET_SIZE;
        s->buf_ptr = s->buf;
        break;
    default:
        s->buf_ptr = s->buf;
        break;
    }

    return 0;
}

/* send an rtcp sender report packet */
static void rtcp_send_sr(AVFormatContext *s1, int64_t ntp_time)
{
    RTPDemuxContext *s = s1->priv_data;
#if defined(DEBUG)
    printf("RTCP: %02x %"PRIx64" %x\n", s->payload_type, ntp_time, s->timestamp);
#endif
    put_byte(&s1->pb, (RTP_VERSION << 6));
    put_byte(&s1->pb, 200);
    put_be16(&s1->pb, 6); /* length in words - 1 */
    put_be32(&s1->pb, s->ssrc);
    put_be64(&s1->pb, ntp_time);
    put_be32(&s1->pb, s->timestamp);
    put_be32(&s1->pb, s->packet_count);
    put_be32(&s1->pb, s->octet_count);
    put_flush_packet(&s1->pb);
}

/* send an rtp packet. sequence number is incremented, but the caller
   must update the timestamp itself */
static void rtp_send_data(AVFormatContext *s1, const uint8_t *buf1, int len, int m)
{
    RTPDemuxContext *s = s1->priv_data;

#ifdef DEBUG
    printf("rtp_send_data size=%d\n", len);
#endif

    /* build the RTP header */
    put_byte(&s1->pb, (RTP_VERSION << 6));
    put_byte(&s1->pb, (s->payload_type & 0x7f) | ((m & 0x01) << 7));
    put_be16(&s1->pb, s->seq);
    put_be32(&s1->pb, s->timestamp);
    put_be32(&s1->pb, s->ssrc);

    put_buffer(&s1->pb, buf1, len);
    put_flush_packet(&s1->pb);

    s->seq++;
    s->octet_count += len;
    s->packet_count++;
}

/* send an integer number of samples and compute time stamp and fill
   the rtp send buffer before sending. */
static void rtp_send_samples(AVFormatContext *s1,
                             const uint8_t *buf1, int size, int sample_size)
{
    RTPDemuxContext *s = s1->priv_data;
    int len, max_packet_size, n;

    max_packet_size = (s->max_payload_size / sample_size) * sample_size;
    /* not needed, but who nows */
    if ((size % sample_size) != 0)
        av_abort();
    while (size > 0) {
        len = (max_packet_size - (s->buf_ptr - s->buf));
        if (len > size)
            len = size;

        /* copy data */
        memcpy(s->buf_ptr, buf1, len);
        s->buf_ptr += len;
        buf1 += len;
        size -= len;
        n = (s->buf_ptr - s->buf);
        /* if buffer full, then send it */
        if (n >= max_packet_size) {
            rtp_send_data(s1, s->buf, n, 0);
            s->buf_ptr = s->buf;
            /* update timestamp */
            s->timestamp += n / sample_size;
        }
    }
}

/* NOTE: we suppose that exactly one frame is given as argument here */
/* XXX: test it */
static void rtp_send_mpegaudio(AVFormatContext *s1,
                               const uint8_t *buf1, int size)
{
    RTPDemuxContext *s = s1->priv_data;
    AVStream *st = s1->streams[0];
    int len, count, max_packet_size;

    max_packet_size = s->max_payload_size;

    /* test if we must flush because not enough space */
    len = (s->buf_ptr - s->buf);
    if ((len + size) > max_packet_size) {
        if (len > 4) {
            rtp_send_data(s1, s->buf, s->buf_ptr - s->buf, 0);
            s->buf_ptr = s->buf + 4;
            /* 90 KHz time stamp */
            s->timestamp = s->base_timestamp +
                (s->cur_timestamp * 90000LL) / st->codec->sample_rate;
        }
    }

    /* add the packet */
    if (size > max_packet_size) {
        /* big packet: fragment */
        count = 0;
        while (size > 0) {
            len = max_packet_size - 4;
            if (len > size)
                len = size;
            /* build fragmented packet */
            s->buf[0] = 0;
            s->buf[1] = 0;
            s->buf[2] = count >> 8;
            s->buf[3] = count;
            memcpy(s->buf + 4, buf1, len);
            rtp_send_data(s1, s->buf, len + 4, 0);
            size -= len;
            buf1 += len;
            count += len;
        }
    } else {
        if (s->buf_ptr == s->buf + 4) {
            /* no fragmentation possible */
            s->buf[0] = 0;
            s->buf[1] = 0;
            s->buf[2] = 0;
            s->buf[3] = 0;
        }
        memcpy(s->buf_ptr, buf1, size);
        s->buf_ptr += size;
    }
    s->cur_timestamp += st->codec->frame_size;
}

/* NOTE: a single frame must be passed with sequence header if
   needed. XXX: use slices. */
static void rtp_send_mpegvideo(AVFormatContext *s1,
                               const uint8_t *buf1, int size)
{
    RTPDemuxContext *s = s1->priv_data;
    AVStream *st = s1->streams[0];
    int len, h, max_packet_size;
    uint8_t *q;

    max_packet_size = s->max_payload_size;

    while (size > 0) {
        /* XXX: more correct headers */
        h = 0;
        if (st->codec->sub_id == 2)
            h |= 1 << 26; /* mpeg 2 indicator */
        q = s->buf;
        *q++ = h >> 24;
        *q++ = h >> 16;
        *q++ = h >> 8;
        *q++ = h;

        if (st->codec->sub_id == 2) {
            h = 0;
            *q++ = h >> 24;
            *q++ = h >> 16;
            *q++ = h >> 8;
            *q++ = h;
        }

        len = max_packet_size - (q - s->buf);
        if (len > size)
            len = size;

        memcpy(q, buf1, len);
        q += len;

        /* 90 KHz time stamp */
        s->timestamp = s->base_timestamp +
            av_rescale((int64_t)s->cur_timestamp * st->codec->time_base.num, 90000, st->codec->time_base.den); //FIXME pass timestamps
        rtp_send_data(s1, s->buf, q - s->buf, (len == size));

        buf1 += len;
        size -= len;
    }
    s->cur_timestamp++;
}

static void rtp_send_raw(AVFormatContext *s1,
                         const uint8_t *buf1, int size)
{
    RTPDemuxContext *s = s1->priv_data;
    AVStream *st = s1->streams[0];
    int len, max_packet_size;

    max_packet_size = s->max_payload_size;

    while (size > 0) {
        len = max_packet_size;
        if (len > size)
            len = size;

        /* 90 KHz time stamp */
        s->timestamp = s->base_timestamp +
            av_rescale((int64_t)s->cur_timestamp * st->codec->time_base.num, 90000, st->codec->time_base.den); //FIXME pass timestamps
        rtp_send_data(s1, buf1, len, (len == size));

        buf1 += len;
        size -= len;
    }
    s->cur_timestamp++;
}

/* NOTE: size is assumed to be an integer multiple of TS_PACKET_SIZE */
static void rtp_send_mpegts_raw(AVFormatContext *s1,
                                const uint8_t *buf1, int size)
{
    RTPDemuxContext *s = s1->priv_data;
    int len, out_len;

    while (size >= TS_PACKET_SIZE) {
        len = s->max_payload_size - (s->buf_ptr - s->buf);
        if (len > size)
            len = size;
        memcpy(s->buf_ptr, buf1, len);
        buf1 += len;
        size -= len;
        s->buf_ptr += len;

        out_len = s->buf_ptr - s->buf;
        if (out_len >= s->max_payload_size) {
            rtp_send_data(s1, s->buf, out_len, 0);
            s->buf_ptr = s->buf;
        }
    }
}

/* write an RTP packet. 'buf1' must contain a single specific frame. */
static int rtp_write_packet(AVFormatContext *s1, AVPacket *pkt)
{
    RTPDemuxContext *s = s1->priv_data;
    AVStream *st = s1->streams[0];
    int rtcp_bytes;
    int64_t ntp_time;
    int size= pkt->size;
    uint8_t *buf1= pkt->data;

#ifdef DEBUG
    printf("%d: write len=%d\n", pkt->stream_index, size);
#endif

    /* XXX: mpeg pts hardcoded. RTCP send every 0.5 seconds */
    rtcp_bytes = ((s->octet_count - s->last_octet_count) * RTCP_TX_RATIO_NUM) /
        RTCP_TX_RATIO_DEN;
    if (s->first_packet || rtcp_bytes >= 28) {
        /* compute NTP time */
        /* XXX: 90 kHz timestamp hardcoded */
        ntp_time = (pkt->pts << 28) / 5625;
        rtcp_send_sr(s1, ntp_time);
        s->last_octet_count = s->octet_count;
        s->first_packet = 0;
    }

    switch(st->codec->codec_id) {
    case CODEC_ID_PCM_MULAW:
    case CODEC_ID_PCM_ALAW:
    case CODEC_ID_PCM_U8:
    case CODEC_ID_PCM_S8:
        rtp_send_samples(s1, buf1, size, 1 * st->codec->channels);
        break;
    case CODEC_ID_PCM_U16BE:
    case CODEC_ID_PCM_U16LE:
    case CODEC_ID_PCM_S16BE:
    case CODEC_ID_PCM_S16LE:
        rtp_send_samples(s1, buf1, size, 2 * st->codec->channels);
        break;
    case CODEC_ID_MP2:
    case CODEC_ID_MP3:
        rtp_send_mpegaudio(s1, buf1, size);
        break;
    case CODEC_ID_MPEG1VIDEO:
        rtp_send_mpegvideo(s1, buf1, size);
        break;
    case CODEC_ID_MPEG2TS:
        rtp_send_mpegts_raw(s1, buf1, size);
        break;
    default:
        /* better than nothing : send the codec raw data */
        rtp_send_raw(s1, buf1, size);
        break;
    }
    return 0;
}

static int rtp_write_trailer(AVFormatContext *s1)
{
    //    RTPDemuxContext *s = s1->priv_data;
    return 0;
}

AVOutputFormat rtp_muxer = {
    "rtp",
    "RTP output format",
    NULL,
    NULL,
    sizeof(RTPDemuxContext),
    CODEC_ID_PCM_MULAW,
    CODEC_ID_NONE,
    rtp_write_header,
    rtp_write_packet,
    rtp_write_trailer,
};
