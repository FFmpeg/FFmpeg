/*
 * YUV4MPEG format
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
#include "avformat.h"
#include "internal.h"
#include "libavutil/pixdesc.h"

#define Y4M_MAGIC "YUV4MPEG2"
#define Y4M_FRAME_MAGIC "FRAME"
#define Y4M_LINE_MAX 256

struct frame_attributes {
    int interlaced_frame;
    int top_field_first;
};

#if CONFIG_YUV4MPEGPIPE_MUXER
static int yuv4_generate_header(AVFormatContext *s, char* buf)
{
    AVStream *st;
    int width, height;
    int raten, rated, aspectn, aspectd, n;
    char inter;
    const char *colorspace = "";

    st     = s->streams[0];
    width  = st->codec->width;
    height = st->codec->height;

    av_reduce(&raten, &rated, st->codec->time_base.den,
              st->codec->time_base.num, (1UL << 31) - 1);

    aspectn = st->sample_aspect_ratio.num;
    aspectd = st->sample_aspect_ratio.den;

    if (aspectn == 0 && aspectd == 1)
        aspectd = 0;  // 0:0 means unknown

    inter = 'p'; /* progressive is the default */
    if (st->codec->coded_frame && st->codec->coded_frame->interlaced_frame)
        inter = st->codec->coded_frame->top_field_first ? 't' : 'b';

    switch (st->codec->pix_fmt) {
    case PIX_FMT_GRAY8:
        colorspace = " Cmono";
        break;
    case PIX_FMT_GRAY16:
        colorspace = " Cmono16";
        break;
    case PIX_FMT_YUV411P:
        colorspace = " C411 XYSCSS=411";
        break;
    case PIX_FMT_YUV420P:
        switch (st->codec->chroma_sample_location) {
        case AVCHROMA_LOC_TOPLEFT: colorspace = " C420paldv XYSCSS=420PALDV"; break;
        case AVCHROMA_LOC_LEFT:    colorspace = " C420mpeg2 XYSCSS=420MPEG2"; break;
        default:                   colorspace = " C420jpeg XYSCSS=420JPEG";   break;
        }
        break;
    case PIX_FMT_YUV422P:
        colorspace = " C422 XYSCSS=422";
        break;
    case PIX_FMT_YUV444P:
        colorspace = " C444 XYSCSS=444";
        break;
    case PIX_FMT_YUV420P9:
        colorspace = " C420p9 XYSCSS=420P9";
        break;
    case PIX_FMT_YUV422P9:
        colorspace = " C422p9 XYSCSS=422P9";
        break;
    case PIX_FMT_YUV444P9:
        colorspace = " C444p9 XYSCSS=444P9";
        break;
    case PIX_FMT_YUV420P10:
        colorspace = " C420p10 XYSCSS=420P10";
        break;
    case PIX_FMT_YUV422P10:
        colorspace = " C422p10 XYSCSS=422P10";
        break;
    case PIX_FMT_YUV444P10:
        colorspace = " C444p10 XYSCSS=444P10";
        break;
    case PIX_FMT_YUV420P16:
        colorspace = " C420p16 XYSCSS=420P16";
        break;
    case PIX_FMT_YUV422P16:
        colorspace = " C422p16 XYSCSS=422P16";
        break;
    case PIX_FMT_YUV444P16:
        colorspace = " C444p16 XYSCSS=444P16";
        break;
    }

    /* construct stream header, if this is the first frame */
    n = snprintf(buf, Y4M_LINE_MAX, "%s W%d H%d F%d:%d I%c A%d:%d%s\n",
                 Y4M_MAGIC, width, height, raten, rated, inter,
                 aspectn, aspectd, colorspace);

    return n;
}

