/*
 * RTP input/output format
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
#include "avformat.h"
#include "mpegts.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#ifndef __BEOS__
# include <arpa/inet.h>
#else
# include "barpainet.h"
#endif
#include <netdb.h>

//#define DEBUG


/* TODO: - add RTCP statistics reporting (should be optional).

         - add support for h263/mpeg4 packetized output : IDEA: send a
         buffer to 'rtp_write_packet' contains all the packets for ONE
         frame. Each packet should have a four byte header containing
         the length in big endian format (same trick as
         'url_open_dyn_packet_buf') 
*/

#define RTP_VERSION 2

#define RTP_MAX_SDES 256   /* maximum text length for SDES */

/* RTCP paquets use 0.5 % of the bandwidth */
#define RTCP_TX_RATIO_NUM 5
#define RTCP_TX_RATIO_DEN 1000

typedef enum {
  RTCP_SR   = 200,
  RTCP_RR   = 201,
  RTCP_SDES = 202,
  RTCP_BYE  = 203,
  RTCP_APP  = 204
} rtcp_type_t;

typedef enum {
  RTCP_SDES_END    =  0,
  RTCP_SDES_CNAME  =  1,
  RTCP_SDES_NAME   =  2,
  RTCP_SDES_EMAIL  =  3,
  RTCP_SDES_PHONE  =  4,
  RTCP_SDES_LOC    =  5,
  RTCP_SDES_TOOL   =  6,
  RTCP_SDES_NOTE   =  7,
  RTCP_SDES_PRIV   =  8, 
  RTCP_SDES_IMG    =  9,
  RTCP_SDES_DOOR   = 10,
  RTCP_SDES_SOURCE = 11
} rtcp_sdes_type_t;

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
    MpegTSContext *ts; /* only used for RTP_PT_MPEG2TS payloads */
    int read_buf_index;
    int read_buf_size;
    
    /* rtcp sender statistics receive */
    int64_t last_rtcp_ntp_time;
    int64_t first_rtcp_ntp_time;
    uint32_t last_rtcp_timestamp;
    /* rtcp sender statistics */
    unsigned int packet_count;
    unsigned int octet_count;
    unsigned int last_octet_count;
    int first_packet;
    /* buffer for output */
    uint8_t buf[RTP_MAX_PACKET_LENGTH];
    uint8_t *buf_ptr;
};

int rtp_get_codec_info(AVCodecContext *codec, int payload_type)
{
    switch(payload_type) {
    case RTP_PT_ULAW:
        codec->codec_type = CODEC_TYPE_AUDIO;
        codec->codec_id = CODEC_ID_PCM_MULAW;
        codec->channels = 1;
        codec->sample_rate = 8000;
        break;
    case RTP_PT_ALAW:
        codec->codec_type = CODEC_TYPE_AUDIO;
        codec->codec_id = CODEC_ID_PCM_ALAW;
        codec->channels = 1;
        codec->sample_rate = 8000;
        break;
    case RTP_PT_S16BE_STEREO:
        codec->codec_type = CODEC_TYPE_AUDIO;
        codec->codec_id = CODEC_ID_PCM_S16BE;
        codec->channels = 2;
        codec->sample_rate = 44100;
        break;
    case RTP_PT_S16BE_MONO:
        codec->codec_type = CODEC_TYPE_AUDIO;
        codec->codec_id = CODEC_ID_PCM_S16BE;
        codec->channels = 1;
        codec->sample_rate = 44100;
        break;
    case RTP_PT_MPEGAUDIO:
        codec->codec_type = CODEC_TYPE_AUDIO;
        codec->codec_id = CODEC_ID_MP2;
        break;
    case RTP_PT_JPEG:
        codec->codec_type = CODEC_TYPE_VIDEO;
        codec->codec_id = CODEC_ID_MJPEG;
        break;
    case RTP_PT_MPEGVIDEO:
        codec->codec_type = CODEC_TYPE_VIDEO;
        codec->codec_id = CODEC_ID_MPEG1VIDEO;
        break;
    case RTP_PT_MPEG2TS:
        codec->codec_type = CODEC_TYPE_DATA;
        codec->codec_id = CODEC_ID_MPEG2TS;
        break;
    default:
        return -1;
    }
    return 0;
}

