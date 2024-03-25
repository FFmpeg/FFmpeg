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
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavutil/parseutils.h"

struct PayloadContext {
    char *sampling;
    AVRational framerate;
    int depth;
    int width;
    int height;
    int interlaced;
    int field;

    uint8_t *frame;
    unsigned int frame_size;
    unsigned int pgroup; /* size of the pixel group in bytes */
    unsigned int xinc;

    uint32_t timestamp;
};

static int rfc4175_parse_format(AVStream *stream, PayloadContext *data)
{
    enum AVPixelFormat pixfmt;
    int tag;
    const AVPixFmtDescriptor *desc;

    if (!strncmp(data->sampling, "YCbCr-4:2:2", 11)) {
        tag = MKTAG('U', 'Y', 'V', 'Y');
        data->xinc = 2;

        if (data->depth == 8) {
            data->pgroup = 4;
            pixfmt = AV_PIX_FMT_UYVY422;
            stream->codecpar->codec_id = AV_CODEC_ID_RAWVIDEO;
        } else if (data->depth == 10) {
            data->pgroup = 5;
            pixfmt = AV_PIX_FMT_YUV422P10;
            stream->codecpar->codec_id = AV_CODEC_ID_BITPACKED;
        } else {
            return AVERROR_INVALIDDATA;
        }
    } else if (!strncmp(data->sampling, "YCbCr-4:2:0", 11)) {
        tag = MKTAG('I', '4', '2', '0');
        data->xinc = 4;

        if (data->depth == 8) {
            data->pgroup = 6;
            pixfmt = AV_PIX_FMT_YUV420P;
            stream->codecpar->codec_id = AV_CODEC_ID_RAWVIDEO;
        } else {
            return AVERROR_INVALIDDATA;
        }
    } else if (!strncmp(data->sampling, "RGB", 3)) {
        tag = MKTAG('R', 'G', 'B', 24);
        if (data->depth == 8) {
            data->xinc = 1;
            data->pgroup = 3;
            pixfmt = AV_PIX_FMT_RGB24;
            stream->codecpar->codec_id = AV_CODEC_ID_RAWVIDEO;
        } else {
            return AVERROR_INVALIDDATA;
        }
    } else if (!strncmp(data->sampling, "BGR", 3)) {
        tag = MKTAG('B', 'G', 'R', 24);
        if (data->depth == 8) {
            data->xinc = 1;
            data->pgroup = 3;
            pixfmt = AV_PIX_FMT_BGR24;
            stream->codecpar->codec_id = AV_CODEC_ID_RAWVIDEO;
        } else {
            return AVERROR_INVALIDDATA;
        }
    } else {
        return AVERROR_INVALIDDATA;
    }

    desc = av_pix_fmt_desc_get(pixfmt);
    stream->codecpar->format = pixfmt;
    stream->codecpar->codec_tag = tag;
    stream->codecpar->bits_per_coded_sample = av_get_bits_per_pixel(desc);
    data->frame_size = data->width * data->height * data->pgroup / data->xinc;

    if (data->interlaced)
        stream->codecpar->field_order = AV_FIELD_TT;
    else
        stream->codecpar->field_order = AV_FIELD_PROGRESSIVE;

    if (data->framerate.den > 0) {
        stream->avg_frame_rate = data->framerate;
        stream->codecpar->bit_rate = data->frame_size * av_q2d(data->framerate) * 8;
    }

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
    else if (!strncmp(attr, "interlace", 9))
        data->interlaced = 1;
    else if (!strncmp(attr, "exactframerate", 14)) {
        if (av_parse_video_rate(&data->framerate, value) < 0)
            return AVERROR(EINVAL);
    } else if (!strncmp(attr, "TCS", 3)) {
        if (!strncmp(value, "SDR", 3))
            stream->codecpar->color_trc = AVCOL_TRC_BT709;
        else if (!strncmp(value, "PQ", 2))
            stream->codecpar->color_trc = AVCOL_TRC_SMPTE2084;
        else if (!strncmp(value, "HLG", 3))
            stream->codecpar->color_trc = AVCOL_TRC_ARIB_STD_B67;
        else if (!strncmp(value, "LINEAR", 6))
            stream->codecpar->color_trc = AVCOL_TRC_LINEAR;
        else if (!strncmp(value, "ST428-1", 7))
            stream->codecpar->color_trc = AVCOL_TRC_SMPTEST428_1;
        else
            stream->codecpar->color_trc = AVCOL_TRC_UNSPECIFIED;
    } else if (!strncmp(attr, "colorimetry", 11)) {
        if (!strncmp(value, "BT601", 5)) {
            stream->codecpar->color_primaries = AVCOL_PRI_BT470BG;
            stream->codecpar->color_space     = AVCOL_SPC_BT470BG;
        } else if (!strncmp(value, "BT709", 5)) {
            stream->codecpar->color_primaries = AVCOL_PRI_BT709;
            stream->codecpar->color_space     = AVCOL_SPC_BT709;
        } else if (!strncmp(value, "BT2020", 6)) {
            stream->codecpar->color_primaries = AVCOL_PRI_BT2020;
            stream->codecpar->color_space     = AVCOL_SPC_BT2020_NCL;
        }
    } else if (!strncmp(attr, "RANGE", 5)) {
        if (!strncmp(value, "NARROW", 6))
            stream->codecpar->color_range = AVCOL_RANGE_MPEG;
        else if (!strncmp(value, "FULL", 4))
            stream->codecpar->color_range = AVCOL_RANGE_JPEG;
    }

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
            return AVERROR(EINVAL);

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
    int ret = 0;

    pkt->stream_index = stream_index;
    if (!data->interlaced || data->field) {
        ret = av_packet_from_data(pkt, data->frame, data->frame_size);
        if (ret < 0) {
            av_freep(&data->frame);
        }
        data->frame = NULL;
    }

    data->field = 0;

    return ret;
}

