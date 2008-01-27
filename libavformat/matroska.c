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

const CodecTags ff_mkv_codec_tags[]={
//    {"V_MS/VFW/FOURCC"  , CODEC_ID_NONE},
    {"V_UNCOMPRESSED"   , CODEC_ID_RAWVIDEO},
    {"V_MPEG4/ISO/ASP"  , CODEC_ID_MPEG4},
    {"V_MPEG4/ISO/SP"   , CODEC_ID_MPEG4},
    {"V_MPEG4/ISO/AP"   , CODEC_ID_MPEG4},
    {"V_MPEG4/ISO/AVC"  , CODEC_ID_H264},
    {"V_MPEG4/MS/V3"    , CODEC_ID_MSMPEG4V3},
    {"V_MPEG1"          , CODEC_ID_MPEG1VIDEO},
    {"V_MPEG2"          , CODEC_ID_MPEG2VIDEO},
    {"V_MJPEG"          , CODEC_ID_MJPEG},
    {"V_REAL/RV10"      , CODEC_ID_RV10},
    {"V_REAL/RV20"      , CODEC_ID_RV20},
    {"V_REAL/RV30"      , CODEC_ID_RV30},
    {"V_REAL/RV40"      , CODEC_ID_RV40},
    {"V_THEORA"         , CODEC_ID_THEORA},
    {"V_SNOW"           , CODEC_ID_SNOW},
/* TODO: Real/Quicktime */

//    {"A_MS/ACM"         , CODEC_ID_NONE},
    {"A_MPEG/L3"        , CODEC_ID_MP3},
    {"A_MPEG/L2"        , CODEC_ID_MP2},
    {"A_MPEG/L1"        , CODEC_ID_MP2},
    {"A_PCM/INT/BIG"    , CODEC_ID_PCM_U16BE},
    {"A_PCM/INT/LIT"    , CODEC_ID_PCM_U16LE},
//    {"A_PCM/FLOAT/IEEE" , CODEC_ID_NONE},
    {"A_AC3"            , CODEC_ID_AC3},
    {"A_DTS"            , CODEC_ID_DTS},
    {"A_VORBIS"         , CODEC_ID_VORBIS},
    {"A_AAC"            , CODEC_ID_AAC},
    {"A_FLAC"           , CODEC_ID_FLAC},
    {"A_WAVPACK4"       , CODEC_ID_WAVPACK},
    {"A_TTA1"           , CODEC_ID_TTA},
    {"A_REAL/14_4"      , CODEC_ID_RA_144},
    {"A_REAL/28_8"      , CODEC_ID_RA_288},
    {"A_REAL/ATRC"      , CODEC_ID_ATRAC3},
    {"A_REAL/COOK"      , CODEC_ID_COOK},
//    {"A_REAL/SIPR"      , CODEC_ID_SIPRO},

    {"S_TEXT/UTF8"      , CODEC_ID_TEXT},
    {"S_TEXT/ASCII"     , CODEC_ID_TEXT},
    {"S_TEXT/ASS"       , CODEC_ID_SSA},
    {"S_TEXT/SSA"       , CODEC_ID_SSA},
    {"S_ASS"            , CODEC_ID_SSA},
    {"S_SSA"            , CODEC_ID_SSA},
    {"S_VOBSUB"         , CODEC_ID_DVD_SUBTITLE},

    {""                 , CODEC_ID_NONE}
/* TODO: AC3-9/10 (?), Real, Musepack, Quicktime */
};

const CodecMime ff_mkv_mime_tags[] = {
    {"text/plain"                 , CODEC_ID_TEXT},
    {"image/gif"                  , CODEC_ID_GIF},
    {"image/jpeg"                 , CODEC_ID_MJPEG},
    {"image/png"                  , CODEC_ID_PNG},
    {"image/tiff"                 , CODEC_ID_TIFF},
    {"application/x-truetype-font", CODEC_ID_TTF},
    {"application/x-font"         , CODEC_ID_TTF},

    {""                           , CODEC_ID_NONE}
};
