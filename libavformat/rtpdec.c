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
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/time.h"

#include "libavcodec/bytestream.h"

#include "avformat.h"
#include "network.h"
#include "srtp.h"
#include "url.h"
#include "rtpdec.h"
#include "rtpdec_formats.h"
#include "internal.h"

#define MIN_FEEDBACK_INTERVAL 200000 /* 200 ms in us */

static const RTPDynamicProtocolHandler l24_dynamic_handler = {
    .enc_name   = "L24",
    .codec_type = AVMEDIA_TYPE_AUDIO,
    .codec_id   = AV_CODEC_ID_PCM_S24BE,
};

static const RTPDynamicProtocolHandler gsm_dynamic_handler = {
    .enc_name   = "GSM",
    .codec_type = AVMEDIA_TYPE_AUDIO,
    .codec_id   = AV_CODEC_ID_GSM,
};

static const RTPDynamicProtocolHandler realmedia_mp3_dynamic_handler = {
    .enc_name   = "X-MP3-draft-00",
    .codec_type = AVMEDIA_TYPE_AUDIO,
    .codec_id   = AV_CODEC_ID_MP3ADU,
};

static const RTPDynamicProtocolHandler speex_dynamic_handler = {
    .enc_name   = "speex",
    .codec_type = AVMEDIA_TYPE_AUDIO,
    .codec_id   = AV_CODEC_ID_SPEEX,
};

static const RTPDynamicProtocolHandler t140_dynamic_handler = { /* RFC 4103 */
    .enc_name   = "t140",
    .codec_type = AVMEDIA_TYPE_SUBTITLE,
    .codec_id   = AV_CODEC_ID_TEXT,
};

extern const RTPDynamicProtocolHandler ff_rdt_video_handler;
extern const RTPDynamicProtocolHandler ff_rdt_audio_handler;
extern const RTPDynamicProtocolHandler ff_rdt_live_video_handler;
extern const RTPDynamicProtocolHandler ff_rdt_live_audio_handler;

static const RTPDynamicProtocolHandler *const rtp_dynamic_protocol_handler_list[] = {
    /* rtp */
    &ff_ac3_dynamic_handler,
    &ff_amr_nb_dynamic_handler,
    &ff_amr_wb_dynamic_handler,
    &ff_av1_dynamic_handler,
    &ff_dv_dynamic_handler,
    &ff_g726_16_dynamic_handler,
    &ff_g726_24_dynamic_handler,
    &ff_g726_32_dynamic_handler,
    &ff_g726_40_dynamic_handler,
    &ff_g726le_16_dynamic_handler,
    &ff_g726le_24_dynamic_handler,
    &ff_g726le_32_dynamic_handler,
    &ff_g726le_40_dynamic_handler,
    &ff_h261_dynamic_handler,
    &ff_h263_1998_dynamic_handler,
    &ff_h263_2000_dynamic_handler,
    &ff_h263_rfc2190_dynamic_handler,
    &ff_h264_dynamic_handler,
    &ff_hevc_dynamic_handler,
    &ff_ilbc_dynamic_handler,
    &ff_jpeg_dynamic_handler,
    &ff_mp4a_latm_dynamic_handler,
    &ff_mp4v_es_dynamic_handler,
    &ff_mpeg_audio_dynamic_handler,
    &ff_mpeg_audio_robust_dynamic_handler,
    &ff_mpeg_video_dynamic_handler,
    &ff_mpeg4_generic_dynamic_handler,
    &ff_mpegts_dynamic_handler,
    &ff_ms_rtp_asf_pfa_handler,
    &ff_ms_rtp_asf_pfv_handler,
    &ff_qcelp_dynamic_handler,
    &ff_qdm2_dynamic_handler,
    &ff_qt_rtp_aud_handler,
    &ff_qt_rtp_vid_handler,
    &ff_quicktime_rtp_aud_handler,
    &ff_quicktime_rtp_vid_handler,
    &ff_rfc4175_rtp_handler,
    &ff_svq3_dynamic_handler,
    &ff_theora_dynamic_handler,
    &ff_vc2hq_dynamic_handler,
    &ff_vorbis_dynamic_handler,
    &ff_vp8_dynamic_handler,
    &ff_vp9_dynamic_handler,
    &gsm_dynamic_handler,
    &l24_dynamic_handler,
    &ff_opus_dynamic_handler,
    &realmedia_mp3_dynamic_handler,
    &speex_dynamic_handler,
    &t140_dynamic_handler,
    /* rdt */
    &ff_rdt_video_handler,
    &ff_rdt_audio_handler,
    &ff_rdt_live_video_handler,
    &ff_rdt_live_audio_handler,
    NULL,
};

