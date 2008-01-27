/*
 * Matroska constants
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

#ifndef FFMPEG_MATROSKA_H
#define FFMPEG_MATROSKA_H

#include "avcodec.h"

/* EBML version supported */
#define EBML_VERSION 1

/* top-level master-IDs */
#define EBML_ID_HEADER             0x1A45DFA3

/* IDs in the HEADER master */
#define EBML_ID_EBMLVERSION        0x4286
#define EBML_ID_EBMLREADVERSION    0x42F7
#define EBML_ID_EBMLMAXIDLENGTH    0x42F2
#define EBML_ID_EBMLMAXSIZELENGTH  0x42F3
#define EBML_ID_DOCTYPE            0x4282
#define EBML_ID_DOCTYPEVERSION     0x4287
#define EBML_ID_DOCTYPEREADVERSION 0x4285

/* general EBML types */
#define EBML_ID_VOID               0xEC

/*
 * Matroska element IDs. max. 32-bit.
 */

/* toplevel segment */
#define MATROSKA_ID_SEGMENT    0x18538067

/* matroska top-level master IDs */
#define MATROSKA_ID_INFO       0x1549A966
#define MATROSKA_ID_TRACKS     0x1654AE6B
#define MATROSKA_ID_CUES       0x1C53BB6B
#define MATROSKA_ID_TAGS       0x1254C367
#define MATROSKA_ID_SEEKHEAD   0x114D9B74
#define MATROSKA_ID_ATTACHMENTS 0x1941A469
#define MATROSKA_ID_CLUSTER    0x1F43B675

/* IDs in the info master */
#define MATROSKA_ID_TIMECODESCALE 0x2AD7B1
#define MATROSKA_ID_DURATION   0x4489
#define MATROSKA_ID_TITLE      0x7BA9
#define MATROSKA_ID_WRITINGAPP 0x5741
#define MATROSKA_ID_MUXINGAPP  0x4D80
#define MATROSKA_ID_DATEUTC    0x4461
#define MATROSKA_ID_SEGMENTUID 0x73A4

/* ID in the tracks master */
#define MATROSKA_ID_TRACKENTRY 0xAE

/* IDs in the trackentry master */
#define MATROSKA_ID_TRACKNUMBER 0xD7
#define MATROSKA_ID_TRACKUID   0x73C5
#define MATROSKA_ID_TRACKTYPE  0x83
#define MATROSKA_ID_TRACKAUDIO 0xE1
#define MATROSKA_ID_TRACKVIDEO 0xE0
#define MATROSKA_ID_CODECID    0x86
#define MATROSKA_ID_CODECPRIVATE 0x63A2
#define MATROSKA_ID_CODECNAME  0x258688
#define MATROSKA_ID_CODECINFOURL 0x3B4040
#define MATROSKA_ID_CODECDOWNLOADURL 0x26B240
#define MATROSKA_ID_TRACKNAME  0x536E
#define MATROSKA_ID_TRACKLANGUAGE 0x22B59C
#define MATROSKA_ID_TRACKFLAGENABLED 0xB9
#define MATROSKA_ID_TRACKFLAGDEFAULT 0x88
#define MATROSKA_ID_TRACKFLAGLACING 0x9C
#define MATROSKA_ID_TRACKMINCACHE 0x6DE7
#define MATROSKA_ID_TRACKMAXCACHE 0x6DF8
#define MATROSKA_ID_TRACKDEFAULTDURATION 0x23E383

/* IDs in the trackvideo master */
#define MATROSKA_ID_VIDEOFRAMERATE 0x2383E3
#define MATROSKA_ID_VIDEODISPLAYWIDTH 0x54B0
#define MATROSKA_ID_VIDEODISPLAYHEIGHT 0x54BA
#define MATROSKA_ID_VIDEOPIXELWIDTH 0xB0
#define MATROSKA_ID_VIDEOPIXELHEIGHT 0xBA
#define MATROSKA_ID_VIDEOFLAGINTERLACED 0x9A
#define MATROSKA_ID_VIDEOSTEREOMODE 0x53B9
#define MATROSKA_ID_VIDEOASPECTRATIO 0x54B3
#define MATROSKA_ID_VIDEOCOLORSPACE 0x2EB524

/* IDs in the trackaudio master */
#define MATROSKA_ID_AUDIOSAMPLINGFREQ 0xB5
#define MATROSKA_ID_AUDIOOUTSAMPLINGFREQ 0x78B5

