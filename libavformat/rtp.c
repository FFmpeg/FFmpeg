/*
 * RTP input/output format
 * Copyright (c) 2002 Fabrice Bellard
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

#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "avformat.h"

#include "rtp.h"

/* from http://www.iana.org/assignments/rtp-parameters last updated 05 January 2005 */
/* payload types >= 96 are dynamic;
 * payload types between 72 and 76 are reserved for RTCP conflict avoidance;
 * all the other payload types not present in the table are unassigned or
 * reserved
 */
static const struct {
    int pt;
    const char enc_name[6];
    enum AVMediaType codec_type;
    enum AVCodecID codec_id;
    int clock_rate;
    int audio_channels;
} rtp_payload_types[] = {
  {0, "PCMU",        AVMEDIA_TYPE_AUDIO,   AV_CODEC_ID_PCM_MULAW, 8000, 1},
  {3, "GSM",         AVMEDIA_TYPE_AUDIO,   AV_CODEC_ID_NONE, 8000, 1},
  {4, "G723",        AVMEDIA_TYPE_AUDIO,   AV_CODEC_ID_G723_1, 8000, 1},
  {5, "DVI4",        AVMEDIA_TYPE_AUDIO,   AV_CODEC_ID_NONE, 8000, 1},
  {6, "DVI4",        AVMEDIA_TYPE_AUDIO,   AV_CODEC_ID_NONE, 16000, 1},
  {7, "LPC",         AVMEDIA_TYPE_AUDIO,   AV_CODEC_ID_NONE, 8000, 1},
  {8, "PCMA",        AVMEDIA_TYPE_AUDIO,   AV_CODEC_ID_PCM_ALAW, 8000, 1},
  {9, "G722",        AVMEDIA_TYPE_AUDIO,   AV_CODEC_ID_ADPCM_G722, 8000, 1},
  {10, "L16",        AVMEDIA_TYPE_AUDIO,   AV_CODEC_ID_PCM_S16BE, 44100, 2},
  {11, "L16",        AVMEDIA_TYPE_AUDIO,   AV_CODEC_ID_PCM_S16BE, 44100, 1},
  {12, "QCELP",      AVMEDIA_TYPE_AUDIO,   AV_CODEC_ID_QCELP, 8000, 1},
  {13, "CN",         AVMEDIA_TYPE_AUDIO,   AV_CODEC_ID_NONE, 8000, 1},
  {14, "MPA",        AVMEDIA_TYPE_AUDIO,   AV_CODEC_ID_MP2, -1, -1},
  {14, "MPA",        AVMEDIA_TYPE_AUDIO,   AV_CODEC_ID_MP3, -1, -1},
  {15, "G728",       AVMEDIA_TYPE_AUDIO,   AV_CODEC_ID_NONE, 8000, 1},
  {16, "DVI4",       AVMEDIA_TYPE_AUDIO,   AV_CODEC_ID_NONE, 11025, 1},
  {17, "DVI4",       AVMEDIA_TYPE_AUDIO,   AV_CODEC_ID_NONE, 22050, 1},
  {18, "G729",       AVMEDIA_TYPE_AUDIO,   AV_CODEC_ID_NONE, 8000, 1},
  {25, "CelB",       AVMEDIA_TYPE_VIDEO,   AV_CODEC_ID_NONE, 90000, -1},
  {26, "JPEG",       AVMEDIA_TYPE_VIDEO,   AV_CODEC_ID_MJPEG, 90000, -1},
  {28, "nv",         AVMEDIA_TYPE_VIDEO,   AV_CODEC_ID_NONE, 90000, -1},
  {31, "H261",       AVMEDIA_TYPE_VIDEO,   AV_CODEC_ID_H261, 90000, -1},
  {32, "MPV",        AVMEDIA_TYPE_VIDEO,   AV_CODEC_ID_MPEG1VIDEO, 90000, -1},
  {32, "MPV",        AVMEDIA_TYPE_VIDEO,   AV_CODEC_ID_MPEG2VIDEO, 90000, -1},
  {33, "MP2T",       AVMEDIA_TYPE_DATA,    AV_CODEC_ID_MPEG2TS, 90000, -1},
  {34, "H263",       AVMEDIA_TYPE_VIDEO,   AV_CODEC_ID_H263, 90000, -1},
  {-1, "",           AVMEDIA_TYPE_UNKNOWN, AV_CODEC_ID_NONE, -1, -1}
};

