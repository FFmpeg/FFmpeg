/*
 * YUV4MPEG muxer
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

#include "libavutil/frame.h"
#include "libavutil/pixdesc.h"
#include "avformat.h"
#include "internal.h"
#include "mux.h"
#include "yuv4mpeg.h"

static int yuv4_write_header(AVFormatContext *s)
{
    AVStream *st;
    AVIOContext *pb = s->pb;
    int width, height;
    int raten, rated, aspectn, aspectd, ret;
    char inter;
    const char *colorspace = "";
    const char *colorrange = "";
    int field_order;

    st     = s->streams[0];
    width  = st->codecpar->width;
    height = st->codecpar->height;
    field_order = st->codecpar->field_order;

    // TODO: should be avg_frame_rate
    av_reduce(&raten, &rated, st->time_base.den,
              st->time_base.num, (1UL << 31) - 1);

    aspectn = st->sample_aspect_ratio.num;
    aspectd = st->sample_aspect_ratio.den;

    if (aspectn == 0 && aspectd == 1)
        aspectd = 0;  // 0:0 means unknown

    switch(st->codecpar->color_range) {
    case AVCOL_RANGE_MPEG:
        colorrange = " XCOLORRANGE=LIMITED";
        break;
    case AVCOL_RANGE_JPEG:
        colorrange = " XCOLORRANGE=FULL";
        break;
    default:
        break;
    }

    switch (field_order) {
    case AV_FIELD_TB:
    case AV_FIELD_TT: inter = 't'; break;
    case AV_FIELD_BT:
    case AV_FIELD_BB: inter = 'b'; break;
    default:          inter = 'p'; break;
    }

    switch (st->codecpar->format) {
    case AV_PIX_FMT_GRAY8:
        colorspace = " Cmono";
        break;
    case AV_PIX_FMT_GRAY9:
        colorspace = " Cmono9";
        break;
    case AV_PIX_FMT_GRAY10:
        colorspace = " Cmono10";
        break;
    case AV_PIX_FMT_GRAY12:
        colorspace = " Cmono12";
        break;
    case AV_PIX_FMT_GRAY16:
        colorspace = " Cmono16";
        break;
    case AV_PIX_FMT_YUV411P:
        colorspace = " C411 XYSCSS=411";
        break;
    case AV_PIX_FMT_YUVJ420P:
        colorspace = " C420jpeg XYSCSS=420JPEG";
        colorrange = " XCOLORRANGE=FULL";
        break;
    case AV_PIX_FMT_YUVJ422P:
        colorspace = " C422 XYSCSS=422";
        colorrange = " XCOLORRANGE=FULL";
        break;
    case AV_PIX_FMT_YUVJ444P:
        colorspace = " C444 XYSCSS=444";
        colorrange = " XCOLORRANGE=FULL";
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
    case AV_PIX_FMT_YUVA444P:
        colorspace = " C444alpha XYSCSS=444";
        break;
    case AV_PIX_FMT_YUV420P9:
        colorspace = " C420p9 XYSCSS=420P9";
        break;
    case AV_PIX_FMT_YUV422P9:
        colorspace = " C422p9 XYSCSS=422P9";
        break;
    case AV_PIX_FMT_YUV444P9:
        colorspace = " C444p9 XYSCSS=444P9";
        break;
    case AV_PIX_FMT_YUV420P10:
        colorspace = " C420p10 XYSCSS=420P10";
        break;
    case AV_PIX_FMT_YUV422P10:
        colorspace = " C422p10 XYSCSS=422P10";
        break;
    case AV_PIX_FMT_YUV444P10:
        colorspace = " C444p10 XYSCSS=444P10";
        break;
    case AV_PIX_FMT_YUV420P12:
        colorspace = " C420p12 XYSCSS=420P12";
        break;
    case AV_PIX_FMT_YUV422P12:
        colorspace = " C422p12 XYSCSS=422P12";
        break;
    case AV_PIX_FMT_YUV444P12:
        colorspace = " C444p12 XYSCSS=444P12";
        break;
    case AV_PIX_FMT_YUV420P14:
        colorspace = " C420p14 XYSCSS=420P14";
        break;
    case AV_PIX_FMT_YUV422P14:
        colorspace = " C422p14 XYSCSS=422P14";
        break;
    case AV_PIX_FMT_YUV444P14:
        colorspace = " C444p14 XYSCSS=444P14";
        break;
    case AV_PIX_FMT_YUV420P16:
        colorspace = " C420p16 XYSCSS=420P16";
        break;
    case AV_PIX_FMT_YUV422P16:
        colorspace = " C422p16 XYSCSS=422P16";
        break;
    case AV_PIX_FMT_YUV444P16:
        colorspace = " C444p16 XYSCSS=444P16";
        break;
    }

    ret = avio_printf(pb, Y4M_MAGIC " W%d H%d F%d:%d I%c A%d:%d%s%s\n",
                      width, height, raten, rated, inter,
                      aspectn, aspectd, colorspace, colorrange);
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR,
               "Error. YUV4MPEG stream header write failed.\n");
        return ret;
    }

    return 0;
}


static int yuv4_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVStream *st = s->streams[pkt->stream_index];
    AVIOContext *pb = s->pb;
    const AVFrame *frame = (const AVFrame *)pkt->data;
    int width, height;
    const AVPixFmtDescriptor *desc;

    /* construct frame header */

    avio_printf(s->pb, Y4M_FRAME_MAGIC "\n");

    if (st->codecpar->codec_id == AV_CODEC_ID_RAWVIDEO) {
        avio_write(pb, pkt->data, pkt->size);
        return 0;
    }

    width  = st->codecpar->width;
    height = st->codecpar->height;
    desc   = av_pix_fmt_desc_get(st->codecpar->format);

    /* The following code presumes all planes to be non-interleaved. */
    for (int k = 0; k < desc->nb_components; k++) {
        int plane_height = height, plane_width = width;
        const uint8_t *ptr = frame->data[k];

        if (desc->nb_components >= 3 && (k == 1 || k == 2)) { /* chroma? */
            plane_width  = AV_CEIL_RSHIFT(plane_width,  desc->log2_chroma_w);
            plane_height = AV_CEIL_RSHIFT(plane_height, desc->log2_chroma_h);
        }
        plane_width *= desc->comp[k].step;

        for (int i = 0; i < plane_height; i++) {
            avio_write(pb, ptr, plane_width);
            ptr += frame->linesize[k];
        }
    }

    return 0;
}

