/*
 * Copyright (c) 2011 Miroslav Slugeň <Thunder.m@seznam.cz>
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
#include "rtpdec_formats.h"

#define RTP_G726_HANDLER(bitrate) \
static int g726_ ## bitrate ##_init(AVFormatContext *s, int st_index, PayloadContext *data) \
{ \
    AVStream *stream = s->streams[st_index]; \
    AVCodecContext *codec = stream->codec; \
\
    codec->bits_per_coded_sample = bitrate/8; \
    codec->bit_rate = codec->bits_per_coded_sample * codec->sample_rate; \
\
    return 0; \
} \
\
RTPDynamicProtocolHandler ff_g726_ ## bitrate ## _dynamic_handler = { \
    .enc_name   = "G726-" #bitrate, \
    .codec_type = AVMEDIA_TYPE_AUDIO, \
    .codec_id   = AV_CODEC_ID_ADPCM_G726, \
    .init       = g726_ ## bitrate ## _init, \
}

RTP_G726_HANDLER(16);
RTP_G726_HANDLER(24);
RTP_G726_HANDLER(32);
RTP_G726_HANDLER(40);
