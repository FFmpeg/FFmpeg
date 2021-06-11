/*
 * The simplest mpeg audio layer 2 encoder
 * Copyright (c) 2000, 2001 Fabrice Bellard
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

#include "libavutil/channel_layout.h"
#define USE_FLOATS 1
#include "mpegaudioenc_template.c"

const AVCodec ff_mp2_encoder = {
    .name                  = "mp2",
    .long_name             = NULL_IF_CONFIG_SMALL("MP2 (MPEG audio layer 2)"),
    .type                  = AVMEDIA_TYPE_AUDIO,
    .id                    = AV_CODEC_ID_MP2,
    .priv_data_size        = sizeof(MpegAudioContext),
    .init                  = MPA_encode_init,
    .encode2               = MPA_encode_frame,
    .sample_fmts           = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16,
                                                            AV_SAMPLE_FMT_NONE },
    .supported_samplerates = (const int[]){
        44100, 48000,  32000, 22050, 24000, 16000, 0
    },
    .channel_layouts       = (const uint64_t[]){ AV_CH_LAYOUT_MONO,
                                                 AV_CH_LAYOUT_STEREO,
                                                 0 },
    .defaults              = mp2_defaults,
    .caps_internal         = FF_CODEC_CAP_INIT_THREADSAFE,
};