static int yuv4_init(AVFormatContext *s)
{
    if (s->streams[0]->codecpar->codec_id != AV_CODEC_ID_WRAPPED_AVFRAME &&
        s->streams[0]->codecpar->codec_id != AV_CODEC_ID_RAWVIDEO) {
        av_log(s, AV_LOG_ERROR, "ERROR: Codec not supported.\n");
        return AVERROR_INVALIDDATA;
    }

    switch (s->streams[0]->codecpar->format) {
    case AV_PIX_FMT_YUV411P:
        av_log(s, AV_LOG_WARNING, "Warning: generating rarely used 4:1:1 YUV "
               "stream, some mjpegtools might not work.\n");
        break;
    case AV_PIX_FMT_GRAY8:
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUV444P:
    // TODO: remove YUVJ pixel formats when they are completely removed from the codebase.
    case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_YUVJ422P:
    case AV_PIX_FMT_YUVJ444P:
        break;
    case AV_PIX_FMT_GRAY9:
    case AV_PIX_FMT_GRAY10:
    case AV_PIX_FMT_GRAY12:
    case AV_PIX_FMT_GRAY16:
    case AV_PIX_FMT_YUV420P9:
    case AV_PIX_FMT_YUV422P9:
    case AV_PIX_FMT_YUV444P9:
    case AV_PIX_FMT_YUV420P10:
    case AV_PIX_FMT_YUV422P10:
    case AV_PIX_FMT_YUV444P10:
    case AV_PIX_FMT_YUV420P12:
    case AV_PIX_FMT_YUV422P12:
    case AV_PIX_FMT_YUV444P12:
    case AV_PIX_FMT_YUV420P14:
    case AV_PIX_FMT_YUV422P14:
    case AV_PIX_FMT_YUV444P14:
    case AV_PIX_FMT_YUV420P16:
    case AV_PIX_FMT_YUV422P16:
    case AV_PIX_FMT_YUV444P16:
    case AV_PIX_FMT_YUVA444P:
        if (s->strict_std_compliance >= FF_COMPLIANCE_NORMAL) {
            av_log(s, AV_LOG_ERROR, "'%s' is not an official yuv4mpegpipe pixel format. "
                   "Use '-strict -1' to encode to this pixel format.\n",
                   av_get_pix_fmt_name(s->streams[0]->codecpar->format));
            return AVERROR(EINVAL);
        }
        av_log(s, AV_LOG_WARNING, "Warning: generating non standard YUV stream. "
               "Mjpegtools will not work.\n");
        break;
    default:
        av_log(s, AV_LOG_ERROR, "ERROR: yuv4mpeg can only handle "
               "yuv444p, yuv422p, yuv420p, yuv411p and gray8 pixel formats. "
               "And using 'strict -1' also yuv444p9, yuv422p9, yuv420p9, "
               "yuv444p10, yuv422p10, yuv420p10, "
               "yuv444p12, yuv422p12, yuv420p12, "
               "yuv444p14, yuv422p14, yuv420p14, "
               "yuv444p16, yuv422p16, yuv420p16, "
               "yuva444p, "
               "gray9, gray10, gray12 "
               "and gray16 pixel formats. "
               "Use -pix_fmt to select one.\n");
        return AVERROR(EIO);
    }

    return 0;
}

const FFOutputFormat ff_yuv4mpegpipe_muxer = {
    .p.name            = "yuv4mpegpipe",
    .p.long_name       = NULL_IF_CONFIG_SMALL("YUV4MPEG pipe"),
    .p.extensions      = "y4m",
    .p.audio_codec     = AV_CODEC_ID_NONE,
    .p.video_codec     = AV_CODEC_ID_WRAPPED_AVFRAME,
    .p.subtitle_codec  = AV_CODEC_ID_NONE,
    .init              = yuv4_init,
    .write_header      = yuv4_write_header,
    .write_packet      = yuv4_write_packet,
    .flags_internal    = FF_OFMT_FLAG_MAX_ONE_OF_EACH,
};