#define MATROSKA_ID_AUDIOBITDEPTH 0x6264
#define MATROSKA_ID_AUDIOCHANNELS 0x9F

/* ID in the cues master */
#define MATROSKA_ID_POINTENTRY 0xBB

/* IDs in the pointentry master */
#define MATROSKA_ID_CUETIME    0xB3
#define MATROSKA_ID_CUETRACKPOSITION 0xB7

/* IDs in the cuetrackposition master */
#define MATROSKA_ID_CUETRACK   0xF7
#define MATROSKA_ID_CUECLUSTERPOSITION 0xF1

/* IDs in the tags master */
/* TODO */

/* IDs in the seekhead master */
#define MATROSKA_ID_SEEKENTRY  0x4DBB

/* IDs in the seekpoint master */
#define MATROSKA_ID_SEEKID     0x53AB
#define MATROSKA_ID_SEEKPOSITION 0x53AC

/* IDs in the cluster master */
#define MATROSKA_ID_CLUSTERTIMECODE 0xE7
#define MATROSKA_ID_BLOCKGROUP 0xA0
#define MATROSKA_ID_SIMPLEBLOCK 0xA3

/* IDs in the blockgroup master */
#define MATROSKA_ID_BLOCK      0xA1
#define MATROSKA_ID_BLOCKDURATION 0x9B
#define MATROSKA_ID_BLOCKREFERENCE 0xFB

/* IDs in the attachments master */
#define MATROSKA_ID_ATTACHEDFILE        0x61A7
#define MATROSKA_ID_FILENAME            0x466E
#define MATROSKA_ID_FILEMIMETYPE        0x4660
#define MATROSKA_ID_FILEDATA            0x465C
#define MATROSKA_ID_FILEUID             0x46AE

typedef enum {
  MATROSKA_TRACK_TYPE_VIDEO    = 0x1,
  MATROSKA_TRACK_TYPE_AUDIO    = 0x2,
  MATROSKA_TRACK_TYPE_COMPLEX  = 0x3,
  MATROSKA_TRACK_TYPE_LOGO     = 0x10,
  MATROSKA_TRACK_TYPE_SUBTITLE = 0x11,
  MATROSKA_TRACK_TYPE_CONTROL  = 0x20,
} MatroskaTrackType;

typedef enum {
  MATROSKA_EYE_MODE_MONO  = 0x0,
  MATROSKA_EYE_MODE_RIGHT = 0x1,
  MATROSKA_EYE_MODE_LEFT  = 0x2,
  MATROSKA_EYE_MODE_BOTH  = 0x3,
} MatroskaEyeMode;

typedef enum {
  MATROSKA_ASPECT_RATIO_MODE_FREE  = 0x0,
  MATROSKA_ASPECT_RATIO_MODE_KEEP  = 0x1,
  MATROSKA_ASPECT_RATIO_MODE_FIXED = 0x2,
} MatroskaAspectRatioMode;

/*
 * These aren't in any way "matroska-form" things,
 * it's just something I use in the muxer/demuxer.
 */

typedef enum {
  MATROSKA_TRACK_ENABLED = (1<<0),
  MATROSKA_TRACK_DEFAULT = (1<<1),
  MATROSKA_TRACK_LACING  = (1<<2),
  MATROSKA_TRACK_SHIFT   = (1<<16)
} MatroskaTrackFlags;

typedef enum {
  MATROSKA_VIDEOTRACK_INTERLACED = (MATROSKA_TRACK_SHIFT<<0)
} MatroskaVideoTrackFlags;

/*
 * Matroska Codec IDs. Strings.
 */

typedef struct CodecTags{
    char str[16];
    enum CodecID id;
}CodecTags;

typedef struct CodecMime{
    char str[32];
    enum CodecID id;
}CodecMime;

#define MATROSKA_CODEC_ID_VIDEO_VFW_FOURCC   "V_MS/VFW/FOURCC"
#define MATROSKA_CODEC_ID_AUDIO_ACM          "A_MS/ACM"

/* max. depth in the EBML tree structure */
#define EBML_MAX_DEPTH 16

extern const CodecTags ff_mkv_codec_tags[];
extern const CodecMime ff_mkv_mime_tags[];

#endif /* FFMPEG_MATROSKA_H */