static int rfc4175_handle_packet(AVFormatContext *ctx, PayloadContext *data,
                                 AVStream *st, AVPacket *pkt, uint32_t *timestamp,
                                 const uint8_t * buf, int len,
                                 uint16_t seq, int flags)
{
    int length, line, offset, cont, field;
    const uint8_t *headers = buf + 2; /* skip extended seqnum */
    const uint8_t *payload = buf + 2;
    int payload_len = len - 2;
    int missed_last_packet = 0;

    uint8_t *dest;

    if (*timestamp != data->timestamp) {
        if (data->frame && (!data->interlaced || data->field)) {
            /*
             * if we're here, it means that we missed the cue to return
             * the previous AVPacket, that cue being the RTP_FLAG_MARKER
             * in the last packet of either the previous frame (progressive)
             * or the previous second field (interlace). Let's finalize the
             * previous frame (or pair of fields) anyway by filling the AVPacket.
             */
            av_log(ctx, AV_LOG_ERROR, "Missed previous RTP Marker\n");
            missed_last_packet = 1;
            rfc4175_finalize_packet(data, pkt, st->index);
        }

        if (!data->frame)
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
        field = (headers[2] & 0x80) >> 7;
        line = ((headers[2] & 0x7f) << 8) | headers[3];
        offset = ((headers[4] & 0x7f) << 8) | headers[5];
        cont = headers[4] & 0x80;
        headers += 6;
        data->field = field;

        if (!data->pgroup || length % data->pgroup)
            return AVERROR_INVALIDDATA;

        if (length > payload_len)
            length = payload_len;

        if (data->interlaced)
            line = 2 * line + field;

        /* prevent ill-formed packets to write after buffer's end */
        copy_offset = (line * data->width + offset) * data->pgroup / data->xinc;
        if (copy_offset + length > data->frame_size || !data->frame)
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

const RTPDynamicProtocolHandler ff_rfc4175_rtp_handler = {
    .enc_name           = "raw",
    .codec_type         = AVMEDIA_TYPE_VIDEO,
    .codec_id           = AV_CODEC_ID_NONE,
    .priv_data_size     = sizeof(PayloadContext),
    .parse_sdp_a_line   = rfc4175_parse_sdp_line,
    .parse_packet       = rfc4175_handle_packet,
};
