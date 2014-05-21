/*
 * RAW muxers
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
#include "rawenc.h"

int ff_raw_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    avio_write(s->pb, pkt->data, pkt->size);
    return 0;
}

static int force_one_stream(AVFormatContext *s)
{
    if (s->nb_streams != 1) {
        av_log(s, AV_LOG_ERROR, "%s files have exactly one stream\n",
               s->oformat->name);
        return AVERROR(EINVAL);
    }
    return 0;
}

/* Note: Do not forget to add new entries to the Makefile as well. */

#if CONFIG_AC3_MUXER
AVOutputFormat ff_ac3_muxer = {
    .name              = "ac3",
    .long_name         = NULL_IF_CONFIG_SMALL("raw AC-3"),
    .mime_type         = "audio/x-ac3",
    .extensions        = "ac3",
    .audio_codec       = AV_CODEC_ID_AC3,
    .video_codec       = AV_CODEC_ID_NONE,
    .write_header      = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .flags             = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_ADX_MUXER
AVOutputFormat ff_adx_muxer = {
    .name              = "adx",
    .long_name         = NULL_IF_CONFIG_SMALL("CRI ADX"),
    .extensions        = "adx",
    .audio_codec       = AV_CODEC_ID_ADPCM_ADX,
    .video_codec       = AV_CODEC_ID_NONE,
    .write_header      = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .flags             = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_CAVSVIDEO_MUXER
AVOutputFormat ff_cavsvideo_muxer = {
    .name              = "cavsvideo",
    .long_name         = NULL_IF_CONFIG_SMALL("raw Chinese AVS (Audio Video Standard) video"),
    .extensions        = "cavs",
    .audio_codec       = AV_CODEC_ID_NONE,
    .video_codec       = AV_CODEC_ID_CAVS,
    .write_header      = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .flags             = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_DATA_MUXER
AVOutputFormat ff_data_muxer = {
    .name              = "data",
    .long_name         = NULL_IF_CONFIG_SMALL("raw data"),
    .write_header      = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .flags             = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_DIRAC_MUXER
AVOutputFormat ff_dirac_muxer = {
    .name              = "dirac",
    .long_name         = NULL_IF_CONFIG_SMALL("raw Dirac"),
    .extensions        = "drc",
    .audio_codec       = AV_CODEC_ID_NONE,
    .video_codec       = AV_CODEC_ID_DIRAC,
    .write_header      = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .flags             = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_DNXHD_MUXER
AVOutputFormat ff_dnxhd_muxer = {
    .name              = "dnxhd",
    .long_name         = NULL_IF_CONFIG_SMALL("raw DNxHD (SMPTE VC-3)"),
    .extensions        = "dnxhd",
    .audio_codec       = AV_CODEC_ID_NONE,
    .video_codec       = AV_CODEC_ID_DNXHD,
    .write_header      = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .flags             = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_DTS_MUXER
AVOutputFormat ff_dts_muxer = {
    .name              = "dts",
    .long_name         = NULL_IF_CONFIG_SMALL("raw DTS"),
    .mime_type         = "audio/x-dca",
    .extensions        = "dts",
    .audio_codec       = AV_CODEC_ID_DTS,
    .video_codec       = AV_CODEC_ID_NONE,
    .write_header      = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .flags             = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_EAC3_MUXER
AVOutputFormat ff_eac3_muxer = {
    .name              = "eac3",
    .long_name         = NULL_IF_CONFIG_SMALL("raw E-AC-3"),
    .mime_type         = "audio/x-eac3",
    .extensions        = "eac3",
    .audio_codec       = AV_CODEC_ID_EAC3,
    .video_codec       = AV_CODEC_ID_NONE,
    .write_header      = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .flags             = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_G722_MUXER
AVOutputFormat ff_g722_muxer = {
    .name              = "g722",
    .long_name         = NULL_IF_CONFIG_SMALL("raw G.722"),
    .mime_type         = "audio/G722",
    .extensions        = "g722",
    .audio_codec       = AV_CODEC_ID_ADPCM_G722,
    .video_codec       = AV_CODEC_ID_NONE,
    .write_header      = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .flags             = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_G723_1_MUXER
AVOutputFormat ff_g723_1_muxer = {
    .name              = "g723_1",
    .long_name         = NULL_IF_CONFIG_SMALL("raw G.723.1"),
    .mime_type         = "audio/g723",
    .extensions        = "tco,rco",
    .audio_codec       = AV_CODEC_ID_G723_1,
    .video_codec       = AV_CODEC_ID_NONE,
    .write_header      = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .flags             = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_H261_MUXER
AVOutputFormat ff_h261_muxer = {
    .name              = "h261",
    .long_name         = NULL_IF_CONFIG_SMALL("raw H.261"),
    .mime_type         = "video/x-h261",
    .extensions        = "h261",
    .audio_codec       = AV_CODEC_ID_NONE,
    .video_codec       = AV_CODEC_ID_H261,
    .write_header      = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .flags             = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_H263_MUXER
AVOutputFormat ff_h263_muxer = {
    .name              = "h263",
    .long_name         = NULL_IF_CONFIG_SMALL("raw H.263"),
    .mime_type         = "video/x-h263",
    .extensions        = "h263",
    .audio_codec       = AV_CODEC_ID_NONE,
    .video_codec       = AV_CODEC_ID_H263,
    .write_header      = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .flags             = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_H264_MUXER
AVOutputFormat ff_h264_muxer = {
    .name              = "h264",
    .long_name         = NULL_IF_CONFIG_SMALL("raw H.264 video"),
    .extensions        = "h264",
    .audio_codec       = AV_CODEC_ID_NONE,
    .video_codec       = AV_CODEC_ID_H264,
    .write_header      = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .flags             = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_HEVC_MUXER
AVOutputFormat ff_hevc_muxer = {
    .name              = "hevc",
    .long_name         = NULL_IF_CONFIG_SMALL("raw HEVC video"),
    .extensions        = "hevc",
    .audio_codec       = AV_CODEC_ID_NONE,
    .video_codec       = AV_CODEC_ID_HEVC,
    .write_packet      = ff_raw_write_packet,
    .flags             = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_M4V_MUXER
AVOutputFormat ff_m4v_muxer = {
    .name              = "m4v",
    .long_name         = NULL_IF_CONFIG_SMALL("raw MPEG-4 video"),
    .extensions        = "m4v",
    .audio_codec       = AV_CODEC_ID_NONE,
    .video_codec       = AV_CODEC_ID_MPEG4,
    .write_packet      = ff_raw_write_packet,
    .flags             = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_MJPEG_MUXER
AVOutputFormat ff_mjpeg_muxer = {
    .name              = "mjpeg",
    .long_name         = NULL_IF_CONFIG_SMALL("raw MJPEG video"),
    .mime_type         = "video/x-mjpeg",
    .extensions        = "mjpg,mjpeg",
    .audio_codec       = AV_CODEC_ID_NONE,
    .video_codec       = AV_CODEC_ID_MJPEG,
    .write_header      = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .flags             = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_MLP_MUXER
AVOutputFormat ff_mlp_muxer = {
    .name              = "mlp",
    .long_name         = NULL_IF_CONFIG_SMALL("raw MLP"),
    .extensions        = "mlp",
    .audio_codec       = AV_CODEC_ID_MLP,
    .video_codec       = AV_CODEC_ID_NONE,
    .write_header      = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .flags             = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_MPEG1VIDEO_MUXER
AVOutputFormat ff_mpeg1video_muxer = {
    .name              = "mpeg1video",
    .long_name         = NULL_IF_CONFIG_SMALL("raw MPEG-1 video"),
    .mime_type         = "video/mpeg",
    .extensions        = "mpg,mpeg,m1v",
    .audio_codec       = AV_CODEC_ID_NONE,
    .video_codec       = AV_CODEC_ID_MPEG1VIDEO,
    .write_header      = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .flags             = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_MPEG2VIDEO_MUXER
AVOutputFormat ff_mpeg2video_muxer = {
    .name              = "mpeg2video",
    .long_name         = NULL_IF_CONFIG_SMALL("raw MPEG-2 video"),
    .extensions        = "m2v",
    .audio_codec       = AV_CODEC_ID_NONE,
    .video_codec       = AV_CODEC_ID_MPEG2VIDEO,
    .write_header      = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .flags             = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_RAWVIDEO_MUXER
AVOutputFormat ff_rawvideo_muxer = {
    .name              = "rawvideo",
    .long_name         = NULL_IF_CONFIG_SMALL("raw video"),
    .extensions        = "yuv,rgb",
    .audio_codec       = AV_CODEC_ID_NONE,
    .video_codec       = AV_CODEC_ID_RAWVIDEO,
    .write_packet      = ff_raw_write_packet,
    .flags             = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_TRUEHD_MUXER
AVOutputFormat ff_truehd_muxer = {
    .name              = "truehd",
    .long_name         = NULL_IF_CONFIG_SMALL("raw TrueHD"),
    .extensions        = "thd",
    .audio_codec       = AV_CODEC_ID_TRUEHD,
    .video_codec       = AV_CODEC_ID_NONE,
    .write_header      = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .flags             = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_VC1_MUXER
AVOutputFormat ff_vc1_muxer = {
    .name              = "vc1",
    .long_name         = NULL_IF_CONFIG_SMALL("raw VC-1 video"),
    .extensions        = "vc1",
    .audio_codec       = AV_CODEC_ID_NONE,
    .video_codec       = AV_CODEC_ID_VC1,
    .write_header      = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .flags             = AVFMT_NOTIMESTAMPS,
};
#endif
