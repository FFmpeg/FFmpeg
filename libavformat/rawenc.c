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
    put_buffer(s->pb, pkt->data, pkt->size);
    put_flush_packet(s->pb);
    return 0;
}

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

#if CONFIG_G722_MUXER
AVOutputFormat g722_muxer = {
    "g722",
    NULL_IF_CONFIG_SMALL("raw G.722"),
    "audio/G722",
    "g722",
    0,
    CODEC_ID_ADPCM_G722,
    CODEC_ID_NONE,
    NULL,
    ff_raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
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
