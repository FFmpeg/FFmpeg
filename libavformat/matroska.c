/*
 * Matroska common data
 * Copyright (c) 2003-2004 The ffmpeg Project
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

#include "matroska.h"

/* If you add a tag here that is not in ff_codec_bmp_tags[]
   or ff_codec_wav_tags[], add it also to additional_audio_tags[]
   or additional_video_tags[] in matroskaenc.c */
const CodecTags ff_mkv_codec_tags[]={
    {"A_AAC"            , AV_CODEC_ID_AAC},
    {"A_AC3"            , AV_CODEC_ID_AC3},
    {"A_ALAC"           , AV_CODEC_ID_ALAC},
    {"A_DTS"            , AV_CODEC_ID_DTS},
    {"A_EAC3"           , AV_CODEC_ID_EAC3},
    {"A_FLAC"           , AV_CODEC_ID_FLAC},
    {"A_MLP"            , AV_CODEC_ID_MLP},
    {"A_MPEG/L2"        , AV_CODEC_ID_MP2},
    {"A_MPEG/L1"        , AV_CODEC_ID_MP2},
    {"A_MPEG/L3"        , AV_CODEC_ID_MP3},
    {"A_OPUS",            AV_CODEC_ID_OPUS},
    {"A_OPUS/EXPERIMENTAL",AV_CODEC_ID_OPUS},
    {"A_PCM/FLOAT/IEEE" , AV_CODEC_ID_PCM_F32LE},
    {"A_PCM/FLOAT/IEEE" , AV_CODEC_ID_PCM_F64LE},
    {"A_PCM/INT/BIG"    , AV_CODEC_ID_PCM_S16BE},
    {"A_PCM/INT/BIG"    , AV_CODEC_ID_PCM_S24BE},
    {"A_PCM/INT/BIG"    , AV_CODEC_ID_PCM_S32BE},
    {"A_PCM/INT/LIT"    , AV_CODEC_ID_PCM_S16LE},
    {"A_PCM/INT/LIT"    , AV_CODEC_ID_PCM_S24LE},
    {"A_PCM/INT/LIT"    , AV_CODEC_ID_PCM_S32LE},
    {"A_PCM/INT/LIT"    , AV_CODEC_ID_PCM_U8},
    {"A_QUICKTIME/QDM2" , AV_CODEC_ID_QDM2},
    {"A_REAL/14_4"      , AV_CODEC_ID_RA_144},
    {"A_REAL/28_8"      , AV_CODEC_ID_RA_288},
    {"A_REAL/ATRC"      , AV_CODEC_ID_ATRAC3},
    {"A_REAL/COOK"      , AV_CODEC_ID_COOK},
    {"A_REAL/SIPR"      , AV_CODEC_ID_SIPR},
    {"A_TRUEHD"         , AV_CODEC_ID_TRUEHD},
    {"A_TTA1"           , AV_CODEC_ID_TTA},
    {"A_VORBIS"         , AV_CODEC_ID_VORBIS},
    {"A_WAVPACK4"       , AV_CODEC_ID_WAVPACK},

    {"D_WEBVTT/SUBTITLES"   , AV_CODEC_ID_WEBVTT},
    {"D_WEBVTT/CAPTIONS"    , AV_CODEC_ID_WEBVTT},
    {"D_WEBVTT/DESCRIPTIONS", AV_CODEC_ID_WEBVTT},
    {"D_WEBVTT/METADATA"    , AV_CODEC_ID_WEBVTT},

    {"S_TEXT/UTF8"      , AV_CODEC_ID_SUBRIP},
    {"S_TEXT/UTF8"      , AV_CODEC_ID_TEXT},
    {"S_TEXT/UTF8"      , AV_CODEC_ID_SRT},
    {"S_TEXT/ASCII"     , AV_CODEC_ID_TEXT},
#if FF_API_ASS_SSA
    {"S_TEXT/ASS"       , AV_CODEC_ID_SSA},
    {"S_TEXT/SSA"       , AV_CODEC_ID_SSA},
    {"S_ASS"            , AV_CODEC_ID_SSA},
    {"S_SSA"            , AV_CODEC_ID_SSA},
#endif
    {"S_TEXT/ASS"       , AV_CODEC_ID_ASS},
    {"S_TEXT/SSA"       , AV_CODEC_ID_ASS},
    {"S_ASS"            , AV_CODEC_ID_ASS},
    {"S_SSA"            , AV_CODEC_ID_ASS},
    {"S_VOBSUB"         , AV_CODEC_ID_DVD_SUBTITLE},
    {"S_DVBSUB"         , AV_CODEC_ID_DVB_SUBTITLE},
    {"S_HDMV/PGS"       , AV_CODEC_ID_HDMV_PGS_SUBTITLE},

    {"V_DIRAC"          , AV_CODEC_ID_DIRAC},
    {"V_MJPEG"          , AV_CODEC_ID_MJPEG},
    {"V_MPEG1"          , AV_CODEC_ID_MPEG1VIDEO},
    {"V_MPEG2"          , AV_CODEC_ID_MPEG2VIDEO},
    {"V_MPEG4/ISO/ASP"  , AV_CODEC_ID_MPEG4},
    {"V_MPEG4/ISO/AP"   , AV_CODEC_ID_MPEG4},
    {"V_MPEG4/ISO/SP"   , AV_CODEC_ID_MPEG4},
    {"V_MPEG4/ISO/AVC"  , AV_CODEC_ID_H264},
    {"V_MPEGH/ISO/HEVC" , AV_CODEC_ID_HEVC},
    {"V_MPEG4/MS/V3"    , AV_CODEC_ID_MSMPEG4V3},
    {"V_PRORES"         , AV_CODEC_ID_PRORES},
    {"V_REAL/RV10"      , AV_CODEC_ID_RV10},
    {"V_REAL/RV20"      , AV_CODEC_ID_RV20},
    {"V_REAL/RV30"      , AV_CODEC_ID_RV30},
    {"V_REAL/RV40"      , AV_CODEC_ID_RV40},
    {"V_SNOW"           , AV_CODEC_ID_SNOW},
    {"V_THEORA"         , AV_CODEC_ID_THEORA},
    {"V_UNCOMPRESSED"   , AV_CODEC_ID_RAWVIDEO},
    {"V_VP8"            , AV_CODEC_ID_VP8},
    {"V_VP9"            , AV_CODEC_ID_VP9},

    {""                 , AV_CODEC_ID_NONE}
};

