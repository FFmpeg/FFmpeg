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

/* needed for gethostname() */
#define _XOPEN_SOURCE 600

#include "libavcodec/bitstream.h"
#include "avformat.h"
#include "mpegts.h"

#include <unistd.h>
#include "network.h"

#include "rtpdec.h"
#include "rtp_h264.h"

//#define DEBUG

/* TODO: - add RTCP statistics reporting (should be optional).

         - add support for h263/mpeg4 packetized output : IDEA: send a
         buffer to 'rtp_write_packet' contains all the packets for ONE
         frame. Each packet should have a four byte header containing
         the length in big endian format (same trick as
         'url_open_dyn_packet_buf')
*/

/* statistics functions */
RTPDynamicProtocolHandler *RTPFirstDynamicPayloadHandler= NULL;

static RTPDynamicProtocolHandler mp4v_es_handler= {"MP4V-ES", CODEC_TYPE_VIDEO, CODEC_ID_MPEG4};
static RTPDynamicProtocolHandler mpeg4_generic_handler= {"mpeg4-generic", CODEC_TYPE_AUDIO, CODEC_ID_AAC};

void ff_register_dynamic_payload_handler(RTPDynamicProtocolHandler *handler)
{
    handler->next= RTPFirstDynamicPayloadHandler;
    RTPFirstDynamicPayloadHandler= handler;
}

void av_register_rtp_dynamic_payload_handlers(void)
{
    ff_register_dynamic_payload_handler(&mp4v_es_handler);
    ff_register_dynamic_payload_handler(&mpeg4_generic_handler);
    ff_register_dynamic_payload_handler(&ff_h264_dynamic_handler);
}

