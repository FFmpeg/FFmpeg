/*
 * Float MPEG Audio decoder
 * Copyright (c) 2010 Michael Niedermayer
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

#include "config.h"
#include "libavutil/samplefmt.h"

#define USE_FLOATS 1

#include "mpegaudio.h"

#define SHR(a,b)       ((a)*(1.0f/(1<<(b))))
#define FIXR_OLD(a)    ((int)((a) * FRAC_ONE + 0.5))
#define FIXR(x)        ((float)(x))
#define FIXHR(x)       ((float)(x))
#define MULH3(x, y, s) ((s)*(y)*(x))
#define MULLx(x, y, s) ((y)*(x))
#define RENAME(a) a ## _float
#define OUT_FMT   AV_SAMPLE_FMT_FLT
#define OUT_FMT_P AV_SAMPLE_FMT_FLTP

#include "mpegaudiodec_template.c"

#if CONFIG_MP1FLOAT_DECODER
AVCodec ff_mp1float_decoder = {
    .name           = "mp1float",
    .long_name      = NULL_IF_CONFIG_SMALL("MP1 (MPEG audio layer 1)"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_MP1,
    .priv_data_size = sizeof(MPADecodeContext),
    .init           = decode_init,
    .close          = decode_close,
    .decode         = decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
    .flush          = flush,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_FLTP,
                                                      AV_SAMPLE_FMT_FLT,
                                                      AV_SAMPLE_FMT_NONE },
};
#endif
#if CONFIG_MP2FLOAT_DECODER
AVCodec ff_mp2float_decoder = {
    .name           = "mp2float",
    .long_name      = NULL_IF_CONFIG_SMALL("MP2 (MPEG audio layer 2)"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_MP2,
    .priv_data_size = sizeof(MPADecodeContext),
    .init           = decode_init,
    .decode         = decode_frame,
    .close          = decode_close,
    .capabilities   = AV_CODEC_CAP_DR1,
    .flush          = flush,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_FLTP,
                                                      AV_SAMPLE_FMT_FLT,
                                                      AV_SAMPLE_FMT_NONE },
};
#endif
#if CONFIG_MP3FLOAT_DECODER
AVCodec ff_mp3float_decoder = {
    .name           = "mp3float",
    .long_name      = NULL_IF_CONFIG_SMALL("MP3 (MPEG audio layer 3)"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_MP3,
    .priv_data_size = sizeof(MPADecodeContext),
    .init           = decode_init,
    .close          = decode_close,
    .decode         = decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
    .flush          = flush,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_FLTP,
                                                      AV_SAMPLE_FMT_FLT,
                                                      AV_SAMPLE_FMT_NONE },
};
#endif
#if CONFIG_MP3ADUFLOAT_DECODER
AVCodec ff_mp3adufloat_decoder = {
    .name           = "mp3adufloat",
    .long_name      = NULL_IF_CONFIG_SMALL("ADU (Application Data Unit) MP3 (MPEG audio layer 3)"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_MP3ADU,
    .priv_data_size = sizeof(MPADecodeContext),
    .init           = decode_init,
    .close          = decode_close,
    .decode         = decode_frame_adu,
    .capabilities   = AV_CODEC_CAP_DR1,
    .flush          = flush,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_FLTP,
                                                      AV_SAMPLE_FMT_FLT,
                                                      AV_SAMPLE_FMT_NONE },
};
#endif
#if CONFIG_MP3ON4FLOAT_DECODER
AVCodec ff_mp3on4float_decoder = {
    .name           = "mp3on4float",
    .long_name      = NULL_IF_CONFIG_SMALL("MP3onMP4"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_MP3ON4,
    .priv_data_size = sizeof(MP3On4DecodeContext),
    .init           = decode_init_mp3on4,
    .close          = decode_close_mp3on4,
    .decode         = decode_frame_mp3on4,
    .capabilities   = AV_CODEC_CAP_DR1,
    .flush          = flush_mp3on4,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_FLTP,
                                                      AV_SAMPLE_FMT_NONE },
};
#endif
