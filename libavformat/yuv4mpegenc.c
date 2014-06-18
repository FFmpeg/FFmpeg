/*
 * YUV4MPEG muxer
 * Copyright (c) 2001, 2002, 2003 Fabrice Bellard
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/pixdesc.h"
#include "avformat.h"
#include "internal.h"
#include "yuv4mpeg.h"

#define Y4M_LINE_MAX 256

static int yuv4_generate_header(AVFormatContext *s, char* buf)
{
    AVStream *st;
    int width, height;
    int raten, rated, aspectn, aspectd, n;
    char inter;
    const char *colorspace = "";

    st     = s->streams[0];
    width  = st->codecpar->width;
    height = st->codecpar->height;

    // TODO: should be avg_frame_rate
    av_reduce(&raten, &rated, st->time_base.den,
              st->time_base.num, (1UL << 31) - 1);

    aspectn = st->sample_aspect_ratio.num;
    aspectd = st->sample_aspect_ratio.den;

    if (aspectn == 0 && aspectd == 1)
        aspectd = 0;  // 0:0 means unknown

    switch (st->codecpar->field_order) {
    case AV_FIELD_TT: inter = 't'; break;
    case AV_FIELD_BB: inter = 'b'; break;
    default:          inter = 'p'; break;
    }

    switch (st->codecpar->format) {
    case AV_PIX_FMT_GRAY8:
        colorspace = " Cmono";
        break;
    case AV_PIX_FMT_YUV411P:
        colorspace = " C411 XYSCSS=411";
        break;
    case AV_PIX_FMT_YUV420P:
        switch (st->codecpar->chroma_location) {
        case AVCHROMA_LOC_TOPLEFT: colorspace = " C420paldv XYSCSS=420PALDV"; break;
        case AVCHROMA_LOC_LEFT:    colorspace = " C420mpeg2 XYSCSS=420MPEG2"; break;
        default:                   colorspace = " C420jpeg XYSCSS=420JPEG";   break;
        }
        break;
    case AV_PIX_FMT_YUV422P:
        colorspace = " C422 XYSCSS=422";
        break;
    case AV_PIX_FMT_YUV444P:
        colorspace = " C444 XYSCSS=444";
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
    AVFrame *frame;
    int* first_pkt = s->priv_data;
    int width, height, h_chroma_shift, v_chroma_shift;
    int i;
    char buf2[Y4M_LINE_MAX + 1];
    char buf1[20];
    uint8_t *ptr, *ptr1, *ptr2;

    frame = (AVFrame *)pkt->data;

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

    width  = st->codecpar->width;
    height = st->codecpar->height;

    ptr = frame->data[0];
    for (i = 0; i < height; i++) {
        avio_write(pb, ptr, width);
        ptr += frame->linesize[0];
    }

    if (st->codecpar->format != AV_PIX_FMT_GRAY8) {
        // Adjust for smaller Cb and Cr planes
        av_pix_fmt_get_chroma_sub_sample(st->codecpar->format, &h_chroma_shift,
                                         &v_chroma_shift);
        // Shift right, rounding up
        width  = AV_CEIL_RSHIFT(width, h_chroma_shift);
        height = AV_CEIL_RSHIFT(height, v_chroma_shift);

        ptr1 = frame->data[1];
        ptr2 = frame->data[2];
        for (i = 0; i < height; i++) {     /* Cb */
            avio_write(pb, ptr1, width);
            ptr1 += frame->linesize[1];
        }
        for (i = 0; i < height; i++) {     /* Cr */
            avio_write(pb, ptr2, width);
            ptr2 += frame->linesize[2];
        }
    }
    return 0;
}

static int yuv4_write_header(AVFormatContext *s)
{
    int *first_pkt = s->priv_data;

    if (s->nb_streams != 1)
        return AVERROR(EIO);

    if (s->streams[0]->codecpar->codec_id != AV_CODEC_ID_WRAPPED_AVFRAME) {
        av_log(s, AV_LOG_ERROR, "ERROR: Codec not supported.\n");
        return AVERROR_INVALIDDATA;
    }

    if (s->streams[0]->codecpar->format == AV_PIX_FMT_YUV411P) {
        av_log(s, AV_LOG_ERROR, "Warning: generating rarely used 4:1:1 YUV "
               "stream, some mjpegtools might not work.\n");
    } else if ((s->streams[0]->codecpar->format != AV_PIX_FMT_YUV420P) &&
               (s->streams[0]->codecpar->format != AV_PIX_FMT_YUV422P) &&
               (s->streams[0]->codecpar->format != AV_PIX_FMT_GRAY8)   &&
               (s->streams[0]->codecpar->format != AV_PIX_FMT_YUV444P)) {
        av_log(s, AV_LOG_ERROR, "ERROR: yuv4mpeg only handles yuv444p, "
               "yuv422p, yuv420p, yuv411p and gray pixel formats. "
               "Use -pix_fmt to select one.\n");
        return AVERROR(EIO);
    }

    *first_pkt = 1;
    return 0;
}

AVOutputFormat ff_yuv4mpegpipe_muxer = {
    .name              = "yuv4mpegpipe",
    .long_name         = NULL_IF_CONFIG_SMALL("YUV4MPEG pipe"),
    .mime_type         = "",
    .extensions        = "y4m",
    .priv_data_size    = sizeof(int),
    .audio_codec       = AV_CODEC_ID_NONE,
    .video_codec       = AV_CODEC_ID_WRAPPED_AVFRAME,
    .write_header      = yuv4_write_header,
    .write_packet      = yuv4_write_packet,
};
