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

static const AVOption options[] = {
    FF_RTP_FLAG_OPTS(RTPMuxContext, flags),
    { "payload_type", "Specify RTP payload type", offsetof(RTPMuxContext, payload_type), AV_OPT_TYPE_INT, {.i64 = -1 }, -1, 127, AV_OPT_FLAG_ENCODING_PARAM },
    { "ssrc", "Stream identifier", offsetof(RTPMuxContext, ssrc), AV_OPT_TYPE_INT, { .i64 = 0 }, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { "cname", "CNAME to include in RTCP SR packets", offsetof(RTPMuxContext, cname), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM },
    { "seq", "Starting sequence number", offsetof(RTPMuxContext, seq), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 65535, AV_OPT_FLAG_ENCODING_PARAM },
    { NULL },
};

static const AVClass rtp_muxer_class = {
    .class_name = "RTP muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

#define RTCP_SR_SIZE 28

static int is_supported(enum AVCodecID id)
{
    switch(id) {
    case AV_CODEC_ID_H261:
    case AV_CODEC_ID_H263:
    case AV_CODEC_ID_H263P:
    case AV_CODEC_ID_H264:
    case AV_CODEC_ID_HEVC:
    case AV_CODEC_ID_MPEG1VIDEO:
    case AV_CODEC_ID_MPEG2VIDEO:
    case AV_CODEC_ID_MPEG4:
    case AV_CODEC_ID_AAC:
    case AV_CODEC_ID_MP2:
    case AV_CODEC_ID_MP3:
    case AV_CODEC_ID_PCM_ALAW:
    case AV_CODEC_ID_PCM_MULAW:
    case AV_CODEC_ID_PCM_S8:
    case AV_CODEC_ID_PCM_S16BE:
    case AV_CODEC_ID_PCM_S16LE:
    case AV_CODEC_ID_PCM_U16BE:
    case AV_CODEC_ID_PCM_U16LE:
    case AV_CODEC_ID_PCM_U8:
    case AV_CODEC_ID_MPEG2TS:
    case AV_CODEC_ID_AMR_NB:
    case AV_CODEC_ID_AMR_WB:
    case AV_CODEC_ID_VORBIS:
    case AV_CODEC_ID_THEORA:
    case AV_CODEC_ID_VP8:
    case AV_CODEC_ID_ADPCM_G722:
    case AV_CODEC_ID_ADPCM_G726:
    case AV_CODEC_ID_ILBC:
    case AV_CODEC_ID_MJPEG:
    case AV_CODEC_ID_SPEEX:
    case AV_CODEC_ID_OPUS:
        return 1;
    default:
        return 0;
    }
}

static int rtp_write_header(AVFormatContext *s1)
{
    RTPMuxContext *s = s1->priv_data;
    int n, ret = AVERROR(EINVAL);
    AVStream *st;

    if (s1->nb_streams != 1) {
        av_log(s1, AV_LOG_ERROR, "Only one stream supported in the RTP muxer\n");
        return AVERROR(EINVAL);
    }
    st = s1->streams[0];
    if (!is_supported(st->codec->codec_id)) {
        av_log(s1, AV_LOG_ERROR, "Unsupported codec %s\n", avcodec_get_name(st->codec->codec_id));

        return -1;
    }

    if (s->payload_type < 0) {
        /* Re-validate non-dynamic payload types */
        if (st->id < RTP_PT_PRIVATE)
            st->id = ff_rtp_get_payload_type(s1, st->codec, -1);

        s->payload_type = st->id;
    } else {
        /* private option takes priority */
        st->id = s->payload_type;
    }

    s->base_timestamp = av_get_random_seed();
    s->timestamp = s->base_timestamp;
    s->cur_timestamp = 0;
    if (!s->ssrc)
        s->ssrc = av_get_random_seed();
    s->first_packet = 1;
    s->first_rtcp_ntp_time = ff_ntp_time();
    if (s1->start_time_realtime != 0  &&  s1->start_time_realtime != AV_NOPTS_VALUE)
        /* Round the NTP time to whole milliseconds. */
        s->first_rtcp_ntp_time = (s1->start_time_realtime / 1000) * 1000 +
                                 NTP_OFFSET_US;
    // Pick a random sequence start number, but in the lower end of the
    // available range, so that any wraparound doesn't happen immediately.
    // (Immediate wraparound would be an issue for SRTP.)
    if (s->seq < 0) {
        if (s1->flags & AVFMT_FLAG_BITEXACT) {
            s->seq = 0;
        } else
            s->seq = av_get_random_seed() & 0x0fff;
    } else
        s->seq &= 0xffff; // Use the given parameter, wrapped to the right interval

    if (s1->packet_size) {
        if (s1->pb->max_packet_size)
            s1->packet_size = FFMIN(s1->packet_size,
                                    s1->pb->max_packet_size);
    } else
        s1->packet_size = s1->pb->max_packet_size;
    if (s1->packet_size <= 12) {
        av_log(s1, AV_LOG_ERROR, "Max packet size %d too low\n", s1->packet_size);
        return AVERROR(EIO);
    }
    s->buf = av_malloc(s1->packet_size);
    if (!s->buf) {
        return AVERROR(ENOMEM);
    }
    s->max_payload_size = s1->packet_size - 12;

    if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
        avpriv_set_pts_info(st, 32, 1, st->codec->sample_rate);
    } else {
        avpriv_set_pts_info(st, 32, 1, 90000);
    }
    s->buf_ptr = s->buf;
    switch(st->codec->codec_id) {
    case AV_CODEC_ID_MP2:
    case AV_CODEC_ID_MP3:
        s->buf_ptr = s->buf + 4;
        avpriv_set_pts_info(st, 32, 1, 90000);
        break;
    case AV_CODEC_ID_MPEG1VIDEO:
    case AV_CODEC_ID_MPEG2VIDEO:
        break;
    case AV_CODEC_ID_MPEG2TS:
        n = s->max_payload_size / TS_PACKET_SIZE;
        if (n < 1)
            n = 1;
        s->max_payload_size = n * TS_PACKET_SIZE;
        break;
    case AV_CODEC_ID_H261:
        if (s1->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL) {
            av_log(s, AV_LOG_ERROR,
                   "Packetizing H261 is experimental and produces incorrect "
                   "packetization for cases where GOBs don't fit into packets "
                   "(even though most receivers may handle it just fine). "
                   "Please set -f_strict experimental in order to enable it.\n");
            ret = AVERROR_EXPERIMENTAL;
            goto fail;
        }
        break;
    case AV_CODEC_ID_H264:
        /* check for H.264 MP4 syntax */
        if (st->codec->extradata_size > 4 && st->codec->extradata[0] == 1) {
            s->nal_length_size = (st->codec->extradata[4] & 0x03) + 1;
        }
        break;
    case AV_CODEC_ID_HEVC:
        /* Only check for the standardized hvcC version of extradata, keeping
         * things simple and similar to the avcC/H264 case above, instead
         * of trying to handle the pre-standardization versions (as in
         * libavcodec/hevc.c). */
        if (st->codec->extradata_size > 21 && st->codec->extradata[0] == 1) {
            s->nal_length_size = (st->codec->extradata[21] & 0x03) + 1;
        }
        break;
    case AV_CODEC_ID_VORBIS:
    case AV_CODEC_ID_THEORA:
        s->max_frames_per_packet = 15;
        break;
    case AV_CODEC_ID_ADPCM_G722:
        /* Due to a historical error, the clock rate for G722 in RTP is
         * 8000, even if the sample rate is 16000. See RFC 3551. */
        avpriv_set_pts_info(st, 32, 1, 8000);
        break;
    case AV_CODEC_ID_OPUS:
        if (st->codec->channels > 2) {
            av_log(s1, AV_LOG_ERROR, "Multistream opus not supported in RTP\n");
            goto fail;
        }
        /* The opus RTP RFC says that all opus streams should use 48000 Hz
         * as clock rate, since all opus sample rates can be expressed in
         * this clock rate, and sample rate changes on the fly are supported. */
        avpriv_set_pts_info(st, 32, 1, 48000);
        break;
    case AV_CODEC_ID_ILBC:
        if (st->codec->block_align != 38 && st->codec->block_align != 50) {
            av_log(s1, AV_LOG_ERROR, "Incorrect iLBC block size specified\n");
            goto fail;
        }
        s->max_frames_per_packet = s->max_payload_size / st->codec->block_align;
        break;
    case AV_CODEC_ID_AMR_NB:
    case AV_CODEC_ID_AMR_WB:
        s->max_frames_per_packet = 50;
        if (st->codec->codec_id == AV_CODEC_ID_AMR_NB)
            n = 31;
        else
            n = 61;
        /* max_header_toc_size + the largest AMR payload must fit */
        if (1 + s->max_frames_per_packet + n > s->max_payload_size) {
            av_log(s1, AV_LOG_ERROR, "RTP max payload size too small for AMR\n");
            goto fail;
        }
        if (st->codec->channels != 1) {
            av_log(s1, AV_LOG_ERROR, "Only mono is supported\n");
            goto fail;
        }
        break;
    case AV_CODEC_ID_AAC:
        s->max_frames_per_packet = 50;
        break;
    default:
        break;
    }

    return 0;

fail:
    av_freep(&s->buf);
    return ret;
}

/* send an rtcp sender report packet */
static void rtcp_send_sr(AVFormatContext *s1, int64_t ntp_time, int bye)
{
    RTPMuxContext *s = s1->priv_data;
    uint32_t rtp_ts;

    av_log(s1, AV_LOG_TRACE, "RTCP: %02x %"PRIx64" %x\n", s->payload_type, ntp_time, s->timestamp);

    s->last_rtcp_ntp_time = ntp_time;
    rtp_ts = av_rescale_q(ntp_time - s->first_rtcp_ntp_time, (AVRational){1, 1000000},
                          s1->streams[0]->time_base) + s->base_timestamp;
    avio_w8(s1->pb, RTP_VERSION << 6);
    avio_w8(s1->pb, RTCP_SR);
    avio_wb16(s1->pb, 6); /* length in words - 1 */
    avio_wb32(s1->pb, s->ssrc);
    avio_wb64(s1->pb, NTP_TO_RTP_FORMAT(ntp_time));
    avio_wb32(s1->pb, rtp_ts);
    avio_wb32(s1->pb, s->packet_count);
    avio_wb32(s1->pb, s->octet_count);

    if (s->cname) {
        int len = FFMIN(strlen(s->cname), 255);
        avio_w8(s1->pb, (RTP_VERSION << 6) + 1);
        avio_w8(s1->pb, RTCP_SDES);
        avio_wb16(s1->pb, (7 + len + 3) / 4); /* length in words - 1 */

        avio_wb32(s1->pb, s->ssrc);
        avio_w8(s1->pb, 0x01); /* CNAME */
        avio_w8(s1->pb, len);
        avio_write(s1->pb, s->cname, len);
        avio_w8(s1->pb, 0); /* END */
        for (len = (7 + len) % 4; len % 4; len++)
            avio_w8(s1->pb, 0);
    }

    if (bye) {
        avio_w8(s1->pb, (RTP_VERSION << 6) | 1);
        avio_w8(s1->pb, RTCP_BYE);
        avio_wb16(s1->pb, 1); /* length in words - 1 */
        avio_wb32(s1->pb, s->ssrc);
    }

    avio_flush(s1->pb);
}

/* send an rtp packet. sequence number is incremented, but the caller
   must update the timestamp itself */
void ff_rtp_send_data(AVFormatContext *s1, const uint8_t *buf1, int len, int m)
{
    RTPMuxContext *s = s1->priv_data;

    av_log(s1, AV_LOG_TRACE, "rtp_send_data size=%d\n", len);

    /* build the RTP header */
    avio_w8(s1->pb, RTP_VERSION << 6);
    avio_w8(s1->pb, (s->payload_type & 0x7f) | ((m & 0x01) << 7));
    avio_wb16(s1->pb, s->seq);
    avio_wb32(s1->pb, s->timestamp);
    avio_wb32(s1->pb, s->ssrc);

    avio_write(s1->pb, buf1, len);
    avio_flush(s1->pb);

    s->seq = (s->seq + 1) & 0xffff;
    s->octet_count += len;
    s->packet_count++;
}

/* send an integer number of samples and compute time stamp and fill
   the rtp send buffer before sending. */
static int rtp_send_samples(AVFormatContext *s1,
                            const uint8_t *buf1, int size, int sample_size_bits)
{
    RTPMuxContext *s = s1->priv_data;
    int len, max_packet_size, n;
    /* Calculate the number of bytes to get samples aligned on a byte border */
    int aligned_samples_size = sample_size_bits/av_gcd(sample_size_bits, 8);

    max_packet_size = (s->max_payload_size / aligned_samples_size) * aligned_samples_size;
    /* Not needed, but who knows. Don't check if samples aren't an even number of bytes. */
    if ((sample_size_bits % 8) == 0 && ((8 * size) % sample_size_bits) != 0)
        return AVERROR(EINVAL);
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
    return 0;
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

    s->timestamp = s->cur_timestamp;
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

static int rtp_send_ilbc(AVFormatContext *s1, const uint8_t *buf, int size)
{
    RTPMuxContext *s = s1->priv_data;
    AVStream *st = s1->streams[0];
    int frame_duration = av_get_audio_frame_duration(st->codec, 0);
    int frame_size = st->codec->block_align;
    int frames = size / frame_size;

    while (frames > 0) {
        if (s->num_frames > 0 &&
            av_compare_ts(s->cur_timestamp - s->timestamp, st->time_base,
                          s1->max_delay, AV_TIME_BASE_Q) >= 0) {
            ff_rtp_send_data(s1, s->buf, s->buf_ptr - s->buf, 1);
            s->num_frames = 0;
        }

        if (!s->num_frames) {
            s->buf_ptr = s->buf;
            s->timestamp = s->cur_timestamp;
        }
        memcpy(s->buf_ptr, buf, frame_size);
        frames--;
        s->num_frames++;
        s->buf_ptr       += frame_size;
        buf              += frame_size;
        s->cur_timestamp += frame_duration;

        if (s->num_frames == s->max_frames_per_packet) {
            ff_rtp_send_data(s1, s->buf, s->buf_ptr - s->buf, 1);
            s->num_frames = 0;
        }
    }
    return 0;
}

static int rtp_write_packet(AVFormatContext *s1, AVPacket *pkt)
{
    RTPMuxContext *s = s1->priv_data;
    AVStream *st = s1->streams[0];
    int rtcp_bytes;
    int size= pkt->size;

    av_log(s1, AV_LOG_TRACE, "%d: write len=%d\n", pkt->stream_index, size);

    rtcp_bytes = ((s->octet_count - s->last_octet_count) * RTCP_TX_RATIO_NUM) /
        RTCP_TX_RATIO_DEN;
    if ((s->first_packet || ((rtcp_bytes >= RTCP_SR_SIZE) &&
                            (ff_ntp_time() - s->last_rtcp_ntp_time > 5000000))) &&
        !(s->flags & FF_RTP_FLAG_SKIP_RTCP)) {
        rtcp_send_sr(s1, ff_ntp_time(), 0);
        s->last_octet_count = s->octet_count;
        s->first_packet = 0;
    }
    s->cur_timestamp = s->base_timestamp + pkt->pts;

    switch(st->codec->codec_id) {
    case AV_CODEC_ID_PCM_MULAW:
    case AV_CODEC_ID_PCM_ALAW:
    case AV_CODEC_ID_PCM_U8:
    case AV_CODEC_ID_PCM_S8:
        return rtp_send_samples(s1, pkt->data, size, 8 * st->codec->channels);
    case AV_CODEC_ID_PCM_U16BE:
    case AV_CODEC_ID_PCM_U16LE:
    case AV_CODEC_ID_PCM_S16BE:
    case AV_CODEC_ID_PCM_S16LE:
        return rtp_send_samples(s1, pkt->data, size, 16 * st->codec->channels);
    case AV_CODEC_ID_ADPCM_G722:
        /* The actual sample size is half a byte per sample, but since the
         * stream clock rate is 8000 Hz while the sample rate is 16000 Hz,
         * the correct parameter for send_samples_bits is 8 bits per stream
         * clock. */
        return rtp_send_samples(s1, pkt->data, size, 8 * st->codec->channels);
    case AV_CODEC_ID_ADPCM_G726:
        return rtp_send_samples(s1, pkt->data, size,
                                st->codec->bits_per_coded_sample * st->codec->channels);
    case AV_CODEC_ID_MP2:
    case AV_CODEC_ID_MP3:
        rtp_send_mpegaudio(s1, pkt->data, size);
        break;
    case AV_CODEC_ID_MPEG1VIDEO:
    case AV_CODEC_ID_MPEG2VIDEO:
        ff_rtp_send_mpegvideo(s1, pkt->data, size);
        break;
    case AV_CODEC_ID_AAC:
        if (s->flags & FF_RTP_FLAG_MP4A_LATM)
            ff_rtp_send_latm(s1, pkt->data, size);
        else
            ff_rtp_send_aac(s1, pkt->data, size);
        break;
    case AV_CODEC_ID_AMR_NB:
    case AV_CODEC_ID_AMR_WB:
        ff_rtp_send_amr(s1, pkt->data, size);
        break;
    case AV_CODEC_ID_MPEG2TS:
        rtp_send_mpegts_raw(s1, pkt->data, size);
        break;
    case AV_CODEC_ID_H264:
        ff_rtp_send_h264_hevc(s1, pkt->data, size);
        break;
    case AV_CODEC_ID_H261:
        ff_rtp_send_h261(s1, pkt->data, size);
        break;
    case AV_CODEC_ID_H263:
        if (s->flags & FF_RTP_FLAG_RFC2190) {
            int mb_info_size = 0;
            const uint8_t *mb_info =
                av_packet_get_side_data(pkt, AV_PKT_DATA_H263_MB_INFO,
                                        &mb_info_size);
            ff_rtp_send_h263_rfc2190(s1, pkt->data, size, mb_info, mb_info_size);
            break;
        }
        /* Fallthrough */
    case AV_CODEC_ID_H263P:
        ff_rtp_send_h263(s1, pkt->data, size);
        break;
    case AV_CODEC_ID_HEVC:
        ff_rtp_send_h264_hevc(s1, pkt->data, size);
        break;
    case AV_CODEC_ID_VORBIS:
    case AV_CODEC_ID_THEORA:
        ff_rtp_send_xiph(s1, pkt->data, size);
        break;
    case AV_CODEC_ID_VP8:
        ff_rtp_send_vp8(s1, pkt->data, size);
        break;
    case AV_CODEC_ID_ILBC:
        rtp_send_ilbc(s1, pkt->data, size);
        break;
    case AV_CODEC_ID_MJPEG:
        ff_rtp_send_jpeg(s1, pkt->data, size);
        break;
    case AV_CODEC_ID_OPUS:
        if (size > s->max_payload_size) {
            av_log(s1, AV_LOG_ERROR,
                   "Packet size %d too large for max RTP payload size %d\n",
                   size, s->max_payload_size);
            return AVERROR(EINVAL);
        }
        /* Intentional fallthrough */
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

    /* If the caller closes and recreates ->pb, this might actually
     * be NULL here even if it was successfully allocated at the start. */
    if (s1->pb && (s->flags & FF_RTP_FLAG_SEND_BYE))
        rtcp_send_sr(s1, ff_ntp_time(), 1);
    av_freep(&s->buf);

    return 0;
}

AVOutputFormat ff_rtp_muxer = {
    .name              = "rtp",
    .long_name         = NULL_IF_CONFIG_SMALL("RTP output"),
    .priv_data_size    = sizeof(RTPMuxContext),
    .audio_codec       = AV_CODEC_ID_PCM_MULAW,
    .video_codec       = AV_CODEC_ID_MPEG4,
    .write_header      = rtp_write_header,
    .write_packet      = rtp_write_packet,
    .write_trailer     = rtp_write_trailer,
    .priv_class        = &rtp_muxer_class,
    .flags             = AVFMT_TS_NONSTRICT,
};
