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

#include "libavutil/avstring.h"
#include "libavutil/imgutils.h"

#include "avformat.h"
#include "internal.h"
#include "yuv4mpeg.h"

/* Header size increased to allow room for optional flags */
#define MAX_YUV4_HEADER 96
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
    int64_t data_offset;

    for (i = 0; i < MAX_YUV4_HEADER; i++) {
        header[i] = avio_r8(pb);
        if (header[i] == '\n') {
            header[i + 1] = 0x20;  // Add a space after last option.
                                   // Makes parsing "444" vs "444alpha" easier.
            header[i + 2] = 0;
            break;
        }
    }
    if (i == MAX_YUV4_HEADER) {
        av_log(s, AV_LOG_ERROR, "Header too large.\n");
        return AVERROR(EINVAL);
    }
    if (strncmp(header, Y4M_MAGIC, strlen(Y4M_MAGIC))) {
        av_log(s, AV_LOG_ERROR, "Invalid magic number for yuv4mpeg.\n");
        return AVERROR(EINVAL);
    }

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
        {
            static const struct {
#define MAX_PIX_FMT_LENGTH 8
                char name[MAX_PIX_FMT_LENGTH + 1];
#undef MAX_PIX_FMT_LENGTH
                enum AVPixelFormat pix_fmt;
                enum AVChromaLocation chroma_loc;
            } pix_fmt_array[] = {
                { "420jpeg",  AV_PIX_FMT_YUV420P,   AVCHROMA_LOC_CENTER      },
                { "420mpeg2", AV_PIX_FMT_YUV420P,   AVCHROMA_LOC_LEFT        },
                { "420paldv", AV_PIX_FMT_YUV420P,   AVCHROMA_LOC_TOPLEFT     },
                { "420p16",   AV_PIX_FMT_YUV420P16, AVCHROMA_LOC_UNSPECIFIED },
                { "422p16",   AV_PIX_FMT_YUV422P16, AVCHROMA_LOC_UNSPECIFIED },
                { "444p16",   AV_PIX_FMT_YUV444P16, AVCHROMA_LOC_UNSPECIFIED },
                { "420p14",   AV_PIX_FMT_YUV420P14, AVCHROMA_LOC_UNSPECIFIED },
                { "422p14",   AV_PIX_FMT_YUV422P14, AVCHROMA_LOC_UNSPECIFIED },
                { "444p14",   AV_PIX_FMT_YUV444P14, AVCHROMA_LOC_UNSPECIFIED },
                { "420p12",   AV_PIX_FMT_YUV420P12, AVCHROMA_LOC_UNSPECIFIED },
                { "422p12",   AV_PIX_FMT_YUV422P12, AVCHROMA_LOC_UNSPECIFIED },
                { "444p12",   AV_PIX_FMT_YUV444P12, AVCHROMA_LOC_UNSPECIFIED },
                { "420p10",   AV_PIX_FMT_YUV420P10, AVCHROMA_LOC_UNSPECIFIED },
                { "422p10",   AV_PIX_FMT_YUV422P10, AVCHROMA_LOC_UNSPECIFIED },
                { "444p10",   AV_PIX_FMT_YUV444P10, AVCHROMA_LOC_UNSPECIFIED },
                { "420p9",    AV_PIX_FMT_YUV420P9,  AVCHROMA_LOC_UNSPECIFIED },
                { "422p9",    AV_PIX_FMT_YUV422P9,  AVCHROMA_LOC_UNSPECIFIED },
                { "444p9",    AV_PIX_FMT_YUV444P9,  AVCHROMA_LOC_UNSPECIFIED },
                { "420",      AV_PIX_FMT_YUV420P,   AVCHROMA_LOC_CENTER      },
                { "411",      AV_PIX_FMT_YUV411P,   AVCHROMA_LOC_UNSPECIFIED },
                { "422",      AV_PIX_FMT_YUV422P,   AVCHROMA_LOC_UNSPECIFIED },
                { "444alpha", AV_PIX_FMT_YUVA444P,  AVCHROMA_LOC_UNSPECIFIED },
                { "444",      AV_PIX_FMT_YUV444P,   AVCHROMA_LOC_UNSPECIFIED },
                { "mono16",   AV_PIX_FMT_GRAY16,    AVCHROMA_LOC_UNSPECIFIED },
                { "mono12",   AV_PIX_FMT_GRAY12,    AVCHROMA_LOC_UNSPECIFIED },
                { "mono10",   AV_PIX_FMT_GRAY10,    AVCHROMA_LOC_UNSPECIFIED },
                { "mono9",    AV_PIX_FMT_GRAY9,     AVCHROMA_LOC_UNSPECIFIED },
                { "mono",     AV_PIX_FMT_GRAY8,     AVCHROMA_LOC_UNSPECIFIED },
            };
            for (i = 0; i < FF_ARRAY_ELEMS(pix_fmt_array); i++) {
                if (av_strstart(tokstart, pix_fmt_array[i].name, NULL)) {
                    pix_fmt = pix_fmt_array[i].pix_fmt;
                    if (pix_fmt_array[i].chroma_loc != AVCHROMA_LOC_UNSPECIFIED)
                        chroma_sample_location = pix_fmt_array[i].chroma_loc;
                    break;
                }
            }
            if (i == FF_ARRAY_ELEMS(pix_fmt_array)) {
                av_log(s, AV_LOG_ERROR, "YUV4MPEG stream contains an unknown "
                       "pixel format.\n");
                return AVERROR_INVALIDDATA;
            }
            while (tokstart < header_end && *tokstart != 0x20)
                tokstart++;
            break;
        }
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
                static const struct {
#define MAX_PIX_FMT_LENGTH 8
                    char name[MAX_PIX_FMT_LENGTH + 1];
#undef MAX_PIX_FMT_LENGTH
                    enum AVPixelFormat pix_fmt;
                } pix_fmt_array[] = {
                    { "420JPEG",  AV_PIX_FMT_YUV420P   },
                    { "420MPEG2", AV_PIX_FMT_YUV420P   },
                    { "420PALDV", AV_PIX_FMT_YUV420P   },
                    { "420P9",    AV_PIX_FMT_YUV420P9  },
                    { "422P9",    AV_PIX_FMT_YUV422P9  },
                    { "444P9",    AV_PIX_FMT_YUV444P9  },
                    { "420P10",   AV_PIX_FMT_YUV420P10 },
                    { "444P10",   AV_PIX_FMT_YUV444P10 },
                    { "420P12",   AV_PIX_FMT_YUV420P12 },
                    { "422P12",   AV_PIX_FMT_YUV422P12 },
                    { "444P12",   AV_PIX_FMT_YUV444P12 },
                    { "420P14",   AV_PIX_FMT_YUV420P14 },
                    { "422P14",   AV_PIX_FMT_YUV422P14 },
                    { "444P14",   AV_PIX_FMT_YUV444P14 },
                    { "420P16",   AV_PIX_FMT_YUV420P16 },
                    { "422P16",   AV_PIX_FMT_YUV422P16 },
                    { "444P16",   AV_PIX_FMT_YUV444P16 },
                    { "411",      AV_PIX_FMT_YUV411P   },
                    { "422",      AV_PIX_FMT_YUV422P   },
                    { "444",      AV_PIX_FMT_YUV444P   },
                };
                // Older nonstandard pixel format representation
                tokstart += 6;
                for (size_t i = 0; i < FF_ARRAY_ELEMS(pix_fmt_array); i++)
                    if (av_strstart(tokstart, pix_fmt_array[i].name, NULL)) {
                        alt_pix_fmt = pix_fmt_array[i].pix_fmt;
                        break;
                    }
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
        return AVERROR_INVALIDDATA;
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
    ffformatcontext(s)->data_offset = data_offset = avio_tell(pb);

    st->duration = (avio_size(pb) - data_offset) / s->packet_size;

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
        return s->pb->eof_reached ? AVERROR_EOF : AVERROR(EIO);
    }
    pkt->stream_index = 0;
    pkt->pts = (off - ffformatcontext(s)->data_offset) / s->packet_size;
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

    if (avio_seek(s->pb, pos + ffformatcontext(s)->data_offset, SEEK_SET) < 0)
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

const AVInputFormat ff_yuv4mpegpipe_demuxer = {
    .name           = "yuv4mpegpipe",
    .long_name      = NULL_IF_CONFIG_SMALL("YUV4MPEG pipe"),
    .read_probe     = yuv4_probe,
    .read_header    = yuv4_read_header,
    .read_packet    = yuv4_read_packet,
    .read_seek      = yuv4_read_seek,
    .extensions     = "y4m",
};