/**
 * Iterate over all registered rtp dynamic protocol handlers.
 *
 * @param opaque a pointer where libavformat will store the iteration state.
 *               Must point to NULL to start the iteration.
 *
 * @return the next registered rtp dynamic protocol handler
 *         or NULL when the iteration is finished
 */
static const RTPDynamicProtocolHandler *rtp_handler_iterate(void **opaque)
{
    uintptr_t i = (uintptr_t)*opaque;
    const RTPDynamicProtocolHandler *r = rtp_dynamic_protocol_handler_list[i];

    if (r)
        *opaque = (void*)(i + 1);

    return r;
}

const RTPDynamicProtocolHandler *ff_rtp_handler_find_by_name(const char *name,
                                                       enum AVMediaType codec_type)
{
    void *i = 0;
    const RTPDynamicProtocolHandler *handler;
    while (handler = rtp_handler_iterate(&i)) {
        if (handler->enc_name &&
            !av_strcasecmp(name, handler->enc_name) &&
            codec_type == handler->codec_type)
            return handler;
    }
    return NULL;
}

const RTPDynamicProtocolHandler *ff_rtp_handler_find_by_id(int id,
                                                     enum AVMediaType codec_type)
{
    void *i = 0;
    const RTPDynamicProtocolHandler *handler;
    while (handler = rtp_handler_iterate(&i)) {
        if (handler->static_payload_id && handler->static_payload_id == id &&
            codec_type == handler->codec_type)
            return handler;
    }
    return NULL;
}

