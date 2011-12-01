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
#include "libavutil/mathematics.h"
#include "libavutil/random_seed.h"
#include "libavutil/opt.h"

#include "rtpenc.h"

//#define DEBUG

static const AVOption options[] = {
    FF_RTP_FLAG_OPTS(RTPMuxContext, flags),
    { "payload_type", "Specify RTP payload type", offsetof(RTPMuxContext, payload_type), AV_OPT_TYPE_INT, {.dbl = -1 }, -1, 127, AV_OPT_FLAG_ENCODING_PARAM },
    { NULL },
};

static const AVClass rtp_muxer_class = {
    .class_name = "RTP muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

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
    case CODEC_ID_VORBIS:
    case CODEC_ID_THEORA:
    case CODEC_ID_VP8:
    case CODEC_ID_ADPCM_G722:
    case CODEC_ID_ADPCM_G726:
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
        av_log(s1, AV_LOG_ERROR, "Unsupported codec %s\n", avcodec_get_name(st->codec->codec_id));

        return -1;
    }

    if (s->payload_type < 0)
        s->payload_type = ff_rtp_get_payload_type(s1, st->codec);
    s->base_timestamp = av_get_random_seed();
    s->timestamp = s->base_timestamp;
    s->cur_timestamp = 0;
    s->ssrc = av_get_random_seed();
    s->first_packet = 1;
    s->first_rtcp_ntp_time = ff_ntp_time();
    if (s1->start_time_realtime)
        /* Round the NTP time to whole milliseconds. */
        s->first_rtcp_ntp_time = (s1->start_time_realtime / 1000) * 1000 +
                                 NTP_OFFSET_US;

    max_packet_size = s1->pb->max_packet_size;
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
                s->max_frames_per_packet = av_rescale_rnd(s1->max_delay, st->codec->sample_rate, AV_TIME_BASE * (int64_t)st->codec->frame_size, AV_ROUND_DOWN);
            }
        }
        if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            /* FIXME: We should round down here... */
            s->max_frames_per_packet = av_rescale_q(s1->max_delay, (AVRational){1, 1000000}, st->codec->time_base);
        }
    }

    avpriv_set_pts_info(st, 32, 1, 90000);
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
    case CODEC_ID_H264:
        /* check for H.264 MP4 syntax */
        if (st->codec->extradata_size > 4 && st->codec->extradata[0] == 1) {
            s->nal_length_size = (st->codec->extradata[4] & 0x03) + 1;
        }
        break;
    case CODEC_ID_VORBIS:
    case CODEC_ID_THEORA:
        if (!s->max_frames_per_packet) s->max_frames_per_packet = 15;
        s->max_frames_per_packet = av_clip(s->max_frames_per_packet, 1, 15);
        s->max_payload_size -= 6; // ident+frag+tdt/vdt+pkt_num+pkt_length
        s->num_frames = 0;
        goto defaultcase;
    case CODEC_ID_VP8:
        av_log(s1, AV_LOG_ERROR, "RTP VP8 payload implementation is "
                                 "incompatible with the latest spec drafts.\n");
        break;
    case CODEC_ID_ADPCM_G722:
        /* Due to a historical error, the clock rate for G722 in RTP is
         * 8000, even if the sample rate is 16000. See RFC 3551. */
        avpriv_set_pts_info(st, 32, 1, 8000);
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
defaultcase:
        if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            avpriv_set_pts_info(st, 32, 1, st->codec->sample_rate);
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

    av_dlog(s1, "RTCP: %02x %"PRIx64" %x\n", s->payload_type, ntp_time, s->timestamp);

    s->last_rtcp_ntp_time = ntp_time;
    rtp_ts = av_rescale_q(ntp_time - s->first_rtcp_ntp_time, (AVRational){1, 1000000},
                          s1->streams[0]->time_base) + s->base_timestamp;
    avio_w8(s1->pb, (RTP_VERSION << 6));
    avio_w8(s1->pb, RTCP_SR);
    avio_wb16(s1->pb, 6); /* length in words - 1 */
    avio_wb32(s1->pb, s->ssrc);
    avio_wb32(s1->pb, ntp_time / 1000000);
    avio_wb32(s1->pb, ((ntp_time % 1000000) << 32) / 1000000);
    avio_wb32(s1->pb, rtp_ts);
    avio_wb32(s1->pb, s->packet_count);
    avio_wb32(s1->pb, s->octet_count);
    avio_flush(s1->pb);
}

