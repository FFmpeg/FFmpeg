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
#include "rtp_mpv.h"
#include "rtp_aac.h"

//#define DEBUG

#define RTCP_SR_SIZE 28

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
  {32, "MPV",        CODEC_TYPE_VIDEO,   CODEC_ID_MPEG2VIDEO, 90000, -1},
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

int rtp_get_codec_info(AVCodecContext *codec, int payload_type)
{
    int i = 0;

    for (i = 0; AVRtpPayloadTypes[i].pt >= 0; i++)
        if (AVRtpPayloadTypes[i].pt == payload_type) {
            if (AVRtpPayloadTypes[i].codec_id != CODEC_ID_NONE) {
                codec->codec_type = AVRtpPayloadTypes[i].codec_type;
                codec->codec_id = AVRtpPayloadTypes[i].codec_id;
                if (AVRtpPayloadTypes[i].audio_channels > 0)
                    codec->channels = AVRtpPayloadTypes[i].audio_channels;
                if (AVRtpPayloadTypes[i].clock_rate > 0)
                    codec->sample_rate = AVRtpPayloadTypes[i].clock_rate;
                return 0;
            }
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

const char *ff_rtp_enc_name(int payload_type)
{
    int i;

    for (i = 0; AVRtpPayloadTypes[i].pt >= 0; i++)
        if (AVRtpPayloadTypes[i].pt == payload_type) {
            return AVRtpPayloadTypes[i].enc_name;
        }

    return "";
}

enum CodecID ff_rtp_codec_id(const char *buf, enum CodecType codec_type)
{
    int i;

    for (i = 0; AVRtpPayloadTypes[i].pt >= 0; i++)
        if (!strcmp(buf, AVRtpPayloadTypes[i].enc_name) && (codec_type == AVRtpPayloadTypes[i].codec_type)){
            return AVRtpPayloadTypes[i].codec_id;
        }

    return CODEC_ID_NONE;
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

// following 2 FIXMEs could be set based on the current time, there is normally no info leak, as RTP will likely be transmitted immediately
    s->base_timestamp = 0; /* FIXME: was random(), what should this be? */
    s->timestamp = s->base_timestamp;
    s->cur_timestamp = 0;
    s->ssrc = 0; /* FIXME: was random(), what should this be? */
    s->first_packet = 1;
    s->first_rtcp_ntp_time = AV_NOPTS_VALUE;

    max_packet_size = url_fget_max_packet_size(s1->pb);
    if (max_packet_size <= 12)
        return AVERROR(EIO);
    s->max_payload_size = max_packet_size - 12;

    s->max_frames_per_packet = 0;
    if (s1->max_delay) {
        if (st->codec->codec_type == CODEC_TYPE_AUDIO) {
            if (st->codec->frame_size == 0) {
                av_log(s1, AV_LOG_ERROR, "Cannot respect max delay: frame size = 0\n");
            } else {
                s->max_frames_per_packet = av_rescale_rnd(s1->max_delay, st->codec->sample_rate, AV_TIME_BASE * st->codec->frame_size, AV_ROUND_DOWN);
            }
        }
        if (st->codec->codec_type == CODEC_TYPE_VIDEO) {
            /* FIXME: We should round down here... */
            s->max_frames_per_packet = av_rescale_q(s1->max_delay, AV_TIME_BASE_Q, st->codec->time_base);
        }
    }

    av_set_pts_info(st, 32, 1, 90000);
    switch(st->codec->codec_id) {
    case CODEC_ID_MP2:
    case CODEC_ID_MP3:
        s->buf_ptr = s->buf + 4;
        break;
    case CODEC_ID_MPEG1VIDEO:
    case CODEC_ID_MPEG2VIDEO:
        break;
    case CODEC_ID_MPEG2TS:
        n = s->max_payload_size / TS_PACKET_SIZE;
        if (n < 1)
            n = 1;
        s->max_payload_size = n * TS_PACKET_SIZE;
        s->buf_ptr = s->buf;
        break;
    case CODEC_ID_AAC:
        s->read_buf_index = 0;
    default:
        if (st->codec->codec_type == CODEC_TYPE_AUDIO) {
            av_set_pts_info(st, 32, 1, st->codec->sample_rate);
        }
        s->buf_ptr = s->buf;
        break;
    }

    return 0;
}

/* send an rtcp sender report packet */
static void rtcp_send_sr(AVFormatContext *s1, int64_t ntp_time)
{
    RTPDemuxContext *s = s1->priv_data;
    uint32_t rtp_ts;

#if defined(DEBUG)
    printf("RTCP: %02x %"PRIx64" %x\n", s->payload_type, ntp_time, s->timestamp);
#endif

    if (s->first_rtcp_ntp_time == AV_NOPTS_VALUE) s->first_rtcp_ntp_time = ntp_time;
    s->last_rtcp_ntp_time = ntp_time;
    rtp_ts = av_rescale_q(ntp_time - s->first_rtcp_ntp_time, AV_TIME_BASE_Q,
                          s1->streams[0]->time_base) + s->base_timestamp;
    put_byte(s1->pb, (RTP_VERSION << 6));
    put_byte(s1->pb, 200);
    put_be16(s1->pb, 6); /* length in words - 1 */
    put_be32(s1->pb, s->ssrc);
    put_be32(s1->pb, ntp_time / 1000000);
    put_be32(s1->pb, ((ntp_time % 1000000) << 32) / 1000000);
    put_be32(s1->pb, rtp_ts);
    put_be32(s1->pb, s->packet_count);
    put_be32(s1->pb, s->octet_count);
    put_flush_packet(s1->pb);
}

/* send an rtp packet. sequence number is incremented, but the caller
   must update the timestamp itself */
void ff_rtp_send_data(AVFormatContext *s1, const uint8_t *buf1, int len, int m)
{
    RTPDemuxContext *s = s1->priv_data;

#ifdef DEBUG
    printf("rtp_send_data size=%d\n", len);
#endif

    /* build the RTP header */
    put_byte(s1->pb, (RTP_VERSION << 6));
    put_byte(s1->pb, (s->payload_type & 0x7f) | ((m & 0x01) << 7));
    put_be16(s1->pb, s->seq);
    put_be32(s1->pb, s->timestamp);
    put_be32(s1->pb, s->ssrc);

    put_buffer(s1->pb, buf1, len);
    put_flush_packet(s1->pb);

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
    n = 0;
    while (size > 0) {
        s->buf_ptr = s->buf;
        len = FFMIN(max_packet_size, size);

        /* copy data */
        memcpy(s->buf_ptr, buf1, len);
        s->buf_ptr += len;
        buf1 += len;
        size -= len;
        s->timestamp = s->cur_timestamp + n / sample_size;
        ff_rtp_send_data(s1, s->buf, s->buf_ptr - s->buf, 0);
        n += (s->buf_ptr - s->buf);
    }
}

/* NOTE: we suppose that exactly one frame is given as argument here */
/* XXX: test it */
static void rtp_send_mpegaudio(AVFormatContext *s1,
                               const uint8_t *buf1, int size)
{
    RTPDemuxContext *s = s1->priv_data;
    int len, count, max_packet_size;

    max_packet_size = s->max_payload_size;

    /* test if we must flush because not enough space */
    len = (s->buf_ptr - s->buf);
    if ((len + size) > max_packet_size) {
        if (len > 4) {
            ff_rtp_send_data(s1, s->buf, s->buf_ptr - s->buf, 0);
            s->buf_ptr = s->buf + 4;
        }
    }
    if (s->buf_ptr == s->buf + 4) {
        s->timestamp = s->cur_timestamp;
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
            ff_rtp_send_data(s1, s->buf, len + 4, 0);
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
}

static void rtp_send_raw(AVFormatContext *s1,
                         const uint8_t *buf1, int size)
{
    RTPDemuxContext *s = s1->priv_data;
    int len, max_packet_size;

    max_packet_size = s->max_payload_size;

    while (size > 0) {
        len = max_packet_size;
        if (len > size)
            len = size;

        s->timestamp = s->cur_timestamp;
        ff_rtp_send_data(s1, buf1, len, (len == size));

        buf1 += len;
        size -= len;
    }
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
            ff_rtp_send_data(s1, s->buf, out_len, 0);
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
    int size= pkt->size;
    uint8_t *buf1= pkt->data;

#ifdef DEBUG
    printf("%d: write len=%d\n", pkt->stream_index, size);
#endif

    /* XXX: mpeg pts hardcoded. RTCP send every 0.5 seconds */
    rtcp_bytes = ((s->octet_count - s->last_octet_count) * RTCP_TX_RATIO_NUM) /
        RTCP_TX_RATIO_DEN;
    if (s->first_packet || ((rtcp_bytes >= RTCP_SR_SIZE) &&
                           (av_gettime() - s->last_rtcp_ntp_time > 5000000))) {
        rtcp_send_sr(s1, av_gettime());
        s->last_octet_count = s->octet_count;
        s->first_packet = 0;
    }
    s->cur_timestamp = s->base_timestamp + pkt->pts;

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
    case CODEC_ID_MPEG2VIDEO:
        ff_rtp_send_mpegvideo(s1, buf1, size);
        break;
    case CODEC_ID_AAC:
        ff_rtp_send_aac(s1, buf1, size);
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
};