const CodecMime ff_mkv_mime_tags[] = {
    {"text/plain"                 , AV_CODEC_ID_TEXT},
    {"image/gif"                  , AV_CODEC_ID_GIF},
    {"image/jpeg"                 , AV_CODEC_ID_MJPEG},
    {"image/png"                  , AV_CODEC_ID_PNG},
    {"image/tiff"                 , AV_CODEC_ID_TIFF},
    {"application/x-truetype-font", AV_CODEC_ID_TTF},
    {"application/x-font"         , AV_CODEC_ID_TTF},
    {"application/vnd.ms-opentype", AV_CODEC_ID_OTF},
    {"binary"                     , AV_CODEC_ID_BIN_DATA},

    {""                           , AV_CODEC_ID_NONE}
};

const AVMetadataConv ff_mkv_metadata_conv[] = {
    { "LEAD_PERFORMER", "performer" },
    { "PART_NUMBER"   , "track"  },
    { 0 }
};

const char * const ff_matroska_video_stereo_mode[MATROSKA_VIDEO_STEREO_MODE_COUNT] = {
    "mono",
    "left_right",
    "bottom_top",
    "top_bottom",
    "checkerboard_rl",
    "checkerboard_lr",
    "row_interleaved_rl",
    "row_interleaved_lr",
    "col_interleaved_rl",
    "col_interleaved_lr",
    "anaglyph_cyan_red",
    "right_left",
    "anaglyph_green_magenta",
    "block_lr",
    "block_rl",
};

const char * const ff_matroska_video_stereo_plane[MATROSKA_VIDEO_STEREO_PLANE_COUNT] = {
    "left",
    "right",
    "background",
};
