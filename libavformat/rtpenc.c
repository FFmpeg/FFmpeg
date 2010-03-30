/*
 * RTP output format
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

#include "avformat.h"
#include "mpegts.h"
#include "internal.h"
#include "libavutil/random_seed.h"

#include <unistd.h>

#include "rtpenc.h"

//#define DEBUG

#define RTCP_SR_SIZE 28

static int is_supported(enum CodecID id)
{
    switch(id) {
    case CODEC_ID_H263:
    case CODEC_ID_H263P:
    case CODEC_ID_H264:
    case CODEC_ID_MPEG1VIDEO:
    case CODEC_ID_MPEG2VIDEO:
    case CODEC_ID_MPEG4:
    case CODEC_ID_AAC:
    case CODEC_ID_MP2:
    case CODEC_ID_MP3:
    case CODEC_ID_PCM_ALAW:
    case CODEC_ID_PCM_MULAW:
    case CODEC_ID_PCM_S8:
    case CODEC_ID_PCM_S16BE:
    case CODEC_ID_PCM_S16LE:
    case CODEC_ID_PCM_U16BE:
    case CODEC_ID_PCM_U16LE:
    case CODEC_ID_PCM_U8:
    case CODEC_ID_MPEG2TS:
    case CODEC_ID_AMR_NB:
    case CODEC_ID_AMR_WB:
        return 1;
    default:
        return 0;
    }
}

static int rtp_write_header(AVFormatContext *s1)
{
    RTPMuxContext *s = s1->priv_data;
    int max_packet_size, n;
    AVStream *st;

    if (s1->nb_streams != 1)
        return -1;
    st = s1->streams[0];
    if (!is_supported(st->codec->codec_id)) {
        av_log(s1, AV_LOG_ERROR, "Unsupported codec %x\n", st->codec->codec_id);

        return -1;
    }

    s->payload_type = ff_rtp_get_payload_type(st->codec);
    if (s->payload_type < 0)
        s->payload_type = RTP_PT_PRIVATE + (st->codec->codec_type == AVMEDIA_TYPE_AUDIO);

    s->base_timestamp = ff_random_get_seed();
    s->timestamp = s->base_timestamp;
    s->cur_timestamp = 0;
    s->ssrc = ff_random_get_seed();
    s->first_packet = 1;
    s->first_rtcp_ntp_time = ff_ntp_time();
    if (s1->start_time_realtime)
        /* Round the NTP time to whole milliseconds. */
        s->first_rtcp_ntp_time = (s1->start_time_realtime / 1000) * 1000 +
                                 NTP_OFFSET_US;

    max_packet_size = url_fget_max_packet_size(s1->pb);
    if (max_packet_size <= 12)
        return AVERROR(EIO);
    s->buf = av_malloc(max_packet_size);
    if (s->buf == NULL) {
        return AVERROR(ENOMEM);
    }
    s->max_payload_size = max_packet_size - 12;

    s->max_frames_per_packet = 0;
    if (s1->max_delay) {
        if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (st->codec->frame_size == 0) {
                av_log(s1, AV_LOG_ERROR, "Cannot respect max delay: frame size = 0\n");
            } else {
                s->max_frames_per_packet = av_rescale_rnd(s1->max_delay, st->codec->sample_rate, AV_TIME_BASE * st->codec->frame_size, AV_ROUND_DOWN);
            }
        }
        if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            /* FIXME: We should round down here... */
            s->max_frames_per_packet = av_rescale_q(s1->max_delay, (AVRational){1, 1000000}, st->codec->time_base);
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
    case CODEC_ID_AMR_NB:
    case CODEC_ID_AMR_WB:
        if (!s->max_frames_per_packet)
            s->max_frames_per_packet = 12;
        if (st->codec->codec_id == CODEC_ID_AMR_NB)
            n = 31;
        else
            n = 61;
        /* max_header_toc_size + the largest AMR payload must fit */
        if (1 + s->max_frames_per_packet + n > s->max_payload_size) {
            av_log(s1, AV_LOG_ERROR, "RTP max payload size too small for AMR\n");
            return -1;
        }
        if (st->codec->channels != 1) {
            av_log(s1, AV_LOG_ERROR, "Only mono is supported\n");
            return -1;
        }
    case CODEC_ID_AAC:
        s->num_frames = 0;
    default:
        if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
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
    RTPMuxContext *s = s1->priv_data;
    uint32_t rtp_ts;

    dprintf(s1, "RTCP: %02x %"PRIx64" %x\n", s->payload_type, ntp_time, s->timestamp);

    s->last_rtcp_ntp_time = ntp_time;
    rtp_ts = av_rescale_q(ntp_time - s->first_rtcp_ntp_time, (AVRational){1, 1000000},
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
    RTPMuxContext *s = s1->priv_data;

    dprintf(s1, "rtp_send_data size=%d\n", len);

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
    RTPMuxContext *s = s1->priv_data;
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

static void rtp_send_mpegaudio(AVFormatContext *s1,
                               const uint8_t *buf1, int size)
{
    RTPMuxContext *s = s1->priv_data;
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
    RTPMuxContext *s = s1->priv_data;
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
    RTPMuxContext *s = s1->priv_data;
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

static int rtp_write_packet(AVFormatContext *s1, AVPacket *pkt)
{
    RTPMuxContext *s = s1->priv_data;
    AVStream *st = s1->streams[0];
    int rtcp_bytes;
    int size= pkt->size;

    dprintf(s1, "%d: write len=%d\n", pkt->stream_index, size);

    rtcp_bytes = ((s->octet_count - s->last_octet_count) * RTCP_TX_RATIO_NUM) /
        RTCP_TX_RATIO_DEN;
    if (s->first_packet || ((rtcp_bytes >= RTCP_SR_SIZE) &&
                           (ff_ntp_time() - s->last_rtcp_ntp_time > 5000000))) {
        rtcp_send_sr(s1, ff_ntp_time());
        s->last_octet_count = s->octet_count;
        s->first_packet = 0;
    }
    s->cur_timestamp = s->base_timestamp + pkt->pts;

    switch(st->codec->codec_id) {
    case CODEC_ID_PCM_MULAW:
    case CODEC_ID_PCM_ALAW:
    case CODEC_ID_PCM_U8:
    case CODEC_ID_PCM_S8:
        rtp_send_samples(s1, pkt->data, size, 1 * st->codec->channels);
        break;
    case CODEC_ID_PCM_U16BE:
    case CODEC_ID_PCM_U16LE:
    case CODEC_ID_PCM_S16BE:
    case CODEC_ID_PCM_S16LE:
        rtp_send_samples(s1, pkt->data, size, 2 * st->codec->channels);
        break;
    case CODEC_ID_MP2:
    case CODEC_ID_MP3:
        rtp_send_mpegaudio(s1, pkt->data, size);
        break;
    case CODEC_ID_MPEG1VIDEO:
    case CODEC_ID_MPEG2VIDEO:
        ff_rtp_send_mpegvideo(s1, pkt->data, size);
        break;
    case CODEC_ID_AAC:
        ff_rtp_send_aac(s1, pkt->data, size);
        break;
    case CODEC_ID_AMR_NB:
    case CODEC_ID_AMR_WB:
        ff_rtp_send_amr(s1, pkt->data, size);
        break;
    case CODEC_ID_MPEG2TS:
        rtp_send_mpegts_raw(s1, pkt->data, size);
        break;
    case CODEC_ID_H264:
        ff_rtp_send_h264(s1, pkt->data, size);
        break;
    case CODEC_ID_H263:
    case CODEC_ID_H263P:
        ff_rtp_send_h263(s1, pkt->data, size);
        break;
    default:
        /* better than nothing : send the codec raw data */
        rtp_send_raw(s1, pkt->data, size);
        break;
    }
    return 0;
}

static int rtp_write_trailer(AVFormatContext *s1)
{
    RTPMuxContext *s = s1->priv_data;

    av_freep(&s->buf);

    return 0;
}

AVOutputFormat rtp_muxer = {
    "rtp",
    NULL_IF_CONFIG_SMALL("RTP output format"),
    NULL,
    NULL,
    sizeof(RTPMuxContext),
    CODEC_ID_PCM_MULAW,
    CODEC_ID_NONE,
    rtp_write_header,
    rtp_write_packet,
    rtp_write_trailer,
};
