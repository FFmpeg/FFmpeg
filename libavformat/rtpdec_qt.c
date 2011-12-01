/*
 * RTP/Quicktime support.
 * Copyright (c) 2009 Ronald S. Bultje
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

/**
 * @file
 * @brief Quicktime-style RTP support
 * @author Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 */

#include "avformat.h"
#include "internal.h"
#include "avio_internal.h"
#include "rtp.h"
#include "rtpdec.h"
#include "isom.h"
#include "libavcodec/get_bits.h"

struct PayloadContext {
    AVPacket pkt;
    int bytes_per_frame, remaining;
    uint32_t timestamp;
};

static int qt_rtp_parse_packet(AVFormatContext *s, PayloadContext *qt,
                               AVStream *st, AVPacket *pkt,
                               uint32_t *timestamp, const uint8_t *buf,
                               int len, int flags)
{
    AVIOContext pb;
    GetBitContext gb;
    int packing_scheme, has_payload_desc, has_packet_info, alen,
        has_marker_bit = flags & RTP_FLAG_MARKER;

    if (qt->remaining) {
        int num = qt->pkt.size / qt->bytes_per_frame;

        if (av_new_packet(pkt, qt->bytes_per_frame))
            return AVERROR(ENOMEM);
        pkt->stream_index = st->index;
        pkt->flags        = qt->pkt.flags;
        memcpy(pkt->data,
               &qt->pkt.data[(num - qt->remaining) * qt->bytes_per_frame],
               qt->bytes_per_frame);
        if (--qt->remaining == 0) {
            av_freep(&qt->pkt.data);
            qt->pkt.size = 0;
        }
        return qt->remaining > 0;
    }

    /**
     * The RTP payload is described in:
     * http://developer.apple.com/quicktime/icefloe/dispatch026.html
     */
    init_get_bits(&gb, buf, len << 3);
    ffio_init_context(&pb, buf, len, 0, NULL, NULL, NULL, NULL);

    if (len < 4)
        return AVERROR_INVALIDDATA;

    skip_bits(&gb, 4); // version
    if ((packing_scheme = get_bits(&gb, 2)) == 0)
        return AVERROR_INVALIDDATA;
    if (get_bits1(&gb))
        flags          |= RTP_FLAG_KEY;
    has_payload_desc    = get_bits1(&gb);
    has_packet_info     = get_bits1(&gb);
    skip_bits(&gb, 23); // reserved:7, cache payload info:1, payload ID:15

    if (has_payload_desc) {
        int data_len, pos, is_start, is_finish;
        uint32_t tag;

        pos = get_bits_count(&gb) >> 3;
        if (pos + 12 > len)
            return AVERROR_INVALIDDATA;

        skip_bits(&gb, 2); // has non-I frames:1, is sparse:1
        is_start  = get_bits1(&gb);
        is_finish = get_bits1(&gb);
        if (!is_start || !is_finish) {
            av_log_missing_feature(s, "RTP-X-QT with payload description "
                                      "split over several packets", 1);
            return AVERROR(ENOSYS);
        }
        skip_bits(&gb, 12); // reserved
        data_len = get_bits(&gb, 16);

        avio_seek(&pb, pos + 4, SEEK_SET);
        tag = avio_rl32(&pb);
        if ((st->codec->codec_type == AVMEDIA_TYPE_VIDEO &&
                 tag != MKTAG('v','i','d','e')) ||
            (st->codec->codec_type == AVMEDIA_TYPE_AUDIO &&
                 tag != MKTAG('s','o','u','n')))
            return AVERROR_INVALIDDATA;
        avpriv_set_pts_info(st, 32, 1, avio_rb32(&pb));

        if (pos + data_len > len)
            return AVERROR_INVALIDDATA;
        /* TLVs */
        while (avio_tell(&pb) + 4 < pos + data_len) {
            int tlv_len = avio_rb16(&pb);
            tag = avio_rl16(&pb);
            if (avio_tell(&pb) + tlv_len > pos + data_len)
                return AVERROR_INVALIDDATA;

#define MKTAG16(a,b) MKTAG(a,b,0,0)
            switch (tag) {
            case MKTAG16('s','d'): {
                MOVStreamContext *msc;
                void *priv_data = st->priv_data;
                int nb_streams = s->nb_streams;
                MOVContext *mc = av_mallocz(sizeof(*mc));
                if (!mc)
                    return AVERROR(ENOMEM);
                mc->fc = s;
                st->priv_data = msc = av_mallocz(sizeof(MOVStreamContext));
                if (!msc) {
                    av_free(mc);
                    st->priv_data = priv_data;
                    return AVERROR(ENOMEM);
                }
                /* ff_mov_read_stsd_entries updates stream s->nb_streams-1,
                 * so set it temporarily to indicate which stream to update. */
                s->nb_streams = st->index + 1;
                ff_mov_read_stsd_entries(mc, &pb, 1);
                qt->bytes_per_frame = msc->bytes_per_frame;
                av_free(msc);
                av_free(mc);
                st->priv_data = priv_data;
                s->nb_streams = nb_streams;
                break;
            }
            default:
                avio_skip(&pb, tlv_len);
                break;
            }
        }

        /* 32-bit alignment */
        avio_skip(&pb, ((avio_tell(&pb) + 3) & ~3) - avio_tell(&pb));
    } else
        avio_seek(&pb, 4, SEEK_SET);

    if (has_packet_info) {
        av_log_missing_feature(s, "RTP-X-QT with packet specific info", 1);
        return AVERROR(ENOSYS);
    }

    alen = len - avio_tell(&pb);
    if (alen <= 0)
        return AVERROR_INVALIDDATA;

    switch (packing_scheme) {
    case 3: /* one data packet spread over 1 or multiple RTP packets */
        if (qt->pkt.size > 0 && qt->timestamp == *timestamp) {
            qt->pkt.data = av_realloc(qt->pkt.data, qt->pkt.size + alen +
                                      FF_INPUT_BUFFER_PADDING_SIZE);
        } else {
            av_freep(&qt->pkt.data);
            av_init_packet(&qt->pkt);
            qt->pkt.data = av_malloc(alen + FF_INPUT_BUFFER_PADDING_SIZE);
            qt->pkt.size = 0;
            qt->timestamp = *timestamp;
        }
        if (!qt->pkt.data)
            return AVERROR(ENOMEM);
        memcpy(qt->pkt.data + qt->pkt.size, buf + avio_tell(&pb), alen);
        qt->pkt.size += alen;
        if (has_marker_bit) {
            *pkt = qt->pkt;
            qt->pkt.size = 0;
            qt->pkt.data = NULL;
            pkt->flags        = flags & RTP_FLAG_KEY ? AV_PKT_FLAG_KEY : 0;
            pkt->stream_index = st->index;
            pkt->destruct     = av_destruct_packet;
            memset(pkt->data + pkt->size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
            return 0;
        }
        return AVERROR(EAGAIN);

    case 1: /* constant packet size, multiple packets per RTP packet */
        if (qt->bytes_per_frame == 0 ||
            alen % qt->bytes_per_frame != 0)
            return AVERROR_INVALIDDATA; /* wrongly padded */
        qt->remaining = (alen / qt->bytes_per_frame) - 1;
        if (av_new_packet(pkt, qt->bytes_per_frame))
            return AVERROR(ENOMEM);
        memcpy(pkt->data, buf + avio_tell(&pb), qt->bytes_per_frame);
        pkt->flags = flags & RTP_FLAG_KEY ? AV_PKT_FLAG_KEY : 0;
        pkt->stream_index = st->index;
        if (qt->remaining > 0) {
            av_freep(&qt->pkt.data);
            qt->pkt.data = av_malloc(qt->remaining * qt->bytes_per_frame);
            if (!qt->pkt.data) {
                av_free_packet(pkt);
                return AVERROR(ENOMEM);
            }
            qt->pkt.size = qt->remaining * qt->bytes_per_frame;
            memcpy(qt->pkt.data,
                   buf + avio_tell(&pb) + qt->bytes_per_frame,
                   qt->remaining * qt->bytes_per_frame);
            qt->pkt.flags = pkt->flags;
            return 1;
        }
        return 0;

    default:  /* unimplemented */
        av_log_missing_feature(NULL, "RTP-X-QT with packing scheme 2", 1);
        return AVERROR(ENOSYS);
    }
}

static PayloadContext *qt_rtp_new(void)
{
    return av_mallocz(sizeof(PayloadContext));
}

static void qt_rtp_free(PayloadContext *qt)
{
    av_freep(&qt->pkt.data);
    av_free(qt);
}

#define RTP_QT_HANDLER(m, n, s, t) \
RTPDynamicProtocolHandler ff_ ## m ## _rtp_ ## n ## _handler = { \
    .enc_name         = s, \
    .codec_type       = t, \
    .codec_id         = CODEC_ID_NONE, \
    .alloc            = qt_rtp_new,    \
    .free             = qt_rtp_free,   \
    .parse_packet     = qt_rtp_parse_packet, \
}

RTP_QT_HANDLER(qt,        vid, "X-QT",        AVMEDIA_TYPE_VIDEO);
RTP_QT_HANDLER(qt,        aud, "X-QT",        AVMEDIA_TYPE_AUDIO);
RTP_QT_HANDLER(quicktime, vid, "X-QUICKTIME", AVMEDIA_TYPE_VIDEO);
RTP_QT_HANDLER(quicktime, aud, "X-QUICKTIME", AVMEDIA_TYPE_AUDIO);
