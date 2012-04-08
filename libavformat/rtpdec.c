/*
 * RTP input format
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

#include "libavutil/mathematics.h"
#include "libavutil/avstring.h"
#include "libavcodec/get_bits.h"
#include "avformat.h"
#include "mpegts.h"
#include "url.h"

#include <unistd.h>
#include "network.h"

#include "rtpdec.h"
#include "rtpdec_formats.h"

//#define DEBUG

/* TODO: - add RTCP statistics reporting (should be optional).

         - add support for h263/mpeg4 packetized output : IDEA: send a
         buffer to 'rtp_write_packet' contains all the packets for ONE
         frame. Each packet should have a four byte header containing
         the length in big endian format (same trick as
         'ffio_open_dyn_packet_buf')
*/

static RTPDynamicProtocolHandler ff_realmedia_mp3_dynamic_handler = {
    .enc_name           = "X-MP3-draft-00",
    .codec_type         = AVMEDIA_TYPE_AUDIO,
    .codec_id           = CODEC_ID_MP3ADU,
};

/* statistics functions */
static RTPDynamicProtocolHandler *RTPFirstDynamicPayloadHandler= NULL;

void ff_register_dynamic_payload_handler(RTPDynamicProtocolHandler *handler)
{
    handler->next= RTPFirstDynamicPayloadHandler;
    RTPFirstDynamicPayloadHandler= handler;
}

void av_register_rtp_dynamic_payload_handlers(void)
{
    ff_register_dynamic_payload_handler(&ff_mp4v_es_dynamic_handler);
    ff_register_dynamic_payload_handler(&ff_mpeg4_generic_dynamic_handler);
    ff_register_dynamic_payload_handler(&ff_amr_nb_dynamic_handler);
    ff_register_dynamic_payload_handler(&ff_amr_wb_dynamic_handler);
    ff_register_dynamic_payload_handler(&ff_h263_1998_dynamic_handler);
    ff_register_dynamic_payload_handler(&ff_h263_2000_dynamic_handler);
    ff_register_dynamic_payload_handler(&ff_h263_rfc2190_dynamic_handler);
    ff_register_dynamic_payload_handler(&ff_h264_dynamic_handler);
    ff_register_dynamic_payload_handler(&ff_vorbis_dynamic_handler);
    ff_register_dynamic_payload_handler(&ff_theora_dynamic_handler);
    ff_register_dynamic_payload_handler(&ff_qdm2_dynamic_handler);
    ff_register_dynamic_payload_handler(&ff_svq3_dynamic_handler);
    ff_register_dynamic_payload_handler(&ff_mp4a_latm_dynamic_handler);
    ff_register_dynamic_payload_handler(&ff_vp8_dynamic_handler);
    ff_register_dynamic_payload_handler(&ff_qcelp_dynamic_handler);
    ff_register_dynamic_payload_handler(&ff_realmedia_mp3_dynamic_handler);

    ff_register_dynamic_payload_handler(&ff_ms_rtp_asf_pfv_handler);
    ff_register_dynamic_payload_handler(&ff_ms_rtp_asf_pfa_handler);

    ff_register_dynamic_payload_handler(&ff_qt_rtp_aud_handler);
    ff_register_dynamic_payload_handler(&ff_qt_rtp_vid_handler);
    ff_register_dynamic_payload_handler(&ff_quicktime_rtp_aud_handler);
    ff_register_dynamic_payload_handler(&ff_quicktime_rtp_vid_handler);

    ff_register_dynamic_payload_handler(&ff_g726_16_dynamic_handler);
    ff_register_dynamic_payload_handler(&ff_g726_24_dynamic_handler);
    ff_register_dynamic_payload_handler(&ff_g726_32_dynamic_handler);
    ff_register_dynamic_payload_handler(&ff_g726_40_dynamic_handler);
}

RTPDynamicProtocolHandler *ff_rtp_handler_find_by_name(const char *name,
                                                  enum AVMediaType codec_type)
{
    RTPDynamicProtocolHandler *handler;
    for (handler = RTPFirstDynamicPayloadHandler;
         handler; handler = handler->next)
        if (!av_strcasecmp(name, handler->enc_name) &&
            codec_type == handler->codec_type)
            return handler;
    return NULL;
}

RTPDynamicProtocolHandler *ff_rtp_handler_find_by_id(int id,
                                                enum AVMediaType codec_type)
{
    RTPDynamicProtocolHandler *handler;
    for (handler = RTPFirstDynamicPayloadHandler;
         handler; handler = handler->next)
        if (handler->static_payload_id && handler->static_payload_id == id &&
            codec_type == handler->codec_type)
            return handler;
    return NULL;
}