static int yuv4_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVStream *st = s->streams[pkt->stream_index];
    AVIOContext *pb = s->pb;
    AVPicture *picture, picture_tmp;
    int* first_pkt = s->priv_data;
    int width, height, h_chroma_shift, v_chroma_shift;
    int i;
    char buf2[Y4M_LINE_MAX + 1];
    char buf1[20];
    uint8_t *ptr, *ptr1, *ptr2;

    memcpy(&picture_tmp, pkt->data, sizeof(AVPicture));
    picture = &picture_tmp;

    /* for the first packet we have to output the header as well */
    if (*first_pkt) {
        *first_pkt = 0;
        if (yuv4_generate_header(s, buf2) < 0) {
            av_log(s, AV_LOG_ERROR,
                   "Error. YUV4MPEG stream header write failed.\n");
            return AVERROR(EIO);
        } else {
            avio_write(pb, buf2, strlen(buf2));
        }
    }

    /* construct frame header */

    snprintf(buf1, sizeof(buf1), "%s\n", Y4M_FRAME_MAGIC);
    avio_write(pb, buf1, strlen(buf1));

    width  = st->codec->width;
    height = st->codec->height;

    ptr = picture->data[0];

    switch (st->codec->pix_fmt) {
    case PIX_FMT_GRAY8:
    case PIX_FMT_YUV411P:
    case PIX_FMT_YUV420P:
    case PIX_FMT_YUV422P:
    case PIX_FMT_YUV444P:
        break;
    case PIX_FMT_GRAY16:
    case PIX_FMT_YUV420P9:
    case PIX_FMT_YUV422P9:
    case PIX_FMT_YUV444P9:
    case PIX_FMT_YUV420P10:
    case PIX_FMT_YUV422P10:
    case PIX_FMT_YUV444P10:
    case PIX_FMT_YUV420P16:
    case PIX_FMT_YUV422P16:
    case PIX_FMT_YUV444P16:
        width *= 2;
        break;
    default:
        av_log(s, AV_LOG_ERROR, "The pixel format '%s' is not supported.\n",
               av_get_pix_fmt_name(st->codec->pix_fmt));
        return AVERROR(EINVAL);
    }

    for (i = 0; i < height; i++) {
        avio_write(pb, ptr, width);
        ptr += picture->linesize[0];
    }

    if (st->codec->pix_fmt != PIX_FMT_GRAY8 &&
        st->codec->pix_fmt != PIX_FMT_GRAY16) {
        // Adjust for smaller Cb and Cr planes
        avcodec_get_chroma_sub_sample(st->codec->pix_fmt, &h_chroma_shift,
                                      &v_chroma_shift);
        width  >>= h_chroma_shift;
        height >>= v_chroma_shift;

        ptr1 = picture->data[1];
        ptr2 = picture->data[2];
        for (i = 0; i < height; i++) {     /* Cb */
            avio_write(pb, ptr1, width);
            ptr1 += picture->linesize[1];
        }
        for (i = 0; i < height; i++) {     /* Cr */
            avio_write(pb, ptr2, width);
            ptr2 += picture->linesize[2];
        }
    }

    avio_flush(pb);
    return 0;
}

static int yuv4_write_header(AVFormatContext *s)
{
    int *first_pkt = s->priv_data;

    if (s->nb_streams != 1)
        return AVERROR(EIO);

    if (s->streams[0]->codec->codec_id != AV_CODEC_ID_RAWVIDEO) {
        av_log(s, AV_LOG_ERROR,
               "A non-rawvideo stream was selected, but yuv4mpeg only handles rawvideo streams\n");
        return AVERROR(EINVAL);
    }

    switch (s->streams[0]->codec->pix_fmt) {
    case PIX_FMT_YUV411P:
        av_log(s, AV_LOG_WARNING, "Warning: generating rarely used 4:1:1 YUV "
               "stream, some mjpegtools might not work.\n");
        break;
    case PIX_FMT_GRAY8:
    case PIX_FMT_GRAY16:
    case PIX_FMT_YUV420P:
    case PIX_FMT_YUV422P:
    case PIX_FMT_YUV444P:
        break;
    case PIX_FMT_YUV420P9:
    case PIX_FMT_YUV422P9:
    case PIX_FMT_YUV444P9:
    case PIX_FMT_YUV420P10:
    case PIX_FMT_YUV422P10:
    case PIX_FMT_YUV444P10:
    case PIX_FMT_YUV420P16:
    case PIX_FMT_YUV422P16:
    case PIX_FMT_YUV444P16:
        if (s->streams[0]->codec->strict_std_compliance >= FF_COMPLIANCE_NORMAL) {
            av_log(s, AV_LOG_ERROR, "'%s' is not a official yuv4mpegpipe pixel format. "
                   "Use '-strict -1' to encode to this pixel format.\n",
                   av_get_pix_fmt_name(s->streams[0]->codec->pix_fmt));
            return AVERROR(EINVAL);
        }
        av_log(s, AV_LOG_WARNING, "Warning: generating non standart YUV stream. "
               "Mjpegtools will not work.\n");
        break;
    default:
        av_log(s, AV_LOG_ERROR, "ERROR: yuv4mpeg can only handle "
               "yuv444p, yuv422p, yuv420p, yuv411p and gray8 pixel formats. "
               "And using 'strict -1' also yuv444p9, yuv422p9, yuv420p9, "
               "yuv444p10, yuv422p10, yuv420p10, "
               "yuv444p16, yuv422p16, yuv420p16 "
               "and gray16 pixel formats. "
               "Use -pix_fmt to select one.\n");
        return AVERROR(EIO);
    }

    *first_pkt = 1;
    return 0;
}