/* return < 0 if unknown payload type */
int rtp_get_payload_type(AVCodecContext *codec)
{
    int payload_type;

    /* compute the payload type */
    payload_type = -1;
    switch(codec->codec_id) {
    case CODEC_ID_PCM_MULAW:
        payload_type = RTP_PT_ULAW;
        break;
    case CODEC_ID_PCM_ALAW:
        payload_type = RTP_PT_ALAW;
        break;
    case CODEC_ID_PCM_S16BE:
        if (codec->channels == 1) {
            payload_type = RTP_PT_S16BE_MONO;
        } else if (codec->channels == 2) {
            payload_type = RTP_PT_S16BE_STEREO;
        }
        break;
    case CODEC_ID_MP2:
    case CODEC_ID_MP3:
        payload_type = RTP_PT_MPEGAUDIO;
        break;
    case CODEC_ID_MJPEG:
        payload_type = RTP_PT_JPEG;
        break;
    case CODEC_ID_MPEG1VIDEO:
        payload_type = RTP_PT_MPEGVIDEO;
        break;
    case CODEC_ID_MPEG2TS:
        payload_type = RTP_PT_MPEG2TS;
        break;
    default:
        break;
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

/**
 * open a new RTP parse context for stream 'st'. 'st' can be NULL for
 * MPEG2TS streams to indicate that they should be demuxed inside the
 * rtp demux (otherwise CODEC_ID_MPEG2TS packets are returned) 
 */
RTPDemuxContext *rtp_parse_open(AVFormatContext *s1, AVStream *st, int payload_type)
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
    if (payload_type == RTP_PT_MPEG2TS) {
        s->ts = mpegts_parse_open(s->ic);
        if (s->ts == NULL) {
            av_free(s);
            return NULL;
        }
    } else {
        switch(st->codec.codec_id) {
        case CODEC_ID_MPEG1VIDEO:
        case CODEC_ID_MPEG2VIDEO:
        case CODEC_ID_MP2:
        case CODEC_ID_MP3:
        case CODEC_ID_MPEG4:
            st->need_parsing = 1;
            break;
        default:
            break;
        }
    }
    return s;
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
    int payload_type, seq, delta_timestamp, ret;
    AVStream *st;
    uint32_t timestamp;
    
    if (!buf) {
        /* return the next packets, if any */
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
    
    /* NOTE: we can handle only one payload type */
    if (s->payload_type != payload_type)
        return -1;
#if defined(DEBUG) || 1
    if (seq != ((s->seq + 1) & 0xffff)) {
        av_log(&s->st->codec, AV_LOG_ERROR, "RTP: PT=%02x: bad cseq %04x expected=%04x\n", 
               payload_type, seq, ((s->seq + 1) & 0xffff));
    }
    s->seq = seq;
#endif
    len -= 12;
    buf += 12;

    st = s->st;
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
        switch(st->codec.codec_id) {
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
        default:
            av_new_packet(pkt, len);
            memcpy(pkt->data, buf, len);
            break;
        }
        
        switch(st->codec.codec_id) {
        case CODEC_ID_MP2:
        case CODEC_ID_MPEG1VIDEO:
            if (s->last_rtcp_ntp_time != AV_NOPTS_VALUE) {
                int64_t addend;
                /* XXX: is it really necessary to unify the timestamp base ? */
                /* compute pts from timestamp with received ntp_time */
                delta_timestamp = timestamp - s->last_rtcp_timestamp;
                /* convert to 90 kHz without overflow */
                addend = (s->last_rtcp_ntp_time - s->first_rtcp_ntp_time) >> 14;
                addend = (addend * 5625) >> 14;
                pkt->pts = addend + delta_timestamp;
            }
            break;
        default:
            /* no timestamp info yet */
            break;
        }
        pkt->stream_index = s->st->index;
    }
    return 0;
}

void rtp_parse_close(RTPDemuxContext *s)
{
    if (s->payload_type == RTP_PT_MPEG2TS) {
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

    payload_type = rtp_get_payload_type(&st->codec);
    if (payload_type < 0)
        payload_type = RTP_PT_PRIVATE; /* private payload type */
    s->payload_type = payload_type;

    s->base_timestamp = random();
    s->timestamp = s->base_timestamp;
    s->ssrc = random();
    s->first_packet = 1;

    max_packet_size = url_fget_max_packet_size(&s1->pb);
    if (max_packet_size <= 12)
        return AVERROR_IO;
    s->max_payload_size = max_packet_size - 12;

    switch(st->codec.codec_id) {
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
    printf("RTCP: %02x %Lx %x\n", s->payload_type, ntp_time, s->timestamp);
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
static void rtp_send_data(AVFormatContext *s1, const uint8_t *buf1, int len)
{
    RTPDemuxContext *s = s1->priv_data;

#ifdef DEBUG
    printf("rtp_send_data size=%d\n", len);
#endif

    /* build the RTP header */
    put_byte(&s1->pb, (RTP_VERSION << 6));
    put_byte(&s1->pb, s->payload_type & 0x7f);
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
            rtp_send_data(s1, s->buf, n);
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
            rtp_send_data(s1, s->buf, s->buf_ptr - s->buf);
            s->buf_ptr = s->buf + 4;
            /* 90 KHz time stamp */
            s->timestamp = s->base_timestamp + 
                (s->cur_timestamp * 90000LL) / st->codec.sample_rate;
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
            rtp_send_data(s1, s->buf, len + 4);
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
    s->cur_timestamp += st->codec.frame_size;
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
        if (st->codec.sub_id == 2)
            h |= 1 << 26; /* mpeg 2 indicator */
        q = s->buf;
        *q++ = h >> 24;
        *q++ = h >> 16;
        *q++ = h >> 8;
        *q++ = h;

        if (st->codec.sub_id == 2) {
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
            av_rescale((int64_t)s->cur_timestamp * st->codec.frame_rate_base, 90000, st->codec.frame_rate);
        rtp_send_data(s1, s->buf, q - s->buf);

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
            av_rescale((int64_t)s->cur_timestamp * st->codec.frame_rate_base, 90000, st->codec.frame_rate);
        rtp_send_data(s1, buf1, len);

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
            rtp_send_data(s1, s->buf, out_len);
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

    switch(st->codec.codec_id) {
    case CODEC_ID_PCM_MULAW:
    case CODEC_ID_PCM_ALAW:
    case CODEC_ID_PCM_U8:
    case CODEC_ID_PCM_S8:
        rtp_send_samples(s1, buf1, size, 1 * st->codec.channels);
        break;
    case CODEC_ID_PCM_U16BE:
    case CODEC_ID_PCM_U16LE:
    case CODEC_ID_PCM_S16BE:
    case CODEC_ID_PCM_S16LE:
        rtp_send_samples(s1, buf1, size, 2 * st->codec.channels);
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

AVOutputFormat rtp_mux = {
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

int rtp_init(void)
{
    av_register_output_format(&rtp_mux);
    return 0;
}