static int rtcp_parse_packet(RTPDemuxContext *s, const unsigned char *buf,
                             int len)
{
    int payload_len;
    while (len >= 4) {
        payload_len = FFMIN(len, (AV_RB16(buf + 2) + 1) * 4);

        switch (buf[1]) {
        case RTCP_SR:
            if (payload_len < 20) {
                av_log(s->ic, AV_LOG_ERROR, "Invalid RTCP SR packet length\n");
                return AVERROR_INVALIDDATA;
            }

            s->last_rtcp_reception_time = av_gettime_relative();
            s->last_rtcp_ntp_time  = AV_RB64(buf + 8);
            s->last_rtcp_timestamp = AV_RB32(buf + 16);
            if (s->first_rtcp_ntp_time == AV_NOPTS_VALUE) {
                s->first_rtcp_ntp_time = s->last_rtcp_ntp_time;
                if (!s->base_timestamp)
                    s->base_timestamp = s->last_rtcp_timestamp;
                s->rtcp_ts_offset = (int32_t)(s->last_rtcp_timestamp - s->base_timestamp);
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

#define RTP_SEQ_MOD (1 << 16)

static void rtp_init_statistics(RTPStatistics *s, uint16_t base_sequence)
{
    memset(s, 0, sizeof(RTPStatistics));
    s->max_seq   = base_sequence;
    s->probation = 1;
}

/*
 * Called whenever there is a large jump in sequence numbers,
 * or when they get out of probation...
 */
static void rtp_init_sequence(RTPStatistics *s, uint16_t seq)
{
    s->max_seq        = seq;
    s->cycles         = 0;
    s->base_seq       = seq - 1;
    s->bad_seq        = RTP_SEQ_MOD + 1;
    s->received       = 0;
    s->expected_prior = 0;
    s->received_prior = 0;
    s->jitter         = 0;
    s->transit        = 0;
}

/* Returns 1 if we should handle this packet. */
static int rtp_valid_packet_in_sequence(RTPStatistics *s, uint16_t seq)
{
    uint16_t udelta = seq - s->max_seq;
    const int MAX_DROPOUT    = 3000;
    const int MAX_MISORDER   = 100;
    const int MIN_SEQUENTIAL = 2;

    /* source not valid until MIN_SEQUENTIAL packets with sequence
     * seq. numbers have been received */
    if (s->probation) {
        if (seq == s->max_seq + 1) {
            s->probation--;
            s->max_seq = seq;
            if (s->probation == 0) {
                rtp_init_sequence(s, seq);
                s->received++;
                return 1;
            }
        } else {
            s->probation = MIN_SEQUENTIAL - 1;
            s->max_seq   = seq;
        }
    } else if (udelta < MAX_DROPOUT) {
        // in order, with permissible gap
        if (seq < s->max_seq) {
            // sequence number wrapped; count another 64k cycles
            s->cycles += RTP_SEQ_MOD;
        }
        s->max_seq = seq;
    } else if (udelta <= RTP_SEQ_MOD - MAX_MISORDER) {
        // sequence made a large jump...
        if (seq == s->bad_seq) {
            /* two sequential packets -- assume that the other side
             * restarted without telling us; just resync. */
            rtp_init_sequence(s, seq);
        } else {
            s->bad_seq = (seq + 1) & (RTP_SEQ_MOD - 1);
            return 0;
        }
    } else {
        // duplicate or reordered packet...
    }
    s->received++;
    return 1;
}

static void rtcp_update_jitter(RTPStatistics *s, uint32_t sent_timestamp,
                               uint32_t arrival_timestamp)
{
    // Most of this is pretty straight from RFC 3550 appendix A.8
    uint32_t transit = arrival_timestamp - sent_timestamp;
    uint32_t prev_transit = s->transit;
    int32_t d = transit - prev_transit;
    // Doing the FFABS() call directly on the "transit - prev_transit"
    // expression doesn't work, since it's an unsigned expression. Doing the
    // transit calculation in unsigned is desired though, since it most
    // probably will need to wrap around.
    d = FFABS(d);
    s->transit = transit;
    if (!prev_transit)
        return;
    s->jitter += d - (int32_t) ((s->jitter + 8) >> 4);
}

int ff_rtp_check_and_send_back_rr(RTPDemuxContext *s, URLContext *fd,
                                  AVIOContext *avio, int count)
{
    AVIOContext *pb;
    uint8_t *buf;
    int len;
    int rtcp_bytes;
    RTPStatistics *stats = &s->statistics;
    uint32_t lost;
    uint32_t extended_max;
    uint32_t expected_interval;
    uint32_t received_interval;
    int32_t  lost_interval;
    uint32_t expected;
    uint32_t fraction;

    if ((!fd && !avio) || (count < 1))
        return -1;

    /* TODO: I think this is way too often; RFC 1889 has algorithm for this */
    /* XXX: MPEG pts hardcoded. RTCP send every 0.5 seconds */
    s->octet_count += count;
    rtcp_bytes = ((s->octet_count - s->last_octet_count) * RTCP_TX_RATIO_NUM) /
        RTCP_TX_RATIO_DEN;
    rtcp_bytes /= 50; // mmu_man: that's enough for me... VLC sends much less btw !?
    if (rtcp_bytes < 28)
        return -1;
    s->last_octet_count = s->octet_count;

    if (!fd)
        pb = avio;
    else if (avio_open_dyn_buf(&pb) < 0)
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
    extended_max          = stats->cycles + stats->max_seq;
    expected              = extended_max - stats->base_seq;
    lost                  = expected - stats->received;
    lost                  = FFMIN(lost, 0xffffff); // clamp it since it's only 24 bits...
    expected_interval     = expected - stats->expected_prior;
    stats->expected_prior = expected;
    received_interval     = stats->received - stats->received_prior;
    stats->received_prior = stats->received;
    lost_interval         = expected_interval - received_interval;
    if (expected_interval == 0 || lost_interval <= 0)
        fraction = 0;
    else
        fraction = (lost_interval << 8) / expected_interval;

    fraction = (fraction << 24) | lost;

    avio_wb32(pb, fraction); /* 8 bits of fraction, 24 bits of total packets lost */
    avio_wb32(pb, extended_max); /* max sequence received */
    avio_wb32(pb, stats->jitter >> 4); /* jitter */

    if (s->last_rtcp_ntp_time == AV_NOPTS_VALUE) {
        avio_wb32(pb, 0); /* last SR timestamp */
        avio_wb32(pb, 0); /* delay since last SR */
    } else {
        uint32_t middle_32_bits   = s->last_rtcp_ntp_time >> 16; // this is valid, right? do we need to handle 64 bit values special?
        uint32_t delay_since_last = av_rescale(av_gettime_relative() - s->last_rtcp_reception_time,
                                               65536, AV_TIME_BASE);

        avio_wb32(pb, middle_32_bits); /* last SR timestamp */
        avio_wb32(pb, delay_since_last); /* delay since last SR */
    }

    // CNAME
    avio_w8(pb, (RTP_VERSION << 6) + 1); /* 1 report block */
    avio_w8(pb, RTCP_SDES);
    len = strlen(s->hostname);
    avio_wb16(pb, (7 + len + 3) / 4); /* length in words - 1 */
    avio_wb32(pb, s->ssrc + 1);
    avio_w8(pb, 0x01);
    avio_w8(pb, len);
    avio_write(pb, s->hostname, len);
    avio_w8(pb, 0); /* END */
    // padding
    for (len = (7 + len) % 4; len % 4; len++)
        avio_w8(pb, 0);

    avio_flush(pb);
    if (!fd)
        return 0;
    len = avio_close_dyn_buf(pb, &buf);
    if ((len > 0) && buf) {
        int av_unused result;
        av_log(s->ic, AV_LOG_TRACE, "sending %d bytes of RR\n", len);
        result = ffurl_write(fd, buf, len);
        av_log(s->ic, AV_LOG_TRACE, "result from ffurl_write: %d\n", result);
        av_free(buf);
    }
    return 0;
}

void ff_rtp_send_punch_packets(URLContext *rtp_handle)
{
    uint8_t buf[RTP_MIN_PACKET_LENGTH], *ptr = buf;

    /* Send a small RTP packet */

    bytestream_put_byte(&ptr, (RTP_VERSION << 6));
    bytestream_put_byte(&ptr, 0); /* Payload type */
    bytestream_put_be16(&ptr, 0); /* Seq */
    bytestream_put_be32(&ptr, 0); /* Timestamp */
    bytestream_put_be32(&ptr, 0); /* SSRC */

    ffurl_write(rtp_handle, buf, ptr - buf);

    /* Send a minimal RTCP RR */
    ptr = buf;
    bytestream_put_byte(&ptr, (RTP_VERSION << 6));
    bytestream_put_byte(&ptr, RTCP_RR); /* receiver report */
    bytestream_put_be16(&ptr, 1); /* length in words - 1 */
    bytestream_put_be32(&ptr, 0); /* our own SSRC */

    ffurl_write(rtp_handle, buf, ptr - buf);
}

static int find_missing_packets(RTPDemuxContext *s, uint16_t *first_missing,
                                uint16_t *missing_mask)
{
    int i;
    uint16_t next_seq = s->seq + 1;
    RTPPacket *pkt = s->queue;

    if (!pkt || pkt->seq == next_seq)
        return 0;

    *missing_mask = 0;
    for (i = 1; i <= 16; i++) {
        uint16_t missing_seq = next_seq + i;
        while (pkt) {
            int16_t diff = pkt->seq - missing_seq;
            if (diff >= 0)
                break;
            pkt = pkt->next;
        }
        if (!pkt)
            break;
        if (pkt->seq == missing_seq)
            continue;
        *missing_mask |= 1 << (i - 1);
    }

    *first_missing = next_seq;
    return 1;
}

int ff_rtp_send_rtcp_feedback(RTPDemuxContext *s, URLContext *fd,
                              AVIOContext *avio)
{
    int len, need_keyframe, missing_packets;
    AVIOContext *pb;
    uint8_t *buf;
    int64_t now;
    uint16_t first_missing = 0, missing_mask = 0;

    if (!fd && !avio)
        return -1;

    need_keyframe = s->handler && s->handler->need_keyframe &&
                    s->handler->need_keyframe(s->dynamic_protocol_context);
    missing_packets = find_missing_packets(s, &first_missing, &missing_mask);

    if (!need_keyframe && !missing_packets)
        return 0;

    /* Send new feedback if enough time has elapsed since the last
     * feedback packet. */

    now = av_gettime_relative();
    if (s->last_feedback_time &&
        (now - s->last_feedback_time) < MIN_FEEDBACK_INTERVAL)
        return 0;
    s->last_feedback_time = now;

    if (!fd)
        pb = avio;
    else if (avio_open_dyn_buf(&pb) < 0)
        return -1;

    if (need_keyframe) {
        avio_w8(pb, (RTP_VERSION << 6) | 1); /* PLI */
        avio_w8(pb, RTCP_PSFB);
        avio_wb16(pb, 2); /* length in words - 1 */
        // our own SSRC: we use the server's SSRC + 1 to avoid conflicts
        avio_wb32(pb, s->ssrc + 1);
        avio_wb32(pb, s->ssrc); // server SSRC
    }

    if (missing_packets) {
        avio_w8(pb, (RTP_VERSION << 6) | 1); /* NACK */
        avio_w8(pb, RTCP_RTPFB);
        avio_wb16(pb, 3); /* length in words - 1 */
        avio_wb32(pb, s->ssrc + 1);
        avio_wb32(pb, s->ssrc); // server SSRC

        avio_wb16(pb, first_missing);
        avio_wb16(pb, missing_mask);
    }

    avio_flush(pb);
    if (!fd)
        return 0;
    len = avio_close_dyn_buf(pb, &buf);
    if (len > 0 && buf) {
        ffurl_write(fd, buf, len);
        av_free(buf);
    }
    return 0;
}

/**
 * open a new RTP parse context for stream 'st'. 'st' can be NULL for
 * MPEG-2 TS streams.
 */
RTPDemuxContext *ff_rtp_parse_open(AVFormatContext *s1, AVStream *st,
                                   int payload_type, int queue_size)
{
    RTPDemuxContext *s;

    s = av_mallocz(sizeof(RTPDemuxContext));
    if (!s)
        return NULL;
    s->payload_type        = payload_type;
    s->last_rtcp_ntp_time  = AV_NOPTS_VALUE;
    s->first_rtcp_ntp_time = AV_NOPTS_VALUE;
    s->ic                  = s1;
    s->st                  = st;
    s->queue_size          = queue_size;

    av_log(s->ic, AV_LOG_VERBOSE, "setting jitter buffer size to %d\n",
           s->queue_size);

    rtp_init_statistics(&s->statistics, 0);
    if (st) {
        switch (st->codecpar->codec_id) {
        case AV_CODEC_ID_ADPCM_G722:
            /* According to RFC 3551, the stream clock rate is 8000
             * even if the sample rate is 16000. */
            if (st->codecpar->sample_rate == 8000)
                st->codecpar->sample_rate = 16000;
            break;
        case AV_CODEC_ID_PCM_MULAW: {
            AVCodecParameters *par = st->codecpar;
            par->bits_per_coded_sample = av_get_bits_per_sample(par->codec_id);
            par->block_align           = par->ch_layout.nb_channels * par->bits_per_coded_sample / 8;
            par->bit_rate              = par->block_align * 8LL * par->sample_rate;
            break;
        }
        default:
            break;
        }
    }
    // needed to send back RTCP RR in RTSP sessions
    gethostname(s->hostname, sizeof(s->hostname));
    return s;
}

void ff_rtp_parse_set_dynamic_protocol(RTPDemuxContext *s, PayloadContext *ctx,
                                       const RTPDynamicProtocolHandler *handler)
{
    s->dynamic_protocol_context = ctx;
    s->handler                  = handler;
}

void ff_rtp_parse_set_crypto(RTPDemuxContext *s, const char *suite,
                             const char *params)
{
    if (!ff_srtp_set_crypto(&s->srtp, suite, params))
        s->srtp_enabled = 1;
}

static int rtp_set_prft(RTPDemuxContext *s, AVPacket *pkt, uint32_t timestamp) {
    int64_t rtcp_time, delta_time;
    int32_t delta_timestamp;

    AVProducerReferenceTime *prft =
        (AVProducerReferenceTime *) av_packet_new_side_data(
            pkt, AV_PKT_DATA_PRFT, sizeof(AVProducerReferenceTime));
    if (!prft)
        return AVERROR(ENOMEM);

    rtcp_time = ff_parse_ntp_time(s->last_rtcp_ntp_time) - NTP_OFFSET_US;
    /* Cast to int32_t to handle timestamp wraparound correctly */
    delta_timestamp = (int32_t)(timestamp - s->last_rtcp_timestamp);
    delta_time = av_rescale_q(delta_timestamp, s->st->time_base, AV_TIME_BASE_Q);

    prft->wallclock = rtcp_time + delta_time;
    prft->flags = 24;
    return 0;
}

/**
 * This was the second switch in rtp_parse packet.
 * Normalizes time, if required, sets stream_index, etc.
 */
static void finalize_packet(RTPDemuxContext *s, AVPacket *pkt, uint32_t timestamp)
{
    if (pkt->pts != AV_NOPTS_VALUE || pkt->dts != AV_NOPTS_VALUE)
        return; /* Timestamp already set by depacketizer */
    if (timestamp == RTP_NOTS_VALUE)
        return;

    if (s->last_rtcp_ntp_time != AV_NOPTS_VALUE) {
        if (rtp_set_prft(s, pkt, timestamp) < 0) {
            av_log(s->ic, AV_LOG_WARNING, "rtpdec: failed to set prft");
        }
    }

    if (s->last_rtcp_ntp_time != AV_NOPTS_VALUE && s->ic->nb_streams > 1) {
        int64_t addend;
        int32_t delta_timestamp;

        /* compute pts from timestamp with received ntp_time */
        /* Cast to int32_t to handle timestamp wraparound correctly */
        delta_timestamp = (int32_t)(timestamp - s->last_rtcp_timestamp);
        /* convert to the PTS timebase */
        addend = av_rescale(s->last_rtcp_ntp_time - s->first_rtcp_ntp_time,
                            s->st->time_base.den,
                            (uint64_t) s->st->time_base.num << 32);
        pkt->pts = s->range_start_offset + s->rtcp_ts_offset + addend +
                   delta_timestamp;
        return;
    }

    if (!s->base_timestamp)
        s->base_timestamp = timestamp;
    /* assume that the difference is INT32_MIN < x < INT32_MAX,
     * but allow the first timestamp to exceed INT32_MAX */
    if (!s->timestamp)
        s->unwrapped_timestamp += timestamp;
    else
        s->unwrapped_timestamp += (int32_t)(timestamp - s->timestamp);
    s->timestamp = timestamp;
    pkt->pts     = s->unwrapped_timestamp + s->range_start_offset -
                   s->base_timestamp;
}

static int rtp_parse_packet_internal(RTPDemuxContext *s, AVPacket *pkt,
                                     const uint8_t *buf, int len)
{
    unsigned int ssrc;
    int payload_type, seq, flags = 0;
    int ext, csrc;
    AVStream *st;
    uint32_t timestamp;
    int rv = 0;

    csrc         = buf[0] & 0x0f;
    ext          = buf[0] & 0x10;
    payload_type = buf[1] & 0x7f;
    if (buf[1] & 0x80)
        flags |= RTP_FLAG_MARKER;
    seq       = AV_RB16(buf + 2);
    timestamp = AV_RB32(buf + 4);
    ssrc      = AV_RB32(buf + 8);
    /* store the ssrc in the RTPDemuxContext */
    s->ssrc = ssrc;

    /* NOTE: we can handle only one payload type */
    if (s->payload_type != payload_type)
        return -1;

    st = s->st;
    // only do something with this if all the rtp checks pass...
    if (!rtp_valid_packet_in_sequence(&s->statistics, seq)) {
        av_log(s->ic, AV_LOG_ERROR,
               "RTP: PT=%02x: bad cseq %04x expected=%04x\n",
               payload_type, seq, ((s->seq + 1) & 0xffff));
        return -1;
    }

    if (buf[0] & 0x20) {
        int padding = buf[len - 1];
        if (len >= 12 + padding)
            len -= padding;
    }

    s->seq = seq;
    len   -= 12;
    buf   += 12;

    len   -= 4 * csrc;
    buf   += 4 * csrc;
    if (len < 0)
        return AVERROR_INVALIDDATA;

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

    if (s->handler && s->handler->parse_packet) {
        rv = s->handler->parse_packet(s->ic, s->dynamic_protocol_context,
                                      s->st, pkt, &timestamp, buf, len, seq,
                                      flags);
    } else if (st) {
        if ((rv = av_new_packet(pkt, len)) < 0)
            return rv;
        memcpy(pkt->data, buf, len);
        pkt->stream_index = st->index;
    } else {
        return AVERROR(EINVAL);
    }

    // now perform timestamp things....
    finalize_packet(s, pkt, timestamp);

    return rv;
}

void ff_rtp_reset_packet_queue(RTPDemuxContext *s)
{
    while (s->queue) {
        RTPPacket *next = s->queue->next;
        av_freep(&s->queue->buf);
        av_freep(&s->queue);
        s->queue = next;
    }
    s->seq       = 0;
    s->queue_len = 0;
    s->prev_ret  = 0;
}

static int enqueue_packet(RTPDemuxContext *s, uint8_t *buf, int len)
{
    uint16_t seq   = AV_RB16(buf + 2);
    RTPPacket **cur = &s->queue, *packet;

    /* Find the correct place in the queue to insert the packet */
    while (*cur) {
        int16_t diff = seq - (*cur)->seq;
        if (diff < 0)
            break;
        cur = &(*cur)->next;
    }

    packet = av_mallocz(sizeof(*packet));
    if (!packet)
        return AVERROR(ENOMEM);
    packet->recvtime = av_gettime_relative();
    packet->seq      = seq;
    packet->len      = len;
    packet->buf      = buf;
    packet->next     = *cur;
    *cur = packet;
    s->queue_len++;

    return 0;
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

    if (!has_next_packet(s)) {
        int pkt_missed  = s->queue->seq - s->seq - 1;

        if (pkt_missed < 0)
            pkt_missed += UINT16_MAX;
        av_log(s->ic, AV_LOG_WARNING,
               "RTP: missed %d packets\n", pkt_missed);
    }

    /* Parse the first packet in the queue, and dequeue it */
    rv   = rtp_parse_packet_internal(s, pkt, s->queue->buf, s->queue->len);
    next = s->queue->next;
    av_freep(&s->queue->buf);
    av_freep(&s->queue);
    s->queue = next;
    s->queue_len--;
    return rv;
}

static int rtp_parse_one_packet(RTPDemuxContext *s, AVPacket *pkt,
                                uint8_t **bufptr, int len)
{
    uint8_t *buf = bufptr ? *bufptr : NULL;
    int flags = 0;
    uint32_t timestamp;
    int rv = 0;

    if (!buf) {
        /* If parsing of the previous packet actually returned 0 or an error,
         * there's nothing more to be parsed from that packet, but we may have
         * indicated that we can return the next enqueued packet. */
        if (s->prev_ret <= 0)
            return rtp_parse_queued_packet(s, pkt);
        /* return the next packets, if any */
        if (s->handler && s->handler->parse_packet) {
            /* timestamp should be overwritten by parse_packet, if not,
             * the packet is left with pts == AV_NOPTS_VALUE */
            timestamp = RTP_NOTS_VALUE;
            rv        = s->handler->parse_packet(s->ic, s->dynamic_protocol_context,
                                                 s->st, pkt, &timestamp, NULL, 0, 0,
                                                 flags);
            finalize_packet(s, pkt, timestamp);
            return rv;
        }
    }

    if (len < 12)
        return -1;

    if ((buf[0] & 0xc0) != (RTP_VERSION << 6))
        return -1;
    if (RTP_PT_IS_RTCP(buf[1])) {
        return rtcp_parse_packet(s, buf, len);
    }

    if (s->st) {
        int64_t received = av_gettime_relative();
        uint32_t arrival_ts = av_rescale_q(received, AV_TIME_BASE_Q,
                                           s->st->time_base);
        timestamp = AV_RB32(buf + 4);
        // Calculate the jitter immediately, before queueing the packet
        // into the reordering queue.
        rtcp_update_jitter(&s->statistics, timestamp, arrival_ts);
    }

    if ((s->seq == 0 && !s->queue) || s->queue_size <= 1) {
        /* First packet, or no reordering */
        return rtp_parse_packet_internal(s, pkt, buf, len);
    } else {
        uint16_t seq = AV_RB16(buf + 2);
        int16_t diff = seq - s->seq;
        if (diff < 0) {
            /* Packet older than the previously emitted one, drop */
            av_log(s->ic, AV_LOG_WARNING,
                   "RTP: dropping old packet received too late\n");
            return -1;
        } else if (diff <= 1) {
            /* Correct packet */
            rv = rtp_parse_packet_internal(s, pkt, buf, len);
            return rv;
        } else {
            /* Still missing some packet, enqueue this one. */
            rv = enqueue_packet(s, buf, len);
            if (rv < 0)
                return rv;
            *bufptr = NULL;
            /* Return the first enqueued packet if the queue is full,
             * even if we're missing something */
            if (s->queue_len >= s->queue_size) {
                av_log(s->ic, AV_LOG_WARNING, "jitter buffer full\n");
                return rtp_parse_queued_packet(s, pkt);
            }
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
    int rv;
    if (s->srtp_enabled && bufptr && ff_srtp_decrypt(&s->srtp, *bufptr, &len) < 0)
        return -1;
    rv = rtp_parse_one_packet(s, pkt, bufptr, len);
    s->prev_ret = rv;
    while (rv < 0 && has_next_packet(s))
        rv = rtp_parse_queued_packet(s, pkt);
    return rv ? rv : has_next_packet(s);
}

void ff_rtp_parse_close(RTPDemuxContext *s)
{
    ff_rtp_reset_packet_queue(s);
    ff_srtp_free(&s->srtp);
    av_free(s);
}

int ff_parse_fmtp(AVFormatContext *s,
                  AVStream *stream, PayloadContext *data, const char *p,
                  int (*parse_fmtp)(AVFormatContext *s,
                                    AVStream *stream,
                                    PayloadContext *data,
                                    const char *attr, const char *value))
{
    char attr[256];
    char *value;
    int res;
    int value_size = strlen(p) + 1;

    if (!(value = av_malloc(value_size))) {
        av_log(s, AV_LOG_ERROR, "Failed to allocate data for FMTP.\n");
        return AVERROR(ENOMEM);
    }

    // remove protocol identifier
    while (*p && *p == ' ')
        p++;                     // strip spaces
    while (*p && *p != ' ')
        p++;                     // eat protocol identifier
    while (*p && *p == ' ')
        p++;                     // strip trailing spaces

    while (ff_rtsp_next_attr_and_value(&p,
                                       attr, sizeof(attr),
                                       value, value_size)) {
        res = parse_fmtp(s, stream, data, attr, value);
        if (res < 0 && res != AVERROR_PATCHWELCOME) {
            av_free(value);
            return res;
        }
    }
    av_free(value);
    return 0;
}

int ff_rtp_finalize_packet(AVPacket *pkt, AVIOContext **dyn_buf, int stream_idx)
{
    int ret;
    av_packet_unref(pkt);

    pkt->size         = avio_close_dyn_buf(*dyn_buf, &pkt->data);
    pkt->stream_index = stream_idx;
    *dyn_buf = NULL;
    if ((ret = av_packet_from_data(pkt, pkt->data, pkt->size)) < 0) {
        av_freep(&pkt->data);
        return ret;
    }
    return pkt->size;
}