int ff_rtp_get_codec_info(AVCodecParameters *par, int payload_type)
{
    int i = 0;

    for (i = 0; rtp_payload_types[i].pt >= 0; i++)
        if (rtp_payload_types[i].pt == payload_type) {
            if (rtp_payload_types[i].codec_id != AV_CODEC_ID_NONE) {
                par->codec_type = rtp_payload_types[i].codec_type;
                par->codec_id = rtp_payload_types[i].codec_id;
                if (rtp_payload_types[i].audio_channels > 0)
                    par->channels = rtp_payload_types[i].audio_channels;
                if (rtp_payload_types[i].clock_rate > 0)
                    par->sample_rate = rtp_payload_types[i].clock_rate;
                return 0;
            }
        }
    return -1;
}

int ff_rtp_get_payload_type(AVFormatContext *fmt,
                            AVCodecParameters *par, int idx)
{
    int i;
    const AVOutputFormat *ofmt = fmt ? fmt->oformat : NULL;

    /* Was the payload type already specified for the RTP muxer? */
    if (ofmt && ofmt->priv_class && fmt->priv_data) {
        int64_t payload_type;
        if (av_opt_get_int(fmt->priv_data, "payload_type", 0, &payload_type) >= 0 &&
            payload_type >= 0)
            return (int)payload_type;
    }

    /* static payload type */
    for (i = 0; rtp_payload_types[i].pt >= 0; ++i)
        if (rtp_payload_types[i].codec_id == par->codec_id) {
            if (par->codec_id == AV_CODEC_ID_H263 && (!fmt || !fmt->oformat ||
                !fmt->oformat->priv_class || !fmt->priv_data ||
                !av_opt_flag_is_set(fmt->priv_data, "rtpflags", "rfc2190")))
                continue;
            /* G722 has 8000 as nominal rate even if the sample rate is 16000,
             * see section 4.5.2 in RFC 3551. */
            if (par->codec_id == AV_CODEC_ID_ADPCM_G722 &&
                par->sample_rate == 16000 && par->channels == 1)
                return rtp_payload_types[i].pt;
            if (par->codec_type == AVMEDIA_TYPE_AUDIO &&
                ((rtp_payload_types[i].clock_rate > 0 &&
                  par->sample_rate != rtp_payload_types[i].clock_rate) ||
                 (rtp_payload_types[i].audio_channels > 0 &&
                  par->channels != rtp_payload_types[i].audio_channels)))
                continue;
            return rtp_payload_types[i].pt;
        }

    if (idx < 0)
        idx = par->codec_type == AVMEDIA_TYPE_AUDIO;

    /* dynamic payload type */
    return RTP_PT_PRIVATE + idx;
}

const char *ff_rtp_enc_name(int payload_type)
{
    int i;

    for (i = 0; rtp_payload_types[i].pt >= 0; i++)
        if (rtp_payload_types[i].pt == payload_type)
            return rtp_payload_types[i].enc_name;

    return "";
}

enum AVCodecID ff_rtp_codec_id(const char *buf, enum AVMediaType codec_type)
{
    int i;

    for (i = 0; rtp_payload_types[i].pt >= 0; i++)
        if (!av_strcasecmp(buf, rtp_payload_types[i].enc_name) && (codec_type == rtp_payload_types[i].codec_type))
            return rtp_payload_types[i].codec_id;

    return AV_CODEC_ID_NONE;
}
