/*
 * Copyright (c) 2011 Miroslav Slugeň <Thunder.m@seznam.cz>
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

#include "avformat.h"
#include "rtpdec_formats.h"

static int g726_16_init(AVFormatContext *s, int st_index, PayloadContext *data)
{
    AVStream *stream = s->streams[st_index];
    AVCodecContext *codec = stream->codec;

    codec->bit_rate = 16000;
    if (codec->sample_rate)
        codec->bits_per_coded_sample =
            av_clip((codec->bit_rate + codec->sample_rate/2) / codec->sample_rate, 2, 5);

    return 0;
}

static int g726_24_init(AVFormatContext *s, int st_index, PayloadContext *data)
{
    AVStream *stream = s->streams[st_index];
    AVCodecContext *codec = stream->codec;

    codec->bit_rate = 24000;
    if (codec->sample_rate)
        codec->bits_per_coded_sample =
            av_clip((codec->bit_rate + codec->sample_rate/2) / codec->sample_rate, 2, 5);

    return 0;
}

static int g726_32_init(AVFormatContext *s, int st_index, PayloadContext *data)
{
    AVStream *stream = s->streams[st_index];
    AVCodecContext *codec = stream->codec;

    codec->bit_rate = 32000;
    if (codec->sample_rate)
        codec->bits_per_coded_sample =
            av_clip((codec->bit_rate + codec->sample_rate/2) / codec->sample_rate, 2, 5);

    return 0;
}

static int g726_40_init(AVFormatContext *s, int st_index, PayloadContext *data)
{
    AVStream *stream = s->streams[st_index];
    AVCodecContext *codec = stream->codec;

    codec->bit_rate = 40000;
    if (codec->sample_rate)
        codec->bits_per_coded_sample =
            av_clip((codec->bit_rate + codec->sample_rate/2) / codec->sample_rate, 2, 5);

    return 0;
}

RTPDynamicProtocolHandler ff_g726_16_dynamic_handler = {
    .enc_name   = "G726-16",
    .codec_type = AVMEDIA_TYPE_AUDIO,
    .codec_id   = CODEC_ID_ADPCM_G726,
    .init       = g726_16_init,
};

RTPDynamicProtocolHandler ff_g726_24_dynamic_handler = {
    .enc_name   = "G726-24",
    .codec_type = AVMEDIA_TYPE_AUDIO,
    .codec_id   = CODEC_ID_ADPCM_G726,
    .init       = g726_24_init,
};

RTPDynamicProtocolHandler ff_g726_32_dynamic_handler = {
    .enc_name   = "G726-32",
    .codec_type = AVMEDIA_TYPE_AUDIO,
    .codec_id   = CODEC_ID_ADPCM_G726,
    .init       = g726_32_init,
};

RTPDynamicProtocolHandler ff_g726_40_dynamic_handler = {
    .enc_name   = "G726-40",
    .codec_type = AVMEDIA_TYPE_AUDIO,
    .codec_id   = CODEC_ID_ADPCM_G726,
    .init       = g726_40_init,
};