static int rtcp_parse_packet(RTPDemuxContext *s, const unsigned char *buf, int len)
{
    if (buf[1] != 200)
        return -1;
    s->last_rtcp_ntp_time = AV_RB64(buf + 8);
    if (s->first_rtcp_ntp_time == AV_NOPTS_VALUE)
        s->first_rtcp_ntp_time = s->last_rtcp_ntp_time;
    s->last_rtcp_timestamp = AV_RB32(buf + 16);
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
    ByteIOContext *pb;
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
    put_byte(pb, (RTP_VERSION << 6) + 1); /* 1 report block */
    put_byte(pb, 201);
    put_be16(pb, 7); /* length in words - 1 */
    put_be32(pb, s->ssrc); // our own SSRC
    put_be32(pb, s->ssrc); // XXX: should be the server's here!
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

    put_be32(pb, fraction); /* 8 bits of fraction, 24 bits of total packets lost */
    put_be32(pb, extended_max); /* max sequence received */
    put_be32(pb, stats->jitter>>4); /* jitter */

    if(s->last_rtcp_ntp_time==AV_NOPTS_VALUE)
    {
        put_be32(pb, 0); /* last SR timestamp */
        put_be32(pb, 0); /* delay since last SR */
    } else {
        uint32_t middle_32_bits= s->last_rtcp_ntp_time>>16; // this is valid, right? do we need to handle 64 bit values special?
        uint32_t delay_since_last= ntp_time - s->last_rtcp_ntp_time;

        put_be32(pb, middle_32_bits); /* last SR timestamp */
        put_be32(pb, delay_since_last); /* delay since last SR */
    }

    // CNAME
    put_byte(pb, (RTP_VERSION << 6) + 1); /* 1 report block */
    put_byte(pb, 202);
    len = strlen(s->hostname);
    put_be16(pb, (6 + len + 3) / 4); /* length in words - 1 */
    put_be32(pb, s->ssrc);
    put_byte(pb, 0x01);
    put_byte(pb, len);
    put_buffer(pb, s->hostname, len);
    // padding
    for (len = (6 + len) % 4; len % 4; len++) {
        put_byte(pb, 0);
    }

    put_flush_packet(pb);
    len = url_close_dyn_buf(pb, &buf);
    if ((len > 0) && buf) {
        int result;
        dprintf(s->ic, "sending %d bytes of RR\n", len);
        result= url_write(s->rtp_ctx, buf, len);
        dprintf(s->ic, "result from url_write: %d\n", result);
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
RTPDemuxContext *rtp_parse_open(AVFormatContext *s1, AVStream *st, URLContext *rtpc, int payload_type, RTPPayloadData *rtp_payload_data)
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
    if (!strcmp(ff_rtp_enc_name(payload_type), "MP2T")) {
        s->ts = mpegts_parse_open(s->ic);
        if (s->ts == NULL) {
            av_free(s);
            return NULL;
        }
    } else {
        av_set_pts_info(st, 32, 1, 90000);
        switch(st->codec->codec_id) {
        case CODEC_ID_MPEG1VIDEO:
        case CODEC_ID_MPEG2VIDEO:
        case CODEC_ID_MP2:
        case CODEC_ID_MP3:
        case CODEC_ID_MPEG4:
        case CODEC_ID_H264:
            st->need_parsing = AVSTREAM_PARSE_FULL;
            break;
        default:
            if (st->codec->codec_type == CODEC_TYPE_AUDIO) {
                av_set_pts_info(st, 32, 1, st->codec->sample_rate);
            }
            break;
        }
    }
    // needed to send back RTCP RR in RTSP sessions
    s->rtp_ctx = rtpc;
    gethostname(s->hostname, sizeof(s->hostname));
    return s;
}

void
rtp_parse_set_dynamic_protocol(RTPDemuxContext *s, PayloadContext *ctx,
                               RTPDynamicProtocolHandler *handler)
{
    s->dynamic_protocol_context = ctx;
    s->parse_packet = handler->parse_packet;
}

static int rtp_parse_mp4_au(RTPDemuxContext *s, const uint8_t *buf)
{
    int au_headers_length, au_header_size, i;
    GetBitContext getbitcontext;
    RTPPayloadData *infos;

    infos = s->rtp_payload_data;

    if (infos == NULL)
        return -1;

    /* decode the first 2 bytes where the AUHeader sections are stored
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
       In my test, the FAAD decoder does not behave correctly when sending each AU one by one
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
    if (s->last_rtcp_ntp_time != AV_NOPTS_VALUE) {
        int64_t addend;
        int delta_timestamp;

        /* compute pts from timestamp with received ntp_time */
        delta_timestamp = timestamp - s->last_rtcp_timestamp;
        /* convert to the PTS timebase */
        addend = av_rescale(s->last_rtcp_ntp_time - s->first_rtcp_ntp_time, s->st->time_base.den, (uint64_t)s->st->time_base.num << 32);
        pkt->pts = addend + delta_timestamp;
    }
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
    int payload_type, seq, ret, flags = 0;
    AVStream *st;
    uint32_t timestamp;
    int rv= 0;

    if (!buf) {
        /* return the next packets, if any */
        if(s->st && s->parse_packet) {
            timestamp= 0; ///< Should not be used if buf is NULL, but should be set to the timestamp of the packet returned....
            rv= s->parse_packet(s->ic, s->dynamic_protocol_context,
                                s->st, pkt, &timestamp, NULL, 0, flags);
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
        return 0;
    } else if (s->parse_packet) {
        rv = s->parse_packet(s->ic, s->dynamic_protocol_context,
                             s->st, pkt, &timestamp, buf, len, flags);
    } else {
        // at this point, the RTP header has been stripped;  This is ASSUMING that there is only 1 CSRC, which in't wise.
        switch(st->codec->codec_id) {
        case CODEC_ID_MP2:
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
            // moved from below, verbatim.  this is because this section handles packets, and the lower switch handles
            // timestamps.
            // TODO: Put this into a dynamic packet handler...
        case CODEC_ID_AAC:
            if (rtp_parse_mp4_au(s, buf))
                return -1;
            {
                RTPPayloadData *infos = s->rtp_payload_data;
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
            rv= 0;
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

void rtp_parse_close(RTPDemuxContext *s)
{
    // TODO: fold this into the protocol specific data fields.
    if (!strcmp(ff_rtp_enc_name(s->payload_type), "MP2T")) {
        mpegts_parse_close(s->ts);
    }
    av_free(s);
}
