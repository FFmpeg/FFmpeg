/*
 * YUV4MPEG demuxer
 * Copyright (c) 2001, 2002, 2003 Fabrice Bellard
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

#include "libavutil/imgutils.h"

#include "avformat.h"
#include "internal.h"
#include "yuv4mpeg.h"

/* Header size increased to allow room for optional flags */
#define MAX_YUV4_HEADER 80
#define MAX_FRAME_HEADER 80

static int yuv4_read_header(AVFormatContext *s)
{
    char header[MAX_YUV4_HEADER + 10];  // Include headroom for
                                        // the longest option
    char *tokstart, *tokend, *header_end;
    int i;
    AVIOContext *pb = s->pb;
    int width = -1, height  = -1, raten   = 0,
        rated =  0, aspectn =  0, aspectd = 0;
    enum AVPixelFormat pix_fmt = AV_PIX_FMT_NONE, alt_pix_fmt = AV_PIX_FMT_NONE;
    enum AVChromaLocation chroma_sample_location = AVCHROMA_LOC_UNSPECIFIED;
    enum AVFieldOrder field_order = AV_FIELD_UNKNOWN;
    enum AVColorRange color_range = AVCOL_RANGE_UNSPECIFIED;
    AVStream *st;

    for (i = 0; i < MAX_YUV4_HEADER; i++) {
        header[i] = avio_r8(pb);
        if (header[i] == '\n') {
            header[i + 1] = 0x20;  // Add a space after last option.
                                   // Makes parsing "444" vs "444alpha" easier.
            header[i + 2] = 0;
            break;
        }
    }
    if (i == MAX_YUV4_HEADER)
        return -1;
    if (strncmp(header, Y4M_MAGIC, strlen(Y4M_MAGIC)))
        return -1;

    header_end = &header[i + 1]; // Include space
    for (tokstart = &header[strlen(Y4M_MAGIC) + 1];
         tokstart < header_end; tokstart++) {
        if (*tokstart == 0x20)
            continue;
        switch (*tokstart++) {
        case 'W': // Width. Required.
            width    = strtol(tokstart, &tokend, 10);
            tokstart = tokend;
            break;
        case 'H': // Height. Required.
            height   = strtol(tokstart, &tokend, 10);
            tokstart = tokend;
            break;
        case 'C': // Color space
            if (strncmp("420jpeg", tokstart, 7) == 0) {
                pix_fmt = AV_PIX_FMT_YUV420P;
                chroma_sample_location = AVCHROMA_LOC_CENTER;
            } else if (strncmp("420mpeg2", tokstart, 8) == 0) {
                pix_fmt = AV_PIX_FMT_YUV420P;
                chroma_sample_location = AVCHROMA_LOC_LEFT;
            } else if (strncmp("420paldv", tokstart, 8) == 0) {
                pix_fmt = AV_PIX_FMT_YUV420P;
                chroma_sample_location = AVCHROMA_LOC_TOPLEFT;
            } else if (strncmp("420p16", tokstart, 6) == 0) {
                pix_fmt = AV_PIX_FMT_YUV420P16;
            } else if (strncmp("422p16", tokstart, 6) == 0) {
                pix_fmt = AV_PIX_FMT_YUV422P16;
            } else if (strncmp("444p16", tokstart, 6) == 0) {
                pix_fmt = AV_PIX_FMT_YUV444P16;
            } else if (strncmp("420p14", tokstart, 6) == 0) {
                pix_fmt = AV_PIX_FMT_YUV420P14;
            } else if (strncmp("422p14", tokstart, 6) == 0) {
                pix_fmt = AV_PIX_FMT_YUV422P14;
            } else if (strncmp("444p14", tokstart, 6) == 0) {
                pix_fmt = AV_PIX_FMT_YUV444P14;
            } else if (strncmp("420p12", tokstart, 6) == 0) {
                pix_fmt = AV_PIX_FMT_YUV420P12;
            } else if (strncmp("422p12", tokstart, 6) == 0) {
                pix_fmt = AV_PIX_FMT_YUV422P12;
            } else if (strncmp("444p12", tokstart, 6) == 0) {
                pix_fmt = AV_PIX_FMT_YUV444P12;
            } else if (strncmp("420p10", tokstart, 6) == 0) {
                pix_fmt = AV_PIX_FMT_YUV420P10;
            } else if (strncmp("422p10", tokstart, 6) == 0) {
                pix_fmt = AV_PIX_FMT_YUV422P10;
            } else if (strncmp("444p10", tokstart, 6) == 0) {
                pix_fmt = AV_PIX_FMT_YUV444P10;
            } else if (strncmp("420p9", tokstart, 5) == 0) {
                pix_fmt = AV_PIX_FMT_YUV420P9;
            } else if (strncmp("422p9", tokstart, 5) == 0) {
                pix_fmt = AV_PIX_FMT_YUV422P9;
            } else if (strncmp("444p9", tokstart, 5) == 0) {
                pix_fmt = AV_PIX_FMT_YUV444P9;
            } else if (strncmp("420", tokstart, 3) == 0) {
                pix_fmt = AV_PIX_FMT_YUV420P;
                chroma_sample_location = AVCHROMA_LOC_CENTER;
            } else if (strncmp("411", tokstart, 3) == 0) {
                pix_fmt = AV_PIX_FMT_YUV411P;
            } else if (strncmp("422", tokstart, 3) == 0) {
                pix_fmt = AV_PIX_FMT_YUV422P;
            } else if (strncmp("444alpha", tokstart, 8) == 0 ) {
                av_log(s, AV_LOG_ERROR, "Cannot handle 4:4:4:4 "
                       "YUV4MPEG stream.\n");
                return -1;
            } else if (strncmp("444", tokstart, 3) == 0) {
                pix_fmt = AV_PIX_FMT_YUV444P;
            } else if (strncmp("mono16", tokstart, 6) == 0) {
                pix_fmt = AV_PIX_FMT_GRAY16;
            } else if (strncmp("mono12", tokstart, 6) == 0) {
                pix_fmt = AV_PIX_FMT_GRAY12;
            } else if (strncmp("mono10", tokstart, 6) == 0) {
                pix_fmt = AV_PIX_FMT_GRAY10;
            } else if (strncmp("mono9", tokstart, 5) == 0) {
                pix_fmt = AV_PIX_FMT_GRAY9;
            } else if (strncmp("mono", tokstart, 4) == 0) {
                pix_fmt = AV_PIX_FMT_GRAY8;
            } else {
                av_log(s, AV_LOG_ERROR, "YUV4MPEG stream contains an unknown "
                       "pixel format.\n");
                return -1;
            }
            while (tokstart < header_end && *tokstart != 0x20)
                tokstart++;
            break;
        case 'I': // Interlace type
            switch (*tokstart++){
            case '?':
                field_order = AV_FIELD_UNKNOWN;
                break;
            case 'p':
                field_order = AV_FIELD_PROGRESSIVE;
                break;
            case 't':
                field_order = AV_FIELD_TT;
                break;
            case 'b':
                field_order = AV_FIELD_BB;
                break;
            case 'm':
                av_log(s, AV_LOG_ERROR, "YUV4MPEG stream contains mixed "
                       "interlaced and non-interlaced frames.\n");
            default:
                av_log(s, AV_LOG_ERROR, "YUV4MPEG has invalid header.\n");
                return AVERROR(EINVAL);
            }
            break;
        case 'F': // Frame rate
            sscanf(tokstart, "%d:%d", &raten, &rated); // 0:0 if unknown
            while (tokstart < header_end && *tokstart != 0x20)
                tokstart++;
            break;
        case 'A': // Pixel aspect
            sscanf(tokstart, "%d:%d", &aspectn, &aspectd); // 0:0 if unknown
            while (tokstart < header_end && *tokstart != 0x20)
                tokstart++;
            break;
        case 'X': // Vendor extensions
            if (strncmp("YSCSS=", tokstart, 6) == 0) {
                // Older nonstandard pixel format representation
                tokstart += 6;
                if (strncmp("420JPEG", tokstart, 7) == 0)
                    alt_pix_fmt = AV_PIX_FMT_YUV420P;
                else if (strncmp("420MPEG2", tokstart, 8) == 0)
                    alt_pix_fmt = AV_PIX_FMT_YUV420P;
                else if (strncmp("420PALDV", tokstart, 8) == 0)
                    alt_pix_fmt = AV_PIX_FMT_YUV420P;
                else if (strncmp("420P9", tokstart, 5) == 0)
                    alt_pix_fmt = AV_PIX_FMT_YUV420P9;
                else if (strncmp("422P9", tokstart, 5) == 0)
                    alt_pix_fmt = AV_PIX_FMT_YUV422P9;
                else if (strncmp("444P9", tokstart, 5) == 0)
                    alt_pix_fmt = AV_PIX_FMT_YUV444P9;
                else if (strncmp("420P10", tokstart, 6) == 0)
                    alt_pix_fmt = AV_PIX_FMT_YUV420P10;
                else if (strncmp("422P10", tokstart, 6) == 0)
                    alt_pix_fmt = AV_PIX_FMT_YUV422P10;
                else if (strncmp("444P10", tokstart, 6) == 0)
                    alt_pix_fmt = AV_PIX_FMT_YUV444P10;
                else if (strncmp("420P12", tokstart, 6) == 0)
                    alt_pix_fmt = AV_PIX_FMT_YUV420P12;
                else if (strncmp("422P12", tokstart, 6) == 0)
                    alt_pix_fmt = AV_PIX_FMT_YUV422P12;
                else if (strncmp("444P12", tokstart, 6) == 0)
                    alt_pix_fmt = AV_PIX_FMT_YUV444P12;
                else if (strncmp("420P14", tokstart, 6) == 0)
                    alt_pix_fmt = AV_PIX_FMT_YUV420P14;
                else if (strncmp("422P14", tokstart, 6) == 0)
                    alt_pix_fmt = AV_PIX_FMT_YUV422P14;
                else if (strncmp("444P14", tokstart, 6) == 0)
                    alt_pix_fmt = AV_PIX_FMT_YUV444P14;
                else if (strncmp("420P16", tokstart, 6) == 0)
                    alt_pix_fmt = AV_PIX_FMT_YUV420P16;
                else if (strncmp("422P16", tokstart, 6) == 0)
                    alt_pix_fmt = AV_PIX_FMT_YUV422P16;
                else if (strncmp("444P16", tokstart, 6) == 0)
                    alt_pix_fmt = AV_PIX_FMT_YUV444P16;
                else if (strncmp("411", tokstart, 3) == 0)
                    alt_pix_fmt = AV_PIX_FMT_YUV411P;
                else if (strncmp("422", tokstart, 3) == 0)
                    alt_pix_fmt = AV_PIX_FMT_YUV422P;
                else if (strncmp("444", tokstart, 3) == 0)
                    alt_pix_fmt = AV_PIX_FMT_YUV444P;
            } else if (strncmp("COLORRANGE=", tokstart, 11) == 0) {
              tokstart += 11;
              if (strncmp("FULL",tokstart, 4) == 0)
                  color_range = AVCOL_RANGE_JPEG;
              else if (strncmp("LIMITED", tokstart, 7) == 0)
                  color_range = AVCOL_RANGE_MPEG;
            }
            while (tokstart < header_end && *tokstart != 0x20)
                tokstart++;
            break;
        }
    }

    if (width == -1 || height == -1) {
        av_log(s, AV_LOG_ERROR, "YUV4MPEG has invalid header.\n");
        return -1;
    }

    if (pix_fmt == AV_PIX_FMT_NONE) {
        if (alt_pix_fmt == AV_PIX_FMT_NONE)
            pix_fmt = AV_PIX_FMT_YUV420P;
        else
            pix_fmt = alt_pix_fmt;
    }

    if (raten <= 0 || rated <= 0) {
        // Frame rate unknown
        raten = 25;
        rated = 1;
    }

    if (aspectn == 0 && aspectd == 0) {
        // Pixel aspect unknown
        aspectd = 1;
    }

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codecpar->width  = width;
    st->codecpar->height = height;
    av_reduce(&raten, &rated, raten, rated, (1UL << 31) - 1);
    avpriv_set_pts_info(st, 64, rated, raten);
    st->avg_frame_rate                = av_inv_q(st->time_base);
    st->codecpar->format              = pix_fmt;
    st->codecpar->codec_type          = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id            = AV_CODEC_ID_RAWVIDEO;
    st->sample_aspect_ratio           = (AVRational){ aspectn, aspectd };
    st->codecpar->chroma_location     = chroma_sample_location;
    st->codecpar->color_range         = color_range;
    st->codecpar->field_order         = field_order;
    s->packet_size = av_image_get_buffer_size(st->codecpar->format, width, height, 1) + Y4M_FRAME_MAGIC_LEN;
    if ((int) s->packet_size < 0)
        return s->packet_size;
    s->internal->data_offset = avio_tell(pb);

    st->duration = (avio_size(pb) - avio_tell(pb)) / s->packet_size;

    return 0;
}

