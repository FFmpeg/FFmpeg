/*
 * RTP Depacketization of RAW video (TR-03)
 * Copyright (c) 2016 Savoir-faire Linux, Inc
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

/* Development sponsored by CBC/Radio-Canada */

#include "avio_internal.h"
#include "rtpdec_formats.h"
#include "libavutil/avstring.h"
#include "libavutil/pixdesc.h"

struct PayloadContext {
    char *sampling;
    int depth;
    int width;
    int height;

    uint8_t *frame;
    unsigned int frame_size;
    unsigned int pgroup; /* size of the pixel group in bytes */
    unsigned int xinc;

    uint32_t timestamp;
};

static int rfc4175_parse_format(AVStream *stream, PayloadContext *data)
{
    enum AVPixelFormat pixfmt = AV_PIX_FMT_NONE;
    int bits_per_sample = 0;
    int tag = 0;

    if (!strncmp(data->sampling, "YCbCr-4:2:2", 11)) {
        tag = MKTAG('U', 'Y', 'V', 'Y');
        data->xinc = 2;

        if (data->depth == 8) {
            data->pgroup = 4;
            bits_per_sample = 16;
            pixfmt = AV_PIX_FMT_UYVY422;
        } else if (data->depth == 10) {
            data->pgroup = 5;
            bits_per_sample = 20;
            pixfmt = AV_PIX_FMT_YUV422P10;
        } else {
            return AVERROR_INVALIDDATA;
        }
    } else {
        return AVERROR_INVALIDDATA;
    }

    stream->codecpar->format = pixfmt;
    stream->codecpar->codec_tag = tag;
    stream->codecpar->bits_per_coded_sample = bits_per_sample;
    data->frame_size = data->width * data->height * data->pgroup / data->xinc;

    return 0;
}

static int rfc4175_parse_fmtp(AVFormatContext *s, AVStream *stream,
                              PayloadContext *data, const char *attr,
                              const char *value)
{
    if (!strncmp(attr, "width", 5))
        data->width = atoi(value);
    else if (!strncmp(attr, "height", 6))
        data->height = atoi(value);
    else if (!strncmp(attr, "sampling", 8))
        data->sampling = av_strdup(value);
    else if (!strncmp(attr, "depth", 5))
        data->depth = atoi(value);

    return 0;
}

static int rfc4175_parse_sdp_line(AVFormatContext *s, int st_index,
                                  PayloadContext *data, const char *line)
{
    const char *p;

    if (st_index < 0)
        return 0;

    if (av_strstart(line, "fmtp:", &p)) {
        AVStream *stream = s->streams[st_index];
        int ret = ff_parse_fmtp(s, stream, data, p, rfc4175_parse_fmtp);

        if (ret < 0)
            return ret;


        if (!data->sampling || !data->depth || !data->width || !data->height)
            return -1;

        stream->codecpar->width = data->width;
        stream->codecpar->height = data->height;

        ret = rfc4175_parse_format(stream, data);
        av_freep(&data->sampling);

        return ret;
    }

    return 0;
}

static int rfc4175_finalize_packet(PayloadContext *data, AVPacket *pkt,
                                   int stream_index)
{
   int ret;

   pkt->stream_index = stream_index;
   ret = av_packet_from_data(pkt, data->frame, data->frame_size);
   if (ret < 0) {
       av_freep(&data->frame);
   }

   data->frame = NULL;

   return ret;
}

static int rfc4175_handle_packet(AVFormatContext *ctx, PayloadContext *data,
                                 AVStream *st, AVPacket *pkt, uint32_t *timestamp,
                                 const uint8_t * buf, int len,
                                 uint16_t seq, int flags)
{
    int length, line, offset, cont;
    const uint8_t *headers = buf + 2; /* skip extended seqnum */
    const uint8_t *payload = buf + 2;
    int payload_len = len - 2;
    int missed_last_packet = 0;

    uint8_t *dest;

    if (*timestamp != data->timestamp) {
        if (data->frame) {
            /*
             * if we're here, it means that two RTP packets didn't have the
             * same timestamp, which is a sign that they were packets from two
             * different frames, but we didn't get the flag RTP_FLAG_MARKER on
             * the first one of these frames (last packet of a frame).
             * Finalize the previous frame anyway by filling the AVPacket.
             */
            av_log(ctx, AV_LOG_ERROR, "Missed previous RTP Marker\n");
            missed_last_packet = 1;
            rfc4175_finalize_packet(data, pkt, st->index);
        }

        data->frame = av_malloc(data->frame_size);

        data->timestamp = *timestamp;

        if (!data->frame) {
            av_log(ctx, AV_LOG_ERROR, "Out of memory.\n");
            return AVERROR(ENOMEM);
        }
    }

    /*
     * looks for the 'Continuation bit' in scan lines' headers
     * to find where data start
     */
    do {
        if (payload_len < 6)
            return AVERROR_INVALIDDATA;

        cont = payload[4] & 0x80;
        payload += 6;
        payload_len -= 6;
    } while (cont);

    /* and now iterate over every scan lines */
    do {
        int copy_offset;

        if (payload_len < data->pgroup)
            return AVERROR_INVALIDDATA;

        length = (headers[0] << 8) | headers[1];
        line = ((headers[2] & 0x7f) << 8) | headers[3];
        offset = ((headers[4] & 0x7f) << 8) | headers[5];
        cont = headers[4] & 0x80;
        headers += 6;

        if (length % data->pgroup)
            return AVERROR_INVALIDDATA;

        if (length > payload_len)
            length = payload_len;

        /* prevent ill-formed packets to write after buffer's end */
        copy_offset = (line * data->width + offset) * data->pgroup / data->xinc;
        if (copy_offset + length > data->frame_size)
            return AVERROR_INVALIDDATA;

        dest = data->frame + copy_offset;
        memcpy(dest, payload, length);

        payload += length;
        payload_len -= length;
    } while (cont);

    if ((flags & RTP_FLAG_MARKER)) {
        return rfc4175_finalize_packet(data, pkt, st->index);
    } else if (missed_last_packet) {
        return 0;
    }

    return AVERROR(EAGAIN);
}

RTPDynamicProtocolHandler ff_rfc4175_rtp_handler = {
    .enc_name           = "raw",
    .codec_type         = AVMEDIA_TYPE_VIDEO,
    .codec_id           = AV_CODEC_ID_BITPACKED,
    .priv_data_size     = sizeof(PayloadContext),
    .parse_sdp_a_line   = rfc4175_parse_sdp_line,
    .parse_packet       = rfc4175_handle_packet,
};