/* send an rtp packet. sequence number is incremented, but the caller
   must update the timestamp itself */
void ff_rtp_send_data(AVFormatContext *s1, const uint8_t *buf1, int len, int m)
{
    RTPMuxContext *s = s1->priv_data;

    av_dlog(s1, "rtp_send_data size=%d\n", len);

    /* build the RTP header */
    avio_w8(s1->pb, (RTP_VERSION << 6));
    avio_w8(s1->pb, (s->payload_type & 0x7f) | ((m & 0x01) << 7));
    avio_wb16(s1->pb, s->seq);
    avio_wb32(s1->pb, s->timestamp);
    avio_wb32(s1->pb, s->ssrc);

    avio_write(s1->pb, buf1, len);
    avio_flush(s1->pb);

    s->seq++;
    s->octet_count += len;
    s->packet_count++;
}

/* send an integer number of samples and compute time stamp and fill
   the rtp send buffer before sending. */
static void rtp_send_samples(AVFormatContext *s1,
                             const uint8_t *buf1, int size, int sample_size_bits)
{
    RTPMuxContext *s = s1->priv_data;
    int len, max_packet_size, n;
    /* Calculate the number of bytes to get samples aligned on a byte border */
    int aligned_samples_size = sample_size_bits/av_gcd(sample_size_bits, 8);

    max_packet_size = (s->max_payload_size / aligned_samples_size) * aligned_samples_size;
    /* Not needed, but who knows. Don't check if samples aren't an even number of bytes. */
    if ((sample_size_bits % 8) == 0 && ((8 * size) % sample_size_bits) != 0)
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
        s->timestamp = s->cur_timestamp + n * 8 / sample_size_bits;
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

    av_dlog(s1, "%d: write len=%d\n", pkt->stream_index, size);

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
        rtp_send_samples(s1, pkt->data, size, 8 * st->codec->channels);
        break;
    case CODEC_ID_PCM_U16BE:
    case CODEC_ID_PCM_U16LE:
    case CODEC_ID_PCM_S16BE:
    case CODEC_ID_PCM_S16LE:
        rtp_send_samples(s1, pkt->data, size, 16 * st->codec->channels);
        break;
    case CODEC_ID_ADPCM_G722:
        /* The actual sample size is half a byte per sample, but since the
         * stream clock rate is 8000 Hz while the sample rate is 16000 Hz,
         * the correct parameter for send_samples_bits is 8 bits per stream
         * clock. */
        rtp_send_samples(s1, pkt->data, size, 8 * st->codec->channels);
        break;
    case CODEC_ID_ADPCM_G726:
        rtp_send_samples(s1, pkt->data, size,
                         st->codec->bits_per_coded_sample * st->codec->channels);
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
        if (s->flags & FF_RTP_FLAG_MP4A_LATM)
            ff_rtp_send_latm(s1, pkt->data, size);
        else
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
    case CODEC_ID_VORBIS:
    case CODEC_ID_THEORA:
        ff_rtp_send_xiph(s1, pkt->data, size);
        break;
    case CODEC_ID_VP8:
        ff_rtp_send_vp8(s1, pkt->data, size);
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

AVOutputFormat ff_rtp_muxer = {
    .name              = "rtp",
    .long_name         = NULL_IF_CONFIG_SMALL("RTP output format"),
    .priv_data_size    = sizeof(RTPMuxContext),
    .audio_codec       = CODEC_ID_PCM_MULAW,
    .video_codec       = CODEC_ID_MPEG4,
    .write_header      = rtp_write_header,
    .write_packet      = rtp_write_packet,
    .write_trailer     = rtp_write_trailer,
    .priv_class = &rtp_muxer_class,
};