static int yuv4_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int i;
    char header[MAX_FRAME_HEADER+1];
    int ret;
    int64_t off = avio_tell(s->pb);

    for (i = 0; i < MAX_FRAME_HEADER; i++) {
        header[i] = avio_r8(s->pb);
        if (header[i] == '\n') {
            header[i + 1] = 0;
            break;
        }
    }
    if (s->pb->error)
        return s->pb->error;
    else if (s->pb->eof_reached)
        return AVERROR_EOF;
    else if (i == MAX_FRAME_HEADER)
        return AVERROR_INVALIDDATA;

    if (strncmp(header, Y4M_FRAME_MAGIC, strlen(Y4M_FRAME_MAGIC)))
        return AVERROR_INVALIDDATA;

    ret = av_get_packet(s->pb, pkt, s->packet_size - Y4M_FRAME_MAGIC_LEN);
    if (ret < 0)
        return ret;
    else if (ret != s->packet_size - Y4M_FRAME_MAGIC_LEN) {
        av_packet_unref(pkt);
        return s->pb->eof_reached ? AVERROR_EOF : AVERROR(EIO);
    }
    pkt->stream_index = 0;
    pkt->pts = (off - s->internal->data_offset) / s->packet_size;
    pkt->duration = 1;
    return 0;
}

static int yuv4_read_seek(AVFormatContext *s, int stream_index,
                          int64_t pts, int flags)
{
    int64_t pos;

    if (flags & AVSEEK_FLAG_BACKWARD)
        pts = FFMAX(0, pts - 1);
    if (pts < 0)
        return -1;
    pos = pts * s->packet_size;

    if (avio_seek(s->pb, pos + s->internal->data_offset, SEEK_SET) < 0)
        return -1;
    return 0;
}

static int yuv4_probe(const AVProbeData *pd)
{
    /* check file header */
    if (strncmp(pd->buf, Y4M_MAGIC, sizeof(Y4M_MAGIC) - 1) == 0)
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

AVInputFormat ff_yuv4mpegpipe_demuxer = {
    .name           = "yuv4mpegpipe",
    .long_name      = NULL_IF_CONFIG_SMALL("YUV4MPEG pipe"),
    .read_probe     = yuv4_probe,
    .read_header    = yuv4_read_header,
    .read_packet    = yuv4_read_packet,
    .read_seek      = yuv4_read_seek,
    .extensions     = "y4m",
};
