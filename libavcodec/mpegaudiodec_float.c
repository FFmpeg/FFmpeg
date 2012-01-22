/*
 * Float MPEG Audio decoder
 * Copyright (c) 2010 Michael Niedermayer
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

#define CONFIG_FLOAT 1
#include "mpegaudiodec.c"

#if CONFIG_MP1FLOAT_DECODER
AVCodec ff_mp1float_decoder = {
    .name           = "mp1float",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_MP1,
    .priv_data_size = sizeof(MPADecodeContext),
    .init           = decode_init,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .flush          = flush,
    .long_name      = NULL_IF_CONFIG_SMALL("MP1 (MPEG audio layer 1)"),
};
#endif
#if CONFIG_MP2FLOAT_DECODER
AVCodec ff_mp2float_decoder = {
    .name           = "mp2float",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_MP2,
    .priv_data_size = sizeof(MPADecodeContext),
    .init           = decode_init,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .flush          = flush,
    .long_name      = NULL_IF_CONFIG_SMALL("MP2 (MPEG audio layer 2)"),
};
#endif
#if CONFIG_MP3FLOAT_DECODER
AVCodec ff_mp3float_decoder = {
    .name           = "mp3float",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_MP3,
    .priv_data_size = sizeof(MPADecodeContext),
    .init           = decode_init,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .flush          = flush,
    .long_name      = NULL_IF_CONFIG_SMALL("MP3 (MPEG audio layer 3)"),
};
#endif
#if CONFIG_MP3ADUFLOAT_DECODER
AVCodec ff_mp3adufloat_decoder = {
    .name           = "mp3adufloat",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_MP3ADU,
    .priv_data_size = sizeof(MPADecodeContext),
    .init           = decode_init,
    .decode         = decode_frame_adu,
    .capabilities   = CODEC_CAP_DR1,
    .flush          = flush,
    .long_name      = NULL_IF_CONFIG_SMALL("ADU (Application Data Unit) MP3 (MPEG audio layer 3)"),
};
#endif
#if CONFIG_MP3ON4FLOAT_DECODER
AVCodec ff_mp3on4float_decoder = {
    .name           = "mp3on4float",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_MP3ON4,
    .priv_data_size = sizeof(MP3On4DecodeContext),
    .init           = decode_init_mp3on4,
    .close          = decode_close_mp3on4,
    .decode         = decode_frame_mp3on4,
    .capabilities   = CODEC_CAP_DR1,
    .flush          = flush_mp3on4,
    .long_name      = NULL_IF_CONFIG_SMALL("MP3onMP4"),
};
#endif