static int rtcp_parse_packet(RTPDemuxContext *s, const unsigned char *buf, int len)
{
    int payload_len;
    while (len >= 4) {
        payload_len = FFMIN(len, (AV_RB16(buf + 2) + 1) * 4);

        switch (buf[1]) {
        case RTCP_SR:
            if (payload_len < 20) {
                av_log(NULL, AV_LOG_ERROR, "Invalid length for RTCP SR packet\n");
                return AVERROR_INVALIDDATA;
            }

            s->last_rtcp_ntp_time = AV_RB64(buf + 8);
            s->last_rtcp_timestamp = AV_RB32(buf + 16);
            if (s->first_rtcp_ntp_time == AV_NOPTS_VALUE) {
                s->first_rtcp_ntp_time = s->last_rtcp_ntp_time;
                if (!s->base_timestamp)
                    s->base_timestamp = s->last_rtcp_timestamp;
                s->rtcp_ts_offset = s->last_rtcp_timestamp - s->base_timestamp;
            }

            break;
        case RTCP_BYE:
            return -RTCP_BYE;
        }

        buf += payload_len;
        len -= payload_len;
    }
    return -1;
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

int ff_rtp_check_and_send_back_rr(RTPDemuxContext *s, int count)
{
    AVIOContext *pb;
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

    if (avio_open_dyn_buf(&pb) < 0)
        return -1;

    // Receiver Report
    avio_w8(pb, (RTP_VERSION << 6) + 1); /* 1 report block */
    avio_w8(pb, RTCP_RR);
    avio_wb16(pb, 7); /* length in words - 1 */
    // our own SSRC: we use the server's SSRC + 1 to avoid conflicts
    avio_wb32(pb, s->ssrc + 1);
    avio_wb32(pb, s->ssrc); // server SSRC
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

    avio_wb32(pb, fraction); /* 8 bits of fraction, 24 bits of total packets lost */
    avio_wb32(pb, extended_max); /* max sequence received */
    avio_wb32(pb, stats->jitter>>4); /* jitter */

    if(s->last_rtcp_ntp_time==AV_NOPTS_VALUE)
    {
        avio_wb32(pb, 0); /* last SR timestamp */
        avio_wb32(pb, 0); /* delay since last SR */
    } else {
        uint32_t middle_32_bits= s->last_rtcp_ntp_time>>16; // this is valid, right? do we need to handle 64 bit values special?
        uint32_t delay_since_last= ntp_time - s->last_rtcp_ntp_time;

        avio_wb32(pb, middle_32_bits); /* last SR timestamp */
        avio_wb32(pb, delay_since_last); /* delay since last SR */
    }

    // CNAME
    avio_w8(pb, (RTP_VERSION << 6) + 1); /* 1 report block */
    avio_w8(pb, RTCP_SDES);
    len = strlen(s->hostname);
    avio_wb16(pb, (6 + len + 3) / 4); /* length in words - 1 */
    avio_wb32(pb, s->ssrc + 1);
    avio_w8(pb, 0x01);
    avio_w8(pb, len);
    avio_write(pb, s->hostname, len);
    // padding
    for (len = (6 + len) % 4; len % 4; len++) {
        avio_w8(pb, 0);
    }

    avio_flush(pb);
    len = avio_close_dyn_buf(pb, &buf);
    if ((len > 0) && buf) {
        int av_unused result;
        av_dlog(s->ic, "sending %d bytes of RR\n", len);
        result= ffurl_write(s->rtp_ctx, buf, len);
        av_dlog(s->ic, "result from ffurl_write: %d\n", result);
        av_free(buf);
    }
    return 0;
}

void ff_rtp_send_punch_packets(URLContext* rtp_handle)
{
    AVIOContext *pb;
    uint8_t *buf;
    int len;

    /* Send a small RTP packet */
    if (avio_open_dyn_buf(&pb) < 0)
        return;

    avio_w8(pb, (RTP_VERSION << 6));
    avio_w8(pb, 0); /* Payload type */
    avio_wb16(pb, 0); /* Seq */
    avio_wb32(pb, 0); /* Timestamp */
    avio_wb32(pb, 0); /* SSRC */

    avio_flush(pb);
    len = avio_close_dyn_buf(pb, &buf);
    if ((len > 0) && buf)
        ffurl_write(rtp_handle, buf, len);
    av_free(buf);

    /* Send a minimal RTCP RR */
    if (avio_open_dyn_buf(&pb) < 0)
        return;

    avio_w8(pb, (RTP_VERSION << 6));
    avio_w8(pb, RTCP_RR); /* receiver report */
    avio_wb16(pb, 1); /* length in words - 1 */
    avio_wb32(pb, 0); /* our own SSRC */

    avio_flush(pb);
    len = avio_close_dyn_buf(pb, &buf);
    if ((len > 0) && buf)
        ffurl_write(rtp_handle, buf, len);
    av_free(buf);
}


/**
 * open a new RTP parse context for stream 'st'. 'st' can be NULL for
 * MPEG2TS streams to indicate that they should be demuxed inside the
 * rtp demux (otherwise CODEC_ID_MPEG2TS packets are returned)
 */
RTPDemuxContext *ff_rtp_parse_open(AVFormatContext *s1, AVStream *st, URLContext *rtpc, int payload_type, int queue_size)
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
    s->queue_size = queue_size;
    rtp_init_statistics(&s->statistics, 0); // do we know the initial sequence from sdp?
    if (!strcmp(ff_rtp_enc_name(payload_type), "MP2T")) {
        s->ts = ff_mpegts_parse_open(s->ic);
        if (s->ts == NULL) {
            av_free(s);
            return NULL;
        }
    } else if (st) {
        switch(st->codec->codec_id) {
        case CODEC_ID_MPEG1VIDEO:
        case CODEC_ID_MPEG2VIDEO:
        case CODEC_ID_MP2:
        case CODEC_ID_MP3:
        case CODEC_ID_MPEG4:
        case CODEC_ID_H263:
        case CODEC_ID_H264:
            st->need_parsing = AVSTREAM_PARSE_FULL;
            break;
        case CODEC_ID_VORBIS:
            st->need_parsing = AVSTREAM_PARSE_HEADERS;
            break;
        case CODEC_ID_ADPCM_G722:
            /* According to RFC 3551, the stream clock rate is 8000
             * even if the sample rate is 16000. */
            if (st->codec->sample_rate == 8000)
                st->codec->sample_rate = 16000;
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

void
ff_rtp_parse_set_dynamic_protocol(RTPDemuxContext *s, PayloadContext *ctx,
                                  RTPDynamicProtocolHandler *handler)
{
    s->dynamic_protocol_context = ctx;
    s->parse_packet = handler->parse_packet;
}

/**
 * This was the second switch in rtp_parse packet.  Normalizes time, if required, sets stream_index, etc.
 */
static void finalize_packet(RTPDemuxContext *s, AVPacket *pkt, uint32_t timestamp)
{
    if (pkt->pts != AV_NOPTS_VALUE || pkt->dts != AV_NOPTS_VALUE)
        return; /* Timestamp already set by depacketizer */
    if (timestamp == RTP_NOTS_VALUE)
        return;

    if (s->last_rtcp_ntp_time != AV_NOPTS_VALUE && s->ic->nb_streams > 1) {
        int64_t addend;
        int delta_timestamp;

        /* compute pts from timestamp with received ntp_time */
        delta_timestamp = timestamp - s->last_rtcp_timestamp;
        /* convert to the PTS timebase */
        addend = av_rescale(s->last_rtcp_ntp_time - s->first_rtcp_ntp_time, s->st->time_base.den, (uint64_t)s->st->time_base.num << 32);
        pkt->pts = s->range_start_offset + s->rtcp_ts_offset + addend +
                   delta_timestamp;
        return;
    }

    if (!s->base_timestamp)
        s->base_timestamp = timestamp;
    /* assume that the difference is INT32_MIN < x < INT32_MAX, but allow the first timestamp to exceed INT32_MAX */
    if (!s->timestamp)
        s->unwrapped_timestamp += timestamp;
    else
        s->unwrapped_timestamp += (int32_t)(timestamp - s->timestamp);
    s->timestamp = timestamp;
    pkt->pts = s->unwrapped_timestamp + s->range_start_offset - s->base_timestamp;
}

static int rtp_parse_packet_internal(RTPDemuxContext *s, AVPacket *pkt,
                                     const uint8_t *buf, int len)
{
    unsigned int ssrc, h;
    int payload_type, seq, ret, flags = 0;
    int ext;
    AVStream *st;
    uint32_t timestamp;
    int rv= 0;

    ext = buf[0] & 0x10;
    payload_type = buf[1] & 0x7f;
    if (buf[1] & 0x80)
        flags |= RTP_FLAG_MARKER;
    seq  = AV_RB16(buf + 2);
    timestamp = AV_RB32(buf + 4);
    ssrc = AV_RB32(buf + 8);
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

    if (buf[0] & 0x20) {
        int padding = buf[len - 1];
        if (len >= 12 + padding)
            len -= padding;
    }

    s->seq = seq;
    len -= 12;
    buf += 12;

    /* RFC 3550 Section 5.3.1 RTP Header Extension handling */
    if (ext) {
        if (len < 4)
            return -1;
        /* calculate the header extension length (stored as number
         * of 32-bit words) */
        ext = (AV_RB16(buf + 2) + 1) << 2;

        if (len < ext)
            return -1;
        // skip past RTP header extension
        len -= ext;
        buf += ext;
    }

    if (!st) {
        /* specific MPEG2TS demux support */
        ret = ff_mpegts_parse_packet(s->ts, pkt, buf, len);
        /* The only error that can be returned from ff_mpegts_parse_packet
         * is "no more data to return from the provided buffer", so return
         * AVERROR(EAGAIN) for all errors */
        if (ret < 0)
            return AVERROR(EAGAIN);
        if (ret < len) {
            s->read_buf_size = len - ret;
            memcpy(s->buf, buf + ret, s->read_buf_size);
            s->read_buf_index = 0;
            return 1;
        }
        return 0;
    } else if (s->parse_packet) {
        rv = s->parse_packet(s->ic, s->dynamic_protocol_context,
                             s->st, pkt, &timestamp, buf, len, flags);
    } else {
        // at this point, the RTP header has been stripped;  This is ASSUMING that there is only 1 CSRC, which in't wise.
        switch(st->codec->codec_id) {
        case CODEC_ID_MP2:
        case CODEC_ID_MP3:
            /* better than nothing: skip mpeg audio RTP header */
            if (len <= 4)
                return -1;
            h = AV_RB32(buf);
            len -= 4;
            buf += 4;
            av_new_packet(pkt, len);
            memcpy(pkt->data, buf, len);
            break;
        case CODEC_ID_MPEG1VIDEO:
        case CODEC_ID_MPEG2VIDEO:
            /* better than nothing: skip mpeg video RTP header */
            if (len <= 4)
                return -1;
            h = AV_RB32(buf);
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
        default:
            av_new_packet(pkt, len);
            memcpy(pkt->data, buf, len);
            break;
        }

        pkt->stream_index = st->index;
    }

    // now perform timestamp things....
    finalize_packet(s, pkt, timestamp);

    return rv;
}

void ff_rtp_reset_packet_queue(RTPDemuxContext *s)
{
    while (s->queue) {
        RTPPacket *next = s->queue->next;
        av_free(s->queue->buf);
        av_free(s->queue);
        s->queue = next;
    }
    s->seq       = 0;
    s->queue_len = 0;
    s->prev_ret  = 0;
}

static void enqueue_packet(RTPDemuxContext *s, uint8_t *buf, int len)
{
    uint16_t seq = AV_RB16(buf + 2);
    RTPPacket *cur = s->queue, *prev = NULL, *packet;

    /* Find the correct place in the queue to insert the packet */
    while (cur) {
        int16_t diff = seq - cur->seq;
        if (diff < 0)
            break;
        prev = cur;
        cur = cur->next;
    }

    packet = av_mallocz(sizeof(*packet));
    if (!packet)
        return;
    packet->recvtime = av_gettime();
    packet->seq = seq;
    packet->len = len;
    packet->buf = buf;
    packet->next = cur;
    if (prev)
        prev->next = packet;
    else
        s->queue = packet;
    s->queue_len++;
}

static int has_next_packet(RTPDemuxContext *s)
{
    return s->queue && s->queue->seq == (uint16_t) (s->seq + 1);
}

int64_t ff_rtp_queued_packet_time(RTPDemuxContext *s)
{
    return s->queue ? s->queue->recvtime : 0;
}

static int rtp_parse_queued_packet(RTPDemuxContext *s, AVPacket *pkt)
{
    int rv;
    RTPPacket *next;

    if (s->queue_len <= 0)
        return -1;

    if (!has_next_packet(s))
        av_log(s->st ? s->st->codec : NULL, AV_LOG_WARNING,
               "RTP: missed %d packets\n", s->queue->seq - s->seq - 1);

    /* Parse the first packet in the queue, and dequeue it */
    rv = rtp_parse_packet_internal(s, pkt, s->queue->buf, s->queue->len);
    next = s->queue->next;
    av_free(s->queue->buf);
    av_free(s->queue);
    s->queue = next;
    s->queue_len--;
    return rv;
}

static int rtp_parse_one_packet(RTPDemuxContext *s, AVPacket *pkt,
                     uint8_t **bufptr, int len)
{
    uint8_t* buf = bufptr ? *bufptr : NULL;
    int ret, flags = 0;
    uint32_t timestamp;
    int rv= 0;

    if (!buf) {
        /* If parsing of the previous packet actually returned 0 or an error,
         * there's nothing more to be parsed from that packet, but we may have
         * indicated that we can return the next enqueued packet. */
        if (s->prev_ret <= 0)
            return rtp_parse_queued_packet(s, pkt);
        /* return the next packets, if any */
        if(s->st && s->parse_packet) {
            /* timestamp should be overwritten by parse_packet, if not,
             * the packet is left with pts == AV_NOPTS_VALUE */
            timestamp = RTP_NOTS_VALUE;
            rv= s->parse_packet(s->ic, s->dynamic_protocol_context,
                                s->st, pkt, &timestamp, NULL, 0, flags);
            finalize_packet(s, pkt, timestamp);
            return rv;
        } else {
            // TODO: Move to a dynamic packet handler (like above)
            if (s->read_buf_index >= s->read_buf_size)
                return AVERROR(EAGAIN);
            ret = ff_mpegts_parse_packet(s->ts, pkt, s->buf + s->read_buf_index,
                                      s->read_buf_size - s->read_buf_index);
            if (ret < 0)
                return AVERROR(EAGAIN);
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
    if (RTP_PT_IS_RTCP(buf[1])) {
        return rtcp_parse_packet(s, buf, len);
    }

    if ((s->seq == 0 && !s->queue) || s->queue_size <= 1) {
        /* First packet, or no reordering */
        return rtp_parse_packet_internal(s, pkt, buf, len);
    } else {
        uint16_t seq = AV_RB16(buf + 2);
        int16_t diff = seq - s->seq;
        if (diff < 0) {
            /* Packet older than the previously emitted one, drop */
            av_log(s->st ? s->st->codec : NULL, AV_LOG_WARNING,
                   "RTP: dropping old packet received too late\n");
            return -1;
        } else if (diff <= 1) {
            /* Correct packet */
            rv = rtp_parse_packet_internal(s, pkt, buf, len);
            return rv;
        } else {
            /* Still missing some packet, enqueue this one. */
            enqueue_packet(s, buf, len);
            *bufptr = NULL;
            /* Return the first enqueued packet if the queue is full,
             * even if we're missing something */
            if (s->queue_len >= s->queue_size)
                return rtp_parse_queued_packet(s, pkt);
            return -1;
        }
    }
}

/**
 * Parse an RTP or RTCP packet directly sent as a buffer.
 * @param s RTP parse context.
 * @param pkt returned packet
 * @param bufptr pointer to the input buffer or NULL to read the next packets
 * @param len buffer len
 * @return 0 if a packet is returned, 1 if a packet is returned and more can follow
 * (use buf as NULL to read the next). -1 if no packet (error or no more packet).
 */
int ff_rtp_parse_packet(RTPDemuxContext *s, AVPacket *pkt,
                        uint8_t **bufptr, int len)
{
    int rv = rtp_parse_one_packet(s, pkt, bufptr, len);
    s->prev_ret = rv;
    while (rv == AVERROR(EAGAIN) && has_next_packet(s))
        rv = rtp_parse_queued_packet(s, pkt);
    return rv ? rv : has_next_packet(s);
}

void ff_rtp_parse_close(RTPDemuxContext *s)
{
    ff_rtp_reset_packet_queue(s);
    if (!strcmp(ff_rtp_enc_name(s->payload_type), "MP2T")) {
        ff_mpegts_parse_close(s->ts);
    }
    av_free(s);
}

int ff_parse_fmtp(AVStream *stream, PayloadContext *data, const char *p,
                  int (*parse_fmtp)(AVStream *stream,
                                    PayloadContext *data,
                                    char *attr, char *value))
{
    char attr[256];
    char *value;
    int res;
    int value_size = strlen(p) + 1;

    if (!(value = av_malloc(value_size))) {
        av_log(stream, AV_LOG_ERROR, "Failed to allocate data for FMTP.");
        return AVERROR(ENOMEM);
    }

    // remove protocol identifier
    while (*p && *p == ' ') p++; // strip spaces
    while (*p && *p != ' ') p++; // eat protocol identifier
    while (*p && *p == ' ') p++; // strip trailing spaces

    while (ff_rtsp_next_attr_and_value(&p,
                                       attr, sizeof(attr),
                                       value, value_size)) {

        res = parse_fmtp(stream, data, attr, value);
        if (res < 0 && res != AVERROR_PATCHWELCOME) {
            av_free(value);
            return res;
        }
    }
    av_free(value);
    return 0;
}
