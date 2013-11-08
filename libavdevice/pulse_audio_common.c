/*
 * Pulseaudio input
 * Copyright (c) 2011 Luca Barbato <lu_zero@gentoo.org>
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

#include "libavutil/attributes.h"
#include "libavcodec/avcodec.h"
#include "pulse_audio_common.h"

pa_sample_format_t av_cold codec_id_to_pulse_format(int codec_id)
{
    switch (codec_id) {
    case AV_CODEC_ID_PCM_U8:    return PA_SAMPLE_U8;
    case AV_CODEC_ID_PCM_ALAW:  return PA_SAMPLE_ALAW;
    case AV_CODEC_ID_PCM_MULAW: return PA_SAMPLE_ULAW;
    case AV_CODEC_ID_PCM_S16LE: return PA_SAMPLE_S16LE;
    case AV_CODEC_ID_PCM_S16BE: return PA_SAMPLE_S16BE;
    case AV_CODEC_ID_PCM_F32LE: return PA_SAMPLE_FLOAT32LE;
    case AV_CODEC_ID_PCM_F32BE: return PA_SAMPLE_FLOAT32BE;
    case AV_CODEC_ID_PCM_S32LE: return PA_SAMPLE_S32LE;
    case AV_CODEC_ID_PCM_S32BE: return PA_SAMPLE_S32BE;
    case AV_CODEC_ID_PCM_S24LE: return PA_SAMPLE_S24LE;
    case AV_CODEC_ID_PCM_S24BE: return PA_SAMPLE_S24BE;
    default:                    return PA_SAMPLE_INVALID;
    }
}
