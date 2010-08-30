/*
 * RAW muxer and demuxer
 * Copyright (c) 2001 Fabrice Bellard
 * Copyright (c) 2005 Alex Beregszaszi
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
#include "raw.h"

/* simple formats */

#if CONFIG_MUXERS
int ff_raw_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    put_buffer(s->pb, pkt->data, pkt->size);
    put_flush_packet(s->pb);
    return 0;
}
#endif

#if CONFIG_DEMUXERS
/* raw input */
int ff_raw_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    AVStream *st;
    enum CodecID id;

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);

        id = s->iformat->value;
        if (id == CODEC_ID_RAWVIDEO) {
            st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
        } else {
            st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
        }
        st->codec->codec_id = id;

        switch(st->codec->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            st->codec->sample_rate = ap->sample_rate;
            if(ap->channels) st->codec->channels = ap->channels;
            else             st->codec->channels = 1;
            st->codec->bits_per_coded_sample = av_get_bits_per_sample(st->codec->codec_id);
            assert(st->codec->bits_per_coded_sample > 0);
            st->codec->block_align = st->codec->bits_per_coded_sample*st->codec->channels/8;
            av_set_pts_info(st, 64, 1, st->codec->sample_rate);
            break;
        case AVMEDIA_TYPE_VIDEO:
            if(ap->time_base.num)
                av_set_pts_info(st, 64, ap->time_base.num, ap->time_base.den);
            else
                av_set_pts_info(st, 64, 1, 25);
            st->codec->width = ap->width;
            st->codec->height = ap->height;
            st->codec->pix_fmt = ap->pix_fmt;
            if(st->codec->pix_fmt == PIX_FMT_NONE)
                st->codec->pix_fmt= PIX_FMT_YUV420P;
            break;
        default:
            return -1;
        }
    return 0;
}

#define RAW_PACKET_SIZE 1024

int ff_raw_read_partial_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, size;

    size = RAW_PACKET_SIZE;

    if (av_new_packet(pkt, size) < 0)
        return AVERROR(ENOMEM);

    pkt->pos= url_ftell(s->pb);
    pkt->stream_index = 0;
    ret = get_partial_buffer(s->pb, pkt->data, size);
    if (ret < 0) {
        av_free_packet(pkt);
        return ret;
    }
    pkt->size = ret;
    return ret;
}
#endif

#if CONFIG_RAWVIDEO_DEMUXER
static int rawvideo_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int packet_size, ret, width, height;
    AVStream *st = s->streams[0];

    width = st->codec->width;
    height = st->codec->height;

    packet_size = avpicture_get_size(st->codec->pix_fmt, width, height);
    if (packet_size < 0)
        return -1;

    ret= av_get_packet(s->pb, pkt, packet_size);
    pkt->pts=
    pkt->dts= pkt->pos / packet_size;

    pkt->stream_index = 0;
    if (ret < 0)
        return ret;
    return 0;
}
#endif

#if CONFIG_DEMUXERS
int ff_raw_audio_read_header(AVFormatContext *s,
                             AVFormatParameters *ap)
{
    AVStream *st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);
    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id = s->iformat->value;
    st->need_parsing = AVSTREAM_PARSE_FULL;
    /* the parameters will be extracted from the compressed bitstream */

    return 0;
}

/* MPEG-1/H.263 input */
int ff_raw_video_read_header(AVFormatContext *s,
                             AVFormatParameters *ap)
{
    AVStream *st;

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);

    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id = s->iformat->value;
    st->need_parsing = AVSTREAM_PARSE_FULL;

    /* for MJPEG, specify frame rate */
    /* for MPEG-4 specify it, too (most MPEG-4 streams do not have the fixed_vop_rate set ...)*/
    if (ap->time_base.num) {
        st->codec->time_base= ap->time_base;
    } else if ( st->codec->codec_id == CODEC_ID_MJPEG ||
                st->codec->codec_id == CODEC_ID_MPEG4 ||
                st->codec->codec_id == CODEC_ID_DIRAC ||
                st->codec->codec_id == CODEC_ID_DNXHD ||
                st->codec->codec_id == CODEC_ID_VC1   ||
                st->codec->codec_id == CODEC_ID_H264) {
        st->codec->time_base= (AVRational){1,25};
    }
    av_set_pts_info(st, 64, 1, 1200000);

    return 0;
}
#endif