AVOutputFormat ff_yuv4mpegpipe_muxer = {
    .name              = "yuv4mpegpipe",
    .long_name         = NULL_IF_CONFIG_SMALL("YUV4MPEG pipe"),
    .extensions        = "y4m",
    .priv_data_size    = sizeof(int),
    .audio_codec       = AV_CODEC_ID_NONE,
    .video_codec       = AV_CODEC_ID_RAWVIDEO,
    .write_header      = yuv4_write_header,
    .write_packet      = yuv4_write_packet,
    .flags             = AVFMT_RAWPICTURE,
};
#endif

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
    enum PixelFormat pix_fmt = PIX_FMT_NONE, alt_pix_fmt = PIX_FMT_NONE;
    enum AVChromaLocation chroma_sample_location = AVCHROMA_LOC_UNSPECIFIED;
    AVStream *st;
    struct frame_attributes *s1 = s->priv_data;

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

    s1->interlaced_frame = 0;
    s1->top_field_first = 0;
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
                pix_fmt = PIX_FMT_YUV420P;
                chroma_sample_location = AVCHROMA_LOC_CENTER;
            } else if (strncmp("420mpeg2", tokstart, 8) == 0) {
                pix_fmt = PIX_FMT_YUV420P;
                chroma_sample_location = AVCHROMA_LOC_LEFT;
            } else if (strncmp("420paldv", tokstart, 8) == 0) {
                pix_fmt = PIX_FMT_YUV420P;
                chroma_sample_location = AVCHROMA_LOC_TOPLEFT;
            } else if (strncmp("420p16", tokstart, 6) == 0) {
                pix_fmt = PIX_FMT_YUV420P16;
            } else if (strncmp("422p16", tokstart, 6) == 0) {
                pix_fmt = PIX_FMT_YUV422P16;
            } else if (strncmp("444p16", tokstart, 6) == 0) {
                pix_fmt = PIX_FMT_YUV444P16;
            } else if (strncmp("420p10", tokstart, 6) == 0) {
                pix_fmt = PIX_FMT_YUV420P10;
            } else if (strncmp("422p10", tokstart, 6) == 0) {
                pix_fmt = PIX_FMT_YUV422P10;
            } else if (strncmp("444p10", tokstart, 6) == 0) {
                pix_fmt = PIX_FMT_YUV444P10;
            } else if (strncmp("420p9", tokstart, 5) == 0) {
                pix_fmt = PIX_FMT_YUV420P9;
            } else if (strncmp("422p9", tokstart, 5) == 0) {
                pix_fmt = PIX_FMT_YUV422P9;
            } else if (strncmp("444p9", tokstart, 5) == 0) {
                pix_fmt = PIX_FMT_YUV444P9;
            } else if (strncmp("420", tokstart, 3) == 0) {
                pix_fmt = PIX_FMT_YUV420P;
                chroma_sample_location = AVCHROMA_LOC_CENTER;
            } else if (strncmp("411", tokstart, 3) == 0) {
                pix_fmt = PIX_FMT_YUV411P;
            } else if (strncmp("422", tokstart, 3) == 0) {
                pix_fmt = PIX_FMT_YUV422P;
            } else if (strncmp("444alpha", tokstart, 8) == 0 ) {
                av_log(s, AV_LOG_ERROR, "Cannot handle 4:4:4:4 "
                       "YUV4MPEG stream.\n");
                return -1;
            } else if (strncmp("444", tokstart, 3) == 0) {
                pix_fmt = PIX_FMT_YUV444P;
            } else if (strncmp("mono16", tokstart, 6) == 0) {
                pix_fmt = PIX_FMT_GRAY16;
            } else if (strncmp("mono", tokstart, 4) == 0) {
                pix_fmt = PIX_FMT_GRAY8;
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
                break;
            case 'p':
                s1->interlaced_frame = 0;
                break;
            case 't':
                s1->interlaced_frame = 1;
                s1->top_field_first = 1;
                break;
            case 'b':
                s1->interlaced_frame = 1;
                s1->top_field_first = 0;
                break;
            case 'm':
                av_log(s, AV_LOG_ERROR, "YUV4MPEG stream contains mixed "
                       "interlaced and non-interlaced frames.\n");
                return -1;
            default:
                av_log(s, AV_LOG_ERROR, "YUV4MPEG has invalid header.\n");
                return -1;
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
                    alt_pix_fmt = PIX_FMT_YUV420P;
                else if (strncmp("420MPEG2", tokstart, 8) == 0)
                    alt_pix_fmt = PIX_FMT_YUV420P;
                else if (strncmp("420PALDV", tokstart, 8) == 0)
                    alt_pix_fmt = PIX_FMT_YUV420P;
                else if (strncmp("420P9", tokstart, 5) == 0)
                    alt_pix_fmt = PIX_FMT_YUV420P9;
                else if (strncmp("422P9", tokstart, 5) == 0)
                    alt_pix_fmt = PIX_FMT_YUV422P9;
                else if (strncmp("444P9", tokstart, 5) == 0)
                    alt_pix_fmt = PIX_FMT_YUV444P9;
                else if (strncmp("420P10", tokstart, 6) == 0)
                    alt_pix_fmt = PIX_FMT_YUV420P10;
                else if (strncmp("422P10", tokstart, 6) == 0)
                    alt_pix_fmt = PIX_FMT_YUV422P10;
                else if (strncmp("444P10", tokstart, 6) == 0)
                    alt_pix_fmt = PIX_FMT_YUV444P10;
                else if (strncmp("420P16", tokstart, 6) == 0)
                    alt_pix_fmt = PIX_FMT_YUV420P16;
                else if (strncmp("422P16", tokstart, 6) == 0)
                    alt_pix_fmt = PIX_FMT_YUV422P16;
                else if (strncmp("444P16", tokstart, 6) == 0)
                    alt_pix_fmt = PIX_FMT_YUV444P16;
                else if (strncmp("411", tokstart, 3) == 0)
                    alt_pix_fmt = PIX_FMT_YUV411P;
                else if (strncmp("422", tokstart, 3) == 0)
                    alt_pix_fmt = PIX_FMT_YUV422P;
                else if (strncmp("444", tokstart, 3) == 0)
                    alt_pix_fmt = PIX_FMT_YUV444P;
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

    if (pix_fmt == PIX_FMT_NONE) {
        if (alt_pix_fmt == PIX_FMT_NONE)
            pix_fmt = PIX_FMT_YUV420P;
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
    st->codec->width  = width;
    st->codec->height = height;
    av_reduce(&raten, &rated, raten, rated, (1UL << 31) - 1);
    avpriv_set_pts_info(st, 64, rated, raten);
    st->codec->pix_fmt                = pix_fmt;
    st->codec->codec_type             = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id               = AV_CODEC_ID_RAWVIDEO;
    st->sample_aspect_ratio           = (AVRational){ aspectn, aspectd };
    st->codec->chroma_sample_location = chroma_sample_location;

    return 0;
}

static int yuv4_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int i;
    char header[MAX_FRAME_HEADER+1];
    int packet_size, width, height;
    AVStream *st = s->streams[0];
    struct frame_attributes *s1 = s->priv_data;

    for (i = 0; i < MAX_FRAME_HEADER; i++) {
        header[i] = avio_r8(s->pb);
        if (header[i] == '\n') {
            header[i + 1] = 0;
            break;
        }
    }
    if (i == MAX_FRAME_HEADER)
        return -1;
    if (strncmp(header, Y4M_FRAME_MAGIC, strlen(Y4M_FRAME_MAGIC)))
        return -1;

    width  = st->codec->width;
    height = st->codec->height;

    packet_size = avpicture_get_size(st->codec->pix_fmt, width, height);
    if (packet_size < 0)
        return -1;

    if (av_get_packet(s->pb, pkt, packet_size) != packet_size)
        return AVERROR(EIO);

    if (st->codec->coded_frame) {
        st->codec->coded_frame->interlaced_frame = s1->interlaced_frame;
        st->codec->coded_frame->top_field_first  = s1->top_field_first;
    }

    pkt->stream_index = 0;
    return 0;
}

static int yuv4_probe(AVProbeData *pd)
{
    /* check file header */
    if (strncmp(pd->buf, Y4M_MAGIC, sizeof(Y4M_MAGIC) - 1) == 0)
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

#if CONFIG_YUV4MPEGPIPE_DEMUXER
AVInputFormat ff_yuv4mpegpipe_demuxer = {
    .name           = "yuv4mpegpipe",
    .long_name      = NULL_IF_CONFIG_SMALL("YUV4MPEG pipe"),
    .priv_data_size = sizeof(struct frame_attributes),
    .read_probe     = yuv4_probe,
    .read_header    = yuv4_read_header,
    .read_packet    = yuv4_read_packet,
    .extensions     = "y4m",
};
#endif
