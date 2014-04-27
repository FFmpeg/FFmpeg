/*
 * Matroska common data
 * Copyright (c) 2003-2004 The ffmpeg Project
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

#include "matroska.h"

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
    {"A_OPUS"           , AV_CODEC_ID_OPUS},
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

    {"S_TEXT/UTF8"      , AV_CODEC_ID_TEXT},
    {"S_TEXT/UTF8"      , AV_CODEC_ID_SRT},
    {"S_TEXT/ASCII"     , AV_CODEC_ID_TEXT},
    {"S_TEXT/ASS"       , AV_CODEC_ID_SSA},
    {"S_TEXT/SSA"       , AV_CODEC_ID_SSA},
    {"S_ASS"            , AV_CODEC_ID_SSA},
    {"S_SSA"            , AV_CODEC_ID_SSA},
    {"S_VOBSUB"         , AV_CODEC_ID_DVD_SUBTITLE},
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

    {""                           , AV_CODEC_ID_NONE}
};

const AVMetadataConv ff_mkv_metadata_conv[] = {
    { "LEAD_PERFORMER", "performer" },
    { "PART_NUMBER"   , "track"  },
    { 0 }
};