/* Note: Do not forget to add new entries to the Makefile as well. */

#if CONFIG_AC3_MUXER
AVOutputFormat ac3_muxer = {
    "ac3",
    NULL_IF_CONFIG_SMALL("raw AC-3"),
    "audio/x-ac3",
    "ac3",
    0,
    CODEC_ID_AC3,
    CODEC_ID_NONE,
    NULL,
    ff_raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_DIRAC_MUXER
AVOutputFormat dirac_muxer = {
    "dirac",
    NULL_IF_CONFIG_SMALL("raw Dirac"),
    NULL,
    "drc",
    0,
    CODEC_ID_NONE,
    CODEC_ID_DIRAC,
    NULL,
    ff_raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_DNXHD_MUXER
AVOutputFormat dnxhd_muxer = {
    "dnxhd",
    NULL_IF_CONFIG_SMALL("raw DNxHD (SMPTE VC-3)"),
    NULL,
    "dnxhd",
    0,
    CODEC_ID_NONE,
    CODEC_ID_DNXHD,
    NULL,
    ff_raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_DTS_MUXER
AVOutputFormat dts_muxer = {
    "dts",
    NULL_IF_CONFIG_SMALL("raw DTS"),
    "audio/x-dca",
    "dts",
    0,
    CODEC_ID_DTS,
    CODEC_ID_NONE,
    NULL,
    ff_raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_EAC3_MUXER
AVOutputFormat eac3_muxer = {
    "eac3",
    NULL_IF_CONFIG_SMALL("raw E-AC-3"),
    "audio/x-eac3",
    "eac3",
    0,
    CODEC_ID_EAC3,
    CODEC_ID_NONE,
    NULL,
    ff_raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_GSM_DEMUXER
AVInputFormat gsm_demuxer = {
    "gsm",
    NULL_IF_CONFIG_SMALL("raw GSM"),
    0,
    NULL,
    ff_raw_audio_read_header,
    ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "gsm",
    .value = CODEC_ID_GSM,
};
#endif

#if CONFIG_H261_MUXER
AVOutputFormat h261_muxer = {
    "h261",
    NULL_IF_CONFIG_SMALL("raw H.261"),
    "video/x-h261",
    "h261",
    0,
    CODEC_ID_NONE,
    CODEC_ID_H261,
    NULL,
    ff_raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_H263_MUXER
AVOutputFormat h263_muxer = {
    "h263",
    NULL_IF_CONFIG_SMALL("raw H.263"),
    "video/x-h263",
    "h263",
    0,
    CODEC_ID_NONE,
    CODEC_ID_H263,
    NULL,
    ff_raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_H264_MUXER
AVOutputFormat h264_muxer = {
    "h264",
    NULL_IF_CONFIG_SMALL("raw H.264 video format"),
    NULL,
    "h264",
    0,
    CODEC_ID_NONE,
    CODEC_ID_H264,
    NULL,
    ff_raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_CAVSVIDEO_MUXER
AVOutputFormat cavsvideo_muxer = {
    "cavsvideo",
    NULL_IF_CONFIG_SMALL("raw Chinese AVS video"),
    NULL,
    "cavs",
    0,
    CODEC_ID_NONE,
    CODEC_ID_CAVS,
    NULL,
    ff_raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_M4V_MUXER
AVOutputFormat m4v_muxer = {
    "m4v",
    NULL_IF_CONFIG_SMALL("raw MPEG-4 video format"),
    NULL,
    "m4v",
    0,
    CODEC_ID_NONE,
    CODEC_ID_MPEG4,
    NULL,
    ff_raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_MJPEG_DEMUXER
AVInputFormat mjpeg_demuxer = {
    "mjpeg",
    NULL_IF_CONFIG_SMALL("raw MJPEG video"),
    0,
    NULL,
    ff_raw_video_read_header,
    ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "mjpg,mjpeg",
    .value = CODEC_ID_MJPEG,
};
#endif

#if CONFIG_MJPEG_MUXER
AVOutputFormat mjpeg_muxer = {
    "mjpeg",
    NULL_IF_CONFIG_SMALL("raw MJPEG video"),
    "video/x-mjpeg",
    "mjpg,mjpeg",
    0,
    CODEC_ID_NONE,
    CODEC_ID_MJPEG,
    NULL,
    ff_raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_MLP_DEMUXER
AVInputFormat mlp_demuxer = {
    "mlp",
    NULL_IF_CONFIG_SMALL("raw MLP"),
    0,
    NULL,
    ff_raw_audio_read_header,
    ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "mlp",
    .value = CODEC_ID_MLP,
};
#endif

#if CONFIG_MLP_MUXER
AVOutputFormat mlp_muxer = {
    "mlp",
    NULL_IF_CONFIG_SMALL("raw MLP"),
    NULL,
    "mlp",
    0,
    CODEC_ID_MLP,
    CODEC_ID_NONE,
    NULL,
    ff_raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_SRT_MUXER
AVOutputFormat srt_muxer = {
    .name           = "srt",
    .long_name      = NULL_IF_CONFIG_SMALL("SubRip subtitle format"),
    .mime_type      = "application/x-subrip",
    .extensions     = "srt",
    .write_packet   = ff_raw_write_packet,
    .flags          = AVFMT_NOTIMESTAMPS,
    .subtitle_codec = CODEC_ID_SRT,
};
#endif

#if CONFIG_TRUEHD_DEMUXER
AVInputFormat truehd_demuxer = {
    "truehd",
    NULL_IF_CONFIG_SMALL("raw TrueHD"),
    0,
    NULL,
    ff_raw_audio_read_header,
    ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "thd",
    .value = CODEC_ID_TRUEHD,
};
#endif

#if CONFIG_TRUEHD_MUXER
AVOutputFormat truehd_muxer = {
    "truehd",
    NULL_IF_CONFIG_SMALL("raw TrueHD"),
    NULL,
    "thd",
    0,
    CODEC_ID_TRUEHD,
    CODEC_ID_NONE,
    NULL,
    ff_raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_MPEG1VIDEO_MUXER
AVOutputFormat mpeg1video_muxer = {
    "mpeg1video",
    NULL_IF_CONFIG_SMALL("raw MPEG-1 video"),
    "video/x-mpeg",
    "mpg,mpeg,m1v",
    0,
    CODEC_ID_NONE,
    CODEC_ID_MPEG1VIDEO,
    NULL,
    ff_raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_MPEG2VIDEO_MUXER
AVOutputFormat mpeg2video_muxer = {
    "mpeg2video",
    NULL_IF_CONFIG_SMALL("raw MPEG-2 video"),
    NULL,
    "m2v",
    0,
    CODEC_ID_NONE,
    CODEC_ID_MPEG2VIDEO,
    NULL,
    ff_raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_RAWVIDEO_DEMUXER
AVInputFormat rawvideo_demuxer = {
    "rawvideo",
    NULL_IF_CONFIG_SMALL("raw video format"),
    0,
    NULL,
    ff_raw_read_header,
    rawvideo_read_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "yuv,cif,qcif,rgb",
    .value = CODEC_ID_RAWVIDEO,
};
#endif

#if CONFIG_RAWVIDEO_MUXER
AVOutputFormat rawvideo_muxer = {
    "rawvideo",
    NULL_IF_CONFIG_SMALL("raw video format"),
    NULL,
    "yuv,rgb",
    0,
    CODEC_ID_NONE,
    CODEC_ID_RAWVIDEO,
    NULL,
    ff_raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_SHORTEN_DEMUXER
AVInputFormat shorten_demuxer = {
    "shn",
    NULL_IF_CONFIG_SMALL("raw Shorten"),
    0,
    NULL,
    ff_raw_audio_read_header,
    ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "shn",
    .value = CODEC_ID_SHORTEN,
};
#endif

#if CONFIG_VC1_DEMUXER
AVInputFormat vc1_demuxer = {
    "vc1",
    NULL_IF_CONFIG_SMALL("raw VC-1"),
    0,
    NULL /* vc1_probe */,
    ff_raw_video_read_header,
    ff_raw_read_partial_packet,
    .extensions = "vc1",
    .value = CODEC_ID_VC1,
};
#endif
