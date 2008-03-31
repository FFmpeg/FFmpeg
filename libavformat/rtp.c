/*
 * RTP input/output format
 * Copyright (c) 2002 Fabrice Bellard.
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
#include "bitstream.h"

#include <unistd.h>
#include "network.h"

#include "rtp_internal.h"

//#define DEBUG

/* from http://www.iana.org/assignments/rtp-parameters last updated 05 January 2005 */
/* payload types >= 96 are dynamic;
 * payload types between 72 and 76 are reserved for RTCP conflict avoidance;
 * all the other payload types not present in the table are unassigned or
 * reserved
 */
static const struct
{
    int pt;
    const char enc_name[6];
    enum CodecType codec_type;
    enum CodecID codec_id;
    int clock_rate;
    int audio_channels;
} AVRtpPayloadTypes[]=
{
  {0, "PCMU",        CODEC_TYPE_AUDIO,   CODEC_ID_PCM_MULAW, 8000, 1},
  {3, "GSM",         CODEC_TYPE_AUDIO,   CODEC_ID_NONE, 8000, 1},
  {4, "G723",        CODEC_TYPE_AUDIO,   CODEC_ID_NONE, 8000, 1},
  {5, "DVI4",        CODEC_TYPE_AUDIO,   CODEC_ID_NONE, 8000, 1},
  {6, "DVI4",        CODEC_TYPE_AUDIO,   CODEC_ID_NONE, 16000, 1},
  {7, "LPC",         CODEC_TYPE_AUDIO,   CODEC_ID_NONE, 8000, 1},
  {8, "PCMA",        CODEC_TYPE_AUDIO,   CODEC_ID_PCM_ALAW, 8000, 1},
  {9, "G722",        CODEC_TYPE_AUDIO,   CODEC_ID_NONE, 8000, 1},
  {10, "L16",        CODEC_TYPE_AUDIO,   CODEC_ID_PCM_S16BE, 44100, 2},
  {11, "L16",        CODEC_TYPE_AUDIO,   CODEC_ID_PCM_S16BE, 44100, 1},
  {12, "QCELP",      CODEC_TYPE_AUDIO,   CODEC_ID_QCELP, 8000, 1},
  {13, "CN",         CODEC_TYPE_AUDIO,   CODEC_ID_NONE, 8000, 1},
  {14, "MPA",        CODEC_TYPE_AUDIO,   CODEC_ID_MP2, 90000, -1},
  {14, "MPA",        CODEC_TYPE_AUDIO,   CODEC_ID_MP3, 90000, -1},
  {15, "G728",       CODEC_TYPE_AUDIO,   CODEC_ID_NONE, 8000, 1},
  {16, "DVI4",       CODEC_TYPE_AUDIO,   CODEC_ID_NONE, 11025, 1},
  {17, "DVI4",       CODEC_TYPE_AUDIO,   CODEC_ID_NONE, 22050, 1},
  {18, "G729",       CODEC_TYPE_AUDIO,   CODEC_ID_NONE, 8000, 1},
  {25, "CelB",       CODEC_TYPE_VIDEO,   CODEC_ID_NONE, 90000, -1},
  {26, "JPEG",       CODEC_TYPE_VIDEO,   CODEC_ID_MJPEG, 90000, -1},
  {28, "nv",         CODEC_TYPE_VIDEO,   CODEC_ID_NONE, 90000, -1},
  {31, "H261",       CODEC_TYPE_VIDEO,   CODEC_ID_H261, 90000, -1},
  {32, "MPV",        CODEC_TYPE_VIDEO,   CODEC_ID_MPEG1VIDEO, 90000, -1},
  {32, "MPV",        CODEC_TYPE_VIDEO,   CODEC_ID_MPEG2VIDEO, 90000, -1},
  {33, "MP2T",       CODEC_TYPE_DATA,    CODEC_ID_MPEG2TS, 90000, -1},
  {34, "H263",       CODEC_TYPE_VIDEO,   CODEC_ID_H263, 90000, -1},
  {-1, "",           CODEC_TYPE_UNKNOWN, CODEC_ID_NONE, -1, -1}
};

int rtp_get_codec_info(AVCodecContext *codec, int payload_type)
{
    int i = 0;

    for (i = 0; AVRtpPayloadTypes[i].pt >= 0; i++)
        if (AVRtpPayloadTypes[i].pt == payload_type) {
            if (AVRtpPayloadTypes[i].codec_id != CODEC_ID_NONE) {
                codec->codec_type = AVRtpPayloadTypes[i].codec_type;
                codec->codec_id = AVRtpPayloadTypes[i].codec_id;
                if (AVRtpPayloadTypes[i].audio_channels > 0)
                    codec->channels = AVRtpPayloadTypes[i].audio_channels;
                if (AVRtpPayloadTypes[i].clock_rate > 0)
                    codec->sample_rate = AVRtpPayloadTypes[i].clock_rate;
                return 0;
            }
        }
    return -1;
}

int rtp_get_payload_type(AVCodecContext *codec)
{
    int i, payload_type;

    /* compute the payload type */
    for (payload_type = -1, i = 0; AVRtpPayloadTypes[i].pt >= 0; ++i)
        if (AVRtpPayloadTypes[i].codec_id == codec->codec_id) {
            if (codec->codec_id == CODEC_ID_PCM_S16BE)
                if (codec->channels != AVRtpPayloadTypes[i].audio_channels)
                    continue;
            payload_type = AVRtpPayloadTypes[i].pt;
        }
    return payload_type;
}

const char *ff_rtp_enc_name(int payload_type)
{
    int i;

    for (i = 0; AVRtpPayloadTypes[i].pt >= 0; i++)
        if (AVRtpPayloadTypes[i].pt == payload_type) {
            return AVRtpPayloadTypes[i].enc_name;
        }

    return "";
}

enum CodecID ff_rtp_codec_id(const char *buf, enum CodecType codec_type)
{
    int i;

    for (i = 0; AVRtpPayloadTypes[i].pt >= 0; i++)
        if (!strcmp(buf, AVRtpPayloadTypes[i].enc_name) && (codec_type == AVRtpPayloadTypes[i].codec_type)){
            return AVRtpPayloadTypes[i].codec_id;
        }

    return CODEC_ID_NONE;
}
